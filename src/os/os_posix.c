#include "os/os.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <linux/fs.h>
#include <sched.h>
#include <semaphore.h>
#include <signal.h>
#include <stddef.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/random.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/timerfd.h>
#include <sys/un.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

#ifndef EDIT_APP_ID
#error "EDIT_APP_ID must be defined by the build (Makefile APP_ID)"
#endif

_Static_assert(sizeof(off_t) == sizeof(int64_t), "file offsets require a 64-bit off_t");
_Static_assert(OS_PATH_MAX == PATH_MAX && OS_NAME_MAX == NAME_MAX + 1u,
               "public capacities must match Linux path and name limits");

/* Restarts a call that returns -1 with errno EINTR (os.h retry policy). */
#define RETRY(var, call) do { (var) = (call); } while ((var) == -1 && errno == EINTR)

/* (P) SI conversions for timespec and epoll. */
#define NS_PER_SEC INT64_C(1000000000)
#define NS_PER_MS INT64_C(1000000)

/* ---- errors ------------------------------------------------------------- */
static os_err map_errno(int e)
{
    switch (e) {
    case ENOENT: return OS_E_NOENT;
    case EEXIST: case ENOTEMPTY: case EADDRINUSE: return OS_E_EXIST;
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
    case ECONNREFUSED: return OS_E_REFUSED;
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
        "OS_E_RANGE", "OS_E_IO", "OS_E_INTR", "OS_E_REFUSED"};
    unsigned i = (unsigned)e;
    return i < sizeof names / sizeof names[0] ? names[i] : "OS_E_?";
}
int os_native_error(void) { return errno; }

_Noreturn void os_assert_fail(const char *file, int line, const char *expr)
{
    /* Async-signal-safe: digits by hand, then write(2) and abort(3). */
    char digits[sizeof "2147483647"];
    size_t at = sizeof digits;
    unsigned v = line > 0 ? (unsigned)line : 0u;
    do digits[--at] = (char)('0' + v % 10u); while ((v /= 10u) != 0 && at > 0);
    const char *part[] = {file, ":", NULL, ": ASSERT failed: ", expr, "\n"};
    for (size_t i = 0; i < sizeof part / sizeof part[0]; i++) {
        const char *p = part[i] ? part[i] : digits + at;
        size_t n = part[i] ? strlen(p) : sizeof digits - at;
        while (n > 0) {
            ssize_t w = write(STDERR_FILENO, p, n);
            if (w < 0 && errno == EINTR) continue;
            if (w <= 0) break;
            p += w;
            n -= (size_t)w;
        }
    }
    abort();
}

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
            : S_ISLNK(s->st_mode) ? OS_KIND_LINK : S_ISSOCK(s->st_mode) ? OS_KIND_SOCKET
            : OS_KIND_OTHER;
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

/* ---- mapped-file fault service ------------------------------------------ */
/* docs/decisions/wave1-os-fault-lease.md. The signal ABI gives the handler no
 * context, so the registry is the one PLAN-named global, os_fault_service.
 * Audited target: Linux x86-64 with glibc, whose mmap is async-signal-safe;
 * the build compiles no other backend.
 *
 * Every atomic operation is seq_cst, written without _explicit forms. A
 * weaker order needs a proof and a measured need; the default needs neither.
 *
 * The handler takes no lock, waits for nothing, allocates nothing and logs
 * nothing. It calls mmap, sem_post, sigemptyset, sigaction and raise (and,
 * on a broken invariant, ASSERT's write and abort), uses only atomics checked
 * lock-free at installation, and restores errno. */

/* The control word: a phase in its low two bits plus FAULT_HANDLER per
 * admitted handler. */
typedef enum fault_phase { SLOT_FREE, SLOT_LIVE, SLOT_RETIRING } fault_phase;
#define FAULT_PHASE_MASK UINT64_C(3)
#define FAULT_HANDLER UINT64_C(4)
/* (P) The count has 62 bits. Every simultaneous admission holds one slot in
 * its own live signal frame, which contains a siginfo_t; a 64-bit address
 * space holds at most 2^64 / sizeof(siginfo_t) such frames, fewer than 2^61
 * when sizeof(siginfo_t) > 8. So the count cannot overflow. */
_Static_assert(sizeof(void *) == sizeof(uint64_t), "fault count bound: 64-bit address space");
_Static_assert(sizeof(siginfo_t) > 8, "fault count bound: signal frame larger than eight bytes");

