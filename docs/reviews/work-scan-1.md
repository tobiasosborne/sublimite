18 findings: **3 BLOCKER, 14 MAJOR, 1 MINOR**.

Reviewed the requested modules, decisions, gates, tests, fuzzers, benchmarks, and relevant callers. The reviewed files remained unchanged while main advanced to `a5cf5ba`. I changed no files.

Existing release and ASan/UBSan work/scan tests passed; LeakSanitizer was disabled. Additional source-based repros ran entirely through piped LLVM IR and `lli`, with trace registration stubbed. Performance gates were not measured on the loaded machine.

The current SSE2 implementation passed guarded-page differential checks for every length from 0 through 8192. I found no current scan overread, accumulator overflow, or CPU-dispatch defect. SSE2-only operation follows the P1.2 decision.

## 1. BLOCKER — Recursive mailbox drain duplicates messages and corrupts slot reservations

Location: [src/work/work.c:195](/home/tobias/Projects/editor/src/work/work.c:195), `src/work/work.c:203`, `src/work/work.c:208`.

Drain retains local `h` and `t` across callbacks. A callback can recursively drain the same pool, advancing its shared head and decrementing pending counts. The outer invocation then resumes from its obsolete local head.

**Repro:** Publish two messages, finish the job, and make the first callback recursively call `work_mailbox_drain`. The executed repro produced:

```text
published=2 callbacks=3 outer_return=2 pending=4294967295
```

The second message is delivered twice. Its extra decrement underflows `pending`, permanently reserving the slot. Duplicate callbacks can also repeat application mutations or resource releases. The header specifies UI-thread ownership but prohibits neither recursion nor synchronous operations that pump mailboxes; `journal_flush` itself drains mailboxes.

**Fix:** Define and enforce callback reentrancy semantics. Either reject nested drains and require callbacks to defer synchronous pumping, or implement consumption without retaining an obsolete cursor across callbacks. Add this exact two-message regression.

## 2. BLOCKER — Existing heap allocations violate `work_pool`’s required alignment

Location: [src/work/work.h:67](/home/tobias/Projects/editor/src/work/work.h:67), `bench/font_bench.c:108`, `tests/layout_test.c:468`, `fuzz/layout_fuzz.c:106`.

The mailbox’s `_Alignas(64)` members make the entire pool require 64-byte alignment. Several callers allocate it with ordinary `malloc`, which does not guarantee that extended alignment. Access through these pointers is undefined behavior.

**Repro:** Clang reports `sizeof(work_pool)=153344`, alignment 64. Four ordinary allocations of that size on this machine all returned addresses congruent to **16 modulo 64**. Those are the allocations used by the affected callers before `work_pool_init` dereferences them.

**Fix:** Use `aligned_alloc(_Alignof(work_pool), sizeof(work_pool))`, a checked `posix_memalign`, or suitably aligned caller storage. Document the storage alignment requirement and correct every heap-allocated pool caller.

## 3. BLOCKER — Epoch wrap permits stale handles to cancel unrelated jobs

Location: [src/work/work.c:109](/home/tobias/Projects/editor/src/work/work.c:109), `src/work/work.c:143`.

A handle’s identity is only its slot and 32-bit epoch. Skipping zero preserves the invalid sentinel but eventually repeats every valid token. Once repeated, the stale-handle precheck and CAS accept an old handle as the replacement job’s handle.

**Repro:** A boundary-injection test retained an old handle with epoch 2, placed the slot at the state reached after a complete epoch cycle, then submitted another job:

```text
old=2 new=2 stale_cancel_stopped_new=1
```

This contradicts the stale-handle no-op contract and can suppress a replacement save or other result. Reinitialization also resets all epochs, so handles cannot distinguish pool incarnations.

**Fix:** Use a substantially wider submission identity, or retire slots before identity wrap. Explicitly define handle invalidation across pool reinitialization. Test wrap boundaries with retained old handles.

## 4. MAJOR — Shutdown leaves dropped queued jobs permanently busy

