#include "editor/private.h"
#include "editor/theme.h"
#include "trace/trace.h"
#include <string.h>

static void paint(void *ctx, uint64_t byte, uint32_t *fg, uint32_t *bg, uint16_t *attrs)
{
    editor *e = ctx;
    editor_hl_paint(e, byte, fg);
    if (!e->paint_ready) return;
    size_t first=0, after=e->find_range_count;
    while (first<after) {
        size_t middle=first+(after-first)/2u;
        if (e->find_ranges[middle].end<=byte) first=middle+1u; else after=middle;
    }
    if (first<e->find_range_count && e->find_ranges[first].start<=byte) *bg=e->find_bg;
    if (byte == e->bracket_source || byte == e->bracket_mate) {
        *bg = e->find_bg; *attrs |= RENDER_ATTR_UNDERLINE;
    }
    /* (P) The byte before a nonzero caret is cursor minus one. A trailing
     * tab shares its source byte across its expanded cells. */
    if (!e->ws_count || (e->v.state.selection.cursor && byte == e->v.state.selection.cursor - 1u)) return;
    size_t lo = 0, hi = e->ws_count;
    while (lo < hi) { size_t mid = lo + (hi - lo) / 2; if (e->ws_ranges[mid].hi <= byte) lo = mid + 1; else hi = mid; }
    if (lo < e->ws_count && e->ws_ranges[lo].lo <= byte) {
        *fg = e->colors.error_fg;
        *bg = e->colors.error_bg; *attrs |= RENDER_ATTR_UNDERLINE;
    }
}
static bool byte_visible(editor *e, uint64_t byte)
{
    if (byte >= piece_len(e->tree)) return false;
    if (e->lay.wrap) {
        for (uint32_t r = 0; r < e->v.config.rows; r++) {
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
    /* Source adoption can precede layout completion in editor_find_service.
     * Refresh the visible cache against the finished layout before painting. */
    int find_rc=editor_find_highlights(e); if (find_rc) return find_rc;
    uint64_t lo = e->v.state.wrap ? e->v.state.visual_byte : e->v.state.first_byte;
    uint64_t hi = e->lay.wrap ? lo : e->lay.pos;
    for (uint32_t r = 0; r < e->v.config.rows; r++) if (e->row_byte[r] != LAYOUT_VOID_ROW) {
        uint64_t end;
        if (e->lay.wrap) end = e->lay.wrap_rows[r].next == LAYOUT_VOID_ROW ? e->lay.wrap_rows[r].end : e->lay.wrap_rows[r].next;
        else end = hi;
        if (end != LAYOUT_VOID_ROW && end > hi) hi = end;
    }
    if (hi > piece_len(e->tree)) hi = piece_len(e->tree);
    /* (P) All-void rows can leave pos below the viewport start. Equal
     * endpoints form an empty range and keep the subtraction from wrapping. */
    if (hi < lo) hi = lo;
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
    if (e->preferences.trailing != CONFIG_TRAILING_NONE) {
        size_t count = 0;
        rc = indent_trailing_ws_ranges(e->tree, lo, hi, e->ws_ranges, e->max_rows, &count);
        if (rc != INDENT_OK && rc != INDENT_ERR_CAPACITY) return EDITOR_ERR_ARG;
        if (count > e->max_rows) count = e->max_rows;
        view_selection selection = e->v.state.selection;
        uint64_t sel_lo = selection.cursor < selection.anchor ? selection.cursor : selection.anchor;
        uint64_t sel_hi = selection.cursor > selection.anchor ? selection.cursor : selection.anchor;
        for (size_t i = 0; i < count; i++) {
            indent_range range = e->ws_ranges[i];
            /* Keep an unfinished typing run plain until the caret leaves its
             * end. Other completed lines keep their decorations. */
            if (range.hi == selection.cursor) continue;
            if (e->preferences.trailing == CONFIG_TRAILING_SELECTION) {
                if (range.lo < sel_lo) range.lo = sel_lo;
                if (range.hi > sel_hi) range.hi = sel_hi;
            }
            if (range.lo < range.hi) e->ws_ranges[e->ws_count++] = range;
        }
    }
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
/* The strip renders into a by-value copy of the grid. A cold lookup appends
 * a descriptor and rebinds only the editor's grids, so rebind the copy too;
 * otherwise its stale glyph_count rejects the new slot as non-resident, the
 * title cell paints blank and adoption finds no cell to repaint (edit-6di). */
typedef struct chrome_glyphs { editor *e; render_grid *scratch; } chrome_glyphs;
static bool glyph(void *ctx, const uint8_t *text, size_t len, int width, uint32_t *identity, uint32_t *slot)
{
    chrome_glyphs *c = ctx; editor *e = c->e;
    if (width < 1 || width > 2 || editor_font_glyph(e, text, len, (uint32_t)width, slot) != FONT_OK) return false;
    (void)font_async_bind(e->fonts, c->scratch);
    /* Chrome requires a resident image, including an empty space image. */
    if (*slot == RENDER_NO_SLOT) *slot = 0; /* baked space: ' ' - ' ' */
    *identity = e->grid.glyphs[*slot].glyph_index;
    editor_font_track(e, *slot, e->menu_rows); /* chrome uses the tab-strip row (below the menu bar) */
    return true;
}
static int compose_chrome(editor *e)
{
    uint32_t full = e->grid.dims.cols; int rc;
    if (e->tab_rows > e->menu_rows) { /* edit-yey: the strip sits below the menu bar */
        size_t active = tabs_active_index(&e->tabs);
        uint32_t left = e->lay.gw ? e->lay.gw - 1u : 0u;
        uint32_t span = full > left + 1u ? full - left - 1u : 0u;
        uint32_t slots = span / 16; if (!slots) slots = 1;
        size_t first_tab = active != SIZE_MAX && active >= slots ? active - slots + 1 : 0;
        e->strip = (tabs_strip){0, left, span, 16, first_tab,
            e->colors.chrome_fg, e->colors.chrome_bg, e->colors.tab_fg, e->colors.tab_bg, NULL, NULL};
        /* Decoration preparation is complete; reuse its aligned reserved
         * scratch for the final chrome. */
        if ((size_t)full * sizeof(render_cell) > EDITOR_STAGE_BYTES) return EDITOR_ERR_CAPACITY;
        render_cell *cells = (render_cell *)e->indent_bytes;
        render_cell blank = {.atlas_slot = RENDER_NO_SLOT, .bg = e->layout_cfg.bg};
        for (uint32_t c = 0; c < left; c++) cells[c] = blank;
        for (uint32_t c = left + span; c < full; c++) cells[c] = blank;
        if (span) { cells[left] = blank; cells[left + span - 1u] = blank; }
        uint64_t dirty = 0; render_grid scratch = e->grid;
        scratch.dims.rows = 1; scratch.cells = cells; scratch.cell_capacity = full;
        scratch.dirty = &dirty; scratch.dirty_word_capacity = 1; scratch.full_frame = false;
        scratch.scroll = (render_scroll){0};
        scratch.region = (render_pixels){0};
        chrome_glyphs ctx = {e, &scratch};
        tabs_strip strip = e->strip; strip.glyph = glyph; strip.glyph_ctx = &ctx;
        rc = tabs_strip_render(&e->tabs, &scratch, &strip); if (rc) return rc;
        if (e->hover.kind == EDITOR_HOVER_TAB && e->hover.index != active &&
            e->hover.index >= first_tab && e->hover.index - first_tab < (span + strip.tab_cols - 1u) / strip.tab_cols) {
            uint32_t from = left + (uint32_t)(e->hover.index - first_tab) * strip.tab_cols;
            uint32_t end = from + strip.tab_cols;
            if (end > left + span) end = left + span;
            for (uint32_t c = from; c < end; c++) cells[c].fg = e->colors.tab_hover;
        }
        render_cell *row = e->grid.cells + (size_t)e->menu_rows * full;
        if (memcmp(row, cells, (size_t)full * sizeof *cells)) {
            memcpy(row, cells, (size_t)full * sizeof *cells);
            rc = render_mark_rows(&e->grid, e->menu_rows, 1); if (rc) return rc;
        }
    }
    if (e->tab_rows > e->menu_rows + 1u && e->grid.full_frame) {
        uint32_t r = e->tab_rows - 1u; bool changed = false;
        render_cell blank = {.atlas_slot = RENDER_NO_SLOT, .bg = e->layout_cfg.bg};
        for (uint32_t c = 0; c < full; c++) {
            render_cell *v = &e->grid.cells[(size_t)r * full + c];
            if (memcmp(v, &blank, sizeof blank)) { *v = blank; changed = true; }
        }
        if (changed) return render_mark_rows(&e->grid, r, 1);
    }
    return 0;
}
static int compose_scroll(editor *e)
{
    e->grid.scroll = (render_scroll){0};
    /* The renderer's scroll region runs to the surface bottom (edit-3kk).
     * With the find panel below the text (edit-73j), draw unshifted rows. */
    if (e->v.state.wrap || e->panel_rows || e->menu.open >= 0) return 0; /* edit-yey: drop-down overlay */
    uint32_t full = e->grid.dims.cols, tc = e->text_grid.dims.cols;
    uint32_t visible_rows = e->v.config.rows;
    render_cell *extra = e->grid.cells + (size_t)e->grid.dims.rows * full;
    memcpy(extra, e->text_grid.cells + (size_t)visible_rows * tc, (size_t)tc * sizeof *extra);
    for (uint32_t c = tc; c < full; c++) extra[c] = (render_cell){.atlas_slot = RENDER_NO_SLOT};
    e->grid.scroll = (render_scroll){e->scroll_plan.origin_y_q8, e->tab_rows, visible_rows, tc};
    return e->scroll_plan.origin_y_q8 ? render_mark_full(&e->grid) : 0;
}
static int compose_map_pixels(editor *e, minimap_input input, uint64_t first,
                              uint64_t visible, const minimap_style *style)
{
    const minimap_style colors = *style;
    uint32_t tc = e->text_grid.dims.cols;
    int rc;
    if (e->map_cols) {
        uint32_t width = e->map_cols * e->grid.dims.cell_w;
        uint32_t height = e->v.config.rows * e->grid.dims.cell_h; /* stops above the find panel (edit-73j) */
        minimap *map = &e->buffer->map;
        bool retained = map->filled && map->height == e->map_grid.dims.rows &&
            map->identity.ctx == input.source.ctx && map->identity.span == input.source.span;
        minimap_window w;
        if (retained && !map->windowed) {
            /* Legacy grouped summaries (none are prepared since edit-cxho.10
             * once the index is ready; kept for any retained one). */
            rc = minimap_pixels(map, e->map_scratch, e->map_pixel_capacity,
                width, height, width, first, visible, &colors);
            if (rc) return rc;
        } else if (editor_map_window(e, &input, &w)) {
            /* edit-cxho.10: the box is the current window's (the pointer's
             * while dragging, so it never lags the resolving scroll). Rows
             * labelled in the current numbering (a fresh exact window) are
             * placed relative to the viewport's first line at box_top; any
             * other retained rows keep their own anchor's geometry until
             * the job for the new anchor lands. */
            minimap_frame frame = {.line_y = w.box_top, .line = w.first_line + w.above,
                .lines = input.lines, .box_top = w.box_top, .box_height = w.box_height,
                .scale = w.scale, .pending = !retained};
            if (e->map_drag && e->map_drag_id == e->buffer->id &&
                e->map_drag_generation == e->buffer->source_generation) frame.box_top = e->map_drag_top;
            if (retained && !(!map->stale && map->window_exact && input.index_ready)) {
                frame.line_y = map->anchor.window.box_top; frame.line = map->anchor.first_line;
            } else if (!retained && !input.index_ready) {
                /* Byte window: the viewport's line label is an estimate. */
                frame.line = e->v.state.first_line;
            }
            rc = minimap_pixels_window(retained ? map : NULL, e->map_scratch, e->map_pixel_capacity,
                width, height, width, &frame, &colors);
            if (rc) return rc;
        } else return EDITOR_ERR_ARG;
        render_pixels *region = &e->grid.region;
        bool changed = !region->pixels || region->width != width || region->height != height ||
            region->x != tc * e->grid.dims.cell_w || region->y != e->tab_rows * e->grid.dims.cell_h ||
            memcmp(e->map_pixels, e->map_scratch, (size_t)width * height * sizeof(uint32_t));
        if (changed) memcpy(e->map_pixels, e->map_scratch, (size_t)width * height * sizeof(uint32_t));
        *region = (render_pixels){.pixels = e->map_pixels, .capacity = e->map_pixel_capacity,
            .stride = width, .x = tc * e->grid.dims.cell_w, .y = e->tab_rows * e->grid.dims.cell_h,
            .width = width, .height = height, .dirty = region->dirty || changed};
    } else e->grid.region = (render_pixels){0};
    return 0;
}
static int compose(editor *e)
{
    /* The bottom row doubles as a transient status line until panel chrome
     * exists. A later generation restores it even on a caret-only frame.
     * Source suspension bypasses text composition, so restore its cached row
     * here before drawing the higher-priority source-change prompt. */
    /* edit-btj: the recovery notice ends at the first mutation; recompose
     * every row once so the status row returns to text. */
    if (e->recovery_notice_shown && e->stats.mutations != e->recovery_notice_mutations) {
        e->recovery_notice = NULL; e->recovery_notice_shown = false;
        e->caret_only = e->glyph_only = false;
    }
    if (e->unimplemented_frame && e->unimplemented_frame != e->grid.frame_id) {
        e->caret_only = false;
        e->unimplemented_frame = 0;
        if (e->buffer->source_stale) {
            uint32_t full = e->grid.dims.cols, tc = e->text_grid.dims.cols;
            uint32_t row = e->grid.dims.rows - 1u, text_row = row - e->tab_rows;
            render_cell *dst = e->grid.cells + (size_t)row * full;
            memcpy(dst, e->text_grid.cells + (size_t)text_row * tc, (size_t)tc * sizeof *dst);
            for (uint32_t c = tc; c < full; c++)
                dst[c] = (render_cell){.atlas_slot = RENDER_NO_SLOT, .bg = e->layout_cfg.bg};
            int rc = render_mark_rows(&e->grid, row, 1); if (rc) return rc;
        }
    }
    /* Fractional caret damage can touch the overscan row too. Use ordinary
     * composition and transformed full damage for that geometry. */
    if (e->caret_only && e->scroll_plan.origin_y_q8) e->caret_only = false;
    if (e->buffer->source_stale) {
        e->grid.region = (render_pixels){0};
        if (e->grid.scroll.origin_y_q8) {
            int rc = render_mark_full(&e->grid); if (rc) return rc;
        }
        e->grid.scroll = (render_scroll){0};
        const char status[] = "Source changed: R reload / K keep";
        uint32_t cols = e->grid.dims.cols;
        /* (E) Use the existing bottom status row: zero-based last index is
         * row count minus one. The later save overlay retains precedence
         * for its required saving acknowledgement (docs/decisions/edit-1um.md). */
        uint32_t row = e->grid.dims.rows ? e->grid.dims.rows - 1u : 0u;
        for (uint32_t c = 0; e->grid.dims.rows && c < cols; c++) { /* no row: nothing to draw */
            uint32_t ch = c < sizeof status - 1 ? (uint8_t)status[c] : (uint32_t)' ';
            e->grid.cells[(size_t)row * cols + c] = (render_cell){ch, ch - ' ',
                e->colors.warning,
                e->colors.chrome_bg, 0, 0};
        }
        int rc = compose_chrome(e);
        return rc || !e->grid.dims.rows ? rc : render_mark_rows(&e->grid, row, 1); /* (E) Only the status row changed. */
    }
    if (e->caret_only || e->glyph_only) {
        uint32_t tc = e->text_grid.dims.cols, full = e->grid.dims.cols;
        for (uint32_t r = 0; r < e->v.config.rows; r++) {
            if (!(e->text_grid.dirty[r / 64] & (UINT64_C(1) << (r % 64)))) continue;
            render_cell *dst = e->grid.cells + (size_t)(r + e->tab_rows) * full;
            const render_cell *src = e->text_grid.cells + (size_t)r * tc;
            if (memcmp(dst, src, (size_t)tc * sizeof *dst)) {
                memcpy(dst, src, (size_t)tc * sizeof *dst);
                int rc = render_mark_rows(&e->grid, r + e->tab_rows, 1);
                if (rc) return rc;
            }
        }
        int rc = compose_scroll(e); if (rc) return rc;
        return editor_menu_paint(e); /* edit-yey: restores overlay cells a text row covered */
    }
    minimap_input input = editor_map_input(e->buffer);
    /* edit-cxho.24: the map's ink is the minimap rung of the ladder (below the
     * gutter, rubric C7) and the viewport a neutral wash of the text tone,
     * not the accent. */
    minimap_style colors = {e->layout_cfg.bg,
        e->colors.map_fg, e->layout_cfg.fg,
        editor_theme_estimated_gutter(e->colors.map_fg, e->colors.chrome_bg), e->hover.kind == EDITOR_HOVER_MAP};
    uint64_t first = e->v.state.first_line, visible = 0;
    for (uint32_t r = 0; r < e->v.config.rows; r++) if (e->row_byte[r] != LAYOUT_VOID_ROW) {
        uint64_t line = e->lay.wrap ? e->lay.wrap_rows[r].line : first + r;
        if (line >= first && line - first + 1 > visible) visible = line - first + 1;
    }
    uint64_t start = trace_now_ns();
    /* A file-backed first map is prepared here, synchronously (main's path
     * before edit-73j); every other summary comes from the worker job. */
    bool sync = e->buffer->map_sync_fill;
    int rc = sync ? editor_map_fill_sync(e, &input, first, visible, &colors) :
        minimap_fill_cached(&e->buffer->map, &input, &e->map_grid, 0, 8, first, visible, &colors);
    if (!rc && sync && e->buffer->map.filled) e->buffer->map_sync_fill = false;
    e->stats.minimap_ns = trace_now_ns() - start; e->stats.minimap_fills++;
    if (rc) return rc;
    uint32_t tc = e->text_grid.dims.cols, full = e->grid.dims.cols;
    for (uint32_t r = 0; r < e->v.config.rows; r++) {
        render_cell *dst = e->grid.cells + (size_t)(r + e->tab_rows) * full;
        const render_cell *src = e->text_grid.cells + (size_t)r * tc;
        bool different = memcmp(dst, src, (size_t)tc * sizeof *dst) != 0;
        if (different) memcpy(dst, src, (size_t)tc * sizeof *dst);
        /* Background cells only; miniature ink lives in the pixel region. */
        for (uint32_t c = 0; c < full - tc; c++) {
            render_cell blank = {.atlas_slot = RENDER_NO_SLOT, .bg = colors.background};
            if (memcmp(dst + tc + c, &blank, sizeof blank)) {
                different = true; dst[tc + c] = blank;
            }
        }
        if (different) { rc = render_mark_rows(&e->grid, r + e->tab_rows, 1); if (rc) return rc; }
    }
    rc = compose_map_pixels(e, input, first, visible, &colors); if (rc) return rc;
    rc = compose_scroll(e); if (rc) return rc;
    rc = compose_chrome(e); if (rc) return rc;
    rc = editor_find_paint(e); if (rc) return rc;
    rc = editor_menu_paint(e); if (rc) return rc;
    if (e->unimplemented_frame == e->grid.frame_id) {
        const char status[] = "not implemented";
        uint32_t row = e->grid.dims.rows - 1u;
        for (uint32_t c = 0; c < full; c++) {
            uint32_t ch = c < sizeof status - 1u ? (uint8_t)status[c] : (uint32_t)' ';
            e->grid.cells[(size_t)row * full + c] = (render_cell){ch, ch - 0x20u,
                e->colors.warning,
                e->colors.chrome_bg, 0, 0};
        }
        return render_mark_rows(&e->grid, row, 1);
    }
    return 0;
}
int editor_compose(editor *e)
{
    e->grid.surface_background = e->layout_cfg.bg;
    trace_record(TRACE_COMPOSE_BEGIN, e->grid.frame_id);
    int rc;
    if (e->hover_only) {
        rc = 0;
        if (render_row_dirty(&e->grid, e->menu_rows)) rc = compose_chrome(e);
        if (!rc) rc = editor_find_paint(e);
        if (!rc) rc = editor_menu_paint(e);
        if (!rc) {
            minimap_style colors = {e->layout_cfg.bg, e->colors.map_fg, e->layout_cfg.fg,
                editor_theme_estimated_gutter(e->colors.map_fg, e->colors.chrome_bg), e->hover.kind == EDITOR_HOVER_MAP};
            /* A region-only frame has no cell damage. Enter/leave of the map
             * is the only hover transition requiring its pixel composition. */
            if (e->hover_map && !e->buffer->source_stale && !e->buffer->pending_open) {
                uint64_t visible = 0;
                for (uint32_t r=0; r<e->v.config.rows; r++) if (e->row_byte[r] != LAYOUT_VOID_ROW) {
                    uint64_t line=e->lay.wrap ? e->lay.wrap_rows[r].line : e->v.state.first_line+r;
                    if (line>=e->v.state.first_line && line-e->v.state.first_line+1u>visible) visible=line-e->v.state.first_line+1u;
                }
                rc = compose_map_pixels(e,editor_map_input(e->buffer),e->v.state.first_line,visible,&colors);
            }
        }
    } else rc = compose(e);
    trace_record(TRACE_COMPOSE_END, e->grid.frame_id);
    return rc;
}
int editor_chrome_pointer(editor *e, const plat_event *ev)
{
    bool map_press = false;
    int64_t col = ev->x < 0 ? -1 : (int64_t)ev->x / e->grid.dims.cell_w;
    int64_t row = ev->y < 0 ? -1 : (int64_t)ev->y / e->grid.dims.cell_h;
    if (ev->kind == PLAT_EV_BUTTON && ev->code == 1) {
        if (!ev->press) { e->drag_tab = SIZE_MAX; e->drag_id = 0; e->map_drag = false; return 0; }
        if (e->tab_rows > e->menu_rows && row == e->menu_rows && col >= 0) {
            size_t hit = tabs_strip_hit(&e->tabs, &e->strip, (uint32_t)col);
            if (hit != SIZE_MAX) {
                int rc = editor_select_tab(e, hit);
                if (!rc) { e->drag_tab = hit; e->drag_id = tabs_at(&e->tabs, hit)->id; }
                return rc;
            }
        }
        if (!e->buffer->source_stale && !e->buffer->pending_open && e->map_cols &&
            col >= (int64_t)e->text_grid.dims.cols && col < (int64_t)e->text_grid.dims.cols + e->map_cols &&
            row >= e->tab_rows && row < (int64_t)e->tab_rows + e->v.config.rows) {
            e->map_drag = true; e->map_drag_id = e->buffer->id;
            e->map_drag_generation = e->buffer->source_generation;
            map_press = true;
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
        e->find_scroll=false; e->wheel_pending=false;
        minimap_input input = editor_map_input(e->buffer); minimap_window w;
        int64_t local_y = (int64_t)ev->y - (int64_t)e->tab_rows * e->grid.dims.cell_h;
        if (!editor_map_window(e, &input, &w)) return 0;
        /* edit-cxho.10: the painted box is the window's (Sublime model).
         * A press inside grabs it at the pointer's offset; outside, the box
         * centres on the pointer (minimap_window_grab) and the document
         * follows. Motion places the box top at pointer - offset, clamped
         * to the travel, and seeks its inverse: 1:1 with the pointer when
         * lines outnumber travel pixels (minimap.h). */
        if (map_press) {
            bool inside = minimap_window_grab(&w, local_y, &e->map_drag_offset);
            e->map_drag_top = w.box_top;
            if (inside) return 0;
        }
        int64_t top = local_y - e->map_drag_offset;
        top = top < 0 ? 0 : top > (int64_t)w.travel ? (int64_t)w.travel : top;
        /* A box over its own lines moves in whole lines (floor: the line
         * under the pointer), as the grouped map did. */
        if (!w.scrolls) top -= top % w.scale;
        bool moved = top != e->map_drag_top;
        e->map_drag_top = top;
        if (top == (int64_t)w.box_top) return moved ? editor_begin_frame(e) : 0;
        uint64_t target = minimap_window_seek(&w, top, 0);
        int rc;
        e->v.state.hscroll = 0;
        if (!input.index_ready) {
            /* edit-73j: while indexing, the byte target is resolved on the
             * scroll path (3kk) when unwrapped, or through the resident
             * window when wrapped; the box shows the pointer meanwhile. */
            if (!e->v.state.wrap) { rc = editor_scroll_seek(e, target); return rc ? rc : editor_begin_frame(e); }
            rc = scroll_init(&e->wheel, (scroll_config){e->v.config.rows, 1, 0},
                (scroll_extent){input.source.len, input.lines, input.index_ready}); if (rc) return rc;
            e->wheel.first_byte = e->v.state.first_byte; e->wheel.first_line = e->v.state.first_line;
            rc = scroll_seek_byte(&e->wheel, target); if (rc) return rc;
            e->wheel_resolver = (scroll_resolver){0}; e->wheel_pending = true;
            return editor_wheel_continue(e);
        }
        uint64_t line = target < input.lines ? target : input.lines - 1u;
        const minimap *map = &e->buffer->map;
        uint64_t base = map->active_rows ? map->rows[0].first_line : 0;
        if (map->windowed && map->window_exact && !minimap_stale(map, &input) &&
            map->active_rows && line >= base && line - base < map->active_rows) {
            /* The summary holds this line's exact start: adopt it now, as
             * go-to-line adopts a certified start (input.c goto_scroll_to). */
            uint64_t byte = map->rows[line - base].first_byte;
            e->v.state.first_line = line; e->v.state.first_byte = e->v.state.visual_byte = byte;
            e->v.state.approximate = false; e->v.state.visual_end = false;
            if (!editor_scroll_place(e, byte, line, false)) { rc = editor_scroll_seek(e, byte); if (rc) return rc; }
            return editor_full_layout(e);
        }
        if (!e->v.state.wrap) { rc = editor_scroll_seek_line(e, line); return rc ? rc : editor_begin_frame(e); }
        rc = scroll_init(&e->wheel, (scroll_config){e->v.config.rows, 1, 0},
            (scroll_extent){input.source.len, input.lines, input.index_ready}); if (rc) return rc;
        e->wheel.first_byte = e->v.state.first_byte; e->wheel.first_line = e->v.state.first_line;
        rc = scroll_seek_line(&e->wheel, line); if (rc) return rc;
        e->wheel_resolver = (scroll_resolver){0}; e->wheel_pending = true;
        return editor_wheel_continue(e);
    }
    return 0;
}

void editor_hit_record(void *ctx, uint32_t row, uint32_t column, uint32_t count,
    uint32_t skip, uint32_t width, uint64_t before, uint64_t after)
{
    editor *e=ctx;
    for (uint32_t c=0;c<count;c++) e->hits[(size_t)row*e->max_cols+column+c]=
        (editor_hit){before,after,skip+c,width};
}

/* The prompt uses the existing status row and atlas. Its storage is embedded
 * in the editor before input; editing it never touches the document/undo log. */
void editor_goto_overlay(editor *e)
{
    const editor_goto *go = &e->go;
    if (!go->active || go->owner != e->buffer->id || go->generation != e->buffer->source_generation ||
        e->buffer->source_stale || !e->grid.dims.rows) return;
    const char *label = go->kind == KEYS_ACTION_GOTO_LINE ? "Line number" :
        go->kind == KEYS_ACTION_OPEN_FILE ? "Open file" : "Save as";
    findui_bounds bounds = editor_panel_bounds(e);
    size_t prefix = bounds.first;
    uint32_t row = e->grid.dims.rows - 1u;
    const uint32_t ink = e->layout_cfg.fg, plain = e->colors.panel, marked = e->layout_cfg.sel_bg;
    for (uint32_t col = 0; col < e->grid.dims.cols; col++) {
        uint32_t ch = col >= prefix && col < bounds.end && go->kind == KEYS_ACTION_GOTO_LINE && col - prefix < go->length ? (uint8_t)go->digits[col - prefix] : (uint32_t)' ';
        uint16_t attrs = col == prefix + go->cursor ? RENDER_ATTR_UNDERLINE : 0;
        uint32_t bg = go->selected && col >= prefix && col - prefix < go->length ? marked : plain;
        e->grid.cells[(size_t)row * e->grid.dims.cols + col] = (render_cell){ch, ch - ' ', ink, bg, attrs, 0};
    }
    if (go->kind != KEYS_ACTION_GOTO_LINE) {
        /* (P) UTF-8 byte cursors map to grapheme cell widths, as tab titles do.
         * Decimal prompts retain their preceding byte-identical drawing. */
        for (uint32_t col = (uint32_t)prefix; col < e->grid.dims.cols; col++)
            e->grid.cells[(size_t)row * e->grid.dims.cols + col] = (render_cell){' ', 0, ink, plain, 0, 0};
        size_t byte = 0; uint32_t col = (uint32_t)prefix;
        while (byte < go->length && col < bounds.end) {
            size_t n = utf8_grapheme_next((const uint8_t *)go->digits + byte, go->length - byte);
            int width = utf8_cluster_width((const uint8_t *)go->digits + byte, n);
            if (width <= 0) { byte += n; continue; }
            uint32_t cells = (uint32_t)width;
            if (cells > bounds.end - col) break;
            uint32_t slot = RENDER_NO_SLOT;
            if (editor_font_glyph(e, (const uint8_t *)go->digits + byte, n, cells, &slot) != FONT_OK) break;
            uint32_t identity = slot == RENDER_NO_SLOT ? (uint32_t)' ' : e->grid.glyphs[slot].glyph_index;
            uint16_t attrs = byte == go->cursor ? RENDER_ATTR_UNDERLINE : 0;
            if (cells > 1u) attrs |= RENDER_ATTR_WIDE_LEFT;
            uint32_t bg = go->selected ? marked : plain;
            e->grid.cells[(size_t)row * e->grid.dims.cols + col] = (render_cell){identity, slot, ink, bg, attrs, 0};
            for (uint32_t tail = 1; tail < cells; tail++)
                e->grid.cells[(size_t)row * e->grid.dims.cols + col + tail] = (render_cell){.glyph_index = ' ', .atlas_slot = RENDER_NO_SLOT, .fg = ink, .bg = bg, .attrs = RENDER_ATTR_WIDE_RIGHT};
            editor_font_track(e, slot, row); byte += n; col += cells;
        }
        if (byte == go->cursor && col < e->grid.dims.cols)
            e->grid.cells[(size_t)row * e->grid.dims.cols + col].attrs |= RENDER_ATTR_UNDERLINE;
    }
    if (!go->length) {
        for (uint32_t c = bounds.first; *label && c < bounds.end; c++, label++) {
            uint32_t ch = (uint8_t)*label;
            render_cell *v = &e->grid.cells[(size_t)row * e->grid.dims.cols + c];
            *v = (render_cell){ch, ch - ' ', e->layout_cfg.gutter_fg, plain, c == bounds.first ? RENDER_ATTR_UNDERLINE : 0, 0};
        }
    }
    (void)render_mark_rows(&e->grid, row, 1);
}
/* edit-2ye3: the close prompt takes the status row as the source-change
 * prompt does: its colours and resident ASCII slots, with the text scroll
 * region and the minimap region disabled for the frame (round 2). Title
 * bytes outside printable ASCII draw '?' per grapheme, so every cell is a
 * plain one-cell ASCII cell and the row is always contract-valid. The title
 * is cut so the choices stay visible. The required answer keeps precedence
 * over the other transients. Embedded storage only. */
static uint32_t close_ascii(editor *e, render_cell *cells, uint32_t col, const char *text, uint32_t fg, uint32_t bg)
{
    for (; *text && col < e->grid.dims.cols; text++, col++) {
        uint32_t ch = (uint8_t)*text;
        cells[col] = (render_cell){ch, ch - ' ', fg, bg, 0, 0};
    }
    return col;
}
void editor_close_overlay(editor *e)
{
    if (e->close_prompt.owner && !editor_close_prompt_shown(e)) { e->close_prompt.owner = 0; return; }
    const tabs_tab *tab = tabs_active(&e->tabs);
    if (!e->close_prompt.owner || !tab || !e->grid.dims.rows) return;
    if (e->grid.region.pixels || e->grid.scroll.origin_y_q8) (void)render_mark_full(&e->grid);
    e->grid.region = (render_pixels){0}; e->grid.scroll = (render_scroll){0};
    static const char prefix[] = "Save changes to ";
    const char *suffix = e->close_prompt.drop_refused ?
        "? Drop refused; S save / D don't save / Esc cancel" : "? S save / D don't save / Esc cancel";
    uint32_t fg = e->colors.warning;
    uint32_t bg = e->colors.chrome_bg;
    uint32_t row = e->grid.dims.rows - 1u, cols = e->grid.dims.cols;
    render_cell *cells = e->grid.cells + (size_t)row * cols;
    for (uint32_t c = 0; c < cols; c++) cells[c] = (render_cell){' ', 0, fg, bg, 0, 0};
    uint32_t col = close_ascii(e, cells, 0, prefix, fg, bg);
    /* (P) string lengths exclude their terminators. */
    uint32_t reserve = (uint32_t)strlen(suffix), end = cols > col + reserve ? cols - reserve : col;
    for (size_t byte = 0; byte < tab->title_len && col < end; col++) {
        const uint8_t *at = (const uint8_t *)tab->title + byte;
        size_t n = utf8_grapheme_next(at, tab->title_len - byte);
        uint32_t ch = n == 1 && *at >= ' ' && *at < 0x7f ? *at : (uint32_t)'?';
        cells[col] = (render_cell){ch, ch - ' ', fg, bg, 0, 0};
        byte += n ? n : 1u;
    }
    (void)close_ascii(e, cells, col, suffix, fg, bg);
    (void)render_mark_rows(&e->grid, row, 1);
}
void editor_status_overlay(editor *e)
{
    /* One owner for the last row: required close, source answer, navigation
     * input, palette/find panel, then ordinary save/error/notice status.
     * Menus occupy their own bar/dropdown and retain the underlying owner. */
    switch (editor_active_modal(e)) {
    case EDITOR_MODAL_CLOSE_PROMPT: editor_close_overlay(e); return;
    case EDITOR_MODAL_GOTO: case EDITOR_MODAL_OPEN_PROMPT: case EDITOR_MODAL_SAVE_AS_PROMPT:
        editor_goto_overlay(e); return;
    case EDITOR_MODAL_SOURCE_STALE: return; /* compose already painted its answer. */
    default:
        if (!e->panel_rows) editor_save_overlay(e);
        return;
    }
}
