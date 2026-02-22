#ifndef MEDA_SCHEDULER_H
#define MEDA_SCHEDULER_H

#include "config.h"
#include "mseed.h"
#include <modbus/modbus.h>

#define MEDA_MAX_REGS       32      /* max registers per single read */
#define MEDA_RETRY_MAX       2
#define MEDA_OFFLINE_THRESH  5      /* consecutive errors before skip */
#define MEDA_OFFLINE_SKIP_S 30      /* seconds to skip offline device */

typedef struct {
    meda_channel_t  *channel;       /* pointer into config array */
    struct timespec   next_poll;    /* absolute monotonic time */
    uint16_t          last_raw[MEDA_MAX_REGS];
    int               last_ok;     /* 1 if last read succeeded */
    int               consec_err;  /* consecutive error count */
    struct timespec   skip_until;  /* monotonic time: skip reads until */
} meda_task_t;

typedef struct {
    meda_config_t   *config;
    modbus_t        *ctx;
    meda_task_t     *tasks;
    size_t           num_tasks;
    volatile int     running;
    meda_mseed_t     mseed;
} meda_scheduler_t;

/* Initialise scheduler: open Modbus port, create task array.
   Returns 0 on success, -1 on error. */
int  meda_scheduler_init(meda_scheduler_t *sched, meda_config_t *cfg);

/* Run the main polling loop (blocks until meda_scheduler_stop). */
void meda_scheduler_run(meda_scheduler_t *sched);

/* Signal the scheduler to stop (safe to call from signal handler). */
void meda_scheduler_stop(meda_scheduler_t *sched);

/* Release resources (close Modbus, free tasks). */
void meda_scheduler_destroy(meda_scheduler_t *sched);

#endif /* MEDA_SCHEDULER_H */
