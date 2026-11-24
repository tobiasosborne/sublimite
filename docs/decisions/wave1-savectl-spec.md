# savectl — proposed header for advisor signature

The save commits the bytes of a particular **published edit boundary**. Its success installs that boundary's content ID, even if the user has since typed or undone. The evidence for success is the synced output inode, the target-directory barrier, verification of the binding to that inode, and, with hot exit enabled, durable publication of the current session. Save admission is a separate observable.

The [plan, edit and save sections](../PLAN_V2.md), [A22, A23 and A26](wave0-plan-amendments.md), [Q7, Q9, Q10 and Q14](wave1-L2-derivations.md), [crash traces](wave1-L2-q2-protocol.md) and [landed wire](../../src/journal/journal.h) govern this proposal. A23 supersedes the retention and shared-path language in the plan and [harvest](wave1-L2-L5-harvest.md). Digits in source identifiers, citations, C type names and trace names identify contracts. (G) denotes a specified gate or design choice, (P) a consequence of the construction. Complexity bounds below are (P), derived from the operations indicated. No threshold or timing measurement is proposed here.

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
/* Opaque lower-layer contracts listed below. file_target owns a canonical
 * path and tagged full expectation. journal_save_view pins the published
 * session root with image-qualified boundaries and generation membership. */
struct file;
struct file_target;
struct journal;
struct journal_save_view;

typedef enum savectl_save_phase {
    SAVECTL_IDLE, SAVECTL_PREPARING, SAVECTL_WRITING, SAVECTL_VERIFYING_FD,
    SAVECTL_FINISHING, SAVECTL_DIRSYNC_RETRY, SAVECTL_CONFLICT_CANCEL,
    SAVECTL_RECOVERY_PENDING
} savectl_save_phase;
typedef enum savectl_external_state {
    SAVECTL_CLEAN, SAVECTL_CHANGED_ON_DISK, SAVECTL_RELOADING
} savectl_external_state;
typedef enum savectl_journal_lease {
    SAVECTL_UI_OWNED, SAVECTL_SAVE_OWNED
} savectl_journal_lease;
typedef enum savectl_close_state {
    SAVECTL_OPEN, SAVECTL_CLOSE_PENDING, SAVECTL_RETIRED
} savectl_close_state;

typedef enum savectl_err {
    SAVECTL_OK, SAVECTL_BUSY, SAVECTL_CLOSED, SAVECTL_CHANGED,
    SAVECTL_SOURCE, SAVECTL_NOMEM, SAVECTL_LIMIT, SAVECTL_UNSUPPORTED,
    SAVECTL_FILE_IO, SAVECTL_JOURNAL_IO
} savectl_err;
/* BUSY: admission guard, unready root/transport, or physical work pending.
 * CLOSED: logical close refuses a new user operation. CHANGED: target
 * conflict or superseded reload. SOURCE: live/recovery source unverifiable.
 * NOMEM: storage refused. LIMIT: bounded handle, id, path or mapping registry
 * exhausted. UNSUPPORTED: required file/format operation unavailable.
 * FILE_IO/JOURNAL_IO: named service failed, including a sync or stat.
 * Preserve the failing lower-layer code and its captured OS error in model.
 * Journal continuity loss suspends edit success until repaired. */
