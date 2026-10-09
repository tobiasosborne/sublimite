/* P2.5 CPU raster backend bench: shared backend bench contract. Drives the
 * backend ONLY through render.h on a real X11 window. G3 rows are gated;
 * everything else is TRACK. --track disables G3/G3z verdicts for battery/Xvfb.
 * Usage: raster_bench [--quick] [--idle N] [--track] [--kernel-only] */
#include "raster/raster.h"
#include "base/base.h"
#include "font/font.h"
#include "work/work.h"
#include "x11/plat.h"
#include "trace/trace.h"
#include "harness.h"
#include <inttypes.h>
#include <poll.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define G3_P50 UINT64_C(5000000)
#define G3_P99 UINT64_C(5560000)

typedef struct rig {
    plat pl; work_pool pool; render_backend b; void *state;
    render_config cfg; uint64_t t5, t6; uint32_t t5_id, t6_id;
    render_grid g; render_cell *cells; uint64_t *bits; render_strip *strips;
    render_atlas_page page; render_glyph glyphs[95];
    render_dims dims; uint32_t next_id; uint64_t rng;
    size_t present_allocs, submit_allocs;
    bool track;
} rig;

void raster_bench_noop(void *u, const plat_event *e);
static void hook5(void *u, uint32_t id, uint64_t ns) { rig *r = u; r->t5 = ns; r->t5_id = id; }
static void hook6(void *u, uint32_t id, uint64_t ns) { rig *r = u; r->t6 = ns; r->t6_id = id; }
static uint32_t rnd(rig *r) { r->rng = r->rng * 6364136223846793005ull + 1442695040888963407ull; return (uint32_t)(r->rng >> 33); }

static void pump_cb(const work_msg *m, void *ud)
{
    rig *r = ud;
    render_event ev = {RENDER_EVENT_WORK, m->generation, 0, m};
    (void)render_backend_event(&r->b, &ev);
}
static void pump(rig *r, int ms)
{
    struct pollfd p = {work_pool_eventfd(&r->pool), POLLIN, 0};
    (void)poll(&p, 1, ms);
    (void)work_mailbox_drain(&r->pool, pump_cb, r);
}

typedef struct init_arg { rig *r; int rc; uint64_t ns; } init_arg;
static void *init_thread(void *u)
{
    init_arg *a = u;
    (void)trace_thread_register();
    uint64_t t0 = bench_now_ns();
    a->rc = render_backend_init(&a->r->b, &a->r->cfg, a->r->state, a->r->b.info.state_size);
    a->ns = bench_now_ns() - t0;
    return NULL;
}
static int init_backend(rig *r, uint64_t *ns)
{
    init_arg a = {r, RENDER_ERR_INIT, 0};
    pthread_t th;
    if (pthread_create(&th, NULL, init_thread, &a) != 0) return -1;
    pthread_join(th, NULL);
    if (ns) *ns = a.ns;
    return a.rc;
}

static void fill_cell(rig *r, size_t i)
{
    static const uint32_t fgs[4] = {0xd0d0d0, 0xe0c080, 0x80c0e0, 0xffffff};
    static const uint32_t bgs[4] = {0x101820, 0x182028, 0x202830, 0x080c10};
    uint32_t v = rnd(r), ch = 32 + v % 95u;
    bool glyph = (v >> 8) % 10u < 7u && ch != 32;
    r->cells[i] = (render_cell){glyph ? ch : 0, glyph ? ch - 32u : RENDER_NO_SLOT, fgs[(v >> 12) & 3u],
                                bgs[(v >> 16) & 3u], 0, 0};
}

