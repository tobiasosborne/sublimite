#include "os/os.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <linux/fs.h>
#include <poll.h>
#include <sched.h>
#include <signal.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/eventfd.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/random.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#ifndef EDIT_APP_ID
#error "EDIT_APP_ID must be defined by the build (Makefile APP_ID)"
#endif

_Static_assert(sizeof(off_t) == sizeof(int64_t), "file offsets require a 64-bit off_t");
_Static_assert(OS_PATH_MAX == PATH_MAX && OS_NAME_MAX == NAME_MAX + 1u,
               "public capacities must match Linux path and name limits");

extern char **environ;

/* Restarts a call that returns -1 with errno EINTR (os.h retry policy). */
#define RETRY(var, call) do { (var) = (call); } while ((var) == -1 && errno == EINTR)

/* (P) SI conversions for timespec and poll. */
#define NS_PER_SEC INT64_C(1000000000)
#define NS_PER_MS INT64_C(1000000)

/* ---- errors ------------------------------------------------------------- */
static os_err map_errno(int e)
{
    switch (e) {
    case ENOENT: return OS_E_NOENT;
    case EEXIST: case ENOTEMPTY: return OS_E_EXIST;
    case EACCES: case EPERM: case EROFS: return OS_E_ACCESS;
    case EBUSY: case ETXTBSY: return OS_E_BUSY;
    case ELOOP: return OS_E_LOOP;
    case ENOTDIR: return OS_E_NOTDIR;
    case EISDIR: return OS_E_ISDIR;
    case EXDEV: return OS_E_XDEV;
    case ENOSPC: case EDQUOT: return OS_E_NOSPC;
    case ENAMETOOLONG: return OS_E_NAMETOOLONG;
    case EINVAL: return OS_E_INVAL;
    case EOPNOTSUPP: case ENOSYS: case ENOTTY: return OS_E_NOTSUP;
    case EAGAIN: return OS_E_AGAIN;
    case ETIMEDOUT: return OS_E_TIMEDOUT;
    case ENOMEM: return OS_E_NOMEM;
    case ERANGE: return OS_E_RANGE;
    case EINTR: return OS_E_INTR;
    default: return OS_E_IO;
    }
}
static os_err fail(void) { return map_errno(errno); }
/* An error the layer itself detects: no native code. */
static os_err fail_with(os_err e) { errno = 0; return e; }

const char *os_err_name(os_err e)
{
    /* A char matrix keeps the names in .rodata without pointer relocations. */
    static const char names[][sizeof "OS_E_NAMETOOLONG"] = {
        "OS_OK", "OS_E_NOENT", "OS_E_EXIST", "OS_E_ACCESS", "OS_E_BUSY", "OS_E_LOOP",
        "OS_E_NOTDIR", "OS_E_ISDIR", "OS_E_XDEV", "OS_E_NOSPC", "OS_E_NAMETOOLONG",
        "OS_E_INVAL", "OS_E_NOTSUP", "OS_E_AGAIN", "OS_E_TIMEDOUT", "OS_E_NOMEM",
        "OS_E_RANGE", "OS_E_IO", "OS_E_INTR"};
    unsigned i = (unsigned)e;
    return i < sizeof names / sizeof names[0] ? names[i] : "OS_E_?";
}
int os_native_error(void) { return errno; }

/* Copies a NUL-terminated string into a caller buffer, or OS_E_RANGE. */
static os_err copy_out(char *out, size_t cap, const char *s, size_t len)
{
    if (!out || len >= cap) return fail_with(OS_E_RANGE);
    memcpy(out, s, len);
    out[len] = 0;
    return OS_OK;
}

/* A single name component for *_at calls (os.h: never a path). */
static bool bad_name(const char *name) { return !name || !name[0] || strchr(name, '/'); }

const char *os_app_id(void) { return EDIT_APP_ID; }

/* ---- clocks, sleep ------------------------------------------------------ */
static uint64_t clock_ns(clockid_t id)
{
    struct timespec ts;
    if (clock_gettime(id, &ts) != 0 || ts.tv_sec < 0) return 0;
    return (uint64_t)ts.tv_sec * NS_PER_SEC + (uint64_t)ts.tv_nsec;
}
uint64_t os_now_ns(void) { return clock_ns(CLOCK_MONOTONIC); }
uint64_t os_thread_cpu_ns(void) { return clock_ns(CLOCK_THREAD_CPUTIME_ID); }
uint64_t os_thread_cpu_resolution_ns(void)
{
    struct timespec r;
    if (clock_getres(CLOCK_THREAD_CPUTIME_ID, &r) != 0 || r.tv_sec < 0) return 1;
    uint64_t ns = (uint64_t)r.tv_sec * NS_PER_SEC + (uint64_t)r.tv_nsec;
    return ns ? ns : 1;
}
uint64_t os_slice_ns(void) { return os_thread_cpu_ns(); }
uint64_t os_wall_ns(void) { return clock_ns(CLOCK_REALTIME); }
void os_sleep_ns(uint64_t ns)
{
    /* nanosleep writes the time left on EINTR: restart with it. */
    struct timespec t = {(time_t)(ns / NS_PER_SEC), (long)(ns % NS_PER_SEC)};
    while (nanosleep(&t, &t) != 0 && errno == EINTR) {}
}
void os_yield(void) { (void)sched_yield(); }

