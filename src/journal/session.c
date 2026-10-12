/* edit-cxo: create a generated session journal whose lease is held before
 * the journal name is visible to any other instance's discovery or
 * retirement. Startup only; never on the typing path. (P) zero is success /
 * empty; POSIX 0600 grants only the owner read/write. */
#include "journal/journal.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <unistd.h>

int journal_session_create(const char *directory, char *path, size_t cap, int *lease)
{
    if (!directory || !path || !cap || !lease) return JOURNAL_INVALID;
    *lease = -1; *path = 0;
    static const char prefix[] = "/.session-", suffix[] = ".lock";
    /* Same path schema capacity as journal_base and retirement. */
    char lock[sizeof ((journal_base *)0)->captured_path];
    size_t dir_len = strlen(directory);
    /* Every iteration consumes a fresh, permanently kept lock name; only a
     * leftover session-XXXXXX journal without its lock file repeats it. Their
     * count bounds the loop. */
    for (;;) {
        int n = snprintf(lock, sizeof lock, "%s%sXXXXXX%s", directory, prefix, suffix);
        if (n < 0 || (size_t)n >= sizeof lock) return JOURNAL_INVALID;
        /* The lock name is the uniqueness carrier; nobody enumerates locks. */
        int fd = mkostemps(lock, (int)(sizeof suffix - 1u), O_CLOEXEC);
        if (fd < 0) return JOURNAL_IO;
        const char *random = lock + dir_len + sizeof prefix - 1u;
        n = snprintf(path, cap, "%s/session-%.*s", directory, (int)(strlen(random) - (sizeof suffix - 1u)), random);
        if (n < 0 || (size_t)n >= cap) { (void)unlink(lock); close(fd); *path = 0; return JOURNAL_INVALID; }
        /* Only an instance that saw a same-named leftover journal can know
         * this lock name. Busy or EEXIST: keep the lock file (stable lease
         * infrastructure for that leftover) and choose another name. */
        if (flock(fd, LOCK_EX | LOCK_NB)) {
            int err = errno; close(fd);
            if (err == EWOULDBLOCK) continue;
            *path = 0; return JOURNAL_IO;
        }
        int created = open(path, O_CREAT | O_EXCL | O_RDWR | O_CLOEXEC | O_NOFOLLOW, 0600);
        if (created >= 0) { close(created); *lease = fd; return JOURNAL_OK; }
        int err = errno;
        if (err == EEXIST) { close(fd); continue; }
        /* No journal carries this name: the unused lock is our litter. Unlink
         * while still holding it so no instance can adopt a stale lease. */
        (void)unlink(lock); close(fd); *path = 0; return JOURNAL_IO;
    }
}
