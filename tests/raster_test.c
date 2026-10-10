/* Raster kernels and live conformance. The unchanged render suite is scoped
 * by this driver to count each ingress-through-submit window (P2.0 addendum).
 * Present/completion and libxcb reply allocations are outside that window. */
#include "raster/raster.h"
#include "base/base.h"
#include "work/work.h"
#include "x11/plat.h"
#include "trace/trace.h"
#include <poll.h>
#include <dlfcn.h>
#include <xcb/shm.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <xcb/xcb.h>

/* Interpose upload issuance so the live test can prove that only UI mailbox
 * handling stages pixels. Resolving the real function happens before init. */
static _Atomic uint32_t upload_calls;
static xcb_void_cookie_t (*real_shm_put)(xcb_connection_t *, xcb_drawable_t,
    xcb_gcontext_t, uint16_t, uint16_t, uint16_t, uint16_t, uint16_t, uint16_t,
    int16_t, int16_t, uint8_t, uint8_t, uint8_t, xcb_shm_seg_t, uint32_t);
xcb_void_cookie_t xcb_shm_put_image(xcb_connection_t *conn, xcb_drawable_t drawable,
    xcb_gcontext_t gc, uint16_t tw, uint16_t th, uint16_t sx, uint16_t sy,
    uint16_t sw, uint16_t sh, int16_t dx, int16_t dy, uint8_t depth,
    uint8_t format, uint8_t send, xcb_shm_seg_t seg, uint32_t offset)
{
    atomic_fetch_add_explicit(&upload_calls, 1, memory_order_relaxed);
    /* Negative-control run: the old even-XOR test accepted stale pixels. */
    if (getenv("EDIT_RASTER_DROP_PARTIAL") && sh < th) return (xcb_void_cookie_t){0};
    return real_shm_put(conn, drawable, gc, tw, th, sx, sy, sw, sh,
                        dx, dy, depth, format, send, seg, offset);
}

/* Driver-local state; no backend or frozen-suite changes. */
typedef struct allocation_scope {
    bool enabled, window;
    size_t allocations, frames;
} allocation_scope;
static allocation_scope *scope_state(void)
{
    static allocation_scope scope;
    return &scope;
}
static void scoped_guard_begin(void)
{
    allocation_scope *scope = scope_state();
    *scope = (allocation_scope){.enabled = true, .window = true};
    edit_malloc_guard_begin();
}
static void scoped_window_end(void)
{
    allocation_scope *scope = scope_state();
    if (scope->window) {
        scope->allocations += edit_malloc_guard_end();
        scope->window = false;
    }
}
static size_t scoped_guard_end(void)
{
    allocation_scope *scope = scope_state();
    scoped_window_end(); scope->enabled = false;
    printf("raster law2: windows=%zu allocations=%zu guard=%d scope=input->submit\n",
        scope->frames, scope->allocations, edit_malloc_guard_active());
    /* A truncated loop must fail even if no allocation was observed. */
    return scope->allocations + (scope->frames != 10000 ? 1u : 0u);
}
static int scoped_frame_begin(render_grid *g, uint32_t id)
{
    allocation_scope *scope = scope_state();
    if (scope->enabled && !scope->window) {
        edit_malloc_guard_begin(); scope->window = true;
    }
    return render_frame_begin(g, id);
}
static int scoped_submit(render_backend *b, const render_grid *g,
                         const render_strip *strips, size_t count)
{
    allocation_scope *scope = scope_state();
    if (scope->enabled && g->frame_id == 1000 && getenv("EDIT_RASTER_ALLOC_AT_1000")) {
        void *(*volatile allocate)(size_t) = malloc;
        void *p = allocate(1); free(p);
    }
    int rc = render_backend_submit(b,g,strips,count);
    if (scope->enabled) {
        scope->frames++; scoped_window_end();
        if (edit_malloc_guard_active() && scope->allocations != 0) {
            fprintf(stderr,"raster law2: FAIL frame=%u allocations=%zu scope=input->submit\n",
                    g->frame_id,scope->allocations);
            return RENDER_ERR_DEVICE;
        }
    }
    return rc;
}
#define RENDER_TEST_EXTERNAL 1
#define main render_conformance_main
#define edit_malloc_guard_begin scoped_guard_begin
#define edit_malloc_guard_end scoped_guard_end
#define render_frame_begin scoped_frame_begin
#define render_backend_submit scoped_submit
#include "render_test.c"
static bool external_guard_enabled(void) { return edit_malloc_guard_active(); }
#undef main
#undef edit_malloc_guard_begin
#undef edit_malloc_guard_end
#undef render_frame_begin
#undef render_backend_submit

#define T(c) do { if (!(c)) { fprintf(stderr, "raster_test:%d: FAIL %s\n", __LINE__, #c); return 1; } } while (0)

static uint64_t rng_state = UINT64_C(0x9e3779b97f4a7c15);
static uint32_t rnd(void)
{
    rng_state ^= rng_state << 13; rng_state ^= rng_state >> 7; rng_state ^= rng_state << 17;
    return (uint32_t)(rng_state >> 16);
}

/* ---- SSE2 vs scalar ---------------------------------------------------- */
#define NGLYPH 12
static int kernel_random_test(void)
{
    for (int iter = 0; iter < 4000; iter++) {
        render_dims d = {1 + rnd() % 40, 1 + rnd() % 3, 1 + rnd() % 32, 1 + rnd() % 32};
        if (iter % 4 == 0) d.cell_w = iter % 8 == 0 ? 8u : 16u;
        size_t n = (size_t)d.cols * d.rows;
        render_cell *cells = calloc(n, sizeof *cells);
        uint8_t pages[2][32 * 32];
        render_atlas_page pg[2];
        render_glyph gl[NGLYPH];
        T(cells != NULL);
        for (int p = 0; p < 2; p++) {
            for (size_t i = 0; i < sizeof pages[p]; i++) {
                uint32_t r = rnd() % 8;
                pages[p][i] = r < 3 ? 0 : r < 5 ? 255 : (uint8_t)rnd();
            }
            pg[p] = (render_atlas_page){pages[p], sizeof pages[p], 32, 32, 32};
        }
        for (uint32_t i = 0; i < NGLYPH; i++) {
            uint32_t w = 1 + rnd() % 30, h = 1 + rnd() % 30;
            gl[i] = (render_glyph){i, rnd() & 1u, rnd() % (33 - w), rnd() % (33 - h), w, h};
        }
        for (uint32_t r = 0; r < d.rows; r++)
            for (uint32_t c = 0; c < d.cols; c++) {
                render_cell *cell = &cells[(size_t)r * d.cols + c];
                if (cell->attrs & RENDER_ATTR_WIDE_RIGHT) continue;
                uint16_t at = (uint16_t)(rnd() & (RENDER_ATTR_BOLD | RENDER_ATTR_ITALIC |
                    RENDER_ATTR_UNDERLINE | RENDER_ATTR_INVERSE | RENDER_ATTR_CURSOR | RENDER_ATTR_SELECTION));
                uint32_t slot = rnd() % 4 == 0 ? RENDER_NO_SLOT : rnd() % NGLYPH;
                *cell = (render_cell){slot == RENDER_NO_SLOT ? 0 : slot, slot, rnd() & 0xffffffu, rnd() & 0xffffffu, at, 0};
                if (c + 1 < d.cols && rnd() % 5 == 0) {
                    cell->attrs |= RENDER_ATTR_WIDE_LEFT;
                    render_cell *right = cell + 1;
                    *right = (render_cell){0, RENDER_NO_SLOT, cell->fg, cell->bg,
                        (uint16_t)((at & ~RENDER_ATTR_WIDE_LEFT) | RENDER_ATTR_WIDE_RIGHT), 0};
                }
            }
        raster_scene s = {d, cells, gl, NGLYPH, pg, 2, (rnd() & 1u) ? 0xff000000u : 0u};
        size_t stride = (size_t)d.cols * d.cell_w + rnd() % 6;
        size_t px = stride * d.cell_h;
        uint32_t *a = malloc(px * 4), *b = malloc(px * 4);
        raster_palette palette = {0};
        T(a && b);
        for (uint32_t row = 0; row < d.rows; row++) {
            for (size_t i = 0; i < px; i++) a[i] = b[i] = 0xdeadbeefu;
            raster_row_scalar(&s, a, stride, row);
            raster_row_sse2(&s, b, stride, row);
            T(memcmp(a, b, px * 4) == 0);
            for (size_t i = 0; i < px; i++) b[i] = 0xdeadbeefu;
            raster_row_sse2_stream(&s, b, stride, row);
            T(memcmp(a, b, px * 4) == 0);
            for (size_t i = 0; i < px; i++) b[i] = 0xdeadbeefu;
            raster_row_cached(&s, b, stride, row, &palette);
            T(memcmp(a, b, px * 4) == 0);
            T(palette.rebuilds <= RASTER_PALETTE_SLOTS);
        }
        free(a); free(b); free(cells);
    }
    return 0;
}

