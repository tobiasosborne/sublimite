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

typedef struct undo_change {
    uint64_t off, len;           /* dirty range for repaint */
    size_t groups;               /* subtract this when resuming */
    bool has_state;              /* a completed group supplied cursor state */
    undo_state state;           /* cursor state for the last completed group */
} undo_change;

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

/* For mapped trees, this callback supplies the owner's live identity check.
 * A same-size external overwrite can leave a pristine fault epoch and the
 * owner's asynchronous notification can arrive later. Undo needs this check
 * at the publication boundary because sliced replay can commit multiple
 * groups internally. The callback may inspect the backing, must not edit
 * the tree or log, and must not wait. Undo also polls the fault id itself. */
typedef bool (*undo_validate_fn)(void *ctx, const piece_backing *backing);

/* Creates an empty log. O(1), one base extent. NOMEM if base refuses it.
 * ASSERT: out and tree are non-NULL, cap_bytes > 0, and mapped trees have a
 * non-NULL validator. Private-copy trees may pass NULL. ctx is borrowed. */
undo_err undo_create(undo_log **out, piece_tree *tree, size_t cap_bytes,
                     undo_validate_fn validate, void *ctx);

/* Destroys the log, aborting an open command or replay and releasing every
 * history extent. O(records). Cannot fail. ASSERT: live log, tree still open. */
void undo_destroy(undo_log *u);

/* Clears undo and redo, aborting an open command or replay. O(records).
 * Cannot fail. ASSERT: live log. Committed ADD is still owned by the tree. */
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

/* Validates the mapped source after the last dependent work, commits the
 * checkpoint and publishes history with the supplied final cursor state.
 * Publication evicts whole groups at the cap and detaches redo on a nonempty
 * edit. O(1) on success, including bounded eviction. CONFLICT on a failed
 * closing validation. It aborts the command in that case. ASSERT: open
 * command without a prior edit error, after != NULL.
 *
 * L2 may prepare journal records from the staged text after its last
 * undo_replace and before undo_commit. Keep that staging private until
 * success. On error discard the staging. The caller never opens a piece
 * checkpoint on this tree. */
undo_err undo_commit(undo_log *u, const undo_state *after);

/* Aborts the tree checkpoint and drops pending history, preserving the
 * published cursor, redo and cap. O(staged records and extents).
 * Allocates nothing and cannot fail. ASSERT: open forward command. */
void undo_abort(undo_log *u);

/* Replays up to groups whole groups in the selected direction. deadline is
 * absolute on os_now_ns(), or OS_DEADLINE_NEVER. With work available each
 * call makes one whole mutation before checking the deadline. MORE can
 * leave a group open. Resume in the same direction for the remaining group
 * count, subtracting change.groups. During a yield, the forward command
 * calls undo_begin/undo_replace/undo_commit/undo_abort ASSERT, as do
 * undo_set_cap and opposite replay. Do not call piece_lines_resolve or any
 * piece operation that ends the live revision. Resume detects a changed
 * revision by snapshot identity and ASSERTs.
 * Cost scales with the piece mutations executed.
 *
 * The dirty interval covers repaint through the larger pre/post EOF on
 * length changes. Only completed groups report cursor state.
 * NOMEM if a checkpoint,
 * mutation or replay view cannot be admitted. RANGE if a mutation exceeds
 * the piece height limit. CONFLICT on a failed backing check, including
 * after resume and before group publication. An error rolls back the current
 * group and reports only groups this call completed. Earlier committed
 * groups stay committed.
 * ASSERT: live log, change != NULL, no forward command, and a yielded group
 * is resumed in its direction with a nonzero group count. */
undo_err undo_undo(undo_log *u, size_t groups, uint64_t deadline, undo_change *change);
undo_err undo_redo(undo_log *u, size_t groups, uint64_t deadline, undo_change *change);

/* O(1) borrowed pre-group view during a mid-group replay yield, otherwise
 * NULL. Retain it to keep it after the group boundary. Cannot fail.
 * ASSERT: live log. */
piece_snapshot *undo_replay_snapshot(const undo_log *u);
#endif
