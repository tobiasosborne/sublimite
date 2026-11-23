# v2-0fv.5 — work pool, mailboxes and foreground lane

The harvest source is the read-only v1 oracle at `6fec2e8`. The UI owns
submission, cancellation, priority, receive and pool lifecycle. Worker jobs
use caller-owned arguments through physical retirement. Job callbacks receive
a worker-local context. Result callbacks receive a UI-local copy while the
slot's result reservation is still held. Foreground is an opt-in CPU lane
with its own worker, queue, mailbox and reserve. Its jobs yield cooperatively.
Bulk retains the worker for blocking I/O.

## Source delta

`diff -u /home/tobias/Projects/sublimite-v1-oracle/src/work/work.c src/work/work.c`
gives these hunks. The ranges locate the original v1 lines.

| v1 hunk | Difference and reason |
| --- | --- |
| 1 | Removed `base/base.h`. The oracle implementation uses no `EDIT_*` or `edit_*` names, so the landed base rename requires zero substitutions in this file. The work tests use the landed `BASE_ASSERT` and `base_malloc_guard_*` APIs. |
| 50 | Documented publication of the worker ID. Kept the result of `trace_thread_register()`. Exhaustion leaves the worker operational without a timing ring, consistent with trace's optional recording contract. |
| 62 | Documented the acquire of the UI's reservation release before a producer handoff. |
| 85 | Documented the retirement and reuse releases. After unlocking on exit, a worker with a registered slot calls `trace_thread_release()` on that same thread. The join therefore returns after the slot is available to later pools. |
| 325 | Documented the submit mutex's publication of the initialized job and cancellation state. |
| 412 | Documented how the invalidating epoch publishes the cancellation timestamp. |
| 423 | Documented head acquisition before ring capacity reuse. |
| 433 | Documented tail release publishing the payload and reservation. |
| 517 | Documented pending release after the UI callback. |
| 544 | Documented the ordinary drain's tail acquire. |
| 581 | Documented the selective receive snapshot's tail acquire. |

The header drops `work_pool_eventfd` and its Windows guard. v2 clients
borrow `work_pool_wake()` for `os_wake_wait()`, or use `os_wake_fd()` in a
platform poll loop. The header guard and introductory comments now describe
the v2 module. The scheduling classes, slot partition, polling and drain
constants, FIFO policy and memory orders match the pinned implementation.
The worker still selects its scheduling class from its fixed identity before
entering the loop. This uses comparisons, so the compiler's switch-enum
check would require an explicit review of this site if classes change.

## State invariants

