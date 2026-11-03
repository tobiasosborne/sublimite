#if !defined(_WIN32)
#include "config/private.h"
#include <stdlib.h>
#include <string.h>
int config_path(char *out, size_t cap)
{
    if (!out || !cap) return CONFIG_ERR_ARG;
    const char *root = getenv("XDG_CONFIG_HOME"), *suffix = "/sublimite/config.toml";
    if (!root || root[0] != '/') { root = getenv("HOME"); suffix = "/.config/sublimite/config.toml"; }
    if (!root || root[0] != '/') return CONFIG_ERR_ARG;
    size_t n = strlen(root);
    while (n && root[n - 1u] == '/') n--;
    if (n >= cap || strlen(suffix) >= cap - n) return CONFIG_ERR_LIMIT;
    memcpy(out, root, n); strcpy(out + n, suffix); return CONFIG_OK;
}
/* edit-b6ai.2: Omarchy's current palette (theme-system.md 1.1, 3.1). */
int config_palette_path(char *out, size_t cap)
{
    if (!out || !cap) return CONFIG_ERR_ARG;
    const char *root = getenv("XDG_STATE_HOME"), *suffix = "/omarchy/current/theme/colors.toml";
    if (!root || root[0] != '/') { root = getenv("HOME"); suffix = "/.local/state/omarchy/current/theme/colors.toml"; }
    if (!root || root[0] != '/') return CONFIG_ERR_ARG;
    size_t n = strlen(root);
    while (n && root[n - 1u] == '/') n--;
    if (n >= cap || strlen(suffix) >= cap - n) return CONFIG_ERR_LIMIT;
    memcpy(out, root, n); strcpy(out + n, suffix); return CONFIG_OK;
}
/* edit-b6ai.3: Omarchy's canonical monospace store (theme-system.md section 4). */
int config_fontconf_path(char *out, size_t cap)
{
    if (!out || !cap) return CONFIG_ERR_ARG;
    const char *root = getenv("XDG_CONFIG_HOME"), *suffix = "/fontconfig/fonts.conf";
    if (!root || root[0] != '/') { root = getenv("HOME"); suffix = "/.config/fontconfig/fonts.conf"; }
    if (!root || root[0] != '/') return CONFIG_ERR_ARG;
    size_t n = strlen(root);
    while (n && root[n - 1u] == '/') n--;
    if (n >= cap || strlen(suffix) >= cap - n) return CONFIG_ERR_LIMIT;
    memcpy(out, root, n); strcpy(out + n, suffix); return CONFIG_OK;
}
void *config_runtime_alloc(size_t alignment, size_t size) { return aligned_alloc(alignment, size); }
void config_runtime_free(void *p) { free(p); }
#endif
