# edit-cxho.10 (s24): minimap editor glue: constant box, scrolling window, 1:1 drag

Worker: Opus, worktree `.wt/edit-cxho.10-s24`, display :95. The design is in `docs/decisions/edit-cxho.10.md`.

## What changed

**Constant box.** The painted box and the grabbable box both come from one window, `editor_map_window`. That window is the edit-cxho.8 model in strip device pixels: 2 px per line over the text rows' pixel height.
- The box is v x 2 px, where v is the number of text rows, or N when the document has fewer lines.
- When the document is taller than the strip, the minimap scrolls like a thumb.

**Drag.**
- A press inside the box grabs it at the pointer's offset and does not scroll.
- A press outside centres the box on the pointer (the warp) and scrolls there.
- Motion moves the box 1:1 with the pointer. While dragging, the box is painted at the pointer-derived top.
- The target line resolves to a byte in one of two ways:
  - When the line is in the exact summary rows, the editor adopts the row's cached start synchronously, as go-to-line does. This reads no source on the UI thread.
  - Otherwise it uses a line seek on the scroll resolver. Unwrapped, that is the new `editor_scroll_seek_line`. Wrapped, it is the wheel's resident resolve, which now also accepts `SCROLL_LINE`.
- While the index builds, the byte-fraction window is used. The document's box then lands at most 1 px above the pointer (M, below).

**Small files use the window too.** They join once their index is ready, which happens at open.
- A small window that does not scroll anchors its job at line 0, so scrolling such a file submits no new job.

**Pending placeholder.** Its ink is now bounded to the scanlines of lines that exist, which closes the edit-cxho.1 note. Its box is the window box.

**Window lines differ by wrap mode.**
- Unwrapped: N. `scroll.c max_top` is N - rows. edit-cxho.8's scroll-past-end assumption does not hold unwrapped.
- Wrapped: N + v - 1. The visual-row view does reach the last line.

## Files touched

- `src/minimap/minimap.h`, `src/minimap/minimap.c`: `minimap_frame` and `minimap_pixels_window`, the window-geometry painter. It draws the box from the caller and the rows by label, and also draws the bounded pending placeholder.
- `src/editor/minimap.c`:
  - `map_windowed` now includes small indexed files.
  - New: `map_visible`, `map_lines`, `editor_map_window`, and `editor_map_due` (exported for the test helpers).
  - `map_anchor` anchors non-scrolling windows at line 0.
- `src/editor/paint.c`:
  - Compose paints through `minimap_pixels_window`, using the current window and the drag top.
  - `map_grab` is removed. The drag uses `minimap_window_grab` and `_seek`.
- `src/editor/private.h`: the `map_drag_top` field, plus declarations for `editor_map_window`, `editor_map_due` and `editor_scroll_seek_line`.
- `src/editor/scrollctl.c`: `editor_scroll_seek_line`.
- `src/editor/input.c`: `editor_wheel_ready` and `editor_wheel_continue` resolve a wrapped `SCROLL_LINE` request through the resident window. Before this, a wrapped line seek was taken for visual-row wheel steps and never moved the document.
- `tests/editor_test.c`:
  - `minimap_track_contract` with five cases: skew20k, 200k, skew20k-indexing, small50, skew20k-wrapped. The selector is `--cxho10-track` and the cases are in the default run.
  - `settle_map` now waits on `editor_map_due`, so it covers windowed refreshes.
- `tests/editor_scroll_test.c`:
  - `settle_map` waits on `editor_map_due`.
  - The main probe restores the normal present before its final settle, because its 200-line fixture's minimap now scrolls with the view (details under "Test adaptations").
- `tests/minimap_test.c`: `window_paint`, a unit test for `minimap_pixels_window`.
- `docs/decisions/edit-cxho.10.md`: new.

## Red run

`build/san/tests/editor_test --cxho10-track` on :95, on the unchanged tree with only the new test added. BAT0 "Not charging", load1 0.32 (`uptime` at 13:33).

Geometry: 40x30, H = 435 px, v = 29, B = 58 px, grab = B/2 = 29. Pointer ys from 29 to 406 in nine steps.

skew20k, box top painted versus pointer - grab:

