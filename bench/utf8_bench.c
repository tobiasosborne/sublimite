/* utf8 bench (P1.1b). Gates from perf/01-perf-target.md §3 (UI slices <= 0.5 ms;
 * <= 96 KiB decode or <= 500 shaped chars per slice):
 *   decode_width_500: decode + utf8_cell_width of 500 scalars, p50 <= 5 us, p99 <= 10 us (G)
 *   ascii_skip_96k:   utf8_ascii_run over 96 KiB of ASCII, >= 8 GB/s at p50 (G), i.e. p50 <= 12288 ns
 *   grapheme_500:     utf8_grapheme_next over the same windows, TRACK (not gated)
 *   cluster_500:      utf8_cluster (length + cluster width) over the same windows, TRACK
 * (P1.1d) Resumable segmentation, one utf8_grapheme_step call with the default
 * budget (UTF8_GRAPHEME_BUDGET bytes) from the start of a pathological cluster:
 *   step_combining: 'a' + 24,576 x U+0301 (49,153 B); step_prepend: 20,000 x U+0600 + 'a';
 *   step_zwj: man ZWJ woman ZWJ ... chain (~35 KB); each p99 <= 0.5 ms (G, perf s3 UI slice).
 *   The whole cluster across calls is reported as ns/byte (TRACK), plus the unbounded
 *   utf8_grapheme_next on the same input (TRACK) for the before/after.
 * Class mix for the 500-scalar windows, by scalar count:
 *   55 % ASCII printable U+0020..U+007E, 20 % Latin-1 U+00A0..U+00FF,
 *   15 % CJK U+4E00..U+A1FF, 10 % emoji U+1F300..U+1F44F.
 * Gated windows ("runs") draw the class per run of 1..40 scalars (uniform),
 * the way text arrives in words and script spans. The "iid" windows draw the
 * class per scalar: an adversarial bound with no run structure for the branch
 * predictor; reported against the same numbers but not gated.
 * 4096 distinct windows per mix are cycled so no single window is learned. */
#include "utf8/utf8.h"
#include "harness.h"
#include <stdlib.h>

#define SCALARS 500
#define WINDOWS 4096
#define SAMPLES 20000
#define SKIP_BYTES (96u * 1024u)
#define SKIP_SAMPLES 5000

static volatile uint64_t sink;

static uint32_t rng = 2463534242u;
static uint32_t rnd(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }

static uint32_t pick(int iid)
{
    static uint32_t run_k, run_left;    /* bench-local generator state */
    uint32_t k, r = rnd();
    if (iid) {
        k = rnd() % 100;
    } else {
        if (run_left == 0) { run_left = 1 + rnd() % 40; run_k = rnd() % 100; }
        run_left--;
        k = run_k;
    }
    if (k < 55) return 0x20 + r % 95;
    if (k < 75) return 0xA0 + r % 0x60;
    if (k < 90) return 0x4E00 + r % 0x5400;
    return 0x1F300 + r % 0x150;
}

static uint64_t decode_width(const uint8_t *p, size_t n)
{
    uint64_t w = 0;
    size_t off = 0;
    for (int k = 0; k < SCALARS && off < n; k++) {
        utf8_step s = utf8_decode(p + off, n - off);
        w += (uint64_t)utf8_cell_width(s.cp);
        off += s.len;
    }
    return w + off;
}

static uint64_t graphemes(const uint8_t *p, size_t n)
{
    uint64_t c = 0;
    for (size_t off = 0; off < n; c++)
        off += utf8_grapheme_next(p + off, n - off);
    return c;
}

/* Cluster step: utf8_cluster (length + cell width) over a window; TRACK only. */
static uint64_t clusters(const uint8_t *p, size_t n)
{
    uint64_t c = 0;
    for (size_t off = 0; off < n;) {
        int w;
        off += utf8_cluster(p + off, n - off, &w);
        c += (uint64_t)w;
    }
    return c;
}

