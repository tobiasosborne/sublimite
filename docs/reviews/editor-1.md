23 findings — 7 BLOCKER, 16 MAJOR, 0 MINOR.

Static review of the editor/main implementation landed in `65990f5`, its contracts, and its test drivers. No files were changed. Builds and integration tests were not run because they create artifacts and journals prohibited by the read-only constraint. Timing concerns below are concrete work-bound or measurement defects, not newly measured gate results.

## 1. BLOCKER — Native window events can overtake earlier keystrokes

Location: [src/editor/editor.c:74](/home/tobias/Projects/editor/src/editor/editor.c:74); supporting code: `src/x11/x11.c:258`, `src/x11/x11.c:359`.

X11 dispatch queues translated keys in `x11_input`, but delivers CLOSE, FOCUS, RESIZE and EXPOSE directly to the editor callback. The platform drains raw events before popping its translated-key queue. Consequently, the editor receives these window events before keys that preceded them on the X connection.

**Repro:** Send a key press followed by WM_DELETE_WINDOW before the next editor pump. Dispatch queues the key internally, immediately injects CLOSE into the editor queue, then injects the key. The editor processes CLOSE, sets `quit`, and exits with the earlier key unprocessed. Shutdown flushes mutations, not pending keystrokes.

**Fix:** Deliver every application event through one ordered queue, or deliver pending translated events before any direct callback. Add native batched key/close, key/focus and key/resize ordering tests. The current native test settles after each key, preventing this interleaving.

## 2. BLOCKER — The stop request does not stop draining; bursts lose keys

Location: [src/editor/editor.c:77](/home/tobias/Projects/editor/src/editor/editor.c:77); supporting code: `src/x11/x11.c:359`, `src/x11/input.h:11`.

The callback calls `plat_quit` to request a local pump return, but `drain()` never checks that flag. It continues draining raw events and queued callbacks to quiescence.

There are two failure modes:

- The 256-event X11 input queue fills before queued events are delivered. `x11_q_push` failures are ignored by dispatch, silently dropping keys.
- Direct events can fill the 1024-event editor queue. The next callback sets a sticky error, and `editor_step` returns before processing the already queued events.

**Repro:** Queue more than 128 ordinary press/release pairs before pumping. The raw drain can produce more than 256 translated events before popping any. Alternatively, flood Configure/Expose events beyond the editor capacity.

**Fix:** Bound the native drain by events/time, honor the stop request, and stop consuming raw events before downstream capacity is exhausted. Preserve keys; coalesce only events whose semantics permit it. Test bursts exceeding both capacities, mixed with repeat and window events.

## 3. BLOCKER — A successfully presented edit can disappear on process crash

Location: [src/editor/editor.c:129](/home/tobias/Projects/editor/src/editor/editor.c:129), [src/editor/editor.c:246](/home/tobias/Projects/editor/src/editor/editor.c:246); supporting code: `src/journal/journal.c:464`.

Mutation first enters editor-private staging, then journal-private staging after submit. Neither is crash recovery storage. A small journal batch is not dispatched until approximately one second has elapsed, unless enough records accumulate.

**Repro:** Open an empty editor with journaling, flush setup, and install an `on_present` hook that sends SIGKILL to the process after typing `x`. T4 has succeeded, but `journal_pump` has not yet run. Replay contains no `x`. Even without the hook, a lone presented edit remains vulnerable during the batching delay. Waiting for rendering can extend the earlier editor-staging interval further.

**Fix:** Make journal persistence independent of rendering. Track the written recovery cutoff and establish the required write/durability barrier before acknowledging edits. Add kill tests after mutation, submit, present, journal append and worker write; graceful `editor_flush` tests cannot establish this contract.

## 4. BLOCKER — Journal append failure discards the unjournaled suffix

Location: [src/editor/input.c:70](/home/tobias/Projects/editor/src/editor/input.c:70), [src/editor/editor.c:297](/home/tobias/Projects/editor/src/editor/editor.c:297).

`editor_journal_staged` stops at the first journal error and unconditionally clears both staging counters. Operations from the failed operation onward are lost. These operations have already changed the tree and successfully submitted a frame.

The editor’s staging-capacity check does not reserve capacity in the actual journal. JOURNAL_FULL is therefore possible after successful mutation/submission.

