# edit-177r.33-s28 — hidden Windows tests and the real pointer

Verdict: **done**. Linux verification and the requested cross-compiles passed.
**Not run on Windows. No Windows executable was started, including helpers,
fixtures, PowerShell or process-list tools.** Native runtime verification belongs
to the coordinator, with Tobias's agreement.

## Changes and gated sites

- `tests/win/test_isolation.h`: `LIVE_ONLY_NATIVE_LOOP` queries native isolation
  and returns with `test_skip(..., "needs live desktop: native move/size loop
  moves the real pointer")` unless it is `TEST_ISOLATION_LIVE`.
- `tests/win/win32_plat_test.c`: live-gated `modal_test` (native size),
  `move_test`, `menu_test` (Alt+Space), `turn_test`, `turn_size_test`,
  `turn_menu_test` (`SC_KEYMENU`), `turn_close_test`, `turn_session_test`,
  `turn_quit_test`, and their two common native entry helpers. Also gated
  `packet_test` (Unicode `SendInput`), `native_input_test` (F12 `SendInput`),
  `capture_test` (native `SetCapture` theft), and `altcode_test` (native Alt
  release reaches DefWindowProc; conservatively treated as menu activation).
- `tests/win/editor_win_test.c`: `native_turn_test` starts with the live check,
  covering `--modal-turn`, `--modal-close`, `--modal-session`, `--modal-save`,
  `--modal-clip`, `--modal-draw`. The live selector now runs these cases; the
  publication variant substitutes all clipboard APIs. `--modal-clock` uses
  direct notifications and remains hidden-safe.
- Hidden platform `notifications` sends sizing and menu entry/exit messages,
  key input, work wakeups and timer ticks on the UI thread. It asserts continued
  `on_modal_turn` calls after turn stops, no queued `WM_CANCELMODE` on those
  stops, and cancellation for close/session/quit.
- Hidden editor `--notify-size`, `--notify-menu`, `--notify-close`,
  `--notify-menu-close`, `--notify-session`, `--notify-quit` drive the production
  platform callbacks and modal service through UI-thread `SendMessageW`.
  They assert exact accepted text, work/frame/resize/journal progress, repeated
  `on_modal_turn`, absence of cancel on turn stops, cancel on close, and the
  ordinary closed result. The watchdog notification test now covers sizing
  and menu entry/exit, exclusion during modality, rebasing afterward and
  ordinary expiry. Callback fixtures restore their stack registrations before
  assertions can return.

These notification tests **do not prove that the real native loop survives**;
the code comments and testing documentation state that limit. The real cases
remain in `check-live` for coordinator-authorized execution.

## Source guard

`tools/check_win_pointer.py` is pure Python and invokes no Windows API or
process. `make TARGET=win check-win-pointer` runs it and its Python regressions
alone. It is a prerequisite of Windows `check`, so it finishes before the check
recipe can launch even the reparse fixture, and also precedes `check-live`.

It scans all `.c`, `.h`, `.inc` files recursively under `tests/`, a conservative
superset of Windows test inputs. This includes unreferenced incoming headers.
It rejects `SC_MOVE`, `SC_SIZE`, `SC_KEYMENU`, `SetCursorPos`,
`SetPhysicalCursorPos`, `ClipCursor`, `mouse_event`, `SendInput`,
`WM_NCLBUTTONDOWN`, `DragDetect`, `TrackPopupMenu`/`TrackPopupMenuEx`,
`BlockInput`, `SetCapture`, `SetForegroundWindow`, `SwitchDesktop`, posted
Alt+Space and native Alt release outside a function whose first unconditional
statement is the trusted live macro. It checks the actual macro definition and
rejects redefinitions, nested/late/conditional gates and return expressions
with input side effects. Comments and literals are ignored. This is a lexical
review guard, not a C preprocessor or protection against deliberately
obfuscated numeric commands; production sources are outside its scope.

## Read-only audit

Searched every test and tool for the requested pointer/input APIs and native
system commands, including fixtures and `win_quiet_run`. The only direct native
input/capture calls are the gated platform cases above; no other test/tool
calls the requested warp/confine/injection APIs. Ordinary client button/motion
messages in tests address their own HWND and do not enter a native move/size
loop. The fixture posts text/control input to its own editor or injects editor
events; it never uses global input injection. The runner creates private
stations/desktops and child jobs but never switches desktop or warps/confines
the pointer. Its opening comment now explicitly says the pointer is shared.

