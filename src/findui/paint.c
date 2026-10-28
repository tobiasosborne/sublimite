#include "findui/private.h"
#include <string.h>

typedef struct painter { render_grid *grid; const findui_style *style; uint32_t row; } painter;
static void cell(painter *p, uint32_t col, uint8_t byte, uint32_t fg, uint32_t bg, uint16_t attrs)
{
    if (col >= p->grid->dims.cols) return;
    render_cell v = {.atlas_slot = RENDER_NO_SLOT, .fg = fg, .bg = bg, .attrs = attrs};
    if (byte >= 32 && byte <= 126 && p->style->ascii_slots) {
        v.atlas_slot = p->style->ascii_slots[byte - 32u];
        if (v.atlas_slot != RENDER_NO_SLOT) v.glyph_index = p->grid->glyphs[v.atlas_slot].glyph_index;
    }
    p->grid->cells[(size_t)p->row * p->grid->dims.cols + col] = v;
}
static uint32_t text(painter *p, uint32_t col, uint32_t end, const char *s, uint32_t fg)
{
    while (*s && col < end) cell(p, col++, (uint8_t)*s++, fg, p->style->background, 0);
    return col;
}
static size_t append(char *s, size_t at, const char *word)
{ size_t n = strlen(word); memcpy(s + at, word, n); return at + n; }
static size_t decimal(char *s, size_t at, uint64_t n)
{
    char digits[20]; size_t count = 0; /* (P) decimal digits of UINT64_MAX */
    do { digits[count++] = (char)('0' + n % 10u); n /= 10u; } while (n);
    while (count) s[at++] = digits[--count];
    return at;
}
static size_t status(const findui_state *s, char *out)
{
    size_t at = 0;
    bool searching = s->searching && !s->complete; /* window refresh does not change the known total */
    if (s->replacing) at = append(out, at, "Replacing");
    else if (s->undo_error) at = append(out, at, "Undo error");
    else if (s->search_error != FIND_OK) at = append(out, at, "Invalid pattern");
    else if (s->query_length) {
        if (!s->match_count && !searching) at = append(out, at, "No results");
        else if (s->match_count) {
            at = decimal(out, at, s->match_index == FINDUI_NO_INDEX ? 0 : s->match_index + 1u);
            at = append(out, at, " of "); at = decimal(out, at, s->match_count);
        }
        if (searching) at = append(out, at, at ? " Searching" : "Searching");
        if (s->cache_overflow || s->visible_overflow) at = append(out, at, " Limited results");
    }
    out[at] = 0; return at;
}
findui_geometry findui_get_geometry(const findui_panel *panel, findui_bounds b)
{
    findui_geometry g = {b.end, b.end, b.end};
    const findui_impl *impl = panel ? panel->private_ : NULL;
    if (!impl || b.first >= b.end) return g;
    char summary[128]; size_t n = status(&impl->state, summary);
    /* (P) Aa + space + ab + space + .* = eight cells; count has a gap.
     * Retain at least one field cell and one separating cell. Tiny panels
     * omit the summary as a unit, leaving all available width to the field. */
    uint32_t summary_cells = 8u + (n ? (uint32_t)n + 1u : 0u);
    if (b.end - b.first >= summary_cells + 2u) {
        g.toggles = b.end - summary_cells; g.field_end = g.toggles - 1u;
        if (n) g.count = g.toggles + 9u;
    }
    return g;
}
static size_t byte_cells(uint8_t b) { return b >= ' ' && b <= '~' ? 1u : sizeof "\\xHH" - 1u; }
static size_t field_scroll(const uint8_t *bytes, size_t cursor, uint32_t width)
{
    size_t col = 0; for (size_t i = 0; i < cursor; i++) col += byte_cells(bytes[i]);
    return col >= width ? col - width + 1u : 0;
}
static void field(painter *p, uint32_t col, uint32_t end, const uint8_t *bytes,
                  size_t length, size_t cursor, bool focus, const char *placeholder)
{
    if (end <= col) return;
    if (!length) {
        (void)text(p, col, end, placeholder, p->style->muted);
        if (focus) p->grid->cells[(size_t)p->row * p->grid->dims.cols + col].attrs |= RENDER_ATTR_UNDERLINE;
        return;
    }
    size_t scroll = field_scroll(bytes, cursor, end - col), virtual_col = 0;
    for (size_t i = 0; i <= length; i++) {
        uint8_t out[4]; size_t n = 1;
        out[0] = i == length ? ' ' : bytes[i];
        if (i < length && byte_cells(bytes[i]) > 1u) {
            const char *digits = "0123456789ABCDEF"; n = 4;
            out[0] = '\\'; out[1] = 'x'; out[2] = (uint8_t)digits[bytes[i] >> 4u]; out[3] = (uint8_t)digits[bytes[i] & 15u];
        }
        for (size_t j = 0; j < n; j++, virtual_col++) if (virtual_col >= scroll && virtual_col - scroll < end - col) {
            bool caret = focus && i == cursor && !j;
            cell(p, col + (uint32_t)(virtual_col - scroll), out[j],
                caret ? p->style->cursor_foreground : p->style->foreground,
                caret ? p->style->cursor_background : p->style->background, caret ? RENDER_ATTR_CURSOR : 0);
        }
        if (virtual_col >= scroll + end - col) break;
    }
}
static findui_geometry hit_geometry(const findui_panel *panel, findui_bounds b)
{
    const findui_impl *impl = panel->private_;
    return impl->painted_geometry_valid && impl->painted_bounds.first == b.first && impl->painted_bounds.end == b.end ?
        impl->painted_geometry : findui_get_geometry(panel,b);
}
findui_hit_kind findui_hit_target(const findui_panel *panel, findui_bounds b,
                                  uint32_t rows, uint32_t row, uint32_t col)
{
    const findui_impl *impl = panel ? panel->private_ : NULL;
    if (!impl || !impl->state.open || b.first > b.end || row >= rows || col < b.first || col >= b.end) return FINDUI_HIT_NONE;
    findui_geometry g = hit_geometry(panel, b);
    if (!row && col >= g.toggles) {
        uint32_t local = col - g.toggles;
        if (local < 2u) return FINDUI_HIT_CASE;
        if (local >= 3u && local < 5u) return FINDUI_HIT_WORD;
        if (local >= 6u && local < 8u) return FINDUI_HIT_REGEX;
        return FINDUI_HIT_NONE;
    }
    if (row == 1u && impl->state.replace_mode) return FINDUI_HIT_REPLACEMENT;
    return !row && col < g.field_end ? FINDUI_HIT_QUERY : FINDUI_HIT_NONE;
}
findui_code findui_hit_test(const findui_panel *panel, findui_bounds b,
                           uint32_t rows, uint32_t row, uint32_t col, findui_hit *hit)
{
    const findui_impl *impl = panel ? panel->private_ : NULL;
    if (!impl || !hit || b.first > b.end || row >= rows) return FINDUI_ERR_ARGUMENT;
    *hit = (findui_hit){findui_hit_target(panel,b,rows,row,col),0};
    if (hit->kind != FINDUI_HIT_QUERY && hit->kind != FINDUI_HIT_REPLACEMENT) return FINDUI_OK;
    const findui_state *s = &impl->state;
    bool replacement = hit->kind == FINDUI_HIT_REPLACEMENT;
    uint32_t end = replacement ? b.end : hit_geometry(panel,b).field_end;
    const uint8_t *bytes = replacement ? s->replacement : s->query;
    size_t length = replacement ? s->replacement_length : s->query_length;
    size_t cursor = replacement ? s->replacement_cursor : s->query_cursor;
    size_t target = field_scroll(bytes, cursor, end - b.first) + col - b.first, at = 0;
    while (hit->cursor < length) {
        size_t n = byte_cells(bytes[hit->cursor]);
        if (at + n > target) break;
        at += n; hit->cursor++;
    }
    return FINDUI_OK;
}
static uint32_t separator(painter *p, uint32_t col, uint32_t end)
{
    col = text(p, col, end, " ", p->style->muted);
    if (col < end) {
        cell(p, col, ' ', p->style->muted, p->style->background, 0);
        if (p->style->dot_slot != RENDER_NO_SLOT && p->style->dot_slot < p->grid->glyph_count) {
            render_cell *v = &p->grid->cells[(size_t)p->row * p->grid->dims.cols + col];
            v->atlas_slot = p->style->dot_slot; v->glyph_index = p->grid->glyphs[v->atlas_slot].glyph_index;
        }
        col++;
    }
    return text(p, col, end, " ", p->style->muted);
}
static void toggles(painter *p, const findui_state *s, findui_geometry g, findui_bounds b)
{
    const findui_style *style=p->style;
    const char *labels[] = {"Aa", "ab", ".*"};
    const bool on[] = {s->options.match_case, s->options.whole_word, s->options.regex};
    for (uint32_t k = 0; k < 3u && g.toggles < b.end; k++) for (uint32_t c = 0; c < 2u; c++)
        cell(p, g.toggles + k * 3u + c, (uint8_t)labels[k][c], on[k] ? style->foreground : style->muted,
            (style->hover_toggle == (uint32_t)(k == 0 ? FINDUI_HIT_CASE : k == 1u ? FINDUI_HIT_WORD : FINDUI_HIT_REGEX)) ? style->hover_background : on[k] ? style->selected_background : style->background, k == 1u ? RENDER_ATTR_UNDERLINE : 0);
}
findui_code findui_render(const findui_panel *panel, render_grid *grid, uint32_t first,
                         uint32_t rows, const findui_style *style)
{
    findui_impl *impl = panel ? panel->private_ : NULL;
    if (!impl || !grid || !style || !grid->begun || !grid->cells || !grid->dirty ||
        !grid->dims.cols || !rows || first >= grid->dims.rows || rows > grid->dims.rows - first ||
        (size_t)grid->dims.rows > SIZE_MAX / grid->dims.cols ||
        grid->cell_capacity < (size_t)grid->dims.rows * grid->dims.cols ||
        grid->dirty_word_capacity < ((size_t)grid->dims.rows + 63u) / 64u ||
        style->bounds.first > style->bounds.end || style->bounds.end > grid->dims.cols ||
        ((style->foreground | style->background | style->accent | style->cursor_foreground |
          style->cursor_background | style->muted | style->error | style->selected_background | style->hover_background) & UINT32_C(0xff000000))) return FINDUI_ERR_ARGUMENT;
    if (style->ascii_slots) {
        if (style->ascii_slot_count < 95) return FINDUI_ERR_ARGUMENT;
        for (size_t i = 0; i < 95; i++) {
            uint32_t slot = style->ascii_slots[i];
            if (slot != RENDER_NO_SLOT && (!grid->glyphs || slot >= grid->glyph_count)) return FINDUI_ERR_ARGUMENT;
        }
    }
    if (!style->hover_only && render_mark_rows(grid, first, rows) != RENDER_OK) return FINDUI_ERR_ARGUMENT;
    (void)findui_get_state(panel);
    painter p = {grid, style, first}; findui_bounds b = style->bounds;
    if (style->hover_only) {
        if (impl->state.open && render_row_dirty(grid,first)) toggles(&p,&impl->state,hit_geometry(panel,b),b);
        return FINDUI_OK;
    }
    for (uint32_t r = first; r < first + rows; r++) {
        if (style->hover_only && !render_row_dirty(grid, r)) continue;
        p.row = r;
        for (uint32_t c = 0; c < grid->dims.cols; c++) cell(&p, c, ' ', style->foreground, style->background, 0);
    }
    if (!impl->state.open) return FINDUI_OK;
    const findui_state *s = &impl->state; findui_geometry g = findui_get_geometry(panel, b);
    impl->painted_bounds=b; impl->painted_geometry=g; impl->painted_geometry_valid=true;
    p.row = first;
    if (!style->hover_only || render_row_dirty(grid, first)) {
    field(&p, b.first, g.field_end, impl->query, s->query_length, s->query_cursor, !s->replacement_focus, "Find");
    toggles(&p,s,g,b);
    char summary[128]; (void)status(s, summary);
    if (g.count < b.end) (void)text(&p, g.count, b.end, summary, s->search_error != FIND_OK ? style->error : style->accent);
    }
    if (rows >= 2 && s->replace_mode && (!style->hover_only || render_row_dirty(grid, first + 1u))) {
        p.row = first + 1u;
        field(&p, b.first, b.end, impl->replacement, s->replacement_length, s->replacement_cursor, s->replacement_focus, "Replace");
    }
    if (rows >= 3 && (!style->hover_only || render_row_dirty(grid, first + 2u))) {
        p.row = first + 2u;
        uint32_t col = text(&p, b.first, b.end, "Enter next", style->muted);
        col = separator(&p, col, b.end); col = text(&p, col, b.end, "Shift+Enter previous", style->muted);
        col = separator(&p, col, b.end); (void)text(&p, col, b.end, "Esc close", style->muted);
    }
    return FINDUI_OK;
}
