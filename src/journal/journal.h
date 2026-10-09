/* P1.9: append-only hot-exit journal. See docs/decisions/P1.9.md. */
#ifndef EDIT_JOURNAL_H
#define EDIT_JOURNAL_H
#include "base/base.h"
#include "work/work.h"
#include "piece/piece.h"
#include <sys/types.h>

#define JOURNAL_PAGE 4096u
#define JOURNAL_HEADER 32u
#define JOURNAL_MAX_RECORD (1024u * 1024u)
#define JOURNAL_MESSAGE 0x4a524e4cu
#define JOURNAL_DEFAULT_BATCH_BYTES (JOURNAL_MAX_RECORD + JOURNAL_PAGE)
#define JOURNAL_DEFAULT_FILE_BYTES 67108864ull
#define JOURNAL_DEFAULT_SYNC_BYTES 65536ull
#define JOURNAL_DEFAULT_SYNC_NS 1000000000ull

typedef enum journal_error {
    JOURNAL_OK = 0, JOURNAL_IO, JOURNAL_INVALID, JOURNAL_FULL,
    JOURNAL_BUSY, JOURNAL_BASE_CHANGED, JOURNAL_CALLBACK, JOURNAL_NOMEM
} journal_error;
/* Resolve the default journal directory without creating it. Absolute XDG_DATA_HOME
 * wins; otherwise use HOME/.local/share. Caller owns output; setup only. */
int journal_default_dir(char *out, size_t cap);

typedef enum journal_type {
    JOURNAL_BASE = 1, JOURNAL_INSERT, JOURNAL_DELETE, JOURNAL_VIEW,
    JOURNAL_TABS, JOURNAL_WINDOW, JOURNAL_SAVE
} journal_type;
/* All wire integers are little-endian; no native structs are persisted. */
typedef struct journal_base {
    uint64_t size, mtime_ns, inode, device;
    uint32_t prefix_crc, prefix_len;
    /* Empty path means never saved; size must be zero, edits create content. */
    const char *path;
    /* Capture owns its canonical path here. Copies borrow the captured object's
     * path, so that object must outlive them; decode uses caller-owned storage. */
    char captured_path[4097];
} journal_base;
typedef struct journal_view {
    uint64_t cursor, anchor, scroll_byte, scroll_x;
} journal_view;
typedef struct journal_record {
    uint32_t type;
    uint64_t buffer_id, sequence;
    const uint8_t *data;
    size_t size;
} journal_record;
typedef struct journal_replay_result {
    uint64_t valid_bytes, records, last_sequence;
    bool corrupt;
} journal_replay_result;
typedef int (*journal_visit)(void *, const journal_record *);
typedef struct journal_options {
    size_t batch_bytes;        /* page multiple >= 8192; default 1 MiB + page, two buffers */
    uint64_t max_file_bytes;   /* page multiple; default 64 MiB log budget; checkpoint adds its size */
    uint64_t sync_bytes;       /* page multiple >= 4096; 0 = current 64 KiB default */
    uint64_t sync_interval_ns; /* 0 = current 1 s default; explicit force always syncs */
} journal_options;
typedef struct journal_stats {
    uint64_t accepted_sequence, written_sequence, durable_sequence;
    uint64_t file_bytes, syncs, last_sync_ns, max_sync_interval_ns;
    uint64_t last_sync_bytes, max_sync_bytes;
    int error;
    uint64_t file_limit_bytes, checkpoint_bytes, queue_bytes;
    uint64_t pending_bytes;    /* bytes retained in the two UI/worker batches */
    int append_errno;          /* sticky UI-write errno; short/zero write => EIO */
} journal_stats;
typedef struct journal journal;
/* Optional per-instance syscall seam; callbacks have POSIX return/errno semantics.
 * sync receives directory=true for namespace barriers, false for data barriers.
 * NULL callbacks use real syscalls; contexts must outlive all jobs, including
 * retained-base and temporary checkpoint jobs. A write hook selects the bounded
 * retained-base copy fallback instead of reflinking. Set only with no active
 * worker. write/read/sync/rename remain off path; append_write runs on the UI
 * owner and must make one attempt without allocation or retry. Its ctx can be
 * used concurrently by the worker's other hooks. open_with_io also covers the
 * creation-directory barrier. */
typedef struct journal_io {
    void *ctx;
    ssize_t (*write)(void *, int, const uint8_t *, size_t, uint64_t);
    int (*sync)(void *, int, bool);
    int (*rename)(void *, const char *, const char *);
    ssize_t (*read)(void *, int, uint8_t *, size_t, uint64_t); /* BASE prefix only */
    /* Single UI-side positioned write attempt; NULL uses pwrite. Separate from
     * worker/copy write so fault tests can distinguish thread ownership. */
    ssize_t (*append_write)(void *, int, const uint8_t *, size_t, uint64_t);
} journal_io;
/* Zero-initialize each save token. Its sequence is the saved snapshot cutoff
 * in the PREPARED checkpoint generation (checkpoint record count). */
