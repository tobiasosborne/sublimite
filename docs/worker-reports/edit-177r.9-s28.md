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
& '\\wsl.localhost\Ubuntu\home\tobiasosborne\Projects\sublimite\.wt\edit-177r.9-s28\build\win-launch\rel\sublimite.exe' --new-instance (Join-Path $p 'one.txt')
```

He should see the native editor displaying one.txt with “base”, with no extra console window; typing including non-ASCII text appears, Ctrl+S persists it, and closing exits normally. Moving between DPI settings scales the font; minimize/session/power resume remains responsive. The coordinator checks native T4/T6 and “N edits give N frames” from paced input plus trace, and lists any live-only failure for follow-up.

## Round 2

Complete. Final verification and evidence are recorded below. This round uses CPUs 16–47, nice 10, make -j32, and only tools/winrun.sh private stations/desktops for Windows execution. All logs are under `build/round2/`. No live desktop execution occurred.

Linux investigation: baseline is `b73edf6^` = `1c0f1ae`, extracted with read-only git archive under build/round2/baseline. The final contention recipe has 16 concurrent ck4-save probes, 32 CPU hogs, and 64 total probes (E: half the 32-CPU lane, one hog per lane CPU, four waves). The recipe further restricts its children to CPU 16, inside the assigned lane, to force contention. Both revisions passed 64/64 natural probes. Initial attempts under the tracing sandbox failed in LeakSanitizer before the assertion and are not counted; the contention comparison disables leak scanning alone. Full Linux make check uses the untraced execution context and retains LeakSanitizer.

Controlled red: `--ck4-slow-save` uses the existing savectl durability hook to wait 2 s (E: two old one-second polling windows) before replacement. Both revisions exit 1 at `!memcmp(saved, "xa", 2)` (`ck4-red.log`, `baseline-slow-red.log`). Green: controller completion, exact saved bytes and exact crash replay pass (`ck4-green.log`). Linux product code is unchanged.

### Changes and red/green evidence

Commands below have the common prefix `taskset -c 16-47 nice -n 10`; make uses `-j32`. Native commands use `WSL_INTEROP=/run/WSL/2_interop tools/winrun.sh` (the inherited endpoint was stale). A scratch native guard kills only its own created child after 3 × the existing editor 5 s watchdog; an observed 124 is a demonstrated hang, not a weakened product deadline. `editor_round2_v*` are immutable copies of successively built test images, to avoid an earlier Windows mapping being reused across a relink. All scratch controls are ignored build output, not release fault switches.

| Item | Red command/result | Green command/result |
|---|---|---|
| 0: D3D merge | `make TARGET=win build/win/rel/tests/d3d11_test.exe`, make exit 2: plat redefinition and invalid window member (`merge-red.log`) | Same target exit 0 (`merge-green.log`); all cases retained using the real plat identity and explicit backend HWND; full isolated pixel/occlusion/resize suites retained |
| 0: Linux recovery | `build/san/tests/editor_test --ck4-slow-save`, exit 1 on both HEAD and `1c0f1ae`, same exact saved-byte assertion (`ck4-red.log`, `baseline-slow-red.log`) | Same selector exit 0 (`ck4-green.log`); current fixed contention 64/64 (`current-green-contention.log`); original natural contention 64/64 on each revision (`current-contention.log`, `baseline-contention.log`) |
| 1: clipboard cut | `editor_win_test.exe --cut`, exit 1: document length changed under forced OpenClipboard failure (`cut-red.log`) | `editor_round2_v6.exe --cut`, exit 0 (`cut-v6.log`): matching failure keeps bytes/selection, success cuts, and close waits for the queued failure then cancels close with a notice |
| 2: minimize | `editor_win_test.exe --minimize`, exit 1 at editor_step after real ShowWindow(SW_MINIMIZE) (`minimize-red.log`) | Full editor campaign exit 0 (`campaign-rel.log`, `campaign-san.log`): real minimize without a draw and with an actual offscreen native draw, edits and named-file save while minimized, restore/re-arm; usable dimensions retained |
| 3: liveness | `editor_round2_v3.exe --retry`, exit 1 on focus retry (`retry-guard.log`); copied-frame latency recovery additionally exits 1 (`latency-red.log`) | Full editor campaign: focus, input, activation, native position change, display-on, an event-free expired probe, same device across a probe, and no probe deadline while truly minimized; final full suite includes the pre-draw recovery case |
| 4: lifecycle pool | Original HEAD backend linked against the new saturation test, `lifecycle_negative.exe --lifecycle`, native watchdog exit 124 (`lifecycle-native-red.log`) | `editor_round2_v3.exe --lifecycle`, exit 0 (`lifecycle-v3-green.log`), plus full campaigns: shutdown/reinitialize while ordinary eligible slots retain background completion messages, and all completions drained |
| 5: blocking wait | `editor_win_test.exe --wait`, exit 1: consumed auto-reset event produces no tag (`wait-red.log`); corrected blocking wait with fallback redispatch removed exits 1 on the full-queue/journal-fence path (`fallback-red.log`) | `editor_round2_v4.exe --fallback`, exit 0 (`fallback-v4.log`), and full campaigns: owner receives the consumed latency credit while native dispatch is blocked |
| 6: profiles | Two profiles without --new-instance: second process has no HWND (`profiles-red.log`, exit 1) | Full executable test: separate simultaneous instances, each profile's own abrupt-exit recovery, independent saved bytes, invalid override fails before handoff; no --new-instance in these launches |
| 7: first paint | Suspension discovered after CPU submit leaves PAINT armed (`paint-red.log`, exit 1) | Refuse one clear through a test-TU wrapper, assert the retained obligation, then clear on the next step; `painted` remains false (`paint-v6.log`, exit 0). Known unavailable CPU submissions never transiently arm a native-paint guard |
| 7: stdin | Attachment seam with disconnected CRT stdin exits 1 (`console-red.log`) | `console_round2_v2.exe`, exit 0 (`console-green.log`): CONIN$ repair, real inherited pipe and real file descriptors preserved; physical console interaction explicitly skipped |
| 7: captured output | Real main with only its usage-output calls removed preserves exit statuses but fails captured text assertions, exit 1 (`streams-red.log`) | Production captured help/error text passes as part of the executable suite; its streams PASS now depends on actual output |
| 7: release hook | Accepted design choice, no behavioral fix requested | `SUBLIMITE_TEST_INPUT` remains opt-in in release, explicitly recorded in the decision document |

The full Windows passes also exposed two fixture mechanisms, corrected without relaxing their byte or resource assertions. The platform modal producer posts 257 resize events (G: its existing ring plus one); adding an expose for each resize doubled the callback count and overflowed its 512-event test recorder. Only minimize transitions now add that expose, while resize itself is a retry trigger. Native session/power registration can create a fixed pair of process-wide service handles after the first HWND teardown: the old test measured 130 → 132 handles. It now warms the exact existing four-lifecycle fixture before measuring another full four lifecycles; the unchanged no-growth assertion passes at 132 → 132 (release) and 130 → 130 (sanitizer), `platform-handle-rel.log` / `platform-handle-san.log`.

The sanitizer executable crash oracle initially killed during a new first-paint attempt started by the parked-state probe (`windows-check-final.log`, saved bytes lacked `!`). It now genuinely minimizes its owned HWND before the protected edit and kill, so this hot-exit oracle runs outside native paint. A distinct protected marker after relaunch, followed by undo, establishes that the recovered tab is editable before save. The exact final bytes remain asserted. Both release and sanitizer pass (`recovery-rel.log`, `recovery-san.log`); the expanded two-profile version separately recovers `@other\n` while the first profile remains alive and unchanged (`two-profiles-san.log`).

### Linux and measurement boundary

Linux production behavior remains unchanged. Windows fields and clipboard publication semantics are conditional; the X11 owned-selection path and epoll retain their semantics. The Windows-only admission retry loop caused the lifecycle deadlock; Linux shutdown remains synchronous, and its init admission still fails promptly if no ordinary slot is available. Found, not fixed: changing Linux XDG_DATA_HOME can select different journal storage while retaining the same display/runtime IPC namespace. SUBLIMITE_PROFILE itself is Windows-only. This is the analogous pre-existing Linux profile hole; it was not changed in this round.

(M), [AC, desktop Spiral], Tobias using the box; BAT0 absent. Recorded loads: 12:02:38 3.13/1.69/5.68; 12:14:01 7.36/4.23/5.07; 12:21:03 10.66/5.52/5.01; 12:26:44 0.41/2.55/3.93; 12:32:03 0.35/2.22/3.53; 12:38:10 0.21/1.22/2.71; 12:42:46 8.52/3.71/3.20. The measurement logs print endpoint/count derivations. Final full-suite allocator/typing figures and check summary are appended below.

Existing Linux release allocation_case: 10,000 edits (G: inherited campaign) with mallocs=0, guard=active (`linux-allocator.log`). Existing editor_bench arguments are unchanged: `--track --no-idle --no-p4 --serial-only --keys=64`; private Runner-owned Xvfb, exit 0 (`linux-editor-bench.log`). (M) Native T4 p50/p99 in ns: null 702280/875969; raster 16739086/18008140; GL raster fallback 16710850/17919685. These are nearest-rank whole-operation samples at n=64, below its printed required_n=10000, and therefore TRACK; structural mutation/journal/allocation assertions pass. The bench prints power unknown; the verified hardware stamp here is AC, desktop Spiral. The existing native-T4 (G) limits remain 1000000/2000000 ns, derived in perf/01-perf-target.md §0.2. Private-station CPU-submit percentiles cannot establish those native/display endpoints.

Updated check-live: visible uncover without WM_PAINT on a flip chain; activation/focus/dragging and an event-free parked probe; hardware/WARP visible T4/T5/T6 and native G1; paced “N edits give N frames”; real minimize/restore, session lock/disconnect and display power; physical multi-monitor DPI; physical console stdin and inherited redirects; Explorer no-console and shell/WSL --wait behavior; actual German AltGr/dead-key input. A PowerShell console launcher remains coordinator-owned and outside this round. Only the coordinator, with Tobias's authorization, runs these live checks.

The complete `windows-check-green.log` pass already returned 0 (130 binaries, 6 desktop passes, 8 suite groups, 427.481 s, failures=0; M, [AC, desktop Spiral]). A final ownership probe then reproduced one more lifecycle defect: the worker received the UI's mutable backend descriptor while completion draining could inspect it (`lifecycle-ownership-red.log`, exit 1 at `atomic_load(&lifecycle_exclusive)`). Shutdown now retires a worker-owned descriptor copy after UI publishes unavailability; physical completion returns the retired copy to UI. This remains entirely Windows-conditional and uses the existing work synchronization. That intermediate ownership rerun is `windows-check-verified.log`; the final Windows result is `windows-check-launch.log`, and the final Linux result is `linux-{all,check}-verified.log`.

The executable fixture had another deterministic initial-state bug. With its owned thread's Ctrl state deliberately set before the original posted-text helper, the file stayed `base\n` rather than the expected Unicode text (`input-state-red.log`, exit 1). Plain WM_CHAR injection must establish its intended modifier state, just as the existing Ctrl+S hook does. `post_character` now explicitly sets zero modifiers; the same held-Ctrl control passes (`input-state-green.log`, exit 0), and the normal full campaign deliberately includes that initial Ctrl state. No production keyboard behavior changed. This also applies to the isolated crash markers.

The first final ownership pass (`windows-check-verified.log`) hit the existing journal_kill_test ten-second child-progress guard during concurrent Linux and Windows I/O. No replay assertion was relaxed. Final verification waits until the Linux check has completed and uses WIN_CHECK_JOBS=4 (E: half the existing eight suite groups, halving this worker's concurrent suite I/O bursts); make still uses -j32 and all trials/selectors remain enabled. The final native output tree is `build/win-launch`, giving Windows fresh image paths after earlier mappings; the command and full summary are recorded below. Intermediate red full-check logs remain preserved rather than hidden by automatic retries.

### Final verification — complete

Files: `src/editor/{backend,clip,editor,open,wait_win}.c`, `src/editor/{private,clip_storage}.h`; `src/win32/{private.h,win32_clip_win.c,win32_plat.c}`; `src/d3d11/{d3d11.h,d3d11_win.c}`; `src/x11/plat.h` (Windows-only publication identity field); `src/main.c`; `mk/win.mk`; `tests/editor_test.c`; `tests/win/{d3d11_test,editor_win_test,editor_exe_test,editor_console_test,win32_plat_test}.c`; this report and `docs/decisions/edit-177r.9.md`.

| Verification command, with affinity/nice prefix above | Exit | Evidence |
|---|---:|---|
| `make -j32 all` | 0 | `linux-all-verified.log` |
| `DEBUGINFOD_URLS= make -j32 check` | 0 | `linux-check-verified.log`: 117 binaries / 121 tasks, no FLAKY; all CLI/display-lock contracts pass, ASan/UBSan/LeakSanitizer retained |
| `make TARGET=win -j32 all` | 0 | `windows-all-verified.log`, final default output builds |
| `make TARGET=win WIN_B=build/win-launch WIN_CHECK_JOBS=4 -j32 check` | 0 | `windows-check-launch.log`: every release/sanitizer suite and desktop pass; win32_plat_test and d3d11_test pass in both configurations and both applicable isolation modes |
| Linux allocator / editor bench | 0 / 0 | `linux-allocator.log`, `linux-editor-bench.log` |
| Windows inherited-modifier negative/positive control | 1 / 0 | `input-state-red.log`, `input-state-green.log` |
| Final native TEMP/profile cleanup | 0 | `native-cleanup.log`: exact owned root removed; absence verified |
| `git diff --check` | 0 | source/docs checked after report completion |

The output override only selects fresh immutable image paths under this worktree's build directory; it does not change any suite selector or assertion. The coordinator's isolated/live artifact for this run is `build/win-launch/rel/sublimite.exe`. The full Windows check line, verbatim (M, [AC, desktop Spiral]; duration is the runner's monotonic elapsed time, counts come from its emitted results):

```text
Windows check: binaries=130 desktop-passes=6 jobs=4 seconds=615.149 failures=0; SKIP cases=162: AddressSanitizer owns malloc=1; Linux SIGBUS signal-action chaining and real truncation: native Windows faults use the OS fault trigger; NTFS refuses mapped truncation=2; Linux syscall interposition; real-file integration awaits W2.1/W2.2=2; POSIX FIFO, mode/uid/xattr/ACL and syscall-interposition cases: Windows metadata and native errors exercised above=2; POSIX chmod unreadable directory (Windows uses ACLs)=2; clipboard: needs private station=16; editor panel clipboard/chord integration (editor not in Windows build)=2; editor-dependent group/retire/session/recovery/dataloss integration and first-paint closed-tab restart (editor/platform not ported)=1; hard-link/symlink fixtures: frozen os.h has no link-creation API=2; hl_worker faulted_snapshot (requires W2 mapped file/fault integration)=2; installed layouts are UK/US/German=4; journal/findui integration and editor-dependent cases: those modules await their Windows beads=2; layouts: needs a station with input=8; libxkbcommon differential name oracle (Linux-only library); binding expectations unchanged=2; lineidx combined_ownership (file-backed integration requires W2 file port)=2; name=G6_dense_a file=C:\Users\tobia\AppData\Local\Temp\sublimite-test-round2-87536\temp/sublimite-test-find-bench-4d10a26105d0d3b1 (quick: optional all-a fixture absent; full mode requires it)=1; name=G6_dense_a file=C:\Users\tobia\AppData\Local\Temp\sublimite-test-round2-87536\temp/sublimite-test-find-bench-6207700519d471ac (quick: optional all-a fixture absent; full mode requires it)=1; name=G6c file=C:\Users\tobia\AppData\Local\Temp\sublimite-test-round2-87536\temp/sublimite-test-find-bench-4d10a26105d0d3b1 (quick: optional all-a fixture absent; full mode requires it)=1; name=G6c file=C:\Users\tobia\AppData\Local\Temp\sublimite-test-round2-87536\temp/sublimite-test-find-bench-6207700519d471ac (quick: optional all-a fixture absent; full mode requires it)=1; name=G6v_a31b file=C:\Users\tobia\AppData\Local\Temp\sublimite-test-round2-87536\temp/sublimite-test-find-bench-4d10a26105d0d3b1 (quick: optional all-a fixture absent; full mode requires it)=1; name=G6v_a31b file=C:\Users\tobia\AppData\Local\Temp\sublimite-test-round2-87536\temp/sublimite-test-find-bench-6207700519d471ac (quick: optional all-a fixture absent; full mode requires it)=1; name=G6v_first_last_early file=C:\Users\tobia\AppData\Local\Temp\sublimite-test-round2-87536\temp/sublimite-test-find-bench-4d10a26105d0d3b1 (quick: optional all-a fixture absent; full mode requires it)=1; name=G6v_first_last_early file=C:\Users\tobia\AppData\Local\Temp\sublimite-test-round2-87536\temp/sublimite-test-find-bench-6207700519d471ac (quick: optional all-a fixture absent; full mode requires it)=1; name=G6v_first_last_middle file=C:\Users\tobia\AppData\Local\Temp\sublimite-test-round2-87536\temp/sublimite-test-find-bench-4d10a26105d0d3b1 (quick: optional all-a fixture absent; full mode requires it)=1; name=G6v_first_last_middle file=C:\Users\tobia\AppData\Local\Temp\sublimite-test-round2-87536\temp/sublimite-test-find-bench-6207700519d471ac (quick: optional all-a fixture absent; full mode requires it)=1; needs live desktop: N edits give N frames=2; needs live desktop: committed presentation=4; needs live desktop: diagnostic Present validates timing=4; needs live desktop: displayed frame completion=60; needs live desktop: foreground Unicode input=4; needs live desktop: foreground native input=4; needs live desktop: hardware presentation=4; needs live desktop: native T4 G1; Linux limits (G=2; needs live desktop: physical console input and shell attachment=2; needs live desktop: visible minimize and restore=4; requires Windows main/platform (W2.4)=2; sanitizer owns allocator=2; sanitizer owns malloc=1; src/file save integration (file module not ported); journal_save_prepare/finish/cancel run in journal_safety_test=1; trace_pairs Python cases (SUBLIMITE_TEST_PYTHON native interpreter path unset)=2; work_foreground editor jump (requires Windows editor/platform port)=2; work_foreground real clients (8 cases require W2 savectl/findui/font discovery)=2
```

Final full-suite typing / allocator measurements (M, [AC, desktop Spiral]; 13:01:19 load 0.25/1.39/2.79; desktop in use):

```text
editor_win: (M)[AC, desktop Spiral] keys=10000 journal_records=keys mallocs=0 guard=active p50_ns=120100 p99_ns=229900 derivation=submit_ns-ingress_ns; nearest-rank=ceil(N*pct/100); N=Linux allocation_case; surface=occluded (CPU submit, no T4/T6); Linux has no percentile G1 assertion
editor_win: (M)[AC, desktop Spiral] keys=10000 journal_records=keys mallocs=0 guard=ASan-inert p50_ns=413100 p99_ns=564000 derivation=submit_ns-ingress_ns; nearest-rank=ceil(N*pct/100); N=Linux allocation_case; surface=occluded (CPU submit, no T4/T6); Linux has no percentile G1 assertion
```

Nearest-rank p50/p99 use index `ceil(N * percentile / 100) - 1`, with N=10000 inherited from Linux allocation_case; elapsed sample = CPU submit timestamp − native ingress timestamp. The release guard is active, observes its allocation positive control and counts zero allocations. ASan's inert guard is labeled and supplies no replacement allocator proof. Neither private-station number claims native T4/T6 or displayed cadence.

Final verdict on editor_test.part-3: **test I/O-deadline bug**. The exact failure is reproduced on both HEAD and before W2.4c with a legitimately delayed save; controller completion preserves exact disk and crash-replay assertions. Natural contention passed 64/64 on each revision; fixed contention passed 64/64. No Linux product behavior changed.

Coordinator: authorize/perform only the check-live list above when Tobias is ready; separately track the existing Linux XDG profile/IPC namespace gap and the already deferred PowerShell console launcher. No live execution or state-changing git/bd command occurred in this round.
