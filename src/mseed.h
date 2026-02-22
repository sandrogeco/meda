#ifndef MEDA_MSEED_H
#define MEDA_MSEED_H

#include "config.h"
#include <libmseed.h>
#include <libdali.h>

#define MEDA_MSEED_BUFSIZE 20   /* max samples per buffer - TODO: spostare in config.json */

/* Per-channel miniSEED state */
typedef struct {
    char         sid[LM_SIDLEN]; /* "FDSN:XX_MEDA1_00_E_H_Z" */
    MS3Record    msr;            /* template record (stack, not heap) */
    int32_t      samples[MEDA_MSEED_BUFSIZE];
    int          sample_count;
    nstime_t     start_time;     /* time of first sample in buffer */
    double       samprate;       /* Hz */
} meda_mseed_channel_t;

/* Global miniSEED output context */
typedef struct {
    meda_mseed_channel_t *channels;
    size_t                num_channels;
    int                  *chan_map;      /* config channel idx -> mseed channel idx, -1 if none */
    size_t                num_cfg_channels;
    DLCP                 *dlconn;       /* NULL if DataLink disabled */
    char                 *file_dir;     /* NULL if file output disabled */
    int                   reclen;       /* record length (default 512) */
} meda_mseed_t;

/* Initialise miniSEED output from config. Returns 0 on success, -1 on error.
   If no mseed output is configured, succeeds with no-op state. */
int  meda_mseed_init(meda_mseed_t *ms, const meda_config_t *cfg);

/* Accumulate samples for a config channel index. Automatically packs and
   sends/writes records when buffer is full. timestamp_ns is CLOCK_REALTIME
   nanoseconds since epoch. */
void meda_mseed_write(meda_mseed_t *ms, size_t config_chan_idx,
                      const int32_t *samples, int nsamples,
                      int64_t timestamp_ns);

/* Flush partially-filled buffers (call at shutdown). */
void meda_mseed_flush(meda_mseed_t *ms);

/* Disconnect DataLink and free resources. */
void meda_mseed_destroy(meda_mseed_t *ms);

#endif /* MEDA_MSEED_H */
