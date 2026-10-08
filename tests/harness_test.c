/* tests/harness_test.c - checks for bench/harness.h (P0.4). */
#include "../bench/harness.h"

#include <stdlib.h>

static int failures = 0;
static int checks = 0;

#define CHECK(cond)                                                          \
    do {                                                                     \
        checks++;                                                            \
        if (!(cond)) {                                                       \
            failures++;                                                      \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);  \
        }                                                                    \
    } while (0)

static void fill(bench_samples *s, uint64_t *buf, size_t cap, const uint64_t *vals, size_t n)
{
    size_t i;
    bench_samples_init(s, buf, cap);
    for (i = 0; i < n; i++)
        CHECK(bench_add(s, vals[i]) == 0);
}

static void test_percentiles_small(void)
{
    uint64_t buf[16];
    bench_samples s;
    uint64_t one[] = {42};
    uint64_t two[] = {7, 3};
    uint64_t eq[] = {5, 5, 5, 5, 5, 5, 5, 5, 5, 5};
    uint64_t hundred[100];
    size_t i;

    fill(&s, buf, 16, one, 1);
    CHECK(bench_p50(&s) == 42);
    CHECK(bench_p99(&s) == 42);

    fill(&s, buf, 16, two, 2);
    CHECK(bench_p50(&s) == 3);   /* ceil(0.5*2)=1 -> smallest */
    CHECK(bench_p99(&s) == 7);   /* ceil(0.99*2)=2 -> largest */
    CHECK(bench_p(&s, 0.0) == 3);
    CHECK(bench_p(&s, 1.0) == 7);

    fill(&s, buf, 16, eq, 10);
    CHECK(bench_p50(&s) == 5);
    CHECK(bench_p99(&s) == 5);

    for (i = 0; i < 100; i++)
        hundred[i] = 100 - i;     /* descending, so sorting is exercised */
    bench_samples_init(&s, hundred, 100);
    s.n = 100;
    CHECK(bench_p50(&s) == 50);  /* rank 50 of 1..100 */
    CHECK(bench_p99(&s) == 99);  /* rank 99 */
    CHECK(bench_p(&s, 0.01) == 1);
}

/* §4 counterexample: 60 % at 20 ms, 40 % at 40 ms. Mean-based or
 * interpolated estimates would mislead; nearest-rank gives p50=20, p99=40. */
static void test_counterexample(void)
{
    uint64_t buf[10];
    bench_samples s;
    uint64_t vals[10];
    size_t i;
    for (i = 0; i < 10; i++)
        vals[i] = (i < 6) ? 20000000u : 40000000u;
    fill(&s, buf, 10, vals, 10);
    CHECK(bench_p50(&s) == 20000000u);
    CHECK(bench_p99(&s) == 40000000u);
}

static void test_empty_and_overflow(void)
{
    uint64_t buf[2];
    bench_samples s;
    bench_samples_init(&s, buf, 2);
    CHECK(bench_p50(&s) == 0);
    CHECK(bench_add(&s, 1) == 0);
    CHECK(bench_add(&s, 2) == 0);
    CHECK(bench_add(&s, 3) == -1);
    CHECK(s.n == 2);
    CHECK(s.dropped == 1);
}

/* Deterministic quasi-uniform sample on [0, 1e6): i*K mod M. The true
 * q-quantile is q*1e6; the 95 % CI must contain it. */
static void test_ci_uniform(void)
{
    enum { N = 10000 };
    static uint64_t buf[N];
    bench_samples s;
    const double qs[] = {0.01, 0.10, 0.50, 0.90, 0.99};
    size_t qi, i;
    bench_samples_init(&s, buf, N);
    for (i = 0; i < N; i++) {
        uint64_t v = ((uint64_t)i * 2654435761u) % 1000000u;
        CHECK(bench_add(&s, v) == 0);
    }
    for (qi = 0; qi < sizeof qs / sizeof qs[0]; qi++) {
        uint64_t lo = 0, hi = 0, p;
        double truth = qs[qi] * 1000000.0;
        bench_ci95(&s, qs[qi], &lo, &hi);
        p = bench_p(&s, qs[qi]);
        CHECK(lo <= hi);
        CHECK((double)lo <= truth && truth <= (double)hi);
        CHECK(lo <= p && p <= hi);
    }
    /* Tiny sets: CI must be well formed and bracket the sample. */
    {
        uint64_t tb[2], lo = 9, hi = 9;
        uint64_t v2[] = {4, 8};
        bench_samples t;
        fill(&t, tb, 2, v2, 2);
        bench_ci95(&t, 0.5, &lo, &hi);
        CHECK(lo >= 4 && hi <= 8 && lo <= hi);
    }
    {
        uint64_t tb[1], lo = 9, hi = 9;
        uint64_t v1[] = {42};
        bench_samples t;
        fill(&t, tb, 1, v1, 1);
        bench_ci95(&t, 0.99, &lo, &hi);
        CHECK(lo == 42 && hi == 42);
    }
}

