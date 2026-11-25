#ifndef EDIT_EDIT_H
#define EDIT_EDIT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "undo/undo.h"

typedef struct edit_buffer edit_buffer;
struct journal;

/* MODEL. One UI owner serializes calls on a buffer. A boundary is the
 * state after a whole command, including text, selection and history
 * position. The published boundary is the latest one eligible for a
 * new-text frame and a successful command reply (ACK). A fence holds
 * both back until the last record needed for recovery has reached the
 * named journal's kernel page cache. Until then readers see the previous
 * published snapshot. A cutoff is the published boundary captured by an
 * admitted save. Its content ID identifies the bytes that save will write.
 * A bookmark associates an owned ID with its still-reachable undo boundary.
 * An embargo is the journal owner's permanent rule that restores from a
 * particular displaced source generation must henceforth carry its bytes.
 * A generation identifies the original file bytes on which recovery
 * depends. BASE records establish a buffer's starting text in a named
 * journal image (the current physical record stream). A restore may name
 * a verified declared generation. An embargoed restore carries literal
 * bytes from the pinned store. ADD is piece's append-only byte store.
 * Edit owns the provisional mutation, history, selection, identities,
 * publication pins, change and source-coordinate descriptors. Journal
 * owns policy, exact record reservations, offsets, sequence and writes.
 * File validates mapped backing. The session orders save capture and
 * edit publication on the UI. Only the UI writes the named image.
 *
 * Before journal_command_begin, size the entire plan with
 * JOURNAL_LITERAL_LIMIT and journal_range_allowed. Reserve edit's exact
 * descriptors, source holds, change/reply slots and candidate snapshot.
 * Begin funds every member and the final record including its retry.
 * Get the window with journal_command_payload. NULL means a held frame
 * still owns it. Wait before filling the next record. Validate each mapped
 * load. For a group, journal_command_member returning OK transfers this
 * frame even on a short write. Advance edit's cursor exactly once. WAIT
 * keeps the filled window with edit. Retry it with the same cursor.
 * For a singleton, fill its funded final payload. After filling it or
 * consuming all grouped members, read journal_state. Only JOURNAL_HEALTHY
 * permits undo_commit. Check health, commit and issue final in one dispatch.
 * For a group all members must reach the named inode before RAM commit.
 * A source or capacity failure while provisional restores the old text
 * and history. Abort any open journal command, release provisional pins,
 * then abort any open undo checkpoint.
 * Accepted records retain their physical sequence and have no text
 * effect without the final. A short member write blocks the physical
 * stream until journal_reconcile accounts for the held record.
 *
 * When all sources and claims are ready, call undo_commit once.
 * On closing CONFLICT, in this dispatch call journal_command_abort,
 * release the candidate and every other provisional pin, then call
 * undo_abort. Return EDIT_CONFLICT. On success call journal_command_final
 * in the same dispatch. The sole remaining write is the standalone record
 * or the group's empty COMMAND_COMMIT. It needs no source reads. A complete
 * write opens the publication fence. With a failed final retain its request,
 * the committed candidate and prior published boundary in FINAL_PENDING.
 * Expose the journal error and withhold the new frame and matching ACK.
 * journal_request_result identifies acceptance after reconciliation.
 * Call journal_request_release once adopted. Every abort exit has a release test.
 *
 * A new incarnation starts at content ID 1 (G, first nonzero identity).
 * ID 0 means unknown (G, sole reserved ID). A nonempty forward command
 * takes a fresh ID even when joined. Undo/redo restore an owned reachable
 * ID or take a fresh one. Revision counts committed text commands including
 * replay, starting at zero; unlike content ID it never goes backwards.
 * Check counters before commit; never wrap. Bytes equal by coincidence
 * do not establish saved identity. For a saved or in-flight cutoff keep
 * its ID even after eviction. Its position, while reachable, is the count
 * of evicted groups plus the count of applied groups at capture. Discard
 * positions in discarded redo, at an endpoint replaced by joining, and
 * outside surviving history. Determine actual joining from undo's group
 * serial. Restore identity by matching reachable positions on replay.
 *
 * All offsets and lengths are bytes. Required object/output pointers,
 * live owners, valid enum values, nonaliasing valid input storage, UI
 * ownership and nonreentrancy are caller invariants checked with ASSERT.
 * File contents, input bounds, deadlines, writer suspension and resource
 * exhaustion are results. All input arrays are borrowed through the call
 * only. All costs below are (P), from the operations named. */
typedef enum edit_phase {
    EDIT_IDLE, EDIT_OPEN, EDIT_REPLAYING, EDIT_FINAL_PENDING, EDIT_READY
} edit_phase;
typedef enum edit_result {
    EDIT_OK, EDIT_WAIT, EDIT_MORE, EDIT_NOMEM, EDIT_RANGE, EDIT_LIMIT,
    EDIT_CONFLICT, EDIT_SUSPENDED
} edit_result;
/* OK completes the command, including a no-op. WAIT leaves the request
 * at the FIFO head for admission. MORE gives the admitted command a
 * continuation. NOMEM reports a refused base claim. RANGE reports an
 * invalid byte interval, selection, resulting length or piece height.
 * LIMIT reports exhausted identity, revision or wire arithmetic. CONFLICT
 * reports a source that failed verification before commit. SUSPENDED
 * exposes the journal's recovery error. The phase distinguishes blocked
 * admission from a committed command waiting for repair. */