static int blend_exact_test(void)
{
    /* every coverage value, assorted colours; also checks the documented formula */
    for (uint32_t a = 0; a < 256; a++)
        for (uint32_t k = 0; k < 40; k++) {
            uint32_t fg = k < 4 ? (k & 1 ? 0xffffffu : 0u) * (k < 2 ? 1u : 0u) | (k == 2 ? 0xff00ffu : 0x010203u) : rnd() & 0xffffffu;
            uint32_t bg = k < 4 ? 0xfeu * (k & 1u) * 0x010101u : rnd() & 0xffffffu;
            uint8_t px[8]; memset(px, (int)a, sizeof px);
            render_atlas_page pg = {px, 8, 8, 8, 1};
            render_glyph gl = {1, 0, 0, 0, 8, 1};
            render_cell cell = {1, 0, fg, bg, 0, 0};
            raster_scene s = {{1, 1, 8, 1}, &cell, &gl, 1, &pg, 1, 0};
            uint32_t o1[8], o2[8];
            raster_row_scalar(&s, o1, 8, 0);
            raster_row_sse2(&s, o2, 8, 0);
            uint32_t want = 0;
            for (unsigned sh = 0; sh < 24; sh += 8)
                want |= ((((fg >> sh) & 255u) * a + ((bg >> sh) & 255u) * (255u - a) + 127u) / 255u) << sh;
            for (int i = 0; i < 8; i++) { T(o1[i] == want); T(o2[i] == want); }
        }
    /* hand-checked: half coverage white on black = 0x808080; underline = fg on last row; inverse swaps */
    uint8_t px[4] = {128, 128, 128, 128};
    render_atlas_page pg = {px, 4, 4, 4, 1};
    render_glyph gl = {1, 0, 0, 0, 4, 1};
    render_cell cell = {1, 0, 0xffffff, 0, RENDER_ATTR_UNDERLINE, 0};
    raster_scene s = {{1, 1, 4, 2}, &cell, &gl, 1, &pg, 1, 0};
    uint32_t o[8];
    raster_row_sse2(&s, o, 4, 0);
    T(o[0] == 0x808080u && o[3] == 0x808080u && o[4] == 0xffffffu && o[7] == 0xffffffu);
    cell.attrs = RENDER_ATTR_INVERSE;
    raster_row_sse2(&s, o, 4, 0);
    T(o[0] == 0x7f7f7fu && o[4] == 0xffffffu); /* fg=0, bg=white: row 1 background only */
    return 0;
}

static unsigned char *visits;
static uint32_t visit_last;
static void visit(void *u, uint32_t row) { (void)u; visits[row]++; visit_last = row; }
static int partition_test(void)
{
    visits = malloc(512);
    T(visits != NULL);
    for (int iter = 0; iter < 2000; iter++) {
        render_strip strips[64]; size_t n = 0; uint32_t row = rnd() % 4, total = 0;
        unsigned char want[512]; memset(want, 0, sizeof want);
        while (n < 64 && row < 400) {
            uint32_t cnt = 1 + rnd() % 7;
            strips[n++] = (render_strip){row, cnt};
            for (uint32_t i = 0; i < cnt; i++) want[row + i] = 1;
            total += cnt; row += cnt + 1 + rnd() % 4;
            if (rnd() % 9 == 0) break;
        }
        for (uint32_t jobs = 1; jobs <= 8; jobs++) {
            memset(visits, 0, 512);
            uint32_t prev_hi = 0;
            for (uint32_t j = 0; j < jobs; j++) {
                uint32_t lo, hi;
                raster_partition(total, jobs, j, &lo, &hi);
                T(lo == prev_hi && hi >= lo); prev_hi = hi;
                raster_for_rows(strips, n, lo, hi, visit, NULL);
            }
            T(prev_hi == total);
            T(memcmp(visits, want, 512) == 0); /* every dirty row exactly once, none else */
        }
    }
    free(visits);
    return 0;
}

/* ---- live backend driver for the frozen conformance suite -------------- */
static plat g_plat;
static work_pool g_pool;
static bool g_up;

int render_test_prepare(render_backend *b, render_config *cfg)
{
    if (!g_up) {
        plat_config pc = {"raster_test", 64, 128, false, -1, 0};
        if (plat_init(&g_plat, &pc) != PLAT_OK) return RENDER_ERR_INIT;
        if (work_pool_init(&g_pool, 1, 4) != 0) { plat_shutdown(&g_plat); return RENDER_ERR_INIT; }
        plat_map(&g_plat);
        g_up = true;
    }
    cfg->platform = &g_plat; cfg->workers = &g_pool;
    return render_cpu_backend(b);
}
static void pump_cb(const work_msg *m, void *ud)
{
    render_event ev = {RENDER_EVENT_WORK, m->generation, 0, m};
    (void)render_backend_event(ud, &ev); /* stale frames report FRAME; ignored */
}
int render_test_pump(render_backend *b)
{
    struct pollfd p = {work_pool_eventfd(&g_pool), POLLIN, 0};
    (void)poll(&p, 1, 1);
    (void)work_mailbox_drain(&g_pool, pump_cb, b);
    return RENDER_OK;
}
void render_test_cleanup(void) { }

static void noop_ev(void *ud, const plat_event *e) { (void)ud; (void)e; }

static int compare_window(const render_config *cfg, const render_cell *cells,
                          const render_glyph *glyph, const render_atlas_page *page)
{
    xcb_connection_t *c = g_plat.conn;
    xcb_get_image_reply_t *im = xcb_get_image_reply(c,
        xcb_get_image(c, XCB_IMAGE_FORMAT_Z_PIXMAP, g_plat.win, 0, 0, 32, 96, ~0u), NULL);
    T(im != NULL);
    T(xcb_get_image_data_length(im) >= 32 * 96 * 4);
    const uint32_t *got = (const uint32_t *)xcb_get_image_data(im);
    uint32_t want[32 * 96];
    raster_scene scene = {cfg->dims, cells, glyph, 1, page, 1, 0};
    for (uint32_t row = 0; row < 6; row++)
        raster_row_scalar(&scene, want + (size_t)row * 16 * 32, 32, row);
    size_t bad = 0;
    for (size_t i = 0; i < 32 * 96; i++) if ((got[i] & 0xffffffu) != want[i]) bad++;
    free(im);
    if (bad) fprintf(stderr, "raster_test: %zu window pixels differ\n", bad);
    T(bad == 0);
    return 0;
}

