#include "rstilt_parser.h"

#include <stdlib.h>
#include <string.h>

/*
 * THK tiltmeter format: 42-char CSV line terminated by \r\n
 *
 *   ss[0]         ss[1]     ss[2]     ss[3]  ss[4]
 *   0000001908148,14510518,05997123,00787,0932
 *   |-- 13 --|   |-- 8 --||-- 8 --||-- 5 -||4|
 *
 *   samples[0] = ss[0]  -> LQV (voltage)
 *   samples[1] = ss[1]  -> LAX (accel Y)
 *   samples[2] = ss[2]  -> LAY (accel X)
 *   samples[3] = ss[3]  -> LKD (temperature)
 *   ss[4] ignored
 */
static int thk_parse(const char *line, int32_t *samples, int max_ch)
{
    if (max_ch < 4)
        return -1;

    char buf[64];
    strncpy(buf, line, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    char *fields[5];
    int n = 0;
    char *saveptr;
    char *tok = strtok_r(buf, ",", &saveptr);
    while (tok && n < 5) {
        fields[n++] = tok;
        tok = strtok_r(NULL, ",", &saveptr);
    }

    if (n < 4)
        return -1;

    samples[0] = (int32_t)atol(fields[0]);
    samples[1] = (int32_t)atol(fields[1]);
    samples[2] = (int32_t)atol(fields[2]);
    samples[3] = (int32_t)atol(fields[3]);

    return 4;
}

const rstilt_parser_t rstilt_parser_thk = {
    .name  = "thk",
    .parse = thk_parse,
};
