/* Raster benchmark: ingress (mutation + damage) -> T5 through render.h.
 * Missing minimap, real bulk integration and vblank make acceptance unmeasured:
 * exit 2 in gate mode, exit 1 on error/miss, explicit --track permits subsets.
 * Usage: [--quick] [--idle 0..8] [--track] [--target A|B|all]
 *        [--kernel-only] [--kernel-samples N] | --self-check
 *        --scroll-track [--target A|B|all] | --pace-self-check */
#include "raster/raster.h"
#include "base/base.h"
#include "font/font.h"
#include "work/work.h"
#include "x11/plat.h"
#include "trace/trace.h"
#include "harness.h"
#include "render_pace.h"
#include <inttypes.h>
#include <poll.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define G3_P50 UINT64_C(5000000)
#define G3_P99 (UINT64_C(1000000000) / (2u * 90u))

/* Verdict helpers are also exercised without a display or timing run. */
typedef struct target_spec {
    const char *name;
    uint32_t width, height, hz;
    uint64_t warm50, warm99, idle50, idle99;
} target_spec;
static target_spec target_for(bool b)
{
    if (b) return (target_spec){"B",1920,1080,60,UINT64_C(4000000),
        UINT64_C(5000000),UINT64_C(10000000),UINT64_C(1000000000)/60};
    return (target_spec){"A",2880,1800,90,G3_P50,G3_P99,
        UINT64_C(8000000),UINT64_C(1000000000)/90};
}
static unsigned bulk_case_count(void) { return 5; }
static bool subset_can_gate(void) { return false; }
static int fixture_status(bool track) { return track ? 0 : 2; }
static int cadence_status(bool track, uint64_t intervals, uint64_t misses,
                          uint64_t invalid, uint64_t dropped, bool error)
{
    /* TRACK relaxes sample-count acceptance only; data errors still fail. */
    if (error || invalid || dropped || misses) return 1;
    return !track && intervals != 10000;
}
/* Use harness statistics, with a load stamp on every measured row. */
static int raster_report_fp(FILE *out, const char *name, const bench_samples *samples,
                            uint64_t gate50, uint64_t gate99)
{
    uint64_t p50 = bench_p50(samples), p99 = bench_p99(samples), lo, hi;
    bench_ci95(samples,0.5,&lo,&hi);
    bool pass = samples->n && !samples->dropped && (!gate50 || p50 <= gate50) && (!gate99 || p99 <= gate99);
    char load[32] = "unknown";
    FILE *load_file = fopen("/proc/loadavg","r");
    if (load_file) { if (fscanf(load_file,"%31s",load) != 1) strcpy(load,"unknown"); fclose(load_file); }
    fprintf(out,"BENCH name=%s n=%zu p50=%" PRIu64 " p99=%" PRIu64 " ci95=[%" PRIu64 ",%" PRIu64
        "] gate_p50=%" PRIu64 " gate_p99=%" PRIu64 " pass=%d evidence=(M)%s load1=%s verdict=TRACK\n",
        name,samples->n,p50,p99,lo,hi,gate50,gate99,pass,bench_evidence_tag(),load);
    return pass ? 0 : 1;
}
static int report_idle(FILE *out, const char *name, bench_samples *s,
                       target_spec target, bool track, unsigned idle_seconds)
{
    bool gated = !track && idle_seconds >= 15;
    return raster_report_fp(out, name, s, gated ? target.idle50 : 0,
                           gated ? target.idle99 : 0);
}


typedef struct rig {
    plat pl; work_pool pool; render_backend b; void *state;
    render_config cfg; uint64_t t5, t6; uint32_t t5_id, t6_id;
    render_grid g; render_cell *cells; uint64_t *bits; render_strip *strips;
    render_atlas_page page; render_glyph glyphs[95];
    render_dims dims; uint32_t next_id; uint64_t rng;
    size_t present_allocs, submit_allocs;
    bool track; int event_error;
    target_spec target; const char *bulk_name; char load_stamp[32];
    uint64_t ingress_ns, first_mutation_ns;
} rig;

void raster_bench_noop(void *u, const plat_event *e);
static void hook5(void *u, uint32_t id, uint64_t ns) { rig *r = u; r->t5 = ns; r->t5_id = id; }
static void hook6(void *u, uint32_t id, uint64_t ns) { rig *r = u; r->t6 = ns; r->t6_id = id; }
static uint32_t rnd(rig *r) { r->rng = r->rng * 6364136223846793005ull + 1442695040888963407ull; return (uint32_t)(r->rng >> 33); }