typedef enum fault_install_state { FAULT_COLD, FAULT_READY, FAULT_UNAVAILABLE } fault_install_state;
/* Where an unrepaired SIGBUS goes: death by SIGBUS, or the callable handler
 * installed before the service, in its signature. */
typedef enum fault_prior { PRIOR_NONE, PRIOR_HANDLER, PRIOR_SIGINFO } fault_prior;

typedef struct fault_slot {
    /* Only the mutex holder changes the phase. Handlers add FAULT_HANDLER
     * only in LIVE and remove it in LIVE or RETIRING. FREE has no handlers;
     * RETIRING exists only while its retiring owner holds the mutex. */
    _Atomic uint64_t control;
    /* The issued generation from publication to invalidation, else 0. */
    _Atomic uint64_t gen;
    /* Recoveries of the current registration, saturating at UINT64_MAX;
     * reset only by register while gen is 0 and no handler is admitted. */
    _Atomic uint64_t epoch;
    /* Written by the registering owner while FREE; read by a handler only
     * after its admission, so they are the registration it entered. */
    uintptr_t base;
    size_t len;
} fault_slot;

typedef struct fault_service {
    fault_slot slots[OS_FAULT_SLOTS];
    /* Serializes register and retire, including the whole drain. Never taken
     * by poll or the handler. */
    pthread_mutex_t mu;
    /* Last issued generation across all slots; under mu, never polled. */
    uint64_t issued;
    os_once once;
    /* Written once by fault_install; read after os_once_run returns. */
    fault_install_state install;
    /* Zero except between the last departure from a closed slot and its
     * retiring owner's wait, both inside that owner's hold of mu. */
    sem_t drain;
    /* A power of two, set before the handler is installed. */
    uintptr_t page;
    /* The action the service replaced; written once, before prior_kind. */
    struct sigaction prior;
    _Atomic fault_prior prior_kind;
} fault_service;

static fault_service os_fault_service = {
    .mu = PTHREAD_MUTEX_INITIALIZER,
    .once = OS_ONCE_INIT,
    .install = FAULT_COLD,
};

static uintptr_t fault_end(uintptr_t base, size_t len, uintptr_t page)
{
    return (base + len + page - 1u) & ~(page - 1u);
}

/* Admits a handler into a LIVE slot; a failed CAS re-evaluates the phase. */
static bool fault_admit(fault_slot *slot)
{
    uint64_t w = atomic_load(&slot->control);
    for (;;) {
        switch ((fault_phase)(w & FAULT_PHASE_MASK)) {
        case SLOT_FREE:
        case SLOT_RETIRING:
            return false;
        case SLOT_LIVE:
            break;
        }
        if (atomic_compare_exchange_weak(&slot->control, &w, w + FAULT_HANDLER)) return true;
    }
}

/* Drops an admission after the handler's last access to the slot and its
 * view. The departure that leaves a closed slot empty owes one post. */
static void fault_leave(fault_slot *slot)
{
    if (atomic_fetch_sub(&slot->control, FAULT_HANDLER) == (FAULT_HANDLER | SLOT_RETIRING)) {
        int posted = sem_post(&os_fault_service.drain);
        ASSERT(posted == 0);
    }
}

/* True if a registration covers a and its suffix now reads zeros. */
static bool fault_recover(uintptr_t a)
{
    fault_service *s = &os_fault_service;
    for (size_t i = 0; i < OS_FAULT_SLOTS; i++) {
        fault_slot *slot = &s->slots[i];
        if (!fault_admit(slot)) continue;
        uintptr_t base = slot->base;
        size_t len = slot->len;
        if (a < base || a - base >= len) {
            fault_leave(slot);
            continue;
        }
        /* Overlap rejection makes this the only match. Taint, saturating,
         * then fence, so no zero page is readable before the count. */
        uint64_t e = atomic_load(&slot->epoch);
        while (e != UINT64_MAX && !atomic_compare_exchange_weak(&slot->epoch, &e, e + 1u)) {}
        atomic_thread_fence(memory_order_seq_cst);
        /* From the faulting page through the logical end; the kernel rounds
         * the length up within the registered page-rounded extent. */
        uintptr_t from = a & ~(s->page - 1u);
        bool ok = mmap((void *)from, base + len - from, PROT_READ,
                       MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED | MAP_NORESERVE, -1, 0) != MAP_FAILED;
        fault_leave(slot);
        return ok;
    }
    return false;
}