| State | Owner and invariant |
| --- | --- |
| `work_pool.active`, `shutting_down` | The UI's active bit gates operations that require a live wake and synchronization objects. During private init it also permits common unwind. Under `mu`, shutdown becomes true once and stops all dequeue or requeue. UI operations cannot race lifecycle. |
| `wake`, `mu`, `cv[]`, `foreground_cv` | The wake owns one OS event counter until close. The mutex serializes submit, class changes, dequeue, requeue and cancellation. Each class condition waits under that mutex on queue work or shutdown. Workers signal the wake after publishing a result. The UI clears it before observing the queues of results and re-arms it when work remains. |
| `n_workers`, `n_bulk`, `threads[]`, `wctx[]` | A live pool configures one bulk worker and the requested raster workers. The thread prefix is complete on successful init and is shortened to the successfully started prefix for partial-init unwind. Each live thread token is joined once. Its stable startup context publishes pool and producer identity by thread creation. |
| `foreground_enabled`, `foreground_started`, `foreground_thread`, `foreground_wctx` | Enabled means the lane's condition was initialized and its mailbox is part of receive traversal. Started means a thread must still be joined, including the distinction during failed start. Enabled survives shutdown for stale-message drain. Its startup context uses the fixed `WORK_FOREGROUND_WORKER` identity. |
| `worker_ids[]` | Each worker publishes its OS identity once to its index with release, and readers acquire it. Zero means it has never started in this pool lifetime. After join this is a diagnostic identity only. |
| `queue[]`, `foreground_queue` | Under `mu`, `head` locates the oldest of `count` FIFO entries. The parallel `q[]` and `epochs[]` entries identify the submitted leases in order. Queued cancellation compacts the live range and deactivates only that lease. Priority moves a queued lease between FIFOs under the same lock. |
| `slots[].job` | Submit initializes the function, argument and caller generation together. They remain immutable for the lease. The scheduler's class member is read and changed under `mu`, including upgrades of jobs already running. |
| `slots[].epoch`, `cancel_ns` | The epoch is monotonic within the initialized pool. A job's snapshot is live while equal. Submit reserves enough epoch space for invalidation without wrap. Cancellation publishes its timestamp before invalidation. Shutdown preserves an earlier cancellation stamp. |
| `slots[].busy`, `finished_epoch` | Busy spans enqueue, execution and any requeues without a gap. Only queued removal or execution retirement clears it. Finished epoch records the latest physically retired identity in that slot, so a handle can still prove retirement after reuse. It says when argument ownership returns to the caller. |
| `slots[].pending` | One reservation per publish, from before tail release until after UI drop or callback. The slot remains reserved even after the consumer has copied the payload and reclaimed the ring entry. Selection requires both busy and pending clear. |
| `slots[].mailbox_worker` | Under `mu`, it names the last invocation's producer, or the unused identity on a new lease. Same-producer continuations preserve FIFO in that mailbox. Any different producer waits until pending is zero, including callbacks, to preserve cross-mailbox delivery order. |
| `slots[].receive_epoch`, `receive_generation`, `receive_cb`, `receive_ud` | The UI tracks dispatch ownership by submitted identity and generation. Cancellation can change the current epoch while the UI still needs to remove the old binding. A reused slot clears the binding before publishing its new queue entry. |
| `slots[].receive_scan_generation`, `receive_pos[]`, `receive_foreground_pos`, `receive_next` | Each selected generation has resumable mailbox cursors and a round-robin position. The UI resets them on generation change, validates them against the current ring interval and resets dormant positions when the head wraps. |
| `mb[]`, `foreground_mb` | Each `tail` has one worker producer and each `head` has the UI as sole consumer. The unsigned distance is the number of occupied ring positions and cannot exceed `WORK_MAILBOX_CAP`. The live interval contains stamped `msgs[]` or UI-only `received[]` holes. Only the contiguous consumed prefix is reclaimed. |
| `draining`, `drain_next` | The UI owns one outer receive/drain at a time. The flag spans callbacks and rejects nested receive. The ordinary drain's cursor names the next mailbox visited for fairness. |
| `dropped_stale`, `dropped_full` | Diagnostics count stale drops at publication or consumption and full-ring drops at publication. Relaxed increments count independently of payload visibility. |
| `work_ctx`, `work_handle`, `work_msg` | A job context identifies its immutable argument and epoch snapshot and the current producer. Its continuation flag is private to that invocation. A handle is the UI's slot/epoch identity within one pool lifetime. Publish stamps the message's slot/epoch and copies the caller's kind, generation and data. |

## Synchronization

The submit mutex publishes job initialization and batch handle outputs
before any worker dequeues them. A worker invoking the job reads immutable
fields after unlocking. The same mutex orders the end of one invocation
before a subsequent invocation. It also orders a continuation's requeue
decision against cancel, priority and shutdown.

The release side of the epoch bump publishes invalidation and its preceding
relaxed timestamp store. A stopping worker acquires the epoch before
reading the stamp. It holds the running lease, so reuse cannot clear that
stamp while it is still reading it. Release completion publishes worker
writes to the physical handle query, while release of busy and pending
provide the acquires used for slot selection. The worker takes the pending
acquire on a mailbox migration, so it can publish only after the old
producer's last result callback has finished.

