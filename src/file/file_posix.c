/* Linux notification and save metadata; deliberately outside os.h. */
#if !defined(_WIN32)
#include "file/file_internal.h"
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <sys/inotify.h>
#include <sys/stat.h>
#include <sys/xattr.h>
#include <unistd.h>

/* src:astra edit-er2: one sanctioned process-wide inotify service. Registry
 * bound (P) = editor_open's existing maximum live + retained reservations,
 * 4096 + 4096. Setup/dispatch/close belong to the single UI owner, as do the
 * per-file check_requested fields; workers only publish CHECK_DONE. No locks
 * or allocation are needed for dispatch. The installed fd lives until exit. */
#define FILE_WATCH_SLOTS (4096u + 4096u)
typedef struct file_watch_service {
    os_once once;
    int fd, err_no;
    size_t count;
    file *files[FILE_WATCH_SLOTS];
    size_t path_count;
    file_path_watch *paths[FILE_WATCH_SLOTS];
} file_watch_service;
static file_watch_service file_watches = { .once = OS_ONCE_INIT, .fd = -1 };

static void watch_install(void)
{
    file_watches.fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    if (file_watches.fd < 0) file_watches.err_no = errno;
}

/* Linux shares a parent watch across all members. Release it only after the
 * last member leaves; refreshing a member must not enqueue IN_IGNORED for its
 * peers. Descriptors are borrowed from the service, never closed per file. */
static void watch_release(file *f, int wd)
{
    if (wd < 0) return;
    for (size_t i = 0; i < file_watches.count; i++)
        if (file_watches.files[i] != f && file_watches.files[i]->iwd == wd) return;
    for (size_t i = 0; i < file_watches.path_count; i++)
        if (file_watches.paths[i]->wd == wd) return;
    (void)inotify_rm_watch(file_watches.fd, wd);
}

void file_watch_detach(file *f)
{
    if (f->watch_slot == SIZE_MAX) return;
    watch_release(f, f->iwd);
    size_t last = --file_watches.count;
    if (f->watch_slot != last) {
        file *moved = file_watches.files[last];
        file_watches.files[f->watch_slot] = moved;
        moved->watch_slot = f->watch_slot;
    }
    file_watches.files[last] = NULL;
    f->watch_slot = SIZE_MAX; f->ifd = -1; f->iwd = -1;
}


/* Watch the parent/name, which survives target deletion and rename-over.
 * IN_IGNORED and moved/deleted parents make setup explicitly refreshable. */
static const uint32_t WATCH_MASK = IN_MODIFY | IN_ATTRIB | IN_CLOSE_WRITE |
    IN_CREATE | IN_DELETE | IN_MOVED_FROM | IN_MOVED_TO | IN_DELETE_SELF | IN_MOVE_SELF;

int file_watch_start(file *f)
{
    if (!file_prefix_ready(f)) { errno = EINVAL; return -1; }
    if (f->ifd >= 0 && f->iwd >= 0 && !f->watch_refresh) return f->ifd;
    char dir[PATH_MAX];
    file_split_path(f->path, dir, sizeof dir, f->watch_name, sizeof f->watch_name);
    if (f->watch_slot == SIZE_MAX && file_watches.count + file_watches.path_count == FILE_WATCH_SLOTS) {
        errno = ENOSPC; return -1;
    }
    os_once_run(&file_watches.once, watch_install);
    if (file_watches.fd < 0) { errno = file_watches.err_no; return -1; }
    f->ifd = file_watches.fd;
    int wd = inotify_add_watch(f->ifd, dir, WATCH_MASK | IN_ONLYDIR);
    f->watch_refresh = wd < 0;
    if (wd < 0) return -1;
    if (f->iwd != wd) watch_release(f, f->iwd);
    f->iwd = wd;
    if (f->watch_slot == SIZE_MAX) {
        f->watch_slot = file_watches.count;
        file_watches.files[file_watches.count++] = f;
    }
    return f->ifd;
}


