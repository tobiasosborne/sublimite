/* edit-yey: Sublime-style menu bar. One row above the tab strip, costing
 * zero rows while hidden; drop-downs overlay the grid like the palette.
 * Items dispatch through editor_dispatch_action, the palette's dispatcher.
 * All state is embedded in the editor (no allocation on any path here). */
#include "editor/private.h"
#include "../../vendor/xkbcommon/xkbcommon-keysyms.h"
#include <string.h>

#define MODS (PLAT_MOD_CTRL | PLAT_MOD_SHIFT | PLAT_MOD_ALT | PLAT_MOD_SUPER | PLAT_MOD_ALTGR)
bool editor_menu_visible(const editor *e)
{
    return e->menu.pinned || e->menu.active || e->menu.open >= 0;
}
/* A visibility change moves the text by one row (full geometry); other
 * menu changes repaint the overlay only when asked. */
static int update(editor *e, bool was, bool repaint)
{
    if (editor_menu_visible(e) != was) return editor_text_geometry(e);
    return repaint ? editor_begin_frame(e) : 0;
}
static void close_all(editor *e)
{
    e->menu.armed = e->menu.active = false; e->menu.open = -1;
}
static void open_menu(editor *e, size_t menu)
{
    e->menu.active = true; e->menu.open = (int)menu; e->menu.title = menu;
    e->menu.item = e->menu.first = 0;
}
int editor_menu_reset(editor *e)
{
    bool was = editor_menu_visible(e);
    close_all(e); return update(e, was, false);
}
int editor_menu_toggle(editor *e)
{
    bool was = editor_menu_visible(e);
    e->menu.pinned = !e->menu.pinned; return update(e, was, false);
}
int editor_menu_configure(editor *e, bool visible)
{
    /* A reload re-pins only when the key itself changed, so an unrelated
     * reload keeps a runtime View > Toggle Menu. */
    if (visible == e->menu.config_visible) return 0;
    bool was = editor_menu_visible(e);
    e->menu.config_visible = e->menu.pinned = visible;
    return update(e, was, false);
}
static bool alt_key(uint32_t sym) { return sym == XKB_KEY_Alt_L || sym == XKB_KEY_Alt_R; }
static bool modifier_key(uint32_t sym)
{
    return (sym >= XKB_KEY_Shift_L && sym <= XKB_KEY_Hyper_R) || sym == XKB_KEY_ISO_Level3_Shift ||
        sym == XKB_KEY_ISO_Level5_Shift || sym == XKB_KEY_Mode_switch || sym == XKB_KEY_Num_Lock;
}
static const keys_binding *item_command(const editor *e, size_t item)
{
    const menu_def *d = &menu_table()[e->menu.open];
    return item < d->count ? menu_command(d->items[item]) : NULL;
}
static bool run(editor *e, const plat_event *ev, int *rc)
{
    const keys_binding *binding = item_command(e, e->menu.item);
    bool was = editor_menu_visible(e);
    close_all(e); keys_reset(&e->keys);
    *rc = update(e, was, true);
    if (*rc || !binding) return true;
    /* As the palette does: the immutable command plus its own stroke, so
     * field/Return semantics see the binding, not the confirming input. */
    plat_event command = {.kind = PLAT_EV_KEY, .press = true, .keysym = binding->keysym,
        .mods = binding->modifiers, .t0_ns = ev->t0_ns};
    *rc = editor_dispatch_action(e, &command, binding, KEYS_MATCH);
    return true;
}
bool editor_menu_key(editor *e, const plat_event *ev, int *rc)
{
    struct editor_menu *m = &e->menu; *rc = 0;
    bool was = editor_menu_visible(e);
    uint16_t mods = (uint16_t)(ev->mods & MODS);
    if (alt_key(ev->keysym)) {
        if (ev->press) {
            if (ev->repeat) return false;
            /* The press carries the modifiers held before it: Alt arms the
             * bar only alone. Arming changes no layout, so a bound Alt chord
             * leaves the grid untouched; the press keeps its inert handling. */
            m->armed = !(mods & (PLAT_MOD_CTRL | PLAT_MOD_SHIFT | PLAT_MOD_SUPER | PLAT_MOD_ALTGR));
            if (m->armed) m->was_active = m->active || m->open >= 0;
            return false;
        }
        bool bare = m->armed; m->armed = false;
        if (bare) {
            /* Press-and-release with nothing between: latch keyboard focus
             * on the bar, or close it when it already had focus. */
            if (m->was_active) { m->active = false; m->open = -1; }
            else { m->active = true; m->open = -1; m->title = 0; }
        }
        *rc = update(e, was, bare); return bare;
    }
    if (!ev->press) return false;
    m->armed = false;
    if (modifier_key(ev->keysym)) return false;
    bool latched = m->active || m->open >= 0;
    if ((mods & PLAT_MOD_ALT) && !(mods & (PLAT_MOD_CTRL | PLAT_MOD_SUPER | PLAT_MOD_ALTGR))) {
        /* A bound Alt chord always wins; an unbound Alt+letter opens its menu. */
        keys_state probe = e->keys; const keys_binding *bound = NULL;
        keys_result result = config_keys_lookup(&e->preferences, &probe, ev->keysym, ev->mods, &bound);
        int menu = mods & PLAT_MOD_SHIFT ? -1 : menu_mnemonic(ev->keysym);
        if (result == KEYS_NONE && menu >= 0) {
            open_menu(e, (size_t)menu); keys_reset(&e->keys);
            *rc = update(e, was, true); return true;
        }
        if (latched) close_all(e);
        *rc = update(e, was, false); return *rc != 0;
    }
    if (!latched) { *rc = update(e, was, false); return *rc != 0; } /* steals nothing */
    if (mods & (PLAT_MOD_CTRL | PLAT_MOD_SUPER)) { /* a shortcut leaves the menu, then runs */
        close_all(e); *rc = update(e, was, false); return *rc != 0;
    }
    keys_reset(&e->keys);
    size_t count = m->open >= 0 ? menu_table()[m->open].count : 0;
    switch (ev->keysym) {
    case XKB_KEY_Escape:
        if (m->open >= 0) m->open = -1; else m->active = false;
        break;
    case XKB_KEY_Left: case XKB_KEY_Right:
        m->title = (m->title + (ev->keysym == XKB_KEY_Left ? MENU_COUNT - 1u : 1u)) % MENU_COUNT;
        if (m->open >= 0) open_menu(e, m->title);
        break;
    case XKB_KEY_Down:
        if (m->open < 0) open_menu(e, m->title);
        else if (m->item + 1u < count) m->item++;
        break;
    case XKB_KEY_Up:
        if (m->open >= 0 && m->item) m->item--;
        break;
    case XKB_KEY_Return: case XKB_KEY_KP_Enter:
        if (m->open >= 0) return run(e, ev, rc);
        open_menu(e, m->title);
        break;
    default: {
        int menu = menu_mnemonic(ev->keysym);
        if (menu >= 0) open_menu(e, (size_t)menu);
        break; /* other keys are swallowed while the bar has focus */
    }
    }
    *rc = update(e, was, true); return true;
}
/* Drop-down box: directly below the bar, starting at its title's slot and
 * shifted left to fit; one padding cell each side, two between label and
 * chord. Rows clip to the grid and scroll like the palette's rows. */
