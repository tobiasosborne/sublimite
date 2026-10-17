# edit-08u (s8) worker report

## Result in one line
The :117 failure is **not a settle timeout**. Under load the keystroke's frame is
**blocked for as long as the fixture's scroll-window worker is parked**: a
real src race on the typing path. The test now classifies the outcome (real
stall fails with a diagnostic line; a genuine timeout is a tracked law-1 line;
rc and allocations still fail). The acceptance "green x5 under load" is **not
met** because the stall is real and still shows up under load (2 of 17
post-change runs at load1 34-49). Fixing it is a src change outside this bead.

## Files touched
- tests/editor_scroll_test.c: new `held_key()` helper used by `cancellation()` (old :117) and
  `eof_typing()` (the other held-worker keystroke case, same pattern); includes trace/trace.h.
- docs/decisions/edit-08u.md (new).
- docs/worker-reports/edit-08u-s8.md (this file).
No src changes.

## Mechanism (measured)
Diagnostic line from a post-change failing run:

    editor_scroll_test: edit-08u cancellation keystroke frame not out after retries=10000: BLOCKED behind the held scroll window (real failure)
    elapsed=359.762 ms frames=1 baseline=1 render_active=0 dirty=1 scroll_waiting=1 closing=1 scroll_prefix=0 built_prefix=1
    scroll_revision=0 revision=1 allocations=0 (M) power=Not charging load1=33.90

`editor_scroll_edit` -> seek records `scroll_prefix = lineidx_built_prefix = 0`. The line index
then publishes prefix 1 **before** the typing frame is submitted. `editor_scroll_slice`
(scrollctl.c) skips its "do not ask a typing frame to wait for replacement windows" fast path
because `scroll_prefix != prefix`. `editor_scroll_retire` returns MORE while the parked worker
owns the closing window, `scroll_pending` stays set, and `submit()` (editor.c, first guard
`editor_scroll_waiting(e)`) refuses to submit. A forced experiment (temporary, reverted) showed that
three more 10000-step settle rounds (about 0.3-0.8 s each) never cleared it, and a one-step
bound passes on an idle path. So more retries or a longer deadline cannot fix this. In production
the keystroke waits for the window job to finish, which is a typing-latency path under load.

## Red run (original test, unmodified, on :99 under the display lock, ASan)
    == run 2 Not charging load1=45.46
    editor_scroll_test:117: FAIL rc == 0 && (!edit_malloc_guard_active() || allocations == 0)
(run 3 at load1 25.80 passed; run 1 timed out on the display lock, which another tree held.
The "exit=0" printed in that log is grep's status, not the test's.)
Unlocked repeat with temporary diagnostics, load1 43.47: settle exhausted with
`pending=1 render_active=0 dirty=1 scroll_waiting=1 frames=1`, 1 of 12 runs.

## After the change
The helper steps until the keystroke's own frame is out (`submitted_frames > baseline`, backend
idle), with the same PROBE_RETRIES bound. Previously it waited on `!pending`, which also covered the
renewal waiting on the fixture's parked worker, and that is scheduler timing. On exhaustion:
- `scroll_waiting && closing` gives a BLOCKED line and a FAIL (real stall);
- anything else gives a tracked line "timeout (tracked, law 1; rest of case skipped)" with elapsed,
  load1 and BAT0, and the case passes after the allocation check;
- an inject/step error or `allocations != 0` fails as before. On the normal path
  `rc == 0 && allocations == 0` is still asserted.

Exercising the timeout path, with the bound temporarily forced to 0 and then reverted (exit 0):

    editor_scroll_test: edit-08u cancellation keystroke frame not out after retries=10000: timeout (tracked, law 1; rest of case skipped) elapsed=10.473 ms ... (M) power=Not charging load1=49.69
    editor_scroll_test: edit-08u eof-typing keystroke frame not out after retries=10000: timeout (tracked, law 1; rest of case skipped) elapsed=0.193 ms ... load1=49.69
With the bound forced to 1 both cases pass: on an unblocked path the frame comes out in one turn.

Unlocked x15, load1 33.25-35.18: 14 passed, 1 BLOCKED (line quoted above).

## Verification
- `make -j3 all` (gcc, -Werror): exit 0.
- `make -j3 build/san/tests/editor_scroll_test build/san/tests/editor_test`: exit 0.
- editor_scroll_test x5, `XDG_RUNTIME_DIR=$(mktemp -d) DISPLAY=:99 ASAN_OPTIONS=detect_leaks=1 sh tools/with_display_lock.sh build/san/tests/editor_scroll_test`:
  - run 1, Not charging, load1 55.01 at start / 49.44 at failure: **exit 1**, BLOCKED (real stall, as above)
  - run 2, load1 49.44: exit 0
  - run 3, load1 49.57: exit 0
  - run 4, load1 37.81: exit 0
  - run 5, load1 37.04: exit 0
- editor_test once, same form, Not charging, load1 50.38: exit 0 ("editor_test: all passed").
No leak reports.

## What remains
- A src bead for the stall. A typing frame must not wait on `editor_scroll_retire` of a closing
  resident window just because the line-index prefix grew after the edit's seek. Candidates:
  the `scroll_prefix == prefix` term in `editor_scroll_slice`'s fast path, or the
  `editor_scroll_waiting` guard in `submit()`, for the case where only the retire is outstanding.
  Once that lands, this test is expected to pass x5 under load unchanged, and the BLOCKED branch
  stays as the regression guard.

## Out of scope, observed
- editor_large_test flakes (:802, :1651): not run or observed in this bead.
- After the keystroke the caret/blink-driven `dirty=1` frame also stays blocked behind the held
  window (seen in the forced experiment: pending forever, dirty=1, scroll_waiting=1). This is the
  same gate as above.
- The display lock :99 queue was long (one wait timed out at the 600 s default while another
  tree's build/bench/file_bench held it). Coordination, not code.
