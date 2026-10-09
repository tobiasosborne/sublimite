#include "lineidx/lineidx.h"
#include "piece/piece.h"
#include "base/base.h"
#include "trace/trace.h"

#include <fcntl.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#if defined(__has_feature)
#if __has_feature(address_sanitizer)
#include <sanitizer/allocator_interface.h>
#define OBSERVE_ALLOCATIONS 1
#endif
#endif

static int fails;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: FAIL %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)
#define REQUIRE(c) do { if (!(c)) { fprintf(stderr, "%s:%d: FATAL %s\n", __FILE__, __LINE__, #c); fails++; return; } } while (0)

static work_pool pool;
static uint64_t rng_s = 0x9E3779B97F4A7C15ull;
static uint64_t rnd(void) { rng_s ^= rng_s >> 12; rng_s ^= rng_s << 25; rng_s ^= rng_s >> 27; return rng_s * 0x2545F4914F6CDD1Dull; }

/* ---- naive model ---- */
static uint64_t n_lines(const uint8_t *b, uint64_t n) { uint64_t c = 1; for (uint64_t i = 0; i < n; i++) if (b[i] == '\n') c++; return c; }
static uint64_t n_l2b(const uint8_t *b, uint64_t n, uint64_t line)
{
    if (line == 0) return 0;
    uint64_t seen = 0;
    for (uint64_t i = 0; i < n; i++) if (b[i] == '\n' && ++seen == line) return i + 1;
    return n;
}
static uint64_t n_b2l(const uint8_t *b, uint64_t n, uint64_t off)
{
    if (off > n) off = n;
    uint64_t c = 0;
    for (uint64_t i = 0; i < off; i++) if (b[i] == '\n') c++;
    return c;
}

/* ---- sources ---- */
typedef struct { const uint8_t *b; uint64_t n; size_t frag; unsigned delay_us; } flat;
static size_t flat_span(void *ctx, uint64_t off, const uint8_t **p)
{
    flat *f = ctx;
    if (f->delay_us) usleep(f->delay_us);
    if (off >= f->n) return 0;
    uint64_t r = f->n - off;
    if (f->frag && r > f->frag) r = f->frag;
    *p = f->b + off;
    return (size_t)r;
}
static lineidx_src mk(flat *f) { return (lineidx_src){ f, f->n, flat_span, NULL }; }

/* A worker lease paused inside span(), without scheduler timing guesses. */
typedef struct {
    const uint8_t *b;
    uint64_t n;
    bool repeat;
    _Atomic bool entered, resume, released;
} lease;

static size_t lease_span(void *ctx, uint64_t off, const uint8_t **p)
{
    lease *s = ctx;
    if (off >= s->n) return 0;
    *p = s->b + (s->repeat ? 0 : off);
    atomic_store_explicit(&s->entered, true, memory_order_release);
    while (!atomic_load_explicit(&s->resume, memory_order_acquire)) sched_yield();
    uint64_t k = s->n - off;
    return (size_t)(k > LINEIDX_CHUNK ? LINEIDX_CHUNK : k);
}

static void lease_release(void *ctx)
{
    lease *s = ctx;
    free((void *)s->b);
    atomic_store_explicit(&s->released, true, memory_order_release);
}

static bool lease_entered(lease *s)
{
    for (unsigned i = 0; i < 20000; i++) {
        if (atomic_load_explicit(&s->entered, memory_order_acquire)) return true;
        nanosleep(&(struct timespec){0, 1000000}, NULL);
    }
    return false;
}

typedef struct { uint8_t b[LINEIDX_CHUNK]; uint64_t n, served; } synthetic;
static size_t synthetic_span(void *ctx, uint64_t off, const uint8_t **p)
{
    synthetic *s = ctx;
    if (off >= s->n) return 0;
    uint64_t k = s->n - off;
    if (k > sizeof s->b) k = sizeof s->b;
    *p = s->b;
    s->served += k;
    return (size_t)k;
}

static void test_review_capacity(void)
{
    lineidx *x = lineidx_create(0);
    REQUIRE(x != NULL);
    CHECK(lineidx_edit(x, 0, 0, 1ull << 32) == -1);
    CHECK(lineidx_len(x) == 0 && lineidx_chunk_count(x) == 1);
    CHECK(lineidx_line_count(x).exact && lineidx_line_count(x).value == 1);
    lineidx_destroy(x);

    synthetic s = { .n = 8ull * LINEIDX_CHUNK };
    memset(s.b, '\n', sizeof s.b);
    lineidx_src src = { &s, s.n, synthetic_span, NULL };
    x = lineidx_create(0);
    REQUIRE(x != NULL);
    CHECK(lineidx_edit(x, 0, 0, s.n) == 0);
    CHECK(lineidx_chunk_count(x) == 8);
    CHECK(lineidx_refresh(x, &src) == 1);
    CHECK(s.served <= LINEIDX_CHUNK);
    CHECK(!lineidx_complete(x));
    lineidx_destroy(x);

    x = lineidx_create(0);
    REQUIRE(x != NULL);
    CHECK(lineidx_edit(x, 0, 0, LINEIDX_CHUNK + 1u) == 0);
    CHECK(lineidx_chunk_count(x) == 2);
    lineidx_destroy(x);
}

