# edit-cxho.7 — parked input and busy tab commands

Implemented in this worktree only. No `bd` or `git` commands, installation,
profile changes, coordinator-document edits, refactors or additional review pass.

Evidence notation: execution output below is raw (M)[AC]. BAT0 reported
`Not charging`, later `Charging`; `/sys/class/power_supply/AC/online` reported
`1` (M)[AC]. Error
and action identifiers derive from the existing source enums (P); exit codes
are recorded process results (M)[AC]. The reproduction burst is 1100 events
(E: brief), exceeding `EDITOR_INPUT_CAP` = 1024 (P: existing constant), so
1100 - 1024 = 76 excess keys (P). Queue size one means the sole coalesced event
(P); zero allocations/errors means their absence (M)[AC]. Geometry and existing
watchdogs are reused, with their derivations noted beside the test code. Display
`:99`, the parallel build bound and toolchain versions come from the brief (E).

## Files touched

- `src/editor/editor.c`: pointer-run coalescing, saturating wheel sums,
  non-fatal overload with rejection accounting, and notice publication after
  retained type-ahead mutations.
- `src/editor/buffers.c`: counted successful no-ops for busy tab commands,
  including refusal before a modified-tab close prompt can open.
- `src/editor/private.h`: private flag retaining an overload notice until the
  accepted queue drains.
- `src/editor/editor.h`: document the injection/overload contract.
- `tests/editor_test.c`: pending-open and active replace-all floods for MOTION,
  WHEEL and keys; every implemented tab action under activation busy; Ctrl+W
  through the input loop; visible notice and retirement; ring wrap, ordering
  barriers and wheel saturation. Update the existing replay-busy assertions to
  the new successful-no-op contract and assert their rejection counts.
- `docs/decisions/edit-cxho.7.md`: design choices and numeric derivations.
- This report. Verification logs are under `build/cxho7-s24/`.

## Reproduction and chosen behavior

The queue-full return reproduced before changing production code in every
requested flood. The test holds an actual pending COPY open using the existing
bulk-worker gate, or starts an actual replace-all and injects the burst before
its next continuation. The pending fixture is sparse, sized from
`FILE_PREFIX_MAX` plus the literal remainder (P), and removed after each case.
No new gigabyte fixture was introduced.

The direct injection tests observed `EDITOR_ERR_INPUT_FULL`; the native
callback's conversion of that return into `e->error` is established by the
existing `platform_event` code. No manual slow-open mouse crash was claimed.
The busy Ctrl+W test reproduced a latched fatal error through `editor_step`.
Its white-box go-to-continuation hold isolates `editor_activation_busy` without
depending on file speed. All implemented close, switch, reopen and new-file
actions were also exercised through the shared command dispatcher.

Chosen Sublime-like overload policy: keep accepted keys in FIFO order, discard
excess incoming input, increment `rejected_commands`, and render
`input queue full: excess input ignored`. The tests assert the exact excess-key
count, retained text, visible status cells, and retirement on the next mutation.
This is a chosen policy, not a claim to have measured Sublime's overflow limit.
Pointer runs retain the latest MOTION and summed WHEEL deltas, without crossing
key/button/control barriers or merging incompatible owner/generation/state.
Wheel arithmetic saturates at its representable signed bounds (P).

New-file commands did not reproduce an error: they already defer until
activation is available. Preserve that behavior. Internal buffer-creation and
activation error contracts remain unchanged.

## Red run, before implementation

Built with `make -j4 build/san/tests/editor_test build/tests/editor_test`, then
ran each selector using `ASAN_OPTIONS=detect_leaks=1`, a fresh
`XDG_RUNTIME_DIR`, `DISPLAY=:99`, and `tools/with_display_lock.sh`. These
null-backend regressions do not require a native window. Each case exited with
the failing status shown below (M)[AC].

