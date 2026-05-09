#ifndef RSTILT_PARSER_H
#define RSTILT_PARSER_H

#include <stdint.h>

/* Parse one line of serial data into per-channel int32 samples.
 * Returns number of channels filled (>= 1), or -1 on parse error.
 * Samples are ordered to match the channels array in the config. */
typedef int (*rstilt_parse_fn)(const char *line, int32_t *samples, int max_ch);

typedef struct {
    const char      *name;
    rstilt_parse_fn  parse;
} rstilt_parser_t;

/* Look up a registered parser by name. Returns NULL if not found. */
const rstilt_parser_t *rstilt_parser_find(const char *name);

/* Built-in parsers — add new ones here and register in rstilt_parser.c */
extern const rstilt_parser_t rstilt_parser_thk;

#endif /* RSTILT_PARSER_H */
