I couldn’t create `docs/reviews/recovery-3.md`: this session permits filesystem reads only, with approvals disabled. No tracked files changed. Below is the report text.

Reviewed commit: `a202e2b9bdabf61fd07fc5bdd0527c73819dfe0e`. Findings are verified by tracing the code; no failure-injection tests were executed because those tests write fixtures.

**1. State machine**

Locations below are checkout-relative. “Durable” means that the code completed the relevant data and directory barriers. Successful page-cache writes alone survive process death, but are not a power-loss durability guarantee.

**Journal representation and durability states**

The journal has several independent state dimensions; treating them as one “open/closed” state conceals important failure cases.

| Dimension | States and implementation |
|---|---|
| Namespace role | Absent; empty launch destination; live session journal; abandoned discoverable `session-*`; ordinary `.set-aside-*`; `.retired`; reserved rotation temporary; reserved rotation set-aside; retained `.sublimite-base-*`. |
| Open mode | Fresh empty journal; existing journal opened with `recovery_pending`, which refuses append/pump until successful checkpoint publication. `src/journal/journal.c:514`, `:528`, `:563`, `:734`. |
| Transport | Current UI batch; sealed batch handed to worker; second current batch while worker owns the first; failed batch retained with exact write progress. `src/journal/journal.c:28`, `:732`, `:768`. |
| Acceptance | Record encoded and sequence accepted; complete record successfully written to page cache; complete prefix covered by successful data sync; new checkpoint name additionally covered by directory sync. `src/journal/journal.c:586`, `:415`, `:954`. |
| Suspension | `FULL`; append failure with queued bytes; append gap pending worker repair; worker write failure; writeback failure requiring a fresh inode; post-rename directory barrier pending. `src/journal/journal.c:543`, `:560`, `:572`, `:768`, `:965`. |
| Ownership | Session lease held; abandoned lease available; directory shared lease during rotation; directory exclusive lease during retirement; deferred-source lease held by placeholder. These are separate from journal data descriptors. |

Wire records are encoded explicitly, little-endian, with CRC checking. The header is 32 bytes. Named BASE payloads contain 40 fixed bytes, the NUL-terminated pathname, and an eight-byte ctime suffix; an empty BASE has zero identity and size. INSERT contains an eight-byte offset followed by bytes; DELETE contains offset and length; SAVE contains cutoff, phase and reserved fields followed by BASE. VIEW, TARGET, TABS and WINDOW preserve session metadata. GROUP_BEGIN/COMMIT have empty payloads. PAD seals a batch to a page boundary. See `src/journal/journal.c:118`, `:129`, `:672`.

The page size used by the format is 4096 bytes; the default sync threshold is 65536 bytes and the interval is one second. These are constants in `src/journal/journal.h:9–17`, not measured maximum loss or latency guarantees. Protected admission includes eventual PAD bytes when calculating credit (`src/journal/journal.c:631`, `:645`).

**Journal and artifact transitions**

