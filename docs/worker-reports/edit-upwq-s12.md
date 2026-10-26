# edit-upwq-s12 worker report

Verdict: done within the assigned platform scope. Implemented the Win32 backend of `x11/plat.h`, with a real-window contract suite and Windows build integration. The editor DPI/font-metrics integration gap and the expected D3D11 glue handoff are listed under coordinator decisions below.

All commands ran from this worktree. Only the binding rules and assigned brief were read from the main checkout; it was not modified. No `bd` or state-changing git commands were run. `src/os/os.h` and `src/render/render.h` are untouched. `src/x11/plat.h` gains only `PLAT_BACKEND_WIN32` and `plat_native_win32`.

## Changes and files

- `src/win32/win32_plat.c`: native window/class lifecycle; per-monitor v2 DPI; event translation and ordered, bounded message loop; borrowed work wake; owned blink/repeat timers; mouse capture and fractional wheels; Unicode clipboard; display geometry/timing; native HWND; presentation mailbox and UI callback delivery.
- `src/win32/private.h`: backend state and the documented renderer-to-platform T6 entry.
- `src/win32/keysyms.h`, `tools/win32_keysyms.py`: reproducible VK and Unicode-to-XKB tables from symbolic VK names and vendored keysyms.
- `src/wayland/dispatch.c`: whole-file Linux guard and Linux NULL native-Win32 accessor. Existing Linux dispatch remains unchanged.
- `mk/win.mk`: includes the Win32 module and system import libraries, and prints an aggregate check summary. Existing test discovery includes the new suite automatically.
- `tests/win/win32_plat_test.c`: semantic cases for real SendInput, posted keys, modifiers, Unicode injection, keypad text, German AltGr/dead keys, native repeat suppression and owned repeat, pointer/capture, fractional wheels, size/DPI/expose, clipboard/PRIMARY, bounded loop, work wake, blink, allocation guard, renderer-thread T6, callback ordering/quit and repeated shutdown handle balance.
- `docs/decisions/edit-upwq.md`: generation, input rules, timing/wheel derivations, loop ownership, DPI behavior, and W1.2 handoff.
- This report.

## Evidence context

(M) All observations here are `[AC, desktop Spiral]`, the desktop with no BAT0 described by the worker rules. Load stamps were captured with `uptime`, not inferred:

```text
Initial inspection:
11:14:30 up 3:31, 1 user, load average: 1.68, 1.59, 2.30
Final Windows full check:
11:48:24 up 4:05, 1 user, load average: 1.40, 1.58, 1.73
Unsandboxed Linux full check start:
11:22:16 up 3:39, 1 user, load average: 4.75, 4.68, 3.51
Final Linux release rebuild:
11:50:39 up 4:07, 1 user, load average: 0.50, 1.22, 1.58
```

(M) Exit codes below are the shell command's observed status. Binary/case totals are printed by their build/test runners; Windows binary totals derive from the discovered Windows suites plus `tests/os_test.c`. Test dimensions/delays are fixture choices (E), not performance gates. No new performance budgets were introduced.

## Red first

The new suite was written and run before implementation:

```text
make TARGET=win -j64 build/win/rel/tests/win32_plat_test.exe
exit 2
fatal error: 'win32/private.h' file not found
```

Raw compile-red output: `build/worker-upwq/red.log`.

The first executable run then exposed an actual input bug and the native DPI-injection restriction:

```text
tools/winrun.sh build/win/rel/tests/win32_plat_test.exe
exit 1
win32_plat: German AltGr/dead keys: PASS
FAIL ... GetCapture()==window(f)
FAIL ... PostMessageW(...WM_DPICHANGED...) (winerr 1159)
win32_plat_test: FAIL (3 failures)
```

Raw output: `build/worker-upwq/red-runtime.log`. Capture was fixed by acquiring only when not already owned. DPI injection uses synchronous SendMessageW because asynchronous system messages cannot carry a RECT pointer.

Additional behavioral regressions were added and observed red before their fixes:

```text
tools/winrun.sh build/win/rel/tests/win32_plat_test.exe
exit 1
FAIL ... KP_1 ... utf8_len==1 ...
FAIL ... shifted->repeat ... keysym==XKB_KEY_A ...
win32_plat_test: FAIL (3 failures)
```

Raw output: `build/worker-upwq/red-input.log`. The additional failure was the original strict handle-equality assertion: Windows retired unrelated handles during the lifecycle. The final assertion checks no growth across repeated init/shutdown, allowing background handle retirement. Keypad text and current-modifier repeat translation were fixed.

```text
tools/winrun.sh build/win/rel/tests/win32_plat_test.exe
exit 1
FAIL ... kind(f,PLAT_EV_KEY)==n
win32_plat: German AltGr/dead keys: FAIL
win32_plat_test: FAIL (1 failures)
```

Raw output: `build/worker-upwq/red-compose.log`. A committed composition now cancels repeat, matching X11.

Final review added a cancelled-composition release regression, observed red before the fix:

```text
tools/winrun.sh build/win/rel/tests/win32_plat_test.exe
exit 1
FAIL ... kind(f,PLAT_EV_KEY)==n
win32_plat: German AltGr/dead keys: FAIL
win32_plat_test: FAIL (1 failures)
```

Raw output: `build/worker-upwq/red-compose-release.log`. Invalid acute+Q composition now suppresses both the press and release. The complete Windows check was rerun after this fix.

## Windows green

```text
make TARGET=win -j64 check
exit 0
```

