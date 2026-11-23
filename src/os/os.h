#ifndef OS_OS_H
#define OS_OS_H

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
 * and stdio conversion may allocate and are off the typing path.
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
    OS_E_INTR,
    OS_E_REFUSED, /* A local stream name exists with no listener (stale socket). */
    OS_E_OVERFLOW /* A kernel event queue overflowed and events were lost (os_watch_read). */
} os_err;

const char *os_err_name(os_err e);
int os_native_error(void);

/* The one assertion for the whole tree (CLAUDE.md, A15), compiled into every
 * build. Only for conditions no input, file content, resource exhaustion or
 * timing can produce; anything those can produce is a return code. A failure
 * writes "file:line: ASSERT failed: expression" to stderr with write(2) and
 * aborts. Both are async-signal-safe, so a signal handler may ASSERT.
 * An ASSERT argument has no side effects: do the call as a statement, then
 * ASSERT its result. */
#define ASSERT(x) ((x) ? (void)0 : os_assert_fail(__FILE__, __LINE__, #x))
_Noreturn void os_assert_fail(const char *file, int line, const char *expr);

/* The second check (CLAUDE.md, A15), for checks too expensive to ship.
 * OS_SAN_CHECKS is 1 exactly in a translation unit built with
 * AddressSanitizer or ThreadSanitizer (the san, tsan and fuzz lanes), else 0.
 * OS_SAN_ASSERT(x) is ASSERT(x) there; elsewhere x is still compiled, so it
 * stays type-checked, but never evaluated. Each use says why it does not
 * ship. */
#if defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_THREAD__)
#define OS_SAN_CHECKS 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer) || __has_feature(thread_sanitizer)
#define OS_SAN_CHECKS 1
#endif
#endif
#ifndef OS_SAN_CHECKS
#define OS_SAN_CHECKS 0
#endif
#define OS_SAN_ASSERT(x) (OS_SAN_CHECKS ? ASSERT(x) : (void)0)

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

/* Signals coalesce until drained. Any thread may signal; one owner waits for
 * it through an os_waitset (os_wake_fd) and drains. Drain before consuming the
 * announced state. */
typedef struct os_wake { int fd; } os_wake;
os_err os_wake_init(os_wake *w);
void os_wake_close(os_wake *w);
/* Async-signal-safe. */
void os_wake_signal(const os_wake *w);
void os_wake_drain(const os_wake *w);
int os_wake_fd(const os_wake *w); /* Borrowed: os_waitset_add. */

