# Wayland — edit-v0o.1

Implemented: optional raw client backend, compositor/xdg-shell configure/ack,
xkbcommon keyboard/compose/repeat, pointer/framed scroll, asynchronous UTF-8
clipboard, frame callbacks and matching presentation feedback. Main chooses
Wayland only without a nonempty X display; legacy plat initialization is X11.
GL uses its existing worker loader and fences, with no extra frame-callback gate.

Headless protocol, mocked-fence integration and benchmark endpoint tests pass
under ASan/UBSan and gcc release. The release decoder allocator guard passes.
The dependency-disabled application/stub build and fixed fuzzer seeds pass.
make all passes. make check was attempted; individual runnable suites pass,
while X and IPC socket restrictions block the host-dependent suites.

Acceptance is pending actual Wayland EGL/pixels, recorded G1/G3, the host full
check with LeakSanitizer and the next Astra comparison. Primary selection is
unsupported by wl_data_device; output scaling/cursor/decorations are not added.
The application clipboard wiring is outside this bead.

[Worker evidence](../../docs/worker-reports/edit-v0o.1.md).
[Design/provenance](../../docs/decisions/edit-v0o.1.md).
