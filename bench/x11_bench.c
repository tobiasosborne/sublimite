/* x11_bench.c - key translation cost (P2.2). TRACKED, no gate invented: G1 is end-to-end and measured later.
 * Startup is a gated platform subset (not a full G4a verdict); misses fail even in TRACK. */
#include "x11/input.h"
#include "base/base.h"
#include "x11/clip.h"
#include "trace/trace.h"
#include <stdlib.h>
#include "harness.h"
#include <stdio.h>

#define N 200000u

/* One exit-status decision for startup and the allocation guard. */
static int startup_result(int init_rc, const bench_samples *samples, size_t allocs) {
    return init_rc != PLAT_OK || !samples->n || samples->dropped ||
           bench_p50(samples) > UINT64_C(25000000) || bench_p99(samples) > UINT64_C(40000000) ||
           (edit_malloc_guard_active() && allocs) ? 1 : 0;
}

/* Synthetic inputs: no X traffic, clock reads, or benchmark measurements. */
static int startup_self_check(void) {
    uint64_t values[100];
    bench_samples s; bench_samples_init(&s, values, 100);
    for (size_t i = 0; i < 100; i++) bench_add(&s, UINT64_C(10000000));
    int failed = 0;
#define EXPECT_STATUS(want, label, rc) do { \
    int got = startup_result((rc), &s, 0); \
    if (got != (want)) { fprintf(stderr, "§23 FAIL %s: exit %d, want %d\n", label, got, want); failed = 1; } \
} while (0)
    EXPECT_STATUS(0, "within subset budget", PLAT_OK);
    EXPECT_STATUS(1, "supplied-display init failure", PLAT_ERR_FAIL);
    EXPECT_STATUS(1, "supplied-display connection failure", PLAT_ERR_NO_DISPLAY);
    for (size_t i = 0; i < s.n; i++) values[i] = UINT64_C(100000000);
    EXPECT_STATUS(1, "100 ms subset exceeds G4a", PLAT_OK);
    for (size_t i = 0; i < s.n; i++) values[i] = UINT64_C(25000000);
    values[98] = values[99] = UINT64_C(40000001);
    EXPECT_STATUS(1, "p99 miss with passing p50", PLAT_OK);
    values[98] = values[99] = UINT64_C(40000000);
    EXPECT_STATUS(0, "inclusive budget boundaries", PLAT_OK);
    s.dropped = 1; EXPECT_STATUS(1, "dropped sample", PLAT_OK);
    s.dropped = 0; s.n = 0; EXPECT_STATUS(1, "empty samples", PLAT_OK);
#undef EXPECT_STATUS
    puts(failed ? "§23 bench self-check: FAILED" : "§23 bench self-check: ok");
    return failed;
}

static int startup_bench(double load) {
    if (!getenv("DISPLAY") || !*getenv("DISPLAY")) { puts("TRACK x11 startup: no DISPLAY, skipped (optional infrastructure)"); return 0; }
    enum { START_N = 200 };
    uint64_t times[START_N]; bench_samples samples; bench_samples_init(&samples, times, START_N);
    trace_init(); trace_thread_register();
    /* One unmeasured warm-up. Each measured sample initializes a fresh connection/window. */
    for (unsigned i = 0; i <= START_N; i++) {
        plat p; plat_config cfg = { "startup bench", 1600, 900, false, -1, 0 };
        uint64_t t0 = bench_now_ns(); cfg.exec_ns = t0;
        int r = plat_init(&p, &cfg);
        if (r != PLAT_OK) { fprintf(stderr, "FAIL x11 startup: plat_init failed (%d) with supplied DISPLAY\n", r); return 1; }
        plat_map(&p);
        uint64_t end = bench_now_ns();
        if (i) bench_add(&samples, end - t0);
        x11_clip_set_limits(&p, 0, 0, UINT32_MAX);
        plat_shutdown(&p);
    }
    int failed = startup_result(PLAT_OK, &samples, 0);
    printf("TRACK x11 startup main->plat_init done->map requested: n=%zu p50=%.3f ms p99=%.3f ms (M)%s; "
           "G4a full startup p50<=25 ms p99<=40 ms (G); subset budget %s; excludes exec, first-frame raster and input acceptance; load=%.2f\n",
           samples.n, (double)bench_p50(&samples) / 1e6, (double)bench_p99(&samples) / 1e6, bench_evidence_tag(), failed ? "MISS" : "PASS", load);
    return failed;
}

int main(int argc, char **argv) {
    if (argc == 2 && strcmp(argv[1], "--self-check") == 0) return startup_self_check();
    char power[32];
    double load = 0;
    FILE *load_file = fopen("/proc/loadavg", "r");
    if (!load_file || fscanf(load_file, "%lf", &load) != 1) {
        if (load_file) fclose(load_file);
        fprintf(stderr, "cannot stamp one-minute load\n"); return 1;
    }
    fclose(load_file);
    printf("power before measurement: %s; one-minute load=%.2f\n", bench_battery_status(power, sizeof power), load);
    struct xkb_context *ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    struct xkb_rule_names rn = { "evdev", "pc105", "us", "", "" };
    struct xkb_keymap *km = xkb_keymap_new_from_names(ctx, &rn, XKB_KEYMAP_COMPILE_NO_FLAGS);
    x11_input in;
    if (!km || x11_input_init(&in, km, NULL) != 0) { fprintf(stderr, "no keymap\n"); return 1; }
    in.rep_rate_hz = 0;
    static uint64_t buf[N];
    bench_samples s;
    bench_samples_init(&s, buf, N);
    xcb_key_press_event_t ev;
    memset(&ev, 0, sizeof ev);
    plat_event e;
    edit_malloc_guard_begin();
    for (uint32_t i = 0; i < N; i++) {
        ev.detail = (uint8_t)(24 + i % 20);           /* letter row */
        ev.state = (i & 1) ? 1u : 0u;
        uint64_t t0 = bench_now_ns();
        x11_input_key(&in, &ev, true, t0, &e);
        uint64_t t1 = bench_now_ns();
        x11_input_key(&in, &ev, false, t1, &e);
        bench_add(&s, t1 - t0);
    }
    size_t allocs = edit_malloc_guard_end();
    printf("x11 key translate (press, us layout, no compose): n=%zu p50 %llu ns p99 %llu ns (TRACK, no separate G1 gate) (M)%s indicative on a loaded box; "
           "includes timing clock read; allocations on path: %zu%s; load=%.2f\n",
           s.n, (unsigned long long)bench_p50(&s), (unsigned long long)bench_p99(&s), bench_evidence_tag(),
           allocs, edit_malloc_guard_active() ? "" : " (guard inactive)", load);
    x11_input_destroy(&in);
    xkb_context_unref(ctx);
    int startup_failed = startup_bench(load);
    return startup_failed || (edit_malloc_guard_active() && allocs) ? 1 : 0;
}
