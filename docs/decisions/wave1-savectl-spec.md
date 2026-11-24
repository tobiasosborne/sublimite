# savectl — proposed header for advisor signature

A save commits the bytes of a published edit boundary. Success installs that boundary's content ID even if the user has since typed or undone. Admission returns before disk work. Success requires a synced output inode, a durable target-directory binding, verification against the held fd and durable publication of the current session.

## Send-back disposition

| Item | Decision |
|---|---|
| 1. Typing under the lease | **Counter-example.** Tail carry under the grant grows with a concurrent paste and can lose an already-synced tail on machine loss. Passing the named writer between checkpoint members can lose an acknowledged key on process kill. Cut both grant-held copies. Build and sync a private image through a matched boundary, then switch the name and writer under the grant. |
| 2. Stored axes | **Cut.** Store the reachable phase/conflict tag and the independent closing state. Query journal for custody. Destruction consumes a quiescent closing controller directly. |
| 3. Resolution selector | **Cut.** Tick drives the first finish as soon as the current session root is available. Resolve requests the sole eligible action after a directory or finish failure, or requests cancellation in a cancel-only state. |
| 4. Standalone mode | **Cut.** Every product controller requires its session journal, including an untitled buffer. Integration fixtures open real journals under `build/`. |
| 5. View ownership | **Cut.** Reload returns the tree and source. The session owner clamps the then-current view when installing them. |
| 6. IDs, time and failure fields | **Cut** the tagged saved ID and request timestamp. Keep the request outcome for edit/session adoption, the common error for user-facing status, the originating service code for file/journal diagnosis and its OS error for the system diagnostic. The service codes share a union. |
| 7. Header vocabulary | **Cut** the dependency on the Q documents for interpreting the API. The header defines the save terms and states the call, ownership and failure contracts in sentences. |

(G) marks a design gate and (P) a consequence of the construction. Digits in identifiers, trace names and references identify existing contracts.

## Draft A27 for PLAN §4.4

> Expose save phase, external state, journal lease and close state as four observables (G, the questions in §4.4). Store one exhaustive phase tag, splitting IDLE and RECOVERY_PENDING by external change, and a separate open/closing enum. Save phase reports IDLE, RELOADING, PREPARING, WRITING, VERIFYING_FD, DIRSYNC_RETRY, FINISHING, RECOVERY_PENDING or CONFLICT_CANCEL. External state projects RELOADING during reload, CHANGED_ON_DISK in the changed idle/recovery variants and CONFLICT_CANCEL, and CLEAN elsewhere. CLEAN means no classified external change. Read UI_OWNED/SAVE_OWNED at the session journal arbiter, which identifies the grantee and independently reports suspension. OPEN/CLOSE_PENDING projects from closing. Successful destruction ends the object's lifetime. Handle every tag with an exhaustive switch under `-Wswitch-enum`.
>
> Capture a published edit boundary. Permit edits during the temp write and sync. Delay edit admission only during exclusive writer sections. Under E2 they cover no sync and no write sized by buffer, peer or session content. A durable PREPARED authorizes publication of the synced temp after peer protection. Verify the installed BASE from the held output fd after the target-directory barrier. A mismatch revokes finish and initiates a by-value conflict checkpoint. A verified finish preserves later commands and independent peer drafts, reconciles their bindings and installs the cutoff ID for the saver alone under A23.

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
struct journal_save_view;

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
 * A peer fence covers the session's live, closed-kept and recovered states
 * that need G. It prevents changes in dependency membership from escaping
 * the durable peer checkpoints through the rename decision.
 *
 * A witness identifies the synced output by its fd's volume, both inode
 * words, byte length and CRC32C of all S. PREPARED durably records the
 * transaction's old expectation, witness and cutoff before target rename.
 * CURRENT is a complete recoverable session image, including later edits,
 * independent peers and other unresolved transactions. A journal grant
 * gives its holder exclusive writer admission at a complete command boundary.
 * A file_target owns a canonical path and its full expected presence/stat.
 * A journal_save_view pins the published session root, image-qualified
 * positions, cutoff bookmark and indexed generation dependencies. */
