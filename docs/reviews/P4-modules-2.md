39 findings: 1 BLOCKER, 36 MAJOR, 2 MINOR. Reviewed source through `e9ce63a`. GCC and Clang warning checks passed for the scoped modules, tests, benches and fuzzers. Fresh release builds in anonymous memory passed the current scroll and findui suites, including active allocation guards. Existing ASan/UBSan binaries and 2,000-run fuzz smokes for both modules passed before concurrent dependency updates, with leak detection disabled. Savectl’s existing release and sanitizer suites stopped at `mkdtemp` because the sandbox denies filesystem writes; memory-only probes verified its reported controller failures. The current scroll bench exited 2 against the new lineidx implementation before collecting samples. No repository or corpus files were changed by this review.

An earlier stock scroll binary reported jump p50/p99 377.923/433.875 ms and five over-budget work steps (M)[AC], power `Not charging`, load1 14.26. Its displayed-frame cadence remained unmeasured. The existing findui cancellation binary reported 0.001688/0.010010 ms over 64 samples (M)[AC], load1 13.15. These measurements describe those binaries, not the subsequently updated dependencies. Nearest-rank percentile arithmetic is correct; sampling, endpoints and verdict behavior have defects below.

## 1. BLOCKER — Reload can install torn bytes when an external writer restores mtime

Location: [src/savectl/savectl.c:65](/home/tobias/Projects/sublimite/src/savectl/savectl.c:65), comparison at line 72 and reload validation at lines 129–133.

The controller constructs and compares only device, inode, size and mtime. Current `file_id` also contains ctime, permissions, ownership and entry type. Reload therefore discards evidence of an intervening rewrite.

Concrete repro: read the first 1 MiB of a 2 MiB file containing `A`; rewrite the file with `B`, restore its original mtime, then let reload read the remaining chunk. Ctime changes, but the controller accepts the mixed copy.

A current-source probe using anonymous files and syscall seams reproduced:

```text
reload install=0 file_error=0 first=A last=B disk=B/B modified=0
```

The replacement tree contains mixed versions and is marked clean. `OP_CHECK` likewise misses detectable metadata/ctime changes; `OP_KEEP` stores another incomplete identity.

Fix: preserve and compare the complete current file identity. Validate both the opened descriptor and canonical directory entry, using consistent file-module identity helpers. Add deterministic mid-read rewrite/restored-mtime tests requiring rejection and preservation of the old tree.

## 2. MAJOR — Scroll queries swallow source failures and publish false exact positions

Location: [src/scroll/index.c:145](/home/tobias/Projects/sublimite/src/scroll/index.c:145), and the byte-to-line query at line 155.

The adapter checks failed spans in its own walkers, but delegates other reads to lineidx queries that have no propagated error result. It then commits their returned offsets and labels.

Concrete repro: build an exact index for `a\nb\nc\nd\ne\nf\ng\nh\ni\nj`, seek line 2 with four visible rows, then resolve through a span callback returning zero before EOF.

The current-source probe returned:

```text
rc=0 byte=19 line=0 approximate=0
```

The expected byte is 4. The unavailable source becomes an “exact” EOF viewport.

Fix: propagate source failure through every delegated query and refuse the scroll-state commit. A failed query must also avoid publishing incomplete chunk counts. Add indexed, unindexed and partially failing-span cases asserting unchanged state.

## 3. MAJOR — Scroll’s byte budget does not bound foreground work

Location: [src/scroll/index.c:16](/home/tobias/Projects/sublimite/src/scroll/index.c:16), walkers at lines 40 and 101, and resolution at line 126.

`budget` limits only the optional lineidx seek. Alignment, relative walking, bottom clipping and legacy lineidx queries perform additional work without a shared callback limit or deadline.

Concrete repro: supply one-byte spans over a long newline-free region and seek a distant byte with `budget=0`. Alignment and bottom lookahead can each invoke tens of thousands of callbacks. A piece-backed callback restarts a tree iterator for each span. These calls cannot yield between input checks.

The new sliced lineidx seek does not bound these adapter-owned loops or the legacy queries they call.

Fix: give the complete adapter operation a shared span/byte/wall-time budget and resumable state. Return `SCROLL_MORE` without committing an unproven viewport. Test total callback work, including alignment, clipping and delegated reads.

## 4. MAJOR — Scroll resolution permits foreground major faults

Location: [src/scroll/index.c:25](/home/tobias/Projects/sublimite/src/scroll/index.c:25), relative reads at lines 53 and 73, and follow reads at line 193.

A span callback can return a valid pointer into a cold mapping without touching its pages. The adapter then dereferences those pages synchronously. A byte bound does not bound storage-fault latency.

Concrete repro: open a large mapped file, evict pages near a distant scroll target, then wheel, seek or follow there. Even `scroll_resolve(..., 0)` can fault on the UI thread. The source contract requires stability but does not require resident bytes or define deferred availability.

