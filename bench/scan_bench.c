#include "scan/scan.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define SIZE (1ull << 30)
#define RUNS 20
#define GATE_GBPS 12.0

static volatile uint64_t sink;

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

/* Prints p50/p99 GB/s for RUNS timed runs; returns 1 if p50 misses the gate. */
static int report(const char *name, double *gbps)
{
    qsort(gbps, RUNS, sizeof gbps[0], cmp_d);
    double p50 = gbps[RUNS / 2];              /* 11th of 20 */
    double p99 = gbps[0];                     /* slow tail: 1st percentile of speed = slowest of 20 */
    int miss = p50 < GATE_GBPS;
    printf("%-24s p50 %.2f GB/s  p99 %.2f GB/s  gate >= %.1f  %s\n",
           name, p50, p99, GATE_GBPS, miss ? "MISS" : "PASS");
    return miss;
}

static void read_battery(void)
{
    char buf[64] = "unknown";
    FILE *f = fopen("/sys/class/power_supply/BAT0/status", "r");
    if (f) {
        if (!fgets(buf, sizeof buf, f)) strcpy(buf, "unreadable");
        fclose(f);
    }
    buf[strcspn(buf, "\n")] = '\0';
    printf("battery: %s\n", buf);
}

int main(void)
{
    uint8_t *buf = malloc(SIZE);
    if (!buf) { fprintf(stderr, "malloc 1 GiB failed\n"); return 2; }

    /* 70-byte lines (69 chars + '\n'), 2 % of bytes replaced by 2-byte UTF-8 (non-ASCII). */
    uint32_t s = 2463534242u;
    size_t i = 0;
    while (i < SIZE) {
        for (int c = 0; c < 69 && i < SIZE; c++) {
            s ^= s << 13; s ^= s >> 17; s ^= s << 5;
            if (s % 100 < 2 && i + 1 < SIZE) { buf[i++] = 0xC3; buf[i++] = 0xA9; c++; }
            else buf[i++] = (uint8_t)(' ' + (s % 95));
        }
        if (i < SIZE) buf[i++] = '\n';
    }
    if (buf[SIZE - 1] != '\n') buf[SIZE - 1] = '\n';

    /* Warm-up: touch every page and run each kernel once. */
    scan_counts wc = scan_count(buf, SIZE);
    const uint8_t *last = scan_find_nth_newline(buf, SIZE, wc.newlines - 1);
    if (!last) { fprintf(stderr, "bad warm-up\n"); return 2; }
    printf("lines=%llu nonascii=%llu\n", (unsigned long long)wc.newlines, (unsigned long long)wc.nonascii);

    double t_count[RUNS], t_nth[RUNS];
    for (int r = 0; r < RUNS; r++) {
        double t0 = now_s();
        scan_counts c = scan_count(buf, SIZE);
        double t1 = now_s();
        sink += c.newlines + c.nonascii;
        t_count[r] = (double)SIZE / (t1 - t0) / 1e9;
    }
    for (int r = 0; r < RUNS; r++) {
        double t0 = now_s();
        const uint8_t *q = scan_find_nth_newline(buf, SIZE, wc.newlines - 1);
        double t1 = now_s();
        sink += (uint64_t)(uintptr_t)q;
        t_nth[r] = (double)SIZE / (t1 - t0) / 1e9;
    }
    int miss = 0;
    miss |= report("scan_count", t_count);
    miss |= report("scan_find_nth_newline", t_nth);
    read_battery();
    free(buf);
    printf("%s\n", miss ? "BENCH GATE MISSED" : "BENCH GATE PASSED");
    return miss ? 1 : 0;
}
