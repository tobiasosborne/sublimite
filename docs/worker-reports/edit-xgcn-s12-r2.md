# edit-xgcn round 2 worker report

Verdict: **done**. Triaged findings 1–8 addressed; final Windows check and Linux release/full sanitizer check exit 0.

Scope: triaged review findings 1–8. No changes to frozen `src/render/render.h`, `src/os/os.h`, the common adapter, Linux implementations, or the main checkout. No `bd` or state-changing git commands. The existing untracked `docs/worker-reports/edit-xgcn-review-final.md` was preserved.

## Changes

- `src/d3d11/d3d11_win.c`: normalize disabled scroll, bound snapshot indices, copy only referenced immutable atlas rectangles, upload private-cache damage, validate ordinary timing on the init worker, defer background timing capability until real validation, run DWM synchronization on an init-reserved private worker lane, distinguish recoverable presentation observation loss from native device failure, repair vacated RGB region bounds, and flush after dependent-resource release.
- `src/d3d11/d3d11.h`: preserve the window-binding signature; document HWND/message-pump/bootstrap/resize ownership and expose UI suspension plus rebuild-required observation.
- `tests/win/d3d11_test.c`: hardware/WARP, ordinary/background init, failed native init retry, concurrent disjoint font append, immediate metadata reuse, post-T5 referenced coverage replacement, independent region transitions, minimize/restore, and same-HWND replacement with old diagnostic device interfaces retained.
- `tests/win/d3d11_win_test.c`: C-vtable/DWM shim tests for the fixes, compiled with the backend directly and without production indirection.
- `mk/win.mk`: link the system DWM API and build the white-box suite without a duplicate backend object.
- `docs/decisions/edit-xgcn.md`: appended the Round 2 finding → change → test table and W1.1 integration protocol.

The atlas cache never reads an unreferenced caller page byte. Its unique-glyph bitmap and upload-row metadata are reserved during init. The concurrent writer test changes only unpublished columns; cache canaries additionally catch forbidden copying that ASan alone cannot diagnose as a data race. Real presented pixels match the scalar oracle exactly.

Ordinary init presents a rendered diagnostic buffer, waits for its actual event query and matching present count on the worker, and rolls back if unsupported. Background init does not present diagnostics and initially withholds `RENDER_CAP_PRESENT_TIMING`; the first exact real-frame observation enables it. On this desktop both hardware and WARP validate in both modes.

The private completion lane runs `DwmFlush`, publishes the application generation/native count/status through `src/work`, and lets UI check the exact DXGI record. Latency readiness, DWM completion and SyncQPCTime never stand in for T6. UI calls remain nonblocking and application-allocation-free. The private lane sleeps when idle and joins before native references or its arena are freed.

A healthy chain that loses an observable frame requests `RENDER_ERR_UNSUPPORTED` with `d3d11_recovery_required=true`; it preserves the adapter's active slot and never invents T6. Known suspension/minimization pauses observation and long UI absence rebases it. Continuously observed missing latency/query/statistics or blocked synchronization is bounded by the inherited GL completion policy. W1.1 must remove borrowed wait handles, stop input, shut down quiescently, re-init on a worker while pumping, and fully repaint a fresh lifetime. Native device failure still returns DEVICE. The common adapter needs no change: its existing error-retains-slot and shutdown semantics implement this protocol.

## Measurement context

All measured results here are (M), [AC, desktop Spiral]; BAT0 is absent. Fixture sizes and frame IDs are test data, not performance budgets. Load snapshots:

```text
start: 12:00:44 up 4:17, load average: 3.28, 4.55, 3.05
Linux/full Windows verification: 12:21:22 up 4:37, load average: 7.36, 6.51, 5.08
12:30:34 up  4:46,  1 user,  load average: 6.09, 7.27, 6.30
```

