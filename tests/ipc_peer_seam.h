#ifndef IPC_PEER_SEAM_H
#define IPC_PEER_SEAM_H
/* Test-only peer-identity seam (v2-0fv.13). The shipped ipc accepts a peer
 * only when os_stream_peer_uid reports our own uid, and it has no override.
 * A test link adds -Wl,--wrap=os_stream_peer_uid (Makefile IPC_PEER_SEAM) and
 * includes this header in exactly one translation unit. While
 * ipc_seam_foreign_abstract is set, a stream whose endpoint is a Linux
 * abstract name reports uid + 1. That models another user on the shared
 * abstract namespace, in both directions: a server sees a foreign client and
 * a client sees a foreign server or squatter. Filesystem endpoints and
 * socket pairs are left alone, as the private fallback directory is ours. A
 * forked child inherits the flag; clear it there to model an own-uid peer. */
#include "os/os.h"

#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>

static bool ipc_seam_foreign_abstract;

os_err __real_os_stream_peer_uid(const os_stream *s, uint32_t *uid);
os_err __wrap_os_stream_peer_uid(const os_stream *s, uint32_t *uid);

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
#endif
