#ifndef EDIT_SAVECTL_H
#define EDIT_SAVECTL_H
#include "file/file.h"
#include "journal/journal.h"
#include <stdbool.h>

typedef struct savectl savectl;
typedef enum savectl_state {
    SAVECTL_IDLE, SAVECTL_SAVING, SAVECTL_SAVED, SAVECTL_FAILED,
    SAVECTL_EXTERNAL_MODIFIED, SAVECTL_EXTERNAL_UNMODIFIED, SAVECTL_RELOADING
} savectl_state;
enum { SAVECTL_OK = 0, SAVECTL_BUSY = -1, SAVECTL_INVALID = -2,
       SAVECTL_NOMEM = -3, SAVECTL_POOL = -4, SAVECTL_SOURCE_GUARD = -5 };
#define SAVECTL_MESSAGE UINT32_C(0x53415645)
typedef struct savectl_view {
    uint64_t cursor, anchor, scroll_byte, scroll_x;
} savectl_view;
typedef struct savectl_model {
    savectl_state state;
    const char *status, *banner;
    bool modified, can_reload, can_keep, busy, needs_finish, journal_leased;
    int file_error, journal_error, err_no;
    bool saving_frame_pending;
    uint64_t request_id, request_ns, saving_submitted_ns;
} savectl_model;
/* Stable, canonical absolute path and baseline from the SAME original opened
 * by the caller (capture off-path). mode is permission bits, not file_mode.
 * COPY originals are immutable; MMAP requires a worker-safe source validator
 * covering original inode AND SIGBUS fault epoch, retained through completion.
 * No unguarded MMAP save/keep is permitted. validate returns FILE_OK/error.
 * A journal must have a PRIVATE pool distinct from pool, be quiescent, and be
 * exclusively leased to this controller while journal_leased is true.
 * step is an optional worker-side durability test hook. Setup may allocate. */
typedef struct savectl_options {
    work_pool *pool;
    const char *path;
    file_id baseline;
    file_mode source_mode;
    file_source *source; /* optional retained file-owned identity/guard lease */
    int (*validate_source)(void *ctx);
    void *source_ctx;
    journal *journal;
    work_pool *journal_pool; /* actual private journal pool, must differ */
    piece_allocator reload_allocator; /* UI-owned reservation for replacement */
    /* Optional paired rollback hooks for non-reclaiming allocators. Reservation
     * is exclusive: no other allocations from mark until install/rollback. */
    size_t (*reload_mark)(void *ctx);
    void (*reload_reset)(void *ctx, size_t mark);
    uint64_t buffer_id;
    uint64_t reload_copy_threshold; /* clone threshold; 0 uses file policy */
    void (*step)(void *ctx, int step);
    void *step_ctx;
    /* New targets only: valid selects exact permission bits, including 0000.
     * Without it, retain the legacy baseline.mode hint (0 selects 0644).
     * These are final fchmod bits: process umask is not applied. Existing
     * targets preserve the file core's captured permissions/ACL policy. */
    uint32_t create_mode;
    bool create_mode_valid;
} savectl_options;
int savectl_create(savectl **out, const savectl_options *options, bool modified);
/* Nonblocking; BUSY until job, result, and staged reload are retired. Does not
 * close the borrowed journal/pool/file/tree. Call tick before retrying. */
int savectl_destroy(savectl *s);
/* Logical close: invalidates save/reload immediately, suppresses new checks.
 * Keep ticking until destroy succeeds. Copy any retained token before destroy;
 * controller never deletes ambiguous recovery generations. */
void savectl_close_begin(savectl *s);
/* Every mutation (including undo/redo) after successfully mutating the tree. */
void savectl_modified(savectl *s);
/* Initial history registration and each successful mutation, instead of
 * modified(). Identity values name content states, never reused across undo
 * branches. saved names the retained saved history state; saved_valid=false
 * after eviction/unknown history means conservatively dirty. Both paths
 * advance revision to invalidate in-flight reloads, even on undo to clean.
 * Register the new clean history identity after installing a reload. */
void savectl_content_identity(savectl *s, uint64_t current, uint64_t saved,
                              bool saved_valid);
/* CPU only: update latest live offsets; used at reload installation time. */
void savectl_set_view(savectl *s, savectl_view view);
savectl_model savectl_get_model(const savectl *s);
/* Snapshot + enqueue + SAVING. Checkpoint and previous are required only with
 * a journal. Records/data must stay immutable until needs_finish or failure.
 * Snapshot cutoff and complete PREPARE checkpoint must describe the same UI
 * state. No UI journal operations while leased; defer edits in caller storage.
 * tick/receive adopts the durable prepare token and returns journal ownership
 * BEFORE file-only writing starts. While busy && !journal_leased, append/pump/
 * receive may resume for all buffers; preserve the token's retained BASE and
 * SAVE metadata in every intervening complete checkpoint. Finish reacquires
 * an exclusive, quiescent journal lease with a complete CURRENT checkpoint.
 * A retained failed token forbids a fresh save (recovery must resolve it). */
int savectl_save(savectl *s, piece_tree *tree, const journal_base *previous,
                 const journal_record *checkpoint, size_t count);
