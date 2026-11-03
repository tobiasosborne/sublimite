# edit-b6ai.6 — Wayland CPU raster fallback over wl_shm

Before this bead, `src/raster/raster.c` returned UNSUPPORTED on Wayland. GL needs `wp_presentation`, so Wayland had no renderer when EGL/GL or presentation-time was missing. The X11 raster backend (XShm + Present) is unchanged by this bead.

## Decisions

1. **One backend, two transports.** `render_cpu_backend` keeps the backend-owned snapshot, the MT4 `WORK_RASTER` strips, the inline caret path and the kernels. On a Wayland platform, `cpu_init` takes the wl_shm variant (`cpu_state.wl`).
   - Strips render into a private retained image, `pix`, exactly as on X11 (where `pix` is the XShm segment). `pix` is anonymous memory at the init reserve, `max_width x max_height`.
   - `present` (UI thread) copies this frame's damage from `pix` into a released wl_shm slot. It then attaches the slot, sends `wl_surface.damage_buffer` per damage rectangle, and commits.
   - No wl_* object, pool or buffer is touched by a worker. The init-time `wl_shm_create_pool` is a request with no events, issued before the UI adopts the backend.
2. **Two slots in one memfd pool, sized at init.** (P) `wl_buffer.release` lets the compositor keep the committed buffer as the surface's content until a later commit replaces it, and the client writes the other slot meanwhile. Hence `RASTER_WL_SLOTS = 2`.
   - The pool holds `2 x max_width x max_height x 4` bytes (stride `max_width x 4`). (P) The wire's int32 pool size, offset and stride bound the reserve; a larger reserve is UNSUPPORTED at init.
   - A slot's `wl_buffer` is created at its first use, and recreated only when the surface size changes. It is never created per frame, and only a released slot is recreated.
   - Slot choice: the released slot filled most recently.
     - If that slot was filled by the previous present (immediate-release compositors), it needs only this frame's damage.
     - If it was filled two presents ago (alternating), it also needs the previous frame's damage list, which is retained.
     - Otherwise (new or recreated slot), the whole surface is copied.
3. **Damage is exact, not the whole surface.** (P) One frame's rectangles are its strips (or at most the two inline caret cells), plus the region, plus at most one margin per axis. The list is allocated at init as `max_strips + 5` rectangles. Overflow is unreachable, but would collapse to whole-surface damage.
   - Margins are painted into `pix` only when they can have changed: first frame, grid resize, background change or surface size change. X11 instead fills them on the server every frame.
   - A frame with no damage still damages one unchanged pixel. A damage-free commit may never be repainted, and then its frame callback could wait forever.
   - With a `wl_surface` older than version 4 (no `damage_buffer`), the whole surface is damaged in surface coordinates.
4. **Buffer size and scale follow the platform.** Each frame's buffer is `plat.width x plat.height` (buffer pixels from the configure, times the scale wayland.c already applies), read at submit. It is clamped to the reserve and rounded down to a multiple of `wayland_buffer_scale()`, because a non-multiple is a `wl_surface.invalid_size` protocol error. `wl_surface_set_buffer_scale` stays where it was, in `wayland_plat_init`.
5. **Completion (T5/T6).**
   - **T5** = the copy into the slot is done and the commit is flushed. The CPU has finished the pixels, and there is no device fence to wait for. It is signalled inside `present` and held by the render adapter until present returns (render.h).
   - **T6 with `wp_presentation`** = the matching `presented` feedback. Its CLOCK_MONOTONIC time is used when the compositor's clock is CLOCK_MONOTONIC (wayland.c's existing conversion, clamped to now). MSC is the feedback sequence.
   - **T6 without `wp_presentation`** = the armed frame callback's `done`, stamped with its dispatch time (`trace_now_ns`). (P) The `done` argument is in milliseconds with no stated epoch, so it is not used. MSC is 0. `wayland_frame_arm` now arms the frame callback alone when presentation is absent, and `frame_done` then marks the frame displayed.
   - **Discarded feedback** retires the frame untimed: T6 is the time the discard was observed, MSC is 0, and `raster_metrics.present_kind = RASTER_PRESENT_DISCARDED`. GL treats a discard as a device error. A renderer that is itself the fallback must not die because one commit was superseded or hidden.
   - **Retirement waits for a released slot**, as X11 waits for the pixmap's IdleNotify. T6 keeps the presentation or `done` time; only the backend's `active` flag waits. `wl_buffer.release` calls `wayland_frame_notify`, which re-delivers `on_present_complete`, and the editor polls raster there and on every pump turn.
