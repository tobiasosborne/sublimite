# edit-cxho.8: fixed-scale scrolling minimap window (pure mapping)

Sublime model chosen by Tobias on 2026-10-27, because on big files the squeeze-to-height box shrinks to a thin line. This bead adds only the pure mapping, in `src/minimap/minimap.h` under "edit-cxho.8". It changes no rendering and no editor code. edit-cxho.9 (rendering) and edit-cxho.10 (editor glue and drag) build on it.

## Model

Inputs: total lines N, visible lines v, scale s in px per line, strip height H in px, and the scroll position f, which is the document's first line.

- Box height: B = min(v*s, H). It depends only on v, s and H, so it stays the same whatever N is.
- Scroll range: R = N - v, or 0 when v >= N. f is clamped to R.
- Travel: D = min(R*s, H - B).
- Scrolling: the window scrolls when R*s > H - B. In that case box_top = floor(f*D/R), so the box acts as a scrollbar thumb whose top runs from 0 to H-B as f runs from 0 to R.
- Not scrolling: box_top = f*s. The box sits over its own lines and the window starts at line 0. Because D = R*s here, one formula covers both cases.
- Window offset: O = f*s - box_top pixels. Scrolling, O rises linearly from 0 at f = 0 to N*s - H at f = R (P: substitute f = R). The window therefore never shows lines past EOF.
- Shared drawing rule: the window draws line L at y = (L - first_line)*s - phase. Its result fields are first_line = floor(O/s), phase = O mod s, and above = f - first_line = ceil(box_top/s). With these, the viewport's first line is drawn exactly at box_top, so the box and the miniature cannot drift apart. That drift is the 387 px defect the research measured. The `rows` field counts the line slots that touch the strip: ceil((H + phase)/s), capped at N - first_line.

Example from the research repro: (M) the strip has about 705 rows of 2 px, so H = 1410; (M) the text view shows v = 45 lines; s = 2.
- Box: B = 45*2 = 90 px at every N.
- At N = 20000: R = 19955 and D = 1410 - 90 = 1320. Each pixel of box travel is ceil(19955/1320) = 16 lines (P).
- Documents with N <= 705 do not scroll (P): R*2 > 1320 means N - 45 > 660, that is N > 705, the same threshold as N*2 > 1410. The tests probe 704, 705 and 706.

## Rounding

The forward mapping uses floor and the inverse (`minimap_window_seek`) uses ceil. This pair makes the round trip exact on whichever side has the finer step:

- When R >= D (lines denser than pixels, every big file), box_top(seek(t)) == t. The drag is 1:1 with the pointer.
- When R <= D, seek(box_top(f)) == f.
- In every other case the error is within one step, ceil(R/D).

Both ends are exact: f = 0 maps to 0, f = R maps to D, and seek(D) = R.

The products f*D and t*R can exceed 64 bits, since lines go up to LINEIDX_MAX_LEN + 1. They are computed in `unsigned __int128`. gcc and clang are the only toolchains, llvm-mingw included. No floating point is used.

## Byte-fraction variant (index building)

`minimap_window_bytes` uses R = bytes, D = H - B and box_top = floor(first_byte*D/bytes), always as a thumb. There is no small-document case, because the line count is unknown while the index builds.
- first_line is MINIMAP_LINE_UNKNOWN. The renderer anchors on the viewport's first byte and walks `above` lines backwards from it.
- The seek returns a byte. The caller resolves it to a line start, as `editor_scroll_seek` already does.
- When the index completes, the box moves from the byte fraction to the line fraction once. That is the "small correction" in the brief.
- Near EOF the viewport's first byte is below `bytes`, so the box stops slightly short of the bottom until the index completes.

## Press and drag

`minimap_window_grab(w, y, &offset)` handles a press at strip y:
- Inside the box, it keeps offset = y - box_top.
- Outside, offset = B/2. Passing that offset to `seek` centres the box on the pointer, which is the click warp.

`minimap_window_seek(w, y, offset)` clamps y - offset to [0, D], saturating on int64 overflow. Any window computed from the same N, v, s and H gives the same seek, because seek reads only range and travel.

## Degenerate inputs

- s = 0 or a NULL out: MINIMAP_ERR_ARG, and out is left unchanged.
- N = 0: rows = 0 and the box is at the top.
- v >= N: R = 0, box_top = 0.
- H < v*s: B = H and D = 0. The box fills the strip, the window starts at f when R > 0, and seek returns 0. That is a strip too short to drag.
- H = 0: an empty box and no rows.
- f > R: clamped to R.

## Notes for the consumers

- **Scroll past end.** The editor lets first_line reach N - 1 (`scroll.c` clamps to `extent.lines - 1`). Under this model the box pins at the bottom once f exceeds N - v. To give the thumb that last stretch, edit-cxho.10 can pass N + v - 1 as `lines`. The map then has v - 1 blank lines after EOF, as Sublime's does.
- **Units.** `scale` and `height` are in the strip's pixel units. MINIMAP_LINE_PX = 2 is the (G) logical scale from perf §1 Layout. The current `minimap_pixels` draws 2 device px per summary row, so on a HiDPI box edit-cxho.10 picks logical times the buffer scale, or keeps device px to match today's strip.
- **Line numbers.** The exact variant needs exact line numbers: index_ready, and f as a logical line. With wrap on, f is the first visible line's logical line, which is what `paint.c` already passes to `minimap_fill`.
