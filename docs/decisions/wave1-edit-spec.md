# Edit kernel · proposed contract for advisor signature

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
 * a verified declared generation; after embargo, stage its bytes while
 * validation can still veto commit. ADD is piece's append-only byte store.
 * Journal ADD and literals by value. Save/file owns the witness, meaning
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
    EDIT_IDLE, EDIT_OPEN, EDIT_UNDOING, EDIT_REDOING, EDIT_EMITTING, EDIT_READY
} edit_phase;
typedef enum edit_result {
    EDIT_OK, EDIT_WAIT, EDIT_MORE, EDIT_NOMEM, EDIT_RANGE, EDIT_LIMIT,
    EDIT_CONFLICT, EDIT_SUSPENDED
} edit_result;
/* OK = command complete, including a no-op. WAIT = not admitted; keep the
 * ordered input at the head while the owner makes progress. MORE = this
 * command owns unfinished work; continue it, never dispatch it again.
 * NOMEM = necessary base storage refused. RANGE = invalid byte interval,
 * selection, resulting length or piece height. LIMIT = identity, revision
 * or wire arithmetic exhausted. CONFLICT = required live/recoverable source
 * failed verification. SUSPENDED = journal continuity failed; show the
 * recovery error. The phase distinguishes a blocked new request from a
 * committed command waiting for repair. Refusals publish no change. */
typedef struct edit_selection {
    uint64_t cursor, anchor, preferred_col;
} edit_selection; /* preferred_col is for vertical motion; UINT64_MAX is unset */
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

/* Setup. Adopt tree only on success. Start a fresh empty history on it
 * using history_cap_bytes. The file owner's live validator and its context
 * outlive the log; validate is required for mapped text. journal outlives
 * the buffer. buffer_id names its existing recoverable BASE (including an
 * empty untitled BASE). current_id and saved_id seed from open/recovery:
 * a fresh clean file supplies the initial ID for both; saved_id may be
 * unknown or an unreachable older ID on restore. Allocate future IDs past
 * both. The host settles old save tokens before changing incarnation.
 * Start with collapsed selection at the beginning, preferred column unset,
 * mark absent. The UI may retain a non-owning tree pointer for off-path
 * line-count adoption at an idle boundary, valid only until destroy.
 * O(1) control/log and snapshot claim. NOMEM leaves *out NULL and tree
 * with caller. ASSERT: unlogged tree, journal and out present, buffer_id,
 * history_cap_bytes and current_id nonzero. */
edit_result edit_create(edit_buffer **out, piece_tree *tree, struct journal *journal,
                        uint64_t buffer_id, size_t history_cap_bytes,
                        undo_validate_fn validate, void *ctx,
                        uint64_t current_id, uint64_t saved_id);
/* Release log, snapshot, tree and storage. Cost of draining their remaining
 * owners/records; off the typing path. Infallible. ASSERT: IDLE, cutoff
 * settled, session has secured kept text or authorized discard. */
void edit_destroy(edit_buffer *b);

/* Copy the last published boundary, including during a fence or yield.
 * Return the actual transaction phase. O(1), infallible. */
edit_phase edit_read(const edit_buffer *b, edit_boundary *out);
/* Install byte selection/mark for motion or a mark command. Both ends of
 * each active selection must be within published length. A change seals
 * the burst. O(1); RANGE or WAIT while a command owns the boundary.
 * No text revision, journal edit, history group or change record. */
edit_result edit_select(edit_buffer *b, edit_selection_state selection);

/* Apply one splice and collapse the caret just after its insertion, with
 * preferred column unset. receipt_ns is the monotonic input timestamp used
 * for burst comparison. The kind distinguishes ordinary text, forward
 * delete and backspace; paste and compound commands use apply_plan.
 * Join only a pure insertion or deletion of the same committed ordinary kind,
 * collapsed selection, empty redo and elapsed time in [0, 300 ms] on
 * the monotonic clock (G, v1 UNDO_BURST_NS at oracle pin 6fec2e8).
 * Type starts at the prior insertion end, forward delete at its start,
 * backspace ends at its start. Paste, compound replacement, a selection
 * change, replay or save capture seals the preceding burst.
 * Cost of a one-element plan below plus O(1) burst comparison.
 * Results as for apply_plan. */
