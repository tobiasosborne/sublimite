/* Report nearest-rank percentiles of batch means. The v1 protocol used
 * a million calls (1000 batches of 1000) to amortize the two clock reads
 * per batch and exercise repeated ring wrap. */
#include "os/os.h"
#include "trace/trace.h"

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum { BATCHES = 1000, CALLS_PER_BATCH = 1000 };

static void report(const char *name, uint64_t samples[BATCHES]) {
    uint64_t p50 = trace_fmt_pct(samples, BATCHES, 50);
    uint64_t p99 = trace_fmt_pct(samples, BATCHES, 99);
    printf("%s p50=%" PRIu64 " ns p99=%" PRIu64 " ns\n", name, p50, p99);
}

int main(void) {
    char power[32] = "unknown";
    char load[32] = "unknown";
    FILE *f = fopen("/sys/class/power_supply/BAT0/status", "r");
    if (f != NULL) {
        if (fgets(power, sizeof power, f) != NULL) power[strcspn(power, "\n")] = '\0';
        fclose(f);
    }
    f = fopen("/proc/loadavg", "r");
    if (f != NULL) {
        if (fscanf(f, "%31s", load) != 1) strcpy(load, "unknown");
        fclose(f);
    }
    const char *tag = "unknown";
    if (strcmp(power, "Discharging") == 0) tag = "bat";
    if (strcmp(power, "Charging") == 0 || strcmp(power, "Full") == 0) tag = "AC";
    printf("(M)[%s] BAT0=%s load1=%s, %d batches of %d calls, nearest-rank batch means\n",
           tag, power, load, BATCHES, CALLS_PER_BATCH);

    if (trace_thread_register() < 0) {
        puts("trace_bench: ring unavailable");
        return 0;
    }

    uint64_t records[BATCHES], clock[BATCHES], input[BATCHES];
    for (uint32_t b = 0; b < BATCHES; b++) {
        uint64_t t0 = os_now_ns();
        for (uint32_t i = 0; i < CALLS_PER_BATCH; i++)
            trace_record(TRACE_T2_MUTATION_DONE, b * CALLS_PER_BATCH + i);
        records[b] = (os_now_ns() - t0) / CALLS_PER_BATCH;

        t0 = os_now_ns();
        for (uint32_t i = 0; i < CALLS_PER_BATCH; i++) (void)os_now_ns();
        clock[b] = (os_now_ns() - t0) / CALLS_PER_BATCH;

        t0 = os_now_ns();
        for (uint32_t i = 0; i < CALLS_PER_BATCH; i++) {
            uint64_t seq = b * CALLS_PER_BATCH + i;
            /* One call of each representative ingress helper per four events. */
            switch (i % 4u) {
            case 0: trace_input_key(seq, TRACE_IN_KEY_DOWN, 'a', 0, 0, "a", 1); break;
            case 1: trace_input_pointer(seq, TRACE_IN_POINTER_MOVE, (int32_t)i, 0, 0, 0); break;
            case 2: trace_input_wheel(seq, 0, 256, 0); break;
            case 3: trace_input_resize(seq, i, i); break;
            }
        }
        input[b] = (os_now_ns() - t0) / CALLS_PER_BATCH;
    }
    report("trace_record", records);
    report("os_now_ns", clock);
    report("trace_input", input);
    return 0;
}
