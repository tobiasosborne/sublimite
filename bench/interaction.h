#ifndef EDIT_BENCH_INTERACTION_H
#define EDIT_BENCH_INTERACTION_H
#include "harness.h"
#include <errno.h>
#include <stdlib.h>

/* Keep optional sample counts subject to the same interaction policy. */
static inline int bench_interaction_samples(const char *text, size_t *out)
{
    char *end = NULL;
    errno = 0;
    unsigned long value = strtoul(text, &end, 10);
    if (errno || !end || end == text || *end || text[0] < '0' || text[0] > '9' ||
        value < BENCH_INTERACTION_MIN_N || value > BENCH_INTERACTION_MIN_N * 10u) return -1;
    *out = (size_t)value;
    return 0;
}
#endif
