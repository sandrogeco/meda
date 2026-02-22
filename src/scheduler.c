#include "scheduler.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* ---- timespec helpers ------------------------------------------------ */

static void timespec_add_ns(struct timespec *ts, long long ns)
{
    ts->tv_sec  += ns / 1000000000LL;
    ts->tv_nsec += ns % 1000000000LL;
    if (ts->tv_nsec >= 1000000000L) {
        ts->tv_sec  += 1;
        ts->tv_nsec -= 1000000000L;
    }
}

/* Return <0, 0, >0  like strcmp. */
static int timespec_cmp(const struct timespec *a, const struct timespec *b)
{
    if (a->tv_sec != b->tv_sec)
        return (a->tv_sec < b->tv_sec) ? -1 : 1;
    if (a->tv_nsec != b->tv_nsec)
        return (a->tv_nsec < b->tv_nsec) ? -1 : 1;
    return 0;
}

static long long timespec_to_ns(const struct timespec *ts)
{
    return (long long)ts->tv_sec * 1000000000LL + ts->tv_nsec;
}

/* ---- Modbus → int32 conversion --------------------------------------- */

static int raw_to_int32(const uint16_t *raw, uint16_t reg_count,
                        meda_data_type_t dtype, int32_t *out, int max_out)
{
    int n = 0;
    for (uint16_t i = 0; i < reg_count && n < max_out; i++) {
        switch (dtype) {
        case MEDA_DTYPE_UINT16:
            out[n++] = (int32_t)raw[i];
            break;
        case MEDA_DTYPE_INT16:
            out[n++] = (int32_t)(int16_t)raw[i];
            break;
        case MEDA_DTYPE_UINT32:
            if (i + 1 < reg_count) {
                out[n++] = (int32_t)(((uint32_t)raw[i] << 16) | raw[i + 1]);
                i++;
            }
            break;
        case MEDA_DTYPE_INT32:
            if (i + 1 < reg_count) {
                out[n++] = (int32_t)(((uint32_t)raw[i] << 16) | raw[i + 1]);
                i++;
            }
            break;
        case MEDA_DTYPE_FLOAT32:
            if (i + 1 < reg_count) {
                uint32_t tmp = ((uint32_t)raw[i] << 16) | raw[i + 1];
                float fv;
                memcpy(&fv, &tmp, sizeof(fv));
                out[n++] = (int32_t)fv;
                i++;
            }
            break;
        }
    }
    return n;
}

/* ---- output ---------------------------------------------------------- */

static const char *dtype_name(meda_data_type_t dt)
{
    switch (dt) {
    case MEDA_DTYPE_UINT16:  return "uint16";
    case MEDA_DTYPE_INT16:   return "int16";
    case MEDA_DTYPE_UINT32:  return "uint32";
    case MEDA_DTYPE_INT32:   return "int32";
    case MEDA_DTYPE_FLOAT32: return "float32";
    }
    return "unknown";
}

static void print_values(const meda_task_t *t, const struct timespec *ts_real)
{
    meda_channel_t *ch = t->channel;
    long long ts_ns = timespec_to_ns(ts_real);

    printf("%lld,%d,%s,%s", ts_ns, ch->slave_id, ch->label,
           dtype_name(ch->data_type));

    for (uint16_t i = 0; i < ch->reg_count; i++) {
        switch (ch->data_type) {
        case MEDA_DTYPE_UINT16:
            printf(",%u", t->last_raw[i]);
            break;
        case MEDA_DTYPE_INT16:
            printf(",%d", (int16_t)t->last_raw[i]);
            break;
        case MEDA_DTYPE_UINT32:
            if (i + 1 < ch->reg_count) {
                uint32_t v = ((uint32_t)t->last_raw[i] << 16) |
                             t->last_raw[i + 1];
                printf(",%u", v);
                i++;  /* skip next register, already consumed */
            }
            break;
        case MEDA_DTYPE_INT32:
            if (i + 1 < ch->reg_count) {
                int32_t v = (int32_t)(((uint32_t)t->last_raw[i] << 16) |
                                       t->last_raw[i + 1]);
                printf(",%d", v);
                i++;
            }
            break;
        case MEDA_DTYPE_FLOAT32:
            if (i + 1 < ch->reg_count) {
                uint32_t raw = ((uint32_t)t->last_raw[i] << 16) |
                               t->last_raw[i + 1];
                float fv;
                memcpy(&fv, &raw, sizeof(fv));
                printf(",%.6f", fv);
                i++;
            }
            break;
        }
    }
    printf("\n");
}

