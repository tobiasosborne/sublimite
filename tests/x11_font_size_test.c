#include "editor/editor.h"
#include "editor/runtime.h"
#include "editor/private.h"
#include "raster/raster.h"
#include <stdio.h>
#include <xcb/xcb.h>
#include <xkbcommon/xkbcommon-keysyms.h>

#define T(c) do { if (!(c)) { fprintf(stderr, "x11_font_size_test:%d: FAIL %s\n", __LINE__, #c); return 1; } } while (0)
static int settle(editor *e)
{
    do { T(editor_step(e, 0) >= 0); } while (editor_get_stats(e).pending || editor_get_stats(e).render_active);
    return 0;
}
int main(void)
{
    editor_display_size display;
    T(editor_runtime_display_size(&display) == 0);
    uint32_t width, height;
    editor_runtime_window(display.work_width, display.work_height, &width, &height);
    editor_config config = {.font_dpi = display.dpi, .window_width = width, .window_height = height,
        .max_window_width = display.work_width, .max_window_height = display.work_height,
        .initial = (const uint8_t *)"tatic", .initial_len = sizeof "tatic" - 1};
    render_backend backend = {0}; T(render_cpu_backend(&backend) == 0);
    editor *e = NULL; T(editor_open(&e, &config, &backend) == 0); T(settle(e) == 0);
    T(e->font.applied_px == editor_runtime_em_px(display.dpi, EDITOR_FONT_DEFAULT_PT));
    xcb_connection_t *c = e->platform.conn;
    xcb_get_geometry_reply_t *geometry = xcb_get_geometry_reply(c, xcb_get_geometry(c, e->platform.win), NULL);
    T(geometry != NULL); T(geometry->width == width && geometry->height == height); free(geometry);
    render_dims before = editor_grid(e)->dims;
    plat_event key = {.kind = PLAT_EV_KEY, .press = true, .keysym = XKB_KEY_equal, .mods = PLAT_MOD_CTRL};
    T(editor_inject(e, &key) == 0); T(settle(e) == 0); T(editor_grid(e)->dims.cell_h > before.cell_h);
    key.keysym = XKB_KEY_minus; T(editor_inject(e, &key) == 0); T(settle(e) == 0);
    T(editor_grid(e)->dims.cell_h == before.cell_h);
    key.keysym = XKB_KEY_equal; T(editor_inject(e, &key) == 0); T(settle(e) == 0);
    key.keysym = XKB_KEY_0; T(editor_inject(e, &key) == 0); T(settle(e) == 0);
    T(editor_grid(e)->dims.cell_h == before.cell_h);
    printf("x11_font_size_test: DPI %.6f from X resources/physical display; em %u = round(%.1f*DPI/72); window %ux%u = floor(work-area %ux%u * .60,.70); native zoom passed\n",
        display.dpi, e->font.default_px, EDITOR_FONT_DEFAULT_PT, width, height, display.work_width, display.work_height);
    editor_close(e); return 0;
}
