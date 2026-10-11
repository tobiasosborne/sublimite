# edit-cx8 worker report: raster G1 `allocations=N structural=FAIL` and the off-by-one completion

Branch `wt/edit-cx8`. Decision record: `docs/decisions/edit-cx8.md`.

## Verdict

This was not a law-2 violation. Nothing allocates on the UI typing path. The
counting guard in `src/base/malloc_guard.c` is process-wide. Its window runs
from UI ingress to submit, and it also counted **worker-thread library
allocations** that law 2 already exempts (P2.0 addendum, items 1 and 3). The
"one completion short" (really one extra) is **foreign X input** on the
shared Xvfb :99. The bench took no display lock.

## Root cause (file:line, thread)

Method: I temporarily instrumented `count_call()` to dump `backtrace()`,
`gettid()` and `getpid()` for each counted call while the guard was on, then
removed it. Results, all (M)[AC]:

1. **Raster fence worker (most counts).** Location: `src/raster/raster.c`
   `fence_job_fn` (main: lines 212 and 220, now 239/247/263). `xcb_poll_for_reply`
   and `xcb_poll_for_special_event` call libxcb's packet reader, which mallocs
   each fence reply and each Present Complete/Idle event. Thread: a raster
   pool worker. In the 500-key serial run, 95 of 95 counted calls were on
   4 worker tids and 0 on the main tid. In the 2000-key four-scenario run,
   180 of 180 were on workers. The job retires frame N (T5/T6) while the UI
   thread already has key N+1's window open (`arrivals_active=10000` of
   10000 keys).
2. **x11 keymap worker (sporadic).** Location: `src/x11/x11.c`
   `rebuild_keyboard` (main: line 165 `xkb_x11_get_core_keyboard_device_id`,
   line 167 `xkb_x11_keymap_new_from_device`). libxkbcommon-x11 and libxcb
   build a new keymap after a server MapNotify. Thread: the x11 runtime
   worker. One captured run had 33 counted calls, all from this job. Trigger:
   another worktree's suite changed the keymap on :99.
   `tests/x11_order_test.c:452` runs `setxkbmap -display :99` with the display
   hard-coded (see out-of-scope below).
3. **Off-by-one completion.** The lwq signature was `invalid=1 incomplete=1
   completed=2280 expected=2279`. A key event from another X client reaches
   the 2880x1800 bench window, which covers the whole Xvfb screen. The editor
   assigns it an input sequence the bench never injected
   (`src/editor/input.c:477`), so every later sequence shifts by one.
   Reproduced deterministically on a private `Xvfb :1371` with one
   `xdotool key x` during the raster row:
   `STRUCTURAL name=editor_raster_serial_injection_T4_G1 structural=FAIL
   invalid=1 incomplete=1 completed=24 expected=23 ... allocations=0`.

## Fix

- `src/base`: added `edit_malloc_guard_exempt_begin/end()`, a thread-local
  exemption that nests. It brackets only exempt library calls on the calling
  thread. Our own code in those jobs (`post`, `publish`) and all other threads
  are still counted. Both calls are no-ops under sanitizers.
- `src/raster/raster.c`: three wrappers (`fence_poll_reply`,
  `fence_poll_special`, `fence_poll_event`) bracket the libxcb polls in
  `fence_job_fn` with the exemption.
- `src/x11/x11.c`: `rebuild_keyboard` brackets its libxkbcommon/libxcb
  keymap construction with the exemption.
- `bench/editor_accounting.h` and `bench/editor_bench.c`: the bench counts
  input sequences it never injected. They are reported as `foreign_input=` in
  the G1 line and the STRUCTURAL diagnostic, so a contaminated row names its
  cause.
- `Makefile`: `make bench` now runs display benches (editor*, x11*, gl*,
  raster*, prewake*, zygote*, file_bench) under `tools/with_display_lock.sh`,
  the same way `make check` already runs display tests.

## Red and green runs (all BAT0=Not charging, i.e. [AC])

| test | red (before the fix) | green (after) |
|---|---|---|
| `tests/raster_test.c` `fence_completion_guard_test`: guard open over present -> retirement, 16 full frames | `raster fence completion: frames=16 worker_fence_jobs=16 counted_allocations=96 guard=1` then `raster_test:553: FAIL allocs == 0`, rc=1 (19:54:44, load1 2.88) | `counted_allocations=0`, `raster_test: PASS`, rc=0 (19:55:28, load1 8.73) |
| `tests/x11_order_test.c` `test_rebuild_guard`: keymap worker job inside the guard | `x11 keymap rebuild: worker_job=1 ready=1 counted_allocations=1258 guard=1`, FAIL, rc=1 (20:20:05, load1 3.32) | `counted_allocations=0`, `rescans ok`, rc=0 (20:20:11, load1 3.54) |
| `editor_bench --self-check-typing` foreign-input probe | STRUCTURAL line had no `foreign_input`, rc=255 | `... foreign_input=2 ...`, rc=0 |
| `tests/base_test.c` `test_guard_exempt` | written with the API, so no separate red run; it would not link without the API | `worker exempt=0 worker counted=1 after_close=1`, PASS |
| `make -n bench` | editor_bench invoked as `DISPLAY=$d $b $args` | `DISPLAY=$d sh tools/with_display_lock.sh $b $args` for the 8 display benches |

