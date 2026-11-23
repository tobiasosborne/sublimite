/* utf8 bench. Tracked numbers only (CLAUDE.md law 1): every timing outcome
 * exits 0; a timed call that returns a wrong answer exits 1.
 *
 * Rows:
 *   decode_width_500   decode + utf8_cell_width of 500 scalars
 *   grapheme_500       utf8_grapheme_next over the same windows
 *   cluster_500        utf8_cluster (length + width) over the same windows
 *   step_<chain>       one utf8_grapheme_step call at UTF8_GRAPHEME_BUDGET from
 *                      the start of a pathological cluster (the UI-slice cost)
 *   step_<chain>_total the whole cluster across budgeted calls, as ns/byte:
 *                      the t of UTF8_GRAPHEME_BUDGET's derivation (utf8.h)
 *   cstep_<chain>      one utf8_cluster_step call, same budget
 *   <row>_unbounded    the one-shot call on the same cluster, for comparison
 * Window mix, by scalar count (v1's, to keep rows comparable with v1 history):
 * 55 % ASCII U+0020..U+007E, 20 % Latin-1 U+00A0..U+00FF (© and ® included),
 * 15 % CJK U+4E00..U+A1FF, 10 % emoji U+1F300..U+1F44F. "runs" windows draw
 * the class per run of 1..40 scalars, the way text arrives in words; "iid"
 * windows draw it per scalar, an adversarial case for the branch predictor.
 * WINDOWS distinct windows are cycled so no single window is learned. */
#include "utf8/utf8.h"
#include "os/os.h"
#include "trace/trace.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* (E) v1's sizes: 500 scalars is v1 perf's "<= 500 shaped chars per slice"
 * window; 4096 windows exceed the branch predictor's history; 20000 samples
 * put about 200 samples above p99. */
enum { SCALARS = 500, WINDOWS = 4096, SAMPLES = 20000, LONG_SAMPLES = 2000 };

static volatile uint64_t sink;
static uint64_t ns[SAMPLES];
static int wrong;

static uint32_t rng = 2463534242u;
static uint32_t rnd(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }

static void stamp(const char *when)
{
    char power[32] = "unknown", load[32] = "unknown";
    FILE *f = fopen("/sys/class/power_supply/BAT0/status", "r");
    if (f) {
        if (fgets(power, sizeof power, f)) power[strcspn(power, "\n")] = '\0';
        fclose(f);
    }
    f = fopen("/proc/loadavg", "r");
    if (f) {
        if (fscanf(f, "%31s", load) != 1) strcpy(load, "unknown");
        fclose(f);
    }
    const char *tag = "unknown";
    if (strcmp(power, "Discharging") == 0) tag = "bat";
    if (strcmp(power, "Charging") == 0 || strcmp(power, "Full") == 0) tag = "AC";
    printf("(M)[%s] BAT0=%s load1=%s at %s\n", tag, power, load, when);
}

static void report(const char *name, size_t n, double per, const char *unit)
{
    uint64_t p50 = trace_fmt_pct(ns, n, 50), p99 = trace_fmt_pct(ns, n, 99);
    printf("%-34s p50=%8" PRIu64 " ns p99=%8" PRIu64 " ns", name, p50, p99);
    if (per > 0) printf("  (p50 %.2f ns/%s)", (double)p50 / per, unit);
    printf("  n=%zu\n", n);
}

