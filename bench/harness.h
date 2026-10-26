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
#if defined(_WIN32)
#include "os/os.h"
#endif

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
#if defined(_WIN32)
    return os_now_ns();
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
#endif
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

static inline int bench__is(const char *s, size_t len, const char *lit)
{
    size_t n = strlen(lit);
    return len == n && memcmp(s, lit, n) == 0;
}

/* Normalises the raw text of /sys/class/power_supply/BAT0/status: trailing
 * newline, CR, space or tab are ignored. Returns one of "Discharging",
 * "Charging", "Not charging", "Full" or "unknown" (anything else, including
 * NULL or empty text). Pure: no I/O. */
static inline const char *bench__power_from_status(const char *raw)
{
    size_t len;
    if (raw == NULL)
        return "unknown";
    len = strlen(raw);
    while (len > 0 && (raw[len - 1] == '\n' || raw[len - 1] == '\r' ||
                       raw[len - 1] == ' ' || raw[len - 1] == '\t'))
        len--;
    if (bench__is(raw, len, "Discharging"))
        return "Discharging";
    if (bench__is(raw, len, "Charging"))
        return "Charging";
    if (bench__is(raw, len, "Not charging"))
        return "Not charging";
    if (bench__is(raw, len, "Full"))
        return "Full";
    return "unknown";
}

/* Writes the normalised power state ("Discharging", "Charging",
 * "Not charging", "Full" or "unknown") into buf. An unreadable file is
 * "unknown". */
static inline char *bench_battery_status(char *buf, size_t n)
{
    char tmp[64];
    const char *out = "unknown";
    FILE *f = fopen("/sys/class/power_supply/BAT0/status", "r");
    if (f != NULL) {
        if (fgets(tmp, sizeof tmp, f) != NULL)
            out = bench__power_from_status(tmp);
        fclose(f);
    }
    if (buf != NULL)
        bench__copy(buf, n, out);
    return buf;
}

/* Maps a normalised power state to its evidence tag: battery-only runs are
 * "[bat]"; AC-powered states are "[AC]". Anything else is "[unknown]", never
 * a claim of AC power. */
static inline const char *bench__tag_from_power(const char *power)
{
    if (strcmp(power, "Discharging") == 0)
        return "[bat]";
    if (strcmp(power, "Charging") == 0 || strcmp(power, "Not charging") == 0 ||
        strcmp(power, "Full") == 0)
        return "[AC]";
    return "[unknown]";
}

static inline const char *bench_evidence_tag(void)
{
    char st[32];
    bench_battery_status(st, sizeof st);
    return bench__tag_from_power(st);
}

/* ---- report ---- */

/* Prints one BENCH line to fp. Returns 0 on pass, 1 on miss (the caller
 * decides; bench_report maps a timing miss to exit 0). A gate of 0 means not
 * gated. Any dropped sample or empty set is a miss (structural). */
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
                "gate_p50=%llu gate_p99=%llu pass=%d%s power=%s\n",
            name, s->n,
            (unsigned long long)p50, (unsigned long long)p99,
            (unsigned long long)lo, (unsigned long long)hi,
            (unsigned long long)gate_p50_ns, (unsigned long long)gate_p99_ns,
            pass ? 1 : 0, pass ? "" : " MISS", bench_evidence_tag());
    return pass ? 0 : 1;
}

/* Process exit contribution of a printed row (edit-lwq, CLAUDE.md law 1,
 * 2026-10-11): a timing MISS is a note, not a failure. Only a structural
 * failure (no samples, or samples that did not fit) is non-zero. */
static inline int bench_structural_exit(const bench_samples *s)
{
    return s->n == 0 || s->dropped != 0 ? 1 : 0;
}

static inline int bench_report(const char *name, const bench_samples *s,
                               uint64_t gate_p50_ns, uint64_t gate_p99_ns)
{
    (void)bench_report_fp(stdout, name, s, gate_p50_ns, gate_p99_ns);
    return bench_structural_exit(s);
}

