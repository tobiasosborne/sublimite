/* edit-cxo: create a generated session journal whose lease is held before
 * the journal name is visible to any other instance's discovery or
 * retirement. Startup only; never on the typing path. (P) zero is success /
 * empty; POSIX 0600 grants only the owner read/write. */
#include "journal/journal.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
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
        /* Retirement removes a lock whose journal is absent, which this one
         * is until the open below. If it did so before our flock, we hold an
         * unlinked inode: pick another name rather than share this one. */
        struct stat held, named;
        if (fstat(fd, &held) || lstat(lock, &named) || held.st_dev != named.st_dev || held.st_ino != named.st_ino) {
            close(fd); continue;
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

/* edit-cxo: once the owner has removed its empty journal, its lease file is
 * litter. Unlink it while still holding the lease, and only if the lease is
 * the file at that name and no journal (or set-aside/retired variant, which
 * share the lease) remains. Never closes lease. */
int journal_session_release(const char *path, int lease)
{
    if (!path || lease < 0) return JOURNAL_INVALID;
    const char *slash = strrchr(path, '/');
    size_t parent = slash ? (size_t)(slash - path) + 1u : 0u;
    char lock[sizeof ((journal_base *)0)->captured_path];
    int n = snprintf(lock, sizeof lock, "%.*s.%s.lock", (int)parent, path, path + parent);
    if (n < 0 || (size_t)n >= sizeof lock) return JOURNAL_INVALID;
    char directory[sizeof lock]; /* parent keeps its slash; "." if none */
    if (parent) { memcpy(directory, path, parent); directory[parent] = 0; }
    else strcpy(directory, ".");
    int dfd = open(directory, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (dfd < 0) return JOURNAL_IO;
    int absent = journal_session_absent(dfd, path + parent); close(dfd);
    if (absent) return absent;
    struct stat st, held;
    if (fstat(lease, &held) || lstat(lock, &st)) return JOURNAL_IO;
    if (held.st_dev != st.st_dev || held.st_ino != st.st_ino) return JOURNAL_BUSY;
    return unlink(lock) ? JOURNAL_IO : JOURNAL_OK;
}

/* JOURNAL_OK when no entry of directory dfd is leaf or leaf.<suffix> (the
 * set-aside, retired and checkpoint names that share leaf's lease);
 * JOURNAL_BUSY when one exists; JOURNAL_IO when the scan fails. */
int journal_session_absent(int dfd, const char *leaf)
{
    int scan = openat(dfd, ".", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (scan < 0) return JOURNAL_IO;
    DIR *dir = fdopendir(scan); if (!dir) { close(scan); return JOURNAL_IO; }
    size_t len = strlen(leaf); int rc = JOURNAL_OK; struct dirent *entry;
    errno = 0;
    while ((entry = readdir(dir)))
        if (!strncmp(entry->d_name, leaf, len) && (!entry->d_name[len] || entry->d_name[len] == '.')) { rc = JOURNAL_BUSY; break; }
    if (!entry && errno) rc = JOURNAL_IO;
    closedir(dir); return rc;
}