Location: [src/work/work.c:29](/home/tobias/Projects/editor/src/work/work.c:29), `src/work/work.c:81`.

Workers exit immediately when `shutting_down` becomes true. Queued jobs consequently never reach the `busy=0` store, and shutdown does not clear their busy bits or queues.

**Repro:** Run one polling job, queue another, then shut down. The executed repro reported:

```text
queued_busy=1 queue_count=1 queued_epoch_delta=1
```

A concrete consumer failure follows: `file_close` waits at `src/file/file.c:435` while busy remains set. For a dropped queued open/save, its completion flag remains zero and its epoch is exactly `h.epoch+1`, so the alternative exit condition also fails. Cleanup waits forever after workers have already joined.

**Fix:** Retire queued jobs during shutdown and clear their busy state. After joining, establish that every slot is physically finished. Preserve caller ownership of arguments so their resources can then be released. Assert these postconditions in the shutdown test.

## 5. MAJOR — Submission after shutdown accesses a destroyed mutex

Location: [src/work/work.c:113](/home/tobias/Projects/editor/src/work/work.c:113), `src/work/work.c:92`.

The advertised shutdown rejection checks `shutting_down` only after locking `mu`. Completed shutdown has already destroyed that mutex.

**Repro:** Initialize, shut down, then submit into the still-existing caller-owned pool storage. An executed wrapper observed:

```text
work_submit epoch=0 ignored pthread_mutex_lock error=22
```

Although the returned handle happens to be invalid on glibc, accessing the destroyed mutex is invalid. Slot job/epoch fields are also modified before rejection.

**Fix:** Reject a shut-down pool before touching slots or synchronization objects. State and enforce the lifecycle/threading contract, and test submission after shutdown.

## 6. MAJOR — Synchronization initialization failures are ignored

Location: [src/work/work.c:60](/home/tobias/Projects/editor/src/work/work.c:60).

Neither mutex initialization nor either condition-variable initialization is checked. Pool initialization can therefore proceed to create workers and report success after a synchronization initializer reported failure.

**Concrete argument:** Inject `ENOMEM` from the second `pthread_cond_init`. The code still returns success and later broadcasts/destroys that uninitialized condition variable. Injecting mutex initialization failure similarly permits lock operations on an uninitialized mutex. The thread-creation failure path also assumes all synchronization initialization succeeded.

**Fix:** Check each return code, track which resources initialized successfully, and unwind only those resources. Add failure injection for every initialization stage.

## 7. MAJOR — Cancelled bulk queues can exhaust all foreground job capacity

Location: [src/work/work.c:103](/home/tobias/Projects/editor/src/work/work.c:103), `src/work/work.c:133`.

Both classes share all 64 slots. Cancelling a queued job does not dequeue it or release its busy reservation. The bulk worker must eventually reach it, even when its current job is waiting on permitted long I/O.

**Repro:** Keep one bulk job active, queue and cancel 63 additional bulk jobs, then submit raster work. The executed repro reported:

```text
idle raster worker, raster_handle_epoch=0 bulk_queue=63
```

Thus cancelled background requests can prevent foreground rendering while its worker is idle. Draining cannot help because these slots contain no messages. This undermines foreground gates under background activity.

**Fix:** Remove and retire queued cancellations under the queue lock, and reserve sufficient slot capacity for foreground/raster work independently of bulk reservations. Test cancellation churn with an active bulk job and idle raster workers.

## 8. MAJOR — Mailbox draining provides no UI slice budget

Location: [src/work/work.c:191](/home/tobias/Projects/editor/src/work/work.c:191).

One call invokes callbacks for every message in each captured mailbox tail: up to 2304 messages. There is no message limit, deadline, continuation result, or pending-input check.

**Concrete argument:** A legal full mailbox with callbacks costing 10 µs consumes at least 2.56 ms in callbacks alone. This exceeds the required 0.5 ms UI slice. Such an invocation cannot yield between subsets of messages through the current API.