static int setup_rig(rig *r, uint32_t px, bool first)
{
    const font_ascii_atlas *a = font_ascii_atlas_for_px(px);
    if (!a) return -1;
    r->dims = (render_dims){2880u / a->cell.cell_w, 1800u / a->cell.cell_h, a->cell.cell_w, a->cell.cell_h};
    size_t n = (size_t)r->dims.cols * r->dims.rows, words = ((size_t)r->dims.rows + 63) / 64;
    r->cells = malloc(n * sizeof *r->cells); r->bits = calloc(words, 8);
    r->strips = malloc(((size_t)r->dims.rows / 2 + 1) * sizeof *r->strips);
    if (!r->cells || !r->bits || !r->strips) return -1;
    r->page = (render_atlas_page){a->pixels, a->pixels_len, (size_t)r->dims.cell_w * 95, r->dims.cell_w * 95, r->dims.cell_h};
    for (uint32_t s = 0; s < 95; s++)
        r->glyphs[s] = (render_glyph){32 + s, 0, s * r->dims.cell_w, 0, r->dims.cell_w, r->dims.cell_h};
    if (render_grid_init(&r->g, r->dims, r->cells, n, r->bits, words) != RENDER_OK) return -1;
    r->g.pages = &r->page; r->g.page_count = 1; r->g.glyphs = r->glyphs; r->g.glyph_count = 95;
    for (size_t i = 0; i < n; i++) fill_cell(r, i);
    memset(&r->b, 0, sizeof r->b);
    r->cfg = (render_config){.dims = r->dims, .max_width = 2880, .max_height = 1800, .max_cells = n,
        .max_glyphs = 95, .max_pages = 1, .max_atlas_bytes = a->pixels_len, .platform = &r->pl,
        .workers = &r->pool, .hooks = {hook5, hook6, r}};
    if (render_cpu_backend(&r->b) != RENDER_OK) return -1;
    r->state = aligned_alloc(64, (r->b.info.state_size + 63) & ~(size_t)63);
    if (!r->state) return -1;
    (void)first;
    return init_backend(r, NULL);
}
static void teardown_rig(rig *r)
{
    render_backend_shutdown(&r->b);
    free(r->state); free(r->cells); free(r->bits); free(r->strips);
}

/* Submit (full or one row) and present; returns ingress -> T5 (ns) or 0 on error.
 * Waits for T6 afterwards (outside the measurement). */
static uint64_t one_frame(rig *r, bool full, uint32_t row, uint64_t *t6_out)
{
    uint32_t id = r->next_id++;
    edit_malloc_guard_begin();
    if (full) for (int k = 0; k < 64; k++) fill_cell(r, rnd(r) % ((size_t)r->dims.cols * r->dims.rows));
    if (render_frame_begin(&r->g, id) != RENDER_OK) return 0;
    size_t count = 0;
    if (full) { if (render_mark_full(&r->g) != RENDER_OK) return 0; }
    else {
        for (uint32_t c = 0; c < r->dims.cols; c++) fill_cell(r, (size_t)row * r->dims.cols + c);
        if (render_mark_rows(&r->g, row, 1) != RENDER_OK) return 0;
    }
    if (render_dirty_strips(&r->g, r->strips, (size_t)r->dims.rows / 2 + 1, &count) != RENDER_OK) return 0;
    uint64_t t0 = bench_now_ns();
    if (render_backend_submit(&r->b, &r->g, r->strips, count) != RENDER_OK) return 0;
    r->submit_allocs = edit_malloc_guard_end();
    if (edit_malloc_guard_active() && r->submit_allocs) return 0;
    edit_malloc_guard_begin();
    int rc;
    uint64_t dl = t0 + UINT64_C(3000000000);
    while ((rc = render_backend_present(&r->b, id)) == RENDER_ERR_BUSY) {
        pump(r, 5);
        if (bench_now_ns() > dl) return 0;
    }
    if (rc != RENDER_OK) return 0;
    while (r->b.active) { pump(r, 5); if (bench_now_ns() > dl) return 0; }
    r->present_allocs = edit_malloc_guard_end();
    if (r->t5_id != id || r->t6_id != id) return 0;
    if (t6_out) *t6_out = r->t6;
    return r->t5 > t0 ? r->t5 - t0 : 1;
}

