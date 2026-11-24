# savectl — proposed header for advisor signature

A save commits the bytes of a published edit boundary. A durable FINISHED installs that boundary's content ID after verification, even if later typing has changed the text. Save, Keep and conflict protection append to the named journal. File operations and journal sync run asynchronously. (G) marks a contract choice and (P) a consequence of the indicated construction. Source and trace numbers are identifiers.

## Draft A27 for PLAN §4.4

> Expose save phase, external change and close status. Store one exhaustive phase tag with clean/changed variants of IDLE and RECOVERY_PENDING. RELOADING projects reload in progress. The changed variants, CONFLICT_CANCEL and CONFLICT_CHECKPOINT project changed-on-disk. Other tags project no classified change. An independent open/close-pending enum governs admission and lifetime. Dispatch each enum with exhaustive switches under `-Wswitch-enum`, without a default. Journal reports append suspension directly to the session.
>
> Pin the published cutoff at capture and embargo the displaced generation immediately. The UI writes all records. Interleave commands with peer checkpoints and seal and sync the covered prefix before PREPARED, then sync PREPARED before rename. The session owner fences dependent membership through target publication. After target-parent durability and verification, append the installed declaration and FINISHED. Require a covering DURABLE journal result and reverify the binding before installing the captured saved ID. A complete FINISHED with foreign binding consumes the token. Clear the saved ID, report SAVE_FAILED with CHANGED and secure a conflict checkpoint. Append a by-value checkpoint for Keep or conflict, and CANCELLED for a reconciled open token. Later commands, independent peers and other tokens retain their own logical positions.

## Proposed src/savectl/savectl.h

