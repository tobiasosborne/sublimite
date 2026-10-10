# edit-o4o session 9 worker report

Scope: the M0 G1 typing checkpoint in `bench/editor_bench.c`. Read CLAUDE.md,
the supplied bead, the M0 editor failure record, edit-zzj.14 decisions, and
the editor status. No git mutations, bd, HANDOFF/worklog edits, production
editor/raster edits, or new globals.

## Finding: undiagnosable typing checkpoint

Implemented the row diagnostic and benchmark self-check first. The diagnostic
identifies the backend/workload, prints all four predicates, actual/expected
completion counts and allocation count. Logging ends the active guard first
and preserves its pre-cleanup state. Design: `docs/decisions/edit-o4o.md`.

Red run, before implementing the diagnostic (exit 255):

```text
$ DISPLAY=:99 build/bench/editor_bench --self-check-typing
editor_bench:511 failed: rc == 1 && printed
PROBE typing checkpoint flag=invalid=1 rc=-1 printed=0
```

Green diagnostic run (exit 0):

```text
$ DISPLAY=:99 build/bench/editor_bench --self-check-typing
PROBE typing checkpoint flag=invalid=1 rc=1 printed=1
PROBE typing checkpoint flag=incomplete=1 rc=1 printed=1
PROBE typing checkpoint flag=guard=1 rc=1 printed=1
PROBE typing checkpoint flag=suspended=1 rc=1 printed=1
editor_bench: typing checkpoint diagnostics and structural exit aggregation passed
```

Missing: allocation-site attribution. The existing guard records only a count;
there is no recorded site to print. Output explicitly says
`allocation_site=unavailable_count_only_guard`. Changing src/base is outside
the bead's allowed files. No confirmed editor/raster typing allocation has
been found; the native allocation-count failure is attributed below.

## Reproduction campaign

The `.args` file is empty. Both native default runs use it unchanged, on
Xvfb :99 through the repository's shared display lock. The first has no
dedicated CPU load, the second adds bounded CPU-load processes. No quiet-box
wait or timing-based tuning is used.

The sandbox-only benchmark attempt exits 1 without running the binary:

```text
waiting for display :99 (held by unknown)
timeout: display lock timeout: display :99 (held by unknown)
```

The approved native run (`timeout 600`, exit 124) reaches all null and raster
typing rows and the GL-fallback serial row. It times out during the next
GL-fallback row. No original four-predicate checkpoint fires. Its completed
null rows have zero allocations and structural OK. Native rows do report
allocation-count structural failures. Selected actual output follows;
all measured fields are (M)[AC], BAT0=Not charging:

```text
STAMP (M)[AC] BAT0=Not charging load1=0.98 shared box
G1 null workload=serial keys=10000 completed=10000 arrivals_active=0 coalesced=0 allocations=0 mutations=10000 journal=10000 structural=OK mode=GATE (M)[AC]
G1 null workload=queued_active_frame keys=10000 completed=10000 arrivals_active=0 coalesced=7500 allocations=0 mutations=10000 journal=10000 structural=OK mode=GATE (M)[AC]
G1 null workload=active_index keys=10000 completed=10000 arrivals_active=0 coalesced=7500 allocations=0 mutations=10000 journal=10000 structural=OK mode=GATE (M)[AC]
G1 null workload=queued_index_find_save keys=10000 completed=10000 arrivals_active=0 coalesced=7500 allocations=0 mutations=10000 journal=10000 structural=OK mode=GATE (M)[AC]
G1 raster workload=serial keys=10000 completed=10000 arrivals_active=10000 coalesced=0 allocations=295 mutations=10000 journal=10000 structural=FAIL mode=GATE (M)[AC]
STAMP (M)[AC] BAT0=Not charging load1=7.51 shared box
G1 raster workload=queued_active_frame keys=10000 completed=10000 arrivals_active=10000 coalesced=8750 allocations=10 mutations=10000 journal=10000 structural=FAIL mode=GATE (M)[AC]
STAMP (M)[AC] BAT0=Not charging load1=6.67 shared box
G1 raster workload=active_index keys=10000 completed=10000 arrivals_active=10000 coalesced=8750 allocations=100 mutations=10000 journal=10000 structural=FAIL mode=GATE (M)[AC]
STAMP (M)[AC] BAT0=Not charging load1=6.04 shared box
G1 raster workload=queued_index_find_save keys=10000 completed=10000 arrivals_active=10000 coalesced=8750 allocations=108 mutations=10000 journal=10000 structural=FAIL mode=GATE (M)[AC]
STAMP (M)[AC] BAT0=Not charging load1=5.35 shared box
G1 gl_fallback_raster workload=serial keys=10000 completed=10000 arrivals_active=10000 coalesced=0 allocations=205 mutations=10000 journal=10000 structural=FAIL mode=GATE (M)[AC]
```

Raw default-run output: `build/edit-o4o/repro-native-01.log`. The second
default run's output is `build/edit-o4o/repro-native-load.log`. It was stopped
(exit 143) while still waiting for the shared display lock, before its dedicated
CPU workers started: insufficient budget remained for a full default run.