```text
Not charging
 13:39:19 up 1 day, 15:42,  1 user,  load average: 0.95, 1.09, 1.40
edit-cxho.7 pending-open MOTION: inject_rc=-102 error=0 queued=1024 preserved=no running=yes mallocs=0
editor_test:7338: FAIL !inject_rc && preserved && alive && !running && !allocations
open-motion exit=1
edit-cxho.7 pending-open WHEEL: inject_rc=-102 error=0 queued=1024 preserved=no running=yes mallocs=0
editor_test:7338: FAIL !inject_rc && preserved && alive && !running && !allocations
open-wheel exit=1
edit-cxho.7 pending-open KEY: inject_rc=-102 error=0 queued=1024 preserved=no running=yes mallocs=0
editor_test:7338: FAIL !inject_rc && preserved && alive && !running && !allocations
open-key exit=1
edit-cxho.7 replace-all MOTION: inject_rc=-102 error=0 queued=1024 preserved=no running=yes mallocs=0
editor_test:7338: FAIL !inject_rc && preserved && alive && !running && !allocations
replace-motion exit=1
edit-cxho.7 replace-all WHEEL: inject_rc=-102 error=0 queued=1024 preserved=no running=yes mallocs=0
editor_test:7338: FAIL !inject_rc && preserved && alive && !running && !allocations
replace-wheel exit=1
edit-cxho.7 replace-all KEY: inject_rc=-102 error=0 queued=1024 preserved=no running=yes mallocs=0
editor_test:7338: FAIL !inject_rc && preserved && alive && !running && !allocations
replace-key exit=1
edit-cxho.7 busy tab action=154: rc=-100 error=0
edit-cxho.7 busy tab action=153: rc=-100 error=0
edit-cxho.7 busy tab action=161: rc=-100 error=0
edit-cxho.7 busy tab action=162: rc=-100 error=0
edit-cxho.7 busy tab action=159: rc=-100 error=0
edit-cxho.7 busy tab action=160: rc=-100 error=0
edit-cxho.7 busy tab action=111: rc=-100 error=0
edit-cxho.7 busy tab action=112: rc=-100 error=0
edit-cxho.7 busy tab action=148: rc=-100 error=0
edit-cxho.7 busy tab action=150: rc=0 error=0
editor_test:7377: FAIL passed
tabs exit=1
edit-cxho.7 busy Ctrl+W through input loop: rc=-100 error=-100 rejected=0
editor_test:7396: FAIL alive
tab-loop exit=1
Not charging
 13:39:20 up 1 day, 15:42,  1 user,  load average: 1.04, 1.10, 1.41
```

The initial complete sanitizer editor run then exposed the existing replay test
that required busy UI calls to fail. Its failure was
`editor_test:2222: FAIL editor_select_tab(e, 1) == EDITOR_ERR_ARG` (M)[AC], exit
`1` (M)[AC]. Updated that expectation and its neighboring tab assertions to
counted successful no-ops; non-UI operations still assert their original errors.
The subsequent complete suite passes in the full check.

## Green run

Focused selectors use `--cxho7-` plus the case names printed below. The pointer
ordering case was added to check the implemented run compaction and integer
limits. All outputs are (M)[AC]; its sequence lengths derive from its explicit
fixture events (P).

```text
Not charging
 13:42:18 up 1 day, 15:45,  1 user,  load average: 1.37, 1.16, 1.37
edit-cxho.7 pending-open MOTION: inject_rc=0 error=0 queued=1 preserved=yes running=yes mallocs=0
open-motion exit=0
edit-cxho.7 pending-open WHEEL: inject_rc=0 error=0 queued=1 preserved=yes running=yes mallocs=0
open-wheel exit=0
edit-cxho.7 pending-open KEY: inject_rc=0 error=0 queued=1024 preserved=yes running=yes mallocs=0
open-key exit=0
edit-cxho.7 replace-all MOTION: inject_rc=0 error=0 queued=1 preserved=yes running=yes mallocs=0
replace-motion exit=0
edit-cxho.7 replace-all WHEEL: inject_rc=0 error=0 queued=1 preserved=yes running=yes mallocs=0
replace-wheel exit=0
edit-cxho.7 replace-all KEY: inject_rc=0 error=0 queued=1024 preserved=yes running=yes mallocs=0
replace-key exit=0
edit-cxho.7 busy tab action=154: rc=0 error=0
edit-cxho.7 busy tab action=153: rc=0 error=0
edit-cxho.7 busy tab action=161: rc=0 error=0
edit-cxho.7 busy tab action=162: rc=0 error=0
edit-cxho.7 busy tab action=159: rc=0 error=0
edit-cxho.7 busy tab action=160: rc=0 error=0
edit-cxho.7 busy tab action=111: rc=0 error=0
edit-cxho.7 busy tab action=112: rc=0 error=0
edit-cxho.7 busy tab action=148: rc=0 error=0
edit-cxho.7 busy tab action=150: rc=0 error=0
tabs exit=0
edit-cxho.7 busy Ctrl+W through input loop: rc=0 error=0 rejected=1
tab-loop exit=0
edit-cxho.7 pointer order, ring wrap and wheel saturation: PASS
pointer-order exit=0
Not charging
 13:42:18 up 1 day, 15:45,  1 user,  load average: 1.37, 1.16, 1.37
```

