#include "config/private.h"
#include "layout/layout.h"
#include "indent/indent.h"
#include <string.h>
#include <math.h>

/* Generated from the public keys_action enum; every enumerated action has its
 * lowercase name. This is immutable data, not a second default keymap. */
#include "config/actions.inc"
void config_defaults(config_settings *settings)
{
    if (settings) memset(settings, 0, sizeof *settings);
}
static int chord(const char *s, keys_binding *binding)
{
    const char *part = s; binding->modifiers = 0;
    for (;;) {
        const char *plus = strchr(part, '+');
        if (!plus || plus == part) break;
        size_t n = (size_t)(plus - part); uint16_t mod;
        if (n == strlen("Ctrl") && !memcmp(part, "Ctrl", n)) mod = PLAT_MOD_CTRL;
        else if (n == strlen("Shift") && !memcmp(part, "Shift", n)) mod = PLAT_MOD_SHIFT;
        else if (n == strlen("Alt") && !memcmp(part, "Alt", n)) mod = PLAT_MOD_ALT;
        else if (n == strlen("Super") && !memcmp(part, "Super", n)) mod = PLAT_MOD_SUPER;
        else return CONFIG_ERR_VALUE;
        if (binding->modifiers & mod) return CONFIG_ERR_VALUE;
        binding->modifiers |= mod; part = plus + 1;
    }
    if (!*part || strchr(part, ' ') || strchr(part, '\t')) return CONFIG_ERR_VALUE;
    binding->keysym = strlen(part) == 1u ? (uint8_t)*part : keys_keysym_from_name(part);
    return keys_binding_normalize(binding) ? CONFIG_OK : CONFIG_ERR_VALUE;
}
static int override(config_settings *s, const char *name, const char *stroke)
{
    keys_action action = KEYS_ACTION_NONE;
    for (size_t i = 0; i < sizeof action_names / sizeof action_names[0]; i++)
        if (!strcmp(action_names[i].name, name)) { action = action_names[i].action; break; }
    if (action == KEYS_ACTION_NONE || action == KEYS_ACTION_CHORD) return CONFIG_ERR_VALUE;
    if (s->binding_count == CONFIG_BINDING_CAP) return CONFIG_ERR_LIMIT;
    keys_binding candidate = {.action = action};
    if (chord(stroke, &candidate)) return CONFIG_ERR_VALUE;
    size_t count; const keys_binding *defaults = keys_defaults(&count); bool found = false;
    for (size_t i = 0; i < count; i++) if (defaults[i].action == action && !defaults[i].args.chord) {
        if (!found || !(defaults[i].args.flags & KEYS_ARG_UNIMPLEMENTED)) {
            candidate.args = defaults[i].args; found = true;
            if (!(candidate.args.flags & KEYS_ARG_UNIMPLEMENTED)) break;
        }
    }
    /* edit-yey: a command with no default stroke takes its unbound form. */
    const keys_binding *strokeless = keys_unbound(&count);
    for (size_t i = 0; !found && i < count; i++)
        if (strokeless[i].action == action) { candidate.args = strokeless[i].args; found = true; }
    if (!found) return CONFIG_ERR_VALUE; /* parameterized/chord-only intent */
    candidate.args.chord = KEYS_CHORD_NONE;
    for (size_t i = 0; i < s->binding_count; i++)
        if (candidate.keysym == s->bindings[i].keysym && candidate.modifiers == s->bindings[i].modifiers)
            return CONFIG_ERR_VALUE;
    s->bindings[s->binding_count++] = candidate; return CONFIG_OK;
}
static int setting(void *ctx, const char *path, const config_value *v)
{
    config_settings *s = ctx;
    if (!strcmp(path, "font.size")) {
        double points = v->type == CONFIG_INTEGER ? (double)v->integer : v->number;
        if ((v->type != CONFIG_INTEGER && v->type != CONFIG_FLOAT) || !isfinite(points) || points <= 0)
            return CONFIG_ERR_VALUE;
        s->font_points = points;
    } else if (!strcmp(path, "font.family") || !strcmp(path, "font.file")) {
        if (v->type != CONFIG_STRING || !*v->string || strpbrk(v->string, "\n\r\t")) return CONFIG_ERR_VALUE;
        strcpy(!strcmp(path, "font.file") ? s->font_file : s->font_family, v->string);
    } else if (!strcmp(path, "font.fallbacks")) {
        /* edit-6e2: ordered array only; entries obey font.family's rules. */
        if (v->type != CONFIG_STRINGS) return CONFIG_ERR_VALUE;
        size_t used = 0;
        for (size_t i = 0; i < v->count; i++) {
            size_t n = strlen(v->strings[i]);
            if (!n || strpbrk(v->strings[i], "\n\r\t")) return CONFIG_ERR_VALUE;
            /* (P) entry + separator/NUL never exceeds the parser's shared bound */
            if (used + n + 1u > sizeof s->font_fallbacks) return CONFIG_ERR_LIMIT;
            if (used) s->font_fallbacks[used - 1u] = '\n';
            memcpy(s->font_fallbacks + used, v->strings[i], n + 1u); used += n + 1u;
        }
        if (!v->count) s->font_fallbacks[0] = 0;
    } else if (!strcmp(path, "theme")) {
        if (v->type != CONFIG_STRING) return CONFIG_ERR_VALUE;
        if (!strcmp(v->string, "dark")) s->theme = CONFIG_DARK;
        else if (!strcmp(v->string, "light")) s->theme = CONFIG_LIGHT;
        else return CONFIG_ERR_VALUE;
    } else if (!strcmp(path, "whitespace.trailing")) {
        if (v->type == CONFIG_STRING && !strcmp(v->string, "all")) s->trailing = CONFIG_TRAILING_ALL;
        else if (v->type == CONFIG_STRING && !strcmp(v->string, "selection")) s->trailing = CONFIG_TRAILING_SELECTION;
        else {
            s->trailing = CONFIG_TRAILING_NONE;
            s->trailing_warning = v->type != CONFIG_STRING || strcmp(v->string, "none") != 0;
        }
    } else if (!strcmp(path, "wrap")) {
        if (v->type != CONFIG_BOOLEAN) return CONFIG_ERR_VALUE;
        s->wrap_set = true; s->wrap = v->boolean;
    } else if (!strcmp(path, "menu.visible")) {
        if (v->type != CONFIG_BOOLEAN) return CONFIG_ERR_VALUE;
        s->menu_visible = v->boolean;
    } else if (!strcmp(path, "tabs") || !strcmp(path, "tabs.width")) {
        /* (P) indentation's existing consumer supports stops up to
         * INDENT_MAX_WIDTH; reject rather than truncate to its byte width. */
        if (v->type != CONFIG_INTEGER || v->integer <= 0 || v->integer > INDENT_MAX_WIDTH)
            return CONFIG_ERR_VALUE;
        s->tab_width = (uint32_t)v->integer;
    } else if (!strcmp(path, "tabs.spaces")) {
        if (v->type != CONFIG_BOOLEAN) return CONFIG_ERR_VALUE;
        s->tabs_set = true; s->tab_spaces = v->boolean;
    } else if (!strncmp(path, "keymap.", strlen("keymap."))) {
        if (v->type == CONFIG_STRING) return override(s, path + strlen("keymap."), v->string);
        if (v->type != CONFIG_STRINGS || !v->count) return CONFIG_ERR_VALUE;
        for (size_t i = 0; i < v->count; i++) {
            int rc = override(s, path + strlen("keymap."), v->strings[i]); if (rc) return rc;
        }
    }
    return CONFIG_OK; /* unknown, syntactically valid settings are ignored */
}
int config_parse_worker(const uint8_t *bytes, size_t len, config_settings *out,
                        config_error *error, const work_ctx *control)
{
    if (!out) { if (error) *error = (config_error){CONFIG_ERR_ARG, 0}; return CONFIG_ERR_ARG; }
    config_settings next; config_defaults(&next);
    int rc = config_toml_parse_worker(bytes, len, setting, &next, error, control);
    if (!rc) *out = next;
    return rc;
}
int config_parse(const uint8_t *bytes, size_t len, config_settings *out, config_error *error)
{
    return config_parse_worker(bytes, len, out, error, NULL);
}
keys_result config_keys_lookup(const config_settings *s, keys_state *state,
                               uint32_t sym, uint16_t mods, const keys_binding **out)
{
    return keys_lookup_overrides(state, sym, mods, s ? s->bindings : NULL,
                                 s ? s->binding_count : 0, out);
}
int config_apply_view(const config_settings *s, view *v, struct layout *context)
{
    if (!s || !v) return VIEW_ERR_ARG;
    if (view_busy(v)) return VIEW_ERR_BUSY;
    if (s->wrap_set) {
        if (!context || layout_set_wrap(context, s->wrap) != LAYOUT_DONE) return VIEW_ERR_ARG;
        int rc = view_set_wrap(v, s->wrap, context); if (rc) return rc;
    }
    if (s->tab_width) return view_set_tab_width(v, s->tab_width);
    return VIEW_OK;
}