/* ---- verdicts (edit-yqu; reworked edit-lwq) ----
 * A timing MISS prints MISS and contributes exit 0 (CLAUDE.md law 1,
 * 2026-10-11: perf is a goal, not a progress blocker). Only structural
 * failures (empty or dropped samples) exit non-zero. REFUSED still marks a
 * population below BENCH_INTERACTION_MIN_N (10 000, perf/01-perf-target.md 4)
 * but is a note too. */
#define BENCH_INTERACTION_MIN_N 10000u

typedef enum bench_verdict_kind {
    BENCH_PASS = 0, BENCH_MISS = 1, BENCH_REFUSED = 2
} bench_verdict_kind;

/* Load stamp (1-minute loadavg); -1.0 when unreadable. Evidence only. */
static inline double bench_load1(void)
{
    FILE *f = fopen("/proc/loadavg", "r");
    double load = -1.0;
    if (f != NULL) {
        if (fscanf(f, "%lf", &load) != 1) load = -1.0;
        fclose(f);
    }
    return load;
}

static inline const char *bench_verdict_word(bench_verdict_kind v)
{
    return v == BENCH_PASS ? "PASS" : v == BENCH_MISS ? "MISS" : "REFUSED";
}

static inline bench_verdict_kind bench_judge(const bench_samples *s, uint64_t gate_p50_ns,
                                             uint64_t gate_p99_ns, size_t required_n)
{
    uint64_t p50 = bench_p50(s), p99 = bench_p99(s);
    if (s->n == 0 || s->dropped != 0 || (gate_p50_ns != 0 && p50 > gate_p50_ns) ||
        (gate_p99_ns != 0 && p99 > gate_p99_ns))
        return BENCH_MISS;
    return s->n < required_n ? BENCH_REFUSED : BENCH_PASS;
}

/* Exit contribution of a verdict: always 0 (timing is a note). Structural
 * failures are reported by the caller (bench_structural_exit). */
static inline int bench_exit_code(bench_verdict_kind v, int track)
{
    (void)v;
    (void)track;
    return 0;
}

/* Formats one verdict line into buf; returns the snprintf length. power is
 * the normalised power state; the evidence tag comes from it ([unknown] when
 * unrecognised, never [bat]). */
static inline int bench_gate_line(char *buf, size_t cap, const char *name, const bench_samples *s,
                                  uint64_t gate_p50_ns, uint64_t gate_p99_ns, size_t required_n,
                                  int track, const char *power, const char *load)
{
    uint64_t l50, h50, l99, h99;
    bench_verdict_kind v = bench_judge(s, gate_p50_ns, gate_p99_ns, required_n);
    const char *word = (track || (gate_p50_ns == 0 && gate_p99_ns == 0)) ? "TRACK"
                                                                     : bench_verdict_word(v);
    bench_ci95(s, 0.50, &l50, &h50);
    bench_ci95(s, 0.99, &l99, &h99);
    return snprintf(buf, cap,
        "BENCH name=%s n=%zu required_n=%zu p50=%llu p99=%llu ci95_p50=[%llu,%llu] "
        "ci95_p99=[%llu,%llu] gate_p50=%llu gate_p99=%llu dropped=%zu (M)%s power=%s load1=%s verdict=%s%s",
        name, s->n, required_n, (unsigned long long)bench_p50(s), (unsigned long long)bench_p99(s),
        (unsigned long long)l50, (unsigned long long)h50, (unsigned long long)l99,
        (unsigned long long)h99, (unsigned long long)gate_p50_ns, (unsigned long long)gate_p99_ns,
        s->dropped, bench__tag_from_power(power), power, load, word,
        (!track && v == BENCH_REFUSED) ? " (insufficient samples; no verdict)" : "");
}

/* Prints the line and returns the process exit contribution. */
static inline int bench_gate_report(const char *name, const bench_samples *s, uint64_t gate_p50_ns,
                                    uint64_t gate_p99_ns, size_t required_n, int track,
                                    const char *power, const char *load)
{
    char line[512];
    (void)bench_gate_line(line, sizeof line, name, s, gate_p50_ns, gate_p99_ns, required_n, track,
                          power, load);
    puts(line);
    return bench_structural_exit(s);
}

/* Combine independently reported rows: any non-zero (structural) wins. */
static inline int bench_merge_exit(int left, int right)
{
    return left != 0 || right != 0 ? 1 : 0;
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