void file_watch_dispatch(void)
{
    if (file_watches.fd >= 0) {
        char buf[4096] __attribute__((aligned(8)));
        /* One nonblocking buffer per call. Repeated EINTR and a continuous
         * alternating producer both return to the event loop. */
        ssize_t r = read(file_watches.fd, buf, sizeof buf);
        size_t off = 0, count = r > 0 ? (size_t)r : 0;
        while (count - off >= sizeof(struct inotify_event)) {
            const struct inotify_event *event = (const struct inotify_event *)(const void *)(buf + off);
            size_t length = sizeof *event + (size_t)event->len;
            if (length > count - off) break;
            for (size_t i = 0; i < file_watches.count; i++) {
                file *member = file_watches.files[i];
                if (event->mask & IN_Q_OVERFLOW) member->check_requested = 1;
                if (event->wd != member->iwd) continue;
                if (event->mask & (IN_IGNORED | IN_DELETE_SELF | IN_MOVE_SELF)) {
                    if (event->mask & IN_IGNORED) member->iwd = -1;
                    member->watch_refresh = 1; member->check_requested = 1;
                }
                if (event->len == 0 || (strnlen(event->name, event->len) < event->len &&
                    strcmp(event->name, member->watch_name) == 0)) member->check_requested = 1;
            }
            for (size_t i = 0; i < file_watches.path_count; i++) {
                file_path_watch *member = file_watches.paths[i];
                if (event->mask & IN_Q_OVERFLOW) member->pending = true;
                if (event->wd != member->wd) continue;
                if (event->mask & (IN_IGNORED | IN_DELETE_SELF | IN_MOVE_SELF)) {
                    if (event->mask & IN_IGNORED) member->wd = -1;
                    member->pending = true;
                }
                const char *name = strrchr(member->path, '/');
                if (event->len == 0 || (name && strnlen(event->name, event->len) < event->len &&
                    !strcmp(event->name, name + 1))) member->pending = true;
            }
            off += length;
        }
        /* Enqueue affected peers in this turn, even when the caller polled an
         * unchanged member. Installation still requires their own mailbox. */
        if (count) for (size_t i = 0; i < file_watches.count; i++)
            file_watch_check_pump(file_watches.files[i]);
    }
}
int file_watch_poll(file *f)
{
    file_watch_dispatch();
    file_watch_check_pump(f);
    return file_changed(f);
}

int file_watch_path_start(file_path_watch *member, const char *path)
{
    if (!member || !path || path[0] != '/' || strlen(path) >= PATH_MAX) { errno = EINVAL; return -1; }
    if (member->registered && member->path != path) { errno = EINVAL; return -1; }
    if (member->registered && member->wd >= 0) return file_watches.fd;
    if (!member->registered && file_watches.count + file_watches.path_count == FILE_WATCH_SLOTS) {
        errno = ENOSPC; return -1;
    }
    char dir[PATH_MAX], name[NAME_MAX + 1]; file_split_path(path, dir, sizeof dir, name, sizeof name);
    if (!*name || strlen(strrchr(path, '/') + 1) > NAME_MAX) { errno = EINVAL; return -1; }
    os_once_run(&file_watches.once, watch_install);
    if (file_watches.fd < 0) { errno = file_watches.err_no; return -1; }
    int wd = inotify_add_watch(file_watches.fd, dir, WATCH_MASK | IN_ONLYDIR);
    if (wd < 0) return -1;
    member->wd = wd; member->path = path;
    if (!member->registered) {
        member->slot = file_watches.path_count;
        file_watches.paths[file_watches.path_count++] = member; member->registered = true;
    }
    return file_watches.fd;
}
void file_watch_path_stop(file_path_watch *member)
{
    if (!member || !member->registered) return;
    int wd = member->wd;
    size_t last = --file_watches.path_count;
    if (member->slot != last) {
        file_path_watch *moved = file_watches.paths[last];
        file_watches.paths[member->slot] = moved; moved->slot = member->slot;
    }
    file_watches.paths[last] = NULL;
    memset(member, 0, sizeof *member); member->wd = -1;
    /* watch_release checks both kinds of remaining member. */
    watch_release(NULL, wd);
}
bool file_watch_path_take(file_path_watch *member)
{
    if (!member || !member->registered) return false;
    bool pending = member->pending; member->pending = false; return pending;
}

/* Copy all supported xattrs, including ACLs and security labels, rather than
 * guessing a namespace allowlist. Inherited attributes absent on the source
 * are removed. Unsupported xattrs on both descriptors imply no xattr state;
 * every other read/set/remove failure refuses the replacement. */
