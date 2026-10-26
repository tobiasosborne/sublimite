#include "editor/private.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>

static void font_selection_set(editor *e, const char *family, const char *font_file)
{
    if (!family) family = "";
    if (!font_file) font_file = "";
    font_selection parsed = {0};
    if (font_selection_parse(&parsed, family, font_file)) {
        /* Invalid selection syntax still restores the guaranteed fallback;
         * its diagnostic is scheduled on a worker, once per editor lifetime. */
        editor_font_bad_config(e); family = font_file = "";
    }
    if (!strcmp(e->preferences_font_family, family) &&
        !strcmp(e->preferences_font_file, font_file)) return;
    strcpy(e->preferences_font_family, family);
    strcpy(e->preferences_font_file, font_file);
    e->preferences_font_selection_pending = true;
}
/* edit-6e2: the chain text crosses config -> editor -> font unchanged. */
_Static_assert(FONT_CHAIN_TEXT_MAX == CONFIG_TEXT_CAP, "font.fallbacks text bound is the config bound");
/* CLI/env form: comma-separated, surrounding spaces trimmed, empty entries
 * dropped, joined by '\n' like the config key. Output never exceeds input. */
static int chain_from_list(char *out, size_t cap, const char *list)
{
    size_t used = 0;
    if (strlen(list) >= cap || strpbrk(list, "\n\r\t")) return -1;
    for (const char *at = list;;) {
        const char *end = strchr(at, ',');
        if (!end) end = at + strlen(at);
        const char *first = at, *last = end;
        while (first < last && *first == ' ') first++;
        while (last > first && last[-1] == ' ') last--;
        if (last > first) {
            if (used) out[used++] = '\n';
            memcpy(out + used, first, (size_t)(last - first)); used += (size_t)(last - first);
        }
        if (!*end) break;
        at = end + 1;
    }
    out[used] = 0;
    return 0;
}
static void font_fallbacks_set(editor *e, const char *chain)
{
    if (!strcmp(e->preferences_font_fallbacks, chain)) return;
    memcpy(e->preferences_font_fallbacks, chain, strlen(chain) + 1u);
    e->preferences_font_selection_pending = true;
}
static int font_selection_poll(editor *e)
{
    if (!e->preferences_font_selection_pending) return 0;
    font_selection parsed = {0};
    if (font_selection_parse(&parsed, e->preferences_font_family, e->preferences_font_file))
        return EDITOR_ERR_ARG;
    const font_selection *requested = &e->font.selection;
    /* A reload can return to the in-flight selection before it is adopted.
     * Compare original UI requests, never worker-resolved file paths. */
    if (!strcmp(parsed.family, requested->family) && !strcmp(parsed.file, requested->file) &&
        parsed.index == requested->index &&
        !strcmp(e->preferences_font_fallbacks, e->font.fallbacks)) {
        e->preferences_font_selection_pending = false; return 0;
    }
    int rc = editor_font_request_chain(e, e->preferences_font_family, e->preferences_font_file,
                                       e->preferences_font_fallbacks);
    if (rc == EDITOR_MORE) return 0; /* retained and retried on the next turn */
    if (!rc) e->preferences_font_selection_pending = false;
    return rc;
}
int editor_preferences_font_init(editor *e)
{
    const char *family = e->cfg.font_family, *font_file = e->cfg.font_file;
    e->preferences_font_selection_override = family || font_file;
    if (!e->preferences_font_selection_override) {
        family = getenv("EDIT_FONT_FAMILY"); font_file = getenv("EDIT_FONT_FILE");
        e->preferences_font_selection_override = (family && *family) || (font_file && *font_file);
    }
    if (!e->preferences_font_selection_override) {
        const config_settings *settings = config_runtime_current(e->preferences_runtime);
        family = settings ? settings->font_family : NULL;
        font_file = settings ? settings->font_file : NULL;
    }
    font_selection_set(e, family, font_file);
    /* edit-6e2: the chain has its own edit-5ih precedence: CLI (even empty)
     * > EDIT_FONT_FALLBACKS (non-empty) > config font.fallbacks. */
    const char *list = e->cfg.font_fallbacks;
    e->preferences_font_fallbacks_override = list != NULL;
    if (!list) {
        list = getenv("EDIT_FONT_FALLBACKS");
        e->preferences_font_fallbacks_override = list && *list;
    }
    char chain[FONT_CHAIN_TEXT_MAX] = "";
    if (e->preferences_font_fallbacks_override) {
        if (chain_from_list(chain, sizeof chain, list)) { editor_font_bad_config(e); chain[0] = 0; }
    } else {
        const config_settings *settings = config_runtime_current(e->preferences_runtime);
        if (settings) memcpy(chain, settings->font_fallbacks, sizeof chain);
    }
    font_fallbacks_set(e, chain);
    /* args/env storage may be released as soon as open returns. */
    e->cfg.font_family = e->cfg.font_file = e->cfg.font_fallbacks = NULL;
    return font_selection_poll(e);
}

