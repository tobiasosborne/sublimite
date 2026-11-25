# Wave 1 headless application owner

**L5 proposal for signature.** A parent process can open a file, edit it through the production kernel, save one published boundary while typing past it, observe a foreign replacement, kill at a save step and read the recovered bytes after restart. Wave 1 admits a save only when the saver is the sole possible dependent of the generation it would displace, or there is no dependent. The index visibly refuses a known other dependent before capture. The signed lower-header errata for this slice appear below. [PLAN §§4.7 and 6](../PLAN_V2.md) · [FIFO and external completion, Q15–16](wave1-L2-derivations.md)

## app_loop.h

`app` is the UI-thread owner of the ordered command FIFO, reply stream, wait set, shared work pool, piece reserve, buffer index and session. A *turn* is one call to `app_step`. A *published boundary* is the snapshot, selection, content ID and revision returned by `edit_read`. A command's ACK is the successful reply for its advertised boundary. A save has separate admission and completion replies.

| Declaration | Contract |
|---|---|
| `app_result app_create(app **out, int command_fd, const char *journal_path)` | Main transfers the inherited stream descriptor on entry. Adopt it, create the shared resources and start recovery of the named journal. Unwind failed setup. |
| `app_step_result app_step(app *a)` | Consume ready events and execute one ordered turn. Return MORE while serving or draining, DONE after orderly close and reply drain, or FAILED with the terminal diagnosis. |
| `bool app_work_pending(const app *a)` | True when a cluster can make progress in a turn now. |
| `uint64_t app_next_deadline_ns(const app *a)` | Earliest actual absolute deadline from the same cluster observations, or OS_DEADLINE_NEVER. |
| `void app_destroy(app *a)` | Consume the stopped owner after its leases and normal shutdown obligations have retired. |

The common app declarations define `app_result` as OK (completed or admitted), WAIT (retry unadmitted work upon a specified dependency change), or REFUSED (final diagnosis). A diagnosis identifies the originating service, its exact code and any native error. App-origin errors include OTHER_DEPENDENT, identifying the blocking owner, and an unclassified destination carries its observation or recovery diagnosis. `app_step_result` carries the step disposition and the diagnosis on failure. The FIFO owns each decoded request, receipt time, resolved target or opening admission, and prepaid reply capacity. While an admitted command yields, it retains its correlation and cursor. All cluster calls and publication decisions are serialized on this thread. Exhaustively switch on lifecycle enums. The step guard has **two values**, outside and inside the call, as required by the non-reentry assertion.

1. Consume available stream input within capacity. Give the FIFO head the first dispatch opportunity. OPEN selects a provisional entry and releases dispatch while acquisition runs. A subsequent SWITCH to a ready owner can proceed. A head needing the opening owner's text waits there, retaining its target even on open failure. Refuse that input visibly on failure. Later input cannot overtake it.
2. Drain a bounded ordinary `work_mailbox_drain`. Route by submitting recipient and lease generation, including retirement, before using an object. Route writer messages to `journal_receive`, controller work to `savectl_receive`, acquisition messages to the file owner for exactly one `file_msg_decode`, and count results to their issuing entry. Callbacks deliver results and return. Drain the accepted parent/name watch in bounded slices through `os_watch_read`; matching, overflow and lost-watch events schedule `savectl_file_event`. Re-arm a lost watch. A focus hint requests the same off-input-path check.
3. Service continued edit commands, save/session control records and writer checkpoint records fairly. Whenever dispatch or continuation publishes a text revision, call `savectl_content_identity` in that decision before ticking the controller or accepting another command. Undo to an equal ID still publishes a revision. Use `edit_continue`, `app_buffers_service`, `app_save_service`, `app_session_service` and `journal_service` on the live writer. Give each ready record class an opportunity before the writer submits ready barriers, rotating ready instances within each class. Count a record issued on input dispatch toward that producer's turn. A blocked head or a full reply stream still allows these steps.
4. Make the matching APPLY eligible when the whole edit final is visible on the named journal. Withhold new text and ACK in FINAL_PENDING. Reconcile the original final, then adopt it once. At headless reply delivery call `edit_take_change`. After submit, open or activation, adopt eligible counts and run idle maintenance. Retire physically finished handles, leave the step and recompute readiness.

Main waits only in the outer OS wait set. Enable stream reads when admission space exists and writes while bytes remain. Resume partial replies at their byte cursor. Register watches and `os_wake_fd(work_pool_wake(pool))`, drain through the mailbox protocol, then check pending mail, finished handles, cluster readiness and deadlines before sleeping. Ready records, fair-turn continuations and the maintenance “more” result schedule an immediate turn. A worker, unresolved explicit retry or blocked socket alone consumes no turns. [Shared work pool](../../src/work/work.h) · [Edit publication](wave1-edit-spec.md)

## app_buffers.h

An `app_buffer` is a stable index entry for an admission or journal owner. An *owner* is the lineage's nonzero buffer ID. An *incarnation* is the currently installed editor/tree for that owner. A *binding* is a canonical name and an accepted `file_expectation`. A *generation* is a journal key for a particular declared source. A *dependent* is an owner whose recoverable origin or complete suffix can still need that source. A live mapping or Undo record can also supply a future operand until capture embargoes it. The index owns membership, active order and reservations. A session leaf is its immutable publication of path, slot, owner, origin and view, copied on the UI. A worker receives only immutable inputs and owned pins.

