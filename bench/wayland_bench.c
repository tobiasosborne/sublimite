/* Wayland editor G1/G3, using the production editor/minimap and EGL renderer.
 * Bounds inherit perf/01-perf-target.md: G1 target A (E/M planning synthesis)
 * p50=1 ms, p99=2 ms; G3 p50=5 ms, p99=(P) (10^9 ns/s)/90 Hz/2.
 * Sample population inherits bench/harness.h's perf §4 policy; no new count.
 * Input is synthetic plat_event ingress, not a physical evdev measurement. */
#include "editor/editor.h"
#include "font/font.h"
#include "trace/trace.h"
#include "harness.h"
#include "interaction.h"
#include <xkbcommon/xkbcommon-keysyms.h>
#include <stdlib.h>
#include <string.h>

typedef struct wayland_bench_stamp { uint64_t ingress, t4, sequence; uint32_t frame; render_backend *backend; } wayland_bench_stamp;
static void ingress(void *user, uint64_t sequence, uint64_t ns)
{ wayland_bench_stamp *s = user; if (!s->ingress) { s->ingress = ns; s->sequence = sequence; } }
static void present(void *user, const editor_frame *frame)
{
    wayland_bench_stamp *s = user;
    if (s->ingress && !s->t4 && frame->first_sequence <= s->sequence && frame->last_sequence >= s->sequence) {
        s->t4 = s->backend->submitted_ns; s->frame = frame->id;
    }
}
static int settle(editor *e)
{
    for (;;) {
        editor_stats s = editor_get_stats(e);
        if (!s.pending && !s.render_active && !s.open_pending) return EDITOR_OK;
        int rc = editor_step(e, 0); if (rc != EDITOR_OK && rc != EDITOR_MORE) return rc;
    }
}
static void report(const char *name, bench_samples *samples, uint64_t bound50, uint64_t bound99)
{
    /* p50/p99: nearest-rank sorted samples as defined by bench/harness.h. */
    printf("BENCH name=%s status=TRACK evidence=(M)%s n=%zu derivation=completed_editor_operations "
        "p50_ns=%llu p99_ns=%llu percentile=nearest_rank bound_p50_ns=%llu bound_p99_ns=%llu "
        "bounds=(G)_perf_goal_sheet input=synthetic_platform_event\n", name, bench_evidence_tag(), samples->n,
        (unsigned long long)bench_p50(samples), (unsigned long long)bench_p99(samples),
        (unsigned long long)bound50, (unsigned long long)bound99);
}
int main(int argc, char **argv)
{
    if (argc < 3 || strcmp(argv[1], "--run")) {
        puts("wayland_bench: SKIP unmeasured; use --run PATH [SAMPLES] on a compositor with wp_presentation; no fabricated G1/G3 values");
        return 0;
    }
    size_t samples = BENCH_INTERACTION_MIN_N;
    if (argc > 4 || (argc == 4 && bench_interaction_samples(argv[3], &samples))) {
        puts("wayland_bench: invalid sample population; follows perf harness interaction policy"); return 0;
    }
    const char *display = getenv("WAYLAND_DISPLAY");
    if (!display || !*display) { puts("wayland_bench: SKIP unmeasured; WAYLAND_DISPLAY absent"); return 0; }
    const char *power = bench_evidence_tag(); /* reads BAT0 before any measurement */
    FILE *load = fopen("/proc/loadavg", "r"); double load1; bool have_load = false;
    if (load) { have_load = fscanf(load, "%lf", &load1) == 1; fclose(load); }
    if (have_load) printf("wayland_bench: power=%s load1=%g (M) from /proc/loadavg\n", power, load1);
    else printf("wayland_bench: power=%s load1=unknown\n", power);
    uint64_t *values = calloc(samples, sizeof *values);
    if (!values) { puts("wayland_bench: SKIP sample allocation unavailable"); return 0; }
    trace_init(); (void)trace_thread_register();
    render_backend b = {0}; editor *e = NULL; wayland_bench_stamp stamp = {.backend = &b};
    font_cell cell = font_ascii_cell();
    /* Target-A pixel dimensions inherited from bench/editor_bench.c.
     * Grid = floor(target pixels / baked cell pixels), same fixture. */
    editor_config cfg = {.path = argv[2], .cols = 2880u / cell.cell_w, .rows = 1800u / cell.cell_h,
        .font_px = font_ascii_px(), .platform_backend = PLAT_BACKEND_WAYLAND,
        .hook_ctx = &stamp, .on_ingress = ingress, .on_present = present};
    cfg.max_cols = cfg.cols; cfg.max_rows = cfg.rows;
    int rc = editor_backend_select(&b, NULL);
    if (!rc) rc = editor_open(&e, &cfg, &b);
    if (!rc) rc = settle(e);
    if (rc) { puts("wayland_bench: SKIP unmeasured; Wayland/EGL/editor init unavailable"); goto done; }
    render_dims actual = editor_grid(e)->dims;
    printf("wayland_bench: cols=%u rows=%u buffer_pixels=%ux%u evidence=(M)%s "
        "derivation=editor_grid_dims,cols*cell_w,rows*cell_h viewport=negotiated\n",
        actual.cols, actual.rows, actual.cols * actual.cell_w, actual.rows * actual.cell_h, power);
    for (unsigned full = 0; full <= 1 && !rc; full++) {
        bench_samples row; bench_samples_init(&row, values, samples);
        for (size_t i = 0; i < samples; i++) {
            stamp = (wayland_bench_stamp){.backend = &b};
            plat_event ev = {.kind = PLAT_EV_KEY, .press = true};
            if (full) ev.keysym = i % 2 ? XKB_KEY_Page_Up : XKB_KEY_Page_Down;
            else if (i % 2) ev.keysym = XKB_KEY_BackSpace;
            else { ev.keysym = XKB_KEY_a; ev.utf8[0] = 'a'; ev.utf8_len = sizeof "a" - 1; }
            rc = editor_inject(e, &ev);
            /* The first PageDown can keep the viewport fixed while moving the
             * caret. Pair navigation with exposure to require a full repaint,
             * and label this synthetic full-frame workload explicitly. */
            if (!rc && full) {
                plat_event expose = {.kind = PLAT_EV_EXPOSE, .w = actual.cols * actual.cell_w, .h = actual.rows * actual.cell_h};
                rc = editor_inject(e, &expose);
            }
            if (!rc) rc = editor_step(e, 0);
            if (rc == EDITOR_MORE) rc = EDITOR_OK;
            if (!rc) rc = settle(e);
            uint64_t end = full ? b.device_ns : stamp.t4;
            if (rc || !stamp.ingress || !stamp.frame || b.last_frame != stamp.frame || end < stamp.ingress ||
                (full && !editor_grid(e)->full_frame)) { rc = EDITOR_ERR_IO; break; }
            (void)bench_add(&row, end - stamp.ingress);
        }
        if (!rc) report(full ? "wayland_G3_navigation_full_repaint_T5" : "wayland_G1_typing_T4", &row,
            full ? UINT64_C(5000000) : UINT64_C(1000000), full ? UINT64_C(1000000000) / 90u / 2u : UINT64_C(2000000));
    }
    if (rc) puts("wayland_bench: incomplete operation; no qualifying G1/G3 measurement");
done:
    if (e) editor_close(e);
    free(values); return 0; /* Performance misses are tracked, per CLAUDE law. */
}
