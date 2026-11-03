# edit-b6ai.17: the viewport box stays inside a short document's strip

## Finding

The reported picture (Tobias's screenshot, terra drive README) is not a geometry drift. `docs/decisions/edit-cxho.10.md` gives the wrapped view the lines `N + v - 1`, because the wrapped editor scrolls the last line to the top (editor maximum first_line = N - 1). A short document does not scroll in the strip (R*s <= H - B), so box_top = f*s. At f = N - 1 the constant box (B = v*s) starts on the last line's scanlines and hangs v - 1 lines below the strip's last line. Dragging to the bottom reaches that top: the editor's own maximum, so the clamp already held (tests added). Wrapped Ctrl+End does the same; that is a separate editor choice (Found).

Sublime reference: its minimap box is the visible region. Past the end of the text there is no minimap line, and edit-cxho.8 chose the window model so the box and lines cannot drift. A box hanging past the last line contradicts "the box covers the strip's lines that are visible".

## Decision

- **Clip the box's bottom to the document's last line when the window does not scroll** (short documents). `minimap_frame.clip_to_end`: bottom = min(box_top + B, line_y + (lines - line)*scale), never fewer than one scanline. (P) line `line` starts at line_y, so line `lines` starts (lines - line)*scale below it.
- A scrolling window keeps the constant box (edit-cxho.8): its travel ends at H - B, so the box cannot leave the strip, and the thumb stays constant for big files. The painter sets the flag only for an indexed, non-scrolling window; while the index builds, `line` and `lines` are estimates.
- The box still spans the strip's full width (x = 0 .. width - 1 edges), checked by the new test. The strip's ink is narrower than the box because a line's marks are capped at MINIMAP_LINE_CAP = 80 columns at 1 px; that is unchanged.
- The drag is unchanged: the target is clamped by `minimap_window_seek` to R, and the editor's scroll clamps to its own maximum. Tests check first_line <= max at ten pointer positions plus one past the strip.
- Not done on purpose: clamping the wrapped drag to N - v. Wrapped lines occupy more than one row, so lines N - v..N - 1 can exceed the viewport and the end of the document would be unreachable by drag.
- Wrapped single long line (N = 1, more wrapped rows than the viewport): v = 1, box = the whole 2 px strip, no travel. The minimap's unit is the logical line (edit-cxho.9), so scrolling inside the line does not move the box.

## Tests

`tests/editor_test.c` `--b6ai17-short` (short unwrapped, short wrapped, one long wrapped line) and `tests/minimap_test.c` `window_paint` (clip_to_end cases).
