/* Public archive benchmark. Latencies are observations. A resource
 * refusal is printed as a note. */
#include "undo/undo.h"
#include "trace/trace.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* (P) The smallest sample count with integral ranks at p50 and p99 and
 * a separate maximum. */
enum { SAMPLES = 100 };

static void stamp(void)
{
    /* (P) Longest named Linux power status plus newline and terminator. */
    char power[sizeof "Not charging\n"] = "unknown";
    FILE *f = fopen("/sys/class/power_supply/BAT0/status", "r");
    if (f) {
        if (fgets(power, sizeof power, f)) power[strcspn(power, "\n")] = '\0';
        fclose(f);
    }
    double load = 0;
    f = fopen("/proc/loadavg", "r");
    bool have_load = f && fscanf(f, "%lf", &load) == 1;
    if (f) fclose(f);
    const char *tag = "unknown";
    if (strcmp(power, "Discharging") == 0) tag = "bat";
    if (strcmp(power, "Charging") == 0 || strcmp(power, "Full") == 0 ||
        strcmp(power, "Not charging") == 0) tag = "AC";
    printf("(M)[%s] BAT0=%s ", tag, power);
    if (have_load) printf("load1=%.2f\n", load);
    else puts("load1=unknown");
}

static void report(const char *label, uint64_t times[SAMPLES])
{
    const uint64_t median = trace_fmt_pct(times, SAMPLES, 50);
    const uint64_t tail = trace_fmt_pct(times, SAMPLES, 99);
    printf("%s p50=%llu ns p99=%llu ns (n=%u)\n", label,
           (unsigned long long)median, (unsigned long long)tail, (unsigned)SAMPLES);
}

int main(void)
{
    stamp();
    piece_reserve *r;
    piece_tree *t = NULL;
    undo_log *u = NULL;
    if (piece_reserve_create(&r) != PIECE_OK) {
        puts("undo_bench p50=n/a p99=n/a (note: piece reserve unavailable)");
        return EXIT_SUCCESS;
    }
    bool complete = false;
    if (piece_open_copy(r, NULL, 0, &t) == PIECE_OK &&
        undo_create(&u, t, SIZE_MAX, NULL, NULL) == UNDO_OK) {
        const undo_state state = {0};
        const uint8_t byte = 'x';
        undo_change change;
        uint64_t insert[SAMPLES], reverse[SAMPLES], forward[SAMPLES];
        for (size_t i = 0; i < SAMPLES; i++) {
            /* (P) One independently undoable keystroke per sample.
             * Replenishment is outside the measured UI operation. */
            uint64_t start = os_now_ns();
            if (undo_begin(u, &state, false) != UNDO_OK) break;
            if (undo_replace(u, i, 0, &byte, 1) != UNDO_OK) {
                undo_abort(u);
                break;
            }
            if (undo_commit(u, &state) != UNDO_OK) break;
            insert[i] = os_now_ns() - start;
            if (piece_replenish(t) != PIECE_OK) break;
            start = os_now_ns();
            if (undo_undo(u, 1, OS_DEADLINE_NEVER, &change) != UNDO_OK) break;
            reverse[i] = os_now_ns() - start;
            if (piece_replenish(t) != PIECE_OK) break;
            start = os_now_ns();
            if (undo_redo(u, 1, OS_DEADLINE_NEVER, &change) != UNDO_OK) break;
            forward[i] = os_now_ns() - start;
            if (piece_replenish(t) != PIECE_OK) break;
            complete = i + 1 == SAMPLES;
        }
        if (complete) {
            report("typed insert", insert);
            report("undo", reverse);
            report("redo", forward);
        }
    }
    if (!complete) puts("undo_bench p50=n/a p99=n/a (note: sample run unavailable)");
    if (u) undo_destroy(u);
    if (t) piece_close(t);
    piece_reserve_destroy(r);
    return EXIT_SUCCESS;
}