```c
#ifndef SAVECTL_SAVECTL_H
#define SAVECTL_SAVECTL_H

#include <stdbool.h>
#include <stdint.h>
#include "journal/journal.h"
#include "os/os.h"
#include "piece/piece.h"
#include "work/work.h"

typedef struct savectl savectl;
struct file;
struct file_target;
struct journal;

/* A cutoff is the saver's last complete published edit boundary at admission.
 * S is its immutable text snapshot. An image-local boundary is a complete
 * command position meaningful only in the journal image that assigned its
 * sequence. Lowercase s denotes that position for S, qualified by its image.
 * Sealed joining means the next edit cannot join an undo group across the
 * cutoff. The saver can therefore recognize a reachable undo back to S.
 *
 * H is the generation supplying the saver's prior source bytes. G is the
 * displaced generation whose named source the destination rename would
 * replace. Either can be absent, and Save As can give them different keys.
 * An embargo requires all later restores from G to be recorded as literal
 * bytes, including undo and redo. It survives cancellation and reopening.
 * A peer fence is the session owner's reservation on changes to which
 * live buffers, closed-kept buffers and recovered drafts depend on G.
 * The session checks every admission, reopen, import, rebind, eviction
 * and Save As against it. Before permitting rename it establishes that
 * every other dependent has durable literal text and an independent
 * suffix. Hold the fence through actual target publication, including
 * uncertain rename. Independent edits continue. An unavailable owner
 * defers displacement. A generation is the journal key for one source.
 *
 * A witness identifies the synced output by its held file descriptor's
 * volume, inode words, length and CRC32C over the whole text. A binding
 * is the intended canonical path and full tagged expectation of absence
 * or presence with stat. A failed stat is an error. A token names an
 * admitted save of this buffer. Its PREPARED record carries the witness,
 * expected destination and cutoff. Only its barrier permits rename.
 * The installed declaration describes that same fd after rename.
 * Append it before FINISHED. A complete FINISHED supplies an origin
 * candidate. Adopt the saved association only with the verified binding,
 * live and in recovery. Recheck the fd/path before issue and adoption.
 * Report SAVED only on verified durable finish. A complete FINISHED
 * whose installed file is missing or foreign at verification consumes
 * the token. Set saved_id to zero, report SAVE_FAILED with CHANGED, and
 * write and sync the conflict checkpoint. FINISHED was the sole terminal.
 *
 * A checkpoint pins published text at its BEGIN. Its typed literals
 * and optional final TARGET describe recoverable text and binding at
 * the covered boundary. Its COMMIT makes that operation visible. A
 * covering barrier is a DURABLE result on the original sync request
 * whose credited position reaches the required file end in its epoch.
 * Journal matches the pinned inode and submitted end internally.
 * Keep and conflict detection use this checkpoint with distinct TARGET
 * expectations, followed by a covering barrier. A cancelled save needs
 * reconciled namespace and an independently usable prior or checkpoint
 * representation, then appended CANCELLED and its barrier. It cannot
 * install the requested saved id. A later command or metadata decision
 * survives any of these cutoffs.
 *
 * IDLE_CLEAN permits ordinary save and IDLE_CHANGED offers Keep or
 * reload. CLEAN means no classified external change. RELOADING acquires
 * a replacement source. PREPARING secures the temp, peers and PREPARED.
 * WRITING publishes the target. DIRSYNC_RETRY owns an installed target
 * awaiting only its directory barrier. VERIFYING_FD compares the path
 * with the held descriptor. FINISHING owns the verified candidate and
 * drives its publication.
 * RECOVERY_PENDING retains a possible or complete prepare pending
 * cancellation, with a variant for a classified change. CONFLICT_CANCEL
 * revokes finish after a post-rename mismatch and secures dirty text.
 * Logical close refuses new requests while settling admitted effects.
 * CONFLICT_CHECKPOINT secures the dirty text after a complete FINISHED
 * with a failed binding check. Resolve can retry its checkpoint or barrier.
 */
typedef enum savectl_phase {
    SAVECTL_IDLE_CLEAN, SAVECTL_IDLE_CHANGED, SAVECTL_RELOADING,
    SAVECTL_PREPARING, SAVECTL_WRITING, SAVECTL_VERIFYING_FD,
    SAVECTL_DIRSYNC_RETRY, SAVECTL_FINISHING,
    SAVECTL_RECOVERY_PENDING_CLEAN, SAVECTL_RECOVERY_PENDING_CHANGED,
    SAVECTL_CONFLICT_CANCEL, SAVECTL_CONFLICT_CHECKPOINT
} savectl_phase;
typedef enum savectl_closing {
    SAVECTL_OPEN, SAVECTL_CLOSE_PENDING
} savectl_closing;
typedef enum savectl_err {
    SAVECTL_OK, SAVECTL_BUSY, SAVECTL_CLOSED, SAVECTL_CHANGED,
    SAVECTL_SOURCE, SAVECTL_NOMEM, SAVECTL_LIMIT, SAVECTL_UNSUPPORTED,
    SAVECTL_FILE_IO, SAVECTL_JOURNAL_IO
} savectl_err;
typedef enum savectl_outcome {
    SAVECTL_NO_SAVE, SAVECTL_ADMITTED, SAVECTL_SAVED, SAVECTL_CANCELLED,
    SAVECTL_SAVE_FAILED
} savectl_outcome;
typedef enum savectl_origin {
    SAVECTL_CONTROLLER, SAVECTL_FILE, SAVECTL_JOURNAL
} savectl_origin;

/* The status presenter uses error to distinguish readiness, closed
 * admission, changed destination, unverifiable source, refused memory,
 * exhausted limits, unsupported operations and service I/O. Diagnostics
 * use origin to decode the active service's exact code. native_error is
 * the OS error captured at that failure, or zero if none was supplied.
 * A controller-origin diagnostic has no service detail. */
typedef struct savectl_failure {
    savectl_err error;
    savectl_origin origin;
    union { int file_error; int journal_error; } detail;
    int native_error;
} savectl_failure;
typedef struct savectl_identity {
    journal_presence presence;
    os_stat stat; /* This is meaningful only for a present binding. */
} savectl_identity;
typedef struct savectl_model {
    savectl_phase phase;
    savectl_closing closing;
    uint64_t current_id, saved_id, request_id;
    bool dirty;
    savectl_outcome outcome;
    savectl_failure failure;
} savectl_model;

/* The session knows the published boundaries and owns generation
 * membership. checkpoint supplies the indicated owner's current
 * published text to journal_checkpoint_request with its canonical
 * TARGET, saved association, piece_validate_fn and borrowed context. The
 * validating owner refuses close through request release and retirement,
 * including supersession. The checkpoint callback returns an owned
 * journal request. fence advances peer coverage for this transaction
 * and retains the membership reservation when ready. It returns BUSY
 * while work is pending. release_fence settles the owner's reservation.
 * These callbacks are nonblocking and serialized with membership changes.
 */
typedef struct savectl_session {
    void *ctx;
    savectl_err (*checkpoint)(void *, uint64_t owner,
                              const journal_target *, journal_request **out);
    savectl_err (*fence)(void *, uint64_t saver, uint64_t token,
                         uint64_t displaced_key);
    void (*release_fence)(void *, uint64_t saver, uint64_t token);
} savectl_session;
typedef struct savectl_options {
    savectl_session session;
    work_pool *pool;
    piece_reserve *reserve;
    struct journal *log;
    struct file *source;
    struct file_target *binding;
    uint64_t buffer_id, current_id, saved_id;
} savectl_options;
typedef struct savectl_request {
    piece_snapshot *snapshot;
    struct file_target *destination;
    uint64_t displaced_key; /* The session resolves G for this destination. */
    uint64_t content_id;
} savectl_request;
typedef struct savectl_reload_result {
    piece_tree *tree;
    struct file *source;
} savectl_reload_result;

/* The UI owner makes every call. Required pointers, live objects and the
 * calling thread are caller invariants checked by ASSERT. The shared pool,
 * reserve and required session journal outlive the controller and its work.
 * The source can be NULL for private text and the binding for untitled text.
 * Each call borrows its inputs. Accepted operations retain the pins and
 * wire-slice storage they need until physical completion. A refusal leaves
 * outputs and ownership unchanged. All UI calls do bounded control work.
 *
 * Create retains the binding and source and starts open in IDLE_CLEAN.
 * Edit IDs start at 1 (G, first nonzero identity per edit). A saved ID of
 * 0 means unknown (G, edit's reserved ID). Create can report BUSY during
 * prior recovery, SOURCE for missing validation, NOMEM or LIMIT. */
savectl_err savectl_create(savectl **out, const savectl_options *options);

/* Edit publishes every committed revision here, even when undo restores a
 * previous ID. This invalidates older reload results without I/O. */
void savectl_content_identity(savectl *s, uint64_t current_id);

/* The model is a value copy. Dirty means saved_id is zero or differs from
 * current_id. Outcome belongs to the latest admitted request_id, the
 * capture token for this buffer. NO_SAVE precedes admission. ADMITTED
 * retains an open request, SAVED reports verified durable finish and
 * CANCELLED reports durable cancellation. SAVE_FAILED carries the
 * failure diagnosis. An ambiguous effect still owns its cutoff. Failure
 * describes the most recent operation failure, including asynchronous I/O.
 * In CONFLICT_CHECKPOINT and at its settlement the outcome stays
 * SAVE_FAILED and failure.error stays CHANGED. If protection fails,
 * origin, service detail and native_error identify the retry cause.
 * target_identity borrows the binding expectation until the next controller
 * call. An unbound target returns NULL. Tick owns the installed declaration,
 * token and FINISHED append. The host observes the request through this model. */
savectl_model savectl_get_model(const savectl *s);
const savectl_identity *savectl_target_identity(const savectl *s);

/* Admit only from open IDLE_CLEAN with a validated destination and
 * matching published snapshot and content ID. In one UI transition
 * reserve all resources, use journal_capture to pin s and embargo G,
 * receive s, H, token and new key in journal_capture_info and the owned
 * cutoff separately, and have edit seal joining. A provisional G operand must finish
 * before capture or be aborted and restaged by value. BUSY covers a
 * stale boundary, outstanding check or conflicting dependency. CLOSED,
 * CHANGED, SOURCE, LIMIT and NOMEM describe pre-admission refusal.
 * Success retains the cutoff while file writes and syncs S. */
savectl_err savectl_save_request(savectl *s, const savectl_request *request);

/* Queue the sole eligible action, including during close. Retry the
 * directory barrier in DIRSYNC_RETRY or failed finish in FINISHING.
 * Both RECOVERY_PENDING variants authorize cancel. In CONFLICT_CANCEL
 * reconcile any possible finish first and cancel only a still-open token.
 * CONFLICT_CHECKPOINT retries protection of a consumed token's dirty text.
 * Return BUSY elsewhere or while the action is pending. */
savectl_err savectl_resolve(savectl *s);

/* Coalesce watch and focus hints and schedule an off-path identity check. */
void savectl_file_event(savectl *s);
/* Reload accepts discarding local edits through the captured revision.
 * Keep accepts the observed destination after securing the current text.
 * Both require an open idle controller and Keep requires IDLE_CHANGED.
 * Admission may return BUSY, CLOSED, SOURCE or FILE_IO. An asynchronous
 * failure leaves the old tree installed and supplies a model diagnostic. */
savectl_err savectl_reload(savectl *s);
savectl_err savectl_keep(savectl *s);
/* Transfer the ready, validated tree and source to the UI owner. The
 * caller installs both with fresh history and the supplied new domain's
 * initial ID in the same session publication before dispatching input.
 * Return BUSY while unready, CLOSED on close or CHANGED if superseded. */
savectl_err savectl_take_reload(savectl *s, uint64_t new_id,
                                savectl_reload_result *out);
/* Adopt an idle peer's reconciled binding. NULL detaches the peer and
 * keeps its former target in the session root. Preserve text and history.
 * Return BUSY, CLOSED or CHANGED if another disposition owns the peer. */
savectl_err savectl_rebase_target(savectl *s, struct file_target *binding);

/* Receive acknowledges messages routed to this controller. Tick
 * advances the eligible action on delivery, capacity or a session
 * dependency change. The session routes journal_receive separately
 * and calls journal_service each turn. Neither controller call waits.
 * For a record use journal_append, inspect journal_request_result,
 * and use journal_sync through its visible position. Adopt only a
 * DURABLE result covering that position in the same image epoch. Use
 * journal_request_release on retirement and journal_cutoff_release
 * after definitive settlement. A possible
 * write retains its request while the session checks journal_state
 * and drives journal_reconcile. */ 
bool savectl_receive(savectl *s, const work_msg *msg);
void savectl_tick(savectl *s);

/* Begin close idempotently. Refuse fresh requests and reload installation
 * while settling admitted effects. The session owner secures kept text
 * or settles explicit discard and detaches membership before destruction.
 * Destroy returns BUSY unless closing, idle and clear of all outstanding
 * holds and obligations. On success it releases setup references and
 * consumes the object. Final backing cleanup runs off the UI path. */
void savectl_close_begin(savectl *s);
savectl_err savectl_destroy(savectl *s);
#endif
```