| Transition | Function and records | Ordering and intended crash survival |
|---|---|---|
| Absent → leased empty launch journal | `journal_session_create`, `src/journal/session.c:16`; no records. Called by `src/main.c:105`. | Create unique lock file, acquire nonblocking exclusive flock, verify named lock inode, then create journal with `O_EXCL`. The journal becomes visible after its lease is held. No directory fsync occurs in this function. |
| Empty launch journal → open destination | `journal_open_with_io`, `src/journal/journal.c:514`; `journal_create`, `:493`. | Resolve and retain parent directory; open regular empty journal; set mode; fsync parent before returning. Reject nonempty journals. |
| Existing journal → recovery-pending destination | `journal_open_checkpoint`, `src/journal/journal.c:528`; selected by `src/editor/open.c:224`. | Retain existing bytes; mark `recovery_pending`. Append and pump return BUSY until a replacement checkpoint is published. Failed startup close preserves nonempty source. |
| Buffer becomes represented | `editor_register_journal`, `src/editor/buffers.c:390`. BASE; initial INSERTs for non-file content; TARGET for named buffer. | Protected records are written before installing the tab. BUSY admission flushes before retry. Named BASE derives from the acquired file generation, rather than a second independent read. |
| Current batch → accepted record | `finish_record`, `src/journal/journal.c:586`. | Increment sequence, encode and CRC, retain immutable bytes, then make one positioned-write attempt. A failed attempt can still have accepted the record; callers compare sequence/progress and must not retry the edit itself. |
| Accepted record → page-cache prefix | `cache_write`, `src/journal/journal.c:572`. | One write attempt, no UI retry loop. Short/error writes retain exact cached prefix and set sticky IO plus `append_pending`. Subsequent admission is suspended. |
| Multi-record action begins/commits | `editor_journal_staged`, `src/editor/input.c:103`. GROUP_BEGIN, data records, GROUP_COMMIT. | Begin precedes mutations’ journal records; commit follows all data. Replay validates through COMMIT before delivering any group member. A kill before complete commit restores the preceding action boundary. |
| Current batch → worker-owned sealed batch | `journal_pump`, `src/journal/journal.c:732`. PAD. | Cache PAD before allowing subsequent-batch writes, preventing a hole from hiding later records after SIGKILL. Only successful work submission transfers batch ownership. |
| Worker-owned batch → written/durable prefix | `worker`, `src/journal/journal.c:429`; `sync_disk`, `:415`. | Account already cached bytes; write missing tails; sync at byte/time thresholds or force. Acknowledge only complete record boundaries. Completion transfers statistics and batch ownership through mailbox. |
| Worker completion → reusable batch | `journal_receive`, `src/journal/journal.c:768`. | Validate handle and completion address. Preserve newer UI acceptance/cache progress. Clear batch only on worker success; retain failed progress otherwise. |
| Partial append/write failure → retryable transport | `journal_retry`, `src/journal/journal.c:543`. | Requires no active worker. Clear sticky transport error while retaining queued progress. Admission remains BUSY until the pending gap is repaired. |
| Writeback failure → repaired generation | `journal_rotate`, `src/journal/journal.c:901`. Complete CURRENT checkpoint. | `journal_retry` refuses writeback repair because previously released unsynced pages may have been lost. A complete checkpoint into a fresh inode is required. |
| FULL → complete replacement | `editor_flush`, `src/editor/editor.c:882`; `editor_checkpoint`, `src/editor/checkpoint.c:268`. | Drain what can be drained, then encode all current buffers and rotate. Configured log budget is added to checkpoint size, allowing large checkpoints without preserving old history. |
| Live journal → rotation temporary | `journal_rotate`, `src/journal/journal.c:901`; `checkpoint_temp`, `:856`. Complete checkpoint records. | Preflight size; acquire shared nonblocking directory flock before creating temp; create private inode; write and fully flush checkpoint. Original remains named until replacement rename. |
| Clean-source rotation temporary | `checkpoint_temp`, `src/journal/journal.c:862`. | Name carries `+`. Source device/inode/ctime and hex-encoded basename identify the old generation where space permits. A dead temp can be deleted only if that exact original survives. |
| Failed-source rotation temporary | Same function. | Name carries `!` when worker or append failure is present. Its checkpoint may contain accepted edits absent from the old inode; housekeeping preserves it even if the original identity matches. |
| Unflagged/legacy rotation temporary | `rotation_parse`, `src/journal/retire.c:60`. | Unflagged means uncertain, not clean. Long source basenames use the independent fallback spelling and cannot prove redundancy by original identity. |
| Flushed temporary → replacement named journal | `journal_rotate`, `src/journal/journal.c:939`. | Rename only after flushing complete temp. On ambiguous rename error, compare replacement fd with named inode; adopt the replacement if rename actually occurred, preventing writes to an unlinked old inode. |
| Renamed checkpoint → durable named checkpoint | `journal_rotate`, `src/journal/journal.c:954`. | Adopt new fd, reset transport, then fsync parent. Barrier failure leaves `directory_pending`, sticky IO and zero public durable sequence/bytes. `journal_retry` must repeat the directory barrier before acknowledgement. |
| Dead clean temp → absent | `sweep_rotations`, `src/journal/retire.c:147`. | Under exclusive directory flock, verify regular temp and matching original generation; unlink; fsync directory after sweep changes. No content scan. |
| Dead failed/unknown temp → permanent reserved set-aside | Same function, `:163`. | Rename with `RENAME_NOREPLACE`; fsync directory. Reserved namespace is excluded from later scans, caps and automatic discovery. This preserves a possible sole checkpoint. |
| Named original → partial retained copy | `retain_async_worker`, `src/journal/journal.c:1079`, or blocking save retention at `:1046`. | Create `.sublimite-base-*`; reflink or bounded copy. Partial file is not yet named by journal records. Kill leaves an unreferenced artifact. |
| Partial retained copy → durable private generation | `retain_generation`, `src/journal/journal.c:1009`. | Validate source identity; clone/copy; capture copy identity and prefix CRC; revalidate source descriptor and pathname; full fsync of retained inode; fsync its parent. |
| Durable private generation → referenced BASE | `editor_retain_tick`, `src/editor/retain.c:29`; `build_checkpoint`, `src/editor/checkpoint.c:173`. BASE(copy), TARGET(real path), text differences. | Only READY copies are referenced. Flush and complete checkpoint rotation establish the durable reference. `retain_used` is updated after successful UI checkpoint. |
| Referenced private generation → unreferenced/removed | `editor_retain_checkpointed`, `src/editor/retain.c:77`; `drop_copy`, `:23`; save finish/cancel below. | Replacing checkpoint must precede deletion. UI retention deletion ignores unlink errors and has no parent-directory barrier. Save finish/cancel use parent sync after unlink. |
| Abandoned session → discovery candidate | `discover_source`, `src/editor/recovery.c:650`. | Only nonempty regular `session-*` entries without dots; exclude current destination; take source’s exclusive nonblocking lease. Process newest first with stable pathname tie-breaker. |
| Candidate → eager or deferred | `restore_deferred`, `src/editor/recovery.c:638`. | Read-only cost scan. Charge journal bytes plus full BASE bytes, or mapped acquisition prefix plus DELETE lengths where eligible. No replay/publication for over-bound sources. |
| Candidate → ordinary set-aside | `restore_source`, `src/editor/recovery.c:503`; `editor_set_aside`, `:479`. | Changed BASE triggers tolerant replay; conflicted edits stay in source. Other discovered-source failure can set source aside. Rename uses `RENAME_NOREPLACE`; caller attempts directory fsync. Several errors are deliberately ignored. |
| Successful eager install → source retired | `restore_source`, `src/editor/recovery.c:555`. | Complete recovered CURRENT checkpoint first; unlink old session and its lock name; fsync directory; then remove retained BASE artifacts in the source directory. |
| Eligible artifact → `.retired` → absent | `remove_verified`, `src/journal/retire.c:258`. | Recheck identity; rename without replacement; fsync directory; recheck renamed inode; unlink; fsync again. Kill leaves original or already-proven discardable retired bytes. |
| Empty owned journal → absent | `close_journal`, `src/journal/journal.c:822`. | Flush; cancel/wait active journal job; verify empty fd still names same inode; unlink; attempt parent sync. Nonempty journals remain as hot-exit state. |
| Unused generated lease → absent | `journal_session_release`, `src/journal/session.c:65`; `sweep_leases`, `src/journal/retire.c:286`. | While holding lease, verify journal and dotted variants absent and lock inode still named, then unlink. Release path does not fsync directory. |
| Set-aside → manually reconstructed target | `journal_manual_replay`, `src/journal/manual.c:88`. | Read-only source scans; operator-supplied target bytes; validate source/target identities; write private target-side temp; fsync temp; recheck identities; rename target; fsync parent. Source journal remains unchanged. |

