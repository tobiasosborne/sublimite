/* os.h - the operating-system contract (edit-177r.2, W0.2). Frozen spec,
 * round 2 (after the Codex review docs/reviews/edit-177r.2.md).
 *
 * Every OS call a module makes outside the platform layers (src/x11,
 * src/wayland, later src/win32) goes through this header once that module is
 * migrated (W0.4, W2). The Linux implementation (src/os/os_posix.c) is today's
 * calls; the Windows implementation (src/os/os_win.c, W0.3) is Win32/NT. The
 * conformance suite tests/os_test.c uses only this header, C11 and base.h, and
 * runs unchanged on both. docs/decisions/edit-177r.2.md maps every POSIX call
 * family in the tree to an entry here or to "stays platform-private", and
 * gives the migration rules where an entry's outcome differs from today's call.
 *
 * Rules for every entry:
 * - Errors are an os_err return code. The native code (errno on POSIX,
 *   GetLastError() on Windows) is left in place for diagnostics and is read
 *   with os_native_error() immediately after the failing call.
 * - Paths and names are NUL-terminated UTF-8 at the API on both platforms.
 *   The Windows implementation converts to UTF-16 internally and adds the
 *   \\?\ long-path prefix itself (relative paths are resolved against the
 *   current directory first); callers never see UTF-16.
 * - Allocation. No entry allocates from the C heap except these, which may:
 *   os_thread_start (thread stack and TLS), os_env_set (the environment
 *   block), os_dir_iter_open (DIR / its record buffer), os_proc_spawn,
 *   os_file_to_stdio (the FILE), os_rmutex_init on Windows (only if the
 *   implementation cannot avoid critical-section debug info), and the C
 *   library underneath os_path_canonical for paths longer than its stack
 *   buffer. The typing-path set never allocates, cold or contended, and the
 *   suite counts it (CLAUDE.md law 2): the clocks, os_thread_self, os_yield,
 *   mutex and recursive-mutex lock/unlock, cond signal/broadcast, wake
 *   signal/drain, os_file_pread, os_file_pwrite and os_file_pwrite_once.
 * - Retry policy, per entry:
 *   . os_file_pwrite_once makes exactly one attempt: an interrupted attempt
 *     returns OS_E_INTR (POSIX EINTR; Windows has none) and is not retried.
 *   . os_file_close / os_dir_close / os_wake_close never retry (Linux
 *     releases the descriptor even when close reports EINTR).
 *   . os_sleep_ns, os_wake_wait and os_proc_wait restart with the time left.
 *   . Every other entry restarts EINTR internally until the call completes.
 *   . No entry retries a short transfer: read/write/pread/pwrite report the
 *     count the one completed transfer moved, and the caller loops.
 * - Transfers are complete on return. Every transfer has completed, or been
 *   cancelled and reaped, before its entry returns, including on error, so
 *   the caller may reuse or free the buffer at once. Windows opens every file
 *   without FILE_FLAG_OVERLAPPED; ERROR_IO_PENDING is handled inside the
 *   implementation (wait for completion) and never reaches the caller.
 * - Every handle is opened non-inheritable (O_CLOEXEC / bInheritHandle FALSE)
 *   and, on Windows, with FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE
 *   so that rename and delete of open or mapped files behave as on POSIX.
 * - Struct fields are private to the implementation. Callers allocate the
 *   structs (no malloc in the layer) and use only the functions. The _WIN32
 *   layouts below are placeholders that W0.3 may change without reopening
 *   this contract; they exist so the header compiles for both targets.
 * - Thread safety: distinct handles may be used concurrently from any thread.
 *   One handle is not used concurrently unless its comment says so.
 */
#ifndef EDIT_OS_H
#define EDIT_OS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#if !defined(_WIN32)
#include <pthread.h>
#endif

/* ---- errors ------------------------------------------------------------- */
/* POSIX / Win32 mapping (implementation tables, both directions documented in
 * the decision doc):
 *   NOENT        ENOENT                    ERROR_FILE_NOT_FOUND, ERROR_PATH_NOT_FOUND
 *   EXIST        EEXIST, ENOTEMPTY          ERROR_FILE_EXISTS, ERROR_ALREADY_EXISTS,
 *                                           ERROR_DIR_NOT_EMPTY
 *   ACCESS       EACCES, EPERM, EROFS       ERROR_ACCESS_DENIED, ERROR_WRITE_PROTECT
 *   BUSY         EWOULDBLOCK from a lock,   ERROR_SHARING_VIOLATION, ERROR_LOCK_VIOLATION,
 *                EBUSY, ETXTBSY             ERROR_USER_MAPPED_FILE
 *   LOOP         ELOOP (NOFOLLOW on a link) ERROR_STOPPED_ON_SYMLINK, name surrogate seen
 *   NOTDIR       ENOTDIR                    ERROR_DIRECTORY
 *   ISDIR        EISDIR                     (opening a directory as a file)
 *   XDEV         EXDEV                      ERROR_NOT_SAME_DEVICE
 *   NOSPC        ENOSPC, EDQUOT             ERROR_DISK_FULL, ERROR_HANDLE_DISK_FULL
 *   NAMETOOLONG  ENAMETOOLONG, or > OS_PATH_MAX / OS_NAME_MAX after conversion
 *   INVAL        EINVAL, bad argument       ERROR_INVALID_PARAMETER, ERROR_INVALID_NAME
 *   NOTSUP       EOPNOTSUPP, ENOSYS, ENOTTY, EINVAL from renameat2 flags / FICLONE
 *                                           ERROR_NOT_SUPPORTED, ERROR_INVALID_FUNCTION
 *   AGAIN        EAGAIN (non-lock)          (none: pending I/O is completed inside)
 *   TIMEDOUT     a wait timed out           WAIT_TIMEOUT
 *   NOMEM        ENOMEM                     ERROR_NOT_ENOUGH_MEMORY, ERROR_OUTOFMEMORY
 *   RANGE        a size or range the caller passed does not fit: an output buffer
 *                too small, or a mapping range past the end of the file (no native code)
 *   IO           anything else (EIO, ESTALE, ...); the native code says which
 *   INTR         EINTR, only from os_file_pwrite_once (single-attempt entries) */
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