edit_result edit_replace(edit_buffer *b, edit_kind kind, edit_splice splice,
                         uint64_t receipt_ns);
/* Execute the ordered splices as a single separate command. Each offset
 * refers to the text immediately before that splice. Install after in the
 * final text; rebase the mark through the actual splices. Empty sequences
 * can change selection alone. ASSERT: array present for nonzero count;
 * pointers remain readable independently of mutation. Precheck ranges,
 * reserve a private journal batch and exclusive append intent. A missing
 * grant yields WAIT with unchanged RAM. Run undo_begin then undo_replace
 * for each nonempty splice. Encode the ordered DELETEs and INSERTs after
 * the last replace, bracket multi-record commands with GROUP_BEGIN and
 * GROUP_COMMIT, and take the candidate snapshot. On replace/stage/take
 * failure abort the log and discard staging. undo_commit validates the
 * mapped source last and aborts internally on CONFLICT. On success hand
 * the batch to journal without allocation and attempt its first record.
 * Continue a longer batch across turns. Only page-cache acceptance of
 * the final record opens the fence. A short write preserves the full
 * record at its original offset; show suspended recovery and keep the
 * committed edit. Do not roll back or issue the command again. Cost is
 * O(count) plus undo mutations, bytes encoded, records and abort cleanup
 * when needed. OK, WAIT, MORE or SUSPENDED; NOMEM, RANGE, LIMIT or
 * CONFLICT before memory commit preserve the entire starting boundary. */
edit_result edit_apply_plan(edit_buffer *b, const edit_splice *splices,
                            size_t count, edit_selection after);

/* Replay one whole history group in the given direction, through undo's
 * sliced replay and the same journal fence as forward edits. deadline_ns
 * is absolute on os_now_ns(), or OS_DEADLINE_NEVER. Empty history returns
 * OK without a change. A deadline permits a yield after one available mutation.
 * Stage each actual splice under undo's checkpoint using the proposed replay
 * observer; seal the batch and take the candidate snapshot on the last one.
 * A refusal there must still abort the group. While yielded, expose the
 * old published snapshot and block mutation, resolve, cap changes and save
 * capture. Resume the same direction until the group completes or aborts.
 * Only the completed group restores cursor/anchor; reset preferred column
 * and rebase the existing mark. Cost is work of mutations and staging in
 * the slice. Results as for apply_plan. */
edit_result edit_replay(edit_buffer *b, edit_direction direction, uint64_t deadline_ns);
/* Resume the saved replay direction/count, or advance the committed journal
 * batch from its retained record/byte position. Same clock as replay.
 * Advance at least one ready unit per call, then honor the deadline between
 * units. No wait or sleep. Same costs/refusals as replay before commit;
 * after commit only OK, MORE or SUSPENDED. IDLE/READY cost O(1), OK.
 * Recovery owner repairs suspension off path; resume consumes its result.
 * ASSERT: no reentry into OPEN. */
edit_result edit_continue(edit_buffer *b, uint64_t deadline_ns);
/* After submit (headless: delivery to the reply path), consume the single
 * pending text change. READY -> IDLE. Otherwise false, *out untouched.
 * O(1), infallible. One UI consumer fans the value out to layout and app. */
bool edit_take_change(edit_buffer *b, edit_change *out);

/* No-fail notification within the session owner's serialized save
 * transition. CAPTURED seals joining and pins the current ID/boundary;
 * INSTALLED transfers that bookmark to saved even if now unreachable;
 * RELEASED drops the cutoff after definitive cancellation/failure.
 * O(1) fixed bookmark slots. ASSERT: matching admitted save incarnation,
 * at most one owned cutoff (G, one active save per controller); CAPTURED
 * at a complete published boundary, the other events with an owned cutoff.
 * A retriable or ambiguous save keeps its cutoff. */