typedef enum savectl_outcome {
    SAVECTL_NO_SAVE, SAVECTL_ADMITTED, SAVECTL_SAVED, SAVECTL_CANCELLED,
    SAVECTL_SAVE_FAILED
} savectl_outcome;
typedef enum savectl_resolution {
    SAVECTL_RETRY_DIRECTORY, SAVECTL_FINISH, SAVECTL_RECOVER_FINISH,
    SAVECTL_RECOVER_CANCEL
} savectl_resolution;
typedef enum savectl_knowledge {
    SAVECTL_UNKNOWN, SAVECTL_KNOWN
} savectl_knowledge;
typedef struct savectl_saved_id {
    savectl_knowledge knowledge;
    uint64_t id;
} savectl_saved_id;
typedef struct savectl_identity {
    journal_presence presence;
    os_stat stat;                  /* meaningful only for PRESENT */
} savectl_identity;
typedef struct savectl_model {
    savectl_save_phase phase;
    savectl_external_state external;
    savectl_journal_lease lease;
    savectl_close_state close;
    uint64_t current_id;
    savectl_saved_id saved;
    bool dirty;                    /* saved unknown || current_id != saved.id */
    uint64_t request_id, request_ns;
    savectl_outcome outcome;       /* latest admitted save */
    savectl_err error;             /* most recent operation failure */
    int file_error, journal_error, native_error;
} savectl_model;
typedef struct savectl_options {
    work_pool *pool;               /* shared, UI dispatches its mailbox */
    piece_reserve *reserve;        /* same UI reserve for reload */
    struct journal *log;           /* NULL for standalone file mode */
    struct file *source;           /* NULL for private/untitled text */
    struct file_target *binding;   /* NULL for untitled */
    uint64_t buffer_id, current_id;
    savectl_saved_id saved;
} savectl_options;
typedef struct savectl_request {
    piece_snapshot *snapshot;
    struct file_target *destination; /* Save As need not equal binding */
    struct journal_save_view *view;  /* NULL in standalone mode */
    uint64_t content_id, request_ns; /* timestamp at input receipt */
} savectl_request;
typedef struct savectl_view {
    uint64_t cursor, anchor, scroll_byte, scroll_x;
} savectl_view;
typedef struct savectl_reload_result {
    piece_tree *tree;
    struct file *source;
    savectl_view view;
} savectl_reload_result;

/* UI thread throughout. ASSERT on missing required pointers, calls on freed
 * objects, wrong owner thread, or internal lease double-return. Pool, log
 * and reserve must outlive the controller and issued work. All other object
 * arguments are borrowed for a call. Accepted operations retain their own
 * pins, including paths underlying wire slices, until final use. A refused
 * request transfers nothing and leaves outputs unchanged. Errors caused by
 * files, capacity, concurrent changes or operation ordering are results.
 *
 * Setup only. Allocate controller and result slot, retain binding/source,
 * start IDLE/CLEAN/UI_OWNED/OPEN with supplied IDs. BUSY while journal
 * recovery still owns this buffer's unresolved transaction, NOMEM, LIMIT,
 * SOURCE if mapped input lacks its validation lease. O(1) fixed storage. */
savectl_err savectl_create(savectl **out, const savectl_options *options);

/* O(1) scalar publication by edit at a committed boundary. Invalidate
 * any reload captured at an earlier revision even when id compares equal.
 * Edit owns the identifying bookmarks. No allocation, blocking or I/O. */
void savectl_content_identity(savectl *s, uint64_t current_id);
/* O(1) value copy or borrowed immutable result, until next controller call.
 * target_identity: accepted binding expectation, NULL when unbound.
 * saved_base: verified finish candidate, only in FINISHING.
 * save_token: PREPARED candidate in UI custody (including uncertain
 * publication), NULL before a candidate exists or during SAVE_OWNED.
 * A borrowed token is evidence, never authority to issue a rename. */
savectl_model savectl_get_model(const savectl *s);
const savectl_identity *savectl_target_identity(const savectl *s);
const journal_generation *savectl_saved_base(const savectl *s);
const journal_save *savectl_save_token(const savectl *s);

/* Pin already-published snapshot, cutoff bookmark, root and destination;
 * seal joining and embargo the displaced generation at this same boundary.
 * The owner supplies the matching edit bookmark with the published root
 * (standalone: holds it in edit while request_id is outstanding).
 * O(1) UI ref operations and indexed policy update, no snapshot build or
 * peer enumeration. OK means admission. BUSY/CLOSED/CHANGED/SOURCE/LIMIT
 * or NOMEM before admission. A stale view/partial group yields BUSY. */
savectl_err savectl_save_request(savectl *s, const savectl_request *request);
/* Request the matching recovery/finish row. O(1) intent, BUSY for wrong
 * phase/in-flight action, CHANGED for finish in CONFLICT_CANCEL. Also
 * available during close. Tick acquires the then-current published root. */
savectl_err savectl_resolve(savectl *s, savectl_resolution action);

/* O(1) invalidate observations and schedule a file check. Duplicate watch
 * or focus events coalesce. Checks may include our own rename. */