/* bench_report's ci95 is the median's; print both quantiles with their own labelled interval. */
static void print_ci(const bench_samples *s)
{
    uint64_t lo, hi;
    bench_ci95(s, 0.50, &lo, &hi);
    printf("  p50 %llu ns ci95 [%llu,%llu]", (unsigned long long)bench_p50(s), (unsigned long long)lo, (unsigned long long)hi);
    bench_ci95(s, 0.99, &lo, &hi);
    printf("; p99 %llu ns ci95 [%llu,%llu]\n", (unsigned long long)bench_p99(s), (unsigned long long)lo, (unsigned long long)hi);
}

static void print_power(void)
{
    char st[32], gov[64] = "unknown";
    bench_battery_status(st, sizeof st);
    FILE *f = fopen("/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor", "r");
    if (f) {
        if (!fgets(gov, sizeof gov, f)) gov[0] = '\0';
        fclose(f);
        gov[strcspn(gov, "\n")] = '\0';
    }
    printf("power: battery=%s governor=%s tag=(M)%s\n", st, gov, bench_evidence_tag());
}

static int run_mix(const char *name, const char *gname, const char *cname, int iid, int gated, uint8_t *text, size_t *start, uint64_t *v)
{
    size_t n = 0;
    for (int w = 0; w < WINDOWS; w++) {
        start[w] = n;
        for (int k = 0; k < SCALARS; k++) n += utf8_encode(pick(iid), text + n);
    }
    start[WINDOWS] = n;
    printf("%s: %d windows x %d scalars, %.2f B/scalar\n", iid ? "mix iid" : "mix runs", WINDOWS, SCALARS,
           (double)n / (WINDOWS * SCALARS));
    for (int w = 0; w < WINDOWS; w++) sink += decode_width(text + start[w], start[w + 1] - start[w]); /* warm */

    bench_samples s;
    bench_samples_init(&s, v, SAMPLES);
    for (int i = 0; i < SAMPLES; i++) {
        int w = i % WINDOWS;
        BENCH_TIME(&s, sink += decode_width(text + start[w], start[w + 1] - start[w]));
    }
    int miss = bench_report(name, &s, gated ? 5000 : 0, gated ? 10000 : 0);
    printf("  -> p50 %.1f ns/scalar, p99 %llu ns vs 5000/10000 (%s)\n", (double)bench_p50(&s) / SCALARS,
           (unsigned long long)bench_p99(&s), gated ? "gated" : "not gated");
    print_ci(&s);

    bench_samples_init(&s, v, SAMPLES);
    for (int i = 0; i < SAMPLES; i++) {
        int w = i % WINDOWS;
        BENCH_TIME(&s, sink += graphemes(text + start[w], start[w + 1] - start[w]));
    }
    (void)bench_report(gname, &s, 0, 0);
    printf("  -> p50 %.1f ns/scalar (TRACK)\n", (double)bench_p50(&s) / SCALARS);

    bench_samples_init(&s, v, SAMPLES);
    for (int i = 0; i < SAMPLES; i++) {
        int w = i % WINDOWS;
        BENCH_TIME(&s, sink += clusters(text + start[w], start[w + 1] - start[w]));
    }
    (void)bench_report(cname, &s, 0, 0);
    printf("  -> p50 %.1f ns/scalar (TRACK)\n", (double)bench_p50(&s) / SCALARS);
    return miss;
}