/* Bounded session acknowledgement entry point. The host maintains a published
 * immutable session root as part of bounded mutation bookkeeping. Capture pins
 * that root and ONE already published selected-buffer snapshot with constant
 * reference operations: no buffer enumeration, data copy, allocation, I/O or
 * snapshot construction on the input-to-frame path. Start request_ns at input
 * receipt, BEFORE capture. All buffers and their allocator/backing leases must
 * survive worker preparation; the root and selected snapshot name one cutoff.
 *
 * On OK, ownership of the already pinned snapshot and ctx lease transfers to
 * the worker. On error, ownership stays with the caller. prepare runs only on
 * the bulk worker, builds a COMPLETE session checkpoint (all buffers, views,
 * tabs/window, retained BASE/SAVE metadata), and returns JOURNAL_OK/error.
 * Its records/previous remain valid until release, also on failure. release
 * runs exactly once on the worker after snapshot retirement, on success or
 * failure, never on UI. It may perform final graph cleanup. The selected
 * buffer's arena/backing must survive file writing as well as preparation.
 * With no journal prepare may be NULL. Keep the journal quiescent BEFORE
 * request admission; defer new accepted edits only until the prepare handoff.
 * Those edits must be appended after handoff and included in CURRENT finish.
 * This bounds controller request work independently of session size; host
 * root publication, backend admission and actual frame latency remain host
 * obligations, not a controller-only G8s gate claim. */
typedef struct savectl_checkpoint {
    journal_base previous;
    const journal_record *records;
    size_t count;
} savectl_checkpoint;
typedef struct savectl_request {
    piece_snapshot *snapshot;
    void *ctx;
    int (*prepare)(void *ctx, const piece_snapshot *snapshot, savectl_checkpoint *out);
    void (*release)(void *ctx);
    uint64_t request_ns;
} savectl_request;
int savectl_save_request(savectl *s, const savectl_request *request);
/* Frame hook: while saving_frame_pending, render model.status ("saving") with
 * request_id even if worker completion arrived first. Call ONLY after backend
 * submission of that exact status frame succeeds, with its monotonic timestamp.
 * Busy/rejected backend attempts leave pending true. Returning from save,
 * preparing a frame or enqueuing render work is not submission. Old IDs and
 * duplicate/regressing timestamps are rejected. G8s measures request_ns to
 * saving_submitted_ns; the future editor test must exercise large dirty roots
 * and busy backends through this hook and record the actual submitted frame. */
int savectl_status_frame_submitted(savectl *s, uint64_t request_id, uint64_t submitted_ns);
/* Complete CURRENT checkpoint, including all edits accepted after save ack.
 * Use savectl_saved_base for new BASE. Data immutable until lease returned.
 * Retry append/directory errors with journal_retry off-path, then finish again.
 * fdatasync/writeback loss leaves journal_retry returning JOURNAL_IO: use
 * recover_finish with a COMPLETE CURRENT-session checkpoint instead. Retain
 * every other in-flight token's BASE/SAVE metadata. Never reappend accepted
 * edits, discard the token, or acknowledge saved while recovery is suspended. */
int savectl_finish(savectl *s, const journal_record *checkpoint, size_t count);
/* Off-path worker recovery: fresh-inode complete-session rotation, then finish
 * reconciliation against saved_base. Requires a quiescent private journal and
 * immutable records through returned lease. Any failure preserves needs_finish
 * and the retained token, including ambiguous rename/directory errors. */
int savectl_recover_finish(savectl *s, const journal_record *checkpoint, size_t count);
const journal_base *savectl_saved_base(const savectl *s);
/* Borrowed UI token, available once prepare hands back journal ownership,
 * including during file work. NULL while journal_leased. Copy before close. */
const journal_save *savectl_save_token(const savectl *s);
/* File/focus events: invalidation is immediate, checking is on a worker.
 * Events cancel a save logically via a monotonic epoch; never hide its result.
 * Controller notifies caller through banner; caller cancels find/index too.
 * Self-generated file events are checked against the replacement baseline. */
void savectl_file_event(savectl *s);
int savectl_reload(savectl *s);
int savectl_keep(savectl *s);
/* Per frame/mailbox wake, CPU only. A sealed completion lease is received
 * exclusively through the mailbox. Full-mailbox publication yields the bulk
 * lane and retries until terminal delivery. tick selectively receives this
 * controller; the host must also drain foreign shared-pool traffic. */
/*
 * Reload installation is explicit: take_reload builds at most 64 original
 * chunks per call, returning BUSY with outputs unchanged between slices. It
 * builds metadata on the UI
 * over worker-read, validated private bytes and returns the replacement tree,
 * clamping byte offsets to EOF, preserving horizontal scroll. Caller retires
 * its old tree/undo/index and installs this tree atomically on UI. If a new edit
 * arrived during a silent reload it becomes a banner instead of losing edits.
 * reload_allocator must be supplied: alloc is UI-only; free must be safe on
 * any snapshot-release thread. It must outlive the returned replacement tree. */
bool savectl_receive(savectl *s, const work_msg *message);
void savectl_tick(savectl *s);
int savectl_take_reload(savectl *s, piece_tree **tree, savectl_view *view);
#endif