static void test_review_overflow(void)
{
    uint8_t b[] = "a\nb";
    flat f = { b, sizeof b - 1, 0, 0 };
    lineidx_src s = mk(&f);
    lineidx *x = lineidx_create(f.n);
    REQUIRE(x != NULL);
    CHECK(lineidx_seek_line(x, &s, 2, f.n).exact);
    CHECK(lineidx_edit(x, 0, 0, UINT64_MAX) == -1);
    CHECK(lineidx_len(x) == f.n && lineidx_chunk_count(x) == 1);
    CHECK(lineidx_line_count(x).exact && lineidx_line_count(x).value == 2);
    CHECK(lineidx_line_to_byte(x, &s, 1).value == 2);
    lineidx_destroy(x);
    x = lineidx_create(UINT64_MAX);
    CHECK(x == NULL);
    lineidx_destroy(x);
    CHECK(lineidx_create_reserved(0, SIZE_MAX) == NULL);

    lease live = { .b = b, .n = f.n };
    lineidx_src leased = { &live, live.n, lease_span, NULL };
    x = lineidx_create(live.n);
    REQUIRE(x != NULL);
    CHECK(lineidx_build_start(x, &pool, &leased) == 0);
    REQUIRE(lease_entered(&live));
    CHECK(lineidx_edit(x, 0, 0, UINT64_MAX) == -1);
    CHECK(lineidx_edit(x, 0, 0, 1ull << 32) == -1);
    CHECK(lineidx_building(x)); /* neither overflow nor capacity refusal cancels */
    atomic_store_explicit(&live.resume, true, memory_order_release);
    lineidx_destroy(x);
}

static void check_epoch_lease(uint32_t epoch)
{
    work_pool wp;
    REQUIRE(work_pool_init(&wp, 1, 0) == 0);
    atomic_store(&wp.slots[0].epoch, epoch);
    uint8_t *b = malloc(LINEIDX_CHUNK);
    REQUIRE(b != NULL);
    memset(b, '\n', LINEIDX_CHUNK);
    lease s = { .b = b, .n = LINEIDX_CHUNK };
    lineidx_src src = { &s, s.n, lease_span, lease_release };
    lineidx *x = lineidx_create(s.n);
    REQUIRE(x != NULL);
    CHECK(lineidx_build_start(x, &wp, &src) == 0);
    REQUIRE(lease_entered(&s));
    bool running = lineidx_building(x);
    CHECK(running);
    /* On the broken code, avoid deliberately freeing the running job here. */
    if (running) {
        lineidx_build_cancel(x);
        CHECK(!atomic_load(&s.released));
        CHECK(lineidx_building(x));
    }
    atomic_store_explicit(&s.resume, true, memory_order_release);
    /* The red version's building() already lies; use the test pool's physical
     * acknowledgement before cleanup, so its expected failure cannot itself
     * turn into a second use-after-free in destroy(). */
    for (unsigned i = 0; i < 20000 && atomic_load(&wp.slots[0].busy); i++)
        nanosleep(&(struct timespec){0, 1000000}, NULL);
    CHECK(atomic_load(&wp.slots[0].busy) == 0);
    if (atomic_load(&wp.slots[0].busy)) work_pool_shutdown(&wp);
    lineidx_destroy(x);
    CHECK(atomic_load(&s.released));
    work_pool_shutdown(&wp);
}

static void test_review_epoch(void)
{
    check_epoch_lease(UINT32_MAX - 2u);
    check_epoch_lease(UINT32_MAX - 1u); /* cancel wraps the lease epoch to zero */
}

static void test_review_boundaries(void)
{
    const uint64_t n = 200000;
    uint8_t *b = malloc(n);
    REQUIRE(b != NULL);
    memset(b, 'z', n);
    flat f = { b, n, 9000, 0 };
    lineidx_src s = mk(&f);
    lineidx *x = lineidx_create(n);
    REQUIRE(x != NULL);
    lineidx_result q = lineidx_line_to_byte(x, &s, 1);
    CHECK(!q.exact && q.value == 0);
    q = lineidx_seek_line(x, &s, UINT64_MAX, 0);
    CHECK(!q.exact && q.value == 0);
    /* CRLF straddling the alignment window and a fragment boundary. */
    b[65535] = '\r'; b[65536] = '\n';
    f.frag = 1;
    q = lineidx_line_to_byte(x, &s, 1700);
    CHECK(!q.exact && q.value == 65537);
    f.frag = 9000;
    q = lineidx_line_to_byte(x, &s, UINT64_MAX);
    CHECK(!q.exact && (q.value == 0 || b[q.value - 1] == '\n'));
    b[n - 1] = '\n';
    q = lineidx_line_to_byte(x, &s, UINT64_MAX);
    CHECK(!q.exact && q.value == n);
    lineidx_destroy(x);

    memset(b, 'z', n);
    b[7] = '\n';
    x = lineidx_create(n);
    REQUIRE(x != NULL);
    q = lineidx_seek_line(x, &s, 2, LINEIDX_CHUNK);
    CHECK(!q.exact && q.value == 8); /* fallback when prefix ends inside a long line */
    q = lineidx_line_to_byte(x, &s, UINT64_MAX);
    CHECK(!q.exact && q.value == 8);
    lineidx_destroy(x);
    free(b);
}

