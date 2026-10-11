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
    /* Prepared row starts bound visibility without warming any preceding
     * original subtree. A clipped tail beyond the prepared window is hidden. */
    uint64_t start = LAYOUT_VOID_ROW;
    for (uint32_t r = 0; r < e->text_grid.dims.rows; r++) {
        uint64_t row = e->row_byte[r];
        if (row == LAYOUT_VOID_ROW || row > byte) continue;
        uint64_t end = r + 1u < e->text_grid.dims.rows ? e->row_byte[r + 1u] : e->lay.pos;
        if (end != LAYOUT_VOID_ROW && byte >= end) continue;
        if (start == LAYOUT_VOID_ROW || row > start) start = row;
    }
    if (start == LAYOUT_VOID_ROW || byte - start >= LAYOUT_BYTE_BUDGET) return false;
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
    editor *e = ctx;
    if (width < 1 || width > 2 || editor_font_glyph(e, text, len, (uint32_t)width, slot) != FONT_OK) return false;
    /* Chrome requires a resident image, including an empty space image. */
    if (*slot == RENDER_NO_SLOT) *slot = 0; /* baked space: ' ' - ' ' */
    *identity = e->grid.glyphs[*slot].glyph_index;
    editor_font_track(e, *slot, 0); /* chrome uses the tab-strip row */
    return true;
}
static int compose_chrome(editor *e)
{
    uint32_t full = e->grid.dims.cols; int rc;
    if (e->tab_rows) {
        size_t active = tabs_active_index(&e->tabs);
        uint32_t slots = full / 16; if (!slots) slots = 1;
        size_t first_tab = active != SIZE_MAX && active >= slots ? active - slots + 1 : 0;
        e->strip = (tabs_strip){0, 0, full, 16, first_tab, 0x8d98a9, 0x171b22, 0xe5e9f0, 0x384356, glyph, e};
        /* Decoration preparation is complete; reuse its aligned reserved
         * scratch for the final chrome. */
        if ((size_t)full * sizeof(render_cell) > EDITOR_STAGE_BYTES) return EDITOR_ERR_CAPACITY;
        render_cell *cells = (render_cell *)e->indent_bytes;
        memset(cells, 0, (size_t)full * sizeof *cells);
        uint64_t dirty = 0; render_grid scratch = e->grid;
        scratch.dims.rows = 1; scratch.cells = cells; scratch.cell_capacity = full;
        scratch.dirty = &dirty; scratch.dirty_word_capacity = 1; scratch.full_frame = false;
        rc = tabs_strip_render(&e->tabs, &scratch, &e->strip); if (rc) return rc;
        if (memcmp(e->grid.cells, cells, (size_t)full * sizeof *cells)) {
            memcpy(e->grid.cells, cells, (size_t)full * sizeof *cells);
            return render_mark_rows(&e->grid, 0, 1);
        }
    }
    return 0;
}
static int compose(editor *e)
{
    /* The bottom row doubles as a transient status line until panel chrome
     * exists. A later generation restores it even on a caret-only frame.
     * Source suspension bypasses text composition, so restore its cached row
     * here before drawing the higher-priority source-change prompt. */
    if (e->unimplemented_frame && e->unimplemented_frame != e->grid.frame_id) {
        e->caret_only = false;
        e->unimplemented_frame = 0;
        if (e->buffer->source_stale) {
            uint32_t full = e->grid.dims.cols, tc = e->text_grid.dims.cols;
            uint32_t row = e->grid.dims.rows - 1u, text_row = e->text_grid.dims.rows - 1u;
            render_cell *dst = e->grid.cells + (size_t)row * full;
            memcpy(dst, e->text_grid.cells + (size_t)text_row * tc, (size_t)tc * sizeof *dst);
            if (e->map_cols) memcpy(dst + tc,
                e->map_grid.cells + (size_t)text_row * e->map_grid.dims.cols,
                (size_t)e->map_cols * sizeof *dst);
            int rc = render_mark_rows(&e->grid, row, 1); if (rc) return rc;
        }
    }
    if (e->buffer->source_stale) {
        e->grid.region = (render_pixels){0};
        const char status[] = "Source changed: R reload / K keep";
        uint32_t cols = e->grid.dims.cols;
        for (uint32_t c = 0; c < cols; c++) {
            uint32_t ch = c < sizeof status - 1 ? (uint8_t)status[c] : (uint32_t)' ';
            e->grid.cells[(size_t)e->tab_rows * cols + c] = (render_cell){ch, ch - 0x20u, 0xe5b56b, 0x171b22, 0, 0};
        }
        int rc = compose_chrome(e);
        return rc ? rc : render_mark_rows(&e->grid, e->tab_rows, 1);
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
    minimap_style colors = {0x20242c, 0x78808e, 0xe5e9f0,
        editor_theme_estimated_gutter(0x8091a8, 0x171b22)};
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
        /* Background cells only; miniature ink lives in the pixel region. */
        for (uint32_t c = 0; c < e->map_cols; c++) {
            render_cell blank = {.atlas_slot = RENDER_NO_SLOT, .bg = colors.background};
            if (memcmp(dst + tc + c, &blank, sizeof blank)) {
                different = true; dst[tc + c] = blank;
            }
        }
        if (different) { rc = render_mark_rows(&e->grid, r + e->tab_rows, 1); if (rc) return rc; }
    }
    if (e->map_cols) {
        uint32_t width = e->map_cols * e->grid.dims.cell_w;
        uint32_t height = e->text_grid.dims.rows * e->grid.dims.cell_h;
        minimap *map = &e->buffer->map;
        bool retained = map->filled && map->height == e->map_grid.dims.rows &&
            map->identity.ctx == input.source.ctx && map->identity.span == input.source.span;
        if (retained) {
            rc = minimap_pixels(map, e->map_scratch, e->map_pixel_capacity,
                width, height, width, first, visible, &colors);
            if (rc) return rc;
        } else {
            /* Existing pending scrollbar at summary-row geometry, with the
             * narrowest physical edge bar (one pixel, from edit-g11). */
            for (uint32_t y = 0; y < height; y++) for (uint32_t x = 0; x < width; x++) {
                uint32_t col = (uint32_t)((uint64_t)x * e->map_grid.dims.cols / width);
                if (col + 1u == e->map_grid.dims.cols) col--; /* pending bar belongs only to the last pixel */
                e->map_scratch[(size_t)y * width + x] = x + 1u == width ? colors.stale :
                    e->map_grid.cells[(size_t)(y / 2u) * e->map_grid.dims.cols + col].bg;
            }
        }
        render_pixels *region = &e->grid.region;
        bool changed = !region->pixels || region->width != width || region->height != height ||
            region->x != tc * e->grid.dims.cell_w || region->y != e->tab_rows * e->grid.dims.cell_h ||
            memcmp(e->map_pixels, e->map_scratch, (size_t)width * height * sizeof(uint32_t));
        if (changed) memcpy(e->map_pixels, e->map_scratch, (size_t)width * height * sizeof(uint32_t));
        *region = (render_pixels){.pixels = e->map_pixels, .capacity = e->map_pixel_capacity,
            .stride = width, .x = tc * e->grid.dims.cell_w, .y = e->tab_rows * e->grid.dims.cell_h,
            .width = width, .height = height, .dirty = region->dirty || changed};
    } else e->grid.region = (render_pixels){0};
    rc = compose_chrome(e); if (rc) return rc;
    if (e->unimplemented_frame == e->grid.frame_id) {
        const char status[] = "not implemented";
        uint32_t row = e->grid.dims.rows - 1u;
        for (uint32_t c = 0; c < full; c++) {
            uint32_t ch = c < sizeof status - 1u ? (uint8_t)status[c] : (uint32_t)' ';
            e->grid.cells[(size_t)row * full + c] = (render_cell){ch, ch - 0x20u, 0xe5b56b, 0x171b22, 0, 0};
        }
        return render_mark_rows(&e->grid, row, 1);
    }
    return 0;
}
int editor_compose(editor *e)
{
    trace_record(TRACE_COMPOSE_BEGIN, e->grid.frame_id);
    int rc = compose(e);
    trace_record(TRACE_COMPOSE_END, e->grid.frame_id);
    return rc;
}
int editor_pointer(editor *e, const plat_event *ev)
{
    int64_t col = ev->x < 0 ? -1 : (int64_t)ev->x / e->grid.dims.cell_w;
    int64_t row = ev->y < 0 ? -1 : (int64_t)ev->y / e->grid.dims.cell_h;
    if (ev->kind == PLAT_EV_BUTTON && ev->code == 1) {
        if (!ev->press) { e->drag_tab = SIZE_MAX; e->drag_id = 0; e->map_drag = false; return 0; }
        if (e->tab_rows && row == 0 && col >= 0) {
            size_t hit = tabs_strip_hit(&e->tabs, &e->strip, (uint32_t)col);
            if (hit != SIZE_MAX) {
                int rc = editor_select_tab(e, hit);
                if (!rc) { e->drag_tab = hit; e->drag_id = tabs_at(&e->tabs, hit)->id; }
                return rc;
            }
        }
        if (!e->buffer->source_stale && !e->buffer->pending_open && e->map_cols &&
            col >= (int64_t)e->text_grid.dims.cols && row >= e->tab_rows) {
            e->map_drag = true; e->map_drag_id = e->buffer->id;
            e->map_drag_generation = e->buffer->source_generation;
        }
    }
    if (ev->kind == PLAT_EV_MOTION && e->drag_id && col >= 0) {
        size_t from = 0;
        while (from < tabs_count(&e->tabs) && tabs_at(&e->tabs, from)->id != e->drag_id) from++;
        if (from == tabs_count(&e->tabs)) { e->drag_id = 0; e->drag_tab = SIZE_MAX; return 0; }
        size_t hit = tabs_strip_hit(&e->tabs, &e->strip, (uint32_t)col);
        if (hit != SIZE_MAX && hit != from) {
            int rc = tabs_reorder(&e->tabs, from, hit); if (rc) return rc;
            e->drag_tab = hit; return editor_begin_frame(e);
        }
    }
    if (e->map_drag && (e->buffer->source_stale || e->buffer->pending_open ||
        e->map_drag_id != e->buffer->id || e->map_drag_generation != e->buffer->source_generation)) e->map_drag = false;
    if (e->map_drag) {
        minimap_input input = editor_map_input(e->buffer); minimap_target target;
        int rc = minimap_hit(&e->buffer->map, &input, e->buffer->index, ((int64_t)ev->y - (int64_t)e->tab_rows * e->grid.dims.cell_h) / 2, &target);
        if (rc == MINIMAP_ERR_STALE) return editor_begin_frame(e);
        if (rc) return rc;
        e->v.state.first_line = target.line; e->v.state.first_byte = e->v.state.visual_byte = target.byte;
        e->v.state.approximate = !target.exact; e->v.state.hscroll = 0;
        return editor_full_layout(e);
    }
    return 0;
}
