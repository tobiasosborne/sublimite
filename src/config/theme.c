#include "config/config.h"
#include <math.h>
#include <string.h>
/* edit-cxho.24 (design hill-climb round 2): every value is derived in
 * tools/mkpalette.py from OKLCH (P: Ottosson OKLab, sRGB transfer, nearest
 * integer of channel * 255); the comment on each line is its WCAG 2.2
 * contrast ratio against its background (P). One neutral tint for both
 * themes: (M) the window manager's dark title bar #222226 (edit-29y2,
 * Mint-Y-Dark wm_bg), so title bar, tab strip and page are one surface in the
 * dark theme. Ladder (rubric C2): text > comment > gutter > minimap; one
 * accent for caret, selection tint and focus; five low-chroma syntax hues at
 * one lightness. tests/config_theme_test.c pins the ladder. */
static const config_token dark[] = {
    {"editor.background", 0x222226},  /*  */
    {"editor.foreground", 0xe2e2e8},  /* 12.29:1 */
    {"gutter.background", 0x222226},  /*  */
    {"gutter.foreground", 0x6e6e73},  /* 3.13:1 */
    {"caret.background", 0x70adea},  /*  */
    {"caret.foreground", 0x222226},  /*  */
    {"selection.background", 0x39485b},  /* text on it 7.23:1 */
    {"selection.foreground", 0xe2e2e8},  /* 12.29:1 */
    {"chrome.background", 0x222226},  /*  */
    {"chrome.foreground", 0x98989d},  /* 5.52:1 */
    {"tab.active.background", 0x222226},  /*  */
    {"tab.active.foreground", 0xe2e2e8},  /* 12.29:1 */
    {"minimap.foreground", 0x444449},  /* 1.64:1 */
    {"warning.foreground", 0xe0b771},  /* 8.44:1 */
    {"bracket.background", 0x443f37},  /* text on it 8.09:1 */
    {"error.foreground", 0xd87972},  /* 5.21:1 */
    {"error.background", 0x403133},  /*  */
    {"syntax.comment", 0x9b9ba0},  /* 5.73:1 */
    {"syntax.keyword", 0xc6b9e3},  /* 8.64:1 */
    {"syntax.string", 0xa9cca9},  /* 8.98:1 */
    {"syntax.number", 0xdbbb97},  /* 8.73:1 */
    {"syntax.constant", 0xdbbb97},  /* 8.73:1 */
    {"syntax.function", 0x93cdd0},  /* 8.97:1 */
    {"syntax.type", 0xc6b9e3},  /* 8.64:1 */
    {"syntax.variable", 0xe2e2e8},  /* 12.29:1 */
    {"syntax.operator", 0xb0b0b6},  /* 7.35:1 */
    {"syntax.punctuation", 0xb0b0b6},  /* 7.35:1 */
    {"syntax.attribute", 0xc6b9e3},  /* 8.64:1 */
    {"panel.background", 0x2a2a2e},  /* text 11.08:1; chrome 4.98:1 */
    {"tab.hover.foreground", 0xbdbdc2},  /* 8.47:1 */
    {"hover.background", 0x2c2c30},  /* text 10.78:1; chrome 4.84:1 */
};
static const config_token light[] = {
    {"editor.background", 0xfbfbff},  /*  */
    {"editor.foreground", 0x1d1d20},  /* 16.29:1 */
    {"gutter.background", 0xfbfbff},  /*  */
    {"gutter.foreground", 0x9e9ea2},  /* 2.59:1 */
    {"caret.background", 0x226bc0},  /*  */
    {"caret.foreground", 0xfbfbff},  /*  */
    {"selection.background", 0xccdbf3},  /* text on it 12.01:1 */
    {"selection.foreground", 0x1d1d20},  /* 16.29:1 */
    {"chrome.background", 0xf4f4f8},  /*  */
    {"chrome.foreground", 0x636367},  /* 5.45:1 */
    {"tab.active.background", 0xfbfbff},  /*  */
    {"tab.active.foreground", 0x1d1d20},  /* 16.29:1 */
    {"minimap.foreground", 0xcdcdd2},  /* 1.53:1 */
    {"warning.foreground", 0x9d671c},  /* 4.63:1 */
    {"bracket.background", 0xe9dec9},  /* text on it 12.62:1 */
    {"error.foreground", 0xaf3c3a},  /* 5.76:1 */
    {"error.background", 0xf1d9da},  /*  */
    {"syntax.comment", 0x68686c},  /* 5.37:1 */
    {"syntax.keyword", 0x6b588d},  /* 5.97:1 */
    {"syntax.string", 0x427044},  /* 5.60:1 */
    {"syntax.number", 0x825a27},  /* 5.91:1 */
    {"syntax.constant", 0x825a27},  /* 5.91:1 */
    {"syntax.function", 0x007176},  /* 5.61:1 */
    {"syntax.type", 0x6b588d},  /* 5.97:1 */
    {"syntax.variable", 0x1d1d20},  /* 16.29:1 */
    {"syntax.operator", 0x4d4d50},  /* 8.16:1 */
    {"syntax.punctuation", 0x4d4d50},  /* 8.16:1 */
    {"syntax.attribute", 0x6b588d},  /* 5.97:1 */
    {"panel.background", 0xf4f4f8},  /* text 15.33:1; chrome 5.45:1 */
    {"tab.hover.foreground", 0x3f3f43},  /* 10.16:1 */
    {"hover.background", 0xeeeef2},  /* text 14.53:1; chrome 5.17:1 */
};
_Static_assert(sizeof dark == sizeof light, "same tokens in both palettes");
_Static_assert(sizeof dark / sizeof dark[0] == CONFIG_THEME_TOKENS, "palette slots match the token tables");
const config_token *config_theme_tokens(config_theme theme, size_t *count)
{
    if (count) *count = sizeof dark / sizeof dark[0];
    return theme == CONFIG_LIGHT ? light : dark;
}
uint32_t config_theme_color(config_theme theme, const char *name)
{
    size_t count; const config_token *tokens = config_theme_tokens(theme, &count);
    if (name) for (size_t i = 0; i < count; i++) if (!strcmp(tokens[i].name, name)) return tokens[i].rgb;
    return 0;
}