**Repro:** Stall journal I/O while producing edits until the current journal batch fills. On the failing append, later staged operations are discarded. `editor_flush` returns the sticky error; `main` then closes the editor and frees the only remaining copy of those edits. A replacement can also leave only its insertion record accepted while its deletion record fails.

**Fix:** Retain the unaccepted staging suffix and its progress index. Reserve journal admission before mutation, or retain the entire current state for retry/checkpoint recovery before freeing it. Surface suspension immediately. Add editor-level FULL/write/sync failure tests and compare recovery bytes with the acknowledged edit model.

## 5. BLOCKER — The journal BASE can describe a different generation than the buffer

Location: [src/editor/open.c:73](/home/tobias/Projects/editor/src/editor/open.c:73), [src/editor/open.c:117](/home/tobias/Projects/editor/src/editor/open.c:117).

The buffer is attached first. After backend initialization, `journal_capture_base` independently reads the current pathname and records its identity. Nothing verifies that this identity and prefix describe the version copied into the tree.

**Repro:** Open a small file containing `aaaa`. After attachment, while backend initialization runs, replace its contents with `bbbb`. BASE captures `bbbb`, while the editor still contains `aaaa`. Insert `x` at offset zero and flush. Recovery validates BASE against `bbbb` and produces `xbbbb`, although the acknowledged buffer was `xaaaa`.

**Fix:** Bind BASE to the exact generation used to initialize the tree. Preserve and validate the open identity and content fingerprint across loading and journal setup; fail/restart setup on mismatch. Retain the original generation where recovery requires it. Add an initialization race test using a delayed backend.

## 6. BLOCKER — Cursor and selection changes can leave partially rendered rows stale

Location: [src/editor/editor.c:52](/home/tobias/Projects/editor/src/editor/editor.c:52).

When layout is busy, `editor_refresh_cursor` schedules extra work only if the affected rows extend outside `[lay.row, lay.row_end)`. The current row may already be partly rendered. Changing cursor/selection styling does not repaint cells already visited in that row.

**Argument:** Pause layout midway through row zero after it has painted the caret at byte zero. Move Right to byte one without changing the viewport. Both cursor rows are zero, so no extra relayout is scheduled. Layout resumes after byte one and submits the old caret at byte zero. A partially rendered selection has the same problem. Long graphemes/rows provide real opportunities for such a turn boundary.

**Fix:** Restart the partially rendered affected row, or always schedule a subsequent relayout when style changes overlap it. Add a controlled interleaving test that inspects submitted cells, including caret movement, selection, blink and focus changes. Buffer/cursor assertions alone cannot detect this.

## 7. BLOCKER — A long first line permanently hides subsequent visible lines

Location: [src/editor/open.c:95](/home/tobias/Projects/editor/src/editor/open.c:95); supporting code: `src/layout/layout.c:429`, `src/layout/layout.c:457`.

The editor never initializes, attaches, requests or routes layout checkpoints. Without a checkpoint, layout stops scanning a line after 64 KiB. It treats that stop as a row ending without a newline, sets the next row to `LAYOUT_VOID_ROW`, and blanks all subsequent rows.

**Repro:** Open `"a" × 70000 + "\nvisible\n"` in a multirow viewport. The first row displays its clipped prefix, but `visible` is absent. No checkpoint request is ever issued to correct the frame. This violates G5’s correct-first-viewport contract for arbitrary line lengths.

**Fix:** Integrate snapshot checkpoint jobs and their mailbox adoption/relayout path, or obtain exact line ends through bounded piece queries. Add submitted-grid tests with long lines followed by ordinary lines. The editor fuzzer’s 4096-byte model cannot reach this case.

## 8. MAJOR — Open waits for whole-file work and scans mapped files on the UI thread

Location: [src/editor/open.c:58](/home/tobias/Projects/editor/src/editor/open.c:58), [src/editor/open.c:76](/home/tobias/Projects/editor/src/editor/open.c:76).

Open waits for `file_open_ready`, so copy-mode files finish their entire background read before any viewport can submit. Attachment then copies and indexes that content synchronously. For mapped files, `piece_line_count` resolves all lazy newline counts by scanning the entire mapping on the UI thread.

**Repro:** Open a cold 1 GB mapped file. Before window/input setup, the UI touches the whole file, including foreground major faults. A file below the copy threshold similarly waits for its complete read and synchronous attachment.

