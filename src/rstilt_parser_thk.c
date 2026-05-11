#include "rstilt_parser.h"

#include <ctype.h>
#include <stdio.h>
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

/* Minimum field lengths — field 0 (voltage) is nominally 13 digits but the
 * sensor does not guarantee zero-padding and may send 12. Other fields are
 * stable. We only check minimums to reject obviously partial frames. */
static const int THK_FIELD_MIN[4] = { 1, 8, 8, 5 };
static const int THK_FIELD_MAX[4] = { 13, 8, 8, 5 };

static int all_digits(const char *s, int len)
{
    for (int i = 0; i < len; i++)
        if (!isdigit((unsigned char)s[i]))
            return 0;
    return 1;
}

static int thk_parse(const char *line, int32_t *samples, int max_ch)
{
    if (max_ch < 4)
        return -1;

    /* minimum plausible line length to reject very short boot garbage */
    if (strlen(line) < 30)
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

    for (int i = 0; i < 4; i++) {
        int len = (int)strlen(fields[i]);
        if (len < THK_FIELD_MIN[i] || len > THK_FIELD_MAX[i] ||
            !all_digits(fields[i], len)) {
            fprintf(stderr, "rstilt/thk: invalid field %d: '%s'\n", i, fields[i]);
            return -1;
        }
    }

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
