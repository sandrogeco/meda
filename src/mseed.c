#include "mseed.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/* ---- pack callback context ------------------------------------------- */

typedef struct {
    DLCP  *dlconn;
    char  *file_dir;
    char  *sid;       /* for building file name */
} pack_ctx_t;

/* Callback invoked by msr3_pack() for each completed record */
static void record_handler(char *record, int reclen, void *handlerdata)
{
    pack_ctx_t *ctx = (pack_ctx_t *)handlerdata;

    /* Send via DataLink */
    if (ctx->dlconn) {
        /* Parse record to get time range for dl_write */
        MS3Record *tmp = NULL;
        if (msr3_parse(record, reclen, &tmp, 0, 0) == MS_NOERROR && tmp) {
            nstime_t endtime = tmp->starttime +
                (nstime_t)((tmp->samplecnt - 1) * (NSTMODULUS / tmp->samprate));
            dltime_t dl_start = tmp->starttime / 1000;  /* ns -> us */
            dltime_t dl_end   = endtime / 1000;

            /* Build DataLink stream ID keeping full SID (incl. "FDSN:" prefix).
             * Ringserver matches SeedLink clients against the full stream ID,
             * so the prefix must be present in the ring. */
            char dl_streamid[128];
            snprintf(dl_streamid, sizeof(dl_streamid), "%s/MSEED", tmp->sid);

            int64_t rv = dl_write(ctx->dlconn, record, reclen,
                                  dl_streamid, dl_start, dl_end, 0);
            if (rv < 0) {
                fprintf(stderr, "mseed: dl_write failed, reconnecting...\n");
                dl_disconnect(ctx->dlconn);
                if (dl_connect(ctx->dlconn) != -1) {
                    fprintf(stderr, "mseed: reconnected to DataLink\n");
                    rv = dl_write(ctx->dlconn, record, reclen,
                                  dl_streamid, dl_start, dl_end, 0);
                    if (rv < 0)
                        fprintf(stderr, "mseed: dl_write still failed after reconnect\n");
                } else {
                    fprintf(stderr, "mseed: reconnect failed, will retry next record\n");
                }
            }
            msr3_free(&tmp);
        }
    }

    /* Write to file */
    if (ctx->file_dir && ctx->sid) {
        /* Build filename from SID: "FDSN:XX_STA_LOC_B_S_S" -> "XX.STA.LOC.BSS.mseed" */
        const char *sid_body = ctx->sid;
        if (strncmp(sid_body, "FDSN:", 5) == 0)
            sid_body += 5;

        /* Replace underscores with dots for filename */
        char fname[256];
        snprintf(fname, sizeof(fname), "%s/", ctx->file_dir);
        size_t off = strlen(fname);
        for (const char *p = sid_body; *p && off < sizeof(fname) - 7; p++) {
            fname[off++] = (*p == '_') ? '.' : *p;
        }
        fname[off] = '\0';
        strncat(fname, ".mseed", sizeof(fname) - strlen(fname) - 1);

        FILE *f = fopen(fname, "ab");
        if (f) {
            fwrite(record, 1, reclen, f);
            fclose(f);
        } else {
            fprintf(stderr, "mseed: cannot open %s: %s\n", fname, strerror(errno));
        }
    }
}

/* ---- helpers --------------------------------------------------------- */

/* Build FDSN source identifier: "FDSN:NET_STA_LOC_B_S_S" */
static void build_sid(char *sid, size_t sidlen,
                      const char *net, const char *sta,
                      const char *loc, const char *chan)
{
    /* chan is 3 chars like "EHZ", FDSN SID splits as band_source_subsource */
    char band[2] = {chan[0], '\0'};
    char source[2] = {chan[1], '\0'};
    char sub[2] = {chan[2], '\0'};
    snprintf(sid, sidlen, "FDSN:%s_%s_%s_%s_%s_%s",
             net, sta, loc, band, source, sub);
}

