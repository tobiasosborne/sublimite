# edit-cxho.1 worker report (session 24, Opus)

Bead: the minimap shows full-height grey bands after closing the last tab.

## Research claims checked in the code

- After the close, `editor_activate` (src/editor/buffers.c) falls back to
  `e->empty`. open.c prepares that buffer and never installs it in
  `e->buffers[]`. Confirmed.
- `editor_map_receive` and the snapshot-release loop in `editor_map_service`
  (src/editor/minimap.c) iterated only over `e->buffers[]`. So the fallback's
  map result was dropped, `map_snapshot` was never released, `map_due` stayed
  false and compose kept drawing the pending scrollbar. Confirmed by the red
  editor test: `filled=0 stale=1 snapshot=held` after waiting the full settle
  bound.
- With `lines=1` and `rows=100`, the pending-band arithmetic in
  `minimap_fill` and its mirror in paint.c `map_grab` marked every row as
  viewport. Confirmed by the red minimap test: row 1 col 0 has the viewport
  edge colour.

## Changes

- src/editor/minimap.c: the static accessor `map_buffer` treats slot
  `buffer_capacity` as `e->empty` (the large.c receiver pattern). Both the
  receive loop and the release loop now cover the fallback.
- src/minimap/minimap.h, src/minimap/minimap.c: new public
  `minimap_pending_band(lines, rows, r, first, end)`, shared by
  `minimap_fill`'s pending branch and paint.c `map_grab`. It was extracted
  unchanged first (minimap_test green), then the tests went red, then it was
  fixed. With fewer lines than rows, row r is line r (`minimap_prepare`'s
  grouping when `lines_per_row` is 1), with half-open overlap, and rows past
  the last line hold none. With at least as many lines as rows, the existing
  scaled mapping and its inclusive rule are unchanged. The stale edge bar is
  kept.
- src/editor/paint.c: `map_grab`'s pending branch calls
  `minimap_pending_band` (its arithmetic copy is removed).
- tests/minimap_test.c: `pending_one_line` covers an unfilled map with lines=1
  and rows=100: viewport cells only in row 0, ink and stale bar unchanged. It
  also checks the predicate (the map_grab mirror) for (1 line, viewport 0..1)
  and (3 lines, viewport 1..2).
- tests/editor_test.c: `minimap_last_tab_contract` opens a three-line named
  file, closes its tab and steps until the map settles. It asserts
  `e->buffer == e->empty`, that the map is filled and not stale, and that the
  snapshot is released. The pixel region must be uniform viewport edge in
  scanlines 0 and 1 and background everywhere below, including the last
  column. The test runs from the main ladder next to
  `minimap_pending_contract` (same line), from `select_73j_minimap`, and
  alone with `--cxho1-minimap`.
- docs/decisions/edit-cxho.1.md explains why I chose the routing fix over
  `map_sync_fill` (the flag covers only the first fill; a resize would bring
  the defect back) and gives the clamped-mapping rationale.

## Red

```
$ DISPLAY=:95 ASAN_OPTIONS=detect_leaks=1 build/san/tests/minimap_test ; exit=1
row 1 col 0 bg 3c4956 want 4c4c4c
minimap_test:473: cells[r*width+c].bg == want

$ XDG_RUNTIME_DIR=$(mktemp -d) DISPLAY=:95 ASAN_OPTIONS=detect_leaks=1 sh tools/with_display_lock.sh build/san/tests/editor_test --cxho1-minimap ; exit=1
edit-cxho.1 last tab: filled=0 stale=1 snapshot=held
editor_test:4729: FAIL e->buffer==e->empty && e->buffer->map.filled && !e->buffer->map.stale && !e->buffer->map_snapshot
```

## Green

```
$ DISPLAY=:95 ASAN_OPTIONS=detect_leaks=1 build/san/tests/minimap_test ; exit=0
edit-cxho.1: pending one-line map lights only its own row: ok
minimap_test: density, hit round trip, stale, malloc guard: ok

$ XDG_RUNTIME_DIR=$(mktemp -d) DISPLAY=:95 ASAN_OPTIONS=detect_leaks=1 sh tools/with_display_lock.sh build/san/tests/editor_test --cxho1-minimap ; exit=0
edit-cxho.1 last tab: filled=1 stale=0 snapshot=released
edit-cxho.1 last-tab close: empty fallback map fills, single-row viewport passed
```

## Verification

- `make -j4 all` (gcc, -Werror): exit 0.
- `XDG_RUNTIME_DIR=$(mktemp -d) DISPLAY=:95 make -j4 check`: exit 0, ending
  `check: 110 test binaries passed`, then `test_replay_cli: all passed` and
  `display lock contract: ... PASS`. The full editor_test ran inside it,
  including the new case (`edit-cxho.1 last-tab close: ... passed`).
  Stamp: BAT0 `Not charging` (on AC), uptime load average 3.35 / 2.43 / 1.60
  at start. No timings are claimed.
- The `cli_test FAIL ... [forced test timeout]` line in the log is the
  deliberate §36 case ("forced startup timeout correctly failed"). The
  ASan BUS report in os_test is its deliberate stray-fault child. The
  `display :99 held` lines belong to the display-lock contract test's own
  holder.
- `df -h /` was 136G used before check and 139G after (build products only;
  there is no GB-scale fixture in this bead).

## Not done

- The optional private-Xvfb screenshot was not taken. The editor test checks
  the pixel region directly instead.

## Found outside scope (not fixed)

- The pending placeholder's quiet ink columns (the `occupied` columns in
  `minimap_fill`) still paint every row, even for a source with fewer lines
  than rows. After this fix the pending state is transient for small buffers,
  because the worker map arrives. A small file could still flash
  full-height ink for the frames before its first map publishes. The same
  clamped mapping (`r < lines`) would bound it if wanted.
