#ifndef EDIT_OS_H
#define EDIT_OS_H

#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/* Linux system-call boundary. Paths and names are NUL-terminated strings.
 * The OS supplies native errors; locally detected errors have native code 0.
 * Read os_native_error() immediately after an error.
 *
 * Objects are caller-owned. Keep live objects at a stable address and use
 * their fields only through this API, except the read-only os_map view and
 * value types such as os_stat. An output handle must not already own a
 * resource. Distinct handles may be used concurrently. Sharing one handle
 * is supported only where documented below.
 *
 * Clocks, thread identity, yield, mutex lock/unlock, condition signal and
 * broadcast, wake signal/drain and positioned file I/O never allocate.
 * Thread start, directory iteration, canonicalization, environment mutation,
 * stdio conversion and process spawn may allocate and are off the typing path.
 *
 * File transfers restart EINTR unless the entry is explicitly single-attempt.
 * A short successful transfer is returned to the caller. Closing a descriptor
 * never retries: Linux releases it even when close reports EINTR. Descriptors
 * opened by this module are close-on-exec. */

typedef enum os_err {
    OS_OK = 0,
    OS_E_NOENT,
    OS_E_EXIST,
    OS_E_ACCESS,
    OS_E_BUSY,
    OS_E_LOOP,
    OS_E_NOTDIR,
    OS_E_ISDIR,
    OS_E_XDEV,
    OS_E_NOSPC,
    OS_E_NAMETOOLONG,
    OS_E_INVAL,
    OS_E_NOTSUP,
    OS_E_AGAIN,
    OS_E_TIMEDOUT,
    OS_E_NOMEM,
    OS_E_RANGE,
    OS_E_IO,
    OS_E_INTR
} os_err;

const char *os_err_name(os_err e);
int os_native_error(void);

/* (P) Linux PATH_MAX includes the NUL. Name storage includes NAME_MAX
 * bytes plus the NUL (linux/limits.h). */
#define OS_PATH_MAX 4096u
#define OS_NAME_MAX (255u + 1u)

/* The build supplies EDIT_APP_ID. */
const char *os_app_id(void);

/* Monotonic time for deadlines, thread CPU time for accounting, and wall time
 * since the Unix epoch. All times are nanoseconds. */
uint64_t os_now_ns(void);
uint64_t os_thread_cpu_ns(void);
uint64_t os_thread_cpu_resolution_ns(void);
/* Short work-slice deadlines use the calling thread's CPU clock. */
uint64_t os_slice_ns(void);
/* For timestamps and unique names. Use monotonic time for elapsed time. */
uint64_t os_wall_ns(void);
/* Restarts interrupted sleeps with the remaining time. */
void os_sleep_ns(uint64_t ns);
void os_yield(void);

/* Keep the object alive and unmoved until a successful join. */
typedef struct os_thread {
    pthread_t thread;
    void (*fn)(void *);
    void *arg;
} os_thread;
os_err os_thread_start(os_thread *t, void (*fn)(void *), void *arg);
os_err os_thread_join(os_thread *t);
/* Nonzero identity, unique among live threads. May be reused after exit. */
uint64_t os_thread_self(void);

typedef struct os_mutex { pthread_mutex_t m; } os_mutex;
#define OS_MUTEX_INIT { PTHREAD_MUTEX_INITIALIZER }
os_err os_mutex_init(os_mutex *m);
void os_mutex_destroy(os_mutex *m);
void os_mutex_lock(os_mutex *m);
void os_mutex_unlock(os_mutex *m);

/* Piece pool callbacks can re-enter the pool lock. Each lock requires an
 * unlock by the owner. Condition waits use os_mutex. */
typedef struct os_rmutex { pthread_mutex_t m; } os_rmutex;
os_err os_rmutex_init(os_rmutex *m);
void os_rmutex_destroy(os_rmutex *m);
void os_rmutex_lock(os_rmutex *m);
void os_rmutex_unlock(os_rmutex *m);