| Declaration | Contract |
|---|---|
| `app_result app_buffers_open(app *a, const char *path)` | OPEN starts acquisition. NEW passes NULL for the empty untitled origin. On duplicate identity, select the indexed owner. |
| `app_result app_buffers_select(app *a, uint64_t owner)` | SWITCH publishes the active owner in session order. |
| `app_result app_buffers_close(app_buffer *b)` | CLOSE_DISCARD authorizes removal of this owner. Settle its effects and secure omission from TABS before reporting removal. |
| `app_result app_buffers_destination(app_buffer *saver, const char *path, file_target *target, uint64_t *displaced_key)` | Save intake asks for a checked destination. NULL selects the accepted binding. On OK the index owns the scoped claim and supplies the target value and scalar key for capture. |
| `savectl_err app_buffers_checkpoint(void *ctx, const journal_target *target, journal_request **out)` | The controller's stable entry is ctx. Offer this owner's matching published boundary and transfer the funded request. |
| `savectl_err app_buffers_fence(void *ctx, uint64_t token, os_identity incoming)` | Extend the admitted claim to the prepared fd identity before PREPARED. |
| `void app_buffers_release_fence(void *ctx, uint64_t token, savectl_settlement settlement)` | Publish the disposition and retire that token's claim under erratum 1. |
| `void app_buffers_service(app *a)` | Advance open, selection, count adoption and close on their actual dependencies. |

**Three entry states.** BUF_OPENING owns an acquisition or staged recovery pending installation. BUF_LIVE owns an editor and controller. BUF_CLOSING retains the entry while discard settles and borrowers retire. Successful removal leaves no entry. An unresolved recovered OPEN member keeps its recorded place and source diagnosis in BUF_OPENING. Reads and dependent input receive that diagnosis. Reload in an existing editor is savectl's operation.

OPEN obtains a canonical name and tagged baseline from the file acquisition. Recheck identity on final adoption, comparing volume and both inode words, including across hard links and simultaneous opens. At OPEN_READY call `file_open_tree` on the process reserve. NEW supplies an empty private tree. Use `journal_new_owner` and, for acquired bytes, `journal_source_key`. Fund construction, publish the required GENERATION, BASE and TARGET before the TABS decision exposing the owner. A partially visible registration has no selectable new member until that decision. Recovery retains its recorded owner. Supply the same initial IDs and verified backing to edit and savectl. A new clean file has matching current and saved IDs. Failure to observe a path has its own error.

For mapped ORIGINAL bytes, pin the installed backing and submit a WORK_BULK count with owner, incarnation and source identity through `work_submit`. One chunk per result is the smallest independently adoptable A19 batch. Use `piece_lines_count` on the pinned ORIGINAL, publish each chunk result through `work_publish`, and use `work_continue` for another chunk. A close or reload cancels stale count work with `work_cancel`. Retain a result across mailbox pressure. Adopt with `piece_lines_adopt` only at an idle, matching tree, after checking bounds. A close, reload or stale count cannot install into a reused slot. Keep job inputs until `work_handle_finished`, then finish eligible file closes. Rotate idle editors. Budget one reclaim step and one undo retirement (minimum positive work for each), and PIECE_MAX_HEIGHT + one resolve visit (one full root-to-leaf path including its leaf). Edit performs reclaim, resolve, replenish and undo maintenance in order. Use the configured history cap derived in edit.h. [Piece line contracts](../../src/piece/piece.h) · [A19 and reserve rules](wave0-plan-amendments.md)

### Destination admission

The index resolves the checked name and expected generation that replacement could displace, G. An explicitly absent destination has a canonical name and FILE_ABSENT. The wire zero key means no generation displaced. On Save As the saver's own prior source H can differ from G. Use the origin associations from registration, recovered controls and verified settlements. Keep the source association needed by a possible suffix across a binding change. Compare the named source and file identity, including aliases. A comparison of active tab paths alone cannot establish independence. Match a declaration by the registration rule in erratum 2. Treat an unclassified dependency as unresolved and withhold displacement with its diagnosis.

In the UI decision that admits SAVE or SAVE_AS, inspect **every indexed owner**, including still-effective closing entries and pending acquisitions as they publish. If another owner depends on G, return **REFUSED / APP / OTHER_DEPENDENT** with its owner ID. The attempt ends there, before `savectl_save_request`: no token, embargo, edit bookmark, checkpoint, temp or journal record. An already bound live identity also remains exclusive. For a transient intersecting claim, wait for settlement or refuse competing admission as busy. A nonintersecting save or acquisition can proceed.

Reserve the canonical destination, present identity and G at intake. Resolve already provisional operands that could commit a reference to G before capture and seal the saver's complete boundary. Serialize recheck, capture, claim-to-token association and edit notification. Capture installs permanent VALUE_ONLY for G, including restores from later Undo/Redo. At the fence extend the claim to the incoming synced temp identity. Check both old and proposed associations on open, reload, rebind and discard publication, including a completion begun before capture. Defer a new conflicting publication and re-observe it after release. A name check at intake alone would let an acquisition publish a dependent between PREPARED and rename. Roll back an unadmitted claim in that dispatch. Hold an admitted claim through the release callback in erratum 1.

## app_save.h

`app_save_request(app_buffer *b, const char *path)` returns `app_result`. SAVE passes NULL to use the accepted binding. SAVE_AS supplies a destination path. At a complete boundary pass `edit_read`'s snapshot and ID, the bounded target value and scalar displaced key to `savectl_save_request`. On admission send EDIT_SAVE_CAPTURED before dispatching more input. Keep the reply correlation (command ID, owner incarnation, save token) through definitive settlement.

