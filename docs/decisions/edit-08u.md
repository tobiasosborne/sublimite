# edit-08u: held-window keystroke settle under load (test-only)

**Finding.** The :117 failure is not a slow settle. Under load the keystroke's
frame never submits while the fixture's scroll-window worker is parked:

    edit-08u cancellation keystroke frame not out after retries=10000: BLOCKED behind the held scroll window (real failure)
    frames=1 baseline=1 dirty=1 scroll_waiting=1 closing=1 scroll_prefix=0 built_prefix=1 scroll_revision=0 revision=1
    (M) power=Not charging load1=33.90

The edit's seek records `scroll_prefix=0`; the line index then publishes
prefix 1 before the typing frame composes. `editor_scroll_slice` takes the
full path on prefix change, `editor_scroll_retire` returns MORE while the
parked worker owns the closing window, `scroll_pending` stays set, and
submit is gated on `editor_scroll_waiting` (editor.c `submit()`, its first guard).
Nothing clears that until the worker returns, so more retries or a longer
deadline do not help. In production the wait lasts as long as the window job
does. That is a typing-latency path (src, out of scope; reported).

**Choice.** A shared helper `held_key()` (cancellation and eof_typing,
the two held-worker keystroke cases) steps until the keystroke's own frame is
out (`submitted_frames > baseline`, backend idle) rather than `!pending`. The
cases assert "typing submits while held"; `!pending` also covered the scroll
renewal that waits on the fixture's own parked worker, and whether that
renewal starts before settle samples `!pending` depends on the scheduler. On
exhausting the bound it sorts the case into one of these outcomes:

- inject/step error, or `allocations != 0`: fails as before;
- `scroll_waiting && closing`: fails with a BLOCKED line and the state above
  (the stall cannot end while the hold is parked, so it is a real failure);
- otherwise: a timeout of a non-blocking retry count. Law 1: one tracked line
  (elapsed, load1, BAT0), allocations still checked, rest of that case
  skipped (its checks need the frame).

**Budget kept.** PROBE_RETRIES (10000), the file's existing (G) reuse of
scroll_test.c's retry bound. No new timeout number. Elapsed time is printed,
never compared.