/* Wait with the mutex held and recheck the predicate after every wake-up. */
typedef struct os_cond { pthread_cond_t c; } os_cond;
os_err os_cond_init(os_cond *c);
void os_cond_destroy(os_cond *c);
void os_cond_wait(os_cond *c, os_mutex *m);
void os_cond_signal(os_cond *c);
void os_cond_broadcast(os_cond *c);

typedef struct os_once { pthread_once_t once; } os_once;
#define OS_ONCE_INIT { PTHREAD_ONCE_INIT }
void os_once_run(os_once *o, void (*fn)(void));

/* Signals coalesce until drained. Any thread may signal and one owner waits
 * and drains. Drain before consuming the announced state. */
typedef struct os_wake { int fd; } os_wake;
os_err os_wake_init(os_wake *w);
void os_wake_close(os_wake *w);
/* Async-signal-safe. */
void os_wake_signal(const os_wake *w);
void os_wake_drain(const os_wake *w);
/* Waits without draining. Negative timeout waits indefinitely, zero polls.
 * Finite poll intervals round up to whole milliseconds. A closed wake gives
 * OS_E_INVAL. */
os_err os_wake_wait(const os_wake *w, int64_t timeout_ns);
int os_wake_fd(const os_wake *w); /* Platform poll loop only. */

typedef enum os_kind {
    OS_KIND_FILE = 1,
    OS_KIND_DIR,
    OS_KIND_LINK,
    OS_KIND_OTHER
} os_kind;

/* Device and inode identity. The second index word is zero on Linux and
 * keeps the existing file identity shape for persistence. */
typedef struct os_identity {
    uint64_t volume;
    uint64_t index[2];
} os_identity;
bool os_identity_equal(const os_identity *a, const os_identity *b);

/* Signed nanoseconds since the Unix epoch, saturating at the int64 bounds
 * (P: 2^63 ns / about 3.156e16 ns per year is about 292 years). */
typedef struct os_stat {
    os_identity id;
    os_kind kind;
    uint64_t size;
    int64_t mtime_ns;
    int64_t ctime_ns;
    uint32_t perm; /* Permission and special mode bits (st_mode & 07777). */
    uint32_t uid, gid;
} os_stat;

typedef struct os_file { int fd; } os_file;
#define OS_FILE_INIT { -1 }
bool os_file_is_open(const os_file *f);

enum {
    OS_OPEN_READ     = 1u << 0,
    OS_OPEN_RDWR     = 1u << 1,
    OS_OPEN_CREATE   = 1u << 2, /* Owner read/write on creation (0600, subject to umask). */
    OS_OPEN_EXCL     = 1u << 3, /* With CREATE, an existing entry gives OS_E_EXIST. */
    OS_OPEN_NOFOLLOW = 1u << 4, /* A final symlink gives OS_E_LOOP. */
    OS_OPEN_NONBLOCK = 1u << 5,
    OS_OPEN_WRITE    = 1u << 6,
    OS_OPEN_TRUNC    = 1u << 7  /* Requires WRITE or RDWR. */
};
/* Exactly one access mode. Directories fail with OS_E_ISDIR. */
os_err os_file_open(os_file *out, const char *path, unsigned flags);
/* Idempotent for an initialized handle. */
void os_file_close(os_file *f);
/* The duplicate shares the sequential position and any flock. */
os_err os_file_dup(os_file *out, const os_file *f);

/* A successful read with *got == 0 and n > 0 is end of file. */
os_err os_file_read(os_file *f, void *buf, size_t n, size_t *got);
os_err os_file_write(os_file *f, const void *buf, size_t n, size_t *done);
/* Positioned I/O on one file may run concurrently and leaves its sequential
 * position unchanged. The caller coordinates any overlapping writes. */
os_err os_file_pread(os_file *f, void *buf, size_t n, uint64_t offset, size_t *got);
os_err os_file_pwrite(os_file *f, const void *buf, size_t n, uint64_t offset, size_t *done);
/* One pwrite for a valid offset, including on EINTR. The journal treats a
 * short transfer as an error. No sync or allocation. */
