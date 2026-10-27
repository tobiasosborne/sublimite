# edit-cxho.10: minimap editor glue (constant box, scrolling window, 1:1 drag)

This bead connects the edit-cxho.8 window model (`minimap_window_*`) and the edit-cxho.9 window rows to the editor's paint and drag code. Glue: `src/editor/minimap.c`. Paint and drag: `src/editor/paint.c`. Painter: `minimap_pixels_window` in `src/minimap`.

## One window for paint and drag

`editor_map_window(e, input, &w)` builds the window for the current viewport. Compose and the pointer handler both call it, so the painted box and the grabbable box are the same box.

- **Units.** Strip device pixels, as the painter used before this bead: `MINIMAP_LINE_PX` = 2 px per line over H = text rows x cell_h, the pixel region's height. No logical/HiDPI scale is introduced; edit-cxho.8's note left this choice open.
- **Box lines v.** v is the number of text rows, or the line count when the document has fewer lines. The box is therefore B = 2v px at every scroll position, in both wrap modes.
  - The count is the input's, which is the buffer's own count while the index catches up after an edit. Gating it on index readiness made the box jump to a full screen during a small file's pending frames. edit-g11's typing-flash contract caught that.
  - Wrapped rows can show fewer logical lines than rows. The box then stands for a screenful of rows, as a scrollbar thumb does. The minimap stays in logical lines (edit-cxho.9).
  - The alternative was v = logical lines on screen, the count compose passes to `minimap_fill`. It makes the box shrink to 2 px when the wrapped view scrolls to the last line.
- **Lines passed.** These differ by wrap mode:
  - **Unwrapped: N.** edit-cxho.8's note assumed the editor scrolls past the end. Unwrapped it does not: `scroll.c max_top` clamps the top to N - rows. A thumb sized for N + v - 1 would stop short of the strip's bottom at the last reachable top. With N, R = N - v matches `max_top` exactly.
  - **Wrapped: N + v - 1.** The visual-row view lets the top reach the last line. Seen in the first screenshot run: a wrapped Markdown file opened at its last line showed that line alone at the top. So R = N - 1 there.
  - The tests check the bottom clamp in both modes: first_line = N - v unwrapped, N - 1 wrapped, with the box at the end of its travel.
- **While indexing.** Exact lines are used when the index is ready, otherwise the byte fraction (`minimap_window_bytes` on `first_byte`).
  - A drag then seeks a byte that the scroll path resolves to a line start at or before it, so the document's own box sits at most a line's pixel share above the pointer. Measured: 1 px on skew20k, 1090000 B over 377 px of travel (M).
  - When the index completes, the box switches once from the byte fraction to the line fraction. That switch is the small correction the brief allows. It is not animated.

## Paint

`minimap_pixels_window` draws each row at y = line_y + (label - line) x 2, marks on the first of its two scanlines. The box comes from the caller, never from the rows. Three cases:

- **Fresh exact window.** Rows are placed relative to the viewport's first line at the current `box_top`. This is the edit-cxho.8 drawing rule, so the miniature and the box cannot drift.
- **Any other retained rows** (stale after an edit, unindexed, or prepared for an older anchor). They keep their own anchor's geometry until the new window lands. Under a typed character the image therefore does not move, which the edit-g11 typing-flash contract checks.
- **While dragging.** The box is painted at the pointer-derived top (`e->map_drag_top`). It never waits for a scroll resolution or a map job.

The pending placeholder (no compatible map yet) is also drawn by `minimap_pixels_window`:
- Its quiet ink covers only the scanlines of lines that exist. This closes edit-cxho.1's note that the placeholder inked every row of a file shorter than the strip.
- Its box is the window box, so the placeholder and the first map put the box in the same place.

## Drag

- **Press inside the box.** It only grabs (`minimap_window_grab`): the offset is kept and nothing scrolls.
  - Reason: `seek(box_top(f))` is the first line of that pixel, which can be up to ceil(R/D) - 1 lines above f. For example, 20000 lines over 377 px gives about 53 lines per pixel (P).
  - For the same reason, a motion that leaves the box top unchanged does not seek.
- **Press outside the box.** It centres the box on the pointer (offset B/2) and seeks there: the warp.
- **Motion.** The box top is t = clamp(y - offset, 0, D). Two cases:
  - When the window does not scroll, t is floored to whole lines (the line under the pointer, as the grouped map did).
  - Otherwise `minimap_window_seek` is used. With lines denser than pixels, `box_top(seek(t)) == t`, so the drag is exactly 1:1.
- **Resolving the target line to a byte**:
  - When the line is in the current exact summary rows, the editor adopts that row's cached start synchronously, as go-to-line does: `editor_scroll_place`, falling back to `editor_scroll_seek`. No source is read on the UI thread.
  - Otherwise the new `editor_scroll_seek_line` hands the line to the resident resolver. Wrapped views use the existing wheel seek, with a line request.
    - `editor_wheel_continue` and `editor_wheel_ready` (`input.c`) used to resolve only `SCROLL_BYTE` through the resident window. Any other request was treated as visual-row wheel steps, so a wrapped line seek never moved the document. The wrapped test case caught this.
    - Both functions now resolve `SCROLL_LINE` the same way. Nothing else issues a line request on `e->wheel`.
    - The wheel's `scroll_state` clamps a line to N - rows. A wrapped async target past that lands there. Targets inside the summary rows, which include the bottom of the strip, are placed exactly.
  - While indexing, the byte target uses the existing byte path.

## Small files join the window

A small file (64 KiB or less and 20000 lines or less) now uses the window as soon as its index is ready, which happens at open (`buffers.c`). Why:
- A 5000-line, 60 KiB file grouped K = ceil(5000/218) = 23 lines per row (P, 218 rows at the test geometry). Its box was ceil(29/23) x 2 = 4 px: the same thin line the owner reported on big files.

How it works:
- A window that does not scroll starts at line 0 whatever the top, so its job is anchored at line 0 / byte 0. Scrolling such a file prepares no new job.
- A file taller than the strip scrolls like a large one. Each scroll that moves the window submits one foreground job, bounded by the window's lines (edit-cxho.9's read bound).
- The grouped whole-file path stays in the library for `minimap_prepare` / `minimap_fill` callers. The editor no longer produces grouped maps once an index is ready. The compose branch for a retained grouped map is kept only for safety.

## Test helpers

`editor_map_due(e)` exposes `map_due`. The `settle_map` helpers in `editor_test.c` and `editor_scroll_test.c` wait on it, so they also wait for windowed refreshes after a scroll, which edit-cxho.9 flagged as missing.
