# Edit kernel · proposed contract for advisor signature

## Send-back disposition

| Item | Disposition for signature |
|---|---|
| E1 | Adopt owner-committed replay. Undo prepares one group and lends an ordered splice iterator. Edit can still abort after the last mutation. |
| E2 | Adopt the Q7 amended grant table. A waiting key stays at the FIFO head before RAM mutation. |
| E3 | Adopt streaming from pinned source stores. A failed mapped read after memory commit suspends emission with the frame and ACK fenced. |
| E4 | Adopt exact descriptor reservations before commit, with a release test for each abort path. |
| E5 | Adopt the caller's configured byte cap. The LP64 default is 17,334,272 B (P, parity depth times charge, derived below). |
| E6a | Adopt the cut. The layout caller owns the preferred column and resets it on change consumption. |
| E6b | Adopt REPLAYING with direction. The five phase tags (P, one per transaction-table row) cover both directions. |
| E6c | Adopt caller-derived budgets. The edit bead brief must name the host owner responsible for deriving each argument. |
| E7 | Adopt full sentences for the function contracts. |

A replacement has two observable completion points. Undo commits the text and history together. The editor may show or acknowledge that text when the complete journal command has reached the named journal's kernel page cache. Between those points the command belongs to edit, including responsibility for completing it after a short write. This proposal covers the production headless and GUI paths. Command-plan producers are a separate bead.

Quantities marked (G) are product or contract choices; (P) quantities follow from the indicated operations or layouts. Digits in identifiers, source pins, language versions and citations name their sources.

## Proposed src/edit/edit.h

```c
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
 * a verified declared generation. After embargo, journal streams its
 * bytes by value from a pinned source during EMITTING. ADD is piece's
 * append-only byte store. Journal writes ADD and literals by value.
 * Save/file owns the witness, meaning
 * the evidence that the installed target is the output written by save.
 *
 * Edit owns the logged piece tree, its undo log, selection including mark,
 * content IDs/bookmarks, revision, burst predecessor, published snapshot,
 * pending change and private journal staging. After undo commit journal
 * takes the sealed batch; edit retains its completion handle. Journal owns file offsets,
 * sequence, generation policy, write progress and exclusive writer grants.
 * File owns source validation. Save owns request outcome and installation
 * of the saved ID. Workers read retained snapshots only.
 *
 * An exclusive journal grant covers no sync and no write whose
 * size depends on buffer, peer or session content (G, E2). A key needing
 * admission waits at the session FIFO head before any RAM mutation. The
 * UI shows the published text and pending status and pumps completions.
 *
 * Every reservation precedes undo_commit. Each reservation is exact.
 * Every abort path releases all its claims and has its own release test.
 * A reservation holds descriptors and source pins. The sole payload copy
 * needed for emission occupies reusable storage for the current encoded
 * record, bounded by its checked wire size (E3). Journal accepts the
 * sealed plan and its pins after memory commit without a fallible claim.
 * It validates mapped loads before issuing their record. On failure it
 * suspends the batch, retains its position and withholds frame and ACK.
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
 * File contents, input bounds, deadlines, lease pressure and resource
 * exhaustion are results. All input arrays are borrowed through the call
 * only. All costs below are (P), from the operations named. */
typedef enum edit_phase {
    EDIT_IDLE, EDIT_OPEN, EDIT_REPLAYING, EDIT_EMITTING, EDIT_READY
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
typedef enum edit_direction { EDIT_UNDO, EDIT_REDO } edit_direction;
typedef enum edit_save_event {
    EDIT_SAVE_CAPTURED, EDIT_SAVE_INSTALLED, EDIT_SAVE_RELEASED
} edit_save_event;

/* On success edit adopts tree and starts an empty history with the
 * caller's history_cap_bytes. Config supplies the default of 32,768
 * records (G, v1 cfg.history_keys at buffers.c:256) times 529 B (P,
 * two 264 B undo slots plus one short input byte on LP64), or
 * 17,334,272 B (P, their product). The implementer places the charge
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
edit_result edit_create(edit_buffer **out, piece_tree *tree, struct journal *journal,
                        uint64_t buffer_id, size_t history_cap_bytes,
                        undo_validate_fn validate, void *ctx,
                        uint64_t current_id, uint64_t saved_id);
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
 * acquire ordered append admission. Contention returns WAIT before mutation.
 * Run undo_begin then undo_replace for each nonempty splice. Describe each
 * insertion in ADD coordinates. Stage the ordered DELETEs and INSERTs,
 * bracket multi-record commands with GROUP_BEGIN and GROUP_COMMIT, and
 * take the candidate snapshot. Reserve exact capacity before commit.
 * On replace, staging, reservation or snapshot
 * failure abort the open log and discard staging. undo_commit validates the
 * mapped source last and aborts internally on CONFLICT. On success hand
 * the batch to journal without allocation and attempt its first record.
 * Continue a longer batch across turns. Only page-cache acceptance of
 * the final record opens the fence. A short write preserves the full
 * record at its original offset. Show suspended recovery and keep the
 * committed edit. Do not roll back or issue the command again. Cost is
 * O(count) plus undo mutations, bytes encoded, records and abort cleanup
 * when needed. Progress returns OK, WAIT, MORE or SUSPENDED. A precommit
 * NOMEM, RANGE, LIMIT or CONFLICT preserves the entire starting boundary. */
edit_result edit_apply_plan(edit_buffer *b, const edit_splice *splices,
                            size_t count, edit_selection after);

/* Replay one whole history group in the given direction, through undo's
 * sliced replay and the same journal fence as forward edits. deadline_ns
 * is absolute on os_now_ns(), or OS_DEADLINE_NEVER. Empty history returns
 * OK without a change. A deadline permits a yield after one available mutation.
 * Once the group is prepared, walk its source-coordinate splices in
 * application order, slicing staging across turns as needed. Seal the
 * descriptors, take the candidate snapshot and commit through undo_commit.
 * On staging failure call undo_abort. While yielded, expose the old
 * published snapshot and block mutation, resolve, cap changes and save
 * capture. Only the completed group restores cursor and anchor and rebases
 * the existing mark. Each slice costs its mutations and staging work.
 * The command returns the same results as edit_apply_plan. */
edit_result edit_replay(edit_buffer *b, edit_direction direction, uint64_t deadline_ns);
/* Continue the open replay, its prepared iterator or the committed
 * journal batch from its retained position. Use the replay clock.
 * Advance at least one ready unit per call, then honor the deadline between
 * units without waiting or sleeping. Before commit the replay costs and
 * refusals apply. After commit this returns OK, MORE or SUSPENDED. In IDLE
 * and READY it returns OK in O(1). The recovery owner repairs suspension
 * off path. Resume consumes its result. Reentry into OPEN violates the
 * owner contract. */
edit_result edit_continue(edit_buffer *b, uint64_t deadline_ns);
/* After frame submit or headless delivery to the reply path, consume the single
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
 * published boundary. The other events require an owned cutoff.
 * A retriable or ambiguous save keeps its cutoff. */
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
 * open command, replay or emission. The caller must be outside the typing window. */
edit_result edit_maintain(edit_buffer *b, size_t reclaim_steps,
                          size_t resolve_visits, size_t undo_records);
#endif
```