static int run_size(rig *r, uint32_t px, bool quick, int *fail)
{
    char name[96];
    const uint32_t warm = quick ? 50 : 200, n = quick ? 300 : 2000;
    uint64_t *vals = malloc(10000 * sizeof *vals);
    if (!vals) return -1;
    bench_samples s;
    enum { NSTAGE = 17 };
    uint64_t *stage_vals = malloc((size_t)NSTAGE * n * sizeof *stage_vals);
    bench_samples stage[NSTAGE];
    if (!stage_vals) return -1;
    for (size_t k = 0; k < NSTAGE; k++) bench_samples_init(&stage[k], stage_vals + k * n, n);
    size_t alloc_min = SIZE_MAX, alloc_max = 0, alloc_total = 0;
    /* full frame warm (G3) */
    for (uint32_t i = 0; i < warm; i++) if (!one_frame(r, true, 0, NULL)) { fprintf(stderr, "frame failed\n"); return -1; }
    bench_samples_init(&s, vals, n);
    for (uint32_t i = 0; i < n; i++) {
        uint64_t t = one_frame(r, true, 0, NULL); if (!t) return -1; (void)bench_add(&s, t);
        raster_metrics m;
        if (!raster_frame_metrics(&r->b, &m)) return -1;
        for (uint32_t j = 0; j < RASTER_JOBS; j++) {
            (void)bench_add(&stage[j], m.strip_ns[j]);
            (void)bench_add(&stage[4u + j], m.queue_ns[j]);
            (void)bench_add(&stage[12u + j], m.upload_issue_ns[j]);
        }
        (void)bench_add(&stage[8], m.ready_ns - m.submit_ns);
        (void)bench_add(&stage[9], m.present_ns > m.ready_ns ? m.present_ns - m.ready_ns : 0);
        (void)bench_add(&stage[10], m.issue_ns);
        (void)bench_add(&stage[11], m.server_ns - m.present_ns);
        (void)bench_add(&stage[16], m.upload_queued_ns - m.submit_ns);
        if (r->present_allocs < alloc_min) alloc_min = r->present_allocs;
        if (r->present_allocs > alloc_max) alloc_max = r->present_allocs;
        alloc_total += r->present_allocs;
    }
    snprintf(name, sizeof name, "full_frame_warm_%upx_%s", px, r->track ? "G3_TRACK" : "G3");
    *fail |= bench_report(name, &s, r->track ? 0 : G3_P50, r->track ? 0 : G3_P99);
    const char *stage_name[NSTAGE] = {"strip0", "strip1", "strip2", "strip3", "queue0", "queue1", "queue2", "queue3",
        "submit_to_strips_ready", "ready_to_present", "present_issue", "server_tail_completion", "upload_issue0", "upload_issue1", "upload_issue2", "upload_issue3", "submit_to_uploads_queued"};
    for (size_t k = 0; k < NSTAGE; k++) {
        snprintf(name, sizeof name, "stage_%s_%upx_TRACK", stage_name[k], px);
        (void)bench_report(name, &stage[k], 0, 0);
    }
    printf("ALLOC typing_path_%upx frames=%u total=0 guard=%d (input/grid mutation/submit, our code) (M)%s\n",
        px, n, edit_malloc_guard_active(), bench_evidence_tag());
    printf("ALLOC present_path_%upx frames=%u total=%zu min=%zu max=%zu mean=%.3f guard=%d (M)%s\n",
        px, n, alloc_total, alloc_min, alloc_max, (double)alloc_total / n, edit_malloc_guard_active(), bench_evidence_tag());
    free(stage_vals);
    /* typing row (TRACK) */
    bench_samples_init(&s, vals, n);
    for (uint32_t i = 0; i < n; i++) { uint64_t t = one_frame(r, false, r->dims.rows / 2, NULL); if (!t) return -1; (void)bench_add(&s, t); }
    snprintf(name, sizeof name, "typing_row_%upx_TRACK", px);
    (void)bench_report(name, &s, 0, 0);
    /* kernel alone: one strip row, and a full frame single-threaded, SSE2 */
    raster_scene sc = {r->dims, r->cells, r->glyphs, 95, &r->page, 1, 0xff000000u};
    size_t stride = r->dims.cols * r->dims.cell_w;
    uint32_t *surf = malloc(stride * r->dims.cell_h * r->dims.rows * 4);
    if (!surf) return -1;
    bench_samples_init(&s, vals, 10000);
    for (uint32_t i = 0; i < 10000; i++) BENCH_TIME(&s, raster_row_sse2(&sc, surf, stride, i % r->dims.rows));
    snprintf(name, sizeof name, "kernel_sse2_one_cell_row_%upx_TRACK", px);
    (void)bench_report(name, &s, 0, 0);
    bench_samples_init(&s, vals, 10000);
    for (uint32_t i = 0; i < 10000; i++) BENCH_TIME(&s, raster_row_scalar(&sc, surf, stride, i % r->dims.rows));
    snprintf(name, sizeof name, "kernel_scalar_one_cell_row_%upx_TRACK", px);
    (void)bench_report(name, &s, 0, 0);
    bench_samples_init(&s, vals, 200);
    for (uint32_t i = 0; i < 200; i++)
        BENCH_TIME(&s, for (uint32_t row = 0; row < r->dims.rows; row++)
                            raster_row_sse2(&sc, surf + (size_t)row * r->dims.cell_h * stride, stride, row));
    snprintf(name, sizeof name, "kernel_sse2_full_frame_1thread_%upx_TRACK", px);
    (void)bench_report(name, &s, 0, 0);
    free(surf); free(vals);
    return 0;
}

/* All variants see the same cells, warmed surface and interleaved rounds.
 * Alternating order avoids assigning one candidate all of a frequency/load drift. */