/* Real X: paint a frame through XShm and read the window back. */
static int live_pixel_test(void)
{
    render_backend b = {0};
    render_cell cells[24]; uint64_t bits[1]; render_grid g;
    uint8_t pixels[8 * 16];
    for (size_t i = 0; i < sizeof pixels; i++) pixels[i] = (uint8_t)(i * 7);
    render_atlas_page page = {pixels, sizeof pixels, 8, 8, 16};
    render_glyph glyph = {65, 0, 0, 0, 8, 16};
    render_config cfg = {.dims = {4, 6, 8, 16}, .max_width = 32, .max_height = 128, .max_cells = 24,
        .max_glyphs = 1, .max_pages = 1, .max_atlas_bytes = 128};
    T(render_test_prepare(&b, &cfg) == RENDER_OK);
    plat_map(&g_plat);
    plat_callbacks cb = {.on_event = noop_ev};
    (void)plat_run_for(&g_plat, &cb, 200);
    render_backend_info info; T(render_backend_query(&b, &info) == RENDER_OK);
    T(info.capabilities & RENDER_CAP_RASTER_POOL);
    void *state = aligned_alloc(info.state_align > 16 ? info.state_align : 16,
                                (info.state_size + 63) & ~(size_t)63);
    T(state != NULL);
    T(init_on_worker(&b, &cfg, state, info.state_size) == RENDER_OK);
    T(render_grid_init(&g, cfg.dims, cells, 24, bits, 1) == RENDER_OK);
    g.pages = &page; g.page_count = 1; g.glyphs = &glyph; g.glyph_count = 1;
    for (uint32_t i = 0; i < 24; i++)
        cells[i] = (render_cell){i % 3 ? 65u : 0u, i % 3 ? 0u : RENDER_NO_SLOT, 0xf0e0d0u - i * 0x010101u, i * 0x050505u,
                                 (uint16_t)(i % 5 == 0 ? RENDER_ATTR_UNDERLINE : i % 7 == 0 ? RENDER_ATTR_INVERSE : 0), 0};
    T(render_frame_begin(&g, 1) == RENDER_OK && render_mark_full(&g) == RENDER_OK);
    render_strip full = {0, 6};
    uint32_t uploads_before = atomic_load_explicit(&upload_calls, memory_order_relaxed);
    T(render_backend_submit(&b, &g, &full, 1) == RENDER_OK);
    /* Let all four workers finish without routing messages. submit must not
     * have side effects on the retained pixmap, even when jobs run immediately. */
    for (uint32_t spin = 0; spin < 2000; spin++) {
        bool busy = false;
        for (uint32_t slot = 0; slot < WORK_MAX_JOBS; slot++)
            busy |= atomic_load_explicit(&g_pool.slots[slot].busy, memory_order_acquire) != 0;
        if (!busy) break;
        struct timespec pause = {0, 1000000}; nanosleep(&pause, NULL);
    }
    T(atomic_load_explicit(&upload_calls, memory_order_relaxed) == uploads_before);
    uint64_t deadline = trace_now_ns() + UINT64_C(3000000000);
    int rc;
    while ((rc = render_backend_present(&b, 1)) == RENDER_ERR_BUSY) { render_test_pump(&b); T(trace_now_ns() < deadline); }
    T(rc == RENDER_OK);
    while (b.active) { render_test_pump(&b); T(trace_now_ns() < deadline); }
    raster_metrics metrics;
    T(raster_frame_metrics(&b, &metrics));
    T(metrics.frame_id == 1 && metrics.jobs == 4);
    T(metrics.fence_sequence < metrics.present_sequence); /* T5 excludes the scheduled window copy */
    T(metrics.present_kind == 0); /* Present pixmap, not NotifyMSC */
    T(metrics.submit_ns > 0 && metrics.ready_ns >= metrics.submit_ns);
    T(metrics.present_ns >= metrics.ready_ns && metrics.server_ns >= metrics.present_ns);
    for (uint32_t j = 0; j < metrics.jobs; j++) T(metrics.strip_ns[j] > 0);
    T(compare_window(&cfg, cells, &glyph, &page) == 0);
    uint32_t initial_fg = cells[8].fg;
    /* Compare EACH non-cancelling edit against a fresh full scalar image,
     * including all unchanged rows. Disconnected runs cross job partitions. */
    for (uint32_t id = 2; id < 12; id++) {
        render_strip strips[3]; size_t count = 0;
        deadline = trace_now_ns() + UINT64_C(3000000000);
        edit_malloc_guard_begin();
        cells[8].fg = (initial_fg + id * 0x010203u) & 0xffffffu;
        T(render_frame_begin(&g, id) == RENDER_OK);
        T(render_mark_rows(&g, 2, 1) == RENDER_OK);
        if (id % 2 == 0) {
            cells[0].bg = id * 0x030201u;
            cells[20].bg = id * 0x010307u;
            T(render_mark_rows(&g, 0, 1) == RENDER_OK);
            T(render_mark_rows(&g, 5, 1) == RENDER_OK);
        } else {
            cells[12].bg = id * 0x010305u;
            T(render_mark_rows(&g, 3, 1) == RENDER_OK);
        }
        T(render_dirty_strips(&g, strips, 3, &count) == RENDER_OK);
        rc = render_backend_submit(&b, &g, strips, count);
        size_t allocs = edit_malloc_guard_end();
        T(rc == RENDER_OK);
        if (edit_malloc_guard_active()) T(allocs == 0);
        while ((rc = render_backend_present(&b, id)) == RENDER_ERR_BUSY) { render_test_pump(&b); T(trace_now_ns() < deadline); }
        T(rc == RENDER_OK);
        while (b.active) { render_test_pump(&b); T(trace_now_ns() < deadline); }
        T(cells[8].fg != initial_fg);
        T(compare_window(&cfg, cells, &glyph, &page) == 0);
    }
    printf("raster pixels: PASS full + 10 individual partial comparisons (disconnected/boundaries/unchanged rows)\n");
    render_backend_shutdown(&b);
    free(state);
    return 0;
}


/* edit-2vs: a fence job must not outlive the backend. The platform loop can
 * deliver PRESENT_COMPLETE to the adapter (bench pump_view's present_done) while
 * the fence job is still queued or polling; the next submit used to forget its
 * handle, so shutdown could not join it and it read freed backend state. The
 * fence job is held QUEUED deterministically by occupying every raster worker. */
typedef struct fence_gate { _Atomic uint32_t started, release; } fence_gate;
static void fence_gate_job(work_ctx *c)
{
    fence_gate *gate = c->arg;
    atomic_fetch_add(&gate->started, 1u);
    while (!atomic_load(&gate->release)) { struct timespec t = {0, 100000}; nanosleep(&t, NULL); }
}
static int fence_outlives_test(void)
{
    render_backend b = {0};
    render_cell cells[24]; uint64_t bits[1]; render_grid g;
    uint8_t pixels[8 * 16] = {0};
    render_atlas_page page = {pixels, sizeof pixels, 8, 8, 16};
    render_glyph glyph = {65, 0, 0, 0, 8, 16};
    render_config cfg = {.dims = {4, 6, 8, 16}, .max_width = 32, .max_height = 128, .max_cells = 24,
        .max_glyphs = 1, .max_pages = 1, .max_atlas_bytes = 128};
    T(render_test_prepare(&b, &cfg) == RENDER_OK);
    render_backend_info info; T(render_backend_query(&b, &info) == RENDER_OK);
    void *state = aligned_alloc(info.state_align > 16 ? info.state_align : 16,
                                (info.state_size + 63) & ~(size_t)63);
    T(state != NULL);
    T(init_on_worker(&b, &cfg, state, info.state_size) == RENDER_OK);
    T(render_grid_init(&g, cfg.dims, cells, 24, bits, 1) == RENDER_OK);
    g.pages = &page; g.page_count = 1; g.glyphs = &glyph; g.glyph_count = 1;
    for (uint32_t i = 0; i < 24; i++) cells[i] = (render_cell){0, RENDER_NO_SLOT, 0x112233u, 0x445566u, 0, 0};
    T(render_frame_begin(&g, 1) == RENDER_OK && render_mark_full(&g) == RENDER_OK);
    render_strip full = {0, 6};
    T(render_backend_submit(&b, &g, &full, 1) == RENDER_OK);
    for (uint32_t spin = 0; spin < 2000; spin++) {   /* strips done, results mailed */
        bool busy = false;
        for (uint32_t slot = 0; slot < WORK_MAX_JOBS; slot++)
            busy |= atomic_load_explicit(&g_pool.slots[slot].busy, memory_order_acquire) != 0;
        if (!busy) break;
        struct timespec pause = {0, 1000000}; nanosleep(&pause, NULL);
    }
    fence_gate gate; atomic_init(&gate.started, 0); atomic_init(&gate.release, 0);
    work_handle blockers[4];
    for (size_t i = 0; i < 4; i++) {
        blockers[i] = work_submit(&g_pool, (work_job){fence_gate_job, &gate, 0, WORK_RASTER});
        T(blockers[i].epoch != 0);
    }
    uint64_t deadline = trace_now_ns() + UINT64_C(3000000000);
    while (atomic_load(&gate.started) < 4) { T(trace_now_ns() < deadline); struct timespec t = {0, 100000}; nanosleep(&t, NULL); }
    int rc;
    while ((rc = render_backend_present(&b, 1)) == RENDER_ERR_BUSY) { render_test_pump(&b); T(trace_now_ns() < deadline); }
    T(rc == RENDER_OK);   /* fence job now sits in the queue behind the gate */
    T(render_backend_signal(&b, RENDER_EVENT_DEVICE_DONE, 1, 0) == RENDER_OK);
    T(render_backend_signal(&b, RENDER_EVENT_PRESENT_COMPLETE, 1, 0) == RENDER_OK);
    T(!b.active);          /* frame 1 complete without its fence job */
    T(render_frame_begin(&g, 2) == RENDER_OK && render_mark_rows(&g, 2, 1) == RENDER_OK);
    render_strip one[3]; size_t count = 0;
    T(render_dirty_strips(&g, one, 3, &count) == RENDER_OK);
    T(render_backend_submit(&b, &g, one, count) == RENDER_OK);
    render_backend_shutdown(&b);
    free(state);           /* the backend is gone; no job may touch it now */
    atomic_store(&gate.release, 1u);
    for (size_t i = 0; i < 4; i++)
        while (!work_handle_finished(&g_pool, blockers[i])) { struct timespec t = {0, 100000}; nanosleep(&t, NULL); }
    for (uint32_t spin = 0; spin < 2000; spin++) {   /* let any straggler run */
        bool busy = false;
        for (uint32_t slot = 0; slot < WORK_MAX_JOBS; slot++)
            busy |= atomic_load_explicit(&g_pool.slots[slot].busy, memory_order_acquire) != 0;
        if (!busy) break;
        struct timespec pause = {0, 1000000}; nanosleep(&pause, NULL);
    }
    printf("raster fence: PASS (queued fence job joined before backend release)\n");
    return 0;
}

