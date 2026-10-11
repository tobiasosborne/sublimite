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
    T(memcmp(bytes, text, n) == 0); T(editor_view(e).selection.cursor == cursor);
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
    T(editor_step(e, 0) >= 0); T(settle(e) == 0);
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
static int minimap_compose_pixels(const char *backend_name)
{
    render_backend backend = {0};
    T((backend_name ? editor_backend_select(&backend, backend_name) : render_null_backend(&backend)) == 0);
    const char fixture[] = "a b\n  cd\n\txyz\nq\nr\ns\nt\nu\nv\nw\nx\ny\nz\n";
    editor_config cfg = {.initial = (const uint8_t *)fixture, .initial_len = sizeof fixture - 1,
        .cols = 40, .rows = 8};
    editor *e = NULL; T(editor_open(&e, &cfg, &backend) == 0); T(settle(e) == 0);
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
    minimap_target target; T(minimap_hit(&e->buffer->map, &input, e->buffer->index, summary_row, &target) == 0);
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
    T(expect(e, text, sizeof text - 1u) == 0);
    T(editor_get_stats(e).mutations == mutations); /* No repeat record exists. */
    T(press(e, key('z', PLAT_MOD_CTRL, NULL)) == 0);
    T(press(e, key('Z', PLAT_MOD_CTRL | PLAT_MOD_SHIFT, NULL)) == 0);
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
    editor_close(e); puts("editor_test: Ctrl+Y redoes inserts/deletes, empty redo is inert, Ctrl+Shift+Z still redoes");
    return 0;
}
static int panel_shortcuts(bool notice)
{
    static const char initial[] = "left right\nnext";
    static const char typed[] = "leftalpha right\nnext";
    static const char status[] = "not implemented";
    const struct { uint32_t sym; uint16_t mods; } shortcuts[] = {
        {'f', PLAT_MOD_CTRL}, {'h', PLAT_MOD_CTRL}, {XKB_KEY_F3, 0},
        {'F', PLAT_MOD_CTRL | PLAT_MOD_SHIFT}, {'p', PLAT_MOD_CTRL}, {'g', PLAT_MOD_CTRL}
    }; /* Each entry derives from the brief's requested shortcut set. */
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
            T(memcmp(e->grid.cells + bottom, bottom_before, sizeof bottom_before) == 0);
            T(e->grid.cells[(size_t)e->tab_rows * e->grid.dims.cols].glyph_index == (uint8_t)'S');
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
static int allocation_test(bool raster)
{
    render_backend b = {0}; T((raster ? render_cpu_backend(&b) : render_null_backend(&b)) == 0);
    counted c = {0}; char path[] = "/tmp/editor-alloc-XXXXXX"; int fd = mkstemp(path); T(fd >= 0); close(fd);
    editor_config cfg = {.cols = 40, .rows = 8, .journal_path = path,
        .hook_ctx = &c, .on_ingress = ingress, .on_submit = submitted, .on_io = io_boundary};
    editor *e = NULL; T(editor_open(&e, &cfg, &b) == 0); T(settle(e) == 0);
    for (unsigned i = 0; i < 10000; i += 100) {
        for (unsigned j = 0; j < 100; j++) {
            plat_event ev = j % 2 ? key(XKB_KEY_BackSpace, 0, NULL) : key('x', 0, "x");
            T(editor_inject(e, &ev) == 0);
        }
        T(settle(e) == 0);
    }
    T(!c.active && !c.suspended);
    if (c.allocations) fprintf(stderr, "editor allocation diagnostic: backend=%s allocations=%zu\n", raster ? "raster" : "null", c.allocations);
    T(c.allocations == 0); T(editor_length(e) == 0);
    editor_stats s = editor_get_stats(e); T(s.mutations == 10000 && s.journal_records == 10000 && !s.journal_error);
    T(editor_flush(e) == 0); editor_close(e); unlink(path);
    printf("editor_test: %s 10000 keys mallocs=%zu guard=%s\n", raster ? "raster" : "null", c.allocations,
        edit_malloc_guard_active() ? "active" : "ASan-inert");
    return 0;
}
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
        /* IO accepted the first record into the journal's retained batch;
         * FULL accepted nothing. Preserve only the unaccepted editor suffix. */
        T(e->op_count == (mode ? 1u : 2u)); T(e->stage_used == 2);
        if (mode) {
            T(e->ops[0].off == 1 && journal_get_stats(e->journal).accepted_sequence == accepted + 1);
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
    unlink(source); unlink(logs[0]); unlink(logs[1]); T(rmdir(root) == 0);
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
static int clipboard_test(bool native)
{
    const char original[] = "a\r\n中";
    const char normal[] = "a\n中";
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
    const char doubled[] = "a\r\n中a\n中";
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
int main(int argc, char **argv)
{
    trace_init(); T(trace_thread_register() >= 0);
    bool headless = argc == 2 && !strcmp(argv[1], "--headless");
    if (argc == 2) {
        if (!strcmp(argv[1], "--minimap-cost")) return minimap_compose_cost();
        if (!strcmp(argv[1], "--minimap-raster")) return minimap_compose_pixels("raster");
        if (!strcmp(argv[1], "--minimap-gl")) return minimap_compose_pixels("gl");
        if (!strcmp(argv[1], "--minimap-pixels")) return minimap_compose_pixels(NULL);
        if (!strcmp(argv[1], "--minimap-flash")) return minimap_typing_flash();
        if (!strcmp(argv[1], "--selection-chords")) return selection_chords();
        if (!strcmp(argv[1], "--redo-or-repeat")) return redo_or_repeat();
        if (!strcmp(argv[1], "--panel-state")) return panel_shortcuts(false);
        if (!strcmp(argv[1], "--panel-notice")) return panel_shortcuts(true);
        if (!strcmp(argv[1], "--clipboard-no-tabs")) return clipboard_no_tabs_test();
        if (!strcmp(argv[1], "--clipboard-primary")) return clipboard_pending_primary_test();
        if (!strcmp(argv[1], "--clipboard-bulk")) return clipboard_large_test();
        if (!strcmp(argv[1], "--clipboard-native")) return clipboard_test(true);
        if (!strcmp(argv[1], "--clipboard")) return clipboard_test(false) || clipboard_large_test() || clipboard_middle_long_test() || clipboard_pending_primary_test() || clipboard_no_tabs_test();
        if (!strcmp(argv[1], "--ck4-target")) return ck4_target();
        if (!strcmp(argv[1], "--ck4-fifo")) return ck4_fifo();
        if (!strcmp(argv[1], "--ck4-metadata")) return ck4_metadata_test();
        if (!strcmp(argv[1], "--ck4-credit")) return ck4_credit();
        if (!strcmp(argv[1], "--ck4-startup")) return ck4_startup();
        if (!strcmp(argv[1], "--ck4-save")) return ck4_recovery(0);
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
    T(minimap_compose_pixels(NULL) == 0);
    T(minimap_typing_flash() == 0);
    T(selection_chords() == 0);
    T(redo_or_repeat() == 0); T(panel_shortcuts(true) == 0);
    T(clipboard_test(false) == 0); T(clipboard_large_test() == 0); T(clipboard_middle_long_test() == 0); T(clipboard_pending_primary_test() == 0); T(clipboard_no_tabs_test() == 0);
    T(ck4_recovery(0) == 0); T(ck4_startup() == 0);
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
    T(script(false) == 0); T(allocation_test(false) == 0);
    if (!headless) {
        T(script(true) == 0); T(allocation_test(true) == 0);
        T(raster_blink_damage() == 0);
    }
    T(idle_policy() == 0);
    T(queue_depth() == 0);
    if (!headless) T(native_input() == 0);
    puts(headless ? "editor_test: headless passed" : "editor_test: all passed"); return 0;
}
