/* scan: differential against a scalar reference, with every span placed
 * flush against PROT_NONE guard pages so a read outside [p, p + n) faults. */
#include "scan/scan.h"
#include "os/os.h"

#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

static int failures;
#define CHECK(c, ...) do { if (!(c)) { failures++; fprintf(stderr, "%s:%d: FAIL ", __FILE__, __LINE__); \
    fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); } } while (0)

enum {
    BLOCK = 16,               /* (P) SSE2 vector width in bytes */
    CHUNK = 255 * BLOCK,      /* (P) blocks per byte-lane accumulator flush, times the block width */
    SWEEP_MAX = 2 * 4096,     /* (P) two 4 KiB pages: every length crosses a page and two chunks */
};
/* (P) v1 §13 size: 128 lineidx-sized 64 KiB chunks, then one block and one tail byte. */
#define LARGE_N ((size_t)8 * 1024 * 1024 + BLOCK + 1)

/* ---- scalar reference --------------------------------------------------- */

static size_t *ref_pos; /* ref_pos[k]: offset of the k-th '\n' of the last ref_scan span */

static scan_counts ref_scan(const uint8_t *p, size_t n)
{
    scan_counts r = {0, 0};
    for (size_t i = 0; i < n; i++) {
        if (p[i] == '\n') ref_pos[r.newlines++] = i;
        if (p[i] >= 0x80) r.nonascii++;
    }
    return r;
}

/* ---- guard fixture ------------------------------------------------------ */

/* [mapping: PROT_NONE page][begin .. end: RW pages][end: PROT_NONE page] */
typedef struct guard_fixture {
    uint8_t *mapping, *begin, *end;
    size_t page, mapped;
} guard_fixture;

static bool guard_init(guard_fixture *fx, size_t need)
{
    fx->page = os_page_size();
    size_t span = (need + fx->page - 1) / fx->page * fx->page;
    fx->mapped = span + 2 * fx->page;
    void *m = mmap(NULL, fx->mapped, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (m == MAP_FAILED) return false;
    fx->mapping = m;
    fx->begin = fx->mapping + fx->page;
    fx->end = fx->begin + span;
    if (mprotect(fx->begin, span, PROT_READ | PROT_WRITE) != 0) {
        (void)munmap(m, fx->mapped);
        return false;
    }
    return true;
}

enum { PROBE_FAULTED = 3 };
static void on_segv(int sig)
{
    (void)sig;
    _exit(PROBE_FAULTED);
}

/* Exit status of a child that reads *addr: PROBE_FAULTED or 0. */
static int probe(const volatile uint8_t *addr)
{
    fflush(stdout);
    fflush(stderr);
    pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        struct sigaction sa;
        memset(&sa, 0, sizeof sa);
        sa.sa_handler = on_segv;
        sigemptyset(&sa.sa_mask);
        if (sigaction(SIGSEGV, &sa, NULL) != 0) _exit(1);
        (void)*addr;
        _exit(0);
    }
    int status = 0;
    if (waitpid(pid, &status, 0) != pid || !WIFEXITED(status)) return -1;
    return WEXITSTATUS(status);
}

/* Positive control: the guard can fail. The first byte on either side of
 * the span faults and the span's own edge bytes do not. */
static bool guard_armed(const guard_fixture *fx, const char *name)
{
    int below = probe(fx->begin - 1), first = probe(fx->begin);
    int last = probe(fx->end - 1), above = probe(fx->end);
    bool armed = below == PROBE_FAULTED && first == 0 && last == 0 && above == PROBE_FAULTED;
    printf("scan_test: %s guard page=%zu: [%p PROT_NONE %zu][%p RW %zu][%p PROT_NONE %zu]; "
           "read begin-1=%s begin=%s end-1=%s end=%s\n",
           name, fx->page, (void *)fx->mapping, fx->page, (void *)fx->begin,
           (size_t)(fx->end - fx->begin), (void *)fx->end, fx->page,
           below == PROBE_FAULTED ? "SIGSEGV" : "ok", first == PROBE_FAULTED ? "SIGSEGV" : "ok",
           last == PROBE_FAULTED ? "SIGSEGV" : "ok", above == PROBE_FAULTED ? "SIGSEGV" : "ok");
    return armed;
}

