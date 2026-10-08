/* bench/harness.h - header-only benchmark harness (P0.4, edit-yy5.4).
 * Statistics per perf/01-perf-target.md §4: nearest-rank percentiles,
 * order-statistic confidence interval (no resampling), failures never dropped.
 * All functions are static inline; no malloc, no libm (sqrt is Newton).
 */
#ifndef EDITOR_BENCH_HARNESS_H
#define EDITOR_BENCH_HARNESS_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

typedef struct bench_samples {
    uint64_t *v;      /* caller-supplied storage */
    size_t cap;       /* capacity of v */
    size_t n;         /* samples stored */
    size_t dropped;   /* samples that did not fit; any drop fails the gate */
} bench_samples;

static inline void bench_samples_init(bench_samples *s, uint64_t *buf, size_t cap)
{
    s->v = buf;
    s->cap = cap;
    s->n = 0;
    s->dropped = 0;
}

/* Returns 0 when stored, -1 when the buffer is full (counted in dropped). */
static inline int bench_add(bench_samples *s, uint64_t ns)
{
    if (s->n >= s->cap) {
        s->dropped++;
        return -1;
    }
    s->v[s->n++] = ns;
    return 0;
}

static inline uint64_t bench_now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

/* Times one statement and records it. samples is a bench_samples pointer. */
#define BENCH_TIME(samples, ...)                                              \
    do {                                                                      \
        uint64_t bench_t0_ = bench_now_ns();                                  \
        __VA_ARGS__;                                                          \
        (void)bench_add((samples), bench_now_ns() - bench_t0_);               \
    } while (0)

/* ---- internal helpers (no libm) ---- */

static inline long long bench__ceil_ll(double x)
{
    long long t = (long long)x;
    if ((double)t < x)
        t++;
    return t;
}

static inline long long bench__floor_ll(double x)
{
    long long t = (long long)x;
    if ((double)t > x)
        t--;
    return t;
}

static inline double bench__sqrt(double v)
{
    double x;
    int i;
    if (v <= 0.0)
        return 0.0;
    x = v > 1.0 ? v : 1.0;
    for (i = 0; i < 64; i++)
        x = 0.5 * (x + v / x);
    return x;
}

static inline void bench__sift(uint64_t *a, size_t root, size_t end)
{
    for (;;) {
        size_t child = 2 * root + 1;
        uint64_t tmp;
        if (child > end)
            return;
        if (child + 1 <= end && a[child] < a[child + 1])
            child++;
        if (!(a[root] < a[child]))
            return;
        tmp = a[root];
        a[root] = a[child];
        a[child] = tmp;
        root = child;
    }
}

/* In-place heapsort of s->v[0..n); O(n log n), no allocation. */
static inline void bench__sort(const bench_samples *s)
{
    uint64_t *a = s->v;
    size_t n = s->n;
    size_t start, end;
    if (n < 2)
        return;
    for (start = n / 2; start-- > 0;)
        bench__sift(a, start, n - 1);
    for (end = n - 1; end > 0; end--) {
        uint64_t tmp = a[0];
        a[0] = a[end];
        a[end] = tmp;
        bench__sift(a, 0, end - 1);
    }
}

/* ---- statistics ---- */

/* Nearest-rank q-quantile, q in [0,1]. Sorts the storage in place (the
 * multiset is unchanged). Returns 0 for an empty set. */
static inline uint64_t bench_p(const bench_samples *s, double q)
{
    long long k;
    if (s->n == 0)
        return 0;
    bench__sort(s);
    k = bench__ceil_ll(q * (double)s->n);
    if (k < 1)
        k = 1;
    if (k > (long long)s->n)
        k = (long long)s->n;
    return s->v[k - 1];
}

static inline uint64_t bench_p50(const bench_samples *s) { return bench_p(s, 0.50); }
static inline uint64_t bench_p99(const bench_samples *s) { return bench_p(s, 0.99); }

/* 95 % confidence interval for the q-quantile by the binomial
 * order-statistic method (normal approximation to the binomial, z = 1.96):
 * ranks l = floor(nq - z*s), u = ceil(1 + nq + z*s), s = sqrt(n q (1-q)),
 * clamped to [1, n]. Returns 0 and sets *lo, *hi to 0 when empty. */