/* Static string, e.g. "OS_E_NOENT"; "OS_E_?" for an unknown value. */
const char *os_err_name(os_err e);
/* errno (POSIX) or GetLastError() (Windows) of the calling thread. Valid
 * immediately after an os call that returned an error other than OS_E_RANGE
 * or OS_E_TIMEDOUT; 0 when the error was detected by the layer itself. */
int os_native_error(void);

/* ---- limits ------------------------------------------------------------- */
/* (P) Linux PATH_MAX is 4096 bytes including the NUL (<linux/limits.h>);
 * modules already size path buffers to it. A Windows path whose UTF-8 form
 * does not fit fails with OS_E_NAMETOOLONG. Paths longer than Windows'
 * MAX_PATH (260 UTF-16 units) work on both: the \\?\ prefix lifts that limit. */
#define OS_PATH_MAX 4096u
/* (P) One name component: Linux NAME_MAX is 255 bytes; NTFS allows 255 UTF-16
 * units, at most 3 UTF-8 bytes each (a surrogate pair is 2 units -> 4 bytes),
 * so 255 * 3 = 765 bytes + NUL = 766, rounded up to 768. */
#define OS_NAME_MAX 768u
#if defined(_WIN32)
#define OS_PATH_SEP '\\'
#else
#define OS_PATH_SEP '/'
#endif

/* ---- clocks, sleep ------------------------------------------------------ */
/* Monotonic nanoseconds (CLOCK_MONOTONIC / QueryPerformanceCounter). The same
 * clock every module's *_ns deadline already uses. Never allocates. */
uint64_t os_now_ns(void);
/* CPU-time ACCOUNTING: CPU time consumed by the calling thread, ns
 * (CLOCK_THREAD_CPUTIME_ID / GetThreadTimes kernel + user). For measurement
 * and reports (G11's cancellation CPU, lineidx_cancel_cpu_ns), not for
 * enforcing short slices: its resolution is platform dependent. Never
 * allocates. */
uint64_t os_thread_cpu_ns(void);
/* Granularity of os_thread_cpu_ns in ns: the smallest step it can move by.
 * Linux: clock_getres(CLOCK_THREAD_CPUTIME_ID) (1 ns, M). Windows: the
 * GetThreadTimes update step, the scheduler tick, 15.625 ms = 156250 FILETIME
 * units x 100 ns (M, Codex on native Windows; P: 1/64 s). The implementation
 * reports what it measures at first use; the suite checks that no observed
 * step is finer than the value reported. */
uint64_t os_thread_cpu_resolution_ns(void);
/* SLICE DEADLINES: the clock for enforcing short per-call CPU budgets on the
 * calling thread (lineidx's 500,000 ns UI slices, src/lineidx/lineidx.c:20).
 * Over any interval it advances at least as much as os_thread_cpu_ns, and it
 * resolves steps far finer than the smallest such budget.
 * Linux: CLOCK_THREAD_CPUTIME_ID, the same clock lineidx reads today (no
 * behaviour change). Windows: QueryPerformanceCounter, because the thread
 * CPU clock moves in 15.625 ms steps and cannot end a 0.5 ms slice. Elapsed
 * time bounds CPU time from above, so a slice measured this way ends no later
 * than on Linux; when the thread is preempted it ends earlier (less work per
 * slice, never more latency). Never allocates. */
uint64_t os_slice_ns(void);
/* Wall clock, ns since 1970-01-01 UTC (CLOCK_REALTIME / GetSystemTimePreciseAsFileTime).
 * Used only for unique names, never for deadlines. */
uint64_t os_wall_ns(void);
/* Sleeps at least ns (may sleep longer). Windows: a high-resolution waitable
 * timer (CREATE_WAITABLE_TIMER_HIGH_RESOLUTION) so sub-millisecond sleeps, which
 * modules use for backoff, are not rounded up to the scheduler tick. */
void os_sleep_ns(uint64_t ns);
/* sched_yield / SwitchToThread. Never allocates. */
void os_yield(void);

/* ---- threads, mutex, condition, once ----------------------------------- */
/* Caller-owned, must stay alive and unmoved until os_thread_join returns.
 * os_thread_start may allocate (stack, TLS); never on the typing path. */
typedef struct os_thread {
#if defined(_WIN32)
    void *handle;
#else
    pthread_t thread;
#endif
    void (*fn)(void *);
    void *arg;
} os_thread;
os_err os_thread_start(os_thread *t, void (*fn)(void *), void *arg);
os_err os_thread_join(os_thread *t);
/* Identifier of the calling thread, unique among live threads (an exited
 * thread's value may be reused); nonzero. ((uintptr_t)pthread_self() /
 * GetCurrentThreadId()). Never allocates. */
uint64_t os_thread_self(void);

/* Non-recursive mutex (pthread default mutex / SRWLOCK exclusive). Locking it
 * again from the owning thread is undefined: use os_rmutex for that. */
typedef struct os_mutex {
#if defined(_WIN32)
    void *srw;
#else
    pthread_mutex_t m;
#endif
} os_mutex;
#if defined(_WIN32)
#define OS_MUTEX_INIT { 0 }
#else
#define OS_MUTEX_INIT { PTHREAD_MUTEX_INITIALIZER }
#endif
os_err os_mutex_init(os_mutex *m);
void   os_mutex_destroy(os_mutex *m);
void   os_mutex_lock(os_mutex *m);
void   os_mutex_unlock(os_mutex *m);

