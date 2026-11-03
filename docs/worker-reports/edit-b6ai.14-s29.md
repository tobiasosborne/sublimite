# edit-b6ai.14 (s29): GL under XWayland on monolith's NVIDIA driver

Stamp: [AC, desktop monolith] (no battery; `/sys/class/power_supply/BAT0` absent).
Driver 580.178.04 (nvidia-580xx-utils), egl-x11 1.0.6, egl-wayland 1.1.24,
libglvnd 1.7.0, Mesa 26.2.5, Xwayland 24.1.14, 2x GTX TITAN X.

## Root cause

One line in `gl_context_init` (src/gl/renderer.inc):
`(!wayland && (vendor==NULL || strstr(vendor,"Mesa")==NULL)) goto fail;` with
`rc=RENDER_ERR_UNSUPPORTED` (-10). On this box glvnd picks the NVIDIA vendor
(`10_nvidia.json` before `50_mesa.json`), whose `EGL_VENDOR` is `NVIDIA`, so a
fully working display/config/context/surface/Present path was rejected after
`eglInitialize`. Nothing else fails: DRI3 and Present are present on Xwayland,
`EGL_EXT_platform_xcb` is offered (via egl-x11's `libnvidia-egl-xcb.so.1`).

Evidence: `build/probe/eglprobe.c` (standalone, replays every step of
`gl_context_init` on the editor's 32-bit ARGB visual, then 6 swaps reading
PIXMAP CompleteNotify on the same connection), DISPLAY=:0, no input:

```
X ext DRI3: yes / Present: yes / DRI2: no
window visual=0x98 depth=32 (root visual 0x68)
EGL 1.5 vendor=NVIDIA version=1.5
has create_context=1 surfaceless=1
configs: 20   (all 20 match visual 0x98)
context, shared upload context, surface: ok; swapinterval0=1
GL_RENDERER=NVIDIA GeForce GTX TITAN X/PCIe/SSE2 GL_VERSION=3.3.0 NVIDIA 580.178.04
swap 1  complete kind=PIXMAP mode=3 serial=1 msc=1
swap 2  complete kind=PIXMAP mode=1 serial=2 msc=1
swap 3  complete kind=PIXMAP mode=1 serial=3 msc=2
swap 4..6 serial=4,5,6 mode=1 (Flip)
```

So NVIDIA's egl-x11 follows the Mesa convention the renderer relies on
(serial = low 32 bits of SBC from 1; mode SuboptimalCopy/Flip, both accepted by
`gl_display_mode`). `strace -f -e trace=connect` on the probe shows a single X
connection: egl-x11 sends PresentPixmap on the connection we pass, so the
edit-848 stall round trip still orders after the swap.

Side fact: forcing Mesa (`__EGL_VENDOR_LIBRARY_FILENAMES=.../50_mesa.json`) gives
`llvmpipe` (no Mesa driver for the NVIDIA PCI id 10de:17c2) and **no** PIXMAP
CompleteNotify at all within 1500 ms per swap (software swaps do not go through
Present here). Mesa is not an alternative on this box.

Kernel `nvidia-drm ... Failed to import NVKMS memory to GEM object`: 19 lines this
boot at 13:50:17, 14:16:53-54, 14:33:18, 14:58:40, 15:13:08; none during any of
this bead's GL runs (15:18-15:24). Unrelated to the editor.

## What changed

- `src/gl/renderer.inc`: new `static bool gl_x11_vendor_supported(const char *vendor)`
  (Mesa substring, unchanged; or exactly `NVIDIA`); the vendor check calls it.
  Header comments updated. No other behaviour change: Wayland has no vendor
  check; Mesa/Intel on the ThinkPad passes as before; Xvfb (no DRI3) is still
  refused before the loader.
- `tests/gl_test.c`: `gl_review_vendor_test` (Mesa Project, Mesa, NVIDIA accepted;
  NULL, "", "NVIDIA Corporation", AMD refused), run in both the native and the
  pure branch of `main`.
- `docs/decisions/edit-b6ai.14.md`: why an allow-list and not dropping the gate.
- `tests/x11_xvfb.h`: copied from the coordinator's fix in the main tree (not part of this bead).

## Red / green

Red (predicate extracted with the old Mesa-only body, test added), `DISPLAY=:99 build/tests/gl_test`:
```
gl_test:797: FAIL gl_x11_vendor_supported("NVIDIA")
gl_test:1698: FAIL gl_review_vendor_test()==0
```
Green (NVIDIA added):
```
gl_review: vendor PASS (Mesa and NVIDIA accepted on X11; unknown refused)
gl_test: PASS (snapshot law 2, failure cleanup, frozen grid/adapter checks; native lifecycle SKIP)
```

## GL init trace on the XWayland server, before / after

Startup only, no input: `timeout -s TERM 4 build/sublimite --new-instance`, throwaway
XDG_DATA_HOME/XDG_CONFIG_HOME/XDG_RUNTIME_DIR under build/probe/, `EDIT_TRACE_DUMP`,
decoded with `build/tools/tracedump` and `build/probe/evlist` (lists every record).
"After" ran with `DISPLAY=/tmp/.X11-unix/X0_` (same Xwayland server, see Found 1).
elapsed_ms = (stage_ns - process_start_ns)/1e6, (M), n=1 each.

| stage | before (ms) | after (ms) |
|---|---|---|
| display_connect | 83.6 | 53.3 |
| raster bootstrap submit (raster_fallback) | 114.9 | 65.8 |
| egl_probe_start | 399.4 | 142.1 |
| egl_load_start / end | 414.0 / 548.6 | 142.5 / 171.1 |
| egl_probe_end | 637.7 | 376.7 |
| stderr | `EGL init failed: EGL/GL/Present unavailable (code=-10); using raster` | nothing |

After the probe end the editor hands over to GL: frame 6 (the handoff full
layout) T3 377.8 -> T4 446.0 -> T6 447.4 ms. Caret-blink frames 7-13 after the
handoff (M, n=7): T3->T4 1.35..3.10 ms, T4->T6 0.19..1.48 ms. Before (raster,
frames 5-10, n=6): T3->T4 2.5..16.3 ms, T4->T6 1.8..14.4 ms. These are blink
frames only (small damage, default window size), not the drive's typing load.
Positive proof that GL presents after handoff: `strace -f -e ioctl` of the "after"
run shows 22-41 ioctls per second in the idle steady state on the DRM fds
(`DRM_IOCTL_SYNCOBJ_*`, egl-x11 explicit sync) at blink times; the same run with
`EDIT_BACKEND=raster` makes 12 ioctls in total.

G1/G3/T6 under typing: not measured. GL cannot run on Xvfb (no DRI3, refused by
design) and input on :0 is forbidden, so there is no GL typing number yet; the
drive's raster numbers (terra-drive-2026-11-03) are the baseline to beat when
Tobias uses it. gl_bench needs a GL display and was not run on :0.

## Native Wayland (edit-b6ai.5), not implemented

Not needed for this fix. Facts for that bead: the glvnd client extension string
offers `EGL_KHR_platform_wayland`/`EGL_EXT_platform_wayland`, and egl-wayland
1.1.24 (`10_nvidia_wayland.json`) is installed, so `gl_context_init`'s Wayland
branch (no vendor gate) would reach NVIDIA's EGL. T6 there comes from
wp_presentation feedback, not Present serials.

## Checks

- `make all`: rc=0
- `make TARGET=win all` (llvm-mingw): rc=0
- `make check`: see below

First run on the old `tests/x11_xvfb.h` (15:25-15:27): rc=0,
`check: 118 test binaries passed in 109.3 s (parallel, 36 jobs, 6 displays)`.
Final run after copying the coordinator's fixed `tests/x11_xvfb.h` (a coordinator
fix, not part of this bead): rc=0,
```
check: 118 test binaries passed in 110.4 s (parallel, 36 jobs, 6 displays)
test_replay_cli: all passed
```

