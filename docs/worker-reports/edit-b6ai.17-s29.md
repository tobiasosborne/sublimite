# edit-b6ai.17 worker report (s29, Sonnet)

Outcome: the viewport box no longer hangs below a short document's strip; drag clamp and box width verified by new tests. Decision: docs/decisions/edit-b6ai.17.md.

## Changes
- src/minimap/minimap.h, minimap.c: `minimap_frame.clip_to_end`; `minimap_pixels_window` clips the box bottom to the document's last line.
- src/editor/paint.c: sets it for an indexed, non-scrolling window.
- tests/editor_test.c: `minimap_short_contract` (`--b6ai17-short`, also run in the main sequence): 20 lines unwrapped, 20 lines wrapped, one 4000 B wrapped line; 30 rows, 40 columns; box inside the strip's document part (2N px), at least one line, first_line <= editor max (0 unwrapped, N-1 wrapped), edge columns at x = 0 and width - 1, box ends at the document end at the drag bottom.
- tests/minimap_test.c: clip_to_end pixel cases.

## Red
`build/tests/editor_test --b6ai17-short` before the fix: `short wrapped: 12 failures`; at drag steps 1..10 `box top=38 height=40` against strip_end 40 (box ended at 78). Unwrapped and one-long-line cases 0 failures (already correct). minimap_test red = compile failure (no `clip_to_end`).

## Green
Same command: 0 failures in all three cases; `--cxho10-track` still 0 failures in all five cases; minimap_test ok. `make check`: `check: 118 test binaries passed in 83.7 s (parallel, 36 jobs, 6 displays)` (the "display lock timeout" lines come from tools/test_display_lock.sh's contract test, which ends PASS). `make all` and `make TARGET=win all` build.

## Manual (Xvfb :97, throwaway XDG under build/x97)
20-line unwrapped file: box covers the whole short strip, no overhang (build/x97/shots/after_ctrlend.png). I did not find how to turn on wrap for a before/after of the wrapped case without touching config, so the wrapped case is covered by the test only. Nothing was run on :0. My launched instance was killed by its PID.

## Found (not fixed)
- Wrapped Ctrl+End and wrapped drag-to-bottom put the last line alone at the top (editor scroll-past-end in wrap mode only; unwrapped stops at N - rows). Same as terra's "Large-file Ctrl+End discards visible context". Changing it is an editor scroll decision.
- In wrap mode the minimap is per logical line, so scrolling within one long wrapped line does not move the box.
