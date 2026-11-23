/* scan throughput over a cached 1 GiB text buffer. Tracked numbers only
 * (CLAUDE.md law 1): every timing outcome exits 0. A wrong answer from a
 * timed call is a correctness failure and exits 1. */
#include "scan/scan.h"
#include "os/os.h"
#include "trace/trace.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* v1 scan_bench size and sample count. With 20 samples the nearest-rank
 * p99 is the slowest sample. */
#define SIZE ((size_t)1 << 30)
enum { RUNS = 20 };

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

static void report(const char *name, uint64_t *ns)
{
    uint64_t p50 = trace_fmt_pct(ns, RUNS, 50), p99 = trace_fmt_pct(ns, RUNS, 99);
    printf("%s p50=%" PRIu64 " ns (%.2f GB/s) p99=%" PRIu64 " ns (%.2f GB/s), %d runs over %zu bytes\n",
           name, p50, (double)SIZE / (double)p50, p99, (double)SIZE / (double)p99, RUNS, SIZE);
}

int main(void)
{
    uint8_t *buf = malloc(SIZE);
    if (!buf) {
        puts("scan_bench: 1 GiB buffer unavailable");
        return 0;
    }
    /* v1 fixture: 70-byte lines (69 bytes + '\n') with 2 % of characters
     * a 2-byte UTF-8 sequence. Generation keeps the expected answer. */
    scan_counts want = {0, 0};
    uint32_t s = 2463534242u;
    size_t i = 0;
    while (i < SIZE) {
        for (int c = 0; c < 69 && i < SIZE; c++) {
            s ^= s << 13; s ^= s >> 17; s ^= s << 5;
            if (s % 100 < 2 && i + 1 < SIZE) { buf[i++] = 0xC3; buf[i++] = 0xA9; want.nonascii += 2; c++; }
            else buf[i++] = (uint8_t)(' ' + s % 95);
        }
        if (i < SIZE) { buf[i++] = '\n'; want.newlines++; }
    }
    if (buf[SIZE - 1] != '\n') {
        if (buf[SIZE - 1] >= 0x80) want.nonascii--;
        buf[SIZE - 1] = '\n';
        want.newlines++;
    }
    const uint8_t *last = buf + SIZE - 1;

    /* Warm-up touches every page through each kernel once. */
    scan_counts warm = scan_count(buf, SIZE);
    int bad = warm.newlines != want.newlines || warm.nonascii != want.nonascii
           || scan_find_nth_newline(buf, SIZE, want.newlines - 1) != last;

    stamp("start");
    uint64_t t_count[RUNS], t_nth[RUNS];
    for (int r = 0; r < RUNS && !bad; r++) {
        uint64_t t0 = os_now_ns();
        scan_counts c = scan_count(buf, SIZE);
        t_count[r] = os_now_ns() - t0;
        bad = c.newlines != want.newlines || c.nonascii != want.nonascii;
    }
    for (int r = 0; r < RUNS && !bad; r++) {
        uint64_t t0 = os_now_ns();
        const uint8_t *q = scan_find_nth_newline(buf, SIZE, want.newlines - 1);
        t_nth[r] = os_now_ns() - t0;
        bad = q != last;
    }
    stamp("end");
    free(buf);
    if (bad) {
        puts("scan_bench: FAIL a timed call returned a wrong answer");
        return 1;
    }
    printf("lines=%" PRIu64 " nonascii=%" PRIu64 "; cached anonymous buffer, primitive only\n",
           want.newlines, want.nonascii);
    report("scan_count", t_count);
    report("scan_find_nth_newline(last)", t_nth);
    return 0;
}