The only completion duration retained is (G) `D3D11_COMPLETION_NS = 3,000,000,000 ns`, derived by retaining the pre-existing GL completion policy, not a new performance target. The private lane has one worker (P: one active adapter frame, at most one synchronization job). Each native summary reports 15 completed frames (M: seven original fixtures + six region transitions + one minimize/coverage fixture + one same-HWND re-init fixture); pending shutdown presents one additional frame without delivering its discarded hooks. There are four native modes per binary (P: hardware/WARP × background/ordinary).

## Red-first evidence

Each finding's new regression was written and run before its production fix. Earlier fixes were already present during the ordered red runs; the offending path for the next finding was still the round-1 implementation. The saved unmodified backend and original native suite are in `build/r2/round1-d3d11_win.c` and `build/r2/round1-d3d11_test.c`. Afterward, the isolated cases below were also compiled directly against that saved backend, excluding the current backend object, to prove the red results independently of earlier fixes.

Build command for the ordered white-box red/green runs:

```sh
make TARGET=win build/win/san/tests/d3d11_win_test.exe
# exit 0 for each behavior run below
```

The unchanged-round-1 replay uses `build/r2/round1-regressions.c`, compiled with the same C11 warning/sanitizer/link flags as the white-box make rule. Its only compatibility adjustment omits the new query-ready snapshot field; the tested expectations are unchanged. Commands and relevant results:

Finding 1:

```text
tools/winrun.sh build/win/san/tests/d3d11_round1_regressions.exe scroll
exit 1
==221304==ERROR: AddressSanitizer: heap-buffer-overflow on address 0x11fac1ca0080 at pc 0x7ff6b07e788a bp 0x0073612fda20 sp 0x0073612fda68
```

Finding 2:

```text
tools/winrun.sh build/win/san/tests/d3d11_round1_regressions.exe atlas
exit 1
d3d11_win_test:79: FAIL s.atlas_cache[2] == 0 && s.atlas_cache[3] == 0
```

Finding 3:

```text
tools/winrun.sh build/win/san/tests/d3d11_round1_regressions.exe caps
exit 1
d3d11_win_test:91: FAIL !(b.info.capabilities & RENDER_CAP_PRESENT_TIMING)
```

Finding 4:

```text
tools/winrun.sh build/win/san/tests/d3d11_round1_regressions.exe progress
exit 1
d3d11_win_test:125: FAIL !b.active && hooks == 2
```

Finding 5:

```text
tools/winrun.sh build/win/san/tests/d3d11_round1_regressions.exe retry
exit 1
d3d11_win_test:137: FAIL d3d11_present(&b,1) == RENDER_ERR_BUSY
```

Finding 7:

```text
tools/winrun.sh build/win/san/tests/d3d11_round1_regressions.exe release
exit 1
d3d11_win_test:166: FAIL chain_ordered
```

Finding 1's sanitizer also reports `READ of size 2` beyond the exactly allocated cell array. The test covers full/partial/zero damage, unused nonzero fields, and disabled INT64_MIN without overscan storage.

Finding 3's real-window negative test against the unchanged round-1 backend (ordinary init while hidden, after native resources have been created):

```text
tools/winrun.sh build/win/san/tests/d3d11_round1_init.exe --warp
exit 1
d3d11_test:244: FAIL init.rc == RENDER_ERR_UNSUPPORTED && !b.initialized && b.state == NULL
```

Finding 6's scalar pixel test against the unchanged round-1 backend:

```text
tools/winrun.sh build/win/san/tests/d3d11_round1_region.exe --warp
exit 1
frame=8 pixel=5,7 got=ff757676 want=ffabcdef
d3d11_test:307: FAIL frame(&b,&g,&p,want,122,51,false) == 0
```

Finding 8 is a documentation correction. The header obligation check iterates the required strings `physical worker completion`, `WM_SIZE`, `bootstrap`, `pumping` and asserts their presence in `src/d3d11/d3d11.h`:

```text
python3 <header obligation check>
exit 1
AssertionError: missing HWND obligation: physical worker completion
# After documenting the binding obligations:
exit 0
HWND lifetime/pump/resize/bootstrap obligations PASS
```

