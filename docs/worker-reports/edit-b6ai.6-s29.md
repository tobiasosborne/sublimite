# edit-b6ai.6 (s29) — Wayland CPU raster fallback via wl_shm

Outcome: done. The headless acceptance probe is green, the mock cases pass, `make all`, full `make check` and `make TARGET=win all` are green, and X11 behaviour is unchanged. Decision note: `docs/decisions/edit-b6ai.6.md`. Real-compositor validation is out of scope (edit-b6ai.5). No probe touched `:0` or the Wayland session; everything ran against the forked `wayland_mock.h` server and Xvfb `:99` through `make check`.

## What changed

- `src/raster/raster.c`, `raster.h`: the wl_shm variant of `render_cpu_backend`.
  - Strips render into a private retained image. One memfd pool holds two slots sized at the init reserve; a slot's `wl_buffer` is created on first use or surface resize only, and only while released.
  - Present copies this frame's damage (plus the previous frame's when the slot alternates) into the slot, then sends `damage_buffer` per rectangle, attach and commit.
  - Margins are repainted only when they can have changed.
  - T5 = the copy and commit are done. T6 = `wp_presentation` feedback, or the frame callback's `done` without it. A discard retires the frame untimed. Retirement waits for a released slot.
  - New: `raster_platform_completion()` and `RASTER_PRESENT_*` kinds.
  - Snapshot allocation is factored into `cpu_snapshot_init` (shared by both variants).
- `src/wayland/wayland.c`, `plat.h`:
  - wl_shm is bound and exposed as `wayland_shm()`; `wayland_buffer_scale()` returns the applied scale.
  - `wayland_frame_arm` works without wp_presentation (frame callback only, and its `done` completes the frame with MSC 0).
  - New: `wayland_frame_discarded()`, and `wayland_frame_notify()` (re-delivers `on_present_complete` on `wl_buffer.release`).
- `src/editor/open.c`, `backend.c`, `private.h`: if Wayland GL refuses or fails at its strict init, the reserved raster replaces it once, synchronously. This records `backend_init_error`, the existing stderr line and `TRACE_STARTUP_RASTER_FALLBACK`. The X11 branch is untouched.
- `src/editor/editor.c`: `platform_complete` routes the Wayland raster to `raster_poll_completions`, and ends the turn when a frame retired.
- `tests/wayland_mock.h`:
  - libwayland-server's real `wl_shm` (`wl_display_init_shm`).
  - A damage-driven screen that copies only damaged rectangles and then asserts that the whole buffer equals it. This catches stale copy-forward and missing damage.
  - Release-on-replace or immediate-release policy, re-attach-while-held assertion, held-destroy counter, `set_buffer_scale` tracking with an `invalid_size` check, a configurable first configure plus a resize configure after N commits, and an optional "present every frame".
- `tests/wayland_test.c`:
  - The probe now also arms, commits and gets ready without presentation.
  - Raster cases: no wl_shm (UNSUPPORTED), presentation-feedback, presentation-discard, no-presentation, immediate-release, scale-2, resize, resize-immediate-scale-2. Each compares every screen pixel with the scalar reference plus margin background after every frame, and counts distinct buffers and the typing-bracket allocations.
  - Editor case: GL requested, no wp_presentation, so the editor falls back to `cpu-raster`. Typing 100 keys, the `on_ingress` -> `on_submit` bracket counts 0 mallocs.
- `tests/raster_test.c`, `fuzz/raster_fuzz.c`: one rename each for the new exported symbol (both include raster.c).
- `src/wayland/STATUS.md`, `src/raster/STATUS.md` updated.

## Red runs

1. Before any change, `build/tests/wayland_test --require-presentation-fallback`:
   ```
   wayland_test: tests/wayland_test.c:72: presentation_fallback_test: Assertion `armed == PLAT_OK' failed.
   Aborted (core dumped)   exit=134
   ```
2. With the new tests plus the platform part, and the raster still refusing Wayland (stub):
   ```
   wayland_test: raster init returned -10
   wayland_test: tests/wayland_test.c:200: wlr_open: Assertion `rc == RENDER_OK' failed.   exit=134
   ```
   Saved as `build/wt-logs/red-probe.txt`.

