# edit-b6ai.15: UI density under XWayland

Problem (Tobias, 2026-11-03, monolith, Hyprland, editor as an XWayland client):
no `Xft.dpi`, so `x11_posix.c` used the RandR output's physical mm. DP-9
3840x2160 reports 1860x1040 mm: hypot(3840,2160)*25.4/hypot(1860,1040) = 52.5
dpi (P), 10 pt = 10*52.5/72 = 7.3 px (P). Target 13 px = round(10*96/72) at
scale 1 (96 dpi; Omarchy terminals use 9 pt at 96 dpi = 12 px).

## Rule

`editor_runtime_dpi_x11(xft, xwayland, scale, w, h, mm_w, mm_h)`:
1. `Xft.dpi` > 0: that (unchanged, any server).
2. XWayland: 96 * scale.
3. Otherwise `editor_runtime_dpi` (physical mm, else 96): unchanged, so the
   ThinkPad (real X11, 243 dpi) and Xvfb are bit-identical. Windows has its own
   path and is untouched.

## Detection signal: the `XWAYLAND` extension

Measured with `xcb_query_extension` (read-only): present=1 on DISPLAY=:0,
present=0 on Xvfb :99. Rejected: RandR output names. Old XWayland named outputs
`XWAYLAND0`, but the current server names them after the compositor's outputs
(`DP-9`, `HDMI-A-1` here), so a name match would silently fail. Other signals
(screen mm, no `Xft.dpi`) are not specific.

## Scale source: assume 1, `EDIT_DISPLAY_SCALE` override

An X client cannot see the compositor scale. With Hyprland
`xwayland:force_zero_scaling=true` the X screen is in physical pixels and the
client must scale itself; without it XWayland scales the client bitmap and 96 is
right. The X protocol does not say which. Options considered:
- Read `wl_output.scale` through the Wayland socket: adds a Wayland connection
  to the X11 path; the value is an integer (a 1.5 monitor reports 2), so it is
  wrong exactly in the fractional case, and wrong under the unscaled mode.
- Config key (`font.dpi`/`display.scale`): config is loaded after the display
  query (`main.c` queries first, `editor_open` loads preferences), so it needs
  plumbing for a startup-only value. `font.size` in config.toml already scales
  text by points, which is the config-level escape hatch.
- Chosen: scale 1 by default (his box: `hyprctl monitors -j` scale 1, 13 px for
  10 pt), plus `EDIT_DISPLAY_SCALE` (sanitized by `editor_runtime_scale`: finite,
  positive, 96*scale finite, else 1) for a later fractional scale. Proper
  fractional scale is edit-b6ai.7 (native Wayland).
`GDK_SCALE` is deliberately not read (it is 2 on his box and would double the size).

## Native Wayland

`src/wayland/output.c` already uses 96 * `wl_output.scale` (no physical mm), so
it follows the same convention; no change.

## Typing path

DPI is computed once at startup by `plat_display_size`; nothing on the typing
path changed.
