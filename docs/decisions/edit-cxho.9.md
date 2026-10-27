# edit-cxho.9: miniature of the window lines replaces the 256-sample path

Root cause of the stripes: on files over 64 KiB or 20000 lines, `large_rows` sampled one line per 64 KiB chunk and copied that line's marks to every row in its group. Tobias's CONTENTS.md is 640943 B (M), so it got 10 samples for about 1000 rows. This bead deletes that path (`large_rows`, `sample_ink`, `MINIMAP_SAMPLE_BYTES`, `MINIMAP_MAX_SAMPLES`).

## Library (`src/minimap`)

- **`minimap_prepare_window(m, input, snapshot_source, anchor, height)`.** Row i is the real logical line `top + i`. The anchor is `{window, first_line, first_byte}`:
  - `window` comes from `minimap_window_lines` when the index is ready, or `minimap_window_bytes` while it is building.
  - `first_byte` is snapped back to its line start.
  - When the window is exact, the walk goes back `first_line - window.first_line` lines. This also covers scrolling past the end, where the window clamps `first` to R. While indexing, the walk goes back `window.above` lines.
  - Rows are labelled `first_line - walked + i`, which is the caller's own line numbering. The painter's viewport band therefore falls on the anchor's row even while the label is still an estimate.
- **The unit stays logical lines.** The minimap followed logical lines before this bead (compose passes `state.first_line`), so wrapped rows are not used.
- **Per-line summary.** Marks and ink come from the first MINIMAP_LINE_CAP = 80 bytes of the line, the existing per-line width. Ink is now a prefix count. The painter never drew density, only marks.
- **Line ends.** The end of each line is found by a newline search capped at MINIMAP_SCAN_BYTES = LINEIDX_CHUNK. (G) That cap is the existing per-query scan bound, the same one `minimap_hit`'s `line_to_byte` and `lineidx_refresh` use. A longer line ends the walk:
  - forward: the remaining rows stay blank;
  - backward: the window starts below that line.
- **Read bound (P).** Every inspected byte belongs to a walked line, or to one backward block of 81 bytes (one summary-width line plus its newline). For lines of at most L bytes, inspected bytes are at most `(walk_back + 1)(L + 82) + rows(L + 1)`. For summary-width lines that is about `(2 walk_back + rows) x 81`. It does not depend on file size.
  - (M) on the test's 200000-line source, the window at line 150000 delivered 64512 bytes out of 4799879, against a bound of 103845.
- **Legacy `minimap_prepare` on a large file.** It now builds the window at line 0 with scale 1 and `height` rows. Every row is a real line, so `minimap_hit` returns that row's cached exact line start and no longer needs the index. This also removes the `lineidx_seek_line` call in the fuzz harness, which was the edit-cxho.29 crash.
- **Staleness.** A window prepared without an index (`window_exact` false) stays fresh for an unindexed input with the same revision, bytes and identity. It ignores the estimated line count. When the index completes, the map becomes stale and the exact window replaces it.
  - `minimap_fill` now sets `stale = !fresh` and not `!index_ready`, so a fresh unindexed window paints as ready.
  - `fill_cached` passes its "pending" decision explicitly, where it used to fake `index_ready = false`.
- **`minimap_anchor_moved`** tells the glue that the viewport scrolled and a new window is due.

## Editor glue (`src/editor/minimap.c`)

- **Which files are windowed.** `len > 64 KiB || (index ready && lines > 20000)`. This is the library's large predicate; while indexing only the length is known.
- **Anchor.** It is built from `state.first_line`, `state.first_byte` and the visible logical line count, the same loop compose uses.
- **Geometry is the current painter's.** The window uses `MINIMAP_LINE_PX` (2) per summary row over `map_grid.dims.rows * 2` px.
  - `minimap_pixels` draws row i at y = 2i. The window's rows fill the strip, and the band over the viewport's rows lands at box_top + phase, where phase < 2 px.
  - No change to paint or drag geometry (edit-cxho.10's scope). Large maps stay `sampled`, so the drag keeps its byte-fraction `hit_approx` path.
  - Until edit-cxho.10, the box is drawn at the window position while the drag maps y over the whole strip by byte fraction. They agree to within B/H (box over strip height).
- **When a job is due:**
  - (unchanged) when the map is stale or the height changed;
  - for a windowed map, also when the anchor moved;
  - for a windowed file while indexing, also when the map is not yet filled (see the next item).
- **Memory-resident snapshots.** These use the foreground lane as before, with the window bound above.
- **File-backed, first map.** It is still prepared synchronously in compose, through `editor_map_fill_sync`, now the window and so bounded. This is the existing edit-73j path; it now also runs during indexing.
- **File-backed, refresh while indexing.** It is held (`map_hold_unindexed`) with the map marked stale, so the retained image and its box stay on screen.
  - Reason: the refresh would go to the bulk lane behind the index build and could only land stale once the index completes.
  - editor_large_test caught a frame slipping in that way.
  - Once the index is ready, refreshes follow the anchor on the bulk lane.
- **Small files keep the exact grouped path.** Below 64 KiB and 20000 lines it is exact per line (K = 1) or per group. Unifying it with the window belongs to edit-cxho.10 together with the scrolling geometry: a scrolling small map with today's absolute drag would run away.
