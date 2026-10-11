# edit-ovu — displayed scroll observer consumer

Implemented and tested the consumer for the remaining displayed-cadence
finding. **The finding remains open for editor/backend integration.** This
tree does not expose the refresh observer or immutable submitted scroll/sidebar
metadata needed for real displayed evidence. Nothing here claims physical G3z
verification or readiness for user handover.

## Changed files

- `bench/scroll_display.h`: caller-owned refresh-stream driver/validator with
  no allocation or global state; correlates displayed identities against an
  independent oracle, immutable input/source/geometry/plan/wrap/sidebar state,
  completion timestamps and captured text/sidebar correctness.
- `tests/scroll_display_test.c`: the assigned every-second displayed-repeat
  counterexample, mutation/order/missing-completion cases, synthetic/unsupported
  qualification, subpixel coverage, driver and report contracts.
- `bench/scroll_bench.c`: `--display-check` capability reporting without the
  corpus; ordinary output keeps the CPU/synthetic-null proxy explicitly labelled.
  Existing sample/exit policy is unchanged.
- `docs/decisions/edit-ovu-display.md`: the consumer design and concrete
  observer/capture handoff to edit-3kk.
- `src/scroll/STATUS.md`, `HANDOFF.md`, `docs/worklog/2026-10-11.md`: current
  scope and remaining integration recorded in lockstep.
- This report.

No runtime scroll/editor source, frame scheduling or typing allocation path
changed. Work stayed in the assigned worktree. No `bd` or `git` was run.
No unrelated backlog was pulled.

## Numerical derivations and environment

