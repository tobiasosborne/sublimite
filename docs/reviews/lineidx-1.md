18 findings: **4 BLOCKER, 14 MAJOR, 0 MINOR**.

Read-only review; no files changed or builds performed. The existing ASan/UBSan `lineidx_test` binary passed with LeakSanitizer disabled. The reproductions below are code arguments unless explicitly identified as executed. Performance gates were not rerun; the loaded-box G7 117/207 ms and G7j 53.7 ms results remain pending a quiet verdict.

## 1. BLOCKER — Capacity exhaustion silently corrupts exact newline counts

Location: [src/lineidx/lineidx.c:380](/home/tobias/Projects/editor/src/lineidx/lineidx.c:380), [src/lineidx/lineidx.c:408](/home/tobias/Projects/editor/src/lineidx/lineidx.c:408), [src/lineidx/lineidx.c:194](/home/tobias/Projects/editor/src/lineidx/lineidx.c:194).

When a split cannot fit, `lineidx_edit()` leaves an arbitrarily large chunk and returns success. Counts stored in entries remain `uint32_t`; worker results also reserve bit 32 for `RES_NA`.

Concrete repro: create an empty index, whose capacity is 66 entries, and insert `2^32` newline bytes. The required split cannot fit. Refresh truncates the chunk’s newline count to zero, and `lineidx_line_count()` returns **1, exact**, instead of **4,294,967,297**. Building that chunk additionally mistakes newline-count bit 32 for the non-ASCII flag. A synthetic source can represent this content by repeatedly returning the same immutable newline buffer.

The same fallback defeats the typing-path bound: pasting 1 GiB into this empty index makes refresh scan 1 GiB synchronously. Even normal splits mark every inserted chunk edited, and refresh scans all of them in one call. Exact queries subsequently scan the oversized chunk; `nth_newline()` counts its entire span before locating an early target.

Fix: enforce a hard maximum chunk length. Preflight available chunk storage before changing geometry, and keep oversized regions approximate until a worker constructs bounded replacement chunks. Refresh must process a bounded slice. Never mark a truncated count exact. Add capacity-exhaustion and synthetic counts above `UINT32_MAX` tests; the fuzzer’s 600 KiB limit cannot exercise this overflow.

## 2. BLOCKER — Length arithmetic wraps and turns a growing buffer into an exact empty index

Location: [src/lineidx/lineidx.c:351](/home/tobias/Projects/editor/src/lineidx/lineidx.c:351), [src/lineidx/lineidx.c:211](/home/tobias/Projects/editor/src/lineidx/lineidx.c:211).

Edit validation checks deletion bounds but does not check the resulting length. Both the merged-region length and total length can overflow.

Concrete repro:

```c
lineidx *x = lineidx_create(1);
lineidx_edit(x, 0, 0, UINT64_MAX);
```

The call returns zero. Both lengths wrap to zero, the sole entry becomes built, and the index reports an exact empty buffer. No enormous content allocation is needed to demonstrate the corrupted model.

Creation has a related overflow: `lineidx_create(UINT64_MAX)` computes zero chunks before substituting one chunk.

Fix: reject unrepresentable resulting lengths **before cancellation or mutation**. Calculate ceiling division as `len / CHUNK + (len % CHUNK != 0)`, check allocation-size conversions, and document supported length limits. Add boundary tests that verify rejected edits preserve length, geometry, and existing query results.

## 3. BLOCKER — Epoch wrap allows the reaper to free a running worker’s job

Location: [src/lineidx/lineidx.c:146](/home/tobias/Projects/editor/src/lineidx/lineidx.c:146).

`epoch >= handle.epoch + 2u` is not a valid wrapping-generation comparison.

Concrete repro: initialize a free work slot’s epoch to `UINT32_MAX - 2`, submit a build, and pause its worker inside `span()`. The returned handle has epoch `UINT32_MAX - 1`. Its completion threshold, `handle.epoch + 2u`, wraps to zero. `job_done()` therefore reports completion while `busy == 1` and `fn_done == 0`.

Cancellation retires and immediately frees the source, result array, starts array, and job. The resumed worker accesses freed storage. `lineidx_building()` can already report false before cancellation.

Fix: expose a wrap-safe completion/lease query from `src/work`; do not infer completion with unsigned ordering on epochs. Add a deterministic epoch-wrap test with a running source lease and verify release occurs only after its last worker access.

## 4. BLOCKER — The concurrent-edit test frees storage still leased to the canceled worker