static void test_review_memory(void)
{
    uint8_t b[LINEIDX_CHUNK];
    memset(b, '\n', sizeof b);
    const uint64_t n = 10000000000ull;
    lease s = { .b = b, .n = n, .repeat = true };
    lineidx_src src = { &s, n, lease_span, NULL };
#ifdef OBSERVE_ALLOCATIONS
    size_t before = __sanitizer_get_current_allocated_bytes();
#endif
    lineidx *x = lineidx_create(n);
    REQUIRE(x != NULL);
    CHECK(lineidx_build_start(x, &pool, &src) == 0);
    REQUIRE(lease_entered(&s));
    size_t owned = lineidx_mem_bytes(x);
#ifdef OBSERVE_ALLOCATIONS
    size_t observed = __sanitizer_get_current_allocated_bytes() - before;
    CHECK(owned == observed);
    CHECK(observed <= 6882816u); /* G10f allowance from review §12. */
    fprintf(stderr, "  10 GB geometry + paused job: %zu B accounted, %zu B allocator-observed\n", owned, observed);
#endif
    CHECK(owned <= 6882816u);
    lineidx_build_cancel(x);
    CHECK(lineidx_mem_bytes(x) == owned); /* retired scratch is still owned */
    /* Never stack more scratch arrays behind a still-running lease. */
    CHECK(lineidx_build_start(x, &pool, &src) == -1);
    atomic_store_explicit(&s.resume, true, memory_order_release);
    lineidx_destroy(x);
#ifdef OBSERVE_ALLOCATIONS
    CHECK(__sanitizer_get_current_allocated_bytes() == before);
#endif
}

static void test_review_owned_source(void)
{
#ifdef OBSERVE_ALLOCATIONS
    size_t before = __sanitizer_get_current_allocated_bytes();
#endif
    uint8_t *b = malloc(LINEIDX_CHUNK);
    REQUIRE(b != NULL);
    memset(b, '\n', LINEIDX_CHUNK);
    lease s = { .b = b, .n = LINEIDX_CHUNK };
    lineidx_src src = { &s, s.n, lease_span, lease_release };
    lineidx *x = lineidx_create(s.n);
    REQUIRE(x != NULL);
    CHECK(lineidx_build_start_owned(x, &pool, &src, LINEIDX_CHUNK) == 0);
    REQUIRE(lease_entered(&s));
#ifdef OBSERVE_ALLOCATIONS
    CHECK(lineidx_mem_bytes(x) == __sanitizer_get_current_allocated_bytes() - before);
#endif
    lineidx_build_cancel(x);
    CHECK(!atomic_load(&s.released));
    atomic_store_explicit(&s.resume, true, memory_order_release);
    lineidx_destroy(x);
    CHECK(atomic_load(&s.released));
#ifdef OBSERVE_ALLOCATIONS
    CHECK(__sanitizer_get_current_allocated_bytes() == before);
#endif

    s = (lease){ .b = malloc(LINEIDX_CHUNK), .n = LINEIDX_CHUNK };
    REQUIRE(s.b != NULL);
    memset((void *)s.b, '\n', LINEIDX_CHUNK);
    src.ctx = &s;
    x = lineidx_create(s.n);
    REQUIRE(x != NULL);
    CHECK(lineidx_build_start(x, &pool, &src) == 0);
    REQUIRE(lease_entered(&s));
    CHECK(lineidx_mem_bytes(x) == SIZE_MAX); /* unknown owned storage cannot pass a guard */
    lineidx_build_cancel(x);
    atomic_store_explicit(&s.resume, true, memory_order_release);
    lineidx_destroy(x);
}

static void count_message(const work_msg *m, void *ctx)
{
    (void)m;
    size_t *n = ctx;
    (*n)++;
}

/* Cancelled results must reach the UI only through work validation. */
static void test_review_mailbox(void)
{
    uint8_t b[LINEIDX_CHUNK];
    memset(b, '\n', sizeof b);
    lease s = { .b = b, .n = sizeof b };
    lineidx_src src = { &s, s.n, lease_span, NULL };
    lineidx *x = lineidx_create(s.n);
    REQUIRE(x != NULL);
    CHECK(lineidx_build_start(x, &pool, &src) == 0);
    REQUIRE(lease_entered(&s));
    work_handle h = { 0, atomic_load(&pool.slots[0].epoch) };
    work_cancel(&pool, h); /* pool rejects all messages from this lease */
    atomic_store_explicit(&s.resume, true, memory_order_release);
    for (unsigned i = 0; i < 20000 && lineidx_building(x); i++)
        nanosleep(&(struct timespec){0, 1000000}, NULL);
    size_t delivered = 0;
    (void)work_mailbox_drain(&pool, count_message, &delivered);
    CHECK(delivered == 0);
    CHECK(lineidx_poll(x) == 0);
    CHECK(!lineidx_complete(x));
    lineidx_destroy(x);
}

static bool wait_complete(lineidx *x, int ms)
{
    for (int i = 0; i < ms; i++) {
        lineidx_poll(x);
        size_t delivered = 0;
        (void)work_mailbox_drain(&pool, count_message, &delivered);
        if (lineidx_complete(x)) return true;
        struct timespec ts = { 0, 1000000 };
        nanosleep(&ts, NULL);
    }
    return false;
}