typedef enum os_kind {
    OS_KIND_FILE = 1,
    OS_KIND_DIR,
    OS_KIND_LINK,
    OS_KIND_OTHER,
    OS_KIND_SOCKET
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
/* Environment reads must be serialized against mutation. Shipped code reads
 * only product variables (tools/check_isolation.py); there is no setter, as
 * no shipped caller sets one (v2-0fv.20). */
os_err os_env_get(const char *name, char *out, size_t cap);

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

/* Mapped-file fault service (docs/decisions/wave1-os-fault-lease.md). A file
 * can shrink, or its device fail, under a view: Linux raises SIGBUS with code
 * BUS_ADRERR or BUS_OBJERR. For a registered view the process-wide handler
 * counts the registration's epoch, then replaces the view from the faulting
 * page through its end with zero pages, and the read completes with zeros.
 * Bytes before that page keep reading the file. A SIGBUS the service does not
 * repair terminates the process by SIGBUS, unless a callable handler was
 * installed before the service, which is then called with the original
 * signal, siginfo and ucontext, errno restored; its SA_RESETHAND is not
 * emulated, and a prior SIG_IGN counts as SIG_DFL. os is the only module that
 * sets a signal action; the service installs on first registration.
 *
 * Capacity: 256 slots, derived from the copy threshold, not inherited. Only
 * files at or above the copy threshold are mapped (v1: 256 MiB on this
 * machine class, (M) v1 src/file/file.c:10 and :139), so the 257th
 * simultaneously registered distinct mapping needs at least
 * 256 x 256 MiB = 64 GiB of open files (P); snapshots of one backing share its
 * slot. Whoever lowers L2 file's copy threshold re-derives this count. On
 * exhaustion register returns OS_E_NOSPC and the open or reload is refused
 * visibly (docs/decisions/wave1-L2-derivations.md Q12).
 * An id is a copyable value. Generation 0 is the absent id; issued
 * generations never repeat, so a stale id never reads another registration.
 * An id grants no access to bytes: readers hold the backing that owns the
 * view, and only its final release retires the id. None of these functions
 * allocate; none may be called from a signal handler. Only conditions an
 * input can produce are returned (A15): misuse is an ASSERT, including a
 * null, empty or unaligned view, an overlapping registration, an absent or
 * forged id, and a second retire. */
#define OS_FAULT_SLOTS 256

typedef struct os_fault_id {
    uint32_t slot;
    uint64_t gen;
} os_fault_id;

#define OS_FAULT_ID_INIT { .slot = 0, .gen = 0 }

/* The owner, before publishing readable bytes. The view is a live
 * read-only private os_map_file view, or a page-aligned part of one,
 * borrowed until retire; its page-rounded extent intersects no registered
 * one. OS_E_NOSPC when all OS_FAULT_SLOTS views are registered or no
 * generation is left to issue; *out is then the absent id. */
os_err os_fault_register(const os_map *view, os_fault_id *out);
/* Any thread, lock-free, on a copied issued id, concurrently with anything
 * else. On OS_OK *epoch is the registration's recovery count (0: pristine;
 * it never returns to 0). OS_E_NOENT once the owner has retired the id;
 * *epoch is then untouched. Poll once after the mapped reads a result
 * depends on and require 0: the poll orders the caller's earlier reads
 * before it, and since the epoch never decreases, 0 then means 0 at every
 * earlier read (advisor, piece Lines freeze; an early poll is optional). */
os_err os_fault_poll(os_fault_id id, uint64_t *epoch);
/* The owner, once, at final release after all byte access has ended.
 * Invalidates the id, waits for handlers already inside the view to finish
 * and frees the slot; the view may then be unmapped. */
void os_fault_retire(os_fault_id id);

/* Reservation uses lazily backed read/write pages. Commit validates the
 * page-aligned range. Decommit discards its backing and the next touch reads
 * zero. The caller tracks the reserved extent and releases it once. */
os_err os_vm_reserve(void **out, size_t len);
os_err os_vm_commit(void *addr, size_t len);
os_err os_vm_decommit(void *addr, size_t len);
void os_vm_release(void *addr, size_t len);

/* Fill the entire buffer with getrandom, restarting interrupted transfers. */
os_err os_random(void *buf, size_t n);

/* Child processes (spawn, kill, wait) were removed in v2-0fv.13, the process
 * id and executable path in v2-0fv.20: no module called them. The bead that
 * needs one adds it together with its client. */

/* The real user ID, for endpoint names and ownership checks. */
uint32_t os_user_id(void);

/* ---- local streams (v2-0fv.13) ----
 * Unix-domain stream endpoints. Every descriptor is close-on-exec and
 * nonblocking, and no call here waits: OS_E_AGAIN means nothing can move now,
 * so register the descriptor in an os_waitset and retry on readiness.
 * Transfers restart EINTR, and a write to a closed peer returns OS_E_IO
 * without raising SIGPIPE. A name is one component inside a pinned directory,
 * or, with dir NULL, a Linux abstract name (no filesystem entry; it ends with
 * the last listener). A name the kernel address cannot hold gives
 * OS_E_NAMETOOLONG. Not async-signal-safe. */
typedef struct os_stream { int fd; } os_stream;
#define OS_STREAM_INIT { -1 }
typedef struct os_listener { int fd; } os_listener;
#define OS_LISTENER_INIT { -1 }

/* Binds and listens with a backlog in [1, INT_MAX]. OS_E_EXIST: the name is
 * bound. In a directory, the bound name must then be a socket owned by
 * os_user_id() (else OS_E_ACCESS), and it is made owner-only (0600) through a
 * pinned reference, never by name; *bound (optional) receives that inode's
 * stat, so the owner can later unlink only that inode. On failure after the
 * bind, the name is unlinked if it still names the inode bound here. */
os_err os_listen_local(os_listener *out, const os_dir *dir, const char *name,
                       uint32_t backlog, os_stat *bound);
/* OS_E_AGAIN when no connection is queued. */
os_err os_listener_accept(os_listener *l, os_stream *out);
void os_listener_close(os_listener *l); /* Idempotent for an initialized handle. */
int os_listener_fd(const os_listener *l); /* Borrowed: os_waitset_add, diagnostics. */

/* Connects at once or fails (P: a nonblocking Linux AF_UNIX stream connect
 * completes or fails in the call, net/unix/af_unix.c unix_stream_connect).
 * OS_E_REFUSED: the name exists with no listener. OS_E_NOENT: no such name.
 * OS_E_AGAIN: the listener's backlog is full. */
os_err os_connect_local(os_stream *out, const os_dir *dir, const char *name);
/* A connected pair. */
os_err os_stream_pair(os_stream *a, os_stream *b);
/* Takes a stream socket descriptor, e.g. one end of a pair inherited across
 * fork, and makes it nonblocking and close-on-exec. *out owns fd on every
 * result; on error the caller closes it with os_stream_close, or forgets the
 * handle to keep fd. A descriptor that is not a socket gives OS_E_INVAL. */
os_err os_stream_adopt(os_stream *out, int fd);
/* A successful read with *got == 0 and n > 0 is end of stream. */
os_err os_stream_read(os_stream *s, void *buf, size_t n, size_t *got);
/* As read, leaving the bytes queued. */
os_err os_stream_peek(os_stream *s, void *buf, size_t n, size_t *got);
os_err os_stream_write(os_stream *s, const void *buf, size_t n, size_t *done);
/* The peer's user ID as the kernel recorded it at connect or pair time. */
os_err os_stream_peer_uid(const os_stream *s, uint32_t *uid);
void os_stream_close(os_stream *s); /* Idempotent for an initialized handle. */
int os_stream_fd(const os_stream *s); /* Borrowed: os_waitset_add, handoff. */

/* ---- the wait set: the one wait primitive (v2-0fv.13) ----
 * Members are descriptors, each with an interest and a caller tag, plus one
 * alarm: an absolute deadline on the os_now_ns clock. The set's own
 * descriptor is readable while a member is ready or the alarm has fired, so
 * a set nests in another set and carries its alarm with it.
 * Invariant (docs/decisions/v2-0fv.13.md): a thread blocks in os_waitset_wait
 * on one set only, its loop's root set; an owner polls a nested set with
 * deadline 0. Waiting does no I/O on any member: it reports readiness and the
 * member's owner reads. One thread owns a set; others may only make members
 * ready (os_wake_signal). Deadlines are absolute: 0 and any past value poll,
 * OS_DEADLINE_NEVER never expires. */
#define OS_DEADLINE_NEVER UINT64_MAX
#define OS_WAIT_ALARM UINT64_MAX /* Tag of the alarm event; no member may use it. */
enum {
    OS_WAIT_IN  = 1u << 0,
    OS_WAIT_OUT = 1u << 1,
    OS_WAIT_HUP = 1u << 2, /* Reported, never requested: peer hung up. */
    OS_WAIT_ERR = 1u << 3  /* Reported, never requested: descriptor error. */
};
typedef struct os_wait_event { uint64_t tag; unsigned events; } os_wait_event;
typedef struct os_waitset { int fd, alarm_fd; uint64_t alarm_ns; } os_waitset;
#define OS_WAITSET_INIT { -1, -1, UINT64_MAX }
os_err os_waitset_init(os_waitset *ws);
void os_waitset_close(os_waitset *ws); /* Idempotent for an initialized handle. */
/* interest is OS_WAIT_IN, OS_WAIT_OUT or both. A descriptor joins once
 * (OS_E_EXIST). Remove it before closing it. */
os_err os_waitset_add(os_waitset *ws, int fd, unsigned interest, uint64_t tag);
os_err os_waitset_modify(os_waitset *ws, int fd, unsigned interest, uint64_t tag);
void os_waitset_remove(os_waitset *ws, int fd);
/* Arms the one-shot alarm; OS_DEADLINE_NEVER disarms it and a past deadline
 * fires at once. Re-arming the armed deadline makes no call. A failure leaves
 * the previous alarm. */
os_err os_waitset_alarm(os_waitset *ws, uint64_t deadline_ns);
/* Waits for readiness or the deadline and fills up to cap events. *n == 0
 * only when the deadline has passed. A fired alarm is consumed, disarmed and
 * reported once as an OS_WAIT_ALARM event. Interrupted waits restart with the
 * time left; finite waits round up to whole milliseconds. Never allocates. */
os_err os_waitset_wait(os_waitset *ws, uint64_t deadline_ns, os_wait_event *ev,
                       size_t cap, size_t *n);
int os_waitset_fd(const os_waitset *ws); /* Borrowed: nest in an outer set. */

/* ---- directory-change watch (L2-0a, docs/decisions/L2-0a.md) ----
 * One kernel watch queue, owned by its caller (no global). A watched
 * directory reports changes to its entries and to itself, so watching a
 * file's parent covers the file and the parent. Event names are entry names;
 * an empty name is the directory itself. The watch reports, it does not
 * filter: a holder compares names. One thread owns a watch. Its descriptor is
 * nonblocking and close-on-exec. No call allocates.
 *
 * Holds are counted: adding a directory that is already watched (the same
 * inode, by any path) returns the same id and one more hold, and the kernel
 * watch ends only when the last hold is removed, so one holder's removal
 * never silences another's. The hold table is caller storage given at init;
 * its size is the caller's derivation. */
typedef uint32_t os_watch_id;
typedef struct os_watch_slot { int wd; uint32_t holds; } os_watch_slot; /* Private. */
/* (P) A queued kernel record is a 16-byte header plus a NUL-padded name of
 * at most NAME_MAX + 1 bytes (inotify(7)), so 272 bytes hold any one record.
 * (G) 4096 bytes is v1's read buffer (v1 file_posix.c:96), inherited: it
 * bounds the records one read takes, not correctness. */
#define OS_WATCH_BUF 4096u
typedef struct os_watch {
    int fd;
    os_watch_slot *slots;
    uint32_t cap, at, len;
    _Alignas(8) unsigned char buf[OS_WATCH_BUF];
} os_watch;
#define OS_WATCH_INIT { .fd = -1 }

typedef enum os_watch_what {
    OS_WATCH_CREATE = 1, /* A name was created in the directory. */
    OS_WATCH_DELETE,     /* A name was removed; "": the directory was removed or unmounted. */
    OS_WATCH_MOVED_OUT,  /* A name was renamed away; "": the directory itself moved. */
    OS_WATCH_MOVED_IN,   /* A name was renamed into the directory, maybe over another. */
    OS_WATCH_MODIFY,     /* An entry's contents changed, or a writer closed it. */
    OS_WATCH_ATTRIB,     /* Metadata of an entry, or with "" of the directory, changed. */
    OS_WATCH_ENDED       /* The kernel dropped this watch: it reports nothing more.
                          * The holds stay until removed; add the path again to resume. */
} os_watch_what;
/* name is borrowed until the next os_watch_read or os_watch_close. */
typedef struct os_watch_event {
    os_watch_id id;
    os_watch_what what;
    const char *name;
} os_watch_event;

/* slots[0, cap) is borrowed until close; cap > 0. */
os_err os_watch_init(os_watch *w, os_watch_slot *slots, uint32_t cap);
void os_watch_close(os_watch *w); /* Idempotent for an initialized handle. */
int os_watch_fd(const os_watch *w); /* Borrowed: os_waitset_add with OS_WAIT_IN. */
/* Watches the directory at path (symlinks followed). OS_E_NOTDIR: not a
 * directory. OS_E_NOSPC: the kernel's per-user watch limit, or no free slot
 * for a directory not yet held (the new kernel watch is then withdrawn). */
os_err os_watch_add(os_watch *w, const char *path, os_watch_id *id);
/* Drops one hold of a held id; the last one ends the kernel watch, and
 * events still queued for it are discarded. */
void os_watch_remove(os_watch *w, os_watch_id id);
/* Fills up to cap events, making at most one read(2), and never waits.
 *   OS_OK with *n < cap: nothing undelivered is left in the object; the
 *     descriptor's readiness covers what the kernel still queues.
 *   OS_OK with *n == cap: more may be held here: call again before waiting.
 *   OS_E_AGAIN: nothing pending (*n == 0).
 *   OS_E_OVERFLOW: the kernel queue overflowed, so changes after the *n
 *     delivered events were lost: recheck every watched name, and call again
 *     before waiting, as events after the overflow may follow.
 *   OS_E_IO: a malformed record (native error 0), or the read's own error;
 *     the *n events before it stand, and the object is drained. */
os_err os_watch_read(os_watch *w, os_watch_event *ev, size_t cap, size_t *n);

/* ---- metadata copy (L2-0a, docs/decisions/L2-0a.md) ----
 * Gives dst src's owner and src's whole extended-attribute set: every name
 * the process can list, ACLs (system.posix_acl_*) and security labels
 * included, with no namespace allowlist. In order:
 *   1. Owner: fchown dst to src's uid and gid, only when they differ (an
 *      unchanged owner needs no privilege). It precedes the xattrs because a
 *      change of owner clears security.capability.
 *   2. Names on dst absent on src are removed (inherited ones, e.g. from a
 *      default ACL).
 *   3. Each src name is set on dst. A set that fails where dst already holds
 *      the identical value counts as copied (an immutable label that already
 *      matches); a different value returns the set's error.
 * When neither descriptor supports xattrs there is no state to copy; when
 * only one does, OS_E_NOTSUP. Every refusal is the failing call's error
 * (OS_E_ACCESS, OS_E_NOTSUP, OS_E_RANGE for a list or value past the kernel
 * limit, OS_E_NOENT for a name removed from src meanwhile, OS_E_NOSPC, ...);
 * a malformed name list is OS_E_IO with native error 0. On failure dst's
 * metadata is partly copied: the caller discards dst. Never allocates: the
 * caller lends the scratch, which holds no state between calls. */
/* (P) The kernel caps a name list at XATTR_LIST_MAX and a value at
 * XATTR_SIZE_MAX, both 65536 bytes (include/uapi/linux/limits.h; E2BIG past
 * them, fs/xattr.c listxattr/getxattr); os_posix.c asserts both equal
 * OS_XATTR_MAX at compile time. A buffer of that size takes any list or value
 * in one call, with no size probe. The copy needs three at once: src's list
 * for the whole call, dst's list (then each src value, once the removals are
 * done), and dst's value for the matching-label comparison. So the scratch is
 * (P) 3 x 65536 B = 196608 B = 192 KiB. */
#define OS_XATTR_MAX 65536u
typedef struct os_xattr_scratch {
    char names[OS_XATTR_MAX];          /* src's name list */
    char a[OS_XATTR_MAX];              /* dst's name list, then src's value */
    char b[OS_XATTR_MAX];              /* dst's current value */
} os_xattr_scratch;
os_err os_file_copy_metadata(os_file *dst, const os_file *src, os_xattr_scratch *scratch);

/* Startup only. Raise the soft RLIMIT_NOFILE to the hard limit. Never lowers
 * it. If getrlimit succeeded, *soft (optional) receives the limit in force. */
os_err os_fd_limit_raise(uint64_t *soft);

#endif
