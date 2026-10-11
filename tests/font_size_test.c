#include "editor/editor.h"
#include "editor/private.h"
#include "base/base.h"
#include "editor/runtime.h"
#include "raster/raster.h"
#include <math.h>
#include <string.h>
#include <stdio.h>
#include <xkbcommon/xkbcommon-keysyms.h>

#define T(c) do { if (!(c)) { fprintf(stderr, "font_size_test:%d: FAIL %s\n", __LINE__, #c); return 1; } } while (0)
static int settle(editor *e)
{
    int rc;
    do { rc = editor_step(e, 0); if (rc < 0) return rc; }
    while (editor_get_stats(e).pending);
    return 0;
}
static int zoom(editor *e, uint32_t sym)
{
    plat_event key = {.kind = PLAT_EV_KEY, .press = true, .keysym = sym, .mods = PLAT_MOD_CTRL};
    T(editor_inject(e, &key) == 0); T(settle(e) == 0); return 0;
}
/* Hold a frame's borrowed atlas until its delayed T5/T6 completion. */
typedef struct held_frame { bool release; } held_frame;
static int held_init(render_backend *b, const render_config *cfg)
{ (void)cfg; ((held_frame *)b->state)->release = false; return 0; }
static int held_resize(render_backend *b, render_dims dims) { (void)b; (void)dims; return 0; }
static int held_submit(render_backend *b, const render_grid *g, const render_strip *strips, size_t count)
{ (void)b; (void)g; (void)strips; (void)count; return 0; }
static int held_present(render_backend *b, uint32_t id)
{
    if (!((held_frame *)b->state)->release) return RENDER_ERR_BUSY;
    int rc = render_backend_signal(b, RENDER_EVENT_DEVICE_DONE, id, 0);
    return rc ? rc : render_backend_signal(b, RENDER_EVENT_PRESENT_COMPLETE, id, 0);
}
static int held_event(render_backend *b, const render_event *event)
{ (void)b; (void)event; return RENDER_ERR_UNSUPPORTED; }
static void held_shutdown(render_backend *b) { (void)b; }
static int retained_atlas(void)
{
    render_backend backend = {.info = {"held font frame", sizeof(held_frame), _Alignof(held_frame), RENDER_CAP_HEADLESS},
        .ops = {held_init, held_resize, held_submit, held_present, held_event, held_shutdown}};
    editor_config cfg = {.font_dpi = 243.0}; /* supplied panel DPI (P), 10*243/72 em */
    editor *e = NULL; T(editor_open(&e, &cfg, &backend) == 0);
    while (!backend.active) T(editor_step(e, 0) >= 0);
    const uint8_t *old_pixels = e->grid.pages[0].pixels;
    font_cell old_cell = e->font.atlas[e->font.current].atlas.cell;
    plat_event key = {.kind = PLAT_EV_KEY, .press = true, .keysym = XKB_KEY_equal, .mods = PLAT_MOD_CTRL};
    T(editor_inject(e, &key) == 0);
    do { T(editor_step(e, 0) >= 0); } while (!e->font.received || !work_handle_finished(&e->pool, e->font.handle));
    T(e->grid.pages[0].pixels == old_pixels && e->grid.dims.cell_h == old_cell.cell_h);
    /* Opposite request and reset coalesce while the backend retains the image. */
    key.keysym = XKB_KEY_minus; T(editor_inject(e, &key) == 0);
    key.keysym = XKB_KEY_0; T(editor_inject(e, &key) == 0);
    while (e->queue_count) T(editor_step(e, 0) >= 0);
    T(e->font.requested_px == e->font.default_px);
    T(e->grid.pages[0].pixels == old_pixels);
    ((held_frame *)backend.state)->release = true; T(settle(e) == 0);
    T(e->font.applied_px == e->font.default_px);
    T(e->grid.pages[0].pixels == old_pixels);
    T(zoom(e, XKB_KEY_equal) == 0);
    T(e->grid.pages[0].pixels != old_pixels && e->grid.dims.cell_h > old_cell.cell_h);
    /* Shutdown joins an outstanding font job before its arenas are freed. */
    key.keysym = XKB_KEY_minus; T(editor_inject(e, &key) == 0); T(editor_step(e, 0) >= 0);
    editor_close(e);
    puts("font_size_test: retained frame atlas, coalesced reset and outstanding-job shutdown passed");
    return 0;
}
static int display_default(void)
{
    /* (P) bead panel: hypot(2880,1800)/14 inches ~= 243 DPI. 10 pt at
     * supplied 243 DPI is 33.75 em pixels; nearest integer is 34. */
    const double dpi = 243.0;
    T(editor_runtime_em_px(dpi, EDITOR_FONT_DEFAULT_PT) == 34u);
    T(editor_runtime_em_px(96.0, EDITOR_FONT_DEFAULT_PT) == 13u); /* (P) round(10*96/72) */
    T(editor_runtime_em_px(NAN, EDITOR_FONT_DEFAULT_PT) == 0u);
    T(editor_runtime_em_px(96.0, 72.0 / 96.0) == 1u); /* (P) one whole em pixel = 72/DPI points */
    /* Synthetic exact physical fixture (P): 254 mm is 10 inches;
     * 2430 pixels / 10 inches = 243 DPI on both axes. */
    T(round(editor_runtime_dpi(0, 2430, 2430, 254, 254)) == dpi); /* nearest whole DPI */
    T(editor_runtime_dpi(96, 2430, 2430, 254, 254) == 96); /* explicit Xft overrides hardware */
    T(editor_runtime_dpi(0, 0, 0, 0, 0) == 96); /* conventional fallback */
    const char resources[] = "! comment\nXft.dpi:\t243.0\n";
    T(editor_runtime_xft_dpi(resources, sizeof resources - 1) == dpi);
    const char bad[] = "Xft.dpi: nan\nXft.dpi: 243junk\nOther.dpi: 243\n";
    T(editor_runtime_xft_dpi(bad, sizeof bad - 1) == 0);
    T(editor_runtime_xft_dpi("Xft*dpi: 243", sizeof "Xft*dpi: 243" - 1) == dpi);
    uint32_t width, height;
    editor_runtime_window(2880, 1800, &width, &height);
    T(width == 1728 && height == 1260); /* (E) floor(2880*.60), floor(1800*.70) */
    render_backend backend = {0}; T(render_null_backend(&backend) == 0);
    editor_config cfg = {.font_dpi = dpi, .window_width = width, .window_height = height,
        .initial = (const uint8_t *)"tatic", .initial_len = sizeof "tatic" - 1};
    editor *e = NULL; T(editor_open(&e, &cfg, &backend) == 0); T(settle(e) == 0);
    /* (G) existing GL_FRAME_PIXEL_LIMIT in src/gl/renderer.inc: the full
     * ASCII band must fit the maximum unique referenced coverage per frame. */
    T(e->font.capacity <= UINT64_C(262144));
    T(e->font.applied_px == 34u); T(!pthread_equal(e->font.worker, pthread_self()));
    render_dims before = editor_grid(e)->dims;
    T((uint64_t)before.cols * before.cell_w <= width && (uint64_t)before.rows * before.cell_h <= height);
    if (edit_malloc_guard_active()) edit_malloc_guard_begin();
    T(zoom(e, XKB_KEY_equal) == 0);
    T(editor_grid(e)->dims.cell_h > before.cell_h);
    T(editor_grid(e)->dims.cell_w > before.cell_w);
    T(zoom(e, XKB_KEY_minus) == 0);
    T(editor_grid(e)->dims.cell_h == before.cell_h);
    T(zoom(e, XKB_KEY_equal) == 0); T(zoom(e, XKB_KEY_0) == 0);
    T(editor_grid(e)->dims.cell_w == before.cell_w && editor_grid(e)->dims.cell_h == before.cell_h);
    size_t allocations = edit_malloc_guard_active() ? edit_malloc_guard_end() : 0;
    T(allocations == 0); /* (G) law 2: no allocation between zoom ingress and submit */
    T(!pthread_equal(e->font.worker, pthread_self()));
    T(render_grid_validate(editor_grid(e)) == 0);
    T(editor_length(e) == cfg.initial_len);
    editor_close(e);
    puts("font_size_test: display DPI to em, Xft precedence, work-area geometry, worker zoom and allocation guard passed");
    return 0;
}
static int zoom_lower_bound(void)
{
    render_backend backend = {0}; T(render_null_backend(&backend) == 0);
    /* Model CPU's existing per-frame capacity with a headless adapter. */
    backend.info.capabilities |= RENDER_CAP_RASTER_POOL;
    editor_config cfg = {.font_dpi = 243.0, .window_width = 1728, .window_height = 1260};
    /* (P)/(E) panel DPI and .60/.70 geometry are derived in display_default. */
    editor *e = NULL; T(editor_open(&e, &cfg, &backend) == 0); T(settle(e) == 0);
    uint32_t previous;
    do {
        previous = e->font.applied_px; T(zoom(e, XKB_KEY_minus) == 0);
        render_dims dims = editor_grid(e)->dims;
        T((size_t)dims.cols * dims.rows <= RASTER_FRAME_CELL_LIMIT); /* existing CPU snapshot bound */
    } while (e->font.applied_px != previous);
    T(zoom(e, XKB_KEY_0) == 0);
    T(e->font.applied_px == e->font.default_px);
    editor_close(e); puts("font_size_test: zoom bounds respect prepaid CPU/GL frame capacities passed");
    return 0;
}
static int minimap_zoom(void)
{
    render_backend backend = {0}; T(render_null_backend(&backend) == 0);
    /* Exercise the launch work-area reserve at conventional 96 DPI. */
    editor_config cfg = {.font_dpi = 96.0, .window_width = 1728, .window_height = 1260,
        .max_window_width = 2880, .max_window_height = 1800,
        .initial = (const uint8_t *)"tatic\nminimap", .initial_len = sizeof "tatic\nminimap" - 1};
    editor *e = NULL; T(editor_open(&e, &cfg, &backend) == 0); T(settle(e) == 0);
    plat_event resize = {.kind = PLAT_EV_RESIZE, .w = 2880, .h = 1800};
    T(editor_inject(e, &resize) == 0); T(settle(e) == 0);
    if (edit_malloc_guard_active()) edit_malloc_guard_begin();
    uint32_t previous;
    do {
        previous = e->font.applied_px; T(zoom(e, XKB_KEY_equal) == 0);
        T(e->map_grid.dims.cell_w == 1u && e->map_grid.dims.cell_h == 2u);
        T(e->map_grid.dims.rows == (e->text_grid.dims.rows * e->grid.dims.cell_h + 1u) / 2u);
        T(e->map_grid.dims.rows <= e->map_capacity);
        T(e->grid.region.width == e->map_cols * e->grid.dims.cell_w);
        T(e->grid.region.height == e->text_grid.dims.rows * e->grid.dims.cell_h);
        T((size_t)e->grid.region.width * e->grid.region.height <= e->map_pixel_capacity);
        T(render_grid_validate(editor_grid(e)) == 0);
    } while (e->font.applied_px != previous);
    T(e->font.applied_px == EDITOR_FONT_MAX_PX);
    T(zoom(e, XKB_KEY_0) == 0);
    size_t allocations = edit_malloc_guard_active() ? edit_malloc_guard_end() : 0;
    T(allocations == 0); /* law 2: prepaid zoom and pixel composition */
    T(editor_length(e) == cfg.initial_len);
    editor_close(e); puts("font_size_test: work-area resize and maximum zoom preserve two-pixel minimap geometry and prepaid pixels passed");
    return 0;
}
int main(void)
{
    render_backend backend = {0}; T(render_null_backend(&backend) == 0);
    /* Existing bake (P): twice the old default 15 px = 30 px. */
    editor_config cfg = {.font_px = 30, .initial = (const uint8_t *)"lowercase", .initial_len = sizeof "lowercase" - 1};
    editor *e = NULL; T(editor_open(&e, &cfg, &backend) == 0); T(settle(e) == 0);
    render_dims before = editor_grid(e)->dims;
    T(zoom(e, XKB_KEY_equal) == 0);
    T(editor_grid(e)->dims.cell_h > before.cell_h);
    T(zoom(e, XKB_KEY_minus) == 0);
    T(editor_grid(e)->dims.cell_h == before.cell_h);
    T(zoom(e, XKB_KEY_equal) == 0); T(zoom(e, XKB_KEY_0) == 0);
    T(editor_grid(e)->dims.cell_h == before.cell_h);
    T(editor_length(e) == cfg.initial_len);
    editor_close(e);
    puts("font_size_test: zoom changes cells and reset restores default; document unchanged");
    T(display_default() == 0);
    T(retained_atlas() == 0);
    T(zoom_lower_bound() == 0);
    T(minimap_zoom() == 0);
    return 0;
}