static void fault_handler(int sig, siginfo_t *si, void *uc)
{
    int saved = errno;
    if ((si->si_code == BUS_ADRERR || si->si_code == BUS_OBJERR) && fault_recover((uintptr_t)si->si_addr)) {
        errno = saved;
        return;
    }
    /* Unrepaired. With no prior handler (PRIOR_NONE) the process dies by
     * SIGBUS: never back to the faulting instruction with nothing changed.
     * With a prior callable handler the outcome is that handler's, as it would
     * be without the service (a handler that returns re-executes the
     * instruction). The prior handler runs in signal context under its own
     * async-signal-safety obligation; its SA_RESETHAND is not emulated.
     * prior_kind's load reads the store fault_install made after writing the
     * prior cell and before the sigaction that installed this handler: that
     * atomic is the happens-before edge to the cell on every thread. */
    switch (atomic_load(&os_fault_service.prior_kind)) {
    case PRIOR_HANDLER:
        errno = saved;
        os_fault_service.prior.sa_handler(sig);
        break;
    case PRIOR_SIGINFO:
        errno = saved;
        os_fault_service.prior.sa_sigaction(sig, si, uc);
        break;
    case PRIOR_NONE: {
        /* SIGBUS stays blocked until this handler returns, then the pending
         * one kills under SIG_DFL (as would a repeated real fault). */
        struct sigaction def = {.sa_handler = SIG_DFL};
        int empty = sigemptyset(&def.sa_mask);
        ASSERT(empty == 0);
        int reset = sigaction(SIGBUS, &def, NULL);
        ASSERT(reset == 0);
        int raised = raise(SIGBUS);
        ASSERT(raised == 0);
        break;
    }
    }
    errno = saved;
}

static void fault_install(void)
{
    fault_service *s = &os_fault_service;
    s->install = FAULT_UNAVAILABLE;
    for (size_t i = 0; i < OS_FAULT_SLOTS; i++) {
        fault_slot *slot = &s->slots[i];
        if (!atomic_is_lock_free(&slot->control) || !atomic_is_lock_free(&slot->gen) ||
            !atomic_is_lock_free(&slot->epoch))
            return;
    }
    if (!atomic_is_lock_free(&s->prior_kind)) return;
    long page = sysconf(_SC_PAGESIZE);
    if (page <= 0 || (page & (page - 1)) != 0) return;
    s->page = (uintptr_t)page;
    if (sem_init(&s->drain, 0, 0) != 0) return;
    /* A fixed action: SA_SIGINFO, empty mask. */
    struct sigaction sa = {.sa_sigaction = fault_handler, .sa_flags = SA_SIGINFO};
    int empty = sigemptyset(&sa.sa_mask);
    ASSERT(empty == 0);
    if (sigaction(SIGBUS, NULL, &s->prior) != 0) {
        (void)sem_destroy(&s->drain);
        return;
    }
    void (*h)(int) = s->prior.sa_handler;
    atomic_store(&s->prior_kind, h == SIG_DFL || h == SIG_IGN ? PRIOR_NONE
                                 : (s->prior.sa_flags & SA_SIGINFO) ? PRIOR_SIGINFO
                                                                     : PRIOR_HANDLER);
    if (sigaction(SIGBUS, &sa, NULL) != 0) {
        (void)sem_destroy(&s->drain);
        return;
    }
    s->install = FAULT_READY;
}

/* Under mu. Rejects intersecting rounded extents, then takes the lowest free
 * slot and publishes a fresh generation. */