**Fix:** Provide bounded draining by message count and/or deadline, with input checks between slices. Require individually bounded callbacks. When a partial drain leaves messages after clearing eventfd, preserve or re-arm notification so continuation cannot sleep indefinitely.

## 9. MAJOR — Real bulk jobs violate the cancellation polling contract

Location: [src/lineidx/lineidx.c:193](/home/tobias/Projects/editor/src/lineidx/lineidx.c:193), `src/lineidx/lineidx.c:380`, `src/font/fallback.c:159`.

The index worker checks cancellation between entries, but an edited entry can become arbitrarily large when the fixed table cannot split it. `scan_range` then scans that entire entry without a cancellation check. The scan functions themselves correctly implement their unrestricted range API; the worker supplies an unrestricted polling interval.

**Repro:** Create a one-byte index and insert 1 GiB. Its 66-entry capacity cannot accommodate the split, leaving one enlarged entry. Cancel while its first source span is entered. The executed repro showed the worker requesting **16,384 further spans** before finishing the entry.

Font fallback also performs its whole fontconfig discovery without polling; its next cancellation check is publication after discovery. Configuration-dependent CPU work can occupy the sole bulk worker without the required bound.

**Fix:** Poll inside bounded index subranges and discard incomplete results on cancellation. Make font discovery cancellable through bounded stages or an isolation mechanism that can stop its computation. Exercise production jobs in cancellation contract tests, rather than only the benchmark’s synthetic 64 KiB loop.

## 10. MAJOR — Work benchmark measures the wrong cancellation interval

Location: [bench/work_bench.c:27](/home/tobias/Projects/editor/bench/work_bench.c:27), `bench/work_bench.c:65`.

The sample begins at a timestamp recorded *inside* `work_cancel` and ends when the worker observes cancellation. It does not measure caller request-to-logical-acknowledgement, nor the worker’s CPU-time slice bound.

**Concrete argument:** Add a 20 ms delay after the epoch CAS but before `work_cancel` returns. The worker can record its small observation interval immediately, while the UI call takes 20 ms. The existing gated sample still passes despite the logical acknowledgement regression. Conversely, worker descheduling or allowed I/O latency can inflate this wall-time metric without violating a CPU-slice bound.

The related find benchmark records logical cancellation separately, but reports it with zero gates at `bench/find_bench.c:188`.

**Fix:** Gate caller-side request-to-return logical acknowledgement at 1/5 ms. Independently check publication suppression and worker CPU-time polling intervals. Track physical cleanup/wall-time observation separately.

## 11. MAJOR — Work benchmark can hang instead of failing

Location: [bench/work_bench.c:62](/home/tobias/Projects/editor/bench/work_bench.c:62), `bench/work_bench.c:66`, `bench/work_bench.c:79`.

All three acknowledgement/start waits are unlimited busy loops. Submission failures are also unchecked.

**Concrete argument:** A lost worker wakeup never sets `started`; a cancellation regression never sets `ack_ns`; a refused stamp submission never sets `start_ns`. Each case prevents the benchmark from reaching its nonzero gate result. The repository benchmark driver supplies no timeout.

**Fix:** Check every handle and give each wait a deadline. Treat timeout as a failed sample and nonzero result, print its cause, and ensure cleanup cannot indefinitely wait for the same broken worker. Apply deadlines to the corresponding work-test waits.

## 12. MAJOR — Scan benchmark reports PASS despite arbitrary tail regression

Location: [bench/scan_bench.c:32](/home/tobias/Projects/editor/bench/scan_bench.c:32).

The benchmark prints p99 but its result depends only on p50. P1.2’s p50 throughput guard therefore supplies no regression guard for the binding G6/G7 tail budgets.

**Repro:** Calling the actual report function with 19 synthetic samples at 16 GB/s and one at 1 GB/s produced:

```text
p50 16.00 GB/s  p99 1.00 GB/s  gate >= 12.0  PASS
exit status=0
```

The slow scan would take over a second for this fixture.