The release build also passes these focused cases with the real allocator
interposition. Sanitizer guard counts are inert by design; the independent
release typing guard establishes the allocation result:

```text
Not charging
 13:43:25 up 1 day, 15:46,  1 user,  load average: 2.05, 1.34, 1.41
editor_test: null 10000 keys mallocs=0 guard=active
release null malloc guard exit=0
Not charging
 13:43:25 up 1 day, 15:46,  1 user,  load average: 2.05, 1.34, 1.41

Not charging
 13:46:12 up 1 day, 15:49,  1 user,  load average: 3.08, 2.45, 1.85
editor_test: raster 10000 keys mallocs=0 guard=active
release raster malloc guard exit=0
Not charging
 13:51:55 up 1 day, 15:55,  1 user,  load average: 1.35, 1.69, 1.69
```

The typing count above comes from the existing allocation harness (E: inherited
test workload), not a new budget. The raster bracket includes display-lock
waiting and release regressions; no elapsed-time claim is made.

## Benchmark

Ran `build/bench/editor_bench` with the arguments copied from
`bench/editor_bench.args` through the display lock on `:99`; exit `0` (M)[AC].
Its sample count is the existing checked-in harness choice (E), not a new
budget. Raw injection-to-submit rows below are (M)[AC]; no comparison baseline
or performance improvement is claimed. Raster rows exceed the existing tracked
aspiration; requested GL used the existing raster fallback because EGL/Present
was unavailable on this Xvfb. No performance or backend change made.

```text
Not charging
 13:48:08 up 1 day, 15:51,  1 user,  load average: 1.87, 2.18, 1.82
Filesystem               Size  Used Avail Use% Mounted on
/dev/mapper/vgmint-root  1.8T  169G  1.6T  10% /
STAMP (M)[AC] BAT0=Not charging load1=1.07 shared box
BENCH name=editor_null_serial_injection_T4_G1 n=64 required_n=10000 p50=442363 p99=566563 ci95_p50=[439572,459723] ci95_p99=[506911,566563] gate_p50=1000000 gate_p99=2000000 dropped=0 (M)[AC] power=Not charging load1=1.07 verdict=TRACK
STAMP (M)[AC] BAT0=Not charging load1=1.07 shared box
BENCH name=editor_raster_serial_injection_T4_G1 n=64 required_n=10000 p50=16802235 p99=18233975 ci95_p50=[16689905,17001799] ci95_p99=[17527935,18233975] gate_p50=1000000 gate_p99=2000000 dropped=0 (M)[AC] power=Not charging load1=1.07 verdict=TRACK
STAMP (M)[AC] BAT0=Not charging load1=1.15 shared box
BENCH name=editor_gl_fallback_raster_serial_injection_T4_G1 n=64 required_n=10000 p50=16647931 p99=18015243 ci95_p50=[16553686,16813361] ci95_p99=[17540536,18015243] gate_p50=1000000 gate_p99=2000000 dropped=0 (M)[AC] power=Not charging load1=1.15 verdict=TRACK
editor_bench exit=0
Not charging
 13:51:49 up 1 day, 15:54,  1 user,  load average: 1.15, 1.66, 1.68
Filesystem               Size  Used Avail Use% Mounted on
/dev/mapper/vgmint-root  1.8T  169G  1.6T  10% /
```

## Execution environment and outside-scope observations

LeakSanitizer could not inspect processes inside the execution sandbox; the
authorized sanitizer reruns used the tool's outside-sandbox execution and leak
detection succeeded. There was no approval rejection. The missing filesystem
socket initially suggested a display limitation, but `xdpyinfo -display :99`
succeeded through the abstract socket; display verification was available.