Location: [tests/lineidx_test.c:280](/home/tobias/Projects/editor/tests/lineidx_test.c:280), [tests/lineidx_test.c:209](/home/tobias/Projects/editor/tests/lineidx_test.c:209).

`test_edit_during_build()` submits a flat source pointing at `b`, cancels logically through `lineidx_edit()`, then calls `model_edit()`, which frees `b`. Logical cancellation does not mean the worker has stopped reading its source.

Concrete interleaving: the worker passes a stop check and obtains a span into the old buffer; the UI cancels and frees that buffer; the worker resumes in `scan_count()` with a dangling pointer. The sanitizer pass observed during this review does not exclude this schedule.

This test also fails to test the intended concurrent-edit contract: an immutable snapshot must remain unchanged while current content changes.

Fix: build from an independently owned copy or retained `piece_snapshot`, with a release hook that preserves its lifetime. Use deterministic synchronization instead of sleeping. Test same-length replacements, changed lengths, chunk-boundary edits, and restart publication against the current generation.

## 5. MAJOR — Approximate queries can return the middle of a line

Location: [src/lineidx/lineidx.c:101](/home/tobias/Projects/editor/src/lineidx/lineidx.c:101), [src/lineidx/lineidx.c:457](/home/tobias/Projects/editor/src/lineidx/lineidx.c:457).

The public seek contract promises an approximate result at a real line start. `align_back()` returns `hi` when it finds no newline, although `hi` is an arbitrary estimate. The EOF shortcut also returns an unterminated EOF as a line start.

Concrete repro: an unbuilt index over 200,000 `z` bytes returns `{40, false}` for line 1. The content’s only line starts at zero. A sufficiently large requested line returns `{200000, false}`, also not a line start.

The fuzzer explicitly detects the invalid-boundary condition at line 112 but executes an empty body, so this violation cannot fail fuzzing.

Fix: maintain a known real line-start anchor and use it when bounded backward alignment finds no newline. Treat unterminated EOF separately. Turn the empty fuzz condition into an assertion and add approximate long-line and CRLF-boundary cases.

## 6. MAJOR — The seek byte budget is exceeded before it is checked

Location: [src/lineidx/lineidx.c:489](/home/tobias/Projects/editor/src/lineidx/lineidx.c:489).

The loop tests `spent < budget`, then scans a whole chunk without checking whether that chunk fits the remaining budget.

Concrete repro: use one unbuilt 64 KiB chunk with its only newline at the last byte. Request line 1 with `budget == 1`. The function scans all 65,536 bytes, extends the prefix, and returns the exact EOF offset. Capacity-exhausted chunks make this overrun arbitrarily larger.

Fix: enforce the remaining budget before scanning. To satisfy “exact whenever the target lies within budget,” support resumable partial-chunk scans and persist their progress without declaring the entire chunk built. Add tests that observe consumed bytes for budgets 0, 1, chunk-minus-one, and chunk boundaries.

## 7. MAJOR — Partial jumps perform a noncancellable bulk scan on the UI thread

Location: [src/lineidx/lineidx.c:483](/home/tobias/Projects/editor/src/lineidx/lineidx.c:483), [bench/lineidx_bench.c:142](/home/tobias/Projects/editor/bench/lineidx_bench.c:142).

The header specifies UI-thread queries, and `lineidx_seek_line()` performs the complete continuation scan on that thread. It has no input checks, continuation return, worker submission, or cancellation point. This contradicts G7j’s cancellable-worker design consequence and G1’s ≤0.5 ms UI slices.

The default benchmark scans roughly 0.4 GiB from its 50% prefix to the 90% target. At the planning throughput of 16 GB/s, that is approximately **26.8 ms** of scanning plus **6.6 ms** of estimated fresh-mapping faults: approximately **33.4 ms (E)** before other work. This provides a concrete reason for pressure on the 30 ms p50 budget without proving a quiet measured miss.

The ordinary loop stops at the target chunk; it does not scan the entire remaining tail. The unavoidable continuation, its faults, and oversized-chunk fallback are the relevant costs.

Fix: perform the continuation over an immutable snapshot on `src/work`, publish the requested line offset, and let the UI submit the viewport in bounded work. Retain a separately bounded synchronous query API if needed.

## 8. MAJOR — Metadata maintenance performs whole-file work on typing and query paths

Location: [src/lineidx/lineidx.c:111](/home/tobias/Projects/editor/src/lineidx/lineidx.c:111), [src/lineidx/lineidx.c:273](/home/tobias/Projects/editor/src/lineidx/lineidx.c:273), [src/lineidx/lineidx.c:365](/home/tobias/Projects/editor/src/lineidx/lineidx.c:365), [src/lineidx/lineidx.c:403](/home/tobias/Projects/editor/src/lineidx/lineidx.c:403).