/* Pack and send/write the buffer of a channel.
   pack_flags: 0 for normal packing, MSF_FLUSHDATA to force partial records. */
static void flush_channel(meda_mseed_t *ms, meda_mseed_channel_t *ch,
                          uint32_t pack_flags)
{
    if (ch->sample_count <= 0)
        return;

    ch->msr.starttime   = ch->start_time;
    ch->msr.samprate    = ch->samprate;
    ch->msr.datasamples = ch->samples;
    ch->msr.numsamples  = ch->sample_count;
    ch->msr.sampletype  = 'i';
    ch->msr.reclen      = ms->reclen;

    pack_ctx_t ctx = {
        .dlconn   = ms->dlconn,
        .file_dir = ms->file_dir,
        .sid      = ch->msr.sid,
    };

    int64_t packed = 0;
    int rv = msr3_pack(&ch->msr, record_handler, &ctx, &packed, pack_flags, 0);
    if (rv < 0) {
        fprintf(stderr, "mseed: msr3_pack failed for %s: %d\n", ch->sid, rv);
    }

    /* If all samples were packed, reset buffer */
    if (packed >= ch->sample_count) {
        ch->msr.datasamples = NULL;
        ch->msr.numsamples  = 0;
        ch->sample_count    = 0;
    } else if (packed > 0) {
        /* Shift unpacked samples to start of buffer */
        int remaining = ch->sample_count - (int)packed;
        memmove(ch->samples, ch->samples + packed,
                remaining * sizeof(int32_t));
        ch->sample_count = remaining;
        /* Advance start_time for remaining samples */
        ch->start_time += (nstime_t)(packed * (NSTMODULUS / ch->samprate));
        ch->msr.datasamples = NULL;
        ch->msr.numsamples  = 0;
    } else {
        /* Pack produced nothing (e.g., not enough samples for a full record
         * without MSF_FLUSHDATA, or encoding error). Reset to avoid overflow. */
        ch->msr.datasamples = NULL;
        ch->msr.numsamples  = 0;
        ch->sample_count    = 0;
    }
}

/* ---- public API ------------------------------------------------------ */

