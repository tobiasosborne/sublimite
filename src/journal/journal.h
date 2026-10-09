/* P1.9: append-only hot-exit journal. See docs/decisions/P1.9.md. */
#ifndef EDIT_JOURNAL_H
#define EDIT_JOURNAL_H
#include "base/base.h"
#include "work/work.h"
#include "piece/piece.h"

#define JOURNAL_PAGE 4096u
#define JOURNAL_HEADER 32u
#define JOURNAL_MAX_RECORD (1024u * 1024u)
#define JOURNAL_MESSAGE 0x4a524e4cu

typedef enum journal_error {
    JOURNAL_OK = 0, JOURNAL_IO, JOURNAL_INVALID, JOURNAL_FULL,
    JOURNAL_BUSY, JOURNAL_BASE_CHANGED, JOURNAL_CALLBACK, JOURNAL_NOMEM
} journal_error;
typedef enum journal_type {
    JOURNAL_BASE = 1, JOURNAL_INSERT, JOURNAL_DELETE, JOURNAL_VIEW,
    JOURNAL_TABS, JOURNAL_WINDOW
} journal_type;
/* All wire integers are little-endian; no native structs are persisted. */
typedef struct journal_base {
    uint64_t size, mtime_ns, inode, device;
    uint32_t prefix_crc, prefix_len;
    /* Empty path means never saved; size must be zero, edits create content. */
    const char *path;
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
    size_t batch_bytes;        /* page multiple >= 8192; default 256 KiB, two buffers */
    uint64_t max_file_bytes;   /* page multiple; default 64 MiB */
} journal_options;
typedef struct journal_stats {
    uint64_t accepted_sequence, written_sequence, durable_sequence;
    uint64_t file_bytes, syncs, last_sync_ns, max_sync_interval_ns;
    uint64_t last_sync_bytes, max_sync_bytes;
    int error;
} journal_stats;
typedef struct journal journal;
/* Setup/close/rotation may allocate or block; never call on typing path.
 * New file only: open refuses a nonempty file (replay + checkpoint first).
 * Caller owns pool and must drain mailboxes through journal_receive (including
 * unrelated messages forwarded to their owners), before journal_pump.
 * Only one UI thread accesses journal. Pool outlives journal_close. */
int journal_open(journal **out, const char *path, work_pool *pool,
                 const journal_options *options);
void journal_close(journal *j);
/* Typing path: no malloc, syscalls, locks, or worker submission. A FULL error
 * is sticky: later mutations must not be journaled until a complete rotation.
 * Application may keep editing but must surface that recovery is suspended.
 * No accepted records are discarded. size excludes header. */
int journal_append(journal *j, uint32_t type, uint64_t id,
                   const uint8_t *data, size_t size);
int journal_insert(journal *j, uint64_t id, uint64_t off,
                   const uint8_t *bytes, size_t size);
int journal_delete(journal *j, uint64_t id, uint64_t off, uint64_t len);
int journal_set_base(journal *j, uint64_t id, const journal_base *base);
int journal_set_view(journal *j, uint64_t id, const journal_view *view);
int journal_set_tabs(journal *j, const uint64_t *ids, size_t count, uint64_t active);
int journal_set_window(journal *j, uint32_t width, uint32_t height);
/* Event-loop timer (<=5 ms recommended), outside the typing path. now_ns is
 * CLOCK_MONOTONIC; force requests a sync even if no new records exist.
 * Worker syncs at >=64 KiB or >=1 s since last sync, plus explicit force.
 * IO/scheduling delays can extend the window; stats expose durable progress. */
int journal_pump(journal *j, uint64_t now_ns, bool force);
bool journal_receive(journal *j, const work_msg *message);
journal_stats journal_get_stats(const journal *j);
/* Register the application's router for unrelated mailbox messages before
 * using blocking flush/close/rotate with a shared pool. No handler means the
 * pool must be private to this journal. Async pump/receive needs no handler. */
void journal_set_message_handler(journal *j,
                                 void (*handler)(const work_msg *, void *), void *ctx);
int journal_flush(journal *j); /* blocking; setup/save/exit only */
/* Complete session checkpoint, after a successful save. Include each BASE,
 * remaining dirty contents as INSERT from empty base or ops against saved base,
 * views, tabs, window. No old records survive. Atomic rename + directory fsync.
 * Requires no active worker or pending records (flush first). */
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
 * Missing or changed base yields BASE_CHANGED, never automatic rebase. */
int journal_capture_base(const char *path, journal_base *base);
int journal_decode_base(const journal_record *record, journal_base *base,
                        char *path, size_t capacity);
int journal_check_base(const journal_base *base);
/* Apply INSERT/DELETE to a supplied tree; BASE and session records handled by
 * caller (check/load base before edits). Range failure returns INVALID. */
int journal_apply_piece(piece_tree *tree, const journal_record *record);
#endif
