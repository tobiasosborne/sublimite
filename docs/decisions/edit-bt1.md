# edit-bt1: recoverable scroll-slice errors never reach fail()

**(1) Refused snapshot.** `install()` returns EDITOR_ERR_MEMORY when
`piece_snapshot_take` cannot grow the piece pool. `editor_scroll_slice` now
defers instead of returning it (`defer()` in scrollctl.c):
- The slice keeps `scroll_pending` and stores `b->scroll_retry_ns` = now +
  JOURNAL_DEFAULT_SYNC_NS. That is edit-dr9's retry tick, the existing
  durability interval (E), reused here; no new duration.
- Until that tick, `editor_scroll_waiting()` is false. Frames, typing
  included, still compose against `scroll_display`, which is the window on
  screen. `runnable()` does not spin on the deferral.
- `wait_timeout()` wakes the step at the tick, and the slice retries then.
  An install that succeeds clears the retry state.
- The install branch is also taken when `scroll_snapshot` is NULL. A failed
  install has already retired the old window. Without this, a retry with
  an unchanged revision would resolve against a closed resident.
- "Persists past the existing retry bound" is read as: the refusal repeats
  at the retry tick. From the second consecutive refusal on, the status row
  shows `scrolling deferred: buffer memory exhausted` (the edit-btj notice,
  as fif.c `notice()` sets it). It clears at the next mutation, as every
  such notice does, and returns if the refusal continues. There is no
  terminal give-up: the next retry stays one tick away.
- Only EDITOR_ERR_MEMORY from the snapshot is deferred. Every other slice
  error still propagates.

Test seam: `editor_piece_storage.fail_request`, unchanged. A slice normally
takes no allocator request, for three reasons. Session publication (named
buffers) or the minimap's take (24 columns or more) already cached the
revision's snapshot. Also, the retired snapshot's pool slot is reused. So
the test uses a results tab at 16 columns and retains each installed
snapshot. Free slots then run out and an install must grow the pool. This
is also why the edit-5vz trace saw the failure in a results buffer.

**(2) Results open, then resize, then re-select.** This is the defect
edit-yey fixed. Its `editor_scroll_edit` restored `scroll_display`, which
still carried the find in files panel's row count from the activation.
After the panel closed in the same turn, the next plan failed with
SCROLL_ERR_ARG (-1). The `nbt1_results_geometry` repro of the old
open-then-close order returns -1 with yey's three lines removed and passes
with them. No second fix was made.