Fix: obtain bounded, resident viewport/line-boundary data from snapshot workers and publish it through work mailboxes. Define pending navigation while data is unavailable. Add cold-source tests recording foreground major faults.

## 5. MAJOR — The scroll bench assumes the obsolete synchronous seek contract

Location: [bench/scroll_bench.c:172](/home/tobias/Projects/sublimite/bench/scroll_bench.c:172), and [docs/decisions/P3.4.md:134](/home/tobias/Projects/sublimite/docs/decisions/P3.4.md:134).

The bench passes the whole file size to one `lineidx_seek_line` call and requires an exact deep target immediately. Current lineidx clamps each continuation to a bounded slice.

Concrete repro: compile the current scroll bench against current lineidx. The anonymous-memory build exited 2 at its first jump, before recording a sample. The decision document also still says a small positive budget rounds up to a chunk.

Fix: use `lineidx_seek_start_owned`/`lineidx_seek_result` with an immutable source, or pump bounded continuations correctly. Charge enqueue, waiting, adoption and correct viewport submission to the timer. Update the integration contract and retain cancellation, timeout and correctness assertions.

## 6. MAJOR — Smooth scrolling lacks a consumable renderer origin and overscan contract

Location: [docs/decisions/P3.4.md:157](/home/tobias/Projects/sublimite/docs/decisions/P3.4.md:157), particularly lines 163–168.

Scroll retains fractional pixels, but the renderer consumes integral rows. The required origin, clipping and overscan operation is only a proposal.

Concrete repro: at row height 17, `scroll_wheel(&s, 1)` produces a 51-Q8-pixel remainder without advancing the byte anchor. Following the documented layout/submit calls produces the same image until a whole row accumulates. Translating without an extra laid-out row leaves the lower edge unfilled.

Fix: provide and wire a renderer origin/clip interface, reserve the overscan row, and apply the same transform to hit testing and damage. Verify fractional movement and lower-edge content through the real backend.

## 7. MAJOR — Wrapped buffers have no scroll adapter

Location: [docs/decisions/P3.4.md:75](/home/tobias/Projects/sublimite/docs/decisions/P3.4.md:75), and proposed adapter at line 176.

The module counts logical lines, while wrapped viewports move in visual rows. The contract prohibits using the existing adapter for wrapped buffers but supplies no replacement. Current editor buffers can default to wrap.

Concrete repro: wrap a long, single logical line into more visual rows than the viewport. Logical extent is one line, so `max_top` is zero and the scroll module cannot move through its visual rows. Following the prohibition leaves wrapped wheel/page scrolling unwired.

Fix: implement a bounded visual-row adapter carrying wrap boundary and affinity information. Cover wheel accumulation, page motions, resize, EOF clipping and cursor follow on long wrapped lines.

## 8. MAJOR — The scroll work proxy cannot enforce displayed G3z

Location: [bench/scroll_bench.c:198](/home/tobias/Projects/sublimite/bench/scroll_bench.c:198), and the acknowledgement at line 210.

The bench measures CPU work plus null submission/presentation. It does not observe displayed refreshes, missed presentation opportunities, fractional translation or compositor/backend queueing.

Concrete regression argument: a backend that drops every second displayed scroll frame leaves this bench’s correctness checks and null-frame counts unchanged. Its fixed half-period work threshold cannot detect that failure.

Fix: retain the proxy as a labelled regression measurement and add an integrated, frame-identity-correlated 10,000-refresh G3z test. Include sidebar correctness, fractional motion and actual presentation completion in the verdict.

## 9. MAJOR — Reload can block the bulk worker opening a FIFO

Location: [src/savectl/savectl.c:103](/home/tobias/Projects/sublimite/src/savectl/savectl.c:103).

Reload opens with `O_RDONLY` before checking the descriptor’s type. It lacks the nonblocking/type-safe acquisition used by the file module.

Concrete repro: replace the target with a named FIFO with no writer immediately before reload opens it. `open` blocks before `fstat` or the controller’s epoch checks. Logical close leaves destruction busy, and the sole bulk worker cannot service other jobs.

Fix: acquire with `O_NONBLOCK`, reject nonregular descriptors before reading, and enforce canonical-entry validation. Add a synchronized regular-file-to-FIFO replacement test requiring a returned error without opening a writer to release the job.

## 10. MAJOR — Journal base changes do not enable external-conflict recovery

Location: [src/savectl/savectl.c:336](/home/tobias/Projects/sublimite/src/savectl/savectl.c:336).

Only `FILE_ERR_CHANGED` invokes `changed(s)`. `journal_save_prepare` can detect the same external replacement earlier and return `JOURNAL_BASE_CHANGED`.

Concrete repro: change the saved BASE before Ctrl+S without delivering a watch notification. Prepare refuses the stale BASE. The controller enters `SAVECTL_FAILED`, with no banner and neither conflict action enabled.