static int paired_kernels(rig *r, uint32_t px, uint32_t samples)
{
    size_t stride = (size_t)r->dims.cols * r->dims.cell_w;
    size_t bytes = stride * r->dims.cell_h * r->dims.rows * 4u;
    uint32_t *surf = aligned_alloc(64, (bytes + 63u) & ~(size_t)63u);
    uint64_t *vals = malloc((size_t)samples * 3u * sizeof *vals);
    if (!surf || !vals) { free(surf); free(vals); return -1; }
    memset(surf, 0, bytes);
    raster_scene sc = {r->dims, r->cells, r->glyphs, 95, &r->page, 1, 0xff000000u};
    raster_palette palette = {0};
    bench_samples stage[3];
    for (uint32_t k = 0; k < 3; k++) bench_samples_init(&stage[k], vals + (size_t)k * samples, samples);
    for (uint32_t i = 0; i < samples; i++) {
        for (uint32_t ord = 0; ord < 3; ord++) {
            uint32_t k = i % 2 ? 2u - ord : ord;
            palette.rebuilds = 0;
            uint64_t start = bench_now_ns();
            for (uint32_t row = 0; row < r->dims.rows; row++) {
                uint32_t *dst = surf + (size_t)row * r->dims.cell_h * stride;
                switch (k) {
                case 0: raster_row_sse2(&sc, dst, stride, row); break;
                case 1: raster_row_cached(&sc, dst, stride, row, &palette); break;
                default: raster_row_sse2_stream(&sc, dst, stride, row); break;
                }
            }
            (void)bench_add(&stage[k], bench_now_ns() - start);
        }
    }
    const char *names[3] = {"sse2", "cached", "stream"};
    for (uint32_t k = 0; k < 3; k++) {
        char name[96]; snprintf(name, sizeof name, "paired_kernel_%s_full_frame_%upx_TRACK", names[k], px);
        (void)bench_report(name, &stage[k], 0, 0);
    }
    free(vals); free(surf);
    return 0;
}

static int scroll_test(rig *r, bool quick, int *fail)
{
    const uint32_t frames = quick ? 300 : 10000;
    uint64_t msc_prev = 0, misses = 0, delivered = 0, zero_gap = 0, maxgap = 0;
    for (uint32_t i = 0; i < frames; i++) {
        uint64_t t6;
        if (!one_frame(r, true, 0, &t6)) { printf("SKIP scroll_10k: frame failed (%u)\n", i); return -1; }
        raster_metrics m;
        if (!raster_frame_metrics(&r->b, &m) || m.present_kind != 0 || m.present_mode == 2) {
            printf("SKIP scroll_10k_G3z: matching pixmap was not displayed (frame %u)\n", i);
            return 0;
        }
        uint64_t ust = 0, msc = 0;
        (void)raster_last_present(&r->b, &ust, &msc);
        if (i > 0) {
            if (msc <= msc_prev) zero_gap++;
            else { uint64_t gap = msc - msc_prev; if (gap > 1) misses += gap - 1; if (gap > maxgap) maxgap = gap; }
        }
        msc_prev = msc; delivered++;
    }
    if (zero_gap != 0) {
        printf("SKIP scroll_10k_G3z: Present MSC not advancing per frame (%" PRIu64 " zero gaps of %" PRIu64 ")\n", zero_gap, delivered);
        return 0;
    }
    if (r->track) {
        printf("SCROLL name=scroll_10k_TRACK frames=%" PRIu64 " missed_refreshes=%" PRIu64
            " max_msc_gap=%" PRIu64 " zero_gap=%" PRIu64 " target_misses=0 verdict=TRACK power=%s\n",
            delivered, misses, maxgap, zero_gap, bench_evidence_tag());
        return 0;
    }
    printf("SCROLL name=scroll_10k_G3z frames=%" PRIu64 " missed_refreshes=%" PRIu64 " max_msc_gap=%" PRIu64
           " zero_gap=%" PRIu64 " gate_misses=0 pass=%d power=%s\n", delivered, misses, maxgap, zero_gap,
           misses == 0, bench_evidence_tag());
    if (misses) *fail = 1;
    return 0;
}

