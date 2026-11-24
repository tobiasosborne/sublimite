#ifndef UNDO_UNDO_H
#define UNDO_UNDO_H

#include "piece/piece.h"

typedef struct undo_log undo_log;

/* Errors caused by admission, shape or source validity. MORE is a replay
 * yield. Call-sequence and argument preconditions use ASSERT (A15). */
typedef enum undo_err {
    UNDO_OK = 0,
    UNDO_ERR_NOMEM,       /* base refused history/checkpoint/piece storage */
    UNDO_ERR_RANGE,       /* replacement exceeds the piece height limit */
    UNDO_ERR_CONFLICT,    /* live backing identity or closing fault poll failed */
    UNDO_MORE
} undo_err;

/* (G) Inherited v1 cursor-state slot, v1 undo.h at the oracle pin.
 * Undo transports its 16 B unchanged (P, two 64-bit coordinates). */
#define UNDO_STATE_BYTES 16u

typedef struct undo_state { uint8_t bytes[UNDO_STATE_BYTES]; } undo_state;

typedef enum undo_replay_direction {
    UNDO_REPLAY_UNDO, UNDO_REPLAY_REDO
} undo_replay_direction;

/* Value cursors. Clients treat the pointer members as private. */
typedef struct undo_splice_iter {
    const undo_log *log;
    const struct undo_record *next;
} undo_splice_iter;

typedef struct undo_prepared_splice {
    uint64_t off, removed;
    size_t span_count;
    const struct undo_record *record;
    undo_replay_direction direction;
} undo_prepared_splice;

typedef struct undo_history {
    size_t undo_groups, redo_groups, evicted_groups;
    uint64_t group_serial;
} undo_history;

typedef struct undo_stats {
    size_t records;             /* published replay records */
    size_t history_bytes;       /* published slot, external and literal charges */
    size_t span_payload_bytes;  /* external entries, including pending and retired */
    size_t storage_bytes;       /* page-rounded history extents, including pending and retired */
} undo_stats;

/* Objects and threads. The UI owns one log per recorded tree. The tree
 * outlives the log and owns text, ADD bytes, the mapped backing and its fault
 * id. Undo owns history slots, span extents, the checkpoint while a command
 * or replay group is open, and the temporary replay views. History records
 * retain zero tree roots. Once a log exists, only undo may edit the tree or
 * call piece_checkpoint_* on it. Trees without history use piece directly. */

/* For mapped trees, piece_validate_fn supplies the owner's live identity check.
 * A same-size external overwrite can leave a pristine fault epoch and the
 * owner's asynchronous notification can arrive later. Undo checks at the
 * publication boundary and around replay mutations. The callback may inspect
 * the backing, must not edit the tree or log, and must not wait. Undo also
 * polls the fault id itself. */

/* Default published history budget. undo_create still takes a caller's cap. */
size_t undo_default_cap_bytes(void);

/* Creates an empty log. O(1), one base extent. NOMEM if base refuses it.
 * ASSERT: out and tree are non-NULL, cap_bytes > 0, and mapped trees have a
 * non-NULL validator. Private-copy trees may pass NULL. ctx is borrowed. */
undo_err undo_create(undo_log **out, piece_tree *tree, size_t cap_bytes,
                     piece_validate_fn validate, void *ctx);

/* Destroys the log and releases every history extent. O(records). Cannot
 * fail. ASSERT: live log, tree still open, no prepared replay. */
void undo_destroy(undo_log *u);

/* Clears undo and redo, aborting an open command or applying replay.
 * O(records). Cannot fail. ASSERT: live log, no failed command or prepared
 * replay. Committed ADD is still owned by the tree. */
void undo_clear(undo_log *u);

/* Changes the published byte cap, evicts whole oldest groups and performs
 * one bounded retirement pass. O(1) bounded boundary visits and frees.
 * Cannot fail. ASSERT: cap_bytes > 0, live idle log. */
void undo_set_cap(undo_log *u, size_t cap_bytes);

