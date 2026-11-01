# edit-177r.32 — resize and minimap, session 28

Implementation and conflict-content resolution are complete; Windows behaviour
and the next-refresh acceptance remain **not run / unverified**. This report
supersedes the provisional report from the interrupted attempt. The revised
brief prohibits every Windows executable, including hidden runs. This
continuation used only Linux execution and cross-compilation, with CPU affinity
32–47, nice 10, and make -j16. No state-changing git command was run. The rebase
index still shows the editor_win_test.c conflict until the coordinator stages
its resolved working-file content and continues the rebase.

## Changes

- D3D flip chain uses SCALING_NONE; DXGI uncovered colour, GDI erase and cell
  margins use the theme base. Initial colour crosses to init by value.
- WM_SIZE applies geometry and paint synchronously at a safe callback boundary.
  It supersedes old snapshots, bypasses old latency credit and uses interval-zero
  DO_NOT_WAIT Present for the replacement. Late completions cannot release it.
- Resize paint reuses layout/compose/submit/present directly. It skips unrelated
  editor maintenance and defers window metadata. Layout/Present continuations
  use a coalesced native message rather than the modal timer. Actual text,
  first-paint, save/clipboard and borrowed-layout fences remain. OOM/device
  rebuild is deferred past the native callback.
- Removed redundant full clears of reserved targets. Existing reservations,
  immutable CPU snapshots and the typing allocation policy remain.
- Preserved both sides of the conflict: WSL/provider, close/publication, font
  and direct-notification cases from HEAD, plus resize/oracle cases. The real
  SC_SIZE helper is gated with LIVE_ONLY_NATIVE_LOOP; hidden coverage sends
  programmatic size and direct entry/exit notifications. Added --resize-live
  to the existing coordinator-only live selector.

Files: src/d3d11/{d3d11.h,d3d11_win.c}, src/win32/{private.h,win32_plat.c},
src/editor/{private.h,editor.c,open.c,paint.c}, mk/win.mk,
tests/win/{d3d11_test.c,d3d11_win_test.c,editor_win_test.c,editor_resize_cases.h},
tests/editor_test.c, decision and live-check documents.

## Resize deadline and remaining work in the path

Acceptance: "it should be visually instantaneous". Next refresh is
(P) 1000 / 165 = 6.060606 ms, from Tobias's display refresh rate. There is no
measured final-tree Windows latency result. The decision document enumerates
every remaining stage and fence. The avoidable active-frame wait, old latency
credit, sync-interval-one delay, deferred SIZE turn, modal-timer continuation,
unrelated service phases, window-metadata persistence and reserved-target
clears have been removed from resize's critical path. Native buffer ordering,
visible layout, necessary protection, actual draw, DXGI/GPU availability, DWM
and refresh remain. Visible timing and absence of flashes require his authorized
hardware check; cross-compilation cannot certify them.

`editor_win_test --resize-profile` is ready for the coordinator's hidden run.
It prints the following stage table, all (M) QPC end minus start, in ns, stamped
[AC, desktop Spiral], box in use. Geometry subtracts timed native calls.
Capture `uptime` beside the eventual run. No values are invented here.

| Stage | Final-tree Windows result | Derivation in ready fixture |
|---|---|---|
| ResizeBuffers | not run | timed native vtable call |
| GetBuffer | not run | timed native vtable call |
| Target view recreation | not run | timed CreateRenderTargetView call counter; views are retained by design |
| Editor geometry/layout setup | not run | editor_resize_now/full_layout minus native intervals |
| Row/atlas preparation | not run | layout slices and paint preparation |
| Composition/minimap | not run | editor_compose interval |
| CPU snapshot | not run | dirty strips and render_backend_submit interval |
| Draw enqueue | not run | actual d3d11_draw interval |
| Present | not run | Present1 interval and actual HRESULT |

The fixture also prints SetWindowPos/drawn-ready intervals before readback and a
back-to-back control reinserting the removed clears. Diagnostic readback waits
are separate. Hidden WARP is not his RTX 3090: software draw/driver timings,
occluded Present costs, and diagnostic Map waits do not transfer to visible
GPU/DWM latency. No final performance speedup is claimed without that run.

## Minimap result

**No defect established; no minimap implementation change.** Reading open.c,
paint.c and D3D/GL/raster shows that enlarged reservations remain capacity;
current region width, height and compact stride drive composition and copying.
The fixed-scale Linux band is visible rows clipped to document lines, multiplied
by MINIMAP_LINE_PX and clipped to strip height. Total lines set scroll range,
not a whole-document image stretch.

