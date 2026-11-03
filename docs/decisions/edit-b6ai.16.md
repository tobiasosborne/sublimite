# edit-b6ai.16 — a resized window uses its whole client area

Bug (terra drive 2026-11-03, `docs/reviews/terra-drive-2026-11-03.md`, "Resized 4K window leaves its right quarter unused"): on Xvfb :97 (3840x2160, -dpi 52) with `EDIT_FONT_SIZE_PT=18`, a window resized to 3840x2160 drew text, gutter, tab strip and minimap only up to x ~ 2870.

## Cause (found by the red test, not the suspect)

The surface reserve was right: `font.max_width/max_height` came from `config.max_window_*` (the work area). The **cell grid** reserve was the problem. `editor_open` sized `max_cols/max_rows` as max(360/300 floor, launch window cells). Only Windows also reserved `ceil(max client / startup cell)` (an `#if defined(_WIN32)` block). On X11/Wayland a window larger than 360 cells wide was clamped in `editor_resize_now` (`dims.cols > e->max_cols`).

(P) em = round(18 pt * 52 dpi / 72) = 13 px, giving an 8x15 px cell (M, window_size_test). The launch window is 3840*0.60 = 2304 px = 288 cols, so max_cols stayed at the 360 floor. 360 * 8 = 2880 px, which is the drive's ~2870 edge. Red run: `grid 360x144 covers 2880x2160 px`.

A second clamp sat behind the first. With the CPU (raster) renderer, `editor_resize_grid` cut rows to `RASTER_FRAME_CELL_LIMIT / cols` (edit-5o0's per-frame admission budget of 65536 cells). A full 4K grid is 480 * 144 = 69120 cells, so rows became 65536/480 = 136, and the bottom 2160 - 136*15 = 120 px stayed blank.

## Decisions

1. **Reservation covers the largest connected output, not the X screen.** For each axis, take the maximum over connected active RandR CRTCs (X11) or over wl_outputs (Wayland). The X screen on Tobias's box spans 3840+1920 = 5760 px. That only matters for a window stretched across two monitors, which is unusual: maximise and fullscreen are per-output. Reserving the screen would add (5760-3840)/5760 = 1/3 more columns that a normal window never uses. A window stretched beyond the largest output is clamped to it: content ends at that output's width, as before. The output size is not clipped to the EWMH work area, because a fullscreen window covers the bar too. The launch size (60%/70% of the work area of the primary or first output, edit-m3g) is unchanged.
   - X11: `x11_display_size` also scans every connected output's CRTC. With no RandR or no outputs it falls back to the root window size.
   - Wayland: `wayland.c` multiplies configure's logical size by the selected (highest) buffer scale. The reserve is therefore max over outputs of ceil(mode / output scale), times the selected scale. A unity-scale 4K output next to a scale-2 laptop panel reserves 3840*2 px wide, which is what such a buffer would really be.
   - Windows: unchanged. `win32_plat_max_client` already enumerates every monitor (`EnumDisplayMonitors`), and `win32_plat_limit_client` holds the client to it. Windows had no clamp. `plat_display_info.max_*` stays zero there and main.c keeps calling `win32_plat_max_client`.
2. **Startup reservation, not growth on resize.** The `ceil(max client / startup cell)` block in `editor_open` is now shared code, so X11 and Wayland get it too. Everything is still prepaid in the arena at INIT. Resize and paint allocate nothing: the malloc guard stays 0 across the 4K resize in window_size_test. Growing on a resize event would mean reallocating the arena that grids, hit maps and backend snapshots borrow pointers from. That is a redesign and it is not needed.
3. **Raster frame admission follows the reservation.** `render_config.max_frame_cells` (new) carries max(`RASTER_FRAME_CELL_LIMIT`, client_cols * client_rows) to the CPU backend. `raster_frame_cell_limit()` is the one helper used by `render_backend_check`, `cpu_submit` and `editor_resize_grid`. 65536 stays the floor, and no buffer is sized by it (only the per-frame descriptor scan). edit-5o0 set "init capacity does not enlarge frame budgets" to bound UI work. This bead loosens that to "the cells physically on the largest output at the startup cell are admitted". Zoom-out below the startup cell still clamps rows at that limit, as it did before at 65536.

## Costs

- (P) Editor arena per reserved cell: 3 hl_span (24 B each) + editor_hit (24 B) + 2 render_cell (20 B each) = 136 B. On a 4K output at the 8x15 cell, the reserve grows from 360 x 301 (floor) to 480 x 301 cells: 120 * 301 * 136 B = 4.9 MB. Backend snapshot storage grows by the same cell ratio (480/360) and is not separately counted here.
- (E) Raster admission scan, from edit-5o0's measured p50 of 193 us at 65536 cells, scaled linearly: 193 * 69120/65536 = 204 us for a full 4K grid at 8x15. This is a tracked number, not a gate (law 1).

## Not covered (left as they are)

- Zoom-out (Ctrl+-) on a window already full-size. `font_size.c` clamps `dims.cols` to `max_cols`, so a smaller cell than the startup cell can again leave a right or bottom margin. Covering it fully means reserving for the 1 px em floor (3840 x 2160 cells x 136 B ~ 1.1 GB), which is not practical. A bounded zoom-out headroom would need its own number.
- Outputs hot-plugged after startup are not re-queried. `output_remove` and RandR change events are not wired for this.