## Found (not fixed)

1. **The real desktop's X socket `/tmp/.X11-unix/X0` was deleted at about
   15:20 today while :0's Xwayland still runs**, so new X11 clients on
   Tobias's desktop with DISPLAY=:0 fail (`connect` to `@/tmp/.X11-unix/X0`
   ECONNREFUSED, `/tmp/.X11-unix/X0` ENOENT). `ss -xlp`: Hyprland/Xwayland
   listen on `/tmp/.X11-unix/X0_` (fd 99) and on the now-unlinked
   `/tmp/.X11-unix/X0` (fd 100); Hyprland holds no abstract `@/tmp/.X11-unix/X0`.
   Timeline: my probes reached :0 at 15:18-15:19; the edit-b6ai.15 worktree's
   `make check` ran its x11 suites 15:20:16-15:20:27 (its logs show Xvfb
   `_XSERVTransMakeAllCOTSServerListeners: server already running` lines); my next
   :0 launch at 15:20:33 failed with EDITOR_ERR_IO. Suspected mechanism (E):
   `tests/x11_xvfb.h` starts `Xvfb -displayfd` (automatic number search from 0);
   with the abstract name for :0 free, Xtrans binds it and unlinks the existing
   path socket before binding, then the server gives up :0 (lock held by
   Hyprland, pid 1491) or exits, removing the path.
   At 15:26:59 `/tmp/.X11-unix/X0` reappeared as a hard link to `X0_`
   (link count 2, inode mtime 13:50:17). I did not make that link. The
   coordinator has since reproduced the mechanism and supplied a fixed
   `tests/x11_xvfb.h`. My first `make check` (old helper, 15:25-15:27) ran
   after the repair; its Xvfb took `:1` and X0 survived. The final run used the
   fixed helper; X0 survived. I did not need to restore X0 myself. Until it was
   repaired, I reached the same Xwayland with `DISPLAY=/tmp/.X11-unix/X0_`
   (libxcb accepts a socket path).
2. Mesa on a box without a Mesa driver for the GPU (llvmpipe under Xwayland)
   passes the vendor gate, and since startup init skips the three-swap proof it
   would only fail 3 s after the first GL frame (no PIXMAP Complete). Not hit on
   monolith (glvnd picks NVIDIA). Backlog.