| pointer y | box top | want | off |
|---|---|---|---|
| 29 | 0 | 0 | 0 |
| 70 | 86 | 41 | **+45** |
| 112 | 178 | 83 | **+95** |
| 154 | 210 | 125 | +85 |
| 196 | 232 | 167 | +65 |
| 238 | 256 | 209 | +47 |
| 280 | 278 | 251 | +27 |
| 322 | 300 | 293 | +7 |
| 364 | 324 | 335 | -11 |
| 406 | 346 | 377 | -31 |

- Warp click at y = 188: box at 228, want 159.
- 13 failures.

This is the research's byte-fraction defect at this geometry. At the research's 1410 px strip it was 387 px.

The other cases:
- **200k:** off -7, -27 ... -49 px. Warp: 140, want 159. 13 failures.
- **skew20k-indexing:** 12 failures. At the bottom no box was painted at all (top -1).
- **small50:** 9 failures. The box was stuck at first_line 22.

Notes on how the red test differs from the final one:
- That first version still expected the small file's travel and bottom from edit-cxho.8's N + v - 1 note ("want 100").
- It lacked the "document follows" check.

Both were corrected after the first green probe showed `scroll.c` clamps the unwrapped top to N - v. The final definitions are below.

**Wrapped case, red.** It was added after the first green. Its red run is the implementation with the window lines forced to N while wrapped (a temporary probe, since removed) and without the `input.c` fix. Result: the document never moved (first_line 0 at every pointer y, doc top 0 against want 41 ... 377). 10 failures.

## Green run

`build/san/tests/editor_test --cxho10-track` on :95, exit 0. BAT0 "Not charging", load1 1.07 to 1.10 (`uptime` 13:50 to 13:55).

| case | box height | box top vs pointer - grab, 10 positions | document's own box top vs want | bottom clamp | warp at y = 188 |
|---|---|---|---|---|---|
| skew20k | 58 | 0 px off at all 10 | 0 px off | top 377 (want 377), first_line 19972 = N - v | 159 (want 159) |
| 200k | 58 | 0 px off | 0 px off | 377, first_line 199972 = N - v | 159 |
| skew20k-indexing (byte fraction) | 58 | painted 0 px off | -1 px (the line-start snap) | 377 | 159 |
| skew20k-wrapped | 58 | 0 px off | 0 px off, -1 at the last step | 377, first_line 20000 = N - 1 | 159 |
| small50 (non-scrolling, travel 44) | 58 | 0 or -1 px (floor to whole lines) | box top == 2 x first_line exactly | 44, first_line 22 | — |

Other suites:
- `minimap_test` (san, detect_leaks=1): exit 0, including "edit-cxho.10: window-geometry pixels, box from the caller, bounded pending ink: ok".
- `editor_test`, full default run (san, :95, detect_leaks=1): exit 0, "editor_test: all passed". This includes:
  - the G1 typing guard and malloc guards (`allocation_test`, `minimap_typing_flash`: mallocs=0, no pending-pixel change);
  - edit-2mhd's grab contracts and the edit-d5o compose and pixel reference, both unchanged;
  - edit-cxho.1's last-tab map.
- `editor_scroll_test`: exit 0, after the adaptation below.
- `editor_large_test`: exit 0.

**One regression, caught and fixed.** The first full `make check`, after the wrap change, failed in `minimap_typing_flash`, which reported changed_pending != 0.
- Cause: `map_visible` had used the line count only when the index was ready. A small file's index is briefly not ready after each keystroke, and in those pending frames the box grew to a full screen.
- Fix: v now always comes from the input's line count, which is the buffer's own count while indexing.
- After the fix: "edit-g11 null typing: keys=95 frames=97 orange_frames=0 stale_frames=96 changed_pending=0 chrome_markers=0 mallocs=0".
- That failing run also reported ASan leaks of the probe's own buffers. These are a consequence of the early `T()` return in the test, not a product leak.

## Test adaptations (existing tests)

**`editor_scroll_test` main probe.** Its 200-line fixture is taller than the strip:
- (P) Its 201 lines at 2 px each are 402 px. The strip is the probe's text rows, fewer than its 8 surface rows, times cell_h. That is shorter than 402 px at the default cell height, so the window scrolls.
- So under the window model each wheel scroll publishes a new window and one more frame.
- The probe had switched to a never-completing `delayed_present` before its last wheel, so that extra frame stayed active forever and the final `settle` failed.
- The probe now restores the original present op before signalling completion. The asserted correlation checks are unchanged.

**`settle_map` helpers** in both files now call `editor_map_due`, as edit-cxho.9 asked.