static void pump_cb(const work_msg *m, void *ud)
{
    rig *r = ud;
    render_event ev = {RENDER_EVENT_WORK, m->generation, 0, m};
    int rc = render_backend_event(&r->b, &ev);
    if (rc != RENDER_OK && rc != RENDER_ERR_UNSUPPORTED) r->event_error = rc;
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
    if (!r->first_mutation_ns) r->first_mutation_ns = bench_now_ns();
    static const uint32_t fgs[4] = {0xd0d0d0, 0xe0c080, 0x80c0e0, 0xffffff};
    static const uint32_t bgs[4] = {0x101820, 0x182028, 0x202830, 0x080c10};
    uint32_t v = rnd(r), ch = 32 + v % 95u;
    bool glyph = (v >> 8) % 10u < 7u && ch != 32;
    r->cells[i] = (render_cell){glyph ? ch : 0, glyph ? ch - 32u : RENDER_NO_SLOT, fgs[(v >> 12) & 3u],
                                bgs[(v >> 16) & 3u], 0, 0};
}

static int setup_rig(rig *r, uint32_t px)
{
    const font_ascii_atlas *a = font_ascii_atlas_for_px(px);
    if (!a) return -1;
    r->dims = (render_dims){r->target.width / a->cell.cell_w, r->target.height / a->cell.cell_h, a->cell.cell_w, a->cell.cell_h};
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
    r->cfg = (render_config){.dims = r->dims, .max_width = r->target.width, .max_height = r->target.height, .max_cells = n,
        .max_glyphs = 95, .max_pages = 1, .max_atlas_bytes = a->pixels_len, .platform = &r->pl,
        .workers = &r->pool, .hooks = {hook5, hook6, r}};
    if (render_cpu_backend(&r->b) != RENDER_OK) return -1;
    r->state = aligned_alloc(64, (r->b.info.state_size + 63) & ~(size_t)63);
    if (!r->state) return -1;
    return init_backend(r, NULL);
}
static void teardown_rig(rig *r)
{
    render_backend_shutdown(&r->b);
    free(r->state); free(r->cells); free(r->bits); free(r->strips);
    r->state = NULL; r->cells = NULL; r->bits = NULL; r->strips = NULL;
}

/* Submit (full or one row) and present; returns ingress -> T5 (ns) or 0 on error.
 * Waits for T6 afterwards (outside the measurement). */
static uint64_t one_frame_impl(rig *r, bool full, uint32_t row, uint64_t *t6_out,
                               bool scrolling, render_pace_frame *pace)
{
    uint32_t id = r->next_id++;
    uint64_t t0 = bench_now_ns();
    r->ingress_ns = t0;
    r->first_mutation_ns = 0;
    r->event_error = RENDER_OK;
    edit_malloc_guard_begin();
    if (scrolling) {
        size_t n = (size_t)r->dims.cols * r->dims.rows;
        memmove(r->cells,r->cells + r->dims.cols,(n - r->dims.cols) * sizeof *r->cells);
        for (size_t k = n - r->dims.cols; k < n; k++) fill_cell(r,k);
    } else if (full) for (int k = 0; k < 64; k++) fill_cell(r, rnd(r) % ((size_t)r->dims.cols * r->dims.rows));
    if (render_frame_begin(&r->g, id) != RENDER_OK) goto error;
    size_t count = 0;
    if (full) { if (render_mark_full(&r->g) != RENDER_OK) goto error; }
    else {
        for (uint32_t c = 0; c < r->dims.cols; c++) fill_cell(r, (size_t)row * r->dims.cols + c);
        if (render_mark_rows(&r->g, row, 1) != RENDER_OK) goto error;
    }
    if (render_dirty_strips(&r->g, r->strips, (size_t)r->dims.rows / 2 + 1, &count) != RENDER_OK) goto error;
    uint64_t submit_start = bench_now_ns();
    if (render_backend_submit(&r->b, &r->g, r->strips, count) != RENDER_OK) goto error;
    uint64_t submit_end = bench_now_ns();
    r->submit_allocs = edit_malloc_guard_end();
    if (edit_malloc_guard_active() && r->submit_allocs) goto error;
    edit_malloc_guard_begin();
    int rc;
    uint64_t dl = t0 + UINT64_C(3000000000);
    while ((rc = render_backend_present(&r->b, id)) == RENDER_ERR_BUSY) {
        pump(r, 5);
        if (r->event_error != RENDER_OK || bench_now_ns() > dl) goto error;
    }
    if (rc != RENDER_OK) goto error;
    while (r->b.active) { pump(r, 5); if (r->event_error != RENDER_OK || bench_now_ns() > dl) goto error; }
    r->present_allocs = edit_malloc_guard_end();
    if (r->event_error != RENDER_OK || r->t5_id != id || r->t6_id != id || r->t5 <= t0) goto error;
    if (t6_out) *t6_out = r->t6;
    if (pace) {
        pace->complete_ns = r->t6;
        pace->stage[0] = submit_start - t0;
        pace->stage[1] = submit_end - submit_start;
        pace->stage[2] = r->t5 - t0;
    }
    return r->t5 - t0;
error:
    (void)edit_malloc_guard_end();
    return 0;
}
static uint64_t one_frame(rig *r, bool full, uint32_t row, uint64_t *t6_out)
{ return one_frame_impl(r,full,row,t6_out,false,NULL); }