/* Recursive mutex: the owning thread may lock it again; it is released when
 * unlock has been called as many times as lock. piece's pool lock needs this
 * (piece_create, src/piece/piece.c:1116: node_unref holds pool_mu and reaches
 * pool_free, which locks it again).
 * Linux: pthread mutex of type PTHREAD_MUTEX_RECURSIVE. Windows:
 * CRITICAL_SECTION (InitializeCriticalSectionEx with
 * CRITICAL_SECTION_NO_DEBUG_INFO, so init does not allocate debug info).
 * No static initialiser (pthread has no portable recursive one). Lock and
 * unlock never allocate. Not usable with os_cond. */
typedef struct os_rmutex {
#if defined(_WIN32)
    void *cs[5];   /* (P) sizeof(CRITICAL_SECTION) on x64 = 40 bytes:
                      DebugInfo 8 + LockCount 4 + RecursionCount 4 +
                      OwningThread 8 + LockSemaphore 8 + SpinCount 8 */
#else
    pthread_mutex_t m;
#endif
} os_rmutex;
os_err os_rmutex_init(os_rmutex *m);
void   os_rmutex_destroy(os_rmutex *m);
void   os_rmutex_lock(os_rmutex *m);
void   os_rmutex_unlock(os_rmutex *m);

/* Condition variable (pthread_cond_t / CONDITION_VARIABLE). Spurious wakeups
 * are possible on both platforms: wait in a predicate loop. signal and
 * broadcast never allocate. */
typedef struct os_cond {
#if defined(_WIN32)
    void *cv;
#else
    pthread_cond_t c;
#endif
} os_cond;
os_err os_cond_init(os_cond *c);
void   os_cond_destroy(os_cond *c);
void   os_cond_wait(os_cond *c, os_mutex *m);
void   os_cond_signal(os_cond *c);
void   os_cond_broadcast(os_cond *c);

/* One-time initialisation (pthread_once / InitOnceExecuteOnce). */
typedef struct os_once {
#if defined(_WIN32)
    void *init_once;
#else
    pthread_once_t once;
#endif
} os_once;
#if defined(_WIN32)
#define OS_ONCE_INIT { 0 }
#else
#define OS_ONCE_INIT { PTHREAD_ONCE_INIT }
#endif
void os_once_run(os_once *o, void (*fn)(void));

/* ---- wake handles (eventfd replacement) -------------------------------- */
/* A level-triggered, coalescing wake-up: signalled until drained. Any thread
 * may signal; one owner thread waits and drains. The platform event loop waits
 * on the native object (the accessors below are for platform code only:
 * src/x11, src/wayland, src/win32 and *_posix.c / *_win.c files).
 * Linux: eventfd(EFD_NONBLOCK). Windows: manual-reset event (SetEvent /
 * ResetEvent), so a MsgWaitForMultipleObjectsEx wakeup does not consume it.
 * Protocol: drain BEFORE consuming the state the wake announces, so a signal
 * that races the drain is never lost. */
typedef struct os_wake {
#if defined(_WIN32)
    void *event;
#else
    int fd;
#endif
} os_wake;
os_err os_wake_init(os_wake *w);
void   os_wake_close(os_wake *w);
/* Thread-safe; never allocates; async-signal-safe on POSIX. */
void   os_wake_signal(const os_wake *w);
void   os_wake_drain(const os_wake *w);
/* Waits until signalled or timeout_ns elapses (< 0: forever, 0: poll). Does
 * not drain. OS_OK when signalled, OS_E_TIMEDOUT otherwise. Linux rounds the
 * timeout up to whole milliseconds (poll). */
os_err os_wake_wait(const os_wake *w, int64_t timeout_ns);
#if defined(_WIN32)
void *os_wake_handle(const os_wake *w);   /* HANDLE */
#else
int   os_wake_fd(const os_wake *w);
#endif

/* ---- file metadata ------------------------------------------------------ */
/* Kinds. Following (no NOFOLLOW) classifies the object a link leads to;
 * NOFOLLOW classifies the name itself.
 * Windows reparse points: only NAME SURROGATES (IsReparseTagNameSurrogate:
 * IO_REPARSE_TAG_SYMLINK, IO_REPARSE_TAG_MOUNT_POINT junctions, and other
 * surrogate tags) are OS_KIND_LINK. A non-directory whose reparse tag is not
 * a name surrogate and not a special object below is an ordinary data file:
 * OS_KIND_FILE, readable through the filter that owns the tag (WOF/compact
 * compression IO_REPARSE_TAG_WOF, deduplication IO_REPARSE_TAG_DEDUP, cloud
 * files such as OneDrive IO_REPARSE_TAG_CLOUD_*, HSM tags). Tobias's files
 * live mostly on Windows drives (decision 1), where OneDrive placeholders and
 * compact-compressed files are common. OS_KIND_OTHER is reserved for objects
 * that are not data files: AF_UNIX sockets (IO_REPARSE_TAG_AF_UNIX), the WSL
 * special-file tags (IO_REPARSE_TAG_LX_FIFO / LX_CHR / LX_BLK), devices and
 * named pipes; on POSIX fifo, socket, character and block devices. */
typedef enum os_kind {
    OS_KIND_FILE = 1,   /* regular file, including reparse-backed data files */
    OS_KIND_DIR,
    OS_KIND_LINK,       /* symlink; Windows: name-surrogate reparse point (symlink, junction) */
    OS_KIND_OTHER       /* fifo, socket, device; Windows also the special reparse tags above */
} os_kind;

/* Identity of a file object: equal identities name the same file.
 * Linux: (st_dev, st_ino, 0). Windows: FILE_ID_INFO (VolumeSerialNumber,
 * 128-bit FileId). */
typedef struct os_identity {
    uint64_t volume;
    uint64_t index[2];
} os_identity;
bool os_identity_equal(const os_identity *a, const os_identity *b);

