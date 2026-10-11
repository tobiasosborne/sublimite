# edit-73j: editor pointer, scrolling, find and runtime publication

This work implements the wiring requested in the supplied bead and the corresponding findings in [the review](../reviews/P4-3.md). The Ctrl+F/H consumption change and editor font runtime owner were absent in this worktree at inspection, so both are implemented here. No tracking or version-control command was run.

Text hits come from an additive optional layout callback. Layout records each drawn cluster's byte boundaries and position within its rendered cells, including tabs, clipped wide characters, synthetic wrap indent, gutters and blank tails. The editor reserves this metadata with its grids and reads it without dereferencing document bytes. Pointer input waits for prepared metadata within the existing input/layout turn, without an added frame timer. The view's existing sliced word/line commands handle multi-click selection and word/line dragging. Shift-click retains the original anchor. Click-chain timing reuses `EDITOR_BLINK_NS` as an (E) policy choice; this is an existing interval, not a measured Sublime timing claim.

The renderer's grid contract has no origin/clip fields. The supplied per-line fallback therefore applies: `SCROLL_CORE_ROWS` is the requested Sublime default (G), and displacement is `dy * SCROLL_CORE_ROWS / PLAT_WHEEL_UNIT`, retaining the fractional remainder. Logical navigation uses immutable snapshots and the scroll resident bridge. Wrapped wheel movement uses prepared visual rows and bounded adjacent-row queries rather than logical-line displacement. Page/follow behavior remains in the existing view implementation. Pixel origin/overscan and displayed-refresh certification are not claimed.

The find controller reserves an exclusive arena and binds findui's shared-pool mailbox router and complete source-storage lease. Query editing never mutates the document. Closing the visible panel retains the search session for F3 navigation. Ctrl+F/H opens the corresponding mode, Enter/Shift+Enter and F3/Shift+F3 navigate, Tab switches query/replacement fields, Ctrl+Enter replaces the current match, Ctrl+Alt+Enter replaces all, and Escape closes or cancels a replacement prefix. Alt+R/C/W toggles regex/case/whole-word options. Ctrl+A selects the current field; global Ctrl chords pass to the editor. Find occupies a query row; replace adds a replacement row and a hints row, clipped to leave text visible. Row counts derive from those visible elements (G).

Findui owns a replacement's explicit undo group. Host callbacks own each successful mutation's index/layout, revision, dirty state and journal delta. History records the contiguous interval between the leftmost and rightmost successful mutation, including intervening unchanged bytes; it does not include the unrelated suffix. Escape and failures retain an undoable successful prefix. New source snapshots are published only outside a replacement group. Far result navigation uses the resident byte resolver, prepared visibility metadata, a certified logical anchor for bounded visual queries, and bounded horizontal follow. The prepared visual seed is carried into layout without a repeated whole-file line lookup. Invalid patterns reject replacement without retaining a runnable request.

Minimap summaries have separate reserved worker storage. Jobs retain immutable snapshots and the buffer's entire allocator/file storage until both mailbox handling and physical completion. Publication checks source identity, revision, size, count and height. UI composition uses only `minimap_fill_cached`; pending/sampled navigation takes an approximate byte target through resident resolution. Publication during queued typing is discarded and retried, preserving the already drawn map through pending input. Publication starts a normal frame; cell comparison damages only changed rows, preserving unchanged tab chrome. Benchmark/test fixtures explicitly await worker preparation outside measured input intervals.

Font discovery and runtime preparation run on the worker, and adoption happens through matching live mailboxes before backend initialization. Backend limits derive from `font_runtime_limits`. Both grids and title lookup use the adopted owner. Titles keep baked ASCII slots, bind runtime additions to the actual scratch grid, and continue the same pending frame when composition yields. The single append-only owner and its arenas survive all backend borrows and are freed after backend shutdown. No font I/O or arena growth occurs on typing.

