# edit-edgt: asynchronous test completion under parallel check

The affected assertions describe completed work. `editor_step(e, 0)` is a
nonblocking, sliced turn: preemption can consume its slice before layout and
decoration preparation reach submission. The editor's `pending` flag covers
input/frame work; asynchronous glyph images have their own pending predicate.
No production code or typing-path allocation behavior changes in this bead.

## Causes and test changes

- `raster_blink_damage`: focus-out stops blinking during event dispatch, but
  its caret/layout frame can remain pending when the turn yields. Use the
  existing `settle` deadline before checking the same sleeping-worker metrics
  and `!blinking && !pending`. The supplied failing log is preserved at
  `build/edit-edgt-evidence/provided-blink.log`.
- `review_chrome_damage`: the first mutation introduces the tab's U+2022
  unsaved marker. `font_async_glyph` installs a placeholder; the mailbox later
  adopts the image and `editor_font_event` damages its visible row. An ordinary
  `settle` can return between those events. Diagnostic failures had
  `mutations=2`, `full=0`, and row damage: the image arrived during the following
  text mutation. Drain `font_async_pending(e->fonts)` together with frame work
  before starting each unchanged-chrome observation. Both key observations
  and the final identical-composition assertion remain intact.
- `review_replay_slices`: the timeout occurred while preparing the undo group,
  with queued keys remaining and an active repair/layout continuation. The
  diagnostic runs observed queue counts of 91 and 310 (M)[AC], sampled at the
  existing settle deadline. A batch of 1,024 edits had been treated as one
  completion operation. Settle each injected key within the existing bound;
  their fixed ingress timestamp still forms the same 4 x 1,024 = 4,096-record
  undo burst (P). The assertions still require a yielded replay snapshot,
  unchanged committed text/frame/journal during the first replay turn, busy
  command rejection, complete undo and complete redo.
- `editor_scroll_test` cancellation: `editor_full_layout` schedules work; a
  turn can finish its layout slice before decoration preparation/submission.
  Step until the submitted-frame counter advances, using the file's existing
  retry bound. Keep the worker parked throughout and check the same closing
  resident, cell bytes, snapshot retirement and restored viewport. Waiting for
  global idle here would wait on the deliberately parked worker.

The editor deadline remains its existing 5,000,000,000 ns (G), now named
`SETTLE_NS`; the scroll probe retains `PROBE_RETRIES=10,000` (G), inherited from
the existing scroll integration fixture. No per-operation deadline or retry
limit grows.
The additional editor selectors call these same case bodies for diagnosis.

## Runner safety net

The parallel phase records failed tasks, including individual editor parts.
After every parallel task finishes, each failed task runs once alone, retaining
its private-display lease protocol and process-group cleanup. The original
output remains `<task>.log`; the isolated attempt is `<task>.rerun.log`.
Scheduling durations retain the original parallel measurements.

A successful isolated attempt emits the requested `check: FLAKY ...` line and
appends the local ISO timestamp, task name and first assertion/error line to
`build/check-flaky.log`. A repeated failure still fails the check. Aggregation
remains per binary, so recovery of one part cannot hide a failing sibling.

Red: `python3 tests/check_parallel_test.py` against the original runner exited
nonzero because the fail-once helper had run only once. Evidence:
`build/edit-edgt-evidence/runner-red.log`. Green: `runner-green.log` covers
recovery, repeated failure, waiting for a parallel peer to exit, exact attempt
counts, preserved logs, audit contents/date, split-part failure aggregation,
display scheduling, timing-cache behavior and interruption cleanup. One retry
is the explicit bead requirement (G), not a newly chosen timing policy.

## Load reproduction

Run `python3 tools/stress_check.py N` after building the sanitizer editor and
scroll binaries. Each round runs the editor's affected parts and the scroll
suite concurrently, each on its own locked private Xvfb. The harness starts
`nproc` busy-loop processes for its entire duration and reaps them on exit.
It reports raw task exits without using the runner's retry safety net. Every
round records power and monotonic elapsed time; its directory keeps all logs.
`--focused` selects the affected cases directly; optional case names narrow it.