An initial full check was stopped after discovering that existing private-Xvfb
helpers also require `EDIT_DISPLAY=:99` to honor the assigned server. That run
exited `143` (M)[AC: SIGTERM to this worktree's make process); it is preserved as
`build/cxho7-s24/check-unpinned-stopped.log`. Subsequent verification pins both
display variables. Production launcher executions had
`EDIT_REFUSE_REAL_DISPLAY=1` through the existing display guard.

Pending-open WHEEL is classified as control input and its handler currently
ignores it until adoption. Its burst test proves bounded ingress during a
pending open; it does not claim wheel input naturally accumulates across normal
pending-open turns. That behavior was left unchanged.

Code-reading observation outside this bead: the active replace-all branch only
checks the queue entry picked first for Escape. An Escape behind another parked
entry can therefore wait for replacement completion. Not reproduced or changed;
no cancellation-routing expansion or additional review was performed.

## Final verification and exit codes

All outcomes below are (M)[AC]. Release uses GCC and sanitizers use clang with
the repository's C11 warning/error flags. Display runs use a fresh
`XDG_RUNTIME_DIR`, both `DISPLAY=:99` and `EDIT_DISPLAY=:99`, and the existing
`tools/with_display_lock.sh`; sanitizer runs set
`ASAN_OPTIONS=detect_leaks=1`. The allocator harness uses the release binary so
the real counting interposition is active.

| Verification command | Exit |
| --- | --- |
| `make -j4 all` after the final source/test changes | `0` |
| `make -j4 all build/san/tests/editor_test` | `0` |
| `build/san/tests/editor_test --cxho7-<case>` for every focused selector listed in the green run | `0` each |
| `build/tests/editor_test --cxho7-<case>` for those same selectors | `0` each |
| `EDITOR_ALLOC_ONLY=null build/tests/editor_test` through the display lock | `0` |
| `EDITOR_ALLOC_ONLY=raster build/tests/editor_test` through the display lock | `0` |
| `build/bench/editor_bench` with `bench/editor_bench.args`, through the display lock | `0` |
| `ASAN_OPTIONS=detect_leaks=1 DISPLAY=:99 EDIT_DISPLAY=:99 EDIT_REFUSE_REAL_DISPLAY=1 make -j4 check` with a fresh runtime directory | `0` |

The complete sanitizer `editor_test`, built and run by the final full check,
passes with leak detection enabled. The required display suites, including
editor scrolling, large files, sessions, hot exit, CLI and raster, passed in
that same run. The binary count in the summary derives from the Makefile's
`TEST_SAN` list (P); all numbers in this output are recorded measurements
(M)[AC]. The replay CLI and display-lock shell contracts passed too. Their
deliberately rejected child commands print timeout/error diagnostics as part of
negative-case verification; the enclosing contract ends in PASS.

```text
Not charging
 13:44:52 up 1 day, 15:48,  1 user,  load average: 4.67, 2.37, 1.77
Filesystem               Size  Used Avail Use% Mounted on
/dev/mapper/vgmint-root  1.8T  168G  1.6T  10% /

editor_test: all passed
check: 112 test binaries passed
test_replay_cli: all passed
display lock contract: owner PID, alias/timeout, make/scripts, nested inheritance, exit status PASS
make -j4 check exit=0
Charging
 13:57:34 up 1 day, 16:00,  1 user,  load average: 1.68, 1.57, 1.62
Filesystem               Size  Used Avail Use% Mounted on
/dev/mapper/vgmint-root  1.8T  170G  1.6T  10% /
```

The disk snapshots bracket the full invocation and its existing sparse-fixture
suites. The earlier standalone editor run was also bracketed: before
`/dev/mapper/vgmint-root 1.8T 169G 1.6T 10% /`, after
`/dev/mapper/vgmint-root 1.8T 169G 1.6T 10% /` (M)[AC], recorded in
`build/cxho7-s24/editor-test-old-contract.log`. A final
`find build -type f -size +1G` found no remaining files (M)[AC; the threshold
is the standard binary gigabyte unit used by `find`, P).

Nothing remains for this bead's implementation or required verification.
Coordinator tracking/landing is outside this worker's scope. Stop after this
bead; the untested cancellation observation above was reported only.
