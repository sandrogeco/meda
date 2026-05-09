#ifndef RSTILT_CONFIG_H
#define RSTILT_CONFIG_H

#include "config.h"

typedef struct {
    meda_config_t  mcfg;        /* serial + mseed fields reused as-is */
    char          *parser_name; /* e.g. "thk" */
} rstilt_config_t;

int  rstilt_config_load(const char *path, rstilt_config_t *cfg);
void rstilt_config_free(rstilt_config_t *cfg);

#endif /* RSTILT_CONFIG_H */
