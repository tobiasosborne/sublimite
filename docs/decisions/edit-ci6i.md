# edit-ci6i: refused minimap and wrapped-seek snapshots never reach fail()

Same class as edit-601(3)/5vz/bt1. The edit-bt1 pattern is applied to
the two remaining callers whose refused `piece_snapshot_take` reached
fail() from `editor_step`:

**(1) Minimap snapshot (`editor_map_service`, minimap.c).** A NULL take
used to return EDITOR_ERR_MEMORY. It now defers:
- The old map stays on screen. `b->map_retry_ns` is set to now +
  JOURNAL_DEFAULT_SYNC_NS. This is edit-dr9's retry tick, reused by bt1
  (E). No new duration is added. `b->map_refusals` counts consecutive
  refusals.
- Until that tick the service does not retry. `editor_map_retry_ns()`
  returns the tick only while a map is still due (the service's own
  predicate, now `map_due()`). `wait_timeout()` wakes for it. A tick
  that is no longer due cannot make the loop spin.
- From the second consecutive refusal on, the status row shows
  `minimap deferred: buffer memory exhausted`. It is the edit-btj notice
  slot that bt1 uses, and it clears at the next mutation.
- A successful take clears the retry state. There is no terminal
  give-up.

**(2) Wrapped byte seek (`editor_scroll_resolve`: far find result and
minimap navigation in a wrapped view).** A refused `install()` now calls
bt1's refusal bookkeeping (`refuse()`, split out of `defer()`), so the
seek shares bt1's tick, counter and notice: it is the same resident
window and the same refusal. The function returns SCROLL_MORE, which
`editor_wheel_continue` already handles by keeping `wheel_pending`.
`editor_wheel_ready()` is false while the tick is ahead, so the step loop
blocks instead of spinning. `wait_timeout()` wakes on the tick for
`wheel_pending` as well as `scroll_pending`. `defer()` is now
`scroll_pending = true` plus `refuse()`. Its behaviour is unchanged.

**(3) `editor_scroll_init` was not changed.** Its only caller is
`editor_buffer_prepare`. There a refused take is one of several
EDITOR_ERR_MEMORY returns of the prepare: `lineidx_create`, the reserve,
and the index's own `piece_snapshot_take` a few lines earlier. Prepare
returns them to the opener as the open's error. For a non-pending buffer,
init's take retains the snapshot the index take just cached, so it cannot
be the first to fail. Turning prepare's memory errors into notices at
every opener (open, new tab, results tab, restore, reload) is a separate
change. It is reported, not made here.

Tests (editor_test): `--ci6i-map` and `--ci6i-seek`. Both use bt1's
fault seam (`editor_piece_storage.fail_request`) on a results tab. A
results tab has no session publication. Each held snapshot keeps the take
from reusing a slot, so the take has to grow the pool. The map case wraps
its view so that the map is the revision's only snapshot taker. Unwrapped,
the scroll slice takes the snapshot right after the refused map take, in
the same step. The map's retry then reuses that cached snapshot, and a
refusal could never persist. The seek case uses 16 columns, so there is
no minimap. Both tests then check, in order:
- a single refusal: editor_step OK and no notice;
- a persisting refusal: the notice appears;
- recovery on a later tick: the map is prepared, or the view lands on the
  seek target.
