#include "scan/scan.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;
#define CHECK(cond, ...) do { if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__); fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); } } while (0)

static scan_counts ref_count(const uint8_t *p, size_t n)
{
    scan_counts r = {0, 0};
    for (size_t i = 0; i < n; i++) { if (p[i] == '\n') r.newlines++; if (p[i] >= 0x80) r.nonascii++; }
    return r;
}
static const uint8_t *ref_find_byte(const uint8_t *p, size_t n, uint8_t c)
{
    for (size_t i = 0; i < n; i++) if (p[i] == c) return p + i;
    return NULL;
}
static const uint8_t *ref_nth(const uint8_t *p, size_t n, uint64_t k)
{
    for (size_t i = 0; i < n; i++) if (p[i] == '\n') { if (k == 0) return p + i; k--; }
    return NULL;
}

/* Heap buffer of exactly n+off bytes; p = buf+off so ASan catches any overread. */
static void check_all(const uint8_t *p, size_t n, const char *what)
{
    scan_counts a = scan_count(p, n), b = ref_count(p, n);
    CHECK(a.newlines == b.newlines && a.nonascii == b.nonascii,
          "%s n=%zu count got %llu/%llu want %llu/%llu", what, n,
          (unsigned long long)a.newlines, (unsigned long long)a.nonascii,
          (unsigned long long)b.newlines, (unsigned long long)b.nonascii);
    static const uint8_t cs[] = {'\n', 'a', 0x80, 0xFF, 0};
    for (size_t j = 0; j < sizeof cs; j++)
        CHECK(scan_find_byte(p, n, cs[j]) == ref_find_byte(p, n, cs[j]), "%s n=%zu find_byte c=%u", what, n, cs[j]);
    uint64_t total = b.newlines;
    uint64_t ks[] = {0, 1, total ? total - 1 : 0, total, total + 1, total + 1000, 17, 255, 256, 300};
    for (size_t j = 0; j < sizeof ks / sizeof ks[0]; j++)
        CHECK(scan_find_nth_newline(p, n, ks[j]) == ref_nth(p, n, ks[j]), "%s n=%zu nth k=%llu", what, n, (unsigned long long)ks[j]);
    for (uint64_t k = 0; k <= total && k < 600; k++)
        CHECK(scan_find_nth_newline(p, n, k) == ref_nth(p, n, k), "%s n=%zu nth k=%llu", what, n, (unsigned long long)k);
}

static uint32_t rng_state = 12345;
static uint32_t rnd(void) { rng_state ^= rng_state << 13; rng_state ^= rng_state >> 17; rng_state ^= rng_state << 5; return rng_state; }

static void run_on(const uint8_t *src, size_t n, const char *what)
{
    for (size_t off = 0; off < 4; off++) {
        uint8_t *buf = malloc(n + off + 1);
        if (!buf) abort();
        memcpy(buf + off, src, n);
        check_all(buf + off, n, what);
        free(buf);
    }
}

int main(void)
{
    /* Fixed edge cases: sizes around the 16-byte block and the 255-block chunk boundary. */
    static const size_t sizes[] = {0, 1, 2, 15, 16, 17, 31, 32, 33, 47, 255, 4080, 4096 + 17};
    for (size_t s = 0; s < sizeof sizes / sizeof sizes[0]; s++) {
        size_t n = sizes[s];
        uint8_t *a = malloc(n + 1);
        if (!a) abort();
        memset(a, 'x', n); run_on(a, n, "no-newlines");
        memset(a, '\n', n); run_on(a, n, "all-newlines");
        memset(a, 0xFF, n); run_on(a, n, "all-high");
        memset(a, 0x80, n); run_on(a, n, "all-0x80");
        for (size_t i = 0; i < n; i++) a[i] = (i % 70 == 69) ? '\n' : (uint8_t)('a' + i % 26);
        run_on(a, n, "70-col-lines");
        for (size_t i = 0; i < n; i++) a[i] = (i % 2) ? '\n' : (uint8_t)0xC3;
        run_on(a, n, "alternating");
        /* random mixes of newline / high / ascii with several densities */
        for (int d = 0; d < 6; d++) {
            for (size_t i = 0; i < n; i++) {
                uint32_t r = rnd() % 100;
                a[i] = r < (uint32_t)(d * 15) ? '\n' : (r < (uint32_t)(d * 15 + 10) ? 0x90 : 'z');
            }
            run_on(a, n, "random-mix");
        }
        free(a);
    }
    /* Nonascii boundary values, with newlines in the same block. */
    uint8_t edge[64];
    for (int i = 0; i < 64; i++) edge[i] = (uint8_t)(i * 4 + 1);
    edge[5] = 0x7F; edge[6] = 0x80; edge[7] = '\n'; edge[40] = 0xFF; edge[41] = '\n';
    run_on(edge, 64, "boundary-bytes");

    if (failures) { fprintf(stderr, "scan_test: %d failure(s)\n", failures); return 1; }
    printf("scan_test: all checks passed\n");
    return 0;
}
