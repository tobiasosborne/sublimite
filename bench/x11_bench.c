/* x11_bench.c - key translation cost (P2.2). TRACKED, no gate invented: G1 is end-to-end and measured later.
 * Exit non-zero only if the typing path allocates (Law 2) when the guard is real. Prints p50/p99 per call. */
#include "x11/input.h"
#include "base/base.h"
#include "harness.h"
#include <stdio.h>

#define N 200000u

int main(void) {
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
    char bat[32];
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
    printf("x11 key translate (press, us layout, no compose): p50 %llu ns p99 %llu ns (tracked, no gate) (M)[%s] loaded?; "
           "includes ~25 ns clock read; allocations on path: %zu%s\n",
           (unsigned long long)bench_p50(&s), (unsigned long long)bench_p99(&s), bench_battery_status(bat, sizeof bat),
           allocs, edit_malloc_guard_active() ? "" : " (guard inactive)");
    x11_input_destroy(&in);
    xkb_context_unref(ctx);
    return (edit_malloc_guard_active() && allocs) ? 1 : 0;
}