**Fix:** Retain the explicitly documented P1.2 p50 criterion, add and enforce a documented p99 module budget, and verify the appropriate cached-file/new-mapping end-to-end G6/G7 rows after scan hot-path changes. Do not present the primitive PASS as proof of those gates.

## 13. MAJOR — Scan benchmark uses the implementation as its correctness oracle

Location: [bench/scan_bench.c:69](/home/tobias/Projects/editor/bench/scan_bench.c:69), `bench/scan_bench.c:84`.

Warm-up derives the expected newline count from `scan_count`, then asks for that count’s last newline. It merely checks for a non-null pointer. Timed counts and pointers are never checked.

**Concrete argument:** A regression that scans only the first 64 KiB for larger requests returns a smaller count. Warm-up then finds that prefix’s last newline successfully. Both timed rows perform a short prefix operation but calculate throughput using the entire 1 GiB. The benchmark passes spectacularly while missing almost all data. The standalone scan suite’s largest input is only 4113 bytes.

**Fix:** Accumulate independent expected counts while generating the fixture. Require the last newline pointer to equal `buf+SIZE-1`, and validate every timed result against independent expectations. Add large-range correctness cases.

## 14. MAJOR — P1.8b causes the persistent line-index fuzzer to exhaust its pool

Location: [fuzz/lineidx_fuzz.c:25](/home/tobias/Projects/editor/fuzz/lineidx_fuzz.c:25), `fuzz/lineidx_fuzz.c:96`.

The fuzzer initializes one persistent pool and never drains its mailboxes. Completed builds publish progress messages, so P1.8b retains their slots permanently. Submission failure is accepted without failing the fuzz case.

**Repro:** Execute input `00 01 00 00 02 02 00` 64 times through the actual fuzz entry point. The executed repro reported:

```text
reserved=64 busy=0 next_submit_epoch=0
```

Subsequent background build attempts are refused, so long campaigns stop testing successful asynchronous builds. Inputs requesting completion can additionally spend their entire two-million-iteration wait after a refused submission.

**Fix:** Drain the persistent pool during and between cases, assert successful asynchronous coverage continues, and wait for completion only after successful submission. Add a campaign-level reservation-leak assertion.

## 15. MAJOR — Work tests bypass the synchronization edges they need to verify

Location: [tests/work_test.c:67](/home/tobias/Projects/editor/tests/work_test.c:67), `tests/work_test.c:110`, `Makefile:14`.

Publication tests wait for `ran` or `busy` before draining. Those acquire/release or sequentially consistent operations independently publish the message writes, hiding mistakes in the mailbox tail/head synchronization. Raster testing submits only a no-op.

**Concrete argument:** Weakening the mailbox tail release to relaxed can leave these tests passing because `ran` supplies the missing happens-before edge. They do not exercise producer/consumer overlap, mailbox fullness, index wrap, or concurrent mailbox producers. ASan/UBSan cannot establish race freedom, and running this same synchronized fixture under TSan does not remedy its coverage gap.

**Fix:** Add live producer/consumer integrity tests whose message visibility depends on the mailbox synchronization itself, full/wrap tests, and a scheduling/state-machine driver under TSan. Add foreground coexistence validation with actual bulk activity; the existing idle submit-start row has no gate.

## 16. MAJOR — Scan suite’s claimed exact-bound overread protection is false

Location: [tests/scan_test.c:26](/home/tobias/Projects/editor/tests/scan_test.c:26), `tests/scan_test.c:51`.

The comment promises an exact `n+off` allocation, but the code allocates `n+off+1`. Therefore `p[n]` is addressable and ASan cannot catch a one-byte overread. Only offsets 0–3 are exercised, and there is no protected-page test or dedicated scan fuzzer.

**Concrete argument:** Add a discarded read of `p[n]` for nonempty input. Outputs can remain correct and this suite’s ASan checks remain silent, while the same read at a protected mapping boundary faults. Related line-index fuzz spans also end inside larger allocations.