Several foreground operations scale with total chunk count:

- Inserting near the beginning shifts every later start.
- Refresh searches the entire table for edited entries.
- Poll applies the entire accumulated publication backlog.
- Every dirty `derive()` rebuilds summaries from chunk zero.

A 10 GB file has 152,588 chunks. A single early keystroke traverses those entries for shifting, again for refresh, and again when the next query derives summaries. No operation yields or checks input. Repeated progress application followed by `lineidx_complete()` also rescans the growing prefix, producing quadratic cumulative metadata work.

Additionally, poll clears `FL_EDITED` without decrementing `n_edited`, causing a later refresh to traverse the entire table even when no edited entries remain.

Fix: use incremental summaries, explicit dirty-range tracking, bounded publication adoption, and offset changes that avoid shifting the entire suffix per edit. Maintain `n_edited` consistently. Test foreground slice limits on 10 GB geometry with a worker active.

## 9. MAJOR — Cancellation lacks a bounded logical acknowledgement and its bench cannot enforce one

Location: [src/lineidx/lineidx.c:289](/home/tobias/Projects/editor/src/lineidx/lineidx.c:289), [bench/lineidx_bench.c:165](/home/tobias/Projects/editor/bench/lineidx_bench.c:165).

Cancellation first calls unbounded poll/application and reap work. Reaping invokes arbitrary source-release work on the UI thread. This work occurs before the cancellation request.

There is also a publication-suppression hole: pause the worker after storing final `done_n` but before its final `work_publish()`. Cancellation polls all results and retires the job; `x->job` becomes null, so `work_cancel()` is skipped. The worker can publish a live message after cancellation returns. `x->gen` is not invalidated by cancellation.

The benchmark times cancellation **plus physical retirement**, passes zero gate limits, and ignores the report result. A 100 ms logical acknowledgement would not fail that row.

Fix: capture the handle and invalidate/request cancellation first; preserve completed results through bounded adoption and defer cleanup. Time logical acknowledgement separately and enforce 1/5 ms with a nonzero exit. Add a final-publication interleaving test.

## 10. MAJOR — Worker cancellation slices are not bounded for fragmented sources

Location: [src/lineidx/lineidx.c:53](/home/tobias/Projects/editor/src/lineidx/lineidx.c:53), [src/lineidx/lineidx.c:181](/home/tobias/Projects/editor/src/lineidx/lineidx.c:181).

Stop checks occur only every 16 chunks. `scan_range()` never checks cancellation between spans. Chunk count therefore does not bound worker CPU time.

A valid source returning one-byte spans requires over a million callback/scanner invocations between stop checks. The tested piece adapter also restarts snapshot iteration for each span. Alternatively, a thread-safe fragmented adapter consuming 2 ms CPU per span can exceed the 5 ms bound inside the first chunk.

The tests’ `delay_us` sleeps measure wall-clock delay rather than the CPU-slice contract and do not enforce the bound.

Fix: pass cancellation context into the scan continuation and check between bounded span/block batches, with a CPU-time slice limit. Test highly fragmented snapshots and a deterministic CPU-cost source, independently of physical I/O latency.

## 11. MAJOR — Destroy can block the UI behind an unrelated bulk job

Location: [src/lineidx/lineidx.c:235](/home/tobias/Projects/editor/src/lineidx/lineidx.c:235), [src/lineidx/lineidx.h:45](/home/tobias/Projects/editor/src/lineidx/lineidx.h:45).

The documented “≤ a few ms” destruction bound does not hold for queued builds.

Concrete repro: submit a long-running bulk save/find job, queue an index build behind it, then destroy the index. Cancellation changes the queued index’s epoch, but its slot remains busy until the bulk worker dequeues it. Destroy sleeps until the unrelated job finishes, potentially blocking the UI for seconds despite the index never having run.

Fix: add safe queued-job removal/completion acknowledgement to `src/work`, or make index disposal asynchronous with an explicit source lease. Add queued-cancellation/destruction tests with another bulk job active.

## 12. MAJOR — Memory accounting excludes owned capacity and makes the memory guard false

Location: [src/lineidx/lineidx.c:215](/home/tobias/Projects/editor/src/lineidx/lineidx.c:215), [src/lineidx/lineidx.c:255](/home/tobias/Projects/editor/src/lineidx/lineidx.c:255), [bench/lineidx_bench.c:210](/home/tobias/Projects/editor/bench/lineidx_bench.c:210).