os_err os_file_pwrite_once(os_file *f, const void *buf, size_t n, uint64_t offset, size_t *done);
os_err os_file_stat(const os_file *f, os_stat *st);
os_err os_file_set_size(os_file *f, uint64_t size);
os_err os_file_sync(os_file *f);      /* Data and metadata. */
os_err os_file_sync_data(os_file *f); /* Data and the metadata needed to read it. */
os_err os_file_set_mode(os_file *f, uint32_t perm);
/* Reflinks the whole file. On OS_E_NOTSUP the caller may copy the bytes. */
os_err os_file_clone(os_file *dst, const os_file *src);
/* On success the FILE owns the descriptor. Failure leaves f open. */
os_err os_file_to_stdio(os_file *f, const char *mode, FILE **out);

/* Advisory, nonblocking flock. Other opens can still read and write.
 * Duplicates share the lock. Any duplicate can unlock it and closing the
 * last one releases it. A separate open conflicts with an exclusive lock
 * and gets OS_E_BUSY. Unlock before changing the lock kind. */
typedef enum os_lock { OS_LOCK_SHARED = 1, OS_LOCK_EXCLUSIVE = 2 } os_lock;
os_err os_file_try_lock(os_file *f, os_lock kind);
void os_file_unlock(os_file *f);
int os_file_fd(const os_file *f); /* Borrows the descriptor. */
os_file os_file_from_fd(int fd);  /* Adopts it with its existing flags. */

/* A pinned directory. Renaming the directory leaves the handle on the same
 * object. The *_at calls take one name component ("." names the directory). */
typedef struct os_dir { int fd; } os_dir;
#define OS_DIR_INIT { -1 }
bool os_dir_is_open(const os_dir *d);
os_err os_dir_open(os_dir *out, const char *path);
os_err os_dir_open_at(os_dir *out, const os_dir *dir, const char *name);
void os_dir_close(os_dir *d);
/* Flushes completed namespace changes through fsync on the directory. */
os_err os_dir_sync(os_dir *d);
/* Advisory flock on the directory itself. The journal's retirement lock
 * survives rename and creates no lock-file entry. */
os_err os_dir_try_lock(os_dir *d, os_lock kind);
void os_dir_unlock(os_dir *d);
/* Borrows the descriptor. A search-only directory can hold an O_PATH fd,
 * which sync or lock can replace with a readable fd. */
int os_dir_fd(const os_dir *d);

enum { OS_STAT_NOFOLLOW = 1u << 0 };
os_err os_stat_at(const os_dir *dir, const char *name, unsigned flags, os_stat *st);
os_err os_stat_path(const char *path, unsigned flags, os_stat *st);
os_err os_file_open_at(os_file *out, const os_dir *dir, const char *name, unsigned flags);
/* Unnamed O_TMPFILE. OS_E_NOTSUP lets the caller select its own fallback. */
os_err os_file_open_anon(os_file *out, const os_dir *dir);
/* Owner-only directory (0700, subject to umask). */
os_err os_mkdir_at(const os_dir *dir, const char *name);
os_err os_mkdir(const char *path);
/* Removes an empty directory, even when held open. Nonempty gives OS_E_EXIST. */
os_err os_rmdir(const char *path);
/* Unlinks a name. Open and mapped handles still refer to the same object. */
os_err os_unlink_at(const os_dir *dir, const char *name);
os_err os_unlink(const char *path);

/* Atomic rename. REPLACE leaves holders of the old target on its old
 * contents. NOREPLACE returns OS_E_EXIST if the target exists and
 * OS_E_NOTSUP if the filesystem cannot provide atomic exclusion. */
typedef enum os_rename_mode { OS_RENAME_REPLACE = 1, OS_RENAME_NOREPLACE = 2 } os_rename_mode;
os_err os_rename_at(const os_dir *from_dir, const char *from, const os_dir *to_dir,
                    const char *to, os_rename_mode mode);
os_err os_rename(const char *from, const char *to, os_rename_mode mode);

/* Each iterator has an independent cursor and skips "." and "..". */
typedef struct os_dir_iter { void *dir; } os_dir_iter;
os_err os_dir_iter_open(os_dir_iter *it, const os_dir *dir);
/* On success *name is NULL at end, or borrowed until the iterator advances,
 * rewinds or closes. */