static int raster_pace_frame(void *user, bool scrolling, render_pace_frame *out)
{
    rig *r = user;
    if (!one_frame_impl(r,true,0,NULL,scrolling,out)) return -1;
    raster_metrics m; uint64_t ust;
    if (!raster_frame_metrics(&r->b,&m) || m.frame_id != r->t6_id ||
        !raster_last_present(&r->b,&ust,&out->msc)) return -1;
    out->msc_available = true;
    out->drops_available = true;
    out->dropped = m.present_kind != 0 || m.present_mode == 2;
    for (size_t j = 0; j < RASTER_JOBS; j++) {
        out->stage[3 + j] = m.strip_ns[j];
        out->stage[7 + j] = m.queue_ns[j];
        out->stage[15 + j] = m.upload_issue_ns[j];
    }
    if (m.ready_ns < m.submit_ns || m.server_ns < m.present_ns ||
        m.upload_queued_ns < m.submit_ns) return -1;
    out->stage[11] = m.ready_ns - m.submit_ns;
    out->stage[12] = m.present_ns > m.ready_ns ? m.present_ns - m.ready_ns : 0;
    out->stage[13] = m.issue_ns;
    out->stage[14] = m.server_ns - m.present_ns;
    out->stage[19] = m.upload_queued_ns - m.submit_ns;
    if (r->t6 < r->t5) return -1;
    out->stage[20] = r->t6 - r->t5;
    return 0;
}
static void raster_scroll_track(rig *r, uint32_t px)
{
    char name[96]; snprintf(name,sizeof name,"%s_raster_scroll_600_%upx",r->target.name,px);
    const char *reason = render_pace_skip_reason(getenv("DISPLAY"));
    if (reason) { render_pace_skip(stdout,name,reason); return; }
    const char *names[] = {"grid_mutation_damage", "submit", "ingress_to_T5",
        "strip0", "strip1", "strip2", "strip3", "queue0", "queue1", "queue2", "queue3",
        "submit_to_strips_ready", "ready_to_present", "present_issue", "server_tail_completion",
        "upload_issue0", "upload_issue1", "upload_issue2", "upload_issue3", "submit_to_uploads_queued",
        "T5_to_completion"};
    render_pace_run(stdout,name,names,sizeof names / sizeof names[0],raster_pace_frame,r);
}