int main(int argc, char **argv)
{
    (void)setvbuf(stdout, NULL, _IOLBF, 0);
    bool quick = false, kernel_only = false, track = false; int idle_n = 5; uint32_t kernel_samples = 200;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--quick")) { quick = true; idle_n = 1; }
        else if (!strcmp(argv[i], "--track")) track = true;
        else if (!strcmp(argv[i], "--kernel-only")) kernel_only = true;
        else if (!strcmp(argv[i], "--kernel-samples") && i + 1 < argc) {
            int value = atoi(argv[++i]); if (value < 1 || value > 10000) return 1;
            kernel_samples = (uint32_t)value;
        }
        else if (!strcmp(argv[i], "--idle") && i + 1 < argc) idle_n = atoi(argv[++i]);
    }
    char status[32]; bench_battery_status(status, sizeof status);
    printf("power=%s %s; (M) indicative under concurrent builds; backend=cpu-raster (SSE2 bounded colour tables, MT4, XShm)\n", status, bench_evidence_tag());
    puts("minimap not built (perf s2.2 CPU MT4 allowance 0.26 ms (E); GPU allowance 0.18 ms (E)); G3 here = submit -> T5 (X Sync Trigger/Await/QueryFence after off-screen XShm upload, T5 >= PresentPixmap submission)");
    if (!getenv("DISPLAY")) { puts("SKIP: no DISPLAY"); return 0; }
    trace_init(); (void)trace_thread_register();
    rig *r = calloc(1, sizeof *r);
    if (!r) return 1;
    r->track = track;
    if (track) puts("TRACK run: G3 reference p50 <= 5.0 ms / p99 <= 5.56 ms; G3z reference 0 misses. No gate verdict on this fixture.");
    plat_config pc = {"raster_bench", 2880, 1800, false, -1, 0};
    if (plat_init(&r->pl, &pc) != PLAT_OK) { puts("SKIP: cannot open display"); return 0; }
    if (work_pool_init(&r->pool, 1, 4) != 0) return 1;
    plat_map(&r->pl);
    plat_callbacks cb = {0};
    int fail = 0;
    cb.on_event = raster_bench_noop;
    (void)plat_run_for(&r->pl, &cb, 500); /* settle map/expose */
    uint64_t v[8]; bench_samples is; bench_samples_init(&is, v, 8);
    /* timed separately (setup_rig includes grid fill): measure backend init alone */
    for (int k = 0; k < 5; k++) {
        const font_ascii_atlas *a = font_ascii_atlas_for_px(15);
        memset(&r->b, 0, sizeof r->b);
        r->cfg = (render_config){.dims = {360, 120, 8, 15}, .max_width = 2880, .max_height = 1800, .max_cells = 43200,
            .max_glyphs = 95, .max_pages = 1, .max_atlas_bytes = a->pixels_len, .platform = &r->pl, .workers = &r->pool};
        if (render_cpu_backend(&r->b) != RENDER_OK) return 1;
        r->state = aligned_alloc(64, (r->b.info.state_size + 63) & ~(size_t)63);
        uint64_t ns = 0;
        if (init_backend(r, &ns) != RENDER_OK) { puts("FAIL: init"); return 1; }
        (void)bench_add(&is, ns);
        render_backend_shutdown(&r->b); free(r->state);
    }
    (void)bench_report("init_cost_TRACK", &is, 0, 0);
    static const uint32_t sizes[2] = {15, 30};
    for (int si = 0; si < 2; si++) {
        r->next_id = 1; r->rng = 12345;
        if (setup_rig(r, sizes[si], false) != 0) { puts("FAIL: setup"); return 1; }
        r->next_id = 1;
        if (!kernel_only && run_size(r, sizes[si], quick, &fail) != 0) { puts("FAIL: run"); return 1; }
        if (paired_kernels(r, sizes[si], kernel_samples) != 0) return 1;
        if (!kernel_only && si == 0) {
            if (scroll_test(r, quick, &fail) != 0) puts("scroll_10k skipped");
        }
        if (!kernel_only && idle_n > 0) {
            bench_samples_init(&is, v, 8);
            for (int k = 0; k < idle_n && k < 8; k++) {
                sleep(quick ? 1 : 15);
                uint64_t t = one_frame(r, true, 0, NULL);
                if (!t) { puts("FAIL: idle frame"); return 1; }
                (void)bench_add(&is, t);
            }
            char idle_name[80];
            snprintf(idle_name, sizeof idle_name, "first_frame_after_idle_%upx_G3i_TRACK", sizes[si]);
            (void)bench_report(idle_name, &is, 0, 0);
        }
        teardown_rig(r);
    }
    work_pool_shutdown(&r->pool);
    plat_shutdown(&r->pl);
    return fail ? 1 : 0;
}
void raster_bench_noop(void *u, const plat_event *e) { (void)u; (void)e; }