static int review_cases(const char *which);

int main(int argc, char **argv)
{
    if (argc > 2 && !strcmp(argv[1], "--review")) return review_cases(argv[2]);
    *(void **)(&real_shm_put) = dlsym(RTLD_NEXT, "xcb_shm_put_image");
    T(real_shm_put != NULL);
    T(!edit_malloc_guard_active() || external_guard_enabled());
    if (review_cases("all") != 0) return 1;
    if (kernel_random_test() || blend_exact_test() || partition_test()) return 1;
    printf("raster_test: kernels PASS (SSE2 == scalar on 4000 random grids, 10240 blends, partitions)\n");
    plat probe;
    plat_config pc = {"probe", 16, 16, false, -1, 0};
    if (getenv("DISPLAY") == NULL || plat_init(&probe, &pc) != PLAT_OK) {
        printf("raster_test: SKIP live X11 (no DISPLAY / cannot connect)\n");
        return 0;
    }
    plat_shutdown(&probe);
    if (!(argc > 1 && !strcmp(argv[1], "--live-only")) && render_conformance_main() != 0) return 1;
    if (live_pixel_test() != 0) return 1;
    if (fence_outlives_test() != 0) return 1;
    if (g_up) { work_pool_shutdown(&g_pool); plat_shutdown(&g_plat); }
    printf("raster_test: PASS (requested conformance, upload ordering, typing allocations, live XShm)\n");
    return 0;
}

/* P2.5b regressions. Include a private copy of the backend, replacing only
 * scheduling/X observation boundaries. Jobs can be deliberately paused, an
 * enqueue can fail, and X observations can arrive in a prescribed order.
 * Product exports below are renamed; the ordinary live driver above still
 * links and exercises the production backend. Existing tests stay intact. */
#include <xcb/present.h>
#include <xcb/sync.h>
#include <xcb/xcbext.h>

static bool review_fake, review_real_mailbox;
static uint32_t review_uploads, review_presents, review_submit_calls;
static uint32_t review_stop_calls, review_stop_at;
static _Atomic uint32_t review_publish_calls;
static uint32_t review_publish_failures;
static uint64_t review_clock;
static unsigned review_observation;
static work_job review_queued[RASTER_JOBS + 1u];
static work_msg review_messages[8];
static size_t review_message_count;
static bool review_enqueue_full, review_partial_capacity, review_run_at_submit;
static uint32_t review_fail_at = 2;
static bool review_watch_sleep;
static pthread_t review_ui_thread;
static unsigned review_rollback_waits;
static int review_sleep(const struct timespec *pause, struct timespec *remaining)
{
    if (review_watch_sleep && pthread_equal(pthread_self(), review_ui_thread))
        review_rollback_waits++;
    return nanosleep(pause, remaining);
}

static bool review_should_stop(const work_ctx *c)
{
    if (review_stop_at) return ++review_stop_calls >= review_stop_at;
    return review_fake && !review_real_mailbox ? false : work_should_stop(c);
}
static bool review_publish(work_ctx *c, const work_msg *m)
{
    if (review_publish_failures) {
        review_publish_failures--; review_publish_calls++; return false;
    }
    if (!review_fake || review_real_mailbox) {
        bool sent = work_publish(c, m);
        /* Observe the attempt only AFTER the full-ring check. Otherwise UI
         * could drain between this counter and publication, hiding saturation. */
        review_publish_calls++;
        return sent;
    }
    review_publish_calls++;
    if (review_message_count >= 8) return false;
    review_messages[review_message_count++] = *m;
    return true;
}
static work_handle review_submit(work_pool *p, work_job job)
{
    if (!review_fake || review_real_mailbox) return work_submit(p, job);
    review_submit_calls++;
    if (review_enqueue_full || (review_partial_capacity && review_submit_calls == review_fail_at)) return (work_handle){0};
    uint32_t index = review_submit_calls - 1u;
    if (index >= RASTER_JOBS + 1u) return (work_handle){0};
    review_queued[index] = job; /* worker remains paused until explicitly run */
    if (review_run_at_submit) {
        work_ctx c = {.generation=job.generation,.arg=job.arg};
        job.fn(&c); /* strongest scheduling race: execute before returning */
    }
    return (work_handle){index, 9};
}
static int review_submit_batch(work_pool *p, const work_job *jobs, size_t count, work_handle *handles)
{
    if (!review_fake || review_real_mailbox) return work_submit_batch(p, jobs, count, handles);
    if (review_enqueue_full || (review_partial_capacity && count >= review_fail_at)) {
        review_submit_calls = review_fail_at;
        return -1;
    }
    for (size_t i = 0; i < count; i++) {
        uint32_t index = review_submit_calls++;
        if (index >= RASTER_JOBS + 1u) return -1;
        review_queued[index] = jobs[i];
        handles[i] = (work_handle){index, 9};
    }
    return 0;
}
static uint64_t review_now(void) { return review_fake ? review_clock : trace_now_ns(); }
static int review_flush(xcb_connection_t *c) { return review_fake ? 1 : xcb_flush(c); }
static xcb_void_cookie_t review_put(xcb_connection_t *c, xcb_drawable_t d,
    xcb_gcontext_t gc, uint16_t tw, uint16_t th, uint16_t sx, uint16_t sy,
    uint16_t sw, uint16_t sh, int16_t dx, int16_t dy, uint8_t depth,
    uint8_t format, uint8_t send, xcb_shm_seg_t seg, uint32_t offset)
{
    if (!review_fake) return xcb_shm_put_image(c,d,gc,tw,th,sx,sy,sw,sh,dx,dy,depth,format,send,seg,offset);
    review_uploads++;
    return (xcb_void_cookie_t){1};
}
static int review_fd(xcb_connection_t *c) { return review_fake ? -1 : xcb_get_file_descriptor(c); }
static int review_error(xcb_connection_t *c) { return review_fake ? 0 : xcb_connection_has_error(c); }
static int review_reply(xcb_connection_t *c, unsigned seq, void **reply, xcb_generic_error_t **err)
{
    if (!review_fake) return xcb_poll_for_reply(c, seq, reply, err);
    *err = NULL;
    if (review_observation < 2) return 0;
    xcb_sync_query_fence_reply_t *r = calloc(1, sizeof *r);
    if (!r) return 0;
    r->triggered = 1; *reply = r; review_clock = 300;
    return 1;
}
static xcb_generic_event_t *review_special(xcb_connection_t *c, xcb_special_event_t *s)
{
    if (!review_fake) return xcb_poll_for_special_event(c, s);
    if (review_observation == 0) {
        void *mem = calloc(1, sizeof(xcb_present_complete_notify_event_t));
        xcb_present_complete_notify_event_t *e = mem;
        if (!e) return NULL;
        e->event_type = XCB_PRESENT_COMPLETE_NOTIFY; e->serial = 7;
        e->kind = XCB_PRESENT_COMPLETE_KIND_PIXMAP; e->ust = 55; e->msc = 66;
        review_clock = 200; review_observation++;
        return mem;
    }
    if (review_observation == 1) {
        xcb_present_idle_notify_event_t *e = calloc(1, sizeof *e);
        if (!e) return NULL;
        e->event_type = XCB_PRESENT_IDLE_NOTIFY; e->serial = 7; e->pixmap = 42;
        review_observation++;
        return (xcb_generic_event_t *)e;
    }
    return NULL;
}
static xcb_generic_event_t *review_event(xcb_connection_t *c) { return review_fake ? NULL : xcb_poll_for_event(c); }
static xcb_void_cookie_t review_reset(xcb_connection_t *c, xcb_sync_fence_t f)
{ return review_fake ? (xcb_void_cookie_t){1} : xcb_sync_reset_fence(c, f); }
static xcb_void_cookie_t review_trigger(xcb_connection_t *c, xcb_sync_fence_t f)
{ return review_fake ? (xcb_void_cookie_t){2} : xcb_sync_trigger_fence(c, f); }
static xcb_void_cookie_t review_await(xcb_connection_t *c, uint32_t n, const xcb_sync_fence_t *f)
{ return review_fake ? (xcb_void_cookie_t){3} : xcb_sync_await_fence(c, n, f); }
static xcb_sync_query_fence_cookie_t review_query(xcb_connection_t *c, xcb_sync_fence_t f)
{ return review_fake ? (xcb_sync_query_fence_cookie_t){4} : xcb_sync_query_fence(c, f); }
static xcb_void_cookie_t review_present(xcb_connection_t *c, xcb_window_t w, xcb_pixmap_t p,
    uint32_t serial, xcb_xfixes_region_t valid, xcb_xfixes_region_t update,
    int16_t x, int16_t y, xcb_randr_crtc_t crtc, xcb_sync_fence_t wait,
    xcb_sync_fence_t idle, uint32_t options, uint64_t msc, uint64_t divisor,
    uint64_t remainder, uint32_t n, const xcb_present_notify_t *notify)
{
    if (!review_fake) return xcb_present_pixmap(c,w,p,serial,valid,update,x,y,crtc,wait,idle,options,msc,divisor,remainder,n,notify);
    review_presents++;
    return (xcb_void_cookie_t){5};
}