All measurements here were taken on 2026-10-28 with `nproc=12` (M); the burner
count is the user's requested one process per available CPU (G). The full
baseline used N=3, matching the requested consecutive-check repetition count
(G). The focused sample used N=`nproc`=12 (M), giving one repetition per
available CPU; each sample's green uses the same N and workload as its red.
Pass/fail counts below are sums of recorded task exits (M)[AC].

| Workload | Red pass/fail | Green pass/fail |
| --- | --- | --- |
| Focused blink, N=12 | 12 / 0 | 12 / 0 |
| Focused chrome, N=12 | 12 / 0 | 12 / 0 |
| Focused replay, N=12 | 9 / 3 | 12 / 0 |
| Scroll suite, N=12 | 12 / 0 | 12 / 0 |

The full baseline had part 4 at 1 pass / 2 failures, part 5 at 3 / 0 and scroll
at 3 / 0 (M)[AC]. Its failures were the replay setup timeout and chrome damage.
The identical full green sample finished with 3 / 0 for each task (M)[AC],
without invoking retries, in 272.904 s (M)[AC]; all its starting/per-round
BAT0/status stamps were `Charging`. Evidence: `stress-green-load.log` and its
matching directory. The wall time again includes display/burner lifecycle.
The focus-out and one-turn scroll failures supplied in the bead did not recur
in these red samples. Their causes follow the slice/submission control flow
and the saved supplied assertions; no claim of a fresh reproduction is made.

Evidence directories: `build/edit-edgt-evidence/stress-red-load/`,
`focused-red/`, and `focused-green/`, with matching driver `.log` files.
Their total wall times were respectively 233.940 s, 69.194 s and 50.200 s
(M)[AC], derived from monotonic end minus start including display/burner
lifecycle. Starting BAT0/status values were `Charging`, `Not charging` and
`Charging`; per-round stamps are in the driver logs. Both battery states were
on AC. A separate chrome-only diagnostic sample recorded 82 passes / 18
failures over 100 invocations (M)[AC], `Charging` at start, in
`chrome-diagnostic.log`; its larger sample isolates the less frequent cold
glyph arrival from the rest of part 4.
The identical chrome-only green sample was 100 / 0 (M)[AC] in 27.229 s,
`Charging` at start, in `chrome-green.log` and its matching directory. Both
wall times are monotonic end minus start. These raw load results precede the
full `make check` acceptance runs and are independent of flake recovery.

## Consecutive acceptance checks

Three consecutive `make check` commands each exited 0, running the existing
114 binaries / 118 tasks with 12 jobs and six private displays (P: one editor
binary is replaced by its five existing parts; 114 - 1 + 5 = 118). Job count
comes from `nproc`; display count retains edit-nafu's packing model. Both
Makefile tail checks passed each time. There were no `check: FLAKY` lines.

| Run, local start (2026-10-28) | Exit | Entire command wall time (M)[AC] | BAT0/status before timing | Load1 before timing (M) | FLAKY lines |
| --- | --- | --- | --- | --- | --- |
| 1, 10:25:09 +01:00 | 0 | 76.437 s | Not charging | 0.33 | none |
| 2, 10:26:26 +01:00 | 0 | 71.786 s | Not charging | 5.24 | none |
| 3, 10:27:38 +01:00 | 0 | 81.009 s | Not charging | 7.44 | none |

Wall times are Python monotonic end minus start around `subprocess.run` of
`make check`, including both tail checks and excluding subsequent log copying.
Load1 is `os.getloadavg()[0]`, rounded to hundredths. Power is read immediately
before each timer. `build/edit-edgt-evidence/check-three.json` retains exact
times, timestamps, power, load and flake-line lists. `check-1.log` through
`check-3.log` retain command output; matching `check-N-logs/` and
`check-N-times.txt` retain each run's suite logs and scheduling cache before
the next run replaces them. No stress burners ran during these checks.

All changes are left for the coordinator to commit; no `bd` command or commit
was run for this bead.
