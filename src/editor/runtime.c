#include "editor/runtime.h"
#include "editor/editor.h"
#include <stdlib.h>
#include <string.h>
#include "os/os.h"
#include <errno.h>

static bool real_display(const char *s)
{
    if (!s) return true;
    const char *colon = strrchr(s, ':');
    if (!colon || !colon[1]) return true;
    const char *p = colon + 1; bool nonzero = false;
    while (*p >= '0' && *p <= '9') { if (*p != '0') nonzero = true; p++; }
    return !nonzero || (*p && *p != '.');
}
const char *editor_runtime_display(const char *display, const char *safe, const char *refuse)
{
    if (!refuse || !real_display(display)) return display;
    return safe && !real_display(safe) ? safe : ":99";
}
int editor_config_dir(char *out, size_t cap)
{
    if (!out || !cap) return EDITOR_ERR_ARG;
    return os_app_dir(OS_APP_CONFIG, out, cap) == OS_OK ? EDITOR_OK : EDITOR_ERR_ARG;
}

int editor_make_directory(const char *source, int (*sync_parent)(void *, int), void *ctx)
{
    if (!source || !os_path_is_absolute(source) || strlen(source) > 4096) return EDITOR_ERR_ARG;
    char path[4097]; strcpy(path, source);
    size_t root = 1;
#if defined(_WIN32)
    for (char *p = path; *p; p++) if (*p == '\\') *p = '/';
    if (path[1] == ':') root = 3;
    else {
        /* UNC roots include both the server and share components. */
        char *server = strchr(path + 2, '/');
        char *share = server ? strchr(server + 1, '/') : NULL;
        root = share ? (size_t)(share - path) + 1u : strlen(path);
    }
    /* The callback is a Linux descriptor fault seam, never a HANDLE cast. */
    if (sync_parent) return EDITOR_ERR_ARG;
    (void)ctx;
#endif
    char saved = path[root]; path[root] = 0;
    os_dir parent = OS_DIR_INIT;
    os_err opened = os_dir_open(&parent, path); path[root] = saved;
    if (opened != OS_OK) return EDITOR_ERR_IO;
    char *part = path + root; int rc = 0;
    while (*part) {
        while (*part == '/') part++;
        if (!*part) break;
        char *end = strchr(part, '/'); if (end) *end = '\0';
        os_err made = os_mkdir_at(&parent, part);
        if (made != OS_OK && made != OS_E_EXIST) { rc = EDITOR_ERR_IO; break; }
        os_dir child = OS_DIR_INIT;
        if (os_dir_open_at(&child, &parent, part) != OS_OK) { rc = EDITOR_ERR_IO; break; }
        /* Also barrier existing components after an interrupted startup. */
        int synced;
#if !defined(_WIN32)
        if (sync_parent) {
            do { synced = sync_parent(ctx, os_dir_fd(&parent)); } while (synced < 0 && errno == EINTR);
        } else
#endif
            synced = os_dir_sync(&parent) != OS_OK;
        if (synced) { os_dir_close(&child); rc = EDITOR_ERR_IO; break; }
        os_dir_close(&parent); parent = child;
        if (!end) break;
        part = end + 1;
    }
    os_dir_close(&parent); return rc;
}
