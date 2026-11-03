# edit-b6ai.15-s29: UI density under XWayland

## Changed
- `src/editor/display_size.c`, `src/editor/runtime.h`: new `editor_runtime_dpi_x11()` (Xft.dpi wins; XWayland -> 96 x scale; else existing physical rule) and `editor_runtime_scale()` (parses EDIT_DISPLAY_SCALE; invalid -> 1). `editor_runtime_dpi` is untouched.
- `src/x11/x11_posix.c`: detects XWayland with `xcb_query_extension("XWAYLAND")`, scale from `EDIT_DISPLAY_SCALE` (default 1).
- `tests/font_size_test.c` (display_default): XWayland/real-X11/Xft/invalid-scale cases; `fuzz/font_size_fuzz.c` extended.
- `docs/config.md` (one paragraph), `docs/decisions/edit-b6ai.15.md` (signal, scale source, rejected options).
- Native Wayland (`src/wayland/output.c`) already uses 96 x wl_output.scale: no change. Windows untouched.

## Red
`make build/san/tests/font_size_test` before the implementation: 18 errors, `call to undeclared function 'editor_runtime_scale'` / `editor_runtime_dpi_x11` (compile-red).

## Green
- `font_size_test`: passes (3 summary lines). Key values: 3840x2160 @ 1860x1040 mm without XWayland = 52.5 dpi -> 7 px (unchanged); with XWayland = 96 -> 13 px; scale 1.5 -> 144; Xft.dpi 144 wins in both; 2880x1800 panel physical result identical to `editor_runtime_dpi`; Xvfb without mm -> 96.
- `make all` ok; `make TARGET=win all` ok (rc 0); `make check`: "check: 118 test binaries passed in 107.3 s (parallel, 36 jobs, 6 displays)", test_replay_cli all passed. fuzz font_size_fuzz: builds, 10 s run clean.

## Measurement [AC, desktop monolith, no battery]
Probe (xcb_query_extension, read-only): `XWAYLAND` present=1 on :0, 0 on Xvfb :99. :0 RandR outputs are named DP-9 / HDMI-A-1 (not XWAYLAND0), so names are no signal.
Run of build/sublimite on DISPLAY=:0 (timeout 6, no input), grim of window 1901x1048 at monitor scale 1: text row pitch of a 60-line file = 15 px on 14 consecutive rows (glyph bands 13 px high). Derivation (P): em = round(10 x 96/72) = 13; DejaVu cell_h = round(13 x (1901+483)/2048) = 15. Before: em 7 -> cell about 8.

## Found (not fixed)
1. During this session `/tmp/.X11-unix/X0` (the filesystem name of the real XWayland socket; `X0_` is its Xserver link partner) disappeared between my first probe and my first `build/sublimite` run, so new X clients on :0 got ECONNREFUSED/ENOENT (sublimite reported `error -103` = EDITOR_ERR_IO, because plat_display_size failed). I do not know the cause (it coincided with a concurrent `make check`; I found no code in tests/tools that unlinks X0). I restored it with `ln /tmp/.X11-unix/X0_ /tmp/.X11-unix/X0`; X clients on :0 work again. Coordinator should know if other X0 clients were affected. Possible follow-up: confirm make check never touches /tmp/.X11-unix/X0.
2. An X connect failure surfaces as the generic "error -103" with no hint; low priority.