/* Timestamps are SIGNED ns since 1970-01-01 UTC, so pre-epoch times survive
 * (a native mtime of -1 s is -1000000000). (P) int64 ns spans
 * 2^63 ns = 9.22e18 ns / 3.156e16 ns per year = +-292 years around 1970;
 * native times outside that range saturate to INT64_MIN / INT64_MAX.
 * (uint64_t)mtime_ns reproduces file_id_from_stat's unsigned encoding
 * (src/file/file.c) for every time in range; journal capture's "reject a
 * negative mtime" (src/journal/journal.c) is mtime_ns < 0. Windows converts
 * FILETIME (100 ns since 1601) by subtracting 116444736000000000 units
 * (P: 11644473600 s from 1601-01-01 to 1970-01-01). */
typedef struct os_stat {
    os_identity id;
    os_kind kind;
    uint64_t size;
    int64_t mtime_ns;   /* last data write */
    int64_t ctime_ns;   /* last status change (st_ctim / FILE_BASIC_INFO.ChangeTime) */
    uint32_t perm;      /* st_mode & 07777; Windows synthesises 0666, or 0444 if read-only (dirs 0777/0555) */
    uint32_t uid, gid;  /* POSIX owner; 0 on Windows (ownership is a security descriptor) */
} os_stat;

/* ---- file handles ------------------------------------------------------- */
typedef struct os_file {
#if defined(_WIN32)
    void *handle;
#else
    int fd;
#endif
} os_file;
#if defined(_WIN32)
#define OS_FILE_INIT { (void *)(intptr_t)-1 }
#else
#define OS_FILE_INIT { -1 }
#endif
bool os_file_is_open(const os_file *f);

/* Exactly one of READ, WRITE, RDWR. */
enum {
    OS_OPEN_READ     = 1u << 0,  /* read only (GENERIC_READ) */
    OS_OPEN_RDWR     = 1u << 1,  /* read and write */
    OS_OPEN_CREATE   = 1u << 2,  /* create if missing; POSIX mode 0600 (owner only),
                                    Windows inherits the directory's ACL */
    OS_OPEN_EXCL     = 1u << 3,  /* with CREATE: fail OS_E_EXIST if the name exists,
                                    whatever it is, a link (dangling or not) included */
    OS_OPEN_NOFOLLOW = 1u << 4,  /* final component a link -> OS_E_LOOP, except that
                                    CREATE|EXCL on an existing name gives OS_E_EXIST */
    OS_OPEN_NONBLOCK = 1u << 5,  /* POSIX: do not block opening FIFOs/devices; no-op on Windows */
    OS_OPEN_WRITE    = 1u << 6,  /* write only (O_WRONLY / GENERIC_WRITE): needs no read
                                    permission; reads through it fail */
    OS_OPEN_TRUNC    = 1u << 7   /* with WRITE or RDWR: truncate an existing file to 0 as
                                    part of the open, atomically (O_TRUNC; Windows
                                    CREATE_ALWAYS with CREATE, TRUNCATE_EXISTING without).
                                    Windows CREATE_ALWAYS refuses an existing hidden or
                                    system file: OS_E_ACCESS. */
};
/* A directory opened as a file fails OS_E_ISDIR: the name exists and is not a
 * file, which callers classify like a non-FILE kind after a successful open
 * (migration rule, decision doc §9). Sequential position starts at 0.
 * NOFOLLOW on Windows: open with FILE_FLAG_OPEN_REPARSE_POINT; a name
 * surrogate -> OS_E_LOOP; any other reparse tag is re-opened without the flag
 * so its filter serves the data, and the two opens must have the same
 * identity (else retried). */
os_err os_file_open(os_file *out, const char *path, unsigned flags);
/* Close; closing a handle that is not open is a no-op. Leaves *f not open. */
void   os_file_close(os_file *f);
/* A second handle to the same open file (dup / DuplicateHandle). Duplicates
 * share the sequential position and the lock (below). */
os_err os_file_dup(os_file *out, const os_file *f);

/* One transfer of at most n bytes at the sequential position, which advances.
 * *got / *done may be less than n; read with *got == 0 and OS_OK is end of file. */
os_err os_file_read(os_file *f, void *buf, size_t n, size_t *got);
os_err os_file_write(os_file *f, const void *buf, size_t n, size_t *done);
/* Positioned transfer at a 64-bit offset (offsets beyond 4 GiB are exact on
 * both). After it the sequential position is UNSPECIFIED (Windows moves it):
 * do not mix sequential and positioned I/O on one handle. Safe to call
 * concurrently on one handle from several threads (Windows serialises them on
 * the synchronous file object). Retries EINTR. */
os_err os_file_pread(os_file *f, void *buf, size_t n, uint64_t offset, size_t *got);
os_err os_file_pwrite(os_file *f, const void *buf, size_t n, uint64_t offset, size_t *done);
/* Single-attempt positioned write for the journal's typing path
 * (journal cache_write, src/journal/journal.c:570; contract in journal.h:245):
 * exactly one write system call, no retry of any kind, no allocation, no
 * lock, no sync. *done is what that one attempt wrote (0 on error):
 *   OS_OK with *done == n  the whole record reached the kernel / cache;
 *   OS_OK with *done < n   a short write (the caller treats it as sticky IO);
 *   OS_E_INTR              POSIX EINTR before anything was written;
 *   any other error        as for os_file_pwrite.
 * Windows: one WriteFile with the offset in an OVERLAPPED on the synchronous
 * handle; there is no EINTR there. */
os_err os_file_pwrite_once(os_file *f, const void *buf, size_t n, uint64_t offset, size_t *done);

os_err os_file_stat(const os_file *f, os_stat *st);
/* ftruncate / FileEndOfFileInfo. Windows refuses to shrink a file that has a
 * mapped view (any process): OS_E_BUSY. */
