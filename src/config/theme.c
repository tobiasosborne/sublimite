#include "config/config.h"
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
