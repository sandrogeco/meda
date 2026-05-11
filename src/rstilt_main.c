#include "rstilt_config.h"
#include "rstilt_parser.h"
#include "mseed.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

static volatile int g_running = 1;

static void sig_handler(int sig) { (void)sig; g_running = 0; }

static int open_serial(const char *device, int baud, char parity,
                        int data_bits, int stop_bits)
{
    int fd = open(device, O_RDWR | O_NOCTTY);
    if (fd < 0) { perror(device); return -1; }

    struct termios t;
    tcgetattr(fd, &t);
    cfmakeraw(&t);

    speed_t speed;
    switch (baud) {
    case 1200:   speed = B1200;   break;
    case 2400:   speed = B2400;   break;
    case 4800:   speed = B4800;   break;
    case 19200:  speed = B19200;  break;
    case 38400:  speed = B38400;  break;
    case 57600:  speed = B57600;  break;
    case 115200: speed = B115200; break;
    default:     speed = B9600;   break;
    }
    cfsetispeed(&t, speed);
    cfsetospeed(&t, speed);

    t.c_cflag &= ~(CSIZE | PARENB | PARODD | CSTOPB);
    switch (data_bits) {
    case 5: t.c_cflag |= CS5; break;
    case 6: t.c_cflag |= CS6; break;
    case 7: t.c_cflag |= CS7; break;
    default: t.c_cflag |= CS8; break;
    }
    if (stop_bits == 2)    t.c_cflag |= CSTOPB;
    if (parity == 'E')     t.c_cflag |= PARENB;
    else if (parity == 'O') t.c_cflag |= PARENB | PARODD;

    t.c_cflag |= CLOCAL | CREAD;
    t.c_cc[VMIN]  = 1;
    t.c_cc[VTIME] = 0;
    tcsetattr(fd, TCSANOW, &t);
    return fd;
}

/* Format ts as local time string: "YYYY-MM-DDTHH:MM:SS+HH:MM" */
static void fmt_local(const struct timespec *ts, char *buf, size_t n)
{
    struct tm tm;
    localtime_r(&ts->tv_sec, &tm);
    strftime(buf, n, "%Y-%m-%dT%H:%M:%S%z", &tm);
}

/* Format ts as UTC string: "YYYY-MM-DDTHH:MM:SS.sssZ" */
static void fmt_utc(const struct timespec *ts, char *buf, size_t n)
{
    struct tm tm;
    gmtime_r(&ts->tv_sec, &tm);
    char base[32];
    strftime(base, sizeof(base), "%Y-%m-%dT%H:%M:%S", &tm);
    snprintf(buf, n, "%s.%03dZ", base, (int)(ts->tv_nsec / 1000000L));
}

int main(int argc, char *argv[])
{
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <config.json>\n", argv[0]);
        return 1;
    }

    rstilt_config_t cfg;
    if (rstilt_config_load(argv[1], &cfg) != 0) {
        fprintf(stderr, "Failed to load config from %s\n", argv[1]);
        return 1;
    }

    const rstilt_parser_t *parser = rstilt_parser_find(cfg.parser_name);
    if (!parser) {
        fprintf(stderr, "rstilt: unknown parser '%s'\n", cfg.parser_name);
        rstilt_config_free(&cfg);
        return 1;
    }

    fprintf(stderr, "rstilt: parser=%s device=%s baud=%d channels=%zu\n",
            cfg.parser_name, cfg.mcfg.serial_device,
            cfg.mcfg.baud, cfg.mcfg.num_channels);

    /* optional time-diagnostic CSV log */
    FILE *timelog = NULL;
    if (cfg.timelog_path) {
        timelog = fopen(cfg.timelog_path, "a");
        if (!timelog)
            fprintf(stderr, "rstilt: cannot open timelog %s: %s\n",
                    cfg.timelog_path, strerror(errno));
        else
            fprintf(timelog, "rut_local,mseed_utc,LAX\n");
    }

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sig_handler;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT,  &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    sa.sa_handler = SIG_IGN;
    sigaction(SIGPIPE, &sa, NULL);

    meda_mseed_t ms;
    if (meda_mseed_init(&ms, &cfg.mcfg) != 0) {
        fprintf(stderr, "rstilt: miniSEED init failed\n");
        if (timelog) fclose(timelog);
        rstilt_config_free(&cfg);
        return 1;
    }

    int fd = open_serial(cfg.mcfg.serial_device, cfg.mcfg.baud,
                         cfg.mcfg.parity, cfg.mcfg.data_bits,
                         cfg.mcfg.stop_bits);
    if (fd < 0) {
        meda_mseed_destroy(&ms);
        if (timelog) fclose(timelog);
        rstilt_config_free(&cfg);
        return 1;
    }

    fprintf(stderr, "rstilt: running\n");

    char line[256];
    int pos = 0;

    while (g_running) {
        char c;
        int n = read(fd, &c, 1);
        if (n < 0) {
            if (errno == EINTR) continue;
            perror("rstilt: read");
            break;
        }
        if (n == 0)
            continue;

        if (c == '\n') {
            if (pos > 0 && line[pos - 1] == '\r')
                pos--;
            line[pos] = '\0';

            if (pos > 0) {
                int32_t samples[32];
                int nch = parser->parse(line, samples, 32);
                if (nch > 0) {
                    struct timespec ts;
                    clock_gettime(CLOCK_REALTIME, &ts);
                    int64_t ts_ns = (int64_t)ts.tv_sec * 1000000000LL
                                  + ts.tv_nsec;
                    size_t nfeed = ((size_t)nch < cfg.mcfg.num_channels)
                                   ? (size_t)nch : cfg.mcfg.num_channels;
                    for (size_t i = 0; i < nfeed; i++)
                        meda_mseed_write(&ms, i, &samples[i], 1, ts_ns);

                    if (timelog) {
                        char local_buf[40], utc_buf[40];
                        fmt_local(&ts, local_buf, sizeof(local_buf));
                        fmt_utc(&ts, utc_buf, sizeof(utc_buf));
                        /* LAX = channel index 1 (accel_y) */
                        int32_t lax = (nfeed > 1) ? samples[1] : 0;
                        fprintf(timelog, "%s,%s,%d\n",
                                local_buf, utc_buf, lax);
                        fflush(timelog);
                    }
                }
            }
            pos = 0;
        } else if (pos < (int)sizeof(line) - 1) {
            line[pos++] = c;
        } else {
            pos = 0; /* line overflow: discard and resync */
        }
    }

    close(fd);
    meda_mseed_destroy(&ms);
    if (timelog) fclose(timelog);
    rstilt_config_free(&cfg);
    fprintf(stderr, "\nrstilt: shutdown\n");
    return 0;
}