os_err os_dir_next(os_dir_iter *it, const char **name);
void os_dir_rewind(os_dir_iter *it);
void os_dir_iter_close(os_dir_iter *it);

bool os_path_is_absolute(const char *path);
/* Absolute path with symlinks resolved. OS_E_RANGE if cap is too small. */
os_err os_path_canonical(const char *path, char *out, size_t cap);
os_err os_cwd(char *out, size_t cap);
/* NUL-terminated local hostname on success, for local file URI classification.
 * A buffer too small for the name including the NUL returns OS_E_RANGE. */
os_err os_host_name(char *out, size_t cap);

/* Resolve a path without creating it. Append EDIT_APP_ID to the absolute
 * XDG_CONFIG_HOME or XDG_DATA_HOME. With a missing or relative XDG value,
 * use absolute HOME with .config or .local/share respectively. An absent
 * absolute root gives OS_E_NOENT. The result has no trailing slash. */
typedef enum os_app_dir_kind { OS_APP_CONFIG = 1, OS_APP_DATA = 2 } os_app_dir_kind;
os_err os_app_dir(os_app_dir_kind kind, char *out, size_t cap);
/* Absolute TMPDIR, otherwise /tmp. Strip trailing slashes except at /. */
os_err os_temp_dir(char *out, size_t cap);
/* Environment access must be serialized against mutation. NULL value unsets. */
os_err os_env_get(const char *name, char *out, size_t cap);
os_err os_env_set(const char *name, const char *value);

size_t os_page_size(void);
/* Alignment required for a file mapping's offset. */
size_t os_map_granularity(void);
uint64_t os_phys_mem_bytes(void);

/* Read-only private view of [offset, offset + len). The offset must be
 * aligned to os_map_granularity(). The length is positive, but may end
 * mid-page. Unmap releases the full page-rounded extent. Closing, renaming
 * or unlinking the file leaves the view intact. A file can shrink underneath
 * the view. Reading a page beyond its current end may raise SIGBUS. */
typedef struct os_map {
    const void *addr;
    size_t len;
} os_map;
os_err os_map_file(os_map *out, const os_file *f, uint64_t offset, size_t len);
void os_unmap(os_map *m);
void os_map_prefetch(const os_map *m, size_t off, size_t len);

/* Reservation uses lazily backed read/write pages. Commit validates the
 * page-aligned range. Decommit discards its backing and the next touch reads
 * zero. The caller tracks the reserved extent and releases it once. */
os_err os_vm_reserve(void **out, size_t len);
os_err os_vm_commit(void *addr, size_t len);
os_err os_vm_decommit(void *addr, size_t len);
void os_vm_release(void *addr, size_t len);

/* Fill the entire buffer with getrandom, restarting interrupted transfers. */
os_err os_random(void *buf, size_t n);

typedef struct os_proc { int pid; } os_proc;
typedef enum os_proc_how {
    OS_PROC_EXITED = 1, /* code is the exit status. */
    OS_PROC_ABNORMAL    /* code is the terminating signal. */
} os_proc_how;
typedef struct os_proc_status { os_proc_how how; int code; } os_proc_status;
/* Spawn argv[0] directly and pass argv and the environment to the child.
 * The child inherits the working directory and standard streams only. */
os_err os_proc_spawn(os_proc *out, const char *const argv[]);
/* Send SIGKILL to a child owned by p. The caller must still reap it. */
os_err os_proc_kill(os_proc *p);
/* A successful wait reaps the child and spends p. Negative timeout waits
 * indefinitely, zero polls. OS_E_TIMEDOUT leaves p live. Use one reaper. */
os_err os_proc_wait(os_proc *p, int64_t timeout_ns, os_proc_status *st);
uint64_t os_proc_id(void);
os_err os_proc_self_path(char *out, size_t cap);

/* Startup only. Raise the soft RLIMIT_NOFILE to the hard limit. Never lowers
 * it. If getrlimit succeeded, *soft (optional) receives the limit in force. */
os_err os_fd_limit_raise(uint64_t *soft);

#endif