void edit_save_boundary(edit_buffer *b, edit_save_event event);

/* UI after submit, also at open/activation outside input-to-submit.
 * The budgets count reclaim steps, resolve node visits and retired undo
 * records respectively. Call reclaim, resolve, replenish, undo_maintain
 * in order. After resolve refresh the published snapshot via snapshot_take
 * so readers see newly exact geometry at the same text revision. Keep the
 * previous snapshot if that claim fails and try again next turn. Cost is
 * their bounded walks, the snapshot claim and actual top-up. Attempt all
 * on an eligible turn; return NOMEM if resolve, take or top-up was refused.
 * This is a maintenance observation only. WAIT skips all work during an
 * open command, replay or emission. ASSERT: outside the typing window. */
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
| Prepare and commit | Encode the 41 B private INSERT (P, same derivation as the append below), take the candidate snapshot, close source validation and commit undo. Install the internal next selection, history boundary, ID and revision; transfer the ready batch to journal without a fallible claim. | 0 / 0 (P, staging and commit are in RAM) |
| Append | Issue one UI `os_file_pwrite_once` (G, A22) on the named image. | **41 B / 0** (P, 32 B record header + 8 B destination + 1 B literal). A lone INSERT needs no markers, PAD, per-key VIEW, sync or worker job. |
| Publish and submit | After a full write, expose the new snapshot, identity and change, make frame submit and matching success reply eligible. After submit consume the change. | 0 / 0 (P, pointer/scalar publication) |