#define render_cpu_backend raster_review_backend
#define raster_last_present raster_review_last_present
#define raster_frame_metrics raster_review_frame_metrics
#define work_submit review_submit
#define work_submit_batch review_submit_batch
#define work_publish review_publish
#define work_should_stop review_should_stop
#define nanosleep review_sleep
#define trace_now_ns review_now
#define xcb_shm_put_image review_put
#define xcb_flush review_flush
#define xcb_get_file_descriptor review_fd
#define xcb_connection_has_error review_error
#define xcb_poll_for_reply review_reply
#define xcb_poll_for_special_event review_special
#define xcb_poll_for_event review_event
#define xcb_sync_reset_fence review_reset
#define xcb_sync_trigger_fence review_trigger
#define xcb_sync_await_fence review_await
#define xcb_sync_query_fence review_query
#define xcb_present_pixmap review_present
#include "../src/raster/raster.c"
#undef render_cpu_backend
#undef raster_last_present
#undef raster_frame_metrics
#undef work_submit
#undef work_submit_batch
#undef work_publish
#undef work_should_stop
#undef nanosleep
#undef trace_now_ns
#undef xcb_shm_put_image
#undef xcb_flush
#undef xcb_get_file_descriptor
#undef xcb_connection_has_error
#undef xcb_poll_for_reply
#undef xcb_poll_for_special_event
#undef xcb_poll_for_event
#undef xcb_sync_reset_fence
#undef xcb_sync_trigger_fence
#undef xcb_sync_await_fence
#undef xcb_sync_query_fence
#undef xcb_present_pixmap