/* ---- edit-b6ai.2: resolved palettes and the Omarchy colors.toml mapping ---- */
static size_t token_index(const char *name)
{
    if (name) for (size_t i = 0; i < CONFIG_THEME_TOKENS; i++) if (!strcmp(dark[i].name, name)) return i;
    return CONFIG_THEME_TOKENS;
}
void config_palette_builtin(config_theme theme, config_palette *out)
{
    if (!out) return;
    const config_token *tokens = config_theme_tokens(theme, NULL);
    out->mode = theme == CONFIG_LIGHT ? CONFIG_LIGHT : CONFIG_DARK;
    for (size_t i = 0; i < CONFIG_THEME_TOKENS; i++) out->rgb[i] = tokens[i].rgb;
}
uint32_t config_palette_color(const config_palette *p, const char *name)
{
    size_t i = token_index(name);
    return p && i < CONFIG_THEME_TOKENS ? p->rgb[i] : 0;
}

/* Input keys: Omarchy's canonical names, then the inputs its resolver
 * aliases (legacy short names, ANSI colorN, purple). Only keys feeding a
 * token are kept; every other key reaches the visitor and is ignored. */
enum {
    K_ACCENT, K_SELECTION, K_SELECTION_BG, K_SELECTION_FG, K_MUTED,
    K_BG, K_DARK_BG, K_LIGHTER_BG, K_FG, K_DARK_FG, K_LIGHT_FG, K_BRIGHT_FG,
    K_RED, K_YELLOW, K_ORANGE, K_GREEN, K_CYAN, K_BLUE, K_MAGENTA,
    K_BRIGHT_YELLOW, K_BRIGHT_BLUE, K_BRIGHT_MAGENTA,
    K_PURPLE, K_BRIGHT_PURPLE,
    K_L_BG, K_L_DARK_BG, K_L_LIGHTER_BG, K_L_FG, K_L_DARK_FG, K_L_LIGHT_FG, K_L_BRIGHT_FG,
    K_C0, K_C1, K_C2, K_C3, K_C4, K_C5, K_C6, K_C7, K_C8, K_C11, K_C12, K_C13, K_C15,
    K_COUNT
};
static const char *const key_names[K_COUNT] = {
    "accent", "selection", "selection_background", "selection_foreground", "muted",
    "background", "dark_background", "lighter_background", "foreground", "dark_foreground",
    "light_foreground", "bright_foreground",
    "red", "yellow", "orange", "green", "cyan", "blue", "magenta",
    "bright_yellow", "bright_blue", "bright_magenta", "purple", "bright_purple",
    "bg", "dark_bg", "lighter_bg", "fg", "dark_fg", "light_fg", "bright_fg",
    "color0", "color1", "color2", "color3", "color4", "color5", "color6", "color7", "color8",
    "color11", "color12", "color13", "color15",
};
typedef struct omarchy {
    uint32_t v[K_COUNT];
    bool have[K_COUNT];
    int mode, theme_type; /* -1 = absent, else config_theme */
} omarchy;
static int hex(char ch)
{
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    return -1;
}
/* "#rrggbb" only: every stock palette key uses it (theme-system.md 2.3);
 * any other spelling of a palette key counts as absent. */