static void test_review_wide_count(void)
{
    synthetic s = { .n = 1ull << 32 };
    memset(s.b, '\n', sizeof s.b);
    lineidx_src src = { &s, s.n, synthetic_span, NULL };
    lineidx *x = lineidx_create(s.n);
    REQUIRE(x != NULL);
    CHECK(lineidx_build_start(x, &pool, &src) == 0);
    CHECK(wait_complete(x, 20000));
    CHECK(lineidx_line_count(x).exact && lineidx_line_count(x).value == (1ull << 32) + 1u);
    CHECK(!lineidx_any_nonascii(x));
    CHECK(lineidx_line_to_byte(x, &src, 1).value == 1);
    CHECK(lineidx_line_to_byte(x, &src, (1ull << 32) - 1u).value == (1ull << 32) - 1u);
    lineidx_destroy(x);
}

static void check_queries(lineidx *x, const uint8_t *b, uint64_t n, int samples)
{
    flat f = { b, n, 7000, 0 };
    lineidx_src s = mk(&f);
    uint64_t lc = n_lines(b, n);
    lineidx_result r = lineidx_line_count(x);
    CHECK(r.exact && r.value == lc);
    uint64_t probes[] = { 0, 1, 2, lc - 1, lc, lc + 5 };
    for (size_t k = 0; k < sizeof probes / sizeof probes[0] + (size_t)samples; k++) {
        uint64_t ln = k < sizeof probes / sizeof probes[0] ? probes[k] : rnd() % (lc + 2);
        lineidx_result q = lineidx_line_to_byte(x, &s, ln);
        if (!(q.exact && q.value == n_l2b(b, n, ln))) { fprintf(stderr, "l2b line=%llu got %llu want %llu\n", (unsigned long long)ln, (unsigned long long)q.value, (unsigned long long)n_l2b(b, n, ln)); fails++; }
        uint64_t off = k < 4 ? (k == 0 ? 0 : k == 1 ? n : k == 2 ? n / 2 : (n ? n - 1 : 0)) : rnd() % (n + 2);
        q = lineidx_byte_to_line(x, &s, off);
        if (!(q.exact && q.value == n_b2l(b, n, off))) { fprintf(stderr, "b2l off=%llu got %llu want %llu\n", (unsigned long long)off, (unsigned long long)q.value, (unsigned long long)n_b2l(b, n, off)); fails++; }
    }
}

static uint8_t *mkbuf(uint64_t n, int style)
{
    uint8_t *b = malloc(n ? n : 1);
    for (uint64_t i = 0; i < n; i++) {
        uint64_t r = rnd();
        switch (style) {
        case 0: b[i] = (r % 40 == 0) ? '\n' : (uint8_t)('a' + r % 26); break;           /* LF */
        case 1: b[i] = (i % 41 == 40) ? '\n' : (i % 41 == 39) ? '\r' : 'x'; break;   /* CRLF */
        case 2: b[i] = (uint8_t)r; break;                                              /* random bytes */
        case 3: b[i] = 'z'; break;                                                     /* one long line */
        default: b[i] = '\n'; break;                                                   /* all newlines */
        }
    }
    return b;
}

static void test_shapes(void)
{
    static const uint64_t sizes[] = { 0, 1, 2, 65535, 65536, 65537, 131072, 3 * 65536 + 17, 400000 };
    for (int style = 0; style < 5; style++)
        for (size_t k = 0; k < sizeof sizes / sizeof sizes[0]; k++) {
            uint64_t n = sizes[k];
            uint8_t *b = mkbuf(n, style);
            if (style == 0 && n > 2) b[n - 1] = 'q';          /* no trailing newline */
            lineidx *x = lineidx_create(n);
            REQUIRE(x != NULL);
            CHECK(lineidx_len(x) == n);
            flat f = { b, n, 0, 0 };
            lineidx_src s = mk(&f);
            if (n > 0) {
                lineidx_result r = lineidx_line_count(x);
                CHECK(!r.exact);                       /* before build: estimate */
                CHECK(!lineidx_complete(x));
            }
            CHECK(lineidx_build_start(x, &pool, &s) == 0);
            CHECK(wait_complete(x, 20000));
            check_queries(x, b, n, 60);
            bool na = false;
            for (uint64_t i = 0; i < n; i++) if (b[i] >= 0x80) na = true;
            CHECK(lineidx_any_nonascii(x) == na);
            lineidx_destroy(x);
            free(b);
        }
}