void savectl_file_event(savectl *s);
/* IDLE + OPEN. Explicit reload accepts discarding local changes only at
 * the captured revision. Keep requires CHANGED_ON_DISK. O(1) intent;
 * BUSY/CLOSED, SOURCE on missing validator, FILE_IO for unbound target.
 * Async failures appear in model; on failure the original tree survives. */
savectl_err savectl_reload(savectl *s);
savectl_err savectl_keep(savectl *s);
/* UI adoption of ready file/tree from the reload job. Supply a fresh
 * content ID in the new edit domain and the latest view. Clamp cursor,
 * anchor and vertical byte offset to length; carry horizontal scroll.
 * O(1) final transfer. BUSY if unready, CLOSED or CHANGED if invalidated.
 * On OK caller owns returned references and installs tree, fresh history
 * and identity as a single session publication before dispatching input. */
savectl_err savectl_take_reload(savectl *s, uint64_t new_id,
                                savectl_view latest, savectl_reload_result *out);
/* Apply the session owner's reconciled binding for an idle peer. NULL
 * detaches its live identity, retaining former TARGET in its session root.
 * Preserves text/history. Never awards another saver's clean ID.
 * O(1) ref swap. BUSY/CLOSED/CHANGED for a conflicting owner/resolution. */
savectl_err savectl_rebase_target(savectl *s, struct file_target *binding);

/* Dispatch a routed message, true if ours (including stale notifications).
 * Tick advances a ready intent or adopts a delivered result. Pass the latest
 * complete published session view, NULL when unavailable or standalone.
 * A missing required view defers progress. Both O(1) controller work per
 * call, no waits, nested drains, file access or unbounded release on UI.
 * File acquisition/build maintenance is scheduled separately from input.
 * Caller pumps the shared pool for all clients and ticks on completions,
 * returned capacity, root publication and recovery requests. */
bool savectl_receive(savectl *s, const work_msg *msg);
void savectl_tick(savectl *s, struct journal_save_view *current);

/* O(1) logical close invalidates new requests/reload installs. Settle
 * previously exposed effects and keep dispatching. Retire only at a
 * session-owner close boundary with kept text/evidence secured or user
 * discard explicitly settled. BUSY while a lease/result/obligation lives.
 * Destroy consumes only RETIRED (else BUSY), frees fixed control storage.
 * Close and retirement never unlink ambiguous evidence. */