`SCROLL_DISPLAY_REFRESHES` is **(G) ten thousand**, copied from the assigned
review and `perf/01-perf-target.md` G3z. Allowed misses are **(G) zero**, from
the same row. The every-second repeat fixture's misses derive as
`SCROLL_DISPLAY_REFRESHES / (1 + 1)` (G: the review's concrete counterexample).
Metadata mutation populations derive from the enumerated contract fields;
timestamps are ordinal synthetic fixture ticks, not performance samples.
These checks introduce no timing budget or frame wait.

Power reads before validation were `Charging` and, before the serial matrix,
`Not charging`; the shared harness classifies both as AC. Process/test outcomes
below are (M)[AC]. Exit codes derive from process wait status; failure line numbers in
quoted logs derive from the compiler's `__LINE__`, not invented counts.
Validation runs on the shared box. Build and raw execution evidence lives
under `build/worker-evidence/` in this worktree.

## Red run — displayed repeat

The failing test and a temporary counter-only stub were written first. The
ordinary release target also observed this failure before the implementation
was replaced. Direct reproducer:

```sh
gcc -std=c11 -Wall -Wextra -Werror -Wshadow -Wconversion -Isrc \
  tests/scroll_display_test.c -o build/worker-evidence/scroll_display_red
build/worker-evidence/scroll_display_red
```

Exit failure (M)[AC], derived from the process status:

```text
scroll_display_test:43: FAIL scroll_display_judge(&h) == SCROLL_DISPLAY_MISS
exit=1
```

The test submits/presents every requested frame while repeating every second
displayed identity. It therefore fails the old counter-only proxy assumption.
Raw evidence: `build/worker-evidence/red-direct.log` and `red.log`.

## Red run — subpixel eligibility

The additional test was written before changing the fractional eligibility
logic. A valid whole-pixel origin in a taller row must not qualify subpixel
coverage. Same compiler flags, then the dedicated red binary:

```text
scroll_display_test:185: FAIL h.misses == 0 && !h.fractional
exit=1
```

Exit failure (M)[AC], from process status; source line from `__LINE__`.
Raw evidence: `build/worker-evidence/red-fractional.log`.

## Green run

Direct GCC regression binary, same warning flags, exits success (M)[AC]:

```text
scroll_display_test: every-second displayed repeat rejected with unchanged submit/present counts PASS
scroll_display_test: healthy synthetic stream unavailable; population/fractional eligibility PASS
scroll_display_test: immutable plan/wrap/input/source/sidebar and actual completion corruption rejected PASS
scroll_display_test: absent refresh callback counted; unsupported hook unavailable PASS
scroll_display_test: whole-pixel translation cannot qualify subpixel coverage PASS
scroll_display_test: observer driver and evidence-labelled unavailable report PASS
exit=0
```

Raw evidence: `build/worker-evidence/green-direct.log`.
Clang ASan/UBSan with the required warnings and `detect_leaks=0` produces the
same green output; `green-sanitizer-direct.log` records its success status.

## Full-tree verification

GCC release `make all` passes, including a final incremental rebuild after the
subpixel regression changed. The final process exit is **0 (M)[AC]**, derived
from wait status. C11 and `-Wall -Wextra -Werror -Wshadow -Wconversion` are in
the compile log, with no warning errors. Evidence: `make-all.log` and
`make-all-final.log` under `build/worker-evidence/`.

`DISPLAY=:99 build/tests/scroll_test` also passes, including active allocation
guards reporting zero allocations (M)[AC], derived from their counting hook.
Existing Q8, wrap, resident/error/budget and cursor-follow regressions stay
green. Evidence: `scroll-release.log`. The retained integration probe:

```sh
DISPLAY=:99 build/tests/scroll_test --require-editor-scroll
```

still fails (M)[AC], with the expected missing wheel frame:

```text
scroll_test:875: FAIL observer.submissions > baseline
```

The exit is **1 (M)[AC]**, from wait status; source line derives from
`__LINE__`. Evidence: `editor-scroll-probe.log`. This is an existing retained
host-integration failure, not this session's fixed regression.

Clang builds every ordinary sanitizer suite with C11, the required warning
flags and ASan/UBSan. The final ordinary `scroll_display_test` and `scroll_test`
both exit **0 (M)[AC]**, from their process statuses in the serial matrix.
`scroll_display_test.log` contains the full green output quoted above;
`scroll_test.log` contains the existing module regressions' PASS results.

`make check` was run as requested. With default leak detection it exits
**2 (M)[AC]**, the make process status, because LeakSanitizer cannot operate
under this sandbox's ptrace restrictions at `base_test`. With
`ASAN_OPTIONS=detect_leaks=0`, the ordinary run passes base and then exits
**2 (M)[AC]** at `cli_test`'s denied private IPC server initialization. It
initially waited for the shared display lane; acquisition eventually completed.
An additional nonblocking run (`EDIT_DISPLAY_LOCK_TIMEOUT=0`, the existing
wrapper's nonblocking selector, not a new wait budget) also recorded the
contended lane rather than waiting. Evidence: `make-check-default.log`,
`make-check.log`, `make-check-nonblocking.log`.

Because make stops at its first failure, the serial matrix then ran every
ordinary sanitizer binary directly, with leak detection disabled, safe display
and private runtime storage. A prior socket probe proved this sandbox cannot
access Xvfb, so these direct runs cannot interfere with the native display
lane. Self-locking suites used nonblocking acquisition. No test was changed
or given a synthetic pass. The matrix is **64 suites (M)[AC]**, derived from
the sorted `tests/*_test.c` population, **47 successful (M)[AC]**, derived by
counting records with exit status zero, and **17 failed (M)[AC] = 64 - 47**.
Pre-run power/load are `Not charging` / **11.55 (M)[AC]**, copied from the
power-status file and kernel load average. Full evidence: `suites.json`,
`matrix.log` and each suite's named log under `build/worker-evidence/`.

Display-related failed suites (each diagnosis derives from its suite log and
the native initialization call, not a blanket filename exemption):

| Suite | Observed failure |
| --- | --- |
| `editor_close_test` | Native close fixture cannot open editor; headless close case passed. |
| `editor_test` | Raster/native editor initialization failures. |
| `file_kill_test` | Its live viewport and queued live-frame children cannot initialize. |
| `raster_test` | `plat_init` fails. |
| `refproto_test` | Display open fails. |
| `refwin_test` | Shared display lock was contended; nonblocking acquisition fails. |
| `refwin_timeout_test` | Expected input-stage probe cannot begin: X connection failure occurs first. |
| `x11_clip_review_test` | Cannot connect to the safe display. |
| `x11_clip_test` | Platform initialization fails after its private Xvfb startup. |
| `x11_identity_test` | `plat_init` fails. |
| `x11_live_test` | Native startup/protocol fixtures cannot initialize. |
| `x11_order_test` | Native fixtures fail; then its startup-trace fixture crashes in libxcb, detailed below. |
| `x11_stall_test` | Live display initialization fails. |

`gl_test` exits successfully for its pure checks, explicitly SKIPping EGL/X11
native diagnostics. Its native portions still need coordinator rerun, along
with the failed display suites, on Xvfb :99 outside this sandbox. Pure X11
alignment/input/display-guard/XI2 checks also pass; these are not native
display evidence. `display-socket.log` records the direct proof:

```text
Xvfb socket: PermissionError Operation not permitted
```

Additional socket-blocked failures are **not claimed as display failures**:

| Suite | Observed failure |
| --- | --- |
| `ipc_test` | Private server initialization returns operation not permitted. |
| `editor_large_test` | Mapped-startup private IPC server cannot initialize. |
| `editor_p4_test` | Private IPC server cannot initialize after its null-backend selection tests pass. |
| `cli_test` | Its private decoy IPC server cannot initialize before native startup. |

`private-socket.log` independently confirms even a pathname socket inside
this writable worktree is denied:

```text
private Unix socket bind: PermissionError Operation not permitted
```

Consequently the **full headless acceptance requirement is not met here**:
the coordinator must also rerun these socket-dependent suites with Unix socket
creation allowed. Every other non-display suite completes successfully. No
ASan/UBSan diagnostic appears in the passing headless suites. Leak-enabled
verification remains required outside this sandbox.

The remaining make-check shell contracts were also run after their sanitizer
helpers existed. Replay CLI and display-lock ownership/timeout/inheritance
contracts both exit **0 (M)[AC]**, from process status, with:

```text
test_replay_cli: all passed
display lock contract: owner PID, alias/timeout, make/scripts, nested inheritance, exit status PASS
```

Evidence: `replay-cli.log`, `display-lock-contract.log`. An initial replay CLI
attempt made before its sanitizer helper was built failed for the absent
binary; the completed-build rerun above passed without source changes.

## Benchmark evidence

`build/bench/scroll_bench --display-check` exits **0 (M)[AC]**, derived from
process status, and explicitly prints `displayed_verdict=UNAVAILABLE`.
Its zero observed refreshes derive from the unsupported/null driver; required
refreshes derive from G3z, not a newly invented target.
Evidence: `build/worker-evidence/bench-display-check.log`.

The existing CPU proxy was exercised with:

```sh
build/bench/scroll_bench --track --probe
```

Pre-run power was `Charging`; the bench stamps `Charging` and load from
`/proc/loadavg` at each population. The null work population's output excerpt:

```text
(M)[AC] load1=101.43 power=Charging n=10000 p50_ns=143804 p99_ns=39211777 max_ns=89358133
```

Derivation: load is the measured kernel load average; population is the
existing G3z `STEPS` count. Every duration is `bench_now_ns(end) -
bench_now_ns(start)` around scroll/resolve/layout/null submit/present. The
quantiles use sorted nearest ranks `ceil(n * 0.50)` and `ceil(n * 0.99)`;
maximum is the largest retained sample. These are loaded-box observations,
not displayed-refresh evidence or new regression budgets. Full diagnostics:
`build/worker-evidence/bench-proxy.log`.

The proxy process exits **3 (M)[AC]**, derived from wait status. Its pre-existing
end-of-run policy refuses a probe population even with `--track`: the probe
records one jump sample (M)[AC], derived from the single `--probe` request,
below `BENCH_INTERACTION_MIN_N` (G, copied from perf interaction sampling).
Viewport correctness assertions passed; no timing gate pass is claimed.
The unchanged sample/exit policy is reported outside scope below.

## Sandbox sanitizer limit

Leak-enabled execution of the regression binary fails in the sandbox:

```text
LeakSanitizer has encountered a fatal error.
HINT: LeakSanitizer does not work under ptrace (strace, gdb, etc)
```

This is distinct from ASan/UBSan, which run with leak detection disabled.
Raw evidence: `build/worker-evidence/leak-sanitizer-default.log`.
The coordinator must rerun leak detection outside this sandbox.

## What remains and scope observations

The editor owner must produce actual refresh opportunities/completion IDs,
including repeats, failed/dropped presents and missing completions; preserve
immutable submitted metadata in setup-reserved storage; and provide an
independent expected-state/captured-pixel driver. The delivered consumer is
ready for that binding. There is no live edit-3kk agent in this session;
the design document and HANDOFF entry provide the coordination contract.

The prior worker's remaining renderer-origin/clip/overscan wiring, actual
translated pixels/hit testing and exact resident wrap-metadata producer remain
external integration work. Existing source-failure/bounded-work/resident-source
fixes stay intact. No new trial/fuzz/trace evidence justified pulling their
other backlog items. Physical displayed G3z is UNAVAILABLE until the integrated
producer/capture test runs; the next Astra comparison drive must verify that
same integrated build before user handover.

Outside scope, left unchanged: scroll_bench still has pre-existing
qualified-sample/gate exit policy, despite the newer TRACK-only bench rule.
Its policy belongs to the separately assigned bench-policy work, not this
displayed-observability consumer. The report does not convert that observation
into another bead or fix.

Also outside scope: after its native fixtures fail to initialize,
`x11_order_test` continues into `test_startup_trace` / `plat_map` and produces
an ASan null-pointer SEGV in `xcb_send_request_with_fds64`. This is a real
failed-fixture path diagnostic, not a clean sanitizer pass. The source
locations derive from the sanitizer stack in `x11_order_test.log`; no X11 test
or production code was edited. The coordinator should retain that evidence
when rerunning the native suite and triage it under the existing review policy.

This worker stops after this bead's consumer work and report. The tree builds;
module STATUS and coordination docs are current. The displayed finding and
environment-blocked acceptance are explicitly left open.
