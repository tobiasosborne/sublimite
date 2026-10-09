/* P1.5: UI-thread inverse log. The tree outlives the log; all mutations of
 * that tree must go through this log until undo_clear/undo_destroy.
 * Records (one contiguous add span each) cost exactly 64 bytes from a base
 * pool. init reserves max_records + 8 slots, the extra 512 B is fixed scratch
 * for capturing insert redo refs (plus mmap page rounding and the caller-owned
 * control block). No allocation by undo after init; piece calls use the tree
 * allocator as specified in piece.h. Cursor blobs are
 * copied byte-for-byte and never interpreted. Zero-length edits are no-ops.
 *
 * Groups: adjacent inserts, backspaces, or forward deletes of the SAME kind,
 * with nondecreasing timestamps <= 300 ms apart, join a burst. Explicit groups
 * may contain arbitrary operations; nesting is rejected. Empty groups do not
 * clear redo. break_burst closes automatic grouping only.
 * Cap: 1..max_records span records, across undo AND redo. Drop whole oldest
 * groups, never part of a group. A single oversized group is discarded at its
 * end (or on crossing the cap for a burst). Trimming is deferred while an
 * explicit or partially replayed group is open; groups exceeding the
 * physical reserve reject further edits with NOMEM. If reducing the cap would
 * drop an undone prerequisite, discard all redo. No document bytes are freed.
 *
 * Errors: edit failures leave content/history/redo unchanged. Batch replay is
 * atomic PER piece operation, not per group (piece.h has no transactions).
 * On error change reports the successful prefix, including records/groups,
 * merged dirty range and last completed group's cursor. A partial group locks
 * editing, cap changes, group begin and opposite replay (BUSY); retry the same
 * direction to finish, or clear history to accept the partial document. If
 * reference expansion exhausts the fixed pool, clear is the recovery path. Return
 * errors even when progress occurred; always inspect change before rendering.
 * Dirty [off,off+len) conservatively covers pre/post bytes in their respective
 * coordinate spaces; from a length-changing mutation to EOF is dirty. The
 * endpoint may exceed current EOF after deletions. Batch merges once, and
 * restores the cursor of the last fully completed group. has_state is false
 * until a complete group has replayed. At history end return OK with no change.
 */
#ifndef EDIT_UNDO_H
#define EDIT_UNDO_H
#include "base/base.h"
#include "piece/piece.h"
#define UNDO_OK PIECE_OK
#define UNDO_ERR_RANGE PIECE_ERR_RANGE
#define UNDO_ERR_NOMEM PIECE_ERR_NOMEM
#define UNDO_ERR_BUSY 3
#define UNDO_BURST_NS UINT64_C(300000000)
#define UNDO_STATE_BYTES 16
typedef struct undo_state { uint8_t bytes[UNDO_STATE_BYTES]; } undo_state;
typedef enum undo_kind { UNDO_INSERT = 1, UNDO_BACKSPACE = 2,
                         UNDO_DELETE = 3 } undo_kind;
typedef struct undo_change {
    uint64_t off, len;
    size_t records, groups;
    int has_state;
    undo_state state;
} undo_change;
typedef struct undo_stats {
    size_t records, undo_groups, redo_groups, live_bytes, reserved_bytes;
    /* live_bytes: pool live counter x stride; reserved_bytes: actual mmap. */
} undo_stats;
/* Caller-owned; fields private. No copies while initialized. */
typedef struct undo_log {
    edit_pool pool;
    piece_tree *tree;
    uint32_t head, tail, cursor;
    size_t count, cap, max_records;
    uint64_t last_time, last_off, last_len;
    undo_kind last_kind;
    int burst, open, partial;
    uint32_t open_first;
    undo_state open_before;
} undo_log;
int undo_init(undo_log *u, piece_tree *tree, size_t max_records);
void undo_destroy(undo_log *u);
void undo_clear(undo_log *u);
void undo_break_burst(undo_log *u);
int undo_set_cap(undo_log *u, size_t records);
int undo_group_begin(undo_log *u, const undo_state *before);
int undo_group_end(undo_log *u, const undo_state *after);
int undo_insert(undo_log *u, uint64_t off, const uint8_t *data, size_t len,
                uint64_t time_ns, const undo_state *before,
                const undo_state *after);
int undo_delete(undo_log *u, uint64_t off, uint64_t len, undo_kind kind,
                uint64_t time_ns, const undo_state *before,
                const undo_state *after);
int undo_undo(undo_log *u, size_t groups, undo_change *change);
int undo_redo(undo_log *u, size_t groups, undo_change *change);
undo_stats undo_get_stats(const undo_log *u);
#endif