static void test_estimate_and_cancel_resume(void)
{
    uint64_t n = 24u << 20;                 /* 24 MiB = 384 chunks */
    uint8_t *b = mkbuf(n, 0);
    lineidx *x = lineidx_create(n);
    REQUIRE(x != NULL);
    flat slow = { b, n, 0, 50 };            /* 50 us per span call: ~20 ms total */
    lineidx_src s = mk(&slow);
    CHECK(lineidx_build_start(x, &pool, &s) == 0);
    struct timespec ts = { 0, 5000000 };
    nanosleep(&ts, NULL);
    lineidx_build_cancel(x);
    for (int i = 0; i < 200 && lineidx_building(x); i++) nanosleep(&(struct timespec){0, 1000000}, NULL);
    CHECK(!lineidx_building(x));
    lineidx_poll(x);
    size_t pre = lineidx_built_prefix(x);
    fprintf(stderr, "  cancel left prefix %zu of %zu chunks\n", pre, lineidx_chunk_count(x));
    flat fast = { b, n, 9000, 0 };
    lineidx_src fs = mk(&fast);
    uint64_t lc = n_lines(b, n);
    if (pre < lineidx_chunk_count(x)) {
        lineidx_result r = lineidx_line_count(x);
        CHECK(!r.exact);
        CHECK(r.value > lc / 2 && r.value < lc * 2);    /* byte-proportion estimate is sane */
    }
    if (pre > 0) {
        /* a line inside the built prefix is exact; one past it is flagged estimate */
        uint64_t inside = n_b2l(b, n, (uint64_t)pre * LINEIDX_CHUNK) / 2;
        lineidx_result q = lineidx_line_to_byte(x, &fs, inside);
        CHECK(q.exact && q.value == n_l2b(b, n, inside));
        q = lineidx_byte_to_line(x, &fs, (uint64_t)pre * LINEIDX_CHUNK - 3);
        CHECK(q.exact && q.value == n_b2l(b, n, (uint64_t)pre * LINEIDX_CHUNK - 3));
    }
    if (pre < lineidx_chunk_count(x)) {
        lineidx_result q = lineidx_line_to_byte(x, &fs, lc - 2);
        CHECK(!q.exact);
        CHECK(q.value <= n);
        CHECK(q.value == 0 || b[q.value - 1] == '\n');  /* estimate still lands on a line start */
    }
    /* resume with a fast source; completes and is exact */
    CHECK(lineidx_build_start(x, &pool, &fs) == 0);
    CHECK(wait_complete(x, 20000));
    fprintf(stderr, "  resumed: lines %llu want %llu\n", (unsigned long long)lineidx_line_count(x).value, (unsigned long long)lc);
    check_queries(x, b, n, 200);
    lineidx_destroy(x);
    free(b);
}

static void test_seek_partial(void)
{
    uint64_t n = 8u << 20;
    uint8_t *b = mkbuf(n, 0);
    lineidx *x = lineidx_create(n);
    REQUIRE(x != NULL);
    flat f = { b, n, 0, 0 };
    lineidx_src s = mk(&f);
    uint64_t lc = n_lines(b, n);
    uint64_t target = lc / 10 * 9;
    lineidx_result r = lineidx_seek_line(x, &s, target, 1u << 20);   /* budget too small */
    CHECK(!r.exact);
    CHECK(r.value == 0 || b[r.value - 1] == '\n');
    r = lineidx_seek_line(x, &s, target, n);                          /* enough */
    CHECK(r.exact && r.value == n_l2b(b, n, target));
    CHECK(lineidx_built_prefix(x) > 0);
    /* the recorded prefix now answers exactly with no scan budget */
    r = lineidx_seek_line(x, &s, target, 0);
    CHECK(r.exact && r.value == n_l2b(b, n, target));
    /* a build over the same index still completes and agrees */
    CHECK(lineidx_build_start(x, &pool, &s) == 0);
    CHECK(wait_complete(x, 20000));
    check_queries(x, b, n, 100);
    lineidx_destroy(x);
    free(b);
}

static void model_edit(uint8_t **b, uint64_t *n, uint64_t off, uint64_t del, const uint8_t *ins, uint64_t il)
{
    uint8_t *nb = malloc(*n - del + il + 1);
    memcpy(nb, *b, off);
    if (il) memcpy(nb + off, ins, il);
    memcpy(nb + off + il, *b + off + del, *n - off - del);
    free(*b);
    *b = nb;
    *n = *n - del + il;
}

static void test_edits(void)
{
    uint64_t n = 5 * 65536 + 123;
    uint8_t *b = mkbuf(n, 0);
    lineidx *x = lineidx_create(n);
    REQUIRE(x != NULL);
    flat f = { b, n, 0, 0 };
    lineidx_src s = mk(&f);
    CHECK(lineidx_build_start(x, &pool, &s) == 0);
    CHECK(wait_complete(x, 20000));
    CHECK(lineidx_complete(x));
    for (int it = 0; it < 400; it++) {
        uint64_t off, del;
        int mode = it % 8;
        if (mode == 0) off = 65536 * (rnd() % 6);               /* exact chunk boundary */
        else if (mode == 1) off = 65536 * (1 + rnd() % 5) - 1;
        else if (mode == 2) off = n;
        else off = n ? rnd() % (n + 1) : 0;
        if (off > n) off = n;
        del = (mode == 3) ? 70000 : (mode == 4) ? rnd() % 300000 : rnd() % 50;
        if (del > n - off) del = n - off;
        uint64_t il = (mode == 5) ? 200000 : rnd() % 90;
        uint8_t *ins = mkbuf(il, (int)(rnd() % 3));
        uint64_t oldn = n;
        CHECK(lineidx_edit(x, off, del, il) == 0);
        model_edit(&b, &n, off, del, ins, il);
        free(ins);
        CHECK(lineidx_len(x) == n);
        CHECK(oldn - del + il == n);
        flat g = { b, n, 11000, 0 };
        lineidx_src gs = mk(&g);
        if (it % 3 == 0 && !lineidx_complete(x)) {
            /* before refresh the edited chunk makes counts past it an estimate */
            lineidx_result r = lineidx_line_count(x);
            CHECK(!r.exact || n == 0);
        }
        while (lineidx_refresh(x, &gs)) { }
        CHECK(lineidx_complete(x));
        check_queries(x, b, n, 12);
    }
    /* delete everything, then grow from empty */
    flat g0 = { b, 0, 0, 0 };
    lineidx_src g0s = mk(&g0);
    CHECK(lineidx_edit(x, 0, n, 0) == 0);
    n = 0;
    lineidx_refresh(x, &g0s);
    CHECK(lineidx_complete(x));
    CHECK(lineidx_line_count(x).value == 1);
    CHECK(lineidx_edit(x, 1, 0, 0) != 0);                  /* out of range */
    lineidx_destroy(x);
    free(b);
}