The seam probe returned:

```text
state=FAILED file_error=0 journal_error=BASE_CHANGED
banner=NULL can_keep=0 can_reload=0
```

Fix: classify `JOURNAL_BASE_CHANGED` as an external conflict while preserving its diagnostic and any retained token. Test notified and unnotified journal-backed changes and enabled recovery actions.

## 11. MAJOR — Undo cannot restore the controller’s clean state

Location: [src/savectl/savectl.c:228](/home/tobias/Projects/sublimite/src/savectl/savectl.c:228), and saved-state handling at line 319.

Every mutation, including undo, permanently sets `modified` until another save/reload. There is no way to communicate that undo returned to the saved state.

Concrete repro: save `base`, insert `!`, then undo that insertion. The tree again contains `base`, but the controller remains modified. This was reproduced in memory. It causes close prompts and external-change banners instead of silent reload.

The current editor already tracks a saved history position; the save contract directs it to propagate the controller’s conservative bit instead.

Fix: separate monotonic revision invalidation from saved-content identity. Accept a host-provided clean/dirty identity derived from retained undo history, including eviction and branch handling.

## 12. MAJOR — Controller destruction waits on an unrelated reused work slot

Location: [src/savectl/savectl.c:219](/home/tobias/Projects/sublimite/src/savectl/savectl.c:219).

Destruction checks the slot’s current `busy` flag without checking whether it still belongs to the controller’s handle.

Concrete repro: finish a controller check, drain its notification, then enqueue an unrelated blocked job that reuses the slot. `savectl_destroy` returns `SAVECTL_BUSY` until that unrelated job finishes. The probe reproduced slot 0 reuse and destruction changing from BUSY to OK only after releasing the unrelated job.

Fix: use `work_handle_finished(pool, handle)` for physical retirement. Add a completed-controller/reused-running-slot regression.

## 13. MAJOR — Reload reserves file-size anonymous memory outside G10f

Location: [src/savectl/savectl.c:114](/home/tobias/Projects/sublimite/src/savectl/savectl.c:114), and [docs/decisions/P4.7.md:98](/home/tobias/Projects/sublimite/docs/decisions/P4.7.md:98).

Every reload allocates and touches the entire file, including large mapped originals. Old trees, snapshots and other tabs remain live during acquisition.

Concrete repro: externally change an unmodified 10 GiB mapped file. Automatic reload attempts a 10 GiB anonymous copy. The mmap G10f allowance is metadata-sized, not another complete anonymous file. Smaller files can also retain both old and replacement copies at peak.

The decision document acknowledges the exclusion but does not provide a gate waiver.

Fix: preserve the large-file mapping/snapshot policy through a validated worker-ready backing interface. Budget overlap and live snapshots explicitly, and test peak ownership during automatic reload.

## 14. MAJOR — Reload tree construction performs file-size metadata work on the UI

Location: [src/savectl/savectl.c:389](/home/tobias/Projects/sublimite/src/savectl/savectl.c:389).

`piece_init_mapped` constructs entries, leaves and branches for the complete replacement. Its work grows with file chunk count, and `savectl_take_reload` supplies no continuation or deadline.

Concrete repro: complete a multi-gigabyte reload while typing in another tab. The next frame’s installation call builds the complete replacement metadata before returning. Moving byte acquisition to a worker does not bound this foreground phase.

Fix: preflight reserved storage and build the UI-owned tree through bounded construction continuations, checking input between slices. Atomically install only the completed tree. Measure the complete UI installation slice.

## 15. MAJOR — Reload installation allocation failure leaves a stuck reloading model

Location: [src/savectl/savectl.c:390](/home/tobias/Projects/sublimite/src/savectl/savectl.c:390), and failure at line 393.

Metadata allocation failure returns `SAVECTL_NOMEM` without changing `ready_reload`, state or model errors. Both recovery actions remain disabled.

Concrete repro: complete a readable reload with an allocator that refuses `piece_create`, then take the result. The probe returned:

```text
install=-3 state=RELOADING busy=0 file_error=0
can_reload=0 can_keep=0
```

The frame contract retries while state is reloading. With the editor’s non-reclaiming arena allocator, repeated partial construction can consume further reservation.

Fix: expose a failed installation state, retire or explicitly retain the staged copy for a defined retry, and roll back construction storage. Test failures at every construction allocation and recovery without closing the tab.

## 16. MAJOR — The required mapped-source identity and guard remain unavailable

Location: [docs/decisions/P4.7.md:107](/home/tobias/Projects/sublimite/docs/decisions/P4.7.md:107), especially lines 111–114, and proposals at lines 165–171.

Safe setup requires the actual opened baseline and a retained, worker-safe original/fault-epoch guard. Current file headers do not export that acquisition token. UI-only `file_check` is not a substitute for worker validation.

