/* file.h -- file open / change detection / durable atomic save (bead P1.7).
 *
 * OPEN (perf s2.4). file_open_begin only captures the path/options and enqueues
 * prefix acquisition. Canonicalization, open(O_NONBLOCK), type checks, prefix
 * reads and sliced EOL/BOM scans run on a worker. FILE_MSG_PREFIX_READY exposes
 * the first viewport source; file_prefix_ready distinguishes an empty prefix
 * from a pending one. Consume file messages through file_msg_decode.
 * A pool with a raster worker uses that lane for acquisition; a bulk-only pool
 * cannot promise G5 while unrelated bulk I/O is active (P1.7d proposal).
 * Everything else is a bulk work job: copy mode (size < copy threshold) reads the
 * whole file into a buffer; map mode mmaps it read-only and prefetches the head.
 * The job posts FILE_MSG_OPEN_READY (or FILE_MSG_OPEN_FAILED) through
 * work_publish; the UI then calls file_attach, which hands the data to the
 * piece tree through piece_init_mapped lifetime hooks for either backing mode.
 * Workers never touch the tree. Small copies also report OPEN_READY; filesystem
 * errors are asynchronous OPEN_FAILED results with errno in the payload/getter.
 * Terminal/phase publication retries while cancellable on mailbox saturation.
 * Copy threshold = min(256 MiB, RAM/32), injectable via file_open_opts.
 *
 * CHANGE DETECTION (perf s2.14). Identity includes dev, ino, size, mtime, ctime,
 * permissions, ownership and entry type. Acquisition validates both descriptor
 * and canonical entry before and after reads; premature EOF invalidates the
 * acquired generation. Checked by file_check (lstat of the canonical path;
 * for mapped files also fstat of the mapped
 * inode vs the mapped length) and, optionally, an inotify fd the UI polls
 * (file_watch_*). Detection sets a sticky "changed" state; the UI surfaces
 * reload/keep. file_resolve_keep accepts the disk state as the new baseline
 * only while the original's bytes remain valid. Private originals can keep
 * truncated/deleted/replaced disk versions; intact mapped originals can keep
 * deletion or replacement. A changed/faulted
 * mapped original returns FILE_ERR_CHANGED: reload is required until the piece
 * contract supports rebasing derived metadata (docs/decisions/P1.7c.md s5).
 * reload = file_close + file_open_begin. A save refuses with FILE_ERR_CHANGED
 * unless FILE_SAVE_FORCE. The worker checks identity immediately before rename,
 * including the snapshot's original
 * inode, recovery epoch and UI invalidation generation. FORCE accepts an
 * already changed source; a later invalidation still rejects that save.
 * The remaining race is the last validation to rename. Save by rename never
 * modifies a mapped inode.
 * SIGBUS (P1.7b): a process-wide handler turns reads past the new EOF of an
 * externally truncated mapped original (any thread) into zero-filled reads and
 * sets the same sticky "changed" state (FILE_CHG_TRUNCATED); file_changed and
 * file_check report it. Faults outside our mappings chain to the previous
 * handler / default action. Bytes read after the fault are zeros: the UI must
 * treat the buffer as stale and offer reload. See docs/decisions/P1.7c.md.
 *
 * SAVE (perf s2.8). file_save_begin (UI): cached change state, piece_snapshot_take,
 * enqueue. Returning 0 IS the ack. The worker writes a temp file in the same
 * directory, fsyncs it, renames over the target, fsyncs the same retained parent
 * directory descriptor, then
 * posts FILE_MSG_SAVE_DONE. Authoritative checks and FORCE backing stat run on
 * the worker. Status/readiness installation performs no filesystem operations;
 * Preparation transfers back to the UI through SAVE_PREPARED to capture
 * immutable authorization for a commit continuation; no UI/worker mutex exists.
 * Temp name: ".<base>.edit-<pid>-<ns>.tmp" in the
 * target directory; a crash can leave such a file but never a partial target.
 *
 * THREADS: file_* except file_save_write and the internal jobs are UI-thread
 * only. One open pipeline and one save pipeline per file at a time. Worker result records
 * are immutable after release publication; only mailbox decode installs them
 * on the UI. Readiness/status polling never installs unreceived results.
 * Watches are managed only on the UI and track the parent/name so replacements
 * and deletion/recreation remain observable.
 * ERRORS: return codes, 0 = success. No printf. */