os_err os_file_set_size(os_file *f, uint64_t size);
/* Marks the file sparse, so ranges never written (growth by set_size or by
 * writing past the end) take no space and cost no zero-fill. Linux: regular
 * files on ext4, xfs, btrfs and tmpfs are sparse already, OS_OK, no call.
 * Windows: FSCTL_SET_SPARSE (NTFS, ReFS); OS_E_NOTSUP where the filesystem
 * cannot (FAT, exFAT). Without it NTFS zero-fills up to a write past the end. */
os_err os_file_set_sparse(os_file *f);
/* Durability: sync = data + metadata (fsync), sync_data = data and the size
 * needed to read it back (fdatasync). Windows: both FlushFileBuffers. */
os_err os_file_sync(os_file *f);
os_err os_file_sync_data(os_file *f);
/* Permission bits (fchmod). Windows honours only the owner-write bit (clear ->
 * FILE_ATTRIBUTE_READONLY); other bits are accepted and ignored. */
os_err os_file_set_mode(os_file *f, uint32_t perm);
/* Reflink the whole of src into dst (ioctl FICLONE). OS_E_NOTSUP where the
 * filesystem or platform cannot (always on NTFS); the caller then copies. */
os_err os_file_clone(os_file *dst, const os_file *src);
/* Hands the open file to C stdio: on OS_OK *out is a FILE that owns the
 * handle (fclose closes it) and *f is left not open. mode is an fopen mode
 * matching the open flags ("rb", "wb", "r+b"). For callers whose API takes a
 * FILE *, e.g. trace_dump (src/trace/trace.h:79, the EDIT_TRACE_DUMP site in
 * src/main.c): os_file_open(path, WRITE | CREATE | TRUNC) then this, so the
 * path is UTF-8 on Windows too. Linux fdopen; Windows _open_osfhandle +
 * _fdopen. May allocate (the FILE). On error *f is unchanged and still open. */
os_err os_file_to_stdio(os_file *f, const char *mode, FILE **out);

/* Advisory whole-file locks with flock semantics. A lock belongs to the OPEN
 * FILE: the object a handle and all its duplicates (os_file_dup) share, as
 * flock's open file description and Windows' file object do. Every other
 * open (same or other process) is excluded. A duplicate may unlock it; closing
 * one of several duplicates does not release it; it is released by unlock,
 * when the LAST duplicate closes, or by process death. This is what the
 * session-lease handoff relies on (src/editor/recovery.c:754: the editor
 * duplicates the creator's locked descriptor, then the creator closes its
 * copy; the lease must hold throughout).
 * An open file holds at most one lock: unlock before changing its kind.
 * Non-blocking only: OS_E_BUSY when held elsewhere.
 * Windows: LockFileEx on one byte at an offset above any reachable file size,
 * so the mandatory byte-range lock never covers data and other handles can
 * still read and write the file; DuplicateHandle shares the file object, and
 * the filesystem releases the lock at the file object's cleanup, i.e. when
 * its last handle closes (confirmed on native NTFS by the Codex review). */
typedef enum os_lock { OS_LOCK_SHARED = 1, OS_LOCK_EXCLUSIVE = 2 } os_lock;
os_err os_file_try_lock(os_file *f, os_lock kind);
void   os_file_unlock(os_file *f);

#if defined(_WIN32)
void  *os_file_handle(const os_file *f);  /* HANDLE, platform code only */
#else
int    os_file_fd(const os_file *f);      /* platform code only */
os_file os_file_from_fd(int fd);          /* adopts fd; platform code only */
#endif

/* ---- directories -------------------------------------------------------- */
/* A pinned directory: *_at operations resolve names relative to the directory
 * object itself, not to its path, so renaming or replacing the path does not
 * redirect them (docs/decisions/P1.7c.md §7).
 * Linux: O_RDONLY | O_DIRECTORY descriptor and openat/unlinkat/renameat2/...
 * Windows: a directory HANDLE (FILE_FLAG_BACKUP_SEMANTICS, share R|W|D) and
 * handle-relative NT calls (NtCreateFile with ObjectAttributes.RootDirectory,
 * NtSetInformationFile FileRenameInformationEx / FileDispositionInformationEx
 * with RootDirectory and POSIX semantics). Names given to *_at are single
 * components ("." names the directory itself); never a path with separators. */
typedef struct os_dir {
#if defined(_WIN32)
    void *handle;
    void *lock;     /* identity-keyed lock file of os_dir_try_lock, or -1 */
#else
    int fd;
#endif
} os_dir;
#if defined(_WIN32)
#define OS_DIR_INIT { (void *)(intptr_t)-1, (void *)(intptr_t)-1 }
#else
#define OS_DIR_INIT { -1 }
#endif
bool   os_dir_is_open(const os_dir *d);
/* Follows links in path (O_DIRECTORY); a non-directory fails OS_E_NOTDIR. */
os_err os_dir_open(os_dir *out, const char *path);
os_err os_dir_open_at(os_dir *out, const os_dir *dir, const char *name);
/* Closes the directory and releases a lock it holds. */
void   os_dir_close(os_dir *d);
/* Makes completed renames, creations and unlinks in this directory durable
 * (fsync of the directory). Windows has no directory flush: a documented
 * no-op that returns OS_OK (perf/01-perf-target.md §2.8, the provisional gap). */
