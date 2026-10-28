# edit-nafu: fast `make check`

The sanitizer suites spent most of their elapsed time waiting for journal sync,
idle timers and Xvfb Present completion. The old Makefile ran every binary in a
serial loop, with display suites sharing one locked Xvfb. Parallel scheduling
and separate displays let these waits overlap without changing production code.

`tools/check_parallel.py` takes the Makefile's existing suite and display lists.
It schedules unknown suites first, then known suites by decreasing elapsed time
from `build/check-times.txt`. A display lease is assigned before a process gets
a job slot; waiting display work cannot starve eligible non-display work.
`CHECK_JOBS` defaults to `nproc`; `CHECK_DISPLAYS` defaults to six. The bead's
packing model supplied 488 s of display work / 6 displays = 81 s (E), before
splitting the editor and reducing the sanitizer-only paced loop. Summing the
33 display-suite rows in the supplied measurements gives 497.37 s (M)[AC],
or 497.37 / 6 = 82.90 s with ideal packing (E).

Each private server uses exactly
`Xvfb :N -screen 0 2880x1800x24 -dpi 100 -nolisten tcp -noreset`.
Candidate displays start above the existing shared `:99`; socket and X lock
checks plus the common flock exclude occupied displays and concurrent runners.
The runner retains the reservation, passes its descriptor to each leased suite,
and invokes `sh tools/with_display_lock.sh`. The existing inherited-lock contract
therefore remains in use. Non-display suites retain the Makefile's safe DISPLAY
and `EDIT_REFUSE_REAL_DISPLAY=1` environment.

All suite output is stored in `build/check-logs/<name>.log`, with individual editor
part logs and a concatenated `editor_test.log`. Failures report the exit status
and log tail; other suites continue and the driver exits nonzero. Shutdown reaps
suite process groups and private Xvfbs before closing the display reservations,
including on failure, startup error, SIGINT, SIGTERM and SIGHUP. Durations are
replaced atomically after a completed run.

GNU make builds independent objects with `nproc` jobs by default, while explicit
`make -j` settings remain effective. `check-serial` is byte-for-byte the old
`check` recipe, including both tail steps. The new `check` keeps those same two
tail steps unchanged: replay CLI and the display-lock contract. An explicit
command-line `TEST_SAN` override takes the shared-display entry lock before
starting the runner. This preserves the contract's nested one-suite make probe,
which must time out on the holder's shared display before any suite starts.
Install/clean recipes and `.SECONDARY` are unchanged.

## Editor coverage

The five selectors are consecutive slices, with no-argument `main` calling those
same slices in order. This partitions the existing waits: the journal section
was about 56 s (M)[AC]; backend idle checks about 2 x 10 s (M)[AC]; the final
allocation passes about 3 x 5.5 s plus an idle check about 10 s (M)[AC]. Isolating
these sections aims to keep each slice near 60 s (E), rather than leaving the
182.21 s (M)[AC] editor binary as the critical path.

| Selector | Consecutive original calls |
| --- | --- |
| `--part-1` | `parked_open_motion` through `n3f2k_save_with_results` |
| `--part-2` | `n6090_typing_top` |
| `--part-3` | `vp4m_return_bottom` through `ck4_recovery(4)` |
| `--part-4` | `review_suite` through the conditional `selected_backend_test` calls |
| `--part-5` | `scrolled_undo` through conditional `native_input` |

Mechanical comparison against the saved original source confirms that joining
the five function bodies, excluding their added `return 0`, equals the old main
test body **byte for byte**. It preserves all 145 `T(...)` calls (source count,
including conditional branches). Evidence:
`build/edit-nafu-evidence/editor-union.txt` and the saved original source beside
it. The same 114 test binaries run; splitting one binary into five processes
means 114 - 1 + 5 = 118 scheduled tasks (P), not additional test binaries.

## Paced raster loop and red-green evidence

