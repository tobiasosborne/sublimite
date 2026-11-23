/* SSE2 byte scanners. SSE2 is part of the x86-64 base ISA, so every target
 * CPU runs these loops and there is nothing to dispatch on. */
#include "scan/scan.h"

#include <emmintrin.h>

enum {
    BLOCK = 16,         /* (P) bytes per SSE2 vector */
    CHUNK_BLOCKS = 255, /* (P) a byte lane gains at most 1 per block and holds 255 */
};

static inline __m128i load(const uint8_t *p)
{
    return _mm_loadu_si128((const __m128i *)(const void *)p);
}

/* Whole blocks in the next accumulator chunk of a span with left >= BLOCK. */
static inline size_t chunk_blocks(size_t left)
{
    size_t blocks = left / BLOCK;
    return blocks < CHUNK_BLOCKS ? blocks : CHUNK_BLOCKS;
}

/* Sum of the 16 byte lanes of acc. */
static inline uint64_t hsum_bytes(__m128i acc)
{
    __m128i sums = _mm_sad_epu8(acc, _mm_setzero_si128());
    return (uint64_t)_mm_cvtsi128_si64(sums) + (uint64_t)_mm_cvtsi128_si64(_mm_unpackhi_epi64(sums, sums));
}

/* A compare-equal lane is -1, so subtracting it counts a match. */
scan_counts scan_count(const uint8_t *p, size_t n)
{
    const __m128i nl = _mm_set1_epi8('\n');
    const __m128i zero = _mm_setzero_si128();
    scan_counts r = {0, 0};
    size_t i = 0;
    while (n - i >= BLOCK) {
        size_t blocks = chunk_blocks(n - i);
        __m128i acc_nl = zero, acc_hi = zero;
        for (size_t b = 0; b < blocks; b++) {
            __m128i x = load(p + i + BLOCK * b);
            acc_nl = _mm_sub_epi8(acc_nl, _mm_cmpeq_epi8(x, nl));
            acc_hi = _mm_sub_epi8(acc_hi, _mm_cmplt_epi8(x, zero)); /* signed < 0 is byte >= 0x80 */
        }
        r.newlines += hsum_bytes(acc_nl);
        r.nonascii += hsum_bytes(acc_hi);
        i += BLOCK * blocks;
    }
    for (; i < n; i++) {
        r.newlines += p[i] == '\n';
        r.nonascii += p[i] >= 0x80;
    }
    return r;
}

/* The k-th newline at or after q. The caller has counted more than k
 * newlines in the whole blocks from q with this same compare, so the walk
 * stops inside them. */
static const uint8_t *select_newline(const uint8_t *q, uint64_t k, __m128i nl)
{
    for (;; q += BLOCK)
        for (unsigned m = (unsigned)_mm_movemask_epi8(_mm_cmpeq_epi8(load(q), nl)); m; m &= m - 1)
            if (k-- == 0) return q + __builtin_ctz(m);
}

/* Count a chunk at a time and skip it whole unless it holds the target. */
const uint8_t *scan_find_nth_newline(const uint8_t *p, size_t n, uint64_t k)
{
    const __m128i nl = _mm_set1_epi8('\n');
    size_t i = 0;
    while (n - i >= BLOCK) {
        size_t blocks = chunk_blocks(n - i);
        __m128i acc = _mm_setzero_si128();
        for (size_t b = 0; b < blocks; b++)
            acc = _mm_sub_epi8(acc, _mm_cmpeq_epi8(load(p + i + BLOCK * b), nl));
        uint64_t c = hsum_bytes(acc);
        if (k < c) return select_newline(p + i, k, nl);
        k -= c;
        i += BLOCK * blocks;
    }
    for (; i < n; i++)
        if (p[i] == '\n' && k-- == 0) return p + i;
    return NULL;
}
