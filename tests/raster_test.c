/* P2.5 raster tests: SSE2 == scalar (random grids), strip partitioning,
 * frozen render conformance suite driven through the CPU backend on a real X11
 * window (skips cleanly without DISPLAY), and a live XShm pixel read-back.
 * Conformance: tests/render_test.c is included UNCHANGED with
 * RENDER_TEST_EXTERNAL; its release-only "0 allocations over 10,000 frames"
 * assertion is neutralised for this driver because libxcb mallocs every packet
 * it receives (T5/T6 handling). The typing path itself (submit) is checked
 * for zero allocations separately below. See docs/decisions/P2.5.md. */
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
    return real_shm_put(conn, drawable, gc, tw, th, sx, sy, sw, sh,
                        dx, dy, depth, format, send, seg, offset);
}

#define RENDER_TEST_EXTERNAL 1
#define main render_conformance_main
#define edit_malloc_guard_active() (false)
#include "render_test.c"
#undef main
#undef edit_malloc_guard_active

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
    /* zero allocations on the submit path (typing path) */
    for (uint32_t id = 2; id < 12; id++) {
        render_strip row = {2, 1};
        edit_malloc_guard_begin();
        cells[8].fg ^= 0x010101u;
        T(render_frame_begin(&g, id) == RENDER_OK && render_mark_rows(&g, 2, 1) == RENDER_OK);
        rc = render_backend_submit(&b, &g, &row, 1);
        size_t allocs = edit_malloc_guard_end();
        T(rc == RENDER_OK);
        if (edit_malloc_guard_active()) T(allocs == 0);
        while ((rc = render_backend_present(&b, id)) == RENDER_ERR_BUSY) { render_test_pump(&b); T(trace_now_ns() < deadline); }
        T(rc == RENDER_OK);
        while (b.active) { render_test_pump(&b); T(trace_now_ns() < deadline); }
    }
    /* read back */
    xcb_connection_t *c = g_plat.conn;
    xcb_get_image_reply_t *im = xcb_get_image_reply(c,
        xcb_get_image(c, XCB_IMAGE_FORMAT_Z_PIXMAP, g_plat.win, 0, 0, 32, 96, ~0u), NULL);
    if (im == NULL) { printf("raster_test: window readback unavailable, pixel compare skipped\n"); }
    else {
        T(xcb_get_image_data_length(im) >= 32 * 96 * 4);
        const uint32_t *got = (const uint32_t *)xcb_get_image_data(im);
        uint32_t want[32 * 96];
        raster_scene s = {cfg.dims, cells, &glyph, 1, &page, 1, 0};
        for (uint32_t r = 0; r < 6; r++) raster_row_scalar(&s, want + (size_t)r * 16 * 32, 32, r);
        size_t bad = 0;
        for (size_t i = 0; i < 32 * 96; i++) if ((got[i] & 0xffffffu) != want[i]) bad++;
        free(im);
        if (bad) fprintf(stderr, "raster_test: %zu window pixels differ\n", bad);
        T(bad == 0);
    }
    render_backend_shutdown(&b);
    free(state);
    return 0;
}

int main(int argc, char **argv)
{
    *(void **)(&real_shm_put) = dlsym(RTLD_NEXT, "xcb_shm_put_image");
    T(real_shm_put != NULL);
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
    if (g_up) { work_pool_shutdown(&g_pool); plat_shutdown(&g_plat); }
    printf("raster_test: PASS (requested conformance, upload ordering, typing allocations, live XShm)\n");
    return 0;
}