Ordinary edited/conflicting/corrupt set-asides have no automatic acknowledgement transition. Reserved rotation set-asides are never reconsidered. Manual replay also leaves its source intact. Thus those states intentionally require operator cleanup; successful manual recovery alone does not release them.

Retirement’s byte cap is conditional, not a cap on total recovery storage. `JOURNAL_ARTIFACT_BYTES` is `32 × (4096 + strlen("base")) = 131200` bytes (`src/journal/journal.h:377`). Referenced, uncertain, edited or conflicting artifacts remain pinned above it. Any reserved rotation artifact makes the BASE-reference inventory uncertain (`src/journal/retire.c:332`).

**Buffer states**

| Axis | States and meaning |
|---|---|
| Savedness | Clean iff `!saved_lost && history_cursor == saved_cursor`; dirty otherwise. Frame dirtiness and the presence of journal INSERT/DELETE records are different concepts. `src/editor/buffers.c:599`, `:1014`. |
| Acquisition | Untitled/absent target; prefix preview with `pending_open`; attached COPY original; attached MMAP original; stale/faulted source. |
| Original identity | `origin` unset, or identity of the generation installed as ORIGINAL. It remains the original generation after save. `src/editor/buffers.c:180`. |
| Recovery BASE | Empty BASE; named mutable BASE; durable journal BASE substituted with retained copy; new saved BASE after finish; BASE not yet recorded for a newly prepared buffer. |
| Reference text | No saved snapshot; in-flight `save_snapshot`; completed `saved_snapshot` plus `saved_text` identity and optional address map. `src/editor/checkpoint.c:67`, `src/editor/session.c:426`, `:531`. |
| Retention | IDLE → RUNNING → READY or FAILED. READY additionally has referenced/unreferenced status (`retain_used`). FAILED persists until BASE identity changes. |
| Recovery installation | Eager materialized gap text; eager mapped op-only replay; deferred closed placeholder; lazy SCANNING; OPENING replacements; installed; rolled back/failed. |
| Tab lifetime | Live; retained closed tab; retired outgoing/evicted buffer; physically destroyed. A retained closed tab is still recovery content. |
| Save | Idle; captured/pending; journal leased for PREPARE; prepared token with file write in flight; replacement needing FINISH; finishing; saved; blocked ambiguous transaction requiring reconciliation. |
| Special buffers | Placeholder and derived buffers have no journal BASE/session leaf. Placeholders park mutations; derived results are read-only. |

COPY acquisition also uses piece mapping lifetime hooks: its backing is private allocated memory, not an externally mutable mmap (`src/file/file.c:936`).

**Buffer transitions and records**