/* ---- differential checks ------------------------------------------------ */

static void check_rank(const uint8_t *p, size_t n, uint64_t k, const uint8_t *want, const char *what)
{
    const uint8_t *got = scan_find_nth_newline(p, n, k);
    CHECK(got == want, "%s n=%zu nth k=%llu got %td want %td", what, n, (unsigned long long)k,
          got ? got - p : -1, want ? want - p : -1);
}

static void check_range(const uint8_t *p, size_t n, const char *what, bool every_rank)
{
    scan_counts want = ref_scan(p, n), got = scan_count(p, n);
    CHECK(got.newlines == want.newlines && got.nonascii == want.nonascii,
          "%s n=%zu count got %llu/%llu want %llu/%llu", what, n,
          (unsigned long long)got.newlines, (unsigned long long)got.nonascii,
          (unsigned long long)want.newlines, (unsigned long long)want.nonascii);
    uint64_t total = want.newlines;
    if (every_rank) {
        for (uint64_t k = 0; k < total; k++) check_rank(p, n, k, p + ref_pos[k], what);
    } else {
        /* First, second, middle, last two. */
        const uint64_t sample[] = {0, 1, total / 2, total - 2, total - 1};
        for (size_t i = 0; i < sizeof sample / sizeof sample[0]; i++)
            if (sample[i] < total) check_rank(p, n, sample[i], p + ref_pos[sample[i]], what);
    }
    const uint64_t absent[] = {total, total + 1, UINT64_MAX - 1, UINT64_MAX};
    for (size_t i = 0; i < sizeof absent / sizeof absent[0]; i++)
        check_rank(p, n, absent[i], NULL, what);
}

/* Left-flush at begin, then right-flush at end minus each of shifts
 * offsets: the right guard sees every start alignment below shifts. */
static void run_case(guard_fixture *fx, const uint8_t *src, size_t n, size_t shifts,
                     const char *what, bool every_rank)
{
    memcpy(fx->begin, src, n);
    check_range(fx->begin, n, what, every_rank);
    for (size_t s = 0; s < shifts; s++) {
        uint8_t *p = fx->end - n - s;
        memcpy(p, src, n);
        check_range(p, n, what, every_rank);
    }
}

static uint32_t rng_state = 12345;
static uint32_t rnd(void)
{
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 17;
    rng_state ^= rng_state << 5;
    return rng_state;
}

/* Every length up to SWEEP_MAX, flush against each guard; all 256 byte
 * values appear as data and the span always ends in '\n'. */
static void sweep(guard_fixture *fx, uint8_t *src)
{
    for (size_t n = 0; n <= SWEEP_MAX; n++) {
        for (size_t i = 0; i < n; i++) src[i] = (uint8_t)i;
        if (n) src[n - 1] = '\n';
        run_case(fx, src, n, 1, "sweep", true);
    }
    /* An empty span at the guard byte itself performs no read. */
    check_range(fx->end, 0, "empty-at-guard", true);
}