#ifndef EDITOR_FILE_FILE_H
#define EDITOR_FILE_FILE_H

#include <stddef.h>
#include <stdint.h>
#include <pthread.h>

#include "piece/piece.h"
#include "work/work.h"

#define FILE_OK            0
#define FILE_ERR_IO        1   /* see file_errno */
#define FILE_ERR_NOMEM     2
#define FILE_ERR_NOTREG    3   /* not a regular file */
#define FILE_ERR_BUSY      4   /* a job of that kind is in flight */
#define FILE_ERR_CHANGED   5   /* source changed on disk */
#define FILE_ERR_CANCELLED 6
#define FILE_ERR_STATE     7   /* not ready / already attached */
#define FILE_ERR_POOL      8   /* work_submit refused */
#define FILE_ERR_DIRSYNC   9   /* renamed, but directory fsync failed */

#define FILE_PREFIX_MAX (1u << 20)

typedef struct file file;

typedef enum file_mode { FILE_MODE_COPY = 1, FILE_MODE_MMAP = 2 } file_mode;

typedef enum file_eol {
    FILE_EOL_NONE = 0,   /* no line break seen */
    FILE_EOL_LF = 1,
    FILE_EOL_CRLF = 2,
    FILE_EOL_MIXED = 3
} file_eol;

typedef struct file_prefix_info {
    uint64_t lf;         /* '\n' not preceded by '\r' */
    uint64_t crlf;       /* "\r\n" */
    uint64_t cr;         /* '\r' not followed by '\n' (a final '\r' counts) */
    file_eol kind;
    file_eol dominant;   /* FILE_EOL_LF or FILE_EOL_CRLF: style for new lines */
    int has_bom;         /* starts with EF BB BF */
} file_prefix_info;

/* Pure scan; any bytes. Counts are exact over [p, p+n). */
void file_prefix_scan(const uint8_t *p, size_t n, file_prefix_info *out);

/* Identity used for change detection. */
typedef struct file_id {
    uint64_t dev, ino, size, mtime_ns;
    uint32_t mode;       /* st_mode permission bits */
    int exists;
    uint64_t ctime_ns;   /* source-version validation, including restored mtime */
    uint32_t uid, gid;
    int metadata_valid; /* extended fields populated by file; 0 for legacy IDs */
    uint32_t type;       /* st_mode & S_IFMT; canonical entry must stay regular */
} file_id;

#define FILE_CHG_NONE      0u
#define FILE_CHG_MTIME     1u
#define FILE_CHG_SIZE      2u
#define FILE_CHG_INODE     4u
#define FILE_CHG_GONE      8u
#define FILE_CHG_TYPE      64u  /* canonical entry type changed (e.g. symlink) */
#define FILE_CHG_METADATA  32u  /* mode/owner/group/ctime changed */
#define FILE_CHG_TRUNCATED 16u   /* mapped inode shorter than the mapping */

typedef struct file_open_opts {
    uint64_t copy_threshold;   /* 0 = file_default_copy_threshold() */
    uint32_t generation;       /* copied into every message */
} file_open_opts;

uint64_t file_default_copy_threshold(void);

/* Message kinds (work_msg.kind). Payload: file_msg_decode. */
enum {
    FILE_MSG_OPEN_READY  = 0x46490001u,
    FILE_MSG_OPEN_FAILED = 0x46490002u,
    FILE_MSG_SAVE_DONE   = 0x46490003u,  /* status 0 or a FILE_ERR_* */
    FILE_MSG_PREFIX_READY = 0x46490004u,
    FILE_MSG_CHECK_DONE   = 0x46490005u, /* internal; decode installs, returns 1 */
    FILE_MSG_REPLACED     = 0x46490006u, /* internal; decode installs, returns 1 */
    FILE_MSG_SAVE_PREPARED = 0x46490007u /* internal; decode queues commit, returns 1 */
};

typedef struct file_msg {
    uint32_t kind;
    uint32_t generation;
    file *f;
    int32_t status;
    uint32_t mode;       /* file_mode (open) */
    uint64_t size;       /* bytes available (open) / written (save) */
    int32_t err_no;
} file_msg;

/* UI-only. Decode every live pool notification. Returns 0 and fills *out for
 * public file notifications, installing state before exposing them. Internal
 * CHECK_DONE/REPLACED/SAVE_PREPARED install/continue ownership then return 1;
 * unrelated messages also return 1. Never decode retained/cancelled copies. */
