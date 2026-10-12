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

Missing: a Wayland CPU raster renderer. The XShm/Present CPU backend rejects
Wayland, so GL still requires wp_presentation and no CPU fallback is available
without it. The opt-in headless acceptance probe remains red:
`build/tests/wayland_test --require-presentation-fallback`.
This needs wl_shm buffer ownership/release and a decision about completion when
matching presentation timestamps are unavailable; it is larger than a local
backend guard correction. The bead remains incomplete.

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