int meda_mseed_init(meda_mseed_t *ms, const meda_config_t *cfg)
{
    memset(ms, 0, sizeof(*ms));

    /* Check if any mseed output is configured */
    int has_mseed = (cfg->datalink_host || cfg->mseed_file_dir);
    if (!has_mseed)
        return 0;  /* no-op, nothing to do */

    /* Count channels that have mseed_channel set */
    size_t count = 0;
    for (size_t i = 0; i < cfg->num_channels; i++) {
        if (cfg->channels[i].mseed_channel)
            count++;
    }

    if (count == 0)
        return 0;  /* no channels produce miniSEED */

    /* Build channel map: config index -> mseed index */
    ms->num_cfg_channels = cfg->num_channels;
    ms->chan_map = calloc(cfg->num_channels, sizeof(int));
    if (!ms->chan_map)
        return -1;
    for (size_t i = 0; i < cfg->num_channels; i++)
        ms->chan_map[i] = -1;

    ms->channels = calloc(count, sizeof(meda_mseed_channel_t));
    if (!ms->channels) {
        free(ms->chan_map);
        ms->chan_map = NULL;
        return -1;
    }
    ms->num_channels = count;
    ms->reclen    = cfg->mseed_reclen   > 0 ? cfg->mseed_reclen   : 512;
    ms->encoding  = cfg->mseed_encoding > 0 ? cfg->mseed_encoding : 11; /* DE_STEIM2 */

    const char *net = cfg->mseed_network  ? cfg->mseed_network  : "XX";
    const char *sta = cfg->mseed_station  ? cfg->mseed_station  : "MEDA1";
    const char *loc = cfg->mseed_location ? cfg->mseed_location : "00";

    size_t midx = 0;
    for (size_t i = 0; i < cfg->num_channels; i++) {
        if (!cfg->channels[i].mseed_channel)
            continue;

        meda_mseed_channel_t *ch = &ms->channels[midx];
        ms->chan_map[i] = (int)midx;

        build_sid(ch->sid, sizeof(ch->sid), net, sta, loc,
                  cfg->channels[i].mseed_channel);

        /* Init MS3Record template */
        ch->msr = (MS3Record)MS3Record_INITIALIZER;
        memcpy(ch->msr.sid, ch->sid, sizeof(ch->msr.sid));
        ch->msr.formatversion = (cfg->mseed_format == 2) ? 2 : 3;
        ch->msr.encoding = ms->encoding;
        ch->msr.pubversion = 1;
        ch->msr.reclen = ms->reclen;

        /* Sample rate from interval */
        long long interval_ns = (long long)cfg->channels[i].interval.tv_sec * 1000000000LL
                                + cfg->channels[i].interval.tv_nsec;
        if (interval_ns > 0)
            ch->samprate = 1e9 / (double)interval_ns;
        else
            ch->samprate = 1.0;

        ch->sample_count = 0;
        ch->start_time = NSTUNSET;

        midx++;
    }

    /* Connect DataLink if configured */
    if (cfg->datalink_host) {
        ms->dlconn = dl_newdlcp(cfg->datalink_host, "meda");
        if (!ms->dlconn) {
            fprintf(stderr, "mseed: dl_newdlcp(%s) failed\n", cfg->datalink_host);
            /* non-fatal: continue with file output only */
        } else {
            if (dl_connect(ms->dlconn) == -1) {
                fprintf(stderr, "mseed: dl_connect(%s) failed, will retry on first record\n",
                        cfg->datalink_host);
                /* Keep dlconn handle: record_handler will retry on dl_write failure */
            } else {
                fprintf(stderr, "mseed: connected to DataLink %s\n", cfg->datalink_host);
            }
        }
    }

    /* File output directory */
    if (cfg->mseed_file_dir) {
        ms->file_dir = strdup(cfg->mseed_file_dir);
        /* Create directory if it doesn't exist */
        mkdir(ms->file_dir, 0755);
    }

    return 0;
}

void meda_mseed_write(meda_mseed_t *ms, size_t config_chan_idx,
                      const int32_t *samples, int nsamples,
                      int64_t timestamp_ns)
{
    if (!ms->channels || !ms->chan_map)
        return;
    if (config_chan_idx >= ms->num_cfg_channels)
        return;

    int midx = ms->chan_map[config_chan_idx];
    if (midx < 0)
        return;

    meda_mseed_channel_t *ch = &ms->channels[midx];

    for (int i = 0; i < nsamples; i++) {
        if (ch->sample_count == 0) {
            /* First sample: set start time, advance by i samples */
            ch->start_time = (nstime_t)timestamp_ns;
            if (ch->samprate > 0 && i > 0)
                ch->start_time += (nstime_t)(i * (NSTMODULUS / ch->samprate));
        }

        ch->samples[ch->sample_count++] = samples[i];

        if (ch->sample_count >= MEDA_MSEED_BUFSIZE) {
            flush_channel(ms, ch, MSF_FLUSHDATA);
        }
    }
}

void meda_mseed_flush(meda_mseed_t *ms)
{
    if (!ms->channels)
        return;

    for (size_t i = 0; i < ms->num_channels; i++) {
        flush_channel(ms, &ms->channels[i], MSF_FLUSHDATA);
    }
}

void meda_mseed_destroy(meda_mseed_t *ms)
{
    meda_mseed_flush(ms);

    if (ms->dlconn) {
        dl_disconnect(ms->dlconn);
        dl_freedlcp(ms->dlconn);
        ms->dlconn = NULL;
    }

    free(ms->file_dir);
    ms->file_dir = NULL;

    free(ms->channels);
    ms->channels = NULL;
    ms->num_channels = 0;

    free(ms->chan_map);
    ms->chan_map = NULL;
}
