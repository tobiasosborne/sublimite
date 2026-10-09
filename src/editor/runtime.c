#include "editor/runtime.h"
#include "editor/editor.h"
#include <stdlib.h>
#include <string.h>

static bool real_display(const char *s)
{
    if (!s) return true;
    const char *colon = strrchr(s, ':');
    if (!colon || !colon[1]) return true;
    const char *p = colon + 1; bool nonzero = false;
    while (*p >= '0' && *p <= '9') { if (*p != '0') nonzero = true; p++; }
    return !nonzero || (*p && *p != '.');
}
const char *editor_runtime_display(const char *display, const char *safe, const char *allow)
{
    if (allow || !real_display(display)) return display;
    return safe && !real_display(safe) ? safe : ":99";
}
int editor_config_dir(char *out, size_t cap)
{
    if (!out || !cap) return EDITOR_ERR_ARG;
    const char *root = getenv("XDG_CONFIG_HOME"), *suffix = "/sublimite";
    if (!root || root[0] != '/') {
        root = getenv("HOME"); suffix = "/.config/sublimite";
    }
    if (!root || root[0] != '/') return EDITOR_ERR_ARG;
    size_t len = strlen(root);
    while (len && root[len - 1] == '/') len--;
    if (len >= cap || strlen(suffix) >= cap - len) return EDITOR_ERR_ARG;
    memcpy(out, root, len); strcpy(out + len, suffix); return EDITOR_OK;
}