/* ---- threads, mutex, condition, once ----------------------------------- */
static void *thread_main(void *arg)
{
    os_thread *t = arg;
    t->fn(t->arg);
    return NULL;
}
os_err os_thread_start(os_thread *t, void (*fn)(void *), void *arg)
{
    if (!t || !fn) return fail_with(OS_E_INVAL);
    t->fn = fn;
    t->arg = arg;
    int rc = pthread_create(&t->thread, NULL, thread_main, t);
    if (rc) { errno = rc; return fail(); }
    return OS_OK;
}
os_err os_thread_join(os_thread *t)
{
    int rc = pthread_join(t->thread, NULL);
    if (rc) { errno = rc; return fail(); }
    return OS_OK;
}
uint64_t os_thread_self(void) { return (uint64_t)(uintptr_t)pthread_self(); }

os_err os_mutex_init(os_mutex *m)
{
    int rc = pthread_mutex_init(&m->m, NULL);
    if (rc) { errno = rc; return fail(); }
    return OS_OK;
}
void os_mutex_destroy(os_mutex *m) { (void)pthread_mutex_destroy(&m->m); }
void os_mutex_lock(os_mutex *m) { (void)pthread_mutex_lock(&m->m); }
void os_mutex_unlock(os_mutex *m) { (void)pthread_mutex_unlock(&m->m); }

os_err os_rmutex_init(os_rmutex *m)
{
    pthread_mutexattr_t attr;
    int rc = pthread_mutexattr_init(&attr);
    if (rc) { errno = rc; return fail(); }
    rc = pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    if (!rc) rc = pthread_mutex_init(&m->m, &attr);
    (void)pthread_mutexattr_destroy(&attr);
    if (rc) { errno = rc; return fail(); }
    return OS_OK;
}
void os_rmutex_destroy(os_rmutex *m) { (void)pthread_mutex_destroy(&m->m); }
void os_rmutex_lock(os_rmutex *m) { (void)pthread_mutex_lock(&m->m); }
void os_rmutex_unlock(os_rmutex *m) { (void)pthread_mutex_unlock(&m->m); }

os_err os_cond_init(os_cond *c)
{
    int rc = pthread_cond_init(&c->c, NULL);
    if (rc) { errno = rc; return fail(); }
    return OS_OK;
}
void os_cond_destroy(os_cond *c) { (void)pthread_cond_destroy(&c->c); }
void os_cond_wait(os_cond *c, os_mutex *m) { (void)pthread_cond_wait(&c->c, &m->m); }
void os_cond_signal(os_cond *c) { (void)pthread_cond_signal(&c->c); }
void os_cond_broadcast(os_cond *c) { (void)pthread_cond_broadcast(&c->c); }

void os_once_run(os_once *o, void (*fn)(void)) { (void)pthread_once(&o->once, fn); }

/* ---- wake handles ------------------------------------------------------- */
os_err os_wake_init(os_wake *w)
{
    w->fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    return w->fd < 0 ? fail() : OS_OK;
}
void os_wake_close(os_wake *w)
{
    if (w->fd >= 0) { int e = errno; (void)close(w->fd); errno = e; }
    w->fd = -1;
}
void os_wake_signal(const os_wake *w)
{
    uint64_t one = 1;
    ssize_t r;
    int saved = errno;
    RETRY(r, write(w->fd, &one, sizeof one));
    /* EAGAIN: the counter is saturated, i.e. already signalled. */
    (void)r;
    errno = saved;
}
void os_wake_drain(const os_wake *w)
{
    uint64_t v;
    ssize_t r;
    int saved = errno;
    RETRY(r, read(w->fd, &v, sizeof v));
    (void)r;
    errno = saved;
}
os_err os_wake_wait(const os_wake *w, int64_t timeout_ns)
{
    if (w->fd < 0) return fail_with(OS_E_INVAL);
    uint64_t start = timeout_ns > 0 ? os_now_ns() : 0;
    for (;;) {
        int ms = -1;
        if (timeout_ns == 0) ms = 0;
        else if (timeout_ns > 0) {
            uint64_t elapsed = os_now_ns() - start;
            uint64_t timeout = (uint64_t)timeout_ns;
            uint64_t left = timeout > elapsed ? timeout - elapsed : 0;
            uint64_t up = (left + NS_PER_MS - 1u) / NS_PER_MS;
            ms = up > (uint64_t)INT_MAX ? INT_MAX : (int)up;
        }
        struct pollfd p = {w->fd, POLLIN, 0};
        int n = poll(&p, 1, ms);
        if (n > 0) {
            if (p.revents & POLLIN) return OS_OK;
            return fail_with(OS_E_IO);
        }
        if (n < 0 && errno != EINTR) return fail();
        if (n == 0 && (timeout_ns == 0 ||
                       (timeout_ns > 0 && os_now_ns() - start >= (uint64_t)timeout_ns)))
            return fail_with(OS_E_TIMEDOUT);
        /* EINTR, or a short poll: go round with the time left. */
    }
}
int os_wake_fd(const os_wake *w) { return w->fd; }