6. **Trace consequences of the fallback rule.**
   - Without `wp_presentation`, T6 is "the compositor's frame callback fired", which is not a scanout time. T6-T4 measures commit-to-done latency. That is an upper bound on presentation for compositors that send `done` after compositing, and an unknown offset for others.
   - `raster_last_present` returns MSC 0, so `editor_refresh_flush` takes its existing "absence of MSC" path. No refresh interval is measured from such frames, and none is invented.
   - Trace consumers that need a real display time must check `present_kind` (`RASTER_PRESENT_FRAME_CALLBACK` / `RASTER_PRESENT_DISCARDED`).
   - The `--require-presentation-fallback` acceptance probe and the `no-presentation` mock case exercise this rule.
7. **Selection.** Wayland GL still initializes synchronously (strict path). Its background-init handover is X11 Present-specific: `gl_finish_init` arms an XCB Present subscription. If GL refuses or fails, `open.c` replaces it once, synchronously and before the first frame, with the raster backend already reserved for `raster_fallback`. This records `backend_init_error`, the existing stderr diagnostic and `TRACE_STARTUP_RASTER_FALLBACK`. Missing `wp_presentation` makes GL refuse before any EGL call, so this path is deterministic. X11 keeps its raster bootstrap with the background GL probe, unchanged. `EDIT_BACKEND=raster` now works on Wayland too.
8. **Editor routing.** For the Wayland raster variant only, `platform_complete` (plat's `on_present_complete`) calls `raster_poll_completions`. It ends the platform turn when a frame retired, so waiting damage can submit. `raster_completion_fd` stays -1, because completions arrive on the platform connection and not on a private fd. `raster_platform_completion()` identifies the variant.

## Costs

- (P) Memory: `pix` plus two slots = `3 x max_width x max_height x 4` B. At the X1C panel (`perf/00-hardware-profile.md`: 2880x1800x4 B = 20.7 MB per frame) that is 62.2 MB. X11 raster needs one XShm segment plus the server pixmap.
- (E) Present copy, from the profile's measured single-thread memcpy of 18.8 GB/s (r+w counted, (M)[bat], X1C):
  - One damaged pixel row at width 2880 is 2880 x 4 = 11.5 KB, so 23 KB of traffic, or 1.2 us.
  - A typing frame of R damaged pixel rows costs about 1.2 us x R, doubled when the alternate slot also takes the previous frame's damage.
  - A full surface (first frame, resize) is 41.4 MB of traffic, or 2.2 ms.
  - These run after submit (T4 path), outside the law-2 bracket, and are not gated.
- Allocation: the typing bracket (decoded input through submit) is 0 in the mock fixture and the editor case (`tests/wayland_test.c`). libwayland allocates a closure for every request marshalled in present (attach, damage, commit, the frame callback and feedback proxies) and for every event read. The existing edit-1zm accounting already counts these outside the bracket: "native transport before decoded ingress; one-shot frame/proxy creation after submit". The fixture prints `post_submit_mallocs` per case.

## Not covered

- No real compositor in this sandbox. Real-compositor pixels, buffer release behaviour, frame-callback timing and fractional-scale outputs belong to edit-b6ai.5 (with Tobias).
- No completion timeout on Wayland. A hidden surface legitimately gets no frame callback, and the frame stays active, as Wayland GL does today. Input keeps mutating, and drawing resumes when the compositor calls back.
- Runtime GL failure after a successful init does not switch to raster (no retry, as on X11).