Reservations reuse existing module bounds (G/E), rather than inventing budgets: glyph entries use `FONT_RUNTIME_MAX_GLYPHS - LAYOUT_ASCII_GLYPHS` (the existing runtime/GL descriptor cap less the baked descriptors), and find ranges use `LAYOUT_CLUSTER_CACHE`; glyph keys reserve that entry count times `FONT_CLUSTER_MAX_BYTES`; font scratch uses `FONT_ATLAS_PAGE_DIM * FONT_ATLAS_PAGE_DIM`, matching the existing runtime fixture; runtime page count uses `FONT_ATLAS_MAX_PAGES`; font file storage reuses the editor's existing default buffer-arena reservation. Hit storage is `max_cols * max_rows * sizeof(editor_hit)`. Find worker storage counts both existing worker slots' scratch/program maxima, both streams' bounded outboxes, exact query storage, and a program-sized metadata/alignment allowance (E). The code records the arithmetic next to each reservation.

Existing findui history-stat traversal and whole-source page/window rescans are reported without changes; those are review findings outside this bead. Runtime cache exhaustion and missing/unsupported fallback fonts retain the font module's existing behavior. Native rendering and actual displayed cadence remain coordinator/Astra acceptance work.

Landing note (squash onto main after v0o.7, 3kk, a6v, 457.8, v0o.1, agh): the
per-line wheel fallback and the per-buffer resident bridge this bead built
(`b->resident`, `editor_scroll_service`) are dropped in favour of edit-3kk's
scroll path. Unwrapped wheel input is 3kk's `editor_handle_wheel`; 73j's
visual-row stepping remains only for wrapped views, where 3kk keeps no state.
Far find results and pending-minimap targets seek through `editor_scroll_seek`
when unwrapped (horizontal follow runs after 3kk adopts the line) and, when
wrapped, through `editor_scroll_resolve`, which shares the buffer's single
resident window. Pointer hits invert the frame's Q8 origin with
`scroll_frame_hit`. The font runtime owner this bead added duplicated edit-08z
(already on main, `font_async` with m3g zoom); main's owner is kept and 73j's
`font_runtime` adoption, title binding and chrome continuation are dropped.
The find panel's rows are excluded from scroll's viewport rows and from the
minimap pixel region; panel open/close reuses the resize text-geometry helper
(`editor_text_geometry`) so the P3.4 overscan row is kept. Ctrl+Shift+F no
longer opens this panel (Find in Files stays unimplemented).
With the panel open, frames draw unshifted text rows: the renderer's scroll
region must reach the surface bottom, so the fractional origin is not applied
while panel rows sit below the text. Fixtures that count frames or hold a
present now await the worker-published minimap first (settle_map in
editor_test and editor_scroll_test), since publication starts its own frame.

Minimap summary lane (coordinator decision at landing, 2026-10-11). The bulk
lane can be monopolised by a large file's index/warm/find jobs (edit-91c is
the same family), and a summary queued behind them would leave the minimap
stale for that whole time. A summary's cost is bounded independently of file
size: small sources scan at most MINIMAP_SMALL_BYTES = 65536 bytes, and
sampled sources read at most MINIMAP_MAX_SAMPLES x MINIMAP_SAMPLE_BYTES =
256 x 256 = 65536 bytes (G, src/minimap/minimap.h). On top of that it writes
map_capacity rows (one per two surface pixel rows). At the naive scalar scan
rate measured in perf/00-hardware-profile.md (6.3 GB/s (M)[AC]), 65536 B /
6.3e9 B/s = about 10 us (E), far inside the foreground lane's 5 ms
cancellation-poll bound (G, src/work/work.h).
- (1) A memory-resident snapshot (no file backing) therefore runs on
  WORK_FOREGROUND.
- (2) A file-backed snapshot's sample reads can fault, which is blocking I/O,
  and the foreground contract excludes that. Its first map is prepared
  synchronously in compose (main's path before this bead); later refreshes use
  the bulk job.
- If the pool has no foreground lane, the job falls back to WORK_BULK.