| Transition | Function/location | Persistence and crash purpose |
|---|---|---|
| Prepare named buffer | `editor_buffer_prepare`, `src/editor/buffers.c:146`; `prefix_job`, `src/file/file.c:687`; `open_job`, `:772`. | Worker canonicalizes, rejects nonregular files, validates descriptor/name before and after reads. BASE and origin derive from the same validated prefix. No records until registration. |
| Preview → attached original | `editor_adopt_open`, `src/editor/buffers.c:296`; `file_attach`, `src/file/file.c:936`. | Recheck source; attach backing; apply lazy ops before indexes/snapshots; retire preview. Existing BASE describes intact full file while preview is pending. |
| Clean/dirty → edited | Staging at `src/editor/input.c:80`, `:103`; savedness publication at `src/editor/buffers.c:599`. | INSERT/DELETE, grouped where needed. Staged operations retain buffer storage and fence further input/save publication. |
| Buffer tree → checkpoint program | `build_checkpoint`, `src/editor/checkpoint.c:173`; `editor_text_ops`, `:96`; worker `encode_leaf`, `src/editor/session.c:208`. | BASE, TARGET, text differences, VIEW, TABS, WINDOW. Original identity or saved-snapshot map determines safe kept ranges. Otherwise DELETE-all plus full INSERT text. |
| Dirty mutable BASE → protected retained BASE | `editor_retain_tick`, `src/editor/retain.c:29`. | Asynchronous copy, then synchronous complete checkpoint. Copy itself is not protection until the journal names it durably. |
| Save request → pinned immutable session | `request_buffer`, `src/editor/session.c:402`; `capture`, `:366`. | Pin complete session root and selected snapshot. Defer start until staged journal operations/groups drain. No filesystem mutation on request path. |
| Pinned session → PREPARED save | Savectl worker `src/savectl/savectl.c:259`; `journal_save_prepare`, `src/journal/journal.c:1105`. | Flush journal; validate previous BASE; retain named previous bytes; replace matching BASEs in complete checkpoint; append PREPARED SAVE; rotate durably. Target write cannot begin after prepare failure. |
| PREPARED → file write in flight | Handoff `src/savectl/savectl.c:333`, `:602`; file write at `:273`. | UI copies token, releases journal lease, acknowledges handoff; worker writes fixed snapshot. Later edits may append while write proceeds. |
| Snapshot → replaced target | `transaction_prepare`, `src/file/file.c:1465`; `transaction_commit`, `:1517`. | Target-side temp → bytes and metadata → fsync temp → validate target/source → rename → capture replacement identity → fsync retained parent. Kill should leave old or complete new target. |
| Replaced target → FINISH checkpoint | `editor_save_tick`, `src/editor/session.c:503`; `journal_save_finish`, `src/journal/journal.c:1153`. | Capture CURRENT session; new saved BASE plus post-cutoff deltas; FINISHED SAVE; durable rotation; only then unlink retained previous generation and sync parent. |
| FINISH → clean saved cutoff | `editor_save_tick`, `src/editor/session.c:511`. | Update BASE, saved history cutoff and saved reference. Later edits remain dirty. Peers sharing target are rebased and conservatively marked modified. |
| Failed/ambiguous save → blocked ownership | `src/editor/session.c:493`; savectl model `src/savectl/savectl.c:439`. | Preserve token/artifact when cancellation or finish is still necessary. Further Ctrl+S reconciles the old transaction before starting the requested save. |
| Blocked save → cancellation/current checkpoint | `request_buffer`, `src/editor/session.c:409`; `journal_save_cancel`, `src/journal/journal.c:1190`. | Encode complete CURRENT state, using empty replacement BASE for affected target; repair namespace barrier where possible; rotate fresh checkpoint; unlink retained generation only afterward. |
| Save As → rebound target | `save_as`, `src/editor/commands.c:28`. | Direct synchronous `file_save_write`; capture replacement; rebind controller/path/BASE; update peers; only afterward flush/checkpoint. **There is no PREPARE transaction around this write.** |
| External change → stale source | Watch/check paths `src/file/file.c:1095`, `:1125`, `:1282`; `editor_check_sources`, `src/editor/buffers.c:1009`. | Sticky change invalidates unsafe actions; modified buffers prompt. Clean live buffers may reload through replacement checkpoint. |
| Stale → Keep | `src/editor/buffers.c:1055`; `file_resolve_keep`, `src/file/file.c:1031`. | Reject unsafe mapped backing; capture new BASE; flush/checkpoint retained editor text; roll BASE back on failure; then mark text modified against adopted disk state. |
| Stale/clean refresh → Reload | `editor_reload_source`, `src/editor/buffers.c:944`. | Prepare replacement; stage tab swap and retired old generation; checkpoint; rollback on failure; forget old session leaf only after success. |
| Mapped source → SIGBUS-faulted | `guard_recover`, `src/file/file.c:92`. | Pin registered mapping; increment fault epoch before mapping anonymous zero pages over faulting suffix. Subsequent checks mark stale; saves validate fault/source state. This prevents process death, not preservation of truncated original bytes. |
| Mapped backing → released | `map_release`, `src/file/file.c:287`; `guard_unregister`, `:183`. | Last reference retires guard, waits pinned signal readers, unmaps/closes backing. Tree, snapshots and file/source owners independently retain backing. |
| Eager source → restored buffers | `restore_record`, `src/editor/recovery.c:148`; `install_buffer`, `:295`; `install_session`, `:402`. | Strict BASE validation; materialized text or mapped op log; install live/closed order and views; capture current BASEs; complete CURRENT checkpoint before retiring source. Changed BASE tolerant path opens disk clean and keeps edits aside. |
| Over-bound source → placeholder | `editor_restore_defer`, `src/editor/restore.c:317`. | Empty inert buffer in closed stack holds source path/lease. No BASE/VIEW/TABS for placeholder. Source remains discoverable after placeholder destruction releases lease. |
| Placeholder → lazy scan | `editor_restore_start`, `src/editor/restore.c:349`; `scan`, `:224`. | WORK_BULK read-only scan produces BASE-relative program; whole-text shape materializes/trims on worker. Submission refusal leaves IDLE placeholder; close/reopen retries. |
| Scan → opening replacements | `prepare_all`, `src/editor/restore.c:449`. | Check capacity; open configured targets; apply ops after attachment, gated by origin/BASE identity. No restored buffer is published yet. |
| Replacements → installed session | `install_all`, `src/editor/restore.c:528`. | Recheck capacity; suppress publication; replace placeholder and install others; flush and complete checkpoint; then retire placeholder and source, remove lock name, sync directory, release lease. |
| Lazy install failure → empty ordinary buffer | `rollback`, `src/editor/restore.c:505`; `restore_fail`, `:405`. | Restore old placeholder tab, retire installed replacements, preserve source (set aside only for INVALID), release lease, convert placeholder to registered empty untitled buffer. |
| Closed tab → retired buffer | `editor_close_tab`, `src/editor/buffers.c:540`. | Closed-stack eviction marks buffer retired. Saving buffer remains recovery state until transaction resolves; complete UI checkpoints encode that retained saving buffer from empty BASE. |
| Retired → physically destroyed | `editor_retire_buffers`, `src/editor/buffers.c:118`. | Intended to wait for session/save/frame/worker leases; no journal records directly. Named-file retirement currently cannot reach destruction: finding below. |
| Shutdown | `editor_close`, `src/editor/open.c:13`; `editor_session_close`, `src/editor/session.c:580`. | Flush saves/journal, close controllers, journal and journal pool, then destroy buffers/main pool. This ordering conflicts with asynchronous retention lifetime: finding below. |