A release of tail publishes both the payload and its preceding relaxed
pending increment. The consumer acquires tail before inspecting the ring.
Release of head publishes reclamation before the producer's acquire allows
overwrite. The producer's own tail and the consumer's own head can be read
relaxed. The event counter makes the UI runnable. Payload synchronization
comes from the ring atomics. Shutdown's join covers the final worker
publication, physical retirement and trace release.

## Verification changes and exclusions

- The Linux work suite retains recursive drain, caller alignment, epoch
  exhaustion, shutdown and timestamps, init unwind, cancellation FIFO,
  reserves, bounded draining, live SPSC integrity, full-ring wrap and the
  randomized state machine. It exercises batch failure atomicity and output
  publication, selective receive with foreign holes and a dormant wrapped
  cursor, bind/unbind, and physical retirement across reuse.
- Foreground unit cases cover init and failure unwind, reserved capacity and
  mixed batch, FIFO cancellation and shutdown with stale results, wrap and
  bound dispatch, concurrent traffic in all three classes, continuations
  in each class, queued priority, running priority and mailbox ordering at
  handoff. A real pipe tests foreground progress and delayed physical
  retirement while the only bulk worker is blocked. Synthetic index/find/save
  labels, their repeated scan variants, and the hardcoded AC latency report
  were dropped. The L0 fixture asserts progress under the inherited watchdog.
- The v1 `core_sync.h` Windows and POSIX adapters, libc interposers, runtime
  symbol lookup and sanitizer-specific interceptor branch were dropped.
  Tests use aligned storage directly, OS wake waits for readiness, and a
  Linux pipe local to the blocking fixture. The init-error suite wraps the
  OS boundary at link time and observes real OS starts, joins and descriptor
  closure. The alignment audit of absent higher-layer v1 callers was dropped.
- The allocation window is a dedicated release suite. One counted allocation
  proves base's interposer executes before the guarded window. The window
  covers repeated live and stale drains, running and queued cancellation,
  same-slot reuse with stale handles, mixed batch submission, selective
  receive, binding and retirement. Setup and teardown surround that window.
- The v1 fuzzer keeps its two raster workers and the same lease operations.
  It uses the OS clock and sleep directly because v2 trace removed the clock
  adapter. The seed `fuzz/corpus/work/lanes` covers all operation codes.
  Its opening mixed batch starts jobs in all three classes, followed by
  additional raster submissions, priority, bind, selective receive, unbind,
  cancellation and ordinary drain.
- The benchmark tracks submit and cancel call cost, submit-to-start,
  one-message drain, observed physical retirement and foreground delivery
  while bulk is held. It uses the v1 sample count and start-wait bound,
  and the landed trace percentile helper. The v1 heap-sized scan fixture,
  paired FIFO delays, benchmark self-check modes, subprocess watchdog and
  benchmark-local timing gates were omitted. L0 tests own the correctness
  checks. The benchmark stamps battery state and load once per run and
  exits zero.
- The v1 `work_posix.c` and `STATUS.md` are omitted. Production imports
  OS wake operations directly. No symbol grants were required.
- **Gap for the coordinator to file**. Real-client foreground integration
  returns with lineidx (L1) and find UI (L4). Re-establish priority,
  continuation order and physical retirement with real snapshot-owning
  clients while bulk I/O is blocked. The L0 work tests use synthetic clients.

## Red and green

With `HOME="$PWD/build/home"` and `TMPDIR="$PWD/build/tmp"`, before adding
the source:

```text
$ make --no-print-directory -s build/rel/tests/work_test
tests/work_test.c:1:10: fatal error: work/work.h: No such file or directory
    1 | #include "work/work.h"
      |          ^~~~~~~~~~~~~
compilation terminated.
make: *** [Makefile:123: build/rel/tests/work_test.o] Error 1
make: *** Waiting for unfinished jobs....
```