Derivation of the 96: 16 frames x 6 libxcb packet allocations per frame =
96 (M). The 6 per frame is inferred from the measured total; I did not
attribute it packet by packet. The test's 16 frames is (E): enough that every
frame retires through its own fence job (`completion_jobs == 16` asserted).
One frame would already show at least one reply allocation.

## Bench rows, back to back, both under the display lock on :99

Command: `EDIT_BACKEND=raster editor_bench --track --no-idle --no-p4`
(10000 keys per row). Power: BAT0=Not charging, so (M)[AC]. The box was
shared: other worktrees' suites and codex workers were running.

Run A, `before` binary (main HEAD 06dbc22 build) at 20:39:16, loadavg
1.89/2.34/3.41:

| workload | allocations | structural | T4 p50 / p99 ns | load1 |
|---|---|---|---|---|
| serial | 702 | FAIL | 17149936 / 52349709 | 2.39 |
| queued_active_frame | 424 | FAIL | 16367169 / 33728927 | 5.40 |
| active_index | 99 | FAIL | 16412699 / 33807250 | 4.99 |
| queued_index_find_save | 934 | FAIL | 16355079 / 33588105 | 5.08 |

Run B, `after` at 20:50:24 (loadavg 3.76/4.97/4.64), then `before` at
20:55:45 (loadavg 4.16/5.72/5.26):

| workload | after: allocations / structural / T4 p50 / p99 ns (load1) | before: allocations / structural / T4 p50 / p99 ns (load1) |
|---|---|---|
| serial | 0 / OK / 16863871 / 60019703 (6.51) | 965 / FAIL / 16642640 / 20469367 (4.02) |
| queued_active_frame | 0 / OK / 16507757 / 18589698 (4.32) | 1139 / FAIL / 16535833 / 18736888 (2.33) |
| active_index | 0 / OK / 16478588 / 34562785 (3.74) | 1572 / FAIL / 16324745 / 32191096 (2.05) |
| queued_index_find_save | 0 / OK / 16345158 / 33433886 (4.10) | 218 / FAIL / 16244724 / 29659017 (2.60) |

Exit codes: `rc_after=0` and `rc_before=1`. Every row had `completed=10000`
and `foreign_input=0`. Timing did not change; the fix touches only
accounting.

The `after` binary in run A completed serial and queued_active_frame with
allocations=0 and OK. It then hit the harness's 5 s settle watchdog in
active_index (see out-of-scope 1), and run B's rerun of the same binary
passed all four rows.

Release `tests/editor_test` (lock held, after run B) passed, rc=0. Its output
included `requested=gl actual=cpu-raster 10000 keys mallocs=0 guard=active`
and `review 21: ... mutation-to-submit=0`.

`make check` (ASan/UBSan, clang): every test up to `file_test` passed.
`file_test:308` failed once (an inotify change-detection timing check) and
then passed 3/3 when rerun alone. I ran the remaining 63 san test binaries
in the same order and with the same display lock. All passed except two,
both pre-existing and unrelated to this diff (out-of-scope 3 and 4).
`x11_order_test` passes with `EDIT_DISPLAY=:99`. `tools/test_replay_cli.sh`
passed. The `test_display_lock.sh contract` step timed out on the lock
because other worktrees held :99. It was not rerun.

## Out-of-scope findings for the coordinator

1. **View/layout stuck for more than 5 s in the active_index row.** Output:
   `SETTLE deadline: view_busy=1 layout_busy=1 view_query_byte=966367560
   layout_row=0 phase=0 mutations=3353 slice_max_ns=3605407`. This was the
   `after` binary in run A at load1 5.34, on the 1 GiB `log_1g.txt` with the
   index progressing. It did not reproduce in the next run. It could be a
   real typing stall under bulk indexing (a hang or lag class) or just load.
   My change does not touch view or layout.
2. **`tests/x11_order_test.c:452` `set_layout()` hard-codes `-display :99`.**
   It fails §21 whenever the test runs on its private Xvfb (no
   `EDIT_DISPLAY=:99`). It also changes the keymap of everyone on the shared
   :99, which is what triggered the keymap-worker counts here. It should use
   the test's own `DISPLAY`.
3. **`refwin_test` fails reproducibly with `editor.trace: invalid trace
   record`.** `tools/refwin_pairs.py:58` rejects `event > 6`, but the editor
   now records `TRACE_COMPOSE_BEGIN/END` (7, 8) and startup stamps
   (10, 14, 16-18) in ring 0 (`src/trace/trace.h`). It predates this diff;
   trace is untouched here.
4. **`file_test:308` is flaky under load:** one failure in `make check`,
   then 3/3 passes.
5. **Raster T4 p50 is about 16.3-17.2 ms on Xvfb in every row.** That is
   close to one synthetic Present interval: refwin measured
   `period=16465187 ns` on :99 (M), i.e. 1/60.7 Hz. T4 waits for the
   previous frame's retirement at that cadence. This is a tracked number,
   not a lag claim for real hardware.
6. **Other suites may still inject input into a bench window if they skip
   the display lock** (ad-hoc runs, codex workers using bare `DISPLAY=:99`).
   `foreign_input=` now names it when it happens.