typedef struct drop_box { uint32_t row, col, width, visible; } drop_box;
static size_t item_chord(const editor *e, size_t item, char *chord, size_t capacity)
{
    chord[0] = '\0';
    const keys_binding *command = item_command(e, item);
    const keys_binding *live = command ? keys_binding_for_command(command, e->preferences.bindings, e->preferences.binding_count) : NULL;
    if (live && keys_chord_format(live, chord, capacity)) chord[0] = '\0';
    return strlen(chord);
}
static const char *item_label(const editor *e, size_t item)
{
    return e->palette->rows[menu_table()[e->menu.open].items[item] - KEYS_ACTION_LEFT].label;
}
static drop_box dropdown(editor *e)
{
    const menu_def *d = &menu_table()[e->menu.open];
    uint32_t cols = e->grid.dims.cols, rows = e->grid.dims.rows;
    size_t widest = 0; char chord[KEYS_CHORD_FORMAT_CAP];
    for (size_t i = 0; i < d->count; i++) {
        size_t n = strlen(item_label(e, i)), c = item_chord(e, i, chord, sizeof chord);
        if (c) n += 2u + c;
        if (n > widest) widest = n;
    }
    drop_box box = {.row = e->menu_rows};
    box.width = widest + 2u < cols ? (uint32_t)widest + 2u : cols;
    box.col = menu_title_column((size_t)e->menu.open);
    if (box.col > cols - box.width) box.col = cols - box.width;
    box.visible = box.row < rows ? rows - box.row : 0;
    if (box.visible > d->count) box.visible = (uint32_t)d->count;
    if (e->menu.item < e->menu.first) e->menu.first = e->menu.item;
    if (box.visible && e->menu.item - e->menu.first >= box.visible) e->menu.first = e->menu.item - box.visible + 1u;
    return box;
}
bool editor_menu_pointer(editor *e, const plat_event *ev, int *rc)
{
    struct editor_menu *m = &e->menu; *rc = 0;
    if (ev->kind == PLAT_EV_BUTTON && ev->press) m->armed = false;
    if (ev->kind != PLAT_EV_BUTTON || ev->code != 1 || !ev->press || !editor_menu_visible(e)) return false;
    bool was = true, latched = m->active || m->open >= 0;
    int64_t col = ev->x < 0 ? -1 : (int64_t)ev->x / e->grid.dims.cell_w;
    int64_t row = ev->y < 0 ? -1 : (int64_t)ev->y / e->grid.dims.cell_h;
    if (e->menu_rows && row == 0) {
        int hit = -1;
        for (size_t i = 0; i < MENU_COUNT; i++) {
            int64_t start = menu_title_column(i);
            if (col >= start && col < start + (int64_t)strlen(menu_table()[i].title) + 2) hit = (int)i;
        }
        if (hit >= 0 && hit != m->open) open_menu(e, (size_t)hit); else close_all(e);
        *rc = update(e, was, true); return true;
    }
    if (m->open >= 0) {
        drop_box box = dropdown(e);
        if (row >= box.row && row < (int64_t)box.row + box.visible && col >= box.col && col < (int64_t)box.col + box.width) {
            m->item = m->first + (size_t)(row - box.row);
            return run(e, ev, rc);
        }
    }
    if (!latched) return false; /* a pinned idle bar leaves other clicks alone */
    close_all(e); *rc = update(e, was, true); return true;
}
/* Paint with change detection, so an unchanged overlay adds no damage and
 * a caret-only frame stays caret-only. ASCII atlas as the palette/find
 * panel; (P) drop-down RGB values are the existing find/palette style. */