/* Gated: one budgeted call; TRACK: whole cluster across calls, and the old unbounded call. */
static int step_row(const char *name, const uint8_t *b, size_t n, uint64_t *v)
{
    bench_samples s;
    size_t used = 0;
    utf8_gseg g;
    for (int i = 0; i < 200; i++) { utf8_gseg_init(&g); (void)utf8_grapheme_step(&g, b, n, UTF8_GRAPHEME_BUDGET, 1, &used); sink += used; }
    bench_samples_init(&s, v, SAMPLES);
    for (int i = 0; i < SAMPLES; i++) {
        utf8_gseg_init(&g);
        BENCH_TIME(&s, sink += (uint64_t)utf8_grapheme_step(&g, b, n, UTF8_GRAPHEME_BUDGET, 1, &used) + used);
    }
    int miss = bench_report(name, &s, 0, 500000);
    printf("  -> %zu B cluster, budget %u B, used %zu B/call; p99 %llu ns vs 500000 (gated)\n", n, UTF8_GRAPHEME_BUDGET, used,
           (unsigned long long)bench_p99(&s));
    print_ci(&s);

    bench_samples_init(&s, v, 2000);
    for (int i = 0; i < 2000; i++) {
        BENCH_TIME(&s, {
            utf8_gseg_init(&g);
            size_t pos = 0;
            int r;
            do { r = utf8_grapheme_step(&g, b + pos, n - pos, UTF8_GRAPHEME_BUDGET, 1, &used); pos += used; } while (r == UTF8_G_BUDGET);
            sink += pos;
        });
    }
    printf("%s_total: p50 %llu ns for %zu B = %.2f ns/byte (TRACK)\n", name, (unsigned long long)bench_p50(&s), n,
           (double)bench_p50(&s) / (double)n);
    bench_samples_init(&s, v, 2000);
    for (int i = 0; i < 2000; i++)
        BENCH_TIME(&s, sink += utf8_grapheme_next(b, n));
    printf("%s_unbounded: p50 %llu ns p99 %llu ns for one utf8_grapheme_next call (TRACK; the pre-P1.1d call, cf. 500000 slice)\n", name,
           (unsigned long long)bench_p50(&s), (unsigned long long)bench_p99(&s));
    return miss;
}

static uint8_t *chain(const char *head, const char *unit, size_t reps, const char *tail, size_t *n)
{
    size_t hl = strlen(head), ul = strlen(unit), tl = strlen(tail);
    uint8_t *b = malloc(hl + ul * reps + tl);
    memcpy(b, head, hl);
    for (size_t i = 0; i < reps; i++) memcpy(b + hl + i * ul, unit, ul);
    memcpy(b + hl + ul * reps, tail, tl);
    *n = hl + ul * reps + tl;
    return b;
}

int main(void)
{
    uint8_t *text = malloc((size_t)WINDOWS * SCALARS * 4);
    size_t *start = malloc((WINDOWS + 1) * sizeof *start);
    uint8_t *ascii = malloc(SKIP_BYTES);
    uint64_t *v = malloc(SAMPLES * sizeof *v);
    if (!text || !start || !ascii || !v) { fprintf(stderr, "alloc failed\n"); return 2; }
    for (size_t i = 0; i < SKIP_BYTES; i++) ascii[i] = (i % 72 == 71) ? '\n' : (uint8_t)(0x20 + rnd() % 95);

    print_power();
    int miss = 0;
    miss |= run_mix("utf8_decode_width_500_runs", "utf8_grapheme_500_runs", "utf8_cluster_500_runs", 0, 1, text, start, v);
    (void)run_mix("utf8_decode_width_500_iid", "utf8_grapheme_500_iid", "utf8_cluster_500_iid", 1, 0, text, start, v);

    bench_samples s;
    bench_samples_init(&s, v, SKIP_SAMPLES);
    for (int i = 0; i < 100; i++) sink += utf8_ascii_run(ascii, SKIP_BYTES);
    for (int i = 0; i < SKIP_SAMPLES; i++)
        BENCH_TIME(&s, sink += utf8_ascii_run(ascii, SKIP_BYTES));
    miss |= bench_report("utf8_ascii_skip_96k", &s, 12288, 0);
    printf("  -> throughput at p50 latency %.2f GB/s (gate >= 8 GB/s); throughput at p99 latency (slow tail) %.2f GB/s\n",
           (double)SKIP_BYTES / (double)bench_p50(&s), (double)SKIP_BYTES / (double)bench_p99(&s));
    print_ci(&s);

    {
        size_t n;
        uint8_t *b = chain("a", "\xCC\x81", 24576, "", &n);
        miss |= step_row("utf8_step_combining_49k", b, n, v);
        free(b);
        b = chain("", "\xD8\x80", 20000, "a", &n);
        miss |= step_row("utf8_step_prepend_40k", b, n, v);
        free(b);
        b = chain("\xF0\x9F\x91\xA8", "\xE2\x80\x8D\xF0\x9F\x91\xA9", 5000, "", &n);
        miss |= step_row("utf8_step_zwj_35k", b, n, v);
        free(b);
    }

    printf("%s\n", miss ? "BENCH GATE MISSED" : "BENCH GATE PASSED");
    free(v); free(ascii); free(start); free(text);
    return miss;
}