Concrete integration repro: create a controller for a mapped editor buffer using available public accessors. Omitting the guard returns `SAVECTL_SOURCE_GUARD`; obtaining a fresh pathname stat cannot establish the original’s identity or sticky fault history.

Fix: implement the file-owned identity/guard accessors and wire their full lifetime before enabling mapped save/keep. Test large mapped buffers, detached originals, restored mtime and late SIGBUS invalidation.

## 17. MAJOR — Save completion bypasses the mailbox-only ownership law

Location: [src/savectl/savectl.c:186](/home/tobias/Projects/sublimite/src/savectl/savectl.c:186), and adoption at line 330.

The worker mutates controller-resident result/token storage and releases `done`; UI ticks adopt it independently of receiving a work message. A full mailbox deliberately uses this side channel.

Concrete repro: fill the worker mailbox, run a save, and tick without receiving its completion. `mailbox_loss` explicitly expects the controller to become saved after publication fails.

Release/acquire synchronizes this implementation, but its result ownership does not follow the binding mailbox-only law.

Fix: publish a sealed, generation-validated result lease through the work mailbox protocol, with a guaranteed terminal notification mechanism under saturation. Keep adoption and retirement inside that protocol and test full-mailbox completion.

## 18. MAJOR — G8s excludes checkpoint construction and displayed saving status

Location: [docs/decisions/P4.7.md:119](/home/tobias/Projects/sublimite/docs/decisions/P4.7.md:119), and [bench/savectl_bench.c:102](/home/tobias/Projects/sublimite/bench/savectl_bench.c:102).

The host must build a complete session checkpoint before calling save. The bench builds its one-record checkpoint before starting the timer, then ends acknowledgement timing at the controller call’s return. It never submits a frame showing “saving”.

Concrete repro: save a session containing many dirty buffers and substantial checkpoint data. UI preparation occurs before the measured interval and has no documented work bound; rendering can also delay the actual acknowledgement beyond 2/5 ms (G).

Fix: capture a bounded immutable request, enqueue checkpoint preparation off the typing path, and measure through the submitted saving status. Add large dirty-session and busy-backend cases to the actual editor acknowledgement test.

## 19. MAJOR — Long file writes unnecessarily hold the whole-session journal lease

Location: [src/savectl/savectl.c:252](/home/tobias/Projects/sublimite/src/savectl/savectl.c:252), and [docs/decisions/P4.7.md:58](/home/tobias/Projects/sublimite/docs/decisions/P4.7.md:58).

`journal_leased` remains true throughout `OP_SAVE`, including the potentially long file write after journal prepare has completed. The contract prohibits journal operations across the entire session during that lease.

Concrete repro: pause a large save at `FILE_STEP_FSYNCED`, type and submit frames in another buffer, then crash before releasing the save. Those deferred edits have not been appended despite the journal otherwise being quiescent. The exposure grows with save duration.

Fix: publish a prepare-stage ownership handoff and return the journal lease during file-only work. Reacquire it for bounded finish work, preserving checkpoint order and accepted append protection. Add crash tests during save with edits in another buffer.

## 20. MAJOR — Finish retry guidance is incompatible with current journal writeback recovery

Location: [src/savectl/savectl.h:68](/home/tobias/Projects/sublimite/src/savectl/savectl.h:68), and [docs/decisions/P4.7.md:69](/home/tobias/Projects/sublimite/docs/decisions/P4.7.md:69).

The save contract recommends off-path `journal_retry` before retrying finish. Current journal semantics require a complete current checkpoint in a fresh inode after failed `fdatasync`; `journal_retry` returns IO and preserves suspension.

Concrete repro: finish encounters a worker writeback error. Following the save contract’s retry sequence repeatedly returns IO while `needs_finish` and the retained token remain set. The controller exposes no documented transition through fresh-inode recovery.

Fix: distinguish retryable append/directory failures from writeback loss. Document and test complete-session rotation, retained-token preservation and subsequent finish reconciliation.

## 21. MAJOR — Default literal search inherits a much smaller regex limit

Location: [src/findui/worker.c:118](/home/tobias/Projects/sublimite/src/findui/worker.c:118), transformation at line 130.

Case-insensitive literal search is converted into regex atoms. The panel accepts 4,096 query bytes, but the transformed expression inherits the 256-state regex limit.

Concrete repro: search a 512-byte all-`a` source for 256 literal `a` bytes with default options. The probe produced `FIND_ERR_LIMIT` and no completed result. Turning on case sensitivity completed with two matches.

This restriction is documented, but it affects ordinary literal input solely because of the default toggle.

Fix: implement literal ASCII case folding in the literal search path, retaining literal pattern limits and counting performance. Add maximum-length binary and ASCII literal tests in both case modes.

## 22. MAJOR — Whole-word search still enumerates every candidate through next

Location: [src/findui/worker.c:205](/home/tobias/Projects/sublimite/src/findui/worker.c:205), particularly lines 208–215.