static void review_reset_fixture(void)
{
    review_fake = true; review_real_mailbox = false; review_uploads = 0; review_presents = 0; review_submit_calls = 0;
    review_stop_calls = 0; review_stop_at = 0; review_publish_calls = 0;
    review_publish_failures = 0; review_clock = 100; review_observation = 0;
    review_message_count = 0; review_enqueue_full = false; review_partial_capacity = false; review_run_at_submit = false;
    review_fail_at = 2; review_watch_sleep = false; review_rollback_waits = 0;
    memset(review_queued, 0, sizeof review_queued);
    memset(review_messages, 0, sizeof review_messages);
}
static render_backend review_backend(cpu_state *st)
{
    render_backend b = {0};
    (void)raster_review_backend(&b);
    b.state = st; b.initialized = true; b.active = true; b.active_frame = 7;
    b.config.hooks = (render_hooks){done_hook, complete_hook, NULL};
    return b;
}
static int review_ownership(void)
{
    review_reset_fixture();
    render_strip band = {0,1};
    cpu_state st = {.strips=&band,.nstrips=1,.total_rows=1,.scene={.dims={1,1,1,1}},
        .max_w=1,.max_h=1,.up = true, .frame_id = 7, .have_frame = true, .pending = 1,
        .metrics = {.frame_id = 7, .jobs = 1, .submit_ns = 90}, .nhandles = 1};
    st.handles[0] = (work_handle){2,9}; /* owned worker paused; no completion */
    render_backend b = review_backend(&st);
    msg_payload payload = {.ns = 100, .ust = 100, .msc = 0};
    work_msg m = {.kind = MSG_STRIPS, .generation = 7, .slot_ = 3, .epoch_ = 9};
    memcpy(m.data, &payload, sizeof payload);
    render_event e = {RENDER_EVENT_WORK, 7, 0, &m};
    T(render_backend_event(&b, &e) == RENDER_ERR_UNSUPPORTED);
    T(st.pending == 1 && review_uploads == 0 && st.metrics.ready_ns == 0);
    m.slot_ = 2; m.epoch_ = 8;
    T(render_backend_event(&b, &e) == RENDER_ERR_UNSUPPORTED);
    m.epoch_ = 9; m.generation = 8;
    T(render_backend_event(&b, &e) == RENDER_ERR_UNSUPPORTED);
    m.generation = 7; payload.msc = 1; memcpy(m.data, &payload, sizeof payload);
    T(render_backend_event(&b, &e) == RENDER_ERR_UNSUPPORTED);
    for (uint32_t kind = MSG_FENCE; kind <= MSG_FAIL; kind++) {
        m.kind = kind; m.slot_ = 3;
        T(render_backend_event(&b, &e) == RENDER_ERR_UNSUPPORTED);
        T(!b.device_seen && !b.complete_seen && st.last_msc == 0);
    }
    /* Leave a second job pending: a zero-duration result must not be usable
     * twice to exhaust pending and expose a still-paused worker's SHM rows. */
    st.pending=2; st.metrics.jobs=2; st.nhandles=2;
    st.handles[1]=(work_handle){4,11};
    band.row_count=2; st.total_rows=2; st.scene.dims.rows=2; st.max_h=2;
    m.kind = MSG_STRIPS; m.slot_ = 2; payload.msc = 0; memcpy(m.data, &payload, sizeof payload);
    T(render_backend_event(&b, &e) == RENDER_OK);
    T(st.pending == 1 && review_uploads == 1);
    T(render_backend_event(&b, &e) == RENDER_ERR_STATE);
    T(st.pending == 1 && review_uploads == 1);
    payload.msc=1; memcpy(m.data,&payload,sizeof payload); m.slot_=4; m.epoch_=11;
    T(render_backend_event(&b,&e)==RENDER_OK);
    T(st.pending==0 && review_uploads==2);
    st.fence_handle = (work_handle){3,9};
    m.kind = MSG_FENCE; m.slot_ = 3; m.epoch_ = 9;
    T(render_backend_event(&b,&e) == RENDER_OK);
    T(render_backend_event(&b,&e) == RENDER_ERR_STATE);
    T(b.device_seen && st.metrics.server_ns == 100);
    m.kind = MSG_PRESENT;
    T(render_backend_event(&b,&e) == RENDER_OK);
    T(render_backend_event(&b,&e) == RENDER_ERR_STATE);
    T(b.complete_seen);
    return 0;
}
/* Atomic batch refusal must avoid the old UI waiting rollback. */
static void *review_release_slot(void *u)
{
    work_pool *p = u;
    struct timespec pause = {0,100000000}; nanosleep(&pause,NULL);
    atomic_store_explicit(&p->slots[0].busy,0,memory_order_release);
    return NULL;
}
static int review_capacity_proposal(void)
{
    review_reset_fixture(); review_partial_capacity = true;
    work_pool pool = {0};
    atomic_store(&pool.slots[0].busy,1); atomic_store(&pool.slots[0].epoch,9);
    render_cell cells[2] = {{0}}; render_strip full = {0,2}, copied;
    cpu_state st = {.up=true,.pool=&pool,.max_strips=1,.cells=cells,.strips=&copied};
    render_grid g = {.dims={1,2,1,1},.cells=cells,.frame_id=7};
    render_backend b = review_backend(&st);
    pthread_t release; T(pthread_create(&release,NULL,review_release_slot,&pool)==0);
    uint64_t start=trace_now_ns();
    int rc=cpu_submit(&b,&g,&full,1);
    uint64_t elapsed=trace_now_ns()-start;
    T(pthread_join(release,NULL)==0);
    T(rc==RENDER_ERR_CAPACITY);
    T(elapsed < UINT64_C(20000000));
    return 0;
}
static int review_diagnostics(void)
{
    render_backend b = {0};
    T(render_null_backend(&b) == RENDER_OK);
    void *state = aligned_alloc(16, 16); T(state != NULL);
    render_config cfg = {.dims = {1,1,1,1}, .max_width = 1, .max_height = 1, .max_cells = 1};
    T(render_backend_init(&b, &cfg, state, b.info.state_size) == RENDER_OK);
    /* Check rejection without outputs first: old code returns true without
     * triggering UB. Then exercise both real output paths under sanitizers. */
    T(!raster_last_present(&b, NULL, NULL));
    uint64_t ust = 123, msc = 456; raster_metrics metrics = {.frame_id = 789};
    T(!raster_last_present(&b, &ust, &msc));
    T(!raster_frame_metrics(&b, &metrics));
    T(ust == 123 && msc == 456 && metrics.frame_id == 789);
    render_backend_shutdown(&b); free(state);
    return 0;
}
static int review_visual(void)
{
    review_fake = false;
    plat pl; plat_config pc = {"P2.5b visual",16,16,false,-1,0};
    T(plat_init(&pl, &pc) == PLAT_OK);
    work_pool pool; T(work_pool_init(&pool,1,1) == 0);
    render_config cfg = {.dims={1,1,1,1},.max_width=1,.max_height=1,.max_cells=1,
        .platform=&pl,.workers=&pool};
    render_backend b = {0}; T(render_cpu_backend(&b) == RENDER_OK);
    void *state = aligned_alloc(64, (b.info.state_size + 63u) & ~(size_t)63u); T(state);
    uint32_t visual = pl.visual;
    pl.visual = UINT32_MAX; /* nonexistent visual: must not accept RGB packing */
    int rc = init_on_worker(&b,&cfg,state,b.info.state_size);
    if (rc == RENDER_OK) render_backend_shutdown(&b);
    pl.visual = visual;
    free(state); work_pool_shutdown(&pool); plat_shutdown(&pl);
    T(rc == RENDER_ERR_UNSUPPORTED);
    struct {xcb_setup_t setup; xcb_screen_t screen; xcb_depth_t depth; xcb_visualtype_t visual;} fixture_x = {0};
    fixture_x.setup.roots_len=1; fixture_x.screen.allowed_depths_len=1;
    fixture_x.depth.depth=24; fixture_x.depth.visuals_len=1;
    fixture_x.visual=(xcb_visualtype_t){.visual_id=123,._class=XCB_VISUAL_CLASS_TRUE_COLOR,
        .bits_per_rgb_value=8,.red_mask=0xff0000,.green_mask=0xff00,.blue_mask=0xff};
    T(cpu_visual_rgb888(&fixture_x.setup,123,24));
    fixture_x.visual.red_mask=0x3ff00000; fixture_x.visual.green_mask=0xffc00;
    fixture_x.visual.blue_mask=0x3ff;
    T(!cpu_visual_rgb888(&fixture_x.setup,123,24));
    fixture_x.depth.depth=30;
    T(!cpu_visual_rgb888(&fixture_x.setup,123,30));
    fixture_x.depth.depth=24; fixture_x.visual.red_mask=0xff0000;
    fixture_x.visual.green_mask=0xff00; fixture_x.visual.blue_mask=0xff;
    fixture_x.visual._class=XCB_VISUAL_CLASS_DIRECT_COLOR;
    T(!cpu_visual_rgb888(&fixture_x.setup,123,24));
    fixture_x.visual._class=XCB_VISUAL_CLASS_TRUE_COLOR; fixture_x.depth.depth=32;
    T(cpu_visual_rgb888(&fixture_x.setup,123,32));
    T(!cpu_visual_rgb888(&fixture_x.setup,123,24));
    return 0;
}
static void review_fill_mailbox(work_ctx *c)
{
    work_msg m = {.kind=99,.generation=c->generation};
    for (uint32_t i=0;i<WORK_MAILBOX_CAP;i++) {
        if (!work_publish(c,&m)) return;
    }
}
static void review_collect(const work_msg *m, void *u)
{
    (void)u;
    if (m->kind != 99 && review_message_count < 8)
        review_messages[review_message_count++] = *m;
}
static int review_wait_free(work_pool *p, work_handle h)
{
    uint64_t limit=trace_now_ns()+UINT64_C(2000000000);
    while (atomic_load_explicit(&p->slots[h.slot].busy,memory_order_acquire)) {
        if (trace_now_ns()>limit) return 1;
        struct timespec pause={0,50000}; nanosleep(&pause,NULL);
    }
    return 0;
}
static int review_fill_pool(work_pool *p)
{
    work_job job={review_fill_mailbox,NULL,77,WORK_RASTER};
    work_handle h=work_submit(p,job); T(h.epoch);
    T(review_wait_free(p,h)==0);
    uint32_t head=atomic_load_explicit(&p->mb[1].head,memory_order_acquire);
    uint32_t tail=atomic_load_explicit(&p->mb[1].tail,memory_order_acquire);
    T(tail-head==WORK_MAILBOX_CAP);
    return 0;
}
static int review_saturated_delivery(void)
{
    review_reset_fixture(); review_real_mailbox=true;
    work_pool pool; T(work_pool_init(&pool,1,1)==0);
    T(review_fill_pool(&pool)==0);
    cpu_state st={.up=true,.pool=&pool,.frame_id=7,.pixmap=42,.have_frame=true,
        .scene={.dims={1,1,1,1}}};
    strip_job j={.st=&st,.njobs=1};
    work_handle h=work_submit(&pool,(work_job){strip_job_fn,&j,7,WORK_RASTER}); T(h.epoch);
    uint64_t deadline=trace_now_ns()+UINT64_C(2000000000);
    while (!atomic_load(&review_publish_calls)) {
        T(trace_now_ns()<deadline);
        struct timespec pause={0,50000}; nanosleep(&pause,NULL);
    }
    /* The producer has attempted publication with an actually full SPSC ring.
     * Drain unrelated traffic and then collect the essential strip result. */
    do {
        (void)work_mailbox_drain(&pool,review_collect,NULL);
        if (review_message_count==1 || !atomic_load_explicit(&pool.slots[h.slot].busy,memory_order_acquire)) break;
        T(trace_now_ns()<deadline);
        struct timespec pause={0,50000}; nanosleep(&pause,NULL);
    } while (true);
    T(review_wait_free(&pool,h)==0);
    /* Bounded drains may leave unrelated traffic ahead of the completion. */
    while (work_mailbox_pending(&pool))
        (void)work_mailbox_drain(&pool,review_collect,NULL);
    bool strip_delivered=review_message_count==1 && review_messages[0].kind==MSG_STRIPS &&
        review_messages[0].slot_==h.slot && review_messages[0].epoch_==h.epoch;
    if (!strip_delivered) {work_pool_shutdown(&pool); review_real_mailbox=false; T(strip_delivered);}
    review_message_count=0; review_publish_calls=0;
    T(review_fill_pool(&pool)==0);
    render_backend b=review_backend(&st);
    T(cpu_present(&b,7)==RENDER_OK); h=st.fence_handle;
    while (!atomic_load(&review_publish_calls)) {
        T(trace_now_ns()<deadline);
        struct timespec pause={0,50000}; nanosleep(&pause,NULL);
    }
    do {
        (void)work_mailbox_drain(&pool,review_collect,NULL);
        if (review_message_count==2 || !atomic_load_explicit(&pool.slots[h.slot].busy,memory_order_acquire)) break;
        T(trace_now_ns()<deadline);
        struct timespec pause={0,50000}; nanosleep(&pause,NULL);
    } while (true);
    T(review_wait_free(&pool,h)==0);
    while (work_mailbox_pending(&pool))
        (void)work_mailbox_drain(&pool,review_collect,NULL);
    bool fence_delivered=review_message_count==2 && review_messages[0].kind==MSG_FENCE &&
        review_messages[1].kind==MSG_PRESENT;
    work_pool_shutdown(&pool); review_real_mailbox=false;
    T(fence_delivered);
    return 0;
}
static int review_delivery(void)
{
    /* Model a full mailbox followed by UI draining it. Essential completion
     * must retry; this applies to strip, fence, Present and failure messages. */
    review_reset_fixture(); work_ctx c = {.generation=7};
    review_publish_failures = 3;
    post(&c, MSG_FENCE, 123, 0, 0, 0);
    T(review_message_count == 1 && review_publish_calls == 4);
    T(review_messages[0].kind == MSG_FENCE);
    review_publish_failures = 2;
    post(&c, MSG_PRESENT, 200, 0, 55, 66);
    T(review_message_count == 2);
    cpu_state st = {.scene = {.dims={1,1,1,1}}};
    strip_job j = {.st=&st,.index=0,.njobs=1}; c.arg = &j;
    review_publish_failures = 2;
    strip_job_fn(&c);
    T(review_message_count == 3 && review_messages[2].kind == MSG_STRIPS);
    review_publish_failures = 100; review_stop_at = 2; review_stop_calls = 0;
    post(&c, MSG_FAIL, 0, -1, 0, 0);
    T(review_message_count == 3 && review_publish_failures > 0);
    T(review_saturated_delivery()==0);
    return 0;
}
static int review_glyph_budget(void)
{
    /* No damaged row / one damaged row still cannot scan an arbitrary table. */
    review_fake = false;
    render_backend b = {0}; T(render_null_backend(&b) == RENDER_OK);
    b.info.capabilities |= RENDER_CAP_RASTER_POOL;
    void *state = aligned_alloc(16,16); T(state);
    const size_t count = 500000;
    render_config cfg = {.dims={1,1,1,1},.max_width=1,.max_height=1,.max_cells=1,
        .max_glyphs=count,.max_pages=1,.max_atlas_bytes=1};
    T(render_backend_init(&b,&cfg,state,b.info.state_size) == RENDER_OK);
    render_cell cell = {1,0,0xffffff,0,0,0}; uint64_t bits;
    uint8_t pixel = 255; render_atlas_page pg = {&pixel,1,1,1,1};
    render_glyph *glyphs = calloc(count,sizeof *glyphs); T(glyphs);
    for (size_t i=0; i<count; i++) glyphs[i]=(render_glyph){1,0,0,0,1,1};
    render_grid g; T(render_grid_init(&g,cfg.dims,&cell,1,&bits,1)==RENDER_OK);
    g.glyphs=glyphs; g.glyph_count=count; g.pages=&pg; g.page_count=1;
    T(render_frame_begin(&g,1)==RENDER_OK && render_mark_full(&g)==RENDER_OK);
    render_strip full={0,1};
    int rc = render_backend_submit(&b,&g,&full,1);
    T(rc == RENDER_ERR_CAPACITY);
    T(!b.active && b.last_frame == 0);
    g.glyph_count=RASTER_FRAME_GLYPH_LIMIT;
    T(render_backend_submit(&b,&g,&full,1)==RENDER_OK);
    T(render_backend_present(&b,1)==RENDER_OK);
    T(render_frame_begin(&g,2)==RENDER_OK); /* zero damage still has a bound */
    g.glyph_count=RASTER_FRAME_GLYPH_LIMIT+1u;
    T(render_backend_submit(&b,&g,NULL,0)==RENDER_ERR_CAPACITY);
    g.glyph_count=RASTER_FRAME_GLYPH_LIMIT;
    glyphs[RASTER_FRAME_GLYPH_LIMIT-1u].w=2; /* unused entries remain validated */
    T(render_backend_submit(&b,&g,NULL,0)==RENDER_ERR_BOUNDS);
    glyphs[RASTER_FRAME_GLYPH_LIMIT-1u].w=1;
    T(render_mark_full(&g)==RENDER_OK); /* one-row typing at the descriptor cap */
    edit_malloc_guard_begin();
    rc=render_backend_submit(&b,&g,&full,1);
    size_t allocations=edit_malloc_guard_end();
    T(rc==RENDER_OK);
    if (edit_malloc_guard_active()) T(allocations==0);
    T(render_backend_present(&b,2)==RENDER_OK);
    render_backend_shutdown(&b); free(glyphs); free(state);
    return 0;
}
static int review_cancellation(void)
{
    review_reset_fixture();
    render_cell cell = {0,RENDER_NO_SLOT,0,0x123456,0,0};
    cpu_state st = {.scene={.dims={1,1,65535,16},.cells=&cell}, .stride_px=65535};
    size_t n = (size_t)65535 * 16;
    st.pix=calloc(n,sizeof *st.pix); T(st.pix);
    work_ctx c = {0}; raster_palette palette = {0};
    row_ctx ctx={&st,&c,&palette};
    review_stop_at=4; do_row(&ctx,0);
    bool tail_untouched = st.pix[n-1]==0;
    bool began = st.pix[0]==0x123456;
    free(st.pix);
    T(tail_untouched && began && review_stop_calls >= 4);
    return 0;
}
static int review_handoff(void)
{
    review_reset_fixture();
    cpu_state st={.up=true,.have_frame=true,.frame_id=7,.pixmap=42};
    render_backend b=review_backend(&st);
    review_enqueue_full=true;
    T(cpu_present(&b,7)==RENDER_ERR_BUSY);
    /* Issue once, retain the phase, retry only immutable job publication. */
    T(review_presents==1);
    unsigned seq=st.metrics.present_sequence;
    review_enqueue_full=false; review_submit_calls=0; review_run_at_submit=true;
    T(cpu_present(&b,7)==RENDER_OK);
    T(review_presents==1 && st.metrics.present_sequence==seq);
    T(review_queued[0].arg != &st && review_queued[0].generation==7);
    T(review_message_count==2);
    return 0;
}
static int review_origin(void)
{
    /* Independent full-surface placement using the row-local calling convention
     * already used by all production/test/bench callers. Check the declaration
     * too, so a stale surface-origin promise cannot regress unnoticed. */
    FILE *h=fopen("src/raster/raster.h","r"); T(h);
    char doc[8192]; size_t n=fread(doc,1,sizeof doc-1,h); fclose(h); doc[n]=0;
    T(strstr(doc,"first pixel row of the requested cell row") != NULL);
    render_cell cells[2]={{0,RENDER_NO_SLOT,0,0x102030,0,0},{0,RENDER_NO_SLOT,0,0x405060,0,0}};
    raster_scene s={.dims={1,2,2,2},.cells=cells};
    uint32_t a[16], b[16];
    for (size_t i=0;i<16;i++) a[i]=b[i]=0xdeadbeef;
    raster_palette palette={0};
    for (uint32_t row=0;row<2;row++) {
        size_t off=(size_t)row*2*4;
        raster_row_scalar(&s,a+off,4,row); raster_row_sse2(&s,b+off,4,row);
    }
    T(memcmp(a,b,sizeof a)==0 && a[0]==0x102030 && a[8]==0x405060);
    for (size_t i=0;i<16;i++) b[i]=0xdeadbeef;
    raster_row_sse2_stream(&s,b+8,4,1);
    T(b[0]==0xdeadbeef && b[8]==0x405060 && b[10]==0xdeadbeef);
    for (size_t i=0;i<16;i++) b[i]=0xdeadbeef;
    raster_row_cached(&s,b+8,4,1,&palette);
    T(b[0]==0xdeadbeef && b[8]==0x405060 && b[10]==0xdeadbeef);
    return 0;
}
static int review_timestamp(void)
{
    review_reset_fixture();
    cpu_state st={.up=true,.have_frame=true,.frame_id=7,.pixmap=42};
    render_backend b=review_backend(&st);
    T(cpu_present(&b,7)==RENDER_OK);
    work_ctx c={.generation=7,.arg=review_queued[0].arg};
    review_queued[0].fn(&c);
    T(review_message_count==2);
    msg_payload t5,t6;
    memcpy(&t5,review_messages[0].data,sizeof t5);
    memcpy(&t6,review_messages[1].data,sizeof t6);
    T(t5.ns==300 && t6.ns==200 && t6.ust==55 && t6.msc==66);
    return 0;
}
/* Optional TRACK self-check for the descriptor cap on the Target-A viewport.
 * Only validation/snapshot/enqueue is timed: the fake scheduler retains jobs
 * without running them, then discards the synthetic frame between samples.
 * This is not a display/G3 benchmark. Run once, with a power/load stamp. */