/* ---- file metadata ------------------------------------------------------ */
bool os_identity_equal(const os_identity *a, const os_identity *b)
{
    return a->volume == b->volume && a->index[0] == b->index[0] && a->index[1] == b->index[1];
}
/* Signed ns since the epoch, saturating outside +-292 years (os.h). */
static int64_t ts_ns(struct timespec t)
{
    const int64_t max_sec = INT64_MAX / NS_PER_SEC;
    /* The first representable second is the floor, not C's truncation
     * toward zero. Compute its ns offset without multiplying that second. */
    const int64_t min_sec = INT64_MIN / NS_PER_SEC - 1;
    const int64_t min_nsec = NS_PER_SEC + INT64_MIN % NS_PER_SEC;
    if (t.tv_sec > max_sec || (t.tv_sec == max_sec && t.tv_nsec > INT64_MAX % NS_PER_SEC))
        return INT64_MAX;
    if (t.tv_sec < min_sec) return INT64_MIN;
    if (t.tv_sec == min_sec)
        return t.tv_nsec <= min_nsec ? INT64_MIN : INT64_MIN + (t.tv_nsec - min_nsec);
    return (int64_t)t.tv_sec * NS_PER_SEC + (int64_t)t.tv_nsec;
}
static void from_stat(os_stat *o, const struct stat *s)
{
    memset(o, 0, sizeof *o);
    o->id.volume = (uint64_t)s->st_dev;
    o->id.index[0] = (uint64_t)s->st_ino;
    o->kind = S_ISREG(s->st_mode) ? OS_KIND_FILE : S_ISDIR(s->st_mode) ? OS_KIND_DIR
            : S_ISLNK(s->st_mode) ? OS_KIND_LINK : OS_KIND_OTHER;
    o->size = s->st_size < 0 ? 0 : (uint64_t)s->st_size;
    o->mtime_ns = ts_ns(s->st_mtim);
    o->ctime_ns = ts_ns(s->st_ctim);
    o->perm = (uint32_t)(s->st_mode & 07777);
    o->uid = (uint32_t)s->st_uid;
    o->gid = (uint32_t)s->st_gid;
}

/* ---- file handles ------------------------------------------------------- */
bool os_file_is_open(const os_file *f) { return f && f->fd >= 0; }