The new visitor fixes ordinary counting, but whole-word mode still repeatedly calls `find_literal_next`/`find_regex_next_budget` and reads boundary bytes per candidate.

Concrete repro: search a large `a ` repetition for whole-word `a`. Every accepted occurrence requires another search initialization and boundary checks, including all occurrences after output caches fill. Hundreds of millions of matches remain hundreds of millions of API calls.

Fix: add word-boundary filtering to the bounded counting visitor, with block counting where applicable and persistent search state otherwise. Gate the actual whole-word worker endpoint, not only ordinary all-`a` counting.

## 23. MAJOR — Whole-word visible overflow still publishes every visible match

Location: [src/findui/worker.c:219](/home/tobias/Projects/sublimite/src/findui/worker.c:219).

Whole-word mode marks every window-intersecting match as wanted without consulting `visible_capacity`. UI storage is bounded; publication work is not.

Concrete repro: configure prefix/visible capacities 8/1, search 600 occurrences of whole-word `a`, and use a window covering the source. The current-source probe observed:

```text
count=600 visible=600 overflow=1 published_ranges=600
```

After capacity exhaustion, the remaining publications only increase the overflow count and consume UI mailbox work.

Fix: publish only the bounded visible prefix, required first cache and selected ordinal. Count discarded visible matches on the worker and report the exact overflow cardinality in completion. Test output-message bounds in whole-word mode.

## 24. MAJOR — Mailbox backpressure occupies the sole bulk worker

Location: [src/findui/worker.c:12](/home/tobias/Projects/sublimite/src/findui/worker.c:12).

A full mailbox makes the worker sleep/retry inside the same job indefinitely. This retains the sole bulk execution slot.

Concrete repro: queue enough find ranges to fill the mailbox, then submit a save/check/jump job behind it. The probe showed the second job had not started while retries accumulated; cancelling the search allowed it to start.

Bounded UI draining does not make the worker scheduler preempt the search, and large publication workloads can extend the delay substantially.

Fix: make publication a resumable job stage that yields the bulk lane, or provide scheduling with bounded foreground-service latency. Verify save/jump service under full find mailboxes while input receives priority.

## 25. MAJOR — Viewport changes discard exact counts and restart whole-file search

Location: [src/findui/findui.c:217](/home/tobias/Projects/sublimite/src/findui/findui.c:217), restart at line 225.

A window-only change advances the search generation and clears the completed count, selection and caches, then starts again from byte zero.

Concrete repro: complete a 512-match search, then change only its visible window. The probe observed:

```text
complete 1→0, count 512→0, selected 0→UNSET
```

Scrolling or revealing a selected match therefore resets navigation readiness and delays late-window highlights until another prefix scan reaches them.

Fix: separate query/source identity from viewport-range requests. Retain exact count and selection while fetching bounded window results. Test repeated viewport changes during a large search without count loss or repeated global restart.

## 26. MAJOR — Required first 4096 offsets depend on an artificial visible window

Location: [src/findui/findui.c:82](/home/tobias/Projects/sublimite/src/findui/findui.c:82), and [bench/findui_count.h:36](/home/tobias/Projects/sublimite/bench/findui_count.h:36).

The combined range budget permits 4,096 ranges, but initialization requires a positive visible capacity. Consequently the first-match cache cannot contain all 4,096 required offsets.

The new count bench compensates with a 4,095-entry prefix and a one-entry visible window deliberately positioned at offset 4,095.

Concrete repro: keep that configuration but use the ordinary initial viewport containing byte zero. Its visible entry duplicates an existing prefix match; offset 4,095 is not published. `match_capacity=4096, visible_capacity=1` is rejected.

Fix: retain the required first offsets independently of viewport position, using suitable offset/length storage or shared entries. Require the count bench to pass with initial, disjoint late and empty windows.

## 27. MAJOR — Replace-all refuses ordinary documents once the first cache fills

Location: [src/findui/findui.c:340](/home/tobias/Projects/sublimite/src/findui/findui.c:340).

Replace-all requires every match to fit the first cache. The hard combined range cap limits this to fewer than 4,096 matches, often fewer with normal configuration.

Concrete repro: search a small file containing 5,000 single-byte matches, with sufficient piece storage and undo capacity. Exact counting completes, but replace-all returns `FINDUI_ERR_LIMIT` before applying anything.

Returning an error avoids a partial replacement, but the required M1 replace-all operation remains unavailable for modest files.

Fix: provide a bounded paged replacement plan over the immutable source, preserving right-to-left application and one undo group. Test beyond-cache replacement, cancellation, allocation failure and exact undo/redo.

## 28. MAJOR — One replacement match can exceed the entire UI deadline

Location: [src/findui/findui.c:380](/home/tobias/Projects/sublimite/src/findui/findui.c:380), deletion at line 385.

The deadline is checked before a match. `undo_delete` can then copy an arbitrarily large original range into the add buffer, scan it and mutate its tree without yielding.