typedef struct journal_save {
    uint64_t buffer_id, sequence;
    char previous_path[4097];
    bool prepared;
} journal_save;
int journal_open_with_io(journal **out, const char *path, work_pool *pool,
                         const journal_options *options, const journal_io *io);
int journal_set_io(journal *j, const journal_io *io);
/* Off-path, after receive returns worker ownership. Clear sticky UI/worker IO,
 * retaining exact batch progress and both buffers; subsequent pump/flush retries
 * the failed batch first. Repeats a failed checkpoint-directory barrier BEFORE
 * permitting new writes/acks. FULL suspension is preserved. After clearing a
 * UI short-write error before drain, append returns BUSY until pump/receive has
 * filled the prefix gap; never retry the edit that was already queued. */
int journal_retry(journal *j);
/* Recoverable save order (all checkpoint work is off the typing path):
 * 1. Build a COMPLETE session checkpoint at the current UI snapshot. Include one
 *    matching previous BASE for id and all state/edits for every other buffer.
 * 2. save_prepare flushes, retains the previous bytes in a private reflink or
 *    bounded worker copy, validates the source across retention, syncs the new
 *    inode and its directory,
 *    substitutes every BASE matching that named identity/path, adds a SAVE
 *    marker with the saved cutoff,
 *    durably rotates. Named previous paths must be absolute (file_path works).
 * 3. Without intervening UI mutation, call file_save_begin on the same tree;
 *    it takes the snapshot. Appends may continue after it returns. Its return
 *    acknowledges enqueue only; route FILE_MSG_SAVE_DONE and require FILE_OK.
 * 4. capture_base(file_path) obtains the new identity. Build another COMPLETE
 *    CURRENT-session checkpoint: new BASE + edits since the saved cutoff for
 *    this buffer; preserve other buffers (including their prepared BASE paths
 *    and SAVE metadata).
 *    Buffers sharing the replaced path must also be rebased/checkpointed before
 *    finish can retire their shared retained generation.
 * 5. save_finish flushes and persists cutoff + new identity inside the new
 *    checkpoint before its rename, then rotates and syncs the
 *    new name, THEN retires the previous generation. Requires no concurrent mutation
 *    between checkpoint construction and either call.
 * Recovery needs no live token: pre-rotation BASE loads the retained generation
 * and replays all its ops; post-rotation BASE loads the saved generation and
 * applies only its remaining deltas. SAVE is metadata, never a tree mutation.
 * Save failure or any journal error: retain previous_path, suspend/resolve IO
 * and retry; never delete it merely because the call failed (a rename may have
 * happened). A failed prepare must NOT be followed by file_save_begin.
 * Tokens for simultaneous saves belong to the caller. Complete checkpoints
 * must retain every in-flight token's BASE and SAVE metadata; finish refuses to retire a BASE
 * referenced by the supplied checkpoint. Caller guarantees checkpoint contents
 * include all accepted edits, as for ordinary rotate. */
int journal_save_prepare(journal *j, uint64_t id, const journal_base *previous,
                         const journal_record *checkpoint, size_t count,
                         journal_save *save);
int journal_save_finish(journal *j, journal_save *save, const journal_base *saved,
                        const journal_record *checkpoint, size_t count);
/* Setup/close/rotation may allocate or block; never call on typing path.
 * New file only: open refuses a nonempty file (replay + checkpoint first).
 * Caller owns pool and must drain mailboxes through journal_receive (including
 * unrelated messages forwarded to their owners), before journal_pump.
 * Only one UI thread accesses journal. Pool outlives journal_close. */
int journal_open(journal **out, const char *path, work_pool *pool,
                 const journal_options *options);
void journal_close(journal *j);
/* Typing path: no malloc, locks, sync, or worker submission. Encode+CRC and one
 * bounded pwrite per record on this calling thread; OK means the complete
 * record is in the kernel page cache, so process crash loses no successful
 * appends. fdatasync stays on the worker (default 1 s / 64 KiB).
 * A UI syscall error (including EAGAIN/ENOSPC/EIO/EINTR), zero or short write
 * returns sticky IO with append_errno, retaining the WHOLE record for worker
 * drain. No UI retry/wait. Do not retry that edit; it was accepted into RAM.
 * Later appends are refused until suspension is resolved. Linux regular-file
 * pwrite can itself block; bounded bytes/attempts are not a wall-clock bound.
 * A FULL error
 * is sticky: later mutations must not be journaled until a complete rotation.
 * Application may keep editing but must surface that recovery is suspended.
 * No accepted records are discarded. size excludes header. Named BASE/SAVE
 * paths must be canonical absolute paths (capture supplies owned storage). */
int journal_append(journal *j, uint32_t type, uint64_t id,
                   const uint8_t *data, size_t size);
