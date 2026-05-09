#include "rstilt_parser.h"

#include <string.h>

static const rstilt_parser_t *parsers[] = {
    &rstilt_parser_thk,
    NULL,
};

const rstilt_parser_t *rstilt_parser_find(const char *name)
{
    for (int i = 0; parsers[i]; i++) {
        if (strcmp(parsers[i]->name, name) == 0)
            return parsers[i];
    }
    return NULL;
}
