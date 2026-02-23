#include "config.h"

#include <cjson/cJSON.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------- helpers --------------------------------------------------- */

static char *read_file(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) {
        fprintf(stderr, "config: cannot open %s: %m\n", path);
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    rewind(f);

    char *buf = malloc(len + 1);
    if (!buf) {
        fclose(f);
        return NULL;
    }
    if ((long)fread(buf, 1, len, f) != len) {
        fprintf(stderr, "config: short read on %s\n", path);
        free(buf);
        fclose(f);
        return NULL;
    }
    buf[len] = '\0';
    fclose(f);
    return buf;
}

static meda_data_type_t parse_data_type(const char *s)
{
    if (!s)                         return MEDA_DTYPE_UINT16;
    if (strcmp(s, "int16")   == 0)  return MEDA_DTYPE_INT16;
    if (strcmp(s, "uint32")  == 0)  return MEDA_DTYPE_UINT32;
    if (strcmp(s, "int32")   == 0)  return MEDA_DTYPE_INT32;
    if (strcmp(s, "float32") == 0)  return MEDA_DTYPE_FLOAT32;
    return MEDA_DTYPE_UINT16;
}

static void ms_to_timespec(int ms, struct timespec *ts)
{
    ts->tv_sec  = ms / 1000;
    ts->tv_nsec = (ms % 1000) * 1000000L;
}

/* ---------- public API ------------------------------------------------ */