An exclusive lease delays application in RAM as well as append. During ordinary temp writing and sync the save has captured its immutable cutoff but the UI owns journal admission, so later typing can actually publish. Exclusive grants cover quiescent peer/PREPARED and finish/rotation work. On return the waiting key runs against the then-current generation policy. “Save while typing” requires a completed, acknowledged edit during the long file-only work. Measure ingress-to-submit including queue residence and page faults. G1 is the enforced latency regression gate. A single bounded syscall count gives no elapsed-time bound. [Save states](wave1-savectl-spec.md#states-and-transitions) · [A22–A23](wave0-plan-amendments.md#a22-rulings-on-the-l2-derivations-advisor-2026-11-23-docsdecisionswave1-l2-derivationsmd)

## Command, publication and failure

- **Admission.** Pin the prior published snapshot; retain or move its existing reference. Serialize the journal intent against other writers, exclusive grants and capture of a displaced generation. Reserve explicitly *without* advancing image length or sequence. For forward input the ordered lengths determine the exact wire size using journal's actual slicing policy. Check each splice by subtraction against the successive length, then the final selection. Skip empty splices. No nonempty splice means no new text ID even if selection changes. A nonempty replacement counts even if its bytes compare equal. Stage mark motion privately. At each splice endpoints before the cut stay put, endpoints through its inclusive old end move to the new end, later endpoints shift by the length difference (v1 `input.c:changed`). Undo transports only cursor and anchor in its opaque 16 B (P, two 64-bit coordinates copied by value).
- **Forward.** `undo_begin(log, before, join)` → `undo_replace` for each nonempty splice → privately stage DELETE/INSERT in operation order, using the still-owned input bytes even if a later splice overwrote them → `piece_snapshot_take` on the staged final tree → `undo_commit(log, after)` → no-fail journal RAM publication. Candidate snapshot and any session-leaf claim are funded before commit. On begin refusal release private claims. On replace error call **only** `undo_abort` on the log. A staging/claim/source refusal before commit also aborts. On commit CONFLICT undo has already aborted; release the private candidate and batch. No external cursor or sequence moved in any of these cases.
- **Replay.** One requested group (G, product Undo/Redo command); this avoids undo's documented partial success across groups. Call `undo_undo` or `undo_redo` with that count and deadline. On MORE retain direction and remaining count (requested minus completed), staging and old published boundary. No forward call, opposite replay, cap change, resolve, live-revision-ending operation, new snapshot publication or save capture during a yield. Resume and keep servicing other UI work. Each actual replay splice contributes an ordered DELETE and/or description of its inserted range. Read source runs with newline hints omitted. Emit ADD, literals, undeclared/private originals and embargoed originals by value. Use RESTORE_BASE_RANGE only for a verified, already declared eligible generation at its source offset, including backward/repeated offsets. Validate after the last dependent mapped load. A failed validation aborts the current group, including any earlier slices. No borrowed live-tree pointer crosses into transport.
- **Replay seam requiring signature.** Landed replay commits the last mutation *inside* the call. `undo_change` is a repaint envelope, available too late to admit journal material or the candidate snapshot. Proposed additive undo contract: install a synchronous optional replay observer on the idle log, taking context, actual splice offset, removed and inserted lengths, post-splice borrowed piece view and an indicator of the final splice. Invoke it after each provisional mutation, before closing validation/commit, also on private trees and on a single-mutation group that never yields. Edit stages exactly this splice; on the final one it takes the publishable snapshot and seals the whole batch. The observer can decline, causing undo to abort the group and return an explicit prepare-refused result; edit returns the observer's saved cause. It may read the view and take a snapshot, never mutate or reenter the log. Undo retains its deadline and checkpoint ownership. No journal or edit types enter undo. Existing callers with no observer retain today's behavior. This is a proposed prerequisite for production replay, not an existing property of `undo_undo`. [Landed undo contract](../../src/undo/undo.h)
- **Journal.** A replacement with deletion and insertion or any other multi-record command has a contiguous nonempty GROUP_BEGIN … GROUP_COMMIT. A single data record stands alone. Journal slices literal bytes, uses checked additions for every destination, offset, size and sequence, retains encoded bytes and write position across turns. Interleaving a different command, generation redeclaration or rotation inside this batch is forbidden. After memory commit the command runs exactly once. Transport pressure yields MORE. A short, zero or failed write preserves the *whole encoded record*, original write offset, accepted prefix and unissued suffix. The owner latches recovery suspension, edit stays EMITTING, status is visible and success remains fenced. Repair off path, explicitly reconcile the exact batch and its final image-qualified position, then continue/publish once. No second INSERT on retry. Continuity loss elsewhere while idle refuses new admission visibly. An uncertain or failed repair keeps the fence closed. [Wire group and reader rules](../../src/journal/journal.h)
- **Observability.** The prior bytes, selection, history cursor (including redo and eviction), saved/cutoff bookmarks, ID, revision, journal sequence and pending change survive every precommit refusal. NOMEM displays **“Edit not applied — buffer memory is full.”** Other causes have distinct statuses. Postcommit suspension preserves the committed RAM edit for recovery and exposes only the prior boundary for frames. After the final complete record (including COMMIT when grouped), install the published snapshot, return the immutable change and signal the matching command ID through the app's reserved reply path. Layout may read the new snapshot before submit. L4/L5 poll the change after submit; headless success uses the same fence with no fake draw or forced flush. Before ACK, process death permits the previous or whole command; after ACK it requires the whole command, untitled included, starting from a verified recoverable BASE. Failed storage barriers, power loss and A23 foreign replacement follow their separate recovery contracts. [PLAN §4.2](../PLAN_V2.md#42-l2-edit-kernel-journal-publish-point--sigkill-kill-oracle) · [Q16](wave1-L2-derivations.md#q16--settled--external-headless-command-completion)

For the change envelope, retain the longest *untouched* exterior prefix and suffix. Initialize both to the original length; for each nonempty splice shorten prefix to at most its offset and suffix to at most the length after its cut in that step's input. Subtract both survivors from the old and new total lengths to get old_len and new_len (P, the two exterior regions survive every step and are disjoint after the first splice). This computes exact extents for a simple replacement and encloses disjoint changes without reading the intervening text. Only the actual splices are journaled or used to move a mark.

## Transaction and burst

Exactly **six reachable tag values** (P, one cell per row below). Implement as a tagged union with exhaustive switches under `-Wswitch-enum`. EMITTING owns a batch even when journal has suspended it. Query suspension and lease custody at the journal owner and source validity at the file owner. No mirror bits, secondary scheduler or stored product of those states.

| Tag | Owner and transitions |
|---|---|
| IDLE | No active command. Refusal/no-op stays here. Successful begin enters OPEN or the requested replay direction. |
| OPEN | Forward checkpoint and private batch. All staging stays here. Failure → IDLE, successful memory commit → EMITTING. No external yield. |
| UNDOING | Private reverse group and old published snapshot. MORE stays here. Refusal/no available group → IDLE; whole commit → EMITTING. |
| REDOING | Same lifecycle, forward replay. Opposite input remains queued. |
| EMITTING | Already committed. Resume from transport cursor. Backpressure, short write or unresolved repair stays here. Complete page-cache acceptance → READY. |
| READY | New boundary visible, change slot full. After submit, consuming it → IDLE. Admission waits rather than overwriting an unseen change. |

For a burst, remember the last eligible **committed** command's kind, offset, extent and monotonic receipt time. Require a collapsed selection, no intervening break, no redo suffix and matching ordinary kind. Type joins at previous insertion end; forward delete at the same start; backspace when this deletion ends at the previous start. Test `now >= last && now - last <= EDIT_BURST_NS`, using **300 ms = 300,000,000 ns** (G, inherited product behavior, including equality and the backwards-clock check at oracle pin `6fec2e8`). Define the constant privately. Each paste, compound/selection replacement, explicit selection change, kind change, admitted save cutoff or replay breaks the chain. A refused mutation leaves the predecessor intact. Undo independently checks structural adjacency and may start a fresh group. [Pinned v1 threshold](</home/tobias/Projects/sublimite-v1-oracle/src/undo/undo.h:67>) · [Pinned v1 join predicate](</home/tobias/Projects/sublimite-v1-oracle/src/undo/undo.c:301>)

## Contracts to check at integration

| Owner | Required call list and obligations |
|---|---|
| Piece | `piece_tree_view/length` for bounds, `piece_iter_init/next_span` for descriptions, `piece_iter_next` for needed literals, `piece_view_backing` for source, `piece_snapshot_take/retain/view/release` for publication, `piece_close` after log destruction. Edit calls neither a logged tree's mutation nor checkpoint functions. At quiescence piece admits from funded slots **and** companions, otherwise exact plan, one local drain and replan if needed, release excess whole extents, then at most one exact base request (G, A26 procedure). Concurrent last release may make the very next request succeed. |
| Undo | `undo_create/destroy/get_history`, `undo_begin/replace/commit/abort`, `undo_undo/redo`, `undo_replay_snapshot` for the pre-group view if needed, plus the proposed replay observer. File supplies nonblocking identity validation; undo performs the closing fault poll. Costs scale with the actual piece operations, captured bytes and staged records. |
| Journal | Proposed runtime operations (wire only has landed): claim ordered batch on the session writer, query generation/reference eligibility, reserve private literal/descriptor storage and checked sequence space, encode/seal, discard, no-fail RAM publish, append/insert_step, inspect accepted position/suspension, reconcile after repair. Allow incremental exact private claims while rollback remains possible. Reject an impossible image length before commit. Batch completion identifies the journal **image and final sequence** for save capture; sequence alone becomes ambiguous after rotation. Use landed insert/delete/restore/frame codecs, group rules and `os_file_pwrite_once`. Journal supplies diagnostics. |
| Savectl through the session owner | Read `edit_read` at the published boundary; call `savectl_content_identity` once per published revision even on return to an equal ID. `savectl_save_request` takes matching snapshot, ID and complete journal view. In the same serialized admission, reserve all claims, seal joining with `edit_save_boundary(CAPTURED)` and have journal embargo the displaced generation **from capture onward**, even after cancellation. A rejected admission does none of these. The cutoff's position, generation and synced temp witness (the fd-derived evidence identifying the file the saver wrote) belong to journal/save. On `savectl_get_model` reporting definitive verified finish transfer the matching cutoff to saved; on definitive loss of the request drop it. Defer logical adoption of completions across an open edit, keep routing messages. Filter stale request/incarnation before notifying edit. On `savectl_take_reload` the host installs a fresh tree/log/domain together, clamping selection itself. |
| App and work | Single UI consumer routes shared-pool messages and ticks owners; edit never drains or submits worker jobs. `os_now_ns` supplies input receipt and slice checks. Global FIFO targets stable buffer incarnations. Open starts acquisition then releases dispatch. Switch-to-ready-tab and Save can proceed until the head needs unavailable text, a grant or replay completion. At that point hold the head, pump progress, and visibly refuse a failed-open dependent before advancing. No input overtakes it. |

Maintain saved and in-flight IDs even after corresponding history becomes unreachable. Position is `evicted_groups + undo_groups` (P, trimmed prefix plus applied groups). On actual branch commit invalidate discarded redo positions before reusing depth. On actual join invalidate the replaced endpoint; use the starting/ending `group_serial` to distinguish a new group. Remove reachability strictly outside the surviving interval, keep IDs for still-owned cutoffs. Match remaining bookmarks only on completed replay. A reload starts a fresh incarnation; identities are compared only within it. With the zero sentinel, savectl can carry a scalar saved ID and compute `saved_id == 0 || saved_id != current_id`. Its proposed knowledge enum and tagged saved-ID struct can be dropped. At the wire boundary translate the existing HAS_SAVED flag. [Q10 and Q15, ratified by A22](wave1-L2-derivations.md)

The **only production caller** of the public maintenance operations is `edit_maintain`. The order is `piece_reclaim` → `piece_lines_resolve` → `piece_replenish` → `undo_maintain`. Reclaim makes retired slots available before resolution, resolution can spend slots, top-up funds the next input, and undo retires history within its own budget. No line-resolution call while replay owns a checkpoint. A failed warm-up rejects no key. Internal exact-admission retirement is a different operation. [A24 and A26](wave0-plan-amendments.md#a24-7b-source-read-rulings-advisor-2026-11-24-docsreviewswave1-piece-7b-source-astramd)

## Proof obligations for the implementer

**Law 2.** Each row below has an expected **zero libc allocations** (G, live release malloc/calloc/realloc guard from ingress through actual submit). VM-backed base claims and their page faults happen inside the guard when necessary. No exemption around admission, a slice, transport or submit.

| Guarded calls | Why the expected count is zero |
|---|---|
| Input decode, FIFO, receipt clock, edit_read/select, join and checked plan | Reserved input/reply slots, borrowed arguments, scalar/stack work. |
| undo_begin, each undo_replace, piece admission and snapshot_take, abort or commit | Base extents, piece slots, ADD chunks and history storage. Exact cold top-up uses base. |
| undo_undo/redo, every continuation and replay observer | Same pools; retained checkpoint, private staging and old published snapshot across deadline. |
| Stage/reserve, wire encode, journal RAM handoff, append/insert_step and write | Checked base staging and retained encoded record. No worker submission, wait, sync or malloc in append. |
| Identity publication, session leaf, layout/frame or headless reply | Preclaimed leaf and reply slot, prepaid render path. A12 excludes libxcb calls capable of reading on UI between ingress and submit, flush included. |

Register the production edit path in the release law2 suite. Fail unless `base_malloc_guard_active() && count == 0` (G, Law 2); count across ingress through **true submit**, including intervening turns, even for back-to-back input and resumed replay. Fixture setup and post-submit maintenance are outside. Run typing, both deletes, paste/compound, undo, redo, cold tab switch and refused exact base claim followed by a successful key. Count maintenance order with wrappers and check production call sites. Measure typing begin/replace/commit p50/p99 (G, provisional bench asks for median and 99th percentile), and full ingress-to-submit median/tail under ordinary typing and heavy writeback with power/load stamps; measure separately from allocation/read counters. [Release guard](../../src/base/base.h) · [A12, A13, A19 and A22](wave0-plan-amendments.md)

**Independent oracle.** Use a flat byte vector with parallel ORIGINAL/ADD coordinates, immutable copies for snapshots, and an undo list of whole before/after states plus selection endpoints. Apply each plan to a private copy. Publish or discard it as a unit. A model clock, adjacency and byte-cap fixtures determine expected grouping, branching and trims. Keep persistent oracle identities for every boundary; compare observable equality for current/saved/in-flight owners, including finishing an older save after typing, undo to cutoff, equal depth on a different branch, joined endpoints and eviction. Model journal as a sequence of complete logical records plus a partial tail; a separate reader applies only complete groups and resolves source generation policy. Compare bytes and accepted revisions independently of production encoding, observers and FSM. Use real named journals in a directory under `build/` for integration and external headless kill tests. Flat services and controlled failures are test doubles at the same mandatory journal contract.

- Inject each claim refusal, conflict on final mapped read, replay refusal after yield, short write and recovery. Compare prior published bytes, selection/mark, history cursor/redo/evictions, bookmarks, ADD length and journal sequence. Exercise private insert then delete in an aborted command, regrow ADD at the old offset, capture and undo; repeat with a literal. Repeat a failed warm-up followed by a fitting key. Verify old snapshots while a replay is provisional and rejection of interleaved writes/lease acquisition.
- Count actual mapped ORIGINAL reads/copied inverse bytes. Eligible one-span delete+undo reads at most **131,072 B** (P, description and deletion each clip two ends at up to 32,768 B; undo carries counts). Inverse copies at most **min(length, 16 × spans) B** (G, A13 chooses the cheaper of literal and 16 B descriptors), with O(spans) inverse storage. Compare the one-byte literal and large one-span equality/reference cases. Test A23's mandatory by-value emission separately and charge its payload to journal. After ORIGINAL counts resolve, check rank/select against the flat bytes before, between and after deletion/undo of **513 chunks** (G, A19 regression, P, one beyond the old 512-chunk boundary); count O(height) node visits. No edit-path newline scan or independent index veto.
- Run AFTER_DELETE / BEFORE_COMMIT / COMPLETE cuts across undo, redo, replacement and paste. The torn-undo fixture has **262,144 B** on one side (P, v1 fixture 16 × 16,384 B) and **1 B** on the other (G, its replacement byte). Only a whole side may recover. Kill just before and after the external matching ACK with no graceful flush. Include untitled, prepared save restoring by value, foreign replacement/unresolved BASE, source conflict on in-place overwrite, independent fsync/dirsync outcomes, and the save/discarded-redo branch fixture. [Harvest acceptance](wave1-L2-L5-harvest.md#44-acceptance-lines-that-already-exist) · [A23](wave0-plan-amendments.md#a23-the-save-protocol-no-retention-rules-17-t5-is-a-stated-product-contract-advisor-2026-11-23)

**Rejected alternative.** Call the landed replay function and stage from its dirty interval afterwards. Delete a mapped range, start a save that makes its generation value-only, then undo that deletion. Undo can successfully restore a span using already-funded tree storage. If journal now cannot fund the literal payload, the history cursor has already moved; issuing a compensating redo can itself fail and cannot restore every observer. A single-group count prevents partial success across groups but does nothing for this last mutation. A preparation veto inside the still-owned replay checkpoint covers exactly that case.

**Advisor signature points.** Approve the replay preparation point and the journal runtime's exclusive, rollback-safe private admission and no-fail RAM handoff. Choose the production history byte cap from actual slot/literal charges; the header accepts that policy as an input. Q10's ID owner and Q15's FIFO rule already have A22 decisions. The proposed ID sentinel is the corresponding small change for the savectl header. No measurement or implementation result is claimed here.