os_err os_dir_sync(os_dir *d);
/* Lock on the directory OBJECT, flock semantics like os_file_try_lock: keyed
 * by the directory's identity, never by its path, so a directory renamed away
 * keeps its lock and a new directory created at the old path is a different,
 * unlocked object (journal_retire_directory, src/journal/retire.c:377, relies
 * on an inode lock with no replaceable lock-file name). Nothing is created
 * inside the directory, no write permission on it is needed, and a user's
 * entry of any name in it is never hidden, touched or removed.
 * Linux: flock on the directory descriptor.
 * Windows (decision doc §8.2, round 2): directories cannot be byte-locked, so
 * the lock is LockFileEx (the same high byte) on a per-user lock file named by
 * the identity, %LOCALAPPDATA%\sublimite\dirlocks\<volume>-<index1><index0>
 * (hex, 16 + 32 digits), created on first use, opened with share READ|WRITE
 * but not DELETE so it cannot be renamed or deleted (and the exclusion split)
 * while anyone holds it open, and never deleted. Its handle is kept in the
 * os_dir; os_dir_unlock unlocks it, os_dir_close and process death close it. */
os_err os_dir_try_lock(os_dir *d, os_lock kind);
void   os_dir_unlock(os_dir *d);

enum { OS_STAT_NOFOLLOW = 1u << 0 };
os_err os_stat_at(const os_dir *dir, const char *name, unsigned flags, os_stat *st);
os_err os_stat_path(const char *path, unsigned flags, os_stat *st);
os_err os_file_open_at(os_file *out, const os_dir *dir, const char *name, unsigned flags);
/* An unnamed read-write file in dir that disappears when its last handle
 * closes (O_TMPFILE). OS_E_NOTSUP where the filesystem lacks it. The name is
 * gone before the call returns (the suite lists dir while the file is open).
 * Windows: create the data handle under a random name (share R|W|D); open a
 * SECOND handle to that name relative to dir with DELETE access; set
 * FileDispositionInformationEx (DELETE | POSIX_SEMANTICS) on the second
 * handle and close it before returning (the name leaves the namespace when
 * the handle that set POSIX disposition closes; doing it on the retained data
 * handle, or a duplicate of it, would keep the name until that handle
 * closes). The data handle stays usable. */
os_err os_file_open_anon(os_file *out, const os_dir *dir);
/* mkdir; POSIX mode 0700. Existing name: OS_E_EXIST. */
os_err os_mkdir_at(const os_dir *dir, const char *name);
os_err os_mkdir(const char *path);
/* Removes an empty directory (rmdir; Windows: POSIX-semantics delete of the
 * directory, FileDispositionInformationEx DELETE | POSIX_SEMANTICS). Non-empty:
 * OS_E_EXIST. An os_dir open on it (locked or not) does not prevent removal,
 * and the name is gone at once, as on POSIX. */
os_err os_rmdir(const char *path);
/* Removes a non-directory name. An open (or mapped) file keeps its data for
 * the existing handles and the name is free for reuse immediately (Windows:
 * POSIX-semantics delete, FILE_DISPOSITION_FLAG_POSIX_SEMANTICS |
 * IGNORE_READONLY_ATTRIBUTE, so a read-only (0444) file is removed as on
 * POSIX). A handle opened without FILE_SHARE_DELETE by another program makes
 * Windows fail OS_E_BUSY. */
os_err os_unlink_at(const os_dir *dir, const char *name);
os_err os_unlink(const char *path);

/* Atomic rename of a non-directory or an empty-target directory.
 * REPLACE: an existing target is replaced atomically, even while open or
 * mapped by other handles, which keep reading the replaced file's bytes, and
 * even when the target is read-only (perm 0444 / FILE_ATTRIBUTE_READONLY):
 * on Linux replacement depends only on the directory's permissions.
 * NOREPLACE: an existing target fails OS_E_EXIST and both names are untouched.
 * Linux: renameat / renameat2(RENAME_NOREPLACE) (OS_E_NOTSUP where the
 * filesystem rejects the flag). Windows: FileRenameInformationEx with
 * FILE_RENAME_POSIX_SEMANTICS, and for REPLACE also
 * FILE_RENAME_REPLACE_IF_EXISTS | FILE_RENAME_IGNORE_READONLY_ATTRIBUTE.
 * POSIX semantics need Windows 10 1709 or later (perf/01-perf-target.md §2.8);
 * the read-only flag a later build, (E) 1809 from the DDK headers, which W0.3
 * confirms on this box (19045). The source is opened with DELETE access; ignoring
 * the read-only attribute additionally needs FILE_WRITE_ATTRIBUTES on the
 * replaced file, which the filesystem checks against its ACL. A genuine ACL
 * refusal (no DELETE on the source or the target, no WRITE_ATTRIBUTES, or
 * no add-file right on the target directory) is OS_E_ACCESS, the same code
 * Linux gives for a directory the caller cannot write. */
typedef enum os_rename_mode { OS_RENAME_REPLACE = 1, OS_RENAME_NOREPLACE = 2 } os_rename_mode;
os_err os_rename_at(const os_dir *from_dir, const char *from, const os_dir *to_dir,
                    const char *to, os_rename_mode mode);
os_err os_rename(const char *from, const char *to, os_rename_mode mode);

/* Directory listing. Never returns "." or "..". Order is unspecified.
 * Opening an iterator may allocate (fdopendir); it is never on the typing
 * path. The iterator has its own cursor: dir stays usable for *_at calls
 * meanwhile. */
typedef struct os_dir_iter {
#if defined(_WIN32)
    void *handle;
    void *buffer;       /* FileFullDirectoryInfo records; W0.3 sizes it at open */
    size_t at, used;
    char name[OS_NAME_MAX];
#else
    void *dir;  /* DIR * */
#endif
} os_dir_iter;
os_err os_dir_iter_open(os_dir_iter *it, const os_dir *dir);
/* OS_OK with *name set (valid until the next call), or OS_OK with *name NULL
 * at the end. */
os_err os_dir_next(os_dir_iter *it, const char **name);
void   os_dir_rewind(os_dir_iter *it);
void   os_dir_iter_close(os_dir_iter *it);

