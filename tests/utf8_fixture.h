#ifndef TESTS_UTF8_FIXTURE_H
#define TESTS_UTF8_FIXTURE_H

#include "os/os.h"
#include "utf8/utf8.h"

/* Supply the full span and budget to the public step functions. */
static inline size_t first_grapheme(const uint8_t *bytes, size_t length)
{
    utf8_gseg state;
    utf8_gseg_init(&state);
    size_t used;
    ASSERT(utf8_grapheme_step(&state, bytes, length, length, 1, &used) == UTF8_G_END);
    return used;
}

static inline size_t first_cluster(const uint8_t *bytes, size_t length, int *width)
{
    utf8_cseg state;
    utf8_cseg_init(&state);
    size_t used;
    ASSERT(utf8_cluster_step(&state, bytes, length, length, 1, &used, width) == UTF8_G_END);
    return used;
}

static inline int first_cluster_width(const uint8_t *bytes, size_t length)
{
    int width;
    (void)first_cluster(bytes, length, &width);
    return width;
}

/* Exercise scalar stepping and width independently in the test-only object. */
size_t utf8_test_prev(const uint8_t *base, size_t off);
int utf8_test_cell_width(uint32_t cp);

#endif