static os_err fault_claim(fault_service *s, uintptr_t base, size_t len, os_fault_id *out)
{
    uintptr_t end = fault_end(base, len, s->page);
    size_t at = OS_FAULT_SLOTS;
    for (size_t i = 0; i < OS_FAULT_SLOTS; i++) {
        fault_slot *slot = &s->slots[i];
        switch ((fault_phase)(atomic_load(&slot->control) & FAULT_PHASE_MASK)) {
        case SLOT_FREE:
            if (at == OS_FAULT_SLOTS) at = i;
            break;
        case SLOT_LIVE:
            if (base < fault_end(slot->base, slot->len, s->page) && slot->base < end) return OS_E_EXIST;
            break;
        case SLOT_RETIRING:
            ASSERT(false && "RETIRING outside its owner's hold of mu");
            break;
        }
    }
    /* Generations never wrap; ids already issued stay usable. */
    if (s->issued == UINT64_MAX || at == OS_FAULT_SLOTS) return OS_E_NOSPC;

    fault_slot *slot = &s->slots[at];
    ASSERT(atomic_load(&slot->control) == SLOT_FREE);
    slot->base = base;
    slot->len = len;
    atomic_store(&slot->epoch, 0);
    atomic_store(&slot->control, SLOT_LIVE);
    uint64_t gen = ++s->issued;
    atomic_store(&slot->gen, gen); /* Last: the pollable identity. */
    out->slot = (uint32_t)at;
    out->gen = gen;
    return OS_OK;
}

os_err os_fault_register(const os_map *view, os_fault_id *out)
{
    if (!out) return fail_with(OS_E_INVAL);
    *out = (os_fault_id)OS_FAULT_ID_INIT;
    if (!view || !view->addr || !view->len) return fail_with(OS_E_INVAL);
    fault_service *s = &os_fault_service;
    os_once_run(&s->once, fault_install);
    switch (s->install) {
    case FAULT_READY:
        break;
    case FAULT_COLD:
    case FAULT_UNAVAILABLE:
        return fail_with(OS_E_NOTSUP);
    }
    uintptr_t base = (uintptr_t)view->addr, mask = s->page - 1u;
    if ((base & mask) || view->len > UINTPTR_MAX - base || base + view->len > UINTPTR_MAX - mask)
        return fail_with(OS_E_INVAL);
    (void)pthread_mutex_lock(&s->mu);
    os_err rc = fault_claim(s, base, view->len, out);
    (void)pthread_mutex_unlock(&s->mu);
    return rc == OS_OK ? OS_OK : fail_with(rc);
}

os_err os_fault_poll(os_fault_id id, uint64_t *epoch)
{
    if (!epoch || !id.gen || id.slot >= OS_FAULT_SLOTS) return fail_with(OS_E_INVAL);
    /* The mapped byte loads are not atomic, so the closing validation needs
     * a barrier compiler and machine both respect. */
    atomic_thread_fence(memory_order_seq_cst);
    fault_slot *slot = &os_fault_service.slots[id.slot];
    /* Two equal samples of a generation issued once bracket the epoch load
     * inside that registration: retire zeroes gen before any reset. */
    if (atomic_load(&slot->gen) != id.gen) return fail_with(OS_E_NOENT);
    uint64_t e = atomic_load(&slot->epoch);
    if (atomic_load(&slot->gen) != id.gen) return fail_with(OS_E_NOENT);
    *epoch = e;
    return OS_OK;
}

os_err os_fault_retire(os_fault_id id)
{
    if (!id.gen || id.slot >= OS_FAULT_SLOTS) return fail_with(OS_E_INVAL);
    fault_service *s = &os_fault_service;
    fault_slot *slot = &s->slots[id.slot];
    (void)pthread_mutex_lock(&s->mu);
    /* Under mu a published generation means LIVE, and only this id's. */
    if (atomic_load(&slot->gen) != id.gen) {
        (void)pthread_mutex_unlock(&s->mu);
        return fail_with(OS_E_NOENT);
    }
    /* Invalidate before the close and before any later epoch reset. */
    atomic_store(&slot->gen, 0);
    /* Close LIVE -> RETIRING keeping the count: no admission after this. */
    uint64_t w = atomic_load(&slot->control);
    do {
        ASSERT((w & FAULT_PHASE_MASK) == SLOT_LIVE);
    } while (!atomic_compare_exchange_weak(&slot->control, &w, (w & ~FAULT_PHASE_MASK) | SLOT_RETIRING));
    /* A positive count owes exactly one post, possibly already made. */
    if (w & ~FAULT_PHASE_MASK) {
        int rc;
        while ((rc = sem_wait(&s->drain)) != 0 && errno == EINTR) {}
        ASSERT(rc == 0);
    }
    /* Close observes every admitted handler gone, then frees the slot. */
    w = atomic_load(&slot->control);
    ASSERT(w == SLOT_RETIRING);
    atomic_store(&slot->control, SLOT_FREE);
    (void)pthread_mutex_unlock(&s->mu);
    return OS_OK;
}