Only the typing loop in render conformance is parameterized. The null backend
keeps its original 10,000 frames (G, the existing law-2 workload). Raster uses
that same workload whenever `edit_malloc_guard_active()` is true. With an inert
guard under ASan, it uses 1,000 frames: 1,000 x (1 / 60 Hz) = 16.7 s, about 17 s
waiting for Present (P), compared with 10,000 / 60 = 166.7 s (P). The old raster
suite measured 168.52 s (M)[AC], consistent with that pacing.

`src/raster/raster.c`'s `cpu_state` contains **one retained pixmap and one SHM
buffer**, rather than a multi-pixmap ring. Each is reused after the fence,
PresentComplete and Idle retirement. The shortened loop therefore cycles the
one-slot resource ring 1,000 / 1 = 1,000 times (P). Every iteration still executes
begin, fill, damage, submit, present and event completion assertions. The final
T5/T6 counts and identities are the parameter plus the four setup frames (P).
The scoped allocation-window count uses the same parameter. Its zero-result
assertion is now unconditional so a truncated loop also fails with an inert
allocator; no existing assertion was removed.

Red: built the parameterized loop with an intentional `- 1u` in its bound.
The sanitizer raster test exited 1 at `CHECK(allocations == 0)`: it submitted
1,000 - 1 = 999 windows (P), and `scoped_guard_end` added the nonzero count
mismatch sentinel. The early assertion return also produced secondary LSan
leaks because cleanup was bypassed. Evidence:
`build/edit-nafu-evidence/raster-red.log` and `raster-red-suite.log`.

Green: removed only that deliberate truncation. The sanitizer suite exited 0,
reporting `windows=1000 allocations=0 guard=0`; its driver wall time was
18.576 s (M)[AC], BAT0/status `Charging`, derived from monotonic end minus start.
Evidence: `raster-green.log` and `raster-green-suite.log` in the same directory.

The release raster suite also exited 0, reporting
`windows=10000 allocations=0 guard=1` and `10000 frames, 0 allocations`. Its
driver wall time was 168.309 s (M)[AC], BAT0/status `Not charging`, derived from
monotonic end minus start. Evidence: `raster-release-green.log` and
`raster-release-green-suite.log`. This exercises the full original live-guard
workload rather than inferring its preservation from the source.

## Measurements and acceptance

The supplied serial measurements are dated 2026-10-28 (M)[AC], on the available
12 CPUs. Their 114 per-suite durations sum to 694.57 s / 60 = 11.58 min (M)[AC]; this
is summed suite time, excluding Makefile tail steps and build time. Input:
`/tmp/claude-1000/-home-tobias-Projects-sublimite/85a23efe-08d5-4b61-b7ac-6f526f14f57f/scratchpad/suite_times.txt`.
The supplied prototype measurements were 185 s and 190 s (M)[AC] for 12 jobs
and four private displays; the unsplit editor remained its limiting suite.
The supplied incremental build measurements were 19.5 s serial and 5.1 s with
12 jobs (M)[AC], or 19.5 / 5.1 = 3.82x faster (M)[AC].

Two consecutive full `make check` commands exited 0 with default settings:
12 jobs from `nproc`, six private displays, 114 test binaries / 118 tasks (P,
the editor partition arithmetic above). Both unchanged tail checks passed.
Build outputs were already current, so these measurements include scheduling,
Xvfb startup/shutdown, all suites and both tail steps, without a rebuild.

| Successful run | BAT0/status before timing | Suite driver (M)[AC] | Entire `make check` (M)[AC] |
| --- | --- | --- | --- |
| `check-3.log`, 2026-10-28 09:44:41 +01:00 | `Not charging` | 142.2 s | 190.528 s |
| `check-4.log`, 2026-10-28 09:48:04 +01:00 | `Charging` | 79.7 s | 81.072 s |

