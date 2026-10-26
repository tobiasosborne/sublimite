/* edit-cxo: create a generated session journal whose lease is held before
 * the journal name is visible to any other instance's discovery or
 * retirement. Startup only; never on the typing path. (P) zero is success /
 * empty; POSIX 0600 grants only the owner read/write. */
#include "journal/private.h"
#include <stdio.h>
#include <string.h>

int journal_session_create_file(const char *directory, char *path, size_t cap, os_file *lease)
{
    if (!directory || !path || !cap || !lease) return JOURNAL_INVALID;
    *lease = (os_file)OS_FILE_INIT; *path = 0;
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
        os_file fd=OS_FILE_INIT;
        if (journal_temp(&fd,lock,sizeof suffix-1u)!=OS_OK) return JOURNAL_IO;
        const char *random = lock + dir_len + sizeof prefix - 1u;
        n = snprintf(path, cap, "%s/session-%.*s", directory, (int)(strlen(random) - (sizeof suffix - 1u)), random);
        if (n < 0 || (size_t)n >= cap) { (void)os_unlink(lock); os_file_close(&fd); *path = 0; return JOURNAL_INVALID; }
        /* Only an instance that saw a same-named leftover journal can know
         * this lock name. Busy or EEXIST: keep the lock file (stable lease
         * infrastructure for that leftover) and choose another name. */
        os_err locked=os_file_try_lock(&fd,OS_LOCK_EXCLUSIVE);
        if (locked!=OS_OK) {
            os_file_close(&fd);
            if (locked == OS_E_BUSY) continue;
            *path = 0; return JOURNAL_IO;
        }
        /* Retirement removes a lock whose journal is absent, which this one
         * is until the open below. If it did so before our flock, we hold an
         * unlinked inode: pick another name rather than share this one. */
        os_stat held, named;
        if (os_file_stat(&fd,&held) || os_stat_path(lock,OS_STAT_NOFOLLOW,&named) || !journal_same_id(&held,&named)) {
            os_file_close(&fd); continue;
        }
        os_file created=OS_FILE_INIT;
        os_err opened=os_file_open(&created,path,OS_OPEN_CREATE|OS_OPEN_EXCL|OS_OPEN_RDWR|OS_OPEN_NOFOLLOW);
        if (opened == OS_OK) { os_file_close(&created); *lease = fd; return JOURNAL_OK; }
        if (opened == OS_E_EXIST) { os_file_close(&fd); continue; }
        /* No journal carries this name: the unused lock is our litter. Unlink
         * while still holding it so no instance can adopt a stale lease. */
        (void)os_unlink(lock); os_file_close(&fd); *path = 0; return JOURNAL_IO;
    }
}

/* edit-cxo: once the owner has removed its empty journal, its lease file is
 * litter. Unlink it while still holding the lease, and only if the lease is
 * the file at that name and no journal (or set-aside/retired variant, which
 * share the lease) remains. Never closes lease. */
int journal_session_release_file(const char *path, const os_file *lease)
{
    if (!path || !lease || !os_file_is_open(lease)) return JOURNAL_INVALID;
    char normalized[sizeof ((journal_base *)0)->captured_path];
    if(strlen(path)>=sizeof normalized) return JOURNAL_INVALID;
    strcpy(normalized,path); journal_slashes(normalized); path=normalized;
    const char *slash = strrchr(path, '/');
    size_t parent = slash ? (size_t)(slash - path) + 1u : 0u;
    char lock[sizeof ((journal_base *)0)->captured_path];
    int n = snprintf(lock, sizeof lock, "%.*s.%s.lock", (int)parent, path, path + parent);
    if (n < 0 || (size_t)n >= sizeof lock) return JOURNAL_INVALID;
    char directory[sizeof lock]; /* parent keeps its slash; "." if none */
    if (parent) { memcpy(directory, path, parent); directory[parent] = 0; }
    else strcpy(directory, ".");
    os_dir dfd=OS_DIR_INIT;
    if (os_dir_open(&dfd,directory)!=OS_OK) return JOURNAL_IO;
    int absent = journal_session_absent_at(&dfd,path+parent); os_dir_close(&dfd);
    if (absent) return absent;
    os_stat st, held;
    if (os_file_stat(lease,&held) || os_stat_path(lock,OS_STAT_NOFOLLOW,&st)) return JOURNAL_IO;
    if (!journal_same_id(&held,&st)) return JOURNAL_BUSY;
    return os_unlink(lock) ? JOURNAL_IO : JOURNAL_OK;
}

/* JOURNAL_OK when no entry of directory dfd is leaf or leaf.<suffix> (the
 * set-aside, retired and checkpoint names that share leaf's lease);
 * JOURNAL_BUSY when one exists; JOURNAL_IO when the scan fails. */
int journal_session_absent_at(const os_dir *dir, const char *leaf)
{
    os_dir_iter it;
    if(os_dir_iter_open(&it,dir)!=OS_OK) return JOURNAL_IO;
    size_t len=strlen(leaf); int rc=JOURNAL_OK; const char *entry;
    os_err e;
    while((e=os_dir_next(&it,&entry))==OS_OK && entry)
        if(!strncmp(entry,leaf,len) && (!entry[len] || entry[len]=='.')) { rc=JOURNAL_BUSY; break; }
    if(e!=OS_OK) rc=JOURNAL_IO;
    os_dir_iter_close(&it); return rc;
}
