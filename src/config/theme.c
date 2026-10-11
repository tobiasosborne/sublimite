#include "config/config.h"
#include <string.h>
/* Palette data: placeholders copied from existing editor dark colors and
 * their light counterparts. Token names require reconciliation with the
 * visual spec when that dependency lands in this worktree. */
static const config_token dark[] = {
    {"editor.background", 0x20242c}, {"editor.foreground", 0xd8dee9},
    {"gutter.background", 0x20242c}, {"gutter.foreground", 0x78808e},
    {"caret.background", 0xe5e9f0}, {"caret.foreground", 0x20242c},
    {"selection.background", 0x425471}, {"selection.foreground", 0xffffff},
    {"chrome.background", 0x171b22}, {"chrome.foreground", 0x8d98a9},
    {"tab.active.background", 0x384356}, {"tab.active.foreground", 0xe5e9f0},
    {"minimap.foreground", 0x8091a8}, {"warning.foreground", 0xe5b56b},
    {"bracket.background", 0x53667e}, {"error.foreground", 0xbf616a},
    {"error.background", 0x493038}, {"syntax.comment", 0x78808e},
    {"syntax.keyword", 0x81a1c1}, {"syntax.string", 0xa3be8c},
    {"syntax.number", 0xb48ead}, {"syntax.type", 0x8fbcbb}
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
    {"error.background", 0xf2d7dc}, {"syntax.comment", 0x777d88},
    {"syntax.keyword", 0x325ea0}, {"syntax.string", 0x426b28},
    {"syntax.number", 0x875090}, {"syntax.type", 0x287576}
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