static int open_flags(unsigned flags, bool *ok)
{
    unsigned mode = flags & (OS_OPEN_READ | OS_OPEN_WRITE | OS_OPEN_RDWR);
    *ok = mode == OS_OPEN_READ || mode == OS_OPEN_WRITE || mode == OS_OPEN_RDWR;
    /* TRUNC needs write access (POSIX leaves O_RDONLY|O_TRUNC undefined). */
    if ((flags & OS_OPEN_TRUNC) && mode == OS_OPEN_READ) *ok = false;
    int o = O_CLOEXEC | (mode == OS_OPEN_RDWR ? O_RDWR : mode == OS_OPEN_WRITE ? O_WRONLY : O_RDONLY);
    if (flags & OS_OPEN_CREATE) o |= O_CREAT;
    if (flags & OS_OPEN_EXCL) o |= O_EXCL;
    if (flags & OS_OPEN_NOFOLLOW) o |= O_NOFOLLOW;
    if (flags & OS_OPEN_NONBLOCK) o |= O_NONBLOCK;
    if (flags & OS_OPEN_TRUNC) o |= O_TRUNC;
    return o;
}
/* A read-only open of a directory succeeds on Linux; os.h says ISDIR. */
static os_err file_open_at(os_file *out, int dirfd, const char *path, unsigned flags)
{
    out->fd = -1;
    bool ok;
    int open_mode = open_flags(flags, &ok);
    if (!ok || !path) return fail_with(OS_E_INVAL);
    int fd;
    RETRY(fd, openat(dirfd, path, open_mode, 0600));
    if (fd < 0) return fail();
    /* After exclusive creation we already own a new regular file. Avoid
     * a fallible check that could leave its name behind after an error. */
    const unsigned exclusive = OS_OPEN_CREATE | OS_OPEN_EXCL;
    if ((flags & exclusive) != exclusive) {
        struct stat st;
        int rc;
        RETRY(rc, fstat(fd, &st));
        if (rc != 0 || S_ISDIR(st.st_mode)) {
            int error = rc != 0 ? errno : EISDIR;
            (void)close(fd);
            errno = error;
            return fail();
        }
    }
    out->fd = fd;
    return OS_OK;
}
os_err os_file_open(os_file *out, const char *path, unsigned flags)
{
    return file_open_at(out, AT_FDCWD, path, flags);
}
os_err os_file_open_at(os_file *out, const os_dir *dir, const char *name, unsigned flags)
{
    if (bad_name(name)) {
        out->fd = -1;
        return fail_with(OS_E_INVAL);
    }
    return file_open_at(out, dir->fd, name, flags);
}
os_err os_file_open_anon(os_file *out, const os_dir *dir)
{
    int fd;
    RETRY(fd, openat(dir->fd, ".", O_TMPFILE | O_RDWR | O_CLOEXEC, 0600));
    out->fd = fd;
    if (fd >= 0) return OS_OK;
    /* Kernels and filesystems without O_TMPFILE answer EISDIR, EINVAL or EOPNOTSUPP. */
    if (errno == EISDIR || errno == EINVAL || errno == EOPNOTSUPP) return OS_E_NOTSUP;
    return fail();
}
void os_file_close(os_file *f)
{
    /* Never retried: Linux releases the descriptor even on EINTR. */
    if (f && f->fd >= 0) { int e = errno; (void)close(f->fd); errno = e; }
    if (f) f->fd = -1;
}
os_err os_file_dup(os_file *out, const os_file *f)
{
    int fd;
    RETRY(fd, fcntl(f->fd, F_DUPFD_CLOEXEC, 0));
    out->fd = fd;
    return fd < 0 ? fail() : OS_OK;
}

os_err os_file_read(os_file *f, void *buf, size_t n, size_t *got)
{
    ssize_t r;
    *got = 0;
    RETRY(r, read(f->fd, buf, n));
    if (r < 0) return fail();
    *got = (size_t)r;
    return OS_OK;
}
os_err os_file_write(os_file *f, const void *buf, size_t n, size_t *done)
{
    ssize_t r;
    *done = 0;
    RETRY(r, write(f->fd, buf, n));
    if (r < 0) return fail();
    *done = (size_t)r;
    return OS_OK;
}
os_err os_file_pread(os_file *f, void *buf, size_t n, uint64_t offset, size_t *got)
{
    ssize_t r;
    *got = 0;
    if (offset > (uint64_t)INT64_MAX) return fail_with(OS_E_INVAL);
    RETRY(r, pread(f->fd, buf, n, (off_t)offset));
    if (r < 0) return fail();
    *got = (size_t)r;
    return OS_OK;
}
os_err os_file_pwrite(os_file *f, const void *buf, size_t n, uint64_t offset, size_t *done)
{
    ssize_t r;
    *done = 0;
    if (offset > (uint64_t)INT64_MAX) return fail_with(OS_E_INVAL);
    RETRY(r, pwrite(f->fd, buf, n, (off_t)offset));
    if (r < 0) return fail();
    *done = (size_t)r;
    return OS_OK;
}
os_err os_file_pwrite_once(os_file *f, const void *buf, size_t n, uint64_t offset, size_t *done)
{
    *done = 0;
    if (offset > (uint64_t)INT64_MAX) return fail_with(OS_E_INVAL);
    ssize_t r = pwrite(f->fd, buf, n, (off_t)offset);
    if (r < 0) return fail();   /* EINTR maps to OS_E_INTR */
    *done = (size_t)r;
    return OS_OK;
}
os_err os_file_stat(const os_file *f, os_stat *st)
{
    struct stat s;
    int rc;
    RETRY(rc, fstat(f->fd, &s));
    if (rc != 0) return fail();
    from_stat(st, &s);
    return OS_OK;
}
os_err os_file_set_size(os_file *f, uint64_t size)
{
    if (size > (uint64_t)INT64_MAX) return fail_with(OS_E_INVAL);
    int rc;
    RETRY(rc, ftruncate(f->fd, (off_t)size));
    return rc ? fail() : OS_OK;
}
os_err os_file_sync(os_file *f)
{
    int rc;
    RETRY(rc, fsync(f->fd));
    return rc ? fail() : OS_OK;
}
os_err os_file_sync_data(os_file *f)
{
    int rc;
    RETRY(rc, fdatasync(f->fd));
    return rc ? fail() : OS_OK;
}
os_err os_file_set_mode(os_file *f, uint32_t perm)
{
    int rc;
    RETRY(rc, fchmod(f->fd, (mode_t)(perm & 07777u)));
    return rc ? fail() : OS_OK;
}
os_err os_file_clone(os_file *dst, const os_file *src)
{
    int rc;
    RETRY(rc, ioctl(dst->fd, FICLONE, src->fd));
    if (!rc) return OS_OK;
    /* No reflink here: different filesystems, or one without the feature. */
    if (errno == EOPNOTSUPP || errno == EXDEV || errno == EINVAL || errno == ENOTTY ||
        errno == ENOSYS) return OS_E_NOTSUP;
    return fail();
}
os_err os_file_to_stdio(os_file *f, const char *mode, FILE **out)
{
    *out = NULL;
    if (!mode || f->fd < 0) return fail_with(OS_E_INVAL);
    FILE *fp = fdopen(f->fd, mode);
    if (!fp) return fail();
    *out = fp;
    f->fd = -1;   /* the FILE owns the descriptor now */
    return OS_OK;
}

