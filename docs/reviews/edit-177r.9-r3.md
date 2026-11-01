Static review of `HEAD`; no builds, tests, or application executables run.

- **BLOCKER — Typing queued behind a cut is lost on window close.** [src/editor/editor.c:1151](/home/tobiasosborne/Projects/sublimite/.wt/edit-177r.9-s28/src/editor/editor.c:1151), [src/editor/editor.c:1176](/home/tobiasosborne/Projects/sublimite/.wt/edit-177r.9-s28/src/editor/editor.c:1176). Start Ctrl+X, type `q` while publication is pending, then click window close. Clipboard busy holds `q` in the FIFO. Close bypasses that FIFO, finishes the cut, and flushes without dispatching `q`. On reopening, the deletion survives and `q` is absent. **Fix:** finish input accepted before the close request, including its continuations, before flushing; establish a close boundary for later input.

- **MAJOR — PAINT is cleared before the complete CPU submission survives.** [src/editor/editor.c:580](/home/tobiasosborne/Projects/sublimite/.wt/edit-177r.9-s28/src/editor/editor.c:580). The first `editor_compose` is guarded, but PAINT clears before status composition, render validation, repair, and D3D’s CPU glyph/pixel snapshot. A crash there leaves the offending recovered tab live, allowing the crash loop edit-dzk1 was intended to prevent. Also, `win_composed` suppresses subsequent composition guards even if glyph inputs change before the tab ever presents. **Fix:** retain protection through the entire first CPU submission; clear before latency waiting, and reconsider CPU safety when unpresented glyph/source inputs change.

  **Native draw protection remains:** readiness is retained before arming the submitted source tab’s PAINT, and the first native draw runs inside that guard. Linux’s guard path is unchanged.

