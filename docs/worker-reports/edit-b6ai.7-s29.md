# edit-b6ai.7 (session 29) — Wayland fractional scale: wp_fractional_scale_v1 + wp_viewporter

Outcome: done. When the compositor advertises both protocols, the surface renders at round(logical x preferred_scale/120) device pixels, the viewport destination is the logical size, the buffer scale stays 1, and fonts use 96 x scale dpi. A preferred-scale change resizes the buffers and re-selects the font at the editor's frame boundary. Without both protocols, the integer path is unchanged. Decisions: [docs/decisions/edit-b6ai.7.md](../decisions/edit-b6ai.7.md).

## What changed

- `src/wayland/protocol/{viewporter,fractional-scale-v1}{-client.h,-server.h,.inc}` (new): generated with wayland-scanner 1.26.0 from `/usr/share/wayland-protocols/{stable/viewporter/viewporter.xml,staging/fractional-scale/fractional-scale-v1.xml}`, with the same layout as the existing glue. Both are MIT; the notices are retained. `protocol.c` compiles both `.inc` files, and `protocol/README.md` records the commands.
- `src/wayland/plat.h`: new `wayland_scale120()`, `wayland_fractional()` and `wayland_dpi()`. `wayland_display_info` gains `fractional`.
- `src/wayland/output.c`: detects both globals. On such sessions the startup `dpi` is the 96 floor. The integer scale and reserve are unchanged.
- `src/wayland/wayland.c`: binds both globals. On the fractional path it creates the viewport and the fractional-scale object and sends no `set_buffer_scale`. `fractional_px` rounds halfway away from zero. `fractional_apply` sets the destination, resizes the buffer and the EGL window, and pushes RESIZE. `preferred_scale` is handled, the logical configure is tracked, the pointer maps through floor(v x s/120), and shutdown destroys the new objects.
- `src/editor/editor.c`: on non-Windows Wayland RESIZE, re-reads `wayland_dpi()` and recomputes `default_px`/`requested_px`. This mirrors the existing Windows `win32_plat_dpi` block, and legacy explicit-px fonts are excluded.
- `tests/wayland_mock.h`: optional `wp_viewporter` and `wp_fractional_scale_manager_v1` globals, with a double-buffered viewport destination. The mock checks that the buffer scale stays 1 while a destination is set. It adds a startup preferred scale, a scale change after N buffer commits, parent requests through shared memory (`request_scale`, `request_w/h`), and a configurable pointer position.
- `tests/wayland_test.c`: new `fractional_test` (platform cases at 150 and 192, each with a runtime change to the other, plus a logical resize at the new scale; absence, with either protocol alone), two fractional raster cases (1.25 -> 1.6 release-on-replace with presentation; 1.6 -> 1.25 immediate release without presentation), and `editor_fractional_test`.
- `src/wayland/STATUS.md` updated.

## Red run (before the implementation; header stubs only, so the suite compiles)

```
display guard: real display :0 refused, using :99
wayland_test: tests/wayland_test.c:337: wlr_open: Assertion `f->p.width == want_w && f->p.height == want_h' failed.
timeout: the monitored command dumped core
```
(rc 134. The fractional raster case runs first, so it is the first failure. Earlier compile-red: `wayland_display_info.fractional` and the protocol headers did not exist.)

## Green run

`build/tests/wayland_test` (release, allocator guard active):
```
wayland_test: raster fractional-1.25-to-1.6 passed: frames=17 commits=17 buffers=4 damage_rects=27 releases=16 typing_mallocs=0 post_submit_mallocs=335 guard=active
wayland_test: raster fractional-1.6-to-1.25-immediate passed: frames=17 commits=17 buffers=2 damage_rects=27 releases=17 typing_mallocs=0 post_submit_mallocs=242 guard=active
wayland_test: fractional 150/120 -> 192/120: buffer 50x130 for logical 31x81, dpi 153.6, pointer (13,25)
wayland_test: fractional 192/120 -> 150/120: buffer 39x101 for logical 31x81, dpi 120.0, pointer (16,32)
wayland_test: wp_viewporter or wp_fractional_scale_v1 alone keeps the integer path
wayland_test: editor fractional 96 dpi -> 1.25 -> 1.6: cell_h 15 -> 20 -> 24, buffer 256x192, typing_mallocs=0 guard=active
```
All the earlier cases are unchanged and pass. The sanitizer build of the same suite passes, with the guard ASan-inert.

What each assertion covers:
- Buffer = round(logical x s): 30 x 1.25 = 37.5 -> 38, the half-away case.
- Viewport destination = logical size, unchanged across a scale change and following a logical resize.
- Buffer scale 1; the mock also asserts it.
- The EGL window's width and height (via `wayland-egl-backend.h`) follow the buffer.
- Pointer: 10.5 x 1.25 -> 13, 20.25 x 1.6 -> 32.
- The font follows the scale: cell height grows at each change.
- The typing bracket (on_ingress -> on_submit) has zero mallocs across a mid-session 1.25 -> 1.6 change: 100 keys, release build.
- Absence: buffer scale 2, startup dpi 192, no add-on objects.

Full gates in this worktree:
- `make all`: exit 0.
- `make TARGET=win all`: exit 0.
- `make check`: `check: 119 test binaries passed in 100.1 s (parallel, 36 jobs, 6 displays)`, exit 0. The `display lock timeout` and `make[1] ... Error 1` lines in the log come from the display-lock contract test, which ends with `display lock contract: ... PASS`.

## Measurements

None. `bench/wayland_bench` measures only against a real `WAYLAND_DISPLAY`, which rules 11 and 13 forbid. The counts above are allocation counts, not timings, stamped [AC, desktop monolith] (no BAT0; load average about 4 on 36 CPUs).

## Found

- **Surface-enter output selection on the integer path** is not a small change, so it is not done. It needs `wl_output` bound on the main registry, `wl_surface.enter/leave` tracking, a runtime `set_buffer_scale` committed together with a matching buffer resize, and a DPI change. The editor side now exists (RESIZE plus `wayland_dpi`), so a plat-only follow-up could reuse it.
- **Startup transient on fractional sessions.** Hyprland and wlroots send `preferred_scale` on output enter, after the first buffer maps the window, so the first frame is at scale 1 (96 dpi) and is replaced within a frame or two. A better startup guess needs `zxdg_output_v1`: scale = mode / logical size. That would need a new vendored protocol and would remove the transient. It is a candidate follow-up, not done.
- **Scales below 1** shrink cells below the startup reservation, so text columns clamp at the reserved count. Font zoom-out already has this limitation.
- **Inherited `WAYLAND_SOCKET` launches** use the legacy explicit-px font and do not follow the DPI (an existing STATUS item).

## Real-compositor validation (edit-b6ai.5)

The checklist is in the decision note's last section:
- `WAYLAND_DEBUG=1` shows preferred_scale 150/192, `set_destination(logical)`, buffers of round(logical x s), no `set_buffer_scale`, and no protocol errors.
- Text is crisp at 1.25 and 1.6, with only the first frame at scale 1.
- A maximized window on the 4K output at 1.25 fills its width with text.
- Pointer hit-testing is right at fractional scales.
- GL and raster both follow a live `hyprctl` scale change while typing.
- Moving between outputs of different scale re-selects the font.
- Integer-only behaviour is unchanged.

## Missing

Nothing in scope. No fuzz op was added: `preferred_scale` is a single uint whose zero value is ignored, and the mock cases cover the values.