Reviewed the remaining `WM_SYSCOMMAND` sites: `SC_MAXIMIZE`, `SC_RESTORE`,
`SC_MINIMIZE`, `SC_CLOSE` and caption double-click in `caption_test` do not enter
the move/size pointer loop; their quiet command-path coverage remains. Alt+F4
routes to close and remains quiet-safe. Alt+Space, explicit system menus and
the uncertain Alt-release route are gated.

Read the sibling `edit-177r.32-s28/tests/win/editor_resize_cases.h` and recorded
a read-only `git diff --no-index` against `/dev/null` (this tree has no such
header). Its `resize_cases` explicitly requires non-live isolation and posts
`SC_SIZE|WMSZ_RIGHT`. The guard rejects that header at its native `SC_SIZE`
line. The sibling worktree was not edited.

Updated `docs/design/windows-port.md` Quiet runs, the live-case inventory in
`docs/testing/windows-check-live.md`, and `docs/decisions/edit-177r.33.md`.
The shared rules file was read and left untouched.

## Red/green and verification

All builds/tests use `taskset -c 32-47 nice -n 10`; make uses `-j16` (G: brief's
assigned CPU lane and job count). Logs are under `build/edit-177r.33/`.
Environment: [AC, desktop Spiral], Tobias using the machine. `uptime` before
verification reported load averages `0.02, 0.35, 1.38` (M: `uptime` output;
no performance claim or benchmark).

- RED: `python3 tools/check_win_pointer.py` on the original sources: exit **1**
  (M: `guard-red.log`), with `editor_win_test.c:1068: SC_MOVE`, platform
  `SC_SIZE`/`SC_MOVE`/`SC_KEYMENU`, Alt+Space, `SendInput`, `SetCapture` violations.
- GREEN: `make TARGET=win -j16 check-win-pointer`: exit **0**
  (M: `guard-green.log`), `Windows pointer guard: PASS`; Python regressions OK.
- Incoming-header RED: `python3 tools/check_win_pointer.py
  /home/tobiasosborne/Projects/sublimite/.wt/edit-177r.32-s28/tests/win/editor_resize_cases.h`:
  exit **1** (M: `guard-sibling-red.log`), `:251: SC_SIZE requires
  LIVE_ONLY_NATIVE_LOOP as the function's first statement`.
- `git diff --no-index -- /dev/null <sibling header>`: exit **1**, expected diff
  (M: `sibling-resize.diff`), read-only.
- `make -j16 all`: exit **0** (M: `make-all.log`).
- Initial `DEBUGINFOD_URLS= make -j16 check`: exit **2** before any suite ran
  (M: `make-check.log`): the restricted sandbox denied Xvfb socket binds.
  A private `/tmp` Unix socket probe returned `EPERM`; automatic review approved
  retrying the same Linux check outside that socket restriction.
- `DEBUGINFOD_URLS= make -j16 check` outside the socket restriction: exit **0**
  (M: `make-check-unrestricted.log`), `check: 117 test binaries passed`
  (M: runner's suite inventory); replay CLI, Linux shell-stub Windows launcher
  contract, and display-lock contract also passed. The Windows launcher
  contract executes a Linux shell stub, never a Windows program.
- Cross-build recipes and full `make -n` output were inspected before building:
  only native Linux compiler/archive/resource tools, mkdir and DLL copies;
  no Windows executable invocation (M: `cross-dry-run.log`).
- `make TARGET=win -j16 build/win/rel/tests/win32_plat_test.exe
  build/win/rel/tests/editor_win_test.exe build/win/san/tests/win32_plat_test.exe
  build/win/san/tests/editor_win_test.exe`: exit **0** on both the initial build
  and final refresh (M: `cross-build.log`, `cross-build-final.log`); no compiler
  warnings/errors in the final log.
- `git diff --check`: exit **0** (M: read-only whitespace check).

## Found, not fixed / coordinator follow-up

- The sibling resize header still starts a native size loop on hidden
  desktops. Do not integrate it unchanged: the new guard rejects it. The
  coordinator must adapt that incoming native case to the same live-only
  policy while retaining its direct-notification coverage.
- Windows runtime results, SKIP census and real-loop survival remain unmeasured
  here by explicit instruction. The coordinator runs them later with agreement.