/* ---- core polling ---------------------------------------------------- */

static int poll_task(meda_scheduler_t *sched, meda_task_t *t,
                     const struct timespec *now_mono)
{
    meda_channel_t *ch = t->channel;

    /* Check offline skip */
    if (t->consec_err >= MEDA_OFFLINE_THRESH &&
        timespec_cmp(now_mono, &t->skip_until) < 0) {
        return -1;  /* still in skip window */
    }

    /* Flush any stale bytes before starting a new transaction */
    modbus_flush(sched->ctx);

    if (modbus_set_slave(sched->ctx, ch->slave_id) == -1) {
        fprintf(stderr, "modbus_set_slave(%d): %s\n",
                ch->slave_id, modbus_strerror(errno));
        return -1;
    }

    int rc = -1;
    for (int attempt = 0; attempt <= MEDA_RETRY_MAX; attempt++) {
        if (attempt > 0) {
            modbus_flush(sched->ctx);
            fprintf(stderr, "  retry %d/%d slave=%d %s\n",
                    attempt, MEDA_RETRY_MAX, ch->slave_id, ch->label);
        }

        if (ch->func_code == 3)
            rc = modbus_read_registers(sched->ctx, ch->reg_start,
                                       ch->reg_count, t->last_raw);
        else
            rc = modbus_read_input_registers(sched->ctx, ch->reg_start,
                                             ch->reg_count, t->last_raw);

        if (rc == ch->reg_count)
            break;
    }

    if (rc != ch->reg_count) {
        t->consec_err++;
        t->last_ok = 0;
        fprintf(stderr, "read failed slave=%d %s (err %d/%d): %s\n",
                ch->slave_id, ch->label, t->consec_err,
                MEDA_OFFLINE_THRESH, modbus_strerror(errno));

        if (t->consec_err >= MEDA_OFFLINE_THRESH) {
            fprintf(stderr, "  -> device %d offline, skipping %ds\n",
                    ch->slave_id, MEDA_OFFLINE_SKIP_S);
            t->skip_until = *now_mono;
            timespec_add_ns(&t->skip_until,
                            (long long)MEDA_OFFLINE_SKIP_S * 1000000000LL);
        }
        return -1;
    }

    t->consec_err = 0;
    t->last_ok = 1;

    /* Timestamp with realtime clock */
    struct timespec ts_real;
    clock_gettime(CLOCK_REALTIME, &ts_real);
    print_values(t, &ts_real);

    /* Feed miniSEED output */
    if (sched->mseed.channels) {
        int32_t i32buf[MEDA_MAX_REGS];
        int nsamples = raw_to_int32(t->last_raw, ch->reg_count,
                                    ch->data_type, i32buf, MEDA_MAX_REGS);
        if (nsamples > 0) {
            long long ts_ns = timespec_to_ns(&ts_real);
            /* Find config channel index from task pointer offset */
            size_t cfg_idx = (size_t)(t->channel - sched->config->channels);
            meda_mseed_write(&sched->mseed, cfg_idx, i32buf, nsamples, ts_ns);
        }
    }

    return 0;
}

/* ---- public API ------------------------------------------------------ */