static void check_snapshot_edit(uint64_t off, uint64_t del, uint64_t il)
{
    uint64_t n = 16u << 20;
    uint8_t *b = mkbuf(n, 0);
    lineidx *x = lineidx_create(n);
    REQUIRE(x != NULL);
    uint8_t *copy = malloc(n);
    REQUIRE(copy != NULL);
    memcpy(copy, b, n);
    uint64_t old_lines = n_lines(copy, n);
    lease slow = { .b = copy, .n = n };
    lineidx_src ss = { &slow, n, lease_span, lease_release };
    CHECK(lineidx_build_start_owned(x, &pool, &ss, (size_t)n) == 0);
    REQUIRE(lease_entered(&slow));
    uint8_t ins[100]; memset(ins, '\n', sizeof ins);
    lineidx_poll(x);
    CHECK(lineidx_edit(x, off, del, il) == 0);
    CHECK(!atomic_load(&slow.released));
    model_edit(&b, &n, off, del, ins, il);
    CHECK(copy != b && n_lines(copy, ss.len) == old_lines);
    atomic_store_explicit(&slow.resume, true, memory_order_release);
    for (unsigned i = 0; i < 20000 && lineidx_building(x); i++)
        nanosleep(&(struct timespec){0, 1000000}, NULL);
    CHECK(!lineidx_building(x));
    CHECK(atomic_load(&slow.released));
    flat cur = { b, n, 0, 0 };
    lineidx_src cs = mk(&cur);
    lineidx_refresh(x, &cs);
    CHECK(lineidx_build_start(x, &pool, &cs) == 0);            /* resume over the new content */
    CHECK(wait_complete(x, 20000));
    check_queries(x, b, n, 200);
    lineidx_destroy(x);
    free(b);
}

static void test_edit_during_build(void)
{
    check_snapshot_edit(5u << 20, 10, 100);
    check_snapshot_edit(LINEIDX_CHUNK - 1u, 100, 100);
    check_snapshot_edit(LINEIDX_CHUNK, LINEIDX_CHUNK + 17u, 50);
    check_snapshot_edit(0, 0, 100);
}

static void test_no_malloc(void)
{
    uint64_t n = 4u << 20;
    uint8_t *b = mkbuf(n, 0);
    lineidx *x = lineidx_create_reserved(n, 256); /* reserve this test's typing burst */
    REQUIRE(x != NULL);
    flat f = { b, n, 0, 0 };
    lineidx_src s = mk(&f);
    CHECK(lineidx_build_start(x, &pool, &s) == 0);
    CHECK(wait_complete(x, 20000));
    uint8_t ins[64]; memset(ins, 'a', sizeof ins);
    edit_malloc_guard_begin();
    volatile uint64_t sink = 0;
    for (int i = 0; i < 200; i++) {
        uint64_t off = rnd() % (n + 1);
        sink += lineidx_line_to_byte(x, &s, rnd() % 50000).value;
        sink += lineidx_byte_to_line(x, &s, off).value;
        sink += lineidx_line_count(x).value;
        sink += lineidx_seek_line(x, &s, rnd() % 50000, 1u << 20).value;
        CHECK(lineidx_edit(x, off, 0, sizeof ins) == 0);
        model_edit(&b, &n, off, 0, ins, sizeof ins);   /* model malloc is counted: compensate below */
        f.b = b; f.n = n; s.len = n;
        while (lineidx_refresh(x, &s)) { }
    }
    size_t mallocs = edit_malloc_guard_end();
    /* model_edit mallocs once per iteration; the index itself must add none */
    if (edit_malloc_guard_active()) CHECK(mallocs == 200);
    (void)sink;
    lineidx_destroy(x);
    free(b);
}

static void test_memory(void)
{
    CHECK(lineidx_entry_bytes() == 16);
    uint64_t n = 1ull << 30;                          /* 1 GiB, never touched */
    lineidx *x = lineidx_create(n);
    REQUIRE(x != NULL);
    size_t chunks = lineidx_chunk_count(x);
    CHECK(chunks == (size_t)(n / LINEIDX_CHUNK));
    size_t mem = lineidx_mem_bytes(x);
    fprintf(stderr, "  1 GiB index: %zu chunks, %zu B live (%.3f B/chunk)\n", chunks, mem, (double)mem / (double)chunks);
    CHECK(mem <= chunks * 16 + 4096);
    lineidx_destroy(x);
}