#ifdef EDIT_TEST_HOOKS
/* Test builds only (A15): register next issues generation `next` (nonzero,
 * forward only). Neither os.h nor libcore.a carries it (tests/lanes.mk). */
os_err os_fault_test_next_gen(uint64_t next);
os_err os_fault_test_next_gen(uint64_t next)
{
    fault_service *s = &os_fault_service;
    (void)pthread_mutex_lock(&s->mu);
    bool ok = next != 0 && next - 1u >= s->issued;
    if (ok) s->issued = next - 1u;
    (void)pthread_mutex_unlock(&s->mu);
    return ok ? OS_OK : fail_with(OS_E_INVAL);
}
#endif

os_err os_fault_test_trigger(const os_map *view, size_t off)
{
    if (!view || !view->addr || off >= view->len || off > UINTPTR_MAX - (uintptr_t)view->addr)
        return fail_with(OS_E_INVAL);
    siginfo_t si;
    memset(&si, 0, sizeof si);
    si.si_signo = SIGBUS;
    si.si_code = BUS_ADRERR;
    si.si_addr = (void *)((uintptr_t)view->addr + off);
    /* Linux accepts a positive si_code only for the caller's own process.
     * Four long words (tests/os_fault_test.c forwards exactly these). */
    long rc = syscall(SYS_rt_tgsigqueueinfo, (long)getpid(), (long)gettid(), (long)SIGBUS, &si);
    return rc ? fail() : OS_OK;
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

/* ---- process identity --------------------------------------------------- */
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

/* ---- local streams (v2-0fv.13) ----------------------------------------- */
uint32_t os_user_id(void) { return (uint32_t)getuid(); }

static void close_quiet(int fd)
{
    int e = errno;
    (void)close(fd);
    errno = e;
}

/* (P) "/proc/self/fd/" is 14 bytes, an int descriptor at most 10 digits,
 * plus the NUL. */
#define PROC_FD_CAP 32u
static size_t proc_fd_path(char out[PROC_FD_CAP], int fd)
{
    static const char prefix[] = "/proc/self/fd/";
    char digits[12];
    size_t d = sizeof digits;
    unsigned v = (unsigned)fd;
    do { digits[--d] = (char)('0' + v % 10u); v /= 10u; } while (v);
    size_t n = sizeof prefix - 1u, nd = sizeof digits - d;
    memcpy(out, prefix, n);
    memcpy(out + n, digits + d, nd);
    out[n + nd] = 0;
    return n + nd;
}

/* A directory name is addressed as /proc/self/fd/<dirfd>/<name>: the pinned
 * directory resolves it, so renaming the path cannot redirect the call, and a
 * deep runtime directory still fits sun_path. An abstract name starts with a
 * NUL and has no terminator. */
static os_err local_addr(struct sockaddr_un *a, socklen_t *len, const os_dir *dir,
                         const char *name)
{
    memset(a, 0, sizeof *a);
    a->sun_family = AF_UNIX;
    if (!name || !name[0]) return fail_with(OS_E_INVAL);
    size_t n = strlen(name);
    if (!dir) {
        if (n > sizeof a->sun_path - 1u) return fail_with(OS_E_NAMETOOLONG);
        memcpy(a->sun_path + 1, name, n);
        *len = (socklen_t)(offsetof(struct sockaddr_un, sun_path) + 1u + n);
        return OS_OK;
    }
    if (bad_name(name) || dir->fd < 0) return fail_with(OS_E_INVAL);
    char base[PROC_FD_CAP];
    size_t at = proc_fd_path(base, dir->fd);
    if (at + 1u + n >= sizeof a->sun_path) return fail_with(OS_E_NAMETOOLONG);
    memcpy(a->sun_path, base, at);
    a->sun_path[at++] = '/';
    memcpy(a->sun_path + at, name, n + 1u);
    *len = (socklen_t)(offsetof(struct sockaddr_un, sun_path) + at + n + 1u);
    return OS_OK;
}

static void unlink_if_same(int dirfd, const char *name, const struct stat *want)
{
    int e = errno, rc;
    struct stat now;
    RETRY(rc, fstatat(dirfd, name, &now, AT_SYMLINK_NOFOLLOW));
    if (rc == 0 && now.st_dev == want->st_dev && now.st_ino == want->st_ino)
        (void)unlinkat(dirfd, name, 0);
    errno = e;
}

os_err os_listen_local(os_listener *out, const os_dir *dir, const char *name,
                       uint32_t backlog, os_stat *bound)
{
    out->fd = -1;
    if (backlog == 0 || backlog > (uint32_t)INT_MAX) return fail_with(OS_E_INVAL);
    struct sockaddr_un a;
    socklen_t len;
    os_err e = local_addr(&a, &len, dir, name);
    if (e != OS_OK) return e;
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (fd < 0) return fail();
    int rc;
    RETRY(rc, bind(fd, (const struct sockaddr *)&a, len));
    if (rc != 0) {
        e = fail();
        close_quiet(fd);
        return e;
    }
    struct stat st;
    memset(&st, 0, sizeof st);
    bool ours = false; /* The name names the socket bound here. */
    if (dir) {
        /* Pin first, then check and chmod the pinned inode: no name lookup
         * sits between the check and the mode change. */
        int pin;
        RETRY(pin, openat(dir->fd, name, O_PATH | O_NOFOLLOW | O_CLOEXEC));
        if (pin < 0) e = fail();
        else {
            RETRY(rc, fstat(pin, &st));
            if (rc != 0) e = fail();
            else if (!S_ISSOCK(st.st_mode) || st.st_uid != getuid()) e = fail_with(OS_E_ACCESS);
            else {
                ours = true;
                char path[PROC_FD_CAP];
                (void)proc_fd_path(path, pin);
                RETRY(rc, chmod(path, 0600));
                if (rc != 0) e = fail();
            }
            close_quiet(pin);
        }
    }
    if (e == OS_OK) {
        RETRY(rc, listen(fd, (int)backlog));
        if (rc != 0) e = fail();
    }
    if (e != OS_OK) {
        if (ours) unlink_if_same(dir->fd, name, &st);
        close_quiet(fd);
        return e;
    }
    if (dir && bound) from_stat(bound, &st);
    out->fd = fd;
    return OS_OK;
}
os_err os_listener_accept(os_listener *l, os_stream *out)
{
    out->fd = -1;
    int fd;
    RETRY(fd, accept4(l->fd, NULL, NULL, SOCK_NONBLOCK | SOCK_CLOEXEC));
    if (fd < 0) return fail();
    out->fd = fd;
    return OS_OK;
}
void os_listener_close(os_listener *l)
{
    if (l->fd >= 0) close_quiet(l->fd);
    l->fd = -1;
}
int os_listener_fd(const os_listener *l) { return l->fd; }

os_err os_connect_local(os_stream *out, const os_dir *dir, const char *name)
{
    out->fd = -1;
    struct sockaddr_un a;
    socklen_t len;
    os_err e = local_addr(&a, &len, dir, name);
    if (e != OS_OK) return e;
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (fd < 0) return fail();
    int rc = connect(fd, (const struct sockaddr *)&a, len);
    /* An interrupted connect can still complete; the retry then says EISCONN. */
    while (rc != 0 && errno == EINTR) {
        rc = connect(fd, (const struct sockaddr *)&a, len);
        if (rc != 0 && errno == EISCONN) rc = 0;
    }
    if (rc != 0) {
        e = fail();
        close_quiet(fd);
        return e;
    }
    out->fd = fd;
    return OS_OK;
}
os_err os_stream_pair(os_stream *a, os_stream *b)
{
    int fds[2];
    a->fd = b->fd = -1;
    if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0, fds) != 0) return fail();
    a->fd = fds[0];
    b->fd = fds[1];
    return OS_OK;
}
os_err os_stream_adopt(os_stream *out, int fd)
{
    out->fd = fd;
    if (fd < 0) return fail_with(OS_E_INVAL);
    struct stat st;
    int rc, fl;
    RETRY(rc, fstat(fd, &st));
    if (rc != 0) return fail();
    if (!S_ISSOCK(st.st_mode)) return fail_with(OS_E_INVAL);
    RETRY(fl, fcntl(fd, F_GETFL));
    if (fl < 0) return fail();
    if (!(fl & O_NONBLOCK)) {
        RETRY(rc, fcntl(fd, F_SETFL, fl | O_NONBLOCK));
        if (rc != 0) return fail();
    }
    RETRY(fl, fcntl(fd, F_GETFD));
    if (fl < 0) return fail();
    if (!(fl & FD_CLOEXEC)) {
        RETRY(rc, fcntl(fd, F_SETFD, fl | FD_CLOEXEC));
        if (rc != 0) return fail();
    }
    return OS_OK;
}
static os_err stream_recv(os_stream *s, void *buf, size_t n, size_t *got, int flags)
{
    ssize_t r;
    *got = 0;
    RETRY(r, recv(s->fd, buf, n, flags));
    if (r < 0) return fail();
    *got = (size_t)r;
    return OS_OK;
}
os_err os_stream_read(os_stream *s, void *buf, size_t n, size_t *got)
{
    return stream_recv(s, buf, n, got, 0);
}
os_err os_stream_peek(os_stream *s, void *buf, size_t n, size_t *got)
{
    return stream_recv(s, buf, n, got, MSG_PEEK);
}
os_err os_stream_write(os_stream *s, const void *buf, size_t n, size_t *done)
{
    ssize_t r;
    *done = 0;
    /* MSG_NOSIGNAL: a closed peer is EPIPE here, never a process signal. */
    RETRY(r, send(s->fd, buf, n, MSG_NOSIGNAL));
    if (r < 0) return fail();
    *done = (size_t)r;
    return OS_OK;
}
os_err os_stream_peer_uid(const os_stream *s, uint32_t *uid)
{
    struct ucred cred;
    socklen_t n = sizeof cred;
    if (getsockopt(s->fd, SOL_SOCKET, SO_PEERCRED, &cred, &n) != 0) return fail();
    *uid = (uint32_t)cred.uid;
    return OS_OK;
}
void os_stream_close(os_stream *s)
{
    if (s->fd >= 0) close_quiet(s->fd);
    s->fd = -1;
}
int os_stream_fd(const os_stream *s) { return s->fd; }