## States and transitions

The table describes the sole stored phase/conflict tag. One shared-pool operation slot per controller (G, serialize this transaction's effects) owns the pending intent, executing handle or sealed result. Its operation and transport status use exhaustive enums. They identify the actual file or journal action, including a failed action awaiting explicit retry. No controller copy of journal custody or suspension drives admission. During one UI turn, derive phase and external observations from the same tag and inspect journal health separately when needed. Closing is independent.

| Tag | Entry evidence and permitted transitions |
|---|---|
| IDLE_CLEAN | Create here with validated source/binding and initial IDs. An equal stat preserves the expectation; a stat error reports its actual cause and bars save pending validation. On confirmed foreign binding enter IDLE_CHANGED. Admit Save only at a complete published boundary without a pending check. Reserve claims, pin S, s, destination and bookmark, seal joining and install VALUE_ONLY for G before another command can stage a restore. Enter PREPARING. Explicit reload enters RELOADING. |
| IDLE_CHANGED | Start a by-value checkpoint of validated live text with its original conflicted TARGET when the change is detected. Preserve history and mark the saved association unknown if lost. If mapped ORIGINAL is stale, report SOURCE and preserve evidence. Explicit Keep validates the current binding and secures text with updated TARGET; on current, durable adoption enter IDLE_CLEAN with saved ID unknown. Explicit reload or silent reload of an unmodified externally changed buffer enters RELOADING. Failed Keep leaves the prompt and text intact. |
| RELOADING | Capture revision, former binding and observation epoch. File opens a new registered source/tree independently. An intervening edit (including undo to the same ID), newer watch, acquisition failure or close invalidates installation. On physical completion discard the invalid candidate and return to idle with the observed classification; preserve the old tree/history. Valid take while open transfers tree/source. Session installs fresh edit history, binding and matching current/saved IDs and clamps its latest selection and scroll before new input. Enter IDLE_CLEAN. |
| PREPARING | File streams S to the temp, copies metadata, computes the whole-stream witness and syncs. Then run the peer, fence and PREPARED protocol below. A definitive pre-marker failure releases the intent and returns idle, changed if appropriate. A possible marker enters the corresponding RECOVERY_PENDING variant; retain cutoff and evidence. A durable prepare authorizes entry to WRITING. Keep the membership fence through publication. |
| WRITING | Recheck source, destination and membership fence. For expected absence use atomic NOREPLACE; an existing file conflicts and lack of support refuses. Existing-target replacement has a race with an external writer after the final check. Rename once, retain output fd and sync target parent. Definite pre-rename refusal (including close before launch) with token enters RECOVERY_PENDING. A proven rename and failed barrier enters DIRSYNC_RETRY; a proven barrier enters VERIFYING_FD. Reconcile uncertain rename from actual binding and fd. A post-rename mismatch/missing/unverifiable binding takes precedence over a barrier error and enters CONFLICT_CANCEL. |
| DIRSYNC_RETRY | Own the proven rename, held fd and missing target-directory barrier. Resolve retries only that barrier. Failure stays here; success enters VERIFYING_FD. A known post-rename conflict enters CONFLICT_CANCEL. |
| VERIFYING_FD | Derive identity and prefix from the held **post-rename** fd. Check the witness and path binding. Equal plus proven directory barrier creates the candidate and enters FINISHING. A foreign, missing or unverifiable binding enters CONFLICT_CANCEL. |
| FINISHING | This tag owns the verified finish candidate. Tick drives initial finish as soon as the verified declaration is available. Recheck fd/path before publication and on adoption. Append GENERATION and matching FINISHED, then request the covering barrier. Failure/ambiguity retains evidence; resolve retries finish on the original request after reconciliation. On durable verified adoption install the cutoff ID and binding, reconcile peer TARGETs, publish SAVED and enter IDLE_CLEAN. A failed binding check clears the saved ID, marks the buffer dirty and starts conflict protection. If FINISHED is already complete enter CONFLICT_CHECKPOINT. If it is absent or possible enter CONFLICT_CANCEL and classify the original append before any terminal decision. |
| RECOVERY_PENDING_CLEAN | A possible or confirmed PREPARED has no verified replacement. Fresh saves are busy. Resolve requests cancel: reconcile and sync the target namespace, secure a usable prior or committed checkpoint plus suffix, append CANCELLED and obtain a covering DURABLE result. Failure retains evidence; a newly observed pre-rename conflict changes the tag to RECOVERY_PENDING_CHANGED. Definitive cancellation enters IDLE_CLEAN and reports CANCELLED. |
| RECOVERY_PENDING_CHANGED | As above with a classified pre-rename conflict. Start the validated conflict checkpoint on detection. Resolve only cancels. Definitive cancellation enters IDLE_CHANGED. Prior saved association survives only while verified. |
| CONFLICT_CANCEL | A post-rename mismatch sets saved ID unknown. Preserve held sources, suffix and actual publication evidence, start the conflict checkpoint immediately and clear the candidate. Resolve reconciles any possible FINISHED on the original request. A complete FINISHED enters CONFLICT_CHECKPOINT. Only an open token can proceed to durable CANCELLED and IDLE_CHANGED. Preserve the foreign file and keep dirty text and obligations on failure. |
| CONFLICT_CHECKPOINT | FINISHED consumed the token and the installed binding failed verification. Publish SAVE_FAILED with CHANGED immediately, clear the saved ID and retain the cutoff until protection settles. Write a by-value checkpoint with the conflicted TARGET and unknown saved ID, preserving later commands. Tick finishes the checkpoint and its covering sync. Resolve retries a failed protection step. After durable protection release the cutoff and fence, enter IDLE_CHANGED and retain SAVE_FAILED with CHANGED. No second terminal is legal. |

There are **24 reachable lifecycle states** (P, twelve phase rows times the two independent closing tags, since each phase can be entered open and logical close can arrive before its next adoption). The CLOSE_PENDING RELOADING case holds an invalidated install until the job retires. Each tag's entry above gives its open witness; for the two recovery variants inject an uncertain prepare sync on an unclassified/clean target or an actual pre-rename destination mismatch after durable prepare. No post-destruction object can be queried. The count excludes work progress, data and journal-owned state.

File hints coalesce by epoch during any operation; classify them against actual effects, including our own rename. A later external replacement after a verified adoption starts a new observation. A published edit advances revision even at an equal ID; defer adoption affecting a provisional operand, and keep pumping. All out-of-order user actions return BUSY, or CLOSED for new admission during close. A duplicate notification never resubmits an effect. Failed or ambiguous admitted attempts report SAVE_FAILED under their request ID while reconciliation is pending. A consumed FINISHED with a failed binding check remains SAVE_FAILED with CHANGED after its conflict checkpoint is durable. Eventual success adopts the cutoff bookmark in edit; cancellation or definitive failure drops it. Preserve owned cutoff identity through history eviction, invalidate discarded redo positions and refuse checked-counter exhaustion before publication.

Logical close sets CLOSE_PENDING, settles effects already exposed, suppresses installation into a removed tab and withdraws unsubmitted file publication. The session owner commits closed-kept membership or settles explicit eviction/discard and releases the associated close obligation. Destroy requires either idle tag, CLOSE_PENDING, and no handle, queued result, token, checkpoint, fence, or other unresolved obligation. Otherwise it returns BUSY, preserving ownership and resolution access.

Work slots carry checked generation/handle identities. Failed submit releases job-only pins; capacity return or result adoption drives tick. A full mailbox retries delivery of the immutable completion through a continuation. Keep worker inputs through physical acknowledgment and defer expensive final-root release. Keep view, tab order, binding, independent tokens, post-cutoff suffixes and generation policy in each complete session publication. The peer index covers closed-kept and detached/recovered drafts as well as live owners. Rebinding an idle displaced peer preserves history and independent dirty identity; only a proved unchanged binding retains clean association. A23 allows at most one live owner per file identity (G, session index rule).

## Append and barrier protocol

| Operation | Writer order and required publication result |
|---|---|
| Capture | The session obtains G from its destination index and settles affected provisional operands. `journal_capture` pins s and installs VALUE_ONLY. Savectl receives the owned cutoff and a `journal_capture_info` containing s, H, the token and the fresh key. Pin S and seal edit joining in the same dispatch. On refusal release partial claims before admitting other input. |
| Prepare | File streams S, copies metadata and syncs the temp. Session enumerates all G-dependent peers, including recovered and closed-kept drafts, and requests a `journal_checkpoint_request` for each. Each BEGIN captures a published boundary. Coalesce only on sufficient coverage and binding, otherwise restart with a fresh BEGIN. Require a covering DURABLE result from `journal_sync` for each committed checkpoint and independent suffix under the embargo. Recheck membership and retain the fence through publication. Append PREPARED via `journal_append`, seal and sync it. Only its covering DURABLE result authorizes the file handoff. |
| Publish the target | Recheck expected absence or full identity and fence. Rename once. On a proven successful rename and failed parent barrier retry only that barrier. Keep fd and evidence across retries. Reconcile uncertain rename by observation. Verify the actual path against the held output fd after the barrier. Foreign or unverifiable output revokes finish immediately. |
| Finish | Append the fd-derived GENERATION, then matching FINISHED and sync through it. Before FINISHED the declaration cannot bind or enable ranges. At complete FINISHED replay evaluates the origin candidate through s, verifies the binding before adopting a saved ID and applies the selected suffix once. Derive peer binding changes from that transaction. Competing later decisions retain priority. On a covering DURABLE result, savectl rechecks the binding before installing the cutoff ID. A foreign binding follows CONFLICT_CHECKPOINT. |
| Conflict | Immediately request a checkpoint of validated published text with the conflicted TARGET and saved association unknown when lost. Keep any unresolved token. At COMMIT process recovery can use the draft and suffix. A covering DURABLE result through COMMIT secures it for machine loss. Never accept foreign bytes as the new BASE. If the live mapped source was overwritten, report SOURCE and preserve what is usable. |
| Keep | Revalidate the observed destination, request a checkpoint with the accepted TARGET, and wait for its covering DURABLE result. Preserve history and later commands. Adopt only if the observation still holds. A newer binding/save decision supersedes stale checkpoint metadata and restarts a request needing the new binding. |
| Cancel | Reconcile and sync the target namespace. Secure the complete matching checkpoint plus suffix, or verify the prior representation still serves. Append CANCELLED referencing its checkpoint COMMIT (zero for the usable prior) and await the covering DURABLE result. Release only that cutoff and token. A possible prior terminal must first be classified. |
| Retry and close | Read `journal_request_result` on the original request. Definitely absent allows a fresh admission. Possibly written preserves evidence. Visible permits requesting/retrying its barrier. Journal verifies the sealed inode, epoch and submitted end internally. Savectl requires DURABLE covering its required position. ACKs stop on continuity loss. The session drives exceptional recovery and retains admitted obligations through physical retirement. |

The UI gives input first service, then each ready record class in rotation, then barriers. A peer checkpoint costs L + 40k + 132 + D journal bytes (P, L literal bytes in k INSERTs with header and destination, two markers and descriptor, optional encoded TARGET of size D). A key can be accepted between those records. Its frame and ACK wait for its own final. Fixed membership, capacity reserved to close, and progressing workers and I/O give a finite checkpoint service opportunities under continuous typing. There is no elapsed-time bound. [Runtime header](wave1-journal-wire-amendment.md)

At a process stop, use the named file's complete physical prefix. At machine loss use the surviving prefix through the last successful seal on the durably named inode. A barrier through a peer's COMMIT covers intervening commands too. A torn PREPARED forbids target rename. Once PREPARED is durable either old or installed binding after rename can recover the saver, and every displaced peer has its own durable checkpoint. A partial FINISHED leaves that prepare branch intact. Ordinary rotation is the writer's exact-match switch and defers from capture until settlement. [Crash cuts](wave1-journal-write-model.md)

## Seam table

The six remaining handoffs (P, three file operations, one session rule and two journal results) are below. The cited brief lines establish order and existing capabilities. The runtime signatures are proposed for joint signature. [File brief](../briefs/L2-1-file.md) · [Save brief](../briefs/L2-3-journal-save.md) · [Runtime proposal](wave1-journal-wire-amendment.md)

| Owner and handoff | Evidence, missing contract and caller |
|---|---|
| File · binding and expectation | L2-1 lines 13–15 cover registered open, change detection and live validation. **GAP** for retaining/releasing an immutable canonical path and full stat tagged PRESENT/ABSENT, with failed observation separate. Savectl uses this for Save As, Keep, reload and rebase. |
| File · synced temp custody | L2-1 line 16 orders snapshot write, metadata copy, witness and temp sync. **GAP** for moving fd, temp name, witness and cleanup ownership across asynchronous jobs, submission refusal and close. PREPARED needs that exact output. |
| File · classified publication | L2-1 line 16 specifies conditional rename, parent sync and fd verification. **GAP** for definite pre-rename failure, uncertain rename, proven rename with failed barrier, verified install and foreign/unverifiable result, including directory-only retry on the original handle. Lines 17–18 and 24 cover registered reload and physical retirement. |
| Session · generation membership | L2-3 line 12 requires peer enumeration and synced checkpoints. **GAP** for the L5 app_buffers owner to fence dependency changes through the actual rename. It also settles provisional operands when installing the embargo at capture. Open, import, reopen, close/eviction and Save As participate in the same index. The session callback returns readiness only after durable coverage and holds it through disposition. |
| Journal · classified append effects | L2-3 lines 12–15 specify prepare/finish/cancel and external-change checkpoint ordering. **GAP in landed runtime**. Proposed `journal_append`, `journal_checkpoint_request`, `journal_request_result`, `journal_state` and `journal_reconcile` distinguish definitely absent, possible, visible, durable and continuity loss. Reconcile a possibly published terminal before retry. No normal save-side journal-name switch exists. |
| Journal · inode-specific barrier | L2-3 line 12 calls for sync through prepare. **GAP in landed runtime**. Proposed `journal_sync` seals the absolute file end, pins the fd and image epoch, and returns only the submitted watermark on success. Savectl checks the public DURABLE class and credited position against the required final and releases the request after adoption. |

## Flat oracle and discriminating traces

Use [the piece model](../../tests/piece_model.h) as the pattern: the reference and implementation accept the same public calls. The reference stores byte arrays for every live/closed draft, linear command histories with stable content IDs and bookmarks, a map of names to inode objects, and separate kernel-visible and durable journal sequences and directory bindings. Keep volatile held fds distinct from named restart sources. Store checkpoints as full byte arrays plus covered command boundary; retain a set of embargoed generation keys. A list of pending work, immutable completions and physically released holds replaces threads. Compute expected dirty, finish eligibility, acceptable resolutions and replay from these facts. Never call the controller's classifier, encoder, transition function or replay code for the expected answer.

Drive each row above through public operations and independent file/journal observations. At each step compare the phase and closing tag, the derived external observation and outstanding requests, publication classes and positions, request outcome/ID, diagnostics, dirty and finish eligibility, actual bytes, binding ownership, issue order, records and release counts. Attempt forbidden requests in each reachable state and compare before/after. Vary message delay/duplication, unrelated mailbox traffic and slot reuse, close before and after physical effects, source invalidation, failed submit, prepare, target-directory and terminal-record barriers. Check zero allocations on the typing path (G, Law 2), continued mailbox progress while an input head waits and balanced holds. Exercise reload revocation after undo to the original ID, host adoption of the returned tree/source, restored-mtime rewrite and failed Keep. [v1 controller](</home/tobias/Projects/sublimite-v1-oracle/src/savectl/savectl.c>) · [controller fixtures](</home/tobias/Projects/sublimite-v1-oracle/tests/savectl_test.c>) · [reload identity fixture](</home/tobias/Projects/sublimite-v1-oracle/tests/savectl_reload_test.c>)

At every temp hook, checkpoint prefix/commit/sync, PREPARED partial append/complete append/sync, target rename/dirsync/verification, installed declaration, FINISHED/CANCELLED append and covering barrier and completion before/after delivery, drop all process-only state. For K replay the visible complete prefix; for M enumerate surviving complete prefixes through at least the last successful barrier, and old/new directory bindings across each unsynced rename. Ignore partial groups. Select old replay on expected old binding, S plus the saver's complete post-s commands on matching new witness, newer usable by-value checkpoint plus suffix independently. Compare every buffer's text, target, membership and view. At startup the session waits for the `journal_recover` request to complete. It takes buffers, selected controls and any preserved evidence with `journal_recovery_next` until false, then calls `journal_request_release`. A recovered FINISHED with a foreign binding supplies an origin candidate and leaves the saved ID unknown. The session retains the changed binding observation for conflict protection before admitting another save. Unresolved evidence stays available for salvage and cannot start a new transaction on the affected binding. [Durability oracle](wave1-L2-q2-protocol.md)

| Trace | Independent oracle |
|---|---|
| T1, ordinary save | Prior or new binding gives identical current text for the surviving prefix. Only verified durable finish installs cutoff as saved in the live service. Later typing stays dirty; undo back to reachable cutoff clears dirty. |
| T2, Save As from H over peer on G | Stop partway through peer checkpoint: G still named. At rename peer can already recover its literal text and suffix. New branch alone adopts destination for saver. Peer keeps its draft, independent identity and dirty state. |
| T3, same peer closed-kept | Repeat with no live tree, then reopen from durable TABS. Membership, bytes and view survive at every cut. Unavailable dependency owner defers rename. |
| T4, undo during temp write | Delete X, capture Y, undo before temp sync. Journal restored X by value. Once whole undo accepted, K returns YX on either binding; finish saves Y and leaves YX dirty. Incomplete group yields prior Y. |
| T5 / Bug 2 | Save edited `old!`; foreign rename to `foreign` at RENAMED. Verification enters CONFLICT_CANCEL while the token is open. Preserve dirty text, its suffix and the foreign file. Repeat during directory retry, during a possible FINISHED, and after complete FINISHED before adoption. In the last case assert CONFLICT_CHECKPOINT, SAVE_FAILED with CHANGED, unknown saved ID, a durable conflict checkpoint and exactly one terminal (P, FINISHED consumed the token). |

**The T5 restart interval matters.** For arbitrary large missing X, PREPARED contains a witness and boundary, not X. If the foreign binding survives and neither old nor new source has a name, a kill before the conflict checkpoint can lose the unsaved difference. Report unresolved BASE, preserve the journal and offer surviving literals; apply nothing to the foreign file. A complete conflict group in the kernel-visible **named** image suffices for K. M can use a surviving complete checkpoint and is guaranteed that representation after a covering DURABLE result, even with the token outstanding. A group still reachable only via a worker fd does not secure either restart. If M instead preserves our binding, the new branch still works. A buffer at or below the self-contained size recovers exactly from its durable whole-text checkpoint and independently replayable suffix. First edits before that off-path checkpoint becomes durable still have the initial BASE-relative exposure. The size and its append-rate measurement belong to [the journal runtime brief](../briefs/L2-2b-journal-runtime.md).

**Rejected alternative.** In v1 the worker detects the wrong inode, but `savectl_tick` sets both `needs_finish` and `external`, preserving a BASE captured from the foreign path. Finish rechecks that same foreign BASE and marks `old!` clean. Tick can issue FINISHED only from FINISHING. A mismatch revokes that capability and keeps the live text dirty. One generic failed-token phase would introduce the opposite error: a verified output followed by journal writeback loss can recover-finish, whereas a foreign output requires conflict protection and the classified wire effect determines whether cancellation is legal. [Bug 2 source trace](wave0-known-bugs.md#bug-2--post-rename-save-conflict)

For A28, import a G-dependent closed draft after enumeration and before target rename. Require protection or deferral. Interleave a same-buffer key and another peer's group among checkpoint members, restart a checkpoint for changed binding, and deliver its stale producer's final. Require the exact text, bindings and ACKs at each cut. Inject an old-epoch sync completion and a submitted end short of the final. Check the public result on the required sync request. Neither can authorize rename or SAVED. Repeat a possible FINISHED followed by foreign replacement before adoption. Reconcile the terminal before conflict settlement. Use real named journals under `build/` for the later integration campaign.

**Host surface.** Cut `savectl_saved_base` and `savectl_save_token`. Neither has a host consumer. `savectl_tick` appends the declaration and FINISHED. The presenter reads the request ID, outcome, failure and dirty flag from `savectl_get_model`.

**Signature boundary.** The revised runtime returns for signature. This controller contract follows its call surface and the signed saved-association rule. The file handoffs and L5 membership rule require their owners. The runtime owner derives the self-contained threshold from measured fair append service and owns exceptional writeback-loss recovery.