/* ---- piece_snapshot as the source ---- */
typedef struct { const piece_snapshot *s; } psrc;
static size_t psnap_span(void *ctx, uint64_t off, const uint8_t **p)
{
    psrc *ps = ctx;
    piece_iter it;
    size_t k;
    if (off >= piece_snapshot_len(ps->s)) return 0;
    piece_iter_begin_snapshot(&it, ps->s, off);
    if (!piece_iter_next(&it, p, &k)) return 0;
    return k;
}
static void test_piece_source(void)
{
    uint64_t n = 300000;
    uint8_t *b = mkbuf(n, 0);
    piece_allocator a = piece_default_allocator();
    piece_tree *t = piece_create(&a);
    REQUIRE(t != NULL);
    CHECK(piece_init_copy(t, b, n) == 0);
    uint8_t ins[50]; memset(ins, '\n', sizeof ins);
    CHECK(piece_insert(t, 100000, ins, sizeof ins) == 0);
    model_edit(&b, &n, 100000, 0, ins, sizeof ins);
    CHECK(piece_delete(t, 5, 70000, NULL) == 0);
    model_edit(&b, &n, 5, 70000, NULL, 0);
    piece_snapshot *snap = piece_snapshot_take(t);
    REQUIRE(snap != NULL);
    psrc ps = { snap };
    lineidx_src s = { &ps, piece_snapshot_len(snap), psnap_span, NULL };
    CHECK(s.len == n);
    lineidx *x = lineidx_create(n);
    REQUIRE(x != NULL);
    CHECK(lineidx_build_start(x, &pool, &s) == 0);
    CHECK(wait_complete(x, 20000));
    CHECK(lineidx_line_count(x).value == piece_snapshot_line_count(snap));
    CHECK(lineidx_line_to_byte(x, &s, 1234).value == piece_snapshot_line_to_byte(snap, 1234));
    CHECK(lineidx_byte_to_line(x, &s, 150001).value == piece_snapshot_byte_to_line(snap, 150001));
    check_queries(x, b, n, 30);
    lineidx_destroy(x);
    piece_snapshot_release(snap);
    piece_destroy(t);
    free(b);
}

static void test_corpus(const char *path)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0) { fprintf(stderr, "  skip %s\n", path); return; }
    struct stat st;
    REQUIRE(fstat(fd, &st) == 0);
    uint64_t n = (uint64_t)st.st_size;
    if (n == 0) { close(fd); return; }
    const uint8_t *m = mmap(NULL, (size_t)n, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    REQUIRE(m != MAP_FAILED);
    lineidx *x = lineidx_create(n);
    REQUIRE(x != NULL);
    flat f = { m, n, 0, 0 };
    lineidx_src s = mk(&f);
    CHECK(lineidx_build_start(x, &pool, &s) == 0);
    CHECK(wait_complete(x, 120000));
    uint64_t lc = n_lines(m, n);
    CHECK(lineidx_line_count(x).exact && lineidx_line_count(x).value == lc);
    for (int i = 0; i < 40; i++) {
        uint64_t ln = rnd() % lc, off = rnd() % n;
        lineidx_result q = lineidx_line_to_byte(x, &s, ln);
        CHECK(q.exact && q.value == n_l2b(m, n, ln));
        q = lineidx_byte_to_line(x, &s, off);
        CHECK(q.exact && q.value == n_b2l(m, n, off));
        if (n > (1u << 28)) break;                 /* naive model is O(n): few probes for the big files */
    }
    lineidx_destroy(x);
    munmap((void *)m, (size_t)n);
}

static void test_mailbox_pressure(void);
static void test_completed_slot_reuse(void);
int main(int argc, char **argv)
{
    trace_init();
    CHECK(work_pool_init(&pool, 1, 0) == 0);
    if (argc == 2) {
        if (strcmp(argv[1], "--review=1") == 0) test_review_capacity();
        else if (strcmp(argv[1], "--review=2") == 0) test_review_overflow();
        else if (strcmp(argv[1], "--review=3") == 0) test_review_epoch();
        else if (strcmp(argv[1], "--review=4") == 0) test_edit_during_build();
        else if (strcmp(argv[1], "--review=5") == 0) test_review_boundaries();
        else if (strcmp(argv[1], "--review=12") == 0) { test_review_memory(); test_review_owned_source(); }
        else if (strcmp(argv[1], "--review=13") == 0) {
            test_review_mailbox(); test_mailbox_pressure(); test_completed_slot_reuse();
        }
        else if (strcmp(argv[1], "--review=alloc") == 0) test_no_malloc();
        else return 2;
        goto finish;
    }
    fprintf(stderr, "-- review_capacity\n"); test_review_capacity();
    fprintf(stderr, "-- review_overflow\n"); test_review_overflow();
    fprintf(stderr, "-- review_epoch\n"); test_review_epoch();
    fprintf(stderr, "-- review_boundaries\n"); test_review_boundaries();
    fprintf(stderr, "-- review_memory\n"); test_review_memory();
    fprintf(stderr, "-- review_owned_source\n"); test_review_owned_source();
    fprintf(stderr, "-- review_mailbox\n"); test_review_mailbox();
    fprintf(stderr, "-- mailbox_pressure\n"); test_mailbox_pressure();
    fprintf(stderr, "-- completed_slot_reuse\n"); test_completed_slot_reuse();
    fprintf(stderr, "-- review_wide_count\n"); test_review_wide_count();
    fprintf(stderr, "-- shapes\n"); test_shapes();
    fprintf(stderr, "-- estimate_and_cancel_resume\n"); test_estimate_and_cancel_resume();
    fprintf(stderr, "-- seek_partial\n"); test_seek_partial();
    fprintf(stderr, "-- edits\n"); test_edits();
    fprintf(stderr, "-- edit_during_build\n"); test_edit_during_build();
    fprintf(stderr, "-- no_malloc\n"); test_no_malloc();
    fprintf(stderr, "-- memory\n"); test_memory();
    fprintf(stderr, "-- piece_source\n"); test_piece_source();
    fprintf(stderr, "-- corpus\n"); test_corpus("/tmp/edit-corpus/crlf.txt");
    fprintf(stderr, "-- corpus\n"); test_corpus("/tmp/edit-corpus/unicode.txt");
    fprintf(stderr, "-- corpus\n"); test_corpus("/tmp/edit-corpus/malformed.txt");
    fprintf(stderr, "-- corpus\n"); test_corpus("/tmp/edit-corpus/oneline_1g.txt");
finish:
    work_pool_shutdown(&pool);
    if (fails) { fprintf(stderr, "lineidx_test: %d failure(s)\n", fails); return 1; }
    puts("lineidx_test: ok");
    return 0;
}

