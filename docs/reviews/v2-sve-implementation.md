# v2-sve implementation report

## Design in five lines

1. `work_continue` keeps the same lease, and a full refusal makes its next invocation wait for space in the producing mailbox.
2. The class FIFO skips parked entries so another runnable job can use the worker.
3. The producer arms and rechecks capacity before waiting on its OS wake and wait set. Ordinary idle workers sleep on the existing class condition variable.
4. Ordinary drain and selective receive publish reclaimed ring capacity before waking its producer, without a UI lock or allocation.
5. Parked cancel and shutdown use queue retirement, and physical completion is visible before the UI pool wake. [Decision](../decisions/v2-sve.md)

## Red and green

Each focused case exited 1 against the baseline and 0 after the fix, under both ASan/UBSan and TSan. Those runs were at 05:14–05:24 CET on 2026-11-25, on battery (BAT0 Not charging). The observed one-minute load was 0.33 before the red build and 1.34 after the focused green runs.

| Test | Red | Green |
| --- | --- | --- |
| `full-idle` | Calls increased from 129,763 to 583,765 under ASan/UBSan and 11,835 to 54,646 under TSan. | One invocation at both observations in both lanes. A second bulk job ran while the mailbox stayed full. |
| `full-drain` | The retry ran repeatedly before either form of drain freed space. | One retry published after ordinary or selective drain. Selective drain before the first invocation returned also resumed the job. |
| `full-cancel` | The retry could still be running at cancel and had made extra calls. | Cancel retired the queued lease immediately. A repeated cancel did not change the epoch, and drain did not invoke it again. |
| `full-shutdown` | Shutdown returned after extra calls while full. | Shutdown returned with the lease retired and its queue empty, after exactly one invocation. |
| `finish-wake` | Synchronous queued cancellation and worker retirement after running cancellation both left an empty mailbox without a wake. | Each completion left a waitable wake with the finished epoch and cleared busy bit already visible. |

The two held-full observations follow one and five cancellation-poll quanta, with each quantum taken from the existing 5 ms worker contract. The assertion counts the one invocation needed to discover fullness. It applies the same bound to both observations and makes no throughput claim. The complete `work_test` suite also passed in both configurations, including per-acquisition descriptor unwind checks.

## Owner review revision

- `work_space_state` merges refusal and parking as phases of the slot's lease. The dequeuing worker owns the field during invocation. Queue transitions and retirement use `pool.mu`.
- `work_wait_mode` selects the worker's wake channel. The worker writes it and the class notifier reads it under `pool.mu`.
- `work_mailbox_arm` uses one atomic `uint32_t`. The producer arms before rechecking capacity. The UI disarms after reclaim without taking `pool.mu`. [Derivation](../decisions/v2-sve.md#state-and-ownership)

The five existing tests passed again in focused ASan/UBSan and TSan runs. The enum refactor rules out representing a running refusal and a queued park on the same lease at once. That combination was already excluded from scheduler behavior by queue ownership, so the test set stays the same.

## Size

Physical diff lines against the saved baseline, excluding this report and the decision.

| File | Removed | Added |
| --- | ---: | ---: |
| `src/work/work.c` | 89 | 224 |
| `src/work/work.h` | 13 | 42 |
| `tests/work_test.c` | 11 | 194 |
| `Makefile` | 1 | 2 |
| Total | 114 | 462 |

Comment-free nonblank physical lines were measured with `gcc -fpreprocessed -dD -E -P`.

| File | Bead baseline | Before owner review | After owner review |
| --- | ---: | ---: | ---: |
| `work.c` | 504 | 585 | 626 |
| `work.h` | 108 | 116 | 128 |

## Outside scope

L2-1 adopts the continuation at its merge. An ordinary raster continuation picked up by a different worker can still requeue while the prior producer's result reservations are outstanding. That path predates full-mailbox parking.

## Initial implementation acceptance lanes

The first `check` run failed the export gate because the initial implementation used wait sets for all worker sleeps, leaving five OS condition exports without a production caller. The revised implementation uses class conditions on the ordinary idle path and the wait set for mailbox parking. A focused `check-exports` passed with that implementation.

- `nice make -j2 check CONTRACT_CHECK_DISPLAYS=:180-:189` exited 0. Six isolated layer links, 32 sanitizer test binaries, eight Law 2 release suites and 64 contract cases passed. The export check also reported non-gating counts for piece (12), undo (17) and journal (28). Started at 05:24:20 CET with a one-minute load of 0.68. [Log](../../build/v2-sve/final-check.log)
- `nice make -j2 check-tsan` exited 0. All 12 threaded suites passed, including every new work test. Started at 05:26:26 CET with load 0.86. [Log](../../build/v2-sve/final-tsan.log)
- `nice make -j2 fuzz-smoke` exited 0. All 15 fuzzers replayed their seeds. Started at 05:26:55 CET with load 0.92. [Log](../../build/v2-sve/final-fuzz.log)

All three final lanes ran on 2026-11-25 with BAT0 Not charging.

## Owner review acceptance lanes

- `nice make -j2 check CONTRACT_CHECK_DISPLAYS=:180-:189` exited 0. Six layer links, 32 sanitizer test binaries, eight Law 2 suites and 64 contract cases passed. Started at 05:30:56 CET with a one-minute load of 0.26. [Log](../../build/v2-sve/owner-final-check.log)
- `nice make -j2 check-tsan` exited 0. All 12 threaded suites passed. Started at 05:32:56 CET with load 0.73. [Log](../../build/v2-sve/owner-final-tsan.log)
- `nice make -j2 fuzz-smoke` exited 0. All 15 fuzzers replayed their seeds. Started at 05:33:28 CET with load 0.87. [Log](../../build/v2-sve/owner-final-fuzz.log)

All three lanes ran on 2026-11-25 with BAT0 Not charging. The held-full test recorded one invocation at both observations in both sanitizer lanes.
