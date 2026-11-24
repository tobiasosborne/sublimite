/* Operation latencies are observations, with no latency gate (Law 1). */
#include "piece/piece.h"
#include "trace/trace.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

/* v1's indent typing sample count. A packed ORIGINAL of 24 chunks
 * plus one byte puts the cursor in a height-one document (P). */
#define PIECE_BENCH_SAMPLES 20000u
#define PIECE_BENCH_TEXT (24u * PIECE_CHUNK + 1u)

static uint64_t inserts[PIECE_BENCH_SAMPLES], deletes[PIECE_BENCH_SAMPLES];
static uint64_t snapshots[PIECE_BENCH_SAMPLES], ranks[PIECE_BENCH_SAMPLES];
static uint64_t starts[PIECE_BENCH_SAMPLES];
static uint8_t text[PIECE_BENCH_TEXT];

static void stamp(void)
{
    char power[32] = "unknown", load[32] = "unknown";
    FILE *status = fopen("/sys/class/power_supply/BAT0/status", "r");
    if (status) {
        if (fgets(power, sizeof power, status)) power[strcspn(power, "\n")] = '\0';
        fclose(status);
    }
    FILE *avg = fopen("/proc/loadavg", "r");
    if (avg) {
        if (fscanf(avg, "%31s", load) != 1) strcpy(load, "unknown");
        fclose(avg);
    }
    const char *tag = "unknown";
    if (strcmp(power, "Discharging") == 0) tag = "bat";
    if (strcmp(power, "Charging") == 0 || strcmp(power, "Full") == 0 ||
        strcmp(power, "Not charging") == 0) tag = "AC";
    printf("(M)[%s] BAT0=%s load1=%s\n", tag, power, load);
}

static void report(const char *name, uint64_t *ns)
{
    uint64_t median = trace_fmt_pct(ns, PIECE_BENCH_SAMPLES, 50);
    uint64_t tail = trace_fmt_pct(ns, PIECE_BENCH_SAMPLES, 99);
    printf("%-14s p50=%" PRIu64 " ns p99=%" PRIu64 " ns (%u samples)\n",
           name, median, tail, PIECE_BENCH_SAMPLES);
}

int main(void)
{
    stamp();
    piece_reserve *r;
    piece_tree *t;
    piece_err err = piece_reserve_create(&r);
    ASSERT(err == PIECE_OK);
    /* One LF per chunk, at its start, so rank at its middle is known, plus
     * one at the first chunk's middle: rank there and the start of line 2
     * each take a half-chunk terminal scan in a private chunk (P). */
    for (size_t i = 0; i < sizeof text; i += PIECE_CHUNK) text[i] = '\n';
    text[PIECE_CHUNK / 2u] = '\n';
    err = piece_open_copy(r, text, sizeof text, &t);
    ASSERT(err == PIECE_OK);
    const piece_view *v = piece_tree_view(t);
    piece_snapshot *saved;
    err = piece_snapshot_take(t, &saved);
    ASSERT(err == PIECE_OK);
    err = piece_replenish(t);
    ASSERT(err == PIECE_OK);
    const uint8_t byte = 'x';
    uint64_t caret = PIECE_CHUNK;
    for (size_t i = 0; i < PIECE_BENCH_SAMPLES; i++) {
        uint64_t start = os_now_ns();
        err = piece_replace(t, caret++, 0, &byte, 1);
        inserts[i] = os_now_ns() - start;
        ASSERT(err == PIECE_OK);
        err = piece_replenish(t);
        ASSERT(err == PIECE_OK);
    }
    for (size_t i = 0; i < PIECE_BENCH_SAMPLES; i++) {
        uint64_t start = os_now_ns();
        piece_snapshot *s;
        err = piece_snapshot_take(t, &s);
        snapshots[i] = os_now_ns() - start;
        ASSERT(err == PIECE_OK);

        uint64_t n;
        start = os_now_ns();
        bool exact = piece_lines_before(v, PIECE_CHUNK / 2u, &n);
        ranks[i] = os_now_ns() - start;
        ASSERT(exact && n == 1);

        uint64_t line_off, line_before;
        start = os_now_ns();
        exact = piece_lines_start(v, 2, &line_off, &line_before);
        starts[i] = os_now_ns() - start;
        ASSERT(exact && line_off == PIECE_CHUNK / 2u + 1u && line_before == 2);

        start = os_now_ns();
        err = piece_replace(t, --caret, 1, NULL, 0);
        deletes[i] = os_now_ns() - start;
        ASSERT(err == PIECE_OK);
        piece_snapshot_release(s);
        (void)piece_reclaim(t, SIZE_MAX);
        err = piece_replenish(t);
        ASSERT(err == PIECE_OK);
    }
    ASSERT(piece_length(v) == sizeof text);
    piece_snapshot_release(saved);
    piece_close(t);
    piece_reserve_destroy(r);
    report("typed insert", inserts);
    report("delete", deletes);
    report("snapshot take", snapshots);
    report("line rank", ranks);
    report("line start", starts);
    return 0;
}
