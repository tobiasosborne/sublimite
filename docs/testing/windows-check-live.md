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
  `--modal-save`, `--modal-clip`, `--modal-draw`; real-loop survival while
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