Entire-command times are Python monotonic end minus start, with power read
immediately before the timer. The driver's own timer includes its private
display lifecycle. Compared with the supplied summed suite time, the driver
ratios are 694.57 / 142.2 = 4.88x and 694.57 / 79.7 = 8.71x (M)[AC]; the earlier
serial sum excludes tail steps, so it is not an entire-command baseline.
Evidence and copied duration caches: `build/edit-nafu-evidence/check-3.log`,
`check-4.log`, `check-3-times.txt` and `check-4-times.txt`.

The successful runs had substantial timing variation. In the final run, the
editor slices took 21.27, 69.34, 66.53, 74.15 and 20.30 s (M)[AC], respectively
(per-process monotonic elapsed times from `check-4-times.txt`, rounded to
hundredths). The preceding successful run's middle slices took 101.84, 106.77
and 103.65 s (M)[AC]. Thus the near-60 s target is the balance estimate derived
from the supplied section timings, not a measured upper bound under concurrent
load. The final run's longest suite was `harness_test`, 79.08 s (M)[AC], rounded
from its duration-cache entry; the unsplit editor is no longer the floor.

The first full attempt exited 2 in 115.878 s (M)[AC], BAT0/status `Charging`:
editor part 4 hit the unchanged `review_chrome_damage` assertion and the
`review_replay_slices` settle timeout (the latter is already recorded as
intermittent in `HANDOFF.md`). Part 4 then passed in isolation. The second
attempt exited 2 in 96.945 s (M)[AC], BAT0/status `Charging`: part 4 passed, but
`os_test` failed its installer slot-uniqueness assertion and `editor_scroll_test`
failed its one-step frame-submission assertion. These attempts are retained in
`check-1.log`, `check-2.log` and their saved failing-suite logs. They demonstrate
that failures propagate; the runner does not retry or suppress assertions.
Both elapsed values are monotonic end minus start for the entire make command.

Additional verification: the runner smoke harness confirms that a display
waiter cannot occupy the job needed by a headless suite, that an intentional
exit-7 failure reports its log tail while other tasks finish, that malformed
timing-cache entries are ignored, and that Ctrl-C exits 130 (POSIX 128 + SIGINT)
with the suite and Xvfb gone. Evidence: `runner-smoke.py` and `runner-smoke.log`
under `build/edit-nafu-evidence/`. Make flag probes report default `-j12` from
`nproc=12` and preserve explicit `-j2`. Clean and install were inspected with
dry runs; `.SECONDARY` remains present. Full serial execution is intentionally
omitted because its unchanged recipe was mechanically compared to the original.

## Coordinator follow-up (2026-10-28)

- `tests/os_test.c` case_fault: the installer loop joined and retired one thread at a time, so a thread still between the start barrier and its registration could legally reuse a slot retired a moment earlier, and the uniqueness check reported a duplicate. Registration itself is a compare-and-swap over free slots (src/os/os_posix.c os_fault_register) and cannot hand out one slot twice. The test now joins all four installers before checking or retiring any slot. Seen once under parallel load (check-2-os.log); not reproduced red on demand, since it needs a thread descheduled across the join.
- The runner smoke harness moved from build/edit-nafu-evidence to `tests/check_parallel_test.py` (temporary work directory); it passes from the repo root.
- Three further consecutive `make check` runs after these changes, 12 jobs, 6 displays, all exit 0 (M): 70.7 s [AC Charging, load 5.4], 204.1 s [AC Charging; the round-3 Codex worker was building and running suites concurrently], 80.7 s [AC Not charging, load 6.2]. The longest tasks in the last run: harness_test 78.7 s, editor_test parts 4/2/3 at 72.5/69.2/64.9 s, lineidx_test 61.8 s (M); these set the floor now.
- Two load-sensitive editor flakes from the worker's first attempts (editor_test:2656 review_chrome_damage, editor_scroll_test:197) did not recur in those three runs; they are tracked in their own bead rather than retried away.