static int review_ns_compare(const void *left, const void *right)
{
    uint64_t a=*(const uint64_t *)left, b=*(const uint64_t *)right;
    return a < b ? -1 : a > b;
}
static int review_budget_track(void)
{
    review_reset_fixture();
    const render_dims dims={360,120,8,15};
    const size_t ncells=(size_t)dims.cols*dims.rows;
    render_cell *cells=calloc(ncells,sizeof *cells);
    render_glyph *glyphs=calloc(RASTER_FRAME_GLYPH_LIMIT,sizeof *glyphs);
    cpu_state *st=calloc(1,sizeof *st); T(cells && glyphs && st);
    st->cells=calloc(ncells,sizeof *st->cells);
    st->glyphs=calloc(RASTER_FRAME_GLYPH_LIMIT,sizeof *st->glyphs);
    st->pages=calloc(1,sizeof *st->pages); st->strips=calloc(1,sizeof *st->strips);
    T(st->cells && st->glyphs && st->pages && st->strips);
    st->up=true; st->max_strips=1; st->max_cells=ncells;
    st->max_glyphs=RASTER_FRAME_GLYPH_LIMIT; st->max_pages=1;
    st->scene=(raster_scene){dims,st->cells,st->glyphs,0,st->pages,0,0};
    for (size_t i=0;i<ncells;i++) cells[i]=(render_cell){1,0,0xffffff,0x102030,0,0};
    for (size_t i=0;i<RASTER_FRAME_GLYPH_LIMIT;i++) glyphs[i]=(render_glyph){1,0,0,0,1,1};
    uint8_t pixel=255; render_atlas_page page={&pixel,1,1,1,1};
    uint64_t bits[2], samples[256];
    render_grid g; T(render_grid_init(&g,dims,cells,ncells,bits,2)==RENDER_OK);
    g.glyphs=glyphs; g.glyph_count=RASTER_FRAME_GLYPH_LIMIT; g.pages=&page; g.page_count=1;
    render_backend backend={0}; T(raster_review_backend(&backend)==RENDER_OK);
    backend.state=st; backend.initialized=true; backend.full_required=true;
    backend.config=(render_config){.dims=dims,.max_width=2880,.max_height=1800,
        .max_cells=ncells,.max_glyphs=RASTER_FRAME_GLYPH_LIMIT,.max_pages=1,.max_atlas_bytes=1};
    size_t allocations=0;
    for (uint32_t i=0;i<257;i++) {
        review_submit_calls=0;
        render_strip strip=i ? (render_strip){60,1} : (render_strip){0,120};
        uint64_t start=trace_now_ns();
        edit_malloc_guard_begin();
        cells[60u*dims.cols].bg ^= 1u;
        int rc=render_frame_begin(&g,i+1u);
        if (rc==RENDER_OK) rc=i ? render_mark_rows(&g,60,1) : render_mark_full(&g);
        if (rc==RENDER_OK) rc=render_backend_submit(&backend,&g,&strip,1);
        allocations+=edit_malloc_guard_end();
        uint64_t elapsed=trace_now_ns()-start;
        T(rc==RENDER_OK);
        if (i) samples[i-1u]=elapsed;
        /* Unit scheduler has no actual queued/running worker. */
        backend.active=false; st->have_frame=false;
    }
    qsort(samples,256,sizeof samples[0],review_ns_compare);
    if (edit_malloc_guard_active()) T(allocations==0);
    printf("TYPING_BUDGET_TRACK cells=%zu glyphs=%u samples=256 p50=%llu ns p99=%llu ns allocations=%zu guard=%d (M)\n",
        ncells,RASTER_FRAME_GLYPH_LIMIT,(unsigned long long)samples[127],
        (unsigned long long)samples[253],allocations,edit_malloc_guard_active()?1:0);
    free(cells); free(glyphs); free(st->cells); free(st->glyphs); free(st->pages); free(st->strips); free(st);
    review_fake=false;
    return 0;
}
static int review_batch_rollback(void);
static int review_cases(const char *which)
{
    if (!strcmp(which,"budget-track")) return review_budget_track();
    if (!strcmp(which,"batch")) return review_batch_rollback();
    const struct {const char *id; int (*fn)(void);} cases[]={
        {"1",review_ownership},{"4",review_capacity_proposal},{"4",review_batch_rollback},{"2",review_diagnostics},{"3",review_visual},
        {"5",review_delivery},{"6",review_glyph_budget},{"7",review_cancellation},
        {"8",review_handoff},{"15",review_origin},{"17",review_timestamp}};
    for (size_t i=0;i<sizeof cases/sizeof cases[0];i++) {
        if (strcmp(which,"all") && strcmp(which,cases[i].id)) continue;
        int rc=cases[i].fn();
        printf("P2.5b section %s: %s\n",cases[i].id,rc?"RED":"GREEN");
        if (rc) return rc;
    }
    review_fake=false;
    return 0;
}