int file_msg_decode(const work_msg *m, file_msg *out);

/* ---- open ---- */
int file_open_begin(work_pool *pool, const char *path, const file_open_opts *opts,
                    file **out);
/* Cancels every outstanding job/message for this file, waits for running job
 * cleanup, and releases resources. Snapshots and the
 * tree keep any mapping alive through the refcount hooks. */
void file_close(file *f);

const uint8_t *file_prefix(const file *f, size_t *len);
int file_prefix_ready(const file *f);      /* prefix/identity available, even if empty */
const file_prefix_info *file_prefix_info_of(const file *f);
uint64_t file_size(const file *f);          /* size at open (stat) */
file_mode file_open_mode(const file *f);
const char *file_path(const file *f);       /* canonical after PREFIX_READY; "" while pending */
int file_errno(const file *f);              /* errno of the last FILE_ERR_IO */
/* 1 once the full content is ready for file_attach. */
int file_open_ready(const file *f);
/* Give the content to an empty tree. Valid once; FILE_ERR_STATE if not ready. */
int file_attach(file *f, piece_tree *t);

/* ---- change detection ---- */
/* Stat-based check. Returns 1 if the source changed (sticky), else 0. If
 * reasons != NULL it receives the FILE_CHG_* bits of this check. */
int file_check(file *f, uint32_t *reasons);
int file_changed(const file *f);
/* User chose "keep": adopt the current disk identity as baseline, clear the
 * flag. Both-absent identities compare equal, allowing normal recreation.
 * FILE_ERR_IO on failed identity/watch refresh, preserving the baseline.
 * FILE_ERR_CHANGED if the canonical entry is nonregular or the mapped backing
 * changed/faulted: accepting it would leave cached counts inconsistent. A
 * metadata/ctime change on the still-named mapped inode conservatively requires
 * reload, since restored mtime cannot prove its original bytes are intact. */
int file_resolve_keep(file *f);
/* Optional inotify. start is explicit off-path setup/refresh (may do I/O).
 * After a save call start again to refresh the watch on the replacement.
 * poll reads at most one 4096-byte nonblocking buffer, coalesces a BULK identity
 * check, and returns cached changed state. Route CHECK_DONE through decode;
 * detection is asynchronous. Saturated submission is retried on a later poll. */
int file_watch_start(file *f);
int file_watch_poll(file *f);

/* ---- save ---- */
#define FILE_SAVE_FORCE 1u   /* overwrite even if the source changed */
int file_save_begin(file *f, piece_tree *t, unsigned flags, uint32_t generation);
/* UI-only. 1 until completion is decoded and the job finishes. Also retries
 * enqueue of a commit continuation after temporary pool saturation (no I/O). */
int file_save_busy(const file *f);

/* Synchronous core used by the job (exported for the durability test, which
 * forks and cannot use the pool). */
enum {
    FILE_STEP_TEMP_CREATED = 1,
    FILE_STEP_MID_WRITE,
    FILE_STEP_TEMP_WRITTEN,
    FILE_STEP_FSYNCED,
    FILE_STEP_RENAMED,       /* == before directory fsync */
    FILE_STEP_DIR_SYNCED
};

typedef struct file_save_args {
    const char *path;                 /* target (canonical) */
    const piece_snapshot *snap;
    uint32_t mode;                    /* new-target bits; 0 -> 0644 unless mode_valid */
    const file_id *expect;            /* NULL = no identity check */
    void (*step)(void *ctx, int step);/* test hook, may be NULL */
    void *step_ctx;
    int (*stop)(void *ctx);           /* non-zero = cancel; may be NULL */
    void *stop_ctx;
    int (*validate)(void *ctx);       /* pre-rename source validation; FILE_OK or error */
    void *validate_ctx;
    void (*replaced)(void *ctx);      /* publish replacement identity under lock, if any */
    void *replaced_ctx;
    pthread_mutex_t *lock;            /* held across check+rename+identity; may be NULL */
    file_id *out_id;                  /* identity of the new file; may be NULL */
    uint64_t written;                 /* out */
    int err_no;                       /* out */
    int mode_valid;                   /* permits explicit mode 0000 for a new target */
} file_save_args;

int file_save_write(file_save_args *a);

/* Test-only: install a step hook on the file's save job. */
void file_set_step_hook(file *f, void (*step)(void *ctx, int step), void *ctx);

#endif