Concrete repro: match `a+` over a large original file and replace it with one byte. `match_budget=1` and a near-term deadline still admit the complete deletion. A 10 GiB logical match is not a bounded unit of foreground work.

Fix: preflight byte/work admission and use resumable, transaction-aware deletion/replacement operations. Preserve one undo group while checking input between byte slices. Test maximum individual match size, not only match count.

## 29. MAJOR — Replacement bypasses the editor’s mutation bookkeeping

Location: [src/findui/findui.c:385](/home/tobias/Projects/sublimite/src/findui/findui.c:385), and [docs/decisions/P4.6.md:167](/home/tobias/Projects/sublimite/docs/decisions/P4.6.md:167).

The module directly calls undo deletion/insertion. It returns only a completed-match count, with a possible additional partial match on error. It does not return the successful edit deltas needed by the editor.

Current editor mutation handling updates revision, line count, lineidx, layout, history, tab dirtiness and journal staging for each operation.

Concrete repro: replace several disjoint newline-containing matches, then fail an insertion after its deletion. The contract’s final dirty-range publication cannot supply the exact successful operations or their newline changes. Calling replacement directly leaves those editor structures stale.

Fix: apply through a host mutation interface or return bounded successful delta records, including partial progress. Preflight coordinated index/history/journal capacity before mutation. Add integrated replacement, replay and crash-recovery tests.

## 30. MAJOR — Snapshot retirement lacks a full buffer-arena lifetime contract

Location: [docs/decisions/P4.6.md:103](/home/tobias/Projects/sublimite/docs/decisions/P4.6.md:103), [docs/decisions/P4.7.md:147](/home/tobias/Projects/sublimite/docs/decisions/P4.7.md:147), and [src/editor/buffers.c:32](/home/tobias/Projects/sublimite/src/editor/buffers.c:32).

The contracts retain mappings, panel storage, guards and jobs, but do not provide the per-buffer retirement handshake for snapshot headers/nodes allocated from `editor_buffer.arena`.

Concrete integration repro: switch the shared panel away from a buffer with a running cancelled job, then evict that buffer. Its snapshot remains in a find argument slot, while `editor_buffer_destroy` destroys the tree and frees its arena. Mapping retention alone does not retain those nodes or their allocator context.

Final snapshot release can also perform complete retirement and mapping cleanup; current piece worker reclamation fixes do not make every final-owner release a typing-safe operation.

Fix: retain a full buffer-storage lease through snapshot retirement. Expose per-source retirement acknowledgement and defer arena/file/final graph cleanup to the appropriate maintenance phase. Test eviction and reload while cancelled find/save snapshots remain live.

## 31. MAJOR — Default benches disable timing-miss exit codes

Location: [bench/savectl_bench.c:144](/home/tobias/Projects/sublimite/bench/savectl_bench.c:144), [bench/findui_bench.c:59](/home/tobias/Projects/sublimite/bench/findui_bench.c:59), and count defaults at line 142.

Save and findui enable timing failures only with explicit `--gate`. Normal `make bench` supplies neither that flag nor an equivalent default.

Concrete repro: a functionally successful save run whose G8d percentiles exceed their thresholds still returns zero by default. The recorded P4.7 run contains such misses. Findui cancellation and count modes have the same default verdict behavior.

Fix: make the normal bench invocation enforce its binding regression gates. Keep an explicit descriptive `--track` mode. Test miss exit behavior with deterministic injected samples.

## 32. MAJOR — Bench gate verdicts use insufficient samples and omit required intervals

Location: [bench/scroll_bench.c:16](/home/tobias/Projects/sublimite/bench/scroll_bench.c:16), [bench/savectl_bench.c:146](/home/tobias/Projects/sublimite/bench/savectl_bench.c:146), [bench/findui_bench.c:16](/home/tobias/Projects/sublimite/bench/findui_bench.c:16), and count sample limits at lines 142–150.

Jump uses five samples; large save seven; cancellation 64; small save 128. The new count mode defaults to three and accepts at most 64. These cannot satisfy perf §4’s interaction sampling requirement.

Nearest-rank p99 is correctly the sample maximum for several of these sets. That is not qualified tail evidence. The original custom reports also omit confidence intervals.

Concrete regression argument: a workload with a significant rare tail can pass `--gate` when none of the few samples encounters it.

Fix: separate descriptive samples from qualified verdicts, enforce sample adequacy, and report required confidence intervals. Use the bench policy’s inexpensive test regression guards where appropriate; retain failures/timeouts in the measured population.

## 33. MAJOR — Savectl harnesses allocate over-aligned work pools with calloc

Location: [tests/savectl_test.c:48](/home/tobias/Projects/sublimite/tests/savectl_test.c:48), [bench/savectl_bench.c:81](/home/tobias/Projects/sublimite/bench/savectl_bench.c:81), and [fuzz/savectl_fuzz.c:75](/home/tobias/Projects/sublimite/fuzz/savectl_fuzz.c:75).