This directly contradicts G5’s bounded-prefix publication and “publish before copy/index” requirement. Putting the scan outside the G1 measurement does not satisfy G4/G5 or the UI-work contract.

**Fix:** Publish the first viewport from the prefix immediately. Move full counting/copy preparation to workers and adopt results incrementally through mailboxes. Add whole-open warm/cold benchmarks and a test proving that a stalled remainder read does not delay the first viewport.

## 9. MAJOR — Every edit traverses the complete undo history

Location: [src/editor/input.c:89](/home/tobias/Projects/editor/src/editor/input.c:89); supporting code: `src/undo/undo.c:357`.

`remember()` calls `undo_get_stats()` after every mutating key. That function traverses every active undo/redo record to count groups. This makes ordinary typing proportional to retained history.

**Argument:** With the default history reservation, a keystroke can traverse tens of thousands of records. Filling history incurs quadratic cumulative work. Larger accepted `history_keys` values increase the uninterrupted traversal. This traversal is outside the recorded view/layout slice timings.

**Fix:** Maintain group counts incrementally or return bounded eviction/group information from undo operations. Update the editor ring from that information. Test typing with a full history, rather than only the first 10,000 keys of an initially empty log.

## 10. MAJOR — Each ordinary edit updates a file-sized index table synchronously

Location: [src/editor/input.c:38](/home/tobias/Projects/editor/src/editor/input.c:38); supporting code: `src/lineidx/lineidx.c:267`, `src/lineidx/lineidx.c:365`.

`changed()` polls and edits the line index on the typing path. Polling can adopt every completed chunk in one call. An edit shifts every later chunk start synchronously.

**Argument:** A 10 GB file has roughly 152,588 decimal-size 64 KiB chunks. Inserting one byte near its beginning updates almost the entire table before returning to the input check. The benchmark edits at 90% of the file, substantially reducing this cost. Completed index work can also arrive as one large adoption pass.

**Fix:** Use lazy offset deltas/an augmented structure, or slice index adoption and maintenance independently of mutation acknowledgement. Add beginning-of-file and completed-backlog cases at the maximum gated file size.

## 11. MAJOR — The 0.5 ms budget excludes indivisible mutation, replay, resize and submit work

Location: [src/editor/editor.c:220](/home/tobias/Projects/editor/src/editor/editor.c:220), [src/editor/editor.c:243](/home/tobias/Projects/editor/src/editor/editor.c:243), [src/editor/input.c:134](/home/tobias/Projects/editor/src/editor/input.c:134).

The deadline is checked after a whole action returns. Newline counting scans an entire selection, undo replay uses the unlimited legacy API, resize clears the entire new grid, and submit validates every active cell. None can yield at the deadline.

`longest_slice_ns` measures selected view/layout calls, omitting these operations and therefore understating the actual uninterrupted UI work.

**Repro:** Select a very large range and delete it: `newlines()` scans the entire range even if undo later rejects the edit for NOMEM. Resize to a large admitted grid: initialization clears all cells and submit validates them synchronously. Neither is captured by the advertised slice maximum.

**Fix:** Introduce resumable mutation/replay and resize work, with appropriate frozen-contract amendments where needed. Bound submission validation or constrain admitted grids to a verified bound. Measure every uninterrupted UI segment and test actual input-check intervals under these commands.

## 12. MAJOR — A successful large deletion cannot be undone with journaling enabled

Location: [src/editor/input.c:169](/home/tobias/Projects/editor/src/editor/input.c:169), [src/editor/private.h:11](/home/tobias/Projects/editor/src/editor/private.h:11).

Replay refuses any restored insertion exceeding the fixed 512 KiB staging buffer. Normal deletion has no corresponding admission limit. The user can therefore make a successful edit that its own undo history cannot replay.

**Repro:** Select a 600 KiB ASCII word using Ctrl+Shift+Left, delete it, then press Ctrl+Z. The delete fits the default arena. Undo fails the staging preflight with `EDITOR_ERR_MEMORY`, and the loop stops permanently.

**Fix:** Stream replay journaling through bounded staging, or reserve enough transaction capacity before admitting the original operation. Add large-delete/undo tests with journaling enabled, including the G9-scale workload.

## 13. MAJOR — Typing-burst undo grouping is disabled

Location: [src/editor/input.c:138](/home/tobias/Projects/editor/src/editor/input.c:138), [src/editor/input.c:97](/home/tobias/Projects/editor/src/editor/input.c:97).