typedef struct edit_selection {
    uint64_t cursor, anchor;
} edit_selection;
typedef enum edit_mark_kind { EDIT_MARK_ABSENT, EDIT_MARK_SET } edit_mark_kind;
typedef struct edit_selection_state {
    edit_selection active, mark;   /* mark commands consume mark when SET */
    edit_mark_kind mark_kind;
} edit_selection_state;
typedef struct edit_boundary {
    piece_snapshot *snapshot;      /* borrowed; retain before the next call */
    edit_selection_state selection;
    uint64_t content_id, revision;
} edit_boundary;
typedef struct edit_change {
    uint64_t off, old_len, new_len, revision;
} edit_change; /* replace this envelope of the previous published snapshot
                * with this range of the new one; unchanged gaps may occur */
typedef struct edit_splice {
    uint64_t off, remove;
    const uint8_t *bytes;          /* NULL permitted for an empty insertion */
    size_t length;
} edit_splice;
typedef enum edit_kind {
    EDIT_TYPE, EDIT_DELETE_FORWARD, EDIT_BACKSPACE
} edit_kind;
typedef enum edit_save_event {
    EDIT_SAVE_CAPTURED, EDIT_SAVE_INSTALLED, EDIT_SAVE_RELEASED,
    EDIT_SAVE_FORGOTTEN
} edit_save_event;

/* The options carry the fields named below; edit_create copies them and
 * does not retain the options object.
 * On success edit adopts tree and starts an empty history with the
 * caller's history_cap_bytes. Config supplies the default of 32,768
 * records (G, v1 cfg.history_keys at buffers.c:256) times 529 B (P,
 * two 264 B undo slots plus one short input byte on LP64), or
 * 17,334,272 B (P, their product). The layout test checks this charge. The implementer places the charge
 * and product beside the config constant. The file owner's live validator
 * and its context outlive the log. Mapped text requires validate. Journal outlives
 * the buffer. buffer_id names its existing recoverable BASE (including an
 * empty untitled BASE). Open or recovery supplies current_id and saved_id.
 * A fresh clean file supplies the initial ID for both. A restored saved_id
 * may be unknown or unreachable. Allocate future IDs past
 * both. The host settles old save tokens before changing incarnation.
 * The selection starts collapsed at the beginning with the mark absent.
 * The UI may retain a non-owning tree pointer for off-path
 * line-count adoption at an idle boundary, valid only until destroy.
 * Creation claims O(1) control, log and snapshot storage. NOMEM leaves
 * *out NULL and tree with the caller. The caller supplies an unlogged tree,
 * a journal, an output pointer and nonzero buffer_id, cap and current_id. */
typedef struct edit_options {
    piece_tree *tree;
    struct journal *journal;
    uint64_t buffer_id;
    size_t history_cap_bytes;
    piece_validate_fn validate;
    void *ctx;
    uint64_t current_id, saved_id;
    /* Verified mapped ORIGINAL key from OPEN, fixed for this tree. Zero
     * selects by-value restores. The journal can embargo this key. */
    uint64_t original_generation;
} edit_options;
edit_result edit_create(edit_buffer **out, const edit_options *options);
/* Destroy releases the log, snapshot, tree and storage off the typing path.
 * It cannot fail and costs the work of draining remaining owners and records.
 * The buffer must be IDLE with the cutoff settled and kept text secured
 * or discard authorized. */
void edit_destroy(edit_buffer *b);

/* Copy the last published boundary, including during a fence or yield.
 * Return the actual transaction phase. This takes O(1) and cannot fail. */
edit_phase edit_read(const edit_buffer *b, edit_boundary *out);
/* Install byte selection/mark for motion or a mark command. Both ends of
 * each active selection must be within published length. A change seals
 * the burst. This takes O(1) and returns RANGE for invalid endpoints or
 * WAIT while a command owns the boundary. It creates no text revision,
 * journal edit, history group or change record. */
edit_result edit_select(edit_buffer *b, edit_selection_state selection);

/* Apply one splice and collapse the caret just after its insertion.
 * receipt_ns is the monotonic input timestamp used
 * for burst comparison. The kind distinguishes ordinary text, forward
 * delete and backspace. Paste and compound commands use edit_apply_plan.
 * Join only a pure insertion or deletion of the same committed ordinary kind,
 * collapsed selection, empty redo and elapsed time in [0, 300 ms] on
 * the monotonic clock (G, v1 UNDO_BURST_NS at oracle pin 6fec2e8).
 * Type starts at the prior insertion end, forward delete at its start,
 * backspace ends at its start. Paste, compound replacement, a selection
 * change, replay or save capture seals the preceding burst.
 * This costs one single-splice plan plus an O(1) burst comparison and
 * returns the same results as edit_apply_plan. */