`work_pool` requires 64-byte alignment. `calloc` does not guarantee that extended alignment. The work header explicitly requires aligned heap allocation.

Concrete repro: three harness-style allocations on this host had address remainders 16, 32 and 32 modulo 64. Accessing them as `work_pool` violates the type’s alignment requirement and is C undefined behavior.

Fix: use `aligned_alloc(_Alignof(work_pool), sizeof *pool)` followed by initialization, or correctly aligned arena storage. Add an alignment assertion before pool initialization in all three harnesses.

## 34. MAJOR — Normal save tests omit the I/O and completion-order assertions

Location: [tests/savectl_test.c:16](/home/tobias/Projects/sublimite/tests/savectl_test.c:16), and conditional invocation at line 452.

`UI_NO_IO` becomes a no-op in ordinary builds. The notification-order test is also compiled out unless the separately documented manual wrapper configuration is used.

Concrete regression argument: inserting UI-side stat/open operations into save/tick, or moving completion publication after its wake, can leave ordinary `make check` green. The active allocation test exercises modified/view/idle notifications rather than enforcing these I/O boundaries.

Fix: make the instrumented configuration part of the normal check target through supported test-specific build/link flags or a dedicated automatically built test. Keep the deterministic held-publication schedule.

## 35. MAJOR — Scroll fuzzing excludes the difficult source and edit cases

Location: [fuzz/scroll_fuzz.c:109](/home/tobias/Projects/sublimite/fuzz/scroll_fuzz.c:109).

The indexed model generates lines only 7–59 bytes long. It never edits the source/index, deletes the anchor’s preceding newline, introduces source failures or exercises long-line proof exhaustion.

Concrete regression argument: removing the `SCROLL_MORE` proof behavior for distant newline-free regions leaves this generator’s short-line follow assertions satisfied. The indexed source-error failure in finding 2 also cannot be generated.

Fix: vary line lengths across and far beyond the scan budget, mutate source/index together, transform anchors independently, and inject staged span failures. Model both successful visibility and unchanged-state `MORE`/error outcomes.

## 36. MAJOR — Findui fuzzing avoids asynchronous lifecycle and capacity boundaries

Location: [fuzz/findui_fuzz.c:5](/home/tobias/Projects/sublimite/fuzz/findui_fuzz.c:5), configuration at line 223 and unconditional settling at line 289.

Subjects fit 256 bytes, queries fit 24, and both caches hold 512 entries. Every operation waits for the search to settle. Regex generation uses a restricted alphabet and small template grammar.

Concrete regression arguments: early release of a running cancelled snapshot is not exercised by these settled sequences. Prefix/visible overflow, uncached ordinals, the default 256-byte query failure and whole-word output saturation are unreachable.

Fix: randomize capacities and interleave query/source/window changes with partial drains and running workers. Add large/raw queries, malformed escapes/classes, allocation faults, replacement deadlines/cancel and source-retirement schedules to an independent oracle.

## 37. MAJOR — Savectl fuzzing omits journal, acquisition and failure states

Location: [fuzz/savectl_fuzz.c:77](/home/tobias/Projects/sublimite/fuzz/savectl_fuzz.c:77), and settling at line 28.

The fuzzer always uses COPY mode without a journal, creates the target successfully, keeps it tiny and usually settles each operation completely. Its filesystem operations require success rather than exploring controller error handling.

Concrete regression arguments: broken prepare/finish token handling, `JOURNAL_BASE_CHANGED` classification, reload metadata-allocation failure and full-identity rewrite detection leave this model unchanged. It cannot generate the torn reload in finding 1.

Fix: add journal-backed session state, retained-token/retry/rotation transitions, guarded mapped sources, absent/nonregular targets, controlled I/O failures and mid-reload rewrites. Exercise reused work slots and partial completion schedules.

## 38. MINOR — Findui labels unknown power status as battery evidence

Location: [bench/findui_bench.c:63](/home/tobias/Projects/sublimite/bench/findui_bench.c:63).

Any successfully read status other than three recognized AC strings becomes `[bat]`. An `Unknown` status or unsupported text is therefore asserted to be battery-powered. The custom parser also trims less whitespace than the shared harness.

Concrete repro: make the status seam return `Unknown`; cancellation results are labelled `[bat]`.

Fix: use `bench_battery_status` and `bench__tag_from_power`, preserving `[unknown]` or explicitly refusing a qualified verdict when power evidence is unavailable.

## 39. MINOR — Save does not forward explicit zero-mode validity for new targets

Location: [src/savectl/savectl.c:162](/home/tobias/Projects/sublimite/src/savectl/savectl.c:162).

Current `file_save_args.mode_valid` distinguishes explicit permission mode `0000` from the default new-target mode. The controller forwards the numeric mode but leaves that validity flag zero and exposes no equivalent option.