New Linux --resize-minimap loads src/minimap/minimap.c, the same text used by
the Windows fixture. It checks editor pixels against an independent minimap
composition and the actual Linux raster-region kernel. Sanitizer run exited 0.
Measured geometry below (M), from that run; band derivation is visible * 2,
where 2 is MINIMAP_LINE_PX (G: existing layout policy), total lines = 705 from
this source. Font metrics are platform-specific; Windows metrics are not run.

| Client fixture (P: brief display and smaller cases) | Linux map (M) | Visible rows (M) | Band px (M; visible * 2) |
|---|---|---:|---:|
| 3072×1008 | 64×975 | 65 | 130 |
| 5120×1440 | 64×1410 | 94 | 188 |
| 2561×721 | 64×690 | 46 | 92 |
| 5119×1439 | 64×1395 | 93 | 186 |

Fixture derivation (P): full client is the brief's display; startup is
floor(5120 * 60/100) by floor(1440 * 70/100); shrink is full/2 + 1 in each
dimension; final grow is full - 1 in each dimension. Linux map width is
8 map columns * 8 px cell width; heights are visible rows * 15 px cell height
(M: current atlas metrics, also recoverable from the table).

Windows --minimap-size has startup/full/shrink/grow/maximize/144-DPI/short/empty
readback oracles ready, comparing D3D with the editor-composed region passed
through the Linux raster kernel, and checking band height independently.
All Windows pixel/oracle behaviour is not run. If Tobias still reports the
size issue, the coordinator needs a screenshot specifying band versus image,
file, zoom/DPI and window size.

## Red/green and verification

Source guard red (exit 1):
`taskset -c 32-47 nice -n 10 python3 tools/check_win_pointer.py`
reported `tests/win/editor_resize_cases.h:251: SC_SIZE requires LIVE_ONLY_NATIVE_LOOP
as the function's first statement`. After gating the real loop and adding
programmatic hidden coverage, the same command exited 0, `Windows pointer guard:
PASS`. Its Python regression suite also exited 0 (M: 8 tests from unittest output).

Windows runtime red/green: **not run**, prohibited by revised brief. Earlier
attempt logs are not verification of this rebased/final implementation.

| Check | Exit/status |
|---|---|
| taskset -c 32-47 nice -n 10 make -j16 all | 0 |
| DEBUGINFOD_URLS= make -j16 check with the same affinity/priority | 0; 117 binaries / 121 tasks (M: runner output; editor split into parts) |
| Linux sanitizer editor_test --resize-minimap | 0 |
| Required Windows rel/san cross-compilation and release app | 0 |
| python3 tools/check_win_pointer.py with the same affinity/priority | 0 |
| python3 tests/win_pointer_guard_test.py with the same affinity/priority | 0 |
| editor_win_test, d3d11_test, d3d11_win_test, win32_plat_test runtime, rel/san | not run |
| Hidden Windows profile/minimap/resize/OOM oracles | not run |
| Live pointer loop, visual transitions, RTX next-refresh acceptance | not run |

Cross-build command: `make TARGET=win -j16` with targets
`build/win/{rel,san}/tests/{d3d11_test,d3d11_win_test,editor_win_test,win32_plat_test}.exe`
and `build/win/rel/sublimite.exe`, under the same affinity/priority. Explicit
targets only; no Windows check target or executable was invoked.

Logs: /tmp/s28-r2-linux-all.log, /tmp/s28-r2-linux-check-final.log,
/tmp/s28-r2-linux-minimap.log, /tmp/s28-r2-win-cross-final.log.
The first headless sanitizer attempt in the sandbox exited 1 because LSan
cannot operate under ptrace; the authorized non-sandbox Linux run passed.
Automatic review initially rejected the final Linux check as potentially
Windows-related. Inspecting make -n and the conditional Makefile branch proved
that this command selects only Linux binaries; test_sublimite_win.sh exports a
POSIX stub, so it executes no Windows program. The same check was approved on
that evidence and completed with exit 0. The final log includes the new
minimap comparison in editor_test.part-3 and the display-lock contract PASS;
the contract's expected contention probes print timeout lines without failing.

## Found, not fixed / coordinator decisions

No unrelated code changes. Windows behaviour remains unverified by instruction.
The coordinator must stage the resolved rebase content, continue the rebase,
and arrange the hidden and live checks with Tobias's agreement. A screenshot is
needed to identify a minimap defect if the Linux/reference comparison does not
explain his observation. docs/testing/windows-check-live.md records the visible
cases and states that private stations do not isolate the real pointer.
