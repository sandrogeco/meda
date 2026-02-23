/*
 * modsim - Modbus RTU slave simulator on virtual serial port
 *
 * Creates a pty pair:
 *   - master fd: raw Modbus RTU server (no libmodbus, full framing control)
 *   - slave pty: symlinked to /tmp/vserial0 for the client
 *
 * Responds to func 3 (holding) and func 4 (input) register reads.
 * Accepts any slave ID.
 *
 * Usage: ./modsim [pty_link_path]
 */

#define _XOPEN_SOURCE 600
#define _DEFAULT_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <termios.h>
#include <time.h>
#include <stdint.h>
#include <unistd.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define NUM_REGS     256
#define MAX_FRAME    512

static volatile int g_running = 1;
static uint16_t holding_regs[NUM_REGS];
static uint16_t input_regs[NUM_REGS];
static unsigned long tick = 0;

static void sig_handler(int sig)
{
    (void)sig;
    g_running = 0;
}

/* ---- CRC-16 Modbus --------------------------------------------------- */

static uint16_t crc16(const uint8_t *buf, int len)
{
    uint16_t crc = 0xFFFF;
    for (int i = 0; i < len; i++) {
        crc ^= buf[i];
        for (int j = 0; j < 8; j++) {
            if (crc & 1)
                crc = (crc >> 1) ^ 0xA001;
            else
                crc >>= 1;
        }
    }
    return crc;
}

/* ---- Simulated register values --------------------------------------- */

static void update_registers(void)
{
    tick++;
    for (int i = 0; i < NUM_REGS; i++)
        holding_regs[i] = (uint16_t)(tick + i);

    for (int i = 0; i < NUM_REGS; i++) {
        double phase = (double)i * 2.0 * M_PI / 8.0;
        double val = sin(2.0 * M_PI * (double)tick / 100.0 + phase);
        input_regs[i] = (uint16_t)((val + 1.0) * 32767.5);
    }
}

/* ---- PTY setup ------------------------------------------------------- */

static int open_pty_pair(char *slave_path, size_t pathlen)
{
    int master = posix_openpt(O_RDWR | O_NOCTTY);
    if (master < 0) {
        perror("posix_openpt");
        return -1;
    }
    if (grantpt(master) < 0 || unlockpt(master) < 0) {
        perror("grantpt/unlockpt");
        close(master);
        return -1;
    }
    if (ptsname_r(master, slave_path, pathlen) != 0) {
        perror("ptsname_r");
        close(master);
        return -1;
    }

    /* Set raw mode on master so bytes pass through unmodified */
    struct termios t;
    tcgetattr(master, &t);
    cfmakeraw(&t);
    tcsetattr(master, TCSANOW, &t);

    return master;
}

/* ---- Read exactly one Modbus RTU frame ------------------------------- */

/*
 * Modbus RTU frames are delimited by silence (3.5 char times).
 * We simulate this by reading with a short inter-byte timeout.
 * First byte: wait up to 5 seconds (idle wait for request).
 * Subsequent bytes: 50ms timeout (frame boundary detection).
 */
