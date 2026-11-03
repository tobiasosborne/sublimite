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

Startup output selection is the highest-scale advertised output. Surface-enter
selection, output moves, runtime DPI/font changes, old compositor versions
without buffer scaling, and font-DPI startup on inherited WAYLAND_SOCKET are
not complete. The inherited descriptor is preserved for platform initialization;
main cannot consume that existing protocol connection in a preliminary query.

No compositor exists in this sandbox. Real Wayland EGL pixels, compositor
buffer-scale negotiation and mixed-output movement need a host compositor.
X display suites must be re-run by the coordinator on the private Xvfb display
specified in the worker brief. Full check also encounters sandbox LeakSanitizer
and IPC restrictions, detailed in the worker report.

[Worker report](../../docs/worker-reports/edit-1zm-s6.md).
[Decisions](../../docs/decisions/edit-1zm.md).
