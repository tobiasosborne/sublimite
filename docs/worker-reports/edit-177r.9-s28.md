# edit-177r.9-s28 — worker report

Verdict: **done for isolated acceptance**. Linux release/full sanitizer checks pass. Windows release/sanitizer integration passes; the full Windows check has only the two expected sibling-owned display suites red, in both builds. Native visible-present/G1/frame-count validation remains the coordinator’s check-live, as required by the brief. All Windows executions use tools/winrun.sh on private stations. Sibling-owned win32_plat_test.c, d3d11_test.c and the Windows check recipe are untouched.

Design: docs/decisions/edit-177r.9.md. Logs: build/worker-177r9/.

## Changes

- GUI-subsystem release/sanitizer application links; all Windows modules, editor and main, no Linux window/render backend.
- Portable editor mutexes, sleeping/yielding, hostname and path stat; Linux-only fd-limit call; portable reverse search and Windows clipboard storage adapter.
- Portable creator/session/deferred-recovery leases and session identity assertions.
- D3D11 exact-statistics -> Win32 mailbox -> authenticated UI completion; auto-reset latency readiness preservation and borrowed-handle lifetime.
- Independent minimize/session/power suspension, DPI font-metric adoption, unobservable-frame rebuild, allocation-free parked CPU snapshots and idle sleep.
- Explicit isolated Windows profile and test-only posted modifier/startup/message fences.
- New executable and production-key integration suites, expanded D3D11 shim suite and os_test hostname case. Linux tool discovery excludes the Windows-only quiet runner.

## Red/green evidence recorded so far

(M) `taskset -c 16-31 nice -n 10 make TARGET=win -j16 build/win/rel/sublimite.exe`, exit 2, link-red.log:

```text
src/main.c:70:11: error: call to undeclared function 'os_fd_limit_raise'
```

Further actual compiler/linker failures (link-next.log, link-third.log): xcb/xcb.h through editor clipboard, gethostname, memrchr, integer consumers of os_file session fields, nanosleep64, sched_yield, plat_set_dark, raster_last_present and gl_displayed_msc. No undefined POSIX entry was stubbed. Application release and sanitizer links subsequently pass.

(M) Executable red, `taskset -c 16-31 nice -n 10 tools/winrun.sh build/win/rel/tests/editor_exe_test.exe`, exit 1, exe-red-native.log:

```text
sublimite: error -10
editor_exe:88 FAIL wait_contents(path,"typed äλ中🙂base\n")
```

After deferred presentation validation, the same isolated run passes Unicode typing/save/close. Expanded crash recovery then went red: saved file lacked the protected exclamation mark (exe-recovery-red.log). Its source was the armed first-paint guard during deliberate occlusion. The corrected release and sanitizer reruns pass, including byte-exact recovered bytes and the final --wait UI/launcher exit handshake (final-launcher-rel.log, final-launcher-san.log).

(M) T6 test red, make of release d3d11_win_test, exit 2, t6-red.log: d3d11_present_complete undeclared. Green t6-final-rel.log and t6-final-san.log: exact-frame native statistics travel through the actual Win32 mailbox, deliver UI T6 once, reject duplicate completion; existing native-shim cases pass.

(M) Production-key release campaign: path-final-rel.log, exit 0, guard=active, keys=10000, mallocs=0, p50_ns=102100, p99_ns=171600. Count inherits Linux allocation_case; latency = submit_ns - ingress_ns, nearest-rank index ceil(N*pct/100). This is CPU submit on the occluded D3D11 path, with no invented T4/T6. Current Linux has no percentile G1 assertion. Final journal-enabled release campaign (final-typing-rel.log): keys=10000, journal_records=10000, mallocs=0, guard=active, p50_ns=122000, p99_ns=334600. Sanitizer counterpart: zero counted allocations with guard=ASan-inert, p50_ns=425500, p99_ns=1120000; its allocator number is not the release no-allocation proof.

## Verification

