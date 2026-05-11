#include "rstilt_config.h"

#include <cjson/cJSON.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *jstr(cJSON *obj, const char *key, const char *def)
{
    cJSON *v = cJSON_GetObjectItem(obj, key);
    const char *s = (v && cJSON_IsString(v)) ? v->valuestring : def;
    return s ? strdup(s) : NULL;
}

static int jint(cJSON *obj, const char *key, int def)
{
    cJSON *v = cJSON_GetObjectItem(obj, key);
    return (v && cJSON_IsNumber(v)) ? (int)v->valuedouble : def;
}

static int parse_encoding(cJSON *ms)
{
    cJSON *enc = cJSON_GetObjectItem(ms, "encoding");
    if (!enc || !cJSON_IsString(enc))
        return 11; /* DE_STEIM2 */
    const char *e = enc->valuestring;
    if (strcmp(e, "steim2") == 0) return 11;
    if (strcmp(e, "steim1") == 0) return 10;
    if (strcmp(e, "int32")  == 0) return 3;
    if (strcmp(e, "int16")  == 0) return 1;
    return 11;
}

int rstilt_config_load(const char *path, rstilt_config_t *cfg)
{
    memset(cfg, 0, sizeof(*cfg));

    FILE *f = fopen(path, "r");
    if (!f) { perror(path); return -1; }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    rewind(f);
    char *buf = malloc(sz + 1);
    if (!buf) { fclose(f); return -1; }
    if ((long)fread(buf, 1, sz, f) != sz) {
        fprintf(stderr, "rstilt_config: short read on %s\n", path);
        free(buf);
        return -1;
    }
    buf[sz] = '\0';
    fclose(f);

    cJSON *root = cJSON_Parse(buf);
    free(buf);
    if (!root) {
        fprintf(stderr, "rstilt_config: JSON parse error in %s\n", path);
        return -1;
    }

    meda_config_t *mc = &cfg->mcfg;

    /* serial */
    cJSON *serial = cJSON_GetObjectItem(root, "serial");
    if (serial) {
        mc->serial_device = jstr(serial, "device", "/dev/ttyCH343USB0");
        mc->baud      = jint(serial, "baud", 9600);
        mc->data_bits = jint(serial, "data_bits", 8);
        mc->stop_bits = jint(serial, "stop_bits", 1);
        char *par = jstr(serial, "parity", "N");
        mc->parity = par ? par[0] : 'N';
        free(par);
    } else {
        mc->serial_device = strdup("/dev/ttyCH343USB0");
        mc->baud = 9600; mc->parity = 'N';
        mc->data_bits = 8; mc->stop_bits = 1;
    }

    /* parser */
    cJSON *pj = cJSON_GetObjectItem(root, "parser");
    cfg->parser_name = (pj && cJSON_IsString(pj))
                       ? strdup(pj->valuestring) : strdup("thk");

    /* timelog (optional) */
    cfg->timelog_path = jstr(root, "timelog", NULL);

    /* mseed */
    cJSON *ms = cJSON_GetObjectItem(root, "mseed");
    if (ms) {
        mc->mseed_network  = jstr(ms, "network",  "XX");
        mc->mseed_station  = jstr(ms, "station",  "RSTILT");
        mc->mseed_location = jstr(ms, "location", "00");
        mc->datalink_host  = jstr(ms, "datalink", NULL);
        mc->mseed_file_dir = jstr(ms, "file_dir", NULL);
        mc->mseed_format   = jint(ms, "format",   2);
        mc->mseed_reclen   = jint(ms, "reclen",   512);
        mc->mseed_encoding = parse_encoding(ms);
    } else {
        mc->mseed_network  = strdup("XX");
        mc->mseed_station  = strdup("RSTILT");
        mc->mseed_location = strdup("00");
        mc->mseed_encoding = 11;
        mc->mseed_reclen   = 512;
        mc->mseed_format   = 2;
    }

    /* channels — order must match parser output */
    cJSON *channels = cJSON_GetObjectItem(root, "channels");
    if (!channels || !cJSON_IsArray(channels)) {
        fprintf(stderr, "rstilt_config: missing 'channels' array\n");
        cJSON_Delete(root);
        return -1;
    }
    int nch = cJSON_GetArraySize(channels);
    if (nch <= 0) {
        fprintf(stderr, "rstilt_config: empty channels array\n");
        cJSON_Delete(root);
        return -1;
    }

    mc->channels = calloc(nch, sizeof(meda_channel_t));
    if (!mc->channels) { cJSON_Delete(root); return -1; }
    mc->num_channels = (size_t)nch;

    for (int i = 0; i < nch; i++) {
        cJSON *ch = cJSON_GetArrayItem(channels, i);
        meda_channel_t *c = &mc->channels[i];

        c->label         = jstr(ch, "label", "ch");
        c->mseed_channel = jstr(ch, "mseed_channel", NULL);

        double sr = 1.0;
        cJSON *srj = cJSON_GetObjectItem(ch, "sample_rate");
        if (srj && cJSON_IsNumber(srj) && srj->valuedouble > 0)
            sr = srj->valuedouble;

        long long interval_ns = (long long)(1e9 / sr);
        c->interval.tv_sec  = interval_ns / 1000000000LL;
        c->interval.tv_nsec = interval_ns % 1000000000LL;
    }

    cJSON_Delete(root);
    return 0;
}

void rstilt_config_free(rstilt_config_t *cfg)
{
    free(cfg->parser_name);
    cfg->parser_name = NULL;
    free(cfg->timelog_path);
    cfg->timelog_path = NULL;
    meda_config_free(&cfg->mcfg);
}