static uint32_t pick(int iid, uint32_t *run_k, uint32_t *run_left)
{
    uint32_t k, r = rnd();
    if (iid) {
        k = rnd() % 100;
    } else {
        if (*run_left == 0) { *run_left = 1 + rnd() % 40; *run_k = rnd() % 100; }
        (*run_left)--;
        k = *run_k;
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

#define TIME(i, expr) do { uint64_t t0_ = os_now_ns(); expr; ns[i] = os_now_ns() - t0_; } while (0)

static void run_mix(const char *mix, int iid, uint8_t *text, size_t *start)
{
    uint32_t run_k = 0, run_left = 0;
    size_t n = 0;
    for (int w = 0; w < WINDOWS; w++) {
        start[w] = n;
        for (int k = 0; k < SCALARS; k++) n += utf8_encode(pick(iid, &run_k, &run_left), text + n);
    }
    start[WINDOWS] = n;
    printf("mix %s: %d windows x %d scalars, %.2f B/scalar\n", mix, WINDOWS, SCALARS, (double)n / (WINDOWS * SCALARS));
    for (int w = 0; w < WINDOWS; w++) sink += decode_width(text + start[w], start[w + 1] - start[w]);   /* warm */

    char name[64];
    for (int i = 0; i < SAMPLES; i++) {
        int w = i % WINDOWS;
        TIME(i, sink += decode_width(text + start[w], start[w + 1] - start[w]));
    }
    snprintf(name, sizeof name, "decode_width_500_%s", mix);
    report(name, SAMPLES, SCALARS, "scalar");
    for (int i = 0; i < SAMPLES; i++) {
        int w = i % WINDOWS;
        TIME(i, sink += graphemes(text + start[w], start[w + 1] - start[w]));
    }
    snprintf(name, sizeof name, "grapheme_500_%s", mix);
    report(name, SAMPLES, SCALARS, "scalar");
    for (int i = 0; i < SAMPLES; i++) {
        int w = i % WINDOWS;
        TIME(i, sink += clusters(text + start[w], start[w + 1] - start[w]));
    }
    snprintf(name, sizeof name, "cluster_500_%s", mix);
    report(name, SAMPLES, SCALARS, "scalar");
}

/* One budgeted call from the start of b[0..n) (one cluster), the whole cluster
 * across calls, and the unbounded one-shot call. */
static void step_rows(const char *chain, const uint8_t *b, size_t n)
{
    char name[64];
    size_t used = 0;
    utf8_gseg g;
    for (int i = 0; i < SAMPLES; i++) {
        utf8_gseg_init(&g);
        TIME(i, sink += (uint64_t)utf8_grapheme_step(&g, b, n, UTF8_GRAPHEME_BUDGET, 1, &used) + used);
    }
    wrong |= used < UTF8_GRAPHEME_BUDGET || used > UTF8_GRAPHEME_BUDGET + 3;
    snprintf(name, sizeof name, "step_%s", chain);
    report(name, SAMPLES, (double)used, "byte");
    for (int i = 0; i < LONG_SAMPLES; i++) {
        size_t pos = 0;
        TIME(i, {
            utf8_gseg_init(&g);
            int r;
            do { r = utf8_grapheme_step(&g, b + pos, n - pos, UTF8_GRAPHEME_BUDGET, 1, &used); pos += used; } while (r == UTF8_G_BUDGET);
            sink += pos;
        });
        wrong |= pos != n;
    }
    snprintf(name, sizeof name, "step_%s_total", chain);
    report(name, LONG_SAMPLES, (double)n, "byte");
    for (int i = 0; i < LONG_SAMPLES; i++) {
        size_t len = 0;
        TIME(i, len = utf8_grapheme_next(b, n));
        wrong |= len != n;
    }
    snprintf(name, sizeof name, "step_%s_unbounded", chain);
    report(name, LONG_SAMPLES, (double)n, "byte");

    utf8_cseg c;
    int w = 0, want = utf8_cluster_width(b, n);
    for (int i = 0; i < SAMPLES; i++) {
        utf8_cseg_init(&c);
        TIME(i, sink += (uint64_t)utf8_cluster_step(&c, b, n, UTF8_GRAPHEME_BUDGET, 1, &used, &w) + used);
    }
    snprintf(name, sizeof name, "cstep_%s", chain);
    report(name, SAMPLES, (double)used, "byte");
    for (int i = 0; i < LONG_SAMPLES; i++) {
        size_t pos = 0;
        int r;
        utf8_cseg_init(&c);
        TIME(i, {
            do { r = utf8_cluster_step(&c, b + pos, n - pos, UTF8_GRAPHEME_BUDGET, 1, &used, &w); pos += used; } while (r == UTF8_G_BUDGET);
        });
        wrong |= pos != n || w != want;
    }
    snprintf(name, sizeof name, "cstep_%s_total", chain);
    report(name, LONG_SAMPLES, (double)n, "byte");
    for (int i = 0; i < LONG_SAMPLES; i++)
        TIME(i, sink += (uint64_t)utf8_cluster_width(b, n));
    snprintf(name, sizeof name, "cstep_%s_unbounded", chain);
    report(name, LONG_SAMPLES, (double)n, "byte");
}

static uint8_t *chain(const char *head, const char *unit, size_t reps, const char *tail, size_t *n)
{
    size_t hl = strlen(head), ul = strlen(unit), tl = strlen(tail);
    uint8_t *b = malloc(hl + ul * reps + tl);
    if (!b) return NULL;
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
    if (!text || !start) {
        puts("utf8_bench: buffers unavailable");
        return 0;
    }
    stamp("start");
    run_mix("runs", 0, text, start);
    run_mix("iid", 1, text, start);
    /* v1's pathological clusters: 'a' + 24,576 x U+0301 (49,153 B), 20,000 x
     * U+0600 + 'a' (40,001 B), man (ZWJ woman) x 5000 (35,004 B), and
     * © (ZWJ ®) x 5000 (25,002 B), the GB11 path this bead fixed. */
    static const struct { const char *name, *head, *unit, *tail; size_t reps; } L[] = {
        { "combining_49k", "a", "\xCC\x81", "", 24576 },
        { "prepend_40k", "", "\xD8\x80", "a", 20000 },
        { "zwj_35k", "\xF0\x9F\x91\xA8", "\xE2\x80\x8D\xF0\x9F\x91\xA9", "", 5000 },
        { "copyright_zwj_25k", "\xC2\xA9", "\xE2\x80\x8D\xC2\xAE", "", 5000 },
    };
    for (size_t i = 0; i < sizeof L / sizeof L[0]; i++) {
        size_t n = 0;
        uint8_t *b = chain(L[i].head, L[i].unit, L[i].reps, L[i].tail, &n);
        if (!b) {
            puts("utf8_bench: chain buffer unavailable");
            return 0;
        }
        wrong |= utf8_grapheme_next(b, n) != n;                /* one cluster */
        step_rows(L[i].name, b, n);
        free(b);
    }
    stamp("end");
    free(start);
    free(text);
    if (wrong) {
        puts("utf8_bench: FAIL a timed call returned a wrong answer");
        return 1;
    }
    puts("utf8_bench: rows printed (tracked numbers; a slow row is a note, not a failure)");
    return 0;
}