Every mutating key begins and ends an explicit group. Both undo operations break automatic burst grouping. Backspace/Delete also call `undo_break_burst` before mutation.

**Repro:** Type `abc` rapidly, then press Ctrl+Z. Only `c` is undone. Holding Backspace similarly creates separate undo groups for every repeat. The decision document acknowledges this choice, but it does not satisfy the requested typing-burst behavior or undo’s burst contract.

**Fix:** Use automatic grouping for adjacent same-kind typing/deletion, driven by event timing. Keep replacements explicit and atomic. Make the replacement-history ring track groups rather than keys. Add burst, timeout, movement, focus and repeat grouping tests; the fuzzer currently models one history entry per key.

## 14. MAJOR — Continuous input can starve layout and submission

Location: [src/editor/editor.c:225](/home/tobias/Projects/editor/src/editor/editor.c:225).

Queued input always wins over layout. If input consumes the turn budget, no layout work runs. Mutations arriving while layout is busy restart it, further postponing completion.

**Repro:** With journaling disabled, keep the injected queue nonempty by replenishing events consumed each turn. Mutations continue, but layout never receives a branch and `submitted_sequence` remains unchanged indefinitely. With journaling enabled, eventual staging exhaustion forces progress, but only after many edits have waited.

**Fix:** Bound input batches and guarantee layout/submission progress by a frame deadline. Preserve event order while finishing a coherent frame. Add sustained-arrival tests asserting bounded ingress-to-containing-frame latency; a finite batch followed by `settle()` does not test this.

## 15. MAJOR — The piece allocator discards reusable storage without reclaiming it

Location: [src/editor/open.c:8](/home/tobias/Projects/editor/src/editor/open.c:8).

The allocator uses a bump arena, while `piece_free` ignores every free request. Piece’s internal slab trimming removes wholly free slabs from its pool and calls that hook. Their storage remains allocated in the editor arena but becomes unreachable for reuse.

**Argument:** Repeated fragmentation/collapse or replacement undo/redo can retire slab storage while leaving current text and retained history bounded. Future reservations allocate new slabs instead of reusing the discarded ones. Owned memory and arena consumption grow with trimming events, eventually causing NOMEM despite available dead storage.

**Fix:** Provide recycling storage for fixed-size piece allocations, with a thread-safe free path compatible with snapshots. Add an editor-level memory-bound test over repeated undo/redo and tree growth/collapse. Counting malloc calls does not detect this G10f failure.

## 16. MAJOR — Mapped-file change detection is never integrated

Location: [src/editor/editor.c:156](/home/tobias/Projects/editor/src/editor/editor.c:156), [src/editor/open.c:124](/home/tobias/Projects/editor/src/editor/open.c:124).

The editor never starts a file watch, polls change notifications, checks file identity on focus, or checks before its initial bulk index. It also never observes `file_changed` after the SIGBUS protection reports truncation.

**Repro:** Open a mapped large file and truncate or overwrite it externally. The file layer can expose zeros and mark the mapping changed, but the editor continues displaying/editing it with cached line information. It neither marks the source stale nor offers the required reload/keep decision.

**Fix:** Integrate notification routing and bounded identity checks at the required boundaries. On change, suspend use of stale source assumptions, cancel affected jobs and surface reload/keep. Add mapped overwrite/truncate tests, including changes while indexing.

## 17. MAJOR — A pending journal job causes periodic idle polling

Location: [src/editor/editor.c:191](/home/tobias/Projects/editor/src/editor/editor.c:191).

Once the journal sync deadline expires, an accepted-but-not-durable journal schedules a new UI timeout every 5 ms. This continues while the worker is already active and cannot make additional progress through `journal_pump`.

**Repro:** Stall a journal write/sync beyond its deadline, then unfocus the null-backend editor. With blinking disabled and no input, `editor_step` repeatedly wakes at roughly 5 ms intervals until journal completion. A queued journal behind slow bulk work has the same polling behavior.

This is an editor-originated G11 problem independent of the known raster blink miss.

**Fix:** Expose journal scheduling state. When a job is active or queued, wait for its mailbox completion instead of repeatedly polling an expired deadline. Test unfocused/after-idle behavior with a deliberately delayed journal job.

## 18. MAJOR — The G1 benchmark measures dequeue latency and removes queue contention