static int run_size(rig *r, uint32_t px, bool quick, int *fail)
{
    char name[96];
    const uint32_t warm = quick ? 10 : 200, n = quick ? 100 : 10000;
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
    snprintf(name, sizeof name, "%s_%s_full_frame_warm_%upx_subset_TRACK", r->target.name,r->bulk_name,px);
    *fail |= raster_report_fp(stdout,name,&s,0,0);
    /* Missing minimap/contention integration cannot establish a G3 PASS.
     * A subset that exceeds the budget is nevertheless sufficient to fail. */
    if (!r->track && (bench_p50(&s) > r->target.warm50 || bench_p99(&s) > r->target.warm99)) *fail = 1;
    const char *stage_name[NSTAGE] = {"strip0", "strip1", "strip2", "strip3", "queue0", "queue1", "queue2", "queue3",
        "submit_to_strips_ready", "ready_to_present", "present_issue", "server_tail_completion", "upload_issue0", "upload_issue1", "upload_issue2", "upload_issue3", "submit_to_uploads_queued"};
    for (size_t k = 0; k < NSTAGE; k++) {
        snprintf(name, sizeof name, "%s_%s_stage_%s_%upx_TRACK", r->target.name,r->bulk_name,stage_name[k],px);
        *fail |= raster_report_fp(stdout,name,&stage[k],0,0);
    }
    printf("ALLOC typing_path_%upx frames=%u total=0 guard=%d (input/grid mutation/submit, our code) (M)%s load1=%s\n",
        px, n, edit_malloc_guard_active(), bench_evidence_tag(),r->load_stamp);
    printf("ALLOC present_path_%upx frames=%u total=%zu min=%zu max=%zu mean=%.3f guard=%d (M)%s load1=%s\n",
        px, n, alloc_total, alloc_min, alloc_max, (double)alloc_total / n, edit_malloc_guard_active(), bench_evidence_tag(),r->load_stamp);
    free(stage_vals);
    /* typing row (TRACK) */
    bench_samples_init(&s, vals, n);
    for (uint32_t i = 0; i < n; i++) { uint64_t t = one_frame(r, false, r->dims.rows / 2, NULL); if (!t) return -1; (void)bench_add(&s, t); }
    snprintf(name, sizeof name, "%s_%s_typing_row_%upx_TRACK", r->target.name,r->bulk_name,px);
    *fail |= raster_report_fp(stdout,name,&s,0,0);
    if (!strcmp(r->bulk_name,"idle")) {
    /* kernel alone: one strip row, and a full frame single-threaded, SSE2 */
    raster_scene sc = {r->dims, r->cells, r->glyphs, 95, &r->page, 1, 0xff000000u};
    size_t stride = r->dims.cols * r->dims.cell_w;
    uint32_t *surf = malloc(stride * r->dims.cell_h * r->dims.rows * 4);
    if (!surf) return -1;
    bench_samples_init(&s, vals, 10000);
    for (uint32_t i = 0; i < 10000; i++) BENCH_TIME(&s, raster_row_sse2(&sc, surf, stride, i % r->dims.rows));
    snprintf(name, sizeof name, "%s_kernel_sse2_one_cell_row_%upx_TRACK", r->target.name,px);
    *fail |= raster_report_fp(stdout,name,&s,0,0);
    bench_samples_init(&s, vals, 10000);
    for (uint32_t i = 0; i < 10000; i++) BENCH_TIME(&s, raster_row_scalar(&sc, surf, stride, i % r->dims.rows));
    snprintf(name, sizeof name, "%s_kernel_scalar_one_cell_row_%upx_TRACK", r->target.name,px);
    *fail |= raster_report_fp(stdout,name,&s,0,0);
    bench_samples_init(&s, vals, 200);
    for (uint32_t i = 0; i < 200; i++)
        BENCH_TIME(&s, for (uint32_t row = 0; row < r->dims.rows; row++)
                            raster_row_sse2(&sc, surf + (size_t)row * r->dims.cell_h * stride, stride, row));
    snprintf(name, sizeof name, "%s_kernel_sse2_full_frame_1thread_%upx_TRACK", r->target.name,px);
    *fail |= raster_report_fp(stdout,name,&s,0,0);
    free(surf);
    }
    free(vals);
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
        char name[96]; snprintf(name, sizeof name, "%s_paired_kernel_%s_full_frame_%upx_TRACK", r->target.name,names[k],px);
        if (raster_report_fp(stdout,name,&stage[k],0,0) != 0) { free(vals); free(surf); return -1; }
    }
    free(vals); free(surf);
    return 0;
}

typedef struct cadence {
    uint64_t previous, intervals, misses, invalid, dropped, maxgap;
    bool baseline, error;
} cadence;
static void cadence_observe(cadence *c, uint64_t msc, uint32_t kind, uint32_t mode)
{
    if (kind != 0 || mode == 2) c->dropped++;
    if (c->baseline) {
        c->intervals++;
        if (msc <= c->previous) c->invalid++;
        else {
            uint64_t gap = msc - c->previous;
            c->misses += gap - 1;
            if (gap > c->maxgap) c->maxgap = gap;
        }
    } else if (!msc) c->invalid++;
    c->previous = msc; c->baseline = true;
}
static int scroll_test(rig *r, bool quick, int *fail)
{
    const uint32_t frames = quick ? 300 : 10000;
    cadence c = {0};
    /* One displayed baseline followed by the required number of intervals. */
    for (uint32_t i = 0; i <= frames; i++) {
        if (!one_frame(r,true,0,NULL)) { c.error = true; break; }
        raster_metrics m; uint64_t ust, msc;
        if (!raster_frame_metrics(&r->b,&m) || !raster_last_present(&r->b,&ust,&msc)) {
            c.error = true; break;
        }
        cadence_observe(&c,msc,m.present_kind,m.present_mode);
        if (c.dropped || c.invalid) break;
    }
    int bad = cadence_status(r->track,c.intervals,c.misses,c.invalid,c.dropped,c.error);
    bool failed = c.error || c.invalid || c.dropped || (!r->track && bad);
    char load[32] = "unknown";
    FILE *load_file = fopen("/proc/loadavg","r");
    if (load_file) { if (fscanf(load_file,"%31s",load) != 1) strcpy(load,"unknown"); fclose(load_file); }
    printf("SCROLL name=%s_%s_scroll_%u_TRACK intervals=%" PRIu64
        " missed_refreshes=%" PRIu64 " invalid=%" PRIu64 " dropped=%" PRIu64
        " error=%d max_msc_gap=%" PRIu64 " verdict=%s evidence=(M)%s load1=%s\n",
        r->target.name,r->bulk_name,frames,c.intervals,c.misses,c.invalid,c.dropped,
        c.error,c.maxgap,failed ? "FAIL" : "TRACK",bench_evidence_tag(),load);
    /* Synthetic Xvfb misses are diagnostic only; errors/drop/invalid data fail
     * even TRACK. Gate-mode misses/incomplete intervals always fail. */
    if (failed) *fail = 1;
    return 0;
}