typedef enum savectl_phase {
    SAVECTL_IDLE_CLEAN, SAVECTL_IDLE_CHANGED, SAVECTL_RELOADING,
    SAVECTL_PREPARING, SAVECTL_WRITING, SAVECTL_VERIFYING_FD,
    SAVECTL_DIRSYNC_RETRY, SAVECTL_FINISHING,
    SAVECTL_RECOVERY_PENDING_CLEAN, SAVECTL_RECOVERY_PENDING_CHANGED,
    SAVECTL_CONFLICT_CANCEL
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
typedef struct savectl_options {
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
    struct journal_save_view *view;
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
 * current_id. Outcome belongs to the latest admitted request_id. Failure
 * describes the most recent operation failure, including asynchronous I/O.
 * The accessors borrow immutable evidence until the next controller call.
 * An unbound target returns NULL. Only FINISHING exposes a verified base.
 * The token is NULL until a candidate exists and while it is in worker
 * custody. A possibly published token alone cannot authorize a rename. */
savectl_model savectl_get_model(const savectl *s);
const savectl_identity *savectl_target_identity(const savectl *s);
const journal_generation *savectl_saved_base(const savectl *s);
const journal_save *savectl_save_token(const savectl *s);

/* Admit only from open IDLE_CLEAN with a validated target, a matching
 * snapshot, content ID and complete published session view. Pin the
 * cutoff, seal joining and embargo G in this serialized admission.
 * Return BUSY for a stale view, partial group or pending check, CLOSED
 * during close, or CHANGED, SOURCE, LIMIT or NOMEM before admission.
 * Success records an intent and leaves peer enumeration to a worker. */
savectl_err savectl_save_request(savectl *s, const savectl_request *request);

/* Queue the sole eligible action, including during close. Retry the
 * directory barrier in DIRSYNC_RETRY or failed finish in FINISHING.
 * Both RECOVERY_PENDING variants and CONFLICT_CANCEL authorize cancel.
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

/* Receive acknowledges routed messages belonging to this controller,
 * including stale notifications. Tick adopts ready results and schedules
 * eligible work. Pass the latest complete session view or NULL to defer
 * work that needs it. The caller pumps shared work and ticks on delivery,
 * capacity, root publication and resolution. Neither call waits. */
bool savectl_receive(savectl *s, const work_msg *msg);
void savectl_tick(savectl *s, struct journal_save_view *current);

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

The table describes the sole stored phase/conflict tag. One shared-pool operation slot per controller (G, serialize this transaction's effects) owns the pending intent, executing handle or sealed result. Its operation and transport status use exhaustive enums. They identify the actual file or journal action, including a failed action awaiting explicit retry. No controller copy of journal custody or suspension drives admission. During one UI turn, derive phase and external observations from the same tag and read writer ownership at the arbiter. Closing is independent.

| Tag | Entry evidence and permitted transitions |
|---|---|
| IDLE_CLEAN | Create here with validated source/binding and initial IDs. An equal stat preserves the expectation; a stat error reports its actual cause and bars save pending validation. On confirmed foreign binding enter IDLE_CHANGED. Admit Save only at a complete published boundary without a pending check. Reserve claims, pin S, s, destination and bookmark, seal joining and install VALUE_ONLY for G before another command can stage a restore. Enter PREPARING. Explicit reload enters RELOADING. |
| IDLE_CHANGED | Start a by-value checkpoint of validated live text with its original conflicted TARGET when the change is detected. Preserve history and mark the saved association unknown if lost. If mapped ORIGINAL is stale, report SOURCE and preserve evidence. Explicit Keep validates the current binding and secures text with updated TARGET; on current, durable adoption enter IDLE_CLEAN with saved ID unknown. Explicit reload or silent reload of an unmodified externally changed buffer enters RELOADING. Failed Keep leaves the prompt and text intact. |
| RELOADING | Capture revision, former binding and observation epoch. File opens a new registered source/tree independently. An intervening edit (including undo to the same ID), newer watch, acquisition failure or close invalidates installation. On physical completion discard the invalid candidate and return to idle with the observed classification; preserve the old tree/history. Valid take while open transfers tree/source. Session installs fresh edit history, binding and matching current/saved IDs and clamps its latest selection and scroll before new input. Enter IDLE_CLEAN. |
| PREPARING | File streams S to the temp, copies metadata, computes the whole-stream witness and syncs. Then run the peer, fence and PREPARED protocol below. A definitive pre-marker failure releases the intent and returns idle, changed if appropriate. A possible marker enters the corresponding RECOVERY_PENDING variant; retain cutoff and evidence. A durable prepare authorizes entry to WRITING. Return writer admission before target publication, even if file transport is full. |
| WRITING | Recheck source, destination and membership fence. For expected absence use atomic NOREPLACE; an existing file conflicts and lack of support refuses. Existing-target replacement has a race with an external writer after the final check. Rename once, retain output fd and sync target parent. Definite pre-rename refusal (including close before launch) with token enters RECOVERY_PENDING. A proven rename and failed barrier enters DIRSYNC_RETRY; a proven barrier enters VERIFYING_FD. Reconcile uncertain rename from actual binding and fd. A post-rename mismatch/missing/unverifiable binding takes precedence over a barrier error and enters CONFLICT_CANCEL. |
| DIRSYNC_RETRY | Own the proven rename, held fd and missing target-directory barrier. Resolve retries only that barrier. Failure stays here; success enters VERIFYING_FD. A known post-rename conflict enters CONFLICT_CANCEL. |
| VERIFYING_FD | Derive identity and prefix from the held **post-rename** fd. Check the witness and path binding. Equal plus proven directory barrier creates the candidate and enters FINISHING. A foreign, missing or unverifiable binding enters CONFLICT_CANCEL. |
| FINISHING | Only this tag owns and exposes the verified finish candidate. Tick drives initial finish when the latest complete root is available; it needs no separate owner decision. Recheck fd/path before publication and on adoption. Publish CURRENT by the switch below. Failure/ambiguity retains evidence; resolve retries finish with the appropriate publication repair, using a fresh inode after writeback loss. On durable verified adoption install the cutoff ID and binding, reconcile peer TARGETs, publish SAVED and enter IDLE_CLEAN. A mismatch at any step revokes the candidate and the finish capability together and enters CONFLICT_CANCEL. |
| RECOVERY_PENDING_CLEAN | A possible or confirmed PREPARED has no verified replacement. Fresh saves are busy. Resolve requests cancel: reconcile and sync the target namespace, secure an independently usable CURRENT with the post-cutoff suffix and other tokens, durably resolve this token. Failure retains evidence; a newly observed pre-rename conflict changes the tag to RECOVERY_PENDING_CHANGED. Definitive cancellation enters IDLE_CLEAN and reports CANCELLED. |
| RECOVERY_PENDING_CHANGED | As above with a classified pre-rename conflict. Start the validated conflict checkpoint on detection. Resolve only cancels. Definitive cancellation enters IDLE_CHANGED. Prior saved association survives only while verified. |
| CONFLICT_CANCEL | A post-rename mismatch sets saved ID unknown. Preserve held sources, suffix and actual publication evidence, start the conflict checkpoint immediately and clear the candidate. Resolve only cancels; never write the foreign target. If a finish image already became visible, build from its actual contents. Cancel success enters IDLE_CHANGED without installing the cutoff. Failure retains the diagnosis, dirty text and obligation. |

There are **22 reachable lifecycle states** (P, the eleven tag rows above each occur while open, and logical close can arrive in every row before the next adoption). The CLOSE_PENDING RELOADING case holds an invalidated install until the job retires. Each tag's entry above gives its open witness; for the two recovery variants inject an uncertain prepare sync on an unclassified/clean target or an actual pre-rename destination mismatch after durable prepare. No post-destruction object can be queried. The count excludes work progress, data and journal-owned state.

File hints coalesce by epoch during any operation; classify them against actual effects, including our own rename. A later external replacement after a verified adoption starts a new observation. A published edit advances revision even at an equal ID; while a provisional group owns journal intent, defer root adoption and keep pumping. All out-of-order user actions return BUSY, or CLOSED for new admission during close. A duplicate notification never resubmits an effect. Failed or ambiguous admitted attempts report SAVE_FAILED under their request ID until definitively SAVED or CANCELLED. Eventual success adopts the cutoff bookmark in edit; cancellation or definitive failure drops it. Preserve owned cutoff identity through history eviction, invalidate discarded redo positions and refuse checked-counter exhaustion before publication.

Logical close sets CLOSE_PENDING, settles effects already exposed, suppresses installation into a removed tab and withdraws unsubmitted file publication. The session owner commits closed-kept membership or settles explicit eviction/discard and releases the associated close obligation. Destroy requires either idle tag, CLOSE_PENDING, and no grant, handle, queued result, token, checkpoint, fence, or other unresolved obligation. Otherwise it returns BUSY, preserving ownership and resolution access.

Work slots carry checked generation/handle identities. Failed submit releases job-only pins; capacity return or result adoption drives tick. A full mailbox retries delivery of the immutable completion through a continuation. Keep worker inputs through physical acknowledgment and defer expensive final-root release. Keep view, tab order, binding, independent tokens, post-cutoff suffixes and generation policy in each complete session publication. The peer index covers closed-kept and detached/recovered drafts as well as live owners. Rebinding an idle displaced peer preserves history and independent dirty identity; only a proved unchanged binding retains clean association. A23 allows at most one live owner per file identity (G, session index rule).

## Exclusive writer grant and crash cuts

**Admission and the crash contract.** A key that needs writer admission stays at the head of the session FIFO during a save grant. It changes neither RAM text nor the journal. The user sees the last published frame and pending/save status. The loop continues dispatching completions. On return, edit admits the head against current generation policy, reserves before undo commit, and makes the changed frame and success reply eligible after the complete named-journal append. This also applies to a completed undo group. During the temp write and sync, ordinary keys can reach submit. K reads the kernel-visible journal name and requires every acknowledged command. M may choose either binding before its parent barrier and must retain every previously guaranteed durable command. [Edit through submit](wave1-edit-spec.md#one-keystroke-through-submit) · [K and M traces](wave1-L2-q2-protocol.md)

**(i) Finish and rotation by tail carry.** Let B be a complete pinned session boundary, and let T be the number of encoded bytes needed to translate all records since B, including complete groups, metadata, image-local positions, pending tokens and generation declarations. The proposed final grant costs **T bytes, one journal rename and zero syncs** (P, it writes that suffix and switches the name). A paste or T4 undo after B makes T depend on content. K works on successful I/O *if* the old fd remains the only append destination until the new image contains the entire acknowledged prefix and writer routing changes atomically with rename. K before rename reads old; K afterwards reads new, including each subsequent acknowledged append. A name switch during an unfinished command or a late write to the old fd breaks that proof.

M refutes the unconditional claim. Build and sync the private image at B. Append an undo with literal X on old, ACK it, and complete an old-journal durability barrier through that undo. Carry the undo into new in page cache and rename. Lose power before new's tail sync. M can retain the new name with only its prefix through B. The old barrier had guaranteed X. Even an unbounded exclusive copy cannot transfer an old inode's sync to the new inode. Conversely, an edit after B which has *only* a page-cache ACK has ordinary M exposure. Holding SAVED until directory sync cannot retract another operation's earlier durability promise.

**Corrected switch, also used for (ii).** The worker writes a private complete CURRENT from B, including both replay branches of unresolved saves, then repeatedly translates complete published suffixes outside the grant. Pin the old image and descriptors/owners needed to read those suffixes. For a finish, incorporate the verified fd-based BASE, saver cutoff ID and reconciled peer targets. For a pending peer checkpoint, keep the old transaction/binding semantics and add the by-value group instead. Replay-equivalence at the same logical boundary includes metadata, membership, other transactions and history-independent saved identities. Account for complete commands whose physical positions move; copying raw sequence numbers is incorrect. An invalidated binding, membership decision or source check discards the candidate.

Seal the private prefix at a wire PAD boundary and sync it **before** attempting a switch. Record its covered edit/metadata watermark and image generation. The journal arbiter serializes image switches and durability completions. Acquire at a complete boundary with no active append/reservation and no outstanding old-inode durability operation. Compare the candidate's covered watermark, source image, dependency/policy epochs and current published root with the writer's current values. This is an indexed version check, including every published change, even undo back to the same ID. On any miss release immediately, catch up and sync privately again. On an exact match, perform the journal rename and switch fd, sequence allocator and boundary mapping as one writer handoff before admitting another command. A failed or uncertain rename keeps admission suspended until the named inode is identified. Defer further image switches and syncs of the newly active journal until its parent barrier succeeds. Keys may append on the identified new fd while the parent barrier is pending. Perform the parent sync off grant, recheck target evidence and only then acknowledge finish or allow use of a new peer checkpoint to displace G. Directory failure retains both branch descriptions and a retry obligation. A writeback-loss repair builds a fresh inode from validated live state; ordinary ACKs remain suspended while continuity is uncertain.

At the corrected switch **zero journal data bytes, zero sync calls, one rename** (P, the candidate is already synced through the matched boundary; the only filesystem mutation is the name switch). A failed comparison has zero of each (P, it only releases the grant). K sees all ACKs on the unique active name before and after the switch. For M, either possible image covers every old durability promise; only later unsynced edits have ordinary exposure. Any new durability promise, including a synced PREPARED relied on by a target rename, waits for the new name's successful parent sync. Private work can be file- or session-sized and can repeat. Continuous changes can defeat an optimistic match indefinitely. This construction establishes safety and progress at quiescence; it gives no finite completion or key-latency bound under continuous writes. The frozen lower contracts do not supply this switch.

**(ii) Peer checkpoints before the grant.** Capture each dependent peer at a published boundary b, including closed-kept and recovered drafts. After temp sync, write its contiguous checkpoint group into that *private* image. Cover exactly the peer's bytes at b, translate the complete suffix after b and retain its independent metadata. The capture-time VALUE_ONLY embargo ensures later restores of G carry literals. Worker bytes per group are L + 40k + 132 + D (P, L literal bytes in k INSERT slices with header and offset, plus BEGIN, descriptor and COMMIT, and optional encoded TARGET of size D; journal.h, CHECKPOINT_BY_VALUE). Sync the groups before PREPARED. Use the corrected switch and secure its journal parent before relying on them. Take or validate the session-wide membership fence at a complete boundary. If another G dependency appeared, release and cover it first. Hold that membership reservation through target publication, allowing ordinary edits under the embargo. Identity checks and checkpoint coverage must still hold when authorizing rename.

If no peer needs protection, preparation skips the peer-image build. Otherwise the construction can write a complete session image even for a single peer. At a private partial group, old still supplies the original T2/T3 peer and G stays named. At peer-image rename K sees the complete peer plus suffix. M may still see old; G remains named in either case. Once the peer image is durably named, PREPARED can be appended; until PREPARED is also durable the target remains untouched. At target rename M can select G or the synced output, and **both** recover the peer. Closed membership replays independently. Thus the pre-grant construction survives K and M with the private-image switch and the displacement fence. A pinned snapshot alone supplies neither. Passing the named writer token per record fails: between CHECKPOINT_BY_VALUE and its COMMIT an unrelated acknowledged INSERT makes the group invalid. K loses that key; M can lose it even after a purported sync of the malformed group. Holding admission across the group instead writes L-dependent bytes. The record maximum bounds only a single member. [Contiguous group rule](../../src/journal/journal.h) · [Peer cuts](wave1-L2-q2-protocol.md)

| Operation | Maximum journal data bytes and sync calls while this operation holds an exclusive writer grant, per attempt |
|---|---|
| Capture/embargo and peer membership fence | 0 B, 0 syncs (P, reserve and compare indexed policy/coverage at a published boundary). If G needs durable VALUE_ONLY redeclaration, include it in the private candidate or the prepare batch counted below. |
| Temp write/sync, target publish, target-directory retry, fd/path verification, reload and ordinary identity checks | 0 B, 0 syncs under grant (P, these file operations run without it). A proven target-directory failure retries that directory barrier alone. |
| Peer/foreign-change checkpoint, Keep, finish, rotation and fresh CURRENT cancel/recovery | 0 B, 0 syncs at the image switch (P, exact-match switch above). For Keep, cancel or detected conflict, include the validated literal checkpoint and appropriate TARGET in the candidate as needed. Reconcile the target namespace for cancel outside the grant. The worker's image/data and directory barriers run outside it. |
| PREPARED append after peer coverage | At most **12,589 B, 0 syncs** (P, at most one VALUE_ONLY declaration of 32 + 72 + 4,095 B, one PREPARED of 32 + 136 + 4,095 B and one canonical PAD of at most 4,096 + 32 − 1 B; sum of wire maxima, a conservative ceiling). Sizes depend only on fixed fields, bounded paths and alignment. Reserve exact offsets; return after page-cache append of the records and sealing PAD at a complete boundary. Sync the pinned named inode off grant through that offset. Later writes begin past the seal. A partial write or uncertain sync suspends publication; never authorize target rename until that marker and its image name are durable. |
| Close and destroy | 0 B, 0 syncs of their own (P, logical close and release); settling an admitted operation uses its row above. |

Counts cover journal file data, including framing; rename counts separately as a namespace operation. No count bounds syscall duration. Image-building, off-grant sync and matching the current writer are journal runtime prerequisites. The shared pool, single UI dispatcher, immutable completions, no nested drains and ownership through physical acknowledgment in Q7 still apply.

## Seam table

A quoted source line supplies the stated part of the contract. GAP means none of the file brief, journal-save brief or landed journal header provides the required handoff. A sequenced operation in a brief is evidence for the ordering; its return type, custody and retry guarantees need their own evidence.

| Required file or journal operation | Source line or GAP |
|---|---|
| Open a registered file/tree; refuse failed mapping registration before publication. | [L2-1-file.md L13](../briefs/L2-1-file.md) <code>- file_open_tree(file*, piece_reserve*, piece_tree**): piece_open_copy below the copy threshold, piece_open_mapped at or above it, the mapping registered with the fault service BEFORE publication; on registration failure (OS_E_NOSPC) the unpublished map and fd are released and the open is refused visibly (Q12). The copy threshold constant states its derivation and that OS_FAULT_SLOTS is derived from it.</code> |
| Check, classify and Keep an observed binding; validate live backing for undo. | [L2-1-file.md L14](../briefs/L2-1-file.md) <code>- Change detection: v1's map_change / check / changed / resolve_keep, with the fault poll as "os_fault_poll != OK or epoch != 0", identity + timestamps for a same-size in-place overwrite (no SIGBUS), and the owned watch object from L2-0a (no global).</code><br>[L2-1-file.md L15](../briefs/L2-1-file.md) <code>- The live backing-identity validator for undo (undo_validate_fn), Law-2 clean, non-blocking.</code> |
| Acquire full tagged target expectations, retain/release immutable bindings and distinguish missing path from failed observation on every check. | **GAP** for the owned handle and complete comparison/error contract. |
| Write and sync the caller's snapshot, interpose PREPARED, rename conditionally, sync parent, verify against fd and classify post-rename mismatch. | [L2-1-file.md L16](../briefs/L2-1-file.md) <code>- Save, file half: file_save_begin takes a caller-owned piece_snapshot (the cutoff is the caller's); write the temp, compute the new-file witness while writing (volume, inode, size, CRC32C over all of S), fsync; the journal's PREPARED is written by the caller between temp sync and rename (L2-3); rename with OS_RENAME_NOREPLACE when the expected destination is absent (OS_E_EXIST -&gt; FILE_ERR_CHANGED; OS_E_NOTSUP needs a stated fallback in your decision doc); dir sync; verify the path against the fd-captured identity unconditionally (Bug 2 variant (a)); an unverifiable post-rename identity is CONFLICT_CANCEL, never FAILED with a stale baseline (§1.5(g)). Metadata copy via os_file_copy_metadata. NO retention, clone or link (A23 rule 1).</code> |
| Resume a synced temp across asynchronous jobs with fd, temp name, metadata and witness intact. | **GAP** for transferable custody, resumption and cleanup after refusal/close. |
| Return classified file publication evidence: definitely before rename, rename uncertain, installed but directory barrier failed, verified, or post-rename conflict. Retry a directory barrier independently using the original handle. | **GAP** for the complete result and barrier-only retry. The file-half line supplies the post-rename conflict case. |
| Acquire a fresh registered reload candidate; retain backing until nonblocking close and physical retirement. | [L2-1-file.md L17](../briefs/L2-1-file.md) <code>- Reload acquisition moves into file (it owns mappings), through the same registered construction (§1.5(h)).</code><br>[L2-1-file.md L18](../briefs/L2-1-file.md) <code>- Close: a non-blocking two-phase close (begin, then work_handle_finished per handle); zero UI-thread sleeps or spins (§1.5(b)); worker publish backpressure with a stated bound or work_continue (§1.5(c)).</code><br>[L2-1-file.md L24](../briefs/L2-1-file.md) <code>- The fault spec's file row and integration tests: block a running job, cancel, release; the backing survives to physical acknowledgement and is retired exactly once; the release child sees zero unmaps/frees of that backing; Law 2 release guard active with zero allocations over snapshot take/retain, validator, foreground queries.</code> |
| Capture S/s, install VALUE_ONLY for G, enumerate and sync peer checkpoints, then append and sync PREPARED. | [L2-3-journal-save.md L12](../briefs/L2-3-journal-save.md) <code>- Prepare, in order: capture the snapshot S and its boundary s (and from this moment journal any restore from the generation being replaced by value: set VALUE_ONLY at capture, rule 3); write and sync the temp (the file layer's work, L2-1; you consume its fd witness); for every OTHER journal-recorded buffer state whose BASE is the generation being replaced — live or closed-but-kept, including recovered drafts of the same target (rule 6) — write and sync a CHECKPOINT_BY_VALUE (rule 4; worst case file-sized, only when such a state exists); then append and sync SAVE(PREPARED) with the temp's witness and s; only then may the caller rename.</code> |
| Fence G across all session membership changes through publication, including reopen, import, eviction and Save As. | **GAP**. Enumerating the peers at prepare does not reserve the later rename decision. |
| Pin or translate an image-qualified root, its s, replay branches, tokens and covered suffix; deliver current root and checked epoch for adoption. | **GAP**. The wire specifies image-local sequences but supplies no root lifetime or translation operation. |
| Use checkpoint_by_value for peer preparation and detected changes, with a contiguous descriptor/literal group and optional final TARGET. | [L2-3-journal-save.md L15](../briefs/L2-3-journal-save.md) <code>- External replacement detected by the file owner: the same CHECKPOINT_BY_VALUE mechanism (rule 5), one code path with two triggers.</code><br>[journal.h L421–422, L428–435](../../src/journal/journal.h) <code> * GROUP_BEGIN .. GROUP_COMMIT bound one contiguous multi-record command</code><br><code> * or checkpoint. Groups never nest; BEGIN inside a group, COMMIT</code><br><code> * group has exactly the descriptor as first member, then INSERT</code><br><code> * members only -- all for the descriptor's buffer, destinations</code><br><code> * strictly cumulative from 0 (each INSERT starts where the previous</code><br><code> * ended), the literal lengths summing -- with checked arithmetic,</code><br><code> * never wrapping -- to exactly the descriptor's text length; an empty</code><br><code> * text has no INSERT members -- and optionally one TARGET member for</code><br><code> * the same buffer, last, restating its binding atomically with the</code><br><code> * text (section 6). Any other shape makes the checkpoint group</code> |
| Finish only after the target-directory barrier and verification; cancel only after namespace and current representation are secure. | [L2-3-journal-save.md L13](../briefs/L2-3-journal-save.md) <code>- Finish: after the target directory sync and the fd/path verification, publish SAVE(FINISHED) declaring the installed generation (a durably published finish image or a complete CURRENT image per the wire section).</code><br>[L2-3-journal-save.md L14](../briefs/L2-3-journal-save.md) <code>- Cancel: after reconciling the namespace and securing an independently usable current representation, SAVE(CANCELLED) (or the token dropped in a complete CURRENT image with the same state); cancellation never marks the buffer saved.</code> |
| Report definitely absent, possibly visible, or durable journal publication, identify the named image after uncertainty, and retain fd/token/evidence for fresh-inode repair. | **GAP** for classified prepare/finish/cancel results. The wire's record discriminator alone cannot report an I/O outcome. |
| Query/acquire/return the quiescent writer grant; publish a bounded sealed append, pin its fd and await an off-grant barrier through the given watermark. | **GAP** for runtime admission, write routing and barrier completion. |
| Build private CURRENT with by-value groups and translated tails, compare-and-switch writer/name at a synced exact boundary, and gate dependent durability on the parent barrier. | **GAP** for the E2 private-image handoff. |

## Flat oracle and discriminating traces

Use [the piece model](../../tests/piece_model.h) as the pattern: the reference and implementation accept the same public calls. The reference stores byte arrays for every live/closed draft, linear command histories with stable content IDs and bookmarks, a map of names to inode objects, and separate kernel-visible and durable journal sequences and directory bindings. Keep volatile held fds distinct from named restart sources. Store checkpoints as full byte arrays plus covered command boundary; retain a set of embargoed generation keys. A list of pending work, immutable completions and physically released holds replaces threads. Compute expected dirty, candidate exposure, acceptable resolutions and replay from these facts. Never call the controller's classifier, encoder, transition function or replay code for the expected answer.

Drive each row above through public operations and independent file/journal observations. At each step compare the phase and closing tag, the derived external observation and actual journal custody, request outcome/ID, diagnostics, dirty and token/candidate visibility, actual bytes, binding ownership, issue order, records and release counts. Attempt forbidden requests in each reachable state and compare before/after. Vary message delay/duplication, unrelated mailbox traffic and slot reuse, close before and after physical effects, source invalidation, failed submit, prepare, both directory barriers and fresh-image publication. Check zero allocations on the typing path (G, Law 2), continued mailbox progress while an input head waits and balanced holds. Exercise reload revocation after undo to the original ID, host adoption of the returned tree/source, restored-mtime rewrite and failed Keep. [v1 controller](</home/tobias/Projects/sublimite-v1-oracle/src/savectl/savectl.c>) · [controller fixtures](</home/tobias/Projects/sublimite-v1-oracle/tests/savectl_test.c>) · [reload identity fixture](</home/tobias/Projects/sublimite-v1-oracle/tests/savectl_reload_test.c>)

At every temp hook, checkpoint prefix/commit/sync, PREPARED partial append/complete append/sync, target rename/dirsync/verification, CURRENT build/sync/rename/dirsync and completion before/after delivery, drop all process-only state. For K replay the visible complete prefix; for M enumerate surviving complete prefixes through at least the last successful barrier, and old/new directory bindings across each unsynced rename. Ignore partial groups. Select old replay on expected old binding, S plus the saver's complete post-s commands on matching new witness, newer usable by-value checkpoint plus suffix independently. Compare every buffer's text, target, membership and view. At startup the journal recovery owner performs this branch selection before normal save admission. Unresolved evidence stays available for salvage and cannot start a new transaction on the affected binding. [Durability oracle](wave1-L2-q2-protocol.md)

| Trace | Independent oracle |
|---|---|
| T1, ordinary save | Prior or new binding gives identical current text for the surviving prefix. Only verified durable finish installs cutoff as saved in the live service. Later typing stays dirty; undo back to reachable cutoff clears dirty. |
| T2, Save As from H over peer on G | Stop partway through peer checkpoint: G still named. At rename peer can already recover its literal text and suffix. New branch alone adopts destination for saver. Peer keeps its draft, independent identity and dirty state. |
| T3, same peer closed-kept | Repeat with no live tree, then reopen from durable TABS. Membership, bytes and view survive at every cut. Unavailable dependency owner defers rename. |
| T4, undo during temp write | Delete X, capture Y, undo before temp sync. Journal restored X by value. Once whole undo accepted, K returns YX on either binding; finish saves Y and leaves YX dirty. Incomplete group yields prior Y. |
| T5 / Bug 2 | Save edited `old!`; foreign rename to `foreign` at RENAMED. Verification → CONFLICT_CANCEL, saved_base NULL, finish unreachable, dirty text and any post-cutoff suffix retained; foreign bytes untouched. Repeat on directory retry and after initial verification but before finish adoption. Resolve can only cancel. |

**The T5 restart interval matters.** For arbitrary large missing X, PREPARED contains a witness and boundary, not X. If the foreign binding survives and neither old nor new source has a name, a kill before the conflict checkpoint can lose the unsaved difference. Report unresolved BASE, preserve the journal and offer surviving literals; apply nothing to the foreign file. A complete conflict group in the kernel-visible **named** image suffices for K. With the private-image construction, the group is synced before journal rename; K can use it at that rename, M is guaranteed it after the journal-parent barrier, even with the token outstanding. A group still reachable only via a worker fd does not secure either restart. If M instead preserves our binding, the new branch still works. A buffer at or below the self-contained size recovers exactly from its durable whole-text checkpoint and independently replayable suffix. First edits before that off-path checkpoint becomes durable still have the initial BASE-relative exposure. The size and its append-rate measurement belong to [the journal runtime brief](../briefs/L2-2b-journal-runtime.md).

**Rejected alternative.** In v1 the worker detects the wrong inode, but `savectl_tick` sets both `needs_finish` and `external`, preserving a BASE captured from the foreign path. Finish rechecks that same foreign BASE and marks `old!` clean. The candidate accessor and finish admission above both derive from FINISHING; the mismatch transition removes both capabilities. One generic failed-token phase would introduce the opposite error: a verified output followed by journal writeback loss can recover-finish, whereas a foreign output permits cancel only. [Bug 2 source trace](wave0-known-bugs.md#bug-2--post-rename-save-conflict)

For E2, force an old-inode sync after B, a private candidate one command behind, a concurrent complete undo, and a newly imported G-dependent closed draft. Require deferral on stale coverage. Cut before/after the writer-name switch, at subsequent UI ACK, and on journal-parent failure and retry. Compare both possible M bindings with the last promised durable boundary. During a grant inject a key and require the old text, unchanged history/sequence and pending status; after return require one complete command and matching frame/ACK. Run the integration oracle with real named journals in a directory under `build/`.

**Signature boundary.** A27 is a draft. The GAP contracts in the seam table require owner decisions before savectl implementation. The journal owner's measured self-contained size, runtime progress under sustained appends, and the edit replay/history decisions are outside this document's implementation scope.