/* Third enqueue of a full strip batch fails after two workers obtained leases.
 * A helper eventually releases them so the old UI rollback is observable
 * without hanging the test. The assertion counts UI sleeps, not scheduler time. */
static void *review_release_batch(void *arg)
{
    work_pool *p = arg;
    struct timespec pause = {0, 100000000};
    (void)nanosleep(&pause, NULL);
    for (uint32_t i = 0; i < 2; i++)
        atomic_store_explicit(&p->slots[i].busy, 0, memory_order_release);
    return NULL;
}
static int review_batch_rollback(void)
{
    review_reset_fixture(); review_partial_capacity = true; review_fail_at = 3;
    work_pool p = {.efd = -1};
    for (uint32_t i = 0; i < 2; i++) {
        atomic_store(&p.slots[i].busy, 1);
        atomic_store(&p.slots[i].epoch, 10); /* cancelled predecessor of handle 9 */
    }
    render_cell cells[RASTER_JOBS] = {{0}};
    render_strip full = {0, RASTER_JOBS}, copied;
    cpu_state st = {.up = true, .pool = &p, .max_strips = 1, .cells = cells, .strips = &copied};
    render_grid grid = {.dims = {1, RASTER_JOBS, 1, 1}, .cells = cells, .frame_id = 7};
    render_backend backend = review_backend(&st);
    pthread_t helper;
    T(pthread_create(&helper, NULL, review_release_batch, &p) == 0);
    review_ui_thread = pthread_self(); review_watch_sleep = true;
    int rc = cpu_submit(&backend, &grid, &full, 1);
    review_watch_sleep = false;
    T(pthread_join(helper, NULL) == 0);
    printf("raster batch: fail_at=3 strips=4 UI_rollback_waits=%u retained_jobs=%zu\n",
           review_rollback_waits, st.nhandles);
    T(rc == RENDER_ERR_CAPACITY);
    T(review_rollback_waits == 0);
    T(st.nhandles == 0 && st.pending == 0 && !st.have_frame);
    for (size_t i = 0; i < RASTER_JOBS; i++) T(review_queued[i].fn == NULL);
    review_partial_capacity = false; review_submit_calls = 0;
    T(cpu_submit(&backend, &grid, &full, 1) == RENDER_OK);
    T(st.nhandles == RASTER_JOBS && st.pending == RASTER_JOBS && st.have_frame);
    return 0;
}
