#include "editor/editor.h"
#include "editor/private.h"
#include "raster/raster.h"
#include "gl/gl.h"
#include "base/base.h"
#include "trace/trace.h"
#include "trace/trace_fmt.h"
#include "journal/journal.h"
#include "x11/input.h"
#include "findui/findui.h"
#include "fif/fif.h"
#include "tree_fixture.h"
#include <sys/mman.h>
#include <xkbcommon/xkbcommon-keysyms.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <sched.h>
#include <dirent.h>
#include <stdarg.h>
#include <sys/syscall.h>
#include <sys/stat.h>
#include <sys/socket.h>

#define T(c) do { if (!(c)) { fprintf(stderr, "editor_test:%d: FAIL %s\n", __LINE__, #c); return 1; } } while (0)
typedef struct counted { bool active, suspended; size_t allocations, frames; } counted;
static void ingress(void *ctx, uint64_t seq, uint64_t ns)
{
    counted *c = ctx; (void)seq; (void)ns;
    if (!c->active) { edit_malloc_guard_begin(); c->active = true; }
}
static void submitted(void *ctx, const editor_frame *frame)
{
    counted *c = ctx; (void)frame;
    if (c->active) { c->allocations += edit_malloc_guard_end(); c->active = false; }
    c->frames++;
}
static void io_boundary(void *ctx, bool entering)
{
    counted *c = ctx;
    if (entering && c->active) {
        c->allocations += edit_malloc_guard_end(); c->active = false; c->suspended = true;
    } else if (!entering && c->suspended) {
        edit_malloc_guard_begin(); c->active = true; c->suspended = false;
    }
}
static plat_event key(uint32_t sym, uint16_t mods, const char *text)
{
    plat_event ev = {.kind = PLAT_EV_KEY, .press = true, .keysym = sym, .mods = mods};
    if (text) { size_t n = strlen(text); EDIT_ASSERT(n <= PLAT_UTF8_MAX); ev.utf8_len = (uint8_t)n; memcpy(ev.utf8, text, n); }
    return ev;
}
static int settle(editor *e)
{
    uint64_t deadline = trace_now_ns() + UINT64_C(5000000000);
    do {
        int rc = editor_step(e, 20); T(rc == EDITOR_OK || rc == EDITOR_MORE);
        editor_stats s = editor_get_stats(e);
        if (!s.pending && !s.render_active) return 0;
    } while (trace_now_ns() < deadline);
    T(false); return 1;
}
static int press(editor *e, plat_event ev)
{
    T(editor_inject(e, &ev) == EDITOR_OK); return settle(e);
}
static int expect(editor *e, const char *text, uint64_t cursor)
{
    uint8_t bytes[256]; size_t n = strlen(text);
    T(editor_length(e) == n); T(editor_read(e, 0, bytes, n) == 0);
    if (memcmp(bytes, text, n)) fprintf(stderr, "expected text: [%s]\nactual text: [%.*s]\n", text, (int)n, bytes);
    T(memcmp(bytes, text, n) == 0); T(editor_view(e).selection.cursor == cursor);
    return 0;
}
/* edit-hgd (P): reuse editor_p4_test's composed-cell indexing, adding the
 * live chrome/gutter offsets. Zero is the first row/column or absence, one
 * removes a string NUL or advances a cell. Geometry: sixteen columns for the
 * existing tab slot plus eight for the minimap; a chrome row plus the longest
 * fixture's newline count plus its final empty line. Minus one is the existing
 * no-wrap option. No time budget. */
static const render_cell *trailing_cell(editor *e, uint32_t row, size_t column)
{
    const render_grid *g = editor_grid(e);
    return &g->cells[(size_t)(row + e->tab_rows) * g->dims.cols + e->lay.gw + column];
}
static int trailing_open(editor **out, render_backend *backend, const char *text,
                         const char *preferences, counted *count)
{
    T(render_null_backend(backend) == RENDER_OK);
    editor_config cfg = {.initial = (const uint8_t *)text, .initial_len = strlen(text),
        .cols = 16u + 8u, .rows = 1u + 3u, .wrap_mode = -1,
        .preferences_path = preferences, .hook_ctx = count,
        .on_ingress = ingress, .on_submit = submitted};
    T(editor_open(out, &cfg, backend) == EDITOR_OK);
    return settle(*out);
}
static int trailing_typing(const char *preferences)
{
    editor *e = NULL; render_backend backend = {0}; counted count = {0};
    T(trailing_open(&e, &backend, "", preferences, &count) == 0);
    const char typed[] = "word ";
    for (size_t i = 0; i < sizeof typed - 1u; i++) {
        char text[] = {typed[i], '\0'};
        T(press(e, key((uint32_t)(uint8_t)typed[i], 0, text)) == EDITOR_OK);
    }
    T(expect(e, typed, sizeof typed - 1u) == 0);
    const render_cell *cell = trailing_cell(e, 0, sizeof "word" - 1u);
    printf("trailing typing %s: space bg=%#x plain=%#x marker=%#x\n",
        preferences ? "all" : "default", cell->bg, e->layout_cfg.bg,
        config_theme_color(e->preferences.theme, "error.background"));
    bool plain = cell->bg == e->layout_cfg.bg && !(cell->attrs & RENDER_ATTR_UNDERLINE);
    T(!count.active && !count.allocations);
    editor_close(e);
    T(plain);
    return 0;
}
static int trailing_config_file(char *path, const char *source)
{
    /* (P) mkstemp's owner-only temporary file is outside the typing guard. */
    int fd = mkstemp(path); T(fd >= 0);
    T(write(fd, source, strlen(source)) == (ssize_t)strlen(source));
    T(close(fd) == 0); return 0;
}
static int trailing_all(void)
{
    char path[] = "/tmp/editor-trailing-all-XXXXXX";
    T(trailing_config_file(path, "whitespace.trailing = 'all'\n") == 0);
    editor *e = NULL; render_backend backend = {0}; counted count = {0};
    const char text[] = "word   \n";
    T(trailing_open(&e, &backend, text, path, &count) == 0);
    T(editor_set_cursor(e, sizeof text - 1u) == EDITOR_OK); T(settle(e) == 0);
    for (size_t column = sizeof "word" - 1u; column < sizeof "word   " - 1u; column++) {
        const render_cell *cell = trailing_cell(e, 0, column);
        T(cell->bg == config_theme_color(e->preferences.theme, "error.background"));
        T(cell->attrs & RENDER_ATTR_UNDERLINE);
    }
    editor_close(e);
    int result = trailing_typing(path);
    T(unlink(path) == 0); T(result == 0);
    puts("trailing all: completed line marked, typing cell plain passed");
    return 0;
}
static int trailing_selection(void)
{
    char path[] = "/tmp/editor-trailing-selection-XXXXXX";
    T(trailing_config_file(path, "[whitespace]\ntrailing='selection'\n") == 0);
    editor *e = NULL; render_backend backend = {0}; counted count = {0};
    T(trailing_open(&e, &backend, "word   \nother  \n", path, &count) == 0);
    T(!(trailing_cell(e, 0, sizeof "word" - 1u)->attrs & RENDER_ATTR_UNDERLINE));
    T(editor_set_cursor(e, sizeof "word" - 1u) == EDITOR_OK); T(settle(e) == 0);
    for (size_t i = 0; i < sizeof "  " - 1u; i++)
        T(press(e, key(XKB_KEY_Right, PLAT_MOD_SHIFT, NULL)) == EDITOR_OK);
    const render_cell *selected = trailing_cell(e, 0, sizeof "word" - 1u);
    T(selected->bg == e->layout_cfg.sel_bg && (selected->attrs & RENDER_ATTR_UNDERLINE));
    T(!(trailing_cell(e, 0, sizeof "word " - 1u)->attrs & RENDER_ATTR_UNDERLINE));
    T(!(trailing_cell(e, 0, sizeof "word  " - 1u)->attrs & RENDER_ATTR_UNDERLINE));
    T(!(trailing_cell(e, 1, sizeof "other" - 1u)->attrs & RENDER_ATTR_UNDERLINE));
    /* (P) Reverse the same half-open selection; its lower endpoint becomes
     * the caret, so both selected spaces are now away from the typing cell. */
    T(editor_set_cursor(e, sizeof "word  " - 1u) == EDITOR_OK); T(settle(e) == 0);
    for (size_t i = 0; i < sizeof "  " - 1u; i++)
        T(press(e, key(XKB_KEY_Left, PLAT_MOD_SHIFT, NULL)) == EDITOR_OK);
    T(trailing_cell(e, 0, sizeof "word" - 1u)->attrs & RENDER_ATTR_UNDERLINE);
    T(trailing_cell(e, 0, sizeof "word " - 1u)->attrs & RENDER_ATTR_UNDERLINE);
    T(!(trailing_cell(e, 0, sizeof "word  " - 1u)->attrs & RENDER_ATTR_UNDERLINE));
    T(press(e, key(XKB_KEY_Left, 0, NULL)) == EDITOR_OK);
    T(!(trailing_cell(e, 0, sizeof "word" - 1u)->attrs & RENDER_ATTR_UNDERLINE));
    T(!count.active && !count.allocations);
    editor_close(e); T(unlink(path) == 0);
    puts("trailing selection: empty, partial, reverse and cleared selection passed");
    return 0;
}
static int trailing_disabled_scan(void)
{
    editor *e = NULL; render_backend backend = {0}; counted count = {0};
    T(trailing_open(&e, &backend, "word   \n", NULL, &count) == 0);
    /* (P) A nonzero capacity with NULL output is rejected by indent's range
     * function. Successful prepare proves none bypasses that function. */
    indent_range *saved = e->ws_ranges; e->ws_ranges = NULL;
    T(editor_full_layout(e) == EDITOR_OK); T(settle(e) == 0);
    T(e->ws_count == 0); e->ws_ranges = saved;
    T(trailing_cell(e, 0, sizeof "word" - 1u)->bg == e->layout_cfg.bg);
    editor_close(e);
    puts("trailing none: range computation bypassed passed");
    return 0;
}
static int paint_all_void(void)
{
    editor *e = NULL; render_backend backend = {0}; counted count = {0};
    T(trailing_open(&e, &backend, "prefix\n()  \n", NULL, &count) == 0);
    /* (P) Reuse trailing_open's geometry. The top follows the prefix (minus
     * its string NUL), with a label past the exact line count. Clipping every
     * row leaves the layout position at zero, below that top. Zero also
     * denotes success, no error and blank cells. */
    T(editor_begin_frame(e) == 0); e->paint_ready = false;
    e->v.state.first_byte = e->lay.first_byte = sizeof "prefix\n" - 1u; e->lay.pos = 0;
    e->v.state.first_line = e->lay.first_line = e->buffer->lines;
    e->buffer->scroll.first_byte = e->v.state.first_byte;
    e->buffer->scroll.first_line = e->buffer->scroll.anchor_line = e->v.state.first_line;
    e->buffer->scroll_display = e->buffer->scroll;
    e->preferences.trailing = CONFIG_TRAILING_ALL;
    for (uint32_t r = 0; r < e->text_grid.dims.rows; r++) e->row_byte[r] = LAYOUT_VOID_ROW;
    T(layout_relayout_rows(&e->lay, 0, e->text_grid.dims.rows) >= 0);
    while (layout_busy(&e->lay)) T(layout_run(&e->lay) >= 0);
    T(e->lay.pos < e->v.state.first_byte);
    size_t frames = count.frames;
    int prepared = editor_paint_prepare(e);
    int step = editor_step(e, 0);
    int latched = e->error;
    printf("all-void paint: prepare=%d step=%d latched=%d\n", prepared, step, latched);
    if (prepared == 0 && step >= 0) T(settle(e) == 0);
    bool blank_frame = count.frames > frames && e->paint_ready &&
        e->bracket_source == INDENT_NONE && e->bracket_mate == INDENT_NONE && e->ws_count == 0;
    for (uint32_t r = 0; r < e->text_grid.dims.rows; r++) {
        blank_frame &= e->row_byte[r] == LAYOUT_VOID_ROW;
        for (uint32_t c = 0; c < e->text_grid.dims.cols; c++) {
            const render_cell *cell = &e->text_grid.cells[(size_t)r * e->text_grid.dims.cols + c];
            blank_frame &= cell->glyph_index == 0 && cell->atlas_slot == RENDER_NO_SLOT &&
                cell->attrs == 0 && cell->bg == (c < e->lay.gw ? e->lay.cfg.gutter_bg : e->lay.cfg.bg);
        }
    }
    editor_close(e);
    T(prepared == 0); T(step == EDITOR_OK || step == EDITOR_MORE); T(latched == 0); T(blank_frame);
    puts("all-void paint: blank frame submitted passed");
    return 0;
}
/* edit-g11: use the drive's keystroke count (G: the supplied typing repro),
 * and the allocation fixture's grid geometry. Storage covers cfg.rows times
 * the editor's existing eight-column minimap, including the chrome row. */
typedef struct flash_probe {
    editor *e;
    counted allocations;
    render_cell *previous;
    uint32_t *previous_pixels;
    size_t frames, orange_frames, stale_frames, changed_pending, chrome_markers;
    uint64_t fill_max_ns;
} flash_probe;
static void flash_ingress(void *ctx, uint64_t seq, uint64_t ns)
{
    flash_probe *p = ctx; ingress(&p->allocations, seq, ns);
}
static void flash_submit(void *ctx, const editor_frame *frame)
{
    flash_probe *p = ctx; submitted(&p->allocations, frame);
    if (!p->e) return;
    editor *e = p->e; bool orange = false;
    for (uint32_t r = e->tab_rows; r < e->grid.dims.rows; r++) {
        for (uint32_t c = e->text_grid.dims.cols; c < e->grid.dims.cols; c++) {
            const render_cell *cell = &e->grid.cells[(size_t)r * e->grid.dims.cols + c];
            /* Raw stale paint and its actual background blend:
             * (0x171b22 + 0xbf853d) / two, rounded per channel by minimap. */
            if (cell->fg == 0xbf853d || cell->bg == 0xbf853d || cell->bg == 0x6b5030)
                orange = true;
        }
    }
    p->frames++; p->orange_frames += orange;
    if (e->buffer->map.stale) {
        p->stale_frames++;
        p->changed_pending += memcmp(p->previous, e->map_grid.cells,
            (size_t)e->map_grid.dims.rows * e->map_grid.dims.cols * sizeof(render_cell)) != 0;
        p->changed_pending += memcmp(p->previous_pixels, e->grid.region.pixels,
            (size_t)e->grid.region.width * e->grid.region.height * sizeof(uint32_t)) != 0;
    }
    if (e->tab_rows && e->grid.cells[e->grid.dims.cols - 1u].glyph_index == '!')
        p->chrome_markers++;
    if (e->stats.minimap_ns > p->fill_max_ns) p->fill_max_ns = e->stats.minimap_ns;
}
static int minimap_typing_flash(void)
{
    render_backend backend = {0}; T(render_null_backend(&backend) == 0);
    flash_probe probe = {0};
    editor_config cfg = {.initial = (const uint8_t *)"abc\n", .initial_len = sizeof "abc\n" - 1,
        .cols = 40, .rows = 8, .hook_ctx = &probe,
        .on_ingress = flash_ingress, .on_submit = flash_submit};
    editor *e = NULL; T(editor_open(&e, &cfg, &backend) == 0); T(settle(e) == 0); probe.e = e;
    probe.previous = calloc((size_t)e->map_grid.dims.rows * e->map_grid.dims.cols, sizeof *probe.previous);
    probe.previous_pixels = calloc(e->map_pixel_capacity, sizeof *probe.previous_pixels);
    T(probe.previous && probe.previous_pixels);
    /* N comes from the supplied Astra repro: 95 keystrokes, not a timing bound. */
    const size_t keys = 95;
    for (size_t i = 0; i < keys; i++) {
        memcpy(probe.previous, e->map_grid.cells,
            (size_t)e->map_grid.dims.rows * e->map_grid.dims.cols * sizeof(render_cell));
        memcpy(probe.previous_pixels, e->grid.region.pixels,
            (size_t)e->grid.region.width * e->grid.region.height * sizeof(uint32_t));
        T(press(e, key('x', 0, "x")) == 0);
    }
    /* Settle excludes index maintenance: explicitly run its next turn so the
     * last typed frame is followed by the ready map's publication. */
    uint64_t ready_deadline=trace_now_ns()+UINT64_C(5000000000); /* existing settle bound */
    while (e->buffer->map.stale || e->buffer->map.revision!=e->buffer->revision || e->dirty) {
        T(editor_step(e,0)>=0); T(trace_now_ns()<ready_deadline); sched_yield();
    }
    printf("edit-g11 null typing: keys=%zu frames=%zu orange_frames=%zu stale_frames=%zu "
        "changed_pending=%zu chrome_markers=%zu mallocs=%zu fill_max_ns=%llu\n",
        keys, probe.frames, probe.orange_frames, probe.stale_frames, probe.changed_pending,
        probe.chrome_markers, probe.allocations.allocations, (unsigned long long)probe.fill_max_ns);
    T(probe.frames >= keys && probe.stale_frames > 0); T(probe.orange_frames == 0);
    T(probe.changed_pending == 0 && probe.chrome_markers == 0);
    T(!probe.allocations.active && probe.allocations.allocations == 0);
    T(!e->buffer->map.stale && e->buffer->map.revision == e->buffer->revision);
    T(editor_length(e) == cfg.initial_len + keys);
    free(probe.previous_pixels); free(probe.previous); editor_close(e); return 0;
}
/* edit-d5o: geometry derives from the existing allocation fixture. The
 * reference prepares ceil(map pixel height / PRD's two pixels per line)
 * summaries independently, so a text-cell-height adapter cannot pass. */
/* edit-73j: summaries publish from a worker; wait for the published map and
 * its frame (existing settle bound) before reading pixels. */
static int settle_map(editor *e)
{
    uint64_t deadline = trace_now_ns() + UINT64_C(5000000000); /* existing settle bound */
    for (;;) {
        /* Mirrors editor_map_service: a job is in flight, or one is due. */
        minimap_input input = editor_map_input(e->buffer);
        bool due = e->map_cols && !e->buffer->pending_open && !e->buffer->source_stale && input.index_ready &&
            (minimap_stale(&e->buffer->map, &input) || e->buffer->map.height != e->map_grid.dims.rows);
        if (!e->buffer->map_snapshot && !due && !e->dirty) break;
        T(editor_step(e, 0) >= 0); T(trace_now_ns() < deadline); sched_yield();
    }
    return settle(e);
}
/* edit-2mhd: (P) the alphabet fixture has one character plus LF per line;
 * sizeof supplies its byte stride and line count. (E) reuse the existing
 * minimap compose fixture's geometry. The initial top is one viewport down,
 * the grab and motion are half a viewport, and the outside click is half a
 * viewport beyond its bottom. (P) two pixels per summary is the existing
 * minimap geometry; one pixel selects the second scanline within a summary.
 * Zero means no allocation/displacement, one is the primary button or an
 * inclusive/exclusive endpoint adjustment; minus one disables wrapping. */
static int minimap_grab_contract(bool outside)
{
    const char fixture[] = "a\nb\nc\nd\ne\nf\ng\nh\ni\nj\nk\nl\nm\nn\no\np\nq\nr\ns\nt\nu\nv\nw\nx\ny\nz\n";
    render_backend backend = {0}; T(render_null_backend(&backend) == 0);
    editor_config cfg = {.initial = (const uint8_t *)fixture, .initial_len = sizeof fixture - 1u,
        .cols = 40, .rows = 8, .wrap_mode = -1};
    editor *e = NULL; T(editor_open(&e, &cfg, &backend) == 0); T(settle_map(e) == 0);
    const uint64_t stride = sizeof "a\n" - 1u;
    uint64_t first = e->v.config.rows, offset = e->v.config.rows / 2u, motion = offset;
    T(editor_scroll_seek(e, first * stride) == 0); T(editor_begin_frame(e) == 0); T(settle_map(e) == 0);
    T(e->v.state.first_line == first && e->buffer->map.lines_per_row == 1u);
    uint64_t clicked = outside ? first + e->v.config.rows + offset : first + offset;
    uint64_t expected = outside ? clicked - offset : first;
    T(expected + motion + e->v.config.rows < sizeof fixture / stride);
    plat_event ev = {.kind = PLAT_EV_BUTTON, .code = 1, .press = true,
        .x = (int32_t)(e->text_grid.dims.cols * e->grid.dims.cell_w),
        .y = (int32_t)(e->tab_rows * e->grid.dims.cell_h + clicked * 2u + 1u)};
    edit_malloc_guard_begin();
    int press_rc = editor_pointer(e, &ev), press_settled = settle(e);
    uint64_t pressed_line = e->v.state.first_line, pressed_byte = e->v.state.first_byte;
    ev.kind = PLAT_EV_MOTION; ev.y += (int32_t)(motion * 2u);
    int motion_rc = editor_pointer(e, &ev), motion_settled = settle(e);
    uint64_t moved_line = e->v.state.first_line, moved_byte = e->v.state.first_byte;
    ev.kind = PLAT_EV_BUTTON; ev.press = false;
    int release_rc = editor_pointer(e, &ev); bool released = !e->map_drag;
    size_t allocations = edit_malloc_guard_end();
    bool guard_active = edit_malloc_guard_active();
    editor_close(e);
    printf("edit-2mhd %s: press top=%llu expected=%llu; motion top=%llu expected=%llu; mallocs=%zu guard=%s\n",
        outside ? "outside" : "inside", (unsigned long long)pressed_line, (unsigned long long)expected,
        (unsigned long long)moved_line, (unsigned long long)(expected + motion), allocations,
        guard_active ? "active" : "ASan-inert");
    T(press_rc == 0 && press_settled == 0 && motion_rc == 0 && motion_settled == 0 && release_rc == 0 && released);
    T(moved_line == expected + motion && moved_byte == (expected + motion) * stride);
    T(pressed_line == expected && pressed_byte == expected * stride);
    T(allocations == 0);
    return 0;
}
static int minimap_compose_pixels(const char *backend_name)
{
    render_backend backend = {0};
    T((backend_name ? editor_backend_select(&backend, backend_name) : render_null_backend(&backend)) == 0);
    const char fixture[] = "a b\n  cd\n\txyz\nq\nr\ns\nt\nu\nv\nw\nx\ny\nz\n";
    editor_config cfg = {.initial = (const uint8_t *)fixture, .initial_len = sizeof fixture - 1,
        .cols = 40, .rows = 8};
    editor *e = NULL; T(editor_open(&e, &cfg, &backend) == 0); T(settle(e) == 0); T(settle_map(e) == 0);
    uint32_t w = e->grid.dims.cols * e->grid.dims.cell_w;
    uint32_t h = e->grid.dims.rows * e->grid.dims.cell_h;
    uint32_t mw = e->map_cols * e->grid.dims.cell_w;
    uint32_t mh = e->text_grid.dims.rows * e->grid.dims.cell_h;
    uint32_t summaries = (mh + 1u) / 2u; /* (G) PRD: two pixels per line. */
    minimap_row *rows = calloc(summaries, sizeof *rows);
    uint32_t *expected = calloc((size_t)mw * mh, sizeof *expected);
    uint32_t *frame = calloc((size_t)w * h, sizeof *frame); T(rows && expected && frame);
    minimap reference; minimap_input input = editor_map_input(e->buffer);
    minimap_style colors = {0x20242c, 0x78808e, 0xe5e9f0, 0}; /* existing paint theme */
    T(minimap_init(&reference, rows, summaries) == 0);
    T(minimap_prepare(&reference, &input, summaries) == 0);
    uint64_t visible = e->buffer->lines < e->text_grid.dims.rows ? e->buffer->lines : e->text_grid.dims.rows;
    T(minimap_pixels(&reference, expected, (size_t)mw * mh, mw, mh, mw, 0, visible, &colors) == 0);
    raster_scene scene = {e->grid.dims, e->grid.cells, e->grid.glyphs,
        e->grid.glyph_count, e->grid.pages, e->grid.page_count, 0};
    for (uint32_t r = 0; r < e->grid.dims.rows; r++)
        raster_row_scalar(&scene, frame + (size_t)r * e->grid.dims.cell_h * w, w, r);
    raster_blit_pixels(&e->grid.region, frame, w, 0, e->grid.region.height, 0);
    for (uint32_t y = 0; y < mh; y++)
        T(!memcmp(frame + (size_t)(y + e->tab_rows * e->grid.dims.cell_h) * w + w - mw,
            expected + (size_t)y * mw, (size_t)mw * sizeof *expected));
    if (backend_name && !strcmp(backend_name, "gl")) {
        uint8_t *rgba = calloc((size_t)w * h, 4); T(rgba);
        T(gl_read_pixels(&backend, rgba, (size_t)w * h * 4u) == 0);
        for (uint32_t y = 0; y < mh; y++) for (uint32_t x = 0; x < mw; x++) {
            size_t at = ((size_t)(y + e->tab_rows * e->grid.dims.cell_h) * w + w - mw + x) * 4u;
            uint32_t color = ((uint32_t)rgba[at] << 16) | ((uint32_t)rgba[at + 1u] << 8) | rgba[at + 2u];
            T(color == expected[(size_t)y * mw + x] && rgba[at + 3u] == UINT8_MAX);
        }
        free(rgba);
    } else if (backend_name) {
        plat *platform = backend.config.platform;
        xcb_get_image_reply_t *reply = xcb_get_image_reply(platform->conn,
            xcb_get_image(platform->conn, XCB_IMAGE_FORMAT_Z_PIXMAP, platform->win,
                (int16_t)(w - mw), (int16_t)(e->tab_rows * e->grid.dims.cell_h),
                (uint16_t)mw, (uint16_t)mh, UINT32_MAX), NULL);
        T(reply && xcb_get_image_data_length(reply) >= (int)((size_t)mw * mh * sizeof(uint32_t)));
        const uint32_t *actual = (const uint32_t *)xcb_get_image_data(reply);
        for (size_t at = 0; at < (size_t)mw * mh; at++) T((actual[at] & UINT32_C(0xffffff)) == expected[at]);
        free(reply);
    }
    T(e->grid.region.pixels && e->buffer->map.height == summaries);
    /* Map-only colour/viewport changes must leave the cell damage empty. */
    e->dirty = false; T(editor_begin_frame(e) == 0);
    e->buffer->map.rows[0].marks[0] ^= UINT64_C(1); /* toggle the first fixture character */
    edit_malloc_guard_begin(); T(editor_compose(e) == 0);
    size_t allocations = edit_malloc_guard_end(); T(allocations == 0);
    T(e->grid.surface_background == config_theme_color(e->preferences.theme, "editor.background"));
    size_t count = 0; T(render_dirty_strips(&e->grid, e->strips, e->strip_cap, &count) == 0);
    T(count == 0 && e->grid.region.dirty);
    uint64_t submitted_cells = backend.stats.submitted_cells;
    T(settle(e) == 0 && backend.stats.submitted_cells == submitted_cells);
    /* Same pixels are retained on a subsequent unchanged compose. */
    e->dirty = false; T(editor_begin_frame(e) == 0); T(editor_compose(e) == 0);
    T(!e->grid.region.dirty);
    /* Pointer coordinates come from the fixture's second summary scanline;
     * dragging above/below the region retains the existing endpoint clamps. */
    uint32_t summary_row = e->buffer->map.active_rows / 2u;
    /* edit-2mhd (P): this outside press centres the visible summary rows;
     * subtract half their count from the clicked row before resolving. */
    minimap_target target;
    T(minimap_hit(&e->buffer->map, &input, e->buffer->index,
        (int64_t)summary_row - (int64_t)(visible / 2u), &target) == 0);
    plat_event click = {.kind = PLAT_EV_BUTTON, .code = 1, .press = true,
        .x = (int32_t)(w - 1u), .y = (int32_t)(e->tab_rows * e->grid.dims.cell_h + summary_row * 2u + 1u)};
    T(editor_pointer(e, &click) == 0 && e->map_drag);
    T(e->v.state.first_line == target.line && e->v.state.first_byte == target.byte);
    click.kind = PLAT_EV_MOTION; click.y = -1; T(editor_pointer(e, &click) == 0);
    T(e->v.state.first_line == 0);
    click.y = (int32_t)h; T(editor_pointer(e, &click) == 0);
    T(e->v.state.first_line == input.lines - 1u);
    click.kind = PLAT_EV_BUTTON; click.press = false; T(editor_pointer(e, &click) == 0 && !e->map_drag);
    free(frame); free(expected); free(rows); editor_close(e);
    puts("edit-d5o compose: minimap pixel rows match independent minimap_pixels reference"); return 0;
}
static int minimap_compose_cost(void)
{
    /* Stamp buffers follow bench/editor_bench.c's existing power stamp. */
    char power[32] = "unknown", load[24] = "unknown";
    FILE *f = fopen("/sys/class/power_supply/BAT0/status", "r");
    if (f) { if (fgets(power, sizeof power, f)) power[strcspn(power, "\n")] = 0; fclose(f); }
    f = fopen("/proc/loadavg", "r");
    if (f) { if (fgets(load, sizeof load, f)) load[strcspn(load, " \n")] = 0; fclose(f); }
    const char *stamp = !strcmp(power, "Discharging") ? "[bat]" :
        !strcmp(power, "Charging") || !strcmp(power, "Full") || !strcmp(power, "Not charging") ? "[AC]" : "[unknown]";
    printf("edit-d5o STAMP (M)%s BAT0=%s load1=%s shared box\n", stamp, power, load);
    trace_reset(); T(minimap_typing_flash() == 0); /* supplied drive's keystroke fixture */
    const char *path = getenv("EDITOR_COMPOSE_TRACE");
    f = path ? fopen(path, "w+b") : tmpfile(); T(f);
    T(trace_dump(f) == 0); rewind(f);
    trace_loaded dump; T(trace_fmt_load_dump(f, &dump) == 0); fclose(f);
    uint64_t *cost = NULL; size_t count = 0;
    T(trace_fmt_latencies(dump.recs, dump.nrecs, TRACE_COMPOSE_BEGIN, TRACE_COMPOSE_END, &cost, &count) == 0);
    T(count > 0);
    uint64_t p50 = trace_fmt_pct(cost, count, 50), p99 = trace_fmt_pct(cost, count, 99);
    printf("edit-d5o compose (M)%s samples=%zu p50_ns=%llu p99_ns=%llu max_ns=%llu "
        "derivation=COMPOSE_END-COMPOSE_BEGIN; nearest-rank=ceil(samples*pct/100)\n",
        stamp, count, (unsigned long long)p50, (unsigned long long)p99, (unsigned long long)cost[count - 1u]);
    free(cost); trace_fmt_dump_free(&dump); return 0;
}
/* (G) Exact byte offsets derive from the ASCII fixture's literal prefixes.
 * The viewport has two text rows to exercise page movement across that pair;
 * its width derives from the whole fixture, so these lines cannot soft-wrap. */
static int selection_chord(editor *e, uint32_t sym, uint16_t mods,
                           uint64_t cursor, uint64_t anchor)
{
    plat_event shift = key(XKB_KEY_Shift_L, PLAT_MOD_SHIFT, NULL);
    T(press(e, shift) == 0);
    if (mods & PLAT_MOD_CTRL) T(press(e, key(XKB_KEY_Control_L, mods, NULL)) == 0);
    T(press(e, key(sym, mods, NULL)) == 0);
    plat_event release = key(sym, mods, NULL); release.press = false;
    T(press(e, release) == 0);
    if (mods & PLAT_MOD_CTRL) {
        release = key(XKB_KEY_Control_L, PLAT_MOD_SHIFT, NULL); release.press = false;
        T(press(e, release) == 0);
    }
    shift.press = false; shift.mods = 0; T(press(e, shift) == 0);
    view_selection s = editor_view(e).selection;
    if (s.cursor != cursor || s.anchor != anchor)
        fprintf(stderr, "selection chord: got cursor=%llu anchor=%llu expected cursor=%llu anchor=%llu\n",
            (unsigned long long)s.cursor, (unsigned long long)s.anchor,
            (unsigned long long)cursor, (unsigned long long)anchor);
    T(s.cursor == cursor); T(s.anchor == anchor);
    return 0;
}
/* edit-d50: every byte offset is (G), derived from the literal prefix.
 * Grid and undo assertions reuse the selection-chord fixture and one-command
 * contract. Run all cases on red so each action has independent evidence. */
typedef struct default_edit_case {
    const char *name, *before, *cursor_prefix, *anchor_prefix, *after;
    const char *after_cursor_prefix, *after_anchor_prefix;
    uint32_t sym;
    uint16_t mods;
    bool chord;
} default_edit_case;
#define D50_CS (PLAT_MOD_CTRL | PLAT_MOD_SHIFT)
static int default_edit_case_path(const default_edit_case *test, int wrap_mode, const char *path)
{
    render_backend backend = {0}; T(render_null_backend(&backend) == 0);
    counted count = {0};
    editor_config cfg = {.initial = (const uint8_t *)test->before,
        .initial_len = strlen(test->before), .cols = sizeof "alpha beta gamma delta!",
        .rows = 2u + 1u, /* text-row pair plus tab row, as selection_chords */
        .wrap_mode = wrap_mode, .hook_ctx = &count, .on_ingress = ingress, .on_submit = submitted};
    editor *e = NULL; T(editor_open(&e, &cfg, &backend) == 0); T(settle(e) == 0);
    if (path) {
        snprintf(e->buffer->path, sizeof e->buffer->path, "%s", path);
        /* (G) Four spaces are the installed default tab_size, as used by
         * min_indent_lines; pin it rather than depending on auto-detection. */
        e->buffer->style.width = sizeof "    " - 1u;
        e->v.config.tab_width = e->buffer->style.width;
        e->lay.tab = e->lay.cfg.tab_width = e->buffer->style.width;
    }
    e->v.state.selection.cursor = strlen(test->cursor_prefix);
    e->v.state.selection.anchor = strlen(test->anchor_prefix);
    view_selection before = e->v.state.selection;
    undo_history history = undo_get_history(e->undo);
    if (test->chord) T(press(e, key(XKB_KEY_k, PLAT_MOD_CTRL, NULL)) == 0);
    T(press(e, key(test->sym, test->mods, NULL)) == 0);
    T(expect(e, test->after, strlen(test->after_cursor_prefix)) == 0);
    T(editor_view(e).selection.anchor == strlen(test->after_anchor_prefix));
    T(editor_get_stats(e).unimplemented_actions == 0);
    T(undo_get_history(e->undo).undo_groups == history.undo_groups + 1u); /* one command */
    T(press(e, key(XKB_KEY_z, PLAT_MOD_CTRL, NULL)) == 0);
    T(expect(e, test->before, before.cursor) == 0);
    T(editor_view(e).selection.anchor == before.anchor);
    T(press(e, key(XKB_KEY_z, D50_CS, NULL)) == 0);
    T(expect(e, test->after, strlen(test->after_cursor_prefix)) == 0);
    T(editor_view(e).selection.anchor == strlen(test->after_anchor_prefix));
    T(count.allocations == 0); /* law: no input-path malloc */
    editor_close(e); return 0;
}
static int default_edit_case_run(const default_edit_case *test, int wrap_mode)
{ return default_edit_case_path(test, wrap_mode, NULL); }
/* (G) Comment tokens come from the installed C++ Comments.tmPreferences.
 * Prefix lengths derive byte positions; newline endpoints exclude the next
 * line exactly as the existing line-command fixtures do. */
static int default_comments(void)
{
    const default_edit_case cases[] = {
        {"comment caret", "  abc\ndef", "  a", "  a", "  // abc\ndef", "  // a", "  // a", XKB_KEY_slash, PLAT_MOD_CTRL, false},
        {"uncomment caret", "  // abc", "  // a", "  // a", "  abc", "  a", "  a", XKB_KEY_slash, PLAT_MOD_CTRL, false},
        {"uncomment compact", "  //abc", "  //a", "  //a", "  abc", "  a", "  a", XKB_KEY_slash, PLAT_MOD_CTRL, false},
        {"comment backward lines", "  abc\n    def\nghi", "  a", "  abc\n    def\n", "  // abc\n  //   def\nghi", "  // a", "  // abc\n  //   def\n", XKB_KEY_slash, PLAT_MOD_CTRL, false},
        {"comment ignores blank", "  abc\n\n    def", "  abc\n\n    def", "", "  // abc\n\n  //   def", "  // abc\n\n  //   def", "", XKB_KEY_slash, PLAT_MOD_CTRL, false},
        {"comment CRLF", "abc\r\ndef", "a", "a", "// abc\r\ndef", "// a", "// a", XKB_KEY_slash, PLAT_MOD_CTRL, false},
        {"uncomment mixed lines", "// abc\n  //def", "// abc\n  //def", "", "abc\n  def", "abc\n  def", "", XKB_KEY_slash, PLAT_MOD_CTRL, false},
        {"comment mixed lines", "// abc\ndef", "// abc\ndef", "", "// // abc\n// def", "// // abc\n// def", "", XKB_KEY_slash, PLAT_MOD_CTRL, false},
        {"block selection", "abc def", "abc", "", "/*abc*/ def", "/*abc*/", "", XKB_KEY_slash, D50_CS, false},
        {"block backward selection", "abc def", "", "abc", "/*abc*/ def", "", "/*abc*/", XKB_KEY_slash, D50_CS, false},
        {"block caret", "abc", "a", "a", "a/**/bc", "a/*", "a/*", XKB_KEY_slash, D50_CS, false},
        {"unblock selection", "/*abc*/ def", "/*abc*/", "", "abc def", "abc", "", XKB_KEY_slash, D50_CS, false},
        {"unblock inner caret", "/*abc*/ def", "/*a", "/*a", "abc def", "a", "a", XKB_KEY_slash, PLAT_MOD_CTRL, false},
        {"block shortcut removes line comment", "// abc", "// a", "// a", "abc", "a", "a", XKB_KEY_slash, D50_CS, false},
        {"comment mixed tabs aligns columns", "\tabc\n  def", "\tabc\n  def", "", "// \tabc\n  // def", "// \tabc\n  // def", "", XKB_KEY_slash, PLAT_MOD_CTRL, false}
    };
    int failures = 0;
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        int rc = default_edit_case_path(&cases[i], -1, "fixture.c");
        if (!rc) rc = default_edit_case_path(&cases[i], 1, "fixture.c");
        printf("default comment: %s: %s\n", cases[i].name, rc ? "FAIL" : "PASS");
        failures |= rc;
    }
    const default_edit_case language[] = {
        {"Python line", "  abc", "  a", "  a", "  # abc", "  # a", "  # a", XKB_KEY_slash, PLAT_MOD_CTRL, false},
        {"Python block fallback", "  abc", "  a", "  a", "  # abc", "  # a", "  # a", XKB_KEY_slash, D50_CS, false},
        {"CSS line fallback", "  abc", "  a", "  a", "  /*abc*/", "  /*a", "  /*a", XKB_KEY_slash, PLAT_MOD_CTRL, false},
        {"HTML line fallback", "  abc", "  a", "  a", "  <!-- abc -->", "  <!-- a", "  <!-- a", XKB_KEY_slash, PLAT_MOD_CTRL, false},
        {"HTML compact removal", "<!--abc-->", "<!--a", "<!--a", "abc", "a", "a", XKB_KEY_slash, PLAT_MOD_CTRL, false},
        {"HTML shared space removal", "<!-- -->", "<!-- ", "<!-- ", " ", " ", " ", XKB_KEY_slash, PLAT_MOD_CTRL, false},
        {"C quoted delimiter", "char *s = \"/*abc*/\";", "char *s = \"/*a", "char *s = \"/*a", "// char *s = \"/*abc*/\";", "// char *s = \"/*a", "// char *s = \"/*a", XKB_KEY_slash, PLAT_MOD_CTRL, false},
        {"C all blank lines", "  \n\n", "  \n\n", "", "//   \n// \n", "//   \n// \n", "", XKB_KEY_slash, PLAT_MOD_CTRL, false}
    };
    const char *paths[] = {"fixture.py", "fixture.py", "fixture.css", "fixture.html", "fixture.html", "fixture.html", "fixture.c", "fixture.c"};
    for (size_t i = 0; i < sizeof language / sizeof language[0]; i++) {
        int rc = default_edit_case_path(&language[i], -1, paths[i]);
        if (!rc) rc = default_edit_case_path(&language[i], 1, paths[i]);
        printf("default comment: %s: %s\n", language[i].name, rc ? "FAIL" : "PASS");
        failures |= rc;
    }
    return failures;
}

/* edit-d50 fix: an undo that removes text at or above an unwrapped
 * viewport's top must never lay out the pre-edit top byte. Every submitted
 * frame's top is checked against the live tree: within the text, a line start,
 * and labelled with that line. (G) Forty "lineNN\n" lines of seven bytes, the
 * "// " C token from default_comments, five text rows plus the tab row. */
typedef struct top_probe { counted count; editor *e; size_t frames, bad; } top_probe;
static void top_ingress(void *ctx, uint64_t seq, uint64_t ns) { ingress(&((top_probe *)ctx)->count, seq, ns); }
static void top_submit(void *ctx, const editor_frame *frame)
{
    top_probe *p = ctx; submitted(&p->count, frame);
    if (!p->e || p->e->v.state.wrap) return;
    editor *e = p->e; uint64_t top = e->lay.first_byte, total = piece_len(e->tree);
    uint8_t before = '\n';
    bool ok = top <= total && (!top || (piece_read(e->tree, top - 1u, &before, 1) == 0 && before == '\n')) &&
        (top > total || e->lay.first_line == piece_byte_to_line(e->tree, top));
    if (!ok) fprintf(stderr, "undo_shrink_top: frame top byte %llu line %llu, total %llu\n",
                     (unsigned long long)top, (unsigned long long)e->lay.first_line, (unsigned long long)total);
    p->frames++; p->bad += !ok;
}
static int undo_shrink_case(int variant)
{
    enum { LINES = 40, LINE = sizeof "lineNN\n" - 1u };
    char text[LINES * LINE + 1u];
    for (unsigned i = 0; i < LINES; i++) snprintf(text + i * LINE, LINE + 1u, "line%02u\n", i);
    render_backend backend = {0}; T(render_null_backend(&backend) == 0);
    top_probe probe = {0};
    editor_config cfg = {.initial = (const uint8_t *)text, .initial_len = LINES * LINE,
        .cols = sizeof "alpha beta gamma delta!", .rows = 5u + 1u, .wrap_mode = -1,
        .hook_ctx = &probe, .on_ingress = top_ingress, .on_submit = top_submit};
    editor *e = NULL; T(editor_open(&e, &cfg, &backend) == 0); T(settle(e) == 0);
    snprintf(e->buffer->path, sizeof e->buffer->path, "%s", "fixture.c");
    probe.e = e;
    if (variant == 2) {
        /* Comment only the first ten lines; the top is then scrolled to EOF. */
        e->v.state.selection.anchor = 0; e->v.state.selection.cursor = 10u * LINE;
    } else T(press(e, key(XKB_KEY_a, PLAT_MOD_CTRL, NULL)) == 0);
    T(press(e, key(XKB_KEY_slash, PLAT_MOD_CTRL, NULL)) == 0);
    T(editor_length(e) > LINES * LINE);
    if (variant) {
        /* (G) Wheel notches of SCROLL_CORE_ROWS rows: up fifteen rows from
         * the EOF page (top line 36 to 21, inside the commented text), or
         * down LINES notches so the top clips to the last page. */
        plat_event wheel = {.kind = PLAT_EV_WHEEL,
            .dy = variant == 1 ? -(int32_t)(SCROLL_WHEEL_UNIT * (15u / SCROLL_CORE_ROWS)) : (int32_t)(SCROLL_WHEEL_UNIT * LINES)};
        T(press(e, wheel) == 0);
    }
    uint64_t top = editor_view(e).first_byte;
    T(top > 0);
    size_t bad = probe.bad, frames = probe.frames;
    T(press(e, key(XKB_KEY_z, PLAT_MOD_CTRL, NULL)) == 0);
    uint8_t now[LINES * LINE];
    T(editor_length(e) == LINES * LINE && editor_read(e, 0, now, LINES * LINE) == 0);
    T(memcmp(now, text, LINES * LINE) == 0);
    T(editor_view(e).selection.cursor == (variant == 2 ? 10u * LINE : LINES * LINE));
    T(probe.frames > frames && probe.bad == bad);
    view_state v = editor_view(e);
    T(v.first_byte <= editor_length(e) && v.first_line == piece_byte_to_line(e->tree, v.first_byte));
    T(probe.count.allocations == 0); /* law: no input-path malloc */
    editor_close(e); return 0;
}
static int undo_shrink_top(void)
{
    static const char *names[] = {"follow at EOF", "scrolled into the edit", "edit above a scrolled top"};
    int failures = 0;
    for (int i = 0; i < 3; i++) {
        int rc = undo_shrink_case(i);
        printf("undo shrink top: %s: %s\n", names[i], rc ? "FAIL" : "PASS");
        failures |= rc;
    }
    return failures;
}
/* edit-d50: go-to-line drives the unwrapped 3kk scroll state. (G) 400
 * "lineNNN\n" rows of eight bytes, five text rows plus the tab row; the
 * target "300" is 0-based line 299; ten wheel notches of SCROLL_CORE_ROWS
 * put the pre-prompt top at line 30, away from both the caret and line 0. */
static int goto_scroll_case(bool accept)
{
    enum { LINES = 400, LINE = sizeof "lineNNN\n" - 1u, TARGET = 299 };
    static char text[LINES * LINE + 1u];
    for (unsigned i = 0; i < LINES; i++) snprintf(text + i * LINE, LINE + 1u, "line%03u\n", i);
    render_backend backend = {0}; T(render_null_backend(&backend) == 0);
    top_probe probe = {0};
    editor_config cfg = {.initial = (const uint8_t *)text, .initial_len = LINES * LINE,
        .cols = sizeof "alpha beta gamma delta!", .rows = 5u + 1u, .wrap_mode = -1,
        .hook_ctx = &probe, .on_ingress = top_ingress, .on_submit = top_submit};
    editor *e = NULL; T(editor_open(&e, &cfg, &backend) == 0); T(settle(e) == 0);
    probe.e = e;
    plat_event wheel = {.kind = PLAT_EV_WHEEL, .dy = (int32_t)(SCROLL_WHEEL_UNIT * 10u)};
    T(press(e, wheel) == 0);
    uint64_t top = e->lay.first_line, top_byte = e->lay.first_byte;
    T(top == 10u * SCROLL_CORE_ROWS && editor_view(e).selection.cursor == 0);
    uint32_t rows = e->v.config.rows;
    T(press(e, key(XKB_KEY_g, PLAT_MOD_CTRL, NULL)) == 0);
    T(press(e, key(XKB_KEY_3, 0, "3")) == 0); T(press(e, key(XKB_KEY_0, 0, "0")) == 0);
    T(press(e, key(XKB_KEY_0, 0, "0")) == 0);
    /* Preview: the submitted frame shows the target line. */
    T(editor_view(e).selection.cursor == TARGET * LINE);
    T(e->lay.first_line <= TARGET && TARGET < e->lay.first_line + rows);
    T(editor_view(e).first_line == e->lay.first_line);
    if (accept) {
        T(press(e, key(XKB_KEY_Return, 0, NULL)) == 0);
        T(editor_view(e).selection.cursor == TARGET * LINE);
        T(e->lay.first_line <= TARGET && TARGET < e->lay.first_line + rows);
        T(press(e, key(XKB_KEY_Right, 0, NULL)) == 0); /* the next motion keeps it there */
        T(e->lay.first_line <= TARGET && TARGET < e->lay.first_line + rows);
    } else {
        T(press(e, key(XKB_KEY_Escape, 0, NULL)) == 0);
        T(editor_view(e).selection.cursor == 0);
        T(e->lay.first_line == top && e->lay.first_byte == top_byte);
        T(editor_view(e).first_line == top);
    }
    T(probe.bad == 0 && probe.count.allocations == 0);
    editor_close(e); return 0;
}
static int goto_scroll(void)
{
    int a = goto_scroll_case(true), c = goto_scroll_case(false);
    printf("default goto: unwrapped preview/accept scrolls to the target: %s\n", a ? "FAIL" : "PASS");
    printf("default goto: Escape restores the pre-prompt top: %s\n", c ? "FAIL" : "PASS");
    return a | c;
}
/* (G) Line numbers count the literal newline-separated lines from one;
 * byte positions are sizeof literal prefixes minus the NUL terminator. */
static int default_goto(void)
{
    static const char text[] = "alpha\r\nbeta\ngamma\n";
    render_backend backend = {0}; T(render_null_backend(&backend) == 0);
    counted count = {0};
    editor_config cfg = {.initial = (const uint8_t *)text, .initial_len = sizeof text - 1u,
        .cols = sizeof "Go to line: :18446744073709551615" + 1u, .rows = 2u + 1u,
        .wrap_mode = -1, .hook_ctx = &count, .on_ingress = ingress, .on_submit = submitted};
    editor *e = NULL; T(editor_open(&e, &cfg, &backend) == 0); T(settle(e) == 0);
    T(editor_set_cursor(e, sizeof "al" - 1u) == 0); T(settle(e) == 0);
    view_state original = editor_view(e); uint64_t revision = e->buffer->revision;
    undo_history history = undo_get_history(e->undo);
    T(press(e, key(XKB_KEY_g, PLAT_MOD_CTRL, NULL)) == 0);
    const char prompt[] = "Go to line: :";
    for (size_t i = 0; i < sizeof prompt - 1u; i++)
        T(e->grid.cells[(size_t)(e->grid.dims.rows - 1u) * e->grid.dims.cols + i].glyph_index == (uint8_t)prompt[i]);
    T(press(e, key(XKB_KEY_2, 0, "2")) == 0);
    T(expect(e, text, sizeof "alpha\r\n" - 1u) == 0); /* preview */
    T(press(e, key(XKB_KEY_Escape, 0, NULL)) == 0);
    T(expect(e, text, original.selection.cursor) == 0);
    T(editor_view(e).selection.anchor == original.selection.anchor);
    T(editor_view(e).first_byte == original.first_byte);
    T(e->buffer->soft_count == 0); /* a cancelled preview is no selection command */
    T(press(e, key(XKB_KEY_g, PLAT_MOD_CTRL, NULL)) == 0);
    T(press(e, key(XKB_KEY_2, 0, "2")) == 0);
    T(press(e, key(XKB_KEY_Return, 0, NULL)) == 0);
    T(expect(e, text, sizeof "alpha\r\n" - 1u) == 0);
    T(press(e, key(XKB_KEY_x, 0, "x")) == 0);
    T(expect(e, "alpha\r\nxbeta\ngamma\n", sizeof "alpha\r\nx" - 1u) == 0);
    T(press(e, key(XKB_KEY_z, PLAT_MOD_CTRL, NULL)) == 0);
    T(expect(e, text, sizeof "alpha\r\n" - 1u) == 0);
    T(press(e, key(XKB_KEY_g, PLAT_MOD_CTRL, NULL)) == 0);
    T(press(e, key(XKB_KEY_9, 0, "9")) == 0);
    T(press(e, key(XKB_KEY_BackSpace, 0, NULL)) == 0);
    T(expect(e, text, sizeof "alpha\r\n" - 1u) == 0); /* empty query restores origin */
    T(press(e, key(XKB_KEY_3, 0, "3")) == 0);
    T(press(e, key(XKB_KEY_Return, 0, NULL)) == 0);
    T(expect(e, text, sizeof "alpha\r\nbeta\n" - 1u) == 0);
    T(press(e, key(XKB_KEY_g, PLAT_MOD_CTRL, NULL)) == 0);
    T(press(e, key(XKB_KEY_9, 0, "99")) == 0);
    T(press(e, key(XKB_KEY_Return, 0, NULL)) == 0);
    T(expect(e, text, sizeof text - 1u) == 0); /* clamp past last line */
    T(press(e, key(XKB_KEY_g, PLAT_MOD_CTRL, NULL)) == 0);
    T(press(e, key(XKB_KEY_0, 0, "0")) == 0);
    T(press(e, key(XKB_KEY_Return, 0, NULL)) == 0);
    T(expect(e, text, 0) == 0);
    T(press(e, key(XKB_KEY_g, PLAT_MOD_CTRL, NULL)) == 0);
    /* (G) UINT64_MAX + one in decimal is outside the parser range.
     * Type one byte per event to honor PLAT_UTF8_MAX. */
    const char overflow[] = "18446744073709551616";
    for (size_t i = 0; i < sizeof overflow - 1u; i++) {
        char digit[] = {overflow[i], 0};
        T(press(e, key((uint32_t)(uint8_t)overflow[i], 0, digit)) == 0);
    }
    T(press(e, key(XKB_KEY_Return, 0, NULL)) == 0); /* overflow stays in prompt */
    T(press(e, key(XKB_KEY_a, PLAT_MOD_CTRL, NULL)) == 0);
    T(press(e, key(XKB_KEY_3, 0, "3")) == 0);
    T(press(e, key(XKB_KEY_Left, 0, NULL)) == 0);
    T(press(e, key(XKB_KEY_Delete, 0, NULL)) == 0);
    T(press(e, key(XKB_KEY_2, 0, "2")) == 0);
    T(press(e, key(XKB_KEY_Return, 0, NULL)) == 0);
    T(expect(e, text, sizeof "alpha\r\n" - 1u) == 0);
    T(press(e, key(XKB_KEY_u, PLAT_MOD_CTRL, NULL)) == 0);
    T(expect(e, text, 0) == 0); /* accept creates one soft selection step */
    T(e->buffer->revision > revision); /* only the explicit x edit and its undo */
    T(undo_get_history(e->undo).undo_groups == history.undo_groups);
    T(editor_get_stats(e).unimplemented_actions == 0); T(count.allocations == 0);
    editor_close(e); puts("default goto: prompt/preview/cancel/accept/edit/overflow/soft undo: PASS"); return 0;
}

/* (G) A chunk pair ensures the query crosses the index's own chunk boundary;
 * line count and target offset derive by repeating the literal row. Replacing
 * the fixture's index before input makes every answer initially inexact. */
static int default_goto_cold(void)
{
    const char row[] = "aa\n";
    static uint8_t text[2u * LINEIDX_CHUNK];
    size_t lines = sizeof text / (sizeof row - 1u), length = lines * (sizeof row - 1u);
    for (size_t i = 0; i < lines; i++) memcpy(text + i * (sizeof row - 1u), row, sizeof row - 1u);
    render_backend backend = {0}; T(render_null_backend(&backend) == 0);
    counted count = {0}; editor_config cfg = {.initial = text, .initial_len = length,
        .cols = sizeof "Go to line: :18446744073709551615" + 1u, .rows = 2u + 1u, .wrap_mode = -1,
        .hook_ctx = &count, .on_ingress = ingress, .on_submit = submitted};
    editor *e = NULL; T(editor_open(&e, &cfg, &backend) == 0); T(settle(e) == 0);
    lineidx_destroy(e->buffer->index); e->buffer->index = lineidx_create(length); T(e->buffer->index);
    T(lineidx_build_reserve(e->buffer->index) == 0); e->buffer->index_dirty = false;
    T(!lineidx_complete(e->buffer->index));
    T(press(e, key(XKB_KEY_g, PLAT_MOD_CTRL, NULL)) == 0);
    char number[sizeof "18446744073709551615"];
    int n = snprintf(number, sizeof number, "%zu", lines); T(n > 0 && (size_t)n < sizeof number);
    for (size_t i = 0; i < (size_t)n; i++) {
        char digit[] = {number[i], 0}; T(press(e, key((uint8_t)number[i], 0, digit)) == 0);
    }
    T(editor_view(e).selection.cursor == (lines - 1u) * (sizeof row - 1u));
    T(press(e, key(XKB_KEY_Return, 0, NULL)) == 0);
    T(press(e, key(XKB_KEY_u, PLAT_MOD_CTRL, NULL)) == 0); T(editor_view(e).selection.cursor == 0);
    T(editor_get_stats(e).unimplemented_actions == 0); T(undo_get_history(e->undo).undo_groups == 0);
    T(editor_length(e) == length); T(count.allocations == 0);
    editor_close(e); puts("default goto: cold index seek across chunk boundary: PASS"); return 0;
}

/* (G) Selected word boundaries are the literal prefixes, including UTF-8
 * byte lengths. Ctrl+D's first expansion does not create text history. */
static int default_word(void)
{
    const char *texts[] = {"alpha beta", "fooBar_baz", "naïve café", "abc"};
    const char *positions[] = {"al", "fooB", "naïve ca", "abc"};
    const char *anchors[] = {"", "", "naïve ", ""};
    const char *cursors[] = {"alpha", "fooBar_baz", "naïve café", "abc"};
    int failures = 0;
    for (size_t i = 0; i < sizeof texts / sizeof texts[0]; i++) {
        render_backend backend = {0}; T(render_null_backend(&backend) == 0);
        counted count = {0}; editor_config cfg = {.initial = (const uint8_t *)texts[i],
            .initial_len = strlen(texts[i]), .cols = sizeof "alpha beta gamma delta!", .rows = 2u + 1u,
            .hook_ctx = &count, .on_ingress = ingress, .on_submit = submitted};
        editor *e = NULL; T(editor_open(&e, &cfg, &backend) == 0); T(settle(e) == 0);
        e->v.state.selection.cursor = e->v.state.selection.anchor = strlen(positions[i]);
        uint64_t revision = e->buffer->revision;
        T(press(e, key(XKB_KEY_d, PLAT_MOD_CTRL, NULL)) == 0);
        int rc = editor_view(e).selection.cursor != strlen(cursors[i]) ||
            editor_view(e).selection.anchor != strlen(anchors[i]);
        printf("default word: %s: %s\n", texts[i], rc ? "FAIL" : "PASS");
        failures |= rc;
        if (!rc) {
            T(editor_get_stats(e).unimplemented_actions == 0);
            T(expect(e, texts[i], strlen(cursors[i])) == 0);
            T(e->buffer->revision == revision); T(undo_get_history(e->undo).undo_groups == 0);
            T(press(e, key(XKB_KEY_d, PLAT_MOD_CTRL, NULL)) == 0);
            T(expect(e, texts[i], strlen(cursors[i])) == 0);
            T(editor_view(e).selection.anchor == strlen(anchors[i]));
            T(editor_get_stats(e).unimplemented_actions == 1u); /* unavailable multicaret expansion */
            T(press(e, key(XKB_KEY_u, PLAT_MOD_CTRL, NULL)) == 0);
            T(expect(e, texts[i], strlen(positions[i])) == 0);
            T(editor_view(e).selection.anchor == strlen(positions[i]));
            T(count.allocations == 0);
        }
        editor_close(e);
    }
    return failures;
}

/* (G) Center row = floor(text rows / two), from the meaning of center.
 * Fixture offsets are literal prefixes. No viewport command edits history. */
static int default_center(void)
{
    const char *texts[] = {"a\nb\nc\nd\ne\nf\ng", "abc def ghi jkl mno pqr stu vwx"};
    const char *positions[] = {"a\nb\nc\nd\ne", "abc def ghi jkl mno pqr st"};
    const char *firsts[] = {"a\nb\nc\n", "abc def ghi jkl "};
    int failures = 0;
    for (size_t i = 0; i < sizeof texts / sizeof texts[0]; i++) {
        render_backend backend = {0}; T(render_null_backend(&backend) == 0);
        counted count = {0}; editor_config cfg = {.initial = (const uint8_t *)texts[i], .initial_len = strlen(texts[i]),
            .cols = (sizeof "  " - 1u) + (sizeof "abc def " - 1u),
            .rows = (sizeof "abc" - 1u) + 1u, /* text row trio + tab row */
            .wrap_mode = i ? 1 : -1, .hook_ctx = &count, .on_ingress = ingress, .on_submit = submitted};
        editor *e = NULL; T(editor_open(&e, &cfg, &backend) == 0); T(settle(e) == 0);
        T(editor_set_cursor(e, strlen(positions[i])) == 0); T(settle(e) == 0);
        view_selection selection = editor_view(e).selection;
        uint64_t revision = e->buffer->revision;
        T(press(e, key(XKB_KEY_k, PLAT_MOD_CTRL, NULL)) == 0);
        T(press(e, key(XKB_KEY_c, PLAT_MOD_CTRL, NULL)) == 0);
        int rc = (i ? editor_view(e).visual_byte : editor_view(e).first_byte) != strlen(firsts[i]);
        printf("default center: %s: %s\n", i ? "wrapped rows" : "hard lines", rc ? "FAIL" : "PASS");
        failures |= rc;
        if (!rc) {
            T(expect(e, texts[i], selection.cursor) == 0); T(editor_view(e).selection.anchor == selection.anchor);
            T(e->buffer->revision == revision); T(undo_get_history(e->undo).undo_groups == 0);
            T(e->buffer->soft_count == 0); T(editor_get_stats(e).unimplemented_actions == 0); T(count.allocations == 0);
        }
        editor_close(e);
    }
    return failures;
}
static int default_skip(void)
{
    const char text[] = "abc xx abc yy abc";
    render_backend backend = {0}; T(render_null_backend(&backend) == 0);
    counted count = {0}; editor_config cfg = {.initial = (const uint8_t *)text, .initial_len = sizeof text - 1u,
        .cols = sizeof "alpha beta gamma delta!", .rows = 2u + 1u,
        .hook_ctx = &count, .on_ingress = ingress, .on_submit = submitted};
    editor *e = NULL; T(editor_open(&e, &cfg, &backend) == 0); T(settle(e) == 0);
    e->v.state.selection = (view_selection){sizeof "abc" - 1u, 0, VIEW_PREFERRED_UNSET};
    uint64_t revision = e->buffer->revision;
    T(press(e, key(XKB_KEY_k, PLAT_MOD_CTRL, NULL)) == 0); T(press(e, key(XKB_KEY_d, PLAT_MOD_CTRL, NULL)) == 0);
    T(expect(e, text, sizeof "abc xx abc" - 1u) == 0);
    T(editor_view(e).selection.anchor == sizeof "abc xx " - 1u);
    T(press(e, key(XKB_KEY_u, PLAT_MOD_CTRL, NULL)) == 0);
    T(expect(e, text, sizeof "abc" - 1u) == 0); T(editor_view(e).selection.anchor == 0);
    e->v.state.selection = (view_selection){sizeof "abc xx abc yy " - 1u, sizeof text - 1u, VIEW_PREFERRED_UNSET};
    T(press(e, key(XKB_KEY_k, PLAT_MOD_CTRL, NULL)) == 0); T(press(e, key(XKB_KEY_d, PLAT_MOD_CTRL, NULL)) == 0);
    T(expect(e, text, 0) == 0); T(editor_view(e).selection.anchor == sizeof "abc" - 1u); /* wrap, preserve direction */
    e->v.state.selection.cursor = e->v.state.selection.anchor = sizeof "abc x" - 1u;
    T(press(e, key(XKB_KEY_k, PLAT_MOD_CTRL, NULL)) == 0); T(press(e, key(XKB_KEY_d, PLAT_MOD_CTRL, NULL)) == 0);
    T(expect(e, text, sizeof "abc xx" - 1u) == 0); T(editor_view(e).selection.anchor == sizeof "abc " - 1u);
    T(e->buffer->revision == revision); T(undo_get_history(e->undo).undo_groups == 0);
    T(editor_get_stats(e).unimplemented_actions == 0); T(count.allocations == 0);
    editor_close(e); puts("default skip: next/wrap/direction/empty selection/soft undo: PASS"); return 0;
}

static int default_edits(void)
{
    const default_edit_case cases[] = {
        {"duplicate line", "abc\ndef", "a", "a", "abc\nabc\ndef", "abc\na", "abc\na", XKB_KEY_d, D50_CS, false},
        {"duplicate backward selection", "abcd", "a", "abc", "abcbcd", "abc", "abcbc", XKB_KEY_d, D50_CS, false},
        {"delete line", "abc\ndef", "a", "a", "def", "", "", XKB_KEY_k, D50_CS, false},
        {"delete selected lines", "abc\ndef\nghi", "abc\ndef\n", "a", "ghi", "", "", XKB_KEY_k, D50_CS, false},
        {"move line up", "abc\ndef\nghi", "abc\nd", "abc\nd", "def\nabc\nghi", "d", "d", XKB_KEY_Up, D50_CS, false},
        {"move line down", "abc\ndef\nghi", "a", "a", "def\nabc\nghi", "def\na", "def\na", XKB_KEY_Down, D50_CS, false},
        {"move final line up", "abc\ndef", "abc\nd", "abc\nd", "def\nabc", "d", "d", XKB_KEY_Up, D50_CS, false},
        {"join lines", "abc\n  def", "a", "a", "abc def", "abc ", "abc ", XKB_KEY_j, D50_CS, false},
        {"join selection whitespace endpoint", "a\n  bc", "a\n ", "", "a bc", "a ", "", XKB_KEY_j, D50_CS, false},
        {"insert line after", "  abc\ndef", "  a", "  a", "  abc\n  \ndef", "  abc\n  ", "  abc\n  ", XKB_KEY_Return, PLAT_MOD_CTRL, false},
        {"insert line before", "  abc\ndef", "  a", "  a", "  \n  abc\ndef", "  ", "  ", XKB_KEY_Return, D50_CS, false},
        {"delete hard BOL", "abc\ndef", "ab", "ab", "c\ndef", "", "", XKB_KEY_BackSpace, D50_CS, false},
        {"delete hard EOL", "abc\ndef", "a", "a", "a\ndef", "a", "a", XKB_KEY_Delete, D50_CS, false},
        {"delete EOL chord", "abc\ndef", "a", "a", "a\ndef", "a", "a", XKB_KEY_k, PLAT_MOD_CTRL, true},
        {"indent selected lines", "abc\ndef\nghi", "abc\ndef\n", "a", "    abc\n    def\nghi", "    abc\n    def\n", "    a", XKB_KEY_bracketright, PLAT_MOD_CTRL, false},
        {"unindent selected lines", "    abc\n\tdef\nghi", "    abc\n\tdef\n", "    a", "abc\ndef\nghi", "abc\ndef\n", "a", XKB_KEY_bracketleft, PLAT_MOD_CTRL, false},
        {"shift tab", "    abc", "    a", "    a", "abc", "a", "a", XKB_KEY_Tab, PLAT_MOD_SHIFT, false},
        {"unindent mixed space/tab", " \tabc", " \ta", " \ta", "abc", "a", "a", XKB_KEY_bracketleft, PLAT_MOD_CTRL, false},
        {"transpose", "abcd", "ab", "ab", "acbd", "acb", "acb", XKB_KEY_t, PLAT_MOD_CTRL, false},
        {"transpose newline", "ab\ncd", "ab\n", "ab\n", "abc\nd", "abc\n", "abc\n", XKB_KEY_t, PLAT_MOD_CTRL, false},
        {"uppercase selection", "aBc", "aBc", "", "ABC", "ABC", "", XKB_KEY_u, PLAT_MOD_CTRL, true},
        {"lowercase word", "ABC def", "A", "A", "abc def", "a", "a", XKB_KEY_l, PLAT_MOD_CTRL, true},
        {"uppercase Unicode word boundary", "abcé，def", "abcé，d", "abcé，d", "abcé，DEF", "abcé，D", "abcé，D", XKB_KEY_u, PLAT_MOD_CTRL, true},
        {"sort insensitive", "b\nA\nc\n", "", "", "A\nb\nc\n", "A\nb\nc", "", XKB_KEY_F9, 0, false},
        {"sort sensitive", "b\na\nC", "", "", "C\na\nb", "C\na\nb", "", XKB_KEY_F9, PLAT_MOD_CTRL, false},
        {"uppercase expansion", "Straße", "Straße", "", "STRASSE", "STRASSE", "", XKB_KEY_u, PLAT_MOD_CTRL, true},
        {"lowercase final sigma", "ΟΣ", "ΟΣ", "", "ος", "ος", "", XKB_KEY_l, PLAT_MOD_CTRL, true},
        {"duplicate CRLF EOF", "abc\r\ndef", "abc\r\nd", "abc\r\nd", "abc\r\ndef\r\ndef", "abc\r\ndef\r\nd", "abc\r\ndef\r\nd", XKB_KEY_d, D50_CS, false},
        {"insert CRLF EOF", "abc\r\ndef", "abc\r\nd", "abc\r\nd", "abc\r\ndef\r\n", "abc\r\ndef\r\n", "abc\r\ndef\r\n", XKB_KEY_Return, PLAT_MOD_CTRL, false}
    };
    int failures = 0;
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        int rc = default_edit_case_run(&cases[i], -1);
        if (!rc) rc = default_edit_case_run(&cases[i], 1); /* existing wrap config encodings */
        printf("default editing: %s: %s\n", cases[i].name, rc ? "FAIL" : "PASS");
        failures |= rc;
    }
    return failures;
}

static int default_state_case(unsigned which)
{
    static const char text[] = "fooBar_baz (abc)\ndef";
    render_backend backend = {0}; T(render_null_backend(&backend) == 0);
    counted count = {0};
    editor_config cfg = {.initial = (const uint8_t *)text, .initial_len = sizeof text - 1u,
        .cols = sizeof text, .rows = 2u + 1u, .wrap_mode = -1,
        .hook_ctx = &count, .on_ingress = ingress, .on_submit = submitted};
    editor *e = NULL; T(editor_open(&e, &cfg, &backend) == 0); T(settle(e) == 0);
    if (which == 0) {
        T(press(e, key(XKB_KEY_Right, PLAT_MOD_ALT, NULL)) == 0);
        T(expect(e, text, sizeof "foo" - 1u) == 0);
        T(editor_view(e).selection.anchor == sizeof "foo" - 1u);
        T(press(e, key(XKB_KEY_Right, PLAT_MOD_ALT | PLAT_MOD_SHIFT, NULL)) == 0);
        T(expect(e, text, sizeof "fooBar" - 1u) == 0);
        T(editor_view(e).selection.anchor == sizeof "foo" - 1u);
        T(press(e, key(XKB_KEY_Left, PLAT_MOD_ALT | PLAT_MOD_SHIFT, NULL)) == 0);
        T(expect(e, text, sizeof "foo" - 1u) == 0);
        T(press(e, key(XKB_KEY_Left, PLAT_MOD_ALT, NULL)) == 0);
        T(expect(e, text, 0) == 0); T(editor_view(e).selection.anchor == 0);
    } else if (which == 1) {
        T(press(e, key(XKB_KEY_Insert, 0, NULL)) == 0);
        T(press(e, key('x', 0, "x")) == 0);
        T(expect(e, "xooBar_baz (abc)\ndef", sizeof "x" - 1u) == 0);
        T(press(e, key(XKB_KEY_z, PLAT_MOD_CTRL, NULL)) == 0);
        T(expect(e, text, 0) == 0);
        T(press(e, key(XKB_KEY_Insert, 0, NULL)) == 0);
        T(press(e, key('x', 0, "x")) == 0);
        T(expect(e, "xfooBar_baz (abc)\ndef", sizeof "x" - 1u) == 0);
    } else if (which == 2) {
        T(press(e, key(XKB_KEY_k, PLAT_MOD_CTRL, NULL)) == 0);
        T(press(e, key(XKB_KEY_space, PLAT_MOD_CTRL, NULL)) == 0);
        T(press(e, key(XKB_KEY_Right, 0, NULL)) == 0);
        T(press(e, key(XKB_KEY_k, PLAT_MOD_CTRL, NULL)) == 0);
        T(press(e, key(XKB_KEY_a, PLAT_MOD_CTRL, NULL)) == 0);
        T(expect(e, text, sizeof "f" - 1u) == 0);
        T(editor_view(e).selection.anchor == 0);
        T(press(e, key(XKB_KEY_k, PLAT_MOD_CTRL, NULL)) == 0);
        T(press(e, key(XKB_KEY_x, PLAT_MOD_CTRL, NULL)) == 0);
        T(expect(e, text, 0) == 0); T(editor_view(e).selection.anchor == 0);
        T(press(e, key(XKB_KEY_k, PLAT_MOD_CTRL, NULL)) == 0);
        T(press(e, key(XKB_KEY_x, PLAT_MOD_CTRL, NULL)) == 0);
        T(expect(e, text, sizeof "f" - 1u) == 0); T(editor_view(e).selection.anchor == 0);
        T(press(e, key(XKB_KEY_k, PLAT_MOD_CTRL, NULL)) == 0);
        T(press(e, key(XKB_KEY_g, PLAT_MOD_CTRL, NULL)) == 0);
        T(press(e, key(XKB_KEY_Right, 0, NULL)) == 0); /* collapse to selection end */
        T(press(e, key(XKB_KEY_Right, 0, NULL)) == 0); /* advance by the next character */
        T(press(e, key(XKB_KEY_k, PLAT_MOD_CTRL, NULL)) == 0);
        T(press(e, key(XKB_KEY_a, PLAT_MOD_CTRL, NULL)) == 0);
        T(expect(e, text, sizeof "fo" - 1u) == 0); T(editor_view(e).selection.anchor == sizeof "fo" - 1u);
    } else if (which == 3) {
        T(press(e, key(XKB_KEY_k, PLAT_MOD_CTRL, NULL)) == 0);
        T(press(e, key(XKB_KEY_space, PLAT_MOD_CTRL, NULL)) == 0);
        T(press(e, key(XKB_KEY_Right, 0, NULL)) == 0);
        T(press(e, key(XKB_KEY_k, PLAT_MOD_CTRL, NULL)) == 0);
        T(press(e, key(XKB_KEY_w, PLAT_MOD_CTRL, NULL)) == 0);
        T(expect(e, "ooBar_baz (abc)\ndef", 0) == 0);
        T(press(e, key(XKB_KEY_k, PLAT_MOD_CTRL, NULL)) == 0);
        T(press(e, key(XKB_KEY_y, PLAT_MOD_CTRL, NULL)) == 0);
        T(expect(e, text, sizeof "f" - 1u) == 0);
        T(press(e, key(XKB_KEY_z, PLAT_MOD_CTRL, NULL)) == 0);
        T(expect(e, "ooBar_baz (abc)\ndef", 0) == 0);
        T(press(e, key(XKB_KEY_z, PLAT_MOD_CTRL, NULL)) == 0);
        T(expect(e, text, sizeof "f" - 1u) == 0);
    } else {
        e->v.state.selection.cursor = e->v.state.selection.anchor = sizeof "fooBar_baz " - 1u;
        if (which == 4) {
            T(press(e, key(XKB_KEY_m, PLAT_MOD_CTRL, NULL)) == 0);
            T(expect(e, text, sizeof "fooBar_baz (abc)" - 1u) == 0);
            T(editor_view(e).selection.anchor == sizeof "fooBar_baz (abc)" - 1u);
        } else {
            T(press(e, key(XKB_KEY_m, D50_CS, NULL)) == 0);
            T(expect(e, text, sizeof "fooBar_baz (abc" - 1u) == 0);
            T(editor_view(e).selection.anchor == sizeof "fooBar_baz (" - 1u);
        }
    }
    T(editor_get_stats(e).unimplemented_actions == 0); T(count.allocations == 0);
    editor_close(e); return 0;
}
static int default_soft_capacity(void)
{
    render_backend backend = {0}; T(render_null_backend(&backend) == 0);
    counted count = {0};
    editor_config cfg = {.initial = (const uint8_t *)"abc", .initial_len = sizeof "abc" - 1u,
        .cols = sizeof "alpha beta gamma delta!", .rows = 2u + 1u, .wrap_mode = -1,
        .history_keys = sizeof "x" - 1u, /* retain the single edit in this fixture */
        .hook_ctx = &count, .on_ingress = ingress, .on_submit = submitted};
    editor *e = NULL; T(editor_open(&e, &cfg, &backend) == 0); T(settle(e) == 0);
    T(press(e, key('x', 0, "x")) == 0);
    T(press(e, key(XKB_KEY_Right, 0, NULL)) == 0);
    T(press(e, key(XKB_KEY_u, PLAT_MOD_CTRL, NULL)) == 0);
    T(expect(e, "xabc", sizeof "x" - 1u) == 0);
    T(press(e, key(XKB_KEY_u, PLAT_MOD_CTRL, NULL)) == 0);
    T(expect(e, "abc", 0) == 0);
    T(press(e, key(XKB_KEY_u, D50_CS, NULL)) == 0);
    T(expect(e, "xabc", sizeof "x" - 1u) == 0);
    T(press(e, key(XKB_KEY_u, D50_CS, NULL)) == 0);
    T(expect(e, "xabc", sizeof "xa" - 1u) == 0);
    T(press(e, key(XKB_KEY_z, PLAT_MOD_CTRL, NULL)) == 0);
    T(expect(e, "abc", 0) == 0);
    T(press(e, key(XKB_KEY_u, D50_CS, NULL)) == 0);
    T(expect(e, "xabc", sizeof "x" - 1u) == 0);
    T(press(e, key(XKB_KEY_u, D50_CS, NULL)) == 0);
    T(expect(e, "xabc", sizeof "xa" - 1u) == 0);
    T(count.allocations == 0); T(editor_get_stats(e).unimplemented_actions == 0);
    editor_close(e); puts("default state: evicted text markers redo before their selection records: PASS"); return 0;
}
static int default_history_anchor(void)
{
    static const uint8_t mark[] = "\xcc\x81";
    uint8_t text[sizeof "a" - 1u + VIEW_SCAN_BOUND + sizeof "x" - 1u];
    text[0] = 'a';
    for (size_t at = sizeof "a" - 1u; at < sizeof text - (sizeof "x" - 1u); at += sizeof mark - 1u)
        memcpy(text + at, mark, sizeof mark - 1u);
    text[sizeof text - 1u] = 'x';
    const uint64_t anchor = sizeof text - (sizeof "x" - 1u); /* base + existing scan-bound combining bytes */
    render_backend backend = {0}; T(render_null_backend(&backend) == 0);
    counted count = {0};
    editor_config cfg = {.initial = text, .initial_len = sizeof text,
        .cols = sizeof "alpha beta gamma delta!", .rows = 2u + 1u, .wrap_mode = -1,
        .hook_ctx = &count, .on_ingress = ingress, .on_submit = submitted};
    editor *e = NULL; T(editor_open(&e, &cfg, &backend) == 0); T(settle(e) == 0);
    e->v.state.selection.cursor = 0; e->v.state.selection.anchor = anchor;
    T(press(e, key(XKB_KEY_BackSpace, 0, NULL)) == 0); T(expect(e, "x", 0) == 0);
    plat_event undo_event = key(XKB_KEY_z, PLAT_MOD_CTRL, NULL);
    T(editor_handle_key(e, &undo_event) == 0);
    while (e->action == EDITOR_ACTION_REPLAY_WORK) T(editor_continue_action(e) == 0);
    T(e->action == EDITOR_ACTION_NONE); /* trusted anchor must not add a normalization continuation */
    T(editor_view(e).selection.cursor == 0 && editor_view(e).selection.anchor == anchor);
    uint8_t actual[sizeof text]; T(editor_length(e) == sizeof text);
    T(editor_read(e, 0, actual, sizeof actual) == 0 && memcmp(actual, text, sizeof text) == 0);
    T(settle(e) == 0); T(count.allocations == 0);
    editor_close(e); puts("default editing: undo preserves a trusted far anchor without a view continuation: PASS"); return 0;
}
static int default_notices(void)
{
    static uint8_t long_line[VIEW_SCAN_BOUND + sizeof "x" - 1u];
    memset(long_line, 'a', sizeof long_line);
    render_backend backend = {0}; T(render_null_backend(&backend) == 0);
    counted count = {0};
    editor_config cfg = {.initial = long_line, .initial_len = sizeof long_line,
        .cols = sizeof "not implemented", .rows = 2u + 1u, .wrap_mode = -1,
        .hook_ctx = &count, .on_ingress = ingress, .on_submit = submitted};
    editor *e = NULL; T(editor_open(&e, &cfg, &backend) == 0); T(settle(e) == 0);
    uint64_t revision = e->buffer->revision;
    T(press(e, key(XKB_KEY_d, D50_CS, NULL)) == 0);
    T(editor_length(e) == sizeof long_line); T(e->buffer->revision == revision);
    T(editor_view(e).selection.cursor == 0 && editor_view(e).selection.anchor == 0);
    T(undo_get_history(e->undo).undo_groups == 0);
    T(editor_get_stats(e).unimplemented_actions == 1u); /* one refused command */
    T(e->unimplemented_frame != 0); /* notice has a frame generation */
    size_t binding_count; const keys_binding *bindings = keys_defaults(&binding_count);
    for (size_t i = 0; i < binding_count; i++) if (bindings[i].args.flags & KEYS_ARG_UNIMPLEMENTED) {
        uint64_t before = editor_get_stats(e).unimplemented_actions;
        e->keys.chord = bindings[i].args.chord;
        T(press(e, key(bindings[i].keysym, bindings[i].modifiers, "q")) == 0);
        T(editor_get_stats(e).unimplemented_actions == before + 1u); /* one consumed binding */
        T(editor_length(e) == sizeof long_line && e->buffer->revision == revision);
        T(editor_view(e).selection.cursor == 0 && editor_view(e).selection.anchor == 0);
        T(undo_get_history(e->undo).undo_groups == 0);
    }
    T(press(e, key('x', 0, "x")) == 0);
    T(editor_length(e) == sizeof long_line + sizeof "x" - 1u);
    uint8_t first; T(editor_read(e, 0, &first, sizeof first) == 0 && first == 'x');
    T(editor_view(e).selection.cursor == sizeof "x" - 1u && editor_view(e).selection.anchor == sizeof "x" - 1u);
    T(count.allocations == 0);
    editor_close(e); puts("default editing: oversized command and every retained capability notice preserve text/selection/history: PASS"); return 0;
}
static int default_soft_lines(void)
{
    static const char text[] = "abc def ghi jkl";
    render_backend backend = {0}; T(render_null_backend(&backend) == 0);
    counted count = {0};
    editor_config cfg = {.initial = (const uint8_t *)text, .initial_len = sizeof text - 1u,
        .cols = (sizeof "  " - 1u) + (sizeof "abc def " - 1u),
        /* Gutter digit/space + row including its break space; minimap is disabled at this width. */
        .rows = 2u + 1u, .wrap_mode = 1,
        .hook_ctx = &count, .on_ingress = ingress, .on_submit = submitted};
    editor *e = NULL; T(editor_open(&e, &cfg, &backend) == 0); T(settle(e) == 0);
    T(e->lay.text_cols == sizeof "abc def " - 1u);
    T(e->lay.wrap_rows[0].end == sizeof "abc def " - 1u);
    e->v.state.selection.cursor = e->v.state.selection.anchor = sizeof "ab" - 1u;
    T(press(e, key(XKB_KEY_k, PLAT_MOD_CTRL, NULL)) == 0);
    T(press(e, key(XKB_KEY_k, PLAT_MOD_CTRL, NULL)) == 0);
    T(expect(e, "abghi jkl", sizeof "ab" - 1u) == 0);
    T(press(e, key(XKB_KEY_z, PLAT_MOD_CTRL, NULL)) == 0);
    T(expect(e, text, sizeof "ab" - 1u) == 0);
    T(press(e, key(XKB_KEY_Delete, D50_CS, NULL)) == 0);
    T(expect(e, "ab", sizeof "ab" - 1u) == 0);
    T(press(e, key(XKB_KEY_z, PLAT_MOD_CTRL, NULL)) == 0);
    e->v.state.selection.cursor = e->v.state.selection.anchor = sizeof "abc def gh" - 1u;
    T(press(e, key(XKB_KEY_k, PLAT_MOD_CTRL, NULL)) == 0);
    T(press(e, key(XKB_KEY_BackSpace, PLAT_MOD_CTRL, NULL)) == 0);
    T(expect(e, "abc def i jkl", sizeof "abc def " - 1u) == 0);
    T(press(e, key(XKB_KEY_z, PLAT_MOD_CTRL, NULL)) == 0);
    T(expect(e, text, sizeof "abc def gh" - 1u) == 0);
    T(press(e, key(XKB_KEY_BackSpace, D50_CS, NULL)) == 0);
    T(expect(e, "i jkl", 0) == 0);
    T(count.allocations == 0); T(editor_get_stats(e).unimplemented_actions == 0);
    editor_close(e); puts("default editing: soft-row and hard-line kill macros have distinct boundaries: PASS"); return 0;
}
static int default_chord_repeat(void)
{
    static const char text[] = "abc\ndef";
    render_backend backend = {0}; T(render_null_backend(&backend) == 0);
    counted count = {0};
    editor_config cfg = {.initial = (const uint8_t *)text, .initial_len = sizeof text - 1u,
        .cols = sizeof "alpha beta gamma delta!", .rows = 2u + 1u, .wrap_mode = -1,
        .hook_ctx = &count, .on_ingress = ingress, .on_submit = submitted};
    editor *e = NULL; T(editor_open(&e, &cfg, &backend) == 0); T(settle(e) == 0);
    e->v.state.selection.cursor = sizeof "abc" - 1u; e->v.state.selection.anchor = 0;
    T(press(e, key(XKB_KEY_k, PLAT_MOD_CTRL, NULL)) == 0);
    T(press(e, key(XKB_KEY_u, PLAT_MOD_CTRL, NULL)) == 0);
    T(expect(e, "ABC\ndef", sizeof "ABC" - 1u) == 0);
    T(press(e, key(XKB_KEY_End, PLAT_MOD_CTRL, NULL)) == 0);
    T(press(e, key(XKB_KEY_Left, D50_CS, NULL)) == 0);
    uint64_t sequence = editor_get_stats(e).input_sequence;
    size_t frames = count.frames;
    T(press(e, key(XKB_KEY_y, PLAT_MOD_CTRL, NULL)) == 0);
    T(expect(e, "ABC\nDEF", sizeof "ABC\n" - 1u) == 0);
    T(editor_view(e).selection.anchor == sizeof "ABC\nDEF" - 1u); /* retain backward selection */
    T(editor_get_stats(e).input_sequence == sequence + 1u); /* one physical input */
    T(count.frames == frames + 1u); /* one command's mutation-to-submit contract */
    T(press(e, key(XKB_KEY_z, PLAT_MOD_CTRL, NULL)) == 0);
    T(expect(e, "ABC\ndef", sizeof "ABC\n" - 1u) == 0);
    T(editor_view(e).selection.anchor == sizeof "ABC\ndef" - 1u);
    T(editor_get_stats(e).unimplemented_actions == 0); T(count.allocations == 0);
    editor_close(e); puts("default editing: repeat retains chord arguments and groups undo: PASS"); return 0;
}
static int default_clipboard_line(void)
{
    static const char text[] = "alpha\nbeta";
    render_backend backend = {0}; T(render_null_backend(&backend) == 0);
    counted count = {0};
    editor_config cfg = {.initial = (const uint8_t *)text, .initial_len = sizeof text - 1u,
        .cols = sizeof "alpha beta gamma delta!", .rows = 2u + 1u, .wrap_mode = -1,
        .hook_ctx = &count, .on_ingress = ingress, .on_submit = submitted};
    editor *e = NULL; T(editor_open(&e, &cfg, &backend) == 0); T(settle(e) == 0);
    T(press(e, key(XKB_KEY_Right, 0, NULL)) == 0);
    T(press(e, key(XKB_KEY_c, PLAT_MOD_CTRL, NULL)) == 0);
    T(expect(e, text, sizeof "a" - 1u) == 0); T(editor_view(e).selection.anchor == sizeof "a" - 1u);
    T(press(e, key(XKB_KEY_End, PLAT_MOD_CTRL, NULL)) == 0);
    T(press(e, key(XKB_KEY_v, PLAT_MOD_CTRL, NULL)) == 0);
    T(expect(e, "alpha\nbetaalpha\n", sizeof "alpha\nbetaalpha\n" - 1u) == 0);
    T(press(e, key(XKB_KEY_z, PLAT_MOD_CTRL, NULL)) == 0);
    T(expect(e, text, sizeof text - 1u) == 0);
    T(press(e, key(XKB_KEY_Home, PLAT_MOD_CTRL, NULL)) == 0);
    T(press(e, key(XKB_KEY_Right, 0, NULL)) == 0);
    T(press(e, key(XKB_KEY_x, PLAT_MOD_CTRL, NULL)) == 0);
    T(expect(e, "beta", 0) == 0); T(editor_view(e).selection.anchor == 0);
    T(press(e, key(XKB_KEY_z, PLAT_MOD_CTRL, NULL)) == 0);
    T(expect(e, text, sizeof "a" - 1u) == 0); T(editor_view(e).selection.anchor == sizeof "a" - 1u);
    T(editor_get_stats(e).unimplemented_actions == 0); T(count.allocations == 0);
    editor_close(e); puts("default editing: copy/cut current line retains caret and undo selection: PASS"); return 0;
}
static int default_soft_history(void)
{
    render_backend backend = {0}; T(render_null_backend(&backend) == 0);
    counted count = {0};
    editor_config cfg = {.initial = (const uint8_t *)"abc", .initial_len = sizeof "abc" - 1u,
        .cols = sizeof "alpha beta gamma delta!", .rows = 2u + 1u, .wrap_mode = -1,
        .hook_ctx = &count, .on_ingress = ingress, .on_submit = submitted};
    editor *e = NULL; T(editor_open(&e, &cfg, &backend) == 0); T(settle(e) == 0);
    T(press(e, key(XKB_KEY_Right, PLAT_MOD_SHIFT, NULL)) == 0);
    T(press(e, key(XKB_KEY_u, PLAT_MOD_CTRL, NULL)) == 0);
    T(expect(e, "abc", 0) == 0); T(editor_view(e).selection.anchor == 0);
    T(press(e, key(XKB_KEY_u, D50_CS, NULL)) == 0);
    T(expect(e, "abc", sizeof "a" - 1u) == 0); T(editor_view(e).selection.anchor == 0);
    T(press(e, key('x', 0, "x")) == 0);
    T(expect(e, "xbc", sizeof "x" - 1u) == 0);
    T(press(e, key(XKB_KEY_Right, 0, NULL)) == 0);
    T(press(e, key(XKB_KEY_u, PLAT_MOD_CTRL, NULL)) == 0);
    T(expect(e, "xbc", sizeof "x" - 1u) == 0);
    T(press(e, key(XKB_KEY_u, PLAT_MOD_CTRL, NULL)) == 0);
    T(expect(e, "abc", sizeof "a" - 1u) == 0); T(editor_view(e).selection.anchor == 0);
    T(press(e, key(XKB_KEY_u, PLAT_MOD_CTRL, NULL)) == 0);
    T(expect(e, "abc", 0) == 0); T(editor_view(e).selection.anchor == 0);
    T(press(e, key(XKB_KEY_u, D50_CS, NULL)) == 0);
    T(expect(e, "abc", sizeof "a" - 1u) == 0); T(editor_view(e).selection.anchor == 0);
    T(press(e, key(XKB_KEY_u, D50_CS, NULL)) == 0);
    T(expect(e, "xbc", sizeof "x" - 1u) == 0);
    T(press(e, key(XKB_KEY_u, D50_CS, NULL)) == 0);
    T(expect(e, "xbc", sizeof "xb" - 1u) == 0);
    T(editor_get_stats(e).unimplemented_actions == 0); T(count.allocations == 0);
    editor_close(e); puts("default state: soft undo/redo interleaves selection and text: PASS"); return 0;
}
static int default_noop(void)
{
    render_backend backend = {0}; T(render_null_backend(&backend) == 0);
    editor_config cfg = {.initial = (const uint8_t *)"abc", .initial_len = sizeof "abc" - 1u,
        .cols = sizeof "alpha beta gamma delta!", .rows = 2u + 1u, .wrap_mode = -1};
    editor *e = NULL; T(editor_open(&e, &cfg, &backend) == 0); T(settle(e) == 0);
    uint64_t revision = e->buffer->revision;
    T(press(e, key(XKB_KEY_bracketleft, PLAT_MOD_CTRL, NULL)) == 0);
    T(expect(e, "abc", 0) == 0); T(editor_view(e).selection.anchor == 0);
    T(e->buffer->revision == revision);
    T(undo_get_history(e->undo).undo_groups == 0);
    editor_close(e); puts("default editing: unchanged unindent retains revision/history: PASS"); return 0;
}
static int default_states(void)
{
    const char *names[] = {"subword selection", "overwrite", "set/select/swap/clear mark", "delete mark/yank", "goto bracket", "select brackets"};
    int failures = 0;
    for (size_t i = 0; i < sizeof names / sizeof names[0]; i++) {
        int rc = default_state_case((unsigned)i);
        printf("default state: %s: %s\n", names[i], rc ? "FAIL" : "PASS"); failures |= rc;
    }
    return failures;
}
#undef D50_CS
static int selection_chords(void)
{
    static const char text[] = "alpha beta gamma delta!\n"
        "epsilon zeta eta theta!!\n" "epsilon zeta eta theta!!\n"
        "epsilon zeta eta theta!!\n" "epsilon zeta eta theta!!";
    const uint64_t start = 0; /* (G) File start is the empty prefix's length. */
    const uint64_t one = sizeof "a" - 1u, word = sizeof "alpha" - 1u;
    const uint64_t next = sizeof "alpha beta gamma delta!\n" - 1u;
    const uint64_t row = sizeof "epsilon zeta eta theta!!\n" - 1u;
    const uint64_t end = sizeof text - 1u;
    const uint16_t shift = PLAT_MOD_SHIFT, ctrl_shift = PLAT_MOD_CTRL | PLAT_MOD_SHIFT;
    const int modes[] = {-1, 1}; /* Existing config encoding: unwrapped, wrapped. */
    for (size_t mode = 0; mode < sizeof modes / sizeof modes[0]; mode++) {
        render_backend b = {0}; T(render_null_backend(&b) == 0);
        counted count = {0};
        editor_config cfg = {.initial = (const uint8_t *)text, .initial_len = sizeof text - 1u,
            .cols = sizeof text, .rows = 2u + 1u, /* text-row pair + tab row */ .wrap_mode = modes[mode],
            .hook_ctx = &count, .on_ingress = ingress, .on_submit = submitted};
        editor *e = NULL; T(editor_open(&e, &cfg, &b) == 0); T(settle(e) == 0);
        T(selection_chord(e, XKB_KEY_Right, shift, one, start) == 0);
        T(selection_chord(e, XKB_KEY_Right, ctrl_shift, word, start) == 0);
        T(selection_chord(e, XKB_KEY_Down, shift, next + word, start) == 0);
        T(selection_chord(e, XKB_KEY_Up, shift, word, start) == 0);
        T(selection_chord(e, XKB_KEY_End, shift, next - one, start) == 0);
        T(selection_chord(e, XKB_KEY_Home, shift, start, start) == 0);
        T(selection_chord(e, XKB_KEY_Right, shift, one, start) == 0);
        T(selection_chord(e, XKB_KEY_Next, shift, next + row + one, start) == 0);
        T(selection_chord(e, XKB_KEY_Prior, shift, one, start) == 0);
        T(selection_chord(e, XKB_KEY_End, ctrl_shift, end, start) == 0);
        T(selection_chord(e, XKB_KEY_Home, ctrl_shift, start, start) == 0);
        /* A plain move fixes a new anchor; reversal crosses that anchor. */
        T(press(e, key(XKB_KEY_Right, PLAT_MOD_CTRL, NULL)) == 0);
        T(selection_chord(e, XKB_KEY_Left, ctrl_shift, start, word) == 0);
        T(selection_chord(e, XKB_KEY_Down, shift, next, word) == 0);
        T(selection_chord(e, XKB_KEY_Up, shift, start, word) == 0);
        T(expect(e, text, start) == 0);
        T(editor_get_stats(e).mutations == 0); /* (G) Movement does not edit bytes. */
        /* Mutation replaces the selection and fixes its new insertion caret. */
        T(press(e, key('X', 0, "X")) == 0);
        T(editor_view(e).selection.cursor == one && editor_view(e).selection.anchor == one);
        T(selection_chord(e, XKB_KEY_Right, shift, one + one, one) == 0);
        T(press(e, key(XKB_KEY_Right, 0, NULL)) == 0);
        T(editor_view(e).selection.cursor == one + one && editor_view(e).selection.anchor == one + one);
        T(selection_chord(e, XKB_KEY_Left, shift, one, one + one) == 0);
        T(!count.active && count.allocations == 0); /* (G) Law 2: no typing-path allocation. */
        editor_close(e);
    }
    puts("editor_test: separate Shift chords preserve exact ranges, reset after movement/mutation, and allocate nothing");
    return 0;
}
/* (G) Dimensions derive from the longest status/fixture literal plus the tab
 * row and a pair of text rows. Offsets are ASCII prefix lengths; zero edits
 * and allocations are required for consumed shortcuts (CLAUDE.md Law 2). */
static int redo_or_repeat(void)
{
    static const char text[] = "a\nbeta";
    render_backend b = {0}; T(render_null_backend(&b) == 0);
    counted count = {0};
    editor_config cfg = {.cols = sizeof "not implemented", .rows = 1u + 2u,
        .hook_ctx = &count, .on_ingress = ingress, .on_submit = submitted};
    editor *e = NULL; T(editor_open(&e, &cfg, &b) == 0); T(settle(e) == 0);
    T(press(e, key('a', 0, text)) == 0); T(expect(e, text, sizeof text - 1u) == 0);
    T(press(e, key('z', PLAT_MOD_CTRL, NULL)) == 0); T(expect(e, "", 0) == 0);
    uint64_t unimplemented = editor_get_stats(e).unimplemented_actions;
    T(press(e, key('y', PLAT_MOD_CTRL, NULL)) == 0);
    T(expect(e, text, sizeof text - 1u) == 0);
    T(editor_get_stats(e).unimplemented_actions == unimplemented);
    uint64_t mutations = editor_get_stats(e).mutations;
    T(press(e, key('y', PLAT_MOD_CTRL, NULL)) == 0);
    T(expect(e, "a\nbetaa\nbeta", 2u * (sizeof text - 1u)) == 0); /* repeated input */
    T(editor_get_stats(e).mutations > mutations);
    T(press(e, key('z', PLAT_MOD_CTRL, NULL)) == 0);
    T(expect(e, text, sizeof text - 1u) == 0);
    T(press(e, key('Z', PLAT_MOD_CTRL | PLAT_MOD_SHIFT, NULL)) == 0);
    T(expect(e, "a\nbetaa\nbeta", 2u * (sizeof text - 1u)) == 0);
    T(press(e, key('z', PLAT_MOD_CTRL, NULL)) == 0);
    T(expect(e, text, sizeof text - 1u) == 0);
    /* Astra repro: line selection, deletion, undo, Ctrl+Y. */
    T(press(e, key('l', PLAT_MOD_CTRL, NULL)) == 0);
    T(press(e, key(XKB_KEY_BackSpace, 0, NULL)) == 0);
    T(expect(e, "a\n", sizeof "a\n" - 1u) == 0);
    T(press(e, key('z', PLAT_MOD_CTRL, NULL)) == 0);
    T(expect(e, text, sizeof text - 1u) == 0);
    T(press(e, key('y', PLAT_MOD_CTRL, NULL)) == 0);
    T(expect(e, "a\n", sizeof "a\n" - 1u) == 0);
    T(!count.active && count.allocations == 0);
    editor_close(e); puts("editor_test: Ctrl+Y redoes inserts/deletes, empty redo repeats the previous edit, Ctrl+Shift+Z still redoes");
    return 0;
}
static int panel_shortcuts(bool notice)
{
    static const char initial[] = "left right\nnext";
    static const char typed[] = "leftalpha right\nnext";
    static const char status[] = "not implemented";
    const struct { uint32_t sym; uint16_t mods; } shortcuts[] = {
        {'p', PLAT_MOD_CTRL}
    }; /* Each entry derives from the brief's requested shortcut set, less
        * Ctrl+F, Ctrl+H and F3, which open/drive the find panel (edit-73j),
        * Ctrl+G, which has its own executable prompt suite (edit-d50), and
        * Ctrl+Shift+F, find in files since edit-n95 (n95_find_in_files). */
    for (size_t i = 0; i < sizeof shortcuts / sizeof shortcuts[0]; i++) {
        render_backend b = {0}; T(render_null_backend(&b) == 0);
        counted count = {0};
        editor_config cfg = {.initial = (const uint8_t *)initial, .initial_len = sizeof initial - 1u,
            .cols = sizeof typed, .rows = 1u + 2u, .wrap_mode = -1,
            .hook_ctx = &count, .on_ingress = ingress, .on_submit = submitted};
        editor *e = NULL; T(editor_open(&e, &cfg, &b) == 0); T(settle(e) == 0);
        T(press(e, key(XKB_KEY_Right, PLAT_MOD_CTRL, NULL)) == 0);
        size_t bottom = (size_t)(e->grid.dims.rows - 1u) * e->grid.dims.cols;
        render_cell bottom_before[sizeof typed]; /* Configured width derives from this literal. */
        memcpy(bottom_before, e->grid.cells + bottom, sizeof bottom_before);
        view_selection before = editor_view(e).selection;
        T(before.cursor == sizeof "left" - 1u && before.anchor == before.cursor);
        uint64_t mutations = editor_get_stats(e).mutations;
        uint64_t unimplemented = editor_get_stats(e).unimplemented_actions;
        uint16_t mods = shortcuts[i].mods;
        if (mods & PLAT_MOD_CTRL) T(press(e, key(XKB_KEY_Control_L, PLAT_MOD_CTRL, NULL)) == 0);
        if (mods & PLAT_MOD_SHIFT) T(press(e, key(XKB_KEY_Shift_L, mods, NULL)) == 0);
        /* Unsupported actions have no ingress hook; guard their status submit
         * explicitly, and end it ourselves if the baseline submits nothing. */
        edit_malloc_guard_begin(); count.active = true;
        T(press(e, key(shortcuts[i].sym, mods, NULL)) == 0);
        if (count.active) { count.allocations += edit_malloc_guard_end(); count.active = false; }
        T(count.allocations == 0);
        plat_event release = key(shortcuts[i].sym, mods, NULL); release.press = false;
        T(press(e, release) == 0);
        if (mods & PLAT_MOD_SHIFT) {
            release = key(XKB_KEY_Shift_L, PLAT_MOD_CTRL, NULL); release.press = false;
            T(press(e, release) == 0);
        }
        if (mods & PLAT_MOD_CTRL) {
            release = key(XKB_KEY_Control_L, 0, NULL); release.press = false;
            T(press(e, release) == 0);
        }
        view_selection after = editor_view(e).selection;
        T(after.cursor == before.cursor && after.anchor == before.anchor);
        T(after.preferred_col == before.preferred_col);
        T(expect(e, initial, before.cursor) == 0);
        T(editor_get_stats(e).mutations == mutations);
        T(editor_get_stats(e).unimplemented_actions == unimplemented + 1u); /* One consumed chord. */
        T(e->keys.chord == KEYS_CHORD_NONE);
        if (notice) {
            size_t row = (size_t)(e->grid.dims.rows - 1u) * e->grid.dims.cols; /* Bottom status line. */
            for (size_t c = 0; c < sizeof status - 1u; c++) T(e->grid.cells[row + c].glyph_index == (uint8_t)status[c]);
            /* No timer: the same generation retains the notice. */
            T(editor_compose(e) == 0);
            T(e->grid.cells[row].glyph_index == (uint8_t)status[0]);
            /* A caret-only generation must restore the overwritten text row. */
            T(editor_begin_frame(e) == 0); e->caret_only = true;
            T(editor_compose(e) == 0);
            T(memcmp(e->grid.cells + row, bottom_before, sizeof bottom_before) == 0);
            T(settle(e) == 0);
        }
        static const char query[] = "alpha";
        for (size_t c = 0; c < sizeof query - 1u; c++) {
            char ch[] = {query[c], '\0'};
            T(press(e, key((uint8_t)query[c], 0, ch)) == 0);
        }
        T(expect(e, typed, sizeof "leftalpha" - 1u) == 0);
        T(editor_view(e).selection.anchor == sizeof "leftalpha" - 1u);
        T(editor_get_stats(e).unimplemented_actions == unimplemented + 1u);
        T(!count.active && count.allocations == 0);
        /* A nonempty selection is also preserved by the unsupported chord. */
        T(press(e, key(XKB_KEY_Left, PLAT_MOD_SHIFT, NULL)) == 0);
        before = editor_view(e).selection;
        T(press(e, key(shortcuts[i].sym, mods, NULL)) == 0);
        after = editor_view(e).selection;
        T(after.cursor == before.cursor && after.anchor == before.anchor);
        T(expect(e, typed, before.cursor) == 0);
        if (notice) {
            /* A source-change prompt can start the next generation before
             * ordinary text composition. It must retire the old notice too. */
            e->buffer->source_stale = true;
            T(editor_begin_frame(e) == 0); T(editor_compose(e) == 0);
            const char source_status[] = "Source changed: R reload / K keep";
            /* (E) Source status now owns the bottom row; literal length
             * excludes the C terminator. The cached first row stays intact. */
            for (size_t c = 0; c < e->grid.dims.cols && c < sizeof source_status - 1u; c++)
                T(e->grid.cells[bottom + c].glyph_index == (uint8_t)source_status[c]);
            T(!memcmp(e->grid.cells + (size_t)e->tab_rows * e->grid.dims.cols,
                e->text_grid.cells, (size_t)e->text_grid.dims.cols * sizeof(render_cell)));
        }
        editor_close(e);
    }
    puts(notice ? "editor_test: panel shortcuts show a generation-only status and restore the bottom row" :
        "editor_test: panel shortcuts are consumed, preserve selection/prefix state, and subsequent alpha types normally");
    return 0;
}
typedef struct replay_model { uint8_t bytes[256]; size_t len, records; } replay_model;
static uint64_t le64(const uint8_t *p)
{
    uint64_t v = 0; for (unsigned i = 0; i < 8; i++) v |= (uint64_t)p[i] << (8u * i); return v;
}
static int replay(void *ctx, const journal_record *r)
{
    replay_model *m = ctx;
    if (r->type == JOURNAL_INSERT) {
        if (r->size < 8) return 1;
        uint64_t off = le64(r->data); size_t n = r->size - 8;
        if (off > m->len || n > sizeof m->bytes - m->len) return 1;
        size_t pos = (size_t)off; memmove(m->bytes + pos + n, m->bytes + pos, m->len - pos);
        memcpy(m->bytes + pos, r->data + 8, n); m->len += n; m->records++;
    } else if (r->type == JOURNAL_DELETE) {
        if (r->size != 16) return 1;
        uint64_t off = le64(r->data), n = le64(r->data + 8);
        if (off > m->len || n > m->len - off) return 1;
        size_t pos = (size_t)off, count = (size_t)n;
        memmove(m->bytes + pos, m->bytes + pos + count, m->len - pos - count);
        m->len -= count; m->records++;
    }
    return 0;
}
static int script(bool raster)
{
    char path[] = "/tmp/editor-test-XXXXXX"; int fd = mkstemp(path); T(fd >= 0); close(fd);
    render_backend b = {0}; T((raster ? render_cpu_backend(&b) : render_null_backend(&b)) == 0);
    editor_config cfg = {.cols = 30, .rows = 6, .journal_path = path};
    editor *e = NULL; int opened = editor_open(&e, &cfg, &b);
    if (opened) fprintf(stderr, "editor_test: open %s returned %d\n", raster ? "raster" : "null", opened);
    T(opened == 0); T(settle(e) == 0);
    T(press(e, key('a', 0, "ab")) == 0); T(expect(e, "ab", 2) == 0);
    T(press(e, key(XKB_KEY_Return, 0, NULL)) == 0); T(expect(e, "ab\n", 3) == 0);
    T(press(e, key('c', 0, "cd")) == 0); T(expect(e, "ab\ncd", 5) == 0);
    T(press(e, key(XKB_KEY_Left, 0, NULL)) == 0);
    T(press(e, key(XKB_KEY_Left, 0, NULL)) == 0); T(expect(e, "ab\ncd", 3) == 0);
    T(press(e, key(XKB_KEY_BackSpace, 0, NULL)) == 0); T(expect(e, "abcd", 2) == 0);
    T(press(e, key('z', PLAT_MOD_CTRL, NULL)) == 0); T(expect(e, "ab\ncd", 3) == 0);
    T(press(e, key('Z', PLAT_MOD_CTRL | PLAT_MOD_SHIFT, NULL)) == 0); T(expect(e, "abcd", 2) == 0);
    T(press(e, key(XKB_KEY_Delete, 0, NULL)) == 0); T(expect(e, "abd", 2) == 0);
    T(press(e, key('z', PLAT_MOD_CTRL, NULL)) == 0); T(expect(e, "abcd", 2) == 0);
    editor_stats s = editor_get_stats(e); T(s.mutations == 8); T(s.journal_records == 8); T(s.journal_error == 0);
    T(editor_flush(e) == 0); editor_close(e);
    replay_model m = {0}; journal_replay_result result;
    T(journal_replay_file(path, replay, &m, &result) == JOURNAL_OK);
    T(m.len == 4 && memcmp(m.bytes, "abcd", 4) == 0 && m.records == 8); unlink(path);
    printf("editor_test: %s script and journal byte model passed\n", raster ? "raster" : "null");
    return 0;
}
static int allocation_case(bool raster, bool highlight)
{
    render_backend b = {0}; T((raster ? render_cpu_backend(&b) : render_null_backend(&b)) == 0);
    /* edit-11v: (P) C source keeps the existing insert/backspace oracle;
     * suffix length comes from its spelling. Refresh is ns/second / panel Hz
     * from perf/00-hardware-profile.md (M)[bat], supplied through the API. */
    char source[]="build/editor-alloc-XXXXXX.c";
    const char syntax_source[]="int main(void) { return 0; }\n"; /* P: C syntax fixture */
    int source_fd=mkstemps(source,(int)(sizeof ".c"-1u)); T(source_fd>=0);
    if (highlight) T(write(source_fd,syntax_source,sizeof syntax_source-1u)==(ssize_t)(sizeof syntax_source-1u));
    close(source_fd);
    counted c = {0}; char path[] = "/tmp/editor-alloc-XXXXXX"; int fd = mkstemp(path); T(fd >= 0); close(fd);
    editor_config cfg = {.cols = 40, .rows = 8, .journal_path = path, .path=highlight ? source : NULL,
        .hook_ctx = &c, .on_ingress = ingress, .on_submit = submitted, .on_io = io_boundary};
    editor *e = NULL; T(editor_open(&e, &cfg, &b) == 0); T(settle(e) == 0);
    if (highlight) {
        uint64_t interval=UINT64_C(1000000000)/90u;
        /* Existing null submit timestamps are synthetic. Set a fresh physical
         * stream identity above them, with monotonic fixture timestamps. */
        T(editor_on_refresh(e,0,e->refresh_sequence+1u,e->refresh_ns+interval,false)==0);
        T(editor_on_refresh(e,0,e->refresh_sequence+1u,e->refresh_ns+interval,false)==0);
    }
    for (unsigned i = 0; i < 10000; i += 100) {
        for (unsigned j = 0; j < 100; j++) {
            plat_event ev = j % 2 ? key(XKB_KEY_BackSpace, 0, NULL) : key('x', 0, "x");
            T(editor_inject(e, &ev) == 0);
        }
        T(settle(e) == 0);
    }
    T(!c.active && !c.suspended);
    if (c.allocations) fprintf(stderr, "editor allocation diagnostic: backend=%s allocations=%zu\n", raster ? "raster" : "null", c.allocations);
    T(c.allocations == 0); T(editor_length(e) == (highlight ? sizeof syntax_source-1u : 0u));
    editor_stats s = editor_get_stats(e); T(s.mutations == 10000 && s.journal_records == 10000 && !s.journal_error);
    T(editor_flush(e) == 0);
    if (highlight) T(e->hl.generation && e->hl.language);
    editor_close(e); unlink(path); unlink(source);
    printf("editor_test: %s%s 10000 keys mallocs=%zu guard=%s\n", raster ? "raster" : "null", highlight ? " hl-on" : "", c.allocations,
        edit_malloc_guard_active() ? "active" : "ASan-inert");
    return 0;
}
static int allocation_test(bool raster) { return allocation_case(raster,false); }

/* G11: one blink must leave the strip/fence pool asleep. Force its deadline
 * rather than wait for a noisy wall-clock interval. */
static int raster_blink_damage(void)
{
    for (unsigned mode = 0; mode < 4; mode++) {
        render_backend b = {0}; T(render_cpu_backend(&b) == 0);
        uint8_t wrapped[320]; memset(wrapped, 'a', sizeof wrapped);
        const uint8_t *text = mode == 1 ? (const uint8_t *)"abc \nxyz" :
            mode == 2 ? (const uint8_t *)"a\xe4\xb8\xad" : mode == 3 ? wrapped : NULL;
        size_t len = mode == 1 ? 8u : mode == 2 ? 4u : mode == 3 ? sizeof wrapped : 0u;
        editor_config cfg = {.cols = 160, .rows = 60, .initial = text, .initial_len = len,
            .wrap_mode = mode == 3 ? 1 : -1}; editor *e = NULL;
        T(editor_open(&e, &cfg, &b) == 0); T(settle(e) == 0);
        if (mode) { T(editor_set_cursor(e, mode == 3 ? 200u : 1u) == 0); T(settle(e) == 0); }
        if (mode == 1) T(press(e, key(XKB_KEY_Right, PLAT_MOD_SHIFT, NULL)) == 0);
        T(settle_map(e) == 0); /* edit-73j: no minimap job may straddle the blink */
        uint64_t fills = e->stats.minimap_fills;
        for (unsigned toggle = 0; toggle < 2; toggle++) {
            uint32_t epochs[WORK_MAX_JOBS];
            for (uint32_t slot = 0; slot < WORK_MAX_JOBS; slot++)
                epochs[slot] = atomic_load_explicit(&e->pool.slots[slot].epoch, memory_order_acquire);
            render_cell before[160 * 60]; memcpy(before, e->grid.cells, sizeof before);
            e->next_blink = trace_now_ns();
            T(editor_step(e, 0) >= 0);
            raster_metrics m; T(raster_frame_metrics(&b, &m));
            size_t changed = 0;
            for (size_t i = 0; i < 160u * 60u; i++)
                if (memcmp(&before[i], &e->grid.cells[i], sizeof before[i])) changed++;
            printf("G11 blink: mode=%u jobs=%u cells=%zu minimap_fills=%llu\n", mode, m.jobs, changed,
                (unsigned long long)(e->stats.minimap_fills - fills));
            T(m.jobs == 0);
            T(changed > 0 && changed <= 2 && m.inline_cells == changed && m.completion_jobs == 0);
            for (uint32_t slot = 0; slot < WORK_MAX_JOBS; slot++) {
                const work_slot *job = &e->pool.slots[slot];
                uint32_t epoch = atomic_load_explicit(&job->epoch, memory_order_acquire);
                /* The first caret submit retires the preceding full-frame
                 * leases. Cancellation bumps an epoch and leaves cancel_ns;
                 * a new submission clears cancel_ns and must still fail. */
                T(epoch == epochs[slot] || (toggle == 0 && epoch == epochs[slot] + 1u &&
                    atomic_load_explicit(&job->cancel_ns, memory_order_acquire) != 0 &&
                    work_handle_finished(&e->pool, (work_handle){slot, epochs[slot]}) &&
                    !atomic_load_explicit(&job->busy, memory_order_acquire)));
            }
            T(e->stats.minimap_fills == fills);
            e->caret_only = false; /* Explicit test drain, rather than deferred idle observation. */
            T(settle(e) == 0);
        }
        plat_event focus = {.kind = PLAT_EV_FOCUS, .focused = false};
        T(editor_inject(e, &focus) == 0); T(editor_step(e, 0) >= 0);
        raster_metrics m; T(raster_frame_metrics(&b, &m));
        T(m.jobs == 0 && m.completion_jobs == 0);
        T(!editor_get_stats(e).blinking && !editor_get_stats(e).pending);
        e->caret_only = false; T(settle(e) == 0);
        editor_close(e);
    }
    puts("editor_test: G11 inline caret damage and sleeping worker pool passed");
    return 0;
}
static int idle_policy(void)
{
    render_backend b = {0}; T(render_null_backend(&b) == 0);
    editor_config cfg = {.cols = 16, .rows = 3}; editor *e = NULL;
    T(editor_open(&e, &cfg, &b) == 0); T(settle(e) == 0);
    uint64_t deadline = trace_now_ns() + UINT64_C(12000000000);
    while (editor_get_stats(e).blinking && trace_now_ns() < deadline) {
        int rc = editor_step(e, -1); T(rc == EDITOR_OK || rc == EDITOR_MORE);
    }
    editor_stats s = editor_get_stats(e);
    T(!s.blinking && s.cursor_visible); T(s.blinks <= 19 && s.poll_returns <= 20);
    uint64_t returns = s.poll_returns;
    T(editor_step(e, 100) == EDITOR_OK);
    T(editor_get_stats(e).poll_returns == returns + 1); /* test deadline only */
    plat_event focus = {.kind = PLAT_EV_FOCUS, .focused = false}; T(press(e, focus) == 0);
    s = editor_get_stats(e); T(!s.focused && !s.blinking);
    returns = s.poll_returns; T(editor_step(e, 100) == EDITOR_OK);
    T(editor_get_stats(e).poll_returns == returns + 1);
    focus.focused = true; T(press(e, focus) == 0); T(editor_get_stats(e).blinking);
    editor_close(e); puts("editor_test: idle blink timeout and unfocused timer disarm passed"); return 0;
}
typedef struct delayed { bool ready; } delayed;
static int delayed_init(render_backend *b, const render_config *cfg) { (void)cfg; ((delayed *)b->state)->ready = false; return 0; }
static int delayed_resize(render_backend *b, render_dims d) { (void)b; (void)d; return 0; }
static int delayed_submit(render_backend *b, const render_grid *g, const render_strip *s, size_t n)
{ (void)b; (void)g; (void)s; (void)n; return 0; }
static int delayed_present(render_backend *b, uint32_t id)
{
    if (!((delayed *)b->state)->ready) return RENDER_ERR_BUSY;
    int rc = render_backend_signal(b, RENDER_EVENT_DEVICE_DONE, id, 0);
    return rc ? rc : render_backend_signal(b, RENDER_EVENT_PRESENT_COMPLETE, id, 0);
}
static int delayed_event(render_backend *b, const render_event *ev) { (void)b; (void)ev; return RENDER_ERR_UNSUPPORTED; }
static void delayed_close(render_backend *b) { (void)b; }
static int queue_depth(void)
{
    render_backend b = {.info = {"editor delayed test", sizeof(delayed), 16, RENDER_CAP_HEADLESS},
        .ops = {delayed_init, delayed_resize, delayed_submit, delayed_present, delayed_event, delayed_close}};
    editor_config cfg = {.cols = 16, .rows = 3}; editor *e = NULL; T(editor_open(&e, &cfg, &b) == 0);
    for (unsigned i = 0; i < 100 && !b.active; i++) T(editor_step(e, 0) >= 0);
    T(b.active && b.stats.submitted_frames == 1);
    for (unsigned i = 0; i < 5; i++) { plat_event ev = key('x', 0, "x"); T(editor_inject(e, &ev) == 0); }
    for (unsigned i = 0; i < 100; i++) T(editor_step(e, 0) >= 0);
    T(editor_length(e) == 5 && editor_get_stats(e).mutations == 5);
    T(b.stats.submitted_frames == 1 && b.active && editor_get_stats(e).pending);
    ((delayed *)b.state)->ready = true; T(settle(e) == 0);
    T(b.stats.submitted_frames == 2 && editor_get_stats(e).submitted_sequence == 5);
    editor_close(e); puts("editor_test: queue depth one and coalesced edits passed"); return 0;
}
static int native_key(editor *e, plat *p, const char *name, uint16_t state)
{
    x11_input *in = p->in;
    xkb_keycode_t code = xkb_keymap_key_by_name(in->keymap, name); T(code <= UINT8_MAX);
    xcb_key_press_event_t ev = {.response_type = XCB_KEY_PRESS, .detail = (uint8_t)code,
        .event = p->win, .state = state, .same_screen = 1};
    xcb_connection_t *conn = p->conn;
    (void)xcb_send_event(conn, 0, p->win, XCB_EVENT_MASK_KEY_PRESS, (const char *)&ev);
    ev.response_type = XCB_KEY_RELEASE;
    (void)xcb_send_event(conn, 0, p->win, XCB_EVENT_MASK_KEY_RELEASE, (const char *)&ev);
    (void)xcb_flush(conn);
    uint64_t sequence = editor_get_stats(e).input_sequence;
    uint64_t deadline = trace_now_ns() + UINT64_C(3000000000);
    while (editor_get_stats(e).input_sequence == sequence) { T(editor_step(e, 20) >= 0); T(trace_now_ns() < deadline); }
    return settle(e);
}
static int native_input(void)
{
    render_backend b = {0}; T(render_cpu_backend(&b) == 0);
    editor_config cfg = {.cols = 32, .rows = 8}; editor *e = NULL;
    T(editor_open(&e, &cfg, &b) == 0); T(settle(e) == 0); plat *p = b.config.platform;
    xcb_get_property_reply_t *title = xcb_get_property_reply(p->conn,
        xcb_get_property(p->conn, 0, p->win, XCB_ATOM_WM_NAME, XCB_ATOM_STRING, 0, 64), NULL);
    T(title != NULL && xcb_get_property_value_length(title) == 9);
    T(memcmp(xcb_get_property_value(title), "sublimite", 9) == 0); free(title);
    T(native_key(e, p, "AC01", 0) == 0); T(expect(e, "a", 1) == 0);
    T(native_key(e, p, "RTRN", 0) == 0); T(expect(e, "a\n", 2) == 0);
    T(native_key(e, p, "BKSP", 0) == 0); T(expect(e, "a", 1) == 0);
    T(native_key(e, p, "AB01", XCB_MOD_MASK_CONTROL) == 0); T(expect(e, "a\n", 2) == 0);
    T(native_key(e, p, "AB01", XCB_MOD_MASK_CONTROL | XCB_MOD_MASK_SHIFT) == 0); T(expect(e, "a", 1) == 0);
    xcb_client_message_event_t close_event = {.response_type = XCB_CLIENT_MESSAGE, .format = 32, .window = p->win,
        .type = p->wm_protocols, .data.data32 = {p->wm_delete, 0, 0, 0, 0}};
    (void)xcb_send_event(p->conn, 0, p->win, 0, (const char *)&close_event); (void)xcb_flush(p->conn);
    int rc = EDITOR_OK; uint64_t deadline = trace_now_ns() + UINT64_C(3000000000);
    while (rc != EDITOR_CLOSED && trace_now_ns() < deadline) rc = editor_step(e, 20);
    T(rc == EDITOR_CLOSED); editor_close(e);
    puts("editor_test: native X11 translation, editor loop and WM close passed"); return 0;
}
static int scrolled_undo(void)
{
    render_backend b = {0}; T(render_null_backend(&b) == 0);
    editor_config cfg = {.cols = 16, .rows = 1}; editor *e = NULL;
    T(editor_open(&e, &cfg, &b) == 0); T(settle(e) == 0);
    T(press(e, key('a', 0, "a\n")) == 0); T(expect(e, "a\n", 2) == 0);
    T(editor_view(e).first_byte == 2);
    T(press(e, key('z', PLAT_MOD_CTRL, NULL)) == 0); T(expect(e, "", 0) == 0);
    T(editor_view(e).first_byte == 0);
    T(press(e, key('Z', PLAT_MOD_CTRL | PLAT_MOD_SHIFT, NULL)) == 0); T(expect(e, "a\n", 2) == 0);
    editor_close(e); puts("editor_test: scrolled undo/redo viewport repair passed"); return 0;
}
static int stopped_error(void)
{
    render_backend b = {0}; T(render_null_backend(&b) == 0);
    editor_config cfg = {.cols = 16, .rows = 1, .max_cols = 16, .max_rows = 1, .history_keys = 1};
    editor *e = NULL;
    /* Find the first open-time reserve that fits metadata. Its slack is less
     * than an add chunk; the first key then fails the piece allocator. */
    for (size_t reserve = 65536; reserve <= 2u * 1024u * 1024u; reserve += 65536) {
        cfg.arena_bytes = reserve;
        int rc = editor_open(&e, &cfg, &b);
        if (!rc) break;
        T(rc == EDITOR_ERR_MEMORY || rc == PIECE_ERR_NOMEM);
    }
    T(e != NULL); T(settle(e) == 0);
    plat_event ev = key('x', 0, "x"); T(editor_inject(e, &ev) == 0);
    int rc = editor_step(e, 0);
    T(rc < 0 && editor_get_stats(e).error_cause == PIECE_ERR_NOMEM);
    T(editor_length(e) == 0 && editor_step(e, 0) == rc);
    editor_close(e); puts("editor_test: mutation error is negative and stops the loop passed"); return 0;
}

/* Review regressions run separately so every finding keeps its own red line. */
static void raw_key(plat *p, const char *name)
{
    x11_input *in = p->in;
    xkb_keycode_t code = xkb_keymap_key_by_name(in->keymap, name);
    EDIT_ASSERT(code <= UINT8_MAX);
    xcb_key_press_event_t ev = {.response_type = XCB_KEY_PRESS, .detail = (uint8_t)code,
        .event = p->win, .same_screen = 1};
    (void)xcb_send_event(p->conn, 0, p->win, XCB_EVENT_MASK_KEY_PRESS, (const char *)&ev);
    ev.response_type = XCB_KEY_RELEASE;
    (void)xcb_send_event(p->conn, 0, p->win, XCB_EVENT_MASK_KEY_RELEASE, (const char *)&ev);
}
static void raw_close(plat *p)
{
    xcb_client_message_event_t ev = {.response_type = XCB_CLIENT_MESSAGE, .format = 32,
        .window = p->win, .type = p->wm_protocols, .data.data32 = {p->wm_delete, 0, 0, 0, 0}};
    (void)xcb_send_event(p->conn, 0, p->win, 0, (const char *)&ev); (void)xcb_flush(p->conn);
}
static void raw_barrier(plat *p)
{
    xcb_get_input_focus_reply_t *r = xcb_get_input_focus_reply(p->conn, xcb_get_input_focus(p->conn), NULL);
    free(r);
}
static int review_native_order(void)
{
    for (unsigned kind = 0; kind < 3; kind++) {
        render_backend b = {0}; T(render_cpu_backend(&b) == 0);
        editor_config cfg = {.cols = 32, .rows = 8}; editor *e = NULL;
        T(editor_open(&e, &cfg, &b) == 0); T(settle(e) == 0); plat *p = b.config.platform;
        raw_key(p, "AC01");
        if (kind == 0) raw_close(p);
        else if (kind == 1) {
            xcb_focus_out_event_t ev = {.response_type = XCB_FOCUS_OUT, .event = p->win,
                .mode = XCB_NOTIFY_MODE_NORMAL, .detail = XCB_NOTIFY_DETAIL_NONLINEAR};
            (void)xcb_send_event(p->conn, 0, p->win, XCB_EVENT_MASK_FOCUS_CHANGE, (const char *)&ev);
        } else {
            xcb_configure_notify_event_t ev = {.response_type = XCB_CONFIGURE_NOTIFY, .event = p->win,
                .window = p->win, .width = 264, .height = 120};
            (void)xcb_send_event(p->conn, 0, p->win, XCB_EVENT_MASK_STRUCTURE_NOTIFY, (const char *)&ev);
        }
        raw_barrier(p);
        if (kind == 0) {
            int rc = 0;
            for (unsigned i = 0; i < 200 && rc != EDITOR_CLOSED; i++) rc = editor_step(e, 0);
            T(rc == EDITOR_CLOSED);
        } else T(settle(e) == 0);
        T(expect(e, "a", 1) == 0); editor_close(e);
    }
    puts("review 1: native batched key/close, key/focus, key/resize ordered passed"); return 0;
}
static int review_native_burst(void)
{
    render_backend b = {0}; T(render_cpu_backend(&b) == 0);
    editor_config cfg = {.cols = 32, .rows = 8}; editor *e = NULL;
    T(editor_open(&e, &cfg, &b) == 0); T(settle(e) == 0); plat *p = b.config.platform;
    for (unsigned i = 0; i < 1300; i++) {
        raw_key(p, "AC01");
        xcb_expose_event_t ev = {.response_type = XCB_EXPOSE, .window = p->win, .width = 256, .height = 120};
        (void)xcb_send_event(p->conn, 0, p->win, XCB_EVENT_MASK_EXPOSURE, (const char *)&ev);
    }
    raw_barrier(p);
    for (unsigned i = 0; i < 10000 && editor_length(e) < 1300; i++) T(editor_step(e, 1) >= 0);
    T(editor_length(e) == 1300); T(settle(e) == 0); editor_close(e);
    puts("review 2: native burst beyond both queue capacities preserves all keys passed"); return 0;
}
static void kill_submit(void *ctx, const editor_frame *f)
{ (void)ctx; if (f->last_sequence) (void)kill(getpid(), SIGKILL); }
static int review_crash_at(unsigned point)
{
    char path[] = "/tmp/editor-crash-XXXXXX"; int fd = mkstemp(path); T(fd >= 0); close(fd);
    pid_t child = fork(); T(child >= 0);
    if (!child) {
        render_backend b = {0}; if (render_null_backend(&b)) _exit(10);
        editor_config cfg = {.cols = 32, .rows = 8, .journal_path = path,
            .on_submit = point == 0 ? kill_submit : NULL, .on_present = point == 1 ? kill_submit : NULL};
        editor *e = NULL; if (editor_open(&e, &cfg, &b) || settle(e) || editor_flush(e)) _exit(11);
        plat_event ev = key('x', 0, "x"); if (editor_inject(e, &ev)) _exit(12);
        if (point == 2) {
            /* Force a continuation after mutation, before layout/submission. */
            e->lay.cfg.slice_clusters = 1;
            if (editor_step(e, 0) < 0 || editor_length(e) != 1) _exit(13);
            (void)kill(getpid(), SIGKILL);
        } else if (point == 3) {
            if (settle(e) || editor_flush(e)) _exit(14);
            (void)kill(getpid(), SIGKILL);
        } else if (settle(e)) _exit(15);
        _exit(16);
    }
    int status = 0; T(waitpid(child, &status, 0) == child);
    T(WIFSIGNALED(status) && WTERMSIG(status) == SIGKILL);
    replay_model m = {0}; journal_replay_result r;
    T(journal_replay_file(path, replay, &m, &r) == 0); unlink(path);
    T(m.len == 1 && m.bytes[0] == 'x');
    printf("review 3/P1.9-2.2: SIGKILL point=%u recovery=x passed\n", point); return 0;
}
static int review_crash(void)
{ int rc = 0; for (unsigned p = 0; p < 4; p++) rc |= review_crash_at(p); return rc; }
static ssize_t reject_append(void *ctx, int fd, const uint8_t *p, size_t n, uint64_t off)
{ (void)ctx; (void)fd; (void)p; (void)n; (void)off; errno = EIO; return -1; }
static int review_suffix_at(unsigned mode)
{
    {
        char path[] = "/tmp/editor-suffix-XXXXXX"; int fd = mkstemp(path); T(fd >= 0); close(fd);
        render_backend b = {0}; T(render_null_backend(&b) == 0);
        editor_config cfg = {.journal_path = path, .cols = 16, .rows = 4}; editor *e = NULL;
        T(editor_open(&e, &cfg, &b) == 0); T(settle(e) == 0); T(editor_flush(e) == 0);
        if (mode) { journal_io io = {.append_write = reject_append}; T(journal_set_io(e->journal, &io) == 0); }
        else {
            /* Exhaust admission before staging the suffix. */
            uint8_t *large = malloc(JOURNAL_DEFAULT_BATCH_BYTES); T(large != NULL);
            memset(large, 'q', JOURNAL_DEFAULT_BATCH_BYTES);
            T(journal_insert(e->journal, e->buffer->id, 0, large, JOURNAL_DEFAULT_BATCH_BYTES) == JOURNAL_FULL); free(large);
        }
        e->stage[0] = 'a'; e->stage[1] = 'b'; e->stage_used = 2; e->op_count = 2;
        e->ops[0] = (editor_jop){.len=1, .insert=true, .id=e->buffer->id};
        e->ops[1] = (editor_jop){.off=1, .len=1, .at=1, .insert=true, .id=e->buffer->id};
        uint64_t accepted = journal_get_stats(e->journal).accepted_sequence;
        editor_journal_staged(e); T(e->stats.journal_error == (mode ? JOURNAL_IO : JOURNAL_FULL));
        /* edit-pgr (P): the staged pair requires BEGIN before either data
         * record. IO retains that marker; FULL accepts nothing. Both original
         * ops and both stage bytes remain unaccepted in the editor queue. */
        T(e->op_count == 2u); T(e->stage_used == 2);
        T(e->journal_group_open == (mode != 0));
        if (mode) {
            T(e->ops[0].off == 0 && journal_get_stats(e->journal).accepted_sequence == accepted + 1);
            T(journal_set_io(e->journal, NULL) == 0);
        }
        editor_close(e); unlink(path);
    }
    puts("review 4: FULL/append IO retains unaccepted suffix without duplicate accepted records passed"); return 0;
}
static int review_suffix(void)
{ int a = review_suffix_at(0), b = review_suffix_at(1); return a | b; }
static int reject_sync(void *ctx, int fd, bool directory)
{ (void)ctx; (void)fd; (void)directory; errno = EIO; return -1; }
static int review_journal_exit_at(unsigned mode)
{
    char path[] = "/tmp/editor-journal-exit-XXXXXX"; int fd = mkstemp(path); T(fd >= 0); close(fd);
    render_backend b = {0}; T(render_null_backend(&b) == 0);
    editor_config cfg = {.journal_path = path, .cols = 16, .rows = 4}; editor *e = NULL;
    T(editor_open(&e, &cfg, &b) == 0); T(settle(e) == 0); T(editor_flush(e) == 0);
    if (!mode) {
        uint8_t *large = calloc(1, JOURNAL_DEFAULT_BATCH_BYTES); T(large != NULL);
        T(journal_insert(e->journal, e->buffer->id, 0, large, JOURNAL_DEFAULT_BATCH_BYTES) == JOURNAL_FULL); free(large);
    } else if (mode == 1) {
        journal_io io = {.append_write = reject_append}; T(journal_set_io(e->journal, &io) == 0);
    }
    plat_event ev = key('x', 0, "x"); T(editor_inject(e, &ev) == 0);
    if (mode < 2) T(editor_step(e, 0) == EDITOR_ERR_IO);
    else {
        T(settle(e) == 0);
        journal_io io = {.sync = reject_sync}; T(journal_set_io(e->journal, &io) == 0);
        T(journal_flush(e->journal) == JOURNAL_IO);
        T(journal_get_stats(e->journal).durable_sequence < journal_get_stats(e->journal).accepted_sequence);
    }
    T(editor_length(e) == 1); T(journal_set_io(e->journal, NULL) == 0);
    T(editor_flush(e) == 0); T(e->op_count == 0 && e->stage_used == 0);
    editor_close(e); replay_model m = {0}; journal_replay_result r;
    T(journal_replay_file(path, replay, &m, &r) == 0); T(m.len == 1 && m.bytes[0] == 'x'); unlink(path);
    printf("review 4: exit checkpoint after FULL/append IO/sync IO mode=%u restores current tree passed\n", mode); return 0;
}
static int review_journal_exit(void)
{ int a = review_journal_exit_at(0), b = review_journal_exit_at(1), c = review_journal_exit_at(2); return a | b | c; }
typedef struct checkpoint_model { replay_model buffers[2]; size_t views; bool tabs, closed; } checkpoint_model;
static int checkpoint_replay(void *ctx, const journal_record *r)
{
    checkpoint_model *m = ctx;
    if (r->type == JOURNAL_TABS) {
        if (r->size != (m->closed ? 24u : 32u) || le64(r->data) != (m->closed ? 1u : 2u) ||
            le64(r->data + 8) != (m->closed ? 0u : 1u) || le64(r->data + 16) != (m->closed ? 2u : 1u) ||
            (!m->closed && le64(r->data + 24) != 2)) return 1;
        m->tabs = true; return 0;
    }
    if (r->type == JOURNAL_WINDOW) return 0;
    if (!r->buffer_id || r->buffer_id > 2) return 1;
    replay_model *b = &m->buffers[r->buffer_id - 1];
    if (r->type == JOURNAL_BASE) {
        journal_base base; char name[4097];
        if (journal_decode_base(r, &base, name, sizeof name)) return 1;
        if (*name) {
            int fd = open(name, O_RDONLY); if (fd < 0) return 1;
            ssize_t n = read(fd, b->bytes, sizeof b->bytes); close(fd); if (n < 0) return 1;
            b->len = (size_t)n;
        }
        return 0;
    }
    if (r->type == JOURNAL_VIEW) {
        if (r->size != JOURNAL_VIEW_STATE_BYTES || le64(r->data) != 1 || le64(r->data + 8) != 1 ||
            le64(r->data + 16) != 0 || le64(r->data + 24) != 0) return 1;
        m->views++; return 0;
    }
    return replay(b, r);
}
static int review_checkpoint_session_at(bool closed)
{
    char path[] = "/tmp/editor-checkpoint-XXXXXX", source[] = "/tmp/editor-named-XXXXXX";
    int fd = mkstemp(path); T(fd >= 0); close(fd);
    fd = mkstemp(source); T(fd >= 0); T(write(fd, "aaaa", 4) == 4); close(fd);
    render_backend b = {0}; T(render_null_backend(&b) == 0);
    editor_config cfg = {.path = source, .journal_path = path, .cols = 16, .rows = 4}; editor *e = NULL;
    T(editor_open(&e, &cfg, &b) == 0); T(settle(e) == 0); T(press(e, key('x', 0, "x")) == 0);
    uint64_t id = 0; T(editor_add_buffer(e, NULL, (const uint8_t *)"two", 3, &id) == 0 && id == 2);
    T(settle(e) == 0); T(press(e, key('y', 0, "y")) == 0); T(editor_flush(e) == 0);
    if (closed) { T(editor_close_tab(e, 0) == 0); T(settle(e) == 0); }
    uint8_t *large = calloc(1, JOURNAL_DEFAULT_BATCH_BYTES); T(large != NULL);
    T(journal_insert(e->journal, id, 0, large, JOURNAL_DEFAULT_BATCH_BYTES) == JOURNAL_FULL); free(large);
    T(editor_flush(e) == 0); editor_close(e);
    checkpoint_model model = {.closed = closed}; journal_replay_result result;
    T(journal_replay_file(path, checkpoint_replay, &model, &result) == 0);
    T(model.buffers[0].len == 5 && !memcmp(model.buffers[0].bytes, "xaaaa", 5));
    T(model.buffers[1].len == 4 && !memcmp(model.buffers[1].bytes, "ytwo", 4));
    T(model.views == 2 && model.tabs); unlink(path); unlink(source);
    puts("review 4: failure checkpoint preserves named/untitled contents, views and active tab order passed"); return 0;
}
static int review_checkpoint_session(void)
{ int a = review_checkpoint_session_at(false), b = review_checkpoint_session_at(true); return a | b; }
static int review_base_race(void)
{
    char source[] = "/tmp/editor-base-race-XXXXXX", path[] = "/tmp/editor-base-log-XXXXXX";
    int fd = mkstemp(source); T(fd >= 0); T(write(fd, "aaaa", 4) == 4); close(fd);
    fd = mkstemp(path); T(fd >= 0); close(fd);
    render_backend backend = {0}; T(render_null_backend(&backend) == 0);
    editor_config cfg = {.journal_path = path, .start_empty = true, .cols = 16, .rows = 4}; editor *e = NULL;
    T(editor_open(&e, &cfg, &backend) == 0);
    editor_buffer *b = NULL; T(editor_buffer_prepare(e, source, NULL, 0, &b) == 0);
    uint8_t bytes[4]; T(piece_read(b->tree, 0, bytes, 4) == 0 && !memcmp(bytes, "aaaa", 4));
    fd = open(source, O_WRONLY | O_TRUNC); T(fd >= 0); T(write(fd, "bbbb", 4) == 4); close(fd);
    T(editor_register_journal(e, b, 1) == EDITOR_ERR_IO);
    T(journal_get_stats(e->journal).accepted_sequence == 0);
    editor_buffer_destroy(b); editor_close(e); unlink(source); unlink(path);
    puts("review 5/P1.9-2.3: source changed between attachment and BASE registration rejected passed"); return 0;
}
static int review_style(void)
{
    const char *initial = "abcdefghijklmnopqrstuv\nnext";
    for (unsigned mode = 0; mode < 4; mode++) {
        render_backend b = {0}; T(render_null_backend(&b) == 0);
        editor_config cfg = {.cols = 32, .rows = 4, .initial = (const uint8_t *)initial,
            .initial_len = strlen(initial), .wrap_mode = -1}; editor *e = NULL;
        T(editor_open(&e, &cfg, &b) == 0); T(settle(e) == 0);
        T(editor_full_layout(e) == 0); e->lay.cfg.slice_clusters = 3;
        T(layout_run(&e->lay) == LAYOUT_MORE); T(e->lay.row == 0 && e->lay.phase == 1);
        /* Style changes can also arrive during the final decoration pass. */
        e->paint_ready = true;
        if (mode < 2) {
            e->old_selection = e->v.state.selection; e->v.state.selection.cursor = 1;
            if (!mode) e->v.state.selection.anchor = 1;
        } else if (mode == 2) e->visible = false;
        else e->focused = false;
        T(editor_refresh_cursor(e, 0) == 0); T(settle(e) == 0);
        const render_grid *g = editor_grid(e); uint32_t col = layout_gutter_width(&e->lay);
        const render_cell *a = &g->cells[g->dims.cols + col], *z = a + 1;
        T(!(a->attrs & RENDER_ATTR_CURSOR));
        if (mode < 2) T(z->attrs & RENDER_ATTR_CURSOR);
        if (mode == 1) T(a->bg == e->layout_cfg.sel_bg);
        editor_close(e);
    }
    puts("review 6: submitted mid-row caret/selection/blink/focus cells passed"); return 0;
}
static int review_long_line(void)
{
    size_t n = 70000, len = 2 * n + 16; uint8_t *bytes = malloc(len); T(bytes != NULL);
    memset(bytes, 'a', n); memcpy(bytes + n, "\nvisible\n", 9);
    memset(bytes + n + 9, 'b', n); memcpy(bytes + 2 * n + 9, "\nagain\n", 7);
    render_backend b = {0}; T(render_null_backend(&b) == 0);
    editor_config cfg = {.initial = bytes, .initial_len = len, .cols = 32, .rows = 7, .wrap_mode = -1};
    editor *e = NULL; T(editor_open(&e, &cfg, &b) == 0); free(bytes); T(settle(e) == 0);
    const render_grid *g = editor_grid(e); uint32_t col = layout_gutter_width(&e->lay);
    T(g->cells[2u * g->dims.cols + col].glyph_index == 'v');
    T(g->cells[2u * g->dims.cols + col + 6].glyph_index == 'e');
    T(g->cells[4u * g->dims.cols + col].glyph_index == 'a');
    T(b.stats.submitted_frames == 1);
    T(press(e, key('x', 0, "x")) == 0);
    T(g->cells[2u * g->dims.cols + col].glyph_index == 'v');
    T(g->cells[4u * g->dims.cols + col].glyph_index == 'a');
    T(press(e, key('z', PLAT_MOD_CTRL, NULL)) == 0);
    T(g->cells[2u * g->dims.cols + col].glyph_index == 'v');
    T(g->cells[4u * g->dims.cols + col].glyph_index == 'a');
    editor_close(e); puts("review 7: multiple long clipped lines preserve subsequent rows in first frame and edit/undo passed"); return 0;
}
static int review_large_undo(void)
{
    size_t n = 600u * 1024u; uint8_t *bytes = malloc(n); T(bytes != NULL); memset(bytes, 'a', n);
    char path[] = "/tmp/editor-large-undo-XXXXXX"; int fd = mkstemp(path); T(fd >= 0); close(fd);
    render_backend b = {0}; T(render_null_backend(&b) == 0);
    editor_config cfg = {.initial = bytes, .initial_len = n, .journal_path = path,
        .cols = 32, .rows = 4, .wrap_mode = -1}; editor *e = NULL;
    T(editor_open(&e, &cfg, &b) == 0); free(bytes); T(settle(e) == 0);
    T(press(e, key('a', PLAT_MOD_CTRL, NULL)) == 0);
    T(press(e, key(XKB_KEY_BackSpace, 0, NULL)) == 0); T(editor_length(e) == 0);
    T(press(e, key('z', PLAT_MOD_CTRL, NULL)) == 0); T(editor_length(e) == n);
    T(editor_flush(e) == 0);
    uint8_t tail[8]; T(editor_read(e, n - 8, tail, 8) == 0 && !memcmp(tail, "aaaaaaaa", 8));
    editor_close(e); unlink(path); puts("review 12: journaled large deletion and undo passed"); return 0;
}
static int review_bursts(void)
{
    render_backend b = {0}; T(render_null_backend(&b) == 0);
    editor_config cfg = {.cols = 32, .rows = 4}; editor *e = NULL;
    T(editor_open(&e, &cfg, &b) == 0); T(settle(e) == 0);
    T(press(e, key('a', 0, "a")) == 0); T(press(e, key('b', 0, "b")) == 0);
    T(press(e, key('c', 0, "c")) == 0);
    T(press(e, key('z', PLAT_MOD_CTRL, NULL)) == 0); T(editor_length(e) == 0);
    T(press(e, key('Z', PLAT_MOD_CTRL | PLAT_MOD_SHIFT, NULL)) == 0); T(expect(e, "abc", 3) == 0);
    T(press(e, key(XKB_KEY_BackSpace, 0, NULL)) == 0);
    plat_event repeat = key(XKB_KEY_BackSpace, 0, NULL); repeat.repeat = true;
    T(press(e, repeat) == 0); T(press(e, repeat) == 0);
    T(press(e, key('z', PLAT_MOD_CTRL, NULL)) == 0); T(expect(e, "abc", 3) == 0);
    T(press(e, key(XKB_KEY_Left, 0, NULL)) == 0); T(press(e, key('x', 0, "x")) == 0);
    T(press(e, key('z', PLAT_MOD_CTRL, NULL)) == 0); T(expect(e, "abc", 2) == 0);
    T(press(e, key('x', 0, "x")) == 0);
    plat_event focus = {.kind = PLAT_EV_FOCUS, .focused = false}; T(press(e, focus) == 0);
    focus.focused = true; T(press(e, focus) == 0);
    T(press(e, key('y', 0, "y")) == 0);
    T(press(e, key('z', PLAT_MOD_CTRL, NULL)) == 0); T(expect(e, "abxc", 3) == 0);
    T(press(e, key('Z', PLAT_MOD_CTRL | PLAT_MOD_SHIFT, NULL)) == 0); T(expect(e, "abxyc", 4) == 0);
    struct timespec pause = {0, 310000000}; T(nanosleep(&pause, NULL) == 0);
    T(press(e, key('q', 0, "q")) == 0);
    T(press(e, key('z', PLAT_MOD_CTRL, NULL)) == 0); T(expect(e, "abxyc", 4) == 0);
    editor_close(e); puts("review 13: typing/repeat bursts, movement/focus/timeout boundaries passed"); return 0;
}
/* Attribute the native translation interval independently of editor ingress.
 * XCB dequeue/packet allocation uses Law 2's library exemption; the
 * actual XKB translation call is the one used by platform dispatch. */
static size_t native_translation_probe(x11_input *in, const xcb_key_press_event_t *raw)
{
    void *(*volatile allocate)(size_t) = malloc;
    edit_malloc_guard_begin();
    void *probe = allocate(17); plat_event translated;
    x11_translate(in, raw->detail, raw->state, &translated);
    free(probe);
    return edit_malloc_guard_end();
}
static int review_native_allocations(void)
{
    render_backend backend = {0}; T(render_cpu_backend(&backend) == 0);
    counted c = {0}; editor_config cfg = {.cols = 32, .rows = 4,
        .hook_ctx = &c, .on_ingress = ingress, .on_submit = submitted, .on_io = io_boundary};
    editor *e = NULL; T(editor_open(&e, &cfg, &backend) == 0); T(settle(e) == 0);
    plat *p = backend.config.platform; x11_input *in = p->in;
    xcb_key_press_event_t calibration = {.detail = 38};
    size_t calibrated = native_translation_probe(in, &calibration);
    T(!edit_malloc_guard_active() || calibrated == 1);
    size_t native_allocations = 0, keys = 0;
    for (unsigned i = 0; i < 100; i++) {
        raw_key(p, "AC01"); raw_barrier(p);
        xcb_generic_event_t *raw;
        while ((raw = xcb_poll_for_event(p->conn)) != NULL) {
            uint8_t type = raw->response_type & 0x7fu;
            if (type == XCB_KEY_PRESS || type == XCB_KEY_RELEASE) {
                plat_event translated; uint64_t now = trace_now_ns();
                edit_malloc_guard_begin();
                bool deliver = x11_input_key(in, (const xcb_key_press_event_t *)(const void *)raw,
                                             type == XCB_KEY_PRESS, now, &translated);
                native_allocations += edit_malloc_guard_end();
                if (deliver) { T(editor_inject(e, &translated) == 0); if (translated.press) keys++; }
            }
            free(raw);
        }
        T(settle(e) == 0);
    }
    T(keys == 100 && native_allocations == 0 && c.allocations == 0);
    T(editor_length(e) == 100);
    editor_close(e);
    printf("review 21: native dequeue-to-translation mallocs=%zu; mutation-to-submit=%zu; guard=%s (M)[AC]\n",
        native_allocations, c.allocations, edit_malloc_guard_active() ? "active" : "ASan-inert");
    return 0;
}
static int review_replay_slices(void)
{
    char journal_path[] = "build/s9-public-replay-XXXXXX";
    int fd = mkstemp(journal_path); T(fd >= 0); close(fd);
    render_backend backend = {0}; T(render_null_backend(&backend) == 0);
    editor_config cfg = {.cols = 40, .rows = 4, .history_keys = 8192, .journal_path = journal_path}; editor *e = NULL;
    T(editor_open(&e, &cfg, &backend) == 0); T(settle(e) == 0);
    for (unsigned batch = 0; batch < 4; batch++) {
        for (unsigned i = 0; i < 1024; i++) T(editor_inject(e, &(plat_event){.kind = PLAT_EV_KEY,
            .press = true, .keysym = 'x', .utf8 = {'x'}, .utf8_len = 1, .t0_ns = 1}) == 0);
        T(settle(e) == 0);
    }
    uint64_t other; T(editor_add_buffer(e, NULL, (const uint8_t *)"other", 5, &other) == 0);
    T(settle(e) == 0); T(editor_select_tab(e, 0) == 0); T(settle(e) == 0);
    uint64_t submitted_before = e->stats.submitted_sequence, journal_before = e->stats.journal_records;
    e->stats.longest_slice_ns = 0;
    T(editor_inject(e, &(plat_event){.kind = PLAT_EV_KEY, .press = true,
        .keysym = 'z', .mods = PLAT_MOD_CTRL}) == 0);
    T(editor_step(e, 0) >= 0);
    printf("review 11: first undo turn longest_slice_ns=%llu (M)[AC], slice limit=500000 ns (G)\n",
        (unsigned long long)e->stats.longest_slice_ns);
    T(undo_replay_snapshot(e->undo) != NULL); /* group yields before completion */
    T(e->stats.submitted_sequence == submitted_before && e->stats.journal_records == journal_before);
    T(editor_length(e) == 4096);
    uint8_t committed[4096]; T(editor_read(e, 0, committed, sizeof committed) == 0);
    for (size_t i = 0; i < sizeof committed; i++) T(committed[i] == 'x');
    T(editor_select_tab(e, 1) == EDITOR_ERR_ARG);
    T(editor_close_tab(e, 0) == EDITOR_ERR_ARG);
    T(editor_reopen_tab(e) == EDITOR_ERR_ARG);
    T(editor_add_buffer(e, NULL, NULL, 0, &other) == EDITOR_ERR_ARG);
    ipc_request request = {0}; T(editor_open_request(e, &request, 0) == EDITOR_ERR_ARG);
    T(editor_cycle_tab(e, false) == EDITOR_ERR_ARG);
    T(editor_set_cursor(e, 0) == EDITOR_ERR_ARG); T(editor_jump_line(e, 0) == EDITOR_ERR_ARG);
    T(e->action == EDITOR_ACTION_REPLAY_WORK && tabs_active_index(&e->tabs) == 0);
    T(settle(e) == 0); T(editor_length(e) == 0);
    T(press(e, key('Z', PLAT_MOD_CTRL | PLAT_MOD_SHIFT, NULL)) == 0); T(editor_length(e) == 4096);
    editor_close(e); T(unlink(journal_path) == 0); puts("review 11: multi-record burst undo/redo yields across turns passed"); return 0;
}
typedef struct source_prompt_probe { editor *owner; bool shown; } source_prompt_probe;
static void source_watch_barrier(work_ctx *ctx) { (void)ctx; }
static void source_prompt_submit(void *ctx, const editor_frame *frame)
{
    source_prompt_probe *probe = ctx; (void)frame;
    if (!probe->owner) return;
    editor *e = probe->owner;
    const char prompt[] = "Source changed: R reload / K keep";
    bool matches = e->grid.dims.cols >= sizeof prompt - 1;
    for (size_t i = 0; matches && i < sizeof prompt - 1; i++)
        /* (E) Last zero-based row = total rows minus one; prompt length
         * excludes its C terminator. Inspect the actual submitted status. */
        matches = e->grid.cells[(size_t)(e->grid.dims.rows - 1u) * e->grid.dims.cols + i].glyph_index == (uint8_t)prompt[i];
    probe->shown |= matches;
}
/* (E) Fixture geometry: columns = prompt literal length; rows = tab row +
 * each newline-terminated fixture line + bottom status row. Unit offsets
 * subtract the C terminator or select a single row; zero is the first index
 * or success/no-allocation result. No timing/count budget is introduced. */
static int source_prompt_placement(void)
{
    const char first[] = "line_one", text[] = "line_one\nline_two\n";
    const char prompt[] = "Source changed: R reload / K keep";
    uint32_t rows = 1u + 1u; /* (E) tab chrome + bottom status. */
    for (size_t i = 0; i < sizeof text - 1u; i++) if (text[i] == '\n') rows++;
    render_backend backend = {0}; T(render_null_backend(&backend) == 0);
    editor_config cfg = {.initial = (const uint8_t *)text, .initial_len = sizeof text - 1u,
        .cols = sizeof prompt - 1u, .rows = rows, .wrap_mode = -1}; /* (E) unwrapped fixture. */
    editor *e = NULL; T(editor_open(&e, &cfg, &backend) == 0); T(settle(e) == 0);
    T(editor_jump_line(e, 0) == 0); T(settle(e) == 0); /* (E) fixture starts at the first line. */
    size_t first_row = (size_t)e->tab_rows * e->grid.dims.cols;
    size_t status_row = (size_t)(e->grid.dims.rows - 1u) * e->grid.dims.cols;
    for (size_t c = 0; c < sizeof first - 1u; c++)
        T(e->grid.cells[first_row + e->lay.gw + c].glyph_index == (uint8_t)first[c]);
    /* Isolate notice composition from the already-tested source watcher. */
    e->buffer->source_stale = true;
    edit_malloc_guard_begin();
    int rc = editor_full_layout(e);
    if (!rc) rc = settle(e);
    size_t allocations = edit_malloc_guard_end();
    T(rc == 0 && allocations == 0);
    for (size_t c = 0; c < sizeof first - 1u; c++)
        T(e->grid.cells[first_row + e->lay.gw + c].glyph_index == (uint8_t)first[c]);
    for (size_t c = 0; c < sizeof prompt - 1u; c++)
        T(e->grid.cells[status_row + c].glyph_index == (uint8_t)prompt[c]);
    /* Retained notice frames must also leave the cached document visible. */
    T(editor_begin_frame(e) == 0); T(settle(e) == 0);
    T(!memcmp(e->grid.cells + first_row, e->text_grid.cells,
        (size_t)e->text_grid.dims.cols * sizeof(render_cell)));
    e->buffer->source_stale = false;
    T(editor_full_layout(e) == 0); T(settle(e) == 0);
    T(!memcmp(e->grid.cells + status_row,
        e->text_grid.cells + (size_t)(e->grid.dims.rows - e->tab_rows - 1u) * e->text_grid.dims.cols,
        (size_t)e->text_grid.dims.cols * sizeof(render_cell)));
    editor_close(e);
    puts("edit-1um: null source prompt preserves first-line glyphs, uses bottom status, restores row and allocates nothing: PASS");
    return 0;
}
static int clean_source_change(void)
{
    /* Cross open/save cleanliness with copied/mapped originals and live/hidden
     * tabs. Offsets below come from this two-byte-per-line fixture. */
    const char initial[] = "a\nb\nc\nd\ne\n", append[] = "EXTERNAL_APPEND\n";
    for (unsigned saved = 2; saved-- > 0;) for (unsigned mapped = 0; mapped < 2; mapped++)
    for (unsigned hidden = 0; hidden < 2; hidden++) {
        char path[] = "build/edit-ret-source-XXXXXX", log[] = "build/edit-ret-log-XXXXXX";
        int fd = mkstemp(path); T(fd >= 0);
        T(write(fd, initial, sizeof initial - 1) == (ssize_t)(sizeof initial - 1)); close(fd);
        fd = mkstemp(log); T(fd >= 0); close(fd);
        render_backend backend = {0}; T(render_null_backend(&backend) == 0);
        source_prompt_probe probe = {0};
        editor_config cfg = {.path = path, .journal_path = log, .copy_threshold = mapped ? 1 : 0,
            .cols = sizeof "Source changed: R reload / K keep" - 1, .rows = 4, .wrap_mode = -1,
            .hook_ctx = &probe, .on_submit = source_prompt_submit};
        editor *e = NULL; T(editor_open(&e, &cfg, &backend) == 0); probe.owner = e; T(settle(e) == 0);
        if (saved) {
            editor_buffer *saved_buffer = e->buffer;
            T(press(e, key('x', 0, "x")) == 0); T(editor_tab(e, 0)->modified);
            T(press(e, key('s', PLAT_MOD_CTRL, NULL)) == 0);
            /* Reuse settle's watchdog: 5 s = 5000000000 ns / 10^9 ns/s. */
            uint64_t deadline = trace_now_ns() + UINT64_C(5000000000);
            while (editor_save_active(e)) { T(editor_step(e, 0) >= 0); T(trace_now_ns() < deadline); sched_yield(); }
            T(settle(e) == 0); T(!editor_tab(e, 0)->modified);
            /* Drain our save's parent-watch events, then let a bulk barrier
             * certify the queued identity checks have returned. A self-save
             * must neither prompt nor replace the buffer/reset its undo. */
            (void)file_watch_poll(e->buffer->file);
            work_handle checked = work_submit(&e->pool, (work_job){source_watch_barrier, NULL, 0, WORK_BULK});
            T(checked.epoch);
            while (!work_handle_finished(&e->pool, checked) || work_mailbox_pending(&e->pool)) {
                T(editor_step(e, 0) >= 0); T(trace_now_ns() < deadline); sched_yield();
            }
            T(editor_step(e, 0) >= 0); T(settle(e) == 0);
            T(e->buffer == saved_buffer && !e->buffer->source_stale && !probe.shown);
            T(undo_get_history(e->undo).undo_groups != 0);
        }
        T(!e->buffer->source_stale && !probe.shown);
        uint64_t id = e->buffer->id;
        uint64_t shift = saved ? 1u : 0u; /* saved 'x' prefixes the fixture */
        T(editor_set_cursor(e, sizeof initial - 3 + shift) == 0); T(settle(e) == 0);
        e->v.state.selection.anchor = sizeof initial - 5 + shift;
        e->v.state.first_byte = e->v.state.visual_byte = sizeof "a\nb\n" - 1 + shift;
        e->v.state.first_line = 2; /* two fixture lines precede the viewport */
        e->v.state.hscroll = 1; /* one-column horizontal offset */
        T(editor_full_layout(e) == 0); T(settle(e) == 0);
        view_state state = editor_view(e);
        editor_buffer *old = e->buffer;
        if (hidden) { uint64_t other; T(editor_add_buffer(e, NULL, (const uint8_t *)"other", sizeof "other" - 1, &other) == 0); T(settle(e) == 0); }
        fd = open(path, O_WRONLY | O_APPEND); T(fd >= 0);
        T(write(fd, append, sizeof append - 1) == (ssize_t)(sizeof append - 1)); close(fd);
        uint64_t deadline = trace_now_ns() + UINT64_C(5000000000);
        while (editor_tab(e, 0)->buffer == old->tree) {
            T(editor_step(e, 0) >= 0); T(!e->buffer->source_stale && !probe.shown);
            T(trace_now_ns() < deadline); sched_yield();
        }
        if (hidden) { T(expect(e, "other", 0) == 0); T(editor_select_tab(e, 0) == 0); }
        T(settle(e) == 0); T(!probe.shown && !e->buffer->source_stale);
        char expected[sizeof initial + sizeof append];
        T(snprintf(expected, sizeof expected, "%s%s%s", saved ? "x" : "", initial, append) > 0);
        T(expect(e, expected, state.selection.cursor) == 0);
        T(e->buffer->id == id && !editor_tab(e, 0)->modified);
        T(editor_view(e).selection.anchor == state.selection.anchor);
        T(editor_view(e).first_byte == state.first_byte && editor_view(e).first_line == state.first_line);
        T(editor_view(e).hscroll == state.hscroll);
        T(undo_get_history(e->undo).undo_groups == 0 && undo_get_history(e->undo).redo_groups == 0);
        T(press(e, key(XKB_KEY_End, PLAT_MOD_CTRL, NULL)) == 0);
        bool visible = false;
        for (size_t i = 0; i < (size_t)e->text_grid.dims.cols * e->text_grid.dims.rows; i++)
            visible |= e->text_grid.cells[i].glyph_index == 'X'; /* appended line reaches the rendered viewport */
        T(visible && !probe.shown);
        /* Shrinking below the old selection/scroll must clamp to real text. */
        fd = open(path, O_WRONLY | O_TRUNC); T(fd >= 0);
        T(write(fd, "z", sizeof "z" - 1) == (ssize_t)(sizeof "z" - 1)); close(fd);
        deadline = trace_now_ns() + UINT64_C(5000000000);
        while (editor_length(e) != sizeof "z" - 1) {
            T(editor_step(e, 0) >= 0); T(!e->buffer->source_stale && !probe.shown);
            T(trace_now_ns() < deadline); sched_yield();
        }
        T(settle(e) == 0); T(expect(e, "z", sizeof "z" - 1) == 0);
        T(editor_view(e).selection.anchor <= editor_length(e));
        T(editor_view(e).first_byte == 0 && editor_view(e).first_line == 0);
        T(editor_view(e).visual_byte <= editor_length(e) && !editor_tab(e, 0)->modified && !probe.shown);
        editor_close(e); T(unlink(path) == 0); T(unlink(log) == 0);
    }
    puts("edit-ret: clean open/save append reload, copied/mapped and inactive tabs, view preservation/clamping, undo reset and no submitted prompt passed");
    return 0;
}
static int review_source_change(void)
{
    for (unsigned mode = 0; mode < 2; mode++) {
        char directory[] = "build/editor-source-change-XXXXXX"; T(mkdtemp(directory));
        char path[128]; int length = snprintf(path, sizeof path, "%s/source", directory);
        T(length > 0 && (size_t)length < sizeof path);
        int fd = open(path, O_CREAT | O_EXCL | O_WRONLY, 0600); T(fd >= 0);
        T(write(fd, "source\n", 7) == 7); close(fd);
        render_backend backend = {0}; T(render_null_backend(&backend) == 0);
        editor_config cfg = {.path = path, .copy_threshold = 1, .cols = 40, .rows = 4}; editor *e = NULL;
        T(editor_open(&e, &cfg, &backend) == 0); T(settle(e) == 0);
        /* This case exercises the dirty-buffer prompt; clean sources reload. */
        T(press(e, key('x', 0, "x")) == 0); T(editor_tab(e, 0)->modified);
        T(editor_set_cursor(e, 0) == 0); T(settle(e) == 0);
        struct stat original; T(stat(path, &original) == 0);
        fd = open(path, O_WRONLY | (mode ? O_TRUNC : 0)); T(fd >= 0);
        T(write(fd, "change\n", 7) == 7);
        /* Same-size writes can share the original's mtime/ctime tick, even
         * after truncate. Give the identity-based watch/focus checks a
         * distinct version without relying on filesystem clock resolution. */
        struct timespec times[2] = {{.tv_nsec = UTIME_OMIT}, original.st_mtim};
        times[1].tv_sec++;
        T(futimens(fd, times) == 0); close(fd);
        if (mode) {
            plat_event focus = {.kind = PLAT_EV_FOCUS, .focused = true}; T(editor_inject(e, &focus) == 0);
        }
        uint64_t deadline = trace_now_ns() + UINT64_C(1000000000);
        while (!editor_get_stats(e).source_stale && trace_now_ns() < deadline) T(editor_step(e, 10) >= 0);
        T(editor_get_stats(e).source_stale);
        uint64_t mutations = editor_get_stats(e).mutations;
        T(press(e, key('x', 0, "x")) == 0); T(editor_get_stats(e).mutations == mutations);
        T(editor_source_resolve(e, false) == 0); T(editor_step(e, 0) >= 0);
        T(editor_get_stats(e).source_stale); /* unsafe mapped keep is refused */
        T(editor_source_resolve(e, true) == 0); T(editor_step(e, 0) >= 0); T(settle(e) == 0);
        T(!editor_get_stats(e).source_stale); T(expect(e, "change\n", 0) == 0);
        editor_close(e); unlink(path); T(rmdir(directory) == 0);
    }
    puts("review 16: mapped watch overwrite, focus truncate, edit suspension and reload/keep passed"); return 0;
}
typedef struct review_hold { _Atomic bool release, expired, entered; } review_hold;
static void review_hold_job(work_ctx *job)
{
    review_hold *h = job->arg; atomic_store(&h->entered, true);
    while (!atomic_load(&h->release) && !work_should_stop(job)) {
        struct timespec pause = {0, 1000000}; (void)nanosleep(&pause, NULL);
    }
}
static void *review_release(void *ctx)
{
    review_hold *h = ctx;
    struct timespec pause = {0, 250000000}; (void)nanosleep(&pause, NULL);
    atomic_store(&h->expired, true); atomic_store(&h->release, true); return NULL;
}
/* Test-only syscall interposition: a directory-owned marker injects ENOSPC.
 * No process-global injection state; unrelated fixture directories are normal. */
int openat(int dirfd, const char *path, int flags, ...)
{
    mode_t mode = 0;
    if (flags & O_CREAT) { va_list args; va_start(args, flags); mode = (mode_t)va_arg(args, int); va_end(args); }
    struct stat st;
    if ((flags & O_CREAT) && fstatat(dirfd, "force-enospc", &st, 0) == 0) { errno = ENOSPC; return -1; }
    return (int)syscall(SYS_openat, dirfd, path, flags, mode);
}
static int review_local_errors(void)
{
    for (unsigned save = 0; save < 2; save++) {
        char dir[] = "build/s9-local-errors-XXXXXX"; T(mkdtemp(dir));
        char path[128], marker[128];
        T(snprintf(path, sizeof path, "%s/source", dir) > 0);
        T(snprintf(marker, sizeof marker, "%s/force-enospc", dir) > 0);
        int fd = open(path, O_CREAT | O_WRONLY, 0600); T(fd >= 0);
        T(write(fd, "prefix\n", 7) == 7); if (!save) T(ftruncate(fd, 2 * FILE_PREFIX_MAX) == 0); close(fd);
        render_backend backend = {0}; T(render_null_backend(&backend) == 0);
        backend.info.capabilities |= RENDER_CAP_RASTER_POOL;
        editor_config cfg = {.cols = 32, .rows = 4}; editor *e = NULL;
        T(editor_open(&e, &cfg, &backend) == 0); T(settle(e) == 0);
        review_hold hold = {0}; work_handle h = {0};
        if (!save) {
            h = work_submit(&e->pool, (work_job){review_hold_job, &hold, 0, WORK_BULK}); T(h.epoch);
            while (!atomic_load(&hold.entered)) sched_yield();
        }
        uint64_t id; T(editor_add_buffer(e, path, NULL, 0, &id) == 0); T(settle(e) == 0);
        editor_buffer *owner = e->buffer;
        if (save) {
            fd = open(marker, O_CREAT | O_WRONLY, 0600); T(fd >= 0); close(fd);
            T(editor_large_save_begin(e) == 0);
        } else {
            T(owner->pending_open); T(truncate(path, 7) == 0);
        }
        T(editor_select_tab(e, 0) == 0); T(settle(e) == 0);
        atomic_store(&hold.release, true);
        uint64_t deadline = trace_now_ns() + UINT64_C(5000000000);
        while (save ? !owner->lg.save_done : !file_changed(owner->file)) {
            T(editor_step(e, 10) >= 0); T(trace_now_ns() < deadline);
        }
        if (save) T(owner->lg.save_status == FILE_ERR_IO && file_errno(owner->file) == ENOSPC);
        T(!e->error); T(press(e, key('x', 0, "x")) == 0); T(expect(e, "x", 1) == 0);
        editor_close(e); if (save) T(unlink(marker) == 0); T(unlink(path) == 0); T(rmdir(dir) == 0);
    }
    puts("P4-3 11: deferred COPY and inactive ENOSPC errors preserve unrelated editing passed"); return 0;
}
typedef struct review_chrome { editor *e; bool checking, damaged; } review_chrome;
static void review_chrome_submit(void *ctx, const editor_frame *frame)
{
    review_chrome *probe = ctx; (void)frame;
    if (probe->checking && (probe->e->grid.full_frame || (probe->e->grid.dirty[0] & 1u))) probe->damaged = true;
}
static int review_chrome_damage(void)
{
    render_backend backend = {0}; T(render_null_backend(&backend) == 0);
    review_chrome probe = {0};
    editor_config cfg = {.initial = (const uint8_t *)"abc", .initial_len = 3, .cols = 40, .rows = 4,
        .hook_ctx = &probe, .on_submit = review_chrome_submit}; editor *e = NULL;
    T(editor_open(&e, &cfg, &backend) == 0); probe.e = e; T(settle(e) == 0);
    T(editor_set_cursor(e, 2) == 0); T(settle(e) == 0);
    probe.checking = true; T(press(e, key(XKB_KEY_Left, 0, NULL)) == 0); T(!probe.damaged);
    probe.checking = false; T(press(e, key('x', 0, "x")) == 0);
    probe.checking = true; probe.damaged = false;
    T(press(e, key('y', 0, "y")) == 0); T(!probe.damaged);
    /* Recomposition of identical final chrome, including its stale marker. */
    T(editor_begin_frame(e) == 0); T(editor_compose(e) == 0); T(!(e->grid.dirty[0] & 1u));
    editor_close(e); puts("P4-3 40: cursor/text frames omit unchanged final tab chrome passed"); return 0;
}
static int review_capture_identity(void)
{
    for (size_t dragged = 1; dragged < 3; dragged++) {
        render_backend backend = {0}; T(render_null_backend(&backend) == 0);
        editor_config cfg = {.cols = 64, .rows = 4}; editor *e = NULL;
        T(editor_open(&e, &cfg, &backend) == 0); T(settle(e) == 0);
        uint64_t id; T(editor_add_buffer(e, NULL, NULL, 0, &id) == 0); T(settle(e) == 0);
        T(editor_add_buffer(e, NULL, NULL, 0, &id) == 0); T(settle(e) == 0);
        plat_event click = {.kind = PLAT_EV_BUTTON, .code = 1, .press = true,
            .x = (int32_t)(dragged * 16 * e->grid.dims.cell_w + 1), .y = 1};
        T(press(e, click) == 0); T(tabs_active_index(&e->tabs) == dragged);
        T(press(e, key('w', PLAT_MOD_CTRL, NULL)) == 0);
        uint64_t first = tabs_at(&e->tabs, 0)->id, second = tabs_at(&e->tabs, 1)->id;
        plat_event motion = {.kind = PLAT_EV_MOTION, .x = 1, .y = 1};
        T(editor_inject(e, &motion) == 0); T(editor_step(e, 0) >= 0);
        T(tabs_at(&e->tabs, 0)->id == first && tabs_at(&e->tabs, 1)->id == second);
        e->map_drag = true; T(editor_select_tab(e, 0) == 0); T(!e->map_drag); T(settle(e) == 0);
        editor_close(e);
    }
    puts("P4-3 41: closed drag owner and switched minimap capture cancel safely passed"); return 0;
}
static void review_findui_receive(const work_msg *msg, void *ctx)
{ (void)findui_accept(ctx, msg); }
static int review_replace_accounting(void)
{
    render_backend backend = {0}; T(render_null_backend(&backend) == 0);
    editor_config cfg = {.initial = (const uint8_t *)"a", .initial_len = 1, .cols = 16, .rows = 2}; editor *e = NULL;
    T(editor_open(&e, &cfg, &backend) == 0); T(settle(e) == 0);
    undo_state state = {{0}};
    for (unsigned i = 0; i < 4096; i++) {
        undo_break_burst(e->undo);
        T(undo_insert(e->undo, piece_len(e->tree), (const uint8_t *)"x", 1, i + 1u, &state, &state) == 0);
    }
    edit_arena arena; T(edit_arena_init(&arena, 16u * 1024u * 1024u) == 0);
    findui_panel panel = {0}; findui_config config = {&arena, &e->pool, 64, 64, NULL, NULL};
    T(findui_init(&panel, &config) == FINDUI_OK);
    piece_snapshot *snap = piece_snapshot_take(e->tree); T(snap);
    T(findui_set_source(&panel, snap, 1) == FINDUI_OK); piece_snapshot_release(snap);
    T(findui_set_window(&panel, 0, piece_len(e->tree)) == FINDUI_OK);
    T(findui_show(&panel, true, true) == FINDUI_OK);
    T(findui_set_query(&panel, (const uint8_t *)"a", 1) == FINDUI_OK);
    uint64_t deadline = trace_now_ns() + UINT64_C(5000000000);
    while (findui_get_state(&panel).searching) {
        T(findui_service(&panel) <= FINDUI_MORE);
        (void)work_mailbox_drain(&e->pool, review_findui_receive, &panel);
        T(trace_now_ns() < deadline); sched_yield();
    }
    T(findui_get_state(&panel).match_count == 1);
    pid_t child = fork(); T(child >= 0);
    if (!child) {
        /* A refusal must read no retained record pages, at any history size. */
        if (mprotect(e->undo->pool.base, e->undo->pool.map_bytes, PROT_NONE)) _exit(2);
        _exit(findui_replace_one(&panel, e->undo, 1, 0, 1, &state, &state) == FINDUI_ERR_LIMIT ? 0 : 3);
    }
    int status; T(waitpid(child, &status, 0) == child);
    bool bounded = WIFEXITED(status) && WEXITSTATUS(status) == 0;
    while (findui_dispose(&panel) == FINDUI_MORE) sched_yield();
    edit_arena_free(&arena); editor_close(e);
    T(bounded); puts("P4-3 27: replacement refusal reads zero retained record pages passed"); return 0;
}
static void review_hold_save(void *ctx, int step)
{
    if (step != FILE_STEP_TEMP_CREATED) return;
    review_hold *hold = ctx; atomic_store(&hold->entered, true);
    while (!atomic_load(&hold->release)) {
        struct timespec pause = {0, 1000000}; (void)nanosleep(&pause, NULL);
    }
}
static int review_retired_save(void)
{
    char path[] = "build/s9-retired-XXXXXX"; int fd = mkstemp(path); T(fd >= 0);
    T(write(fd, "source", 6) == 6); close(fd);
    render_backend backend = {0}; T(render_null_backend(&backend) == 0);
    editor_config cfg = {.path = path, .cols = 40, .rows = 4, .closed_capacity = 1}; editor *e = NULL;
    T(editor_open(&e, &cfg, &backend) == 0); T(settle(e) == 0);
    review_hold hold = {0}; file_set_step_hook(e->buffer->file, review_hold_save, &hold);
    T(editor_large_save_begin(e) == 0);
    while (!atomic_load(&hold.entered)) sched_yield();
    uint64_t id; T(editor_add_buffer(e, NULL, NULL, 0, &id) == 0); T(settle(e) == 0);
    T(editor_add_buffer(e, NULL, NULL, 0, &id) == 0); T(settle(e) == 0);
    T(editor_close_tab(e, 0) == 0); T(editor_close_tab(e, 1) == 0);
    pthread_t timer; T(pthread_create(&timer, NULL, review_release, &hold) == 0);
    T(editor_step(e, 0) >= 0); T(!atomic_load(&hold.expired));
    T(press(e, key('x', 0, "x")) == 0); T(expect(e, "x", 1) == 0); T(!atomic_load(&hold.expired));
    T(pthread_join(timer, NULL) == 0); editor_close(e); T(unlink(path) == 0);
    puts("P4-3 22: evicted buffer retains held save lease during unrelated typing passed"); return 0;
}
static int review_reload_identity(void)
{
    char path[] = "build/s9-reload-XXXXXX"; int fd = mkstemp(path); T(fd >= 0);
    T(write(fd, "a\nb\nc\n", 6) == 6); close(fd);
    ipc_server server; T(ipc_server_init_isolated(&server) == IPC_OK);
    int pair[2]; T(ipc_launcher_pair(pair) == IPC_OK); pid_t child = fork(); T(child >= 0);
    if (!child) { close(pair[0]); ipc_result rc = ipc_launcher_wait(pair[1]); if (rc) fprintf(stderr, "reload launcher result=%d\n", (int)rc); _exit(rc == IPC_OK ? 0 : 1); }
    close(pair[1]); ipc_token token; T(ipc_server_adopt_wait(&server, pair[0], &token) == IPC_OK);
    render_backend backend = {0}; T(render_null_backend(&backend) == 0);
    editor_config cfg = {.cols = 40, .rows = 4, .server = &server}; editor *e = NULL;
    T(editor_open(&e, &cfg, &backend) == 0); T(settle(e) == 0);
    ipc_request request = {.count = 1, .wait = true};
    request.paths[0] = (ipc_path){.path = path, .line = 1, .col = 1};
    T(editor_open_request(e, &request, token) == 0); T(settle(e) == 0);
    T(press(e, key('x', 0, "x")) == 0); T(editor_tab(e, 1)->modified);
    uint64_t id = e->buffer->id; T(editor_set_cursor(e, 5) == 0); T(settle(e) == 0);
    e->v.state.first_byte = e->v.state.visual_byte = 2; e->v.state.first_line = 1; e->v.state.hscroll = 3;
    e->v.state.selection.anchor = 3; e->v.state.selection.preferred_col = 9;
    view_state saved = e->v.state;
    fd = open(path, O_WRONLY); T(fd >= 0); T(write(fd, "a\nB\nc\n", 6) == 6); close(fd);
    T(file_check(e->buffer->file, NULL)); T(editor_check_sources(e) == 0); T(settle(e) == 0);
    T(editor_source_resolve(e, true) == 0); T(editor_step(e, 0) >= 0); T(settle(e) == 0);
    T(tabs_count(&e->tabs) == 2 && tabs_active_index(&e->tabs) == 1 && e->buffer->id == id);
    T(e->buffer->wait_token == token && ipc_server_token_live(&server, token));
    T(e->v.state.selection.cursor == saved.selection.cursor && e->v.state.selection.anchor == saved.selection.anchor);
    T(e->v.state.first_byte == saved.first_byte && e->v.state.first_line == saved.first_line && e->v.state.hscroll == saved.hscroll);
    int status; T(waitpid(child, &status, WNOHANG) == 0);
    T(editor_close_tab(e, 1) == 0); T(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0);
    editor_close(e); ipc_server_fini(&server); T(unlink(path) == 0);
    puts("P4-3 16: reload preserves logical tab, view and real launcher wait passed"); return 0;
}
static int review_prompt_choices(void)
{
    render_backend backend = {0}; T(render_null_backend(&backend) == 0);
    editor_config cfg = {.cols = 40, .rows = 4}; editor *e = NULL;
    T(editor_open(&e, &cfg, &backend) == 0); T(settle(e) == 0);
    plat_event old = key('r', 0, "r"); old.t0_ns = trace_now_ns(); T(editor_inject(e, &old) == 0);
    e->buffer->source_stale = true; T(editor_full_layout(e) == 0);
    T(editor_step(e, 0) >= 0); T(e->source_choice == 0); T(settle(e) == 0);
    T(press(e, key('r', PLAT_MOD_CTRL, NULL)) == 0); T(e->source_choice == 0);
    plat_event repeat = key('k', 0, "k"); repeat.repeat = true;
    T(press(e, repeat) == 0); T(e->source_choice == 0);
    T(press(e, old) == 0); T(e->source_choice == 0);
    uint64_t id; T(editor_add_buffer(e, NULL, NULL, 0, &id) == 0); T(settle(e) == 0);
    plat_event hidden = key('r', 0, "r"); hidden.t0_ns = trace_now_ns();
    T(editor_inject(e, &hidden) == 0); T(editor_select_tab(e, 0) == 0);
    T(editor_step(e, 0) >= 0); T(e->source_choice == 0); T(settle(e) == 0);
    plat_event fresh = key('r', 0, "r"); T(editor_inject(e, &fresh) == 0);
    T(editor_step(e, 0) >= 0); T(e->source_choice == 2);
    editor_close(e); puts("P4-3 14: pre-prompt, modified, repeated and aged recovery keys ignored passed"); return 0;
}
static int review_stale_controls(void)
{
    render_backend backend = {0}; T(render_null_backend(&backend) == 0);
    editor_config cfg = {.cols = 40, .rows = 4}; editor *e = NULL;
    T(editor_open(&e, &cfg, &backend) == 0); T(settle(e) == 0);
    uint64_t id; T(editor_add_buffer(e, NULL, (const uint8_t *)"safe", 4, &id) == 0); T(settle(e) == 0);
    e->buffer->source_stale = true; T(editor_full_layout(e) == 0); T(settle(e) == 0);
    T(press(e, key(XKB_KEY_Tab, PLAT_MOD_CTRL, NULL)) == 0); T(tabs_active_index(&e->tabs) == 0);
    T(editor_select_tab(e, 1) == 0); T(settle(e) == 0);
    plat_event click = {.kind = PLAT_EV_BUTTON, .code = 1, .press = true, .x = 1, .y = 1};
    T(press(e, click) == 0); T(tabs_active_index(&e->tabs) == 0);
    T(editor_select_tab(e, 1) == 0); T(settle(e) == 0);
    T(press(e, key('w', PLAT_MOD_CTRL, NULL)) == 0); T(tabs_count(&e->tabs) == 1);
    T(press(e, key('T', PLAT_MOD_CTRL | PLAT_MOD_SHIFT, NULL)) == 0); T(tabs_count(&e->tabs) == 2);
    T(e->buffer->source_stale); T(press(e, key('x', 0, "x")) == 0); T(editor_length(e) == 4);
    editor_close(e); puts("P4-3 13: suspended source preserves tab keys and tab pointer controls passed"); return 0;
}
static int review_adoption_error(void)
{
    char path[] = "build/s9-adoption-error-XXXXXX"; int fd = mkstemp(path); T(fd >= 0);
    T(write(fd, "prefix\n", 7) == 7); T(ftruncate(fd, 2 * FILE_PREFIX_MAX) == 0); close(fd);
    render_backend backend = {0}; T(render_null_backend(&backend) == 0);
    backend.info.capabilities |= RENDER_CAP_RASTER_POOL;
    editor_config cfg = {.cols = 40, .rows = 4}; editor *e = NULL;
    T(editor_open(&e, &cfg, &backend) == 0); T(settle(e) == 0);
    review_hold hold = {0}; work_handle h = work_submit(&e->pool, (work_job){review_hold_job, &hold, 0, WORK_BULK}); T(h.epoch);
    while (!atomic_load(&hold.entered)) sched_yield();
    uint64_t id; T(editor_add_buffer(e, path, NULL, 0, &id) == 0); T(settle(e) == 0);
    editor_buffer *owner = e->buffer; T(owner->pending_open);
    T(editor_select_tab(e, 0) == 0); T(settle(e) == 0); atomic_store(&hold.release, true);
    uint64_t deadline = trace_now_ns() + UINT64_C(5000000000);
    while (!file_open_ready(owner->file)) {
        (void)work_mailbox_drain(&e->pool, editor_route_work, e); T(trace_now_ns() < deadline); sched_yield();
    }
    fd = open(path, O_WRONLY); T(fd >= 0); T(write(fd, "change\n", 7) == 7); close(fd);
    T(editor_adopt_open(e, owner) == 0); T(owner->source_stale && !e->error);
    T(press(e, key('x', 0, "x")) == 0); T(expect(e, "x", 1) == 0);
    editor_close(e); T(unlink(path) == 0);
    puts("P4-3 11: failed full-source identity adoption stays owner-local passed"); return 0;
}
static int review_pending_controls(void)
{
    char path[] = "build/s9-controls-XXXXXX"; int fd = mkstemp(path); T(fd >= 0);
    T(write(fd, "prefix\n", 7) == 7); T(ftruncate(fd, 2 * FILE_PREFIX_MAX) == 0); close(fd);
    render_backend backend = {0}; T(render_null_backend(&backend) == 0);
    backend.info.capabilities |= RENDER_CAP_RASTER_POOL;
    editor_config cfg = {.cols = 40, .rows = 4}; editor *e = NULL;
    T(editor_open(&e, &cfg, &backend) == 0); T(settle(e) == 0);
    review_hold hold = {0}; work_handle h = work_submit(&e->pool, (work_job){review_hold_job, &hold, 0, WORK_BULK}); T(h.epoch);
    while (!atomic_load(&hold.entered)) sched_yield();
    uint64_t id; T(editor_add_buffer(e, path, NULL, 0, &id) == 0); T(settle(e) == 0);
    editor_buffer *pending = e->buffer; T(pending->pending_open);
    plat_event text = key('x', 0, "x"), next = key(XKB_KEY_Tab, PLAT_MOD_CTRL, NULL);
    T(editor_inject(e, &text) == 0); T(editor_inject(e, &next) == 0);
    T(editor_inject(e, &(plat_event){.kind = PLAT_EV_FOCUS, .focused = false}) == 0);
    for (unsigned i = 0; i < 100; i++) T(editor_step(e, 0) >= 0);
    T(tabs_active_index(&e->tabs) == 0 && !e->focused);
    T(editor_length(e) == 0 && e->stats.mutations == 0 && e->queue_count == 1);
    plat_event y = key('y', 0, "y"); T(editor_inject(e, &y) == 0);
    for (unsigned i = 0; i < 100; i++) T(editor_step(e, 0) >= 0);
    T(expect(e, "y", 1) == 0);
    atomic_store(&hold.release, true);
    uint64_t deadline = trace_now_ns() + UINT64_C(5000000000);
    while (pending->pending_open) { T(editor_step(e, 10) >= 0); T(trace_now_ns() < deadline); }
    T(editor_select_tab(e, 1) == 0); T(settle(e) == 0);
    uint8_t bytes[8]; T(editor_read(e, 0, bytes, sizeof bytes) == 0 && !memcmp(bytes, "xprefix\n", 8));
    T(editor_select_tab(e, 0) == 0); T(settle(e) == 0); T(expect(e, "y", 1) == 0);
    editor_close(e); T(unlink(path) == 0);
    puts("P4-3 12: pending COPY admits controls and retains edit owner passed"); return 0;
}
static int review_prefix_open(void)
{
    char path[] = "build/s9-prefix-fixture-XXXXXX";
    int fd = mkstemp(path); T(fd >= 0);
    T(write(fd, "prefix\n", 7) == 7); T(ftruncate(fd, 2 * FILE_PREFIX_MAX) == 0); close(fd);
    render_backend b = {0}; T(render_null_backend(&b) == 0);
    b.info.capabilities |= RENDER_CAP_RASTER_POOL; /* independent prefix lane */
    editor_config cfg = {.cols = 32, .rows = 4}; editor *e = NULL;
    T(editor_open(&e, &cfg, &b) == 0); T(settle(e) == 0);
    review_hold hold = {0}; work_handle handle = work_submit(&e->pool,
        (work_job){review_hold_job, &hold, 0, WORK_BULK}); T(handle.epoch);
    while (!atomic_load(&hold.entered)) { struct timespec pause = {0, 1000000}; (void)nanosleep(&pause, NULL); }
    pthread_t timer; T(pthread_create(&timer, NULL, review_release, &hold) == 0);
    uint64_t id; int rc = editor_add_buffer(e, path, NULL, 0, &id);
    if (!rc) rc = settle(e);
    bool first_before_remainder = !atomic_load(&hold.expired);
    if (!rc && first_before_remainder) {
        T(e->buffer->pending_open && editor_length(e) == FILE_PREFIX_MAX);
        T(editor_inject(e, &(plat_event){.kind = PLAT_EV_KEY, .press = true,
            .keysym = 'x', .utf8 = {'x'}, .utf8_len = 1}) == 0);
        T(editor_step(e, 0) >= 0); T(e->queue_count == 1 && e->stats.mutations == 0);
    }
    T(pthread_join(timer, NULL) == 0);
    if (!rc) {
        uint64_t deadline = trace_now_ns() + UINT64_C(5000000000);
        while (e->buffer->pending_open) { T(editor_step(e, 10) >= 0); T(trace_now_ns() < deadline); }
        T(settle(e) == 0); T(editor_length(e) == 2u * FILE_PREFIX_MAX + 1u);
        T(e->stats.mutations == 1 && e->queue_count == 0);
        uint8_t first[8]; T(editor_read(e, 0, first, sizeof first) == 0 && !memcmp(first, "xprefix\n", 8));
    }
    editor_close(e); unlink(path);
    T(rc == 0); T(first_before_remainder);
    puts("review 8: first viewport precedes stalled remainder work passed"); return 0;
}
static int review_index_backlog(void)
{
    size_t n = 16u * 1024u * 1024u; uint8_t *bytes = malloc(n); T(bytes);
    memset(bytes, 0xff, n);
    render_backend b = {0}; T(render_null_backend(&b) == 0);
    editor_config cfg = {.initial = bytes, .initial_len = n, .cols = 16, .rows = 2, .wrap_mode = -1};
    editor *e = NULL; T(editor_open(&e, &cfg, &b) == 0); free(bytes); T(settle(e) == 0);
    lineidx_destroy(e->buffer->index); e->buffer->index = lineidx_create(n); T(e->buffer->index);
    lineidx_src src = editor_source(e->buffer);
    T(lineidx_build_start(e->buffer->index, &e->pool, &src) == 0);
    /* Let a whole completed backlog arrive without an editor maintenance turn. */
    for (;;) {
        bool busy = false;
        for (size_t i = 0; i < WORK_MAX_JOBS; i++) busy |= atomic_load(&e->pool.slots[i].busy) != 0;
        if (!busy) break;
        struct timespec pause = {0, 1000000}; (void)nanosleep(&pause, NULL);
    }
    T(!lineidx_any_nonascii(e->buffer->index));
    plat_event ev = key('x', 0, "x"); T(editor_handle_key(e, &ev) == 0);
    T(!lineidx_any_nonascii(e->buffer->index));
    T(settle(e) == 0); editor_close(e);
    /* Exercise the editor's own metadata update at the maximum gated size.
     * No content reads: an unbuilt table can model the sparse suffix without
     * mapping/touching it. Each action changes only byte zero of the tree. */
    b = (render_backend){0}; T(render_null_backend(&b) == 0);
    cfg = (editor_config){.cols = 16, .rows = 2, .wrap_mode = -1}; e = NULL;
    T(editor_open(&e, &cfg, &b) == 0); T(settle(e) == 0);
    lineidx_destroy(e->buffer->index);
    e->buffer->index = lineidx_create(UINT64_C(10) * 1024 * 1024 * 1024); T(e->buffer->index);
    uint64_t worst = 0;
    for (unsigned i = 0; i < 128; i++) {
        uint64_t before = lineidx_foreground_work(e->buffer->index);
        ev = i % 2 ? key(XKB_KEY_BackSpace, 0, NULL) : key('x', 0, "x");
        T(editor_handle_key(e, &ev) == 0);
        uint64_t visits = lineidx_foreground_work(e->buffer->index) - before;
        if (visits > worst) worst = visits;
    }
    editor_close(e);
    printf("review 10: 10 GiB beginning edits metadata visits max=%llu (M)[AC], bound=4096 (G)\n",
        (unsigned long long)worst); T(worst < 4096);
    puts("review 10: mutation cancels completed backlog without adopting it passed"); return 0;
}
static int review_piece_recycling(void)
{
    size_t n = 600u * 1024u; uint8_t *bytes = malloc(n); T(bytes); memset(bytes, 'a', n);
    render_backend b = {0}; T(render_null_backend(&b) == 0);
    editor_config cfg = {.initial = bytes, .initial_len = n, .cols = 16, .rows = 2, .wrap_mode = -1};
    editor *e = NULL; T(editor_open(&e, &cfg, &b) == 0); free(bytes); T(settle(e) == 0);
    T(press(e, key('a', PLAT_MOD_CTRL, NULL)) == 0);
    T(press(e, key('x', 0, "x")) == 0);
    for (unsigned i = 0; i < 4; i++) {
        T(press(e, key('z', PLAT_MOD_CTRL, NULL)) == 0);
        T(press(e, key('Z', PLAT_MOD_CTRL | PLAT_MOD_SHIFT, NULL)) == 0);
    }
    size_t warm = e->buffer->arena.used;
    for (unsigned i = 0; i < 256; i++) {
        T(press(e, key('z', PLAT_MOD_CTRL, NULL)) == 0); T(editor_length(e) == n);
        T(press(e, key('Z', PLAT_MOD_CTRL | PLAT_MOD_SHIFT, NULL)) == 0); T(editor_length(e) == 1);
    }
    size_t growth = e->buffer->arena.used - warm; editor_close(e);
    printf("review 15: replay arena growth=%zu bytes (M)[AC], bound=65536 bytes (G)\n", growth);
    T(growth <= 65536); return 0;
}
typedef struct review_submit_state { delayed device; uint64_t elapsed; } review_submit_state;
static int review_slow_submit(render_backend *b, const render_grid *g, const render_strip *s, size_t n)
{
    uint64_t start = trace_now_ns(); struct timespec pause = {0, 2000000};
    (void)nanosleep(&pause, NULL);
    ((review_submit_state *)b->state)->elapsed = trace_now_ns() - start;
    return delayed_submit(b, g, s, n);
}
static int review_slice_accounting(void)
{
    render_backend b = {.info = {"editor submit accounting", sizeof(review_submit_state), 16, RENDER_CAP_HEADLESS},
        .ops = {delayed_init, delayed_resize, review_slow_submit, delayed_present, delayed_event, delayed_close}};
    editor_config cfg = {.cols = 16, .rows = 3}; editor *e = NULL;
    T(editor_open(&e, &cfg, &b) == 0); ((review_submit_state *)b.state)->device.ready = true;
    T(settle(e) == 0);
    uint64_t observed = ((review_submit_state *)b.state)->elapsed;
    uint64_t reported = editor_get_stats(e).longest_slice_ns; editor_close(e);
    printf("review 11: indivisible submit observed=%llu reported=%llu ns (M)[AC]\n",
        (unsigned long long)observed, (unsigned long long)reported);
    T(reported >= observed); return 0;
}
static int review_held_sync(void *ctx, int fd, bool directory)
{
    review_hold *h = ctx;
    if (!directory) {
        atomic_store(&h->entered, true);
        while (!atomic_load(&h->release)) {
            struct timespec pause = {0, 1000000}; (void)nanosleep(&pause, NULL);
        }
    }
    return directory ? fsync(fd) : fdatasync(fd);
}
static int review_journal_idle_at(bool queued)
{
    char path[] = "build/s9-idle-journal-XXXXXX"; int fd = mkstemp(path); T(fd >= 0); close(fd);
    render_backend b = {0}; T(render_null_backend(&b) == 0);
    editor_config cfg = {.cols = 16, .rows = 2, .journal_path = path}; editor *e = NULL;
    T(editor_open(&e, &cfg, &b) == 0); T(settle(e) == 0); T(editor_flush(e) == 0);
    review_hold hold = {0}; journal_io io = {.ctx = &hold, .sync = review_held_sync};
    if (!queued) T(journal_set_io(e->journal, &io) == 0);
    T(press(e, key('x', 0, "x")) == 0);
    plat_event focus = {.kind = PLAT_EV_FOCUS, .focused = false}; T(press(e, focus) == 0);
    if (queued) {
        work_handle handle = work_submit(&e->pool, (work_job){review_hold_job, &hold, 0, WORK_BULK});
        T(handle.epoch);
        while (!atomic_load(&hold.entered)) {
            struct timespec ready = {0, 1000000}; (void)nanosleep(&ready, NULL);
        }
    }
    struct timespec pause = {1, 10000000}; (void)nanosleep(&pause, NULL);
    T(editor_step(e, 0) >= 0);
    while (!atomic_load(&hold.entered)) { pause = (struct timespec){0, 1000000}; (void)nanosleep(&pause, NULL); }
    pthread_t timer; T(pthread_create(&timer, NULL, review_release, &hold) == 0);
    uint64_t before = editor_get_stats(e).poll_returns, deadline = trace_now_ns() + UINT64_C(150000000);
    while (trace_now_ns() < deadline) T(editor_step(e, 80) >= 0);
    uint64_t polls = editor_get_stats(e).poll_returns - before;
    T(pthread_join(timer, NULL) == 0); T(editor_flush(e) == 0); editor_close(e); unlink(path);
    printf("review 17: %s job idle polls=%llu (M)[AC], requested-timeout bound=4 (G)\n",
        queued ? "queued" : "active", (unsigned long long)polls);
    T(polls <= 4); return 0;
}
static int review_journal_idle(void)
{ int active = review_journal_idle_at(false), queued = review_journal_idle_at(true); return active | queued; }
typedef struct review_bytes { uint8_t *bytes; size_t len, cap; } review_bytes;
static int review_replay_bytes(void *ctx, const journal_record *r)
{
    review_bytes *m = ctx;
    if (r->type == JOURNAL_INSERT) {
        if (r->size < 8) return 1;
        uint64_t off = le64(r->data); size_t n = r->size - 8;
        if (off > m->len || n > m->cap - m->len) return 1;
        size_t at = (size_t)off; memmove(m->bytes + at + n, m->bytes + at, m->len - at);
        memcpy(m->bytes + at, r->data + 8, n); m->len += n;
    } else if (r->type == JOURNAL_DELETE) {
        if (r->size != 16) return 1;
        uint64_t off = le64(r->data), n = le64(r->data + 8);
        if (off > m->len || n > m->len - off) return 1;
        size_t at = (size_t)off, count = (size_t)n;
        memmove(m->bytes + at, m->bytes + at + count, m->len - at - count); m->len -= count;
    }
    return 0;
}
static int review_replay_atomic(void)
{
    char path[] = "build/s9-atomic-journal-XXXXXX"; int fd = mkstemp(path); T(fd >= 0); close(fd);
    size_t n = 600u * 1024u; uint8_t *bytes = malloc(n); T(bytes); memset(bytes, 'a', n);
    size_t requests = 0, failures = 0;
    for (size_t position = 0; position <= requests; position++) {
        unlink(path); render_backend b = {0}; T(render_null_backend(&b) == 0);
        editor_config cfg = {.initial = bytes, .initial_len = n, .cols = 16, .rows = 2,
            .journal_path = path, .wrap_mode = -1}; editor *e = NULL;
        T(editor_open(&e, &cfg, &b) == 0); T(settle(e) == 0);
        T(press(e, key('a', PLAT_MOD_CTRL, NULL)) == 0); T(press(e, key('x', 0, "x")) == 0);
        T(editor_flush(e) == 0);
        editor_piece_storage *s = &e->buffer->piece_storage;
        size_t before = s->requests, cursor = e->buffer->history_cursor;
        uint64_t records = editor_get_stats(e).journal_records;
        view_state view_before = editor_view(e);
        render_cell visible[32]; memcpy(visible, editor_grid(e)->cells, sizeof visible);
        if (position) s->fail_request = before + position;
        plat_event ev = key('z', PLAT_MOD_CTRL, NULL); T(editor_inject(e, &ev) == 0);
        int rc = editor_step(e, 0);
        if (!position) { requests = s->requests - before; T(requests > 0); T(rc >= 0); }
        else if (rc < 0) {
            failures++; uint8_t current = 0;
            T(editor_length(e) == 1 && editor_read(e, 0, &current, 1) == 0 && current == 'x');
            T(e->buffer->history_cursor == cursor && e->buffer->lines == 1);
            T(editor_view(e).selection.cursor == view_before.selection.cursor &&
                editor_view(e).selection.anchor == view_before.selection.anchor);
            T(editor_view(e).first_byte == view_before.first_byte && editor_view(e).first_line == view_before.first_line &&
                editor_view(e).hscroll == view_before.hscroll && editor_view(e).visual_byte == view_before.visual_byte);
            T(!memcmp(visible, editor_grid(e)->cells, sizeof visible));
            T(editor_get_stats(e).journal_records == records && e->op_count == 0);
        }
        s->fail_request = 0;
        if (rc >= 0) T(settle(e) == 0);
        T(editor_flush(e) == 0); editor_close(e);
        review_bytes model = {malloc(n + 8), 0, n + 8}; T(model.bytes);
        journal_replay_result result; T(journal_replay_file(path, review_replay_bytes, &model, &result) == 0);
        if (rc < 0) T(model.len == 1 && model.bytes[0] == 'x');
        else T(model.len == n && !memcmp(model.bytes, bytes, n));
        free(model.bytes); unlink(path);
    }
    free(bytes); T(failures > 0);
    printf("review 22: allocator positions=%zu failures=%zu; tree/view/history/journal rollback passed (M)[AC]\n", requests, failures);
    return 0;
}
static int review_fairness(void)
{
    render_backend b = {0}; T(render_null_backend(&b) == 0);
    editor_config cfg = {.cols = 32, .rows = 8}; editor *e = NULL;
    T(editor_open(&e, &cfg, &b) == 0); T(settle(e) == 0);
    uint64_t before = b.stats.submitted_frames;
    for (unsigned turn = 0; turn < 64; turn++) {
        while (e->queue_count < EDITOR_INPUT_CAP) {
            plat_event ev = key('x', 0, "x"); T(editor_inject(e, &ev) == 0);
        }
        T(editor_step(e, 0) >= 0);
    }
    T(b.stats.submitted_frames > before && editor_get_stats(e).submitted_sequence > 0);
    editor_close(e); puts("review 14: sustained arrivals make containing-frame progress passed"); return 0;
}
static int worker_owned_init(render_backend *b, const render_config *cfg)
{
    (void)b;
    return pthread_equal(pthread_self(), cfg->workers->threads[0]) ? 0 : RENDER_ERR_INIT;
}
static int worker_failed_init(render_backend *b, const render_config *cfg)
{ (void)b; return pthread_equal(pthread_self(), cfg->workers->threads[0]) ? RENDER_ERR_INIT : RENDER_ERR_ARG; }
static int review_init(void)
{
    render_backend b = {.info = {"mailbox init test", sizeof(delayed), 16, RENDER_CAP_HEADLESS},
        .ops = {worker_owned_init, delayed_resize, delayed_submit, delayed_present, delayed_event, delayed_close}};
    editor_config cfg = {.cols = 16, .rows = 3}; editor *e = NULL;
    T(editor_open(&e, &cfg, &b) == 0); T(b.initialized); editor_close(e);
    b.ops.init = worker_failed_init; e = NULL;
    T(editor_open(&e, &cfg, &b) == RENDER_ERR_INIT); T(e == NULL && !b.initialized && b.state == NULL);
    puts("review 23: backend init success/failure executes on work pool and adopts mailbox result passed"); return 0;
}
static int review_close_flush(void)
{
    char path[] = "/tmp/editor-close-flush-XXXXXX"; int fd = mkstemp(path); T(fd >= 0); close(fd);
    render_backend b = {0}; T(render_cpu_backend(&b) == 0);
    editor_config cfg = {.journal_path = path, .cols = 32, .rows = 8}; editor *e = NULL;
    T(editor_open(&e, &cfg, &b) == 0); T(settle(e) == 0);
    T(native_key(e, b.config.platform, "AC01", 0) == 0);
    raw_close(b.config.platform); raw_barrier(b.config.platform);
    /* Production run uses an indefinite wait. CLOSE must bypass frame finishing
     * and return a flushed acknowledgement on this very turn. */
    int rc = editor_step(e, -1); T(rc == EDITOR_CLOSED);
    journal_stats js = journal_get_stats(e->journal); T(js.durable_sequence == js.accepted_sequence);
    editor_close(e); replay_model m = {0}; journal_replay_result r;
    T(journal_replay_file(path, replay, &m, &r) == 0 && m.len == 1 && m.bytes[0] == 'a'); unlink(path);
    puts("required WM_DELETE_WINDOW: one turn, flushed recovery and shutdown passed"); return 0;
}
static int review_suite(const char *only)
{
    bool headless = only && !strcmp(only, "headless");
    struct { const char *name; int (*fn)(void); } cases[] = {
        {"p4-40", review_chrome_damage}, {"p4-41", review_capture_identity}, {"p4-27", review_replace_accounting}, {"p4-22", review_retired_save}, {"p4-16", review_reload_identity}, {"p4-14", review_prompt_choices}, {"p4-13", review_stale_controls}, {"p4-12", review_pending_controls}, {"p4-11-adopt", review_adoption_error}, {"p4-11", review_local_errors}, {"1", review_native_order}, {"2", review_native_burst}, {"3", review_crash}, {"4", review_suffix}, {"4-exit", review_journal_exit}, {"4-session", review_checkpoint_session}, {"5", review_base_race},
        {"6", review_style}, {"7", review_long_line}, {"8", review_prefix_open}, {"10", review_index_backlog}, {"11-accounting", review_slice_accounting}, {"11-replay", review_replay_slices}, {"12", review_large_undo}, {"15", review_piece_recycling},
        {"13", review_bursts}, {"16", review_source_change}, {"21", review_native_allocations}, {"14", review_fairness}, {"17", review_journal_idle}, {"22", review_replay_atomic}, {"23", review_init}, {"close", review_close_flush}
    };
    int failed = 0;
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        /* The regular suite still requires native X and launcher sockets.
         * Select its remaining cases explicitly for sandbox verification. */
        if (headless && (!strcmp(cases[i].name, "1") || !strcmp(cases[i].name, "2") ||
            !strcmp(cases[i].name, "21") || !strcmp(cases[i].name, "close") ||
            !strcmp(cases[i].name, "p4-16"))) continue;
        if (((!only || headless) && strcmp(cases[i].name, "p4-27") && (atoi(cases[i].name) <= 7 || !strcmp(cases[i].name, "8") || !strcmp(cases[i].name, "10") ||
            !strcmp(cases[i].name, "11-accounting") || !strcmp(cases[i].name, "11-replay") || !strcmp(cases[i].name, "12") ||
            !strcmp(cases[i].name, "13") || !strcmp(cases[i].name, "16") || !strcmp(cases[i].name, "15") || !strcmp(cases[i].name, "17") ||
            !strcmp(cases[i].name, "21") || !strcmp(cases[i].name, "22") || !strcmp(cases[i].name, "23") || !strcmp(cases[i].name, "close"))) ||
            (only && (!strcmp(only, "all") || !strcmp(only, cases[i].name)))) failed |= cases[i].fn();
    }
    return failed;
}
static int backend_selection(void)
{
    render_backend b = {0};
    T(editor_backend_select(&b, NULL) == 0);
    T((b.info.capabilities & RENDER_CAP_GPU) != 0);
    T(editor_backend_select(&b, "gl") == 0);
    T((b.info.capabilities & RENDER_CAP_GPU) != 0);
    T(editor_backend_select(&b, "raster") == 0);
    T((b.info.capabilities & RENDER_CAP_RASTER_POOL) != 0);
    render_backend saved = b;
    T(editor_backend_select(&b, "typo") == EDITOR_ERR_ARG);
    T(memcmp(&b, &saved, sizeof b) == 0);
    puts("editor_test: default EGL and gl/raster override passed"); return 0;
}
static int injected_gpu_init_failure(render_backend *b, const render_config *cfg)
{
    /* cfg->workers owns rendering jobs, not background discovery. Worker
     * ownership is covered by review_init and the held startup probes; this
     * seam must return the same injected error on either initializer pool. */
    (void)b; (void)cfg; return RENDER_ERR_INIT;
}
static int injected_gpu_unsupported(render_backend *b, const render_config *cfg)
{ (void)b; (void)cfg; return RENDER_ERR_UNSUPPORTED; }
/* Discovery has no first-paint deadline. This helper only reuses the existing
 * settle watchdog (5 s = 5000000000 ns / 1000000000 ns/s) to observe the final
 * selection before diagnostic/allocator assertions in these fixtures. */
static int settle_backend_probe(editor *e)
{
    uint64_t deadline = trace_now_ns() + UINT64_C(5000000000);
    while (e->startup.pending) { T(editor_step(e, 0) >= 0); T(trace_now_ns() < deadline); sched_yield(); }
    return settle(e);
}
static int backend_fallback_case(bool injected, bool headless)
{
    render_backend b = {0}; T(editor_backend_select(&b, NULL) == 0);
    if (headless) {
        T(render_null_backend(&b) == 0);
        b.info.capabilities |= RENDER_CAP_GPU;
    }
    editor_config cfg = {.cols = 32, .rows = 8, .raster_fallback = true};
    /* Null-backed GPU descriptors exercise quick failure without a display.
     * Native descriptors retain the selected-default and loader coverage. */
    if (injected) b.ops.init = injected_gpu_init_failure;
    else if (headless) b.ops.init = injected_gpu_unsupported;
    else T(setenv("EDIT_GL_EGL_LIBRARY", "/tmp/edit-zzj.15-no-such-EGL.so", 1) == 0);
    editor *e = NULL;
    FILE *log = tmpfile(); T(log != NULL);
    int saved_stderr = dup(STDERR_FILENO); T(saved_stderr >= 0);
    T(dup2(fileno(log), STDERR_FILENO) >= 0);
    int opened = editor_open(&e, &cfg, &b);
    if (!opened) T(settle_backend_probe(e) == 0);
    T(dup2(saved_stderr, STDERR_FILENO) >= 0);
    T(opened == 0);
    T(editor_get_stats(e).backend_init_error == (injected ? RENDER_ERR_INIT : RENDER_ERR_UNSUPPORTED));
    uint32_t fallback_cap = headless ? RENDER_CAP_HEADLESS : RENDER_CAP_RASTER_POOL;
    T((b.info.capabilities & fallback_cap) != 0);
    T((b.info.capabilities & RENDER_CAP_GPU) == 0);
    if (!injected && !headless) T(unsetenv("EDIT_GL_EGL_LIBRARY") == 0);
    /* Capture through typing/close too: a later retry/log must fail the
     * one-line assertion, even after the loader fault has been removed. */
    T(dup2(fileno(log), STDERR_FILENO) >= 0);
    T(settle(e) == 0); T(press(e, key('x', 0, "x")) == 0);
    T(expect(e, "x", 1) == 0);
    T(!e->startup.pending);
    T(editor_get_stats(e).backend_init_error == (injected ? RENDER_ERR_INIT : RENDER_ERR_UNSUPPORTED));
    T((b.info.capabilities & fallback_cap) != 0);
    T((b.info.capabilities & RENDER_CAP_GPU) == 0);
    editor_close(e);
    T(dup2(saved_stderr, STDERR_FILENO) >= 0); close(saved_stderr);
    rewind(log); char line[256]; T(fgets(line, sizeof line, log) != NULL);
    T(strstr(line, "EGL init failed:") && strstr(line, "code=") && strstr(line, "using raster; no retry"));
    T(fgets(line, sizeof line, log) == NULL); fclose(log);
    T(!b.initialized && b.state == NULL);
    /* With fallback disabled a GPU init error remains an error. */
    T(editor_backend_select(&b, NULL) == 0); cfg.raster_fallback = false;
    if (headless) {
        T(render_null_backend(&b) == 0);
        b.info.capabilities |= RENDER_CAP_GPU;
    }
    if (injected) b.ops.init = injected_gpu_init_failure;
    else if (headless) b.ops.init = injected_gpu_unsupported;
    else T(setenv("EDIT_GL_EGL_LIBRARY", "/tmp/edit-zzj.15-no-such-EGL.so", 1) == 0);
    T(editor_open(&e, &cfg, &b) == (injected ? RENDER_ERR_INIT : RENDER_ERR_UNSUPPORTED));
    T(e == NULL && !b.initialized);
    if (!injected && !headless) T(unsetenv("EDIT_GL_EGL_LIBRARY") == 0);
    printf("editor_test: %s EGL %s failure -> %s, one log through typing/close passed\n",
        headless ? "headless" : "default", injected ? "injected init" : headless ? "injected unsupported" : "dlopen",
        headless ? "null" : "raster"); return 0;
}
static int backend_fallback_headless(void)
{
    T(backend_fallback_case(true, true) == 0);
    T(backend_fallback_case(false, true) == 0);
    return 0;
}
static int backend_fallback(void)
{
    T(backend_fallback_headless() == 0);
    T(backend_fallback_case(true, false) == 0);
    T(backend_fallback_case(false, false) == 0);
    puts("editor_test: failed EGL init falls back once and stays raster passed"); return 0;
}
static int gpu_polled_event(render_backend *b, const render_event *ev)
{
    if (!ev->work || ev->work->kind != GL_POLL_MESSAGE) return RENDER_ERR_UNSUPPORTED;
    ((delayed *)b->state)->ready = true;
    return delayed_present(b, b->active_frame);
}
static int gpu_polled_present(render_backend *b, uint32_t id)
{ (void)b; (void)id; return 0; }
static int backend_gpu_completion(void)
{
    /* Native EGL fails on :99. This seam models a fence observed only after
     * present, with no new input/mailbox traffic to wake the editor. */
    render_backend b = {.info = {"editor GPU poll test", sizeof(delayed), 16, RENDER_CAP_HEADLESS | RENDER_CAP_GPU},
        .ops = {delayed_init, delayed_resize, delayed_submit, gpu_polled_present, gpu_polled_event, delayed_close}};
    editor_config cfg = {.cols = 16, .rows = 3}; editor *e = NULL;
    T(editor_open(&e, &cfg, &b) == 0);
    T(settle(e) == 0);
    T(b.device_seen && b.complete_seen && !b.active);
    plat_event focus = {.kind = PLAT_EV_FOCUS, .focused = false}; T(press(e, focus) == 0);
    uint64_t before = editor_get_stats(e).poll_returns;
    T(editor_step(e, 100) == EDITOR_OK);
    T(editor_get_stats(e).poll_returns == before + 1);
    T(b.stats.submitted_frames == 2);
    editor_close(e);
    puts("editor_test: GPU completion progresses and idle disarms polling passed"); return 0;
}
static int selected_backend_test(const char *name, bool require_gl)
{
    editor *e = NULL;
    int result = 1;
    bool path_created = false;
    char path[] = "/tmp/editor-selected-XXXXXX";
    counted c = {0};
#define SELECT_T(c) do { if (!(c)) { fprintf(stderr, "editor_test:%d: FAIL %s\n", __LINE__, #c); goto cleanup; } } while (0)
    render_backend b = {0}; SELECT_T(editor_backend_select(&b, name) == 0);
    int fd = mkstemp(path); SELECT_T(fd >= 0); path_created = true; close(fd);
    editor_config cfg = {.cols = 40, .rows = 8, .journal_path = path, .raster_fallback = true,
        .hook_ctx = &c, .on_ingress = ingress, .on_submit = submitted, .on_io = io_boundary};
    SELECT_T(editor_open(&e, &cfg, &b) == 0); SELECT_T(settle_backend_probe(e) == 0);
    SELECT_T(!require_gl || (b.info.capabilities & RENDER_CAP_GPU));
    for (unsigned i = 0; i < 10000; i += 100) {
        for (unsigned j = 0; j < 100; j++) {
            plat_event ev = j % 2 ? key(XKB_KEY_BackSpace, 0, NULL) : key('x', 0, "x");
            SELECT_T(editor_inject(e, &ev) == 0);
        }
        SELECT_T(settle(e) == 0);
    }
    SELECT_T(!c.active && !c.suspended && c.allocations == 0 && editor_length(e) == 0);
    editor_stats s = editor_get_stats(e);
    SELECT_T(s.mutations == 10000 && s.journal_records == 10000 && !s.journal_error);
    printf("editor_test: requested=%s actual=%s 10000 keys mallocs=%zu guard=%s\n",
        name ? name : "default", b.info.name, c.allocations, edit_malloc_guard_active() ? "active" : "ASan-inert");
    SELECT_T(editor_flush(e) == 0); editor_close(e); e = NULL; unlink(path); path_created = false;
    /* Same idle fixture as idle_policy, without a journal deadline. Test both
     * natural Xvfb EGL failure and the explicit raster override. */
    SELECT_T(editor_backend_select(&b, name) == 0); cfg.journal_path = NULL;
    SELECT_T(editor_open(&e, &cfg, &b) == 0); SELECT_T(settle_backend_probe(e) == 0);
    SELECT_T(!require_gl || (b.info.capabilities & RENDER_CAP_GPU));
    uint64_t deadline = trace_now_ns() + UINT64_C(15000000000);
    while (editor_get_stats(e).blinking && trace_now_ns() < deadline) SELECT_T(editor_step(e, -1) >= 0);
    SELECT_T(settle(e) == 0);
    s = editor_get_stats(e); SELECT_T(!s.blinking && s.cursor_visible);
    uint64_t before = s.poll_returns;
    SELECT_T(editor_step(e, 100) == EDITOR_OK);
    SELECT_T(editor_get_stats(e).poll_returns == before + 1);
    plat_event focus = {.kind = PLAT_EV_FOCUS, .focused = false}; SELECT_T(press(e, focus) == 0);
    before = editor_get_stats(e).poll_returns;
    SELECT_T(editor_step(e, 100) == EDITOR_OK);
    SELECT_T(editor_get_stats(e).poll_returns == before + 1);
    printf("editor_test: requested=%s actual=%s idle/unfocused background wakeups=0 passed\n", name ? name : "default", b.info.name);
    result = 0;
cleanup:
    /* Assertions retain their strength, but failure must still release the
     * live backend's worker allocations before returning to main/LSan. */
    if (c.active) { (void)edit_malloc_guard_end(); c.active = false; }
    c.suspended = false;
    editor_close(e);
    if (path_created) unlink(path);
    return result;
#undef SELECT_T
}
static int partial_line_start(void)
{
    const size_t len = 8u * 1024u * 1024u;
    uint8_t *bytes = malloc(len); T(bytes != NULL);
    for (size_t i = 0; i < len; i++) bytes[i] = i % 128u == 127u ? '\n' : 'a';
    render_backend b = {0}; T(render_null_backend(&b) == 0);
    editor_config cfg = {.initial = bytes, .initial_len = len, .cols = 32, .rows = 8, .wrap_mode = -1};
    editor *e = NULL; T(editor_open(&e, &cfg, &b) == 0); free(bytes); T(settle(e) == 0);
    /* P3.4 clips EOF to the last full viewport. The fixture has one final
     * empty line after its LF-terminated rows; chrome consumes one row. */
    uint64_t visible_rows = cfg.rows - 1u;
    const uint64_t starts[] = {0, (uint64_t)(len / 128u * 9u / 10u) * 128u,
        ((uint64_t)len / 128u + 1u - visible_rows) * 128u};
    for (size_t at = 0; at < sizeof starts / sizeof starts[0]; at++) {
        uint64_t start = starts[at];
        T(editor_set_cursor(e, start + 10u) == 0); T(settle(e) == 0);
        lineidx_destroy(e->buffer->index);
        e->buffer->index = lineidx_create(len); T(e->buffer->index != NULL);
        e->buffer->index_dirty = false;
        for (unsigned i = 0; i < 12; i++) {
            plat_event ev = i % 2u ? key(XKB_KEY_BackSpace, 0, NULL) :
                (i % 4u == 2u ? key(XKB_KEY_Return, 0, NULL) : key('x', 0, "x"));
            uint64_t frames = b.stats.submitted_frames;
            T(editor_inject(e, &ev) == 0);
            unsigned turns = 0;
            do {
                T(editor_step(e, 0) >= 0); turns++;
                if (view_busy(&e->v) && e->v.query_kind && e->v.query_pos < start)
                    fprintf(stderr, "partial line-start: query_byte=%llu below viewport_byte=%llu\n",
                        (unsigned long long)e->v.query_pos, (unsigned long long)start);
                T(!view_busy(&e->v) || !e->v.query_kind || e->v.query_pos >= start);
            }
            while (editor_get_stats(e).pending && turns < 16u);
            if (editor_get_stats(e).pending)
                fprintf(stderr, "partial line-start: turns=%u query_byte=%llu viewport_byte=%llu\n", turns,
                    (unsigned long long)e->v.query_pos, (unsigned long long)start);
            T(!editor_get_stats(e).pending);
            T(b.stats.submitted_frames > frames);
            T(editor_view(e).first_byte == start);
            T(e->row_byte[1] == (i % 4u == 2u ? start + 11u : start + 128u + (i % 2u ? 0u : 1u)));
            T(!editor_index_complete(e));
        }
    }
    uint64_t join = starts[1];
    T(editor_set_cursor(e, join) == 0); T(settle(e) == 0);
    lineidx_destroy(e->buffer->index);
    e->buffer->index = lineidx_create(len); T(e->buffer->index != NULL);
    e->buffer->index_dirty = false;
    plat_event back = key(XKB_KEY_BackSpace, 0, NULL); T(editor_inject(e, &back) == 0);
    unsigned turns = 0;
    do {
        T(editor_step(e, 0) >= 0); turns++;
        if (view_busy(&e->v) && e->v.query_kind && e->v.query_pos < join - 128u)
            fprintf(stderr, "partial joined line-start: query_byte=%llu below anchor_byte=%llu\n",
                (unsigned long long)e->v.query_pos, (unsigned long long)(join - 128u));
        T(!view_busy(&e->v) || !e->v.query_kind || e->v.query_pos >= join - 128u);
    } while (editor_get_stats(e).pending && turns < 16u);
    T(!editor_get_stats(e).pending && editor_view(e).first_byte == join - 128u);
    T(editor_length(e) == len - 1u);
    editor_close(e);
    puts("editor_test: partial index deep typing reuses viewport line starts passed"); return 0;
}
static int index_capacity_typing(void)
{
    char path[] = "build/s9-index-capacity-XXXXXX";
    int fd = mkstemp(path); T(fd >= 0);
    uint8_t *bytes = malloc(LINEIDX_CHUNK); T(bytes);
    memset(bytes, 'a', LINEIDX_CHUNK); bytes[LINEIDX_CHUNK - 1u] = '\n';
    T(write(fd, bytes, LINEIDX_CHUNK) == LINEIDX_CHUNK); close(fd); free(bytes);
    render_backend backend = {0}; T(render_null_backend(&backend) == 0);
    editor_config cfg = {.cols = 32, .rows = 4, .wrap_mode = -1}; editor *e = NULL;
    T(editor_open(&e, &cfg, &backend) == 0); T(settle(e) == 0);
    uint64_t id; T(editor_add_buffer(e, path, NULL, 0, &id) == 0); T(settle(e) == 0);
    for (unsigned i = 0; i < 256; i++) {
        plat_event ev = key('x', 0, "x");
        int rc = editor_handle_key(e, &ev);
        if (rc) fprintf(stderr, "capacity typing: key=%u rc=%d tree=%llu index=%llu\n", i + 1u, rc,
            (unsigned long long)editor_length(e), (unsigned long long)lineidx_len(e->buffer->index));
        T(lineidx_len(e->buffer->index) == editor_length(e)); T(rc == 0);
        /* Like editor_step, finish cooperative cursor repair before the next
         * key. Do not settle: index maintenance would rebuild the table and
         * stop this fixture from exercising successive reserved-capacity edits.
         * The completion deadline is the existing settle() bound. */
        uint64_t deadline = trace_now_ns() + UINT64_C(5000000000);
        while (view_busy(&e->v) && trace_now_ns() < deadline)
            T(editor_continue_action(e) == 0);
        T(!view_busy(&e->v)); T(e->action == EDITOR_ACTION_NONE);
        T(editor_view(e).selection.cursor == (uint64_t)i + 1u);
        T(lineidx_len(e->buffer->index) == editor_length(e));
    }
    T(lineidx_chunk_count(e->buffer->index) <= 3u);
    T(settle(e) == 0); editor_close(e); unlink(path);
    puts("P1-1 section 1: file-backed successive typing keeps bounded index geometry passed"); return 0;
}
static int index_capacity_refusal(void)
{
    uint8_t *bytes = malloc(LINEIDX_CHUNK + 1u); T(bytes); memset(bytes, 'a', LINEIDX_CHUNK + 1u);
    render_backend backend = {0}; T(render_null_backend(&backend) == 0);
    editor_config cfg = {.initial = bytes, .initial_len = LINEIDX_CHUNK, .cols = 16, .rows = 2, .wrap_mode = -1};
    editor *e = NULL; T(editor_open(&e, &cfg, &backend) == 0); T(settle(e) == 0);
    lineidx_destroy(e->buffer->index); e->buffer->index = lineidx_create_reserved(LINEIDX_CHUNK, 0); T(e->buffer->index);
    uint64_t revision = e->buffer->revision; size_t history = e->buffer->history_cursor;
    plat_event ev = key('x', 0, "xx");
    e->v.state.selection.cursor = 1; e->v.state.selection.anchor = 0;
    int rc = editor_handle_key(e, &ev); T(rc == EDITOR_ERR_CAPACITY);
    T(editor_length(e) == LINEIDX_CHUNK && lineidx_len(e->buffer->index) == LINEIDX_CHUNK);
    T(e->buffer->revision == revision && e->buffer->history_cursor == history);
    uint8_t first; T(editor_read(e, 0, &first, 1) == 0 && first == 'a');
    editor_close(e);
    cfg.initial_len = LINEIDX_CHUNK + 1u; e = NULL;
    T(editor_open(&e, &cfg, &backend) == 0); T(settle(e) == 0);
    T(press(e, key('a', PLAT_MOD_CTRL, NULL)) == 0); T(press(e, key('x', 0, "x")) == 0);
    lineidx_destroy(e->buffer->index); e->buffer->index = lineidx_create_reserved(1, 0); T(e->buffer->index);
    revision = e->buffer->revision; history = e->buffer->history_cursor;
    ev = key('z', PLAT_MOD_CTRL, NULL); rc = editor_handle_key(e, &ev); T(rc == EDITOR_ERR_CAPACITY);
    T(editor_length(e) == 1 && lineidx_len(e->buffer->index) == 1);
    T(e->buffer->revision == revision && e->buffer->history_cursor == history);
    T(editor_read(e, 0, &first, 1) == 0 && first == 'x');
    editor_close(e); free(bytes);
    puts("P1-1 section 1: replacement and undo capacity refuse before buffer/history mutation passed"); return 0;
}
static int index_eventual_repair(void)
{
    char path[] = "build/s9-index-repair-XXXXXX"; int fd = mkstemp(path); T(fd >= 0);
    const size_t n = 4u * FILE_PREFIX_MAX;
    uint8_t *bytes = malloc(n); T(bytes); memset(bytes, 'a', n);
    for (size_t i = FILE_PREFIX_MAX; i < n; i += 128u) bytes[i] = '\n';
    T(write(fd, bytes, n) == (ssize_t)n); close(fd); free(bytes);
    render_backend backend = {0}; T(render_null_backend(&backend) == 0);
    editor_config cfg = {.cols = 16, .rows = 2, .wrap_mode = -1}; editor *e = NULL;
    T(editor_open(&e, &cfg, &backend) == 0); T(settle(e) == 0);
    uint64_t id; T(editor_add_buffer(e, path, NULL, 0, &id) == 0);
    uint64_t deadline = trace_now_ns() + UINT64_C(5000000000);
    while (e->buffer->pending_open) { T(editor_step(e, 0) >= 0); T(trace_now_ns() < deadline); }
    T(settle(e) == 0);
    /* Reproduce edit before the low-density prefix's bulk index arrives. */
    lineidx_destroy(e->buffer->index); e->buffer->index = lineidx_create(n); T(e->buffer->index);
    T(lineidx_build_reserve(e->buffer->index) == 0);
    e->buffer->lines = 1; e->buffer->lg.lines_exact = false;
    edit_malloc_guard_begin();
    T(press(e, key('x', 0, "x")) == 0);
    editor_buffer *edited = e->buffer;
    /* Dirty inactive buffers must also resume; maintenance owns their trees. */
    T(editor_select_tab(e, 0) == 0); T(settle(e) == 0);
    deadline = trace_now_ns() + UINT64_C(5000000000);
    while (!lineidx_complete(edited->index) || lineidx_building(edited->index)) {
        T(editor_step(e, 0) >= 0); T(trace_now_ns() < deadline); sched_yield();
    }
    size_t allocations = edit_malloc_guard_end();
    if (edit_malloc_guard_active()) T(allocations == 0);
    T(lineidx_complete(edited->index));
    T(lineidx_line_count(edited->index).exact && lineidx_line_count(edited->index).value == 24577u);
    T(edited->lines == 24577u && edited->lg.lines_exact);
    T(editor_select_tab(e, 1) == 0); T(settle(e) == 0);
    T(editor_jump_line(e, 20000u) == EDITOR_OK); T(settle(e) == 0);
    /* Queue a restart behind real bulk work, then cancel that queued snapshot
     * with another edit. Both revisions must survive retirement and retry. */
    review_hold hold = {0}; work_handle held = work_submit(&e->pool,
        (work_job){review_hold_job, &hold, 0, WORK_BULK}); T(held.epoch);
    deadline = trace_now_ns() + UINT64_C(5000000000);
    while (!atomic_load(&hold.entered)) { T(trace_now_ns() < deadline); sched_yield(); }
    lineidx_destroy(edited->index); edited->index = lineidx_create(editor_length(e)); T(edited->index);
    T(lineidx_build_reserve(edited->index) == 0);
    T(press(e, key('x', 0, "x")) == 0);
    for (unsigned i = 0; i < 8u; i++) T(editor_step(e, 0) >= 0);
    T(lineidx_building(edited->index) && !lineidx_complete(edited->index));
    T(press(e, key('x', 0, "x")) == 0);
    atomic_store(&hold.release, true);
    deadline = trace_now_ns() + UINT64_C(5000000000);
    while (!editor_index_complete(e) || lineidx_building(edited->index)) {
        T(editor_step(e, 0) >= 0); T(trace_now_ns() < deadline); sched_yield();
    }
    T(lineidx_len(edited->index) == editor_length(e));
    T(lineidx_line_count(edited->index).exact && lineidx_line_count(edited->index).value == 24577u);
    T(editor_jump_line(e, 20000u) == EDITOR_OK); T(settle(e) == 0);
    editor_close(e); unlink(path);
    printf("P1-1 section 6: inactive exact counts/jump, bulk cancellation/retry, mallocs=%zu guard=%s passed (M)[AC]\n",
        allocations, edit_malloc_guard_active() ? "active" : "ASan-inert"); return 0;
}
/* P4-3 process recovery probes. _exit bypasses all orderly flush/close. */
static int ck4_restore(void *ctx, const journal_record *r)
{
    replay_model *m = ctx;
    if (r->type == JOURNAL_BASE) {
        journal_base base; char path[4097];
        if (journal_decode_base(r, &base, path, sizeof path) || base.size > sizeof m->bytes) return 1;
        m->len = (size_t)base.size;
        if (*path) {
            int fd = open(path, O_RDONLY); if (fd < 0) return 1;
            ssize_t n = read(fd, m->bytes, m->len); close(fd);
            if (n != (ssize_t)m->len) return 1;
        }
        return 0;
    }
    return replay(ctx, r);
}
typedef struct ck4_session { replay_model buffers[3]; } ck4_session;
static int ck4_session_restore(void *ctx, const journal_record *record)
{
    ck4_session *session=ctx;
    if(!record->buffer_id) return 0;
    if(record->buffer_id>3) return 1;
    return ck4_restore(&session->buffers[record->buffer_id-1],record);
}
static int ck4_recovery(unsigned choice)
{
    char source[] = "build/ck4-source-XXXXXX", log[] = "build/ck4-log-XXXXXX";
    int fd = mkstemp(source); T(fd >= 0 && write(fd, "a", 1) == 1); close(fd);
    fd = mkstemp(log); T(fd >= 0); close(fd);
    pid_t child = fork(); T(child >= 0);
    if (!child) {
        render_backend backend = {0}; T(render_null_backend(&backend) == 0);
        editor_config cfg = {.path = source, .journal_path = log, .cols = 32, .rows = 4, .copy_threshold = choice >= 3 ? 1 : 0};
        editor *e = NULL; T(editor_open(&e, &cfg, &backend) == 0); T(settle(e) == 0);
        uint64_t unrelated;
        T(editor_add_buffer(e,NULL,(const uint8_t *)"other",5,&unrelated)==0); T(settle(e)==0);
        T(editor_add_buffer(e,NULL,(const uint8_t *)"closed",6,&unrelated)==0); T(settle(e)==0);
        T(editor_close_tab(e,tabs_active_index(&e->tabs))==0); T(settle(e)==0);
        T(editor_select_tab(e,0)==0); T(settle(e)==0);
        T(press(e, key('x', 0, "x")) == 0);
        if (!choice) {
            T(press(e, key('s', PLAT_MOD_CTRL, NULL)) == 0);
            uint64_t deadline = trace_now_ns() + UINT64_C(1000000000);
            char saved[2] = {0};
            do {
                T(editor_step(e, 0) >= 0);
                fd = open(source, O_RDONLY); T(fd >= 0);
                ssize_t n = read(fd, saved, sizeof saved); close(fd);
                if (n == 2 && !memcmp(saved, "xa", 2)) break;
                sched_yield();
            } while (trace_now_ns() < deadline);
            T(!memcmp(saved, "xa", 2));
            uint8_t actual[sizeof saved]; T(editor_read(e, 0, actual, sizeof actual) == 0);
            T(!memcmp(actual, saved, sizeof saved));
            while (editor_save_active(e)) { T(editor_step(e, 0) >= 0); T(trace_now_ns() < deadline); sched_yield(); }
            T(!editor_tab(e, 0)->modified);
            savectl_model completed = savectl_get_model(e->buffer->save);
            T(completed.saving_submitted_ns >= completed.request_ns && !completed.saving_frame_pending);
        } else {
            if(choice>=3) {
                char next[128]; T(snprintf(next,sizeof next,"%s.new",source)>0);
                fd=open(next,O_WRONLY|O_CREAT|O_EXCL,0600); T(fd>=0 && write(fd,"z",1)==1); close(fd);
                T(rename(next,source)==0);
            } else {
                fd = open(source, O_WRONLY | O_TRUNC); T(fd >= 0 && write(fd, "z", 1) == 1); close(fd);
            }
            uint64_t deadline = trace_now_ns() + UINT64_C(2000000000);
            while (!e->buffer->source_stale) { T(editor_step(e, 0) >= 0); T(trace_now_ns() < deadline); }
            T(editor_source_resolve(e, choice == 2 || choice == 4) == 0); T(settle(e) == 0);
            T(!e->buffer->source_stale);
            if (choice == 1 || choice == 3) T(editor_tab(e, tabs_active_index(&e->tabs))->modified);
        }
        T(press(e, key('y', 0, "y")) == 0);
        _exit(0);
    }
    int status; T(waitpid(child, &status, 0) == child);
    T(WIFEXITED(status) && WEXITSTATUS(status) == 0);
    ck4_session session = {0}; journal_replay_result result;
    int rc = journal_replay_file(log, ck4_session_restore, &session, &result);
    replay_model model = session.buffers[0];
    unlink(source); unlink(log);
    T(rc == JOURNAL_OK);
    const char *expected = choice == 2 || choice == 4 ? "zy" : "xya";
    T(model.len == strlen(expected) && !memcmp(model.bytes, expected, model.len));
    T(session.buffers[1].len==5 && !memcmp(session.buffers[1].bytes,"other",5));
    T(session.buffers[2].len==6 && !memcmp(session.buffers[2].bytes,"closed",6));
    printf("P4-3 section %s: mode=%s choice=%s child abrupt exit; selected/unrelated/closed recovery exact\n",
        choice ? "4" : "3", choice>=3?"mapped":"COPY", choice==2 || choice==4?"Reload":"Keep");
    return 0;
}
/* Headless process-crash oracle: same discovery entry point used by main.
 * Paths/capacities derive from IPC_PATH_CAP; launch count is crash + recovery. */
/* edit-fi2: a hot exit with unsaved edits keeps the retained BASE copy its
 * journal names (`.sublimite-base-*` beside the journal); remove it with the
 * fixture. */
static void remove_retained(const char *dir)
{
    DIR *d = opendir(dir); if (!d) return;
    struct dirent *entry;
    while ((entry = readdir(d))) if (!strncmp(entry->d_name, ".sublimite-base-", strlen(".sublimite-base-")))
        (void)unlinkat(dirfd(d), entry->d_name, 0);
    closedir(d);
}
static int ck4_startup(void)
{
    char root[] = "build/ck4-startup-XXXXXX"; T(mkdtemp(root));
    char source[IPC_PATH_CAP], logs[2][IPC_PATH_CAP];
    T(snprintf(source, sizeof source, "%s/source", root) > 0);
    int fd = open(source, O_RDWR | O_CREAT | O_EXCL, 0600);
    T(fd >= 0 && write(fd, "base", strlen("base")) == (ssize_t)strlen("base")); close(fd);
    for (size_t launch = 0; launch < sizeof logs / sizeof logs[0]; launch++) {
        T(snprintf(logs[launch], sizeof logs[launch], "%s/session-XXXXXX", root) > 0);
        fd = mkstemp(logs[launch]); T(fd >= 0); close(fd);
        pid_t child = fork(); T(child >= 0);
        if (!child) {
            render_backend backend = {0}; T(render_null_backend(&backend) == 0);
            editor_config cfg = {.journal_path = logs[launch], .start_empty = true, .cols = 32, .rows = 4};
            editor *e = NULL; T(editor_open(&e, &cfg, &backend) == 0);
            ipc_path path = {.path = source, .line = 1, .col = 1};
            ipc_request request = {.count = 1}; request.paths[0] = path;
            T(editor_open_request(e, &request, 0) == 0); T(settle(e) == 0);
            if (!launch) T(press(e, key('x', 0, "x")) == 0);
            else {
                uint8_t bytes[sizeof "xbase" - 1];
                T(tabs_count(&e->tabs) == 1);
                T(editor_read(e, 0, bytes, sizeof bytes) == 0);
                T(!memcmp(bytes, "xbase", sizeof bytes));
                T(editor_tab(e, 0)->modified);
            }
            _exit(0); /* deliberately skips editor_flush/editor_close */
        }
        int status; T(waitpid(child, &status, 0) == child);
        T(WIFEXITED(status) && WEXITSTATUS(status) == 0);
    }
    replay_model model = {0}; journal_replay_result result;
    T(journal_replay_file(logs[1], ck4_restore, &model, &result) == 0);
    T(model.len == strlen("xbase") && !memcmp(model.bytes, "xbase", model.len));
    char lock[IPC_PATH_CAP];
    T(snprintf(lock, sizeof lock, "%s/.%s.lock", root, strrchr(logs[1], '/') + 1) > 0);
    T(unlink(lock) == 0);
    unlink(source); unlink(logs[0]); unlink(logs[1]); remove_retained(root); T(rmdir(root) == 0);
    puts("ck4 startup: abrupt exit, same-file discovery, recovered bytes/dirty state before first paint passed");
    return 0;
}
typedef struct ck4_power { uint8_t image[256u*1024u]; size_t len; } ck4_power;
static int ck4_sync_image(void *ctx, int fd, bool directory)
{
    if(directory) return fsync(fd);
    ck4_power *power=ctx;
    if(fdatasync(fd)) return -1;
    ssize_t n=pread(fd,power->image,sizeof power->image,0);
    if(n<0) return -1;
    power->len=(size_t)n; return 0;
}
static int ck4_busy_submit(render_backend *backend, const render_grid *grid, const render_strip *strips, size_t count)
{ (void)backend; (void)grid; (void)strips; (void)count; return RENDER_ERR_BUSY; }
static int ck4_credit(void)
{
    char log[]="build/ck4-credit-XXXXXX"; int fd=mkstemp(log); T(fd>=0); close(fd);
    render_backend backend={0}; T(render_null_backend(&backend)==0);
    editor_config cfg={.journal_path=log,.cols=16,.rows=2}; editor *e=NULL;
    T(editor_open(&e,&cfg,&backend)==0); T(settle(e)==0); T(editor_flush(e)==0);
    ck4_power *power=calloc(1,sizeof *power); T(power);
    journal_io io={.ctx=power,.sync=ck4_sync_image}; T(journal_set_io(e->journal,&io)==0);
    review_hold hold={0}; work_handle bulk=work_submit(&e->pool,(work_job){review_hold_job,&hold,0,WORK_BULK}); T(bulk.epoch);
    while(!atomic_load(&hold.entered)) sched_yield();
    size_t size=128u*1024u; memset(e->stage,'q',size); e->stage_used=size;
    e->ops[0]=(editor_jop){.id=e->buffer->id,.insert=true,.len=size}; e->op_count=1;
    editor_journal_staged(e);
    T(journal_get_stats(e->journal).unprotected_bytes<=JOURNAL_DEFAULT_SYNC_BYTES);
    int (*submit_fn)(render_backend *,const render_grid *,const render_strip *,size_t)=backend.ops.submit;
    backend.ops.submit=ck4_busy_submit; T(editor_begin_frame(e)==0); e->paint_ready=true;
    uint64_t deadline=trace_now_ns()+UINT64_C(5000000000);
    while(e->op_count || journal_get_stats(e->journal).durable_sequence<journal_get_stats(e->journal).accepted_sequence) {
        T(editor_step(e,0)>=0); T(e->dirty);
        T(journal_get_stats(e->journal).unprotected_bytes<=JOURNAL_DEFAULT_SYNC_BYTES);
        T(trace_now_ns()<deadline);
        sched_yield();
    }
    uint8_t *bytes=malloc(size); T(bytes); review_bytes model={bytes,0,size}; journal_replay_result result;
    T(journal_replay_bytes(power->image,power->len,review_replay_bytes,&model,&result)==0);
    T(model.len==size); for(size_t j=0;j<size;j++) T(bytes[j]=='q');
    atomic_store(&hold.release,true); backend.ops.submit=submit_fn;
    T(settle(e)==0); editor_close(e); unlink(log); free(bytes); free(power);
    puts("P4-3 section 8: complete-record credit bounded; independent durable image exact with dirty frames and occupied bulk worker");
    return 0;
}
typedef struct ck4_metadata { journal_view views[2]; uint64_t ids[2], active, count; uint32_t width,height; } ck4_metadata;
static int ck4_meta_restore(void *ctx,const journal_record *record)
{
    ck4_metadata *meta=ctx; const uint8_t *p=record->data;
    if(record->type==JOURNAL_VIEW && record->buffer_id && record->buffer_id<=2)
        meta->views[record->buffer_id-1]=(journal_view){le64(p),le64(p+8),le64(p+16),le64(p+24)};
    else if(record->type==JOURNAL_TABS) {
        meta->count=le64(p); meta->active=le64(p+8); if(meta->count>2) return 1;
        for(size_t j=0;j<meta->count;j++) meta->ids[j]=le64(p+16+j*8);
    } else if(record->type==JOURNAL_WINDOW) {
        meta->width=(uint32_t)le64(p); meta->height=(uint32_t)(le64(p)>>32);
    }
    return 0;
}
static int ck4_metadata_test(void)
{
    char log[]="build/ck4-metadata-XXXXXX"; int fd=mkstemp(log); T(fd>=0); close(fd);
    pid_t child=fork(); T(child>=0);
    if(!child) {
        render_backend backend={0}; T(render_null_backend(&backend)==0);
        editor_config cfg={.journal_path=log,.cols=32,.rows=4}; editor *e=NULL;
        T(editor_open(&e,&cfg,&backend)==0); T(settle(e)==0);
        uint64_t id; T(editor_add_buffer(e,NULL,(const uint8_t *)"ab",2,&id)==0); T(id==2); T(settle(e)==0);
        T(editor_set_cursor(e,2)==0); T(settle(e)==0);
        T(press(e,key(XKB_KEY_Left,PLAT_MOD_SHIFT,NULL))==0);
        T(tabs_reorder(&e->tabs,1,0)==0); T(editor_begin_frame(e)==0); T(settle(e)==0);
        plat_event resized={.kind=PLAT_EV_RESIZE,.w=320,.h=120}; T(press(e,resized)==0);
        T(editor_flush(e)==0); _exit(0);
    }
    int status; T(waitpid(child,&status,0)==child); T(WIFEXITED(status) && WEXITSTATUS(status)==0);
    ck4_metadata meta={0}; journal_replay_result result;
    T(journal_replay_file(log,ck4_meta_restore,&meta,&result)==0); unlink(log);
    T(meta.count==2 && meta.active==0 && meta.ids[0]==2 && meta.ids[1]==1);
    T(meta.views[1].cursor==1 && meta.views[1].anchor==2 && meta.views[1].scroll_x==0);
    T(meta.width==320 && meta.height==120);
    puts("P4-3 section 7: healthy flush survives abrupt child exit with exact selection/tab order/active/window metadata");
    return 0;
}
static int ck4_fifo(void)
{
    char root[]="build/ck4-fifo-XXXXXX"; T(mkdtemp(root));
    char fifo[128],log[128]; T(snprintf(fifo,sizeof fifo,"%s/pipe",root)>0);
    T(snprintf(log,sizeof log,"%s/journal",root)>0); T(mkfifo(fifo,0600)==0);
    pid_t child=fork(); T(child>=0);
    if(!child) {
        alarm(2); render_backend backend={0}; T(render_null_backend(&backend)==0);
        editor_config cfg={.path=fifo,.journal_path=log,.cols=16,.rows=2}; editor *e=NULL;
        int rc=editor_open(&e,&cfg,&backend); editor_close(e);
        _exit(rc==EDITOR_ERR_IO?0:1);
    }
    int status; T(waitpid(child,&status,0)==child); unlink(fifo); unlink(log); rmdir(root);
    T(WIFEXITED(status) && WEXITSTATUS(status)==0);
    puts("P4-3 section 10: journal-enabled editor rejects FIFO without a writer; child completes before alarm");
    return 0;
}
static int ck4_target_visit(void *ctx,const journal_record *record)
{
    bool *found=ctx;
    if(record->type==JOURNAL_TARGET && record->buffer_id==1 && record->size==sizeof "build/ck4-intended-missing" &&
        !memcmp(record->data,"build/ck4-intended-missing",record->size)) *found=true;
    return 0;
}
static int ck4_target(void)
{
    char log[]="build/ck4-target-XXXXXX"; int fd=mkstemp(log); T(fd>=0); close(fd);
    render_backend backend={0}; T(render_null_backend(&backend)==0);
    editor_config cfg={.path="build/ck4-intended-missing",.journal_path=log,.cols=16,.rows=2}; editor *e=NULL;
    T(editor_open(&e,&cfg,&backend)==0); T(settle(e)==0); T(editor_flush(e)==0); editor_close(e);
    bool found=false; journal_replay_result result;
    T(journal_replay_file(log,ck4_target_visit,&found,&result)==0); unlink(log); T(found);
    puts("P4-3 section 7: missing named target association persisted independently of empty content BASE"); return 0;
}
/* edit-cde fixtures derive byte offsets from the strings themselves; window
 * dimensions reuse the existing script fixture. No timing threshold added. */
/* edit-bml (P): lengths/offsets come from the literal byte sequences; the
 * viewport covers the fixture's logical rows plus editor chrome. */
static int clipboard_crlf_line_test(void)
{
    const char original[] = "alpha\r\nbeta\r\n";
    const char pasted[] = "alpha\r\nbeta\r\nalpha\r\n";
    char path[] = "build/bml-crlf-XXXXXX";
    int fd = mkstemp(path); T(fd >= 0);
    T(write(fd, original, sizeof original - 1u) == (ssize_t)(sizeof original - 1u));
    T(close(fd) == 0);
    render_backend backend = {0}; T(render_null_backend(&backend) == 0);
    counted count = {0};
    editor_config cfg = {.path = path, .cols = sizeof pasted,
        .rows = sizeof "\n\n\n" + 1u, /* (P) pasted rows, EOF row, chrome */
        .wrap_mode = -1, .hook_ctx = &count, .on_ingress = ingress, .on_submit = submitted};
    editor *e = NULL; T(editor_open(&e, &cfg, &backend) == 0); T(settle(e) == 0);
    T(file_prefix_info_of(e->buffer->file)->dominant == FILE_EOL_CRLF);
    T(press(e, key(XKB_KEY_c, PLAT_MOD_CTRL, NULL)) == 0);
    T(press(e, key(XKB_KEY_End, PLAT_MOD_CTRL, NULL)) == 0);
    T(press(e, key(XKB_KEY_v, PLAT_MOD_CTRL, NULL)) == 0);
    T(expect(e, pasted, sizeof pasted - 1u) == 0);
    T(press(e, key(XKB_KEY_z, PLAT_MOD_CTRL, NULL)) == 0);
    T(expect(e, original, sizeof original - 1u) == 0);
    T(press(e, key(XKB_KEY_y, PLAT_MOD_CTRL, NULL)) == 0);
    T(expect(e, pasted, sizeof pasted - 1u) == 0);
    T(press(e, key(XKB_KEY_Home, PLAT_MOD_CTRL, NULL)) == 0);
    T(press(e, key(XKB_KEY_x, PLAT_MOD_CTRL, NULL)) == 0);
    T(expect(e, "beta\r\nalpha\r\n", 0) == 0);
    T(press(e, key(XKB_KEY_v, PLAT_MOD_CTRL, NULL)) == 0);
    T(expect(e, pasted, sizeof "alpha\r\n" - 1u) == 0);
    T(press(e, key(XKB_KEY_z, PLAT_MOD_CTRL, NULL)) == 0);
    T(expect(e, "beta\r\nalpha\r\n", 0) == 0);
    T(press(e, key(XKB_KEY_y, PLAT_MOD_CTRL, NULL)) == 0);
    T(expect(e, pasted, sizeof "alpha\r\n" - 1u) == 0);
    T(press(e, key(XKB_KEY_s, PLAT_MOD_CTRL, NULL)) == 0);
    /* (E) reuse settle()'s existing scheduling bound, no new duration. */
    uint64_t deadline = trace_now_ns() + UINT64_C(5000000000);
    while (editor_save_active(e)) { T(editor_step(e, 0) >= 0); T(trace_now_ns() < deadline); sched_yield(); }
    T(!editor_tab(e, tabs_active_index(&e->tabs))->modified);
    char saved[sizeof pasted]; fd = open(path, O_RDONLY); T(fd >= 0);
    T(read(fd, saved, sizeof saved) == (ssize_t)(sizeof pasted - 1u)); T(close(fd) == 0);
    T(!memcmp(saved, pasted, sizeof pasted - 1u));
    T(!count.active && count.allocations == 0); /* (P) no typing-path allocations */
    editor_close(e); T(unlink(path) == 0);
    puts("edit-bml: CRLF current-line copy/cut/paste, undo/redo, byte-exact save passed"); return 0;
}
/* edit-bml (P): each output is the source literal with LF/CRLF converted to
 * the dominant separator in the target literal. Lone CR is ordinary data. */
static int clipboard_endings_test(void)
{
    const struct { const char *source, *target, *expected; } cases[] = {
        {"a\nb\r\nc\rd", "target\r\n", "a\r\nb\r\nc\rd"},
        {"a\nb\r\nc\rd", "target\n", "a\nb\nc\rd"},
        {"a\nb\r\nc\rd", "target", "a\nb\nc\rd"},
        {"a\nb\r\nc\rd", "a\r\nb\nc\r\n", "a\r\nb\r\nc\rd"},
        {"a\nb\r\nc\rd", "a\r\nb\n", "a\nb\nc\rd"}
    };
    for (size_t i = 0; i < sizeof cases / sizeof *cases; i++) {
        render_backend backend = {0}; T(render_null_backend(&backend) == 0);
        editor_config cfg = {.initial = (const uint8_t *)cases[i].source,
            .initial_len = strlen(cases[i].source), .cols = sizeof "alpha beta gamma delta!",
            .rows = sizeof "\n\n\n" + 1u, .wrap_mode = -1};
        editor *e = NULL; T(editor_open(&e, &cfg, &backend) == 0); T(settle(e) == 0);
        T(press(e, key(XKB_KEY_a, PLAT_MOD_CTRL, NULL)) == 0);
        T(press(e, key(XKB_KEY_c, PLAT_MOD_CTRL, NULL)) == 0);
        uint64_t target;
        T(editor_add_buffer(e, NULL, (const uint8_t *)cases[i].target, strlen(cases[i].target), &target) == 0);
        T(settle(e) == 0); T(press(e, key(XKB_KEY_a, PLAT_MOD_CTRL, NULL)) == 0);
        T(press(e, key(XKB_KEY_v, PLAT_MOD_CTRL, NULL)) == 0);
        T(expect(e, cases[i].expected, strlen(cases[i].expected)) == 0);
        editor_close(e);
    }
    puts("edit-bml: small mixed paste follows CRLF/LF, no-ending fallback, majority/tie passed"); return 0;
}
/* edit-bml (P): cutting the sole CRLF line must retain the buffer's detected
 * ending even while its current text has no separators. */
static int clipboard_crlf_empty_test(void)
{
    const char original[] = "alpha\r\n";
    render_backend backend = {0}; T(render_null_backend(&backend) == 0);
    editor_config cfg = {.initial = (const uint8_t *)original,
        .initial_len = sizeof original - 1u, .cols = sizeof "alpha beta gamma delta!",
        .rows = sizeof "\n" + 1u, .wrap_mode = -1};
    editor *e = NULL; T(editor_open(&e, &cfg, &backend) == 0); T(settle(e) == 0);
    T(press(e, key(XKB_KEY_x, PLAT_MOD_CTRL, NULL)) == 0); T(expect(e, "", 0) == 0);
    T(press(e, key(XKB_KEY_v, PLAT_MOD_CTRL, NULL)) == 0);
    T(expect(e, original, sizeof original - 1u) == 0);
    editor_close(e);
    puts("edit-bml: in-memory CRLF style survives cutting all text passed"); return 0;
}
/* edit-bml (P): cross VIEW_SCAN_BOUND with CRLF split at its boundary;
 * LF-only input is the maximum CRLF expansion (sizeof CRLF / sizeof LF). */
static int clipboard_crlf_bulk_test(void)
{
    size_t len = VIEW_SCAN_BOUND + sizeof "\n\r\xff" - 1u;
    size_t capacity = len * (sizeof "\r\n" - 1u);
    uint8_t *raw = malloc(len), *expected = malloc(capacity), *got = malloc(capacity);
    T(raw && expected && got); memset(raw, '\n', len);
    raw[VIEW_SCAN_BOUND - 1u] = '\r'; raw[VIEW_SCAN_BOUND] = '\n';
    raw[len - (sizeof "\r\xff" - 1u)] = '\r'; raw[len - 1u] = 0xff; /* (P) lone CR and invalid UTF-8 survive */
    size_t written = 0;
    for (size_t at = 0; at < len; at++) {
        if (raw[at] == '\r' && at + 1u < len && raw[at + 1u] == '\n') continue;
        if (raw[at] == '\n') expected[written++] = '\r';
        expected[written++] = raw[at];
    }
    render_backend backend = {0}; T(render_null_backend(&backend) == 0);
    editor_config cfg = {.initial = raw, .initial_len = len,
        .cols = sizeof "alpha beta gamma delta!", .rows = sizeof "\n\n" + 1u, .wrap_mode = -1};
    editor *e = NULL; T(editor_open(&e, &cfg, &backend) == 0); T(settle(e) == 0);
    T(press(e, key(XKB_KEY_a, PLAT_MOD_CTRL, NULL)) == 0);
    T(press(e, key(XKB_KEY_c, PLAT_MOD_CTRL, NULL)) == 0);
    uint64_t target;
    T(editor_add_buffer(e, NULL, (const uint8_t *)"target\r\n", sizeof "target\r\n" - 1u, &target) == 0);
    T(settle(e) == 0); T(press(e, key(XKB_KEY_a, PLAT_MOD_CTRL, NULL)) == 0);
    edit_malloc_guard_begin();
    T(press(e, key(XKB_KEY_v, PLAT_MOD_CTRL, NULL)) == 0);
    T(editor_length(e) == written); T(editor_read(e, 0, got, written) == 0);
    T(!memcmp(got, expected, written));
    T(press(e, key(XKB_KEY_z, PLAT_MOD_CTRL, NULL)) == 0);
    T(expect(e, "target\r\n", sizeof "target\r\n" - 1u) == 0);
    T(press(e, key(XKB_KEY_y, PLAT_MOD_CTRL, NULL)) == 0);
    T(editor_length(e) == written); T(editor_read(e, 0, got, written) == 0);
    T(!memcmp(got, expected, written));
    size_t allocations = edit_malloc_guard_end();
    if (edit_malloc_guard_active()) T(allocations == 0); /* (P) typing-path law */
    editor_close(e); free(got); free(expected); free(raw);
    puts("edit-bml: bulk LF expansion, split CRLF, lone CR/invalid UTF-8, undo/redo passed"); return 0;
}
static int clipboard_test(bool native)
{
    const char original[] = "a\r\n中";
    const char normal[] = "a\r\n中"; /* edit-bml (P): target's detected CRLF */
    counted io_count = {0};
    render_backend backend = {0};
    T((native ? render_cpu_backend(&backend) : render_null_backend(&backend)) == 0);
    editor_config cfg = {.initial = (const uint8_t *)original,
        .initial_len = sizeof original - 1, .cols = 32, .rows = 8, .wrap_mode = -1,
        .hook_ctx = &io_count, .on_io = native ? io_boundary : NULL};
    editor *e = NULL; T(editor_open(&e, &cfg, &backend) == 0); T(settle(e) == 0);
    edit_malloc_guard_begin(); io_count.active = native;
    T(press(e, key('a', PLAT_MOD_CTRL, NULL)) == 0);
    plat_event copy = key('c', PLAT_MOD_CTRL, NULL); copy.t0_ns = trace_now_ns();
    T(press(e, copy) == 0); T(e->edit_time_ns == copy.t0_ns);
    T(press(e, key(XKB_KEY_End, PLAT_MOD_CTRL, NULL)) == 0);
    T(press(e, key('v', PLAT_MOD_CTRL, NULL)) == 0);
    const char doubled[] = "a\r\n中a\r\n中"; /* edit-bml (P): preserve target ending */
    T(expect(e, doubled, sizeof doubled - 1) == 0);
    T(press(e, key('z', PLAT_MOD_CTRL, NULL)) == 0);
    T(expect(e, original, sizeof original - 1) == 0);
    T(press(e, key('a', PLAT_MOD_CTRL, NULL)) == 0);
    T(press(e, key('x', PLAT_MOD_CTRL, NULL)) == 0);
    T(expect(e, "", 0) == 0);
    T(press(e, key('z', PLAT_MOD_CTRL, NULL)) == 0);
    T(expect(e, original, sizeof original - 1) == 0);
    T(editor_view(e).selection.anchor == 0);
    T(press(e, key(XKB_KEY_Insert, PLAT_MOD_SHIFT, NULL)) == 0);
    T(expect(e, normal, sizeof normal - 1) == 0);
    T(press(e, key('z', PLAT_MOD_CTRL, NULL)) == 0);
    T(expect(e, original, sizeof original - 1) == 0);
    T(press(e, key(XKB_KEY_Insert, PLAT_MOD_CTRL, NULL)) == 0);
    T(press(e, key(XKB_KEY_Delete, PLAT_MOD_SHIFT, NULL)) == 0);
    T(expect(e, "", 0) == 0);
    T(press(e, key('z', PLAT_MOD_CTRL, NULL)) == 0);
    T(expect(e, original, sizeof original - 1) == 0);
    T(press(e, key(XKB_KEY_End, PLAT_MOD_CTRL, NULL)) == 0);
    plat_event middle = {.kind = PLAT_EV_BUTTON, .press = true, .code = 2,
        .x = (int32_t)((e->lay.gw + 2) * e->grid.dims.cell_w), /* after the fixture's wide cluster */
        .y = (int32_t)((e->tab_rows + 1) * e->grid.dims.cell_h)}; /* second logical row */
    view_state click_view = editor_view(e);
    T(press(e, middle) == 0); T(expect(e, doubled, sizeof doubled - 1) == 0);
    T(editor_view(e).first_line == click_view.first_line); /* all fixture rows fit the viewport */
    T(editor_view(e).hscroll == click_view.hscroll);
    T(press(e, key('z', PLAT_MOD_CTRL, NULL)) == 0);
    T(expect(e, original, sizeof original - 1) == 0);
    T(press(e, key(XKB_KEY_Left, PLAT_MOD_SHIFT, NULL)) == 0); /* select final UTF-8 cluster */
    T(press(e, key(XKB_KEY_End, PLAT_MOD_CTRL, NULL)) == 0);
    middle.x = (int32_t)((e->lay.gw + 1) * e->grid.dims.cell_w); /* after fixture's 'a' */
    middle.y = (int32_t)(e->tab_rows * e->grid.dims.cell_h);
    T(press(e, middle) == 0);
    const char inserted_primary[] = "a中\r\n中";
    T(expect(e, inserted_primary, sizeof "a中" - 1) == 0);
    T(press(e, key('z', PLAT_MOD_CTRL, NULL)) == 0);
    T(press(e, key(XKB_KEY_End, PLAT_MOD_CTRL, NULL)) == 0);
    T(press(e, key('v', PLAT_MOD_CTRL, NULL)) == 0);
    T(expect(e, doubled, sizeof doubled - 1) == 0); /* PRIMARY change leaves CLIPBOARD intact */
    plat_event outside = middle; outside.x = outside.y = -1; outside.t0_ns = trace_now_ns();
    T(press(e, outside) == 0); T(expect(e, doubled, sizeof doubled - 1) == 0);
    T(editor_get_stats(e).unimplemented_actions == 0);
    size_t allocations = edit_malloc_guard_end() + io_count.allocations; io_count.active = false;
    if (edit_malloc_guard_active()) T(allocations == 0);
    printf("clipboard malloc guard: %zu allocations (%s); expected zero by law\n", allocations,
        edit_malloc_guard_active() ? "active" : "ASan-inert");
    editor_close(e); puts("clipboard: null round trip, UTF-8/CRLF, replacement, cut/undo, alternate keys, PRIMARY passed");
    return 0;
}
static int clipboard_middle_long_test(void)
{
    /* Beyond one existing view window: the click must use the actual visible
     * column, and selecting a tab uses its absolute logical-line column. */
    size_t len = 2u * VIEW_WINDOW;
    uint8_t *bytes = malloc(len), *got = malloc(len + 1); T(bytes && got);
    memset(bytes, 'a', len); bytes[len - 1] = 'p';
    render_backend backend = {0}; T(render_null_backend(&backend) == 0);
    editor_config cfg = {.initial = bytes, .initial_len = len, .cols = 32, .rows = 8, .wrap_mode = -1};
    editor *e = NULL; T(editor_open(&e, &cfg, &backend) == 0); T(settle(e) == 0);
    T(editor_set_cursor(e, len) == 0); T(settle(e) == 0);
    T(press(e, key(XKB_KEY_Left, PLAT_MOD_SHIFT, NULL)) == 0);
    T(press(e, key(XKB_KEY_End, PLAT_MOD_CTRL, NULL)) == 0);
    uint64_t target = editor_view(e).hscroll; T(target > VIEW_WINDOW);
    plat_event middle = {.kind = PLAT_EV_BUTTON, .press = true, .code = 2,
        .x = (int32_t)(e->lay.gw * e->grid.dims.cell_w),
        .y = (int32_t)(e->tab_rows * e->grid.dims.cell_h)};
    T(press(e, middle) == 0);
    T(editor_view(e).selection.cursor == target + 1); T(editor_view(e).hscroll == target);
    T(editor_length(e) == len + 1); T(editor_read(e, 0, got, len + 1) == 0);
    T(!memcmp(got, bytes, (size_t)target)); T(got[target] == 'p');
    T(!memcmp(got + target + 1, bytes + target, len - (size_t)target));
    editor_close(e);
    len = (VIEW_WINDOW - 1) + sizeof "\r\np" - 1;
    memset(bytes, 'a', len); bytes[VIEW_WINDOW - 1] = '\r'; bytes[VIEW_WINDOW] = '\n'; bytes[VIEW_WINDOW + 1] = 'p';
    cfg.initial_len = len; memset(&backend, 0, sizeof backend); T(render_null_backend(&backend) == 0);
    T(editor_open(&e, &cfg, &backend) == 0); T(settle(e) == 0);
    T(editor_set_cursor(e, len) == 0); T(settle(e) == 0);
    T(press(e, key(XKB_KEY_Left, PLAT_MOD_SHIFT, NULL)) == 0);
    T(editor_set_cursor(e, VIEW_WINDOW - 1) == 0); T(settle(e) == 0);
    e->v.state.hscroll = VIEW_WINDOW - 1; T(editor_full_layout(e) == 0); T(settle(e) == 0);
    middle.x = (int32_t)((e->lay.gw + 1) * e->grid.dims.cell_w);
    T(press(e, middle) == 0); T(editor_view(e).selection.cursor == VIEW_WINDOW);
    T(editor_read(e, VIEW_WINDOW - 1, got, sizeof "p\r\np" - 1) == 0);
    T(!memcmp(got, "p\r\np", sizeof "p\r\np" - 1));
    editor_close(e); free(got); free(bytes);
    puts("clipboard middle click: deep horizontal position exact"); return 0;
}
static int clipboard_large_test(void)
{
    /* MiB crossover comes from the bead; an additional existing view window
     * exercises continuation and a CRLF spanning adjacent worker slices. */
    size_t len = 1024u * 1024u + VIEW_SCAN_BOUND;
    uint8_t *bytes = malloc(len), *got = malloc(len); T(bytes && got);
    memset(bytes, 'a', len); bytes[VIEW_SCAN_BOUND - 1] = '\r'; bytes[VIEW_SCAN_BOUND] = '\n';
    render_backend backend = {0}; T(render_null_backend(&backend) == 0);
    char log[] = "build/cde-clipboard-XXXXXX"; int fd = mkstemp(log); T(fd >= 0); close(fd);
    editor_config cfg = {.initial = bytes, .initial_len = len, .cols = 32, .rows = 8,
        .wrap_mode = -1, .journal_path = log};
    editor *e = NULL; T(editor_open(&e, &cfg, &backend) == 0); T(settle(e) == 0);
    T(press(e, key('a', PLAT_MOD_CTRL, NULL)) == 0);
    T(press(e, key('c', PLAT_MOD_CTRL, NULL)) == 0);
    uint64_t target; T(editor_add_buffer(e, NULL, (const uint8_t *)"target", sizeof "target" - 1, &target) == 0);
    T(settle(e) == 0); T(press(e, key('a', PLAT_MOD_CTRL, NULL)) == 0);
    review_hold hold = {0}; work_handle held = work_submit(&e->pool,
        (work_job){review_hold_job, &hold, 0, WORK_BULK}); T(held.epoch);
    while (!atomic_load(&hold.entered)) sched_yield();
    edit_malloc_guard_begin();
    T(editor_inject(e, &(plat_event){.kind = PLAT_EV_KEY, .press = true,
        .keysym = 'v', .mods = PLAT_MOD_CTRL}) == 0);
    T(editor_step(e, 0) >= 0);
    T(editor_get_stats(e).pending); T(editor_length(e) == sizeof "target" - 1);
    T(e->stats.submitted_sequence < e->stats.input_sequence); /* no unchanged paste acknowledgement */
    /* UI returns while normalization is queued behind unrelated bulk work. */
    atomic_store(&hold.release, true); T(settle(e) == 0);
    T(editor_length(e) == len - 1); T(editor_read(e, 0, got, len - 1) == 0);
    T(!memcmp(got, bytes, VIEW_SCAN_BOUND - 1));
    T(!memcmp(got + VIEW_SCAN_BOUND - 1, bytes + VIEW_SCAN_BOUND, len - VIEW_SCAN_BOUND));
    T(undo_get_history(e->undo).undo_groups == 1); /* one paste command */
    T(press(e, key('z', PLAT_MOD_CTRL, NULL)) == 0);
    T(expect(e, "target", sizeof "target" - 1) == 0);
    T(editor_view(e).selection.anchor == 0);
    size_t allocations = edit_malloc_guard_end();
    if (edit_malloc_guard_active()) T(allocations == 0);
    T(editor_flush(e) == 0);
    editor_close(e); unlink(log); free(got); free(bytes);
    printf("clipboard bulk: worker queue, cross-slice CRLF, replacement/undo, journal, mallocs=%zu passed\n", allocations);
    return 0;
}
static int clipboard_pending_primary_test(void)
{
    /* Cross the existing scan bound so selection capture must use a worker. */
    size_t len = VIEW_SCAN_BOUND + 1;
    uint8_t *bytes = malloc(len), *got = malloc(2u * len); T(bytes && got); memset(bytes, 'p', len);
    render_backend backend = {0}; T(render_null_backend(&backend) == 0);
    editor_config cfg = {.initial = bytes, .initial_len = len, .cols = 32, .rows = 8, .wrap_mode = -1};
    editor *e = NULL; T(editor_open(&e, &cfg, &backend) == 0); T(settle(e) == 0);
    review_hold hold = {0}; work_handle held = work_submit(&e->pool,
        (work_job){review_hold_job, &hold, 0, WORK_BULK}); T(held.epoch);
    while (!atomic_load(&hold.entered)) sched_yield();
    plat_event select = key('a', PLAT_MOD_CTRL, NULL), move = key(XKB_KEY_End, PLAT_MOD_CTRL, NULL);
    plat_event middle = {.kind = PLAT_EV_BUTTON, .press = true, .code = 2,
        .x = (int32_t)(e->lay.gw * e->grid.dims.cell_w),
        .y = (int32_t)(e->tab_rows * e->grid.dims.cell_h)};
    edit_malloc_guard_begin();
    T(editor_inject(e, &select) == 0); T(editor_inject(e, &move) == 0); T(editor_inject(e, &middle) == 0);
    uint64_t deadline = trace_now_ns() + UINT64_C(5000000000); /* existing settle guard */
    while (e->queue_count || view_busy(&e->v) || e->action == EDITOR_ACTION_CLIP) {
        T(editor_step(e, 0) >= 0); T(trace_now_ns() < deadline);
    }
    T(editor_length(e) == len); atomic_store(&hold.release, true); T(settle(e) == 0);
    T(editor_length(e) == 2u * len); T(editor_read(e, 0, got, 2u * len) == 0);
    T(!memcmp(got, bytes, len) && !memcmp(got + len, bytes, len));
    T(press(e, key('z', PLAT_MOD_CTRL, NULL)) == 0); T(editor_length(e) == len);
    size_t allocations = edit_malloc_guard_end(); if (edit_malloc_guard_active()) T(allocations == 0);
    editor_close(e); free(got); free(bytes);
    printf("clipboard PRIMARY: queued selection/collapse/middle paste waits for latest capture, mallocs=%zu passed\n", allocations);
    return 0;
}
static int clipboard_no_tabs_test(void)
{
    const char text[] = "p";
    render_backend backend = {0}; T(render_null_backend(&backend) == 0);
    editor_config cfg = {.initial = (const uint8_t *)text, .initial_len = sizeof text - 1,
        .cols = 32, .rows = 8, .wrap_mode = -1};
    editor *e = NULL; T(editor_open(&e, &cfg, &backend) == 0); T(settle(e) == 0);
    T(press(e, key('a', PLAT_MOD_CTRL, NULL)) == 0); T(press(e, key('c', PLAT_MOD_CTRL, NULL)) == 0);
    T(press(e, key('w', PLAT_MOD_CTRL, NULL)) == 0); T(tabs_count(&e->tabs) == 0);
    uint64_t before = editor_length(e); T(before == 0); /* empty placeholder, no active document */
    plat_event middle = {.kind = PLAT_EV_BUTTON, .press = true, .code = 2,
        .x = (int32_t)(e->lay.gw * e->grid.dims.cell_w),
        .y = (int32_t)(e->tab_rows * e->grid.dims.cell_h)};
    T(press(e, middle) == 0); T(editor_length(e) == before);
    editor_close(e); puts("clipboard middle click: no active tab preserves placeholder"); return 0;
}
/* edit-73j: coordinates derive from the live cell/gutter geometry. Expected
 * offsets derive from literal fixture byte positions; wheel rows from
 * SCROLL_CORE_ROWS (Sublime default requested in the bead). */
static plat_event pointer_event(editor *e, plat_ev_kind kind, uint32_t row,
                                uint32_t column, bool down, uint64_t when)
{
    plat_event ev = {.kind=kind, .code=1, .press=down, .t0_ns=when,
        .x=(int32_t)((column+e->lay.gw)*e->grid.dims.cell_w),
        .y=(int32_t)((row+e->tab_rows)*e->grid.dims.cell_h)};
    return ev;
}
/* edit-48u fixtures (E): geometry reuses mouse_contract's columns/rows.
 * (P) zeros are empty state/first row, ones are adjacent rows/buttons;
 * halves bisect a cell, offsets derive from the literal fixture bytes. */
static int pointer_review_open(editor **out, render_backend *backend, bool wrap)
{
    static const char text[]="alpha beta\nalpha beta\nalpha beta\nalpha beta\n"
        "alpha beta\nalpha beta\nalpha beta\nalpha beta\nalpha beta\nalpha beta\n";
    T(render_null_backend(backend)==0);
    editor_config cfg={.initial=(const uint8_t *)text,.initial_len=sizeof text-1,
        .cols=40,.rows=8,.wrap_mode=wrap ? 1 : -1};
    T(editor_open(out,&cfg,backend)==0); T(settle_map(*out)==0); return 0;
}
static int pointer_overscan_contract(void)
{
    render_backend backend={0}; editor *e=NULL;
    T(pointer_review_open(&e,&backend,false)==0);
    plat_event wheel={.kind=PLAT_EV_WHEEL,.smooth=true,
        .dy=PLAT_WHEEL_UNIT/(int32_t)(SCROLL_CORE_ROWS*2u)}; /* P: half row per notch fraction */
    T(press(e,wheel)==0); T(e->scroll_plan.origin_y_q8<0);
    uint64_t byte=e->row_byte[e->v.config.rows];
    T(byte!=LAYOUT_VOID_ROW);
    plat_event click=pointer_event(e,PLAT_EV_BUTTON,e->v.config.rows-1u,0,true,trace_now_ns());
    click.y+=(int32_t)e->grid.dims.cell_h-1;
    T(press(e,click)==0); T(e->v.state.selection.cursor==byte);
    click.press=false; T(press(e,click)==0);
    editor_close(e); puts("edit-48u overscan passed"); return 0;
}
static int pointer_chain_contract(bool other_button)
{
    render_backend backend={0}; editor *e=NULL;
    T(pointer_review_open(&e,&backend,false)==0);
    plat_event click=pointer_event(e,PLAT_EV_BUTTON,0,0,true,trace_now_ns());
    T(press(e,click)==0); click.press=false; T(press(e,click)==0);
    if (other_button) {
        click.code++; click.press=true; T(press(e,click)==0);
        click.press=false; T(press(e,click)==0); click.code--;
    } else {
        uint64_t id;
        T(editor_add_buffer(e,NULL,(const uint8_t *)"alpha beta\n",sizeof "alpha beta\n"-1,&id)==0);
        T(settle_map(e)==0);
        /* Normal switching already clears time; isolate the missing identity
         * predicate by retaining the prior chain's timestamp. */
        e->click_ns=click.t0_ns;
    }
    click.press=true; T(press(e,click)==0);
    T(e->v.state.selection.cursor==e->v.state.selection.anchor);
    click.press=false; T(press(e,click)==0);
    editor_close(e); puts("edit-48u chain identity passed"); return 0;
}
static int pointer_empty_rows_contract(void)
{
    render_backend backend={0}; editor *e=NULL;
    T(pointer_review_open(&e,&backend,false)==0);
    plat_event click=pointer_event(e,PLAT_EV_BUTTON,0,0,true,trace_now_ns());
    T(press(e,click)==0);
    uint32_t rows=e->v.config.rows; e->v.config.rows=0;
    click.kind=PLAT_EV_MOTION;
    T(editor_inject(e,&click)==0); T(editor_step(e,0)>=0);
    T(!e->text_drag); e->v.config.rows=rows;
    editor_close(e); puts("edit-48u empty rows passed"); return 0;
}
static int findpanel_resize_contract(void)
{
    render_backend backend={0}; editor *e=NULL;
    T(pointer_review_open(&e,&backend,false)==0);
    T(press(e,key('h',PLAT_MOD_CTRL,NULL))==0);
    uint32_t wanted=e->panel_rows;
    plat_event size={.kind=PLAT_EV_RESIZE,.w=e->grid.dims.cols*e->grid.dims.cell_w,
        .h=(e->tab_rows+1u)*e->grid.dims.cell_h}; /* P: strip plus sole text row */
    uint32_t original=e->grid.dims.rows*e->grid.dims.cell_h;
    T(press(e,size)==0); T(e->panel_rows<wanted);
    size.h=original; T(press(e,size)==0); T(e->panel_rows==wanted);
    T(e->v.config.rows==e->grid.dims.rows-e->tab_rows-wanted);
    editor_close(e); puts("edit-48u panel resize passed"); return 0;
}
static int pointer_autoscroll_contract(bool wrap)
{
    render_backend backend={0}; editor *e=NULL;
    T(pointer_review_open(&e,&backend,wrap)==0);
    edit_malloc_guard_begin();
    plat_event click=pointer_event(e,PLAT_EV_BUTTON,0,0,true,trace_now_ns());
    T(press(e,click)==0);
    uint64_t anchor=e->v.state.selection.anchor, top=e->v.state.first_byte;
    click.kind=PLAT_EV_MOTION;
    click.y=(int32_t)((e->tab_rows+e->v.config.rows+1u)*e->grid.dims.cell_h)-1;
    T(editor_inject(e,&click)==0);
    /* E: reuse settle's existing deadline; this is readiness, not a rate budget. */
    uint64_t deadline=trace_now_ns()+UINT64_C(5000000000);
    do { T(editor_step(e,0)>=0); T(trace_now_ns()<deadline); }
    while (e->v.state.first_byte==top || e->v.state.selection.cursor!=e->row_byte[e->v.config.rows-1u]);
    T(e->v.state.first_byte>top);
    T(e->v.state.selection.anchor==anchor && e->v.state.selection.cursor>anchor);
    uint64_t line=e->v.state.first_line;
    T(line==1); /* P: one row-height outside advances one row in the frame. */
    top=e->v.state.first_byte;
    /* No new motion event: the held pointer must keep scrolling. */
    do { T(editor_step(e,0)>=0); T(trace_now_ns()<deadline); }
    while (e->v.state.first_byte==top || e->v.state.selection.cursor!=e->row_byte[e->v.config.rows-1u]);
    T(e->v.state.first_line==line+1u);
    /* P: twice the outside row height reverses twice the displacement. */
    click.y=(int32_t)((int64_t)e->tab_rows*e->grid.dims.cell_h-2*(int64_t)e->grid.dims.cell_h);
    top=e->v.state.first_byte;
    T(editor_inject(e,&click)==0);
    do { T(editor_step(e,0)>=0); T(trace_now_ns()<deadline); }
    while (e->v.state.first_byte==top || e->v.state.selection.cursor!=e->row_byte[0]);
    T(e->v.state.first_line==line-1u);
    T(e->v.state.selection.anchor==anchor);
    click.kind=PLAT_EV_BUTTON; click.press=false;
    T(press(e,click)==0); top=e->v.state.first_byte;
    T(settle(e)==0); T(e->v.state.first_byte==top);
    T(edit_malloc_guard_end()==0);
    editor_close(e); puts("edit-48u drag autoscroll passed"); return 0;
}
/* edit-ywd (P): geometry = query plus the existing replace panel's field
 * and hints rows, one text row, one tab row. Width fits the longest expected
 * label + gap + chord, plus the existing minimap. Fixture offsets are sizeof
 * prefixes; zero is no allocation/no selection/first index. */
static bool palette_grid_text(editor *e, uint32_t row, size_t column, const char *text)
{
    const render_grid *grid = editor_grid(e);
    for (size_t i = 0; text[i]; i++) {
        if (column >= grid->dims.cols || row >= grid->dims.rows) return false;
        const render_cell *cell = &grid->cells[(size_t)row * grid->dims.cols + column++];
        if (text[i] == ' ' && cell->atlas_slot == RENDER_NO_SLOT) continue;
        if (cell->atlas_slot != e->find_ascii[(uint8_t)text[i] - (uint8_t)' ']) return false;
    }
    return true;
}
static int palette_contract(void)
{
    render_backend backend = {0}; T(render_null_backend(&backend) == 0);
    const char initial[] = "palette buffer";
    editor_config cfg = {.initial = (const uint8_t *)initial, .initial_len = sizeof initial - 1u,
        .cols = sizeof "Save As Ctrl+Shift+S" - 1u + 8u,
        .rows = 1u + 1u + 3u, .wrap_mode = -1};
    editor *e = NULL; T(editor_open(&e, &cfg, &backend) == 0); T(settle(e) == 0);
    uint32_t text_rows = e->v.config.rows;
    edit_malloc_guard_begin();
    T(press(e, key('p', PLAT_MOD_CTRL | PLAT_MOD_SHIFT, NULL)) == 0);
    T(e->panel_rows == 3u); /* (P) existing replace/find-in-files panel rows */
    uint32_t first = e->grid.dims.rows - e->panel_rows;
    T(palette_grid_text(e, first, 0, "Command: "));
    T(palette_grid_text(e, first + 1u, 0, "Left"));
    T(palette_grid_text(e, first + 2u, 0, "Right"));
    T(press(e, key(XKB_KEY_Down, 0, NULL)) == 0);
    T(press(e, key(XKB_KEY_Down, 0, NULL)) == 0);
    T(palette_grid_text(e, first + 1u, 0, "Right"));
    T(palette_grid_text(e, first + 2u, 0, "Up"));
    T(press(e, key(XKB_KEY_Up, 0, NULL)) == 0);
    T(press(e, key(XKB_KEY_Up, 0, NULL)) == 0);
    T(press(e, key('s', 0, "sav")) == 0);
    T(palette_grid_text(e, first, 0, "Command: sav"));
    T(palette_grid_text(e, first + 1u, 0, "Save"));
    T(palette_grid_text(e, first + 2u, 0, "Save As"));
    T(palette_grid_text(e, first + 1u, e->grid.dims.cols - (sizeof "Ctrl+S" - 1u), "Ctrl+S"));
    T(palette_grid_text(e, first + 2u, e->grid.dims.cols - (sizeof "Ctrl+Shift+S" - 1u), "Ctrl+Shift+S"));
    T(expect(e, initial, 0) == 0);
    T(press(e, key(XKB_KEY_Escape, 0, NULL)) == 0);
    T(!e->panel_rows && e->v.config.rows == text_rows);
    T(palette_grid_text(e, e->tab_rows, e->lay.gw, initial));
    T(edit_malloc_guard_end() == 0);
    T(press(e, key('s', PLAT_MOD_CTRL | PLAT_MOD_SHIFT, NULL)) == 0);
    T(e->go.active && e->go.kind == KEYS_ACTION_SAVE_AS);
    editor_goto chord = e->go;
    T(press(e, key(XKB_KEY_Escape, 0, NULL)) == 0);
    T(press(e, key('f', PLAT_MOD_CTRL, NULL)) == 0); T(e->find_visible);
    T(press(e, key('p', PLAT_MOD_CTRL | PLAT_MOD_SHIFT, NULL)) == 0);
    T(!e->find_visible);
    T(press(e, key('s', 0, "sav")) == 0);
    T(press(e, key(XKB_KEY_Down, 0, NULL)) == 0);
    T(press(e, key(XKB_KEY_Up, 0, NULL)) == 0);
    T(press(e, key(XKB_KEY_Down, 0, NULL)) == 0);
    T(press(e, key(XKB_KEY_Return, 0, NULL)) == 0);
    T(!e->panel_rows && e->go.active && e->go.kind == chord.kind);
    T(e->go.owner == chord.owner && e->go.generation == chord.generation &&
      e->go.length == chord.length && e->go.cursor == chord.cursor && e->go.selected == chord.selected);
    T(!memcmp(e->go.digits, chord.digits, chord.length));
    T(press(e, key(XKB_KEY_Escape, 0, NULL)) == 0);
    T(press(e, key('p', PLAT_MOD_CTRL | PLAT_MOD_SHIFT, NULL)) == 0);
    /* Owner mutation bypasses key capture (as clipboard/worker edits can). */
    undo_state state = {0}; const uint8_t inserted[] = "!";
    T(editor_find_insert(e, e->undo, 0, inserted, sizeof inserted - 1u,
        trace_now_ns(), &state, &state).changed);
    T(settle(e) == 0); T(!e->panel_rows);
    /* (P) user overrides change the displayed chord and opening stroke. */
    const char preferences[] = "[keymap]\ncommand_palette='Alt+p'\nsave='Alt+s'\nsave_as='Alt+w'\n";
    T(config_parse((const uint8_t *)preferences, sizeof preferences - 1u, &e->preferences, NULL) == CONFIG_OK);
    T(press(e, key('p', PLAT_MOD_CTRL | PLAT_MOD_SHIFT, NULL)) == 0);
    T(!e->panel_rows);
    T(press(e, key('p', PLAT_MOD_ALT, NULL)) == 0);
    T(e->panel_rows);
    T(press(e, key('s', 0, "savx")) == 0);
    T(press(e, key(XKB_KEY_BackSpace, 0, NULL)) == 0);
    T(palette_grid_text(e, first, 0, "Command: sav"));
    T(palette_grid_text(e, first + 1u, e->grid.dims.cols - (sizeof "Alt+S" - 1u), "Alt+S"));
    T(palette_grid_text(e, first + 2u, e->grid.dims.cols - (sizeof "Alt+W" - 1u), "Alt+W"));
    plat_event focus = {.kind = PLAT_EV_FOCUS, .focused = false};
    T(press(e, focus) == 0); T(!e->panel_rows);
    editor_close(e);
    puts("edit-ywd palette: paint/filter/chords, shared Save As, Escape/mutation/focus and malloc guard passed");
    return 0;
}
static int findpanel_escape_contract(void)
{
    render_backend backend={0}; editor *e=NULL;
    T(pointer_review_open(&e,&backend,false)==0);
    T(press(e,key('f',PLAT_MOD_CTRL,NULL))==0);
    T(press(e,key('a',0,"alpha"))==0);
    T(press(e,key(XKB_KEY_Return,0,NULL))==0);
    view_selection match=e->v.state.selection;
    T(match.cursor!=match.anchor);
    T(press(e,key(XKB_KEY_Escape,0,NULL))==0); T(!e->find_visible);
    T(e->v.state.selection.cursor==match.cursor && e->v.state.selection.anchor==match.anchor);
    editor_close(e); puts("edit-48u Escape retains match passed"); return 0;
}
/* edit-m15: geometry reuses mouse_contract. Grow from a short source to a
 * full visible viewport, then exercise stale hits independently of layout. */
static int gutter_click_contract(void)
{
    render_backend backend={0}; T(render_null_backend(&backend)==0);
    const char initial[]="old";
    editor_config cfg={.initial=(const uint8_t *)initial,.initial_len=sizeof initial-1,
        .cols=40,.rows=8,.wrap_mode=-1};
    editor *e=NULL; T(editor_open(&e,&cfg,&backend)==0); T(settle_map(e)==0);
    uint64_t old_eof=editor_length(e);
    T(e->row_byte[1]==LAYOUT_VOID_ROW);
    T(editor_set_cursor(e,old_eof)==0); T(settle(e)==0);
    edit_malloc_guard_begin();
    for (uint32_t r=1;r<e->v.config.rows;r++) {
        T(press(e,key(XKB_KEY_Return,0,NULL))==0);
        T(press(e,key('n',0,"new"))==0);
    }
    T(edit_malloc_guard_end()==0);
    T(settle_map(e)==0);
    const uint32_t row=1;
    uint64_t start=e->row_byte[row]; T(start!=LAYOUT_VOID_ROW && start!=old_eof);
    for (uint32_t c=0;c<e->lay.gw;c++) {
        const editor_hit *hit=&e->hits[(size_t)row*e->max_cols+c];
        T(hit->width && hit->before==start && hit->after==start);
    }
    /* edit-dr3: a gutter click selects the line through its newline (P:
     * the next row's start, every fixture line being one row wide). */
    uint64_t next=e->row_byte[row+1]; T(next!=LAYOUT_VOID_ROW && next>start);
    plat_event click=pointer_event(e,PLAT_EV_BUTTON,row,0,true,trace_now_ns()); click.x=0;
    T(press(e,click)==0); T(editor_view(e).selection.anchor==start && editor_view(e).selection.cursor==next);
    click.press=false; T(press(e,click)==0);
    /* A stale old-EOF hit must never be consulted, even if its width is zero.
     * Reset the click chain to test an ordinary single gutter click. */
    e->click_ns=0;
    e->hits[(size_t)row*e->max_cols]=(editor_hit){old_eof,old_eof,0,1};
    edit_malloc_guard_begin();
    click.press=true; T(press(e,click)==0);
    T(edit_malloc_guard_end()==0);
    T(editor_view(e).selection.anchor==start && editor_view(e).selection.cursor==next);
    editor_close(e); puts("edit-m15 gutter: filled viewport ignores stale old EOF passed"); return 0;
}
/* edit-a7fo: a click on ASCII text places the caret under the pointer, and a
 * drag selects by character, with word wrap on (the file default; Tobias's
 * trial) and off, with the menu bar visible, and at a non-default font size.
 * (P) every fixture line is the 26-byte alphabet plus LF, so line L starts at
 * byte 27*L and text column c of a one-row line is byte 27*L+c. font_px 30 =
 * the default 15 px (editor.h) at Xft.dpi 192 / the 96 dpi base (M: xrdb on
 * the trial display :0 reports Xft.dpi 192). */
static int a7fo_click_contract(int wrap, bool menu, uint32_t font_px)
{
    static const char text[]="abcdefghijklmnopqrstuvwxyz\nabcdefghijklmnopqrstuvwxyz\n"
        "abcdefghijklmnopqrstuvwxyz\nabcdefghijklmnopqrstuvwxyz\n";
    render_backend backend={0}; T(render_null_backend(&backend)==0);
    editor_config cfg={.initial=(const uint8_t *)text,.initial_len=sizeof text-1,
        .cols=40,.rows=8,.font_px=font_px,.wrap_mode=wrap};
    editor *e=NULL; T(editor_open(&e,&cfg,&backend)==0); T(settle_map(e)==0);
    if (menu) { T(editor_menu_configure(e,true)==0); T(settle_map(e)==0); T(e->menu_rows==1u); }
    T(e->v.state.wrap==(wrap>0));
    uint64_t when=trace_now_ns();
    /* Click text column 4 of line 1 ('e'): caret before 'e', no selection. */
    T(press(e,pointer_event(e,PLAT_EV_BUTTON,1,4,true,when))==0);
    T(editor_view(e).selection.cursor==27u+4u && editor_view(e).selection.anchor==27u+4u);
    /* Drag to column 10 of the same line: a character selection, not a line. */
    T(press(e,pointer_event(e,PLAT_EV_MOTION,1,10,false,when))==0);
    T(editor_view(e).selection.anchor==27u+4u && editor_view(e).selection.cursor==27u+10u);
    /* Drag to column 2 of line 2. */
    T(press(e,pointer_event(e,PLAT_EV_MOTION,2,2,false,when))==0);
    T(editor_view(e).selection.anchor==27u+4u && editor_view(e).selection.cursor==54u+2u);
    T(press(e,pointer_event(e,PLAT_EV_BUTTON,2,2,false,when))==0);
    /* A click in the right half of a cell snaps after that character. */
    plat_event right=pointer_event(e,PLAT_EV_BUTTON,0,7,true,when+EDITOR_BLINK_NS+1u);
    right.x+=(int32_t)(e->grid.dims.cell_w-1u);
    T(press(e,right)==0);
    T(editor_view(e).selection.cursor==8u && editor_view(e).selection.anchor==8u);
    right.press=false; T(press(e,right)==0);
    editor_close(e);
    printf("edit-a7fo click placement wrap=%d menu=%d font_px=%u passed\n",wrap,(int)menu,font_px);
    return 0;
}
static int find_follow_before_top_contract(void)
{
    const char text[]="before\nafter\nlast";
    render_backend backend={0}; T(render_null_backend(&backend)==0);
    editor_config cfg={.initial=(const uint8_t *)text,.initial_len=sizeof text-1,
        .cols=40,.rows=8,.wrap_mode=-1};
    editor *e=NULL; T(editor_open(&e,&cfg,&backend)==0); T(settle_map(e)==0);
    /* Inject 3kk's adopted approximate top, after the find anchor. This
     * isolates the large-file/inexact-count state without a worker race. */
    e->buffer->scroll.first_byte=sizeof "before\n"-1;
    e->buffer->scroll.first_line=e->buffer->scroll.anchor_line=1;
    e->buffer->scroll.approximate=true;
    e->buffer->scroll_display=e->buffer->scroll;
    editor_scroll_mirror(e);
    e->v.state.selection.anchor=0; e->v.state.selection.cursor=sizeof "before"-1;
    e->v.state.hscroll=sizeof "before"-1;
    e->wheel_pending=true; e->find_scroll=true;
    T(e->v.state.selection.anchor<e->v.state.first_byte);
    T(editor_step(e,0)==EDITOR_OK); T(e->error==0);
    T(!e->wheel_pending && !e->find_scroll);
    T(e->v.state.hscroll==sizeof "before"-1);
    T(editor_scroll_seek(e,0)==0); T(settle(e)==0);
    T(editor_view(e).first_byte==0 && e->error==0);
    editor_close(e); puts("edit-m15 find: anchor before approximate top is nonfatal passed"); return 0;
}
static int gutter_growth_contract(void)
{
    /* G: the supplied decimal-width transition, 99 -> 100 logical lines.
     * One line's bytes come from the literal; LF separates all but the last. */
    enum { lines=99 };
    const char line[]="text\n";
    uint8_t text[lines*(sizeof line-1)];
    for (size_t i=0;i<lines;i++) memcpy(text+i*(sizeof line-1),line,sizeof line-1);
    render_backend backend={0}; T(render_null_backend(&backend)==0);
    editor_config cfg={.initial=text,.initial_len=sizeof text-1,.cols=40,.rows=8,.wrap_mode=-1};
    editor *e=NULL; T(editor_open(&e,&cfg,&backend)==0); T(settle_map(e)==0);
    uint32_t old_gw=e->lay.gw;
    T(press(e,key(XKB_KEY_End,PLAT_MOD_CTRL,NULL))==0);
    T(press(e,key(XKB_KEY_Return,0,NULL))==0); T(settle_map(e)==0);
    T(e->lay.gw==old_gw+1);
    uint64_t start=e->row_byte[0]; T(start!=LAYOUT_VOID_ROW);
    for (uint32_t c=0;c<e->lay.gw;c++) {
        const editor_hit *hit=&e->hits[c]; T(hit->width && hit->before==start && hit->after==start);
    }
    /* Model an old text-cluster hit in the newly claimed gutter column. */
    e->hits[old_gw]=(editor_hit){start+1,start+2,0,1};
    plat_event click=pointer_event(e,PLAT_EV_BUTTON,0,0,true,trace_now_ns());
    click.x=(int32_t)(old_gw*e->grid.dims.cell_w);
    /* edit-dr3: whole-line selection; one line is the literal's bytes (P). */
    T(press(e,click)==0);
    T(editor_view(e).selection.anchor==start && editor_view(e).selection.cursor==start+sizeof line-1);
    editor_close(e); puts("edit-m15 gutter: decimal widening ignores old text hit passed"); return 0;
}
static int gutter_wrap_contract(void)
{
    /* E: two surface widths of text guarantee a continuation with the
     * minimap/gutter reducing usable text columns. No source byte lookup. */
    uint8_t text[2u*40u]; memset(text,'x',sizeof text);
    render_backend backend={0}; T(render_null_backend(&backend)==0);
    editor_config cfg={.initial=text,.initial_len=sizeof text,.cols=40,.rows=8,.wrap_mode=1};
    editor *e=NULL; T(editor_open(&e,&cfg,&backend)==0); T(settle_map(e)==0);
    T(e->lay.wrap_rows[1].continuation && e->row_byte[1]>0);
    plat_event click=pointer_event(e,PLAT_EV_BUTTON,1,0,true,trace_now_ns()); click.x=0;
    /* edit-dr3: the sole logical line has no newline, so it selects to EOF (P). */
    T(press(e,click)==0); T(editor_view(e).selection.anchor==0 && editor_view(e).selection.cursor==sizeof text);
    for (uint32_t c=0;c<e->lay.gw;c++) {
        const editor_hit *hit=&e->hits[e->max_cols+c]; T(hit->width && hit->before==0 && hit->after==0);
    }
    editor_close(e); puts("edit-m15 gutter: wrapped continuation uses logical line start passed"); return 0;
}
/* edit-dr3: Sublime's gutter rule (docs/reviews/astra-vs-sublime-3.md §15).
 * Geometry reuses pointer_review_open; x=0 is the gutter's first cell (P).
 * L is one fixture line's bytes from the literal; row r starts at r*L (P). */
static plat_event gutter_event(editor *e, plat_ev_kind kind, uint32_t row, bool down, uint16_t mods)
{
    plat_event ev=pointer_event(e,kind,row,0,down,trace_now_ns());
    ev.x=0; ev.mods=mods; return ev;
}
static int gutter_line_contract(void)
{
    const uint64_t L=sizeof "alpha beta\n"-1;
    render_backend backend={0}; editor *e=NULL;
    T(pointer_review_open(&e,&backend,false)==0);
    T(e->row_byte[1]==L && e->row_byte[4]==4u*L);
    edit_malloc_guard_begin();
    /* Click selects the clicked line, newline included. */
    plat_event ev=gutter_event(e,PLAT_EV_BUTTON,1,true,0);
    T(press(e,ev)==0);
    T(e->v.state.selection.anchor==L && e->v.state.selection.cursor==2u*L);
    /* Drag extends by whole lines, in the gutter and over text. */
    ev=gutter_event(e,PLAT_EV_MOTION,3,false,0); T(press(e,ev)==0);
    T(e->v.state.selection.anchor==L && e->v.state.selection.cursor==4u*L);
    ev=pointer_event(e,PLAT_EV_MOTION,3,2,false,trace_now_ns()); T(press(e,ev)==0);
    T(e->v.state.selection.anchor==L && e->v.state.selection.cursor==4u*L);
    ev=gutter_event(e,PLAT_EV_MOTION,0,false,0); T(press(e,ev)==0);
    T(e->v.state.selection.anchor==2u*L && e->v.state.selection.cursor==0);
    ev=gutter_event(e,PLAT_EV_BUTTON,0,false,0); T(press(e,ev)==0);
    /* Shift+click extends from the anchor to the clicked line's far edge. */
    ev=gutter_event(e,PLAT_EV_BUTTON,1,true,0); T(press(e,ev)==0);
    ev.press=false; T(press(e,ev)==0);
    ev=gutter_event(e,PLAT_EV_BUTTON,4,true,PLAT_MOD_SHIFT); T(press(e,ev)==0);
    T(e->v.state.selection.anchor==L && e->v.state.selection.cursor==5u*L);
    ev.press=false; T(press(e,ev)==0);
    ev=gutter_event(e,PLAT_EV_BUTTON,0,true,PLAT_MOD_SHIFT); T(press(e,ev)==0);
    T(e->v.state.selection.anchor==L && e->v.state.selection.cursor==0);
    ev.press=false; T(press(e,ev)==0);
    T(edit_malloc_guard_end()==0);
    editor_close(e);
    /* The last line without a trailing newline selects to EOF. */
    static const char tail[]="one\ntwo";
    T(render_null_backend(&backend)==0);
    editor_config cfg={.initial=(const uint8_t *)tail,.initial_len=sizeof tail-1,.cols=40,.rows=8,.wrap_mode=-1};
    T(editor_open(&e,&cfg,&backend)==0); T(settle_map(e)==0);
    ev=gutter_event(e,PLAT_EV_BUTTON,1,true,0); T(press(e,ev)==0);
    T(e->v.state.selection.anchor==sizeof "one\n"-1 && e->v.state.selection.cursor==sizeof tail-1);
    editor_close(e); puts("edit-dr3 gutter: click/drag/Shift+click select whole lines passed"); return 0;
}
static int mouse_contract(void)
{
    const char text[]="alpha beta\n\tA界é z\nlast";
    render_backend backend={0}; T(render_null_backend(&backend)==0);
    editor_config cfg={.initial=(const uint8_t *)text,.initial_len=sizeof text-1,
        .cols=40,.rows=8,.wrap_mode=-1};
    editor *e=NULL; T(editor_open(&e,&cfg,&backend)==0); T(settle(e)==0);
    uint64_t when=trace_now_ns();
    T(press(e,pointer_event(e,PLAT_EV_BUTTON,0,2,true,when))==0);
    T(editor_view(e).selection.cursor==2 && editor_view(e).selection.anchor==2);
    T(press(e,pointer_event(e,PLAT_EV_MOTION,0,8,false,when))==0);
    T(editor_view(e).selection.cursor==8 && editor_view(e).selection.anchor==2);
    T(press(e,pointer_event(e,PLAT_EV_BUTTON,0,8,false,when))==0);
    /* A distinct location resets the click chain. */
    T(press(e,pointer_event(e,PLAT_EV_BUTTON,0,7,true,when))==0);
    T(press(e,pointer_event(e,PLAT_EV_BUTTON,0,7,false,when))==0);
    T(press(e,pointer_event(e,PLAT_EV_BUTTON,0,7,true,when))==0);
    T(editor_view(e).selection.anchor==6 && editor_view(e).selection.cursor==10);
    T(press(e,pointer_event(e,PLAT_EV_MOTION,0,2,false,when))==0);
    T(editor_view(e).selection.anchor==10 && editor_view(e).selection.cursor==0);
    T(press(e,pointer_event(e,PLAT_EV_BUTTON,0,7,false,when))==0);
    T(press(e,pointer_event(e,PLAT_EV_BUTTON,0,7,true,when))==0);
    T(editor_view(e).selection.anchor==0 && editor_view(e).selection.cursor==11);
    T(press(e,pointer_event(e,PLAT_EV_MOTION,1,5,false,when))==0);
    T(editor_view(e).selection.anchor==0 && editor_view(e).selection.cursor==22);
    T(press(e,pointer_event(e,PLAT_EV_BUTTON,0,7,false,when))==0);
    T(press(e,pointer_event(e,PLAT_EV_BUTTON,1,2,true,when))==0);
    T(editor_view(e).selection.cursor==12); /* midpoint of HT snaps after HT */
    T(press(e,pointer_event(e,PLAT_EV_BUTTON,1,2,false,when))==0);
    T(press(e,pointer_event(e,PLAT_EV_BUTTON,1,5,true,when))==0);
    T(editor_view(e).selection.cursor==13); /* leading CJK cell */
    T(press(e,pointer_event(e,PLAT_EV_BUTTON,1,5,false,when))==0);
    T(press(e,pointer_event(e,PLAT_EV_BUTTON,1,6,true,when))==0);
    T(editor_view(e).selection.cursor==16); /* trailing CJK cell snaps after */
    T(press(e,pointer_event(e,PLAT_EV_BUTTON,1,6,false,when))==0);
    T(press(e,pointer_event(e,PLAT_EV_BUTTON,1,8,true,when))==0);
    T(editor_view(e).selection.cursor==19); /* combining cluster stays intact */
    T(press(e,pointer_event(e,PLAT_EV_BUTTON,1,8,false,when))==0);
    e->v.state.hscroll=sizeof "alpha "-1; T(editor_full_layout(e)==0); T(settle(e)==0);
    T(press(e,pointer_event(e,PLAT_EV_BUTTON,0,0,true,when))==0);
    T(editor_view(e).selection.cursor==sizeof "alpha "-1);
    T(press(e,pointer_event(e,PLAT_EV_BUTTON,0,0,false,when))==0);
    plat_event shifted=pointer_event(e,PLAT_EV_BUTTON,0,3,true,when); shifted.mods=PLAT_MOD_SHIFT;
    T(press(e,shifted)==0);
    T(editor_view(e).selection.anchor==sizeof "alpha "-1 && editor_view(e).selection.cursor==sizeof "alpha bet"-1);
    editor_close(e); puts("edit-73j mouse contract passed"); return 0;
}
static int wheel_contract(void)
{
    const char text[]="a\nb\nc\nd\ne\nf\ng\nh\ni\nj\nk\nl\nm\nn\no\np\nq\nr\ns\nt";
    render_backend backend={0}; T(render_null_backend(&backend)==0);
    editor_config cfg={.initial=(const uint8_t *)text,.initial_len=sizeof text-1,.cols=40,.rows=8,.wrap_mode=-1};
    editor *e=NULL; T(editor_open(&e,&cfg,&backend)==0); T(settle(e)==0);
    view_selection before=editor_view(e).selection;
    plat_event ev={.kind=PLAT_EV_WHEEL,.dy=PLAT_WHEEL_UNIT};
    T(press(e,ev)==0); T(editor_view(e).first_line==3 && editor_view(e).first_byte==6);
    T(editor_view(e).selection.cursor==before.cursor && editor_view(e).selection.anchor==before.anchor);
    ev.dy=-PLAT_WHEEL_UNIT; T(press(e,ev)==0); T(editor_view(e).first_line==0);
    ev.dy=PLAT_WHEEL_UNIT/2; ev.smooth=true;
    T(press(e,ev)==0); T(editor_view(e).first_line==1);
    T(press(e,ev)==0); T(editor_view(e).first_line==3);
    size_t length=(size_t)e->text_grid.dims.cols*e->text_grid.dims.rows*2u; /* two viewport areas */
    editor_close(e); memset(&backend,0,sizeof backend); T(render_null_backend(&backend)==0);
    uint8_t *wrapped=malloc(length); T(wrapped); memset(wrapped,'x',length);
    cfg.initial=wrapped; cfg.initial_len=length; cfg.wrap_mode=1;
    T(editor_open(&e,&cfg,&backend)==0); free(wrapped); T(settle(e)==0);
    uint64_t target=e->lay.wrap_rows[SCROLL_CORE_ROWS].start;
    ev.dy=PLAT_WHEEL_UNIT; ev.smooth=false;
    T(press(e,ev)==0); T(editor_view(e).first_line==0 && editor_view(e).visual_byte==target);
    T(editor_view(e).selection.cursor==0 && editor_view(e).selection.anchor==0);
    ev.dy=-PLAT_WHEEL_UNIT; T(press(e,ev)==0); T(editor_view(e).visual_byte==0);
    editor_close(e); puts("edit-73j wheel contract passed"); return 0;
}
/* (P) fixture offsets are prefix string lengths. Geometry is the existing
 * minimap plus tab slot, and tab/document/query/replacement/status rows.
 * Zero is success/empty/first byte; one is a C terminator or API direction.
 * No timing or allocation budget is introduced. */
/* edit-djg (P): geometry reuses g02's tab/document/query/replacement/status
 * rows and tab-slot/minimap columns. Compare every panel cell to the same
 * composition without the recovery notice; zero denotes the first cell. */
static int recovery_notice_findpanel(void)
{
    const char notice[] = "file changed on disk since this session; its unsaved edits were set aside";
    render_backend backend = {0}; T(render_null_backend(&backend) == 0);
    editor_config cfg = {.cols = 16u + 8u, .rows = 1u + 1u + 3u};
    editor *e = NULL; T(editor_open(&e, &cfg, &backend) == 0); T(settle(e) == 0);
    e->recovery_notice = notice; e->recovery_notice_mutations = e->stats.mutations;
    T(editor_full_layout(e) == 0); T(settle(e) == 0);
    T(editor_grid(e)->cells[(size_t)(e->grid.dims.rows - 1u) * e->grid.dims.cols].glyph_index == (uint32_t)notice[0]);
    T(press(e, key(XKB_KEY_f, PLAT_MOD_CTRL, NULL)) == 0); T(e->panel_rows);
    const render_grid *g = editor_grid(e);
    size_t first = (size_t)(g->dims.rows - e->panel_rows) * g->dims.cols;
    size_t cells = (size_t)e->panel_rows * g->dims.cols;
    render_cell *with_notice = malloc(cells * sizeof *with_notice); T(with_notice);
    memcpy(with_notice, g->cells + first, cells * sizeof *with_notice);
    e->recovery_notice = NULL; T(editor_full_layout(e) == 0); T(settle(e) == 0);
    bool intact = !memcmp(with_notice, g->cells + first, cells * sizeof *with_notice);
    free(with_notice);
    printf("edit-djg: recovery notice with find panel: panel_cells_intact=%s\n", intact ? "yes" : "no");
    if (!intact) editor_close(e);
    T(intact);
    e->recovery_notice = notice; T(editor_full_layout(e) == 0); T(settle(e) == 0);
    T(press(e, key(XKB_KEY_Escape, 0, NULL)) == 0); T(!e->panel_rows);
    T(g->cells[(size_t)(g->dims.rows - 1u) * g->dims.cols].glyph_index == (uint32_t)notice[0]);
    T(press(e, key(XKB_KEY_x, 0, "x")) == 0); T(!e->recovery_notice);
    editor_close(e); return 0;
}
static int g02_find_return(void)
{
    static const char text[]="cat cat cat", query[]="cat";
    render_backend backend={0}; T(render_null_backend(&backend)==0);
    counted counts={0};
    editor_config cfg={.initial=(const uint8_t *)text,.initial_len=sizeof text-1u,
        .cols=8u+16u,.rows=1u+1u+3u,
        .hook_ctx=&counts,.on_ingress=ingress,.on_submit=submitted};
    editor *e=NULL; T(editor_open(&e,&cfg,&backend)==0); T(settle(e)==0);
    T(press(e,key(XKB_KEY_f,PLAT_MOD_CTRL,NULL))==0);
    T(press(e,key(XKB_KEY_c,0,query))==0);
    T(press(e,key(XKB_KEY_Return,0,NULL))==0); T(e->find_visible);
    T(editor_view(e).selection.anchor==sizeof "cat "-1u &&
      editor_view(e).selection.cursor==sizeof "cat cat"-1u);
    T(press(e,key(XKB_KEY_KP_Enter,0,NULL))==0); T(e->find_visible);
    T(editor_view(e).selection.anchor==sizeof "cat cat "-1u &&
      editor_view(e).selection.cursor==sizeof text-1u);
    T(press(e,key(XKB_KEY_Return,0,NULL))==0); T(e->find_visible);
    T(editor_view(e).selection.anchor==0 && editor_view(e).selection.cursor==sizeof query-1u);
    T(press(e,key(XKB_KEY_Return,PLAT_MOD_SHIFT,NULL))==0); T(e->find_visible);
    T(editor_view(e).selection.anchor==sizeof "cat cat "-1u &&
      editor_view(e).selection.cursor==sizeof text-1u);
    /* A fresh query has an internal preview even before search completes;
     * Return must still advance when its selection is serviced later. */
    T(findui_set_query(&e->find,(const uint8_t *)"dog",sizeof "dog"-1u)==FINDUI_OK);
    T(settle(e)==0);
    view_selection before=editor_view(e).selection;
    T(press(e,key(XKB_KEY_Return,0,NULL))==0); T(e->find_visible);
    T(editor_view(e).selection.anchor==before.anchor && editor_view(e).selection.cursor==before.cursor);
    T(findui_set_query(&e->find,(const uint8_t *)query,sizeof query-1u)==FINDUI_OK);
    T(findui_get_state(&e->find).searching);
    plat_event enter=key(XKB_KEY_Return,0,NULL);
    T(editor_find_key(e,&enter)); T(settle(e)==0); T(e->find_visible);
    T(editor_view(e).selection.anchor==sizeof "cat "-1u &&
      editor_view(e).selection.cursor==sizeof "cat cat"-1u);
    T(press(e,key(XKB_KEY_Escape,0,NULL))==0); T(!e->find_visible);
    T(counts.allocations==0 && !counts.active);
    editor_close(e); puts("edit-g02 Return/keypad/previous/wrap/pending/empty/Escape passed"); return 0;
}
static int g02_find_previous_uncached(void)
{
    /* (P) one literal-byte match per byte; cache capacity plus the query's
     * byte length puts the previous wrapped match just beyond the cache.
     * Geometry and scalar sentinels follow g02_find_return above. */
    uint8_t text[LAYOUT_CLUSTER_CACHE+sizeof "a"-1u]; memset(text,'a',sizeof text);
    render_backend backend={0}; T(render_null_backend(&backend)==0);
    editor_config cfg={.initial=text,.initial_len=sizeof text,.cols=8u+16u,.rows=1u+1u+3u};
    editor *e=NULL; T(editor_open(&e,&cfg,&backend)==0); T(settle(e)==0);
    T(press(e,key(XKB_KEY_f,PLAT_MOD_CTRL,NULL))==0);
    T(press(e,key(XKB_KEY_a,0,"a"))==0);
    T(findui_get_state(&e->find).cache_overflow);
    T(press(e,key(XKB_KEY_KP_Enter,PLAT_MOD_SHIFT,NULL))==0); T(e->find_visible);
    T(editor_view(e).selection.anchor==sizeof text-(sizeof "a"-1u) &&
      editor_view(e).selection.cursor==sizeof text);
    editor_close(e); puts("edit-g02 uncached Shift+keypad Enter selects previous exactly once passed"); return 0;
}
static int findpanel_contract(void)
{
    const char text[]="cat dog cat";
    render_backend backend={0}; T(render_null_backend(&backend)==0);
    editor_config cfg={.initial=(const uint8_t *)text,.initial_len=sizeof text-1,.cols=40,.rows=8};
    char log[]="build/73j-find-XXXXXX"; int fd=mkstemp(log); T(fd>=0); close(fd); cfg.journal_path=log;
    counted counts={0}; cfg.hook_ctx=&counts; cfg.on_ingress=ingress; cfg.on_submit=submitted; cfg.on_io=io_boundary;
    editor *e=NULL; T(editor_open(&e,&cfg,&backend)==0); T(settle(e)==0);
    T(press(e,key('f',PLAT_MOD_CTRL,NULL))==0);
    const render_grid *grid=editor_grid(e);
    T(grid->cells[(size_t)(grid->dims.rows-1)*grid->dims.cols].glyph_index=='F');
    T(press(e,key('c',0,"cat"))==0); T(expect(e,text,0)==0);
    T(press(e,key('a',PLAT_MOD_CTRL,NULL))==0); T(e->find_field_all);
    T(press(e,key('c',0,"cat"))==0); T(expect(e,text,0)==0);
    T(findui_get_state(&e->find).query_length==sizeof "cat"-1);
    /* (P) match offsets derive from the fixture prefixes. Return advances
     * past the initial preview; keymap navigation retains its own contract. */
    T(press(e,key(XKB_KEY_Return,0,NULL))==0);
    T(editor_view(e).selection.anchor==sizeof "cat dog "-1u && editor_view(e).selection.cursor==sizeof text-1u);
    T(press(e,key(XKB_KEY_Return,0,NULL))==0);
    T(editor_view(e).selection.anchor==0 && editor_view(e).selection.cursor==sizeof "cat"-1u);
    T(press(e,key(XKB_KEY_Return,PLAT_MOD_SHIFT,NULL))==0);
    T(editor_view(e).selection.anchor==sizeof "cat dog "-1u && editor_view(e).selection.cursor==sizeof text-1u);
    T(press(e,key(XKB_KEY_Escape,0,NULL))==0);
    T(press(e,key(XKB_KEY_F3,0,NULL))==0);
    T(editor_view(e).selection.anchor==0 && editor_view(e).selection.cursor==sizeof "cat"-1u);
    T(press(e,key(XKB_KEY_F3,0,NULL))==0);
    T(editor_view(e).selection.anchor==8 && editor_view(e).selection.cursor==11);
    T(press(e,key('h',PLAT_MOD_CTRL,NULL))==0);
    T(press(e,key(XKB_KEY_Tab,0,NULL))==0);
    T(press(e,key('x',0,"fox"))==0);
    T(press(e,key(XKB_KEY_Return,PLAT_MOD_CTRL,NULL))==0);
    T(editor_length(e)==sizeof text-1);
    uint8_t bytes[sizeof text]; T(editor_read(e,0,bytes,sizeof text-1)==0);
    T(!memcmp(bytes,"cat dog fox",sizeof text-1));
    T(press(e,key(XKB_KEY_Return,PLAT_MOD_CTRL|PLAT_MOD_ALT,NULL))==0);
    T(editor_read(e,0,bytes,sizeof text-1)==0); T(!memcmp(bytes,"fox dog fox",sizeof text-1));
    T(press(e,key(XKB_KEY_Escape,0,NULL))==0);
    T(press(e,key('z',PLAT_MOD_CTRL,NULL))==0);
    T(editor_read(e,0,bytes,sizeof text-1)==0); T(!memcmp(bytes,"cat dog fox",sizeof text-1));
    T(press(e,key('Z',PLAT_MOD_CTRL|PLAT_MOD_SHIFT,NULL))==0);
    T(editor_read(e,0,bytes,sizeof text-1)==0); T(!memcmp(bytes,"fox dog fox",sizeof text-1));
    T(press(e,key('z',PLAT_MOD_CTRL,NULL))==0);
    T(press(e,key('z',PLAT_MOD_CTRL,NULL))==0);
    T(editor_read(e,0,bytes,sizeof text-1)==0); T(!memcmp(bytes,text,sizeof text-1));
    /* Replace all in one group across disjoint offsets, with a shorter value. */
    T(press(e,key('h',PLAT_MOD_CTRL,NULL))==0);
    (void)findui_set_replacement(&e->find,(const uint8_t *)"x",sizeof "x"-1,sizeof "x"-1);
    T(press(e,key(XKB_KEY_Return,PLAT_MOD_CTRL|PLAT_MOD_ALT,NULL))==0);
    T(editor_length(e)==sizeof "x dog x"-1);
    T(press(e,key(XKB_KEY_Escape,0,NULL))==0);
    T(press(e,key('z',PLAT_MOD_CTRL,NULL))==0);
    T(editor_read(e,0,bytes,sizeof text-1)==0); T(!memcmp(bytes,text,sizeof text-1));
    T(counts.allocations==0 && !counts.active); T(editor_flush(e)==0); editor_close(e);
    replay_model model={0}; journal_replay_result result;
    T(journal_replay_file(log,replay,&model,&result)==0); unlink(log);
    T(model.len==sizeof text-1 && !memcmp(model.bytes,text,sizeof text-1));
    puts("edit-73j find/replace, disjoint undo/redo and journal recovery passed"); return 0;
}
static int findpanel_cancel_error_contract(void)
{
    /* G: fits the reserved match cache; the cancellation step deliberately
     * applies one API budget unit before Escape, so the prefix is deterministic. */
    const size_t matches=LAYOUT_CLUSTER_CACHE/2u, length=matches*2u;
    uint8_t *text=malloc(length); T(text);
    for (size_t i=0;i<length;i++) text[i]=i%2u ? ' ' : 'a';
    char log[]="build/73j-cancel-XXXXXX"; int fd=mkstemp(log); T(fd>=0); close(fd);
    render_backend backend={0}; T(render_null_backend(&backend)==0);
    editor_config cfg={.initial=text,.initial_len=length,.cols=40,.rows=8,.journal_path=log};
    editor *e=NULL; T(editor_open(&e,&cfg,&backend)==0); T(settle(e)==0);
    T(press(e,key('h',PLAT_MOD_CTRL,NULL))==0); T(press(e,key('a',0,"a"))==0);
    T(findui_get_state(&e->find).match_count==matches);
    T(findui_set_replacement(&e->find,(const uint8_t *)"b",sizeof "b"-1,sizeof "b"-1)==FINDUI_OK);
    plat_event all=key(XKB_KEY_Return,PLAT_MOD_CTRL|PLAT_MOD_ALT,NULL);
    T(editor_find_key(e,&all)); T(e->find_replace_active);
    size_t replaced=0;
    T(findui_replace_step(&e->find,1,0,&replaced)==FINDUI_MORE && replaced==1);
    T(press(e,key(XKB_KEY_Escape,0,NULL))==0);
    T(!e->find_replace_active); T(undo_get_history(e->undo).undo_groups==1);
    uint8_t *actual=malloc(length); T(actual); T(editor_read(e,0,actual,length)==0);
    T(actual[length-2u]=='b'); actual[length-2u]='a'; T(!memcmp(actual,text,length));
    T(press(e,key('z',PLAT_MOD_CTRL,NULL))==0); T(editor_read(e,0,actual,length)==0);
    T(!memcmp(actual,text,length));
    T(press(e,key('h',PLAT_MOD_CTRL,NULL))==0);
    uint64_t mutations=e->stats.mutations;
    /* G: the next allocator request, irrespective of size/free-list class. */
    e->buffer->piece_storage.fail_request=e->buffer->piece_storage.requests+1u;
    T(press(e,all)==0);
    e->buffer->piece_storage.fail_request=0;
    T(!e->find_replace_active && e->find_error==FINDUI_ERR_UNDO);
    /* This injected failure occurs after the successful delete prefix; the
     * host must retain that prefix as an undoable/recoverable group. */
    T(e->stats.mutations>mutations && editor_length(e)==length-1u);
    T(undo_get_history(e->undo).undo_groups==1);
    T(press(e,key(XKB_KEY_Escape,0,NULL))==0);
    T(press(e,key('z',PLAT_MOD_CTRL,NULL))==0);
    T(editor_read(e,0,actual,length)==0); T(!memcmp(actual,text,length));
    T(press(e,key('h',PLAT_MOD_CTRL,NULL))==0);
    T(press(e,key('r',PLAT_MOD_ALT,NULL))==0);
    T(press(e,key(XKB_KEY_Home,0,NULL))==0); T(press(e,key(XKB_KEY_Delete,0,NULL))==0);
    T(press(e,key('[',0,"["))==0);
    T(findui_get_state(&e->find).search_error!=FIND_OK);
    T(press(e,all)==0); T(e->find_replace_request==0 && !e->find_replace_active);
    T(press(e,key(XKB_KEY_Escape,0,NULL))==0);
    T(editor_flush(e)==0); editor_close(e);
    review_bytes replayed={actual,0,length}; journal_replay_result result;
    T(journal_replay_file(log,review_replay_bytes,&replayed,&result)==0);
    T(replayed.len==length && !memcmp(actual,text,length));
    unlink(log); free(actual); free(text);
    puts("edit-73j replacement Escape/error prefixes, undo and recovery passed"); return 0;
}
static int findpanel_far_wrap_contract(void)
{
    editor_config cfg={.cols=40,.rows=8,.wrap_mode=1};
    /* G: two configured viewport cell areas, subtracting the existing minimap
     * and tab-strip geometry; the unique match lies beyond the first screen. */
    size_t length=(size_t)(cfg.cols-8u)*(cfg.rows-1u)*2u;
    uint8_t *text=malloc(length); T(text); memset(text,'x',length);
    const char needle[]="token"; size_t at=length-(sizeof needle-1u);
    memcpy(text+at,needle,sizeof needle-1u);
    cfg.initial=text; cfg.initial_len=length;
    render_backend backend={0}; T(render_null_backend(&backend)==0);
    editor *e=NULL; T(editor_open(&e,&cfg,&backend)==0); T(settle(e)==0);
    T(press(e,key('f',PLAT_MOD_CTRL,NULL))==0); T(press(e,key('t',0,needle))==0);
    T(press(e,key(XKB_KEY_Return,0,NULL))==0);
    T(editor_view(e).selection.anchor==at && editor_view(e).selection.cursor==length);
    T(editor_view(e).visual_byte>0);
    T(e->lay.wrap_rows[0].start<=at && e->lay.wrap_rows[0].end>=length);
    editor_close(e); memset(&backend,0,sizeof backend); T(render_null_backend(&backend)==0);
    cfg.wrap_mode=-1;
    T(editor_open(&e,&cfg,&backend)==0); free(text); T(settle(e)==0);
    T(press(e,key('f',PLAT_MOD_CTRL,NULL))==0); T(press(e,key('t',0,needle))==0);
    T(press(e,key(XKB_KEY_Return,0,NULL))==0);
    T(editor_view(e).selection.anchor==at && editor_view(e).selection.cursor==length);
    T(editor_view(e).hscroll>0);
    editor_close(e); puts("edit-73j distant wrapped/clipped find results are visible passed"); return 0;
}
static int unicode_editor_contract(void)
{
    const char text[]="é é 界";
    render_backend backend={0}; T(render_null_backend(&backend)==0);
    editor_config cfg={.initial=(const uint8_t *)text,.initial_len=sizeof text-1,.cols=40,.rows=8};
    editor *e=NULL; T(editor_open(&e,&cfg,&backend)==0); T(settle(e)==0);
    const render_cell *cell=&e->text_grid.cells[e->lay.gw];
    T(cell->atlas_slot>=LAYOUT_ASCII_GLYPHS && cell->atlas_slot!=RENDER_NO_SLOT);
    T(e->grid.glyphs==e->text_grid.glyphs && e->grid.pages==e->text_grid.pages);
    const render_glyph *image=&e->grid.glyphs[cell->atlas_slot];
    T(e->grid.page_count>image->page); /* page placement belongs to the edit-08z owner */
    const render_atlas_page *page=&e->grid.pages[image->page]; bool ink=false;
    for (uint32_t y=0;y<image->h;y++) for (uint32_t x=0;x<image->w;x++)
        ink |= page->pixels[(image->y+y)*page->stride+image->x+x]!=0;
    T(ink);
    const render_cell *combined=&e->text_grid.cells[e->lay.gw+2u]; /* é, space, e+mark */
    T(combined->atlas_slot>=LAYOUT_ASCII_GLYPHS && combined->atlas_slot!=RENDER_NO_SLOT);
    const render_cell *wide=&e->text_grid.cells[e->lay.gw+4u]; /* preceding clusters occupy four cells */
    T((wide->attrs&RENDER_ATTR_WIDE_LEFT)!=0);
    /* The wide glyph's image depends on an installed CJK fallback (edit-08z owner). */
    /* Unicode tab titles: owned by edit-08z chrome lookup; a path title such as
     * "73j-å文" currently renders blank after its ASCII prefix on main too. */
    editor_close(e); puts("edit-73j production Unicode body atlas contract passed"); return 0;
}
static int minimap_protected_contract(void)
{
    /* G: reuse two index chunks; the viewport occupies the first, and the
     * second supplies a dispersed minimap sample. Protect one OS page only. */
    size_t length=2u*LINEIDX_CHUNK;
    uint8_t *text=malloc(length); T(text);
    for (size_t i=0;i<length;i++) text[i]=i%MINIMAP_LINE_CAP==MINIMAP_LINE_CAP-1u ? '\n' : 'x';
    char path[]="build/73j-map-XXXXXX"; int fd=mkstemp(path); T(fd>=0);
    T(write(fd,text,length)==(ssize_t)length); close(fd); free(text);
    render_backend backend={0}; T(render_null_backend(&backend)==0);
    editor_config cfg={.path=path,.copy_threshold=1,.cols=40,.rows=8,.wrap_mode=-1};
    editor *e=NULL; T(editor_open(&e,&cfg,&backend)==0);
    uint64_t deadline=trace_now_ns()+UINT64_C(5000000000); /* existing settle bound */
    while (e->buffer->pending_open || !editor_index_complete(e) ||
        !e->buffer->map.filled || e->buffer->map.stale || e->dirty || e->buffer->map_snapshot) {
        T(editor_step(e,0)>=0); T(trace_now_ns()<deadline); sched_yield();
    }
    review_hold hold={0}; work_handle handle=work_submit(&e->pool,(work_job){review_hold_job,&hold,0,WORK_BULK}); T(handle.epoch);
    while (!atomic_load(&hold.entered)) { T(trace_now_ns()<deadline); sched_yield(); }
    piece_snapshot *snapshot=piece_snapshot_take(e->tree); T(snapshot);
    const uint8_t *bytes=NULL; size_t size=0;
    T(file_snapshot_bytes(snapshot,NULL,&bytes,&size)==1 && size==length);
    long page=sysconf(_SC_PAGESIZE); T(page>0);
    uint8_t *protected_page=(uint8_t *)(uintptr_t)(bytes+LINEIDX_CHUNK);
    T((uintptr_t)protected_page%(uintptr_t)page==0);
    T(mprotect(protected_page,(size_t)page,PROT_NONE)==0);
    T(editor_begin_frame(e)==0); e->buffer->map.revision++;
    /* Calling compose directly excludes the deliberately blocked worker and
     * isolates the UI cached-publication contract from text layout. */
    int rc=editor_compose(e);
    T(mprotect(protected_page,(size_t)page,PROT_READ)==0);
    T(rc==0); atomic_store(&hold.release,true);
    piece_snapshot_release(snapshot); editor_close(e); unlink(path);
    puts("edit-73j protected remote minimap sample: UI compose passed"); return 0;
}
static int minimap_pending_contract(void)
{
    const char text[]="a\nb\nc\nd\ne\nf\ng\nh\ni\nj\nk\nl\nm\nn\no\np";
    render_backend backend={0}; T(render_null_backend(&backend)==0);
    editor_config cfg={.initial=(const uint8_t *)text,.initial_len=sizeof text-1,.cols=40,.rows=8,.wrap_mode=-1};
    editor *e=NULL; T(editor_open(&e,&cfg,&backend)==0); T(settle(e)==0);
    /* A pending index must still navigate; no exact cache/index query needed. */
    lineidx_destroy(e->buffer->index); e->buffer->index=lineidx_create(editor_length(e)); T(e->buffer->index);
    e->buffer->lg.lines_exact=false;
    plat_event ev={.kind=PLAT_EV_BUTTON,.code=1,.press=true,
        .x=(int32_t)(e->text_grid.dims.cols*e->grid.dims.cell_w),
        .y=(int32_t)((e->grid.dims.rows-1)*e->grid.dims.cell_h)};
    T(press(e,ev)==0); T(editor_view(e).first_byte>0);
    editor_close(e); puts("edit-73j pending minimap navigation passed"); return 0;
}

/* edit-o20: (P) offsets/counts derive from fixture bytes, tab count before
 * the command, and viewport rows. Geometry reuses default_goto's chrome
 * plus text rows, expanded for the existing find panel. No timing budget. */
static int o20_path(editor *e, const char *path)
{
    for (size_t i = 0; path[i]; i++) {
        char ch[] = {path[i], '\0'};
        T(press(e, key((uint8_t)path[i], 0, ch)) == 0);
    }
    return press(e, key(XKB_KEY_Return, 0, NULL));
}
static int o20_case(const char *command)
{
    for (bool panel = false;; panel = true) {
        char dir[] = "/tmp/edit-o20-XXXXXX"; T(mkdtemp(dir) != NULL);
        char path[IPC_PATH_CAP], target[IPC_PATH_CAP], log[IPC_PATH_CAP];
        T(snprintf(path, sizeof path, "%s/open.txt", dir) > 0);
        T(snprintf(target, sizeof target, "%s/save.txt", dir) > 0);
        T(snprintf(log, sizeof log, "%s/journal", dir) > 0);
        static const char text[] = "alpha\nbeta\ngamma\ndelta\nepsilon\nzeta\neta\ntheta\niota\nkappa\n";
        int fd = open(path, O_CREAT | O_WRONLY, S_IRUSR | S_IWUSR); T(fd >= 0);
        T(write(fd, text, sizeof text - 1u) == (ssize_t)(sizeof text - 1u)); T(close(fd) == 0);
        if (!strcmp(command, "save-shared")) {
            fd = open(target, O_CREAT | O_WRONLY, S_IRUSR | S_IWUSR); T(fd >= 0);
            T(write(fd, text, sizeof text - 1u) == (ssize_t)(sizeof text - 1u)); T(close(fd) == 0);
        }
        render_backend backend = {0}; T(render_null_backend(&backend) == 0);
        counted count = {0};
        editor_config cfg = {.initial = (const uint8_t *)text, .initial_len = sizeof text - 1u,
            .cols = sizeof "Go to line: :18446744073709551615" + 1u,
            .rows = sizeof "text" + 1u, .wrap_mode = -1, .journal_path = log,
            .hook_ctx = &count, .on_ingress = ingress, .on_submit = submitted};
        if (!strcmp(command, "save-source") || !strcmp(command, "save-mapped")) cfg.path = path;
        /* (P) force MMAP with a threshold below this fixture's byte length. */
        if (!strcmp(command, "save-mapped")) cfg.copy_threshold = sizeof text - sizeof "a";
        if (!strcmp(command, "scroll-wrap")) cfg.wrap_mode = 1;
        editor *e = NULL; T(editor_open(&e, &cfg, &backend) == 0); T(settle(e) == 0);
        if (!strcmp(command, "save-shared")) {
            uint64_t id; T(editor_add_buffer(e, target, NULL, 0, &id) == 0); T(settle(e) == 0);
            T(editor_select_tab(e, 0) == 0); T(settle(e) == 0);
        }
        if (panel) { T(press(e, key(XKB_KEY_f, PLAT_MOD_CTRL, NULL)) == 0); T(e->find_visible); }
        size_t tabs = tabs_count(&e->tabs), history = e->buffer->history_cursor;
        uint64_t unsupported = editor_get_stats(e).unimplemented_actions;
        if (!strcmp(command, "new")) {
            T(press(e, key(XKB_KEY_n, PLAT_MOD_CTRL, NULL)) == 0);
            T(tabs_count(&e->tabs) == tabs + 1u); T(tabs_active_index(&e->tabs) == tabs);
            T(!strcmp(tabs_active(&e->tabs)->title, "untitled")); T(!*e->buffer->path);
            T(expect(e, "", 0) == 0);
            T(editor_select_tab(e, tabs - 1u) == 0); T(settle(e) == 0); T(expect(e, text, 0) == 0);
        } else if (!strcmp(command, "open-empty")) {
            if (panel) T(press(e, key(XKB_KEY_Escape, 0, NULL)) == 0);
            T(editor_close_tab(e, tabs_active_index(&e->tabs)) == 0); T(settle(e) == 0);
            T(!tabs_count(&e->tabs));
            T(press(e, key(XKB_KEY_o, PLAT_MOD_CTRL, NULL)) == 0); T(e->go.active);
            T(o20_path(e, path) == 0); T(tabs_count(&e->tabs) == tabs); T(expect(e, text, 0) == 0);
        } else if (!strcmp(command, "path-edit")) {
            T(press(e, key(XKB_KEY_o, PLAT_MOD_CTRL, NULL)) == 0); T(e->go.active);
            static const char unicode[] = "é";
            T(press(e, key(XKB_KEY_eacute, 0, unicode)) == 0);
            T(press(e, key(XKB_KEY_BackSpace, 0, NULL)) == 0); T(e->go.length == 0);
            T(press(e, key(XKB_KEY_eacute, 0, unicode)) == 0);
            T(press(e, key(XKB_KEY_Left, 0, NULL)) == 0); T(e->go.cursor == 0);
            T(press(e, key(XKB_KEY_Delete, 0, NULL)) == 0); T(e->go.length == 0);
            T(press(e, key(XKB_KEY_Escape, 0, NULL)) == 0); T(expect(e, text, 0) == 0);
        } else if (!strcmp(command, "open")) {
            T(press(e, key(XKB_KEY_o, PLAT_MOD_CTRL, NULL)) == 0); T(e->go.active);
            T(o20_path(e, path) == 0); T(!e->go.active);
            T(tabs_count(&e->tabs) == tabs + 1u); T(!strcmp(e->buffer->path, path)); T(expect(e, text, 0) == 0);
            T(press(e, key(XKB_KEY_o, PLAT_MOD_CTRL, NULL)) == 0); T(o20_path(e, target) == 0);
            T(tabs_count(&e->tabs) == tabs + 1u); T(!strcmp(e->buffer->path, path)); T(e->recovery_notice != NULL);
            T(press(e, key(XKB_KEY_o, PLAT_MOD_CTRL, NULL)) == 0); T(o20_path(e, dir) == 0);
            T(tabs_count(&e->tabs) == tabs + 1u); T(expect(e, text, 0) == 0);
        } else if (!strcmp(command, "save-as") || !strcmp(command, "save-source") || !strcmp(command, "save-watch") || !strcmp(command, "save-clean-watch") || !strcmp(command, "save-mapped") || !strcmp(command, "save-shared")) {
            T(press(e, key(XKB_KEY_s, PLAT_MOD_CTRL | PLAT_MOD_SHIFT, NULL)) == 0); T(e->go.active);
            T(o20_path(e, target) == 0); T(!e->go.active);
            if (!strcmp(command, "save-shared")) {
                T(e->recovery_notice == NULL);
                for (size_t i = 0; i < e->buffer_capacity; i++)
                    if (e->buffers[i] && !e->buffers[i]->retired) T(journal_check_base(&e->buffers[i]->base) == 0);
            }
            T(tabs_count(&e->tabs) == tabs); T(!strcmp(e->buffer->path, target));
            T(!strcmp(tabs_active(&e->tabs)->title, "save.txt")); T(!strcmp(e->buffer->save_path, target));
            T(e->buffer->history_cursor == history); T(expect(e, text, 0) == 0);
            fd = open(target, O_RDONLY); T(fd >= 0); char bytes[sizeof text];
            T(read(fd, bytes, sizeof bytes) == (ssize_t)(sizeof text - 1u)); T(close(fd) == 0);
            T(!memcmp(bytes, text, sizeof text - 1u));
            if (panel) { T(press(e, key(XKB_KEY_Escape, 0, NULL)) == 0); }
            T(press(e, key(XKB_KEY_x, 0, "x")) == 0); T(press(e, key(XKB_KEY_s, PLAT_MOD_CTRL, NULL)) == 0);
            T(editor_flush(e) == 0); T(settle(e) == 0);
            fd = open(target, O_RDONLY); T(fd >= 0); T(read(fd, bytes, sizeof bytes) == (ssize_t)sizeof text);
            T(close(fd) == 0); T(bytes[0] == 'x'); T(!memcmp(bytes + 1u, text, sizeof text - 1u));
            T(press(e, key(XKB_KEY_z, PLAT_MOD_CTRL, NULL)) == 0); T(expect(e, text, 0) == 0);
            T(press(e, key(XKB_KEY_s, PLAT_MOD_CTRL | PLAT_MOD_SHIFT, NULL)) == 0);
            T(press(e, key(XKB_KEY_a, PLAT_MOD_CTRL, NULL)) == 0); T(o20_path(e, dir) == 0);
            T(!strcmp(e->buffer->path, target)); T(expect(e, text, 0) == 0);
            if (!strcmp(command, "save-source") || !strcmp(command, "save-mapped")) {
                fd = open(path, O_RDONLY); T(fd >= 0);
                T(read(fd, bytes, sizeof bytes) == (ssize_t)(sizeof text - 1u)); T(close(fd) == 0);
                T(!memcmp(bytes, text, sizeof text - 1u));
            }
            if (!strcmp(command, "save-clean-watch")) {
                T(press(e, key(XKB_KEY_s, PLAT_MOD_CTRL, NULL)) == 0); T(editor_flush(e) == 0); T(settle(e) == 0);
                fd = open(target, O_WRONLY | O_TRUNC); T(fd >= 0);
                static const char external[] = "external";
                T(write(fd, external, sizeof external - 1u) == (ssize_t)(sizeof external - 1u)); T(close(fd) == 0);
                file_watch_dispatch(); T(settle(e) == 0); T(!e->buffer->source_stale); T(expect(e, external, 0) == 0);
            }
            if (!strcmp(command, "save-watch")) {
                fd = open(target, O_WRONLY | O_TRUNC); T(fd >= 0);
                static const char external[] = "external";
                T(write(fd, external, sizeof external - 1u) == (ssize_t)(sizeof external - 1u)); T(close(fd) == 0);
                file_watch_dispatch(); T(settle(e) == 0); T(e->buffer->source_stale);
                T(editor_source_resolve(e, false) == 0); T(settle(e) == 0); T(!e->buffer->source_stale);
                T(expect(e, text, 0) == 0);
            }
        } else if (!strcmp(command, "exit")) {
            plat_event ev = key(XKB_KEY_q, PLAT_MOD_CTRL, NULL); T(editor_inject(e, &ev) == 0);
            int rc = editor_step(e, 0);
            T(rc == EDITOR_CLOSED); T(e->quit);
            journal_stats st = journal_get_stats(e->journal); T(st.accepted_sequence == st.durable_sequence);
        } else if (!strcmp(command, "cancel")) {
            e->v.state.selection.anchor = sizeof "alpha" - 1u;
            T(press(e, key(XKB_KEY_Escape, 0, NULL)) == 0);
            if (panel) { T(!e->find_visible); T(e->v.state.selection.anchor == sizeof "alpha" - 1u);
                T(press(e, key(XKB_KEY_Escape, 0, NULL)) == 0); }
            T(e->v.state.selection.anchor == e->v.state.selection.cursor);
            T(press(e, key(XKB_KEY_Escape, 0, NULL)) == 0); T(expect(e, text, 0) == 0);
            T(e->buffer->history_cursor == history);
            T(press(e, key(XKB_KEY_g, PLAT_MOD_CTRL, NULL)) == 0);
            T(press(e, key(XKB_KEY_2, 0, "2")) == 0); T(press(e, key(XKB_KEY_Escape, 0, NULL)) == 0);
            T(!e->go.active); T(expect(e, text, 0) == 0);
        } else {
            view_selection sel = e->v.state.selection; uint64_t top = e->v.state.first_line;
            T(press(e, key(XKB_KEY_Down, PLAT_MOD_CTRL, NULL)) == 0);
            T(e->v.state.first_line == top + 1u); T(!memcmp(&sel, &e->v.state.selection, sizeof sel));
            T(press(e, key(XKB_KEY_Up, PLAT_MOD_CTRL, NULL)) == 0);
            T(e->v.state.first_line == top); T(!memcmp(&sel, &e->v.state.selection, sizeof sel));
            T(press(e, key(XKB_KEY_Up, PLAT_MOD_CTRL, NULL)) == 0); T(e->v.state.first_line == top);
            T(e->buffer->history_cursor == history); T(expect(e, text, 0) == 0);
        }
        T(editor_get_stats(e).unimplemented_actions == unsupported); T(count.allocations == 0);
        editor_close(e); unlink(path); unlink(target); unlink(log);
        /* Existing journal lock is retained by the session contract. */
        char lock[IPC_PATH_CAP]; T(snprintf(lock, sizeof lock, "%s/.journal.lock", dir) > 0); unlink(lock);
        remove_retained(dir);
        T(rmdir(dir) == 0);
        if (panel) break;
    }
    printf("edit-o20 %s: text and find focus PASS\n", command); return 0;
}
static int o20_all(void)
{
    const char *commands[] = {"new", "open", "save-as", "exit", "cancel", "scroll", "open-empty", "path-edit", "save-source", "save-watch", "scroll-wrap", "save-clean-watch", "save-mapped", "save-shared"};
    for (size_t i = 0; i < sizeof commands / sizeof *commands; i++) T(o20_case(commands[i]) == 0);
    return 0;
}
/* edit-97i: (P) geometry comes from o20_case; tab capacity is exactly the
 * Unicode file plus its peer. Offsets/lengths derive from the literal text;
 * zero denotes origin/success and one denotes the next byte or peer index. */
static int duplicate_path_contract(void)
{
    char dir[] = "/tmp/edit-97i-editor-XXXXXX"; T(mkdtemp(dir));
    char path[IPC_PATH_CAP], alias[IPC_PATH_CAP], hard[IPC_PATH_CAP], relative[IPC_PATH_CAP];
    T(snprintf(path, sizeof path, "%s/é文.txt", dir) > 0);
    T(snprintf(alias, sizeof alias, "%s/alias.txt", dir) > 0);
    T(snprintf(hard, sizeof hard, "%s/hard.txt", dir) > 0);
    T(snprintf(relative, sizeof relative, "%s/./é文.txt", dir) > 0);
    static const char text[] = "alpha\nbeta\ngamma\ndelta\n";
    int fd = open(path, O_CREAT | O_WRONLY, S_IRUSR | S_IWUSR); T(fd >= 0);
    T(write(fd, text, sizeof text - 1u) == (ssize_t)(sizeof text - 1u)); T(close(fd) == 0);
    T(symlink(path, alias) == 0); T(link(path, hard) == 0);
    render_backend backend = {0}; T(render_null_backend(&backend) == 0);
    editor_config cfg = {.path = path, .cols = sizeof "Go to line: :18446744073709551615" + 1u,
        .rows = sizeof "text" + 1u, .tab_capacity = sizeof "ab" - 1u, .wrap_mode = -1};
    editor *e = NULL; T(editor_open(&e, &cfg, &backend) == 0); T(settle(e) == 0);
    uint64_t original = tabs_active(&e->tabs)->id, peer, reused;
    T(press(e, key('x', 0, "x")) == 0); T(editor_set_cursor(e, sizeof "alpha\n" - 1u) == 0); T(settle(e) == 0);
    view_state retained = editor_view(e); piece_tree *tree = e->tree;
    T(editor_add_buffer(e, NULL, (const uint8_t *)"peer", sizeof "peer" - 1u, &peer) == 0); T(settle(e) == 0);
    size_t tabs = tabs_count(&e->tabs); uint64_t next = e->tabs.next_id;
    const char *paths[] = {path, relative, alias, hard};
    for (size_t i = 0; i < sizeof paths / sizeof *paths; i++) {
        T(editor_add_buffer(e, paths[i], NULL, 0, &reused) == 0); T(settle(e) == 0);
        T(reused == original && e->tree == tree && tabs_count(&e->tabs) == tabs && e->tabs.next_id == next);
        T(expect(e, "xalpha\nbeta\ngamma\ndelta\n", retained.selection.cursor) == 0);
        T(editor_view(e).selection.anchor == retained.selection.anchor);
        T(editor_view(e).first_byte == retained.first_byte && tabs_active(&e->tabs)->modified);
        T(editor_select_tab(e, 1u) == 0); T(settle(e) == 0);
    }
    /* Ctrl+O takes the same path after its containing input frame retires. */
    T(press(e, key(XKB_KEY_o, PLAT_MOD_CTRL, NULL)) == 0); T(o20_path(e, alias) == 0);
    T(tabs_count(&e->tabs) == tabs && tabs_active(&e->tabs)->id == original && e->tree == tree);
    T(expect(e, "xalpha\nbeta\ngamma\ndelta\n", retained.selection.cursor) == 0);
    T(press(e, key(XKB_KEY_z, PLAT_MOD_CTRL, NULL)) == 0); T(expect(e, text, 0) == 0);
    T(editor_select_tab(e, 1u) == 0); T(settle(e) == 0);
    ipc_request request = {.paths = {{path, 1u, 1u}, {alias, 1u, 1u}, {hard, 1u, 1u}},
        .count = sizeof "abc" - 1u}; /* (P) one entry per named alias above. */
    T(editor_open_request(e, &request, 0) == 0); T(settle(e) == 0);
    T(tabs_count(&e->tabs) == tabs && tabs_active(&e->tabs)->id == original);
    request.count = sizeof "a" - 1u;
    request.paths[0].line = sizeof "ab" - 1u; /* (P) second LF-delimited line. */
    T(editor_open_request(e, &request, 0) == 0); T(settle(e) == 0);
    T(editor_view(e).selection.cursor == sizeof "alpha\n" - 1u);
    editor_close(e);
    /* (P) append a line longer than the resident coordinate scan bound by
     * one byte, so an out-of-bound column deterministically rejects. */
    FILE *long_file = fopen(path, "a"); T(long_file);
    for (size_t at = 0; at < VIEW_SCAN_BOUND + sizeof "a" - 1u; at++) T(fputc('a', long_file) != EOF);
    T(fclose(long_file) == 0);
    uint32_t long_line = 1u;
    for (size_t at = 0; at < sizeof text - 1u; at++) long_line += text[at] == '\n' ? 1u : 0u;
    cfg.path = NULL; cfg.start_empty = true;
    T(render_null_backend(&backend) == 0); T(editor_open(&e, &cfg, &backend) == 0); T(settle(e) == 0);
    request = (ipc_request){.paths = {{path, 1u, 1u}, {alias, 1u, 1u}, {hard, 1u, 1u}},
        .count = sizeof "abc" - 1u}; /* (P) repeated aliases require only the source tab. */
    T(editor_open_request(e, &request, 0) == 0); T(settle(e) == 0);
    T(tabs_count(&e->tabs) == sizeof "a" - 1u);
    /* (P) reject the oversized column on the appended line after an earlier
     * new path: failed requests publish neither target. The line ordinal is
     * the original fixture's LF count plus the appended line above. */
    editor_buffer *owner = e->buffer; original = tabs_active(&e->tabs)->id;
    next = e->tabs.next_id; tabs = tabs_count(&e->tabs);
    T(snprintf(relative, sizeof relative, "%s/missing.txt", dir) > 0);
    request = (ipc_request){.paths = {{relative, 1u, 1u}, {path, long_line, UINT32_MAX}},
        .count = sizeof "ab" - 1u};
    T(editor_open_request(e, &request, 0) == EDITOR_ERR_CAPACITY);
    T(tabs_count(&e->tabs) == tabs && e->tabs.next_id == next);
    T(e->buffer == owner && tabs_active(&e->tabs)->id == original);
    T(editor_add_buffer(e, NULL, NULL, 0, &peer) == 0); T(settle(e) == 0);
    next = e->tabs.next_id;
    request = (ipc_request){.paths = {{path, 1u, 1u}, {dir, 1u, 1u}}, .count = sizeof "ab" - 1u};
    T(editor_open_request(e, &request, 0) != 0);
    T(tabs_active(&e->tabs)->id == peer && e->tabs.next_id == next);
    editor_close(e); T(unlink(hard) == 0 && unlink(alias) == 0 && unlink(path) == 0 && rmdir(dir) == 0);
    puts("edit-97i: resolved-path reuse, full capacity, Ctrl+O, retained text/view/undo and CLI coordinates passed");
    return 0;
}

/* edit-n95: find in files UI on the tree_fixture.h witness tree. (E) Every
 * expected line, column, byte and count below is counted in the fixture's
 * literals (tests/tree_fixture.h); zero/one keep their index/count meanings.
 * Geometry: one tab row, a three-row find panel and text rows for the longest
 * expectation fit without wrapping at the fixture's path widths. */
static char n95_text[2u * FIF_DEFAULT_CONTEXT * 8u];
static size_t n95_read(editor *e)
{
    size_t n = (size_t)editor_length(e);
    if (n >= sizeof n95_text || editor_read(e, 0, (uint8_t *)n95_text, n)) return SIZE_MAX;
    n95_text[n] = 0; return n;
}
static int n95_type(editor *e, const char *text)
{
    for (const char *p = text; *p; p++) {
        char one[2] = {*p, 0};
        T(press(e, key((uint32_t)(uint8_t)*p, 0, one)) == 0);
    }
    return 0;
}
static const char *n95_active_path(editor *e)
{
    const tabs_tab *tab = tabs_active(&e->tabs);
    return tab ? tab->path : "";
}
static bool n95_ends(const char *path, const char *suffix)
{
    size_t a = strlen(path), b = strlen(suffix);
    return a >= b && !strcmp(path + a - b, suffix);
}
static size_t n95_results_tab(editor *e)
{
    for (size_t i = 0; i < tabs_count(&e->tabs); i++)
        if (!strcmp(editor_tab(e, i)->title, "Find Results")) return i;
    return SIZE_MAX;
}
#define N95_CS (PLAT_MOD_CTRL | PLAT_MOD_SHIFT)
static int n95_find_in_files(void)
{
    tree_fixture tree; T(tree_fixture_init(&tree) == 0);
    /* (E) Twice the mailbox capacity of one-hit lines: more than the mailbox
     * plus the engine's single outbox, so a cancelled search is partial. */
    static char dense[2u * WORK_MAILBOX_CAP * 2u];
    for (size_t i = 0; i < sizeof dense; i += 2u) { dense[i] = 'x'; dense[i + 1u] = '\n'; }
    T(tree_fixture_file(&tree, "dense", dense, sizeof dense) == 0);
    render_backend backend = {0}; T(render_null_backend(&backend) == 0);
    counted count = {0};
    editor_config cfg = {.cols = 120u, .rows = 1u + 3u + 20u, .wrap_mode = -1,
        .hook_ctx = &count, .on_ingress = ingress, .on_submit = submitted};
    editor *e = NULL; T(editor_open(&e, &cfg, &backend) == 0); T(settle(e) == 0);
    /* (a) Ctrl+Shift+F, query, Tab, Where, Return -> a Find Results tab. */
    T(press(e, key(XKB_KEY_f, N95_CS, NULL)) == 0);
    T(e->find_visible && e->fif.mode);
    T(n95_type(e, "cat") == 0);
    T(press(e, key(XKB_KEY_Tab, 0, NULL)) == 0); T(e->fif.where_focus);
    T(n95_type(e, tree.root) == 0);
    T(press(e, key(XKB_KEY_Return, 0, NULL)) == 0);
    T(settle(e) == 0);
    T(n95_results_tab(e) == tabs_active_index(&e->tabs));
    char expected[4096];
    int length = snprintf(expected, sizeof expected,
        "%s/a.txt:\n    1: Alpha cat\n    2: catapult CAT cat\n    3: last cat\n\n"
        "%s/nested/b.c:\n    1: cat\n    2: scatter\n    3: cat!\n\n"
        "%s/nested/skip.txt:\n    1: cat\n\n"
        "9 matches across 3 files\n", tree.root, tree.root, tree.root);
    T(length > 0 && (size_t)length < sizeof expected);
    size_t n = n95_read(e);
    if (n == SIZE_MAX || strcmp(n95_text, expected))
        fprintf(stderr, "n95 expected:\n[%s]\nactual:\n[%s]\n", expected, n == SIZE_MAX ? "" : n95_text);
    T(n != SIZE_MAX && !strcmp(n95_text, expected));
    /* Match ranges carry the find-match highlight: "Alpha cat" marks "cat". */
    size_t first = sizeof "/a.txt:\n    1: Alpha " - 1u + strlen(tree.root);
    T(e->find_range_count >= 1u && e->find_ranges[0].start == first &&
      e->find_ranges[0].end == first + sizeof "cat" - 1u);
    /* (e) Typing, deleting, undo and line edits change nothing, latch nothing. */
    uint64_t revision = e->buffer->revision;
    T(press(e, key('q', 0, "q")) == 0);
    T(press(e, key(XKB_KEY_BackSpace, 0, NULL)) == 0);
    T(press(e, key(XKB_KEY_Delete, 0, NULL)) == 0);
    T(press(e, key(XKB_KEY_z, PLAT_MOD_CTRL, NULL)) == 0);
    T(press(e, key(XKB_KEY_d, N95_CS, NULL)) == 0);
    T(press(e, key(XKB_KEY_x, PLAT_MOD_CTRL, NULL)) == 0);
    T(e->buffer->revision == revision && !e->error && !editor_get_stats(e).journal_error);
    T(n95_read(e) == n && !strcmp(n95_text, expected));
    /* (b) Enter on the "scatter" hit (results line 7, zero-based) opens
     * nested/b.c at line 2, caret on the hit's byte column 2: byte 4 + 1. */
    for (int i = 0; i < 7; i++) T(press(e, key(XKB_KEY_Down, 0, NULL)) == 0);
    T(press(e, key(XKB_KEY_Return, 0, NULL)) == 0);
    T(n95_ends(n95_active_path(e), "/nested/b.c"));
    T(editor_view(e).selection.cursor == sizeof "cat\ns" - 1u);
    /* (c) F4 / Shift+F4 continue from the last visited hit and wrap. */
    T(press(e, key(XKB_KEY_F4, 0, NULL)) == 0);
    T(n95_ends(n95_active_path(e), "/nested/b.c") && editor_view(e).selection.cursor == sizeof "cat\nscatter\n" - 1u);
    T(press(e, key(XKB_KEY_F4, 0, NULL)) == 0);
    T(n95_ends(n95_active_path(e), "/nested/skip.txt") && editor_view(e).selection.cursor == 0);
    T(press(e, key(XKB_KEY_F4, 0, NULL)) == 0);
    T(n95_ends(n95_active_path(e), "/a.txt") && editor_view(e).selection.cursor == sizeof "Alpha " - 1u);
    T(press(e, key(XKB_KEY_F4, PLAT_MOD_SHIFT, NULL)) == 0);
    T(n95_ends(n95_active_path(e), "/nested/skip.txt"));
    T(press(e, key(XKB_KEY_F4, PLAT_MOD_SHIFT, NULL)) == 0);
    T(n95_ends(n95_active_path(e), "/nested/b.c") && editor_view(e).selection.cursor == sizeof "cat\nscatter\n" - 1u);
    /* Double-click on results row 1 ("    1: Alpha cat", inside "Alpha")
     * opens a.txt at its hit: column 7, byte 6. */
    T(editor_select_tab(e, n95_results_tab(e)) == 0); T(settle(e) == 0);
    plat_event click = {.kind = PLAT_EV_BUTTON, .code = 1, .press = true,
        .x = (int32_t)((e->lay.gw + sizeof "    1: Al" - 1u) * e->grid.dims.cell_w + 1u),
        .y = (int32_t)((e->tab_rows + 1u) * e->grid.dims.cell_h + 1u)};
    plat_event lift = click; lift.press = false;
    T(press(e, click) == 0); T(press(e, lift) == 0);
    T(n95_results_tab(e) == tabs_active_index(&e->tabs));
    T(press(e, click) == 0); T(press(e, lift) == 0);
    T(n95_ends(n95_active_path(e), "/a.txt") && editor_view(e).selection.cursor == sizeof "Alpha " - 1u);
    /* (d) Escape mid-search: the engine blocks on a full mailbox (the
     * dropped_full handshake of tests/fif_test.c), the cancel arrives before
     * any further drain, the partial buffer stays with a cancelled summary. */
    T(press(e, key(XKB_KEY_f, N95_CS, NULL)) == 0); T(e->fif.mode && !e->fif.where_focus);
    T(press(e, key(XKB_KEY_a, PLAT_MOD_CTRL, NULL)) == 0);
    T(n95_type(e, "x") == 0);
    T(press(e, key(XKB_KEY_Tab, 0, NULL)) == 0);
    char where[PATH_MAX + 16]; T(snprintf(where, sizeof where, "%s, dense", tree.root) > 0);
    T(n95_type(e, where) == 0);
    uint64_t full = atomic_load(&e->pool.dropped_full);
    plat_event enter = key(XKB_KEY_Return, 0, NULL);
    T(editor_inject(e, &enter) == 0);
    for (int i = 0; i < 64 && e->queue_count; i++) {
        int rc = editor_step(e, 0); T(rc == EDITOR_OK || rc == EDITOR_MORE);
    }
    T(!e->queue_count && e->fif.running);
    while (atomic_load(&e->pool.dropped_full) == full) sched_yield();
    plat_event escape = key(XKB_KEY_Escape, 0, NULL);
    T(editor_handle_key(e, &escape) == 0);
    T(settle(e) == 0);
    T(e->fif.done_received && e->fif.done.cancelled && !e->fif.running);
    uint64_t stored = e->fif.done.hits;
    T(stored > 0 && stored < sizeof dense / 2u);
    n = n95_read(e); T(n != SIZE_MAX);
    char tail[128];
    T(snprintf(tail, sizeof tail, "\n\n%llu %s in 1 file\nSearch cancelled\n",
        (unsigned long long)stored, stored == 1u ? "match" : "matches") > 0);
    T(n95_ends(n95_text, tail));
    char head[PATH_MAX + 32]; T(snprintf(head, sizeof head, "%s/dense:\n    1: x\n", tree.root) > 0);
    T(!strncmp(n95_text, head, strlen(head)));
    /* Replacing a live search: the old job retires (cancelled, its mail and
     * companion DONE ignored by generation) and is disposed once finished;
     * the new search's results replace the tab's content. */
    full = atomic_load(&e->pool.dropped_full);
    T(editor_fif_start(e) == 0); T(e->fif.running);
    uint32_t first_generation = e->fif.current.generation;
    while (atomic_load(&e->pool.dropped_full) == full) sched_yield();
    T(editor_fif_start(e) == 0);
    T(e->fif.retiring.live && e->fif.retiring.generation == first_generation);
    T(e->fif.current.generation != first_generation);
    T(settle(e) == 0);
    T(!e->fif.retiring.live && e->fif.done_received && !e->fif.done.cancelled);
    n = n95_read(e); T(n != SIZE_MAX);
    T(snprintf(tail, sizeof tail, "\n\n%zu matches in 1 file\n", sizeof dense / 2u) > 0);
    T(n95_ends(n95_text, tail) && !strncmp(n95_text, head, strlen(head)));
    /* (f) Results assembly frames and refused typing allocate nothing. */
    T(!count.active && count.allocations == 0);
    editor_close(e); T(tree_fixture_remove(tree.root) == 0);
    puts("edit-n95 find in files: panel, results format, open at hit, F4 cycle, cancel, read-only passed");
    return 0;
}

/* edit-3f2k (recovery-3 F1): Ctrl+S on ordinary files while a Find Results
 * tab is live. Tab order a.txt (0), Find Results (1, derived, never
 * published), nested/b.c (2): the session's recorded order skips the derived
 * tab, so saving needs the filtered live count (2, not 3) and dense ranks
 * (b.c is recorded rank 1). (P) Expected bytes are the tree_fixture.h
 * literals with one "x" typed at the caret; b.c's caret is the n95 "scatter"
 * hit, byte 5. A count-only fix fails the first save in encode_all (TABS
 * ids gap at sparse rank 1). */
static int n3f2k_saved(editor *e)
{
    /* (E) reuse settle()'s existing scheduling bound, no new duration. */
    uint64_t deadline = trace_now_ns() + UINT64_C(5000000000);
    while (editor_save_active(e)) { T(editor_step(e, 0) >= 0); T(trace_now_ns() < deadline); sched_yield(); }
    T(settle(e) == 0);
    return 0;
}
static int n3f2k_file(const char *path, const char *text)
{
    char bytes[64]; size_t n = strlen(text); T(n < sizeof bytes);
    int fd = open(path, O_RDONLY); T(fd >= 0);
    ssize_t got = read(fd, bytes, sizeof bytes); T(close(fd) == 0);
    if (got != (ssize_t)n || memcmp(bytes, text, n))
        fprintf(stderr, "edit-3f2k %s: expected [%s] actual [%.*s]\n", path, text, got > 0 ? (int)got : 0, bytes);
    T(got == (ssize_t)n && !memcmp(bytes, text, n));
    return 0;
}
static int n3f2k_save_with_results(void)
{
    tree_fixture tree; T(tree_fixture_init(&tree) == 0);
    char a[PATH_MAX], b[PATH_MAX];
    T(tree_fixture_path(&tree, "a.txt", a) == 0); T(tree_fixture_path(&tree, "nested/b.c", b) == 0);
    /* The journal (outside the searched tree) makes each save encode the
     * pinned session in its PREPARE checkpoint (savectl prepare). */
    char log_path[] = "/tmp/editor-3f2k-XXXXXX";
    int fd = mkstemp(log_path); T(fd >= 0); T(close(fd) == 0);
    render_backend backend = {0}; T(render_null_backend(&backend) == 0);
    counted count = {0};
    editor_config cfg = {.path = a, .journal_path = log_path, .cols = 120u, .rows = 1u + 3u + 20u, .wrap_mode = -1,
        .hook_ctx = &count, .on_ingress = ingress, .on_submit = submitted};
    editor *e = NULL; T(editor_open(&e, &cfg, &backend) == 0); T(settle(e) == 0);
    T(press(e, key(XKB_KEY_f, N95_CS, NULL)) == 0);
    T(n95_type(e, "cat") == 0);
    T(press(e, key(XKB_KEY_Tab, 0, NULL)) == 0);
    T(n95_type(e, tree.root) == 0);
    T(press(e, key(XKB_KEY_Return, 0, NULL)) == 0); T(settle(e) == 0);
    T(n95_results_tab(e) == 1u && tabs_active_index(&e->tabs) == 1u);
    for (int i = 0; i < 7; i++) T(press(e, key(XKB_KEY_Down, 0, NULL)) == 0);
    T(press(e, key(XKB_KEY_Return, 0, NULL)) == 0);
    T(n95_ends(n95_active_path(e), "/nested/b.c") && tabs_active_index(&e->tabs) == 2u);
    T(editor_view(e).selection.cursor == sizeof "cat\ns" - 1u);
    /* Saving the tab recorded after the derived one (sparse live index). */
    T(press(e, key(XKB_KEY_x, 0, "x")) == 0);
    T(press(e, key(XKB_KEY_s, PLAT_MOD_CTRL, NULL)) == 0); T(n3f2k_saved(e) == 0);
    T(e->buffer->save_error == 0 && !editor_tab(e, 2u)->modified);
    T(n3f2k_file(b, "cat\nsxcatter\ncat!\n") == 0);
    /* Saving the tab recorded before it. */
    T(editor_select_tab(e, 0u) == 0); T(settle(e) == 0); T(n95_ends(n95_active_path(e), "/a.txt"));
    T(press(e, key(XKB_KEY_Home, PLAT_MOD_CTRL, NULL)) == 0);
    T(press(e, key(XKB_KEY_x, 0, "x")) == 0);
    T(press(e, key(XKB_KEY_s, PLAT_MOD_CTRL, NULL)) == 0); T(n3f2k_saved(e) == 0);
    T(e->buffer->save_error == 0 && !editor_tab(e, 0u)->modified);
    T(n3f2k_file(a, "xAlpha cat\ncatapult CAT cat\nlast cat") == 0);
    T(n95_results_tab(e) == 1u);
    T(!count.active && count.allocations == 0); /* (P) no typing-path allocations */
    editor_close(e); T(unlink(log_path) == 0); T(tree_fixture_remove(tree.root) == 0);
    puts("edit-3f2k: Ctrl+S on files before and after a live Find Results tab passed");
    return 0;
}

/* edit-5vz: find in files results-tab failures are refusals, never fail().
 * A results append the piece allocator refuses truncates the results with a
 * row (or, when even the row cannot be stored, a status notice); a refused
 * results-tab open submits nothing and keeps the old results text. */
static const char n5vz_full[] = "Search truncated (results buffer full)\n";
static int n5vz_query(editor *e, const char *query, const char *where)
{
    T(press(e, key(XKB_KEY_f, N95_CS, NULL)) == 0); T(e->fif.mode && !e->fif.where_focus);
    T(press(e, key(XKB_KEY_a, PLAT_MOD_CTRL, NULL)) == 0);
    T(n95_type(e, query) == 0);
    T(press(e, key(XKB_KEY_Tab, 0, NULL)) == 0); T(e->fif.where_focus && e->fif.where_all);
    T(n95_type(e, where) == 0);
    return 0;
}
/* settle() with the failing step's rc printed: the red line of this bead. */
static int n5vz_settle(editor *e)
{
    uint64_t deadline = trace_now_ns() + UINT64_C(5000000000); /* settle()'s bound */
    do {
        int rc = editor_step(e, 20);
        if (rc != EDITOR_OK && rc != EDITOR_MORE) {
            fprintf(stderr, "edit-5vz: editor_step rc %d error_cause %d\n", rc, editor_get_stats(e).error_cause);
            return 1;
        }
        editor_stats s = editor_get_stats(e);
        if (!s.pending && !s.render_active) return 0;
    } while (trace_now_ns() < deadline);
    T(false); return 1;
}
static int n5vz_tail(editor *e, const char *tail)
{
    size_t n = (size_t)editor_length(e), k = strlen(tail); char got[256];
    T(k < sizeof got && n >= k && editor_read(e, n - k, (uint8_t *)got, k) == 0);
    got[k] = 0;
    return strcmp(got, tail) ? 1 : 0;
}
static int n5vz_results_full(void)
{
    tree_fixture tree; T(tree_fixture_init(&tree) == 0);
    /* (P) One hit per line: "cat" then filler to FIF_DEFAULT_CONTEXT - 1
     * content bytes, so no context is cut; the line count makes the file
     * twice EDITOR_STAGE_BYTES, so its results (each line plus a number
     * field) need more than one staged append. */
    enum { W = FIF_DEFAULT_CONTEXT - 1u, L = 2u * EDITOR_STAGE_BYTES / (W + 1u) };
    _Static_assert(L <= FIF_DEFAULT_HITS && (size_t)L * (W + 1u) <= FIF_DEFAULT_FILE_BYTES, "one engine job");
    static char wide[(size_t)L * (W + 1u)];
    for (size_t line = 0; line < L; line++) {
        char *p = wide + line * (W + 1u);
        memset(p, 'y', W); memcpy(p, "cat", 3); p[W] = '\n';
    }
    T(tree_fixture_file(&tree, "wide", wide, sizeof wide) == 0);
    render_backend backend = {0}; T(render_null_backend(&backend) == 0);
    editor_config cfg = {.cols = 120u, .rows = 1u + 3u + 20u, .wrap_mode = -1};
    editor *e = NULL; T(editor_open(&e, &cfg, &backend) == 0); T(settle(e) == 0);
    /* (a1) G: the next piece allocator request of the fresh results buffer
     * fails once: the whole first batch is refused, the row stands alone. */
    T(n5vz_query(e, "cat", tree.root) == 0);
    T(editor_fif_start(e) == 0); T(editor_fif_results_active(e));
    editor_piece_storage *s = &e->buffer->piece_storage;
    s->fail_request = s->requests + 1u;
    T(n5vz_settle(e) == 0);
    s->fail_request = 0;
    T(!e->error && e->fif.assembled == 0);
    size_t n = n95_read(e); T(n != SIZE_MAX);
    if (strcmp(n95_text, n5vz_full)) fprintf(stderr, "edit-5vz a1 text [%s]\n", n95_text);
    T(!strcmp(n95_text, n5vz_full));
    /* The editor keeps taking keys; Enter and F4 find no hit to open. */
    T(press(e, key(XKB_KEY_Down, 0, NULL)) == 0);
    T(press(e, key(XKB_KEY_Return, 0, NULL)) == 0);
    T(press(e, key(XKB_KEY_F4, 0, NULL)) == 0);
    T(editor_fif_results_active(e) && !e->error && n95_read(e) == n);
    /* A new search runs normally afterwards: the fixture's nine hits in
     * three files (n95) plus one per wide line, in a fourth file. */
    T(editor_fif_start(e) == 0); T(settle(e) == 0);
    char all[64]; T(snprintf(all, sizeof all, "%u matches across 4 files\n", 9u + (unsigned)L) > 0);
    T(n5vz_tail(e, all) == 0);
    /* (a2) Each later request fails in turn (the K-th request of the results
     * buffer), until the whole result fits: line maps stay inside the stored
     * text and the row follows the last stored hit. All hits are received
     * first and assembly is driven by editor_fif_service alone, so the armed
     * request is an append's, not another service's (a scroll snapshot). */
    char where[PATH_MAX + 16]; T(snprintf(where, sizeof where, "%s, wide", tree.root) > 0);
    T(n5vz_query(e, "cat", where) == 0);
    char done[64]; T(snprintf(done, sizeof done, "\n%u matches in 1 file\n", (unsigned)L) > 0);
    bool mid = false;
    for (uint64_t k = 1u;; k++) {
        T(editor_fif_start(e) == 0); T(editor_fif_results_active(e));
        while (!e->fif.done_received) { (void)work_mailbox_drain(&e->pool, editor_route_work, e); sched_yield(); }
        s = &e->buffer->piece_storage; s->fail_request = s->requests + k;
        while (editor_fif_runnable(e)) T(editor_fif_service(e) == 0);
        s->fail_request = 0;
        T(n5vz_settle(e) == 0);
        T(!e->error);
        uint64_t length = editor_length(e);
        if (!n5vz_tail(e, done) && e->fif.assembled == L) break;
        T(n5vz_tail(e, n5vz_full) == 0);
        uint32_t a = e->fif.assembled; T(a <= L); /* a == L: the summary append was the refused one */
        uint64_t row = length - (sizeof n5vz_full - 1u);
        T(a ? row == e->fif.line_end[a - 1u] + 2u : row == 0);
        for (uint32_t h = 0; h < a; h++) {
            char want[16], got[16]; int m = snprintf(want, sizeof want, "%5u: cat", h + 1u);
            T(m > 0 && e->fif.line_end[h] < row);
            T(editor_read(e, e->fif.line_start[h], (uint8_t *)got, (size_t)m) == 0 && !memcmp(got, want, (size_t)m));
            uint8_t lf; T(editor_read(e, e->fif.line_end[h], &lf, 1) == 0 && lf == '\n');
        }
        mid |= a > 0;
        T(press(e, key(XKB_KEY_Down, 0, NULL)) == 0);
    }
    T(mid);
    editor_close(e);
    /* (a3) Every request fails: the stopped_error reserve (open-time metadata
     * fits, slack below one add chunk). Neither the hits nor the row can be
     * stored; the notice reports the truncation and keys still work. */
    cfg.history_keys = 1; e = NULL;
    for (size_t reserve = 65536; reserve <= 2u * 1024u * 1024u; reserve += 65536) {
        cfg.arena_bytes = reserve;
        int rc = editor_open(&e, &cfg, &backend);
        if (!rc) break;
        T(rc == EDITOR_ERR_MEMORY || rc == PIECE_ERR_NOMEM);
    }
    T(e != NULL); T(settle(e) == 0);
    T(n5vz_query(e, "cat", tree.root) == 0);
    T(editor_fif_start(e) == 0); T(editor_fif_results_active(e));
    T(n5vz_settle(e) == 0);
    T(!e->error && editor_length(e) == 0);
    T(e->recovery_notice && !strncmp(e->recovery_notice, n5vz_full, sizeof n5vz_full - 2u));
    T(press(e, key(XKB_KEY_Down, 0, NULL)) == 0); T(!e->error);
    editor_close(e); T(tree_fixture_remove(tree.root) == 0);
    puts("edit-5vz: results append refusals truncate with a row or notice, never fail() passed");
    return 0;
}
static int n5vz_open_refused(void)
{
    tree_fixture tree; T(tree_fixture_init(&tree) == 0);
    render_backend backend = {0}; T(render_null_backend(&backend) == 0);
    editor_config cfg = {.cols = 120u, .rows = 1u + 3u + 20u, .wrap_mode = -1};
    editor *e = NULL; T(editor_open(&e, &cfg, &backend) == 0); T(settle(e) == 0);
    T(n5vz_query(e, "cat", tree.root) == 0);
    T(editor_fif_start(e) == 0); T(settle(e) == 0);
    T(n5vz_tail(e, "9 matches across 3 files\n") == 0);
    size_t n = n95_read(e); T(n != SIZE_MAX);
    static char old[sizeof n95_text]; memcpy(old, n95_text, n + 1u);
    uint32_t generation = e->fif.current.generation;
    size_t results = tabs_active_index(&e->tabs);
    /* (b1) Results tab active, activation busy (G: find_replace_active is
     * one editor_activation_busy term): editor_results_open refuses. */
    T(n5vz_query(e, "cat", tree.root) == 0);
    e->find_replace_active = true;
    int rc = editor_fif_start(e);
    e->find_replace_active = false;
    T(rc == 0); T(n5vz_settle(e) == 0);
    n = n95_read(e); T(n != SIZE_MAX);
    if (strcmp(n95_text, old)) fprintf(stderr, "edit-5vz b1 text [%s]\n", n95_text);
    T(!strcmp(n95_text, old));
    T(e->fif.current.generation == generation);
    T(e->recovery_notice && !strcmp(e->recovery_notice, "find in files: no results tab available"));
    /* (b2) Results tab not active: a refused open must not run the search
     * invisibly. */
    T(editor_select_tab(e, 0) == 0); T(settle(e) == 0); T(results != 0);
    e->find_replace_active = true;
    rc = editor_fif_start(e);
    e->find_replace_active = false;
    T(rc == 0); T(n5vz_settle(e) == 0);
    T(e->fif.current.generation == generation && tabs_active_index(&e->tabs) == 0);
    T(!e->fif.running && !e->error);
    /* Not busy: the search replaces the results as before. */
    T(editor_fif_start(e) == 0); T(settle(e) == 0);
    T(e->fif.current.generation != generation && tabs_active_index(&e->tabs) == results);
    n = n95_read(e); T(n != SIZE_MAX && !strcmp(n95_text, old));
    editor_close(e); T(tree_fixture_remove(tree.root) == 0);
    puts("edit-5vz: refused results tab submits nothing, keeps the old results, shows a notice passed");
    return 0;
}

/* edit-bt1 (2): the edit-5vz repro of the pre-5vz editor_fif_start order.
 * The results tab opens (activation sizes its scroll with the find in files
 * panel's rows), the panel then closes in the same turn (text geometry
 * grows), results text arrives, and the tab is left and re-selected. The
 * next scroll slice returned -1 (SCROLL_ERR_ARG, gdb editor.c:712). */
static int nbt1_results_geometry(void)
{
    render_backend backend = {0}; T(render_null_backend(&backend) == 0);
    editor_config cfg = {.cols = 120u, .rows = 1u + 3u + 20u, .wrap_mode = -1};
    editor *e = NULL; T(editor_open(&e, &cfg, &backend) == 0); T(settle(e) == 0);
    T(press(e, key(XKB_KEY_f, N95_CS, NULL)) == 0); T(e->fif.mode && e->panel_rows == EDITOR_FIND_PANEL_ROWS);
    uint64_t id = 0;
    T(editor_results_open(e, &id) == 0); T(e->buffer->derived && e->buffer->read_only);
    size_t results = tabs_active_index(&e->tabs); T(results != 0);
    (void)findui_show(&e->find, false, false);
    e->find_visible = false; e->fif.mode = e->fif.where_focus = e->fif.where_all = false;
    T(editor_text_geometry(e) == 0); T(e->panel_rows == 0);
    /* (P) More result lines (64) than the 20 text rows, so the view can
     * scroll; each line is the 8 bytes of "   1: x\n". */
    static const char row[] = "   1: x\n";
    static char text[64u * (sizeof row - 1u)];
    for (size_t line = 0; line < 64u; line++) memcpy(text + line * (sizeof row - 1u), row, sizeof row - 1u);
    T(editor_results_append(e, (const uint8_t *)text, sizeof text) == 0);
    T(n5vz_settle(e) == 0);
    T(editor_select_tab(e, 0) == 0); T(n5vz_settle(e) == 0);
    T(editor_select_tab(e, results) == 0); T(n5vz_settle(e) == 0);
    T(press(e, key(XKB_KEY_End, PLAT_MOD_CTRL, NULL)) == 0); T(n5vz_settle(e) == 0);
    T(!e->error && editor_view(e).selection.cursor == sizeof text);
    editor_close(e);
    puts("edit-bt1: results open, panel close, re-select: the scroll slice keeps the settled geometry passed");
    return 0;
}

/* edit-ci6i: a minimap snapshot the buffer's piece allocator refuses
 * (editor_piece_storage.fail_request, edit-bt1's fault seam) is deferred,
 * never fail(): editor_step stays OK/MORE, the old map stays, the take is
 * retried on the edit-bt1 tick (E: JOURNAL_DEFAULT_SYNC_NS, edit-dr9's
 * durability interval), and a refusal that persists past one retry shows a
 * status notice. A find results tab, as in edit-bt1: a derived buffer has
 * no session publication, whose snapshot of each revision would otherwise
 * be the cached one the minimap reuses. The view wraps, so the scroll
 * slice (unwrapped only) takes no snapshot either: the map is the
 * revision's only taker. 120 columns (P: open.c gives a minimap from 24). */
static const char ci6i_notice[] = "minimap deferred: buffer memory exhausted";
static bool ci6i_map_due(editor *e)
{
    minimap_input input = editor_map_input(e->buffer);
    return input.index_ready && minimap_stale(&e->buffer->map, &input);
}
static int nci6i_map_refused(void)
{
    /* (P) One appended line ("a\n", two bytes) per attempt. Snapshot
     * headers are pooled two per slab (piece.c pool_grow) and a released
     * snapshot's slot is reused, so a take normally needs no allocator
     * request; the test retains each revision's snapshot until free slots
     * run out and a take must grow the pool, which is the armed (refused)
     * request. */
    enum { ATTEMPTS = 64 };
    /* (E) TICKS retry ticks cover one persisting retry and the recovery. */
    enum { TICKS = 4 };
    render_backend backend = {0}; T(render_null_backend(&backend) == 0);
    editor_config cfg = {.cols = 120u, .rows = 1u + 3u + 20u, .wrap_mode = 1};
    editor *e = NULL; T(editor_open(&e, &cfg, &backend) == 0); T(settle(e) == 0);
    uint64_t id = 0;
    T(editor_results_open(e, &id) == 0 && e->buffer->derived);
    static const char line[] = "a\n";
    T(editor_results_append(e, (const uint8_t *)line, sizeof line - 1u) == 0);
    T(settle_map(e) == 0); T(e->map_cols && e->buffer->map.filled && e->v.state.wrap);
    editor_piece_storage *s = &e->buffer->piece_storage;
    piece_snapshot *held[ATTEMPTS]; size_t holds = 0;
    bool hit = false;
    for (unsigned attempt = 0; attempt < ATTEMPTS && !hit; attempt++) {
        held[holds] = piece_snapshot_take(e->buffer->tree); T(held[holds]); holds++;
        T(editor_results_append(e, (const uint8_t *)line, sizeof line - 1u) == 0);
        s->fail_request = s->requests + 1u;
        int rc = editor_step(e, 0);
        hit = s->requests >= s->fail_request && !e->buffer->map_snapshot && ci6i_map_due(e);
        s->fail_request = 0;
        if (rc) fprintf(stderr, "edit-ci6i: editor_step rc %d error_cause %d (attempt %u, hit %d)\n",
            rc, editor_get_stats(e).error_cause, attempt, hit);
        T(rc == EDITOR_OK || rc == EDITOR_MORE); T(!e->error);
        if (!hit) T(settle_map(e) == 0);
    }
    T(hit);
    /* One refusal: no notice; the editor keeps taking keys. */
    T(!e->recovery_notice);
    T(press(e, key(XKB_KEY_Down, 0, NULL)) == 0); T(!e->error);
    /* Persisting: the first request of each step is refused again; the
     * retry tick fails too and the notice appears. */
    uint64_t deadline = trace_now_ns() + TICKS * JOURNAL_DEFAULT_SYNC_NS;
    while (!e->recovery_notice && trace_now_ns() < deadline) {
        s->fail_request = s->requests + 1u;
        int rc = editor_step(e, 20);
        s->fail_request = 0;
        T(rc == EDITOR_OK || rc == EDITOR_MORE); T(!e->error);
    }
    T(e->recovery_notice && !strcmp(e->recovery_notice, ci6i_notice));
    T(ci6i_map_due(e));
    /* Recovered allocator: the map is prepared on a later tick. */
    deadline = trace_now_ns() + TICKS * JOURNAL_DEFAULT_SYNC_NS;
    while (ci6i_map_due(e) || e->buffer->map_snapshot) {
        int rc = editor_step(e, 20);
        T(rc == EDITOR_OK || rc == EDITOR_MORE); T(trace_now_ns() < deadline);
    }
    T(settle(e) == 0); T(!e->error && !ci6i_map_due(e));
    while (holds) piece_snapshot_release(held[--holds]);
    editor_close(e);
    puts("edit-ci6i: refused minimap snapshot defers the map, keeps the editor, notices persistence passed");
    return 0;
}

/* edit-ci6i (2): the wrapped byte seek (far find result, pending minimap
 * target; editor_wheel_continue -> editor_scroll_resolve) whose resident
 * window cannot be installed is deferred like edit-bt1's slice: the seek
 * stays pending, the view stays, the retry tick lands it, and a refusal
 * persisting past one retry shows edit-bt1's notice (the same resident
 * window and the same refusal). 16 columns: no minimap (P: open.c gives one
 * from 24 columns), whose take would cache each revision's snapshot. */
static int nci6i_seek(editor *e, uint64_t byte)
{
    /* The wrapped seek findpanel.c sets up for a far match. */
    T(scroll_init(&e->wheel, (scroll_config){e->text_grid.dims.rows, 1, 0},
        (scroll_extent){piece_len(e->tree), e->buffer->lines, e->buffer->lg.lines_exact}) == 0);
    e->wheel.first_line = e->v.state.first_line; e->wheel.first_byte = e->v.state.first_byte;
    T(scroll_seek_byte(&e->wheel, byte) == 0);
    e->wheel_resolver = (scroll_resolver){0}; e->wheel_pending = true; e->find_scroll = false;
    return 0;
}
static int nci6i_resolve_refused(void)
{
    /* (P) LINES lines of "a\n" (two bytes each), many more than the text
     * rows, so the seek target is off screen: the last line from the top
     * (byte 0), byte 0 from anywhere else (an earlier attempt's seek). */
    enum { LINES = 512, ATTEMPTS = 64 };
    enum { TICKS = 4 }; /* (E) as in nci6i_map_refused */
    static const char bt1_notice[] = "scrolling deferred: buffer memory exhausted";
    static char text[2u * LINES];
    for (size_t i = 0; i < sizeof text; i++) text[i] = i % 2u ? '\n' : 'a';
    render_backend backend = {0}; T(render_null_backend(&backend) == 0);
    editor_config cfg = {.cols = 16u, .rows = 8u, .wrap_mode = 1};
    editor *e = NULL; T(editor_open(&e, &cfg, &backend) == 0); T(settle(e) == 0);
    uint64_t id = 0;
    T(editor_results_open(e, &id) == 0 && e->buffer->derived);
    T(editor_results_append(e, (const uint8_t *)text, sizeof text) == 0);
    T(settle(e) == 0); T(!e->map_cols && e->v.state.wrap);
    editor_piece_storage *s = &e->buffer->piece_storage;
    piece_snapshot *held[ATTEMPTS]; size_t holds = 0;
    bool hit = false; uint64_t top = 0, target = 0;
    for (unsigned attempt = 0; attempt < ATTEMPTS && !hit; attempt++) {
        held[holds] = piece_snapshot_take(e->buffer->tree); T(held[holds]); holds++;
        T(editor_results_append(e, (const uint8_t *)"a\n", 2u) == 0);
        T(settle(e) == 0);
        top = editor_view(e).first_byte; target = top ? 0 : editor_length(e) - 2u;
        T(nci6i_seek(e, target) == 0);
        s->fail_request = s->requests + 1u;
        int rc = editor_step(e, 0);
        hit = s->requests >= s->fail_request && !e->buffer->scroll_snapshot;
        s->fail_request = 0;
        if (rc) fprintf(stderr, "edit-ci6i: wrapped seek editor_step rc %d error_cause %d (attempt %u, hit %d)\n",
            rc, editor_get_stats(e).error_cause, attempt, hit);
        T(rc == EDITOR_OK || rc == EDITOR_MORE); T(!e->error);
        if (!hit) { T(settle(e) == 0); T(!e->wheel_pending); }
    }
    T(hit);
    T(e->wheel_pending && editor_view(e).first_byte == top && !e->recovery_notice);
    uint64_t deadline = trace_now_ns() + TICKS * JOURNAL_DEFAULT_SYNC_NS;
    while (!e->recovery_notice && trace_now_ns() < deadline) {
        s->fail_request = s->requests + 1u;
        int rc = editor_step(e, 20);
        s->fail_request = 0;
        T(rc == EDITOR_OK || rc == EDITOR_MORE); T(!e->error);
    }
    T(e->recovery_notice && !strcmp(e->recovery_notice, bt1_notice));
    T(e->wheel_pending && editor_view(e).first_byte == top);
    deadline = trace_now_ns() + TICKS * JOURNAL_DEFAULT_SYNC_NS;
    while (e->wheel_pending) {
        int rc = editor_step(e, 20);
        T(rc == EDITOR_OK || rc == EDITOR_MORE); T(trace_now_ns() < deadline);
    }
    T(settle(e) == 0); T(!e->error);
    fprintf(stderr, "edit-ci6i: wrapped seek landed top %llu (from %llu, target %llu)\n",
        (unsigned long long)editor_view(e).first_byte, (unsigned long long)top, (unsigned long long)target);
    T(target ? editor_view(e).first_byte > top : editor_view(e).first_byte == 0);
    while (holds) piece_snapshot_release(held[--holds]);
    editor_close(e);
    puts("edit-ci6i: refused wrapped seek window defers the seek, keeps the view, notices persistence passed");
    return 0;
}
/* edit-yey (P): menu bar contract. Geometry: the File drop-down's longest
 * label + gap + chord + two padding cells, plus the existing minimap; rows
 * are the bar, the tab strip and enough text rows to show File's items.
 * Zero is absence/first index/row, one is a row/index step or a NUL. */
static plat_event key_release(uint32_t sym, uint16_t mods)
{
    plat_event ev = key(sym, mods, NULL); ev.press = false; return ev;
}
static size_t menu_grid_find(editor *e, uint32_t row, const char *text)
{
    const render_grid *grid = editor_grid(e);
    for (size_t column = 0; column + strlen(text) <= grid->dims.cols; column++)
        if (palette_grid_text(e, row, column, text)) return column;
    return SIZE_MAX;
}
static size_t menu_index(size_t menu, keys_action action)
{
    const menu_def *m = &menu_table()[menu];
    for (size_t i = 0; i < m->count; i++) if (m->items[i] == action) return i;
    return SIZE_MAX;
}
static int menu_contract(void)
{
    render_backend backend = {0}; T(render_null_backend(&backend) == 0);
    const char initial[] = "menu buffer";
    size_t items = menu_table()[0].count;
    editor_config cfg = {.initial = (const uint8_t *)initial, .initial_len = sizeof initial - 1u,
        .cols = sizeof " Close Window  Ctrl+Shift+W " - 1u + 8u,
        .rows = 1u + 1u + (uint32_t)items + 1u, .wrap_mode = -1};
    editor *e = NULL; T(editor_open(&e, &cfg, &backend) == 0); T(settle(e) == 0);
    uint32_t text_rows = e->v.config.rows, strip = e->tab_rows;
    T(!e->menu_rows && strip == 1u); /* hidden by default: zero rows */
    T(palette_grid_text(e, strip, e->lay.gw, initial));
    size_t save_as = menu_index(0, KEYS_ACTION_SAVE_AS); T(save_as != SIZE_MAX);
    edit_malloc_guard_begin();
    /* (a) a held Alt changes nothing; a bare press-and-release shows the bar. */
    uint64_t geometry = e->geometry_generation;
    T(press(e, key(XKB_KEY_Alt_L, 0, NULL)) == 0);
    T(!e->menu_rows && e->tab_rows == strip && e->v.config.rows == text_rows);
    T(e->geometry_generation == geometry);
    T(press(e, key_release(XKB_KEY_Alt_L, PLAT_MOD_ALT)) == 0);
    T(e->menu_rows == 1u && e->tab_rows == strip + 1u && e->v.config.rows == text_rows - 1u);
    T(palette_grid_text(e, 0, 1, "File") && menu_grid_find(e, 0, "Edit") != SIZE_MAX);
    T(palette_grid_text(e, strip + 1u, e->lay.gw, initial)); /* grid shifted one row */
    T(press(e, key(XKB_KEY_Escape, 0, NULL)) == 0);
    T(!e->menu_rows && e->tab_rows == strip && e->v.config.rows == text_rows);
    T(palette_grid_text(e, strip, e->lay.gw, initial));
    T(expect(e, initial, 0) == 0);
    /* (b) Alt+F opens File; Save As carries its chord. */
    T(press(e, key(XKB_KEY_Alt_L, 0, NULL)) == 0);
    T(press(e, key('f', PLAT_MOD_ALT, NULL)) == 0);
    T(press(e, key_release(XKB_KEY_Alt_L, PLAT_MOD_ALT)) == 0);
    T(e->menu_rows == 1u && e->menu.open == 0);
    uint32_t item_row = e->menu_rows + (uint32_t)save_as;
    size_t label = menu_grid_find(e, item_row, "Save As"), chord = menu_grid_find(e, item_row, "Ctrl+Shift+S");
    T(label != SIZE_MAX && chord != SIZE_MAX && chord > label);
    /* (c) Escape closes the drop-down, a second Escape hides the bar. */
    T(press(e, key(XKB_KEY_Escape, 0, NULL)) == 0);
    T(e->menu.open < 0 && e->menu_rows == 1u);
    T(press(e, key(XKB_KEY_Escape, 0, NULL)) == 0);
    T(!e->menu_rows && e->v.config.rows == text_rows);
    T(palette_grid_text(e, strip, e->lay.gw, initial));
    T(edit_malloc_guard_end() == 0); /* (P) open/navigate/escape allocate nothing */
    /* (d) Enter on Save As dispatches exactly what its chord does. */
    T(press(e, key('s', PLAT_MOD_CTRL | PLAT_MOD_SHIFT, NULL)) == 0);
    T(e->go.active && e->go.kind == KEYS_ACTION_SAVE_AS);
    editor_goto expected = e->go;
    T(press(e, key(XKB_KEY_Escape, 0, NULL)) == 0); T(!e->go.active);
    T(press(e, key(XKB_KEY_Alt_L, 0, NULL)) == 0);
    T(press(e, key('F', PLAT_MOD_ALT | PLAT_MOD_SHIFT, NULL)) == 0); /* not a mnemonic: Shift */
    T(e->menu.open < 0);
    T(press(e, key_release(XKB_KEY_Alt_L, PLAT_MOD_ALT | PLAT_MOD_SHIFT)) == 0);
    T(!e->menu_rows);
    T(press(e, key(XKB_KEY_Alt_L, 0, NULL)) == 0);
    T(press(e, key('f', PLAT_MOD_ALT, NULL)) == 0);
    T(press(e, key_release(XKB_KEY_Alt_L, PLAT_MOD_ALT)) == 0);
    for (size_t i = 0; i < save_as; i++) T(press(e, key(XKB_KEY_Down, 0, NULL)) == 0);
    T(e->menu.item == save_as);
    T(press(e, key(XKB_KEY_Return, 0, NULL)) == 0);
    T(!e->menu_rows && e->menu.open < 0 && e->go.active && e->go.kind == expected.kind);
    T(e->go.owner == expected.owner && e->go.generation == expected.generation &&
      e->go.length == expected.length && e->go.cursor == expected.cursor && e->go.selected == expected.selected);
    T(press(e, key(XKB_KEY_Escape, 0, NULL)) == 0); T(!e->go.active);
    /* Left/Right switch menus; Escape closes one level each. */
    T(press(e, key('e', PLAT_MOD_ALT, NULL)) == 0); T(e->menu.open == 1);
    T(press(e, key(XKB_KEY_Right, 0, NULL)) == 0); T(e->menu.open == 2);
    T(press(e, key(XKB_KEY_Left, 0, NULL)) == 0); T(press(e, key(XKB_KEY_Left, 0, NULL)) == 0);
    T(e->menu.open == 0);
    T(press(e, key(XKB_KEY_Escape, 0, NULL)) == 0); T(press(e, key(XKB_KEY_Escape, 0, NULL)) == 0);
    T(!e->menu_rows && e->menu.open < 0);
    /* (e) Alt as the modifier of a bound chord: the chord runs, no menu. */
    T(press(e, key('f', PLAT_MOD_CTRL, NULL)) == 0); T(e->find_visible);
    bool match_case = findui_get_state(&e->find).options.match_case;
    uint32_t chord_rows = e->v.config.rows; geometry = e->geometry_generation;
    T(press(e, key(XKB_KEY_Alt_L, 0, NULL)) == 0);
    T(!e->menu_rows && e->v.config.rows == chord_rows && e->geometry_generation == geometry);
    T(press(e, key('c', PLAT_MOD_ALT, NULL)) == 0);
    T(findui_get_state(&e->find).options.match_case != match_case);
    T(e->menu.open < 0 && !e->menu_rows && e->geometry_generation == geometry);
    T(press(e, key_release(XKB_KEY_Alt_L, PLAT_MOD_ALT)) == 0);
    T(e->menu.open < 0 && !e->menu_rows && !e->menu.active);
    T(e->v.config.rows == chord_rows && e->geometry_generation == geometry); /* layout untouched */
    T(press(e, key(XKB_KEY_Escape, 0, NULL)) == 0); T(!e->find_visible);
    /* Hidden menu steals no ordinary key. */
    T(press(e, key('f', 0, "f")) == 0); T(expect(e, "fmenu buffer", 1) == 0);
    T(press(e, key(XKB_KEY_BackSpace, 0, NULL)) == 0); T(expect(e, initial, 0) == 0);
    /* Escape and a typed key in one turn: the edit keeps the new geometry. */
    T(press(e, key(XKB_KEY_Alt_L, 0, NULL)) == 0);
    T(press(e, key_release(XKB_KEY_Alt_L, PLAT_MOD_ALT)) == 0); T(e->menu_rows == 1u);
    plat_event escape = key(XKB_KEY_Escape, 0, NULL), typed = key('f', 0, "f");
    T(editor_inject(e, &escape) == EDITOR_OK && editor_inject(e, &typed) == EDITOR_OK);
    T(settle(e) == 0); T(!e->menu_rows && e->v.config.rows == text_rows);
    T(expect(e, "fmenu buffer", 1) == 0);
    T(press(e, key(XKB_KEY_BackSpace, 0, NULL)) == 0); T(expect(e, initial, 0) == 0);
    /* (f) Mouse: click a title opens it, click an item runs it, click
     * elsewhere closes. Pixel centre = cell * cell size + half a cell. */
    uint32_t cw = e->grid.dims.cell_w, ch = e->grid.dims.cell_h;
    plat_event click = {.kind = PLAT_EV_BUTTON, .press = true, .code = 1};
    T(press(e, key(XKB_KEY_Alt_L, 0, NULL)) == 0);
    T(press(e, key_release(XKB_KEY_Alt_L, PLAT_MOD_ALT)) == 0);
    click.x = (int32_t)(menu_title_column(0) * cw + cw + cw / 2u); click.y = (int32_t)(ch / 2u);
    T(press(e, click) == 0); click.press = false; T(press(e, click) == 0); click.press = true;
    T(e->menu.open == 0);
    click.y = (int32_t)(item_row * ch + ch / 2u);
    T(press(e, click) == 0); click.press = false; T(press(e, click) == 0); click.press = true;
    T(!e->menu_rows && e->go.active && e->go.kind == KEYS_ACTION_SAVE_AS);
    T(press(e, key(XKB_KEY_Escape, 0, NULL)) == 0);
    T(press(e, key(XKB_KEY_Alt_L, 0, NULL)) == 0); T(press(e, key('f', PLAT_MOD_ALT, NULL)) == 0);
    T(press(e, key_release(XKB_KEY_Alt_L, PLAT_MOD_ALT)) == 0);
    click.x = (int32_t)((e->text_grid.dims.cols - 1u) * cw + cw / 2u);
    click.y = (int32_t)((e->grid.dims.rows - 1u) * ch + ch / 2u);
    T(press(e, click) == 0); click.press = false; T(press(e, click) == 0);
    T(!e->menu_rows && e->menu.open < 0 && expect(e, initial, 0) == 0);
    /* Focus loss closes a latched or open menu. */
    T(press(e, key(XKB_KEY_Alt_L, 0, NULL)) == 0);
    T(press(e, key_release(XKB_KEY_Alt_L, PLAT_MOD_ALT)) == 0); T(e->menu_rows);
    plat_event focus = {.kind = PLAT_EV_FOCUS, .focused = false};
    T(press(e, focus) == 0); T(!e->menu_rows);
    editor_close(e);
    /* (g) menu.visible = true keeps the bar; View > Toggle Menu hides it. */
    char path[] = "/tmp/editor-menu-visible-XXXXXX";
    T(trailing_config_file(path, "menu.visible = true\n") == 0);
    cfg.preferences_path = path;
    T(render_null_backend(&backend) == 0);
    T(editor_open(&e, &cfg, &backend) == 0); T(settle(e) == 0);
    T(e->menu_rows == 1u && e->v.config.rows == text_rows - 1u && palette_grid_text(e, 0, 1, "File"));
    T(press(e, key(XKB_KEY_Escape, 0, NULL)) == 0); T(e->menu_rows == 1u);
    T(press(e, key('f', 0, "f")) == 0); T(expect(e, "fmenu buffer", 1) == 0); /* pinned bar steals nothing */
    T(press(e, key(XKB_KEY_BackSpace, 0, NULL)) == 0);
    size_t toggle = menu_index(4, KEYS_ACTION_TOGGLE_MENU); T(toggle != SIZE_MAX);
    T(press(e, key(XKB_KEY_Alt_L, 0, NULL)) == 0); T(press(e, key('v', PLAT_MOD_ALT, NULL)) == 0);
    T(press(e, key_release(XKB_KEY_Alt_L, PLAT_MOD_ALT)) == 0); T(e->menu.open == 4);
    for (size_t i = 0; i < toggle; i++) T(press(e, key(XKB_KEY_Down, 0, NULL)) == 0);
    T(press(e, key(XKB_KEY_Return, 0, NULL)) == 0);
    T(!e->menu_rows && e->v.config.rows == text_rows);
    editor_close(e); T(unlink(path) == 0);
    puts("edit-yey menu bar: bare Alt release shows (held Alt inert), Alt+F, chords, shared Save As, Escape levels, Alt chords, mouse, menu.visible and malloc guard passed");
    return 0;
}


/* edit-2ghs: a binary file (PDF-like: NUL, high bytes, invalid UTF-8, blank
 * and private-use scalars) opened as text must paint, never end the editor.
 * The fixture is generated here, not committed. */
static int binary_open_fixture(char *path, size_t *length)
{
    /* (P) 256 KiB exceeds FILE_PREFIX_MAX-scale prefixes and any viewport;
     * the size only needs to be "a few hundred KB" like the bead's PDF. */
    size_t len = (size_t)256u * 1024u;
    uint8_t *bytes = malloc(len); T(bytes);
    static const uint8_t header[] = "%PDF-1.5\n%\xe2\xe3\xcf\xd3\n";
    /* (P) Scalars a font may draw without ink (NBSP U+00A0, EN SPACE U+2002,
     * IDEOGRAPHIC SPACE U+3000 wide, ZWSP U+200B) and the PUA scalar U+F227
     * that the bead's PDF showed rendering inkless through a fallback face. */
    static const char *const blanks[] = {"\xc2\xa0", "\xe2\x80\x82", "\xe3\x80\x80",
                                         "\xe2\x80\x8b", "\xef\x88\xa7"};
    uint32_t state = 2463534242u; /* (P) xorshift32 seed, any nonzero value */
    size_t at = 0;
    memcpy(bytes, header, sizeof header - 1u); at = sizeof header - 1u;
    while (at < len) {
        state ^= state << 13; state ^= state >> 17; state ^= state << 5;
        size_t pick = state % 16u;
        if (pick < 5u) {
            const char *b = blanks[pick]; size_t n = strlen(b);
            if (at + n > len) break;
            memcpy(bytes + at, b, n); at += n;
        } else if (pick == 5u) bytes[at++] = 0;
        else if (pick == 6u) bytes[at++] = '\n';
        else bytes[at++] = (uint8_t)(state >> 8);
    }
    while (at < len) bytes[at++] = 0;
    int fd = mkstemp(path); T(fd >= 0);
    T(write(fd, bytes, len) == (ssize_t)len); close(fd); free(bytes);
    *length = len;
    return 0;
}
static int binary_open_contract(void)
{
    char path[] = "build/2ghs-binary-XXXXXX"; size_t len = 0;
    T(binary_open_fixture(path, &len) == 0);
    for (int launch = 0; launch < 2; launch++) {
        render_backend backend = {0}; T(render_null_backend(&backend) == 0);
        counted count = {0};
        /* (P) A large viewport, like the bead's 2880x1800 repro, so many
         * distinct clusters reach the glyph table in the first frame. */
        editor_config cfg = {.path = path, .cols = 200, .rows = 60, .wrap_mode = -1,
            .hook_ctx = &count, .on_submit = submitted};
        editor *e = NULL; T(editor_open(&e, &cfg, &backend) == EDITOR_OK);
        /* (P) 200 steps: the bead's acceptance loop. */
        for (int step = 0; step < 200; step++) {
            int rc = editor_step(e, 5);
            if (rc != EDITOR_OK && rc != EDITOR_MORE)
                fprintf(stderr, "editor_test: 2ghs launch %d step %d rc %d\n", launch, step, rc);
            T(rc == EDITOR_OK || rc == EDITOR_MORE);
        }
        T(settle(e) == 0);
        T(count.frames > 0);
        T(render_grid_validate(&e->grid) == RENDER_OK);
        editor_close(e);
    }
    unlink(path);
    puts("edit-2ghs binary file opens, paints and reopens"); return 0;
}
/* edit-6090 (trial, Tobias 2026-10-20): while typing mid-file the view
 * periodically jumps so that only the file's last line is visible. The
 * fixture types a long stream (words, Return, BackSpace) into the middle
 * of a journaled file across index chunk boundaries and the journal's
 * sync/page ticks, with the real wall-clock journal tick between batches,
 * and asserts after every key that the caret's line stays on screen and
 * the top never moves by more than the typed newline (no jump). Sizes (P):
 * the fixture is 3000 lines of 40 bytes (120,000 B, above LINEIDX_CHUNK so
 * typing crosses chunk boundaries); the stream is 3000 keys, enough to pass
 * JOURNAL_DEFAULT_SYNC_BYTES (65,536 B at ~41 B per 1-byte INSERT record)
 * and several JOURNAL_PAGE batches. */
static int n6090_check(editor *e, uint64_t cursor_line, uint64_t previous_top, unsigned at, char what)
{
    editor_buffer *b = e->buffer;
    view_state v = editor_view(e);
    uint32_t rows = e->v.config.rows;
    /* The bead's contract: the top never jumps past the typed newline and
     * the caret's line is never above it. (The caret's row can still lag one
     * key below the unwrapped viewport after a Return at the bottom row:
     * a separate defect, reported in docs/worker-reports/edit-6090-s9.md.) */
    bool below_top = v.first_line <= cursor_line;
    bool jumped = v.first_line > previous_top + 1u;
    if (!below_top || jumped) {
        fprintf(stderr, "edit-6090: key %u '%c': top %llu (was %llu) cursor line %llu rows %u lines %llu"
            " scroll{first %llu/%llu req %d approx %d extent %llu/%llu exact %d} display{%llu/%llu}"
            " pending %d following %d stale %d index complete %d dirty %d lines_exact %d top_estimated %d\n",
            at, what, (unsigned long long)v.first_line, (unsigned long long)previous_top,
            (unsigned long long)cursor_line, rows, (unsigned long long)editor_line_count(e),
            (unsigned long long)b->scroll.first_line, (unsigned long long)b->scroll.first_byte, (int)b->scroll.request,
            (int)b->scroll.approximate, (unsigned long long)b->scroll.extent.lines, (unsigned long long)b->scroll.extent.bytes,
            (int)b->scroll.extent.exact, (unsigned long long)b->scroll_display.first_line, (unsigned long long)b->scroll_display.first_byte,
            (int)b->scroll_pending, (int)b->scroll_following, (int)b->scroll_display_stale,
            b->index ? (int)lineidx_complete(b->index) : -1, (int)b->index_dirty, (int)b->lg.lines_exact, (int)b->lg.top_estimated);
        fprintf(stderr, "    view{line %llu relative %d busy %d cursor %llu} lay{first %llu/%llu src==tree %d approx %d pos %llu} full_pending %d"
            " row_byte[%u]=%llu row_byte[%u]=%llu scroll_cursor %llu repair_valid %d\n",
            (unsigned long long)e->v.line, (int)e->v.relative, (int)view_busy(&e->v), (unsigned long long)v.selection.cursor,
            (unsigned long long)e->lay.first_line, (unsigned long long)e->lay.first_byte, (int)(e->lay.src == e->tree), (int)e->lay.approximate,
            (unsigned long long)e->lay.pos, (int)e->full_pending, rows - 1u, (unsigned long long)e->row_byte[rows - 1u], rows,
            (unsigned long long)e->row_byte[rows], (unsigned long long)b->scroll_cursor, (int)e->repair_line_valid);
        uint8_t text[200]; uint64_t from = e->row_byte[rows - 1u], to = e->lay.pos;
        if (from != LAYOUT_VOID_ROW && to > from && to - from <= sizeof text && editor_read(e, from, text, (size_t)(to - from)) == 0) {
            fprintf(stderr, "    rows %u..: [", rows - 1u);
            for (size_t i = 0; i < to - from; i++) fputs(text[i] == '\n' ? "\\n" : (char[]){(char)text[i], 0}, stderr);
            fprintf(stderr, "] length %llu\n", (unsigned long long)editor_length(e));
        }
        return 1;
    }
    return 0;
}
static int n6090_tick(editor *e)
{
    /* The journal's durability tick (JOURNAL_DEFAULT_SYNC_NS) is wall-clock;
     * let one pass between batches, stepping as the main loop would. */
    uint64_t until = trace_now_ns() + JOURNAL_DEFAULT_SYNC_NS + JOURNAL_DEFAULT_SYNC_NS / 4u;
    while (trace_now_ns() < until) { int rc = editor_step(e, 20); T(rc == EDITOR_OK || rc == EDITOR_MORE); }
    return settle(e);
}
/* One run: `long_tail` puts 300 lines of 420 B after 2000 lines of 22 B so
 * the line-density estimate of the edited chunk overshoots the real count
 * (P: average 59 B/line over the prefix against 420 B/line at the top);
 * `burst` keys are injected per settle so a key can land while layout is
 * still busy (fast typing, key repeat). (P) sizes: see the fixture. */
static int n6090_case(bool wrap, bool long_tail, unsigned burst, uint64_t start_line, unsigned keys)
{
    char source[] = "build/editor-6090-XXXXXX.txt";
    int fd = mkstemps(source, (int)(sizeof ".txt" - 1u)); T(fd >= 0);
    FILE *f = fdopen(fd, "w"); T(f);
    char filler[409]; memset(filler, 'x', sizeof filler - 1u); filler[sizeof filler - 1u] = 0;
    for (unsigned i = 0; i < 3000u; i++) {
        if (long_tail && i >= 2000u) fprintf(f, "line %04u: %s\n", i, filler); /* (P) 420 B */
        else if (long_tail) fprintf(f, "line %04u: short text\n", i); /* (P) 22 B */
        else fprintf(f, "line %04u: the quick brown fox jumps over\n", i); /* (P) 42 B */
        if (long_tail && i == 2299u) break;
    }
    T(fclose(f) == 0);
    char log_path[] = "/tmp/editor-6090-XXXXXX"; int lfd = mkstemp(log_path); T(lfd >= 0); T(close(lfd) == 0);
    render_backend backend = {0}; T(render_null_backend(&backend) == 0);
    counted count = {0};
    editor_config cfg = {.path = source, .journal_path = log_path, .cols = 80u, .rows = 1u + 3u + 24u, .wrap_mode = wrap ? 1 : -1,
        .hook_ctx = &count, .on_ingress = ingress, .on_submit = submitted};
    editor *e = NULL; T(editor_open(&e, &cfg, &backend) == 0); T(settle(e) == 0);
    T(editor_view(e).wrap == wrap);
    int jr;
    while ((jr = editor_jump_line(e, start_line)) == EDITOR_MORE) T(settle(e) == 0);
    T(jr == 0); T(settle(e) == 0);
    uint64_t cursor_line = start_line, top = editor_view(e).first_line;
    T(top <= cursor_line && cursor_line < top + e->v.config.rows);
    const char words[] = "lorem ipsum dolor sit amet consectetur ";
    unsigned column = 0; size_t w = 0; int failed = 0; unsigned typed_lines = 0; bool blank = false;
    for (unsigned at = 0; at < keys && failed < 40; ) {
        unsigned n = 0; char last = 0;
        for (; n < burst && at + n < keys; n++) {
            char c; plat_event ev;
            if ((at + n) % 97u == 96u) { c = '\b'; ev = key(XKB_KEY_BackSpace, 0, NULL); }
            else if (blank) { c = '\n'; ev = key(XKB_KEY_Return, 0, NULL); blank = false; } /* paragraph break */
            else if (column >= 40u) { c = '\n'; ev = key(XKB_KEY_Return, 0, NULL); blank = ++typed_lines % 4u == 0; }
            else { c = words[w++ % (sizeof words - 1u)]; char one[2] = {c, 0}; ev = key((uint32_t)(uint8_t)c, 0, one); }
            T(editor_inject(e, &ev) == 0);
            if (c == '\n') { cursor_line++; column = 0; }
            else if (c == '\b') { if (column) column--; }
            else column++;
            last = c;
        }
        T(settle(e) == 0);
        at += n;
        failed += n6090_check(e, cursor_line, top + n, at - 1u, last == '\n' ? 'N' : last == '\b' ? 'B' : last);
        top = editor_view(e).first_line;
        if (at % 500u < burst) { T(n6090_tick(e) == 0); failed += n6090_check(e, cursor_line, top, at - 1u, 'T'); }
    }
    T(!count.active && count.allocations == 0); /* (P) no typing-path allocations */
    editor_stats s = editor_get_stats(e); T(!s.journal_error);
    editor_close(e); T(unlink(log_path) == 0); T(unlink(source) == 0);
    T(!failed);
    return 0;
}
static int n6090_typing_top(void)
{
    T(n6090_case(true, false, 1u, 3000u, 3000u) == 0); /* wrapped (.md/.txt/.tex default), typing at the end of the file */
    T(n6090_case(true, false, 1u, 2999u, 3000u) == 0); /* wrapped, on the last text line */
    T(n6090_case(true, false, 1u, 1500u, 3000u) == 0);
    T(n6090_case(true, true, 1u, 2299u, 1500u) == 0);
    T(n6090_case(false, false, 1u, 2999u, 3000u) == 0); /* unwrapped, appending at the end of the file */
    T(n6090_case(false, true, 1u, 2299u, 1500u) == 0);
    T(n6090_case(false, true, 3u, 2260u, 1500u) == 0);
    T(n6090_case(false, false, 1u, 1500u, 3000u) == 0);
    puts("edit-6090: mid-file typing keeps the caret's line on screen and the top steady");
    return 0;
}

int main(int argc, char **argv)
{
    trace_init(); T(trace_thread_register() >= 0);
    bool headless = argc == 2 && !strcmp(argv[1], "--headless");
    if (argc == 2) {
        /* (P) argv[1] is the selector after the program name. */
        if (!strcmp(argv[1], "--paint-all-void")) return paint_all_void();
        if (!strcmp(argv[1], "--2ghs-binary")) return binary_open_contract();
        if (!strcmp(argv[1], "--recovery-findpanel")) return recovery_notice_findpanel();
        if (!strcmp(argv[1], "--ywd-palette")) return palette_contract();
        if (!strcmp(argv[1], "--yey-menu")) return menu_contract();
        if (!strcmp(argv[1], "--trailing-default")) return trailing_typing(NULL);
        if (!strcmp(argv[1], "--trailing-all")) return trailing_all();
        if (!strcmp(argv[1], "--trailing-selection")) return trailing_selection();
        if (!strcmp(argv[1], "--trailing-disabled-scan")) return trailing_disabled_scan();
        if (!strcmp(argv[1], "--g02-find")) return g02_find_return();
        if (!strcmp(argv[1], "--n95-fif")) return n95_find_in_files();
        if (!strcmp(argv[1], "--3f2k-save")) return n3f2k_save_with_results();
        if (!strcmp(argv[1], "--6090-typing")) return n6090_typing_top();
        if (!strcmp(argv[1], "--5vz-full")) return n5vz_results_full();
        if (!strcmp(argv[1], "--5vz-open")) return n5vz_open_refused();
        if (!strcmp(argv[1], "--bt1-geometry")) return nbt1_results_geometry();
        if (!strcmp(argv[1], "--ci6i-map")) return nci6i_map_refused();
        if (!strcmp(argv[1], "--ci6i-seek")) return nci6i_resolve_refused();
        if (!strcmp(argv[1], "--g02-previous")) return g02_find_previous_uncached();
        if (!strcmp(argv[1], "--duplicate-path")) return duplicate_path_contract();
        if (!strncmp(argv[1], "--o20-", sizeof "--o20-" - 1u)) return !strcmp(argv[1], "--o20-all") ? o20_all() : o20_case(argv[1] + sizeof "--o20-" - 1u);
        if (!strcmp(argv[1], "--48u-overscan")) return pointer_overscan_contract();
        if (!strcmp(argv[1], "--48u-buffer")) return pointer_chain_contract(false);
        if (!strcmp(argv[1], "--48u-button")) return pointer_chain_contract(true);
        if (!strcmp(argv[1], "--48u-empty")) return pointer_empty_rows_contract();
        if (!strcmp(argv[1], "--48u-resize")) return findpanel_resize_contract();
        if (!strcmp(argv[1], "--48u-drag")) return pointer_autoscroll_contract(false);
        if (!strcmp(argv[1], "--48u-drag-wrap")) return pointer_autoscroll_contract(true);
        if (!strcmp(argv[1], "--48u-escape")) return findpanel_escape_contract();
        if (!strcmp(argv[1], "--hl-allocation")) return allocation_case(false,true);
        if (!strcmp(argv[1], "--m15-gutter")) return gutter_click_contract();
        if (!strcmp(argv[1], "--a7fo-click")) return a7fo_click_contract(1,false,0) ||
            a7fo_click_contract(-1,false,0) || a7fo_click_contract(1,true,0) || a7fo_click_contract(1,true,30) ||
            a7fo_click_contract(-1,true,30);
        if (!strcmp(argv[1], "--m15-gutter-growth")) return gutter_growth_contract();
        if (!strcmp(argv[1], "--m15-gutter-wrap")) return gutter_wrap_contract();
        if (!strcmp(argv[1], "--m15-follow")) return find_follow_before_top_contract();
        if (!strcmp(argv[1], "--dr3-gutter")) return gutter_line_contract();
        if (!strcmp(argv[1], "--minimap-cost")) return minimap_compose_cost();
        if (!strcmp(argv[1], "--minimap-grab-inside")) return minimap_grab_contract(false);
        if (!strcmp(argv[1], "--minimap-grab-outside")) return minimap_grab_contract(true);
        if (!strcmp(argv[1], "--minimap-raster")) return minimap_compose_pixels("raster");
        if (!strcmp(argv[1], "--minimap-gl")) return minimap_compose_pixels("gl");
        if (!strcmp(argv[1], "--minimap-pixels")) return minimap_compose_pixels(NULL);
        if (!strcmp(argv[1], "--73j-headless")) return mouse_contract() || wheel_contract() ||
            findpanel_contract() || findpanel_cancel_error_contract() || findpanel_far_wrap_contract() || unicode_editor_contract() || minimap_pending_contract() || minimap_protected_contract() ||
            minimap_typing_flash() || script(false) || allocation_test(false);
        if (!strcmp(argv[1], "--73j-find")) return findpanel_contract() || findpanel_cancel_error_contract() || findpanel_far_wrap_contract();
        if (!strcmp(argv[1], "--73j-font")) return unicode_editor_contract();
        if (!strcmp(argv[1], "--73j-minimap")) return minimap_pending_contract() || minimap_protected_contract();
        if (!strcmp(argv[1], "--73j-mouse")) return mouse_contract();
        if (!strcmp(argv[1], "--73j-wheel")) return wheel_contract();
        if (!strcmp(argv[1], "--minimap-flash")) return minimap_typing_flash();
        if (!strcmp(argv[1], "--default-soft-capacity")) return default_soft_capacity();
        if (!strcmp(argv[1], "--default-history-anchor")) return default_history_anchor();
        if (!strcmp(argv[1], "--default-notices")) return default_notices();
        if (!strcmp(argv[1], "--default-soft-lines")) return default_soft_lines();
        if (!strcmp(argv[1], "--default-repeat")) return default_chord_repeat();
        if (!strcmp(argv[1], "--default-clipboard-line")) return default_clipboard_line();
        if (!strcmp(argv[1], "--default-soft")) return default_soft_history();
        if (!strcmp(argv[1], "--default-noop")) return default_noop();
        if (!strcmp(argv[1], "--default-states")) return default_states();
        if (!strcmp(argv[1], "--default-center")) return default_center();
        if (!strcmp(argv[1], "--default-skip")) return default_skip();
        if (!strcmp(argv[1], "--default-word")) return default_word();
        if (!strcmp(argv[1], "--default-goto-cold")) return default_goto_cold();
        if (!strcmp(argv[1], "--default-goto")) return default_goto();
        if (!strcmp(argv[1], "--default-comments")) return default_comments();
        if (!strcmp(argv[1], "--undo-shrink-top")) return undo_shrink_top();
        if (!strcmp(argv[1], "--goto-scroll")) return goto_scroll();
        if (!strcmp(argv[1], "--default-edits")) return default_edits();
        if (!strcmp(argv[1], "--selection-chords")) return selection_chords();
        if (!strcmp(argv[1], "--redo-or-repeat")) return redo_or_repeat();
        if (!strcmp(argv[1], "--panel-state")) return panel_shortcuts(false);
        if (!strcmp(argv[1], "--panel-notice")) return panel_shortcuts(true);
        if (!strcmp(argv[1], "--clipboard-no-tabs")) return clipboard_no_tabs_test();
        if (!strcmp(argv[1], "--clipboard-primary")) return clipboard_pending_primary_test();
        if (!strcmp(argv[1], "--clipboard-bulk")) return clipboard_large_test();
        if (!strcmp(argv[1], "--clipboard-native")) return clipboard_test(true);
        if (!strcmp(argv[1], "--clipboard-crlf")) return clipboard_crlf_line_test() || clipboard_endings_test() || clipboard_crlf_empty_test() || clipboard_crlf_bulk_test();
        if (!strcmp(argv[1], "--clipboard")) return clipboard_test(false) || clipboard_large_test() || clipboard_middle_long_test() || clipboard_pending_primary_test() || clipboard_no_tabs_test();
        if (!strcmp(argv[1], "--ck4-target")) return ck4_target();
        if (!strcmp(argv[1], "--ck4-fifo")) return ck4_fifo();
        if (!strcmp(argv[1], "--ck4-metadata")) return ck4_metadata_test();
        if (!strcmp(argv[1], "--ck4-credit")) return ck4_credit();
        if (!strcmp(argv[1], "--ck4-startup")) return ck4_startup();
        if (!strcmp(argv[1], "--ck4-save")) return ck4_recovery(0);
        if (!strcmp(argv[1], "--clean-source-change")) return clean_source_change();
        if (!strcmp(argv[1], "--source-prompt-placement")) return source_prompt_placement();
        if (!strcmp(argv[1], "--ck4-keep")) return ck4_recovery(1);
        if (!strcmp(argv[1], "--ck4-reload")) return ck4_recovery(2);
        if (!strcmp(argv[1], "--ck4-mapped-keep")) return ck4_recovery(3);
        if (!strcmp(argv[1], "--ck4-mapped-reload")) return ck4_recovery(4);
        if (!strcmp(argv[1], "--index-capacity")) return index_capacity_typing();
        if (!strcmp(argv[1], "--index-refusal")) return index_capacity_refusal();
        if (!strcmp(argv[1], "--index-repair")) return index_eventual_repair();
        if (!strcmp(argv[1], "--raster-blink")) return raster_blink_damage();
        if (!strcmp(argv[1], "--line-start-only")) return partial_line_start();
        if (!strcmp(argv[1], "--selection")) return backend_selection();
        if (!strcmp(argv[1], "--fallback")) return backend_fallback();
        if (!strcmp(argv[1], "--fallback-headless")) return backend_fallback_headless();
        if (!strcmp(argv[1], "--gpu-completion")) return backend_gpu_completion();
        if (!strcmp(argv[1], "--backend-only")) return selected_backend_test(getenv("EDIT_BACKEND"), false);
        if (!strcmp(argv[1], "--require-gl")) return selected_backend_test("gl", true);
        if (!headless) return 2;
    }
    const char *allocation = getenv("EDITOR_ALLOC_ONLY");
    if (allocation) return allocation_test(!strcmp(allocation, "raster"));
    const char *only = getenv("EDITOR_REVIEW_ONLY");
    if (only) return review_suite(only);
    T(trailing_typing(NULL) == 0); T(trailing_all() == 0);
    T(trailing_selection() == 0); T(trailing_disabled_scan() == 0);
    T(paint_all_void() == 0);
    T(binary_open_contract() == 0);
    T(duplicate_path_contract() == 0);
    T(recovery_notice_findpanel() == 0);
    T(o20_all() == 0);
    T(palette_contract() == 0);
    T(menu_contract() == 0);
    T(g02_find_return() == 0);
    T(n95_find_in_files() == 0);
    T(n3f2k_save_with_results() == 0);
    T(n6090_typing_top() == 0);
    T(n5vz_results_full() == 0); T(n5vz_open_refused() == 0);
    T(nbt1_results_geometry() == 0);
    T(nci6i_map_refused() == 0);
    T(nci6i_resolve_refused() == 0);
    T(g02_find_previous_uncached() == 0);
    T(minimap_grab_contract(false) == 0); T(minimap_grab_contract(true) == 0);
    T(minimap_compose_pixels(NULL) == 0);
    T(pointer_overscan_contract()==0); T(pointer_chain_contract(false)==0); T(pointer_chain_contract(true)==0);
    T(pointer_empty_rows_contract()==0); T(findpanel_resize_contract()==0);
    T(pointer_autoscroll_contract(false)==0); T(pointer_autoscroll_contract(true)==0); T(findpanel_escape_contract()==0);
    T(gutter_click_contract()==0); T(gutter_growth_contract()==0); T(gutter_wrap_contract()==0);
    T(gutter_line_contract()==0);
    T(a7fo_click_contract(1,false,0)==0); T(a7fo_click_contract(-1,false,0)==0);
    T(a7fo_click_contract(1,true,0)==0); T(a7fo_click_contract(1,true,30)==0); T(a7fo_click_contract(-1,true,30)==0);
    T(find_follow_before_top_contract()==0);
    T(mouse_contract()==0); T(wheel_contract()==0); T(findpanel_contract()==0); T(findpanel_cancel_error_contract()==0); T(findpanel_far_wrap_contract()==0);
    T(unicode_editor_contract()==0); T(minimap_pending_contract()==0); T(minimap_protected_contract()==0);
    T(minimap_typing_flash() == 0);
    T(default_center() == 0); T(default_skip() == 0); T(default_word() == 0); T(default_goto() == 0); T(default_goto_cold() == 0); T(default_comments() == 0); T(undo_shrink_top() == 0); T(goto_scroll() == 0); T(default_edits() == 0); T(default_states() == 0); T(default_noop() == 0); T(default_soft_history() == 0); T(default_clipboard_line() == 0); T(default_chord_repeat() == 0); T(default_soft_lines() == 0); T(default_notices() == 0); T(default_history_anchor() == 0); T(default_soft_capacity() == 0);
    T(selection_chords() == 0);
    T(redo_or_repeat() == 0); T(panel_shortcuts(true) == 0);
    T(clipboard_crlf_line_test() == 0);
    T(clipboard_endings_test() == 0);
    T(clipboard_crlf_empty_test() == 0);
    T(clipboard_crlf_bulk_test() == 0);
    T(clipboard_test(false) == 0); T(clipboard_large_test() == 0); T(clipboard_middle_long_test() == 0); T(clipboard_pending_primary_test() == 0); T(clipboard_no_tabs_test() == 0);
    T(ck4_recovery(0) == 0); T(ck4_startup() == 0);
    T(clean_source_change() == 0);
    T(source_prompt_placement() == 0);
    T(ck4_credit() == 0); T(ck4_metadata_test() == 0); T(ck4_target() == 0); T(ck4_fifo() == 0);
    T(ck4_recovery(1) == 0); T(ck4_recovery(2) == 0); T(ck4_recovery(3) == 0); T(ck4_recovery(4) == 0);
    T(review_suite(headless ? "headless" : NULL) == 0);
    T(index_capacity_typing() == 0); T(index_capacity_refusal() == 0); T(index_eventual_repair() == 0);
    T(partial_line_start() == 0);
    T(backend_selection() == 0);
    T((headless ? backend_fallback_headless() : backend_fallback()) == 0);
    T(backend_gpu_completion() == 0);
    const char *name = getenv("EDIT_BACKEND");
    if (!headless) {
        if (name) T(selected_backend_test(name, false) == 0);
        else { T(selected_backend_test("gl", false) == 0); T(selected_backend_test("raster", false) == 0); }
    }
    T(scrolled_undo() == 0);
    T(stopped_error() == 0);
    T(script(false) == 0); T(allocation_test(false) == 0); T(allocation_case(false,true) == 0);
    if (!headless) {
        T(script(true) == 0); T(allocation_test(true) == 0);
        T(raster_blink_damage() == 0);
    }
    T(idle_policy() == 0);
    T(queue_depth() == 0);
    if (!headless) T(native_input() == 0);
    puts(headless ? "editor_test: headless passed" : "editor_test: all passed"); return 0;
}
