# edit-xdro (s9): a deferred Reload/Keep choice no longer spins the UI loop

Design: docs/decisions/edit-xdro.md.

## Cause

`editor.c` `runnable()` counted any pending `e->source_choice` as runnable work.
The consumer, `editor_check_sources` (buffers.c), runs the choice only when
`!editor_save_active(e) && editor_checkpoint_due(e)`. While a choice waited on
a save in flight, or on the edit-ivv journal tick, three things happened:

- `wait_timeout` returned 0;
- every `editor_step` returned `EDITOR_MORE`;
- the loop polled with a zero timeout for the whole wait.

## Fix

`runnable()` now uses the consumer's gate:
`(e->source_choice && !editor_save_active(e) && editor_checkpoint_due(e))`.
The existing wakes cover both deferred cases:

- **Save in flight.** The save's completion is a mailbox message in the poll
  set. `editor_poll_sources` drains it and runs `editor_check_sources` in the
  same call.
- **Journal tick.** `wait_timeout` already sleeps until a future `busy_due_ns`.

No new wake source, number or allocation was added.

## Files touched

- src/editor/editor.c: `runnable()`, one condition plus a comment.
- tests/editor_session_test.c: `xdro_save`, `xdro_tick` and `xdro_all`; the
  `--edit-xdro` option; added to the full run.
- docs/decisions/edit-xdro.md
- docs/worker-reports/edit-xdro-s9.md

## Test

Both cases pend a choice for the live buffer, which is not stale. Each case
steps with the watchdog as its timeout until the choice has run, and counts the
`EDITOR_MORE` returns while the choice is still deferred. The (P) bound is zero,
because MORE means runnable work.

- **(a)** A save is held at `FILE_STEP_DIR_SYNCED` by the savectl step hook.
  The hold is released `JOURNAL_DEFAULT_SYNC_NS / 2` later, from a thread.
- **(b)** `busy_due_ns` is set to one `JOURNAL_DEFAULT_SYNC_NS` ahead, as a
  refused checkpoint leaves it.

Red, before the fix. Full, load 27.34 (03:49):

```
edit-xdro (a): choice waiting on a save in flight: steps=7103 more=7102 (M)[AC]
editor_session_test:2225: FAIL more == 0
edit-xdro (b): choice waiting on the journal tick: steps=12808 more=12806 (M)[AC]
editor_session_test:2237: FAIL more == 0
exit=1
```

Green, after the fix. Full, load 28.66 (03:50):

```
edit-xdro (a): choice waiting on a save in flight: steps=11 more=0 (M)[AC]
edit-xdro (b): choice waiting on the journal tick: steps=8 more=0 (M)[AC]
edit-xdro: a deferred Reload/Keep choice sleeps until the save completes or the journal tick fires
exit=0
```

In both runs the waits end on the save's completion or on the tick. The test
did not hit its watchdog.

## Verification

Full, load 10.84 (04:04). Display suites ran as
`XDG_RUNTIME_DIR=$(mktemp -d) DISPLAY=:99 EDIT_DISPLAY=:99 EDIT_DISPLAY_LOCK_TIMEOUT=1800 sh tools/with_display_lock.sh`,
with `ASAN_OPTIONS=detect_leaks=1` for the sanitizer binaries.

| Check | Exit |
|---|---|
| `make -j3 all` (gcc, -Werror) | 0 |
| `make -j3 build/san/tests/{dataloss_test,editor_test,editor_session_test}` | 0 |
| build/san/tests/editor_test | 0 |
| build/san/tests/editor_session_test (full; edit-xdro (a) steps=14 more=0, (b) steps=10 more=0) | 0 |
| build/san/tests/dataloss_test | 0 |
| build/tests/editor_test (release; malloc guards active, e.g. "clipboard malloc guard: 0 allocations (active)") | 0 |

## What remains

Nothing in scope.

The sibling e3z5 tree changes `editor_reload_source`'s outcome type. This change
does not touch buffers.c, so the two should merge without conflict. If e3z5
moves the choice gate in `editor_check_sources`, the `runnable()` condition must
follow it.

## Out of scope

None found.
