#ifndef SCAN_H
#define SCAN_H
#include <stddef.h>
#include <stdint.h>

typedef struct { uint64_t newlines; uint64_t nonascii; } scan_counts;

scan_counts scan_count(const uint8_t *p, size_t n);                       /* count '\n' and bytes >= 0x80 */
const uint8_t *scan_find_byte(const uint8_t *p, size_t n, uint8_t c);     /* first occurrence or NULL */
const uint8_t *scan_find_nth_newline(const uint8_t *p, size_t n, uint64_t k); /* k-th (0-based) '\n' or NULL */

#endif