static int read_frame(int fd, uint8_t *buf, int bufsz)
{
    int pos = 0;

    while (pos < bufsz && g_running) {
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(fd, &fds);

        struct timeval tv;
        if (pos == 0) {
            /* Waiting for start of frame */
            tv.tv_sec = 1;
            tv.tv_usec = 0;
        } else {
            /* Inter-byte timeout: frame boundary */
            tv.tv_sec = 0;
            tv.tv_usec = 50000;  /* 50ms */
        }

        int ret = select(fd + 1, &fds, NULL, NULL, &tv);
        if (ret < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (ret == 0) {
            if (pos == 0) continue;  /* still idle, keep waiting */
            break;  /* inter-byte timeout: end of frame */
        }

        int n = read(fd, buf + pos, bufsz - pos);
        if (n <= 0) return -1;
        pos += n;
    }

    return pos;
}

/* ---- Process one Modbus request -------------------------------------- */

static void process_frame(int fd, const uint8_t *req, int req_len)
{
    /* Minimum RTU frame: addr(1) + func(1) + data(2) + crc(2) = 6 */
    if (req_len < 6) {
        fprintf(stderr, "modsim: frame too short (%d bytes)\n", req_len);
        return;
    }

    /* Verify CRC */
    uint16_t rx_crc = (uint16_t)(req[req_len - 1] << 8 | req[req_len - 2]);
    uint16_t calc_crc = crc16(req, req_len - 2);
    if (rx_crc != calc_crc) {
        fprintf(stderr, "modsim: CRC error (got %04X, expected %04X)\n",
                rx_crc, calc_crc);
        return;
    }

    uint8_t slave = req[0];
    uint8_t func  = req[1];

    /* Only handle func 3 (holding) and func 4 (input) */
    if (func != 3 && func != 4) {
        fprintf(stderr, "modsim: slave=%d unsupported func=%d\n", slave, func);
        /* Send exception response: illegal function */
        uint8_t resp[5];
        resp[0] = slave;
        resp[1] = func | 0x80;
        resp[2] = 0x01;  /* illegal function */
        uint16_t c = crc16(resp, 3);
        resp[3] = c & 0xFF;
        resp[4] = (c >> 8) & 0xFF;
        (void)!write(fd, resp, 5);
        return;
    }

    if (req_len < 8) {
        fprintf(stderr, "modsim: frame too short for read request\n");
        return;
    }

    uint16_t reg_start = (uint16_t)(req[2] << 8 | req[3]);
    uint16_t reg_count = (uint16_t)(req[4] << 8 | req[5]);

    /* Validate range */
    if (reg_start + reg_count > NUM_REGS || reg_count == 0 || reg_count > 125) {
        fprintf(stderr, "modsim: slave=%d func=%d reg=%d+%d out of range\n",
                slave, func, reg_start, reg_count);
        uint8_t resp[5];
        resp[0] = slave;
        resp[1] = func | 0x80;
        resp[2] = 0x02;  /* illegal data address */
        uint16_t c = crc16(resp, 3);
        resp[3] = c & 0xFF;
        resp[4] = (c >> 8) & 0xFF;
        (void)!write(fd, resp, 5);
        return;
    }

    /* Update simulated values */
    update_registers();

    uint16_t *src = (func == 3) ? holding_regs : input_regs;

    /* Build response */
    uint8_t resp[MAX_FRAME];
    resp[0] = slave;
    resp[1] = func;
    resp[2] = (uint8_t)(reg_count * 2);
    for (uint16_t i = 0; i < reg_count; i++) {
        resp[3 + i * 2]     = (src[reg_start + i] >> 8) & 0xFF;
        resp[3 + i * 2 + 1] =  src[reg_start + i] & 0xFF;
    }
    int resp_len = 3 + reg_count * 2;
    uint16_t c = crc16(resp, resp_len);
    resp[resp_len]     = c & 0xFF;
    resp[resp_len + 1] = (c >> 8) & 0xFF;
    resp_len += 2;

    /* Small delay before responding (simulate slave processing time) */
    usleep(2000);

    /* Write response as single block */
    (void)!write(fd, resp, resp_len);

    /* Log */
    fprintf(stderr, "modsim: slave=%d func=%d reg=%d+%d -> [",
            slave, func, reg_start, reg_count);
    for (uint16_t i = 0; i < reg_count && i < 4; i++)
        fprintf(stderr, "%s%u", i ? "," : "", src[reg_start + i]);
    if (reg_count > 4)
        fprintf(stderr, ",...");
    fprintf(stderr, "]\n");
}

/* ---- main ------------------------------------------------------------ */

int main(int argc, char *argv[])
{
    const char *link_path = (argc > 1) ? argv[1] : "/tmp/vserial0";

    struct sigaction sa = { .sa_handler = sig_handler };
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT,  &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    char slave_path[256];
    int master_fd = open_pty_pair(slave_path, sizeof(slave_path));
    if (master_fd < 0)
        return 1;

    unlink(link_path);
    if (symlink(slave_path, link_path) < 0) {
        fprintf(stderr, "symlink %s -> %s: %s\n",
                link_path, slave_path, strerror(errno));
        close(master_fd);
        return 1;
    }

    fprintf(stderr, "modsim: pty slave = %s\n", slave_path);
    fprintf(stderr, "modsim: symlink   = %s\n", link_path);
    fprintf(stderr, "modsim: ready, waiting for requests...\n");

    uint8_t frame[MAX_FRAME];

    while (g_running) {
        int len = read_frame(master_fd, frame, sizeof(frame));
        if (len <= 0) {
            usleep(10000);  /* 10ms: evita busy loop se slave non aperto (POLLHUP/EIO) */
            continue;
        }
        process_frame(master_fd, frame, len);
    }

    fprintf(stderr, "\nmodsim: shutting down\n");
    close(master_fd);
    unlink(link_path);
    return 0;
}
