/* work_bench.c - G6c cancel->ack and submit->start latency (P1.8). */
#include "work/work.h"
#include "base/base.h"
#include "trace/trace.h"
#include "harness.h"

#include <sys/mman.h>
#include <time.h>

#define BUF_SZ (1ull << 30)
#define SLICE  (64u * 1024u)
#define TRIALS 1000u

static unsigned char *buf;
static _Atomic int started;
static _Atomic uint64_t ack_ns;      /* cancel->observed */
static _Atomic uint64_t sink;
static _Atomic uint64_t start_ns;

static void scan_job(work_ctx *c)
{
    uint64_t acc = 0;
    atomic_store(&started, 1);
    for (;;) {
        for (size_t off = 0; off + SLICE <= BUF_SZ; off += SLICE) {
            if (work_should_stop(c)) {
                uint64_t t = bench_now_ns(), t0 = work_cancel_time_ns(c);
                atomic_store(&ack_ns, t > t0 ? t - t0 : 0);
                atomic_store(&sink, acc);
                return;
            }
            const uint64_t *w = (const uint64_t *)(buf + off);
            for (size_t i = 0; i < SLICE / 8; i++) acc += w[i];
        }
    }
}

static void stamp_job(work_ctx *c) { (void)c; atomic_store(&start_ns, bench_now_ns()); }

static uint64_t rng = 88172645463325252ull;
static uint64_t xr(void) { rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17; return rng; }

int main(void)
{
    static work_pool pool;
    static uint64_t ackbuf[TRIALS], stbuf[TRIALS];
    bench_samples ack, st;
    int rc = 0;

    buf = mmap(NULL, BUF_SZ, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    EDIT_ASSERT(buf != MAP_FAILED);
    for (size_t i = 0; i < BUF_SZ; i += 4096) buf[i] = (unsigned char)i;
    bench_samples_init(&ack, ackbuf, TRIALS);
    bench_samples_init(&st, stbuf, TRIALS);
    trace_init();
    EDIT_ASSERT(work_pool_init(&pool, 1, 0) == 0);

    for (unsigned t = 0; t < TRIALS; t++) {
        atomic_store(&started, 0);
        atomic_store(&ack_ns, 0);
        work_handle h = work_submit(&pool, (work_job){ scan_job, NULL, 0, WORK_BULK });
        while (!atomic_load(&started)) {}
        struct timespec ts = { 0, (long)(xr() % 3000000u) };  /* 0..3 ms random point */
        nanosleep(&ts, NULL);
        work_cancel(&pool, h);
        while (atomic_load(&ack_ns) == 0) {}
        (void)bench_add(&ack, atomic_load(&ack_ns));
        /* wait for job to return so the slot frees */
        struct timespec w = { 0, 100000 };
        nanosleep(&w, NULL);
    }
    for (unsigned t = 0; t < TRIALS; t++) {
        struct timespec ts = { 0, 200000 };  /* let worker go idle */
        nanosleep(&ts, NULL);
        atomic_store(&start_ns, 0);
        uint64_t t0 = bench_now_ns();
        (void)work_submit(&pool, (work_job){ stamp_job, NULL, 0, WORK_BULK });
        uint64_t s;
        while ((s = atomic_load(&start_ns)) == 0) {}
        (void)bench_add(&st, s > t0 ? s - t0 : 0);
    }
    work_pool_shutdown(&pool);
    rc |= bench_report("work_cancel", &ack, 1000000, 5000000);
    rc |= bench_report("work_submit_start", &st, 0, 0);
    return rc;
}