void savectl_close_begin(savectl *s);
savectl_err savectl_retire(savectl *s);
savectl_err savectl_destroy(savectl *s);
#endif
```

**Ownership at the call boundary.** The session supplies its immutable published view as an opaque lower-layer input. It includes all buffer IDs, snapshots or replayable kept states, bookmarks, views, targets, tab order, other transactions and the identity/dependency index. A lease pins the view and all backing owners. The owner serializes membership changes affecting a displaced generation through rename, including reopen, import, Save As and eviction. Unrelated buffers can continue editing. Reserve any claims needed for admission before publishing effects. After admission, pool pressure retains intent; failed submit releases job-only pins. Hold worker arguments until physical completion, including after cancellation. Defer expensive last-root cleanup. The only saved/cutoff bookmarks edit must maintain are those still owned; discard redo-branch reachability, retain an owned cutoff ID across history eviction, restore equal IDs at reachable completed undo boundaries, refuse ID wrap. The host transfers the cutoff bookmark to saved on success or drops its cutoff claim on cancellation/failure. [Q7 and Q10](wave1-L2-derivations.md)

## States and transitions

Every multi-mode tag, including the operation in a work slot, has an exhaustive switch under `-Wswitch-enum`. One outstanding controller job (G, serialize effects on this transaction) carries an operation tag, immutable inputs, a checked handle/generation and a sealed result. Delivery state distinguishes pending admission, executing, and delivered awaiting physical retirement. These are transport facts. The four axes below describe the buffer. UI_OWNED means this controller has no exclusive grant. The session arbiter must also exclude another controller's grant before edit/append. Standalone mode never acquires the journal lease.

| Phase | Possible (external, lease) before retirement | Owned evidence |
|---|---|---|
| IDLE | CLEAN/UI, CHANGED_ON_DISK/UI or SAVE, RELOADING/UI | Binding, current/saved IDs. SAVE only while securing a detected change or Keep. Reload holds its own revision, event epoch and candidate. |
| PREPARING | CLEAN/UI or SAVE | Immutable cutoff and embargo. UI during temp write/sync and admission wait. SAVE for peer and PREPARED barriers only. |
| WRITING | CLEAN/UI | Prepared handoff; file-only **publication** of the synced temp (standalone skips the marker). |
| VERIFYING_FD | CLEAN/UI | Output fd, witness and completed target-directory barrier. |
| DIRSYNC_RETRY | CLEAN/UI | Known rename, held output fd, missing directory barrier. |
| FINISHING | CLEAN/UI or SAVE | Verified fd-derived candidate, cutoff, unresolved journal publication. |
| RECOVERY_PENDING | CLEAN or CHANGED_ON_DISK, each UI or SAVE | Possible or confirmed token without verified replacement. Only cancellation may resolve it. |
| CONFLICT_CANCEL | CHANGED_ON_DISK/UI or SAVE | Observed post-rename mismatch/unverifiable binding. Cutoff, dirty live text and evidence survive. No finish candidate. |

The product has **144 tuples** (P, eight phases from PLAN plus Q9 taken together × three external × two lease × three close values). **35 are reachable** in this proposal (P, the rows admit respectively 4 + 2 + 1 + 1 + 1 + 2 + 4 + 2 = 17 pairs; each can be OPEN or become CLOSE_PENDING by logical close; RETIRED has only IDLE/CLEAN/UI). This counts axis tuples across both modes and all histories, excluding internal progress and data. The table is exhaustive: reload owns an idle controller, file-only jobs use UI custody, a known foreign result immediately revokes finish, recovery never shares a fresh save, and retirement releases all resources. CLEAN here means no classified external conflict; a queued watch hint has no identity yet. Canonical CLEAN on retirement has no claim about a pathname.

**Reading the transition tables.** Unmentioned axes and existing evidence persist. UI adopts worker results in the matching operation and generation. “Lease” means quiesce transport and grant exclusive use at a complete edit boundary; “return” reopens UI journal admission. All other commands that require a different source cell return BUSY, new user requests during close return CLOSED, finish after post-rename conflict returns CHANGED. These refusals, repeated ticks without progress, and duplicate/stale notifications write nothing. A late *physical effect* is always reconciled from its owning completion even after logical close. No second submission may repeat a rename or marker merely because delivery was delayed.

| Event and guard | Next phase / other axis change | Effect and record | Invariant preserved |
|---|---|---|---|
| Save accepted at IDLE/CLEAN/UI/OPEN, no outstanding check or unresolved source | PREPARING/UI | Pin S, image-local s, ID, destination and displaced G; set generation-wide VALUE_ONLY before another command can stage a restore. Redeclare G with the flag if needed. | S includes exactly the saver through s. Previous source H and displaced G may differ. |
| Temp create, write, sync under PREPARING/UI | same | File writes S and its metadata, accumulates whole-stream witness. No SAVE yet. | Destination is unchanged; typing and undo can append after s. |
| Temp synced; log present; current membership and quiescent lease available | PREPARING/SAVE | Checkpoint each other G-dependent recorded state by value; sync through its commit. Append and sync SAVE(PREPARED). | No controlled rename before every dependency is covered and the marker durable. No new G dependency may slip in. |
| Durable prepare adopted, or standalone temp synced with no journal dependencies | WRITING/UI | Return lease before admitting file-only publication. Preserve token across future CURRENTs. Standalone emits no record. | UI may append immediately, even if no file-job slot is available. |
| Failure before rename, no token possible | IDLE/UI, CHANGED_ON_DISK on detected change | Return lease; release temp/intent when safe; start conflict checkpoint on foreign replacement. No save record required. | Prior target and saved ID survive; embargo already established persists. |
| Ambiguous PREPARED publication, or prepared then known pre-rename validation, rename or close cancellation failure/refusal | RECOVERY_PENDING/UI; CHANGED_ON_DISK on mismatch | Return lease; retain token candidate, cutoff and sources. No second PREPARED. | Uncertain journal state bars a fresh save; no Saved result. |
| WRITING: recheck held source, destination and peer fence; atomic rename succeeds | WRITING/UI | Record rename in file result; sync target parent, preserving observer fd. No journal write. | After rename process recovery can select new witness. Rename alone cannot authorize finish. |
| Proven rename, target dirsync failed; no known foreign mismatch | DIRSYNC_RETRY/UI | Keep fd, token and error. RETRY_DIRECTORY attempts the directory barrier only; repeated failure stays here. | Machine loss may still yield the previous binding. |
| Directory barrier succeeds, initially or on retry | VERIFYING_FD/UI | Capture generation identity/prefix from the held **post-rename fd**; verify target and saved witness. No journal record. | Pre-rename ctime and reopened pathname cannot supply the installed BASE. |
| Post-rename binding foreign, missing or unverifiable, in WRITING, DIRSYNC_RETRY, VERIFYING_FD or FINISHING | CONFLICT_CANCEL/CHANGED_ON_DISK/UI | Return lease, revoke candidate, set saved ID unknown, queue current conflict checkpoint. Preserve token, suffix and error. | Cancel only, even without a journal. A known mismatch takes precedence over a simultaneous barrier error. |
| VERIFYING_FD: equal and directory barrier proven | FINISHING/UI with journal; IDLE/CLEAN/UI standalone | Expose fd-derived candidate only in FINISHING. Standalone adopts binding and cutoff ID here. No record. | Standalone and journal paths perform the same verification. |
| FINISH or RECOVER_FINISH in FINISHING/UI | FINISHING/SAVE | Freeze complete CURRENT under lease; reverify fd/path before publication; declare installed generation, bind TARGET and encode SAVE(FINISHED) in new image. Sync image, rename journal, sync its parent. | Every accepted post-s edit and independent peer survives either journal binding. |
| Finish failed or publication ambiguous, replacement still verified | FINISHING/UI | Return lease, retain candidate/token; report error. After writeback loss only RECOVER_FINISH with fresh-inode CURRENT is admissible. Recheck binding before adoption. | Directory retry cannot repair lost journal bytes; no new success ACK on uncertain continuity. |
| Durable finish adopted and verification current | IDLE/CLEAN/UI | Return lease. Install destination and cutoff content ID; resolve transaction. Publish SAVED for this request. | Only the saver acquires that saved ID; current equals it iff clean. |
| RECOVER_CANCEL in RECOVERY_PENDING or CONFLICT_CANCEL | same phase/SAVE with journal, same/UI standalone | Sync target namespace. Secure independently usable CURRENT including post-cutoff edits; on conflict commit/sync literal checkpoint first. Resolve token with SAVE(CANCELLED) or equivalent complete CURRENT, sync and durably publish. No target rewrite. | Namespace, current text, peers and other tokens remain recoverable before release. |
| Cancel fails, including stale mapped bytes during checkpoint | same phase/UI; CHANGED_ON_DISK on observed conflict | Return lease, keep evidence and diagnosis; retry from fresh CURRENT. | No fabricated checkpoint, lost evidence or saved acknowledgment. |
| Cancel succeeds | IDLE/UI, keep external classification | Release token/cutoff; preserve prior binding or conflict/detached state. Standalone acknowledges live reconciliation, with no journal durability claim. | Never installs cutoff as saved; foreign replacement stays dirty. |

SAVE_FAILED describes the last admitted attempt's failure, including unresolved states; a successful resolution replaces it with SAVED or CANCELLED. Use request ID to distinguish a prior success. Journal suspension belongs to journal; a returned UI lease alone cannot clear it. An unknown rename outcome must be reconciled as potentially published: if installation and its directory barrier cannot be established and the binding cannot be verified, use the conflict row. A proved unchanged target with a token uses RECOVERY_PENDING. Pre-publication file validation errors carry their real cause; absence, access denial and unknown identity are distinct.

| Event and guard | Next state | Effect and record | Invariant preserved |
|---|---|---|---|
| Published edit or completed replay, UI admitted | axes unchanged; in-flight reload becomes stale | Update current ID/revision. Edit emits literal/range or whole group; every G restore after capture is literal, including undo, redo and checkpoint encoders. | No provisional group in S/CURRENT. No new-text frame/command success before final accepted record. RAM-accepted text survives append failure without reapplication. |
| File/focus hint | axes unchanged | Bump observation epoch; coalesce off-path check. Serialize with file effect to distinguish own rename. No record. | A hint alone cannot confirm replacement or erase a real completion. |
| IDLE check equal or stat error | same | No record. Report stat error; retain expectation/refuse save pending valid check. | An I/O error never manufactures ABSENT. |
| IDLE check confirms foreign replacement | IDLE/CHANGED_ON_DISK/UI, then SAVE during journal checkpoint, then UI | Checkpoint validated current text by value with original/conflicted TARGET; sync. On failure retain prompt and diagnosis. In-place rewrite invalidates dependent work with zero bytes copied at detection (G, A23 rule 7); independently valid bytes can still be secured. | Dirty edit/history preserved. Stale ORIGINAL cannot be published as intact. |
| Explicit reload at IDLE or silent reload of unmodified externally changed text | IDLE/RELOADING/UI | File acquires fresh registered source/tree; stage until take. No SAVE. | Original view, history and backing remain installed during acquisition. |
| Reload fails, edit/watch epoch changed even after ready, or close invalidates install | IDLE/CHANGED_ON_DISK/UI on result/discard | Release only candidate on physical completion; report actual error. No record. | A mid-read rewrite, edit-undo round trip or registry refusal cannot replace the old tree. |
| Take valid reload while OPEN | IDLE/CLEAN/UI | Hand over tree/source and clamped view. Host publishes fresh edit domain and consistent BASE/TARGET/VIEW in session; new current and saved IDs equal. | Future equality cannot alias prior history. |
| Explicit Keep, IDLE/CHANGED_ON_DISK/OPEN | same, SAVE during journal reconciliation, then IDLE/CLEAN/UI on success | Validate live source and expected target, secure current text by value with updated TARGET, sync, recheck on adoption. Set saved unknown. Errors/stale result leave changed prompt, prior expectation and text. | Keep authorizes future Save against the observed target; it never writes that file. |
| Session reconciles idle peer on verified publication | IDLE/CHANGED_ON_DISK/UI for displaced/detached peer; CLEAN only for a proven unchanged binding | Adopt only this peer's reconciled TARGET; preserve checkpoint, suffix, view, history, mark unknown if its prior saved association was lost. | At most one live owner per file identity (G, A23 identity index); independent drafts survive. |
| CLOSE_BEGIN from OPEN, repeated while pending | CLOSE_PENDING, other axes unchanged on entry | Refuse fresh work; suppress reload installation. Withdraw unsubmitted file publication; settle launched operations by the actual before/after-rename rows. Continue required checkpoint/finish/cancel. No close-time implicit Saved. | Worker arguments outlive physical acknowledgment. No ambiguous state is freed. |
| Retire at CLOSE_PENDING/IDLE/UI with all physical handles retired and session close disposition committed | RETIRED/IDLE/CLEAN/UI | Remove live ownership; durable TABS closed-kept or explicit session eviction handled by session owner. Destroy frees control. Otherwise BUSY, retaining reasons and recovery actions. | Closing a kept tab cannot remove a dependency. Standalone dirty text needs explicit caller custody or discard. |

Conflict checkpointing starts on detection, independently of a user's later cancel/keep decision. In either cancel-only phase it may temporarily take SAVE custody just for this checkpoint, then return to the same phase/UI with token unresolved. A subsequent cancel may reuse it with its complete suffix. Missing/changed source never licenses saving zeros. No-journal controllers omit every journal action and may publish only where the session owner establishes that no journal-recorded peer depends on the displaced generation.

**Progress and publication.** Tick starts the next normal prepare/write/verify action. FINISHING/UI awaits FINISH from the session owner; a failed recovery attempt awaits an explicit retry. A full mailbox retries only delivery of the sealed result via a worker continuation. A worker runs leased journal I/O inline and never submits to or drains its pool. At adoption the owner publishes binding, bookmarks and peer-index changes before dispatching another command. Logical close suppresses success notifications to a removed tab; the outcome still belongs to the original incarnation. The verification observation orders the result: a later external rename is a new change. A watch epoch invalidated before adoption requires rechecking off path.

## Calls required of the lower layers

These are semantic calls to check at header review. Only the wire types are landed at the design pin; lower-layer spellings and opaque forward declarations above are proposals.

| Service / call | Guarantee required at the seam |
|---|---|
| file source acquire/validate/release, target observe/compare, file_check | Hold mapped backing through last load and closing fault poll; compare full tagged ABSENT/PRESENT(os_stat), using file's classifier for path and fd. Compare kind, permissions, owner/group, volume, both inode words, size, mtime and ctime. Missing path policy comes from file. Preserve actual OS errors. |
| file_save_begin(snapshot, destination), write/sync temp → held handle + witness | Take caller's immutable cutoff. Copy metadata, stream bytes, compute volume, both inode words, size and CRC32C of **all** S, sync temp. Stop here for PREPARED; keep fd and working temp through handoff. No old-file clone, link, backup or retirement scan. |
| file publish(handle, expectation, membership authorization), sync parent, verify(handle, path) | Separate known pre-rename refusal, installed with directory failure, verified and post-rename unverifiable outcomes. Expected ABSENT uses atomic NOREPLACE, existence is CHANGED, unsupported refuses. Final check then REPLACE for existing targets has a race with an external rename; the OS seam supplies no expected-inode atomic replace. Validate installed bytes against witness and compare fresh fd/path stats; capture BASE/prefix from that fd only. Retrying a barrier/verification never renames again. |
| file reload acquisition, file_open_tree, file_resolve_keep, nonblocking close | Same registered open path and validated candidate through adoption, with old tree intact on failure. Registry exhaustion and descriptor refusal distinguished in diagnostics. Under A26 a piece storage refusal during concurrent release may succeed on a later retry. No denial latch, polling loop or guessed reserve cap. |
| journal capture/pin boundary and mark generation VALUE_ONLY; acquire/return quiescent lease | Atomically pair snapshot S, s, content bookmark and source policy against publication of subsequent commands. Either pin image or translate both replay branches, s, checkpoints and tokens across rotation. Once value-only, always value-only for that key, including across cancel/reopen. Capture can stage the redeclaration; secure it with dependent publication. |
| journal checkpoint_by_value(view, buffer, covered boundary) | Same encoder for peer, external change and small-buffer policy. Complete group: BEGIN, CHECKPOINT_BY_VALUE, contiguous literal INSERT slices from empty, optional final TARGET, COMMIT. Validate source/length, sync before displacement. Preserve later complete edits even when physically before the checkpoint group; preserve independent metadata. Worst-case literal payload equals current byte length (P, slice sum). |
| journal_save_prepare(cut, synced witness, peers) | Include live, closed-kept and detached/recovered drafts depending on **displaced** G. Hold admission fence against new dependencies through rename or defer. Sync each required peer checkpoint and then PREPARED; report definitely absent vs possibly published vs durable token. Wire uses (buffer_id, token), previous_key, fresh new_key, image-local s, cutoff ID, expectation and destination. New key unusable for range restores before declaration. |
| journal_save_finish / recover_finish(current, token, verified generation), journal_save_cancel(current, token, outcome) | Exclusive current root, including post-cutoff edits, other tokens, VALUE_ONLY declarations and independent metadata. FINISHED references same-image installed declaration, s and ID. CANCELLED references its same-image checkpoint commit (or wire's prior-representation case) and outcome/saved flags. On writeback loss use fresh inode, file sync, journal rename and parent sync. Distinguish failed or ambiguous publication; successful retry never duplicates accepted text. Reverify before adoption, even on recovery. If an image already became visible before a newly detected mismatch, reconcile its actual contents in fresh CURRENT without assuming PREPARED is still named. |

## Flat oracle and discriminating traces

Use [the piece model](../../tests/piece_model.h) as the pattern: the reference and implementation accept the same public calls. The reference stores byte arrays for every live/closed draft, linear command histories with stable content IDs and bookmarks, a map of names to inode objects, and separate kernel-visible and durable journal sequences and directory bindings. Keep volatile held fds distinct from named restart sources. Store checkpoints as full byte arrays plus covered command boundary; retain a set of embargoed generation keys. A list of pending work, immutable completions and physically released holds replaces threads. Compute expected dirty, candidate exposure, acceptable resolutions and replay from these facts. Never call the controller's classifier, encoder, transition function or replay code for the expected answer.

Drive each row above through public operations and independent file/journal observations. At each step compare all axes, request outcome/ID, diagnostics, dirty and token/candidate visibility, actual bytes, binding ownership, issue order, records and release counts. Attempt forbidden requests in each tuple and compare before/after. Vary message delay/duplication, unrelated mailbox traffic and slot reuse, close before and after physical effects, source invalidation, failed submit, prepare, both directory barriers and fresh-image publication. Check zero allocations on the typing path (G, Law 2), uninterrupted unrelated UI progress and balanced holds. Exercise reload revocation after undo to the original ID, latest-view clamping, restored-mtime rewrite and failed Keep. [v1 controller](</home/tobias/Projects/sublimite-v1-oracle/src/savectl/savectl.c>) · [controller fixtures](</home/tobias/Projects/sublimite-v1-oracle/tests/savectl_test.c>) · [reload identity fixture](</home/tobias/Projects/sublimite-v1-oracle/tests/savectl_reload_test.c>)

At every temp hook, checkpoint prefix/commit/sync, PREPARED partial append/complete append/sync, target rename/dirsync/verification, CURRENT build/sync/rename/dirsync and completion before/after delivery, drop all process-only state. For K replay the visible complete prefix; for M enumerate surviving complete prefixes through at least the last successful barrier, and old/new directory bindings across each unsynced rename. Ignore partial groups. Select old replay on expected old binding, S plus the saver's complete post-s commands on matching new witness, newer usable by-value checkpoint plus suffix independently. Compare every buffer's text, target, membership and view. At startup the journal recovery owner performs this branch selection before normal save admission. Unresolved evidence stays available for salvage and cannot start a new transaction on the affected binding. [Durability oracle](wave1-L2-q2-protocol.md)

| Trace | Independent oracle |
|---|---|
| T1, ordinary save | Prior or new binding gives identical current text for the surviving prefix. Only verified durable finish installs cutoff as saved in the live service. Later typing stays dirty; undo back to reachable cutoff clears dirty. |
| T2, Save As from H over peer on G | Stop partway through peer checkpoint: G still named. At rename peer can already recover its literal text and suffix. New branch alone adopts destination for saver. Peer keeps its draft, independent identity and dirty state. |
| T3, same peer closed-kept | Repeat with no live tree, then reopen from durable TABS. Membership, bytes and view survive at every cut. Unavailable dependency owner defers rename. |
| T4, undo during temp write | Delete X, capture Y, undo before temp sync. Journal restored X by value. Once whole undo accepted, K returns YX on either binding; finish saves Y and leaves YX dirty. Incomplete group yields prior Y. |
| T5 / Bug 2 | Save edited `old!`; foreign rename to `foreign` at RENAMED. Verification → CONFLICT_CANCEL, saved_base NULL, finish refused, dirty text and any post-cutoff suffix retained; foreign bytes untouched. Repeat with no journal, on directory retry, and after initial verification but before finish adoption. |

**The T5 restart interval matters.** For arbitrary large missing X, PREPARED contains a witness and boundary, not X. If the foreign binding survives and neither old nor new source has a name, a kill before the conflict checkpoint can lose the unsaved difference. Report unresolved BASE, preserve the journal and offer surviving literals; apply nothing to the foreign file. A complete kernel-visible conflict group suffices for K; after its sync it also suffices for M, even with the token outstanding. If M instead preserves our binding, the new branch still works. A buffer at or below the self-contained size recovers exactly from its durable whole-text checkpoint and independently replayable suffix. First edits before that off-path checkpoint becomes durable still have the initial BASE-relative exposure. The size and its append-rate measurement belong to [the journal runtime brief](../briefs/L2-2b-journal-runtime.md).

**Rejected alternative.** In v1 the worker detects the wrong inode, but `savectl_tick` sets both `needs_finish` and `external`, preserving a BASE captured from the foreign path. Finish rechecks that same foreign BASE and marks `old!` clean. The candidate accessor and finish admission above both derive from FINISHING; the mismatch transition removes both capabilities. One generic failed-token phase would introduce the opposite error: a verified output followed by journal writeback loss can recover-finish, whereas a foreign output permits cancel only. [Bug 2 source trace](wave0-known-bugs.md#bug-2--post-rename-save-conflict)

**Advisor signature points.** Approve PREPARING/UI for the long temp write and WRITING/UI for publication after the lease handoff. Confirm the lower services expose a resumable synced temp, a session-wide displacement fence, image-qualified root pins and classified publication outcomes with enough evidence for close/recovery. Those seams, rather than any new wire field, gate implementation. The self-contained size remains the journal owner's measurement.
