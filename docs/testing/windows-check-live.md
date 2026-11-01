# Windows native window operations — physical mouse cases

Only the coordinator runs `make TARGET=win check-live WIN_LIVE_OK=1`, after
Tobias explicitly authorizes use of his desktop. Run these cases with the real
editor as part of that live check. Private stations/desktops do not isolate the
system pointer: keyboard-mode native move/size can warp the real pointer even
when the window is hidden. Quiet suites skip native loops and use direct modal
notifications instead. They cannot prove real-loop survival, physical mouse
tracking or visible presentation.

The automated live selectors also run these cases, in release and sanitizer
builds (G: the configurations in `WIN_LIVE_TESTS` in `mk/win.mk`):

- `win32_plat_test`: `modal`, `move`, `menu`, `turn`, `turn-size`, `turn-menu`,
  `turn-close`, `turn-session`, `turn-quit`; native pointer loops, turn-stop
  survival and explicit close/session/quit termination. `packet`, native F12
  input, capture theft and `altcode` (native Alt release) also require the live
  desktop.
- `editor_win_test`: `--modal-turn`, `--modal-close`, `--modal-session`,
  `--modal-save`, `--modal-clip`, `--modal-draw`, `--resize-live`; real-loop survival while
  typing, work, resize, frame and journal progress, plus save/publication and
  close. The publication case substitutes clipboard APIs and never accesses
  the live clipboard. `--modal-clock` stays hidden-safe because it sends only
  entry/exit notifications; the physical long-hold check below is still needed.

Maximize, restore, minimize, close and caption double-click commands do not
enter the native move/size pointer loop; their command-path tests remain in
quiet checks. The physical cases below still need a person to exercise them.

- Drag the caption, hold it while editor work completes, then release elsewhere.
- Drag a maximized caption to restore, continue moving, and release.
- Drag the caption to the screen edges to Aero Snap; unsnap it by dragging.
- Double-click the caption to maximize and restore.
- Click maximize, restore, and minimize; restore from the taskbar and click close.
- Resize from every edge and corner. The text and exposed content must redraw
  throughout the drag, without black/stale content waiting for release.
- Hold a resize or move beyond the renderer's ordinary completion watchdog
  (G: existing D3D11_COMPLETION_NS), then release. No false recovery, freeze, or
  paint-crash recovery marker should result from the hold.
- Open Alt+Space, keep the menu open while work and durability progress, then
  dismiss with Escape. Exercise Move/Size and finish with Enter and Escape.
- During a move/resize, let an outstanding save/checkpoint finish and then close;
  accepted typing must survive the ordinary close/recovery boundary.

Record the visible result and any untested cases in the worker/coordinator
report. Printed command-path PASS lines do not certify physical mouse tracking
or visible D3D11 presentation.

For edit-177r.32, use real text on Spiral's ultrawide display and check both
the dark and light themes. These Windows cases are **not run** on the final
worker tree: the revised brief prohibits every Windows executable until the
coordinator arranges execution with Tobias.

- Drag every edge and corner rapidly, then slowly. Glyph size and position
  stay fixed during each size transition; the retained frame must never stretch.
- Grow the window into previously uncovered pixels. The exposed area and the
  fractional cell margins must use the theme base, without black or white flashes.
- Maximize, restore, and resize by less than a cell. The drawn surface must
  follow the client immediately, including while Windows tracks a size loop.
- Tobias's acceptance is "it should be visually instantaneous". Check the new
  frame at the next refresh (P: 1000 / 165 Hz = 6.060606 ms). Hidden WARP timing
  cannot certify that deadline on the RTX 3090.
- Compare the minimap image and viewport band at startup, after shrinking and
  growing, maximized, and after a DPI/zoom change. The band uses the Linux
  fixed-scale model (visible rows times MINIMAP_LINE_PX, clipped to the strip
  and document). If Tobias still sees the wrong size, capture the window and
  identify whether he means the band or the map image, with file and zoom.

The ready hidden fixtures are `editor_win_test --resize-sync`,
`--resize-native-fail`, `--minimap-size` and `--resize-profile`, in release and
sanitizer builds. They use programmatic size changes and direct modal
notifications; the real loop prints a live-only SKIP there. Their buffer,
exact-pixel, background and recovery assertions are **not run** in this worker
continuation. `--resize-profile` prints QPC stage timings; record `uptime` beside
the coordinator's eventual run. These checks cannot authenticate a visible DWM
transition or presentation latency on the RTX 3090.
