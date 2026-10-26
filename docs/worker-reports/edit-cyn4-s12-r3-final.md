The fixture assumed execution began beside the executable, but `winrun.sh` now starts at the worktree root. The probe confirmed both CRT and OS file opens succeeded before the fixture’s parent-directory walk and failed afterward.

Fixed `font_fixture.h` to derive the root from the executable path; repeated calls also pass.

Windows `check` exited 0: all 66 release/sanitizer results passed, and expected ASan detection passed. Linux `all` and both sanitizer font suites passed.

Coordinator: confirm the integration base—this worktree lacks `src/d3d11`, and `WIN_MODULES` omits `raster`.

Done. [Report](docs/worker-reports/edit-cyn4-s12-r3.md).