## Green runs

The machine has no battery (`/sys/class/power_supply/BAT0` is absent), so these are [AC, desktop monolith], load1 8.1 at the start of check.

- `--require-presentation-fallback`: exit 0, all raster cases passed.
- Release `wayland_test`, guard active:
  ```
  raster presentation-feedback passed: frames=15 commits=15 buffers=2 damage_rects=25 releases=14 typing_mallocs=0 post_submit_mallocs=284 guard=active
  raster presentation-discard passed: ... buffers=2 ... typing_mallocs=0 post_submit_mallocs=284
  raster no-presentation passed: ... buffers=2 ... typing_mallocs=0 post_submit_mallocs=210
  raster immediate-release passed: ... buffers=1 releases=15 typing_mallocs=0 post_submit_mallocs=282
  raster scale-2 passed: ... buffers=2 ... typing_mallocs=0 post_submit_mallocs=284
  raster resize passed: frames=17 commits=17 buffers=4 ... typing_mallocs=0 post_submit_mallocs=254
  raster resize-immediate-scale-2 passed: frames=17 commits=17 buffers=2 ... typing_mallocs=0 post_submit_mallocs=330
  editor on Wayland without wp_presentation fell back to raster: startup_bracket=2 typing_mallocs=0 frames=305 commits=305 guard=active
  ```
  - (P) Buffers: two slots alternate under release-on-replace and one is reused under immediate release. After a resize each slot is recreated once. No buffer is created per frame.
  - (M) `post_submit_mallocs` is libwayland request/event closures in present and completion. This is outside the law-2 bracket under the edit-1zm accounting.
  - (M) `startup_bracket=2`: gdb shows both are `wl_display_read_events` transport closures. The bracket was opened by the mock's pre-first-frame key while startup was settling. The steady-state typing bracket is 0.
- `make all`: exit 0.
- `make check`, final tree: exit 0. `check: 118 test binaries passed in 84.3 s (parallel, 36 jobs, 6 displays)`, with no flaky suite. On the first full run, before a one-line damage-overflow hardening, `work_foreground_test` was FLAKY: it failed in parallel and passed alone. It is a work-pool test that this bead does not touch.
- `make TARGET=win all`: exit 0.
- Wayland-free compile (no `EDIT_HAVE_WAYLAND`) of raster.c, wayland.c, open.c, editor.c and backend.c: ok.
- X11 unchanged, release builds with the guard active:
  - `raster_test`: PASS (live XShm pixels, fence completion counted_allocations=0).
  - `editor_test`: `requested=gl actual=cpu-raster 10000 keys mallocs=0`, `raster 10000 keys mallocs=0`, `null 10000 keys mallocs=0`, all guard=active.
  - `wayland_gl_test`: passed.

## Measurements

No bench was run. The present copy cost in the decision note is (E), derived from the profile's (M)[bat] memcpy bandwidth and not measured here.

## Found (not fixed)

- `bench_evidence_tag()` prints `(M)[unknown]` on this desktop, which has no BAT0. The rules stamp it [AC, desktop monolith], so the tag should probably say AC when no battery exists.
- Every Wayland event read allocates a libwayland closure on the UI thread. If a key arrives while a frame is in flight, the completion events read inside that key's ingress-to-submit window are counted, as in the startup bracket above. This is native transport, outside the enforced bracket per edit-1zm. It is the same class as X11's inline-frame `xcb_poll_for_reply`. An exemption around `wl_display_read_events` in `wayland_plat_run_for` would make the count exact, but it is not in this bead.
- Wayland frames (GL and raster) have no completion timeout. A compositor that never sends frame done for a hidden surface keeps the frame active, and drawing resumes on the next callback.

## Missing / for edit-b6ai.5

Real compositor checks: actual pixels, release timing (whether a real compositor ever holds both slots), frame-callback cadence while hidden, fractional scale, and the EGL-fails-then-raster path with a real EGL.