**Decision documents versus code**

The requested decision documents were read after tracing the implementation.

- `edit-28u.md:76` says the session builder retains per-record allocation. Current `src/editor/session.c:300` uses the shared two-pass builder; `edit-f3i.md:110` correctly supersedes that statement.
- `edit-fm4.md:57` describes charging every BASE fully and says lazy placeholders are unimplemented. Current cost selection is mapped-aware (`src/editor/recovery.c:597`), and `restore.c` implements placeholders. These passages are historical, superseded by qct/4da.
- `edit-qct.md:81` describes eager recovery as whole-text replacement under forced COPY. Current eager recovery preserves ORIGINAL through logged ops and has mapped op-only installation (`src/editor/recovery.c:301`, `:353`).
- `edit-52o.md:44` says rotation waits for directory inventory. Current `src/journal/journal.c:916` returns BUSY using `LOCK_NB`, as amended by dr9.
- `edit-601.md:33` assumes restore checkpoint failure occurs before journal replacement. That is false for post-rename directory-sync failure or ambiguous rename error (`src/journal/journal.c:945–968`). Rollback must handle an already-replaced journal.
- `edit-fi2.md:35` says recovery already supports retained BASE plus TARGET. That is true of eager recovery; lazy restore’s origin comparison rejects this pair (`src/editor/restore.c:378`).
- `edit-fi2.md:84–86` says replacing checkpoints precede BASE changes for Keep. Keep actually changes `b->base` before checkpoint construction and restores it on failure (`src/editor/buffers.c:1059–1067`). That temporary staging is intentional, but the stated order is inaccurate.
- rfq’s “stable leases remain” is qualified by current generated orphan-lease cleanup. btj, dr9, hw1 and y5x otherwise describe their principal current transitions accurately. Their documented exclusions do not establish safety of adjacent transitions.

**2. Hostile transition findings**

Severity follows the requested reachability definitions. A finding can be BLOCKER without losing bytes—for example, preventing ordinary saves or crashing shutdown.

**F1 — BLOCKER: a live Find Results tab prevents saving ordinary files**

Sequence: open a named editable file; open Find Results; select the file, edit and Ctrl+S.

`publish` omits derived buffers (`src/editor/session.c:121`), but save admission requires the session root’s live count to equal **all** live tabs (`:423`). Find Results is explicitly derived (`src/editor/buffers.c:580`). Admission therefore reports failure before starting the save. Retrying cannot change the mismatch while Results stays open.

The same mismatch affects live recovery placeholders. Published ranks also use unfiltered tab indexes (`src/editor/session.c:183`), while `encode_all` requires a dense ID array (`:283`).

Smallest fix: use the same filtered count/rank/active-index mapping already implemented by `recorded_tabs` in `src/editor/checkpoint.c:254` for session publication and save admission. Removing only the count check leaves sparse-rank encoding failures.

**F2 — BLOCKER: journal teardown can unmap IO callbacks borrowed by a running retention job**

Sequence: a retention job is RUNNING while the current journal prefix is already durable—for example, retention of a new saved BASE while post-cutoff edits remain dirty. Close before that retention finishes.

`journal_retain_start` stores `r->io = &j->io` (`src/journal/journal.c:1097`). `journal_flush` can return immediately when its own accepted prefix is durable (`:813`); it does not join asynchronous retention. `editor_close` then frees the journal (`src/editor/open.c:21`) **before** shutting down/joining the journal pool (`:33`). Buffer retention is stopped still later (`:38`, `src/editor/buffers.c:82`).