static bool put(editor *e, uint32_t row, uint32_t column, unsigned char byte, uint32_t fg, uint32_t bg, uint16_t attrs)
{
    if (row >= e->grid.dims.rows || column >= e->grid.dims.cols) return false;
    render_cell value = {.atlas_slot = RENDER_NO_SLOT, .fg = fg, .bg = bg, .attrs = attrs};
    if (byte >= ' ' && byte <= '~') {
        value.atlas_slot = e->find_ascii[byte - (unsigned char)' '];
        if (value.atlas_slot != RENDER_NO_SLOT) value.glyph_index = e->grid.glyphs[value.atlas_slot].glyph_index;
    }
    render_cell *cell = &e->grid.cells[(size_t)row * e->grid.dims.cols + column];
    if (!memcmp(cell, &value, sizeof value)) return false;
    *cell = value; return true;
}
static int mark(editor *e, uint32_t row, bool changed)
{
    if (!changed) return 0;
    e->caret_only = false;
    return render_mark_rows(&e->grid, row, 1) == RENDER_OK ? 0 : EDITOR_ERR_ARG;
}
int editor_menu_paint(editor *e)
{
    const struct editor_menu *m = &e->menu;
    uint32_t cols = e->grid.dims.cols; int rc;
    config_theme theme = e->preferences.theme;
    if (e->menu_rows) {
        uint32_t fg = config_theme_color(theme, "chrome.foreground"), bg = config_theme_color(theme, "chrome.background");
        bool focus = m->active || m->open >= 0, changed = false;
        size_t lit = m->open >= 0 ? (size_t)m->open : m->title;
        uint32_t column = 0;
        for (size_t i = 0; i < MENU_COUNT && column < cols; i++) {
            const char *title = menu_table()[i].title;
            bool selected = focus && i == lit;
            uint32_t f = selected ? e->layout_cfg.sel_fg : fg, b = selected ? e->layout_cfg.sel_bg : bg;
            size_t underline = menu_mnemonic_offset(i), n = strlen(title);
            changed |= put(e, 0, column++, ' ', f, b, 0);
            for (size_t c = 0; c < n; c++)
                changed |= put(e, 0, column++, (unsigned char)title[c], f, b, c == underline ? RENDER_ATTR_UNDERLINE : 0);
            changed |= put(e, 0, column++, ' ', f, b, 0);
        }
        for (; column < cols; column++) changed |= put(e, 0, column, ' ', fg, bg, 0);
        rc = mark(e, 0, changed); if (rc) return rc;
    }
    if (m->open < 0) return 0;
    drop_box box = dropdown(e);
    uint32_t dim = config_theme_color(theme, "gutter.foreground");
    char chord[KEYS_CHORD_FORMAT_CAP];
    for (uint32_t r = 0; r < box.visible; r++) {
        size_t item = e->menu.first + r;
        const keys_binding *command = item_command(e, item);
        bool selected = item == m->item;
        bool unimplemented = command && (command->args.flags & KEYS_ARG_UNIMPLEMENTED);
        uint32_t fg = selected ? e->layout_cfg.sel_fg : unimplemented ? dim : 0xd8dee9;
        uint32_t bg = selected ? e->layout_cfg.sel_bg : 0x171b22;
        const char *label = item_label(e, item);
        size_t length = item_chord(e, item, chord, sizeof chord);
        uint32_t right = box.col + box.width - 1u; /* the trailing padding cell */
        uint32_t chord_at = length < right - box.col ? right - (uint32_t)length : right;
        bool changed = false; size_t at = 0, from = 0;
        for (uint32_t c = box.col; c < box.col + box.width; c++) {
            unsigned char byte = ' ';
            if (c > box.col && c < chord_at && label[at]) byte = (unsigned char)label[at++];
            else if (c >= chord_at && c < right && chord[from]) byte = (unsigned char)chord[from++];
            changed |= put(e, box.row + r, c, byte, fg, bg, 0);
        }
        rc = mark(e, box.row + r, changed); if (rc) return rc;
    }
    return 0;
}
