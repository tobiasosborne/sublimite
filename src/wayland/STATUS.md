# Wayland — edit-1zm disposition

Implemented in this worktree: input overflow drops/counts rather than poisoning
the connection; readable startup diagnostics; descriptor-based connection paths
for long runtime directories; explicit Wayland display guard; startup output
DPI, EGL buffer scale, configure conversion and pointer coordinate conversion.
The headless protocol mock and the null-editor allocation test cover these paths.

The release allocator test separately counts native transport before decoded
ingress and one-shot frame/proxy creation after submit. The enforced bracket
covers decoded input, mutation, layout and null backend submit. Calibration
proves that the release interposer is active; sanitizer counts remain inert.
Actual EGL-driver submit allocation and physical input latency need a compositor.

CPU raster fallback (edit-b6ai.6): `render_cpu_backend` has a wl_shm variant.
It uses one memfd pool, two slots sized at init and recreated only on surface
resize, release before reuse, `damage_buffer` per damage rectangle, the frame
callback, and T6 from `wp_presentation` feedback. Without wp_presentation,
`wayland_frame_arm` arms only the frame callback and its `done` is the
completion (MSC 0). The rule and its trace consequences are in
[decisions/edit-b6ai.6.md](../../docs/decisions/edit-b6ai.6.md). wl_shm is bound
here (`wayland_shm`), and `wayland_buffer_scale` exposes the applied scale.
`wayland_frame_notify` re-delivers completion on `wl_buffer.release`. Wayland GL
that refuses or fails at init falls back to raster once (open.c). The opt-in
probe `build/tests/wayland_test --require-presentation-fallback` is green. The
mock (`tests/wayland_mock.h`) now has libwayland-server's real wl_shm. Its
screen copies only damaged rectangles and asserts the whole buffer equals it,
and it supports release-on-replace or immediate release and a resize configure.

Fractional scale (edit-b6ai.7): when the compositor advertises both
`wp_fractional_scale_manager_v1` and `wp_viewporter`, the surface gets a
viewport and a fractional-scale object. The buffer is round(logical x
preferred_scale / 120), the destination is the logical size, and the buffer
scale stays 1. `preferred_scale` resizes the buffer and the EGL window and
pushes RESIZE. The editor re-reads `wayland_dpi()` (96 x scale) there and
re-selects the font at its frame boundary. Pointer coordinates use the same
factor. Startup DPI on such sessions is the 96 floor, so the cell reservation
covers every scale >= 1, until the compositor's first `preferred_scale` arrives.
With either protocol missing, the integer path is unchanged. Mock cases: 1.25
and 1.6, a runtime change both ways, a logical resize at a fractional scale,
both rasters, the editor font change and its typing bracket, and absence.
[Decisions](../../docs/decisions/edit-b6ai.7.md).

Startup output selection on the integer path is the highest-scale advertised
output. Surface-enter selection and output moves on the integer path (runtime
`set_buffer_scale`), scales below 1 beyond the cell reservation, old
compositor versions without buffer scaling, and font-DPI startup on inherited
WAYLAND_SOCKET are not complete. The inherited descriptor is preserved for platform initialization;
main cannot consume that existing protocol connection in a preliminary query.

No compositor exists in this sandbox. Real Wayland EGL pixels, compositor
buffer-scale negotiation and mixed-output movement need a host compositor.
X display suites must be re-run by the coordinator on the private Xvfb display
specified in the worker brief. Full check also encounters sandbox LeakSanitizer
and IPC restrictions, detailed in the worker report.

[Worker report](../../docs/worker-reports/edit-1zm-s6.md);
fractional scale: [edit-b6ai.7](../../docs/worker-reports/edit-b6ai.7-s29.md).
[Decisions](../../docs/decisions/edit-1zm.md).

Primary selection (edit-b6ai.9): `zwp_primary_selection_device_manager_v1` v1 is
bound when advertised. PRIMARY and CLIPBOARD share one transfer path (offer
list, `claim`, `serve`, one receive pipe tagged by `rx_which`); events carry
`clip_which`. The editor now publishes through `plat_clip_set_buf` (dispatch.c),
which routes Wayland to `wayland_plat_clip_set_buf` (zero-copy adoption of the
editor's buffer); before this it called the X11 function on a Wayland plat.
Without the global: `plat_clip_request/set(PRIMARY)` fail as before and
`plat_clip_set_buf(PRIMARY)` frees the buffer and returns OK. Claim uses the
last serial, as the clipboard does. Mock cases: tests/wayland_test.c
`primary_plat_test`, `primary_editor_test`. Not done: UTF8_STRING/other mime
names on receive (only text/plain;charset=utf-8 and text/plain, as CLIPBOARD).