Location: [src/editor/input.c:228](/home/tobias/Projects/editor/src/editor/input.c:228), [bench/editor_bench.c:164](/home/tobias/Projects/editor/bench/editor_bench.c:164).

The ingress callback receives `key_ns`, created when the editor drains the key. The benchmark subtracts this timestamp, rather than injection/native ingress time. Time waiting in the editor queue is excluded.

Each sample also waits for the preceding frame’s T5/T6 before injecting another key, and indexing is allowed to finish before measurement. Required active/queued bulk workloads are absent.

**Repro:** Delay processing an injected key by 100 ms before calling `editor_step`. The reported sample still starts at dequeue. Likewise, input waiting behind an active frame is absent from the fixture.

**Fix:** Timestamp injection/native ingress and retain a timestamp per sequence through coalescing. Measure every key to its containing T4. Include arrivals during active frames and the required progressing bulk-worker mix. Report dequeue-to-submit separately.

## 19. MAJOR — The A benchmark renders one quarter of its advertised pixel area

Location: [bench/editor_bench.c:106](/home/tobias/Projects/editor/bench/editor_bench.c:106), [bench/editor_bench.c:144](/home/tobias/Projects/editor/bench/editor_bench.c:144).

`font_ascii_cell()` returns the 30 px atlas’s 16×30 cell. The benchmark computes 180 columns and 60 rows for “2880×1800”, but omits `font_px`. `editor_open` defaults to the 15 px atlas’s 8×15 cell.

The actual viewport is therefore **1440×900**, for both G1 and G11.

**Argument:** `180 × 8 = 1440`; `60 × 15 = 900`. Raster area and strip geometry are substantially smaller than the labeled A fixture. The printed A dimensions cannot establish the intended gates.

**Fix:** Set `font_px = font_ascii_px()` or derive dimensions from the atlas actually selected. Assert the backend’s pixel dimensions equal the requested fixture and print them from runtime configuration.

## 20. MAJOR — TRACK suppresses structural failures, and normal benchmark runs disable gates

Location: [bench/editor_bench.c:179](/home/tobias/Projects/editor/bench/editor_bench.c:179), [bench/editor_bench.c:136](/home/tobias/Projects/editor/bench/editor_bench.c:136); `bench/editor_bench.args:1`.

The shipped args file supplies `--track`, so `make bench` ignores timing misses. More seriously, TRACK also suppresses allocation violations and idle wake-policy violations because they are combined with timing misses before returning zero.

Mutation and journal totals are printed but never asserted.

**Argument:** A regression producing nonzero guarded allocations still returns success under TRACK. A command path that produces measured frames without mutating/journaling can satisfy the frame checks and return success while printing incorrect totals. This contradicts the documented promise that structural failures remain fatal.

**Fix:** Separate structural assertions from timing verdicts. Enforce allocations, edit/journal counts, matching frames and wake-policy invariants in every mode. Make gate enforcement explicit in the acceptance runner; preserve TRACK only for measurements without a timing verdict.

## 21. MAJOR — The allocation suite cannot enforce the native input part of Law 2

Location: [tests/editor_test.c:15](/home/tobias/Projects/editor/tests/editor_test.c:15), [tests/editor_test.c:26](/home/tobias/Projects/editor/tests/editor_test.c:26), [src/editor/editor.c:215](/home/tobias/Projects/editor/src/editor/editor.c:215).

The guard starts after a recognized key is dequeued. The guarded workloads inject already translated events. Native platform input handling runs inside the wholesale `on_io` suspension, while `native_input()` installs no allocation guard.

The allowed XCB packet exemption therefore also exempts the application’s own input dispatch, translation and mailbox/backend-event code.

**Concrete gap:** Adding an application `malloc` to `platform_event`, or another native input-processing branch before `on_ingress`, leaves the editor’s allocation tests green. `make check` additionally uses the sanitizer build, where the guard is inert.

**Fix:** Guard native application input processing and narrowly attribute/exempt permitted library packet allocations. Add a release native-input allocation test with bursts, repeat and interleaved completions, plus a deliberate allocation regression proving that the assertion fails.

## 22. MAJOR — Failed group replay persists the intermediate tree despite undo’s snapshot contract

Location: [src/editor/input.c:173](/home/tobias/Projects/editor/src/editor/input.c:173); contract: `src/undo/undo.h:22`.