- **MAJOR — A cloaked window can poll indefinitely while idle.** [src/d3d11/d3d11_win.c:90](/home/tobiasosborne/Projects/sublimite/.wt/edit-177r.9-s28/src/d3d11/d3d11_win.c:90), [src/d3d11/d3d11_win.c:882](/home/tobiasosborne/Projects/sublimite/.wt/edit-177r.9-s28/src/d3d11/d3d11_win.c:882). Switch virtual desktops with a frame pending. The new cloak check pauses drawing and continually rebases its watchdog, but `d3d11_surface_unavailable` omits cloaking. The editor therefore retains an active, unparked frame and the polling cadence at [src/editor/editor.c:1050](/home/tobiasosborne/Projects/sublimite/.wt/edit-177r.9-s28/src/editor/editor.c:1050). Tobias gets sustained background wakeups until the window becomes drawable. Cloaked windows can retain `WS_VISIBLE`, so `IsWindowVisible` does not close this gap. [Microsoft’s explanation](https://devblogs.microsoft.com/oldnewthing/20200302-00/?p=103507/). **Fix:** propagate cloaking into surface availability and park the frame, retaining a bounded recovery obligation.

- **MAJOR — A stuck native publication has no application deadline or cancellation.** [src/editor/clip.c:674](/home/tobiasosborne/Projects/sublimite/.wt/edit-177r.9-s28/src/editor/clip.c:674), [src/editor/editor.c:1178](/home/tobiasosborne/Projects/sublimite/.wt/edit-177r.9-s28/src/editor/editor.c:1178). If publication never returns from foreign clipboard notification handling, close keeps waiting, ordinary keys remain blocked, and no progress/cancel notice appears. Repeated close does not escape; process termination does. Windows clipboard notifications can synchronously involve other applications. [Microsoft’s discussion](https://devblogs.microsoft.com/oldnewthing/20250508-00/?p=111162). **Fix:** bound the whole identified operation, preserve text on expiry, invalidate late deletion completion, and provide cancellation/exit using the existing detached-worker lifetime.

  A clipboard **merely held open** is bounded: `clip_open` stops at `start + GetDoubleClickTime()`, using the documented default when unavailable. Failure preserves the selection, displays “clipboard publication failed; text kept”, and cancels that close. Another close can then exit.

- **MINOR — Normal maximization can leave a wide blank strip despite fitting the surface reservation.** [src/editor/open.c:112](/home/tobiasosborne/Projects/sublimite/.wt/edit-177r.9-s28/src/editor/open.c:112), [src/editor/editor.c:682](/home/tobiasosborne/Projects/sublimite/.wt/edit-177r.9-s28/src/editor/editor.c:682). This is an existing grid limit exposed by the requested desktop check. With the documented display supplying a full-width work area and default embedded font:
  - Initial width: `5120 × 60 / 100 = 3072`.
  - Default em: `round(10 pt × 96 DPI / 72) = 13`.
  - Cell width: `round(1233 × 13 / 2048) = 8`, from the embedded font metrics.
  - Reserved columns: `max(360, 3072 / 8) = 384`.
  - Maximized grid width remains `384 × 8 = 3072`, leaving `5120 − 3072 = 2048` background pixels beside it.

  Tobias sees text, chrome and wrapping confined to the narrower grid. **Fix:** reserve grid columns from the maximum client width at startup.

Previous findings:

| Previous finding | Status |
|---|---|
| Cut deletion before publication | **Fixed.** Matching native success precedes deletion. |
| Full FIFO deadlocking clipboard completion | **Fixed.** Results bypass FIFO capacity; native pumping continues. |
| Close abandoning a partially executed cut | **Fixed for the cut itself.** Replacement, repair, undo and journal commit continue. Queued typing remains broken above. |
| Minimize / oversized restore terminating editor | **Fixed by retained geometry and actual HWND limiting.** |
| Parking recovery | **Partial.** Owed-frame retries exist; cloaking remains broken above. |
| Expired expose deadline causing idle spin | **Fixed.** Parking exit clears it; waits ignore it outside parking. |
| Parked PAINT false positives / deliberate discard | **Fixed for undrawn waits/discards.** CPU protection regressed as above. |
| Lifecycle-pool deadlock / descriptor race / missing mailbox | **Fixed.** Dedicated pool, exclusive retiring descriptor and explicit mailbox transfer. |
| Lost auto-reset readiness | **Fixed; unchanged this round.** |
| Profile identity / stdin attachment | **Fixed; unchanged this round.** |
| PowerShell synchronous invocation | **Remains:** no console launcher. |
| Weakened crash fixture | **Restored:** the executable fixture is non-minimized. |
| Clipboard-command heap allocation | **Remains.** No additional plain-typing allocation regression found. |
| Linux behaviour | **None found.** These production changes retain Windows guards. |

The retained slot introduces **no additional finding** for second cuts, intervening tab-close commands, ordinary typing order, or memory ordering. Those commands stay behind the pending operation; deletion and repair precede their dispatch. Slot access is UI-owned, and native results cross the worker boundary under the clipboard lock.

For the owed-frame probe, a **handled resume** requests full layout even when nothing was previously owed; rebuilding also creates an obligation. Under the stipulated **content loss with no delivered event and nothing owed**, there is no automatic detection or recovery bound; the next input/expose is the way out. Occlusion registration does not establish continued surface validity. Track that validity separately and retain a recovery obligation whenever it is uncertain. The parked cap is the documented engineering choice `3 s × 2⁴ = 48 s`; it bounds the delay until another attempt, not successful display.

The HWND clamp shrinks the actual client rectangle, so I found **no clamp-induced surface mismatch or self-sustaining `SetWindowPos` loop**: the nested size notification reaches the within-limit return. Size/DPI changes recheck it, but returning monitors never increase the startup reservation. Startup prepays the selected monitor’s `rcWork` dimensions ([src/main.c:270](/home/tobiasosborne/Projects/sublimite/.wt/edit-177r.9-s28/src/main.c:270)); on his screen that is at most the documented display dimensions, reduced by work-area exclusions. Normal maximization on that same monitor fits this surface bound. The grid limitation above remains.

**fit for his first live launch: no**