**Fix:** Allocate exact bounds, exercise all 16 alignments, and add protected pages before/after ranges. Add scalar differential fuzzing for all three APIs, including chunk boundaries, all byte values, empty ranges, and extreme `k`.

## 17. MAJOR — Mailbox UI allocation contract lacks an enforcing test

Location: [tests/work_test.c:57](/home/tobias/Projects/editor/tests/work_test.c:57), `src/work/work.c:185`.

The work suite never enables the counting allocator. Existing indirect submit checks do not establish zero allocations across live drain, cancellation, refusal, and slot-reuse paths.

**Concrete argument:** Introducing a temporary heap copy in `work_mailbox_drain` would preserve existing work-test results. The work benchmark never drains messages, so it also supplies no protection against that Law-2 regression. ASan builds disable the counting interposer.

**Fix:** Add an executed release counting test covering repeated submit/cancel/drain/reuse and failure paths after initialization, with allocation-free worker fixtures. Assert that the allocator guard is active and that the UI-side allocation count stays zero.

## 18. MINOR — Shutdown cancellation returns a zero cancellation timestamp

Location: [src/work/work.c:81](/home/tobias/Projects/editor/src/work/work.c:81), `src/work/work.c:152`.

Shutdown invalidates epochs without setting `cancel_ns`. A worker consequently observes `work_should_stop=true` but `work_cancel_time_ns=0`, despite the header describing zero as “not cancelled.”

**Repro:** A running job recorded its cancellation timestamp after observing shutdown. The executed result was zero.

**Fix:** Stamp shutdown cancellation before publishing epoch invalidation, or explicitly document the zero-timestamp shutdown case and require measurement callers to handle it.

| # | Severity | Location | One line |
|---|---|---|---|
| 1 | BLOCKER | `src/work/work.c:195` | Recursive drain duplicates messages and underflows pending reservations. |
| 2 | BLOCKER | `src/work/work.h:67` | Existing malloc-based callers violate the pool’s 64-byte alignment. |
| 3 | BLOCKER | `src/work/work.c:109` | Epoch reuse lets stale handles cancel replacement jobs. |
| 4 | MAJOR | `src/work/work.c:29` | Shutdown leaves dropped queued slots busy and cleanup waiting forever. |
| 5 | MAJOR | `src/work/work.c:113` | Post-shutdown submission accesses a destroyed mutex. |
| 6 | MAJOR | `src/work/work.c:60` | Synchronization initialization errors are ignored. |
| 7 | MAJOR | `src/work/work.c:103` | Cancelled bulk jobs can consume every foreground slot. |
| 8 | MAJOR | `src/work/work.c:191` | Drain cannot enforce the UI slice budget. |
| 9 | MAJOR | `src/lineidx/lineidx.c:193` | Enlarged index entries and font discovery bypass bounded polling. |
| 10 | MAJOR | `bench/work_bench.c:27` | Cancellation benchmark measures neither required G6c interval correctly. |
| 11 | MAJOR | `bench/work_bench.c:62` | Missing deadlines turn failures into benchmark hangs. |
| 12 | MAJOR | `bench/scan_bench.c:32` | Arbitrarily slow p99 samples still report PASS. |
| 13 | MAJOR | `bench/scan_bench.c:69` | Self-derived expectations let incorrect prefix scans pass. |
| 14 | MAJOR | `fuzz/lineidx_fuzz.c:25` | Undrained persistent mailboxes eventually disable asynchronous fuzz coverage. |
| 15 | MAJOR | `tests/work_test.c:67` | Extra synchronization masks mailbox ordering defects. |
| 16 | MAJOR | `tests/scan_test.c:51` | Allocation slack hides boundary overreads; guarded/fuzz coverage is absent. |
| 17 | MAJOR | `tests/work_test.c:57` | UI mailbox paths lack an enforcing allocation contract test. |
| 18 | MINOR | `src/work/work.c:81` | Shutdown stop reports no cancellation timestamp. |