Undo explicitly requires render/save to use the borrowed pre-group snapshot during partial replay. The editor ignores `undo_replay_snapshot`. If replay completes some records and then fails, it stages the intermediate tree, updates layout bookkeeping and returns the error. Exit subsequently flushes that prefix.

**Argument:** A replacement is a multi-operation group. NOMEM after its first inverse operation leaves a partial replay and a retained pre-group snapshot. The editor serializes the partial state rather than the required snapshot. Its documented prefix-acceptance policy conflicts with the dependency contract, and the suite only tests failure before the first mutation.

**Fix:** Preserve the pre-group view for display/recovery while retaining the retry position, or implement an approved atomic group transaction. If accepting an intermediate tree is intended, explicitly resolve the frozen contract rather than bypass it. Inject failures after each replay operation and compare buffer, visible state and journal recovery.

## 23. MAJOR — Backend initialization bypasses the required mailbox handoff

Location: [src/editor/open.c:16](/home/tobias/Projects/editor/src/editor/open.c:16), [src/editor/open.c:110](/home/tobias/Projects/editor/src/editor/open.c:110).

The editor starts a raw pthread with pointers into the live editor/backend and publishes its result through a stack object followed by `pthread_join`. The project rule requires shared state to cross through `src/work` mailboxes; frozen `render.h` likewise requires the initialization result to be published through a work mailbox.

**Argument:** `pthread_join` orders the current accesses, so this is not a demonstrated data race. It is a concrete handoff-contract deviation and a separate blocking initialization path outside the prescribed worker integration.

**Fix:** Submit initialization through `src/work`, retain exclusive ownership of initialization storage until its completion message, and adopt the result through the UI router. Test successful, failed and cancelled initialization and teardown before storage release.

| # | Severity | Location | One line |
|---|---|---|---|
| 1 | BLOCKER | `src/editor/editor.c:74` | Window events overtake earlier keys; close loses input. |
| 2 | BLOCKER | `src/editor/editor.c:77` | Unstoppable drain overflows queues and drops keys. |
| 3 | BLOCKER | `src/editor/editor.c:129` | Presented edits remain outside crash-recoverable storage. |
| 4 | BLOCKER | `src/editor/input.c:70` | Journal failure discards the unaccepted edit suffix. |
| 5 | BLOCKER | `src/editor/open.c:117` | BASE can reference a different generation than the buffer. |
| 6 | BLOCKER | `src/editor/editor.c:52` | Mid-row cursor/selection changes submit stale cells. |
| 7 | BLOCKER | `src/editor/open.c:95` | Missing checkpoints hide lines following a long line. |
| 8 | MAJOR | `src/editor/open.c:58` | Whole-file setup blocks bounded-prefix publication. |
| 9 | MAJOR | `src/editor/input.c:89` | Every edit scans the full undo history. |
| 10 | MAJOR | `src/editor/input.c:38` | Every edit performs file-sized index maintenance. |
| 11 | MAJOR | `src/editor/editor.c:220` | Large UI operations exceed and escape slice accounting. |
| 12 | MAJOR | `src/editor/input.c:169` | Journaling prevents undo of deletions above 512 KiB. |
| 13 | MAJOR | `src/editor/input.c:138` | Explicit per-key groups disable typing-burst undo. |
| 14 | MAJOR | `src/editor/editor.c:225` | Sustained input can starve layout and submission. |
| 15 | MAJOR | `src/editor/open.c:8` | No-op frees lose reusable piece storage. |
| 16 | MAJOR | `src/editor/editor.c:156` | Mapped-file changes and truncation are never surfaced. |
| 17 | MAJOR | `src/editor/editor.c:191` | Pending journal work causes repeated idle UI wakeups. |
| 18 | MAJOR | `bench/editor_bench.c:164` | G1 omits ingress waiting and contention workloads. |
| 19 | MAJOR | `bench/editor_bench.c:144` | Advertised A fixture actually renders 1440×900. |
| 20 | MAJOR | `bench/editor_bench.c:179` | TRACK hides structural failures and normal gate misses. |
| 21 | MAJOR | `tests/editor_test.c:26` | Allocation tests exempt native application input handling. |
| 22 | MAJOR | `src/editor/input.c:173` | Partial replay persistence violates undo’s snapshot contract. |
| 23 | MAJOR | `src/editor/open.c:110` | Initialization uses a raw pthread handoff instead of mailboxes. |