const config_settings *editor_preferences(const editor *e) { return e ? &e->preferences : NULL; }
uint64_t editor_preferences_generation(const editor *e) { return e ? e->preferences_generation : 0; }
void editor_preferences_buffer(editor *e, editor_buffer *b)
{
    b->style = b->detected_style;
    if (!b->style.width) b->style.width = 4; /* existing view/indent default */
    if (e->preferences.tab_width) b->style.width = (uint8_t)e->preferences.tab_width;
    if (e->preferences.tabs_set) b->style.uses_tabs = !e->preferences.tab_spaces;
    if (e->preferences.wrap_set) b->initial.wrap = e->preferences.wrap;
    else if (b->config_wrap) b->initial.wrap = e->cfg.wrap_mode ? e->cfg.wrap_mode > 0 : view_wrap_default(b->path);
    b->config_wrap = e->preferences.wrap_set;
}
int editor_preferences_poll(editor *e)
{
    if (!e->preferences_runtime) return 0;
    (void)config_runtime_tick(e->preferences_runtime);
    int fd = config_runtime_watchfd(e->preferences_runtime);
    if (fd >= 0 && fd != e->preferences_watchfd) {
        if (editor_wait_file(&e->poll_set, fd) != OS_OK) return EDITOR_ERR_IO;
        e->preferences_watchfd = fd;
    }
    return 0;
}
int editor_preferences_apply(editor *e)
{
    if (!e->preferences_runtime || e->preferences_generation == config_runtime_generation(e->preferences_runtime))
        return font_selection_poll(e);
    bool was_wrap_set = e->preferences.wrap_set;
    bool old_wrap = e->v.state.wrap; uint32_t old_tab = e->lay.tab;
    e->preferences = *config_runtime_current(e->preferences_runtime);
    e->preferences_generation = config_runtime_generation(e->preferences_runtime);
    keys_reset(&e->keys);
    editor_preferences_buffer(e, e->buffer);
    e->lay.row = e->lay.row_end; /* discard old settings' layout continuation */
    const config_settings *s = &e->preferences;
    if (!e->preferences_font_selection_override)
        font_selection_set(e, s->font_family, s->font_file);
    if (!e->preferences_font_fallbacks_override) font_fallbacks_set(e, s->font_fallbacks);
    e->cfg.font_points = e->preferences_font_override != 0.0 ? e->preferences_font_override : s->font_points;
    if (!e->font.legacy) {
        double points = e->cfg.font_points != 0.0 ? e->cfg.font_points : EDITOR_FONT_DEFAULT_PT;
        uint32_t px = editor_runtime_em_px(e->font.dpi, points);
        /* Keep the current font for reload sizes outside the prepaid atlas.
         * Startup retains editor_font_init's explicit argument validation. */
        if (px && px <= EDITOR_FONT_MAX_PX && points != e->font.default_points) {
            e->font.default_points = e->font.points = points;
            e->font.default_px = e->font.requested_px = px;
        }
    }
    config_theme theme = s->theme;
    if (e->has_platform) plat_set_dark(&e->platform, theme == CONFIG_DARK); /* edit-29y2 */
    e->layout_cfg.fg = config_theme_color(theme, "editor.foreground");
    e->layout_cfg.bg = config_theme_color(theme, "editor.background");
    e->layout_cfg.gutter_fg = config_theme_color(theme, "gutter.foreground");
    e->layout_cfg.gutter_bg = config_theme_color(theme, "gutter.background");
    e->layout_cfg.cursor_fg = config_theme_color(theme, "caret.foreground");
    e->layout_cfg.cursor_bg = config_theme_color(theme, "caret.background");
    e->layout_cfg.sel_fg = config_theme_color(theme, "selection.foreground");
    e->layout_cfg.sel_bg = config_theme_color(theme, "selection.background");
    e->layout_cfg.tab_width = e->buffer->style.width;
    e->lay.cfg = e->layout_cfg; e->lay.tab = e->buffer->style.width;
    int rc = view_set_tab_width(&e->v, e->buffer->style.width); if (rc) return EDITOR_ERR_ARG;
    bool wrap = s->wrap_set ? s->wrap : was_wrap_set ?
        e->cfg.wrap_mode ? e->cfg.wrap_mode > 0 : view_wrap_default(e->buffer->path) : e->v.state.wrap;
    rc = layout_set_wrap(&e->lay, wrap); if (rc) return rc;
    rc = view_set_wrap(&e->v, wrap, &e->lay); if (rc) return EDITOR_ERR_ARG;
    if (wrap != old_wrap || e->lay.tab != old_tab) {
        e->geometry_generation++; /* reloaded wrap/tab-width geometry, as at activation */
        if (!wrap) { /* the resident scroll window was not maintained while wrapped */
            rc = editor_scroll_resize(e); if (rc) return rc;
            if (e->v.state.first_byte != e->buffer->scroll.first_byte) {
                rc = editor_scroll_seek(e, e->v.state.first_byte); if (rc) return rc;
            }
        }
    }
    rc = editor_full_layout(e); if (rc) return rc;
    rc = editor_menu_configure(e, s->menu_visible); if (rc) return rc; /* edit-yey */
    return font_selection_poll(e);
}
