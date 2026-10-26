# edit-cyn4 round 3 — rebased Windows font integration

Verdict: **done** for the supplied worktree; integration-base discrepancy below.

## Root cause and direct experiment

`tools/winrun.sh` now enters the repository root before launching an executable
in place. `font_test_root()` still assumed the initial cwd was
`build/win/{rel,san}/tests` and unconditionally walked up to its fourth parent.
It therefore left this worktree; archive extraction, the test-only file stub,
compiler flags, and `font_load_file()` were not responsible.

A temporary probe was linked beside the release tests against the same
`libcore.a`, manifest, system libraries and malloc wrappers. It printed
`GetCurrentDirectoryW()` and tried both CRT `fopen()` and `os_file_open()` on
`vendor/DejaVuSansMono.ttf`, before/after the fixture, after a repeated fixture
call, and after explicitly entering the executable directory.

Red command: `tools/winrun.sh build/win/rel/tests/font_path_probe.exe` — exit 1.
Relevant output (UNC prefix `\\wsl.localhost\Ubuntu` omitted from cwd here):

```text
runner cwd=\home\tobiasosborne\Projects\sublimite\.wt\edit-cyn4-s12
runner CRT=ok os_file_open=0 native=0
fixture cwd=\home\tobiasosborne
fixture CRT=fail os_file_open=1 native=3
repeated fixture cwd= [UNC share root]
repeated fixture CRT=fail os_file_open=1 native=3
exe fixture cwd=\home\tobiasosborne\Projects\sublimite\.wt\edit-cyn4-s12
exe fixture CRT=ok os_file_open=0 native=0
```

The original regression was also reproduced directly:
`tools/winrun.sh build/win/rel/tests/font_test.exe` — exit 1,
`font_test: cannot read vendor/DejaVuSansMono.ttf`.

## Change and green experiment

Only `tests/win/font_fixture.h` changes code. It obtains the absolute executable
path with `GetModuleFileNameW()`, removes the filename and the documented
`build/win/{rel,san}/tests` components, then enters that worktree root. The
component count follows directly from that build layout. It rejects truncated
or malformed executable paths and works independently of initial cwd, including
repeated calls. No changes to build rules, production code, or frozen headers.

Recompile the same probe and rerun the same command — exit 0. Every observation
now prints this worktree root and `CRT=ok os_file_open=0 native=0`, including
the repeated call and executable-directory start. The probe source and exe
were deleted after verification; logs remain in `build/worker-logs/r3/`:
`font-red.log`, `probe-red.log`, `probe-green.log`.

## Verification

(M) [AC, desktop Spiral]: `/sys/class/power_supply/BAT0` absent. Starting
`uptime`: `13:08:33 up 5:23, 1 user, load average: 1.63, 2.51, 2.81`.
Those values are command observations, not budgets or performance gates.

- `make TARGET=win -j64 check` — exit 0. (M) All 66 `RESULT` lines have
  `exit=0`: (26 `WIN_CORE_TESTS` + 4 Windows-only suites + 3 explicit portable
  suites) × 2 configurations = 66. This includes `font_test`, `font_gsub_test`,
  `font_dwrite_test`, and `font_dwrite_r2_test` in release and sanitizer builds.
  The expected sanitizer smoke failure also reports `ASan fired: ok`.
  Log: `windows-check.log`.
- `make -j64 all` — exit 0 (`Nothing to be done for 'all'`);
  `make -j64 build/san/tests/font_test build/san/tests/font_gsub_test` — exit 0
  (both current). Logs: `linux-all.log`, `linux-font-build.log`.
- `DISPLAY=:96 build/san/tests/font_test` — exit 0, `font_test: all passed`.
- `DISPLAY=:96 build/san/tests/font_gsub_test` — exit 0,
  `font_gsub_test: all passed`. Logs: `linux-font-test.log`,
  `linux-font-gsub-test.log`. Neither suite uses a display; no display lock was
  needed. An initial lock wait was interrupted before running the suite.
- `git diff --check` — exit 0.

Windows interop required execution outside the sandbox (inside it WSL reports
`UtilBindVsockAnyPort: socket failed`). The Linux font sanitizer also required
execution outside the sandbox because LeakSanitizer reported its ptrace
restriction; the successful runs kept ASan/UBSan/LeakSanitizer enabled.

## Found, not fixed / coordinator

Existing suite availability notes remain: Linux skips the absent system Noto
CJK fixture, and the Windows OS suite reports its file-symlink privilege case
unavailable. No coordinator decision is needed for the fixture fix.

The supplied worktree differs from the handoff's integration-base description:
`src/d3d11` and `src/win32` do not exist here, and `WIN_MODULES` does not include
`raster`. The coordinator should confirm the intended base; this run verifies
the targets actually present in this worktree. Those unrelated integrations
were not changed.

All edits and output stayed in this worktree. No bd or state-changing git,
main-checkout changes, HANDOFF edits, or docs/worklog edits.
