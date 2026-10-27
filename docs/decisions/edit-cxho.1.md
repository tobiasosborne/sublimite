# edit-cxho.1: minimap after closing the last tab

Verified before relying on the research: `editor_activate` falls back to
`e->empty` (src/editor/buffers.c, `find_buffer`/`editor_activate`), which
open.c prepares outside `e->buffers[]`; `editor_map_receive` and the snapshot
release in `editor_map_service` looped only over `e->buffers[]`. The red
editor test confirms it: after the close the fallback's map stays
`filled=0 stale=1` with its snapshot held for the whole settle bound.

## Root cause 1: route, not the synchronous flag

Two candidates were named: (a) make the map receiver and the service's release
loop cover `e->empty` (large.c's receiver pattern), or (b) set
`map_sync_fill` on `e->empty` so compose prepares it synchronously. Chosen: (a).

- (a) is one static accessor (`map_buffer`: slot `buffer_capacity` is
  `e->empty`) and two loop bounds in src/editor/minimap.c. The fallback then
  behaves exactly like a tab: worker preparation, publication, release.
- (b) only covers the first fill (`compose` clears `map_sync_fill` once the
  map is filled). Any later refresh of the fallback (for example a window
  resize, which changes the summary height) would go through
  `editor_map_service` again, submit a worker job whose result is dropped and
  whose snapshot is never released, so the defect would come back on the
  next resize. It would also put a synchronous
  prepare on the UI thread that the edit-73j landing kept only for
  file-backed sources.

## Root cause 2: shared pending-band predicate

The pending scrollbar's viewport test and its mirror in paint.c `map_grab`
were two copies of the same arithmetic. They are now one public function,
`minimap_pending_band` (src/minimap/minimap.h), so the painted band and the
grabbable band cannot drift; the extraction landed first unchanged (minimap
test green), then the tests went red, then the predicate changed.

The change: with fewer lines than summary rows, row r is line r (the grouping
`minimap_prepare` uses when `lines_per_row` is one), rows past the last line
hold none, and the overlap is half-open as for ready summaries. So the pending
band has the same geometry as the ready map it is waiting for; a one-line
buffer lights row 0 only. With at least as many lines as rows the existing
scaled mapping and its inclusive lower-bound rule are unchanged (large-file
behaviour stays as it was; that geometry is edit-cxho.8-.10's scope).

The pending edge bar (last column, `style.stale`) and the pending ink columns
are unchanged.