Ordered originals are also retained as `build/r2/f1-red.log` through `f8-red.log`; green counterparts exist for each finding (native capability evidence is in `native-lifecycle.log`).

Additional red/green fixes discovered while validating the required init/recovery cases:

```text
# Every command below runs:
tools/winrun.sh build/win/san/tests/d3d11_win_test.exe
# query-red.log, exit 1:
FAIL d3d11_present(&b,41) == RENDER_OK && present_committed && s.swapped
# query-green.log, exit 0:
pending GPU query cannot prevent native Present progress PASS
# resume-red.log, exit 1:
FAIL !s.resume_pending
# resume-green.log, exit 0: post-resume commit clears obsolete recovery state
# recovery-red.log, exit 1:
FAIL rc == RENDER_ERR_UNSUPPORTED && d3d11_recovery_required(&b) && !s.failed
# recovery-green.log, exit 0:
missing statistics/blocked progress request rebuild, no fabricated T6 PASS
# readiness-red.log, exit 1: same recovery assertion for unready latency
# readiness-green.log, exit 0:
continuously observed missing latency requests healthy-device rebuild PASS
```

The query regression was first observed on real hardware in ordinary mode: frame 5 stayed BUSY with GetData=S_FALSE, visible HWND, no removal reason and no debug warning (`ordinary-isolated.log`). A C-vtable query that becomes ready only after native Present pins that dependency. The fix permits native Present progress and delivers T5 only after both actual query readiness and T4, following the frozen contract's max(device completion,T4) rule. It does not report Present readiness as device completion. All hardware/WARP ordinary fixtures then passed (`native-modes-green.log`, `native-lifecycle.log`). This replaces the round-1 decision's unnecessarily strict pre-Present query polling order; it changes neither frozen header.

## Green verification

Final white-box sanitizer run:

```text
tools/winrun.sh build/win/san/tests/d3d11_win_test.exe
exit 0
d3d11_win_test: disabled-scroll exact-capacity full/partial/zero INT64_MIN PASS
d3d11_win_test: concurrent disjoint atlas append/cache canaries PASS
d3d11_win_test: present timing withheld until validated PASS
d3d11_win_test: stale statistics require worker DwmFlush, original ID hooks once PASS
d3d11_win_test: delayed BUSY retry and pre-Present poll preserve healthy device PASS
d3d11_win_test: release dependent resources, flush, release swapchain PASS
d3d11_win_test: pending GPU query cannot prevent native Present progress PASS
d3d11_win_test: missing statistics/blocked progress request rebuild, no fabricated T6 PASS
d3d11_win_test: continuously observed missing latency requests healthy-device rebuild PASS
```

Final Windows command (WSL interop outside the sandbox):

```sh
make TARGET=win check
# exit 0
```

Literal D3D11 summary lines, release first then sanitizer:

```text
d3d11_test: hardware pixels=exact frames=15 T5/T6=15/15 monotone UI resize shutdown-pending PASS T6=DXGI-statistics:15 debug=enabled, no live children guard=zero allocations init=background present-timing=validated
d3d11_test: WARP pixels=exact frames=15 T5/T6=15/15 monotone UI resize shutdown-pending PASS T6=DXGI-statistics:15 debug=enabled, no live children guard=zero allocations init=background present-timing=validated
d3d11_test: hardware pixels=exact frames=15 T5/T6=15/15 monotone UI resize shutdown-pending PASS T6=DXGI-statistics:15 debug=enabled, no live children guard=zero allocations init=ordinary present-timing=validated
d3d11_test: WARP pixels=exact frames=15 T5/T6=15/15 monotone UI resize shutdown-pending PASS T6=DXGI-statistics:15 debug=enabled, no live children guard=zero allocations init=ordinary present-timing=validated
d3d11_test: hardware pixels=exact frames=15 T5/T6=15/15 monotone UI resize shutdown-pending PASS T6=DXGI-statistics:15 debug=enabled, no live children guard=sanitizer owns malloc init=background present-timing=validated
d3d11_test: WARP pixels=exact frames=15 T5/T6=15/15 monotone UI resize shutdown-pending PASS T6=DXGI-statistics:15 debug=enabled, no live children guard=sanitizer owns malloc init=background present-timing=validated
d3d11_test: hardware pixels=exact frames=15 T5/T6=15/15 monotone UI resize shutdown-pending PASS T6=DXGI-statistics:15 debug=enabled, no live children guard=sanitizer owns malloc init=ordinary present-timing=validated
d3d11_test: WARP pixels=exact frames=15 T5/T6=15/15 monotone UI resize shutdown-pending PASS T6=DXGI-statistics:15 debug=enabled, no live children guard=sanitizer owns malloc init=ordinary present-timing=validated
```

