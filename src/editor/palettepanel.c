#include "editor/private.h"
#include "../../vendor/xkbcommon/xkbcommon-keysyms.h"
#include <string.h>

int editor_palette_show(editor *e)
{
    if (e->find_replace_active) { e->stats.rejected_commands++; return 0; }
    /* The palette overlays the find panel. Closing it or dispatching a
     * command restores the same field and its current source. */
    (void)palette_filter(e->palette, "", 0);
    e->palette_open = true; e->palette_owner = e->buffer->id;
    e->palette_generation = e->buffer->source_generation; e->palette_revision = e->buffer->revision;
    e->text_drag = false; keys_reset(&e->keys); undo_break_burst(e->undo);
    return editor_text_geometry(e);
}
int editor_palette_close(editor *e)
{
    if (!e->palette_open) return 0;
    e->palette_open = false; keys_reset(&e->keys);
    return editor_text_geometry(e);
}
int editor_palette_sync(editor *e)
{
    if (e->palette_open && (e->palette_owner != e->buffer->id ||
        e->palette_generation != e->buffer->source_generation || e->palette_revision != e->buffer->revision))
        return editor_palette_close(e);
    return 0;
}
int editor_palette_key(editor *e, const plat_event *ev)
{
    if (!ev->press) return 0;
    keys_reset(&e->keys);
    palette_state *p = e->palette;
    if (ev->keysym == XKB_KEY_Escape) return editor_palette_close(e);
    if (ev->mods & (PLAT_MOD_CTRL | PLAT_MOD_SUPER) ||
        ((ev->mods & PLAT_MOD_ALT) && !(ev->mods & PLAT_MOD_ALTGR))) return 0;
    if (ev->keysym == XKB_KEY_Return || ev->keysym == XKB_KEY_KP_Enter) {
        const keys_binding *binding = p->count ? p->rows[p->matches[p->selected]].binding : NULL;
        int rc = editor_palette_close(e); if (rc || !binding) return rc;
        /* The immutable command pointer also remains valid for repeat/history.
         * Synthesize its stroke so field/Return semantics see the same event
         * as the binding, rather than the palette's confirming Enter. */
        plat_event command = {.kind = PLAT_EV_KEY, .press = true, .keysym = binding->keysym,
            .mods = binding->modifiers, .t0_ns = ev->t0_ns};
        return editor_dispatch_action(e, &command, binding, KEYS_MATCH);
    }
    if (ev->keysym == XKB_KEY_Up) {
        if (p->selected) p->selected--;
    } else if (ev->keysym == XKB_KEY_Down) {
        if (p->count && p->selected < p->count - 1u) p->selected++;
    } else if (ev->keysym == XKB_KEY_BackSpace) {
        size_t length = p->query_length ? utf8_grapheme_prev((const uint8_t *)p->query, p->query_length) : 0;
        (void)palette_filter(p, p->query, length);
    } else if (ev->utf8_len) {
        if (ev->utf8_len >= sizeof p->query - p->query_length) { e->stats.rejected_commands++; return 0; }
        size_t length = p->query_length;
        memcpy(p->query + length, ev->utf8, ev->utf8_len);
        (void)palette_filter(p, p->query, length + ev->utf8_len);
    }
    return editor_begin_frame(e);
}
/* Same ASCII atlas/cell convention and style as findui/paint.c. Numbers are
 * (P) ASCII space/tilde bounds; colours are the find panel's theme tokens. */
static void cell(editor *e, uint32_t row, uint32_t column, unsigned char byte, bool selected, bool cursor)
{
    if (column >= e->grid.dims.cols) return;
    config_theme t = e->preferences.theme;
    render_cell value = {.atlas_slot = RENDER_NO_SLOT, .fg = config_theme_color(t, "editor.foreground"),
        .bg = config_theme_color(t, "chrome.background")};
    if (byte >= ' ' && byte <= '~') {
        value.atlas_slot = e->find_ascii[byte - (unsigned char)' '];
        if (value.atlas_slot != RENDER_NO_SLOT) value.glyph_index = e->grid.glyphs[value.atlas_slot].glyph_index;
    }
    if (selected) { value.fg = e->layout_cfg.sel_fg; value.bg = e->layout_cfg.sel_bg; }
    if (cursor) { value.fg = config_theme_color(t, "caret.foreground");
        value.bg = config_theme_color(t, "caret.background"); value.attrs = RENDER_ATTR_CURSOR; }
    e->grid.cells[(size_t)row * e->grid.dims.cols + column] = value;
}
static void text(editor *e, uint32_t row, uint32_t column, uint32_t limit, const char *bytes, bool selected)
{
    while (*bytes && column < limit) cell(e, row, column++, (unsigned char)*bytes++, selected, false);
}
int editor_palette_paint(editor *e)
{
    if (!e->palette_open || !e->panel_rows) return 0;
    palette_state *p = e->palette;
    uint32_t first = e->grid.dims.rows - e->panel_rows, cols = e->grid.dims.cols;
    if (render_mark_rows(&e->grid, first, e->panel_rows) != RENDER_OK) return EDITOR_ERR_ARG;
    /* (P) the query consumes the first panel row; the rest are action rows. */
    uint32_t visible = e->panel_rows - 1u;
    if (p->selected < p->first) p->first = p->selected;
    if (visible && p->selected - p->first >= visible) p->first = p->selected - visible + 1u;
    for (uint32_t r = 0; r < e->panel_rows; r++) {
        bool selected = r && p->first + r - 1u < p->count && p->first + r - 1u == p->selected;
        for (uint32_t c = 0; c < cols; c++) cell(e, first + r, c, ' ', selected, false);
    }
    const char prompt[] = "Command: ";
    text(e, first, 0, cols, prompt, false);
    uint32_t column = (uint32_t)(sizeof prompt - 1u);
    if (cols > column) {
        size_t width = cols - column;
        size_t scroll = p->query_length >= width ? p->query_length - width + 1u : 0;
        text(e, first, column, cols, p->query + scroll, false);
        cell(e, first, column + (uint32_t)(p->query_length - scroll), ' ', false, true);
    }
    for (uint32_t r = 0; r < visible && p->first + r < p->count; r++) {
        size_t index = p->first + r;
        const palette_row *row = &p->rows[p->matches[index]];
        const keys_binding *live = keys_binding_for_command(row->binding, e->preferences.bindings, e->preferences.binding_count);
        char chord[KEYS_CHORD_FORMAT_CAP]; chord[0] = '\0';
        if (live && keys_chord_format(live, chord, sizeof chord)) chord[0] = '\0';
        size_t length = strlen(chord);
        uint32_t right = length < cols ? cols - (uint32_t)length : cols;
        bool selected = index == p->selected;
        text(e, first + r + 1u, 0, right < cols && right ? right - 1u : right, row->label, selected);
        if (right < cols) text(e, first + r + 1u, right, cols, chord, selected);
    }
    return 0;
}