static int bulk_self_check(void);

static int self_check(void)
{
    int failed = 0;
#define CHECK_FINDING(n, c) do { bool ok_ = (c); \
    printf("SELF-CHECK section=%s %s\n", n, ok_ ? "PASS" : "FAIL"); \
    failed |= !ok_; } while (0)
    trace_init(); (void)trace_thread_register();
    rig r = {0}; uint64_t bits[1]; render_strip strips[1]; render_cell cells[1];
    uint8_t pixel = 255; _Alignas(64) uint8_t state[64];
    r.dims = (render_dims){1, 1, 1, 1}; r.cells = cells; r.strips = strips;
    r.next_id = 1; r.rng = 12345;
    r.page = (render_atlas_page){&pixel, 1, 1, 1, 1};
    for (uint32_t i = 0; i < 95; i++) r.glyphs[i] = (render_glyph){32+i,0,0,0,1,1};
    r.cfg = (render_config){.dims = r.dims, .max_width = 1, .max_height = 1,
        .max_cells = 1, .max_glyphs = 95, .max_pages = 1, .max_atlas_bytes = 1,
        .hooks = {hook5,hook6,&r}};
    r.state = state;
    if (render_grid_init(&r.g, r.dims, cells, 1, bits, 1) != RENDER_OK ||
        render_null_backend(&r.b) != RENDER_OK ||
        init_backend(&r,NULL) != RENDER_OK) return 1;
    r.g.pages = &r.page; r.g.page_count = 1; r.g.glyphs = r.glyphs; r.g.glyph_count = 95;
    uint64_t elapsed = one_frame(&r, true, 0, NULL);
    CHECK_FINDING("9", elapsed && r.ingress_ns <= r.first_mutation_ns && !subset_can_gate());
    render_backend_shutdown(&r.b);
    CHECK_FINDING("10", cadence_status(false,10000,1,0,1,false) != 0 &&
        cadence_status(false,10000,0,1,0,false) != 0 &&
        cadence_status(false,10000,0,0,0,true) != 0 &&
        cadence_status(false,299,0,0,0,false) != 0 &&
        cadence_status(false,10000,0,0,0,false) == 0);
    cadence sequence = {0}; cadence_observe(&sequence,100,0,0);
    cadence_observe(&sequence,103,0,0); cadence_observe(&sequence,104,0,2);
    CHECK_FINDING("10-history", sequence.intervals == 2 && sequence.misses == 2 &&
        sequence.dropped == 1 && cadence_status(false,sequence.intervals,sequence.misses,
        sequence.invalid,sequence.dropped,false) != 0);
    uint64_t slow[1] = {UINT64_C(100000000)}; bench_samples samples;
    bench_samples_init(&samples,slow,1); (void)bench_add(&samples,slow[0]);
    FILE *out = tmpfile(); if (!out) return 1;
    target_spec a = target_for(false), b = target_for(true);
    CHECK_FINDING("11", report_idle(out,"synthetic_idle",&samples,a,false,15) != 0 &&
        a.idle50 == UINT64_C(8000000) && a.idle99 == UINT64_C(1000000000)/90);
    CHECK_FINDING("11-short-idle", report_idle(out,"synthetic_short_idle",&samples,a,false,1) == 0);
    fclose(out);
    CHECK_FINDING("12", b.width == 1920 && b.height == 1080 && b.hz == 60 &&
        b.warm50 == UINT64_C(4000000) && b.warm99 == UINT64_C(5000000) &&
        b.idle50 == UINT64_C(10000000) && b.idle99 == UINT64_C(1000000000)/60 &&
        bulk_case_count() == 5 && bulk_self_check() == 0);
    CHECK_FINDING("16", fixture_status(false) != 0 && fixture_status(true) == 0);
    out = tmpfile(); if (!out) return 1;
    uint64_t edge[1] = {UINT64_C(5555555)};
    bench_samples_init(&samples,edge,1); (void)bench_add(&samples,edge[0]);
    bool inside = bench_report_fp(out,"synthetic_boundary",&samples,0,G3_P99) == 0;
    edge[0]++;
    bool outside = bench_report_fp(out,"synthetic_boundary",&samples,0,G3_P99) != 0;
    CHECK_FINDING("18", inside && outside && G3_P99 == UINT64_C(1000000000)/(2*90) &&
        UINT64_C(5558000) > G3_P99);
    fclose(out);
#undef CHECK_FINDING
    return failed;
}

/* Standalone contention surrogates: real index/find/save integration needs
 * their owning modules. These rows explicitly remain TRACK. */