There are **11 functions** (P, four pairs for lifetime, observation/motion, forward editing and replay/progress, plus change consumption, save notification and maintenance). Selection belongs to edit under PLAN §4.2. The snapshot is a piece object; viewport, scrolling, grapheme repair and frame construction belong to the caller. Soft-row producers receive the row's byte ends; the comment producer receives its style. Each builds a plan against the current selection and snapshot in the same UI dispatch. [Harvest §4](wave1-L2-L5-harvest.md#4-edit--srcedit-the-l2-edit-kernel)

## One keystroke, through submit

Take an ordinary one-byte insertion into a journaled, writable buffer whose BASE and generation declaration are already recoverable. Byte counts in this table are exact **journal file** bytes, including framing. RAM work is listed separately because the number of tree slots touched depends on sharing, height and actual funding. Zero syncs at each step is (G, A22 permits a single-attempt page-cache append and moves barriers off path).

| Step | RAM effect | Journal bytes / sync calls |
|---|---|---|
| UI receives the key | Stamp once on the monotonic clock. Resolve target in FIFO order. Ask the journal owner for uninterrupted command admission. If a save or another buffer holds the grant, leave the key at the head and show the last published text with pending/save status. Continue mailbox and save progress. | 0 / 0 (P, no mutation or append) |
| Admit and replace | Validate the input and reserve staging, change slot, reply/publication capacity and checked counters. Compute join. Begin with cursor/anchor, replace at caret. The input contributes one ADD byte (P, length of this insertion), plus tree/history metadata. | 0 / 0 (P, private tree checkpoint) |
| Prepare and commit | Reserve the exact descriptor and scratch for this INSERT, take the candidate snapshot, close source validation and commit undo. Install the internal next boundary and transfer the sealed plan to journal. | 0 / 0 (P, claims and commit are in RAM) |
| Append | Issue one UI `os_file_pwrite_once` (G, A22) on the named image. | **41 B / 0** (P, 32 B record header + 8 B destination + 1 B literal). A lone INSERT needs no markers, PAD, per-key VIEW, sync or worker job. |
| Publish and submit | After a full write, expose the new snapshot, identity and change, make frame submit and matching success reply eligible. After submit consume the change. | 0 / 0 (P, pointer/scalar publication) |

