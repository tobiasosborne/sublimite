#include "scan/scan.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#if defined(__SANITIZE_ADDRESS__)
#define SCAN_ASAN 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
#define SCAN_ASAN 1
#endif
#endif
#ifdef SCAN_ASAN
#include <sanitizer/asan_interface.h>
#endif

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

/* Scalar differential checks; exhaustive ranks are reserved for small ranges. */
static void check_range(const uint8_t *p, size_t n, const char *what, int exhaustive)
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
    uint64_t ks[] = {0, 1, total ? total - 1 : 0, total, total + 1, total + 1000, 17, 255, 256, 300, UINT64_MAX - 1, UINT64_MAX};
    for (size_t j = 0; j < sizeof ks / sizeof ks[0]; j++)
        CHECK(scan_find_nth_newline(p, n, ks[j]) == ref_nth(p, n, ks[j]), "%s n=%zu nth k=%llu", what, n, (unsigned long long)ks[j]);
    for (uint64_t k = 0; exhaustive && k <= total && k < 600; k++)
        CHECK(scan_find_nth_newline(p, n, k) == ref_nth(p, n, k), "%s n=%zu nth k=%llu", what, n, (unsigned long long)k);
}

static uint32_t rng_state = 12345;
static uint32_t rnd(void) { rng_state ^= rng_state << 13; rng_state ^= rng_state >> 17; rng_state ^= rng_state << 5; return rng_state; }

static void run_on(const uint8_t *src, size_t n, const char *what)
{
    for (size_t off = 0; off < 16; off++) {
        uint8_t *buf = malloc((n + off) ? n + off : 1);
        if (!buf) abort();
        memcpy(buf + off, src, n);
#ifdef SCAN_ASAN
        CHECK(n == 0 || __asan_address_is_poisoned(buf + off + n),
              "§16 exact-bound FAIL: p[n] is addressable n=%zu off=%zu", n, off);
#endif
        check_range(buf + off, n, what, 1);
        free(buf);
    }
}

static void guarded_ranges(void)
{
    long page_value = sysconf(_SC_PAGESIZE);
    if (page_value <= 0) { CHECK(0, "sysconf page size"); return; }
    size_t page = (size_t)page_value;
    const size_t limit = 8192;
    size_t accessible = ((limit + page - 1) / page) * page;
    size_t mapped = accessible + 2 * page;
    uint8_t *mapping = mmap(NULL, mapped, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (mapping == MAP_FAILED) { CHECK(0, "guard mmap"); return; }
    uint8_t *begin = mapping + page, *end = begin + accessible;
    if (mprotect(begin, accessible, PROT_READ | PROT_WRITE) != 0) {
        CHECK(0, "guard mprotect"); (void)munmap(mapping, mapped); return;
    }
    /* Right-bound ranges end exactly at PROT_NONE, including an empty range
     * whose pointer itself is inaccessible. Every SIMD alignment is covered.
     * Left-bound ranges start exactly after PROT_NONE to catch backward loads. */
    for (size_t n = 0; n <= limit; n++) {
        for (unsigned side = 0; side < 2; side++) {
            uint8_t *p = side ? begin : end - n;
            for (size_t i = 0; i < n; i++) p[i] = (uint8_t)i;
            if (n) p[n - 1] = '\n';
            check_range(p, n, side ? "left-guard" : "right-guard", 0);
        }
    }
    CHECK(munmap(mapping, mapped) == 0, "guard munmap");
}

static void large_ranges(void)
{
    const size_t n = 8u * 1024u * 1024u + 17u;
    uint8_t *buf = malloc(n);
    if (!buf) { CHECK(0, "large allocation"); return; }
    memset(buf, 'x', n);
    buf[7] = '\n'; buf[65536] = '\n'; buf[n - 1] = '\n';
    buf[65537] = 0x80; buf[n - 2] = 0xFF;
    scan_counts counts = scan_count(buf, n);
    CHECK(counts.newlines == 3 && counts.nonascii == 2, "§13 large count beyond 64 KiB");
    CHECK(scan_find_nth_newline(buf, n, 2) == buf + n - 1, "§13 last newline beyond 64 KiB");
    CHECK(scan_find_byte(buf, n, 0xFF) == buf + n - 2, "§13 byte beyond 64 KiB");
    for (size_t off = 0; off < 16; off++) check_range(buf + off, n - off, "large-range", 0);
    free(buf);
}

int main(void)
{
    /* Fixed edge cases: sizes around the 16-byte block and the 255-block chunk boundary. */
    static const size_t sizes[] = {0, 1, 2, 15, 16, 17, 31, 32, 33, 47, 255, 4080, 4096 + 17};
    for (size_t s = 0; s < sizeof sizes / sizeof sizes[0]; s++) {
        size_t n = sizes[s];
        uint8_t *a = malloc(n ? n : 1);
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

    /* Every possible byte appears as a query and as data. */
    uint8_t bytes[256];
    for (unsigned c = 0; c < 256; c++) bytes[c] = (uint8_t)c;
    for (unsigned c = 0; c < 256; c++)
        CHECK(scan_find_byte(bytes, sizeof bytes, (uint8_t)c) == bytes + c, "all byte values c=%u", c);
    large_ranges();
    guarded_ranges();

    if (failures) { fprintf(stderr, "scan_test: %d failure(s)\n", failures); return 1; }
    printf("scan_test: all checks passed (§13 large ranges; §16 exact bounds, 16 alignments, guarded pages)\n");
    return 0;
}
