/* x11_bench.c - key translation cost (P2.2). TRACKED, no gate invented: G1 is end-to-end and measured later.
 * Exit non-zero only if the typing path allocates (Law 2) when the guard is real. Prints p50/p99 per call. */
#include "x11/input.h"
#include "base/base.h"
#include "x11/clip.h"
#include "trace/trace.h"
#include <stdlib.h>
#include "harness.h"
#include <stdio.h>

#define N 200000u

static void startup_bench(void) {
    if (!getenv("DISPLAY") || !*getenv("DISPLAY")) { puts("TRACK x11 startup: no DISPLAY, skipped"); return; }
    enum { START_N = 200 };
    uint64_t times[START_N]; bench_samples samples; bench_samples_init(&samples, times, START_N);
    trace_init(); trace_thread_register();
    /* One unmeasured warm-up. Each measured sample initializes a fresh connection/window. */
    for (unsigned i = 0; i <= START_N; i++) {
        plat p; plat_config cfg = { "startup bench", 100, 100, false, -1, 0 };
        uint64_t t0 = bench_now_ns(); cfg.exec_ns = t0;
        int r = plat_init(&p, &cfg);
        if (r != PLAT_OK) { printf("TRACK x11 startup: plat_init failed (%d), skipped\n", r); return; }
        plat_map(&p);
        uint64_t end = bench_now_ns();
        if (i) bench_add(&samples, end - t0);
        x11_clip_set_limits(&p, 0, 0, UINT32_MAX);
        plat_shutdown(&p);
    }
    printf("TRACK x11 startup main->plat_init done->map requested: n=%zu p50=%.3f ms p99=%.3f ms (M)%s; "
           "G4a full startup p50<=25 ms p99<=40 ms (G); excludes exec and first-frame raster; loaded box\n",
           samples.n, (double)bench_p50(&samples) / 1e6, (double)bench_p99(&samples) / 1e6, bench_evidence_tag());
}

int main(void) {
    char power[32];
    printf("power before measurement: %s\n", bench_battery_status(power, sizeof power));
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
    printf("x11 key translate (press, us layout, no compose): n=%zu p50 %llu ns p99 %llu ns (TRACK, baseline p50 341 ns (M), no separate G1 gate) (M)%s indicative on a loaded box; "
           "includes timing clock read; allocations on path: %zu%s\n",
           s.n, (unsigned long long)bench_p50(&s), (unsigned long long)bench_p99(&s), bench_evidence_tag(),
           allocs, edit_malloc_guard_active() ? "" : " (guard inactive)");
    x11_input_destroy(&in);
    xkb_context_unref(ctx);
    startup_bench();
    return (edit_malloc_guard_active() && allocs) ? 1 : 0;
}