A worker completing copy/validation can consequently dereference unmapped `j->io` in `write_at`, `metadata_sync` or `sync_parent` (`src/journal/journal.c:1004`, `:1033`). Pool cancellation after journal close does not protect this interval.

Smallest fix: quiesce all buffer retention jobs before closing the journal or its pool, while retaining the existing referenced-copy preservation policy.

**F3 — BLOCKER: retired named buffers permanently consume buffer slots and file resources**

Sequence: repeatedly open and close different ordinary named files until closed-stack eviction retires old buffers.

`editor_retire_buffers` unconditionally continues while `b->file` is non-NULL (`src/editor/buffers.c:137`). No retirement transition clears/closes that file. Physical destruction is therefore unreachable for every retired named buffer until whole-editor shutdown.

Eventually `add_buffer` finds no free slot (`src/editor/buffers.c:469–471`), despite a mostly empty tab strip. This also retains file descriptors, watches, mappings and arenas.

Capacity derives directly from `live + closed` (`src/editor/open.c:85–88`). Main configures 1000 live slots (`src/main.c:135`); default closed capacity is 16, so the executable reserves 1016 buffer slots. Sequential retirement can exhaust those slots; this is not a claim that 1016 simultaneously live tabs are required.

Smallest fix: provide a nonblocking “file jobs physically finished” retirement check, then close/clear the file and release the buffer once remaining leases are gone. Simply removing the guard would introduce blocking `file_close` waits into maintenance.

**F4 — BLOCKER: a failed checkpoint name barrier has no editor-level recovery transition**

Sequence: checkpoint rename succeeds; its parent fsync fails; storage subsequently becomes healthy; retry flush/checkpoint or close.

Rotation sets `directory_pending`, zeroes public durable sequence and returns IO (`src/journal/journal.c:965–968`). Repair exists in `journal_retry` (`:551`). Ordinary editor flush never calls it (`src/editor/editor.c:882–913`).

Instead, flush sees IO and calls `editor_checkpoint`. Rotation admission then returns BUSY because accepted and durable sequences differ (`src/journal/journal.c:904`). Repeated flushes cannot clear that condition. A close entering the BUSY retry branch repeatedly postpones itself (`src/editor/editor.c:693–698`).

Smallest fix: in off-path editor recovery/flush, attempt `journal_retry` for sticky IO before deciding whether transport draining or a complete fresh checkpoint is required. Preserve fresh-inode repair for writeback loss.

**F5 — MAJOR: the second save of a mapped original can fail permanently after the first save’s rename**

Sequence: open a file acquired as MMAP; edit and save successfully; edit again; save again.

The save controller retains a `file_source` whose identity is captured once (`src/file/file.c:962–973`; `src/editor/session.c:340`). `file_source_validate` compares the entire identity, including ctime/metadata (`src/file/file.c:349–360`, `:641–651`).

The first save renames over the original pathname (`src/file/file.c:1536`), changing the old inode’s ctime when its link is removed. The controller continues borrowing that old mapping and old identity. `file_resolve_keep` deliberately tolerates this retired-inode metadata change (`src/file/file.c:1041–1047`), but does not refresh the controller’s `file_source`. The next savectl worker rejects it at initial validation (`src/savectl/savectl.c:260`).

Smallest fix: refresh the source authorization after a successful own replacement, explicitly validating that the retained backing’s bytes/fault epoch remain safe. Do not globally ignore ctime; that would weaken external-rewrite detection.

**F6 — MAJOR: lazy recovery rejects valid retained BASE/TARGET pairs**

Sequence: produce a checkpoint with BASE pointing to a durable retained copy and TARGET pointing to the real file; make the session exceed the eager cost bound; reopen its placeholder.

Lazy preparation opens TARGET (`src/editor/restore.c:455–457`). Applying the program requires the opened target’s `origin` to equal the retained copy’s BASE identity (`:378`). The identities necessarily differ because retention creates a different inode. The buffer becomes a conflict, its operations are skipped, and it is marked clean (`:488`). The source is set aside (`:623`) although all required bytes are available.

This affects interrupted-save retained BASEs as well as fi2 copies. A single BASE exactly at `retain_limit` is sufficient: the limit equals 105000000 bytes (`src/editor/open.c:75`), while eager cost adds the nonempty journal to that full BASE (`src/editor/recovery.c:642–648`), necessarily exceeding the same bound.

Smallest fix: lazy recovery must open/replay the recorded BASE generation and bind the resulting text to TARGET separately, following eager recovery’s retained-generation semantics. TARGET identity cannot authorize operations expressed against a different BASE inode.

**F7 — MAJOR: eager materialized recovery can publish edits applied to rewritten BASE bytes**

Sequence:

1. Strict replay validates mutable BASE.
2. Another process rewrites that file before/during `restore_record`’s full read.
3. Recovery applies old journal offsets to the newly read bytes.
4. Installation opens the new generation, falls back to the materialized text, checkpoints it and removes the source.

