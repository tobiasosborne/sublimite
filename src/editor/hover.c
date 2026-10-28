#include "editor/private.h"
#include "editor/theme.h"

static bool visible_ink(const editor *e, editor_hover h)
{
    if (h.kind == EDITOR_HOVER_NONE) return false;
    if (h.kind == EDITOR_HOVER_TAB) return h.index != tabs_active_index(&e->tabs);
    if (h.kind == EDITOR_HOVER_PALETTE) return h.index != e->palette->selected;
    if (h.kind == EDITOR_HOVER_MENU) return h.index != e->menu.item;
    return true;
}
static editor_hover target(editor *e, uint32_t row, uint32_t col, plat_cursor_shape *shape)
{
    editor_hover h = {0};
    if (editor_close_prompt_shown(e)) return h;
    if (editor_menu_hover(e, row, col, &h)) return h;
    if (e->tab_rows > e->menu_rows && row == e->menu_rows) {
        size_t i = tabs_strip_hit(&e->tabs, &e->strip, col);
        if (i != SIZE_MAX) return (editor_hover){EDITOR_HOVER_TAB, row, i};
        return h;
    }
    findui_bounds b = editor_panel_bounds(e);
    if (e->go.active && row == e->grid.dims.rows - 1u) {
        if (col >= b.first && col < b.end) *shape = PLAT_CURSOR_TEXT;
        return h;
    }
    uint32_t first = e->grid.dims.rows - e->panel_rows;
    if (e->panel_rows && row >= first) {
        if (e->palette_open) {
            if (row == first && col >= b.first && col < b.end) *shape = PLAT_CURSOR_TEXT;
            size_t i = e->palette->first + row - first;
            if (row > first && i - 1u < e->palette->count && col >= b.first && col < b.end)
                h = (editor_hover){EDITOR_HOVER_PALETTE, row, i - 1u};
        } else {
            if (e->fif.mode && row==first+1u) {
                if (col>=sizeof "Where: "-1u && col<e->grid.dims.cols-1u) *shape=PLAT_CURSOR_TEXT;
                return h;
            }
            findui_hit_kind hit = findui_hit_target(&e->find,b,e->panel_rows,row-first,col);
            {
                if (hit == FINDUI_HIT_QUERY || hit == FINDUI_HIT_REPLACEMENT) *shape = PLAT_CURSOR_TEXT;
                else if (hit >= FINDUI_HIT_REGEX)
                    h = (editor_hover){EDITOR_HOVER_FIND, row, (size_t)hit};
            }
        }
        return h;
    }
    if (row >= e->tab_rows && row < e->tab_rows + e->v.config.rows) {
        if (!e->buffer->source_stale && !e->buffer->pending_open && e->grid.region.pixels &&
            col >= e->text_grid.dims.cols && col < e->text_grid.dims.cols + e->map_cols)
            return (editor_hover){EDITOR_HOVER_MAP, row, 0};
        if (col >= e->lay.gw && col < e->text_grid.dims.cols) *shape = PLAT_CURSOR_TEXT;
    }
    return h;
}
int editor_hover_pointer(editor *e, const plat_event *ev)
{
    editor_hover next = {0}; plat_cursor_shape shape = PLAT_CURSOR_DEFAULT;
    if (ev->kind != PLAT_EV_LEAVE && ev->x >= 0 && ev->y >= 0) {
        uint32_t col = (uint32_t)ev->x / e->grid.dims.cell_w, row = (uint32_t)ev->y / e->grid.dims.cell_h;
        if (col < e->grid.dims.cols && row < e->grid.dims.rows) next = target(e, row, col, &shape);
    }
    /* Map identity is the region, not the particular scanline under it. */
    if (next.kind == EDITOR_HOVER_MAP) next.row = e->tab_rows;
    if (shape != e->pointer_shape) {
        e->pointer_shape = shape;
        if (e->has_platform) plat_set_cursor(&e->platform, shape);
    }
    editor_hover old = e->hover;
    if (old.kind == next.kind && old.index == next.index && old.row == next.row) return 0;
    e->hover = next;
    bool a = visible_ink(e, old), b = visible_ink(e, next);
    if (!a && !b) return 0;
    bool isolated = !e->dirty || e->hover_only;
    bool map_dirty = e->hover_map || old.kind == EDITOR_HOVER_MAP || next.kind == EDITOR_HOVER_MAP;
    int rc = editor_begin_frame(e); if (rc) return rc;
    e->hover_only = isolated; e->hover_map = map_dirty;
    if (a && old.kind != EDITOR_HOVER_MAP && old.row < e->grid.dims.rows) {
        rc = render_mark_rows(&e->grid, old.row, 1); if (rc) return rc;
    }
    if (b && next.kind != EDITOR_HOVER_MAP && next.row < e->grid.dims.rows)
        return render_mark_rows(&e->grid, next.row, 1);
    return 0;
}