## Verification

- `make -j4 all` (gcc 13, -Werror): exit 0.
- Sanitizer binaries, run with `ASAN_OPTIONS=detect_leaks=1 XDG_RUNTIME_DIR=$(mktemp -d) DISPLAY=:95 sh tools/with_display_lock.sh build/san/tests/<name>`: minimap_test 0, editor_test 0, editor_scroll_test 0, editor_large_test 0.
- `make -j4 check` (DISPLAY=:95, detect_leaks=1), started 13:55 with BAT0 "Not charging", load1 1.07: **exit 0**. It printed `check: 112 test binaries passed`, then `test_replay_cli: all passed`, then `display lock contract: ... PASS`. This run is after the `map_visible` fix. The earlier failing run is described above.
- The final release build `make -j4 all` exit 0 came before that check, at the same tree.

## Screenshots

Taken with `build/sublimite --new-instance` on :95 under `tools/with_display_lock.sh`, with `EDIT_REFUSE_REAL_DISPLAY=1` and private HOME/XDG directories under the scratchpad.

Fixtures:
- `doc20k.md`: 20000 lines of generated Markdown, 909723 B, wrapped by default as `.md`. It has headings, lists, indented code and paragraphs.
- `doc50.md`: its first 50 lines.

Screenshots:
- `/tmp/claude-1000/-home-tobias-Projects-sublimite/3dfb7436-2086-45df-b788-60ade1f4789a/scratchpad/cxho10/shot/doc20k-top.png`
- `/tmp/claude-1000/-home-tobias-Projects-sublimite/3dfb7436-2086-45df-b788-60ade1f4789a/scratchpad/cxho10/shot/doc20k-mid.png`
- `/tmp/claude-1000/-home-tobias-Projects-sublimite/3dfb7436-2086-45df-b788-60ade1f4789a/scratchpad/cxho10/shot/doc20k-end.png`
- `/tmp/claude-1000/-home-tobias-Projects-sublimite/3dfb7436-2086-45df-b788-60ade1f4789a/scratchpad/cxho10/shot/doc50.png`

- `/tmp/claude-1000/-home-tobias-Projects-sublimite/3dfb7436-2086-45df-b788-60ade1f4789a/scratchpad/cxho10/shot/strips.png`: the four minimap strips side by side, cropped and scaled 2x.

What they show:
- **20k file.** The box has the same height (one screenful of rows) at the top, the middle and the end. The miniature scrolls under it.
- **End (line 20001, wrapped).** The box sits at the strip's bottom, over the last lines and the scroll-past-end space.
- **50-line file** (fewer lines than rows). The box covers the whole document, and the miniature stops after line 50. No ink below it.

Each launch had a fresh HOME. The first shot run shared one HOME, so session restore mixed tabs; those images were discarded.

## What remains

- **File-backed huge file while indexing.** The map refresh stays held (`map_hold_unindexed`, edit-cxho.9). During a drag the box follows the pointer, but the miniature rows keep the held window's geometry until the index completes. The exact window then replaces them.
- **Wrapped async line target.** A target outside the summary rows goes through `e->wheel`, whose `scroll_state` clamps the top to N - rows. The wrapped bottom stretch (N - rows .. N - 1) is reached only through the summary rows. Those rows cover the strip's bottom, so dragging to the bottom lands exactly, as the test shows.
- **Wheel over the strip and the Sublime config keys** belong to edit-cxho.11.

## Found outside scope (not fixed)

- **edit-cxho.8 note on scroll past end.** `docs/decisions/edit-cxho.8.md` says "the editor lets first_line reach N - 1 (scroll.c clamps to extent.lines - 1)". That holds only for wrapped views. Unwrapped, `scroll.c max_top` is lines - rows. The `minimap.h` comment "Scroll past end: pass lines + visible - 1" is correct only for the wrapped case.
- **Compose still calls `minimap_fill_cached` every frame.** It fills `e->map_grid` cells that nothing displays any more: paint.c blanks the strip's cells, and the pixel region no longer reads `map_grid`. Removing it saves per-frame work on the compose path, but `minimap_fill_cached` also maintains `m->stale` and `painted_*`, so the removal needs care.
- **Unused minimap API.** `minimap_hit`, `minimap_hit_approx` and `minimap_pending_band` are no longer used by the editor. They are still covered by `minimap_test`.