BASE checking happens before delivery (`src/journal/journal.c:255–260`). The callback subsequently opens/reads BASE without validating its generation afterward (`src/editor/recovery.c:200–210`). At install, an origin mismatch prevents op replay but selects whole-text fallback (`:353–362`); it does not reject the mixed/wrong text. BASE is recaptured and the result published (`:438–444`), then source unlinked (`:556`).

Smallest fix: validate the actual opened BASE descriptor and pathname across materialization and reject/retry if they differ from the recorded generation. Do not treat an identity mismatch at install as sufficient justification to trust materialized mutable-source bytes.

**F8 — MAJOR: Save As can destroy a modified closed peer’s recovery BASE before checkpoint protection**

Sequence: modify a large named buffer above the retention limit; close it into retained history; open another buffer; Save As onto the closed buffer’s pathname; kill after target rename but before the session checkpoint.

Save As checks modified **live** peers only (`src/editor/commands.c:36`). It then replaces the target directly (`:68`), without `journal_save_prepare`. Closed-peer rebasing happens afterward (`:120`), and durable checkpoint publication later still (`:141`).

The old closed buffer’s journal can contain differences against the overwritten named BASE. Its mapped old inode survives only in process memory; SIGKILL removes that last access. The journal cannot reconstruct kept bytes.

Smallest fix for this concrete hole: extend the refusal check to modified retained closed peers before any target write. Transactional destination retention remains necessary for broader Save As crash guarantees.

**F9 — MAJOR: transient asynchronous retention failure never retries for the same BASE**

Sequence: first-modification retention fails because of temporary IO/space pressure; clear the pressure; continue editing the same BASE; external rewrite followed by kill.

RUNNING becomes FAILED (`src/editor/retain.c:40`). FAILED changes back to IDLE only if BASE identity changes (`:50–52`). Further mutations and healthy storage do not retry retention. The session remains dependent on the mutable original indefinitely.

Smallest fix: permit a bounded retry on a later mutation or explicit retry event while the source generation still validates, using the existing mutation-based retry pattern rather than retrying every tick.

There are also explicit unprotected states independent of this defect:

- Before copy completion **and durable reference publication**, accepted edits depend on the mutable original.
- BASEs above 105000000 bytes are never retained (`src/editor/retain.c:54–55`).
- A cancelled-save checkpoint can remove retained protection while retention bookkeeping still says used, as acknowledged in `edit-fi2.md:141`.

These are real external-rewrite-plus-kill loss windows, not protection supplied by the current journal format. The smallest local improvement to the cancellation case is to update reference bookkeeping after every durable save/cancel checkpoint so retention publication can run again.

**F10 — BLOCKER: automatic retention publication performs unbounded waits on the UI thread**

Sequence: ordinary named-file edit; retention becomes READY; UI reaches the maintenance gate while journal IO or its worker queue is stalled.

`editor_retain_tick` calls blocking `journal_flush` and `editor_checkpoint` directly (`src/editor/retain.c:68–69`). Flush polls until worker completion with no deadline (`src/journal/journal.c:808–820`); checkpoint rotation also waits for temp writes and syncs. No queued input at entry does not prevent input arriving during the wait.

Save As has the same problem for the entire file write (`src/editor/commands.c:68`), becoming especially visible for large files.

Smallest fix: run these existing filesystem transactions under a worker-owned journal lease and adopt completion in maintenance, reusing the save controller’s ownership pattern. This requires no format or piece-tree rewrite.

**F11 — MAJOR: lazy recovery discards the saved active-tab index**

Sequence: a deferred session has multiple live tabs and a non-first active tab; reopen its placeholder.

The lazy scanner retains TABS IDs but discards the active index (`src/editor/restore.c:118–125`). Installation always selects the first restored buffer (`:592`). Eager recovery preserves the source active index (`src/editor/recovery.c:432`).

Result: recovery shows another buffer on screen even though the saved order and views exist.

Smallest fix: retain the active source ID/index during scan, reconcile it through deduplication, and select its installed buffer after publication.

**F12 — MINOR: reserved set-asides permanently pin unrelated orphan BASE cleanup**

Sequence: a failed/unflagged rotation temp is set aside; manual recovery succeeds; leave the reserved set-aside present.

Every reserved rotation name makes inventory uncertain (`src/journal/retire.c:332`), and uncertain inventory makes all unreferenced retained BASEs ineligible (`:397`). Reserved set-asides are skipped forever by the rotation sweep (`:153`). Manual replay never removes its source (`src/journal/manual.c:88`).

This is conservative preservation, but the condition cannot clear through any recovery operation presently offered. Artifact storage can consequently grow without the nominal cap reclaiming otherwise orphaned BASEs.

Smallest fix: provide an explicit verified retirement/acknowledgement operation for manually resolved sources. Do not add age-based deletion of possible sole copies.

**3. Coverage gaps**

These are gaps in the named suites, not claims about every test elsewhere in the repository. Existing synthetic durable-image tests provide useful coverage that SIGKILL alone cannot provide.