/* Releases up to record_budget retired records and wholly unused extents.
 * Returns records released, or zero while a checkpoint is open. O(records
 * released plus one boundary check). Cannot fail. ASSERT: live log. */
size_t undo_maintain(undo_log *u, size_t record_budget);

/* O(1) boundary counters. While a transaction is pending, published counts
 * stay at its starting boundary and storage stats include staged storage.
 * Cannot fail. ASSERT: live log. */
undo_history undo_get_history(const undo_log *u);
undo_stats undo_get_stats(const undo_log *u);

/* Opens a command and its tree checkpoint with the starting cursor state.
 * O(1) bounded retirement plus the checkpoint claim. NOMEM if base refuses
 * the checkpoint. ASSERT: live idle log, before != NULL.
 *
 * Edits in a command belong to one group. join allows the command to extend
 * the previous group. Undo starts a new group if structural adjacency
 * fails or the previous group is gone. Render and save at command boundaries. */
undo_err undo_begin(undo_log *u, const undo_state *before, bool join);

/* Describes or captures the removed range and replaces it with bytes in the
 * unpublished tree. Records remain pending. O(piece description and mutation
 * costs, plus literal length or external span count when required). NOMEM if
 * admission fails, RANGE at the piece height limit. After an error the only
 * legal call on the log is undo_abort. ASSERT: open command without a prior
 * error, valid range and replacement size, and bytes per piece_replace. */
undo_err undo_replace(undo_log *u, uint64_t off, uint64_t del,
                      const uint8_t *bytes, size_t len);

/* Validates the mapped source after the last dependent work, closes the
 * checkpoint and publishes history on success. A forward command supplies
 * its final cursor state. Cap eviction and redo detachment apply to nonempty
 * edits. Replay supplies the saved target and moves only the applied position.
 * O(1) on success, including bounded eviction. CONFLICT aborts the checkpoint.
 * ASSERT: open forward command without a prior edit error, after != NULL,
 * or prepared replay with after pointing to its borrowed target. */
undo_err undo_commit(undo_log *u, const undo_state *after);

/* Aborts the tree checkpoint and drops pending history, preserving the
 * published cursor, redo and cap. O(staged records and extents).
 * Allocates nothing and cannot fail. ASSERT: open or failed forward command,
 * or applying or prepared replay. */
void undo_abort(undo_log *u);

/* Applies or resumes one group. On OK, *target borrows its saved cursor
 * state if a group is prepared. Empty history yields OK and NULL. An
 * absolute os_now_ns() deadline (or OS_DEADLINE_NEVER) is checked after at
 * least one available mutation. MORE keeps the checkpoint open. NOMEM,
 * RANGE or CONFLICT aborts the group. MORE and errors clear *target.
 *
 * ASSERT: live log, valid direction, target != NULL, and log idle or resuming
 * a yield in the same direction and revision. While the checkpoint is open,
 * piece operations that end the live revision (including piece_lines_resolve)
 * are prohibited. The owner may take a candidate snapshot after preparation.
 * History counters remain at entry until the owner commits. */
undo_err undo_replay_prepare(undo_log *u, undo_replay_direction direction,
                             uint64_t deadline, const undo_state **target);

/* Starts an allocation-free traversal of the complete prepared group.
 * Initialize after successful nonempty preparation. Cursors, results and
 * the borrowed target are valid until the owner commits or aborts. */
void undo_splice_iter_init(undo_splice_iter *it, const undo_log *u);
/* Returns the next splice in application order. At end returns false and
 * leaves *out untouched. A pure delete has zero insertion spans. O(1). */
bool undo_splice_iter_next(undo_splice_iter *it, undo_prepared_splice *out);
/* Returns the indexed insertion span by value in document insertion order.
 * ASSERT: index < splice->span_count. O(1). */
piece_span undo_splice_span(const undo_prepared_splice *splice, size_t index);

/* O(1) borrowed pre-group view during a multi-record replay, otherwise NULL.
 * Retain it to keep it after the group boundary. Cannot fail.
 * ASSERT: live log. */
piece_snapshot *undo_replay_snapshot(const undo_log *u);
#endif