static os_err lock_fd(int fd, os_lock kind)
{
    int operation;
    switch (kind) {
    case OS_LOCK_SHARED: operation = LOCK_SH; break;
    case OS_LOCK_EXCLUSIVE: operation = LOCK_EX; break;
    default: return fail_with(OS_E_INVAL);
    }
    int rc;
    RETRY(rc, flock(fd, operation | LOCK_NB));
    if (!rc) return OS_OK;
    return errno == EWOULDBLOCK ? OS_E_BUSY : fail();
}
static void unlock_fd(int fd)
{
    int e = errno, rc;
    RETRY(rc, flock(fd, LOCK_UN));
    (void)rc;
    errno = e;
}
os_err os_file_try_lock(os_file *f, os_lock kind) { return lock_fd(f->fd, kind); }
void os_file_unlock(os_file *f) { unlock_fd(f->fd); }
int os_file_fd(const os_file *f) { return f->fd; }
os_file os_file_from_fd(int fd) { os_file f = {fd}; return f; }

/* ---- directories -------------------------------------------------------- */
bool os_dir_is_open(const os_dir *d) { return d && d->fd >= 0; }
static os_err dir_open_at(os_dir *out, int dirfd, const char *path)
{
    out->fd = -1;
    if (!path) return fail_with(OS_E_INVAL);
    int fd;
    RETRY(fd, openat(dirfd, path, O_RDONLY | O_DIRECTORY | O_CLOEXEC));
    /* Anonymous creation only needs search/create permission. Keep the usual
     * readable reference whenever possible, so sync and flock stay unchanged. */
    if (fd < 0 && errno == EACCES)
        RETRY(fd, openat(dirfd, path, O_PATH | O_DIRECTORY | O_CLOEXEC));
    if (fd < 0) return fail();
    out->fd = fd;
    return OS_OK;
}
os_err os_dir_open(os_dir *out, const char *path)
{
    return dir_open_at(out, AT_FDCWD, path);
}
os_err os_dir_open_at(os_dir *out, const os_dir *dir, const char *name)
{
    if (bad_name(name)) {
        out->fd = -1;
        return fail_with(OS_E_INVAL);
    }
    return dir_open_at(out, dir->fd, name);
}
void os_dir_close(os_dir *d)
{
    if (d && d->fd >= 0) { int e = errno; (void)close(d->fd); errno = e; }
    if (d) d->fd = -1;
}
static os_err readable_directory(os_dir *d)
{
    int flags;
    RETRY(flags, fcntl(d->fd, F_GETFL));
    if (flags < 0) return fail();
    if (!(flags & O_PATH)) return OS_OK;
    int fd;
    RETRY(fd, openat(d->fd, ".", O_RDONLY | O_DIRECTORY | O_CLOEXEC));
    if (fd < 0) return fail();
    (void)close(d->fd);
    d->fd = fd;
    return OS_OK;
}
os_err os_dir_sync(os_dir *d)
{
    os_err rc = readable_directory(d);
    if (rc != OS_OK) return rc;
    int synced;
    RETRY(synced, fsync(d->fd));
    return synced ? fail() : OS_OK;
}
/* flock on the directory inode itself (journal_retire_directory's guard). */
os_err os_dir_try_lock(os_dir *d, os_lock kind)
{
    os_err rc = readable_directory(d);
    return rc != OS_OK ? rc : lock_fd(d->fd, kind);
}
void os_dir_unlock(os_dir *d) { unlock_fd(d->fd); }
int os_dir_fd(const os_dir *d) { return d->fd; }