| Transition | Existing coverage | Missing kill/failure coverage |
|---|---|---|
| Lease creation before journal visibility | `journal_retire_test.c:316`, orphan lease cases at `:346`. | Kill between lock creation, flock, inode verification and journal creation; creation-directory barrier failure through the launcher lifecycle. |
| Mutation → staged journal record | `dataloss_test.c:154` kills after a completed editor turn’s accepted edit; hot-exit captures at script boundaries. | Kill inside mutation-to-stage/append transition; corresponding failure recovery before a containing frame completes. |
| Group boundaries/PAD | `journal_group_test.c:129` exhausts byte cuts and corrupt COMMIT bytes; actual action kills at `:175`; `journal_kill_test.c:182`. | Combined group framing with editor-level writeback-loss repair and checkpoint publication failure. Basic torn-group coverage is strong. |
| Rotation temporary lifecycle | `journal_retire_test.c:421` kills while held immediately before publication, including clean/failed/unflagged cases. | Kill during temp creation, partial checkpoint construction, data-sync completion, replacement adoption and post-rename directory barrier. |
| Journal barriers | `journal_test.c:332` injects creation, writeback, rename and directory failures and explicitly calls `journal_retry`. | Same post-rename directory failure routed through `editor_flush`, lazy rollback, automatic retention publication and close retry. This is why F4 is not caught. |
| Async fi2 retention | `dataloss_test.c:798` waits until a retained path is present in the journal before killing; lifecycle at `:851` waits for settled cleanup. | Kill/cancel at RUNNING, READY-but-unreferenced and publication stages; transient failure then retry; close while RUNNING; publication by save checkpoint rather than UI checkpoint. |
| Retained-copy faults | `journal_test.c:852` injects short writes, mutation and sync failures into **save preparation** retention. | Equivalent asynchronous `journal_retain_start` lifetime, cancellation and retry tests. |
| Repeated saves | `editor_session_test.c:102` exercises repeated small COPY saves. `editor_checkpoint_test.c:674` performs one mapped large save, then edits/checkpoints. | Second and third saves of the same mapped backing, including after Keep or own rename; catches F5. |
| PREPARE → target replacement | `dataloss_test.c:195` kills at file save hooks; `editor_session_test.c:365` tests interrupted saves. | Retained PREPARED source forced through lazy restoration; kill/failure during PREPARE and FINISH checkpoint namespace publication itself. |
| In-flight UI checkpoint | `editor_checkpoint_test.c:766` exits after checkpoint while target write is held, or after finished save. | Release held target write **after** intermediate checkpoint, then kill after rename before FINISH; preservation of PREPARED BASE/SAVE obligations across that intermediate checkpoint. |
| Save cancellation/retirement | `editor_session_test.c:238` injects pre/post-target-rename failures and exercises reconciliation. | Kill during cancel checkpoint publication, retained-artifact unlink, and its parent fsync; restoration/protection after cancellation followed by external rewrite. |
| Save As | `dataloss_test.c:687` tests modified live-peer refusal and clean-peer completion. | Modified closed peer; kill immediately after target rename before any replacement checkpoint; mapped source and failing final checkpoint. |
| Eager materialization | Changed BASE before recovery and ordinary round trips are covered. | Rewrite between strict BASE check, callback read and install; mixed-generation read rejection; F7. |
| Mapped fault/recovery | Named suites exercise mapping acquisition and some file mailbox paths. | Actual SIGBUS during recovery apply/checkpoint/save, followed by kill and byte comparison. File-module tests outside this requested set should be assessed separately. |
| Deferred/lazy install | `editor_session_test.c:1244` tests no-journal source preservation; `:1265` fails temp creation via permissions; `:1304` injects scan NOMEM; `:1352` tests duplicate TABS IDs. | Retained BASE differing from TARGET; post-rename checkpoint failure; failure after individual swaps; kill during source unlink/fsync; multi-buffer active-index restoration. |
| No-journal lazy restore | `editor_session_test.c:1255–1262` asserts live tab and unchanged source. | It does not assert recovered edit bytes. The known origin-unset path can return clean disk text while the test passes. |
| Session publication exclusions | Placeholder checkpoint exclusion is checked at `editor_session_test.c:1022`. | Save another buffer while a placeholder or derived Results tab is live; dense rank/active mapping after exclusions; F1. |
| Named-buffer retirement | Saving-buffer eviction and retained closed recovery are covered. | Repeated named-file open/close beyond `live + closed`, with fd/watch/backing/slot reclamation assertions; F3. |
| Ordinary artifact retirement | Healthy retirement, pinned edits, leases, conflicts and cap ordering: `journal_retire_test.c:64–155`. | Kill/fail after `.retired` rename, first directory sync, unlink and final sync; verify restart reaches the correct preserved/deleted state. |
| Manual replay | `journal_retire_test.c:236` injects target-temp write failure using file-size limit and checks source preservation. | Temp fsync, rename and parent fsync failure/kill; concurrent source or target rewrite at final validation boundaries. |
| Shutdown | Healthy settled saves/retention are exercised. | Retention worker stopped at each callback/barrier while editor closes; already-durable journal with independent retention still running; F2. |

`editor_hot_exit_test.c:88` builds its recovery oracle from untitled buffers and kills after successful setup/action boundaries (`:146`). It verifies text, views, tab order and geometry well, but does not exercise named BASE replacement, retained originals, mapped-source authorization, lazy restore or filesystem failure transitions.