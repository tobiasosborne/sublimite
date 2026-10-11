#include "editor/private.h"
#include <sys/epoll.h>
#include <errno.h>

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
        struct epoll_event event = {.events = EPOLLIN, .data.fd = fd};
        if (epoll_ctl(e->poll_fd, EPOLL_CTL_ADD, fd, &event) && errno != EEXIST) return EDITOR_ERR_IO;
        e->preferences_watchfd = fd;
    }
    return 0;
}
int editor_preferences_apply(editor *e)
{
    if (!e->preferences_runtime || e->preferences_generation == config_runtime_generation(e->preferences_runtime)) return 0;
    bool was_wrap_set = e->preferences.wrap_set;
    e->preferences = *config_runtime_current(e->preferences_runtime);
    e->preferences_generation = config_runtime_generation(e->preferences_runtime);
    keys_reset(&e->keys);
    editor_preferences_buffer(e, e->buffer);
    e->lay.row = e->lay.row_end; /* discard old settings' layout continuation */
    const config_settings *s = &e->preferences;
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
    return editor_full_layout(e);
}
