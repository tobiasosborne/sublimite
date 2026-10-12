# edit-pd6: navigation during an unresolved byte jump

Source: docs/reviews/editor-2.md finding 13. A far byte jump (find result,
minimap click, Ctrl+End on an inexact count, goto) leaves `b->scroll.request ==
SCROLL_BYTE` until the resident resolver lands it. `scroll_wheel` and
`scroll_key_motion` reject that state (`SCROLL_ERR_ARG`), and the editor loop
made the rejection sticky (`fail`), stopping the editor.

## Choice: queue, not cancel and not drop

The review proposed cancelling the jump into the displayed READY state. That
loses the jump: Ctrl+End or a find result followed by an accidental wheel tick
would leave the view at the old top. Dropping the input (counter only) would
also swallow a wheel tick or PageDown that lands in the same step as a typed
key, because every edit re-anchors through `editor_scroll_seek` (a SCROLL_BYTE
request) until the next slice resolves it.

So the rule is: the result equals the unraced order, as if the input had arrived
after the jump landed.

- Wheel (unwrapped): the delta is queued per buffer (`scroll_queued_wheel`,
  saturated to the int32 range of `plat_event.dy`). The jump and the find host's
  horizontal-follow flags are kept.
- PageUp/PageDown: the cursor motion runs now through `view_command`, as it
  does unraced. The viewport page is queued (`scroll_queued_pages`, signed, one
  per key event). `editor_scroll_follow` does not adopt a top while motion is
  queued behind a jump, because that top comes from the pre-jump scroll state.
  It only marks following, so the follow runs after the queued page, which is
  the unraced order.
- Ctrl+Home/End: these are absolute, so they supersede the jump.
  `editor_scroll_cancel_jump` returns to the displayed READY top, as a mutation
  does in `editor_scroll_edit`, and the motion applies at once.
- `editor_scroll_slice` applies the queue right after the jump resolves and
  before the cursor follow. The queued motion then resolves on the next slice.
  If anything else replaced the byte request first, the queue applies at the
  slice's start.
- A new absolute target (`editor_scroll_seek`, `editor_scroll_place`,
  `editor_scroll_init`) clears the queue. A mutation therefore cancels queued
  motion, the same way it cancels unpublished navigation.

## Approximation

Queued pages are applied before the queued wheel delta, whatever order they
arrived in. Relative motions commute except at the top and EOF clamps, so this
only differs from arrival order when a clamp is hit. Mixing wheel and page
input inside one unresolved jump is the only case affected.

## No new numbers

No time or count budgets were added. The page loop in `apply_queued` runs once
per queued key event, so its cost is O(1) per input already received. The
test's mid-document target is derived next to it from `PROBE_BYTES` (P).

## Landing note (lander 5, 2026-10-13)

- Rebased onto edit-91c: `jump_then` sets `hold.ui` because 91c's `held_span` holds only off the UI thread. Without it, the UI-thread lane probe spun forever.
- Rebased onto edit-o20: o20's Ctrl+Up/Down `scroll_lines` chord calls `scroll_pixels`. That call returns SCROLL_ERR_ARG during an unresolved SCROLL_BYTE jump, the same sticky error this bead fixes. The chord is now queued like a wheel notch (`scroll_queued_pixels`, Q8 pixels, saturating). It is applied after the queued pages and wheel. The jump's find-follow flags are kept, as in the wheel branch.
- Test: `--pending-jump-keys` gained Ctrl+Down and Ctrl+Up. Red run: `editor_scroll_test:203` reported a sticky error at Ctrl+Down. Green run: all six keys match their unraced reference.