int meda_scheduler_init(meda_scheduler_t *sched, meda_config_t *cfg)
{
    memset(sched, 0, sizeof(*sched));
    sched->config = cfg;
    sched->running = 0;

    /* Open Modbus RTU context */
    sched->ctx = modbus_new_rtu(cfg->serial_device, cfg->baud,
                                cfg->parity, cfg->data_bits,
                                cfg->stop_bits
#ifdef RUTOS_SDK
                                , 0  /* flow_ctrl: RutOS extension */
#endif
                                );
    if (!sched->ctx) {
        fprintf(stderr, "modbus_new_rtu(%s): %s\n",
                cfg->serial_device, modbus_strerror(errno));
        return -1;
    }

    /* Response timeout: 500 ms */
    modbus_set_response_timeout(sched->ctx, 0, 500000);

    if (modbus_connect(sched->ctx) == -1) {
        fprintf(stderr, "modbus_connect(%s): %s\n",
                cfg->serial_device, modbus_strerror(errno));
        modbus_free(sched->ctx);
        sched->ctx = NULL;
        return -1;
    }

    /* Allocate task array */
    sched->num_tasks = cfg->num_channels;
    sched->tasks = calloc(sched->num_tasks, sizeof(meda_task_t));
    if (!sched->tasks) {
        modbus_close(sched->ctx);
        modbus_free(sched->ctx);
        sched->ctx = NULL;
        return -1;
    }

    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);

    for (size_t i = 0; i < sched->num_tasks; i++) {
        meda_task_t *t = &sched->tasks[i];
        t->channel   = &cfg->channels[i];
        t->next_poll = now;      /* first poll immediately */
        t->last_ok   = 0;
        t->consec_err = 0;
    }

    /* Initialise miniSEED output (no-op if not configured) */
    if (meda_mseed_init(&sched->mseed, cfg) != 0) {
        fprintf(stderr, "warning: miniSEED init failed, continuing without\n");
    }

    return 0;
}

void meda_scheduler_run(meda_scheduler_t *sched)
{
    sched->running = 1;

    /* Print CSV header */
    printf("timestamp_ns,slave_id,channel,data_type,values...\n");
    fflush(stdout);

    while (sched->running) {
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);

        /* Find and execute all expired tasks (earliest first) */
        int did_work;
        do {
            did_work = 0;
            int best = -1;

            for (size_t i = 0; i < sched->num_tasks; i++) {
                meda_task_t *t = &sched->tasks[i];
                if (timespec_cmp(&t->next_poll, &now) > 0)
                    continue;
                if (best < 0 ||
                    timespec_cmp(&t->next_poll,
                                 &sched->tasks[best].next_poll) < 0)
                    best = (int)i;
            }

            if (best >= 0) {
                meda_task_t *t = &sched->tasks[best];
                poll_task(sched, t, &now);
                fflush(stdout);

                /*
                 * Inter-frame delay: Modbus RTU requires 3.5 char silence
                 * between frames (~4ms at 9600 baud). Also needed on ptys
                 * where there is no real baud-rate timing.
                 */
                usleep(5000);

                /* Schedule next poll from now (not from missed deadline) */
                clock_gettime(CLOCK_MONOTONIC, &t->next_poll);
                long long interval_ns = timespec_to_ns(&t->channel->interval);
                timespec_add_ns(&t->next_poll, interval_ns);

                /* Refresh 'now' after the Modbus transaction */
                clock_gettime(CLOCK_MONOTONIC, &now);
                did_work = 1;
            }
        } while (did_work && sched->running);

        if (!sched->running)
            break;

        /* Find nearest next_poll for sleep target */
        struct timespec next_wakeup = {0};
        int first = 1;
        for (size_t i = 0; i < sched->num_tasks; i++) {
            if (first || timespec_cmp(&sched->tasks[i].next_poll,
                                       &next_wakeup) < 0) {
                next_wakeup = sched->tasks[i].next_poll;
                first = 0;
            }
        }

        /* Sleep until next wakeup (absolute time, no drift) */
        clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next_wakeup, NULL);
    }
}

void meda_scheduler_stop(meda_scheduler_t *sched)
{
    sched->running = 0;
}

void meda_scheduler_destroy(meda_scheduler_t *sched)
{
    meda_mseed_destroy(&sched->mseed);

    if (sched->ctx) {
        modbus_close(sched->ctx);
        modbus_free(sched->ctx);
        sched->ctx = NULL;
    }
    free(sched->tasks);
    sched->tasks = NULL;
    sched->num_tasks = 0;
}
