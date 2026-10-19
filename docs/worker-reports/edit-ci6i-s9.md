# edit-ci6i (s9): refused minimap and wrapped-seek snapshots never reach fail()

Worker: Claude Opus. Worktree: .wt/edit-ci6i-s9. The design is in
docs/decisions/edit-ci6i.md.

## Cause

`editor_map_service` (minimap.c) returned EDITOR_ERR_MEMORY when
`piece_snapshot_take` refused, and `editor_step` passed that to fail().
`editor_scroll_resolve` (the wrapped far-find or minimap byte seek) did
the same with `install()`'s EDITOR_ERR_MEMORY: `editor_wheel_continue`
returned it and `editor_step` called fail(). Both now follow the edit-bt1
pattern: defer to the JOURNAL_DEFAULT_SYNC_NS tick (E, edit-dr9's
interval) and show a notice from the second consecutive refusal. A
successful retry clears the state.

## Files touched

- src/editor/minimap.c: the `map_due()` predicate,
  `editor_map_retry_ns()`, `defer()`, and the notice
  `minimap deferred: buffer memory exhausted`.
- src/editor/scrollctl.c: `refuse()` split out of bt1's `defer()`;
  `editor_scroll_resolve` defers (returns SCROLL_MORE) on a refused
  install and does not retry before the tick; `editor_scroll_deferred()`
  added.
- src/editor/input.c: `editor_wheel_ready` is false for a wrapped byte
  seek while its retry tick is ahead, so the loop does not spin.
- src/editor/editor.c: `wait_timeout` wakes for the map tick, and for the
  scroll tick while `wheel_pending` is set as well as `scroll_pending`.
- src/editor/private.h: `map_retry_ns` and `map_refusals` added to
  editor_buffer; the two new prototypes.
- tests/editor_test.c: `nci6i_map_refused` (`--ci6i-map`) and
  `nci6i_resolve_refused` (`--ci6i-seek`), both added to the default run.
- docs/decisions/edit-ci6i.md and this report.

## Red

Both final tests ran against a build with the two fix lines reverted to
the old propagation:

    --ci6i-map exit 1
    edit-ci6i: editor_step rc -101 error_cause -101 (attempt 0, hit 1)
    editor_test:5299: FAIL rc == EDITOR_OK || rc == EDITOR_MORE
    --ci6i-seek red exit 1
    edit-ci6i: wrapped seek editor_step rc -101 error_cause -101 (attempt 1, hit 1)
    editor_test:5379: FAIL rc == EDITOR_OK || rc == EDITOR_MORE

The first drafts failed the same way on the unchanged tree: the map case
returned rc -101 at attempt 1, and so did the seek case. The map draft was
unwrapped. That proved unusable for the persistence phase, because the
slice's same-step take cached the snapshot (see the decision doc).

## Green

Power `Full`, 03:49:10, load average 28.27 / 29.31 / 26.83.

    --ci6i-map green exit 0
    edit-ci6i: refused minimap snapshot defers the map, keeps the editor, notices persistence passed
    --ci6i-seek green exit 0
    edit-ci6i: wrapped seek landed top 0 (from 1014, target 0)
    edit-ci6i: refused wrapped seek window defers the seek, keeps the view, notices persistence passed

## Verification

Power `Full`. Runs from 03:50:23 to 04:04:22; load average 10.37 at the
end. Harness: `XDG_RUNTIME_DIR=$(mktemp -d) DISPLAY=:99 EDIT_DISPLAY=:99
EDIT_DISPLAY_LOCK_TIMEOUT=7200 ASAN_OPTIONS=detect_leaks=1 sh
tools/with_display_lock.sh <bin>`. The lock timeout was raised from 1800
because one earlier attempt hit the 1800 s timeout while :99 was held by
other workers.

| command | exit |
|---|---|
| `make -j3 all` (gcc, -Werror), after the last change | 0 |
| build/san/tests/editor_scroll_test (scrollctl.c moved; includes bt1's case) | 0 |
| build/san/tests/editor_test, leaks on (both ci6i cases included) | 0 (`editor_test: all passed`) |
| release build/tests/editor_test (G1 malloc guard) | 0 (`all passed`; `null 10000 keys mallocs=0 guard=active`, `null hl-on ... mallocs=0 guard=active`, `raster` and `gl->cpu-raster` the same) |

There are no GB fixtures. Journal, session and render code was not
touched, so their suites were not run.

## What remains

- `editor_scroll_init` was deliberately left unchanged (decision doc (3)).
  For a non-pending buffer, its take reuses the snapshot that the index
  take just cached in `editor_buffer_prepare`. Its memory errors are
  therefore prepare errors, the same as `lineidx_create` and the index's
  own take, and prepare returns them to the opener.
- Not yet checked: whether every opener reports a prepare memory error as
  a notice instead of passing it to fail(). The openers are open, new tab,
  the results tab, restore and reload; reload already swallows them. That
  is the remaining part of the bead title's "buffer prepare, activation"
  clause. The coordinator should decide whether it needs a bead of its
  own.

## Out of scope (noticed, not fixed)

1. Other EDITOR_ERR_MEMORY returns from snapshot takes that can reach
   `editor_step`: large.c:468 (`seek.snap`) and :589, recovery.c:376,
   buffers.c:320/323 (`editor_adopt_open`). I did not check which of
   these reach fail().
2. bt1's existing wake condition in `wait_timeout` (`scroll_retry_ns &&
   scroll_pending`) has no `> now` guard. Once the tick has passed, if the
   slice cannot run (for example while view_busy), the wait is 0 ms on
   every step until it can. The new map wake uses a due-predicate for
   exactly this reason. bt1's condition is unchanged apart from the added
   `wheel_pending` term.
3. Display :99 is heavily contended by parallel workers: one run timed out
   on the 1800 s lock.
