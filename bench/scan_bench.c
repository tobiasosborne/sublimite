#include "scan/scan.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include "harness.h"

#define SIZE (1ull << 30)
#define RUNS 20
#define GATE_GBPS 12.0
#define TAIL_GBPS 12.0
#define G6_BASIS_GBPS 16.0

static double now_s(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

static int cmp_d(const void *a, const void *b)
{
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}

/* Nearest-rank latency quantiles expressed as throughput (slow tail first). */
static int report_gate(const char *name, double *gbps, double gate50, double gate99)
{
    int invalid = 0;
    for (int r = 0; r < RUNS; r++) {
        if (!isfinite(gbps[r]) || gbps[r] <= 0.0) { invalid = 1; gbps[r] = 0.0; }
    }
    qsort(gbps, RUNS, sizeof gbps[0], cmp_d);
    double p50 = gbps[RUNS / 2];              /* 10th fastest latency = 11th ascending speed */
    double p99 = gbps[0];                     /* slow tail: 1st percentile of speed = slowest of 20 */
    int miss = invalid || p50 < gate50 || p99 < gate99;
    printf("%-24s p50 %.2f GB/s  p99 %.2f GB/s  gates p50 >= %.1f / p99 >= %.1f (G)  %s\n",
           name, p50, p99, gate50, gate99, miss ? "MISS" : "PASS");
    return miss;
}

static int report(const char *name, double *gbps)
{
    return report_gate(name, gbps, GATE_GBPS, TAIL_GBPS);
}

static int self_check_tail(void)
{
    double samples[RUNS];
    for (int r = 0; r < RUNS; r++) samples[r] = 16.0;
    samples[0] = 1.0;
    int rejected = report("synthetic_tail", samples);
    double basis[RUNS];
    for (int r = 0; r < RUNS; r++) basis[r] = 15.0;
    int basis_rejected = report_gate("synthetic_G6_basis", basis, G6_BASIS_GBPS, TAIL_GBPS);
    for (int r = 0; r < RUNS; r++) samples[r] = 16.0;
    int boundary_accepted = !report("synthetic_boundary", samples);
    samples[0] = NAN;
    int invalid_rejected = report("synthetic_invalid", samples);
    int pass = rejected && basis_rejected && boundary_accepted && invalid_rejected;
    printf("scan_bench self-check §12: %s slow p99 rejected; G6 basis/invalid/boundary checked\n", pass ? "PASS" : "FAIL");
    return pass ? 0 : 1;
}

/* The same warm-up acceptance rule used by the benchmark. */
static int valid_result(const uint8_t *buf, size_t n, scan_counts expected,
                        scan_counts got, const uint8_t *last)
{
    return n > 0 && expected.newlines > 0
        && got.newlines == expected.newlines && got.nonascii == expected.nonascii
        && last == buf + n - 1;
}

static int self_check_oracle(void)
{
    uint8_t buf[131073];
    memset(buf, 'x', sizeof buf);
    buf[7] = '\n'; buf[65536] = 0x80; buf[sizeof buf - 1] = '\n';
    scan_counts expected = {2, 1};
    scan_counts prefix = scan_count(buf, 65536);
    const uint8_t *last = scan_find_nth_newline(buf, 65536, prefix.newlines - 1);
    int rejected = !valid_result(buf, sizeof buf, expected, prefix, last);
    int wrong_pointer = !valid_result(buf, sizeof buf, expected, expected, buf + 7);
    scan_counts wrong_high = expected; wrong_high.nonascii = 0;
    int wrong_count = !valid_result(buf, sizeof buf, expected, wrong_high, buf + sizeof buf - 1);
    int correct = valid_result(buf, sizeof buf, expected, expected, buf + sizeof buf - 1);
    int pass = rejected && wrong_pointer && wrong_count && correct;
    printf("scan_bench self-check §13: %s prefix result rejected=%d wrong pointer=%d wrong count=%d correct accepted=%d\n",
           pass ? "PASS" : "FAIL", rejected, wrong_pointer, wrong_count, correct);
    return pass ? 0 : 1;
}

int main(int argc, char **argv)
{
    if (argc == 2 && strcmp(argv[1], "--self-check-tail") == 0) return self_check_tail();
    if (argc == 2 && strcmp(argv[1], "--self-check-oracle") == 0) return self_check_oracle();
    volatile uint64_t sink = 0;
    uint8_t *buf = malloc(SIZE);
    if (!buf) { fprintf(stderr, "malloc 1 GiB failed\n"); return 2; }

    /* 70-byte lines (69 chars + '\n'), 2 % of bytes replaced by 2-byte UTF-8 (non-ASCII). */
    scan_counts expected = {0, 0};
    uint32_t s = 2463534242u;
    size_t i = 0;
    while (i < SIZE) {
        for (int c = 0; c < 69 && i < SIZE; c++) {
            s ^= s << 13; s ^= s >> 17; s ^= s << 5;
            if (s % 100 < 2 && i + 1 < SIZE) { buf[i++] = 0xC3; buf[i++] = 0xA9; expected.nonascii += 2; c++; }
            else buf[i++] = (uint8_t)(' ' + (s % 95));
        }
        if (i < SIZE) { buf[i++] = '\n'; expected.newlines++; }
    }
    if (buf[SIZE - 1] != '\n') {
        if (buf[SIZE - 1] >= 0x80) expected.nonascii--;
        buf[SIZE - 1] = '\n'; expected.newlines++;
    }

    /* Warm-up: touch every page and run each kernel once. */
    scan_counts wc = scan_count(buf, SIZE);
    const uint8_t *last = scan_find_nth_newline(buf, SIZE, expected.newlines - 1);
    if (!valid_result(buf, SIZE, expected, wc, last)) {
        fprintf(stderr, "scan_bench FAIL: warm-up count/last pointer\n"); free(buf); return 2;
    }
    printf("lines=%llu nonascii=%llu\n", (unsigned long long)wc.newlines, (unsigned long long)wc.nonascii);

    char power[32];
    bench_battery_status(power, sizeof power);
    FILE *load_file = fopen("/proc/loadavg", "r");
    double load = -1.0;
    if (load_file) { if (fscanf(load_file, "%lf", &load) != 1) load = -1.0; fclose(load_file); }
    printf("TRACK primitive cached anonymous buffer; (M)%s load1=%.2f power=%s; G6/G7 end-to-end unmeasured\n",
           bench__tag_from_power(power), load, power);
    double t_count[RUNS], t_nth[RUNS];
    for (int r = 0; r < RUNS; r++) {
        double t0 = now_s();
        scan_counts c = scan_count(buf, SIZE);
        double t1 = now_s();
        if (!valid_result(buf, SIZE, expected, c, last)) {
            fprintf(stderr, "scan_bench FAIL: timed scan_count sample %d\n", r); free(buf); return 2;
        }
        sink += c.newlines + c.nonascii;
        t_count[r] = (double)SIZE / (t1 - t0) / 1e9;
    }
    for (int r = 0; r < RUNS; r++) {
        double t0 = now_s();
        const uint8_t *q = scan_find_nth_newline(buf, SIZE, expected.newlines - 1);
        double t1 = now_s();
        if (!valid_result(buf, SIZE, expected, wc, q)) {
            fprintf(stderr, "scan_bench FAIL: timed nth sample %d\n", r); free(buf); return 2;
        }
        sink += (uint64_t)(uintptr_t)q;
        t_nth[r] = (double)SIZE / (t1 - t0) / 1e9;
    }
    int miss = 0;
    printf("TRACK (M)%s load1=%.2f ", bench__tag_from_power(power), load);
    miss |= report("scan_count", t_count);
    printf("TRACK (M)%s load1=%.2f ", bench__tag_from_power(power), load);
    miss |= report("scan_find_nth_newline", t_nth);
    printf("TRACK (M)%s load1=%.2f ", bench__tag_from_power(power), load);
    miss |= report_gate("scan_count_G6_basis", t_count, G6_BASIS_GBPS, TAIL_GBPS);
    (void)sink;
    free(buf);
    printf("%s\n", miss ? "TRACK: thresholds missed" : "TRACK: thresholds met");
    return miss ? 1 : 0;
}
