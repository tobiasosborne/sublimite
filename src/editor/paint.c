#include "editor/private.h"
#include "editor/theme.h"
#include "trace/trace.h"
#include <string.h>

static void paint(void *ctx, uint64_t byte, uint32_t *fg, uint32_t *bg, uint16_t *attrs)
{
    editor *e = ctx;
    if (!e->paint_ready) return;
    if (byte == e->bracket_source || byte == e->bracket_mate) {
        *bg = 0x53667e; *attrs |= RENDER_ATTR_UNDERLINE;
    }
    size_t lo = 0, hi = e->ws_count;
    while (lo < hi) { size_t mid = lo + (hi - lo) / 2; if (e->ws_ranges[mid].hi <= byte) lo = mid + 1; else hi = mid; }
    if (lo < e->ws_count && e->ws_ranges[lo].lo <= byte) { *fg = 0xbf616a; *bg = 0x493038; *attrs |= RENDER_ATTR_UNDERLINE; }
}
static bool byte_visible(editor *e, uint64_t byte)
{
    if (byte >= piece_len(e->tree)) return false;
    if (e->lay.wrap) {
        for (uint32_t r = 0; r < e->text_grid.dims.rows; r++) {
            const layout_wrap_row *row = &e->lay.wrap_rows[r];
            if (row->start != LAYOUT_VOID_ROW && byte >= row->start && byte < row->end) return true;
        }
        return false;
    }
    uint64_t line = piece_byte_to_line(e->tree, byte);
    if (line < e->v.state.first_line || line - e->v.state.first_line >= e->text_grid.dims.rows) return false;
    uint64_t start = e->row_byte[line - e->v.state.first_line];
    if (start == LAYOUT_VOID_ROW || byte < start || byte - start >= LAYOUT_BYTE_BUDGET) return false;
    size_t n = (size_t)(byte - start + 1), at = 0; uint64_t col = 0;
    if (piece_read(e->tree, start, e->indent_bytes, n)) return false;
    while (at + 1 < n) {
        size_t step = utf8_grapheme_next(e->indent_bytes + at, n - at);
        if (step > n - 1 - at) return false;
        int width = utf8_cluster_width(e->indent_bytes + at, step);
        col += e->indent_bytes[at] == '\t' ? e->lay.tab - col % e->lay.tab : (uint64_t)width;
        at += step;
    }
    return col >= e->lay.hscroll && col - e->lay.hscroll < e->lay.text_cols;
}
int editor_paint_prepare(editor *e)
{
    if (e->paint_ready) return 0;
    uint64_t lo = e->v.state.wrap ? e->v.state.visual_byte : e->v.state.first_byte;
    uint64_t hi = e->lay.wrap ? lo : e->lay.pos;
    for (uint32_t r = 0; r < e->text_grid.dims.rows; r++) if (e->row_byte[r] != LAYOUT_VOID_ROW) {
        uint64_t end;
        if (e->lay.wrap) end = e->lay.wrap_rows[r].next == LAYOUT_VOID_ROW ? e->lay.wrap_rows[r].end : e->lay.wrap_rows[r].next;
        else end = hi;
        if (end != LAYOUT_VOID_ROW && end > hi) hi = end;
    }
    if (hi > piece_len(e->tree)) hi = piece_len(e->tree);
    /* No-wrap may skip an unbounded unused line tail. Decorations use only
     * its bounded prepared byte window; a cropped whitespace run is omitted
     * by indent until a visible terminator establishes it. */
    if (!e->lay.wrap && hi - lo > LAYOUT_BYTE_BUDGET) hi = lo + LAYOUT_BYTE_BUDGET;
    e->bracket_source = e->bracket_mate = INDENT_NONE; e->ws_count = 0;
    indent_code rc = indent_bracket_match(e->tree, e->v.state.selection.cursor, lo, hi, &e->bracket_mate);
    if (rc != INDENT_OK) return EDITOR_ERR_ARG;
    if (e->bracket_mate != INDENT_NONE) {
        uint64_t source = e->v.state.selection.cursor; uint8_t ch = 0;
        if (source >= lo && source < hi) (void)piece_read(e->tree, source, &ch, 1);
        if (!strchr("()[]{}", ch) || !ch) source--;
        if (byte_visible(e, source) && byte_visible(e, e->bracket_mate)) e->bracket_source = source;
        else e->bracket_mate = INDENT_NONE;
    }
    size_t count = 0;
    rc = indent_trailing_ws_ranges(e->tree, lo, hi, e->ws_ranges, e->max_rows, &count);
    if (rc != INDENT_OK && rc != INDENT_ERR_CAPACITY) return EDITOR_ERR_ARG;
    e->ws_count = count < e->max_rows ? count : e->max_rows;
    /* The existing decoration relayout writes the gutter too: choose its
     * theme colour once, with no extra row pass or allocation. Publication
     * already requests full layout before this frame reaches submit. */
    bool estimated = !e->buffer->lg.lines_exact || e->buffer->lg.top_estimated;
    e->lay.cfg.gutter_fg = estimated
        ? editor_theme_estimated_gutter(e->layout_cfg.gutter_fg, e->layout_cfg.gutter_bg)
        : e->layout_cfg.gutter_fg;
    e->paint_ready = true;
    layout_set_paint(&e->lay, paint, e);
    int lr = layout_relayout_rows(&e->lay, 0, e->text_grid.dims.rows);
    return lr < 0 ? lr : 0;
}
static bool glyph(void *ctx, const uint8_t *text, size_t len, int width, uint32_t *identity, uint32_t *slot)
{
    (void)ctx;
    if (width != 1) return false; /* no resident wide atlas image */
    uint32_t ch = len == 1 && text[0] >= 0x20 && text[0] <= 0x7e ? text[0] : '?';
    if (len == 3 && !memcmp(text, "\xe2\x80\xa6", 3)) ch = '.';
    *identity = ch; *slot = ch - 0x20u; return true;
}
int editor_compose(editor *e)
{
    if (e->buffer->source_stale) {
        const char status[] = "Source changed: R reload / K keep";
        uint32_t cols = e->grid.dims.cols;
        for (uint32_t c = 0; c < cols; c++) {
            uint32_t ch = c < sizeof status - 1 ? (uint8_t)status[c] : (uint32_t)' ';
            e->grid.cells[c] = (render_cell){ch, ch - 0x20u, 0xe5b56b, 0x171b22, 0, 0};
        }
        return render_mark_rows(&e->grid, 0, 1);
    }
    if (e->caret_only) {
        uint32_t tc = e->text_grid.dims.cols, full = e->grid.dims.cols;
        for (uint32_t r = 0; r < e->text_grid.dims.rows; r++) {
            if (!(e->text_grid.dirty[r / 64] & (UINT64_C(1) << (r % 64)))) continue;
            render_cell *dst = e->grid.cells + (size_t)(r + e->tab_rows) * full;
            const render_cell *src = e->text_grid.cells + (size_t)r * tc;
            if (memcmp(dst, src, (size_t)tc * sizeof *dst)) {
                memcpy(dst, src, (size_t)tc * sizeof *dst);
                int rc = render_mark_rows(&e->grid, r + e->tab_rows, 1);
                if (rc) return rc;
            }
        }
        return 0;
    }
    minimap_input input = editor_map_input(e->buffer);
    minimap_style colors = {0x171b22, 0x8091a8, 0x687d98, 0xbf853d};
    uint64_t first = e->v.state.first_line, visible = 0;
    for (uint32_t r = 0; r < e->text_grid.dims.rows; r++) if (e->row_byte[r] != LAYOUT_VOID_ROW) {
        uint64_t line = e->lay.wrap ? e->lay.wrap_rows[r].line : first + r;
        if (line >= first && line - first + 1 > visible) visible = line - first + 1;
    }
    uint64_t start = trace_now_ns();
    int rc = minimap_fill(&e->buffer->map, &input, &e->map_grid, 0, 8, first, visible, &colors);
    e->stats.minimap_ns = trace_now_ns() - start; e->stats.minimap_fills++;
    if (rc) return rc;
    uint32_t tc = e->text_grid.dims.cols, full = e->grid.dims.cols;
    for (uint32_t r = 0; r < e->text_grid.dims.rows; r++) {
        render_cell *dst = e->grid.cells + (size_t)(r + e->tab_rows) * full;
        const render_cell *src = e->text_grid.cells + (size_t)r * tc;
        bool different = memcmp(dst, src, (size_t)tc * sizeof *dst) != 0;
        if (different) memcpy(dst, src, (size_t)tc * sizeof *dst);
        if (e->map_cols) {
            src = e->map_grid.cells + (size_t)r * 8;
            if (memcmp(dst + tc, src, (size_t)e->map_cols * sizeof *dst)) {
                different = true; memcpy(dst + tc, src, (size_t)e->map_cols * sizeof *dst);
            }
        }
        if (different) { rc = render_mark_rows(&e->grid, r + e->tab_rows, 1); if (rc) return rc; }
    }
    if (e->tab_rows) {
        size_t active = tabs_active_index(&e->tabs);
        uint32_t slots = full / 16; if (!slots) slots = 1;
        size_t first_tab = active != SIZE_MAX && active >= slots ? active - slots + 1 : 0;
        e->strip = (tabs_strip){0, 0, full, 16, first_tab, 0x8d98a9, 0x171b22, 0xe5e9f0, 0x384356, glyph, e};
        rc = tabs_strip_render(&e->tabs, &e->grid, &e->strip); if (rc) return rc;
        if (e->buffer->map.stale) {
            render_cell *cell = &e->grid.cells[full - 1];
            *cell = (render_cell){'!', '!' - 0x20u, 0xe5b56b, 0x171b22, 0, 0};
        }
    }
    return 0;
}
int editor_pointer(editor *e, const plat_event *ev)
{
    int64_t col = ev->x < 0 ? -1 : (int64_t)ev->x / e->grid.dims.cell_w;
    int64_t row = ev->y < 0 ? -1 : (int64_t)ev->y / e->grid.dims.cell_h;
    if (ev->kind == PLAT_EV_BUTTON && ev->code == 1) {
        if (!ev->press) { e->drag_tab = SIZE_MAX; e->map_drag = false; return 0; }
        if (e->tab_rows && row == 0 && col >= 0) {
            size_t hit = tabs_strip_hit(&e->tabs, &e->strip, (uint32_t)col);
            if (hit != SIZE_MAX) { e->drag_tab = hit; return editor_select_tab(e, hit); }
        }
        if (e->map_cols && col >= (int64_t)e->text_grid.dims.cols && row >= e->tab_rows) e->map_drag = true;
    }
    if (ev->kind == PLAT_EV_MOTION && e->drag_tab != SIZE_MAX && col >= 0) {
        size_t hit = tabs_strip_hit(&e->tabs, &e->strip, (uint32_t)col);
        if (hit != SIZE_MAX && hit != e->drag_tab) {
            int rc = tabs_reorder(&e->tabs, e->drag_tab, hit); if (rc) return rc;
            e->drag_tab = hit; return editor_begin_frame(e);
        }
    }
    if (e->map_drag) {
        minimap_input input = editor_map_input(e->buffer); minimap_target target;
        int rc = minimap_hit(&e->buffer->map, &input, e->buffer->index, row - e->tab_rows, &target);
        if (rc == MINIMAP_ERR_STALE) return editor_begin_frame(e);
        if (rc) return rc;
        e->v.state.first_line = target.line; e->v.state.first_byte = e->v.state.visual_byte = target.byte;
        e->v.state.approximate = !target.exact; e->v.state.hscroll = 0;
        return editor_full_layout(e);
    }
    return 0;
}