static inline void bench_ci95(const bench_samples *s, double q, uint64_t *lo, uint64_t *hi)
{
    const double z = 1.96;
    double nq = (double)s->n * q;
    double sd = bench__sqrt((double)s->n * q * (1.0 - q));
    long long l, u;
    if (s->n == 0) {
        *lo = 0;
        *hi = 0;
        return;
    }
    bench__sort(s);
    l = bench__floor_ll(nq - z * sd);
    u = bench__ceil_ll(1.0 + nq + z * sd);
    if (l < 1)
        l = 1;
    if (l > (long long)s->n)
        l = (long long)s->n;
    if (u < 1)
        u = 1;
    if (u > (long long)s->n)
        u = (long long)s->n;
    *lo = s->v[l - 1];
    *hi = s->v[u - 1];
}

/* ---- power evidence ---- */

static inline void bench__copy(char *buf, size_t n, const char *src)
{
    size_t i = 0;
    if (n == 0)
        return;
    while (src[i] != '\0' && i + 1 < n) {
        buf[i] = src[i];
        i++;
    }
    buf[i] = '\0';
}

/* Writes "Discharging", "Charging", "Full" or "unknown" into buf. */
static inline char *bench_battery_status(char *buf, size_t n)
{
    char tmp[64];
    const char *out = "unknown";
    FILE *f = fopen("/sys/class/power_supply/BAT0/status", "r");
    if (f != NULL) {
        if (fgets(tmp, sizeof tmp, f) != NULL) {
            size_t len = strlen(tmp);
            while (len > 0 && (tmp[len - 1] == '\n' || tmp[len - 1] == ' ' || tmp[len - 1] == '\r'))
                tmp[--len] = '\0';
            if (strcmp(tmp, "Discharging") == 0)
                out = "Discharging";
            else if (strcmp(tmp, "Charging") == 0)
                out = "Charging";
            else if (strcmp(tmp, "Full") == 0)
                out = "Full";
        }
        fclose(f);
    }
    if (buf != NULL)
        bench__copy(buf, n, out);
    return buf;
}

static inline const char *bench_evidence_tag(void)
{
    char st[32];
    bench_battery_status(st, sizeof st);
    return strcmp(st, "Discharging") == 0 ? "[bat]" : "[AC]";
}

/* ---- report ---- */

/* Prints one BENCH line to fp. Returns 0 on pass, 1 on miss. A gate of 0
 * means not gated. Any dropped sample or empty set is a miss. */
static inline int bench_report_fp(FILE *fp, const char *name, const bench_samples *s,
                                  uint64_t gate_p50_ns, uint64_t gate_p99_ns)
{
    uint64_t p50 = bench_p50(s);
    uint64_t p99 = bench_p99(s);
    uint64_t lo = 0, hi = 0;
    int pass;
    bench_ci95(s, 0.50, &lo, &hi); /* CI for the median; sorts storage */
    pass = s->n > 0 && s->dropped == 0
        && (gate_p50_ns == 0 || p50 <= gate_p50_ns)
        && (gate_p99_ns == 0 || p99 <= gate_p99_ns);
    fprintf(fp, "BENCH name=%s n=%zu p50=%llu p99=%llu ci95=[%llu,%llu] "
                "gate_p50=%llu gate_p99=%llu pass=%d power=%s\n",
            name, s->n,
            (unsigned long long)p50, (unsigned long long)p99,
            (unsigned long long)lo, (unsigned long long)hi,
            (unsigned long long)gate_p50_ns, (unsigned long long)gate_p99_ns,
            pass ? 1 : 0, bench_evidence_tag());
    return pass ? 0 : 1;
}

static inline int bench_report(const char *name, const bench_samples *s,
                               uint64_t gate_p50_ns, uint64_t gate_p99_ns)
{
    return bench_report_fp(stdout, name, s, gate_p50_ns, gate_p99_ns);
}

/* Cold runs are manual. Procedure (Linux):
 *   sync; echo 3 | sudo tee /proc/sys/vm/drop_caches
 *   verify with fincore <file> (resident pages should be 0) and
 *   /proc/vmstat pgmajfault / block I/O counters during the run.
 * Windows: empty standby list in RAMMap or reboot; verify hard faults in ETW.
 * This function intentionally does nothing. */
static inline void bench_evict_caches_hint(void)
{
    /* see the procedure above */
}

#endif /* EDITOR_BENCH_HARNESS_H */
