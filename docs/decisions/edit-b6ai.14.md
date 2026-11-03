# edit-b6ai.14: GL on XWayland with the NVIDIA proprietary driver

## Choice

The X11 EGL admission keeps a vendor allow-list and adds NVIDIA to it:
`gl_x11_vendor_supported(vendor)` in `src/gl/renderer.inc` accepts a vendor
string containing `Mesa` (unchanged) or exactly equal to `NVIDIA`. Every other
vendor is still refused on X11 and falls back to raster. Wayland is unchanged
(no vendor check).

## Why not drop the vendor gate

The gate exists because EGL has no portable Present serial API (P2.4-egl §6):
the renderer predicts `pending_serial = ++swap_serial` and only a PIXMAP
CompleteNotify with that serial on the worker's own connection acknowledges
a swap (T6). That is a convention of the EGL implementation, not a guarantee.
A vendor that issues PresentPixmap with other serials, or on another
connection, would pass startup admission (the startup path skips the
three-swap proof) and then latch a device failure 3 s after the first real
frame (`GL_COMPLETION_TIMEOUT_NS`), i.e. a visible stall before raster. So
a vendor is admitted only once its convention has been observed.

## Evidence for NVIDIA (M)[AC, desktop monolith], 2026-11-03

Driver 580.178.04, egl-x11 1.0.6 (`/usr/share/egl/egl_external_platform.d/20_nvidia_xcb.json`
-> `libnvidia-egl-xcb.so.1`), Xwayland 24.1.14, 2x GTX TITAN X. Probe
`build/probe/eglprobe.c` replays `gl_context_init` on the editor's 32-bit ARGB
visual and reads Present events on the same connection:

- `eglGetPlatformDisplay(EGL_PLATFORM_XCB_EXT)` + `eglInitialize`: EGL 1.5, vendor `NVIDIA`.
- 20 configs match visual 0x98; 3.3 core context, shared upload context,
  `EGL_KHR_surfaceless_context`, window surface, `eglSwapInterval(0)` all succeed;
  GL_RENDERER `NVIDIA GeForce GTX TITAN X/PCIe/SSE2`, GL_VERSION `3.3.0 NVIDIA 580.178.04`.
- Swaps 1..6 produced PIXMAP CompleteNotify with serial 1,2,3,4,5,6, nonzero MSC,
  mode SuboptimalCopy (first) then Flip: the Mesa convention.
- `strace -e connect`: one X connection only, so egl-x11 issues PresentPixmap on
  the connection we pass, and the edit-848 stall probe still orders after the swap.

Exact match on `NVIDIA` rather than a prefix: the observed string is exactly
`NVIDIA`; a different NVIDIA string would be a different implementation whose
convention has not been seen. An NVIDIA driver without egl-x11 does not offer
`EGL_EXT_platform_xcb` and is refused before the vendor check.