#if defined(_WIN32)
void *os_dir_handle(const os_dir *d);   /* platform code only */
#else
int   os_dir_fd(const os_dir *d);       /* platform code only */
#endif

/* ---- paths and well-known directories ---------------------------------- */
/* POSIX: starts with '/'. Windows: drive-absolute "C:\" or "C:/", or UNC
 * "\\server\share" (including \\wsl.localhost\...). Every path entry also
 * accepts a relative path, resolved against the current directory. */
bool   os_path_is_absolute(const char *path);
/* Absolute path with links resolved (realpath / GetFinalPathNameByHandleW
 * without the \\?\ prefix). OS_E_RANGE if cap is too small. */
os_err os_path_canonical(const char *path, char *out, size_t cap);
os_err os_cwd(char *out, size_t cap);
/* Per-user application directories, not created:
 *   OS_APP_CONFIG  $XDG_CONFIG_HOME/sublimite, else $HOME/.config/sublimite
 *                  Windows %APPDATA%\sublimite (FOLDERID_RoamingAppData)
 *   OS_APP_DATA    $XDG_DATA_HOME/sublimite, else $HOME/.local/share/sublimite
 *                  Windows %LOCALAPPDATA%\sublimite (FOLDERID_LocalAppData)
 * POSIX ignores a non-absolute XDG value (today's rule) and fails OS_E_NOENT
 * when neither variable gives an absolute root. No trailing separator. */
typedef enum os_app_dir_kind { OS_APP_CONFIG = 1, OS_APP_DATA = 2 } os_app_dir_kind;
os_err os_app_dir(os_app_dir_kind kind, char *out, size_t cap);
/* $TMPDIR if absolute, else /tmp. Windows GetTempPath2W (GetTempPathW before
 * build 22000). No trailing separator. */
os_err os_temp_dir(char *out, size_t cap);
/* Environment as UTF-8. get: OS_E_NOENT if unset, OS_E_RANGE if cap is too
 * small. set: value NULL unsets; may allocate (setenv copies the string).
 * Not thread-safe against concurrent set. */
os_err os_env_get(const char *name, char *out, size_t cap);
os_err os_env_set(const char *name, const char *value);

/* ---- memory: pages, file mappings, address space ---------------------- */
size_t   os_page_size(void);
/* Alignment of os_map_file offsets: the page size on Linux, the allocation
 * granularity on Windows. */
size_t   os_map_granularity(void);
uint64_t os_phys_mem_bytes(void);

/* A read-only private view of [offset, offset + len) of an open file.
 * offset is a multiple of os_map_granularity(); len > 0 and need not be a
 * page multiple. addr/len are the LOGICAL view; the native extent is len
 * rounded up to the page size (os_unmap releases all of it), and the bytes
 * between the end of the file and the end of its last page read zero.
 * The view stays valid after the file handle closes and after the file is
 * renamed over or unlinked. Writes by other handles to the file may become
 * visible.
 * File shorter than offset + len (it shrank after the caller sized it):
 *   Linux maps it, today's call with no size check; touching a page past the
 *   end faults, which a registered view recovers as zero pages with a rising
 *   epoch (the shrink outcome the file module already handles).
 *   Windows cannot map past the section and returns OS_E_RANGE.
 * Migration rule (decision doc §9): a caller that sized the view from an
 * earlier stat treats OS_E_RANGE as "the source changed".
 * Linux: mmap PROT_READ MAP_PRIVATE. Windows: CreateFileMappingW(PAGE_READONLY,
 * maximum size 0 = the file's size), VirtualAlloc2 MEM_RESERVE |
 * MEM_RESERVE_PLACEHOLDER of the page-rounded extent, MapViewOfFile3(section,
 * offset, MEM_REPLACE_PLACEHOLDER, PAGE_READONLY): MapViewOfFile3 takes
 * page-protection constants (PAGE_READONLY), not FILE_MAP_* access flags. */
typedef struct os_map {
    const void *addr;
    size_t len;
#if defined(_WIN32)
    void *section;
    uint64_t offset;
#endif
} os_map;
os_err os_map_file(os_map *out, const os_file *f, uint64_t offset, size_t len);
void   os_unmap(os_map *m);
/* Advisory read-ahead of [off, off + len) of the view (MADV_WILLNEED /
 * PrefetchVirtualMemory). */
void   os_map_prefetch(const os_map *m, size_t off, size_t len);

/* Anonymous address space. reserve gives len bytes (a multiple of the page
 * size) of address space; pages must be committed before they are touched.
 * commit is a no-op on Linux (the reservation is already readable and
 * writable, MAP_NORESERVE, lazily backed); Windows MEM_COMMIT. decommit
 * returns the pages' memory (MADV_DONTNEED / MEM_DECOMMIT); they must be
 * committed again before the next touch and then read zero. */
os_err os_vm_reserve(void **out, size_t len);
os_err os_vm_commit(void *addr, size_t len);
os_err os_vm_decommit(void *addr, size_t len);
void   os_vm_release(void *addr, size_t len);