static int xattr_list(int fd, char **names, size_t *length)
{
    *names = NULL; *length = 0;
    ssize_t n = flistxattr(fd, NULL, 0);
    if (n < 0) return -1;
    if (n == 0) return 0;
    char *p = malloc((size_t)n);
    if (!p) { errno = ENOMEM; return -1; }
    ssize_t got = flistxattr(fd, p, (size_t)n);
    if (got < 0) { int en = errno; free(p); errno = en; return -1; }
    *names = p; *length = (size_t)got;
    return 0;
}
static int xattr_has(const char *list, size_t length, const char *name)
{
    size_t off = 0;
    while (off < length) {
        size_t n = strnlen(list + off, length - off);
        if (n == length - off) return 0;
        if (strcmp(list + off, name) == 0) return 1;
        off += n + 1;
    }
    return 0;
}
static int metadata_xattrs(int source, int target)
{
    char *names = NULL, *inherited = NULL;
    size_t length = 0, inherited_length = 0;
    int rc = -1, en = 0;
    if (xattr_list(source, &names, &length) != 0) {
        en = errno;
        if (en == ENOTSUP) {
            ssize_t n = flistxattr(target, NULL, 0);
            if (n < 0 && errno == ENOTSUP) return 0;
        }
        errno = en; return -1;
    }
    if (xattr_list(target, &inherited, &inherited_length) != 0) { en = errno; goto done; }
    for (size_t off = 0; off < inherited_length;) {
        const char *name = inherited + off;
        size_t n = strnlen(name, inherited_length - off);
        if (n == inherited_length - off) { en = EIO; goto done; }
        if (!xattr_has(names, length, name) && fremovexattr(target, name) != 0) { en = errno; goto done; }
        off += n + 1;
    }
    for (size_t off = 0; off < length;) {
        const char *name = names + off;
        size_t key_length = strnlen(name, length - off);
        if (key_length == length - off) { en = EIO; goto done; }
        ssize_t n = fgetxattr(source, name, NULL, 0);
        if (n < 0) { en = errno; goto done; }
        void *value = malloc(n ? (size_t)n : 1);
        if (!value) { en = ENOMEM; goto done; }
        ssize_t got = fgetxattr(source, name, value, (size_t)n);
        if (got != n) { en = got < 0 ? errno : EIO; free(value); goto done; }
        if (fsetxattr(target, name, value, (size_t)n, 0) != 0) {
            /* Some labels are immutable to this process but already match the
             * temp's inherited label. That preserves them without a write. */
            en = errno;
            void *current = malloc(n ? (size_t)n : 1);
            if (!current) { en = ENOMEM; free(value); goto done; }
            ssize_t existing = fgetxattr(target, name, current, (size_t)n);
            int equal = existing == n && memcmp(value, current, (size_t)n) == 0;
            free(current); free(value);
            if (!equal) goto done;
        } else free(value);
        off += key_length + 1;
    }
    rc = 0;
done:
    free(names); free(inherited);
    if (rc != 0) errno = en;
    return rc;
}

int file_native_error(os_err error)
{
    errno = error == OS_E_NAMETOOLONG ? ENAMETOOLONG : error == OS_E_RANGE ? EFBIG : EIO;
    return errno;
}

os_err file_pread_once(os_file *source, void *bytes, size_t n, uint64_t offset, size_t *got)
{
    ssize_t result = pread(os_file_fd(source), bytes, n, (off_t)offset);
    *got = result < 0 ? 0 : (size_t)result;
    return result >= 0 ? OS_OK : errno == EINTR ? OS_E_INTR : OS_E_IO;
}
int file_save_observer(const os_file *writer, os_file *observer)
{ return os_file_dup(observer, writer) == OS_OK ? 0 : -1; }
int file_metadata_prepare(save_transaction *tx) { (void)tx; return 0; }
int file_save_temp(save_transaction *tx)
{
    return os_file_open_at(&tx->fd, &tx->dfd, tx->tmp,
                           OS_OPEN_WRITE | OS_OPEN_CREATE | OS_OPEN_EXCL) == OS_OK ? 0 : -1;
}
int file_metadata_check(const os_file *source) { (void)source; return 0; }

int file_metadata_copy(const os_file *source, os_file *target, const file_id *identity)
{
    int src = os_file_fd(source), dst = os_file_fd(target);
    struct stat st;
    if (fstat(dst, &st) != 0) return -1;
    if (((uint32_t)st.st_uid != identity->uid || (uint32_t)st.st_gid != identity->gid) &&
        fchown(dst, (uid_t)identity->uid, (gid_t)identity->gid) != 0) return -1;
    return metadata_xattrs(src, dst);
}
#endif