static void foreign_mailbox_job(work_ctx *c)
{
    for (uint32_t i = 0; i < WORK_MAILBOX_CAP; i++) {
        work_msg m = {.kind = 99, .generation = c->generation};
        memcpy(m.data, &i, sizeof i);
        CHECK(work_publish(c, &m));
    }
}
static void foreign_message(const work_msg *m, void *arg)
{
    size_t *count = arg;
    uint32_t ordinal;
    memcpy(&ordinal, m->data, sizeof ordinal);
    CHECK(m->kind == 99 && m->generation == 81 && ordinal == *count);
    (*count)++;
}
static void test_mailbox_pressure(void)
{
    work_pool wp;
    REQUIRE(work_pool_init(&wp, 1, 0) == 0);
    work_handle foreign = work_submit(&wp, (work_job){foreign_mailbox_job, NULL, 81, WORK_BULK});
    REQUIRE(foreign.epoch != 0);
    for (unsigned i = 0; i < 20000 && !work_handle_finished(&wp, foreign); i++)
        nanosleep(&(struct timespec){0, 1000000}, NULL);
    CHECK(work_handle_finished(&wp, foreign));
    uint8_t b[LINEIDX_CHUNK]; memset(b, '\n', sizeof b);
    lease s = {.b = b, .n = 33ull * LINEIDX_CHUNK, .repeat = true, .resume = true};
    lineidx_src src = {&s, s.n, lease_span, NULL};
    lineidx *x = lineidx_create(s.n);
    REQUIRE(x != NULL);
    REQUIRE(lineidx_build_start(x, &wp, &src) == 0);
    for (unsigned i = 0; i < 20000 && atomic_load(&wp.dropped_full) == 0; i++)
        nanosleep(&(struct timespec){0, 1000000}, NULL);
    CHECK(atomic_load(&wp.dropped_full) != 0);
    CHECK(lineidx_poll(x) == 0);
    CHECK(atomic_load(&wp.slots[foreign.slot].pending) == WORK_MAILBOX_CAP);
    size_t foreign_count = 0;
    bool completed = false;
    for (unsigned i = 0; i < 20000; i++) {
        /* Existing callers may pump the shared pool before their first poll. */
        (void)work_mailbox_drain(&wp, foreign_message, &foreign_count);
        (void)lineidx_poll(x);
        if (lineidx_complete(x)) { completed = true; break; }
        nanosleep(&(struct timespec){0, 1000000}, NULL);
    }
    CHECK(completed && foreign_count == WORK_MAILBOX_CAP);
    CHECK(lineidx_line_count(x).exact && lineidx_line_count(x).value == s.n + 1u);
    lineidx_destroy(x);
    work_pool_shutdown(&wp);
    fprintf(stderr, "lineidx mailbox: full-ring retry + foreign FIFO + shared dispatcher ok\n");
}
static void hold_replacement(work_ctx *c)
{
    while (!work_should_stop(c)) sched_yield();
}
static void test_completed_slot_reuse(void)
{
    work_pool wp;
    REQUIRE(work_pool_init(&wp, 1, 0) == 0);
    uint8_t b[] = "a\nb\n";
    flat f = {.b = b, .n = sizeof b - 1};
    lineidx_src src = mk(&f);
    lineidx *x = lineidx_create(src.len);
    REQUIRE(x != NULL);
    REQUIRE(lineidx_build_start(x, &wp, &src) == 0);
    work_handle completed = {0, 2};
    for (unsigned i = 0; i < 20000 && !work_handle_finished(&wp, completed); i++)
        nanosleep(&(struct timespec){0, 1000000}, NULL);
    CHECK(work_handle_finished(&wp, completed));
    size_t stolen = 0;
    while (work_mailbox_pending(&wp)) (void)work_mailbox_drain(&wp, count_message, &stolen);
    CHECK(stolen == 0); /* the bound receiver, rather than fallback, adopted */
    work_handle replacement = work_submit(&wp, (work_job){hold_replacement, NULL, 82, WORK_BULK});
    REQUIRE(replacement.epoch != 0);
    CHECK(replacement.slot == completed.slot && replacement.epoch == completed.epoch + 1u);
    CHECK(lineidx_poll(x) == 1);
    CHECK(lineidx_complete(x) && lineidx_line_count(x).value == 3);
    CHECK(!lineidx_building(x));
    lineidx_destroy(x);
    work_cancel(&wp, replacement);
    work_pool_shutdown(&wp);
    fprintf(stderr, "lineidx mailbox: completed lease survives slot reuse ok\n");
}
