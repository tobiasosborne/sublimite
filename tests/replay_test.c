/* replay_test.c - trace_replay() delivery order and timing (P0.6).
 * Builds a mixed input session, dumps it, reloads it through the loader and
 * replays it into a test sink: --fast order and payloads, original gaps at
 * 2 ms spacing (tolerant: the box is loaded), speed scaling, bad options.
 * With one argument, writes the mixed session dump to that path instead so
 * tools/replay can be run on a dump produced by this test. */
#include "../src/trace/trace.h"
#include "../src/trace/trace_fmt.h"

#include <stdio.h>
#include <string.h>

#define GAP_NS 2000000ull     /* 2 ms between events */
#define IN_BASE 7000000000ull

static int g_fail;

#define CHECK(cond, ...)                                  \
    do {                                                  \
        if (!(cond)) {                                    \
            fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__); \
            fprintf(stderr, __VA_ARGS__);                 \
            fputc('\n', stderr);                          \
            g_fail = 1;                                   \
        }                                                 \
    } while (0)

static trace_input_rec g_src[TRACE_INPUT_CAP];
static trace_input_rec g_got[TRACE_INPUT_CAP];
static uint64_t g_when[TRACE_INPUT_CAP];
static size_t g_ngot;

static void collect_sink(const trace_input_rec *ev, void *user) {
    (void)user;
    if (g_ngot < TRACE_INPUT_CAP) {
        g_when[g_ngot] = trace_now_ns();
        g_got[g_ngot] = *ev;
    }
    g_ngot++;
}

static void build_session(void) {
    trace_reset();
    trace_input_key(IN_BASE + 0 * GAP_NS, TRACE_IN_KEY_DOWN, 0x61, 0x1, 0, "a", 1);
    trace_input_key(IN_BASE + 1 * GAP_NS, TRACE_IN_KEY_UP, 0x61, 0x1, 0, NULL, 0);
    trace_input_pointer(IN_BASE + 2 * GAP_NS, TRACE_IN_BUTTON_DOWN, 4, 9, 1, 0);
    trace_input_wheel(IN_BASE + 3 * GAP_NS, 0, 1 << 16, 0);
    trace_input_resize(IN_BASE + 4 * GAP_NS, 800, 600);
    trace_input_focus(IN_BASE + 5 * GAP_NS, 1);
    trace_input_clipboard(IN_BASE + 6 * GAP_NS, 1, 12);
    trace_input_filechange(IN_BASE + 7 * GAP_NS, 3, 1);
    trace_input_key(IN_BASE + 8 * GAP_NS, TRACE_IN_KEY_DOWN, 0x62, 0, 0, "b", 1);
}

/* Dumps the current session to path; used by the optional one-argument mode. */
static int write_session(const char *path) {
    FILE *f;
    int rc;
    build_session();
    f = fopen(path, "wb");
    if (f == NULL) return -1;
    rc = trace_dump(f);
    if (fclose(f) != 0) rc = -1;
    return rc;
}

static void test_fast_order_and_payload(void) {
    FILE *f = tmpfile();
    trace_loaded d;
    trace_replay_opts o = { 1, 1.0 };
    size_t n;
    int rc;

    build_session();
    n = trace_input_copy(g_src, TRACE_INPUT_CAP);
    CHECK(n == 9, "session n=%zu", n);
    CHECK(f != NULL && trace_dump(f) == 0, "dump session");
    rewind(f);
    CHECK(trace_fmt_load_dump(f, &d) == 0, "load session");
    fclose(f);

    g_ngot = 0;
    rc = trace_replay(d.in, d.nin, &o, collect_sink, NULL);
    CHECK(rc == 0, "replay fast rc=%d", rc);
    CHECK(g_ngot == n, "delivered %zu want %zu", g_ngot, n);
    for (size_t i = 0; i < n && i < TRACE_INPUT_CAP; i++) {
        CHECK(g_got[i].seq == i, "order: slot %zu got seq %llu", i, (unsigned long long)g_got[i].seq);
        CHECK(memcmp(&g_got[i], &g_src[i], sizeof g_src[i]) == 0, "payload %zu differs", i);
    }
    trace_fmt_dump_free(&d);
}

