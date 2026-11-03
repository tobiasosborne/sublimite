# Sublimité test drive — 2026-11-03

Setup: `build/sublimite`, mtime 2026-11-03 14:21:06.660082682 +0100, SHA-256 prefix `3507b26c66f1`. Private Xvfb `:97`, 3840×2160, 52 dpi; 18 pt; raster fallback (EGL unavailable); throwaway XDG data/config/runtime, state unset. Load average at setup: 0.91 / 0.90 / 2.09. Fixtures were copies of README.md and paint.c and the first 50,000,000 bytes of the log corpus. Window resized to 3840×2160, confirmed with xdotool. Numbers below are (M) [AC, desktop monolith, Xvfb :97 raster].

## Short-file minimap viewport extends beyond its content — MINOR

**Did:** Open README, Ctrl+End, go to line 20, and click/drag in the minimap. **Saw:** the line strip ends well above the bottom of the shaded viewport box; dragging to the bottom places the final empty line at the top of an otherwise blank editor. The indicator moves but suggests an area below the document. See `build/terra/shots/palette.png` and `build/terra/shots/README-real-drag.png`. **Expected:** the viewport indicator to correspond clearly to the visible portion of the document, including at the end of a short file. This reproduces the vertical separation in the supplied screenshot; I did not establish that the box hangs off the window's right edge in this run.

For paint.c, the minimap shows recognizable indentation and blank-line structure, its box moves after wheel, PageDown and Ctrl+End, and clicking then dragging moved the first visible line from approximately 215 to 415 (`build/terra/shots/paint-start.png`, `paint-wheel.png`, `paint-pagedown.png`, `paint-end.png`, `paint-real-click.png`, `paint-real-drag.png`). For the large log, click and drag moved the first visible line from about 96,607 to 186,892 and the indicator moved (`build/terra/shots/big-start.png`, `big-wheel.png`, `big-pagedown.png`, `big-end.png`, `big-real-click.png`, `big-real-drag.png`). Its compressed strip is nearly uniform, so individual lines cannot be visually matched to the log. README is shorter than one viewport, limiting scroll tracking checks. The first scripted click/drag attempted x=3790, outside the actual rendered minimap; the later `real-click` and `real-drag` screenshots use x=2820 on the visible strip.

## Resized 4K window leaves its right quarter unused — MAJOR

**Did:** Resize the editor window to 3840×2160 on :97 and inspect all three files. **Saw:** the visible minimap and the right end of the editor UI stop around x=2870, leaving roughly 960 pixels on the right without editor content, despite xdotool reporting the window as 3840×2160. See `build/terra/shots/paint-start.png`, `big-start.png`, and `readme-full.png`. **Expected:** editor layout and minimap to use the available window width. This also makes a click at the physical right edge miss the visible minimap.

## Input-to-frame latency on raster — MAJOR

**Did:** In each file send 100 wheel-down notches (10 ms spacing), 50 PageDown keys (20 ms spacing), Ctrl+End, mouse interactions, then type 200 ASCII characters using `xdotool type --delay 30`; quit with Ctrl+Q and discard. Tracedump summarized the entire mixed session, not separately isolated operations. The trace logs are `build/terra/paint-trace.txt` and `build/terra/big-trace.txt`, with binary traces alongside them. **Saw:** (M) paint.c G1 input-to-submit n=280, p50 23.610 ms, p99 57.519 ms; G3 n=279, p50 30.133 ms, p99 68.341 ms; T6-T0 n=279, p50 53.380 ms, p99 99.767 ms. (M) 50 MB log G1 n=236, p50 25.971 ms, p99 43.243 ms; G3 n=235, p50 34.167 ms, p99 56.772 ms; T6-T0 n=235, p50 56.505 ms, p99 85.364 ms. Screenshots: `build/terra/shots/paint-typed.png`, `big-typed.png`. **Expected:** lower and more consistent response on ordinary typing and scrolling. These are internal trace timings, not independently measured keypress-to-visible-glyph latency; samples may be coalesced and cannot be assigned to individual input types. Tracedump did not print separate frame statistics beyond these timing sample counts. No separate screenshot-timed response was measured.

## Large-file Ctrl+End discards visible context — MINOR

**Did:** Ctrl+End in the 50 MB log. **Saw:** the final partial line appears alone at the top, with the rest of the 2160-pixel editor blank (`build/terra/shots/big-end.png`). **Expected:** the end of the document to be visible with preceding lines filling the available viewport where possible. The file remained navigable by minimap afterward.

## Other checks

Ctrl+N created a tab, Shift+Home highlighted the entered text, Ctrl+W displayed a save/discard/cancel prompt, Ctrl+Tab switched tabs, Ctrl+F displayed the search field, Ctrl+G accepted a line number, and Ctrl+Shift+P displayed the command palette (`build/terra/shots/selection.png`, `save-prompt.png`, `find.png`, `goto.png`, `palette.png`). Syntax colours appeared in paint.c. These were smoke checks, not exhaustive tests; wrapping and save-to-disk were not independently verified. Throwaway session recovery caused prior tabs to reappear between launches, so later screenshots include those tabs.

## Three findings that most hurt daily use

1. A 4K-sized window leaves about one quarter of its width unused.
2. Raster input-to-submit latency reaches (M) 43–58 ms at the 99th percentile in these mixed runs, with later trace milestones higher.
3. The minimap's short-document viewport box extends below its line strip and can lead to a mostly blank view.
