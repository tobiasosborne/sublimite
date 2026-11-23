#ifndef IPC_SEAM_H
#define IPC_SEAM_H
/* Test-only ipc seams. The shipped ipc reads no test variable: it accepts a
 * peer only when os_stream_peer_uid reports our own uid (v2-0fv.13), names
 * its abstract endpoint <app-id>-<uid><display-suffix> and falls back to
 * /run/user/<uid> (v2-0fv.20). A test link adds the Makefile's IPC_SEAM
 * wraps and includes this header in exactly one translation unit. Forked
 * clients inherit every setting below.
 *
 * Peer identity: while ipc_seam_foreign_abstract is set, a stream whose
 * endpoint is a Linux abstract name reports uid + 1. That models another user
 * on the shared abstract namespace, in both directions: a server sees a
 * foreign client and a client sees a foreign server or squatter. Filesystem
 * endpoints and socket pairs are left alone, as the private fallback
 * directory is ours. Clear it in a child to model an own-uid peer.
 *
 * Endpoint isolation: a non-empty ipc_seam_namespace appends -<namespace> to
 * every abstract name at listen and connect, so suites, parallel runs and
 * fuzzers never meet the live editor or each other, while the name ipc
 * derived stays visible as the prefix. A non-empty ipc_seam_fallback is
 * opened in place of /run/user/<uid>. Filesystem endpoints are otherwise
 * left alone: a test isolates those through XDG_RUNTIME_DIR or an explicit
 * runtime directory, which the product reads anyway. */
#include "os/os.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

static bool ipc_seam_foreign_abstract;
static char ipc_seam_namespace[64];
static char ipc_seam_fallback[OS_PATH_MAX];

os_err __real_os_stream_peer_uid(const os_stream *s, uint32_t *uid);
os_err __wrap_os_stream_peer_uid(const os_stream *s, uint32_t *uid);
os_err __real_os_listen_local(os_listener *out, const os_dir *dir, const char *name,
                              uint32_t backlog, os_stat *bound);
os_err __wrap_os_listen_local(os_listener *out, const os_dir *dir, const char *name,
                              uint32_t backlog, os_stat *bound);
os_err __real_os_connect_local(os_stream *out, const os_dir *dir, const char *name);
os_err __wrap_os_connect_local(os_stream *out, const os_dir *dir, const char *name);
os_err __real_os_dir_open(os_dir *out, const char *path);
os_err __wrap_os_dir_open(os_dir *out, const char *path);

/* The connected peer's address (client side) or the listener's own address
 * (accepted side) names the endpoint; an abstract name starts with NUL. */
static bool ipc_seam_abstract(int fd)
{
    for (int side = 0; side < 2; side++) {
        struct sockaddr_un a;
        socklen_t n = sizeof a;
        memset(&a, 0, sizeof a);
        int rc = side ? getsockname(fd, (struct sockaddr *)&a, &n)
                      : getpeername(fd, (struct sockaddr *)&a, &n);
        if (rc == 0 && n > offsetof(struct sockaddr_un, sun_path) && a.sun_path[0] == '\0')
            return true;
    }
    return false;
}

os_err __wrap_os_stream_peer_uid(const os_stream *s, uint32_t *uid)
{
    os_err e = __real_os_stream_peer_uid(s, uid);
    if (e == OS_OK && ipc_seam_foreign_abstract && ipc_seam_abstract(os_stream_fd(s))) *uid += 1u;
    return e;
}

/* (P) sun_path holds 108 bytes; an abstract name gives one to the leading NUL,
 * so 108 bytes here hold the longest name and its terminator. */
static os_err ipc_seam_name(const os_dir *dir, const char *name, char out[108], const char **use)
{
    *use = name;
    if (dir != NULL || ipc_seam_namespace[0] == '\0') return OS_OK;
    int n = snprintf(out, 108, "%s-%s", name, ipc_seam_namespace);
    if (n < 0 || n >= 108) return OS_E_NAMETOOLONG;
    *use = out;
    return OS_OK;
}

os_err __wrap_os_listen_local(os_listener *out, const os_dir *dir, const char *name,
                              uint32_t backlog, os_stat *bound)
{
    char buf[108];
    const char *use;
    os_err e = ipc_seam_name(dir, name, buf, &use);
    return e == OS_OK ? __real_os_listen_local(out, dir, use, backlog, bound) : e;
}

os_err __wrap_os_connect_local(os_stream *out, const os_dir *dir, const char *name)
{
    char buf[108];
    const char *use;
    os_err e = ipc_seam_name(dir, name, buf, &use);
    return e == OS_OK ? __real_os_connect_local(out, dir, use) : e;
}

os_err __wrap_os_dir_open(os_dir *out, const char *path)
{
    char run[32]; /* (P) "/run/user/" + 10 digits of a 32-bit uid + NUL */
    int n = snprintf(run, sizeof run, "/run/user/%lu", (unsigned long)getuid());
    if (ipc_seam_fallback[0] != '\0' && path != NULL && n > 0 && (size_t)n < sizeof run &&
        strcmp(path, run) == 0)
        path = ipc_seam_fallback;
    return __real_os_dir_open(out, path);
}
#endif