static void test_fast_rejects_bad_opts(void) {
    trace_replay_opts bad = { 0, 0.0 };
    trace_replay_opts nan_opts = { 0, 0.0 };
    CHECK(trace_replay(g_src, 0, &bad, collect_sink, NULL) != 0, "speed 0 must be rejected");
    nan_opts.speed = -1.0;
    CHECK(trace_replay(g_src, 0, &nan_opts, collect_sink, NULL) != 0, "negative speed rejected");
    CHECK(trace_replay(g_src, 1, NULL, collect_sink, NULL) != 0, "NULL opts rejected");
    CHECK(trace_replay(g_src, 1, &bad, NULL, NULL) != 0, "NULL sink rejected");
}

/* Checks each delivery against its absolute deadline: event i must be
 * delivered no earlier than start + (t0_i - t0_0) / speed, minus 100 us of
 * slack. A late delivery shortens the next measured gap legitimately, so
 * per-gap bounds are wrong; the upper bound is loose (200 ms) so a stalled
 * box does not fail. */
static void check_deadlines(const char *tag, size_t n, double speed, uint64_t start) {
    for (size_t i = 0; i < n && i < TRACE_INPUT_CAP; i++) {
        uint64_t offset = (uint64_t)((double)(g_src[i].t0_ns - g_src[0].t0_ns) / speed);
        uint64_t rel = g_when[i] - start;
        CHECK(rel + 100000u >= offset, "%s event %zu at %llu ns, deadline %llu ns",
              tag, i, (unsigned long long)rel, (unsigned long long)offset);
        CHECK(rel < offset + 200000000u, "%s event %zu at %llu ns, far too late",
              tag, i, (unsigned long long)rel);
        if (i > 0) printf("%s event %zu: %llu us (deadline %llu us)\n", tag, i,
                          (unsigned long long)(rel / 1000u), (unsigned long long)(offset / 1000u));
    }
}

static void test_timing_gaps(void) {
    /* Three events 2 ms apart at 1x: each delivery must not precede its
     * absolute deadline relative to the replay start. */
    trace_replay_opts o = { 0, 1.0 };
    size_t n = 3;
    uint64_t start;
    for (size_t i = 0; i < n; i++) {
        g_src[i].seq = i;
        g_src[i].t0_ns = IN_BASE + i * GAP_NS;
        g_src[i].kind = TRACE_IN_FOCUS;
        g_src[i].p.focus.focused = 1;
    }
    g_ngot = 0;
    start = trace_now_ns();
    CHECK(trace_replay(g_src, n, &o, collect_sink, NULL) == 0, "replay 1x");
    CHECK(g_ngot == n, "1x delivered %zu", g_ngot);
    check_deadlines("1x", n, 1.0, start);
}

static void test_speed_scales_gaps(void) {
    /* --speed=2 halves the recorded offsets: 2 ms events become 1 ms apart. */
    trace_replay_opts o = { 0, 2.0 };
    size_t n = 3;
    uint64_t start;
    g_ngot = 0;
    start = trace_now_ns();
    CHECK(trace_replay(g_src, n, &o, collect_sink, NULL) == 0, "replay 2x");
    CHECK(g_ngot == n, "2x delivered %zu", g_ngot);
    check_deadlines("2x", n, 2.0, start);
}

int main(int argc, char **argv) {
    if (argc == 2) {
        if (write_session(argv[1]) != 0) {
            fprintf(stderr, "replay_test: cannot write %s\n", argv[1]);
            return 1;
        }
        printf("replay_test: wrote session dump %s\n", argv[1]);
        return 0;
    }
    test_fast_order_and_payload();
    test_fast_rejects_bad_opts();
    test_timing_gaps();
    test_speed_scales_gaps();
    trace_reset();
    if (g_fail) {
        printf("replay_test: FAIL\n");
        return 1;
    }
    printf("replay_test: PASS\n");
    return 0;
}