os_err os_stat_at(const os_dir *dir, const char *name, unsigned flags, os_stat *st)
{
    if (bad_name(name)) return fail_with(OS_E_INVAL);
    struct stat s;
    int rc;
    RETRY(rc, fstatat(dir->fd, name, &s, (flags & OS_STAT_NOFOLLOW) ? AT_SYMLINK_NOFOLLOW : 0));
    if (rc != 0) return fail();
    from_stat(st, &s);
    return OS_OK;
}
os_err os_stat_path(const char *path, unsigned flags, os_stat *st)
{
    struct stat s;
    if (!path) return fail_with(OS_E_INVAL);
    int rc;
    RETRY(rc, (flags & OS_STAT_NOFOLLOW) ? lstat(path, &s) : stat(path, &s));
    if (rc != 0) return fail();
    from_stat(st, &s);
    return OS_OK;
}
os_err os_mkdir_at(const os_dir *dir, const char *name)
{
    if (bad_name(name)) return fail_with(OS_E_INVAL);
    int rc;
    RETRY(rc, mkdirat(dir->fd, name, 0700));
    return rc ? fail() : OS_OK;
}
os_err os_mkdir(const char *path)
{
    if (!path) return fail_with(OS_E_INVAL);
    int rc;
    RETRY(rc, mkdir(path, 0700));
    return rc ? fail() : OS_OK;
}
os_err os_rmdir(const char *path)
{
    if (!path) return fail_with(OS_E_INVAL);
    int rc;
    RETRY(rc, rmdir(path));
    return rc ? fail() : OS_OK;
}
os_err os_unlink_at(const os_dir *dir, const char *name)
{
    if (bad_name(name)) return fail_with(OS_E_INVAL);
    int rc;
    RETRY(rc, unlinkat(dir->fd, name, 0));
    return rc ? fail() : OS_OK;
}
os_err os_unlink(const char *path)
{
    if (!path) return fail_with(OS_E_INVAL);
    int rc;
    RETRY(rc, unlink(path));
    return rc ? fail() : OS_OK;
}
static os_err rename_fds(int from_fd, const char *from, int to_fd, const char *to, os_rename_mode mode)
{
    int rc;
    switch (mode) {
    case OS_RENAME_REPLACE:
        RETRY(rc, renameat(from_fd, from, to_fd, to));
        break;
    case OS_RENAME_NOREPLACE:
        RETRY(rc, renameat2(from_fd, from, to_fd, to, RENAME_NOREPLACE));
        /* Some filesystems reject the flag with EINVAL. */
        if (rc && errno == EINVAL) return OS_E_NOTSUP;
        break;
    default:
        return fail_with(OS_E_INVAL);
    }
    return rc ? fail() : OS_OK;
}
os_err os_rename_at(const os_dir *from_dir, const char *from, const os_dir *to_dir,
                    const char *to, os_rename_mode mode)
{
    if (bad_name(from) || bad_name(to)) return fail_with(OS_E_INVAL);
    return rename_fds(from_dir->fd, from, to_dir->fd, to, mode);
}
os_err os_rename(const char *from, const char *to, os_rename_mode mode)
{
    if (!from || !to) return fail_with(OS_E_INVAL);
    return rename_fds(AT_FDCWD, from, AT_FDCWD, to, mode);
}

os_err os_dir_iter_open(os_dir_iter *it, const os_dir *dir)
{
    it->dir = NULL;
    /* A fresh description of the same directory: its own cursor. */
    int fd;
    RETRY(fd, openat(dir->fd, ".", O_RDONLY | O_DIRECTORY | O_CLOEXEC));
    if (fd < 0) return fail();
    DIR *d = fdopendir(fd);
    if (!d) { int e = errno; (void)close(fd); errno = e; return fail(); }
    it->dir = d;
    return OS_OK;
}
os_err os_dir_next(os_dir_iter *it, const char **name)
{
    *name = NULL;
    for (;;) {
        errno = 0;
        struct dirent *e = readdir(it->dir);
        if (!e) {
            if (errno == EINTR) continue;
            return errno ? fail() : OS_OK;
        }
        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) continue;
        *name = e->d_name;
        return OS_OK;
    }
}
void os_dir_rewind(os_dir_iter *it) { rewinddir(it->dir); }
void os_dir_iter_close(os_dir_iter *it)
{
    if (it->dir) (void)closedir(it->dir);
    it->dir = NULL;
}

