# Windows native window operations — physical mouse cases

Only the coordinator runs `make TARGET=win check-live WIN_LIVE_OK=1`, after
Tobias explicitly authorizes use of his desktop. Run these cases with the real
editor as part of that live check. The quiet suites cannot prove them: their
SC_MOVE is keyboard-mode, their resize is programmatic, and hidden stations do
not receive physical mouse input or authenticate visible presents.

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
