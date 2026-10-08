/* trace_test.c - records a synthetic sequence from 2 threads, dumps, reloads
 * through trace_fmt.h and checks the gate percentiles against known values. */
#include "../src/trace/trace.h"
#include "../src/trace/trace_fmt.h"

#include <pthread.h>
#include <stdio.h>

#define NFRAMES 100u
#define BASE_NS 1000000000ull

static int g_fail;

#define CHECK(cond, ...)                                  \
    do {                                                  \
        if (!(cond)) {                                    \
            fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__); \
            fprintf(stderr, __VA_ARGS__);                 \
            fputc('\n', stderr);                          \
            g_fail = 1;                                   \
        }                                                 \
    } while (0)

/* Thread A: ingress T0 and device-done T5. Thread B: present-submitted T4 and
 * present-complete T6. Frame f: T0 = BASE + f*1e6, T4 = T0 + f*1000,
 * T5 = T0 + 2f*1000, T6 = T0 + 3f*1000. Known percentiles over f = 1..100:
 * G1 (T4-T0) p50 50000 p99 99000; G3 (T5-T0) p50 100000 p99 198000;
 * T6-T0 p50 150000 p99 297000. */
static void *thread_a(void *arg) {
    (void)arg;
    trace_thread_register();
    for (uint32_t f = 1; f <= NFRAMES; f++) {
        uint64_t t0 = BASE_NS + (uint64_t)f * 1000000u;
        trace_record_at(t0, TRACE_T0_INGRESS, f);
        trace_record_at(t0 + (uint64_t)f * 2000u, TRACE_T5_DEVICE_DONE, f);
    }
    return NULL;
}

static void *thread_b(void *arg) {
    (void)arg;
    trace_thread_register();
    for (uint32_t f = 1; f <= NFRAMES; f++) {
        uint64_t t0 = BASE_NS + (uint64_t)f * 1000000u;
        trace_record_at(t0 + (uint64_t)f * 1000u, TRACE_T4_PRESENT_SUBMITTED, f);
        trace_record_at(t0 + (uint64_t)f * 3000u, TRACE_T6_PRESENT_COMPLETE, f);
    }
    return NULL;
}

static void check_gate(const trace_rec *recs, size_t n, enum trace_ev from, enum trace_ev to,
                       const char *name, uint64_t want50, uint64_t want99) {
    uint64_t *lat = NULL;
    size_t nlat = 0;
    uint64_t p50, p99;
    CHECK(trace_fmt_latencies(recs, n, from, to, &lat, &nlat) == 0, "latencies %s", name);
    CHECK(nlat == NFRAMES, "%s n=%zu want %u", name, nlat, NFRAMES);
    p50 = trace_fmt_pct(lat, nlat, 50);
    p99 = trace_fmt_pct(lat, nlat, 99);
    CHECK(p50 == want50, "%s p50=%llu want %llu", name, (unsigned long long)p50, (unsigned long long)want50);
    CHECK(p99 == want99, "%s p99=%llu want %llu", name, (unsigned long long)p99, (unsigned long long)want99);
    printf("%-14s n=%zu p50=%llu ns p99=%llu ns\n", name, nlat,
           (unsigned long long)p50, (unsigned long long)p99);
    free(lat);
}

int main(void) {
    pthread_t a, b;
    FILE *f;
    trace_rec *recs = NULL;
    size_t n = 0;

    trace_init();
    CHECK(pthread_create(&a, NULL, thread_a, NULL) == 0, "create a");
    CHECK(pthread_create(&b, NULL, thread_b, NULL) == 0, "create b");
    pthread_join(a, NULL);
    pthread_join(b, NULL);

    /* Exercise the real clock path on the main thread (not part of the gates). */
    trace_thread_register();
    trace_record(TRACE_T1_DEQUEUE, 999999u);

    f = tmpfile();
    CHECK(f != NULL, "tmpfile");
    CHECK(trace_dump(f) == 0, "trace_dump");
    rewind(f);
    CHECK(trace_fmt_load(f, &recs, &n) == 0, "reload");
    fclose(f);
    CHECK(n == 2u * NFRAMES * 2u + 1u, "record count %zu", n);

    check_gate(recs, n, TRACE_T0_INGRESS, TRACE_T4_PRESENT_SUBMITTED, "G1 T4-T0", 50000u, 99000u);
    check_gate(recs, n, TRACE_T0_INGRESS, TRACE_T5_DEVICE_DONE, "G3 T5-T0", 100000u, 198000u);
    check_gate(recs, n, TRACE_T0_INGRESS, TRACE_T6_PRESENT_COMPLETE, "T6-T0", 150000u, 297000u);

    {
        uint64_t t = trace_now_ns();
        CHECK(t > BASE_NS, "trace_now_ns not monotonic-looking");
    }

    free(recs);
    trace_reset();
    if (g_fail) {
        printf("trace_test: FAIL\n");
        return 1;
    }
    printf("trace_test: PASS\n");
    return 0;
}