`app_save_action(app_buffer *b, app_save_action_kind action)` returns `savectl_err`. Its three choices are the three user decisions KEEP, RELOAD and RESOLVE, mapped to the corresponding savectl calls. KEEP secures this buffer's validated text and accepted TARGET before adopting the observed binding. RELOAD installs `savectl_take_reload`'s validated tree/source with fresh history, clamped selection/view, matched IDs, updated watch and index claim in one decision. Check the incarnation, intervening revision, binding and close state before adoption. RESOLVE queues the single retry or cancellation eligible in the controller's phase.

`void app_save_service(app *a)` ticks eligible controllers, including closing ones, and delivers correlated outcomes. Wire the three app_buffers callbacks to the controller's stable entry. For a self-checkpoint lend the current published snapshot, IDs, requested TARGET and `file_backing_validate` with null ctx for file-created mapped bytes. In `journal_checkpoint_goal.minimum` use capture's image-qualified position when coverage through that cutoff is required, or the signed all-zero minimum when any independent boundary with the required binding and suffix suffices. A changed binding needing replacement uses the signed supersession path. Keep the entry alive through controller destruction. Defer logical adoption across a provisional edit while continuing delivery. Savectl owns temp custody, verification, token, save outcome and the journal effects of save/Keep/conflict. Read `savectl_get_model` for status. On definitive verified install, transfer the matching edit cutoff to saved even if typing has since advanced. Release a definitively failed cutoff. Forget an invalidated saved association in the same publication. See the complete-FINISHED exception below.

**Zero L5 save phases.** The signed controller has eleven phase values and two independent closing dispositions, giving 22 reachable combinations. The adapter stores only its own reservations, reply correlations and borrower obligations. [Signed savectl transitions](wave1-savectl-spec.md)

## app_session.h

The session owns the named writer, ordered metadata publication, recovery staging and shutdown. It uses the buffer index for effective membership. Its **three lifetime states** are RECOVERING while installing the selected image, SERVING while admitting commands and DRAINING while settling close. Successful close consumes the writer. Compaction, gap repair and sync each have a journal request or writer condition that already describes their progress.

| Declaration | Contract |
|---|---|
| `app_result app_session_start(app *a, const char *journal_path)` | Admit `journal_recover` and own its writer and recovery request. |
| `app_result app_session_rotate(app *a)` | COMPACT requests `journal_rotate`. Retry unadmitted WAIT on progress. Follow an admitted request through the journal-parent barrier. |
| `void app_session_shutdown(app *a)` | QUIT or disconnect stops fresh mutation and begins drain idempotently. |
| `void app_session_service(app *a)` | Advance the recovery cursor, metadata, small-draft protection, rotation and retirement. Drive `journal_state` / `journal_reconcile` on the original ambiguous effect. |

**Restart.** Wait for the recovery request while servicing the shared pool. For OK, DAMAGED or SOURCE, drain `journal_recovery_next` in bounded turns. Take BUFFER trees, IDs and pending cutoffs; copy borrowed CONTROL payloads and EVIDENCE names/bytes or secure the preserved image before releasing the request. Other terminal failures have an empty cursor and a diagnosis. Stage controls before publishing any editable owner. Use the effective BASE/checkpoint, TARGET, VIEW, ordered OPEN entries in TABS and active selection. Verify a saved ID together with the real binding. Restore open-PREPARED claims before enabling competing operations and reconcile each original token as in erratum 5. A staged tree can supply a self-checkpoint before edit creation. A foreign or unresolved source yields evidence and a visible diagnosis; never apply its suffix to the foreign file. Preserve the source image and report unrepresentable membership on a partial restore.

**Metadata and small drafts.** Copy leaves at membership/view decisions on the UI. Serialize whole-list TABS writes for new/open/switch/discard. Keep the previous effective decision until the controls required for the new one are visible. Cover irreversible discard with sync. Preserve applicable VIEW bytes at membership/shutdown boundaries. After a qualifying edit ACK, protect the published text with the ordinary by-value checkpoint and covering sync specified in erratum 9. The ACK itself proves whole-command publication to the kernel page cache.