`d3d11_win_test` also passed every case in both builds. Other final Windows summaries:

```text
os_win_test: 18/19 passed, 1 unavailable
os_test: 17 cases, 0 failed
ASan fired: ok
```

The OS summaries occur in both release and sanitizer sections (M); the single unavailable fixture is the existing symlink privilege case. `ASan fired` is the suite's required positive control.

Linux release:

```sh
make -j64 all
# exit 0 (initial and final Windows-only source state)
```

Full Linux sanitizer check, with LeakSanitizer enabled:

```sh
DISPLAY=:96 EDIT_DISPLAY_LOCK_TIMEOUT=0 make -j64 check
# exit 0
```

```text
check: 104 test binaries passed
test_replay_cli: all passed
display lock contract: owner PID, alias/timeout, make/scripts, nested inheritance, exit status PASS
```

The measured binary count is the Makefile's expanded `tests/*_test.c` list (M: 104). Full log: `build/r2/linux-check.log`. LeakSanitizer was never disabled. The sandbox's first run stopped at base_test because LSan cannot inspect under ptrace; the successful full run used the required sandbox exception. The final display-lock contract deliberately holds its own lock and verifies rejected attempts, so its nested timeout/Error messages are expected positive controls; the parent command exits 0. All actual display suites used :96.

Final checks: `git diff --check` exits 0; frozen render/OS headers and common adapter have no diff. Final load (M), [AC, desktop Spiral]: `12:38:13 up 4:54, load average: 2.62, 4.89, 5.66`.


## Found, not fixed / coordinator handoff

- Review findings 9 and 10 remain backlog as triaged. This round adds focused acquisition/progress/lifecycle coverage needed for findings 1–8; it does not claim exhaustive native-acquisition injection, diagnostic retention, counter-wrap handling, DXGI object census or Windows platform integration tests.
- W1.1 must wire the documented session/power/suspension and recovery calls, defer HWND destruction during init, pump through worker completion, and release any bootstrap flip chain before this initial init. GDI/CPU bootstrap is supported. Close-during-init and actual lock/unlock integration belong to that platform bead; this worker did not lock Tobias's desktop. Real minimization and synthetic long absence/missing-record/blocked-progress paths were exercised.
- Initial native runs returned all-zero DXGI records even with DWM synchronization; later hardware/WARP runs produced exact records. No unsupported timing was accepted as a fabricated completion. The root display-state cause of the transient empty records was not established.
- The first full Windows run failed outside this bead: OS reparse fixture `wof.txt` disappeared (`OS_E_NOENT`, native 3). The helper uses a fixed shared TEMP directory, so collision with another worker is an inference, not a proven cause. The complete failure log is `build/r2/windows-check-first.log`; the next full run passed. No OS/fixture helper was modified.
- Windows `os_win_test` reports its existing file-symlink fixture unavailable without Developer Mode/elevation. It is an explicit suite UNAVAILABLE result, not a D3D11 skip.

No changes outside this worktree; no new coordinator design decision is required for the backend fix. The W1.1 ownership/recovery handoff above remains integration work.