Concrete repro: request creation of an absent target with explicitly selected mode `0000`. The forwarded arguments select the file core’s `0644` default instead.

Fix: add an explicit creation-permission validity contract and forward `mode_valid`. Define setup-time creation defaults, including the intended umask policy, and test zero permission bits.

| # | Severity | Location | One line |
|---|---|---|---|
| 1 | BLOCKER | src/savectl/savectl.c:65 | Reload accepts mixed-version bytes after restored-mtime rewrites. |
| 2 | MAJOR | src/scroll/index.c:145 | Delegated source failures become false exact viewports. |
| 3 | MAJOR | src/scroll/index.c:16 | Adapter work lacks a shared foreground slice bound. |
| 4 | MAJOR | src/scroll/index.c:25 | Navigation can fault cold mapped pages on UI. |
| 5 | MAJOR | bench/scroll_bench.c:172 | The bench fails against sliced lineidx seeking. |
| 6 | MAJOR | docs/decisions/P3.4.md:157 | Fractional rendering origin and overscan remain unavailable. |
| 7 | MAJOR | docs/decisions/P3.4.md:75 | Wrapped visual-row scrolling has no adapter. |
| 8 | MAJOR | bench/scroll_bench.c:198 | Null work timings cannot enforce displayed G3z. |
| 9 | MAJOR | src/savectl/savectl.c:103 | FIFO replacement can block reload before type validation. |
| 10 | MAJOR | src/savectl/savectl.c:336 | Journal base changes omit conflict actions. |
| 11 | MAJOR | src/savectl/savectl.c:228 | Undo cannot restore clean state. |
| 12 | MAJOR | src/savectl/savectl.c:219 | Destruction waits on unrelated slot reuse. |
| 13 | MAJOR | src/savectl/savectl.c:114 | Reload allocates file-size anonymous storage. |
| 14 | MAJOR | src/savectl/savectl.c:389 | Replacement metadata construction is unsliced UI work. |
| 15 | MAJOR | src/savectl/savectl.c:390 | Installation allocation failure strands reloading state. |
| 16 | MAJOR | docs/decisions/P4.7.md:107 | Safe mapped-save identity/guard acquisition is unavailable. |
| 17 | MAJOR | src/savectl/savectl.c:186 | Mutable completion adoption bypasses mailboxes. |
| 18 | MAJOR | bench/savectl_bench.c:102 | G8s excludes checkpoint preparation and shown status. |
| 19 | MAJOR | src/savectl/savectl.c:252 | File writes hold the whole-session journal lease. |
| 20 | MAJOR | src/savectl/savectl.h:68 | Finish recovery guidance omits fresh-inode rotation. |
| 21 | MAJOR | src/findui/worker.c:118 | Default literals inherit the smaller regex limit. |
| 22 | MAJOR | src/findui/worker.c:205 | Whole-word counting still invokes next per candidate. |
| 23 | MAJOR | src/findui/worker.c:219 | Whole-word visible overflow keeps publishing ranges. |
| 24 | MAJOR | src/findui/worker.c:12 | Full-mailbox retries retain the bulk execution slot. |
| 25 | MAJOR | src/findui/findui.c:217 | Window changes discard completed global search state. |
| 26 | MAJOR | src/findui/findui.c:82 | First 4096 offsets depend on a handpicked window. |
| 27 | MAJOR | src/findui/findui.c:340 | Replace-all cannot exceed the first-match cache. |
| 28 | MAJOR | src/findui/findui.c:380 | One large match can exceed the UI deadline. |
| 29 | MAJOR | src/findui/findui.c:385 | Replacement omits editor mutation deltas and bookkeeping. |
| 30 | MAJOR | docs/decisions/P4.6.md:103 | Snapshot retirement does not retain full buffer storage. |
| 31 | MAJOR | bench/savectl_bench.c:144 | Default benchmark invocations ignore timing misses. |
| 32 | MAJOR | bench/scroll_bench.c:16 | Small samples cannot qualify percentile gate verdicts. |
| 33 | MAJOR | tests/savectl_test.c:48 | Calloc does not satisfy work-pool alignment. |
| 34 | MAJOR | tests/savectl_test.c:16 | Normal checks omit I/O and publication-order enforcement. |
| 35 | MAJOR | fuzz/scroll_fuzz.c:109 | Scroll fuzzing excludes long lines, edits and source failures. |
| 36 | MAJOR | fuzz/findui_fuzz.c:223 | Findui fuzzing excludes lifecycle and capacity boundaries. |
| 37 | MAJOR | fuzz/savectl_fuzz.c:77 | Save fuzzing excludes journal and acquisition failures. |
| 38 | MINOR | bench/findui_bench.c:63 | Unknown power is incorrectly labelled battery. |
| 39 | MINOR | src/savectl/savectl.c:162 | Explicit zero-mode validity is not forwarded. |