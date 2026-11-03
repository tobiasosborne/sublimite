# edit-b6ai.9 worker report (s29)

Wayland primary selection via zwp_primary_selection_device_manager_v1 v1.

## Changed
- src/wayland/protocol/primary-selection-unstable-v1{-client.h,-server.h,.inc}, protocol.c, README.md (vendored with wayland-scanner 1.26.0).
- src/wayland/wayland.c: PRIMARY and CLIPBOARD share one offer list, claim/serve/receive path (rx_which); new wayland_plat_clip_set_buf; absent global = unchanged.
- src/wayland/dispatch.c, private.h, src/x11/clip.h: backend-neutral plat_clip_set_buf; src/editor/clip.c uses it (was x11_clip_set_buf on a Wayland plat).
- tests/wayland_mock.h (primary manager/device/source/offer mock), tests/wayland_test.c (primary_plat_test, primary_editor_test).
- src/wayland/STATUS.md, docs/decisions/edit-b6ai.9.md.

## Red
Before wayland.c changes, `build/san/tests/wayland_test`:
`wayland_test.c:613: primary_plat_test: Assertion plat_clip_request(&p, PLAT_CLIP_PRIMARY) == PLAT_OK failed` (output in /tmp/b9-red.txt).

## Green
`make check`: "check: 120 test binaries passed in 100.6 s (parallel, 36 jobs, 6 displays)". `make all` and `make TARGET=win all` rc=0. wayland_test prints the primary-selection and editor lines.

## Found (not fixed beyond what the feature needed)
- The editor called x11_clip_set_buf on a Wayland plat (X11 state read from Wayland state); Wayland copy via the editor was broken. Now routed through plat_clip_set_buf, which fixes CLIPBOARD too.

## Missing / notes
- Receive accepts only text/plain;charset=utf-8 and text/plain (as CLIPBOARD), not UTF8_STRING.
- Not tested against a real compositor (rule 13). No bench run (not a perf-touching module).
- Typing-path malloc bracket unchanged; claim happens off it (editor poll), the existing allocation tests passed.