int meda_config_load(const char *path, meda_config_t *cfg)
{
    memset(cfg, 0, sizeof(*cfg));

    char *text = read_file(path);
    if (!text)
        return -1;

    cJSON *root = cJSON_Parse(text);
    free(text);
    if (!root) {
        fprintf(stderr, "config: JSON parse error near: %s\n",
                cJSON_GetErrorPtr());
        return -1;
    }

    /* --- serial section --- */
    cJSON *serial = cJSON_GetObjectItem(root, "serial");
    if (!serial) {
        fprintf(stderr, "config: missing \"serial\" section\n");
        cJSON_Delete(root);
        return -1;
    }

    cJSON *j;
    j = cJSON_GetObjectItem(serial, "device");
    cfg->serial_device = strdup(j ? j->valuestring : "/dev/ttyUSB0");

    j = cJSON_GetObjectItem(serial, "baud");
    cfg->baud = j ? j->valueint : 9600;

    j = cJSON_GetObjectItem(serial, "parity");
    cfg->parity = (j && j->valuestring[0]) ? j->valuestring[0] : 'N';

    j = cJSON_GetObjectItem(serial, "data_bits");
    cfg->data_bits = j ? j->valueint : 8;

    j = cJSON_GetObjectItem(serial, "stop_bits");
    cfg->stop_bits = j ? j->valueint : 1;

    /* --- mseed section (optional) --- */
    cJSON *mseed = cJSON_GetObjectItem(root, "mseed");
    if (mseed) {
        j = cJSON_GetObjectItem(mseed, "network");
        if (j && j->valuestring) cfg->mseed_network = strdup(j->valuestring);

        j = cJSON_GetObjectItem(mseed, "station");
        if (j && j->valuestring) cfg->mseed_station = strdup(j->valuestring);

        j = cJSON_GetObjectItem(mseed, "location");
        if (j && j->valuestring) cfg->mseed_location = strdup(j->valuestring);

        j = cJSON_GetObjectItem(mseed, "datalink");
        if (j && j->valuestring) cfg->datalink_host = strdup(j->valuestring);

        j = cJSON_GetObjectItem(mseed, "file_dir");
        if (j && j->valuestring) cfg->mseed_file_dir = strdup(j->valuestring);

        j = cJSON_GetObjectItem(mseed, "format");
        cfg->mseed_format = j ? j->valueint : 3;

        j = cJSON_GetObjectItem(mseed, "reclen");
        cfg->mseed_reclen = j ? j->valueint : 512;

        j = cJSON_GetObjectItem(mseed, "encoding");
        if (j && j->valuestring) {
            const char *enc = j->valuestring;
            if      (strcmp(enc, "steim2")  == 0) cfg->mseed_encoding = 11;
            else if (strcmp(enc, "steim1")  == 0) cfg->mseed_encoding = 10;
            else if (strcmp(enc, "int32")   == 0) cfg->mseed_encoding = 3;
            else if (strcmp(enc, "int16")   == 0) cfg->mseed_encoding = 1;
            else if (strcmp(enc, "float32") == 0) cfg->mseed_encoding = 4;
            else if (strcmp(enc, "float64") == 0) cfg->mseed_encoding = 5;
            else {
                fprintf(stderr, "config: unknown mseed encoding \"%s\", using steim2\n", enc);
                cfg->mseed_encoding = 11;
            }
        } else {
            cfg->mseed_encoding = 11; /* DE_STEIM2 default */
        }
    }

    /* --- devices / channels --- */
    cJSON *devices = cJSON_GetObjectItem(root, "devices");
    if (!devices || !cJSON_IsArray(devices)) {
        fprintf(stderr, "config: missing \"devices\" array\n");
        cJSON_Delete(root);
        meda_config_free(cfg);
        return -1;
    }

    /* first pass: count total channels */
    size_t total = 0;
    cJSON *dev;
    cJSON_ArrayForEach(dev, devices) {
        cJSON *chans = cJSON_GetObjectItem(dev, "channels");
        if (chans)
            total += cJSON_GetArraySize(chans);
    }

    cfg->channels = calloc(total, sizeof(meda_channel_t));
    if (!cfg->channels) {
        cJSON_Delete(root);
        meda_config_free(cfg);
        return -1;
    }

    /* second pass: populate */
    size_t idx = 0;
    cJSON_ArrayForEach(dev, devices) {
        cJSON *sid = cJSON_GetObjectItem(dev, "slave_id");
        uint8_t slave_id = sid ? (uint8_t)sid->valueint : 1;

        cJSON *chans = cJSON_GetObjectItem(dev, "channels");
        if (!chans)
            continue;

        cJSON *ch;
        cJSON_ArrayForEach(ch, chans) {
            meda_channel_t *c = &cfg->channels[idx++];

            j = cJSON_GetObjectItem(ch, "label");
            c->label = strdup(j ? j->valuestring : "unknown");

            c->slave_id = slave_id;

            j = cJSON_GetObjectItem(ch, "reg_start");
            c->reg_start = j ? (uint16_t)j->valueint : 0;

            j = cJSON_GetObjectItem(ch, "reg_count");
            c->reg_count = j ? (uint16_t)j->valueint : 1;

            j = cJSON_GetObjectItem(ch, "func");
            c->func_code = j ? (uint8_t)j->valueint : 4;

            j = cJSON_GetObjectItem(ch, "interval_ms");
            int ms = j ? j->valueint : 1000;
            ms_to_timespec(ms, &c->interval);

            j = cJSON_GetObjectItem(ch, "data_type");
            c->data_type = parse_data_type(j ? j->valuestring : NULL);

            j = cJSON_GetObjectItem(ch, "mseed_channel");
            if (j && j->valuestring)
                c->mseed_channel = strdup(j->valuestring);
        }
    }
    cfg->num_channels = idx;

    cJSON_Delete(root);
    return 0;
}

void meda_config_free(meda_config_t *cfg)
{
    if (!cfg)
        return;
    free(cfg->serial_device);
    cfg->serial_device = NULL;
    for (size_t i = 0; i < cfg->num_channels; i++) {
        free(cfg->channels[i].label);
        free(cfg->channels[i].mseed_channel);
    }
    free(cfg->channels);
    cfg->channels = NULL;
    cfg->num_channels = 0;
    free(cfg->mseed_network);
    free(cfg->mseed_station);
    free(cfg->mseed_location);
    free(cfg->datalink_host);
    free(cfg->mseed_file_dir);
    cfg->mseed_network = NULL;
    cfg->mseed_station = NULL;
    cfg->mseed_location = NULL;
    cfg->datalink_host = NULL;
    cfg->mseed_file_dir = NULL;
}
