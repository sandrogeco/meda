#ifndef MEDA_CONFIG_H
#define MEDA_CONFIG_H

#include <stdint.h>
#include <stddef.h>
#include <time.h>

/* Data types for register interpretation */
typedef enum {
    MEDA_DTYPE_UINT16,
    MEDA_DTYPE_INT16,
    MEDA_DTYPE_UINT32,
    MEDA_DTYPE_INT32,
    MEDA_DTYPE_FLOAT32,
} meda_data_type_t;

/* A single acquisition channel (one Modbus read operation) */
typedef struct {
    char            *label;
    uint8_t          slave_id;
    uint16_t         reg_start;
    uint16_t         reg_count;
    uint8_t          func_code;     /* 3 = holding, 4 = input */
    meda_data_type_t data_type;
    struct timespec  interval;      /* polling interval (converted from ms) */
    char            *mseed_channel; /* SEED channel code e.g. "EHZ", NULL = no miniSEED */
} meda_channel_t;

/* Top-level configuration */
typedef struct {
    char            *serial_device;
    int              baud;
    char             parity;        /* 'N', 'E', 'O' */
    int              data_bits;
    int              stop_bits;
    meda_channel_t  *channels;
    size_t           num_channels;
    /* miniSEED output */
    char            *mseed_network;  /* "XX" default */
    char            *mseed_station;  /* "MEDA1" default */
    char            *mseed_location; /* "00" default */
    char            *datalink_host;  /* "localhost:16000", NULL = disabled */
    char            *mseed_file_dir; /* output directory, NULL = disabled */
    int              mseed_format;   /* 2 or 3, default 3 */
    int              mseed_reclen;   /* record length, default 512 */
    int              mseed_encoding; /* DE_STEIM2=11 default; DE_STEIM1=10, DE_INT32=3, DE_INT16=1, DE_FLOAT32=4 */
} meda_config_t;

/* Load configuration from JSON file. Returns 0 on success, -1 on error. */
int  meda_config_load(const char *path, meda_config_t *cfg);

/* Free all memory owned by cfg. */
void meda_config_free(meda_config_t *cfg);

#endif /* MEDA_CONFIG_H */