The Makefile runs every executable through `tools/winrun.sh`, in place. WSL interop initially failed inside the sandbox (`UtilBindVsockAnyPort ... socket failed 1`); the authorised desktop tests and full check ran with sandbox escalation.

Relevant full-check output (M), `[AC, desktop Spiral]`:

```text
== build/win/rel/tests/win32_plat_test.exe
win32_plat: SendInput: PASS
win32_plat: ASCII/modifiers/native-repeat suppression/Unicode/keypad: PASS
win32_plat: installed layouts: FFFFFFFFF0010809 0000000004070407 0000000004090409
win32_plat: German AltGr/dead keys: PASS
win32_plat: pointer/capture/notches/fractional wheel: PASS
win32_plat: display dpi=96.0 work=5120x1400 interval_ns=6060569 (M)
win32_plat: resize/DPI/expose/display: PASS
win32_plat: Unicode clipboard/PRIMARY no-op: PASS
win32_plat: typing allocation guard: PASS (0 calls)
win32_plat: input/queue allocation: PASS
win32_plat: bounded loop/work/blink/owned repeat/T6: PASS
win32_plat: close/order/quit callback: PASS
win32_plat: handles before=192 after=190 (M)
win32_plat: shutdown handle balance: PASS
win32_plat_test: PASS (0 failures)
== build/win/san/tests/win32_plat_test.exe
win32_plat: SendInput: PASS
win32_plat: ASCII/modifiers/native-repeat suppression/Unicode/keypad: PASS
win32_plat: German AltGr/dead keys: PASS
win32_plat: pointer/capture/notches/fractional wheel: PASS
win32_plat: resize/DPI/expose/display: PASS
win32_plat: Unicode clipboard/PRIMARY no-op: PASS
win32_plat: typing allocation guard: SKIP (sanitizer owns allocator) (0 calls)
win32_plat: input/queue allocation: PASS
win32_plat: bounded loop/work/blink/owned repeat/T6: PASS
win32_plat: close/order/quit callback: PASS
win32_plat: handles before=192 after=190 (M)
win32_plat: shutdown handle balance: PASS
win32_plat_test: PASS (0 failures)
os_test: 17 cases, 0 failed
ASan fired: ok
check (win): 4 release and 4 sanitizer test binaries passed
Windows check exit=0
```

Raw log: `build/worker-upwq/windows-check.log`. (M) Display values come directly from GetDpiForMonitor/GetMonitorInfoW and the DWM refresh rational; interval derivation is in the decision note. (M) Handle counts are GetProcessHandleCount before/after repeated lifecycle. The allocator guard counts project allocations during translation/queue/pointer input; the sanitizer owns allocation interception in the sanitizer binary, so that specific guard is explicitly skipped there. Windows LeakSanitizer is unsupported by this toolchain, as recorded in the port rules.

## Linux verification

```text
make -j64 all
exit 0
```

Raw build log: `build/worker-upwq/linux-all.log`.

The first sandboxed full sanitizer attempt failed in the existing base suite because LeakSanitizer cannot operate under ptrace. It was rerun with escalation; leak detection was never disabled:

```text
DISPLAY=:99 ASAN_OPTIONS=detect_leaks=1 make -j64 check
```

Final result (M), `[AC, desktop Spiral]`:

```text
exit 0
check: 104 test binaries passed
== tools/test_replay_cli.sh
test_replay_cli: all passed
== tools/test_display_lock.sh contract
display lock contract: owner PID, alias/timeout, make/scripts, nested inheritance, exit status PASS
Linux check exit=0
```

The binary total is the Makefile's `$(words $(TEST_SAN))` count. The display suites ran on `:99` using the Makefile's `tools/with_display_lock.sh`. Shared-display lock contention is visible in the log. The final display-lock contract deliberately tests timed-out nested commands and prints their errors before PASS; those are expected assertions of the existing contract suite, not failures of the enclosing full check. Raw log: `build/worker-upwq/linux-check.log`; observed exit status: `build/worker-upwq/linux-check-exit.txt`. The earlier sandbox/LeakSanitizer failure remains in `build/worker-upwq/linux-check-sandbox.log`.

Other checks: `git diff --check` passed. Regenerating the keysym header and comparing against its saved copy passed (`cmp` exit 0); the generator also passed Python compilation. Build logs and saved generated-header comparison live under `build/worker-upwq/`.

## Found, not fixed / coordinator decisions

- **DPI font metrics:** WM_DPICHANGED updates native geometry/DPI and emits RESIZE, including unchanged-size changes. Contrary to the brief's assumption, the current editor RESIZE path only updates dimensions and does not re-query DPI or rebuild font/cell metrics. Decide the editor integration follow-up; no editor code was changed in this platform bead.
- **D3D11 T6 glue:** `src/d3d11` was absent. W1.2 should call `win32_plat_present_complete` with actual displayed-frame statistics and retry a full mailbox. Its contract and lifetime rules are in `src/win32/private.h` and the decision note. W1.2/W2 also need to route the editor callback to the D3D11 consumer; the current editor callback is GL-specific. The suite verifies the platform's producer-thread-to-UI delivery independently.
- **Existing Windows OS-suite availability:** both configurations report `os_win_test: 18/19 passed, 1 unavailable`; the existing symlink fixture requires Developer Mode/elevation (native error 1314). This did not fail the existing suite and was not changed. The portable OS suite also reports its existing relative-path skip when its CWD and scratch directory have different roots.
- Precision-device behavior is pinned with fractional WM_MOUSEWHEEL messages, not claimed as a test on physical touchpad hardware. Native IME composition and other keyboard layouts are not covered by this suite.