/* ---- paths and well-known directories ---------------------------------- */
bool os_path_is_absolute(const char *path) { return path && path[0] == '/'; }
os_err os_path_canonical(const char *path, char *out, size_t cap)
{
    char tmp[PATH_MAX];
    if (!path) return fail_with(OS_E_INVAL);
    if (!realpath(path, tmp)) return fail();
    return copy_out(out, cap, tmp, strlen(tmp));
}
os_err os_host_name(char *out, size_t cap)
{
    if (!out || !cap) return fail_with(OS_E_INVAL);
    if (gethostname(out, cap)) return errno == ENAMETOOLONG ? OS_E_RANGE : fail();
    if (!memchr(out, 0, cap)) return fail_with(OS_E_RANGE);
    return OS_OK;
}
os_err os_cwd(char *out, size_t cap)
{
    if (!out || !cap) return fail_with(OS_E_RANGE);
    return getcwd(out, cap) ? OS_OK : fail();
}
/* Join a root and an already slash-prefixed suffix, regardless of the
 * number of trailing slashes in the root. */
static os_err root_join(char *out, size_t cap, const char *root, const char *suffix)
{
    size_t len = strlen(root), sl = strlen(suffix);
    while (len && root[len - 1] == '/') len--;
    if (!out || len >= cap || sl >= cap - len) return fail_with(OS_E_RANGE);
    memcpy(out, root, len);
    memcpy(out + len, suffix, sl + 1);
    return OS_OK;
}
os_err os_app_dir(os_app_dir_kind kind, char *out, size_t cap)
{
    const char *var, *fallback;
    switch (kind) {
    case OS_APP_CONFIG:
        var = "XDG_CONFIG_HOME";
        fallback = "/.config/" EDIT_APP_ID;
        break;
    case OS_APP_DATA:
        var = "XDG_DATA_HOME";
        fallback = "/.local/share/" EDIT_APP_ID;
        break;
    default:
        return fail_with(OS_E_INVAL);
    }
    const char *root = getenv(var);
    if (os_path_is_absolute(root)) return root_join(out, cap, root, "/" EDIT_APP_ID);
    root = getenv("HOME");
    if (!os_path_is_absolute(root)) return fail_with(OS_E_NOENT);
    return root_join(out, cap, root, fallback);
}
os_err os_temp_dir(char *out, size_t cap)
{
    const char *t = getenv("TMPDIR");
    if (!os_path_is_absolute(t)) t = "/tmp";
    size_t len = strlen(t);
    while (len > 1 && t[len - 1] == '/') len--;
    return copy_out(out, cap, t, len);
}
os_err os_env_get(const char *name, char *out, size_t cap)
{
    if (!name) return fail_with(OS_E_INVAL);
    const char *v = getenv(name);
    if (!v) return fail_with(OS_E_NOENT);
    return copy_out(out, cap, v, strlen(v));
}
os_err os_env_set(const char *name, const char *value)
{
    if (!name) return fail_with(OS_E_INVAL);
    int rc = value ? setenv(name, value, 1) : unsetenv(name);
    return rc ? fail() : OS_OK;
}

