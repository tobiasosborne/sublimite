/* tracedump - reads a trace dump and prints per-gate latency p50/p99 (nearest rank).
 * Usage: tracedump <dump.bin>
 * G1 = T4 - T0, G3 = T5 - T0, T6 - T0 (optical model input). */
#include "../src/trace/trace_fmt.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static void report(const trace_rec *recs, size_t n, enum trace_ev from, enum trace_ev to,
                   const char *name) {
    uint64_t *lat = NULL;
    size_t nlat = 0;
    if (trace_fmt_latencies(recs, n, from, to, &lat, &nlat) != 0) {
        printf("%-12s error\n", name);
        return;
    }
    if (nlat == 0) {
        printf("%-12s n=0\n", name);
    } else {
        uint64_t p50 = trace_fmt_pct(lat, nlat, 50);
        uint64_t p99 = trace_fmt_pct(lat, nlat, 99);
        printf("%-12s n=%zu p50=%.3f ms p99=%.3f ms\n", name, nlat, (double)p50 / 1e6, (double)p99 / 1e6);
    }
    free(lat);
}

int main(int argc, char **argv) {
    FILE *f;
    trace_rec *recs = NULL;
    size_t n = 0;
    char bat[64] = "n/a";
    FILE *b;

    if (argc != 2) {
        fprintf(stderr, "usage: tracedump <dump.bin>\n");
        return 2;
    }
    f = fopen(argv[1], "rb");
    if (f == NULL) {
        perror(argv[1]);
        return 1;
    }
    if (trace_fmt_load(f, &recs, &n) != 0) {
        fprintf(stderr, "%s: not a valid trace dump\n", argv[1]);
        fclose(f);
        return 1;
    }
    fclose(f);

    report(recs, n, TRACE_T0_INGRESS, TRACE_T4_PRESENT_SUBMITTED, "G1 T4-T0");
    report(recs, n, TRACE_T0_INGRESS, TRACE_T5_DEVICE_DONE, "G3 T5-T0");
    report(recs, n, TRACE_T0_INGRESS, TRACE_T6_PRESENT_COMPLETE, "T6-T0");

    b = fopen("/sys/class/power_supply/BAT0/status", "r");
    if (b != NULL) {
        if (fgets(bat, sizeof bat, b) != NULL) {
            size_t l = strlen(bat);
            if (l > 0 && bat[l - 1] == '\n') bat[l - 1] = '\0';
        }
        fclose(b);
    }
    printf("battery: %s\n", bat);
    free(recs);
    return 0;
}
