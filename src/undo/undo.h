/* UI-thread inverse log over piece.h. Tree must outlive the log; mutations
 * go through this log. Span records occupy 64 B. init reserves lazy virtual
 * capacity for worst-case capture expansion; no undo allocation after init.
 * Allocated records (including retired history) stay packed. Whole free tail
 * pages are decommitted. committed_bytes counts all touched/retained pages;
 * reserved_bytes is address space, not the physical G10f charge.
 *
 * Adjacent same-kind edits with nondecreasing timestamps <= 300 ms apart join
 * a burst. Explicit groups may mix operations; nesting is rejected. Boundary
 * cursor blobs are copied unchanged. Empty/failed edits retain redo.
 *
 * Cap counts active undo+redo span records. Eviction always detaches complete
 * groups. At most 16 boundaries are visited per trim; very large cap
 * reductions can discard extra oldest history to keep detachment bounded.
 * Each edit reclaims at most UNDO_RECLAIM_RECORDS retired records; call
 * undo_maintain between input checks to amortise the remainder. Retired records
 * remain owned memory and must be included in G10f until reclaimed. No piece
 * add-buffer bytes are freed. Explicit/partial groups are protected. Edit
 * admission uses max_records+8 slots; replay reserves expansion capacity for
 * all of them, so first-undo capture cannot permanently exhaust scratch.
 *
 * Edit failures leave content and logical history unchanged. Replay uses one
 * piece checkpoint per group. Any failure aborts the entire current group,
 * restoring exact pre-group bytes, refs and cursor/record metadata; previously
 * completed groups in the batch stay completed. Retry starts that group anew.
 * Undo-owned capture scratch is reserved at init and decommitted at boundaries.
 * Its live/committed/virtual bytes are included in stats (replay_records).
 *
 * A checkpoint spans every slice of its group. Edits, cap changes and opposite
 * replay are BUSY while partial; maintenance pauses until the boundary. Nothing
 * else may mutate the tree between slices, including a direct piece mutation.
 * This is asserted using the in-tree kernel's retained current-snapshot identity
 * (see P1.5e.md). Queries and immutable worker snapshots are permitted. Render/
 * save the borrowed pre-group view while partial. clear/destroy abort an open
 * replay checkpoint before discarding history/view. The tree must still live.
 *
 * Slice budgets count piece mutations, not expanded span records; a capture
 * may report up to eight records for one operation. deadline_ns is an absolute
 * CLOCK_MONOTONIC deadline (UINT64_MAX disables it). Check the deadline before
 * every operation; the frozen piece API cannot interrupt one costly mutation
 * or snapshot acquisition. UNDO_MORE means work remains, including a normal
 * yield inside a group. Retry the same direction with the remaining group
 * count (subtract change.groups), after checking input. Boundary state is
 * reported only on group completion. Legacy replay calls have no slice limit.
 *
 * change describes this call's prefix: records/operations, completed groups
 * and their last boundary state,
 * conservative dirty [off,off+len). Length changes dirty through the larger
 * pre/post EOF; the endpoint can exceed current EOF. Merge slices and submit
 * one final frame for the requested batch. MORE includes provisional operations
 * in the open group; on error that whole group's operations (including earlier
 * slices) are rolled back. The error's change includes only groups completed in
 * this call. Discard accumulated provisional counts for the aborted group; its
 * prior dirty intervals still conservatively cover restoration. Boundary state
 * is never reported for an aborted group. History end is OK/no change.
 */
#ifndef EDIT_UNDO_H
#define EDIT_UNDO_H
#include "base/base.h"
#include "piece/piece.h"
#define UNDO_OK PIECE_OK
#define UNDO_ERR_RANGE PIECE_ERR_RANGE
#define UNDO_ERR_NOMEM PIECE_ERR_NOMEM
#define UNDO_ERR_BUSY 3
#define UNDO_MORE 4
#define UNDO_RECLAIM_RECORDS 16u
#define UNDO_BURST_NS UINT64_C(300000000)
#define UNDO_STATE_BYTES 16
typedef struct undo_state { uint8_t bytes[UNDO_STATE_BYTES]; } undo_state;
typedef enum undo_kind { UNDO_INSERT = 1, UNDO_BACKSPACE = 2,
                         UNDO_DELETE = 3 } undo_kind;
typedef struct undo_change {
    uint64_t off, len;
    size_t records, groups, operations;
    int has_state;
    undo_state state;
} undo_change;
typedef struct undo_stats {
    size_t records, undo_groups, redo_groups, live_bytes, reserved_bytes;
    size_t retired_records, committed_bytes;
    size_t replay_records;
    /* live_bytes includes retired/capture scratch slots; committed_bytes
     * includes page slack in both mappings. */
} undo_stats;
/* Caller-owned; fields private. No copies while initialized. */
typedef struct undo_log {
    edit_pool pool;
    piece_tree *tree;
    uint32_t head, tail, cursor;
    size_t count, cap, max_records, applied_count;
    size_t retired_count, page_bytes, committed_bytes;
    uint32_t retired_head, retired_tail, replay_first;
    piece_snapshot *replay_view;
    piece_checkpoint *replay_checkpoint;
    piece_snapshot *replay_guard;
    edit_pool replay_pool;
    size_t replay_committed_bytes, replay_fresh, replay_count, replay_applied;
    uint32_t replay_cursor, replay_tail, replay_last;
    undo_state replay_first_after, replay_last_before, replay_last_after;
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
int undo_undo_slice(undo_log *u, size_t groups, size_t operation_budget,
                    uint64_t deadline_ns, undo_change *change);
int undo_redo_slice(undo_log *u, size_t groups, size_t operation_budget,
                    uint64_t deadline_ns, undo_change *change);
/* Borrowed until group completion/clear/destroy; NULL outside a multi-record
 * group's replay. Retain it yourself if a worker needs a longer lifetime. */
const piece_snapshot *undo_replay_snapshot(const undo_log *u);
/* Reclaim <= record_budget slots, decommit wholly unused tail pages; returns
 * slots reclaimed. Returns zero while a replay checkpoint is active. */
size_t undo_maintain(undo_log *u, size_t record_budget);
undo_stats undo_get_stats(const undo_log *u);
#endif