static bool parse_rgb(const char *s, uint32_t *out)
{
    if (s[0] != '#' || strlen(s) != 7u) return false;
    uint32_t v = 0;
    for (size_t i = 1; i < 7u; i++) { int d = hex(s[i]); if (d < 0) return false; v = v << 4 | (uint32_t)d; }
    *out = v; return true;
}
static int visit(void *ctx, const char *path, const config_value *v)
{
    omarchy *o = ctx;
    if (v->type != CONFIG_STRING) return CONFIG_OK;
    if (!strcmp(path, "mode") || !strcmp(path, "theme_type")) {
        int m = !strcmp(v->string, "light") ? CONFIG_LIGHT : !strcmp(v->string, "dark") ? CONFIG_DARK : -1;
        if (path[0] == 'm') o->mode = m; else o->theme_type = m;
        return CONFIG_OK;
    }
    for (size_t k = 0; k < K_COUNT; k++) if (!strcmp(path, key_names[k])) {
        o->have[k] = parse_rgb(v->string, &o->v[k]); break;
    }
    return CONFIG_OK;
}
static void alias(omarchy *o, size_t key, size_t from)
{
    if (!o->have[key] && o->have[from]) { o->v[key] = o->v[from]; o->have[key] = true; }
}
/* omarchy-theme-color mix_color: channel int(a*(1-t) + b*t + 0.5). */
static uint32_t mix(uint32_t a, uint32_t b, unsigned percent)
{
    uint32_t out = 0;
    for (unsigned shift = 0; shift <= 16u; shift += 8u) {
        /* Same double arithmetic as the awk, so ties round identically. */
        double x = (double)(a >> shift & 0xffu), y = (double)(b >> shift & 0xffu), t = percent / 100.0;
        out |= (uint32_t)(x * (1.0 - t) + y * t + 0.5) << shift;
    }
    return out;
}
static void derive(omarchy *o, size_t key, size_t from, uint32_t other, unsigned percent)
{
    if (!o->have[key] && o->have[from]) { o->v[key] = mix(o->v[from], other, percent); o->have[key] = true; }
}
/* WCAG 2.2 relative luminance and contrast ratio (P). */
static double linear(uint32_t channel)
{
    double c = (double)channel / 255.0;
    return c <= 0.04045 ? c / 12.92 : pow((c + 0.055) / 1.055, 2.4);
}
static double luminance(uint32_t rgb)
{
    return 0.2126 * linear(rgb >> 16 & 0xffu) + 0.7152 * linear(rgb >> 8 & 0xffu) + 0.0722 * linear(rgb & 0xffu);
}
static double contrast(uint32_t a, uint32_t b)
{
    double x = luminance(a), y = luminance(b);
    return x > y ? (x + 0.05) / (y + 0.05) : (y + 0.05) / (x + 0.05);
}
static void put(config_palette *p, const char *token, const omarchy *o, size_t key)
{
    if (o->have[key]) p->rgb[token_index(token)] = o->v[key];
}
static void put_rgb(config_palette *p, const char *token, uint32_t rgb) { p->rgb[token_index(token)] = rgb; }
/* The ladder rule (decision edit-b6ai.2): Omarchy's muted/dark_foreground
 * are often too faint for comments, gutter numbers, minimap ink and chrome
 * text. Take the first whole percent of a mix toward foreground at which
 * the token reaches the contrast the built-in theme of the same mode has
 * for that token against the same surface; foreground itself if none does. */
