#include "wayland/private.h"
#include "base/base.h"
#include <stdlib.h>
#include <string.h>
#ifdef EDIT_HAVE_WAYLAND
#include <wayland-client.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>

/* INIT only. Linux Unix-domain sockets store the pathname in sun_path.
 * A directory descriptor through procfs keeps a long runtime directory out
 * of that bounded field, without changing the process working directory. */
int wayland_connect(void **out)
{
    if (!out) return PLAT_ERR_FAIL;
    *out = NULL;
    if (getenv("WAYLAND_SOCKET")) {
        *out = wl_display_connect(NULL);
        return *out ? PLAT_OK : PLAT_ERR_NO_DISPLAY;
    }
    const char *name = getenv("WAYLAND_DISPLAY");
    if (!name || !*name) name = "wayland-0"; /* (P) libwayland's default socket name */
    const char *runtime = getenv("XDG_RUNTIME_DIR");
    if (*name != '/' && (!runtime || *runtime != '/')) return PLAT_ERR_NO_DISPLAY;
    struct sockaddr_un address = {.sun_family = AF_UNIX};
    int n = *name == '/' ? snprintf(address.sun_path, sizeof address.sun_path, "%s", name) :
        snprintf(address.sun_path, sizeof address.sun_path, "%s/%s", runtime, name);
    int directory = -1;
    edit_arena path = {0};
    if (n < 0) return PLAT_ERR_FAIL;
    if ((size_t)n >= sizeof address.sun_path) {
        const char *base = strrchr(name, '/');
        const char *parent = runtime;
        if (base) {
            size_t length = (size_t)(base - name);
            if (edit_arena_init(&path, length + 1)) return PLAT_ERR_FAIL; /* NUL */
            char *copy = edit_arena_alloc(&path, length + 1, _Alignof(char));
            if (!copy) { edit_arena_free(&path); return PLAT_ERR_FAIL; }
            memcpy(copy, name, length); copy[length] = '\0';
            parent = length ? copy : "/"; base++;
            /* Relative subdirectories need the runtime prefix; the regular
             * socket-name ABI uses a basename. Reject rather than truncate. */
            if (*name != '/') { edit_arena_free(&path); return PLAT_ERR_SOCKET_PATH; }
        } else base = name;
        if (strlen(base) >= sizeof address.sun_path) { edit_arena_free(&path); return PLAT_ERR_SOCKET_PATH; }
        directory = open(parent, O_PATH | O_DIRECTORY | O_CLOEXEC);
        int saved = errno;
        edit_arena_free(&path);
        if (directory < 0) return saved == ENAMETOOLONG ? PLAT_ERR_SOCKET_PATH : PLAT_ERR_NO_DISPLAY;
        n = snprintf(address.sun_path, sizeof address.sun_path, "/proc/self/fd/%d/%s", directory, base);
        if (n < 0 || (size_t)n >= sizeof address.sun_path) { close(directory); return PLAT_ERR_SOCKET_PATH; }
    }
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    int rc = PLAT_ERR_NO_DISPLAY;
    if (fd >= 0) {
        if (!connect(fd, (const struct sockaddr *)&address, sizeof address)) {
            *out = wl_display_connect_to_fd(fd);
            if (*out) rc = PLAT_OK;
        } else close(fd);
    }
    if (directory >= 0) close(directory);
    return rc;
}
#else
int wayland_connect(void **out)
{ if (out) *out = NULL; return PLAT_ERR_NO_DISPLAY; }
#endif
