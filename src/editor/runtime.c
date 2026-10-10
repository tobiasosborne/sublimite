#include "editor/runtime.h"
#include "editor/editor.h"
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>

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

int editor_make_directory(const char *source, int (*sync_parent)(void *, int), void *ctx)
{
    if (!source || source[0] != '/' || strlen(source) > 4096) return EDITOR_ERR_ARG;
    char path[4097]; strcpy(path, source);
    int parent = open("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC); if (parent < 0) return EDITOR_ERR_IO;
    char *part = path + 1; int rc = 0;
    while (*part) {
        while (*part == '/') part++;
        if (!*part) break;
        char *end = strchr(part, '/'); if (end) *end = '\0';
        if (mkdirat(parent, part, 0700) && errno != EEXIST) { rc = EDITOR_ERR_IO; break; }
        int child = openat(parent, part, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
        if (child < 0) { rc = EDITOR_ERR_IO; break; }
        /* Also barrier existing components: a preceding failed startup may
         * have created their names without completing this same barrier. */
        int synced;
        do { synced = sync_parent ? sync_parent(ctx, parent) : fsync(parent); } while (synced < 0 && errno == EINTR);
        if (synced) { close(child); rc = EDITOR_ERR_IO; break; }
        close(parent); parent = child;
        if (!end) break;
        part = end + 1;
    }
    close(parent); return rc;
}