/* ---- memory ------------------------------------------------------------- */
size_t os_page_size(void) { return (size_t)getpagesize(); }
size_t os_map_granularity(void) { return os_page_size(); }
uint64_t os_phys_mem_bytes(void)
{
    long pages = sysconf(_SC_PHYS_PAGES);
    return pages > 0 ? (uint64_t)pages * os_page_size() : 0;
}
os_err os_map_file(os_map *out, const os_file *f, uint64_t offset, size_t len)
{
    out->addr = NULL;
    out->len = 0;
    if (!len || len > (uint64_t)INT64_MAX || offset > (uint64_t)INT64_MAX - len ||
        offset % os_map_granularity())
        return fail_with(OS_E_INVAL);
    void *p = mmap(NULL, len, PROT_READ, MAP_PRIVATE, f->fd, (off_t)offset);
    if (p == MAP_FAILED) return fail();
    out->addr = p;
    out->len = len;
    return OS_OK;
}
void os_unmap(os_map *m)
{
    /* munmap rounds the logical length up to the native page extent. */
    if (m->addr) (void)munmap((void *)(uintptr_t)m->addr, m->len);
    m->addr = NULL;
    m->len = 0;
}
void os_map_prefetch(const os_map *m, size_t off, size_t len)
{
    if (!m->addr || !len || off >= m->len) return;
    if (len > m->len - off) len = m->len - off;
    uintptr_t page = os_page_size(), at = (uintptr_t)m->addr + off;
    uintptr_t start = at & ~(page - 1u);
    int e = errno;
    (void)madvise((void *)start, len + (at - start), MADV_WILLNEED);
    errno = e;
}
static bool page_range(const void *addr, size_t len)
{
    size_t page = os_page_size();
    return addr && len && ((uintptr_t)addr % page) == 0 && len % page == 0;
}
os_err os_vm_reserve(void **out, size_t len)
{
    *out = NULL;
    if (!len || len % os_page_size()) return fail_with(OS_E_INVAL);
    void *p = mmap(NULL, len, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    if (p == MAP_FAILED) return fail();
    /* Decommit is page-granular. Opt out of transparent huge pages at map
     * time so MADV_DONTNEED can return the backing at that granularity. */
    (void)madvise(p, len, MADV_NOHUGEPAGE);
    *out = p;
    return OS_OK;
}
os_err os_vm_commit(void *addr, size_t len)
{
    return page_range(addr, len) ? OS_OK : fail_with(OS_E_INVAL);
}
os_err os_vm_decommit(void *addr, size_t len)
{
    if (!page_range(addr, len)) return fail_with(OS_E_INVAL);
    return madvise(addr, len, MADV_DONTNEED) ? fail() : OS_OK;
}
void os_vm_release(void *addr, size_t len)
{
    if (addr && len) (void)munmap(addr, len);
}

/* ---- random ------------------------------------------------------------- */
os_err os_random(void *buf, size_t n)
{
    size_t got = 0;
    while (got < n) {
        ssize_t r = getrandom((unsigned char *)buf + got, n - got, 0);
        if (r < 0) { if (errno == EINTR) continue; return fail(); }
        got += (size_t)r;
    }
    return OS_OK;
}

/* ---- processes ---------------------------------------------------------- */
os_err os_proc_spawn(os_proc *out, const char *const argv[])
{
    out->pid = -1;
    if (!argv || !argv[0]) return fail_with(OS_E_INVAL);
    /* Only the standard streams reach the child: close every descriptor from
     * 3 up, including ones opened without O_CLOEXEC outside os.h (os.h). */
    posix_spawn_file_actions_t fa;
    int rc = posix_spawn_file_actions_init(&fa);
    if (rc) { errno = rc; return fail(); }
    rc = posix_spawn_file_actions_addclosefrom_np(&fa, 3);
    pid_t pid = -1;
    /* posix_spawn's argv is char *const[] for historical reasons; it does not
     * modify the strings. */
    if (!rc) rc = posix_spawn(&pid, argv[0], &fa, NULL, (char *const *)(uintptr_t)argv, environ);
    (void)posix_spawn_file_actions_destroy(&fa);
    if (rc) { errno = rc; return fail(); }
    out->pid = (int)pid;
    return OS_OK;
}
os_err os_proc_kill(os_proc *p)
{
    if (p->pid <= 0) return fail_with(OS_E_INVAL);
    return kill((pid_t)p->pid, SIGKILL) ? fail() : OS_OK;
}
static void status_of(int status, os_proc_status *st)
{
    if (WIFEXITED(status)) { st->how = OS_PROC_EXITED; st->code = WEXITSTATUS(status); }
    else { st->how = OS_PROC_ABNORMAL; st->code = WIFSIGNALED(status) ? WTERMSIG(status) : 0; }
}
os_err os_proc_wait(os_proc *p, int64_t timeout_ns, os_proc_status *st)
{
    if (p->pid <= 0) return fail_with(OS_E_INVAL);
    int status = 0;
    pid_t r;
    if (timeout_ns < 0) {
        RETRY(r, waitpid((pid_t)p->pid, &status, 0));
    } else {
        uint64_t start = os_now_ns();
        for (;;) {
            RETRY(r, waitpid((pid_t)p->pid, &status, WNOHANG));
            if (r != 0) break;
            uint64_t elapsed = os_now_ns() - start;
            if (elapsed >= (uint64_t)timeout_ns) return fail_with(OS_E_TIMEDOUT);
            uint64_t left = (uint64_t)timeout_ns - elapsed;
            /* Preserve v1 font discovery's 1 ms waitpid polling interval. */
            os_sleep_ns(left < NS_PER_MS ? left : NS_PER_MS);
        }
    }
    if (r < 0) return fail();
    status_of(status, st);
    p->pid = -1;
    return OS_OK;
}
uint64_t os_proc_id(void) { return (uint64_t)getpid(); }
os_err os_fd_limit_raise(uint64_t *soft)
{
    struct rlimit now;
    if (getrlimit(RLIMIT_NOFILE, &now)) return fail();
    if (soft) *soft = (uint64_t)now.rlim_cur;
    if (now.rlim_cur == now.rlim_max) return OS_OK;
    /* Linux already applies the kernel ceiling to the hard limit. */
    now.rlim_cur = now.rlim_max;
    if (setrlimit(RLIMIT_NOFILE, &now)) return fail();
    if (soft) *soft = (uint64_t)now.rlim_cur;
    return OS_OK;
}
os_err os_proc_self_path(char *out, size_t cap)
{
    char tmp[PATH_MAX];
    ssize_t n = readlink("/proc/self/exe", tmp, sizeof tmp);
    if (n < 0) return fail();
    if ((size_t)n >= sizeof tmp) return fail_with(OS_E_NAMETOOLONG);
    return copy_out(out, cap, tmp, (size_t)n);
}