The new test uses `TRACE_MAX_THREADS + 1` pool lifetimes to overflow the
trace registry if any joined worker keeps its slot. With the v1 worker
loop copied into v2:

```text
$ make --no-print-directory -s build/rel/tests/work_test && build/rel/tests/work_test trace
tests/work_test.c:681: FAIL seen == lifetimes
work_test: trace pool lifetimes recorded=16/17
work_test: trace_pool_lifetimes FAIL
work_test: FAIL
exit=1
```

With the checked registration and matching release in place:

```text
$ make --no-print-directory -s build/rel/tests/work_test && build/rel/tests/work_test trace
work_test: trace pool lifetimes recorded=17/17
work_test: trace_pool_lifetimes ok
work_test: ok
exit=0
```

## Acceptance

All commands use the checkout-local HOME and TMPDIR above. The complete
logs are under `build/work-evidence/`. The lane suffixes are:

```text
$ make all
exit=0

$ make check
check-layers: 6 isolated layer links passed (L0-L4 tests, L5 via main.o)
check-san: 10 test binaries passed
== build/rel/tests/law2_work_test
work guard positive control: active=1 count=1
work submit/cancel/drain/reuse Law-2 window: active=1 count=0
law2_work_test: PASS
check-law2: 3 release suites passed
check: ok
exit=0

$ make check-tsan
work_test: trace pool lifetimes recorded=17/17
work_test: trace_pool_lifetimes ok
work_test: ok
check-tsan: 4 threaded suites passed
exit=0

$ make fuzz-smoke
fuzz-smoke: 3 fuzzers replayed their seeds
exit=0
```

## Benchmark

Both runs use the same synthetic v2 harness and release flags. The
comparison compiles the unmodified oracle `work.c` against the v2 headers
and OS/trace implementations, writing its executable under this checkout's
`build/`. It isolates the work implementation changes from harness changes.
The results track scheduler and OS wake costs under the stamped load.
Registration and release occur outside the timed per-job trials.

```sh
make build/rel/bench/work_bench
gcc -std=c11 -Wall -Wextra -Werror -Wshadow -Wconversion -Wswitch-enum \
    -D_GNU_SOURCE -O2 -g -msse2 -pthread -Isrc bench/work_bench.c \
    /home/tobias/Projects/sublimite-v1-oracle/src/work/work.c build/rel/libcore.a \
    -lm -ldl -o build/work-evidence/work_bench-v1
```

```text
$ build/work-evidence/work_bench-v1
(M)[AC] BAT0=Charging load1=6.29 at start, 1000 trials
work_submit_call p50=43508 ns p99=64988 ns
work_submit_to_start p50=32454 ns p99=51977 ns
work_drain_one p50=1262 ns p99=1959 ns
work_cancel_call p50=293 ns p99=569 ns
work_cancel_to_observed_retirement p50=16930 ns p99=29421 ns
work_foreground_delivery_while_bulk_held p50=37621 ns p99=58831 ns
exit=0

$ build/rel/bench/work_bench
(M)[AC] BAT0=Charging load1=6.29 at start, 1000 trials
work_submit_call p50=40631 ns p99=66404 ns
work_submit_to_start p50=30368 ns p99=54388 ns
work_drain_one p50=1209 ns p99=2005 ns
work_cancel_call p50=273 ns p99=569 ns
work_cancel_to_observed_retirement p50=15817 ns p99=26245 ns
work_foreground_delivery_while_bulk_held p50=34114 ns p99=53525 ns
exit=0
```

## Files

- `src/work/work.c` and `src/work/work.h`. `wc -l` reports 643 and 249 lines respectively, 892 total.
- `tests/work_test.c`, `tests/law2_work_test.c` and `tests/lanes.mk`.
- `fuzz/work_fuzz.c` and `fuzz/corpus/work/lanes`.
- `bench/work_bench.c` and `Makefile`.
- `docs/decisions/v2-0fv.5.md`.