All commands use CPU affinity `16-31`, nice `10`, and make parallelism `-j16` (brief's assigned range). Every native Windows binary goes through tools/winrun.sh on a private station. No live desktop execution, focus injection, registry write, or package installation occurred.

| Command / suite | Exit | Evidence |
|---|---:|---|
| `taskset -c 16-31 nice -n 10 make -j16 all` | 0 | linux-all-verdict.log |
| `DEBUGINFOD_URLS= taskset -c 16-31 nice -n 10 make -j16 check` | 0 | linux-check-complete.log; (M) emitted `117 test binaries passed`, replay CLI and display-lock contracts PASS; ASan/UBSan/LeakSanitizer enabled |
| `taskset -c 16-31 nice -n 10 make TARGET=win -j16 all` plus all sanitizer prerequisites | 0 | windows-build-complete.log; both GUI application links and all native suite links |
| `tools/winrun.sh build/win/{rel,san}/tests/editor_exe_test.exe` | 0 / 0 | final-launcher-rel.log, final-launcher-san.log: Unicode save, close, help/error status, TerminateProcess recovery, --wait and no surviving UI process |
| `tools/winrun.sh build/win/{rel,san}/tests/editor_win_test.exe` | 0 / 0 | final-typing-rel.log, final-typing-san.log: consumed auto-reset readiness, idle deadline only, DPI metrics, minimize/restore, session/power/display notifications, journal typing and active release guard |
| `tools/winrun.sh build/win/{rel,san}/tests/d3d11_win_test.exe` | 0 / 0 | final-d3d11_win-rel.log, final-d3d11_win-san.log: exact-statistics mailbox route and existing native shim cases |
| Full `make TARGET=win -j16 check`, private native TEMP/profile | 2 (expected) | windows-check-final.log: 61/63 release and 61/63 sanitizer binaries passed; only win32_plat_test and d3d11_test red in each; ASan negative control fired |
| Existing Linux release null/raster allocation cases | 0 / 0 | linux-allocator.log, linux-raster-allocator.log; each inherits 10000 edits and prints mallocs=0 guard=active |
| Existing Linux editor benchmark fixture | 0 | linux-editor-bench.log; private Xvfb from repository Runner, unchanged bench/editor_bench.args; all serial structural rows OK, allocations=0 |
| `git diff --check` | 0 | current source/docs |

The typing regression has a bounded negative control: a separate scratch object with windows_surface retirement disabled, linked to editor_spin_red.exe, exits 1 in spin-red.log:

```text
settle timeout dirty=1 active=1 queue=0
editor_win:94 FAIL settle(e)==0
```

The production implementation passes the same minimized/occluded idle assertion: exactly the requested test-deadline wake, no GPU polling wake. Scratch negative-control source/binaries remain only under ignored build output.

The first Linux full check went red on the new hostname short-buffer expectation; os_host_name now consistently returns OS_E_RANGE. External debug-info download also exceeded os_test's fault-child guard. Final runs disable that download alone with DEBUGINFOD_URLS empty; local symbolization and all sanitizers remain active. Existing dataloss detached-rewrite failed under parallel load, then passed both the runner's automatic isolated retry and our standalone retry; no data-loss assertion or test limit changed.

Intermediate Windows attempts are preserved. Native TEMP/TMP initially made Linux clang choose an unusable Windows temporary pathname; the final invocation supplies a separate Linux TMPDIR. A fresh native TEMP/profile avoids PID-named stale scratch fixtures and the real user's data. Its path is windows-final-profile.txt. The new profile exposed a missing data-parent creation in Windows directory locking; that is fixed, with journal_ack and related suites passing in the final run. One intermediate check was stopped after becoming obsolete, by exact PID/cwd/arguments and its own descendants only (exit 143); it is not counted as final evidence. Relinking while an earlier test image was still mapped invalidated an intermediate run; immutable copies passed and final build/run dependencies were sequenced. An auxiliary --wait fixture initially selected any child process; it now selects the editor image among that launcher's children, and the final release/sanitizer reruns pass.

The Windows suite explicitly prints `SKIP (needs live desktop: native T4 G1 ...)` and `SKIP (needs live desktop: N edits give N frames)`. Linux editor_bench's existing native-T4 limits are (G) 1000000/2000000 ns for p50/p99, derived in perf/01-perf-target.md section 0.2. CPU submit on an occluded surface cannot establish that endpoint. The unchanged Linux fixture uses 64 samples (from bench/editor_bench.args), below its printed required_n=10000, and is TRACK: null T4 p50/p99=737884/2472445 ns; raster=16568133/25698813 ns; GL's raster fallback=16664561/18026199 ns (M, [AC, desktop Spiral], nearest-rank whole-operation samples). These are descriptive shared-box results, not a Windows G1 verdict. The benchmark's battery helper prints unknown on this battery-less desktop; this report supplies the verified AC stamp.

## Full Windows result and final guard boundary

(M) The complete run contains 126 RESULT lines: 63 release + 63 sanitizer. In each build, 63 - 2 known failures = 61 passes. The only nonzero results are:

```text
RESULT build/win/rel/tests/d3d11_test.exe exit=1
RESULT build/win/rel/tests/win32_plat_test.exe exit=1
RESULT build/win/san/tests/d3d11_test.exe exit=1
RESULT build/win/san/tests/win32_plat_test.exe exit=1
ASan fired: ok
make: *** [mk/win.mk:226: check] Error 1
```

This is make exit 2, matching the brief's known isolation failures. No suite was disabled or assertion weakened. The native profile roots recorded in windows-profile.txt and windows-final-profile.txt are removed after the run (windows-profile-cleanup.log); all measurement/source logs remain in this worktree.

The final counting interval starts **before PostMessage**, so it includes Win32 decoding through editor mutation/layout/journal work and D3D11 CPU submit. It is not reset by on_ingress. The stronger release rerun is full-platform-guard-rel.log, exit 0: 10000 keys and journal records, mallocs=0, guard=active, p50_ns=126100, p99_ns=511000 (M, [AC, desktop Spiral], submit_ns - ingress_ns, inherited N and nearest-rank formula above). The sanitizer counterpart (full-platform-guard-san.log) also exits 0, with p50_ns=439900 and p99_ns=1038000 (M, same derivation and AC stamp); it retains the explicit ASan-inert label and does not replace the release proof. The initial weaker interval and its raw logs remain recorded for audit.

WSL shell output/status was also checked through winrun.sh: --help prints usage and exits 0; an invalid option prints usage and exits 2 (shell-help.log, shell-error.log). llvm-readobj confirms IMAGE_SUBSYSTEM_WINDOWS_GUI; release-imports.txt contains only Windows system imports. No Explorer/shortcut console claim is based on a live launch here; that final visible check belongs to Tobias.

## Environment

All measurements (M), [AC, desktop Spiral]; BAT0 absent, Tobias is using the box. Uptime/load captures: 10:25:49 up 2:00, load 1.05/2.32/2.45; 10:35:35 up 2:10, load 7.95/6.92/4.89; 10:41:59 up 2:16, load 5.81/6.26/5.56; 11:01:47 up 2:36, load 2.78/4.58/5.78; 11:07:47 up 2:42, load 14.24/8.61/6.95; 11:09:16 up 2:43, load 7.77/8.04/6.91; 11:42:07 up 3:16, load 3.89/14.45/13.95. Counts derive from emitted suite summaries or inherited test loops, not estimates.

## Found, not fixed

- Live T4/T6, physical monitor DPI and optical/cadence claims cannot be established on the private station.
- Existing Linux dataloss detached-rewrite is load-sensitive; runner isolated retry passed.
- Sibling-owned Windows platform/render display suites remain outside this worker's scope; they are the only nonzero suite results in the full final run. Their source files and the check recipe were not edited.

check-live: N edits give N frames (paced input), hardware/WARP; real visible T4/T6 and native G1 latency; minimize/restore, session lock/unlock and display power; physical monitor DPI/font scaling; Explorer no-console and shell/WSL --wait behavior.

Only the coordinator, after Tobias authorizes a live run, gives him this exact Windows PowerShell command (the trace file enables frame-count review afterward):

```powershell
$p = Join-Path $env:TEMP ('sublimite-test-live-177r9-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $p | Out-Null
[IO.File]::WriteAllText((Join-Path $p 'one.txt'), "base`n", [Text.UTF8Encoding]::new($false))
$env:SUBLIMITE_PROFILE = $p
$env:EDIT_TRACE_DUMP = Join-Path $p 'trace.bin'
& '\\wsl.localhost\Ubuntu\home\tobiasosborne\Projects\sublimite\.wt\edit-177r.9-s28\build\win\rel\sublimite.exe' --new-instance (Join-Path $p 'one.txt')
```

He should see the native editor displaying one.txt with “base”, with no extra console window; typing including non-ASCII text appears, Ctrl+S persists it, and closing exits normally. Moving between DPI settings scales the font; minimize/session/power resume remains responsive. The coordinator checks native T4/T6 and “N edits give N frames” from paced input plus trace, and lists any live-only failure for follow-up.
