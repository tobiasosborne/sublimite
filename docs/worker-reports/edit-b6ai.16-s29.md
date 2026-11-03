# edit-b6ai.16 (s29) — resized 4K window leaves its right quarter unused

Box: monolith, [AC, desktop monolith] (no BAT0). Load average 24 during make check (other workers).

## Cause

The pixel surface was reserved correctly. The cell grid was not. `editor_open` reserved `max_cols/max_rows` as max(360/300 floor, launch cells). The `ceil(max client / startup cell)` reservation existed only under `#if defined(_WIN32)`. On X11 and Wayland, `editor_resize_now` clamped the grid to 360 columns. At 18 pt / 52 dpi the cell is 8x15, so the grid ended at 360 * 8 = 2880 px, which matches the drive's ~2870.

A second clamp would have shown up next, on raster only. `editor_resize_grid` held a CPU frame to `RASTER_FRAME_CELL_LIMIT` = 65536 cells. A full 4K grid is 480 * 144 = 69120 cells, so rows would drop to 136 and the bottom 120 px would stay blank.

The reservation also came from one output (primary or first), so a larger second monitor was not covered. That part is fixed too. Details and the X-screen vs largest-output decision are in `docs/decisions/edit-b6ai.16.md`.

## Changes

- `src/editor/open.c`: the max-client cell reservation is now shared code. It also sets `e->frame_cell_limit` = max(65536, client cells) and passes it as `render_config.max_frame_cells`.
- `src/editor/editor.c`, `src/editor/private.h`: the raster row clamp uses `e->frame_cell_limit`.
- `src/render/render.h`, `src/raster/raster.h`, `src/render/render.c`, `src/raster/raster.c`: new `max_frame_cells` field and `raster_frame_cell_limit()`, with 65536 as the floor.
- `src/x11/plat.h`: `plat_display_info.max_width/max_height`.
- `src/x11/x11_posix.c`: per-axis maximum over connected RandR CRTCs, falling back to the root size. The Wayland mapping is also here.
- `src/wayland/plat.h`, `src/wayland/output.c`: max over outputs of ceil(mode / output scale), times the selected buffer scale.
- `src/main.c`: `max_window_*` = max(largest output, work area). The launch size is unchanged.
- `src/win32/win32_plat.c`: initializer only (`0,0`). Windows already enumerates every monitor through `win32_plat_max_client`, so it had no clamp.
- Tests:
  - `tests/window_size_test.c`: new `large_low_dpi_window`, run on the GPU path and the raster path.
  - `tests/wayland_mock.h`, `tests/wayland_test.c`: optional second, unity-scale output.
  - `tests/x11_font_size_test.c`: asserts and prints the reserve.

## Red

```
window_size_test:170: FAIL after.cols == capture.width / after.cell_w
window_size_test: gpu 3840x2160 at 52 dpi/18 pt: cell 8x15 grid 360x144 covers 2880x2160 px
```

After the shared reservation, the raster path was still red:

```
window_size_test:171: FAIL after.rows == capture.height / after.cell_h
window_size_test: raster 3840x2160 at 52 dpi/18 pt: cell 8x15 grid 480x136 covers 3840x2040 px
```

Wayland, with the old output loop and the new fields zeroed:

```
wayland_test.c:148: Assertion `info.max_width == info.width && info.max_height == info.height' failed.
```

## Green

```
window_size_test: gpu 3840x2160 at 52 dpi/18 pt: cell 8x15 grid 480x144 covers 3840x2160 px
window_size_test: raster 3840x2160 at 52 dpi/18 pt: cell 8x15 grid 480x144 covers 3840x2160 px
```

The malloc guard reads 0 across the resize and settle.

- wayland_test passes. The two-output case reserves 3*2 x 5*2 buffer px.
- x11_font_size_test on :97: `reserve 3840x2160`.
- `make all`: green.
- `make TARGET=win all`: green.
- `make check`: `check: 118 test binaries passed in 120.7 s (parallel, 36 jobs, 6 displays)`, exit 0. editor_test, which includes G1 and the mallocs=0 typing checks, passed.
  - scroll_test failed once in parallel (`scroll_test:749 scroll_visual_resolve_slice`) and the runner marked it FLAKY. It passed on the runner's solo rerun and on 3 more solo runs of mine. It is unrelated to this change.

## Manual check (:97, raster fallback since Xvfb has no EGL)

I ran `EDIT_FONT_SIZE_PT=18 build/sublimite readme.md` with throwaway XDG dirs under `build/b6ai16/`. The window opened at 2304x1512. I resized it with `xdotool windowsize 3840 2160` and captured it with `xwd -root`.

The minimap sits at the right edge (x ~3770-3840) and text and chrome span the full width. Screenshots:

- `/home/tobias/sublimite/.wt/edit-b6ai.16-s29/build/b6ai16/shots/full.png`
- `full-small.png` and `right-edge.png` in the same directory

## Measurements

- (P) The arena grows by 136 B per reserved cell. On a 4K output at an 8x15 cell that is +120 * 301 cells = 4.9 MB.
- (E) The raster admission scan for a full 4K grid is about 204 us. This scales edit-5o0's measured 193 us p50 at 65536 cells by 69120/65536. I ran no new bench.

## Found (not fixed)

- Zoom-out (Ctrl+-) below the startup cell still clamps to `max_cols/max_rows`, so a right or bottom margin returns on a full-size window. See the decision note for why full coverage is impractical.
- Outputs hot-plugged after startup are not re-queried (RandR change events, Wayland `global_remove`).
- `/tmp/.X11-unix/X0` was present throughout and was not touched.
