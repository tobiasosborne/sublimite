#include "scan/scan.h"
#include <emmintrin.h>

/* Sum of the 16 byte lanes of acc (each lane <= 255 by construction). */
static inline uint64_t hsum_bytes(__m128i acc)
{
    uint64_t lanes[2];
    _mm_storeu_si128((__m128i *)lanes, _mm_sad_epu8(acc, _mm_setzero_si128()));
    return lanes[0] + lanes[1];
}

scan_counts scan_count(const uint8_t *p, size_t n)
{
    const __m128i nl = _mm_set1_epi8('\n');
    const __m128i zero = _mm_setzero_si128();
    scan_counts r = {0, 0};
    size_t i = 0;
    while (n - i >= 16) {
        /* Byte lanes count up to 255 blocks before they must be flushed. */
        size_t blocks = (n - i) / 16;
        if (blocks > 255) blocks = 255;
        __m128i acc_nl = zero, acc_hi = zero;
        for (size_t b = 0; b < blocks; b++) {
            __m128i x = _mm_loadu_si128((const __m128i *)(p + i + 16 * b));
            acc_nl = _mm_sub_epi8(acc_nl, _mm_cmpeq_epi8(x, nl));   /* -1 per match */
            acc_hi = _mm_sub_epi8(acc_hi, _mm_cmplt_epi8(x, zero)); /* signed < 0 == byte >= 0x80 */
        }
        r.newlines += hsum_bytes(acc_nl);
        r.nonascii += hsum_bytes(acc_hi);
        i += 16 * blocks;
    }
    for (; i < n; i++) {
        if (p[i] == '\n') r.newlines++;
        if (p[i] >= 0x80) r.nonascii++;
    }
    return r;
}

const uint8_t *scan_find_byte(const uint8_t *p, size_t n, uint8_t c)
{
    const __m128i v = _mm_set1_epi8((char)c);
    size_t i = 0;
    for (; n - i >= 16; i += 16) {
        int m = _mm_movemask_epi8(_mm_cmpeq_epi8(_mm_loadu_si128((const __m128i *)(p + i)), v));
        if (m) return p + i + (size_t)__builtin_ctz((unsigned)m);
    }
    for (; i < n; i++)
        if (p[i] == c) return p + i;
    return NULL;
}

const uint8_t *scan_find_nth_newline(const uint8_t *p, size_t n, uint64_t k)
{
    const __m128i nl = _mm_set1_epi8('\n');
    const __m128i zero = _mm_setzero_si128();
    size_t i = 0;
    while (n - i >= 16) {
        size_t blocks = (n - i) / 16;
        if (blocks > 255) blocks = 255;
        __m128i acc = zero;
        for (size_t b = 0; b < blocks; b++)
            acc = _mm_sub_epi8(acc, _mm_cmpeq_epi8(_mm_loadu_si128((const __m128i *)(p + i + 16 * b)), nl));
        uint64_t c = hsum_bytes(acc);
        if (k >= c) {          /* target is beyond this chunk: skip it whole */
            k -= c;
            i += 16 * blocks;
            continue;
        }
        /* Target lies in this chunk: descend block by block, then clear low bits. */
        for (size_t b = 0; b < blocks; b++) {
            int m = _mm_movemask_epi8(_mm_cmpeq_epi8(_mm_loadu_si128((const __m128i *)(p + i + 16 * b)), nl));
            while (m) {
                if (k == 0) return p + i + 16 * b + (size_t)__builtin_ctz((unsigned)m);
                k--;
                m &= m - 1;
            }
        }
        return NULL; /* unreachable: k < c guarantees a hit */
    }
    for (; i < n; i++) {
        if (p[i] == '\n') {
            if (k == 0) return p + i;
            k--;
        }
    }
    return NULL;
}