`lineidx_mem_bytes()` counts occupied entries, not allocated capacity, and excludes active and retired job storage. Both the test and benchmark trust this formula instead of observing owned allocations.

For 1 GiB it reports **264,200 B**, while reserved tables total **529,432 B** and one active job’s arrays add **262,152 B**, excluding object headers.

For an unedited 10 GB mapping, tables plus one active job’s arrays already own **7,363,424 B**, excluding headers. G10f permits **6,882,816 B** for that file. The index alone exceeds the allowance, while the benchmark still reports approximately 16.125 B/chunk and passes.

Repeated restarts queued behind another bulk job also retain multiple full scratch arrays and snapshots.

Fix: account for capacity, summaries, active/retired jobs, and retained owned source storage. Use allocator-observed peak memory in tests and benches. Bound retained build jobs and redesign scratch/capacity storage to fit G10f.

## 13. MAJOR — Worker results bypass the required mailbox handoff

Location: [src/lineidx/lineidx.c:194](/home/tobias/Projects/editor/src/lineidx/lineidx.c:194), [src/lineidx/lineidx.c:272](/home/tobias/Projects/editor/src/lineidx/lineidx.c:272).

Chunk data crosses threads through private atomic `res[]` and `done_n`. Mailbox messages contain only wake-up hints; `lineidx_poll()` accepts results independently of mailbox generation filtering.

The acquire/release protocol can safely publish results under the immutable-source precondition, but it directly violates the project rule that shared state crosses only through `src/work` mailboxes. P1.6 documents this separate transport without reconciling that rule.

Fix: publish sealed result batches or leased result blocks through generation-tagged work messages, and perform bounded adoption after mailbox validation. Put completion/retirement ownership in the work API rather than inspecting its private lifecycle fields.

## 14. MAJOR — The fuzzer eventually stops exercising background builds

Location: [fuzz/lineidx_fuzz.c:25](/home/tobias/Projects/editor/fuzz/lineidx_fuzz.c:25), [fuzz/lineidx_fuzz.c:96](/home/tobias/Projects/editor/fuzz/lineidx_fuzz.c:96).

The persistent work pool is never drained. Every successful small build publishes a final message, retaining that slot through `pending`. Destroying the index does not consume the message.

Concrete repro: repeatedly fuzz a nonempty small input containing a build-and-wait operation. After 64 completed builds, all 64 slots can remain unavailable. Subsequent build failures are silently accepted at line 96. Background-build coverage disappears; the wait variant can instead spend two million sleep/poll iterations waiting for a job that was never submitted.

The default large-file benchmark has another consequence of never draining: its reference build fills the mailbox, so subsequent timed builds drop progress messages rather than exercising normal publication.

Fix: drain mailboxes during and between fuzz cases and benchmark repetitions. Assert successful submission except in deliberate refusal tests, and assert completion after bounded waits. Track successful background operations so exhausted coverage cannot appear clean.

## 15. MAJOR — The G7j benchmark can pass after the target is already indexed and excludes the gate endpoint

Location: [bench/lineidx_bench.c:136](/home/tobias/Projects/editor/bench/lineidx_bench.c:136).

The benchmark waits until the applied prefix is **at least** the requested fraction. Worker progress is unrestricted. If the UI is descheduled, the next poll can apply the whole index before cancellation, turning the timed “partial jump” into an indexed lookup.

`--partial=0.95` provides a direct example: the 90% target is already indexed, yet the row retains the G7j limits. Only one repetition’s actual prefix is printed, and scanned work is never validated.

The timer also ends at byte-offset discovery. G7j ends at the correct viewport submitted. A renderer taking 200 ms would leave this row unchanged and could still produce “all gates met.” The timed interval has no active bulk worker.

Fix: prepare a controlled prefix that stops before the target, verify its size and scanned bytes for every sample, and benchmark worker request through correct viewport submission under the required background load. Keep an offset-only row explicitly tracked if an integration gate is unavailable.

## 16. MAJOR — Benchmark correctness uses the implementation as its own oracle

Location: [bench/lineidx_bench.c:67](/home/tobias/Projects/editor/bench/lineidx_bench.c:67), [bench/lineidx_bench.c:96](/home/tobias/Projects/editor/bench/lineidx_bench.c:96).

The reference line count, target line, and expected offset all come from `lineidx`. Later runs are checked against those same results. The known fixture count, **8,947,842**, is never asserted.

Concrete regression: if the scanner accidentally counts no newlines, the reference yields one line, target zero, and expected byte zero. Later builds reproduce that count and jumps return zero exactly, passing the benchmark’s correctness checks.