typedef struct bulk_task {
    uint8_t *source, *dest;
    size_t bytes; unsigned kind;
    _Atomic bool started;
    _Atomic uint64_t chunks, checksum;
} bulk_task;
typedef struct bulk_fixture {
    bulk_task task[3]; work_handle handle[3]; unsigned count;
} bulk_fixture;
static void bulk_job(work_ctx *ctx)
{
    bulk_task *task = ctx->arg;
    atomic_store_explicit(&task->started,true,memory_order_release);
    size_t offset = 0;
    while (!work_should_stop(ctx)) {
        uint64_t sum = 0;
        const size_t chunk = 64u * 1024u;
        if (task->kind == 2) {
            memcpy(task->dest + offset,task->source + offset,chunk);
            sum = task->dest[offset];
        } else {
            uint8_t needle = task->kind == 0 ? (uint8_t)'\n' : (uint8_t)'x';
            for (size_t i = offset; i < offset + chunk; i++) sum += task->source[i] == needle;
        }
        atomic_fetch_add_explicit(&task->checksum,sum,memory_order_relaxed);
        atomic_fetch_add_explicit(&task->chunks,1,memory_order_relaxed);
        offset = (offset + chunk) % task->bytes;
    }
}
static void bulk_stop(rig *r, bulk_fixture *fixture)
{
    for (unsigned i = 0; i < fixture->count; i++) work_cancel(&r->pool,fixture->handle[i]);
    for (unsigned i = 0; i < fixture->count; i++) {
        while (atomic_load_explicit(&r->pool.slots[fixture->handle[i].slot].busy,memory_order_acquire)) {
            struct timespec pause = {0,1000000}; nanosleep(&pause,NULL);
        }
        free(fixture->task[i].source); free(fixture->task[i].dest);
    }
    fixture->count = 0;
}
static int bulk_start(rig *r, bulk_fixture *fixture, unsigned scenario)
{
    memset(fixture,0,sizeof *fixture);
    if (scenario == 0) return 0;
    unsigned jobs = scenario == 4 ? 3u : 1u;
    for (unsigned i = 0; i < jobs; i++) {
        bulk_task *task = &fixture->task[i];
        task->bytes = 8u * 1024u * 1024u;
        task->kind = scenario == 4 ? i : scenario - 1u;
        task->source = malloc(task->bytes);
        task->dest = malloc(task->bytes);
        if (!task->source || !task->dest) {
            free(task->source); free(task->dest); bulk_stop(r,fixture); return -1;
        }
        memset(task->source,'x',task->bytes);
        for (size_t j = 0; j < task->bytes; j += 80) task->source[j] = '\n';
        atomic_init(&task->started,false); atomic_init(&task->chunks,0); atomic_init(&task->checksum,0);
        fixture->handle[i] = work_submit(&r->pool,(work_job){bulk_job,task,0,WORK_BULK});
        if (!fixture->handle[i].epoch) {
            free(task->source); free(task->dest); bulk_stop(r,fixture); return -1;
        }
        fixture->count++;
        if (i == 0) {
            uint64_t deadline = bench_now_ns() + UINT64_C(3000000000);
            while (!atomic_load_explicit(&task->chunks,memory_order_acquire)) {
                if (bench_now_ns() > deadline) { bulk_stop(r,fixture); return -1; }
                struct timespec pause = {0,1000000}; nanosleep(&pause,NULL);
            }
        }
    }
    if (jobs == 3) {
        pthread_mutex_lock(&r->pool.mu);
        bool queued = r->pool.queue[WORK_BULK].count == 2;
        pthread_mutex_unlock(&r->pool.mu);
        if (!queued) { bulk_stop(r,fixture); return -1; }
    }
    return 0;
}

static int bulk_self_check(void)
{
    rig *r = aligned_alloc(_Alignof(rig), sizeof *r); if (!r) return 1;
    memset(r, 0, sizeof *r);
    if (work_pool_init(&r->pool,1,1) != 0) { free(r); return 1; }
    int fail = 0;
    for (unsigned scenario = 0; scenario < bulk_case_count(); scenario++) {
        bulk_fixture fixture;
        if (bulk_start(r,&fixture,scenario) != 0) { fail = 1; break; }
        unsigned want = scenario == 0 ? 0u : scenario == 4 ? 3u : 1u;
        if (fixture.count != want) fail = 1;
        if (want && !atomic_load_explicit(&fixture.task[0].chunks,memory_order_acquire)) fail = 1;
        if (want && fixture.task[0].kind != (scenario == 4 ? 0u : scenario - 1u)) fail = 1;
        bulk_stop(r,&fixture);
    }
    work_pool_shutdown(&r->pool); free(r); return fail;
}