static void ladder(config_palette *p, const char *token, uint32_t source, const char *against)
{
    const config_token *builtin = config_theme_tokens(p->mode, NULL);
    double target = contrast(builtin[token_index(token)].rgb, builtin[token_index(against)].rgb);
    uint32_t fg = config_palette_color(p, "editor.foreground"), bg = config_palette_color(p, against), out = source;
    for (unsigned t = 0; t <= 100u; t++) {
        out = mix(source, fg, t);
        if (contrast(out, bg) >= target) break;
    }
    put_rgb(p, token, out);
}
int config_palette_parse(const uint8_t *bytes, size_t len, bool light_marker, config_theme fallback,
                         config_palette *out, config_error *error)
{
    config_error local; if (!error) error = &local;
    *error = (config_error){0};
    if (!out) { error->code = CONFIG_ERR_ARG; return CONFIG_ERR_ARG; }
    omarchy o; memset(&o, 0, sizeof o); o.mode = o.theme_type = -1;
    int rc = bytes || !len ? config_toml_parse(bytes ? bytes : (const uint8_t *)"", len, visit, &o, error) : CONFIG_ERR_ARG;
    /* Legacy short names, then ANSI names (resolve_theme_colors order). */
    static const uint8_t legacy[][2] = {
        {K_BG, K_L_BG}, {K_DARK_BG, K_L_DARK_BG}, {K_LIGHTER_BG, K_L_LIGHTER_BG}, {K_FG, K_L_FG},
        {K_DARK_FG, K_L_DARK_FG}, {K_LIGHT_FG, K_L_LIGHT_FG}, {K_BRIGHT_FG, K_L_BRIGHT_FG},
        {K_BG, K_C0}, {K_FG, K_C7},
        {K_RED, K_C1}, {K_GREEN, K_C2}, {K_YELLOW, K_C3}, {K_BLUE, K_C4}, {K_MAGENTA, K_C5}, {K_CYAN, K_C6},
        {K_BRIGHT_YELLOW, K_C11}, {K_BRIGHT_BLUE, K_C12}, {K_BRIGHT_MAGENTA, K_C13},
        {K_MAGENTA, K_PURPLE}, {K_BRIGHT_MAGENTA, K_BRIGHT_PURPLE},
        /* color7 is foreground once foreground exists, so these reach it. */
        {K_LIGHT_FG, K_FG}, {K_BRIGHT_FG, K_C15}, {K_BRIGHT_FG, K_FG}, {K_LIGHTER_BG, K_BG},
        {K_DARK_FG, K_C8}, {K_DARK_FG, K_FG}, {K_MUTED, K_C8}, {K_MUTED, K_DARK_FG},
        {K_SELECTION, K_SELECTION_BG}, {K_SELECTION, K_C8}, {K_SELECTION, K_BG},
        {K_SELECTION_FG, K_BRIGHT_FG}, {K_ORANGE, K_YELLOW},
    };
    for (size_t i = 0; i < sizeof legacy / sizeof legacy[0]; i++) alias(&o, legacy[i][0], legacy[i][1]);
    derive(&o, K_DARK_BG, K_BG, 0x000000, 25u);
    derive(&o, K_BRIGHT_YELLOW, K_YELLOW, 0xffffff, 20u);
    derive(&o, K_BRIGHT_BLUE, K_BLUE, 0xffffff, 20u);
    derive(&o, K_BRIGHT_MAGENTA, K_MAGENTA, 0xffffff, 20u);
    /* Mode: mode, theme_type, light.mode marker, luminance > 382 (P: the
     * resolver's own constant on R+G+B of background), then the caller's. */
    config_theme mode = fallback == CONFIG_LIGHT ? CONFIG_LIGHT : CONFIG_DARK;
    if (o.mode >= 0) mode = (config_theme)o.mode;
    else if (o.theme_type >= 0) mode = (config_theme)o.theme_type;
    else if (light_marker) mode = CONFIG_LIGHT;
    else if (o.have[K_BG])
        mode = (o.v[K_BG] >> 16 & 0xffu) + (o.v[K_BG] >> 8 & 0xffu) + (o.v[K_BG] & 0xffu) > 382u ? CONFIG_LIGHT : CONFIG_DARK;
    config_palette p; config_palette_builtin(mode, &p);
    if (!rc && (!o.have[K_BG] || !o.have[K_FG])) { rc = CONFIG_ERR_VALUE; *error = (config_error){rc, len}; }
    if (rc) { *out = p; return rc; }
    uint32_t bg = o.v[K_BG], fg = o.v[K_FG];
    put(&p, "editor.background", &o, K_BG); put(&p, "editor.foreground", &o, K_FG);
    put(&p, "gutter.background", &o, K_BG); put(&p, "chrome.background", &o, K_BG);
    put(&p, "tab.active.background", &o, K_BG); put(&p, "tab.active.foreground", &o, K_FG);
    put(&p, "tab.hover.foreground", &o, K_FG);
    put(&p, "caret.background", &o, K_ACCENT); put(&p, "caret.foreground", &o, K_BG);
    put(&p, "selection.background", &o, K_SELECTION); put(&p, "selection.foreground", &o, K_SELECTION_FG);
    put(&p, "warning.foreground", &o, K_YELLOW); put(&p, "error.foreground", &o, K_RED);
    if (o.have[K_RED]) put_rgb(&p, "error.background", mix(bg, o.v[K_RED], 15u));
    if (o.have[K_ACCENT]) put_rgb(&p, "bracket.background", mix(bg, o.v[K_ACCENT], 20u));
    put(&p, "panel.background", &o, mode == CONFIG_LIGHT ? K_DARK_BG : K_LIGHTER_BG);
    put_rgb(&p, "hover.background", mix(bg, fg, 6u));
    put(&p, "syntax.keyword", &o, K_BRIGHT_MAGENTA); put(&p, "syntax.string", &o, K_GREEN);
    put(&p, "syntax.number", &o, K_ORANGE); put(&p, "syntax.constant", &o, K_BRIGHT_YELLOW);
    put(&p, "syntax.function", &o, K_BLUE); put(&p, "syntax.type", &o, K_YELLOW);
    put(&p, "syntax.variable", &o, K_FG); put(&p, "syntax.operator", &o, K_BRIGHT_BLUE);
    put(&p, "syntax.punctuation", &o, K_LIGHT_FG); put(&p, "syntax.attribute", &o, K_CYAN);
    ladder(&p, "syntax.comment", o.v[K_MUTED], "editor.background");
    ladder(&p, "gutter.foreground", o.v[K_MUTED], "gutter.background");
    ladder(&p, "minimap.foreground", o.v[K_MUTED], "editor.background");
    ladder(&p, "chrome.foreground", o.v[K_DARK_FG], "chrome.background");
    *out = p; return CONFIG_OK;
}