/* One logical INSERT: preflight all capacity/disk/sequence bounds, then split
 * into wire records <=1 MiB. FULL accepts none of this call. No pump/wait/alloc.
 * IO retains ALL preflighted chunks, including any not yet attempted; only its
 * complete written prefix is process-crash protected until worker drain.
 * Default empty current batch admits a 1 MiB paste plus normal session metadata.
 * Existing backlog can still suspend recovery; caller surfaces FULL. */
int journal_insert(journal *j, uint64_t id, uint64_t off,
                   const uint8_t *bytes, size_t size);
int journal_delete(journal *j, uint64_t id, uint64_t off, uint64_t len);
int journal_set_base(journal *j, uint64_t id, const journal_base *base);
int journal_set_view(journal *j, uint64_t id, const journal_view *view);
int journal_set_tabs(journal *j, const uint64_t *ids, size_t count, uint64_t active);
int journal_set_window(journal *j, uint32_t width, uint32_t height);
/* Event-loop timer (<=5 ms recommended), outside the typing path. now_ns is
 * CLOCK_MONOTONIC; force requests a sync even if no new records exist.
 * Caches the batch's PAD before transferring it (so later appends have no
 * unwritten gap). PAD errors queue worker retry and return sticky IO.
 * Worker accounts cached bytes, writes only missing tails, and syncs at the
 * configured byte/time thresholds, plus explicit force.
 * Zero option fields preserve the current 64 KiB / 1 s defaults.
 * IO/scheduling delays can extend the window; stats expose durable progress. */
int journal_pump(journal *j, uint64_t now_ns, bool force);
bool journal_receive(journal *j, const work_msg *message);
journal_stats journal_get_stats(const journal *j);
/* Register the application's router for unrelated mailbox messages before
 * using blocking flush/close/rotate with a shared pool. No handler means the
 * pool must be private to this journal. Async pump/receive needs no handler. */
void journal_set_message_handler(journal *j,
                                 void (*handler)(const work_msg *, void *), void *ctx);
/* Drain accepted records, then return sticky UI IO or FULL while suspended.
 * Worker/directory IO may require journal_retry before drain can continue.
 * Clean exit: on FULL build/rotate a complete CURRENT checkpoint, require OK
 * from flush, then close. IO takes precedence over FULL. */
int journal_flush(journal *j); /* blocking; setup/save/exit only */
/* Complete session checkpoint. Use the save transaction BEFORE file_save_begin
 * when a named BASE will be replaced. Include each BASE,
 * remaining dirty contents as INSERT from empty base or ops against saved base,
 * views, tabs, window. No old records survive. Atomic rename + directory fsync.
 * Requires no active worker; normally flush first. After received worker IO
 * failure it can replace both retained batches with a complete checkpoint.
 * Failure before rename preserves the old recovery prefix and retry state.
 * Failure at the directory barrier suspends IO and reports durable_sequence=0
 * for the new generation until retry completes that barrier. Checkpoint size
 * is preflighted; its durable bytes plus the original configured log budget
 * bound the new file. Large untitled checkpoints may exceed the old log limit. */
int journal_rotate(journal *j, const journal_record *records, size_t count);
/* Parser: visits only structurally valid, CRC-checked records. Callback must
 * apply atomically; nonzero stops WITHOUT truncating that record. */
int journal_replay_bytes(const uint8_t *bytes, size_t size, journal_visit visit,
                         void *ctx, journal_replay_result *result);
/* Streaming, bounded memory; truncate first bad/short record and fdatasync.
 * Checks every BASE identity before delivering it. On callback/base conflict
 * returns unchanged file (surface user choice); discard partial restored state. */
int journal_replay_file(const char *path, journal_visit visit, void *ctx,
                        journal_replay_result *result);
/* Capture/check base identity, stat before/after prefix read. Off typing path.
 * check compares size/mtime/inode/device + CRC32C of first <=4096 bytes.
 * Capture canonicalizes into base->captured_path (off path); named paths in
 * manually built records must already be canonical absolute paths. Missing,
 * unreadable, short-read or changed bases yield BASE_CHANGED. The optional read
 * seam applies only to BASE prefixes, not journal transport or bulk-copy reads. */
int journal_capture_base(const char *path, journal_base *base);
int journal_capture_base_with_io(const char *path, journal_base *base, const journal_io *io);
int journal_replay_file_with_io(const char *path, journal_visit visit, void *ctx,
                                journal_replay_result *result, const journal_io *io);
int journal_decode_base(const journal_record *record, journal_base *base,
                        char *path, size_t capacity);
int journal_check_base(const journal_base *base);
/* Apply INSERT/DELETE to a supplied tree; BASE and session records handled by
 * caller (check/load base before edits); SAVE is metadata. Range failure returns INVALID. */
int journal_apply_piece(piece_tree *tree, const journal_record *record);
#endif