int main(int argc, char **argv)
{
    (void)setvbuf(stdout,NULL,_IOLBF,0);
    if (argc == 2 && !strcmp(argv[1],"--pace-self-check")) return render_pace_self_check();
    if (argc == 2 && !strcmp(argv[1],"--self-check")) return self_check();
    bool quick = false, kernel_only = false, track = false, scroll_only = false;
    int idle_n = 5, target_pick = -1; uint32_t kernel_samples = 200;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i],"--quick")) { quick = true; idle_n = 1; }
        else if (!strcmp(argv[i],"--track")) track = true;
        else if (!strcmp(argv[i],"--scroll-track")) { scroll_only = true; track = true; }
        else if (!strcmp(argv[i],"--kernel-only")) kernel_only = true;
        else if (!strcmp(argv[i],"--target") && i + 1 < argc) {
            const char *arg = argv[++i];
            if (!strcmp(arg,"A")) target_pick = 0;
            else if (!strcmp(arg,"B")) target_pick = 1;
            else if (!strcmp(arg,"all")) target_pick = -1;
            else return 1;
        } else if ((!strcmp(argv[i],"--kernel-samples") || !strcmp(argv[i],"--idle")) && i + 1 < argc) {
            bool idle = !strcmp(argv[i],"--idle");
            char *end; long value = strtol(argv[++i],&end,10);
            if (*end || value < (idle ? 0 : 1) || value > (idle ? 8 : 10000)) return 1;
            if (idle) idle_n = (int)value; else kernel_samples = (uint32_t)value;
        } else return 1;
    }
    const char *pace_skip = render_pace_skip_reason(getenv("DISPLAY"));
    if ((scroll_only || !kernel_only) && pace_skip) {
        for (int ti = 0; ti < 2; ti++) {
            if (target_pick >= 0 && ti != target_pick) continue;
            for (unsigned si = 0; si < 2; si++) {
                char name[96]; snprintf(name,sizeof name,"%s_raster_scroll_600_%upx",
                    target_for(ti == 1).name,si == 0 ? 15u : 30u);
                render_pace_skip(stdout,name,pace_skip);
            }
        }
        if (scroll_only) return 0;
    }
    if (!getenv("DISPLAY") || !*getenv("DISPLAY")) {
        puts("SKIP G3/G3z/G3i: no DISPLAY; not measured"); return fixture_status(track);
    }
    trace_init(); (void)trace_thread_register();
    rig *r = aligned_alloc(_Alignof(rig), sizeof *r);
    if (!r) {
        if (scroll_only) render_pace_skip(stdout,"raster_scroll_600","rig_storage_unavailable");
        return scroll_only ? 0 : 1;
    }
    memset(r, 0, sizeof *r);
    r->track = track;
    plat_config pc = {"raster_bench",2880,1800,false,-1,0};
    if (plat_init(&r->pl,&pc) != PLAT_OK) {
        if (scroll_only) render_pace_skip(stdout,"raster_scroll_600","X11_unavailable");
        puts("SKIP G3/G3z/G3i: cannot open display; not measured"); free(r); return fixture_status(track);
    }
    char status[32], load[32] = "unknown"; bench_battery_status(status,sizeof status);
    FILE *load_file = fopen("/proc/loadavg","r");
    if (load_file) { if (fscanf(load_file,"%31s",load) != 1) strcpy(load,"unknown"); fclose(load_file); }
    strcpy(r->load_stamp,load);
    printf("power=%s %s load1=%s; (M) TRACK under shared load; backend=cpu-raster\n",
        status,bench_evidence_tag(),load);
    puts("SKIP G3/G3i: minimap integration missing; ingress -> T5 subset is TRACK only");
    puts("SKIP G3z: real-vblank fixture unverified (Xvfb :99 has no real vblank); not measured");
    puts("SKIP acceptance contention: index/find/save integration absent; synthetic memory/queue cases are TRACK");
    if (target_pick != 0) puts("B resolution on this X11 host is a proxy, not the Windows B acceptance fixture");
    if (work_pool_init(&r->pool,1,4) != 0) {
        if (scroll_only) render_pace_skip(stdout,"raster_scroll_600","worker_pool_unavailable");
        plat_shutdown(&r->pl); free(r); return scroll_only ? 0 : 1;
    }
    plat_map(&r->pl); plat_callbacks cb = {.on_event = raster_bench_noop};
    (void)plat_run_for(&r->pl,&cb,500);
    int fail = 0;
    static const uint32_t sizes[2] = {15,30};
    static const char *cases[5] = {"idle","synthetic_index","synthetic_find","synthetic_save_copy","synthetic_all_queued"};
    for (int ti = 0; ti < 2 && !fail; ti++) {
        if (target_pick >= 0 && ti != target_pick) continue;
        r->target = target_for(ti == 1);
        printf("TARGET %s pixels=%ux%u reference_G3=%" PRIu64 "/%" PRIu64
            " reference_G3i=%" PRIu64 "/%" PRIu64 " ns (G)\n",
            r->target.name,r->target.width,r->target.height,r->target.warm50,r->target.warm99,
            r->target.idle50,r->target.idle99);
        /* Preserve the separate backend-init diagnostic. */
        uint64_t init_vals[5]; bench_samples init_samples;
        bench_samples_init(&init_samples,init_vals,5);
        for (unsigned k = 0; k < (scroll_only ? 0u : 5u); k++) {
            const font_ascii_atlas *a = font_ascii_atlas_for_px(15);
            if (!a) { fail = 1; break; }
            render_dims dims = {r->target.width / a->cell.cell_w,r->target.height / a->cell.cell_h,
                                a->cell.cell_w,a->cell.cell_h};
            memset(&r->b,0,sizeof r->b);
            r->cfg = (render_config){.dims = dims,.max_width = r->target.width,.max_height = r->target.height,
                .max_cells = (size_t)dims.cols * dims.rows,.max_glyphs = 95,.max_pages = 1,
                .max_atlas_bytes = a->pixels_len,.platform = &r->pl,.workers = &r->pool};
            if (render_cpu_backend(&r->b) != RENDER_OK) { fail = 1; break; }
            r->state = aligned_alloc(64,(r->b.info.state_size + 63) & ~(size_t)63);
            if (!r->state) { fail = 1; break; }
            uint64_t ns = 0;
            int init_rc = init_backend(r,&ns);
            render_backend_shutdown(&r->b); free(r->state); r->state = NULL;
            if (init_rc != RENDER_OK) { fail = 1; break; }
            (void)bench_add(&init_samples,ns);
        }
        if (!fail && !scroll_only) {
            char name[96]; snprintf(name,sizeof name,"%s_init_cost_TRACK",r->target.name);
            fail |= raster_report_fp(stdout,name,&init_samples,0,0);
        }
        for (unsigned si = 0; si < 2 && !fail; si++) {
            r->next_id = 1; r->rng = 12345;
            if (setup_rig(r,sizes[si]) != 0) {
                if (scroll_only) render_pace_skip(stdout,"raster_scroll_600","backend_init_unavailable");
                teardown_rig(r);
                if (scroll_only) continue;
                fail = 1; break;
            }
            r->next_id = 1;
            if (scroll_only) {
                raster_scroll_track(r,sizes[si]); teardown_rig(r); continue;
            }
            for (unsigned scenario = 0; scenario < bulk_case_count() && !fail; scenario++) {
                if (kernel_only && scenario != 0) break;
                r->bulk_name = cases[scenario];
                bulk_fixture fixture;
                if (bulk_start(r,&fixture,scenario) != 0) { fail = 1; break; }
                if (!kernel_only && run_size(r,sizes[si],quick,&fail) != 0) fail = 1;
                if (scenario == 0 && paired_kernels(r,sizes[si],kernel_samples) != 0) fail = 1;
                if (!kernel_only && si == 0 && scroll_test(r,quick,&fail) != 0) fail = 1;
                bulk_stop(r,&fixture);
                if (scenario != 0) printf("CONTENTION %s_%s chunks=%" PRIu64 " queue_checked=%d verdict=TRACK evidence=(M)%s load1=%s\n",
                    r->target.name,r->bulk_name,atomic_load_explicit(&fixture.task[0].chunks,memory_order_relaxed),scenario == 4,bench_evidence_tag(),r->load_stamp);
                /* Isolated idle wake: stop every bulk job before the idle interval. */
                if (!kernel_only && scenario == 0 && idle_n > 0 && !fail) {
                    uint64_t vals[8]; bench_samples idle; bench_samples_init(&idle,vals,8);
                    unsigned seconds = quick ? 1u : 15u;
                    for (int k = 0; k < idle_n; k++) {
                        sleep(seconds);
                        uint64_t elapsed = one_frame(r,true,0,NULL);
                        if (!elapsed) { fail = 1; break; }
                        (void)bench_add(&idle,elapsed);
                    }
                    char name[96]; snprintf(name,sizeof name,"%s_first_frame_after_%us_idle_%upx_subset_%s",
                        r->target.name,seconds,sizes[si],track || seconds < 15 ? "TRACK" : "budget");
                    fail |= report_idle(stdout,name,&idle,r->target,track,seconds);
                }
            }
            /* Last use of this rig: a diagnostic error cannot affect an old gate. */
            if (!kernel_only && !fail && !pace_skip) raster_scroll_track(r,sizes[si]);
            teardown_rig(r);
        }
    }
    work_pool_shutdown(&r->pool); plat_shutdown(&r->pl); free(r);
    if (fail) { puts("FAIL raster bench: frame/data/threshold/fixture error"); return 1; }
    return track || subset_can_gate() ? 0 : 2; /* Missing acceptance integrations. */
}
void raster_bench_noop(void *u, const plat_event *e) { (void)u; (void)e; }