During a save grant the waiting key stays at the FIFO head. Mailbox routing and presentation of the previous boundary continue. Temp writes, peer-image construction and every sync run off grant. Only bounded control publication or the exact-boundary name and writer switch holds the save grant. Ordinary edit batches use ordered append admission, which excludes a switch until the batch completes. A waiting key runs against the then-current generation policy. Measure ingress-to-submit including queue residence and page faults. The grant table bounds work under the grant. G1 measures elapsed time. [Savectl grant table](wave1-savectl-spec.md#exclusive-writer-grant-and-crash-cuts) · [Q7 amended](wave1-L2-derivations.md#q7-amended-exclusive-grant-requirement)

## Command, publication and failure

- **Admission.** Pin the prior published snapshot; retain or move its existing reference. Serialize the journal intent against other writers, exclusive grants and capture of a displaced generation. Reserve explicitly *without* advancing image length or sequence. For forward input the ordered lengths determine the exact wire size using journal's actual slicing policy. Check each splice by subtraction against the successive length, then the final selection. Skip empty splices. No nonempty splice means no new text ID even if selection changes. A nonempty replacement counts even if its bytes compare equal. Stage mark motion privately. At each splice endpoints before the cut stay put, endpoints through its inclusive old end move to the new end, later endpoints shift by the length difference (v1 `input.c:changed`). Undo transports only cursor and anchor in its opaque 16 B (P, two 64-bit coordinates copied by value).
- **Forward.** After each successful `undo_replace`, record the splice destination and deletion. For a nonempty insertion of length n, the last n ADD bytes are its input (`change_text` captures any inverse before calling `piece_replace`). Keep that span even if a later splice removes it. All input is borrowed only during the call. On begin refusal release private claims. On replace error the only legal log call is `undo_abort`.
- **Replay.** Request one group (G, the user Undo or Redo command). Undo applies it under a checkpoint. Edit can yield during application and again while reading the prepared splices, before committing. Use the operation offsets to move the mark and build the change envelope. DELETE precedes insertion at the same offset. Emit ADD, undo literals, private or undeclared ORIGINAL and embargoed ORIGINAL by value. Only a verified eligible declared generation may use RESTORE_BASE_RANGE, including repeated and backward source offsets.
- **Common commit.** With append admission held, size and reserve the exact descriptors, source holds, image bytes and sequence interval, reusable record scratch, session leaf, counters, change and reply slots. Check arithmetic before memory commit. Sizing uses the actual encoder policy. Sealing and snapshot acquisition may still fail. Take the candidate snapshot and bind its source holds. On any precommit failure abort the open checkpoint and release all claims. Call `undo_commit` once. On closing CONFLICT undo has already aborted. On success transfer the sealed batch without a claim, allocation, I/O or refusal, then attempt the first append. All data needed after the call is owned. Both directions and forward editing use this sequence.
- **Journal.** A replacement with deletion and insertion or any other multi-record command has a contiguous nonempty GROUP_BEGIN … GROUP_COMMIT. A single data record stands alone. Journal slices literal bytes, uses checked additions for every destination, offset, size and sequence, retains encoded bytes and write position across turns. Interleaving a different command, generation redeclaration or rotation inside this batch is forbidden. After memory commit the command runs exactly once. Transport pressure yields MORE. A short, zero or failed write preserves the *whole encoded record*, original write offset, accepted prefix and unissued suffix. The owner latches recovery suspension, edit stays EMITTING, status is visible and success remains fenced. Repair off path, explicitly reconcile the exact batch and its final image-qualified position, then continue/publish once. No second INSERT on retry. Continuity loss elsewhere while idle refuses new admission visibly. An uncertain or failed repair keeps the fence closed. [Wire group and reader rules](../../src/journal/journal.h)
- **Observability.** The prior bytes, selection, history cursor (including redo and eviction), saved/cutoff bookmarks, ID, revision, journal sequence and pending change survive every precommit refusal. NOMEM displays **“Edit not applied — buffer memory is full.”** Other causes have distinct statuses. Postcommit suspension preserves the committed RAM edit for recovery and exposes only the prior boundary for frames. After the final complete record (including COMMIT when grouped), install the published snapshot, return the immutable change and signal the matching command ID through the app's reserved reply path. Layout may read the new snapshot before submit. L4/L5 poll the change after submit; headless success uses the same fence with no fake draw or forced flush. Before ACK, process death permits the previous or whole command; after ACK it requires the whole command, untitled included, starting from a verified recoverable BASE. Failed storage barriers, power loss and A23 foreign replacement follow their separate recovery contracts. [PLAN §4.2](../PLAN_V2.md#42-l2-edit-kernel-journal-publish-point--sigkill-kill-oracle) · [Q16](wave1-L2-derivations.md#q16--settled--external-headless-command-completion)

For the change envelope, retain the longest *untouched* exterior prefix and suffix. Initialize both to the original length; for each nonempty splice shorten prefix to at most its offset and suffix to at most the length after its cut in that step's input. Subtract both survivors from the old and new total lengths to get old_len and new_len (P, the two exterior regions survive every step and are disjoint after the first splice). This computes exact extents for a simple replacement and encloses disjoint changes without reading the intervening text. Only the actual splices are journaled or used to move a mark.

## E1 · undo.h erratum for signature

Replace the internally committing, group-count `undo_undo` and `undo_redo` entry points with the following one-group operation. Migrate their callers in the erratum. A caller requesting multiple groups loops at owner-committed boundaries. The public cursor types below are value types that fit in the owner's continuation. Clients treat their pointer members as private.

```c
typedef enum undo_replay_direction {
    UNDO_REPLAY_UNDO, UNDO_REPLAY_REDO
} undo_replay_direction;

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

/* Apply or resume one group. On OK, *target borrows its saved cursor
 * state if a group is prepared. Empty history yields OK and NULL. */
undo_err undo_replay_prepare(undo_log *u, undo_replay_direction direction,
                             uint64_t deadline, const undo_state **target);
/* Start an allocation-free traversal of the complete prepared group. */
void undo_splice_iter_init(undo_splice_iter *it, const undo_log *u);
/* Return the next splice in application order. At end return false
 * and leave *out untouched. */
bool undo_splice_iter_next(undo_splice_iter *it, undo_prepared_splice *out);
/* Return the indexed insertion span by value in document insertion order. */
piece_span undo_splice_span(const undo_prepared_splice *splice, size_t index);

/* Close the checkpoint and publish the prepared group on success. */
undo_err undo_commit(undo_log *u, const undo_state *after);
/* Roll back the open checkpoint and discard its provisional work. */
void undo_abort(undo_log *u);
```

- **Preparation.** The UI owner starts on an idle log. Each call applies at least one available mutation before checking its absolute deadline on the landed clock. MORE sets *target to NULL and keeps the checkpoint open. Resume only the same direction. Empty history leaves the log idle. Even a single available mutation returns prepared with a live checkpoint. The borrowed target is the group's before state for undo or after state for redo. Published history, redo, eviction and cursor remain at entry throughout. Keep the landed source checks, resume guard and borrowed pre-group snapshot semantics for a mid-group yield. NOMEM, RANGE or CONFLICT during preparation rolls back the entire group, clears *target and leaves the log idle. [Landed replay and errors](../../src/undo/undo.h)
- **Iteration.** Initialize only after successful nonempty preparation. Walk last to first on undo and first to last on redo. Each result has the destination offset in the text immediately before the mutation and the count of bytes it removed. A zero span count denotes a pure delete. Otherwise the sum of span lengths is its inserted length. On redo use the record's removed count and inserted ADD run. On undo remove the forward insertion and enumerate the inverse. An empty inverse supplies no spans. Local or external spans keep their recorded order. A literal uses its owned ADD copy, including the reversed sequence of capture segments reconstructed from the cut mask for joined backspaces. The resulting spans describe exactly the bytes passed to the piece mutation. No document-offset lookup or mapped payload read occurs here. O(1) cursor space and work per record or span, including the fixed-width literal cut walk. The iterator may restart for an exact sizing pass. Its cursor and results are valid across UI turns only while this prepared checkpoint is open. The caller checks index < span_count. [Record representations and apply order](../../src/undo/undo.c)
- **Commit or abort.** Block begin, replace, another prepare, cap change and revision-ending operations while prepared. Reads of published counters remain legal. Edit may take its candidate snapshot. For replay pass the saved target to the existing commit signature. Undo validates last, commits the checkpoint and moves the applied-history position in the saved direction without appending history, trimming or branching. A forward caller supplies its own final cursor state and retains landed join, branch and cap behavior. Only the owner's successful undo_commit advances an edit's history position. Closing CONFLICT aborts internally. Explicit undo_abort is allocation-free and valid for an open or failed forward command and for an applying or prepared replay. Either close invalidates every borrowed target, cursor and splice. The owner copies descriptors into its plan and acquires pins before that point. There is no replay callback or additional undo refusal code.

## E3 · bytes between memory commit and the final append

Delete a contiguous mapped ORIGINAL range, save the deletion, then undo it after capture has embargoed its generation. Undo restores the range by reference in the tree. Its bytes still reside in the old mapped backing, kept alive by the candidate snapshot. The named journal must receive the bytes by value. Journal takes the source pin, compact operation/run descriptors, exact wire reservation and one reusable encoded-record buffer. It advances destination and source cursors as INSERTs complete. A long run needs one descriptor regardless of the number of slices. Journal releases its pins only after acceptance or definitive reconciliation. The pin also keeps the snapshot's ADD and private ORIGINAL sources alive for mixed commands. [Snapshot ownership and mapped faults](../../src/piece/piece.h)

**Source coordinates matter.** In a compound command, insert bytes, then delete them before the candidate snapshot is taken. Redo must journal that insertion before its deletion. The final document view cannot locate it. The sealed span still names bytes in the snapshot's source stores. Require a bounded, allocation-free piece reader for a provenanced span in those pinned stores, including ADD below the captured high-water even when absent from the document. Bind descriptors to the snapshot's tree/incarnation and checkpoint lineage. Discard all provisional descriptors on abort, including when a later command grows ADD over the same offset. Snapshot ownership preserves the storage lifetime. File supplies the evidence for trusting a mapped load. [Landed document-coordinate readers](../../src/piece/piece.h)

**Validation and repair.** Fill the literal directly in the current record's scratch, then check the held backing's live identity (including same-size overwrite) and poll its registered fault ID after the last dependent load. Require OS_OK and epoch zero (G, the mapped reader contract). Only a validated record may acquire its CRC and be issued. Keep a validated short-written record verbatim for retry at its original offset. A failed source check *before* owner commit aborts the group. A failed read/check *after* owner commit discards the unissued scratch, keeps the committed history position, pins, descriptor cursor and accepted prefix, latches journal suspension and leaves edit in EMITTING. Report the source conflict and unacknowledged undo. Keep the prior submitted frame or a conflict status if that backing is also untrustworthy. No provisional frame, success reply, further writer admission or GROUP_COMMIT may pass. In particular, fault-substituted zeroes never become an INSERT.

Resume only with independently verified bytes for the same original generation/range and a reconciled named-image position. A faulted registration cannot regain a pristine epoch. The current target after save may itself be the file *with the range deleted*. It cannot supply the missing bytes. If exact repair is impossible, the user sees a persistent recovery conflict and the incomplete group remains unacknowledged. Preserve the journal and available evidence for salvage. A restart ignores the incomplete group. The normal final append publishes once.

**Space and visible work.** Let L be the restored length, S the journal encoder's literal slice size, R the number of described source runs and A the number of splice operations. For each batch reserve scratch equal to its largest actual encoded record. For this restore it is at most 40 + min(L,S) bytes (P, 32 B header + 8 B destination + slice), at most 1,048,576 B (G, landed maximum record size). Fill that same buffer in place and reuse it only after the complete write. Checked record-count, offset and sequence intervals need constant space. Descriptor memory is O(A + R) (P, one operation entry per splice and one source description per run). The reservation contains no per-slice array. Physically pinning an existing snapshot shares its nodes. A new header takes one 400 B slot on a cache miss (P, piece snapshot layout). A reference-only replay appends no ADD bytes, so it needs no private ADD boundary copy. [Wire maximum and framing](../../src/journal/journal.h) · [Snapshot claims](../../src/piece/piece.h)

At **10 GiB** (G, Wave 2 mapped-file case), take the entire aligned ORIGINAL as one restored run into the now empty document. L = 10 × 2³⁰ = 10,737,418,240 B (P, binary GiB). A = R = 1 (P, one inverse splice with one contiguous source). Incremental journal staging therefore needs **at most 1 MiB of payload-plus-framing scratch, one splice, one run and fixed pins/cursors**. The process also owns tree and history metadata. The range spans 163,840 chunk descriptors (P, L / 65,536). For the snapshot-held empty-tree bulk shape the piece planner charges 8,378 new structural slots, or 3,351,200 B (P, the signed contiguous-batch count times 400 B). Other starting shapes use the exact piece claim. Charge existing trees, pinned roots, page-rounded extents and mapped pages separately. Scanning L bytes can fault in that entire range and dirty file-cache pages. The staging bound is an owned-storage bound, not a peak RSS promise. [Bulk insertion derivation](wave1-piece-pool.md#bulk-insertion-signed-review-10a)

The user sees the preceding frame and pending undo while journal writes **L + 40 × ceil(L/S) + 64 B** (P, one INSERT per slice and a BEGIN/COMMIT pair for this multi-record command). Work and journal space grow with L even though the payload buffer stays bounded. The loop advances across turns and pumps completions while later dependent input waits. On full acceptance the restored text and matching ACK become eligible together. Actual metadata admission failure before commit leaves the deletion in place. Disk exhaustion or source loss during emission keeps the undo fenced. The size of the range alone does not require a matching payload allocation or establish a latency bound.

## E5 · default history charge

Use the v1 config default of **32,768 history records** (G, `cfg.history_keys`, v1 `src/editor/buffers.c:256` at the oracle pin). Convert depth using the **published history ledger** for an unjoined one-byte typing group. In the landed LP64 layout, eight inline spans plus their counts occupy 192 B (P, 8 × (16 + 8)). The kind and alignment occupy 8 B and the count occupies 8 B, giving a 208 B inverse. The record adds 16 B for its links, 16 B for offset and removed length, and 16 B for its inserted span, yielding 256 B. The record is the largest slot union member. The slab pointer adds 8 B, giving **sizeof(undo_slot) = 264 B** (all P, field layout). The group takes a whole slot even though its fields are smaller. [Undo layouts](../../src/undo/undo.c)

`stage` charges one group slot. `stage_record` charges one record slot, zero external or inverse literal bytes for the empty deletion, and one short input byte because the insertion is shorter than sizeof(piece_span). Thus **c = 264 + 264 + 1 = 529 B** and the default cap is **32,768 × 529 = 17,334,272 B** (P). Put both figures and the layout derivation beside the config default. The caller still supplies the cap in bytes. Page rounding and committed ADD storage belong to different ledgers. [Charge paths](../../src/undo/undo.c)

## Transaction and burst

Exactly **five reachable phase tags** (P, one per row below). Use a tagged union and exhaustive switches under `-Wswitch-enum`. REPLAYING carries its direction and a continuation for applying mutations or staging prepared splices. EMITTING retains the batch handle through journal suspension. Query lease and suspension at journal and source validity at file. Savectl's **22 reachable states** (P, its eleven phases each open or closing) remain with savectl. [Savectl states](wave1-savectl-spec.md#states-and-transitions)

| Tag | Owner and transitions |
|---|---|
| IDLE | A refusal or empty history leaves the buffer here. Admission enters OPEN for a forward mutation or REPLAYING for a history command. |
| OPEN | Forward checkpoint and private batch. All staging stays here. Failure → IDLE, successful memory commit → EMITTING. No external yield. |
| REPLAYING | Direction selects undo or redo. The old snapshot remains published across application and iterator yields. Refusal or abort returns to IDLE. Owner commit enters EMITTING. Opposite input stays queued. |
| EMITTING | Already committed. Resume from transport cursor. Backpressure, short write or unresolved repair stays here. Complete page-cache acceptance → READY. |
| READY | New boundary visible, change slot full. After submit, consuming it → IDLE. Admission waits rather than overwriting an unseen change. |

For a burst, remember the last eligible **committed** command's kind, offset, extent and monotonic receipt time. Require a collapsed selection, no intervening break, no redo suffix and matching ordinary kind. Type joins at previous insertion end; forward delete at the same start; backspace when this deletion ends at the previous start. Test `now >= last && now - last <= EDIT_BURST_NS`, using **300 ms = 300,000,000 ns** (G, inherited product behavior, including equality and the backwards-clock check at oracle pin `6fec2e8`). Define the constant privately. Each paste, compound/selection replacement, explicit selection change, kind change, admitted save cutoff or replay breaks the chain. A refused mutation leaves the predecessor intact. Undo independently checks structural adjacency and may start a fresh group. [Pinned v1 threshold](</home/tobias/Projects/sublimite-v1-oracle/src/undo/undo.h:67>) · [Pinned v1 join predicate](</home/tobias/Projects/sublimite-v1-oracle/src/undo/undo.c:301>)

## Contracts to check at integration

| Owner | Required call list and obligations |
|---|---|
| Piece | `piece_tree_view/length` for bounds, `piece_iter_init/next_span` for descriptions, `piece_iter_next` for needed literals, `piece_view_backing` for source, `piece_snapshot_take/retain/view/release` for publication, `piece_close` after log destruction. Edit calls neither a logged tree's mutation nor checkpoint functions. At quiescence piece admits from funded slots **and** companions, otherwise exact plan, one local drain and replan if needed, release excess whole extents, then at most one exact base request (G, A26 procedure). Concurrent last release may make the very next request succeed. |
| Undo | Lifetime, history and forward calls remain as landed. The prepared replay and iteration signatures below replace internal replay publication. File supplies live backing validation and undo owns its closing fault poll. |
| Journal | Reserve an ordered command against the named image and generation policy. Reserve exact descriptor, sequence, wire and scratch capacity. Discard privately or take custody infallibly on commit. Encode, validate and append each record, expose suspension and reconcile the batch through its image-qualified end. Report native diagnostics. |
| Savectl through the session owner | Read `edit_read` at the published boundary; call `savectl_content_identity` once per published revision even on return to an equal ID. `savectl_save_request` takes matching snapshot, ID and complete journal view. In the same serialized admission, reserve all claims, seal joining with `edit_save_boundary(CAPTURED)` and have journal embargo the displaced generation **from capture onward**, even after cancellation. A rejected admission does none of these. The cutoff's position, generation and synced temp witness (the fd-derived evidence identifying the file the saver wrote) belong to journal/save. On `savectl_get_model` reporting definitive verified finish transfer the matching cutoff to saved; on definitive loss of the request drop it. Defer logical adoption of completions across an open edit, keep routing messages. Filter stale request/incarnation before notifying edit. On `savectl_take_reload` the host installs a fresh tree/log/domain together, clamping selection itself. |
| App and work | Single UI consumer routes shared-pool messages and ticks owners; edit never drains or submits worker jobs. `os_now_ns` supplies input receipt and slice checks. Global FIFO targets stable buffer incarnations. Open starts acquisition then releases dispatch. Switch-to-ready-tab and Save can proceed until the head needs unavailable text, a grant or replay completion. At that point hold the head, pump progress, and visibly refuse a failed-open dependent before advancing. No input overtakes it. |

Maintain saved and in-flight IDs even after corresponding history becomes unreachable. Position is `evicted_groups + undo_groups` (P, trimmed prefix plus applied groups). On actual branch commit invalidate discarded redo positions before reusing depth. On actual join invalidate the replaced endpoint; use the starting/ending `group_serial` to distinguish a new group. Remove reachability strictly outside the surviving interval, keep IDs for still-owned cutoffs. Match remaining bookmarks only on completed replay. A reload starts a fresh incarnation; identities are compared only within it. With the zero sentinel, savectl carries a scalar saved ID and computes `saved_id == 0 || saved_id != current_id`. At the wire boundary translate the existing HAS_SAVED flag. [Q10 and Q15, ratified by A22](wave1-L2-derivations.md)

The **only production caller** of the public maintenance operations is `edit_maintain`. The order is `piece_reclaim` → `piece_lines_resolve` → `piece_replenish` → `undo_maintain`. Reclaim makes retired slots available before resolution, resolution can spend slots, top-up funds the next input, and undo retires history within its own budget. No line-resolution call while replay owns a checkpoint. A failed warm-up rejects no key. Internal exact-admission retirement is a different operation. [A24 and A26](wave0-plan-amendments.md#a24-7b-source-read-rulings-advisor-2026-11-24-docsreviewswave1-piece-7b-source-astramd)

The edit bead brief must name the **UI host that schedules post-submit maintenance** as owner of all three call-site derivations, including its headless equivalent. At each scheduling site it must justify the reclaim steps it can pay for, the resolve visits it can fund while making progress on the tree in question, and the retired undo records it can release. Carry the respective work units and Law 4 evidence with the arguments. Edit owns the call order and honors those inputs.

### Additions to the savectl seam table for joint signature

| Required operation | Evidence or GAP |
|---|---|
| Prepare and enumerate a replay group while retaining the owner's right to commit or abort. | **GAP (undo).** Landed replay commits inside the call. E1 above specifies the prerequisite erratum. [undo.h](../../src/undo/undo.h) |
| Read a bounded slice by source coordinate from the pinned stores, including a private ORIGINAL or ADD interval absent from the final document. | **GAP (piece).** The public byte iterator starts at a document offset. The mapped backing accessor alone cannot supply ADD or private ORIGINAL. The read needs snapshot-relative provenance and lifetime. [piece.h](../../src/piece/piece.h) |
| Reserve/discard an exact descriptor plan, source holds, wire interval and reusable scratch, then transfer custody infallibly and resume at an image-qualified record position. Suspend on invalid source before issuing that record. | **GAP (journal runtime).** This extends savectl's existing writer-admission gap. The wire defines record framing and contiguous groups. The runtime brief retains a short-written record. Neither defines rollback of private claims, transfer on undo commit, or repair of an unread suffix after mapped validation fails. [Journal runtime brief](../briefs/L2-2b-journal-runtime.md) · [Savectl seam table](wave1-savectl-spec.md#seam-table) |

## Proof obligations for the implementer

**Law 2.** Each row below has an expected **zero libc allocations** (G, live release malloc/calloc/realloc guard from ingress through actual submit). VM-backed base claims and their page faults happen inside the guard when necessary. No exemption around admission, a slice, transport or submit.

| Guarded calls | Why the expected count is zero |
|---|---|
| Input decode, FIFO, receipt clock, edit_read/select, join and checked plan | Reserved input/reply slots, borrowed arguments, scalar/stack work. |
| undo_begin, each undo_replace, piece admission and snapshot_take, abort or commit | Base extents, piece slots, ADD chunks and history storage. Exact cold top-up uses base. |
| Replay preparation, iterator staging and each continuation | Undo uses its checkpoint, piece storage and history records. Edit keeps a private descriptor plan and its prior published snapshot across the deadline. |
| Stage/reserve, wire encode, journal RAM handoff, append/insert_step and write | Checked base staging and retained encoded record. No worker submission, wait, sync or malloc in append. |
| Identity publication, session leaf, layout/frame or headless reply | Preclaimed leaf and reply slot, prepaid render path. A12 excludes libxcb calls capable of reading on UI between ingress and submit, flush included. |

Register the production edit path in the release law2 suite. Fail unless `base_malloc_guard_active() && count == 0` (G, Law 2); count across ingress through **true submit**, including intervening turns, even for back-to-back input and resumed replay. Fixture setup and post-submit maintenance are outside. Run typing, both deletes, paste/compound, undo, redo, cold tab switch and refused exact base claim followed by a successful key. Count maintenance order with wrappers and check production call sites. Measure typing begin/replace/commit p50/p99 (G, provisional bench asks for median and 99th percentile), and full ingress-to-submit median/tail under ordinary typing and heavy writeback with power/load stamps; measure separately from allocation/read counters. [Release guard](../../src/base/base.h) · [A12, A13, A19 and A22](wave0-plan-amendments.md)

**Independent oracle.** Use a flat byte vector with parallel ORIGINAL/ADD coordinates, immutable copies for snapshots, and an undo list of whole before/after states plus selection endpoints. Apply each plan to a private copy. Publish or discard it as a unit. A model clock, adjacency and byte-cap fixtures determine expected grouping, branching and trims. Keep persistent oracle identities for every boundary; compare observable equality for current/saved/in-flight owners, including finishing an older save after typing, undo to cutoff, equal depth on a different branch, joined endpoints and eviction. Model journal as a sequence of complete logical records plus a partial tail; a separate reader applies only complete groups and resolves source generation policy. Compare bytes and accepted revisions independently of production encoding, splice iterator and FSM. Use real named journals in a directory under `build/` for integration and external headless kill tests. Flat services and controlled failures are test doubles at the same mandatory journal contract.

- Inject each claim refusal, conflict on final mapped read, replay refusal after yield, short write and recovery. Compare prior published bytes, selection/mark, history cursor/redo/evictions, bookmarks, ADD length and journal sequence. Exercise private insert then delete in an aborted command, regrow ADD at the old offset, capture and undo; repeat with a literal. Repeat a failed warm-up followed by a fitting key. Verify old snapshots while a replay is provisional and rejection of interleaved writes/lease acquisition.
- Count actual mapped ORIGINAL reads/copied inverse bytes. Eligible one-span delete+undo reads at most **131,072 B** (P, description and deletion each clip two ends at up to 32,768 B; undo carries counts). Inverse copies at most **min(length, 16 × spans) B** (G, A13 chooses the cheaper of literal and 16 B descriptors), with O(spans) inverse storage. Compare the one-byte literal and large one-span equality/reference cases. Test A23's mandatory by-value emission separately and charge its payload to journal. After ORIGINAL counts resolve, check rank/select against the flat bytes before, between and after deletion/undo of **513 chunks** (G, A19 regression, P, one beyond the old 512-chunk boundary); count O(height) node visits. No edit-path newline scan or independent index veto.
- Run AFTER_DELETE / BEFORE_COMMIT / COMPLETE cuts across undo, redo, replacement and paste. The torn-undo fixture has **262,144 B** on one side (P, v1 fixture 16 × 16,384 B) and **1 B** on the other (G, its replacement byte). Only a whole side may recover. Kill just before and after the external matching ACK with no graceful flush. Include untitled, prepared save restoring by value, foreign replacement/unresolved BASE, source conflict on in-place overwrite, independent fsync/dirsync outcomes, and the save/discarded-redo branch fixture. [Harvest acceptance](wave1-L2-L5-harvest.md#44-acceptance-lines-that-already-exist) · [A23](wave0-plan-amendments.md#a23-the-save-protocol-no-retention-rules-17-t5-is-a-stated-product-contract-advisor-2026-11-23)

**Reservation tests.** Use a separate failure case for each abort exit and inject refusal after each successful partial claim. Cover forward begin and replace failure, replay preparation failure before and after a yield, checked wire/sequence/identity overflow, descriptor or scratch refusal, snapshot/source/session/reply claim refusal, owner abort during staging and the internally aborted commit conflict. Count claimed and released pins, descriptors, scratch, sequence and append admission. Each case returns exactly to its starting ledger and the next fitting command can acquire it. On success transfer ownership once and confirm release only at completion or definitive reconciliation.

**Discriminating replay case.** Restore an embargoed span, yield after the last provisional mutation, then refuse the last descriptor or snapshot claim. The owner aborts and a second Undo still selects the same group. Repeat with a one-mutation group that reaches preparation in the initial call. Exercise redo of an insertion erased later in the group and undo of a joined backspace literal with cuts. Compare every inserted byte with the flat oracle.

**Streaming cuts.** Count peak live staging bytes and plans while increasing the length of a single run at fixed slice size. Inject a key during a save grant and again during an unfinished large undo. Check FIFO order, mailbox progress, prior frame and absence of success. After an accepted INSERT and before the next one, inject truncation/fault, registration failure and same-size in-place overwrite separately. Verify the failed slice was never written and no COMMIT or ACK escaped. Try repair with a foreign source, then with independently verified original bytes. Kill at incomplete-group and completed-group boundaries. Separately short-write a validated slice and make its backing unreadable before retry. The retry must use the retained encoded bytes at the original offset.

**Signature boundary.** The coordinator can file the undo erratum as an edit prerequisite after signature. The piece reader and journal handoffs in the seam table need owner agreement. Command-plan producers, implementation, measurements and bead filing belong to their respective owners.
