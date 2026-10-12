#include "config/config.h"
#include <string.h>
/* Palette data: placeholders copied from existing editor dark colors and
 * their light counterparts. Token names require reconciliation with the
 * visual spec when that dependency lands in this worktree.
 * Syntax RGB: (M)[AC] installed Sublime Mariana/Breakers scheme HSL values;
 * (P) HSL -> sRGB, nearest integer(channel * 255), the uint8 channel range.
 * Per-token source variables/selectors: docs/decisions/edit-v0o.3.md. */
static const config_token dark[] = {
    {"editor.background", 0x20242c}, {"editor.foreground", 0xd8dee9},
    {"gutter.background", 0x20242c}, {"gutter.foreground", 0x78808e},
    {"caret.background", 0xe5e9f0}, {"caret.foreground", 0x20242c},
    {"selection.background", 0x425471}, {"selection.foreground", 0xffffff},
    {"chrome.background", 0x171b22}, {"chrome.foreground", 0x8d98a9},
    {"tab.active.background", 0x384356}, {"tab.active.foreground", 0xe5e9f0},
    {"minimap.foreground", 0x8091a8}, {"warning.foreground", 0xe5b56b},
    {"bracket.background", 0x53667e}, {"error.foreground", 0xbf616a},
    {"error.background", 0x493038}, {"syntax.comment", 0xa6acb9},
    {"syntax.keyword", 0xc695c6},
    {"syntax.string", 0x99c794},
    {"syntax.number", 0xf9ae58},
    {"syntax.constant", 0xc695c6},
    {"syntax.function", 0x5fb4b4},
    {"syntax.type", 0xc695c6},
    {"syntax.variable", 0xd8dee9},
    {"syntax.operator", 0xf97b58},
    {"syntax.punctuation", 0xa6acb9},
    {"syntax.attribute", 0xc695c6}
};
static const config_token light[] = {
    {"editor.background", 0xf7f7f8}, {"editor.foreground", 0x282c34},
    {"gutter.background", 0xf7f7f8}, {"gutter.foreground", 0x777d88},
    {"caret.background", 0x282c34}, {"caret.foreground", 0xf7f7f8},
    {"selection.background", 0xc5d6ee}, {"selection.foreground", 0x20242c},
    {"chrome.background", 0xe9ebef}, {"chrome.foreground", 0x555d6a},
    {"tab.active.background", 0xf7f7f8}, {"tab.active.foreground", 0x282c34},
    {"minimap.foreground", 0x777d88}, {"warning.foreground", 0x895714},
    {"bracket.background", 0xd0deee}, {"error.foreground", 0xa0303d},
    {"error.background", 0xf2d7dc}, {"syntax.comment", 0x999999},
    {"syntax.keyword", 0xc695c6},
    {"syntax.string", 0x80b979},
    {"syntax.number", 0xf9ae58},
    {"syntax.constant", 0xc695c6},
    {"syntax.function", 0x5fb4b4},
    {"syntax.type", 0xc695c6},
    {"syntax.variable", 0x333333},
    {"syntax.operator", 0xf97b58},
    {"syntax.punctuation", 0xac7a68},
    {"syntax.attribute", 0xc695c6}
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