static void patterns(guard_fixture *fx, uint8_t *a)
{
    /* Block, two-block, chunk and page edges, and two chunks plus a tail. */
    static const size_t sizes[] = {0, 1, 2, BLOCK - 1, BLOCK, BLOCK + 1, 2 * BLOCK - 1, 2 * BLOCK,
                                   2 * BLOCK + 1, 3 * BLOCK - 1, 255, CHUNK - 1, CHUNK, CHUNK + 1,
                                   4096 + BLOCK + 1, 2 * CHUNK + 1};
    for (size_t s = 0; s < sizeof sizes / sizeof sizes[0]; s++) {
        size_t n = sizes[s];
        memset(a, 'x', n); run_case(fx, a, n, BLOCK, "no-newlines", true);
        memset(a, '\n', n); run_case(fx, a, n, BLOCK, "all-newlines", true);
        memset(a, 0xFF, n); run_case(fx, a, n, BLOCK, "all-0xFF", true);
        memset(a, 0x80, n); run_case(fx, a, n, BLOCK, "all-0x80", true);
        for (size_t i = 0; i < n; i++) a[i] = (i % 70 == 69) ? '\n' : (uint8_t)('a' + i % 26);
        run_case(fx, a, n, BLOCK, "70-col-lines", true);
        for (size_t i = 0; i < n; i++) a[i] = (i % 2) ? '\n' : (uint8_t)0xC3;
        run_case(fx, a, n, BLOCK, "alternating", true);
        /* Newline share d*15 %, then 10 % high bytes, rest ASCII: 0 .. 75 % newlines. */
        for (uint32_t d = 0; d < 6; d++) {
            for (size_t i = 0; i < n; i++) {
                uint32_t r = rnd() % 100;
                a[i] = r < d * 15 ? '\n' : (r < d * 15 + 10 ? 0x90 : 'z');
            }
            run_case(fx, a, n, BLOCK, "random-mix", true);
        }
    }
    /* 0x7F/0x80 straddle the signed compare; newlines share their blocks. */
    for (size_t i = 0; i < 4 * BLOCK; i++) a[i] = (uint8_t)(i * 4 + 1);
    a[5] = 0x7F; a[6] = 0x80; a[7] = '\n'; a[40] = 0xFF; a[41] = '\n';
    run_case(fx, a, 4 * BLOCK, BLOCK, "boundary-bytes", true);
}

static void large(guard_fixture *fx, uint8_t *a)
{
    const size_t n = LARGE_N;
    memset(a, 'x', n);
    a[7] = '\n'; a[65536] = '\n'; a[n - 1] = '\n'; a[65537] = 0x80; a[n - 2] = 0xFF;
    run_case(fx, a, n, 2, "large-sparse", false);
    memcpy(fx->begin, a, n);
    scan_counts c = scan_count(fx->begin, n);
    CHECK(c.newlines == 3 && c.nonascii == 2, "large-sparse fixed counts");
    CHECK(scan_find_nth_newline(fx->begin, n, 2) == fx->begin + n - 1, "large-sparse last newline");
    memset(a, '\n', n); run_case(fx, a, n, 2, "large-all-newlines", false);
    memset(a, 0xFF, n); run_case(fx, a, n, 2, "large-all-0xFF", false);
}

int main(void)
{
    guard_fixture small, big;
    if (!guard_init(&small, SWEEP_MAX + BLOCK - 1) || !guard_init(&big, LARGE_N + 1)) {
        fprintf(stderr, "scan_test: FAIL guard mapping\n");
        return 1;
    }
    bool armed = guard_armed(&small, "small");
    armed = guard_armed(&big, "large") && armed;
    if (!armed) {
        fprintf(stderr, "scan_test: FAIL guard pages not armed; overread checks would be inert\n");
        return 1;
    }
    ref_pos = malloc(LARGE_N * sizeof *ref_pos);
    uint8_t *src = malloc(LARGE_N);
    if (!ref_pos || !src) {
        fprintf(stderr, "scan_test: FAIL allocation\n");
        return 1;
    }
    sweep(&small, src);
    patterns(&small, src);
    large(&big, src);
    free(src);
    free(ref_pos);
    CHECK(munmap(small.mapping, small.mapped) == 0, "munmap small");
    CHECK(munmap(big.mapping, big.mapped) == 0, "munmap large");
    if (failures) {
        fprintf(stderr, "scan_test: %d failure(s)\n", failures);
        return 1;
    }
    printf("scan_test: PASS (sweep 0..%d both guards, %zu-byte large spans, guards armed)\n",
           SWEEP_MAX, LARGE_N);
    return 0;
}
