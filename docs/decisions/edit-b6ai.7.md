# edit-b6ai.7 — Wayland fractional scale: wp_fractional_scale_v1 + wp_viewporter

Before this bead the Wayland backend knew only the integer `wl_output.scale`. Omarchy's scale steps include 1.25 and 1.6 (`/usr/share/omarchy/bin/omarchy-hyprland-monitor-scaling`: `SCALES=(1 1.25 1.6 2 3 4)`). At those steps the compositor resampled an integer-scaled buffer, so text was blurry.

## Decisions

1. **Both protocols, or neither.** The fractional path is active only when the compositor advertises both `wp_fractional_scale_manager_v1` (staging, v1) and `wp_viewporter` (stable, v1). If either is missing, the integer path runs exactly as before: `set_buffer_scale(wl_output.scale)`, buffer = logical x integer scale, pointer = trunc(x) x scale, startup DPI = 96 x scale. `fractional_absent_test` checks both single-protocol subsets.
2. **Buffer, viewport and scale on the fractional path.**
   - The buffer is `round(logical x preferred_scale / 120)` device pixels on each axis. (P) The protocol rounds toplevel sizes halfway away from zero, which is `(logical x s + 60) / 120` for non-negative sizes. The result is clamped to [1, INT32_MAX].
   - `wp_viewport.set_destination` is the logical size, the toplevel configure's size.
   - The `wl_surface` buffer scale stays at its default of 1. `set_buffer_scale` is never sent. `wayland_buffer_scale()` returns 1, so the raster backend's divisibility rounding is the identity.
   - `p->width/height` stay in buffer pixels, so the raster backend (which reads them at submit) and GL (through `wl_egl_window_resize`) follow without changes to `src/raster` or `src/gl`.
3. **Startup values.**
   - The launch logical size is today's: the caller's reserve divided by the integer `wl_output.scale`. The window opens at the same on-screen size as before.
   - The scale starts at 1 (120/120) until the compositor sends `preferred_scale`. Hyprland, and wlroots in general, send it when the surface enters an output. That happens after the first buffer commit maps the window, so the first frame is drawn at scale 1, and the compositor resamples it once.
   - `wayland_display_size` reports `fractional = true` and `dpi = 96` (the scale-one floor) on such sessions, so main.c's startup font DPI is 96 there. The reason is the editor's cell reservation: `editor_open` sizes `max_cols x max_rows` from `max_window_width / startup cell` and never grows them. Starting at the integer ceiling (Hyprland reports `wl_output.scale = 2` for a 1.25 monitor) gives 192 dpi cells. After the switch to 1.25, the cells shrink to the 120 dpi size and need about 1.6 x the reserved columns, so the text area would end well short of the right edge. Starting at 96 dpi is safe because the cell size never decreases as DPI rises. For every scale >= 1, `cols = buffer_w / cell(96 s) <= max_window_width / cell(96)`, which is the reserved count; the buffer never exceeds `max_window_width`, which is at least the output's mode width.
   - Cost: on integer-scaled outputs of fractional-capable compositors, the first frame uses the 96 dpi font and is replaced once `preferred_scale` arrives. The scale-1 integer path and compositors without the protocols are unchanged.
4. **Scale change: the platform applies at dispatch, the editor at its frame boundary.**
   - `preferred_scale` (UI-thread dispatch) updates `scale120`, recomputes the buffer size, calls `wl_egl_window_resize`, and pushes `PLAT_EV_RESIZE`. A scale change always pushes RESIZE, even when the rounded size happens not to change, because the editor reads the DPI there. This is the same place and the same mechanism the configure path already used. Nothing here allocates.
   - The viewport destination does not change on a scale change (the logical size is unchanged), so a scale change touches no surface state except the buffer size. On a logical resize, `set_destination` is sent from the configure handler. It is double-buffered and lands with the next commit.
   - Editor (`nonkey_dispatch`, non-Windows): on RESIZE from the Wayland platform, if `wayland_dpi()` differs from `font.dpi`, it recomputes `default_px` and `requested_px` from the preserved points. This mirrors the existing Windows `win32_plat_dpi` block. The font worker builds the atlas off the UI thread, and `editor_font_poll` adopts it only when the backend is idle and nothing is dirty. `resize_pending` likewise applies only when the backend is idle. Both changes therefore happen between frames, never inside input -> mutation -> layout -> submit (law 2). `editor_fractional_test` types 100 settled keys across a 1.25 -> 1.6 change with the release allocator guard active: typing_mallocs=0.
   - Transient: between the RESIZE and the atlas adoption, one frame can show the old cell size in the new buffer. The compositor then scales it to the unchanged destination, so the frame is correct in layout and the wrong size for one frame. This is inherent to the asynchronous font load, and it is the same shape as a Windows DPI change.
   - Legacy (explicit-pixel, `font_dpi == 0`) editors do not follow the DPI, as on X11. This includes the inherited-`WAYLAND_SOCKET` launch, where main skips the preliminary query (an existing STATUS item).
5. **Pointer.** On the fractional path a surface-local `wl_fixed` maps to the buffer pixel containing it: `floor(v x scale120 / (256 x 120))`, with 256 = wl_fixed's units per logical px. The integer path keeps truncate-then-multiply.
6. **`wayland_dpi()` on the integer path** is `96 x wl_output.scale` (the startup output's), even when an old compositor lacks `set_buffer_scale`. It therefore equals main.c's startup DPI, and the new editor block never fires there.

## Not done (see the worker report's Found)

- Surface-enter output selection on the integer path. It needs runtime `wl_output` tracking on the main registry, `wl_surface.enter/leave`, a runtime `set_buffer_scale` with a matching buffer resize, and a DPI change. That is a separate change, not a small one.
- Scales below 1 (`preferred_scale < 120`) shrink cells below the 96 dpi reservation, so text columns clamp at the reserved count. This is the same limitation that font zoom-out already has.

## Real-compositor checks (edit-b6ai.5)

Run with `WAYLAND_DEBUG=1` on Hyprland at each Omarchy step (`hyprctl keyword monitor ,preferred,auto,1.25`, then 1.6, 2, 1):
- The client binds `wp_fractional_scale_manager_v1` and `wp_viewporter`. The log shows `wp_fractional_scale_v1.preferred_scale(150)` (or 192), then `wp_viewport.set_destination(<logical>)` and buffers of `round(logical x s)`. No `set_buffer_scale` is sent, and there are no protocol errors.
- Text is crisp at 1.25 and 1.6 (compare against a native client such as foot). Expect only the first frame at scale 1, and a font change within a frame or two of mapping.
- The text area fills the window width after the switch, with no right-hand margin from cell clamping, maximized on the 4K output at 1.25.
- Pointer hit-testing: clicking on a character places the caret there at 1.25 and 1.6, including near the right and bottom edges.
- GL path and raster fallback (`EDIT_BACKEND=raster`, or a session without wp_presentation) both follow a live `hyprctl` scale change while typing, without a stuck stretched frame.
- Moving the window between outputs of different scale re-selects the font: only if the compositor sends a new `preferred_scale` on enter.
- Integer-only compositor (for example, without the protocols): behaviour unchanged.