/* ---- fault service for mapped files ------------------------------------ */
/* A mapped file can lose its backing under a reader: truncation by another
 * handle (Linux SIGBUS BUS_ADRERR), or an I/O error on a network or removable
 * volume (Linux SIGBUS BUS_OBJERR, Windows EXCEPTION_IN_PAGE_ERROR). Windows
 * refuses to shrink a mapped file (os_file_set_size: OS_E_BUSY), so there the
 * service mostly covers network shares such as \\wsl.localhost.
 * For a registered view, the process-wide service (Linux: a SIGBUS handler,
 * the file module's file_bus algorithm; Windows: a vectored exception handler)
 * recovers the faulting read: it increments the slot's epoch, then replaces
 * the view from the faulting page to its end with zero pages, and the read
 * completes with zeros. Bytes before the faulting page keep reading the file.
 * os_unmap of a recovered view releases all of it (file prefix and zero
 * suffix). Faults outside every registered view are passed to the previous
 * handler (and, with none, kill the process as they would without the
 * service).
 * Windows recipe for W0.3: map views into placeholders (os_map_file);
 * recover by UnmapViewOfFile2(MEM_PRESERVE_PLACEHOLDER), splitting the
 * placeholder at the faulting page (VirtualFree MEM_RELEASE |
 * MEM_PRESERVE_PLACEHOLDER), remapping the prefix from the section with
 * MapViewOfFile3 and committing zeros over the suffix (VirtualAlloc2
 * MEM_REPLACE_PLACEHOLDER); an access violation inside a slot that is
 * mid-recovery is retried.
 * ONE INSTALLER PER PROCESS. The service installs on first registration,
 * once per process (os_once). Concurrent first registrations from several
 * threads install exactly once (the suite races them at a barrier). Linux
 * installs with one sigaction(SIGBUS, &new, &old) call, so a handler
 * installed before it is chained exactly. A second, independent SIGBUS
 * installer that queries and installs in two steps can still lose this one
 * under a concurrent first use, so a process must not contain one: the file
 * module's file_bus is deleted in the same change (W2) that moves src/file
 * onto os_fault_*, and until then no binary links both (decision doc §8.1).
 * Capacity OS_FAULT_SLOTS registrations, the file module's FILE_GUARD_SLOTS
 * (M: src/file/file.c). */
#define OS_FAULT_SLOTS 256
/* OS_E_NOSPC when every slot is in use; OS_E_NOTSUP if the service could not
 * be installed. The view must stay mapped until os_fault_retire returns. */
os_err   os_fault_register(const os_map *m, int *slot);
/* Number of recoveries in this slot so far. Read it before and after reading
 * mapped bytes: a change means the bytes read may be zero-fill. */
uint64_t os_fault_epoch(int slot);
/* Stops recovery for the slot and waits for handlers inside it to finish;
 * then the view may be unmapped and the slot reused. */
void     os_fault_retire(int slot);
/* TEST ONLY. Delivers the platform's native lost-backing fault for the byte
 * at m->addr + off, on the calling thread, through the same handler and
 * recovery path as a real fault, then returns: Linux rt_tgsigqueueinfo of
 * SIGBUS (si_code BUS_ADRERR, si_addr = that byte) to the calling thread;
 * Windows RaiseException(EXCEPTION_IN_PAGE_ERROR, 0, 3, {0 (read), address,
 * STATUS_DEVICE_DATA_ERROR}) into the vectored handler, which recovers and
 * returns EXCEPTION_CONTINUE_EXECUTION. Lets the suite exercise recovery on
 * NTFS, where truncation of a mapped file is refused. off < m->len, else
 * OS_E_INVAL. An address in no registered view takes the chain like a real
 * fault (the process dies if nothing handles it). */
os_err   os_fault_test_trigger(const os_map *m, size_t off);

/* ---- random ------------------------------------------------------------- */
/* Cryptographic random bytes (getrandom / BCryptGenRandom). */
os_err os_random(void *buf, size_t n);

/* ---- processes ---------------------------------------------------------- */
/* For tests (crash, kill and data-loss suites re-execute themselves with a
 * role argument instead of fork) and launchers. Not for the typing path. */
typedef struct os_proc {
#if defined(_WIN32)
    void *process;
    uint32_t id;
    bool killed;
#else
    int pid;
#endif
} os_proc;
typedef enum os_proc_how {
    OS_PROC_EXITED = 1,  /* code = exit status (Windows: the 32-bit exit code;
                            abort() is reported as EXITED with the CRT's code 3) */
    OS_PROC_ABNORMAL     /* code = POSIX signal number; Windows: killed by
                            os_proc_kill, or an NTSTATUS exception code */
} os_proc_how;
typedef struct os_proc_status { os_proc_how how; int code; } os_proc_status;
/* argv[0] is the executable path (no PATH search); argv is NULL-terminated.
 * The child inherits the environment as it is now, the working directory and
 * the three standard streams, and NO other handle, including ones the process
 * created outside os.h without close-on-exec (fopen, library code):
 *   Linux: posix_spawn with posix_spawn_file_actions_addclosefrom_np(3)
 *   (glibc 2.34+), so descriptors >= 3 close in the child.
 *   Windows: CreateProcessW with STARTUPINFOEXW and
 *   PROC_THREAD_ATTRIBUTE_HANDLE_LIST naming only inheritable duplicates of
 *   the three standard handles, STARTF_USESTDHANDLES, bInheritHandles TRUE
 *   (the handle list is the only mechanism that hands redirected standard
 *   handles to the child without inheriting every inheritable handle).
 * The child receives argv byte for byte as UTF-8, including empty
 * arguments, spaces, quotes and trailing backslashes: Windows quotes argv into
 * one command line by the CommandLineToArgvW rules, and every executable that
 * uses os.h on Windows embeds the UTF-8 activeCodePage manifest
 * (src/os/utf8.manifest, W0.3), so the CRT's narrow argv (and fopen, getenv)
 * is UTF-8; without it the narrow command line is converted lossily to the
 * ANSI code page. May allocate. */
os_err os_proc_spawn(os_proc *out, const char *const argv[]);
/* Immediate termination with no cleanup (SIGKILL / TerminateProcess). */
os_err os_proc_kill(os_proc *p);
/* Waits up to timeout_ns (< 0 forever, 0 poll) for the child to end, then
 * reaps it (the os_proc is spent). OS_E_TIMEDOUT if still running. */
os_err os_proc_wait(os_proc *p, int64_t timeout_ns, os_proc_status *st);
uint64_t os_proc_id(void);
/* Path of the running executable (/proc/self/exe / GetModuleFileNameW). */
os_err os_proc_self_path(char *out, size_t cap);

#endif
