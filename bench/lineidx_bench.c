/* lineidx_bench: P1.6 gates G7 (index build, 1 GB warm), G7j (partial-index
 * jump), memory (16 B / 64 KiB), plus TRACK rows (cancel ack, queries, edit).
 * Usage: lineidx_bench [--file=PATH] [--reps=N] [--partial=F] */
#include "lineidx/lineidx.h"
#include "../bench/harness.h"

#include <fcntl.h>
#include <sched.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

typedef struct { const uint8_t *b; uint64_t n; } flat;
static size_t flat_span(void *ctx, uint64_t off, const uint8_t **p)
{
    const flat *f = ctx;
    if (off >= f->n) return 0;
    *p = f->b + off;
    return (size_t)(f->n - off);
}

static work_pool pool;
static const char *path = "/tmp/edit-corpus/log_1g.txt";

static const uint8_t *map_file(uint64_t *n)
{
    int fd = open(path, O_RDONLY);
    struct stat st;
    if (fd < 0 || fstat(fd, &st) != 0) return NULL;
    *n = (uint64_t)st.st_size;
    void *m = mmap(NULL, (size_t)*n, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    return m == MAP_FAILED ? NULL : m;
}

static void nap(long us) { nanosleep(&(struct timespec){0, us * 1000}, NULL); }

static bool wait_complete(lineidx *x)
{
    for (;;) {
        lineidx_poll(x);
        if (lineidx_complete(x)) return true;
        nap(50);
    }
}

int main(int argc, char **argv)
{
    int reps = 9;
    double partial = 0.5;
    int rc = 0;
    for (int i = 1; i < argc; i++) {
        if (strncmp(argv[i], "--file=", 7) == 0) path = argv[i] + 7;
        else if (strncmp(argv[i], "--reps=", 7) == 0) reps = atoi(argv[i] + 7);
        else if (strncmp(argv[i], "--partial=", 10) == 0) partial = atof(argv[i] + 10);
    }
    if (reps < 3 || reps > 200) reps = 9;
    char st[32];
    printf("# lineidx_bench file=%s reps=%d partial=%.2f power=%s\n", path, reps, partial, bench_battery_status(st, sizeof st));
    if (work_pool_init(&pool, 1, 0) != 0) { fprintf(stderr, "pool init failed\n"); return 2; }

    uint64_t n = 0;
    const uint8_t *m0 = map_file(&n);
    if (!m0) { fprintf(stderr, "cannot map %s\n", path); return 2; }

    /* Reference: full build once, derive line_count, target line and its byte. */
    lineidx *ref = lineidx_create(n);
    flat f0 = { m0, n };
    lineidx_src s0 = { &f0, n, flat_span, NULL };
    if (lineidx_build_start(ref, &pool, &s0) != 0) return 2;
    wait_complete(ref);
    uint64_t lc = lineidx_line_count(ref).value;
    uint64_t target = lc * 9 / 10;                /* floor(0.9 x line_count): line 10^7 does not exist in log_1g */
    uint64_t want = lineidx_line_to_byte(ref, &s0, target).value;
    size_t chunks = lineidx_chunk_count(ref);
    printf("# line_count=%llu target_line=%llu target_byte=%llu chunks=%zu\n",
           (unsigned long long)lc, (unsigned long long)target, (unsigned long long)want, chunks);
    lineidx_destroy(ref);
    munmap((void *)m0, (size_t)n);

    static uint64_t buf[256];
    bench_samples sm;

    /* G7: new mapping -> exact index (mmap + create + job + completion). */
    bench_samples_init(&sm, buf, 256);
    for (int r = 0; r < reps + 2; r++) {
        uint64_t t0 = bench_now_ns();
        const uint8_t *m = map_file(&n);
        flat f = { m, n };
        lineidx_src s = { &f, n, flat_span, NULL };
        lineidx *x = lineidx_create(n);
        if (lineidx_build_start(x, &pool, &s) != 0) return 2;
        wait_complete(x);
        uint64_t dt = bench_now_ns() - t0;
        if (lineidx_line_count(x).value != lc) { fprintf(stderr, "G7: wrong line count\n"); return 2; }
        if (r >= 2) bench_add(&sm, dt);            /* two warm-up reps (page cache) */
        lineidx_destroy(x);
        munmap((void *)m, (size_t)n);
    }
    rc |= bench_report("G7_index_1g_warm", &sm, 80000000ull, 125000000ull);

    /* G7 build job alone (mapping faulted outside the timer): TRACK. */
    {
        const uint8_t *m = map_file(&n);
        volatile uint8_t sink = 0;
        for (uint64_t o = 0; o < n; o += 4096) sink ^= m[o];
        bench_samples_init(&sm, buf, 256);
        for (int r = 0; r < reps; r++) {
            flat f = { m, n };
            lineidx_src s = { &f, n, flat_span, NULL };
            lineidx *x = lineidx_create(n);
            uint64_t t0 = bench_now_ns();
            if (lineidx_build_start(x, &pool, &s) != 0) return 2;
            wait_complete(x);
            bench_add(&sm, bench_now_ns() - t0);
            lineidx_destroy(x);
        }
        (void)bench_report("TRACK_build_prefaulted", &sm, 0, 0);
        munmap((void *)m, (size_t)n);
    }
    printf("TRACK G7_index_1g_cold: manual (drop_caches), not run here\n");

    /* G7j: partial index (build cancelled once `partial` of the chunks are applied),
     * then request line `target` -> byte offset of that line's start. Includes the
     * bounded exact scan from the built prefix to the target; excludes layout/render.
     * The mapping is new each rep (warm page cache, cold PTEs). */
    bench_samples_init(&sm, buf, 256);
    uint64_t exact_hits = 0;
    for (int r = 0; r < reps + 2; r++) {
        const uint8_t *m = map_file(&n);
        flat f = { m, n };
        lineidx_src s = { &f, n, flat_span, NULL };
        lineidx *x = lineidx_create(n);
        if (lineidx_build_start(x, &pool, &s) != 0) return 2;
        size_t goal = (size_t)((double)chunks * partial);
        while (lineidx_built_prefix(x) < goal) { lineidx_poll(x); nap(20); }
        lineidx_build_cancel(x);
        while (lineidx_building(x)) nap(20);
        size_t pre = lineidx_built_prefix(x);
        uint64_t t0 = bench_now_ns();
        lineidx_result q = lineidx_seek_line(x, &s, target, n);
        uint64_t dt = bench_now_ns() - t0;
        if (!q.exact || q.value != want) { fprintf(stderr, "G7j: wrong answer %llu want %llu exact=%d\n", (unsigned long long)q.value, (unsigned long long)want, q.exact); return 2; }
        exact_hits++;
        if (r >= 2) bench_add(&sm, dt);
        if (r == 2) printf("# G7j prefix at cancel = %zu of %zu chunks\n", pre, chunks);
        lineidx_destroy(x);
        munmap((void *)m, (size_t)n);
    }
    rc |= bench_report("G7j_partial_jump_90pct", &sm, 30000000ull, 50000000ull);
    (void)exact_hits;

    /* Cancel acknowledgement of a running 1 GB build: TRACK (binding G6c bench is P1.8's). */
    {
        const uint8_t *m = map_file(&n);
        bench_samples_init(&sm, buf, 256);
        for (int r = 0; r < reps; r++) {
            flat f = { m, n };
            lineidx_src s = { &f, n, flat_span, NULL };
            lineidx *x = lineidx_create(n);
            if (lineidx_build_start(x, &pool, &s) != 0) return 2;
            nap(10000);
            uint64_t t0 = bench_now_ns();
            lineidx_build_cancel(x);
            while (lineidx_building(x)) sched_yield();
            bench_add(&sm, bench_now_ns() - t0);
            lineidx_destroy(x);
        }
        (void)bench_report("TRACK_cancel_ack_gate_1ms_5ms", &sm, 0, 0);
        munmap((void *)m, (size_t)n);
    }

    /* Queries and edits on a complete 1 GB index (mapping resident): TRACK. */
    {
        const uint8_t *m = map_file(&n);
        flat f = { m, n };
        lineidx_src s = { &f, n, flat_span, NULL };
        lineidx *x = lineidx_create(n);
        if (lineidx_build_start(x, &pool, &s) != 0) return 2;
        wait_complete(x);
        uint64_t rs = 88172645463325252ull;
        volatile uint64_t sink = 0;
        bench_samples_init(&sm, buf, 256);
        for (int r = 0; r < 200; r++) {
            rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17;
            uint64_t ln = rs % lc;
            BENCH_TIME(&sm, sink += lineidx_line_to_byte(x, &s, ln).value);
        }
        (void)bench_report("TRACK_line_to_byte_exact", &sm, 0, 0);
        bench_samples_init(&sm, buf, 256);
        for (int r = 0; r < 200; r++) {
            rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17;
            uint64_t off = rs % n;
            BENCH_TIME(&sm, sink += lineidx_byte_to_line(x, &s, off).value);
        }
        (void)bench_report("TRACK_byte_to_line_exact", &sm, 0, 0);
        /* typing: edit + refresh on a flat copy would need a mutable source; the
         * refresh rescan is one chunk, so time it on the unchanged mapping by
         * invalidating a chunk with a zero-length replace. */
        bench_samples_init(&sm, buf, 256);
        for (int r = 0; r < 200; r++) {
            rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17;
            uint64_t off = rs % n;
            BENCH_TIME(&sm, { (void)lineidx_edit(x, off, 0, 0); (void)lineidx_refresh(x, &s); });
        }
        (void)bench_report("TRACK_edit_refresh_1chunk", &sm, 0, 0);
        (void)sink;
        /* G10f memory: 16 B per 64 KiB chunk (+ summaries). */
        size_t mem = lineidx_mem_bytes(x);
        double per = (double)mem / (double)lineidx_chunk_count(x);
        int miss = per > 16.5;
        printf("BENCH name=mem_bytes_per_chunk value=%.3f gate=16.5 pass=%d (%zu B live, %zu chunks)\n", per, miss ? 0 : 1, mem, lineidx_chunk_count(x));
        rc |= miss;
        lineidx_destroy(x);
        munmap((void *)m, (size_t)n);
    }

    work_pool_shutdown(&pool);
    printf("lineidx_bench: %s\n", rc ? "MISS" : "all gates met");
    return rc ? 1 : 0;
}
