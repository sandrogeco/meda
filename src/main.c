#include "config.h"
#include "scheduler.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static meda_scheduler_t g_sched;

static void signal_handler(int sig)
{
    (void)sig;
    meda_scheduler_stop(&g_sched);
}

static void usage(const char *prog)
{
    fprintf(stderr, "Usage: %s <config.json>\n", prog);
    exit(1);
}

int main(int argc, char *argv[])
{
    if (argc < 2)
        usage(argv[0]);

    const char *config_path = argv[1];

    /* Load configuration */
    meda_config_t cfg;
    if (meda_config_load(config_path, &cfg) != 0) {
        fprintf(stderr, "Failed to load config from %s\n", config_path);
        return 1;
    }

    fprintf(stderr, "meda: loaded %zu channels from %s\n",
            cfg.num_channels, config_path);
    fprintf(stderr, "meda: serial %s @ %d baud (%c,%d,%d)\n",
            cfg.serial_device, cfg.baud, cfg.parity,
            cfg.data_bits, cfg.stop_bits);

    for (size_t i = 0; i < cfg.num_channels; i++) {
        meda_channel_t *ch = &cfg.channels[i];
        long long interval_ms = (long long)ch->interval.tv_sec * 1000 +
                                ch->interval.tv_nsec / 1000000;
        fprintf(stderr, "  [%zu] slave=%d func=%d reg=%d+%d %s every %lldms\n",
                i, ch->slave_id, ch->func_code,
                ch->reg_start, ch->reg_count, ch->label, interval_ms);
    }

    /* Install signal handlers */
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    /* Init and run scheduler */
    if (meda_scheduler_init(&g_sched, &cfg) != 0) {
        fprintf(stderr, "Failed to initialise scheduler\n");
        meda_config_free(&cfg);
        return 1;
    }

    fprintf(stderr, "meda: running (Ctrl-C to stop)\n");
    meda_scheduler_run(&g_sched);
    fprintf(stderr, "\nmeda: shutting down\n");

    meda_scheduler_destroy(&g_sched);
    meda_config_free(&cfg);
    return 0;
}