static void test_report_and_gate(void)
{
    uint64_t buf[10];
    bench_samples s;
    uint64_t vals[10];
    char line[512];
    FILE *fp = tmpfile();
    size_t i;
    int rc;
    long len;

    CHECK(fp != NULL);
    if (fp == NULL)
        return;
    for (i = 0; i < 10; i++)
        vals[i] = (i < 6) ? 20000000u : 40000000u;
    fill(&s, buf, 10, vals, 10);

    /* gate met: p50=20ms<=25ms, p99=40ms<=50ms -> pass, rc 0 */
    rc = bench_report_fp(fp, "ctr_pass", &s, 25000000u, 50000000u);
    CHECK(rc == 0);
    /* gate missed on p99: 40ms > 30ms -> rc 1 */
    rc = bench_report_fp(fp, "ctr_miss", &s, 25000000u, 30000000u);
    CHECK(rc == 1);
    /* ungated (0 = not gated) -> rc 0 */
    rc = bench_report_fp(fp, "ctr_ungated", &s, 0, 0);
    CHECK(rc == 0);
    /* gate missed on p50: 20ms > 10ms -> rc 1 */
    rc = bench_report_fp(fp, "ctr_miss50", &s, 10000000u, 0);
    CHECK(rc == 1);

    /* dropped samples are a miss even when the percentiles pass */
    {
        uint64_t tb[2];
        bench_samples t;
        bench_samples_init(&t, tb, 2);
        (void)bench_add(&t, 1);
        (void)bench_add(&t, 1);
        (void)bench_add(&t, 1);
        rc = bench_report_fp(fp, "dropped", &t, 0, 0);
        CHECK(rc == 1);
    }

    rewind(fp);
    {
        int lines = 0;
        int found = 0;
        while (fgets(line, sizeof line, fp) != NULL) {
            lines++;
            if (strstr(line, "name=ctr_pass ") != NULL) {
                found = 1;
                CHECK(strstr(line, "BENCH name=ctr_pass n=10 p50=20000000 p99=40000000 ") != NULL);
                CHECK(strstr(line, "ci95=[") != NULL);
                CHECK(strstr(line, "gate_p50=25000000 gate_p99=50000000 pass=1 power=") != NULL);
                CHECK(strstr(line, "power=[bat]") != NULL || strstr(line, "power=[AC]") != NULL);
            }
        }
        CHECK(lines == 5);
        CHECK(found);
    }
    len = ftell(fp);
    CHECK(len > 0);
    fclose(fp);
}

static void test_battery(void)
{
    char st[32];
    const char *tag;
    bench_battery_status(st, sizeof st);
    CHECK(strcmp(st, "Discharging") == 0 || strcmp(st, "Charging") == 0 ||
          strcmp(st, "Full") == 0 || strcmp(st, "unknown") == 0);
    tag = bench_evidence_tag();
    CHECK(strcmp(tag, "[bat]") == 0 || strcmp(tag, "[AC]") == 0);
    CHECK((strcmp(st, "Discharging") == 0) == (strcmp(tag, "[bat]") == 0));
}

static void test_time_macro(void)
{
    uint64_t buf[4];
    bench_samples s;
    volatile int x = 0;
    bench_samples_init(&s, buf, 4);
    BENCH_TIME(&s, x = x + 1);
    BENCH_TIME(&s, x = x + 1, x = x + 1);
    CHECK(s.n == 2);
    CHECK(x == 3);
    bench_evict_caches_hint();
}

int main(void)
{
    test_percentiles_small();
    test_counterexample();
    test_empty_and_overflow();
    test_ci_uniform();
    test_report_and_gate();
    test_battery();
    test_time_macro();
    printf("harness_test: %d checks, %d failures\n", checks, failures);
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
