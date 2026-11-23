#ifndef SCAN_SCAN_H
#define SCAN_SCAN_H

#include <stddef.h>
#include <stdint.h>

/* Stateless scanners over one byte span [p, p + n). Each reads exactly
 * those bytes and no others, so a span may end at an inaccessible page.
 * With n == 0 nothing is read. */

typedef struct scan_counts {
    uint64_t newlines; /* bytes equal to '\n' */
    uint64_t nonascii; /* bytes >= 0x80 */
} scan_counts;

scan_counts scan_count(const uint8_t *p, size_t n);

/* The k-th '\n' of the span (k counts from 0), or NULL when the span holds
 * at most k newlines. */
const uint8_t *scan_find_nth_newline(const uint8_t *p, size_t n, uint64_t k);

#endif