A supplemental short campaign is queued in `build/edit-o4o/repro-short-paired.log`:
the same empty args file plus `--keys=64 --no-idle --no-p4 --track`, alternating
no dedicated load and four CPU workers. It is explicitly undersampled and
cannot satisfy the default-population reproduction requirement or certify
timing gates. Its controller bounds both the display wait and runtime and
cleans up its children. It exits 1 at the display-lock timeout without running
any benchmark or starting CPU workers; no paired result or CPU-load comparison
is claimed:

```text
waiting for display :99 (held by 3872167)
python3: display lock timeout: display :99 (held by 3990114)
```

The requested ten complete passing default runs have not been achieved within
the available wall-clock budget. No original checkpoint assertion fired in
the actual rows completed by the default/native diagnostic attempts.

## Out-of-scope finding: process-wide allocation attribution

A bounded debugger probe uses the actual benchmark, with serial rows and a
shorter diagnostic population. It stops at the first allocation with
`guard_on` true. This is attribution evidence, not a timing measurement or
a completed benchmark pass. The stack, retaining only symbols/repo paths:

```text
Thread 6 "editor_bench" hit Breakpoint 1.1, malloc (n=32) at src/base/malloc_guard.c:33
#0 malloc at src/base/malloc_guard.c:33
#1 libxcb internal
#2 xcb_poll_for_reply
#3 fence_job_fn at src/raster/raster.c:178
#4 worker_main at src/work/work.c:82
#5 start_thread
#6 clone3
```

This is the raster completion worker's library IO, covered by the existing
IO exemption in docs/decisions/zzj.3.md. The process-wide guard counts it when
the UI's guard is active. A positive aggregate count therefore does not
establish a typing-path allocation in editor/raster. No production fix or
allocation-suite regression is made for this separate guard attribution
finding. Changing src/base lies outside the allowed files. The coordinator
should track allocation-site capture and thread/IO attribution separately.

The conditional checkpoint handling change is not applied unless an original
checkpoint failure is reproduced. The diagnostic self-check deliberately
forces each predicate; it does not claim reproduction of M0's real failure.

## Validation

The initial and final-source gcc 13 release `make -j4 all` exited 0 with the
repository's strict warning flags. Full clang 18 ASan/UBSan `make -j4 check`
was retried with a longer shared-display-lock wait, after both sandbox
and initial native attempts hit that wait's timeout at cli_test. These are
environment wait failures, not suite passes. The retry passes the editor,
GL, harness, journal, layout, index, minimap and piece suites, then waits for
the shared display at raster_test. The harness includes the final benchmark
translation unit; it was rebuilt before that suite executed:

```text
harness_test: 20213 checks, 0 failures
piece_test: all passed
== build/san/tests/prewake_test
prewake_test: variant=a PASS idle threshold, three triggers, debounce, errors, spin budget
prewake_test: variant=b PASS idle threshold, three triggers, debounce, errors, spin budget
prewake_test: variant=c PASS idle threshold, three triggers, debounce, errors, spin budget
== build/san/tests/raster_test
waiting for display :99 (held by 3989351)
```

The waiting process belonging to this worktree was stopped before the final
wall-clock deadline. The full retry exits 2, not green:

```text
== build/san/tests/raster_test
waiting for display :99 (held by 3989351)
Terminated
make: *** [Makefile:105: check] Error 143
```

No complete `make check` pass is claimed. The checks run
with clang 18 ASan/UBSan and all the required strict warning flags. Editor
fuzz completed cleanly:

```text
$ ASAN_OPTIONS=detect_leaks=0 DISPLAY=:99 EDIT_DISPLAY=:99 build/fuzz/editor_fuzz -max_total_time=60 -timeout=10
#23158 DONE cov: 13285 ft: 41959 corp: 658/6534b lim: 17 exec/s: 379 rss: 385Mb
Done 23158 runs in 61 second(s)
```

Fuzz exit 0; duration/executions are (M)[AC], against 60 seconds (G).
LeakSanitizer is disabled with
`ASAN_OPTIONS=detect_leaks=0`; the coordinator must rerun with leaks enabled.
All display invocations use DISPLAY/EDIT_DISPLAY=:99.

Scope is frozen at the requested cutoff. No timing gate acceptance is claimed
from a single loaded run. The bead remains incomplete: allocation-site capture,
the full repeated default-run campaign, and a passing complete sanitizer check
are outstanding at delivery. There is no confirmed original checkpoint
failure on which to base the conditional continuation fix. The diagnostics
remain in place, and production editor/raster sources remain unchanged.

Final release/self-check confirmation:

```text
$ make -j4 all
make: Nothing to be done for 'all'.
$ DISPLAY=:99 build/bench/editor_bench --self-check-typing
PROBE typing checkpoint flag=invalid=1 rc=1 printed=1
PROBE typing checkpoint flag=incomplete=1 rc=1 printed=1
PROBE typing checkpoint flag=guard=1 rc=1 printed=1
PROBE typing checkpoint flag=suspended=1 rc=1 printed=1
editor_bench: typing checkpoint diagnostics and structural exit aggregation passed
```

Both commands exit 0. `git diff --check` passes. Changed files are exactly
`bench/editor_bench.c`, this report, and `docs/decisions/edit-o4o.md`.