**Compaction and close.** Publish effective membership, TARGET, VIEW and policy controls before rotation admission and before making a changed decision effective during rotation. Journal defers rotation for live commands, checkpoints, cutoffs, tokens or durability promises and switches only on its signed exact match. Report COMPACTED at DURABLE after journal-parent sync. QUIT preserves the open order for restart. Finish accepted edits/saves, secure required checkpoints and final metadata, sync, then retire controllers, editors, count jobs and request references in dependency order. `journal_close` returning WAIT preserves the writer. Service it until success, then retire pool and reserve. A connected drain exposes diagnoses and eligible resolution. On disconnect, settle progressing work. If a retained possible effect reaches a diagnosed explicit-retry-only failure, exit with failure, reporting path, owner/token, publication and service/native error. Preserve the named image and set-aside evidence; identify anything left only in RAM. This is a crash disposition, with no successful close reply or success-only destruction. Item 10 requires no lower-header change. [Signed journal runtime](wave1-journal-wire-amendment.md#proposed-runtime-section-of-journalh)

## Headless stream

Invoke `sublimite-v2 --headless-fd FD` with an inherited connected stream and a disposable namespaced XDG data root shared across relaunches. Frame each request or reply as a netstring of field netstrings. Each length counts raw bytes in canonical unsigned decimal, followed by a colon, the bytes and a comma. First fields are a positive increasing command ID and verb or reply kind. Reserve ID zero for startup diagnostics. Reject duplicate IDs, wrong arity, invalid byte ranges, NUL in a path and unavailable capacity before effects. Broken framing ends the connection with a diagnosis. Paths are nonempty. Text is arbitrary bytes. Reserve reply space before mutation and use partial nonblocking reads/writes. Repeated reads can cover long ranges. Address editing, save and close commands to the active owner at their FIFO position.

| Request fields following the ID | Completion observed by the parent |
|---|---|
| NEW · OPEN path | OPENED(owner) at recoverable registration or selection of the deduplicated owner. |
| SWITCH owner · CLOSE_DISCARD | SELECTED(owner) at published selection or CLOSED(owner) after durable removal. |
| SELECT cursor anchor | APPLY(owner, content_id, revision) after `edit_select` on valid byte offsets. |
| TYPE bytes · DELETE off length | APPLY after `edit_replace` on the selection for typing or the stated interval for forward deletion. Pass the monotonic receipt time. |
| REPLACE off length bytes | APPLY after the separate replacement through `edit_apply_plan`, with the caret at the inserted end. |
| UNDO · REDO | APPLY after `edit_replay` and its continuations publish the whole group. A no-op reports the existing boundary. |
| SAVE · SAVE_AS path | ADMITTED(owner, token), then SAVED(owner, token, saved_id) at verified durable finish, CANCELLED on durable cancellation, or SAVE_FAILED with token and diagnosis. Preserve correlation through an ambiguous effect and resolution. A pre-capture refusal returns REFUSED with the blocker where applicable. |
| KEEP · RELOAD · RESOLVE | ACCEPTED(owner) on asynchronous admission, then SETTLED(owner, outcome) at actual adoption or a diagnosis. |
| LIST | ENTRY(owner, binding kind, path) for each effective open member in order from one decision, then END(active owner or zero for none). |
| STATUS owner | STATE(entry state, edit phase, savectl phase, closing, current_id, saved_id, dirty, request_id, outcome, journal condition, origin, service error, native error). |
| READ owner off length | BYTES(content_id, raw bytes) from a validated published snapshot, or SOURCE with evidence name. |
| LINE_BEFORE owner off · LINE_START owner line | RANK(exact, preceding newlines) or START(exact, offset, preceding newlines) from `piece_lines_before/start` on the published view. |
| COMPACT · QUIT | COMPACTED at new-name durability or CLOSED_SESSION on successful journal close. Drain the final reply before normal exit. |

Each reply argument occupies one field. Use declared enum names, decimal integers, zero/one for false/true, and empty fields for unavailable values. REFUSED returns origin, service code, native error and any specified blocking owner. SUSPENDED is an interim diagnosis on the original correlation. Startup diagnostics identify the recovery reason and preserved evidence name. The observation verbs expose the membership, save/recovery decisions, bytes and line queries exercised by the exit and integration fixtures. Fault injection, cut notification, counters and allocator guards belong to the harness.

## Acceptance traces

* **Open and edit.** Open a file and an alias, confirm the same owner, then start a slow open and switch to a ready owner. Exercise failed-open dependent input, partial replies and a full FIFO. Kill before and after APPLY for ordinary, grouped and replayed edits, including untitled text. A process kill before receipt permits the prior or whole published command; after receipt the whole command must survive. Compare recovered bytes by owner against an independent byte model. A partial group is never an answer. Assert the live release malloc guard is active with zero allocations through production mutation and submit (Law 2).
* **Refusal before capture.** Open existing B, edit B while its origin still needs G (use a mapped file above the self-contained limit), NEW A, type distinct bytes and SAVE_AS B's destination from A. Expect REFUSED / APP / OTHER_DEPENDENT(B), the same save request ID/outcome, edit IDs and bookmarks, and unchanged target bytes. Instrument capture, temp and journal issue: this attempt issues none. Restart, LIST and READ both originals including B's unsaved suffix. Cover an alias of the present identity, a closing B before durable removal and an open that tries to publish into an admitted claim. Test a dependency whose current path has changed too. In a separate fixture delete a large span from H, SAVE_AS to an absent name, and Undo during temp write. Establish that the complete post-cutoff restore names H. After FINISHED the saver's binding names the new file, but another owner's SAVE_AS over H must still be refused. As positive controls, save to an absent destination and save over B after explicit discard has durably removed its dependency.
* **Typing through a save.** Hold temp write after capture, type again, and require APPLY before releasing the worker. Disk and SAVED identify the earlier cutoff and current text stays dirty. Repeat with Undo restoring a displaced mapped span during the write, and with a second save to the same name. Compare disk, current text and dirty identity independently.
* **Rename and crash cuts.** A parent waits on a test-only hook marking each actual step, kills, relaunches into the same root and compares LIST, STATUS, READ and disk with the cut oracle. Cut at capture, temp creation/partial write/sync, VALUE_ONLY, self-checkpoint BEGIN/literals/COMMIT/sync where applicable, PREPARED partial/visible/durable, rename, target-parent barrier and retry, fd verification, installed GENERATION, FINISHED or CANCELLED partial/visible/durable and outcome delivery. Fail temp and journal fsync, target and journal-parent dirsync independently. A torn prepare cannot authorize rename. At an open prepare the prior branch replays its verified origin; the installed branch uses the saved snapshot plus complete post-cutoff commands. An ambiguous terminal is reconciled once. Exercise foreign replacement immediately after rename and after complete FINISHED before adoption. Preserve foreign bytes. In the latter case STATUS shows IDLE_CHANGED, unknown saved ID and SAVE_FAILED/CHANGED while protection sync is still held. Count one terminal for that token. A conflict checkpoint visible on the named inode protects process restart; its covering barrier protects machine restart. Before independent protection, a lost BASE can leave unresolved evidence under A23. Model machine loss separately using surviving whole prefixes and names constrained by successful barriers. [A28 cut table](wave1-journal-write-model.md) · [T5 restart interval](wave1-savectl-spec.md#flat-oracle-and-discriminating-traces)
* **Lower integration.** Inject OOM with a reader holding an older snapshot. A provisional abort preserves text, selection, IDs, history and the reader's bytes; retry after capacity return. Exercise supersession, stale deliveries, cancellation without a message and shutdown waiting on a physical lease. Observe that other edits progress during checkpoint production and the loop sleeps during a blocked sync. Count a mapped source off path, verify LINE_BEFORE/LINE_START before/after DELETE and UNDO against flat bytes, and SAVE_AS to an absent path. Cover a one-byte literal, a large ORIGINAL span and 513 chunks (the old limit of 512 plus its first crossing). A13 bounds inverse copying over the pair by min(length, 16 × spans) bytes from its literal/span choice, with O(spans) inverse storage. For a single eligible mapped span, UI reads at most two boundary chunks of 65,536 bytes each, as derived in PLAN §6. Reject counts from a stale incarnation. Check the PLAN TSan lanes and rotation kill/parent-sync cuts in their owning suites. L2-3's T2/T3 checkpoint and fence cases remain journal-level tests with live and closed-kept peer fixtures. [Save bead](../briefs/L2-3-journal-save.md) · [Wave 1 exit](../PLAN_V2.md#wave-1--walking-skeleton-vertical-slice-1-headless-crash-safe-editor)

## Signed lower-header errata

The insertion points below are named declarations in the [signed savectl header](wave1-savectl-spec.md#proposed-srcsavectlsavectlh), [signed edit header](wave1-edit-spec.md#proposed-srceditedith), [journal runtime section](wave1-journal-wire-amendment.md#proposed-runtime-section-of-journalh), `review/l2-1:src/file/file.h` and [work.h](../../src/work/work.h). Code blocks give the declarations and quoted paragraphs give the header contract. Item numbers identify the advisor's rulings.

### 1. Fence settlement and edit bookmarks

**savectl.h.** Insert before `savectl_session` and replace its definition with the following.

```c
typedef enum savectl_destination_result {
    SAVECTL_PRIOR, SAVECTL_INSTALLED, SAVECTL_FOREIGN
} savectl_destination_result;

typedef struct savectl_settlement {
    savectl_destination_result result;
    struct { uint64_t key; os_stat stat; } installed; /* INSTALLED only */
} savectl_settlement;

typedef struct savectl_session {
    void *ctx;
    savectl_err (*checkpoint)(void *ctx, const journal_target *target,
                              journal_request **out);
    savectl_err (*fence)(void *ctx, uint64_t token, os_identity incoming);
    void (*release_fence)(void *ctx, uint64_t token,
                          savectl_settlement settlement);
} savectl_session;
```

Replace the callback contract and the model paragraph defining the fence lifetime with this text.

> The session owns generation membership. ctx identifies the saver. checkpoint requests a by-value checkpoint of the saver's current published boundary, with the supplied TARGET and matching saved association. Return one owned journal request on success. Use the validator of the pinned backing for mapped reads. The session's peer policy must establish that no unprotected dependent can lose its source through displacement. It may refuse admission when another owner depends on the displaced generation. A session admitting that displacement secures durable independent peer coverage and suffixes under the fence.
>
> Capture associates the caller's existing destination claim with the admitted token before yielding. On refusal before capture the caller unwinds the intake claim. After temp sync and before PREPARED, fence atomically extends this claim to incoming, the volume and both inode words of the prepared fd. Compare live owners, acquisitions about to publish and competing claims. Refuse an intersecting held reservation without waiting on its holder. Each subsequent call checks the same claim. Return BUSY while an eligible dependency progresses. Before OK, fund the eventual index publication and secure required coverage. Serialize callbacks with affected membership publications. Independent typing continues.
>
> Invoke release_fence exactly once per admitted token at definitive settlement, including a definite failure before the first fence call. Serialize release with final model publication before new admission. The callback applies the indicated index action before dropping the matching claim. A possible append, uncertain rename, pending barrier or obligatory conflict checkpoint keeps the claim. A complete FINISHED with failed verification reports its outcome immediately; release follows durable conflict protection.
>
> PRIOR leaves the prior destination ownership in place on definitive pre-prepare failure or durable cancellation on the prior branch. INSTALLED transfers the reserved destination to the saver on verified durable FINISHED. Its key and full fd stat identify that same installed generation. Publish the new claim, release the superseded one and reconcile any still-displaced bindings together. FOREIGN withdraws authority relying on a disproved destination and preserves independently verified claims and recoverable text. It applies to a classified foreign destination on definitive failure or protected cancellation, including conflict protection after a complete FINISHED. Classify the destination itself: a conflict solely on the saver's old source during Save As does not disprove the destination. The three outcomes correspond to retaining, transferring and withdrawing ownership.

**edit.h.** Replace `edit_save_event` and the cutoff preconditions at `edit_save_boundary` with the following.

```c
typedef enum edit_save_event {
    EDIT_SAVE_CAPTURED, EDIT_SAVE_INSTALLED, EDIT_SAVE_RELEASED,
    EDIT_SAVE_FORGOTTEN
} edit_save_event;
```

> CAPTURED requires a complete published boundary. INSTALLED and RELEASED require the matching owned cutoff. FORGOTTEN clears only the saved bookmark, is idempotent and requires no cutoff. Deliver it in the same UI publication as invalidation of the saved association, including Keep, detachment and idle conflict. Preserve any independent in-flight bookmark. On rejection of a complete FINISHED at verification, release the matching edit cutoff and forget saved on entry to IDLE_CHANGED. The journal cutoff and index claim survive through durable conflict protection. In other ambiguous cases retain the edit cutoff until its definitive disposition. Deliver INSTALLED on verified durable install or RELEASED once on definitive non-install if still owned.

### 2. Journal lineage allocation

**journal.h runtime.** Insert after `journal_append`.

```c
journal_err journal_new_owner(journal *j, uint64_t *owner);
journal_err journal_source_key(journal *j, const journal_generation *source,
                               uint64_t *key, journal_request **declaration);
```

> new_owner allocates a fresh nonzero owner from the writer's buffer high-water. source_key accepts a validated acquisition's canonical location, fd-derived wire identity and prefix witness; ignore the input key and flags. Reuse the active matching declaration with its current policy and return its key and a null declaration. Otherwise allocate from capture's generation high-water and admit the GENERATION append, returning its key and original owned request. Serialize competing lookups through declaration visibility or reconciliation. Exclude staged and abandoned install keys. Check outputs and counter exhaustion before admission; refusal preserves outputs. Burn allocated numbers for this writer lifetime. On restart advance past the preamble high-waters and every retained mention, including discarded owners, orphan declarations and TABS entries. Carry both high-waters through rotation.

### 3. File target value and source custody

**file.h.** Insert immediately after `file_expectation`.

```c
typedef struct file_target {
    char path[OS_PATH_MAX];          /* canonical, NUL-terminated */
    file_expectation expectation;
} file_target;
```

**savectl.h.** Include `"file/file.h"`. In the common input-ownership clause, use “Inputs are borrowed unless the call explicitly transfers ownership on success.” Remove the `struct file_target` forward declaration and the entire `savectl_identity` definition. Replace the indicated fields and declarations with these lines in their respective scopes.

```c
/* savectl_options */
file_source *source;
const file_target *binding;

/* savectl_request */
const file_target *destination;

const file_expectation *savectl_target_identity(const savectl *s);
savectl_err savectl_rebase_target(savectl *s, const file_target *binding);
```

Replace the binding/source ownership text on construction, admission, rebase and the accessor with the following.

> file_target is a borrowed path/expectation value. Copy it into controller storage on successful create, save admission or rebase. A null binding represents untitled or detached text. The session preserves the intended former target when needed. A named absent target carries its canonical name and FILE_ABSENT. Resolve the parent and validate the leaf for a new name. Refuse an overlong name or failed observation with its diagnosis. Observe new targets using file_expectation_observe_path/at. Recheck the accepted expectation for a bound name; an observation by itself grants no authority to overwrite a changed target. target_identity borrows the accepted file_expectation until the next controller call and returns NULL when unbound.
>
> source is the nullable owned-backing type file_source. The caller lends its reference; create retains it on success. An acquisition owner gets its reference through file_source_acquire. Release the controller's reference after its borrowers retire. An acquisition or watch owner retains its own struct file storage. Private text can use the temp core with no opened source. savectl_take_reload still transfers the acquired tree and file to the UI for installation.

### 4. Held output descriptor

**file.h on review/l2-1.** Replace `file_temp_state` with the following.

```c
typedef enum file_temp_state {
    FILE_TEMP_EMPTY = 0,
    FILE_TEMP_SYNCED,
    FILE_TEMP_INSTALLED_UNSYNCED,
    FILE_TEMP_HELD,
    FILE_TEMP_DONE
} file_temp_state;
```

After `file_temp_retry_barrier` insert the two declarations. Replace the publication, retry and discard custody clauses, and append the last sentence below to the one-shot `file_save_write` contract.

```c
int file_temp_verify(const file_temp *t, file_expectation *installed,
                     uint32_t *prefix_crc32c, os_err *oserr);
int file_temp_recover(file_temp *t, const char *path,
                      const file_witness *prepared, os_err *oserr);
```

> EMPTY owns nothing. Prepare hands off a synced, unpublished temp in SYNCED. Publish may rename only from SYNCED and at most once. PRE_RENAME may clean up a definitely unpublished temp and finish DONE. INSTALLED_UNSYNCED retains the written fd and original directory for barrier-only retry. VERIFIED, RENAME_UNCERTAIN and CONFLICT retain both descriptors in HELD for verification, reconciliation or discard. On an uncertain rename, a subsequent proof of installation permits file_temp_retry_barrier on HELD to settle the outstanding barrier on that original directory. A retry only syncs and re-verifies. Discard closes held descriptors and reaches DONE. Unlink only an identified, still-owned, definitely unpublished temp. An uncertain rename gives no authority to unlink either the destination or a reused temp name.
>
> file_temp_verify compares the named path to the held fd and verifies the full prepared witness on a stable observation. On success return the full post-rename fd expectation and wire prefix CRC32C; derive prefix length from the wire rule and fd size. A mismatch reports FILE_ERR_CHANGED, an observation failure its actual error. Failure preserves custody and baseline. Savectl obtains this candidate off the input path, then confirms fd/path identity and stat against it at FINISHED issue and durable adoption. A newer observation invalidates a stale result. Defer on uncertainty and do expensive content reads off path. Verified durable adoption changes the accepted binding. The original backing continues to validate original bytes.
>
> file_temp_recover takes an empty temp and a selected open PREPARED's path and witness. Open the named candidate and parent off path. On a stable fd/path match to the whole prepared witness, transfer the descriptors, destination name and witness to the temp in INSTALLED_UNSYNCED. Use ordinary barrier-only retry and verification to finish. On mismatch or I/O failure release partial acquisitions, return the cause and leave the output empty. The one-shot file_save_write caller discards retained custody before returning.

### 5. Restart custody and validation

**journal.h runtime.** Replace the BUFFER member in `journal_recovery_item.value` and extend its ownership comment.

```c
struct {
    uint64_t owner, current_id, saved_id;
    piece_tree *tree;
    journal_cutoff *pending;
} buffer;
```

> BUFFER transfers the original writer cutoff for a selected open PREPARED, otherwise NULL. Match it to the selected CONTROL by owner/token. Preserve its image, reserved closing credits, embargo and evidence. The cursor releases untaken trees and cutoffs. A taken cutoff transfers once to the recovering disposition owner. A mapped BUFFER tree uses a file-created piece_backing whose context owns the file_source for that mapping. Private and unresolved text require no mapped source. Retain EVIDENCE for each unresolved origin.

**file.h.** In the `file_backing_validate` contract replace the sentence specifying ctx.

> A non-null ctx is the struct file. A null ctx selects the source of the supplied file-created mapped backing: its piece_backing.ctx is the file_source for that mapping. Validate the supplied backing against its own acquisition. The tree or snapshot reference pins that context through the last read and physical retirement.

**savectl.h.** Insert before `savectl_options` and add the indicated member.

```c
typedef struct savectl_recovery {
    const journal_target *intended;
    const journal_save *prepared;       /* selected open PREPARED, if any */
    journal_cutoff *pending;            /* matching cutoff, taken on success */
} savectl_recovery;

/* savectl_options */
const savectl_recovery *recovery;
```

Replace the ordinary create initial-state sentence with this contract.

> NULL recovery starts a fresh open controller in IDLE_CLEAN. On recovery copy the effective TARGET, any selected PREPARED and their referenced bytes. Take its matching cutoff on successful create. Before admission compare a fresh full file observation with the selected wire expectation and verified origin. Supply a binding only with its real full expectation. Distinguish intentional absence/untitled from a missing expected file. Preserve a mismatching intended TARGET. Accept a verified saved ID only together with its matching binding. Idle foreign/missing enters IDLE_CHANGED and requests protection before save. Observation failure retains the staged draft and diagnosis for retry; unresolved text remains evidence-only.
>
> With an open prepare, reinstall the session reservation before enabling opens or saves. Reconcile the old expectation, installed whole-file witness and namespace under it. Prior enters recovery-pending cancellation. Foreign enters changed protection/cancellation. A matched installed candidate requires file_temp_recover custody, target-parent barrier and fresh verification before FINISHING. Reuse the original token, position and installed key. Failure preserves the obligation. Recovery authorizes no second rename. A selected complete FINISHED has no open token. Settle the recovered transaction using the staged unlogged tree when needed, then construct edit with reconciled IDs. Its new history has no pre-crash edit bookmark.

### 7. Request lifetime and stale checkpoint decisions

**Cut both proposed functions.** `journal_request_retain` would add an observer whose sole purpose was to keep the validator alive. The request pins the snapshot through its last physical read. The snapshot pins the file-created backing, which owns its validator context under item 5. A join uses the original validator and already returns an owned request to each joining caller. Savectl can take the callback's returned request directly.

There is no Wave 1 session action requiring `journal_checkpoint_invalidate` without replacement. A changed binding needing protection submits its current checkpoint decision to `journal_checkpoint_request`. That notification revokes the stale producer before replacement funding, even on NOMEM. Ignore stale adoption, including an old covering barrier, and retry the required decision on progress. Discard or shutdown can settle an outstanding producer and its physical holds before durable removal or close. Neither needs to revoke it to publish a successor. A possible/torn frame still goes through ordinary reconciliation.

### 8. Runnable service and finish-before-wake

**journal.h runtime.** Add beside `journal_service`.

```c
bool journal_runnable(const journal *j);
```

> Inspect present continuations on the UI without I/O. Return true when another service opportunity can advance a record, adoption, barrier or retirement now. Waiting only for a worker, explicit retry or unchanged external dependency is quiescent. Delivery and returned capacity re-enable eligible work. An admitted journal_sync records its required position and returns the owned request. Only the writer submits ready barriers, at the end of the record round after external producers have had their opportunities. Advance each ready class in rotation and rotate instances within classes. Use one ready record per opportunity, the smallest positive fair record quantum. A blocked class counts as visited. A newly ready record may start the next turn.

**savectl.h.** Add beside `savectl_tick`.

```c
bool savectl_runnable(const savectl *s);
```

> Inspect present continuations on the UI without I/O. Return true for a ready control record, adoption or retirement. A worker, explicit retry or unchanged session dependency alone is quiescent. The host offers another tick on delivery, capacity return or relevant fence/checkpoint progress. Apply the writer's single-record quantum inside tick. Request barriers through journal_sync. Only a covering DURABLE result permits the dependent step. Runnable work schedules an immediate turn; there is no retry timer.

**edit.h.** At the edit command emission contract add the first paragraph. Replace the `edit_maintain` declaration with the following and replace its unconditional next-turn retry clause with the second paragraph.

```c
edit_result edit_maintain(edit_buffer *b, size_t reclaim_steps,
                          size_t resolve_visits, size_t undo_records,
                          bool *more);
```

> Each edit call may issue one ready journal record in its service opportunity. Return MORE if another ready record remains. Yield before owner commit if final needs a later turn. The health check, owner commit and funded final remain indivisible.
>
> Set *more when another funded maintenance slice can progress on the same idle tree. Combine piece_reclaim's remaining work and piece_lines_resolve's more result. When undo consumed its full positive budget and remaining work is unknown, schedule a probe slice. WAIT sets *more false until edit settlement. NOMEM waits for capacity or explicit retry for the refused action, preserving prior published geometry; independent ready work still sets *more. Count adoption, snapshot release and a new edit boundary reschedule eligible idle maintenance.

**work.h.** Append to the `work_handle_finished` contract.

> Publish physical completion and returned capacity before signaling the pool wake, including completion without a message and completion following cancellation. A UI cancellation that retires synchronously is itself a dependency change. The waiter registers the wake, drains through the mailbox protocol, then checks finished handles, readiness and pending mail before sleeping. A racing completion leaves a waitable wake.

### 9. Journal volume policy

**journal.h runtime.** Insert a header constant beside the checkpoint contract. In the following replacement template L2-2b-3 substitutes its derived unsigned byte count for `@LIMIT_BYTES@` and records the pin, rotation budget and margin in the adjacent comment. The value is inclusive.

```c
/* Whole-text/delta split at the v1 parity pin. Reduce only if one such
 * framed checkpoint plus the stated margin exceeds the rotation budget. */
#define JOURNAL_SELF_CONTAINED_LIMIT UINT64_C(@LIMIT_BYTES@)
```

> After the first acknowledged edit whose published text length is at most JOURNAL_SELF_CONTAINED_LIMIT and whose recovery still depends on a source, the session retains its validated snapshot, IDs and intended TARGET. After submit request an ordinary by-value checkpoint and sync through its visible COMMIT. Secure an independent suffix from BEGIN, including Undo between literal records. For a restart-resolvable ORIGINAL, settle any provisional reference to its key and publish its monotone VALUE_ONLY redeclaration before BEGIN. ADD and materialized private bytes emit literals. If edits advanced the boundary, take the current matching snapshot and IDs at BEGIN. Recheck eligibility and source on change. Retain the protection obligation on a refused claim and retry on progress. A newer qualifying boundary can satisfy it. First becoming small on a later ACK qualifies. Use the existing checkpoint slot. The edit ACK does not wait for this covering sync.

L2-2b-3 sets the numeric constant from journal volume and reports time to durability under typing separately. Ordinary rotation already has an exact-match switch and a parent barrier. [Rotation bead and derivation](../briefs/L2-2b3-journal-rotation.md)

## Deferred to the Wave 3 session vertical

| Session feature | Lower mechanism and work deferred |
|---|---|
| CLOSE_KEEP, REOPEN, EVICT | Encode closed-kept position, active order and view with TABS/VIEW. Secure text through a validated snapshot or durable by-value checkpoint plus independent suffix. Reopen on a verified source or protected bytes. Settle tokens and durably omit an evicted member before releasing its last recovery representation. |
| Imports | Use the writer's lineage allocator for the new local owner. Validate/translate source declarations or materialize bytes by value, then publish BASE/checkpoint, TARGET and membership. Preserve unresolved source evidence. |
| Peer protection in the index | Extend dependency admission to kept, detached, recovered and imported drafts. Under the L2 fence and permanent VALUE_ONLY embargo obtain each dependent's BEGIN-bound checkpoint COMMIT and covering sync, secure its independent suffix, then permit PREPARED. Apply the signed settlement under the reservation. The L2 fence hook, checkpoint groups and L2-3 journal-level T2/T3 coverage are signed for Wave 1. |
| Item 6, displaced-key sets and dependency queries | Wave 1 uses scalar `journal_capture(..., displaced_key, ...)`. The Wave 3 spec must supply a reachable input under item 2's active-declaration reuse before proposing a set. Reconsider `journal_next_source`, `journal_depends` and `journal_owner_boundary` there against effective declarations, selectable origins/suffixes and image-qualified checkpoint goals. None is a signed addition. |
| Wave 3 portion of item 5, rebuilding a kept editor | Iterate the retained validated snapshot into a fresh private unlogged tree off the input path. Validate after the final mapped read with the backing-owned context, preserve the kept leaf on refusal and emit materialized private spans by value unless provenance is proved. Recovery of an OPEN Wave 1 BUFFER and its pending cutoff uses signed item 5 today. |

Implementation, numeric policy selection by L2-2b-3, changes to the lower owners' source files, execution of the gate and the GUI recovery interface are outside this document change.

**Pool lifetime and the idle check (coordinator, naming the existing calls the export reservations cite).** App creation initialises the shared pool with `work_pool_init`, and destruction consumes it with `work_pool_shutdown`. Before sleeping, the loop checks remaining mail with `work_mailbox_pending`, alongside runnable work and finished handles.

