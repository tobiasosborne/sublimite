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
    int (*validate_source)(void *ctx);
    void *source_ctx;
    journal *journal;
    work_pool *journal_pool; /* actual private journal pool, must differ */
    piece_allocator reload_allocator; /* UI-owned arena for replacement tree */
    uint64_t buffer_id;
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
/* CPU only: update latest live offsets; used at reload installation time. */
void savectl_set_view(savectl *s, savectl_view view);
savectl_model savectl_get_model(const savectl *s);
/* Snapshot + enqueue + SAVING. Checkpoint and previous are required only with
 * a journal. Records/data must stay immutable until needs_finish or failure.
 * Snapshot cutoff and complete PREPARE checkpoint must describe the same UI
 * state. No UI journal operations while leased; defer edits in caller storage.
 * A retained failed token forbids a fresh save (recovery must resolve it). */
int savectl_save(savectl *s, piece_tree *tree, const journal_base *previous,
                 const journal_record *checkpoint, size_t count);
/* Complete CURRENT checkpoint, including all edits accepted after save ack.
 * Use savectl_saved_base for new BASE. Data immutable until lease returned.
 * Finish can be retried after journal_retry off-path; retained token survives. */
int savectl_finish(savectl *s, const journal_record *checkpoint, size_t count);
const journal_base *savectl_saved_base(const savectl *s);
const journal_save *savectl_save_token(const savectl *s);
/* File/focus events: invalidation is immediate, checking is on a worker.
 * Events cancel a save logically via a monotonic epoch; never hide its result.
 * Controller notifies caller through banner; caller cancels find/index too.
 * Self-generated file events are checked against the replacement baseline. */
void savectl_file_event(savectl *s);
int savectl_reload(savectl *s);
int savectl_keep(savectl *s);
/* Per frame/mailbox wake, CPU only. Completion survives a full work mailbox.
 * receive routes this controller's notifications; tick also works without it.
 * Reload installation is explicit: take_reload builds metadata on the UI
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