`--file` can also substitute a tiny file while retaining the `G7_index_1g_warm` name and 1 GB limits; file size is not checked.

Fix: use an independent scalar oracle or a validated fixture manifest. Verify default size, line count, target **8,053,057**, and target offset independently. Alternate-size fixtures need separate labels and calibrated gates or TRACK status.

## 17. MAJOR — Cold gates have no executable benchmark mode

Location: [bench/lineidx_bench.c:122](/home/tobias/Projects/editor/bench/lineidx_bench.c:122).

Cold G7 is only a printed TRACK sentence; cold G7j is not measured. Evicting caches before launching this benchmark cannot measure either cold gate: the reference full build and warm-up repetitions warm the file before timed samples.

Consequently, cold performance can regress beyond G7 600/800 ms or G7j 250/350 ms while the executable reports “all gates met.”

Fix: provide a cold mode using prevalidated oracle metadata, no cache-warming reference pass, verified eviction before each sample, and the correct cold limits. External eviction may remain manual, but the executable must measure and fail the corresponding rows. Report unvalidated gates explicitly.

## 18. MAJOR — Benchmark completion failures hang instead of returning a miss

Location: [bench/lineidx_bench.c:39](/home/tobias/Projects/editor/bench/lineidx_bench.c:39), [bench/lineidx_bench.c:137](/home/tobias/Projects/editor/bench/lineidx_bench.c:137).

`wait_complete()` has no deadline or failure condition. Partial-prefix and physical-retirement waits are also unbounded.

Concrete repro: `--partial=2` is accepted and requests twice the available chunks. After the index completes, the prefix can never reach that goal, so the benchmark hangs permanently. Nonfinite/out-of-range partial values also reach an unchecked floating-to-integer conversion. A regression that strands completion similarly never produces a gate failure.

Fix: validate numeric arguments with checked parsing, finite/range checks, and explicit workload semantics. Give every wait a monotonic deadline and return a nonzero diagnostic failure with the observed prefix/job state.

| # | Severity | Location | One line |
|---|---|---|---|
| 1 | BLOCKER | `src/lineidx/lineidx.c:380` | Capacity fallback permits huge chunks, truncated exact counts, and bulk UI scans. |
| 2 | BLOCKER | `src/lineidx/lineidx.c:351` | Unchecked length overflow corrupts geometry and reports an exact empty index. |
| 3 | BLOCKER | `src/lineidx/lineidx.c:146` | Epoch wrap falsely acknowledges completion and frees a running job. |
| 4 | BLOCKER | `tests/lineidx_test.c:280` | Concurrent-edit test frees the canceled worker’s still-live source. |
| 5 | MAJOR | `src/lineidx/lineidx.c:101` | Approximate results can point inside a line; fuzz assertion is empty. |
| 6 | MAJOR | `src/lineidx/lineidx.c:489` | Whole-chunk scanning exceeds the supplied seek budget. |
| 7 | MAJOR | `src/lineidx/lineidx.c:483` | Partial jumps scan bulk data synchronously without cancellation. |
| 8 | MAJOR | `src/lineidx/lineidx.c:111` | Whole-table foreground passes and repeated prefix derivation lack slices. |
| 9 | MAJOR | `src/lineidx/lineidx.c:289` | Logical cancellation can be delayed or skip publication suppression; bench is ungated. |
| 10 | MAJOR | `src/lineidx/lineidx.c:181` | Fragmented sources defeat the worker’s 5 ms CPU-slice bound. |
| 11 | MAJOR | `src/lineidx/lineidx.c:235` | Destroy waits behind unrelated bulk jobs on the UI thread. |
| 12 | MAJOR | `src/lineidx/lineidx.c:255` | Memory guard excludes capacity/jobs and misses a 10 GB G10f violation. |
| 13 | MAJOR | `src/lineidx/lineidx.c:272` | Results cross threads outside required generation-filtered mailboxes. |
| 14 | MAJOR | `fuzz/lineidx_fuzz.c:96` | Undrained messages exhaust slots and silently remove build coverage. |
| 15 | MAJOR | `bench/lineidx_bench.c:136` | G7j workload can already be indexed and ends before viewport submission. |
| 16 | MAJOR | `bench/lineidx_bench.c:67` | Self-derived oracle and unchecked fixture size allow false correctness passes. |
| 17 | MAJOR | `bench/lineidx_bench.c:122` | Cold gate regressions cannot fail any implemented row. |
| 18 | MAJOR | `bench/lineidx_bench.c:39` | Invalid workloads and stranded completion cause indefinite waits. |