/* ---- wait set (v2-0fv.13) ---------------------------------------------- */
/* (E) Stack batch of 64 x 12 B = 768 B, the harvested ipc drain batch
 * (v2-0fv.6). Correctness does not depend on it: epoll is level-triggered, so
 * members past the batch stay ready for the next call. */
#define WAIT_BATCH 64

os_err os_waitset_init(os_waitset *ws)
{
    ws->fd = ws->alarm_fd = -1;
    ws->alarm_ns = OS_DEADLINE_NEVER;
    int fd = epoll_create1(EPOLL_CLOEXEC);
    if (fd < 0) return fail();
    int alarm = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
    os_err e = OS_OK;
    if (alarm < 0) e = fail();
    else {
        struct epoll_event ev;
        memset(&ev, 0, sizeof ev);
        ev.events = EPOLLIN;
        ev.data.u64 = OS_WAIT_ALARM;
        int rc;
        RETRY(rc, epoll_ctl(fd, EPOLL_CTL_ADD, alarm, &ev));
        if (rc != 0) {
            e = fail();
            close_quiet(alarm);
        }
    }
    if (e != OS_OK) {
        close_quiet(fd);
        return e;
    }
    ws->fd = fd;
    ws->alarm_fd = alarm;
    return OS_OK;
}
void os_waitset_close(os_waitset *ws)
{
    if (ws->alarm_fd >= 0) close_quiet(ws->alarm_fd);
    if (ws->fd >= 0) close_quiet(ws->fd);
    ws->fd = ws->alarm_fd = -1;
    ws->alarm_ns = OS_DEADLINE_NEVER;
}
static os_err waitset_ctl(os_waitset *ws, int op, int fd, unsigned interest, uint64_t tag)
{
    if (ws->fd < 0 || fd < 0 || tag == OS_WAIT_ALARM ||
        (interest & ~(unsigned)(OS_WAIT_IN | OS_WAIT_OUT)))
        return fail_with(OS_E_INVAL);
    struct epoll_event ev;
    memset(&ev, 0, sizeof ev);
    /* RDHUP with IN: a half-closed peer wakes its owner to read the end. */
    if (interest & OS_WAIT_IN) ev.events |= EPOLLIN | EPOLLRDHUP;
    if (interest & OS_WAIT_OUT) ev.events |= EPOLLOUT;
    ev.data.u64 = tag;
    int rc;
    RETRY(rc, epoll_ctl(ws->fd, op, fd, &ev));
    return rc ? fail() : OS_OK;
}
os_err os_waitset_add(os_waitset *ws, int fd, unsigned interest, uint64_t tag)
{
    return waitset_ctl(ws, EPOLL_CTL_ADD, fd, interest, tag);
}
os_err os_waitset_modify(os_waitset *ws, int fd, unsigned interest, uint64_t tag)
{
    return waitset_ctl(ws, EPOLL_CTL_MOD, fd, interest, tag);
}
void os_waitset_remove(os_waitset *ws, int fd)
{
    if (ws->fd < 0 || fd < 0) return;
    int e = errno;
    (void)epoll_ctl(ws->fd, EPOLL_CTL_DEL, fd, NULL);
    errno = e;
}
os_err os_waitset_alarm(os_waitset *ws, uint64_t deadline_ns)
{
    if (ws->fd < 0) return fail_with(OS_E_INVAL);
    if (deadline_ns == ws->alarm_ns) return OS_OK;
    struct itimerspec its;
    memset(&its, 0, sizeof its);
    if (deadline_ns != OS_DEADLINE_NEVER) {
        /* An all-zero value would disarm; 1 ns is equally long past. */
        uint64_t d = deadline_ns ? deadline_ns : 1u;
        its.it_value.tv_sec = (time_t)(d / (uint64_t)NS_PER_SEC);
        its.it_value.tv_nsec = (long)(d % (uint64_t)NS_PER_SEC);
    }
    int rc;
    RETRY(rc, timerfd_settime(ws->alarm_fd, TFD_TIMER_ABSTIME, &its, NULL));
    if (rc != 0) return fail();
    ws->alarm_ns = deadline_ns;
    return OS_OK;
}
/* Milliseconds to the deadline, rounded up so a wait never ends early. */
static int wait_ms(uint64_t deadline_ns)
{
    if (deadline_ns == OS_DEADLINE_NEVER) return -1;
    uint64_t now = os_now_ns();
    if (now >= deadline_ns) return 0;
    uint64_t left = deadline_ns - now, ms = (uint64_t)NS_PER_MS;
    uint64_t up = left / ms + (left % ms != 0);
    return up > (uint64_t)INT_MAX ? INT_MAX : (int)up;
}
/* Consumes a fired alarm. False when it was re-armed after readiness. */
static bool alarm_take(os_waitset *ws)
{
    uint64_t ticks;
    ssize_t r;
    int e = errno;
    RETRY(r, read(ws->alarm_fd, &ticks, sizeof ticks));
    errno = e;
    if (r != (ssize_t)sizeof ticks) return false;
    ws->alarm_ns = OS_DEADLINE_NEVER;
    return true;
}
static unsigned wait_events(uint32_t e)
{
    unsigned r = 0;
    if (e & EPOLLIN) r |= OS_WAIT_IN;
    if (e & EPOLLOUT) r |= OS_WAIT_OUT;
    if (e & (EPOLLHUP | EPOLLRDHUP)) r |= OS_WAIT_HUP;
    if (e & EPOLLERR) r |= OS_WAIT_ERR;
    return r;
}
os_err os_waitset_wait(os_waitset *ws, uint64_t deadline_ns, os_wait_event *ev,
                       size_t cap, size_t *n)
{
    *n = 0;
    if (ws->fd < 0 || !ev || cap == 0) return fail_with(OS_E_INVAL);
    struct epoll_event raw[WAIT_BATCH];
    int max = cap < (size_t)WAIT_BATCH ? (int)cap : WAIT_BATCH;
    for (;;) {
        int got = epoll_wait(ws->fd, raw, max, wait_ms(deadline_ns));
        if (got < 0 && errno != EINTR) return fail();
        size_t k = 0;
        for (int i = 0; i < got; i++) {
            if (raw[i].data.u64 == OS_WAIT_ALARM && !alarm_take(ws)) continue;
            ev[k].tag = raw[i].data.u64;
            ev[k].events = wait_events(raw[i].events);
            k++;
        }
        if (k) {
            *n = k;
            return OS_OK;
        }
        /* Deadline reached; else EINTR, a stale alarm or a rounded-short
         * wait: go round with the time left. */
        if (wait_ms(deadline_ns) == 0) return OS_OK;
    }
}
int os_waitset_fd(const os_waitset *ws) { return ws->fd; }