edit_result edit_replace(edit_buffer *b, edit_kind kind, edit_splice splice,
                         uint64_t receipt_ns);
/* Execute the ordered splices as a single separate command. Each offset
 * refers to the text immediately before that splice. Install after in the
 * final text and rebase the mark through the actual splices. Empty sequences
 * can change selection alone. The caller provides a readable array for a
 * nonzero count and storage independent of mutation. Check ranges and
 * acquire this buffer's command admission. Execute undo_begin and
 * undo_replace on the UI. Describe each insertion in ADD coordinates,
 * even if another splice removes it. Consume borrowed input before
 * returning. OPEN may yield across turns with owned descriptors and
 * pins. Emit and close using the common protocol above. The cost is
 * O(count) plus mutation, encoding and abort cleanup. WAIT preserves
 * the request for admission, MORE resumes provisional work, and
 * SUSPENDED exposes an unresolved physical failure. A provisional
 * refusal preserves the prior published boundary. */
edit_result edit_apply_plan(edit_buffer *b, const edit_splice *splices,
                            size_t count, edit_selection after);

/* Replay one whole history group in the given direction, through undo's
 * sliced replay and the same journal fence as forward edits. deadline_ns
 * is absolute on os_now_ns(), or OS_DEADLINE_NEVER. Empty history returns
 * OK without a change. A deadline permits a yield after one available mutation.
 * Once the group is prepared, walk its source-coordinate splices in
 * application order, slicing staging across turns as needed. Seal the
 * descriptors, take the candidate snapshot and emit every precommit member before calling undo_commit.
 * On staging failure abort any open journal command, release provisional
 * pins, then call undo_abort. While yielded, expose the old
 * published snapshot and block mutation, resolve, cap changes and save
 * capture. Only the completed group restores cursor and anchor and rebases
 * the existing mark. Each slice costs its mutations and staging work.
 * The command returns the same results as edit_apply_plan. */
edit_result edit_replay(edit_buffer *b, undo_replay_direction direction, uint64_t deadline_ns);
/* Resume provisional OPEN or REPLAYING work from owned cursors.
 * Use the replay clock and honor the deadline between ready units.
 * Resume at edit's first unconsumed member. Repair never moves this cursor.
 * In FINAL_PENDING inspect the original final request and publish once
 * it is accepted. This costs O(1) and returns SUSPENDED while the
 * physical gap persists. In IDLE and READY this returns OK. */
edit_result edit_continue(edit_buffer *b, uint64_t deadline_ns);
/* After submit or headless reply delivery, consume the single
 * pending text change. Success moves READY to IDLE. Otherwise the call
 * returns false and preserves *out. It takes O(1) and cannot fail. The
 * UI consumer distributes the change and resets its preferred column. */
bool edit_take_change(edit_buffer *b, edit_change *out);

/* This O(1) notification uses fixed bookmark slots and cannot fail within
 * the session owner's serialized transition. CAPTURED seals joining
 * and pins the current ID and boundary. INSTALLED transfers that bookmark
 * to saved even if its position has become unreachable.
 * RELEASED drops the cutoff after definitive cancellation/failure.
 * The caller matches the admitted incarnation and holds at most one cutoff
 * (G, one active save per controller). CAPTURED requires a complete
 * published boundary. INSTALLED and RELEASED require the matching owned
 * cutoff. FORGOTTEN clears only the saved bookmark, is idempotent and
 * requires no cutoff. Deliver it in the same UI publication as
 * invalidation of the saved association, including Keep, detachment and
 * idle conflict. Preserve any independent in-flight bookmark. On rejection
 * of a complete FINISHED at verification, release the matching edit
 * cutoff and forget saved on entry to IDLE_CHANGED. The journal cutoff and
 * index claim survive through durable conflict protection. In other
 * ambiguous cases retain the edit cutoff until its definitive disposition.
 * Deliver INSTALLED on verified durable install or RELEASED once on
 * definitive non-install if still owned. (Erratum: L5 item 1, signed by the
 * advisor 2026-11-25.) */
void edit_save_boundary(edit_buffer *b, edit_save_event event);

/* The UI calls this after submit or upon open or activation outside
 * input-to-submit. The caller derives each budget at its call site and
 * documents its evidence under Law 4.
 * The budgets count reclaim steps, resolve node visits and retired undo
 * records respectively. Call reclaim, resolve, replenish, undo_maintain
 * in order. After resolve refresh the published snapshot via snapshot_take
 * so readers see newly exact geometry at the same text revision. Keep the
 * previous snapshot if that claim fails and try again next turn. Cost is
 * their bounded walks, the snapshot claim and actual top-up. Attempt all
 * on an eligible turn and return NOMEM if resolve, take or top-up was refused.
 * This is a maintenance observation only. WAIT skips all work during an
 * open command, replay or pending final. The caller must be outside the typing window. */
edit_result edit_maintain(edit_buffer *b, size_t reclaim_steps,
                          size_t resolve_visits, size_t undo_records);
#endif
