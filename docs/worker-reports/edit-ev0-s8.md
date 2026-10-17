# edit-ev0 — worker s8

Status: scoped code changes complete; acceptance verification BLOCKED by repeated shared-display lock timeouts. No successful full make bench run is claimed.

Implemented the bounded editor default and quick-mode skips for absent
all-`a` find fixtures. No production `src/` files or corpus generator changes.

## Files touched

- `bench/editor_bench.args`: replace the pre-existing track-only default with the derived serial probe.
- `bench/find_bench.c`: skip absent optional all-`a` inputs in quick mode, including cancellation; retain other fixture errors.
- `tests/find_bench_test.c`: regression for optional absence, full-mode requirements, required inputs, wrong sizes and cancellation.
- `bench/README.md`: explicit full `--track` invocation and quick-fixture behavior.
- `docs/decisions/edit-ev0.md`: choices and key-count/cutoff arithmetic.
- `docs/worker-reports/edit-ev0-s8.md`: this report.

Generated build outputs and raw evidence are under `build/edit-ev0/`.
Protected documents were not edited; no `bd` or `git` commands were run.

## Evidence conventions and choices

The [decision](../decisions/edit-ev0.md) contains the (P)/(M)[AC]/(E)
derivation of the key count and the reproduction cutoff. The args file is
strictly one line because the Makefile passes its entire contents as arguments.
The full editor workload remains explicit and documented. Its default probe
prints descriptive percentiles against the unchanged G1 row.

Quick find skips only absent optional all-`a` input. It prints the affected
row and filename. Full mode still requires it; other errors remain errors.
Skipping avoids adding dense fixture generation to the default. The regression
creates only a private empty file and then removes it before testing absence.

Raw values below are (M), with power and load stamps; `Not charging` maps to
[AC] by the existing harness policy. Population sizes and printed goal values
are the existing benchmark/test inputs, except the derived editor population.
Percentiles use the existing harness; goal rows come from
`perf/01-perf-target.md`. Return values are process/API status semantics,
not performance budgets. Deliberate negative-control fixtures print `FAIL`
while testing the expected rejection; their enclosing suite status is decisive.

The post-change default attempt overlapped sanitizer compilation, the
standalone quick find run, and sanitizer tests. It timed out at the GL bench
while this worker's editor sanitizer held the display; that overlap was
avoidable and the failed attempt is preserved below. Other worktrees also held
the display. Subsequent scroll and large-file attempts each failed to acquire
it before their existing lock deadlines. The planned sequential full rerun
could not be completed in this environment. Totals include shared-display
waits. Treat all performance observations as provisional under the stamped
load.

## Verification commands and observed exit statuses (M)

| Command | Exit | Evidence |
| --- | --- | --- |
| `make -j3 all` (after implementation) | 0 | `build/edit-ev0/green-all.log` |
| `make -j3 all` (final tree) | 0 | `build/edit-ev0/final-all.log` |
| `make -j3 build/san/tests/find_bench_test build/san/tests/harness_test build/san/tests/editor_test build/san/tests/editor_scroll_test build/san/tests/editor_large_test build/san/tests/editor_session_test build/san/tests/editor_hot_exit_test build/san/tests/cli_test build/san/tests/raster_test` | 0 | `build/edit-ev0/san-build.log` |
| `timeout` with derived cutoff, `make bench` before changes | 124 | red log below |
| `build/bench/find_bench --quick` before changes | 1 | red log below |
| `build/tests/find_bench_test` before implementation | 1 | red log below |
| `build/tests/find_bench_test` after implementation | 0 | green log below |
| `build/bench/find_bench --quick` after changes | 0 | green log below |
| First post-change `make bench`, verification overlapped | 2 | failed attempt below |
| Sequential full `make bench` rerun | not run: display unavailable | coordinator rerun required |
| `ASAN_OPTIONS=detect_leaks=1 build/san/tests/find_bench_test` | 0 | raw log below |
| `ASAN_OPTIONS=detect_leaks=1 build/san/tests/harness_test` | 0 | raw log below |
| `ASAN_OPTIONS=detect_leaks=1 build/san/tests/editor_test` | 0 | raw log below |
| `ASAN_OPTIONS=detect_leaks=1 build/san/tests/editor_scroll_test` | 1 | raw log below |
| `ASAN_OPTIONS=detect_leaks=1 build/san/tests/editor_large_test` | 1 | raw log below |
| `ASAN_OPTIONS=detect_leaks=1 build/san/tests/editor_session_test` | not run (display unavailable) | raw log below |
| `ASAN_OPTIONS=detect_leaks=1 build/san/tests/editor_hot_exit_test` | not run (display unavailable) | raw log below |
| `ASAN_OPTIONS=detect_leaks=1 build/san/tests/cli_test` | not run (display unavailable) | raw log below |
| `ASAN_OPTIONS=detect_leaks=1 build/san/tests/raster_test` | not run (display unavailable) | raw log below |

Sanitizer tests use the same `build/san/tests/%` rules as `make check`.
There is no separate sanitizer bench-binary target; `find_bench_test` and
`harness_test` include the two bench sources. Display suites run with a fresh
`XDG_RUNTIME_DIR=$(mktemp -d)`, `DISPLAY=:99`, and
`sh tools/with_display_lock.sh build/san/tests/<name>`, as shown in their
raw commands. The display identifier is the supplied shared Xvfb fixture.
Sandbox display access failed initially; the authorized escaped display check
succeeded, and display execution uses that access. Every private runtime is
removed by exit traps for completed commands; the canceled runtime was
removed explicitly after confirming its process group had stopped.

## Remaining work and observations outside this bead

Acceptance is incomplete. The coordinator must obtain a free shared display
window and rerun the complete default `make bench` to exit successfully, plus
`editor_scroll_test`, `editor_large_test`, `editor_session_test`,
`editor_hot_exit_test`, `cli_test` and `raster_test` with the prescribed
LeakSanitizer/runtime/display-lock environment. Scroll and large-file attempts
exited because they could not acquire the lock; their test binaries did not
start. The session attempt was canceled while still queued. Hot-exit, CLI and
raster were not started. The display batch driver exited on termination; its
observed status was 143 (M), shell signal status semantics.

The worker stopped only its verified, isolated process group while no
sanitizer binary was active, confirmed it exited, and removed its private
runtime directories. No queued worker checks remain. Release build, release
regression, standalone quick find, sanitizer find/harness and sanitizer
`editor_test` passed. This is the one-bead stopping point; no display-lane or
other workload changes were made.

The absent all-`a` rows intentionally remain unmeasured in quick mode. The
full editor contention, idle and tab populations remain available through the
explicit command. Neither those performance numbers nor other bench misses
were changed.

The existing full find-in-files workload and fresh large-file save population
in `file_bench` made the attempted default run long. Their workloads are outside
the authorized changes and were left intact. The GL request fell back to
raster on this Xvfb; no native-GL performance claim is made. While checking
harness progress, an unchanged comment in `test_gate_verdict` was observed to
claim a timing miss exits nonzero, although its adjacent assertions require
success. It was not edited or reviewed further. Other worktrees held the
shared display during verification; the first scroll attempt timed out before
its test binary ran. That infrastructure failure is preserved below.

The timed-out red editor run left a private journal in this worktree's build
directory. Only that known red-run artifact was removed. No fixture generation code was added, and existing corpus files were not changed. Disk stamps accompany
the runs below, including before and after the failed default attempt.

## Calibration (M)[AC]

Evidence: (M)[AC].

```text
BEFORE
Not charging
 08:47:19 up 6 days, 13:35,  1 user,  load average: 55.96, 56.63, 47.33
Filesystem      Size  Used Avail Use% Mounted on
/dev/nvme0n1p5  480G  375G   81G  83% /
COMMAND ['sh', '-c', 'ev0_runtime=$(mktemp -d); trap \'rm -rf "$ev0_runtime"\' EXIT; XDG_RUNTIME_DIR="$ev0_runtime" DISPLAY=:99 sh tools/with_display_lock.sh build/bench/editor_bench --track --no-idle --no-p4 --serial-only --keys=2']
STAMP (M)[AC] BAT0=Not charging load1=55.96 shared box
BACKEND requested=null actual=null init_error=0 TARGET A=2880x1800 font_px=30 (M)[AC]
POSITION null byte=966367641 index=progressing workload=serial edits=alternating_insert_backspace
BENCH name=editor_null_serial_injection_submit n=2 required_n=10000 p50=5561798 p99=5797804 ci95_p50=[5561798,5797804] ci95_p99=[5561798,5797804] gate_p50=0 gate_p99=0 dropped=0 (M)[AC] power=Not charging load1=55.96 verdict=TRACK
BENCH name=editor_null_serial_dequeue_submit n=2 required_n=10000 p50=5434931 p99=5686303 ci95_p50=[5434931,5686303] ci95_p99=[5434931,5686303] gate_p50=0 gate_p99=0 dropped=0 (M)[AC] power=Not charging load1=55.96 verdict=TRACK
BENCH name=editor_null_serial_injection_T4_G1 n=2 required_n=10000 p50=6067844 p99=6357968 ci95_p50=[6067844,6357968] ci95_p99=[6067844,6357968] gate_p50=1000000 gate_p99=2000000 dropped=0 (M)[AC] power=Not charging load1=55.96 verdict=TRACK
G1 null workload=serial keys=2 completed=2 arrivals_active=0 coalesced=0 foreign_input=0 allocations=0 mutations=2 journal=2 structural=OK mode=TRACK (M)[AC]
STAMP (M)[AC] BAT0=Not charging load1=55.96 shared box
BACKEND requested=raster actual=cpu-raster init_error=0 TARGET A=2880x1800 font_px=30 (M)[AC]
POSITION raster byte=966367641 index=progressing workload=serial edits=alternating_insert_backspace
BENCH name=editor_raster_serial_injection_submit n=2 required_n=10000 p50=41835277 p99=70616795 ci95_p50=[41835277,70616795] ci95_p99=[41835277,70616795] gate_p50=0 gate_p99=0 dropped=0 (M)[AC] power=Not charging load1=55.96 verdict=TRACK
BENCH name=editor_raster_serial_dequeue_submit n=2 required_n=10000 p50=41769114 p99=70523695 ci95_p50=[41769114,70523695] ci95_p99=[41769114,70523695] gate_p50=0 gate_p99=0 dropped=0 (M)[AC] power=Not charging load1=55.96 verdict=TRACK
BENCH name=editor_raster_serial_injection_T4_G1 n=2 required_n=10000 p50=44234494 p99=77295022 ci95_p50=[44234494,77295022] ci95_p99=[44234494,77295022] gate_p50=1000000 gate_p99=2000000 dropped=0 (M)[AC] power=Not charging load1=55.96 verdict=TRACK
G1 raster workload=serial keys=2 completed=2 arrivals_active=2 coalesced=0 foreign_input=0 allocations=0 mutations=2 journal=2 structural=OK mode=TRACK (M)[AC]
STAMP (M)[AC] BAT0=Not charging load1=56.85 shared box
sublimite: EGL init failed: EGL/GL/Present unavailable (code=-10); using raster; no retry
BACKEND requested=gl actual=cpu-raster init_error=-10 TARGET A=2880x1800 font_px=30 (M)[AC]
POSITION gl_fallback_raster byte=966367641 index=progressing workload=serial edits=alternating_insert_backspace
BENCH name=editor_gl_fallback_raster_serial_injection_submit n=2 required_n=10000 p50=32997151 p99=57835221 ci95_p50=[32997151,57835221] ci95_p99=[32997151,57835221] gate_p50=0 gate_p99=0 dropped=0 (M)[AC] power=Not charging load1=56.85 verdict=TRACK
BENCH name=editor_gl_fallback_raster_serial_dequeue_submit n=2 required_n=10000 p50=32874853 p99=57749311 ci95_p50=[32874853,57749311] ci95_p99=[32874853,57749311] gate_p50=0 gate_p99=0 dropped=0 (M)[AC] power=Not charging load1=56.85 verdict=TRACK
BENCH name=editor_gl_fallback_raster_serial_injection_T4_G1 n=2 required_n=10000 p50=49160736 p99=62865608 ci95_p50=[49160736,62865608] ci95_p99=[49160736,62865608] gate_p50=1000000 gate_p99=2000000 dropped=0 (M)[AC] power=Not charging load1=56.85 verdict=TRACK
G1 gl_fallback_raster workload=serial keys=2 completed=2 arrivals_active=2 coalesced=0 foreign_input=0 allocations=0 mutations=2 journal=2 structural=OK mode=TRACK (M)[AC]
AFTER
Not charging
 08:47:29 up 6 days, 13:35,  1 user,  load average: 56.85, 56.79, 47.48
Filesystem      Size  Used Avail Use% Mounted on
/dev/nvme0n1p5  480G  375G   81G  83% /
RESULT exit=0 wall_ns=10197160018 (M); monotonic end-start
```

## Red: new regression before implementation

Evidence: (M)[AC].

```text
BEFORE
Not charging
 08:47:16 up 6 days, 13:35,  1 user,  load average: 55.96, 56.63, 47.33
Filesystem      Size  Used Avail Use% Mounted on
/dev/nvme0n1p5  480G  375G   81G  83% /
COMMAND ['build/tests/find_bench_test']
P1R10 self-check: PASS delayed_ack_rejected=1 cancellation_CPU_interval_rejected=1 boundary=1 median=1 tail=1
find fixture FAIL: missing start timeout
find fixture FAIL: missing completion timeout
find fixture FAIL: stuck shutdown deadline; terminating fixture (no shutdown join)
fixture missing/wrong size: build/find-bench-test-BymHkL
fixture missing/wrong size: build/find-bench-test-BymHkL
fixture missing/wrong size: build/find-bench-test-BymHkL
fixture missing/wrong size: build/find-bench-test-BymHkL
fixture missing/wrong size: build/find-bench-test-BymHkL
fixture missing/wrong size: build/find-bench-test-BymHkL
fixture missing/wrong size: build/find-bench-test-BymHkL
edit-ev0 FAIL: quick absent all-a rows must skip; required/full/bad-size fixtures must fail
P1R10/P1R11 FAIL: cancellation verdict=0 deadline verdict=0
P1R11 self-check: PASS missing_start=2 missing_completion=2 shutdown=2
AFTER
Not charging
 08:47:16 up 6 days, 13:35,  1 user,  load average: 55.96, 56.63, 47.33
Filesystem      Size  Used Avail Use% Mounted on
/dev/nvme0n1p5  480G  375G   81G  83% /
RESULT exit=1 wall_ns=59051806 (M); monotonic end-start
```

## Red: original default make bench

Evidence: (M)[AC].

```text
BEFORE
Not charging
 08:47:51 up 6 days, 13:35,  1 user,  load average: 57.70, 56.97, 47.74
Filesystem      Size  Used Avail Use% Mounted on
/dev/nvme0n1p5  480G  375G   81G  83% /
COMMAND ['sh', '-c', 'ev0_runtime=$(mktemp -d); trap \'rm -rf "$ev0_runtime"\' EXIT; XDG_RUNTIME_DIR="$ev0_runtime" DISPLAY=:99 timeout 26s make bench']
== build/bench/base_bench 
BENCH name=base_pool n=1000000 p50=46 p99=67
== build/bench/config_bench 
config_bench: parse (M)[AC] power=Not charging load1=57.57 n=77 derivation=one_sample_per_fixture_byte fixture_bytes=77 p50_ns=5290 p99_ns=5037910 verdict=TRACK
== build/bench/editor_bench --track
STAMP (M)[AC] BAT0=Not charging load1=57.57 shared box
BACKEND requested=null actual=null init_error=0 TARGET A=2880x1800 font_px=30 (M)[AC]
POSITION null byte=966367641 index=progressing workload=serial edits=alternating_insert_backspace
make: *** [Makefile:220: bench] Terminated
AFTER
Not charging
 08:48:17 up 6 days, 13:36,  1 user,  load average: 60.58, 57.67, 48.26
Filesystem      Size  Used Avail Use% Mounted on
/dev/nvme0n1p5  480G  375G   81G  83% /
RESULT exit=124 wall_ns=26027397754 (M); monotonic end-start
```

## Red: original quick find

Evidence: (M)[AC].

```text
BEFORE
Not charging
 08:47:52 up 6 days, 13:35,  1 user,  load average: 57.57, 56.95, 47.78
Filesystem      Size  Used Avail Use% Mounted on
/dev/nvme0n1p5  480G  375G   81G  83% /
COMMAND ['build/bench/find_bench', '--quick']
POWER status=Not charging (M)[AC] indicative: concurrent workers; default full gates, quick scaled TRACK
ROW name=G6_ERROR bytes=8388608 expected=14060 stored=4096 first=268 last_stored=2468428 mapping=NEW cache=read-warm kind=TRACK
BENCH name=G6_ERROR n=3 p50=23700137 p99=30277176 ci95=[20424548,30277176] gate_p50=0 gate_p99=0 pass=1 power=[AC]
FIND G6_ERROR TRACK correctness=PASS (M)[AC]; limits=(G); unit=ns
ROW name=G6_newline bytes=8388608 expected=69905 stored=4096 first=119 last_stored=491519 mapping=NEW cache=read-warm kind=TRACK
BENCH name=G6_newline n=3 p50=8256267 p99=9029373 ci95=[2367498,9029373] gate_p50=0 gate_p99=0 pass=1 power=[AC]
FIND G6_newline TRACK correctness=PASS (M)[AC]; limits=(G); unit=ns
fixture missing/wrong size: /tmp/edit-corpus/all_a_1g.txt
fixture missing/wrong size: /tmp/edit-corpus/all_a_1g.txt
fixture missing/wrong size: /tmp/edit-corpus/all_a_1g.txt
fixture missing/wrong size: /tmp/edit-corpus/all_a_1g.txt
ROW name=periodic_dense bytes=8388608 expected=262143 stored=4096 first=6 last_stored=131046 mapping=NEW cache=read-warm kind=TRACK
BENCH name=periodic_dense n=3 p50=85351676 p99=103452594 ci95=[75081689,103452594] gate_p50=0 gate_p99=0 pass=1 power=[AC]
FIND periodic_dense TRACK correctness=PASS (M)[AC]; limits=(G); unit=ns
ROW name=regex_prefix bytes=8388608 expected=14060 stored=4096 first=268 last_stored=2468428 mapping=NEW cache=read-warm kind=TRACK
BENCH name=regex_prefix n=3 p50=100222570 p99=112494704 ci95=[83501903,112494704] gate_p50=0 gate_p99=0 pass=1 power=[AC]
FIND regex_prefix TRACK correctness=PASS (M)[AC]; limits=(G); unit=ns
ROW name=regex_no_prefix bytes=8388608 expected=14060 stored=4096 first=268 last_stored=2468428 mapping=NEW cache=read-warm kind=TRACK
BENCH name=regex_no_prefix n=3 p50=7014572799 p99=8163138312 ci95=[4294976306,8163138312] gate_p50=0 gate_p99=0 pass=1 power=[AC]
FIND regex_no_prefix TRACK correctness=PASS (M)[AC]; limits=(G); unit=ns
find_bench: STRUCTURAL FAILURE (quick: limits off)
AFTER
Not charging
 08:48:13 up 6 days, 13:36,  1 user,  load average: 59.41, 57.40, 48.12
Filesystem      Size  Used Avail Use% Mounted on
/dev/nvme0n1p5  480G  375G   81G  83% /
RESULT exit=1 wall_ns=21023176550 (M); monotonic end-start
```

## Green: new regression

Evidence: (M)[AC].

```text
BEFORE
Not charging
 08:49:01 up 6 days, 13:37,  1 user,  load average: 69.98, 60.58, 49.69
Filesystem      Size  Used Avail Use% Mounted on
/dev/nvme0n1p5  480G  375G   81G  83% /
COMMAND ['build/tests/find_bench_test']
P1R10 self-check: PASS delayed_ack_rejected=1 cancellation_CPU_interval_rejected=1 boundary=1 median=1 tail=1
find fixture FAIL: missing start timeout
find fixture FAIL: missing completion timeout
find fixture FAIL: stuck shutdown deadline; terminating fixture (no shutdown join)
fixture missing/wrong size: build/find-bench-test-K2KkTG
fixture missing/wrong size: build/find-bench-test-K2KkTG
fixture missing/wrong size: build/find-bench-test-K2KkTG
P1R11 self-check: PASS missing_start=2 missing_completion=2 shutdown=2
SKIP name=G6_dense_a file=build/find-bench-test-K2KkTG (quick: optional all-a fixture absent; full mode requires it)
SKIP name=G6v_a31b file=build/find-bench-test-K2KkTG (quick: optional all-a fixture absent; full mode requires it)
SKIP name=G6v_first_last_middle file=build/find-bench-test-K2KkTG (quick: optional all-a fixture absent; full mode requires it)
SKIP name=G6v_first_last_early file=build/find-bench-test-K2KkTG (quick: optional all-a fixture absent; full mode requires it)
SKIP name=G6c file=build/find-bench-test-K2KkTG (quick: optional all-a fixture absent; full mode requires it)
find_bench_test: ok (G6c misses rejected; missing events/shutdown bounded; absent optional quick fixtures skipped)
AFTER
Not charging
 08:49:02 up 6 days, 13:37,  1 user,  load average: 69.98, 60.58, 49.69
Filesystem      Size  Used Avail Use% Mounted on
/dev/nvme0n1p5  480G  375G   81G  83% /
RESULT exit=0 wall_ns=57693458 (M); monotonic end-start
```

## Green: standalone quick find

Evidence: (M)[AC].

```text
BEFORE
Not charging
 09:00:36 up 6 days, 13:48,  1 user,  load average: 55.65, 61.40, 57.00
Filesystem      Size  Used Avail Use% Mounted on
/dev/nvme0n1p5  480G  380G   76G  84% /
COMMAND ['build/bench/find_bench', '--quick']
POWER status=Not charging (M)[AC] indicative: concurrent workers; default full gates, quick scaled TRACK
ROW name=G6_ERROR bytes=8388608 expected=14060 stored=4096 first=268 last_stored=2468428 mapping=NEW cache=read-warm kind=TRACK
BENCH name=G6_ERROR n=3 p50=14500509 p99=23785645 ci95=[13531119,23785645] gate_p50=0 gate_p99=0 pass=1 power=[AC]
FIND G6_ERROR TRACK correctness=PASS (M)[AC]; limits=(G); unit=ns
ROW name=G6_newline bytes=8388608 expected=69905 stored=4096 first=119 last_stored=491519 mapping=NEW cache=read-warm kind=TRACK
BENCH name=G6_newline n=3 p50=16343621 p99=22216394 ci95=[2594167,22216394] gate_p50=0 gate_p99=0 pass=1 power=[AC]
FIND G6_newline TRACK correctness=PASS (M)[AC]; limits=(G); unit=ns
SKIP name=G6_dense_a file=/tmp/edit-corpus/all_a_1g.txt (quick: optional all-a fixture absent; full mode requires it)
SKIP name=G6v_a31b file=/tmp/edit-corpus/all_a_1g.txt (quick: optional all-a fixture absent; full mode requires it)
SKIP name=G6v_first_last_middle file=/tmp/edit-corpus/all_a_1g.txt (quick: optional all-a fixture absent; full mode requires it)
SKIP name=G6v_first_last_early file=/tmp/edit-corpus/all_a_1g.txt (quick: optional all-a fixture absent; full mode requires it)
ROW name=periodic_dense bytes=8388608 expected=262143 stored=4096 first=6 last_stored=131046 mapping=NEW cache=read-warm kind=TRACK
BENCH name=periodic_dense n=3 p50=79312150 p99=80017596 ci95=[77958728,80017596] gate_p50=0 gate_p99=0 pass=1 power=[AC]
FIND periodic_dense TRACK correctness=PASS (M)[AC]; limits=(G); unit=ns
ROW name=regex_prefix bytes=8388608 expected=14060 stored=4096 first=268 last_stored=2468428 mapping=NEW cache=read-warm kind=TRACK
BENCH name=regex_prefix n=3 p50=103316827 p99=115013448 ci95=[74462990,115013448] gate_p50=0 gate_p99=0 pass=1 power=[AC]
FIND regex_prefix TRACK correctness=PASS (M)[AC]; limits=(G); unit=ns
ROW name=regex_no_prefix bytes=8388608 expected=14060 stored=4096 first=268 last_stored=2468428 mapping=NEW cache=read-warm kind=TRACK
BENCH name=regex_no_prefix n=3 p50=7769290925 p99=9289015248 ci95=[7277221160,9289015248] gate_p50=0 gate_p99=0 pass=1 power=[AC]
FIND regex_no_prefix TRACK correctness=PASS (M)[AC]; limits=(G); unit=ns
SKIP name=G6c file=/tmp/edit-corpus/all_a_1g.txt (quick: optional all-a fixture absent; full mode requires it)
find_bench: rows printed (MISS is a note) (quick: limits off)
AFTER
Not charging
 09:01:03 up 6 days, 13:49,  1 user,  load average: 54.97, 60.82, 56.93
Filesystem      Size  Used Avail Use% Mounted on
/dev/nvme0n1p5  480G  380G   76G  84% /
RESULT exit=0 wall_ns=26139024550 (M); monotonic end-start
```

## Post-change default make bench attempt: complete output through display-lock failure

Evidence: (M)[AC].

```text
BEFORE
Not charging
 08:49:11 up 6 days, 13:37,  1 user,  load average: 69.19, 60.71, 49.85
Filesystem      Size  Used Avail Use% Mounted on
/dev/nvme0n1p5  480G  375G   81G  83% /
COMMAND ['sh', '-c', 'ev0_runtime=$(mktemp -d); trap \'rm -rf "$ev0_runtime"\' EXIT; XDG_RUNTIME_DIR="$ev0_runtime" DISPLAY=:99 make bench']
== build/bench/base_bench 
BENCH name=base_pool n=1000000 p50=44 p99=62
== build/bench/config_bench 
config_bench: parse (M)[AC] power=Not charging load1=69.19 n=77 derivation=one_sample_per_fixture_byte fixture_bytes=77 p50_ns=5798 p99_ns=69029 verdict=TRACK
== build/bench/editor_bench --track --no-idle --no-p4 --serial-only --keys=64
STAMP (M)[AC] BAT0=Not charging load1=69.19 shared box
BACKEND requested=null actual=null init_error=0 TARGET A=2880x1800 font_px=30 (M)[AC]
POSITION null byte=966367641 index=progressing workload=serial edits=alternating_insert_backspace
BENCH name=editor_null_serial_injection_submit n=64 required_n=10000 p50=8318292 p99=18344672 ci95_p50=[7283837,10380828] ci95_p99=[14776773,18344672] gate_p50=0 gate_p99=0 dropped=0 (M)[AC] power=Not charging load1=69.19 verdict=TRACK
BENCH name=editor_null_serial_dequeue_submit n=64 required_n=10000 p50=7195384 p99=14676187 ci95_p50=[5178186,8235855] ci95_p99=[14103598,14676187] gate_p50=0 gate_p99=0 dropped=0 (M)[AC] power=Not charging load1=69.19 verdict=TRACK
BENCH name=editor_null_serial_injection_T4_G1 n=64 required_n=10000 p50=9740096 p99=19367460 ci95_p50=[8746278,11814946] ci95_p99=[15765866,19367460] gate_p50=1000000 gate_p99=2000000 dropped=0 (M)[AC] power=Not charging load1=69.19 verdict=TRACK
G1 null workload=serial keys=64 completed=64 arrivals_active=0 coalesced=0 foreign_input=0 allocations=0 mutations=64 journal=64 structural=OK mode=TRACK (M)[AC]
STAMP (M)[AC] BAT0=Not charging load1=68.69 shared box
BACKEND requested=raster actual=cpu-raster init_error=0 TARGET A=2880x1800 font_px=30 (M)[AC]
POSITION raster byte=966367641 index=progressing workload=serial edits=alternating_insert_backspace
BENCH name=editor_raster_serial_injection_submit n=64 required_n=10000 p50=35489564 p99=59459183 ci95_p50=[33884523,39616603] ci95_p99=[51382145,59459183] gate_p50=0 gate_p99=0 dropped=0 (M)[AC] power=Not charging load1=68.69 verdict=TRACK
BENCH name=editor_raster_serial_dequeue_submit n=64 required_n=10000 p50=34786365 p99=59322995 ci95_p50=[33720227,39474800] ci95_p99=[51229064,59322995] gate_p50=0 gate_p99=0 dropped=0 (M)[AC] power=Not charging load1=68.69 verdict=TRACK
BENCH name=editor_raster_serial_injection_T4_G1 n=64 required_n=10000 p50=39657907 p99=69318021 ci95_p50=[36952963,42696654] ci95_p99=[60433202,69318021] gate_p50=1000000 gate_p99=2000000 dropped=0 (M)[AC] power=Not charging load1=68.69 verdict=TRACK
G1 raster workload=serial keys=64 completed=64 arrivals_active=64 coalesced=0 foreign_input=0 allocations=0 mutations=64 journal=64 structural=OK mode=TRACK (M)[AC]
STAMP (M)[AC] BAT0=Not charging load1=68.69 shared box
sublimite: EGL init failed: EGL/GL/Present unavailable (code=-10); using raster; no retry
BACKEND requested=gl actual=cpu-raster init_error=-10 TARGET A=2880x1800 font_px=30 (M)[AC]
POSITION gl_fallback_raster byte=966367641 index=progressing workload=serial edits=alternating_insert_backspace
BENCH name=editor_gl_fallback_raster_serial_injection_submit n=64 required_n=10000 p50=32220037 p99=54065794 ci95_p50=[31495194,34120186] ci95_p99=[45889893,54065794] gate_p50=0 gate_p99=0 dropped=0 (M)[AC] power=Not charging load1=68.69 verdict=TRACK
BENCH name=editor_gl_fallback_raster_serial_dequeue_submit n=64 required_n=10000 p50=32064427 p99=53956716 ci95_p50=[31365775,33969640] ci95_p99=[45795263,53956716] gate_p50=0 gate_p99=0 dropped=0 (M)[AC] power=Not charging load1=68.69 verdict=TRACK
BENCH name=editor_gl_fallback_raster_serial_injection_T4_G1 n=64 required_n=10000 p50=35089588 p99=62919906 ci95_p50=[34065848,39005529] ci95_p99=[50636045,62919906] gate_p50=1000000 gate_p99=2000000 dropped=0 (M)[AC] power=Not charging load1=68.69 verdict=TRACK
G1 gl_fallback_raster workload=serial keys=64 completed=64 arrivals_active=64 coalesced=0 foreign_input=0 allocations=0 mutations=64 journal=64 structural=OK mode=TRACK (M)[AC]
== build/bench/editor_large_bench --attribute-find-floor
waiting for display :99 (held by 2053474)
STAMP (M)[AC] BAT0=Not charging load1=68.44 shared box mode=TRACK
ATTR find_floor sample=0 variant=flat kernel_ns=772454596 total=298542 (M)[AC]
ATTR find_floor sample=0 variant=snapshot kernel_ns=1160883802 total=298542 (M)[AC]
ATTR find_floor sample=1 variant=flat kernel_ns=507626955 total=298542 (M)[AC]
ATTR find_floor sample=1 variant=snapshot kernel_ns=1994856722 total=298542 (M)[AC]
ATTR find_floor sample=2 variant=flat kernel_ns=1457936595 total=298542 (M)[AC]
ATTR find_floor sample=2 variant=snapshot kernel_ns=1728277695 total=298542 (M)[AC]
ATTR find_floor sample=3 variant=flat kernel_ns=1004759572 total=298542 (M)[AC]
ATTR find_floor sample=3 variant=snapshot kernel_ns=780287341 total=298542 (M)[AC]
ATTR find_floor sample=4 variant=flat kernel_ns=609263670 total=298542 (M)[AC]
ATTR find_floor sample=4 variant=snapshot kernel_ns=993093044 total=298542 (M)[AC]
BENCH name=editor_large_log_1g_find_flat_kernel_floor_TRACK n=5 p50=772454596 p99=1457936595 ci95=[507626955,1457936595] gate_p50=0 gate_p99=0 pass=1 power=[AC]
BENCH name=editor_large_log_1g_find_snapshot_kernel_TRACK n=5 p50=1160883802 p99=1994856722 ci95=[780287341,1994856722] gate_p50=0 gate_p99=0 pass=1 power=[AC]
== build/bench/fif_bench 
fif_bench: (M)[AC] power=Not charging load1=69.77 fixture=/tmp/edit-corpus
fif_bench: bytes=339738656 files=7 truncated=1 errors=0 code=0 targets=(E)G6_scaled_by_read_bytes
BENCH name=G6f_walk_search_tracked n=100 p50=3017979394 p99=5970451665 ci95=[2771523968,3252600866] gate_p50=27179092 gate_p99=42467332 pass=0 MISS power=[AC]
== build/bench/file_bench 
waiting for display :99 (held by 2061327)
file_bench power=Not charging [AC] load1=68.22 mode=GATE; real CPU raster submit on DISPLAY=:99
BEGIN G5_warm_sparse10g busy0 samples=100
PROGRESS G5_sparse10g busy0 done=10/100
PROGRESS G5_sparse10g busy0 done=20/100
PROGRESS G5_sparse10g busy0 done=30/100
PROGRESS G5_sparse10g busy0 done=40/100
PROGRESS G5_sparse10g busy0 done=50/100
PROGRESS G5_sparse10g busy0 done=60/100
PROGRESS G5_sparse10g busy0 done=70/100
PROGRESS G5_sparse10g busy0 done=80/100
PROGRESS G5_sparse10g busy0 done=90/100
PROGRESS G5_sparse10g busy0 done=100/100
TRACK primitive_prefix_sparse10g_busy0 (M)[AC] load1=67.88 n=100 p50=2.792 ms p99=8.588 ms gate=0/0 ms (G)
MISS G5_warm_sparse10g_busy0 (M)[AC] load1=67.88 n=100 p50=14.848 ms p99=22.760 ms gate=6/9 ms (G)
BEGIN G5_warm_oneline1g busy0 samples=100
PROGRESS G5_oneline1g busy0 done=10/100
PROGRESS G5_oneline1g busy0 done=20/100
PROGRESS G5_oneline1g busy0 done=30/100
PROGRESS G5_oneline1g busy0 done=40/100
PROGRESS G5_oneline1g busy0 done=50/100
PROGRESS G5_oneline1g busy0 done=60/100
PROGRESS G5_oneline1g busy0 done=70/100
PROGRESS G5_oneline1g busy0 done=80/100
PROGRESS G5_oneline1g busy0 done=90/100
PROGRESS G5_oneline1g busy0 done=100/100
TRACK primitive_prefix_oneline1g_busy0 (M)[AC] load1=68.21 n=100 p50=2.742 ms p99=9.957 ms gate=0/0 ms (G)
MISS G5_warm_oneline1g_busy0 (M)[AC] load1=68.21 n=100 p50=14.083 ms p99=39.445 ms gate=6/9 ms (G)
BEGIN G5_warm_log1g busy0 samples=100
PROGRESS G5_log1g busy0 done=10/100
PROGRESS G5_log1g busy0 done=20/100
PROGRESS G5_log1g busy0 done=30/100
PROGRESS G5_log1g busy0 done=40/100
PROGRESS G5_log1g busy0 done=50/100
PROGRESS G5_log1g busy0 done=60/100
PROGRESS G5_log1g busy0 done=70/100
PROGRESS G5_log1g busy0 done=80/100
PROGRESS G5_log1g busy0 done=90/100
PROGRESS G5_log1g busy0 done=100/100
TRACK primitive_prefix_log1g_busy0 (M)[AC] load1=68.83 n=100 p50=4.375 ms p99=12.692 ms gate=0/0 ms (G)
MISS G5_warm_log1g_busy0 (M)[AC] load1=68.83 n=100 p50=20.673 ms p99=43.328 ms gate=6/9 ms (G)
BEGIN G5_warm_unicode_copy busy0 samples=100
PROGRESS G5_unicode_copy busy0 done=10/100
PROGRESS G5_unicode_copy busy0 done=20/100
PROGRESS G5_unicode_copy busy0 done=30/100
PROGRESS G5_unicode_copy busy0 done=40/100
PROGRESS G5_unicode_copy busy0 done=50/100
PROGRESS G5_unicode_copy busy0 done=60/100
PROGRESS G5_unicode_copy busy0 done=70/100
PROGRESS G5_unicode_copy busy0 done=80/100
PROGRESS G5_unicode_copy busy0 done=90/100
PROGRESS G5_unicode_copy busy0 done=100/100
TRACK primitive_prefix_unicode_copy_busy0 (M)[AC] load1=68.69 n=100 p50=7.151 ms p99=23.701 ms gate=0/0 ms (G)
MISS G5_warm_unicode_copy_busy0 (M)[AC] load1=68.69 n=100 p50=24.859 ms p99=47.773 ms gate=6/9 ms (G)
BEGIN save bytes=1048576 busy0 fresh=1 cold=0 samples=100
PROGRESS save busy0 done=10/100
PROGRESS save busy0 done=20/100
PROGRESS save busy0 done=30/100
PROGRESS save busy0 done=40/100
PROGRESS save busy0 done=50/100
PROGRESS save busy0 done=60/100
PROGRESS save busy0 done=70/100
PROGRESS save busy0 done=80/100
PROGRESS save busy0 done=90/100
PROGRESS save busy0 done=100/100
MISS G8s_fresh_fragmented_status_1MiB_busy0 (M)[AC] load1=68.55 n=100 p50=3.827 ms p99=20.257 ms gate=2/5 ms (G)
MISS G8d_1MiB_warm_source (M)[AC] load1=68.55 n=100 p50=44.231 ms p99=133.099 ms gate=10/50 ms (G)
BEGIN G5_warm_sparse10g busy1 samples=100
PROGRESS G5_sparse10g busy1 done=10/100
PROGRESS G5_sparse10g busy1 done=20/100
PROGRESS G5_sparse10g busy1 done=30/100
PROGRESS G5_sparse10g busy1 done=40/100
PROGRESS G5_sparse10g busy1 done=50/100
PROGRESS G5_sparse10g busy1 done=60/100
PROGRESS G5_sparse10g busy1 done=70/100
PROGRESS G5_sparse10g busy1 done=80/100
PROGRESS G5_sparse10g busy1 done=90/100
PROGRESS G5_sparse10g busy1 done=100/100
TRACK primitive_prefix_sparse10g_busy1 (M)[AC] load1=69.79 n=100 p50=2.968 ms p99=15.507 ms gate=0/0 ms (G)
MISS G5_warm_sparse10g_busy1 (M)[AC] load1=69.79 n=100 p50=13.528 ms p99=27.005 ms gate=6/9 ms (G)
BEGIN G5_warm_oneline1g busy1 samples=100
PROGRESS G5_oneline1g busy1 done=10/100
PROGRESS G5_oneline1g busy1 done=20/100
PROGRESS G5_oneline1g busy1 done=30/100
PROGRESS G5_oneline1g busy1 done=40/100
PROGRESS G5_oneline1g busy1 done=50/100
PROGRESS G5_oneline1g busy1 done=60/100
PROGRESS G5_oneline1g busy1 done=70/100
PROGRESS G5_oneline1g busy1 done=80/100
PROGRESS G5_oneline1g busy1 done=90/100
PROGRESS G5_oneline1g busy1 done=100/100
TRACK primitive_prefix_oneline1g_busy1 (M)[AC] load1=69.97 n=100 p50=1.822 ms p99=11.010 ms gate=0/0 ms (G)
MISS G5_warm_oneline1g_busy1 (M)[AC] load1=69.97 n=100 p50=12.137 ms p99=29.538 ms gate=6/9 ms (G)
BEGIN G5_warm_log1g busy1 samples=100
PROGRESS G5_log1g busy1 done=10/100
PROGRESS G5_log1g busy1 done=20/100
PROGRESS G5_log1g busy1 done=30/100
PROGRESS G5_log1g busy1 done=40/100
PROGRESS G5_log1g busy1 done=50/100
PROGRESS G5_log1g busy1 done=60/100
PROGRESS G5_log1g busy1 done=70/100
PROGRESS G5_log1g busy1 done=80/100
PROGRESS G5_log1g busy1 done=90/100
PROGRESS G5_log1g busy1 done=100/100
TRACK primitive_prefix_log1g_busy1 (M)[AC] load1=68.85 n=100 p50=2.651 ms p99=9.093 ms gate=0/0 ms (G)
MISS G5_warm_log1g_busy1 (M)[AC] load1=68.85 n=100 p50=17.055 ms p99=36.514 ms gate=6/9 ms (G)
BEGIN G5_warm_unicode_copy busy1 samples=100
PROGRESS G5_unicode_copy busy1 done=10/100
PROGRESS G5_unicode_copy busy1 done=20/100
PROGRESS G5_unicode_copy busy1 done=30/100
PROGRESS G5_unicode_copy busy1 done=40/100
PROGRESS G5_unicode_copy busy1 done=50/100
PROGRESS G5_unicode_copy busy1 done=60/100
PROGRESS G5_unicode_copy busy1 done=70/100
PROGRESS G5_unicode_copy busy1 done=80/100
PROGRESS G5_unicode_copy busy1 done=90/100
PROGRESS G5_unicode_copy busy1 done=100/100
TRACK primitive_prefix_unicode_copy_busy1 (M)[AC] load1=68.85 n=100 p50=5.833 ms p99=19.450 ms gate=0/0 ms (G)
MISS G5_warm_unicode_copy_busy1 (M)[AC] load1=68.85 n=100 p50=18.085 ms p99=32.606 ms gate=6/9 ms (G)
BEGIN save bytes=1048576 busy1 fresh=1 cold=0 samples=100
PROGRESS save busy1 done=10/100
PROGRESS save busy1 done=20/100
PROGRESS save busy1 done=30/100
PROGRESS save busy1 done=40/100
PROGRESS save busy1 done=50/100
PROGRESS save busy1 done=60/100
PROGRESS save busy1 done=70/100
PROGRESS save busy1 done=80/100
PROGRESS save busy1 done=90/100
PROGRESS save busy1 done=100/100
MISS G8s_fresh_fragmented_status_1MiB_busy1 (M)[AC] load1=67.93 n=100 p50=0.524 ms p99=9.635 ms gate=2/5 ms (G)
BEGIN G5_warm_sparse10g busy3 samples=100
PROGRESS G5_sparse10g busy3 done=10/100
PROGRESS G5_sparse10g busy3 done=20/100
PROGRESS G5_sparse10g busy3 done=30/100
PROGRESS G5_sparse10g busy3 done=40/100
PROGRESS G5_sparse10g busy3 done=50/100
PROGRESS G5_sparse10g busy3 done=60/100
PROGRESS G5_sparse10g busy3 done=70/100
PROGRESS G5_sparse10g busy3 done=80/100
PROGRESS G5_sparse10g busy3 done=90/100
PROGRESS G5_sparse10g busy3 done=100/100
TRACK primitive_prefix_sparse10g_busy3 (M)[AC] load1=67.77 n=100 p50=3.697 ms p99=20.522 ms gate=0/0 ms (G)
MISS G5_warm_sparse10g_busy3 (M)[AC] load1=67.77 n=100 p50=24.090 ms p99=49.723 ms gate=6/9 ms (G)
BEGIN G5_warm_oneline1g busy3 samples=100
PROGRESS G5_oneline1g busy3 done=10/100
PROGRESS G5_oneline1g busy3 done=20/100
PROGRESS G5_oneline1g busy3 done=30/100
PROGRESS G5_oneline1g busy3 done=40/100
PROGRESS G5_oneline1g busy3 done=50/100
PROGRESS G5_oneline1g busy3 done=60/100
PROGRESS G5_oneline1g busy3 done=70/100
PROGRESS G5_oneline1g busy3 done=80/100
PROGRESS G5_oneline1g busy3 done=90/100
PROGRESS G5_oneline1g busy3 done=100/100
TRACK primitive_prefix_oneline1g_busy3 (M)[AC] load1=67.31 n=100 p50=3.726 ms p99=21.432 ms gate=0/0 ms (G)
MISS G5_warm_oneline1g_busy3 (M)[AC] load1=67.31 n=100 p50=22.492 ms p99=48.894 ms gate=6/9 ms (G)
BEGIN G5_warm_log1g busy3 samples=100
PROGRESS G5_log1g busy3 done=10/100
PROGRESS G5_log1g busy3 done=20/100
PROGRESS G5_log1g busy3 done=30/100
PROGRESS G5_log1g busy3 done=40/100
PROGRESS G5_log1g busy3 done=50/100
PROGRESS G5_log1g busy3 done=60/100
PROGRESS G5_log1g busy3 done=70/100
PROGRESS G5_log1g busy3 done=80/100
PROGRESS G5_log1g busy3 done=90/100
PROGRESS G5_log1g busy3 done=100/100
TRACK primitive_prefix_log1g_busy3 (M)[AC] load1=66.41 n=100 p50=3.316 ms p99=14.695 ms gate=0/0 ms (G)
MISS G5_warm_log1g_busy3 (M)[AC] load1=66.41 n=100 p50=22.923 ms p99=43.093 ms gate=6/9 ms (G)
BEGIN G5_warm_unicode_copy busy3 samples=100
PROGRESS G5_unicode_copy busy3 done=10/100
PROGRESS G5_unicode_copy busy3 done=20/100
PROGRESS G5_unicode_copy busy3 done=30/100
PROGRESS G5_unicode_copy busy3 done=40/100
PROGRESS G5_unicode_copy busy3 done=50/100
PROGRESS G5_unicode_copy busy3 done=60/100
PROGRESS G5_unicode_copy busy3 done=70/100
PROGRESS G5_unicode_copy busy3 done=80/100
PROGRESS G5_unicode_copy busy3 done=90/100
PROGRESS G5_unicode_copy busy3 done=100/100
TRACK primitive_prefix_unicode_copy_busy3 (M)[AC] load1=66.29 n=100 p50=5.926 ms p99=17.714 ms gate=0/0 ms (G)
MISS G5_warm_unicode_copy_busy3 (M)[AC] load1=66.29 n=100 p50=19.972 ms p99=46.997 ms gate=6/9 ms (G)
BEGIN save bytes=1048576 busy3 fresh=1 cold=0 samples=100
PROGRESS save busy3 done=10/100
PROGRESS save busy3 done=20/100
PROGRESS save busy3 done=30/100
PROGRESS save busy3 done=40/100
PROGRESS save busy3 done=50/100
PROGRESS save busy3 done=60/100
PROGRESS save busy3 done=70/100
PROGRESS save busy3 done=80/100
PROGRESS save busy3 done=90/100
PROGRESS save busy3 done=100/100
MISS G8s_fresh_fragmented_status_1MiB_busy3 (M)[AC] load1=65.07 n=100 p50=1.272 ms p99=8.783 ms gate=2/5 ms (G)
BEGIN save bytes=1048576 busy0 fresh=0 cold=0 samples=100
PROGRESS save busy0 done=10/100
PROGRESS save busy0 done=20/100
PROGRESS save busy0 done=30/100
PROGRESS save busy0 done=40/100
PROGRESS save busy0 done=50/100
PROGRESS save busy0 done=60/100
PROGRESS save busy0 done=70/100
PROGRESS save busy0 done=80/100
PROGRESS save busy0 done=90/100
PROGRESS save busy0 done=100/100
TRACK primitive_unchanged_save_enqueue_1MiB_busy0 (M)[AC] load1=65.30 n=100 p50=0.015 ms p99=5.730 ms gate=0/0 ms (G)
BEGIN save bytes=1073741824 busy0 fresh=1 cold=0 samples=100
PROGRESS save busy0 done=10/100
PROGRESS save busy0 done=20/100
PROGRESS save busy0 done=30/100
PROGRESS save busy0 done=40/100
PROGRESS save busy0 done=50/100
PROGRESS save busy0 done=60/100
PROGRESS save busy0 done=70/100
PROGRESS save busy0 done=80/100
PROGRESS save busy0 done=90/100
PROGRESS save busy0 done=100/100
MISS G8s_fresh_fragmented_status_1GiB_busy0 (M)[AC] load1=31.99 n=100 p50=3.987 ms p99=16.811 ms gate=2/5 ms (G)
MISS G8d_1GiB_warm_source (M)[AC] load1=31.99 n=100 p50=4682.546 ms p99=8881.435 ms gate=1500/2500 ms (G)
MANUAL cold rows unmeasured: --cold evicts + verifies residency before EACH sample; G8d cold 2200/3000 ms (G); see docs/decisions/P1.7f.md
== build/bench/find_bench --quick
POWER status=Not charging (M)[AC] indicative: concurrent workers; default full gates, quick scaled TRACK
ROW name=G6_ERROR bytes=8388608 expected=14060 stored=4096 first=268 last_stored=2468428 mapping=NEW cache=read-warm kind=TRACK
BENCH name=G6_ERROR n=3 p50=3194626 p99=3203513 ci95=[3164127,3203513] gate_p50=0 gate_p99=0 pass=1 power=[AC]
FIND G6_ERROR TRACK correctness=PASS (M)[AC]; limits=(G); unit=ns
ROW name=G6_newline bytes=8388608 expected=69905 stored=4096 first=119 last_stored=491519 mapping=NEW cache=read-warm kind=TRACK
BENCH name=G6_newline n=3 p50=2659949 p99=2966980 ci95=[2462429,2966980] gate_p50=0 gate_p99=0 pass=1 power=[AC]
FIND G6_newline TRACK correctness=PASS (M)[AC]; limits=(G); unit=ns
SKIP name=G6_dense_a file=/tmp/edit-corpus/all_a_1g.txt (quick: optional all-a fixture absent; full mode requires it)
SKIP name=G6v_a31b file=/tmp/edit-corpus/all_a_1g.txt (quick: optional all-a fixture absent; full mode requires it)
SKIP name=G6v_first_last_middle file=/tmp/edit-corpus/all_a_1g.txt (quick: optional all-a fixture absent; full mode requires it)
SKIP name=G6v_first_last_early file=/tmp/edit-corpus/all_a_1g.txt (quick: optional all-a fixture absent; full mode requires it)
ROW name=periodic_dense bytes=8388608 expected=262143 stored=4096 first=6 last_stored=131046 mapping=NEW cache=read-warm kind=TRACK
BENCH name=periodic_dense n=3 p50=18364346 p99=22487740 ci95=[14785443,22487740] gate_p50=0 gate_p99=0 pass=1 power=[AC]
FIND periodic_dense TRACK correctness=PASS (M)[AC]; limits=(G); unit=ns
ROW name=regex_prefix bytes=8388608 expected=14060 stored=4096 first=268 last_stored=2468428 mapping=NEW cache=read-warm kind=TRACK
BENCH name=regex_prefix n=3 p50=24964794 p99=25558573 ci95=[16905722,25558573] gate_p50=0 gate_p99=0 pass=1 power=[AC]
FIND regex_prefix TRACK correctness=PASS (M)[AC]; limits=(G); unit=ns
ROW name=regex_no_prefix bytes=8388608 expected=14060 stored=4096 first=268 last_stored=2468428 mapping=NEW cache=read-warm kind=TRACK
BENCH name=regex_no_prefix n=3 p50=2349648450 p99=2694019834 ci95=[1869039867,2694019834] gate_p50=0 gate_p99=0 pass=1 power=[AC]
FIND regex_no_prefix TRACK correctness=PASS (M)[AC]; limits=(G); unit=ns
SKIP name=G6c file=/tmp/edit-corpus/all_a_1g.txt (quick: optional all-a fixture absent; full mode requires it)
find_bench: rows printed (MISS is a note) (quick: limits off)
== build/bench/findui_bench 
findui_bench: power=Not charging [AC] load1=30.39 fixture=/tmp/edit-corpus/log_1g.txt track=0
BENCH name=G6c_findui_keystroke_logical_ack n=10000 required_n=10000 p50=2757 p99=25106 ci95_p50=[2706,2805] ci95_p99=[23661,26626] gate_p50=1000000 gate_p99=5000000 dropped=0 (M)[AC] power=Not charging load1=30.39 verdict=PASS
findui_scan_thread_check (M)[AC] load1=30.39 calling_thread_scans=0 expected=(G)0
== build/bench/font_bench 
BENCH name=font_raster_mallocs n=20000 value=0 gate=0 guard=1 tag=(G)(M)[AC] TRACK
BENCH name=font_raster_TRACK n=20000 p50=8429 p99=15941 ci95=[8426,8433] gate_p50=0 gate_p99=0 pass=1 power=[AC]
BENCH name=font_raster_correctness failures=0 nonempty=1 guard=1 pass=1
BENCH name=font_fallback_discover n=1 ns=98702402 gate=none fontconfig=1 cjk=/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc emoji=/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf tag=(M)[AC] TRACK
BENCH name=font_raster_cjk_4E2D_TRACK n=20000 p50=9758 p99=17419 ci95=[9742,9776] gate_p50=0 gate_p99=0 pass=1 power=[AC]
BENCH name=font_cache_negative_pressure_TRACK n=8192 p50=450 p99=667 ci95=[449,451] gate_p50=500000 gate_p99=500000 pass=1 power=[AC]
BENCH name=font_cluster_varied_cold_TRACK n=768 p50=6273 p99=83857 ci95=[6092,6399] gate_p50=500000 gate_p99=500000 pass=1 power=[AC]
BENCH name=font_cluster_long_extension_slice_TRACK n=8192 p50=262600 p99=501464 ci95=[262447,262754] gate_p50=500000 gate_p99=500000 pass=0 MISS power=[AC]
BENCH name=font_cluster_cold_TRACK n=3 p50=53216 p99=69324 ci95=[35973,69324] gate_p50=500000 gate_p99=500000 pass=1 power=[AC]
BENCH name=font_cluster_cached_TRACK n=20000 p50=109 p99=141 ci95=[109,109] gate_p50=500000 gate_p99=500000 pass=1 power=[AC]
BENCH name=font_cluster_mallocs n=20000 value=0 gate=0 guard=1 tag=(G)(M)[AC] TRACK
== build/bench/gl_bench 
waiting for display :99 (held by 2077101)
gl_bench: display lock timeout: display :99 (held by 2079373)
make: *** [Makefile:220: bench] Error 1
AFTER
Not charging
 09:17:51 up 6 days, 14:05,  1 user,  load average: 41.62, 41.62, 45.44
Filesystem      Size  Used Avail Use% Mounted on
/dev/nvme0n1p5  480G  382G   74G  84% /
RESULT exit=2 wall_ns=1719998963485 (M); monotonic end-start
```

Total wall **1719.998963485 s (M)[AC]** = `1719998963485 ns / 1,000,000,000 ns/s (P)`; the adjacent BAT0 and uptime/load stamps above apply. Includes lock waits and concurrent work as stated above.

## Sanitizers: find_bench_test

Evidence: (M)[AC].

```text
BEFORE
Not charging
 09:05:20 up 6 days, 13:53,  1 user,  load average: 44.71, 53.05, 54.78
Filesystem      Size  Used Avail Use% Mounted on
/dev/nvme0n1p5  480G  383G   73G  84% /
COMMAND ['env', 'ASAN_OPTIONS=detect_leaks=1', 'build/san/tests/find_bench_test']
P1R10 self-check: PASS delayed_ack_rejected=1 cancellation_CPU_interval_rejected=1 boundary=1 median=1 tail=1
find fixture FAIL: missing start timeout
find fixture FAIL: missing completion timeout
find fixture FAIL: stuck shutdown deadline; terminating fixture (no shutdown join)
fixture missing/wrong size: build/find-bench-test-39LCLJ
fixture missing/wrong size: build/find-bench-test-39LCLJ
fixture missing/wrong size: build/find-bench-test-39LCLJ
P1R11 self-check: PASS missing_start=2 missing_completion=2 shutdown=2
SKIP name=G6_dense_a file=build/find-bench-test-39LCLJ (quick: optional all-a fixture absent; full mode requires it)
SKIP name=G6v_a31b file=build/find-bench-test-39LCLJ (quick: optional all-a fixture absent; full mode requires it)
SKIP name=G6v_first_last_middle file=build/find-bench-test-39LCLJ (quick: optional all-a fixture absent; full mode requires it)
SKIP name=G6v_first_last_early file=build/find-bench-test-39LCLJ (quick: optional all-a fixture absent; full mode requires it)
SKIP name=G6c file=build/find-bench-test-39LCLJ (quick: optional all-a fixture absent; full mode requires it)
find_bench_test: ok (G6c misses rejected; missing events/shutdown bounded; absent optional quick fixtures skipped)
AFTER
Not charging
 09:05:20 up 6 days, 13:53,  1 user,  load average: 44.71, 53.05, 54.78
Filesystem      Size  Used Avail Use% Mounted on
/dev/nvme0n1p5  480G  383G   73G  84% /
RESULT exit=0 wall_ns=108301427 (M); monotonic end-start
```

## Sanitizers: harness_test

Evidence: (M)[AC].

```text
BEFORE
Not charging
 09:05:23 up 6 days, 13:53,  1 user,  load average: 43.45, 52.65, 54.64
Filesystem      Size  Used Avail Use% Mounted on
/dev/nvme0n1p5  480G  383G   74G  84% /
COMMAND ['sh', '-c', 'ev0_runtime=$(mktemp -d); trap \'rm -rf "$ev0_runtime"\' EXIT; ASAN_OPTIONS=detect_leaks=1 XDG_RUNTIME_DIR="$ev0_runtime" DISPLAY=:99 build/san/tests/harness_test']
savectl fixture FAIL: monotonic phase deadline; terminating without joins
savectl fixture FAIL: harness missing retirement deadline; terminating fixture (no shutdown join)
savectl fixture FAIL: harness missing retirement signal=9
PROBE fixture phase and retirement deadlines preserve structural failure
PROBE logical_padding slices=513 result=0 first_byte=2048 expected=2048 (G)
STAMP (M)[AC] BAT0=Not charging load1=43.45 shared box
PROBE A runtime=2880x1800 requested=2880x1800 (G)
PROBE G1 injected_wait=20000000 (G) reported=53276157 injection_to_T4=53276157 (M)[AC]
PROBE queued_keys first=2 last=3 dequeue_sequence=3 (M)[AC]
PROBE structural_failure TRACK exit=1 expected=1 (G)
BENCH name=editor_probe_insufficient n=1 required_n=10000 p50=1 p99=1 ci95_p50=[1,1] ci95_p99=[1,1] gate_p50=100 gate_p99=100 dropped=0 (M)[AC] power=Not charging load1=43.45 verdict=REFUSED (insufficient samples; no verdict)
PROBE insufficient_samples exit=0 expected=0 (a REFUSED row is a note) (G)
PROBE active_frame keys=4 completed=4 coalesced=3 (M)[AC]
PROBE structural counts, allocation, missing/duplicate frame failures remain fatal in TRACK
PROBE scan load: both endpoints stamped, miss prints MISS exit 0, invalid samples fatal
STAMP (M)[AC] BAT0=Not charging load1=43.45 shared box
BENCH name=editor_null_100tabs_ingress_T5_G3 n=10000 required_n=10000 p50=10780380 p99=41817613 ci95_p50=[10747928,10815280] ci95_p99=[40294881,44648481] gate_p50=5000000 gate_p99=5560000 dropped=0 (M)[AC] power=Not charging load1=43.45 verdict=MISS
BENCH name=editor_null_100tabs_minimap_inside_frame n=10000 required_n=10000 p50=552980 p99=3605216 ci95_p50=[551665,554328] ci95_p99=[3533465,3868619] gate_p50=0 gate_p99=500000 dropped=0 (M)[AC] power=Not charging load1=43.45 verdict=MISS
G3 null (M)[AC] load1=43.45 tabs=100 A=2880x1800 switches=10000 allocations=0 minimap_fills=10201 stale=0 mode=GATE
STAMP (M)[AC] BAT0=Not charging load1=31.99 shared box
TARGET A=2880x1800 font_px=30 (M)[AC]
BACKEND requested=null actual=null init_error=0
G11 real_cadence_cpu verdict=REFUSED n=19 required_n=10000 (G); separate due-now guard follows
BENCH name=editor_null_blink_cpu_regression_due_now n=10000 required_n=10000 p50=2854691 p99=3607744 ci95_p50=[2851719,2858017] ci95_p99=[3571987,3654204] gate_p50=0 gate_p99=0 dropped=0 (M)[AC] power=Not charging load1=31.99 verdict=TRACK
G11_schedule_TRACK null process_context_switches=64 (M)[AC] wakeup trace unavailable; qualifying wakeup gate unmeasured
G11 null (M)[AC] load1=31.99 blinks=19 poll_returns=21 wakeups/s=2.091 idle=0 unfocused=0 TRACK (E)<=2/s,0,0 mode=GATE structural=OK
G11 null structural worker_jobs=0 inline_frames=0
harness_test: 20239 checks, 0 failures
AFTER
Not charging
 09:08:25 up 6 days, 13:56,  1 user,  load average: 29.85, 43.40, 50.82
Filesystem      Size  Used Avail Use% Mounted on
/dev/nvme0n1p5  480G  380G   76G  84% /
RESULT exit=0 wall_ns=182033015388 (M); monotonic end-start
```

## Sanitizers: editor_test

Evidence: (M)[AC].

```text
BEFORE
Not charging
 09:05:53 up 6 days, 13:53,  1 user,  load average: 45.17, 52.24, 54.44
Filesystem      Size  Used Avail Use% Mounted on
/dev/nvme0n1p5  480G  382G   74G  84% /
COMMAND ['sh', '-c', 'ev0_runtime=$(mktemp -d); trap \'rm -rf "$ev0_runtime"\' EXIT; ASAN_OPTIONS=detect_leaks=1 XDG_RUNTIME_DIR="$ev0_runtime" DISPLAY=:99 sh tools/with_display_lock.sh "$1"', 'sh', 'build/san/tests/editor_test']
waiting for display :99 (held by 2061509)
trailing typing default: space bg=0x20242c plain=0x20242c marker=0x493038
trailing typing all: space bg=0x20242c plain=0x20242c marker=0x493038
trailing all: completed line marked, typing cell plain passed
trailing selection: empty, partial, reverse and cleared selection passed
trailing none: range computation bypassed passed
edit-97i: resolved-path reuse, full capacity, Ctrl+O, retained text/view/undo and CLI coordinates passed
edit-o20 new: text and find focus PASS
edit-o20 open: text and find focus PASS
edit-o20 save-as: text and find focus PASS
edit-o20 exit: text and find focus PASS
edit-o20 cancel: text and find focus PASS
edit-o20 scroll: text and find focus PASS
edit-o20 open-empty: text and find focus PASS
edit-o20 path-edit: text and find focus PASS
edit-o20 save-source: text and find focus PASS
edit-o20 save-watch: text and find focus PASS
edit-o20 scroll-wrap: text and find focus PASS
edit-o20 save-clean-watch: text and find focus PASS
edit-o20 save-mapped: text and find focus PASS
edit-o20 save-shared: text and find focus PASS
edit-g02 Return/keypad/previous/wrap/pending/empty/Escape passed
edit-n95 find in files: panel, results format, open at hit, F4 cycle, cancel, read-only passed
edit-g02 uncached Shift+keypad Enter selects previous exactly once passed
edit-d5o compose: minimap pixel rows match independent minimap_pixels reference
edit-48u overscan passed
edit-48u chain identity passed
edit-48u chain identity passed
edit-48u empty rows passed
edit-48u panel resize passed
edit-48u drag autoscroll passed
edit-48u drag autoscroll passed
edit-48u Escape retains match passed
edit-m15 gutter: filled viewport ignores stale old EOF passed
edit-m15 gutter: decimal widening ignores old text hit passed
edit-m15 gutter: wrapped continuation uses logical line start passed
edit-dr3 gutter: click/drag/Shift+click select whole lines passed
edit-m15 find: anchor before approximate top is nonfatal passed
edit-73j mouse contract passed
edit-73j wheel contract passed
edit-73j find/replace, disjoint undo/redo and journal recovery passed
edit-73j replacement Escape/error prefixes, undo and recovery passed
edit-73j distant wrapped/clipped find results are visible passed
edit-73j production Unicode body atlas contract passed
edit-73j pending minimap navigation passed
edit-73j protected remote minimap sample: UI compose passed
edit-g11 null typing: keys=95 frames=97 orange_frames=0 stale_frames=96 changed_pending=0 chrome_markers=0 mallocs=0 fill_max_ns=42239
default center: hard lines: PASS
default center: wrapped rows: PASS
default skip: next/wrap/direction/empty selection/soft undo: PASS
default word: alpha beta: PASS
default word: fooBar_baz: PASS
default word: naïve café: PASS
default word: abc: PASS
default goto: prompt/preview/cancel/accept/edit/overflow/soft undo: PASS
default goto: cold index seek across chunk boundary: PASS
default comment: comment caret: PASS
default comment: uncomment caret: PASS
default comment: uncomment compact: PASS
default comment: comment backward lines: PASS
default comment: comment ignores blank: PASS
default comment: comment CRLF: PASS
default comment: uncomment mixed lines: PASS
default comment: comment mixed lines: PASS
default comment: block selection: PASS
default comment: block backward selection: PASS
default comment: block caret: PASS
default comment: unblock selection: PASS
default comment: unblock inner caret: PASS
default comment: block shortcut removes line comment: PASS
default comment: comment mixed tabs aligns columns: PASS
default comment: Python line: PASS
default comment: Python block fallback: PASS
default comment: CSS line fallback: PASS
default comment: HTML line fallback: PASS
default comment: HTML compact removal: PASS
default comment: HTML shared space removal: PASS
default comment: C quoted delimiter: PASS
default comment: C all blank lines: PASS
undo shrink top: follow at EOF: PASS
undo shrink top: scrolled into the edit: PASS
undo shrink top: edit above a scrolled top: PASS
default goto: unwrapped preview/accept scrolls to the target: PASS
default goto: Escape restores the pre-prompt top: PASS
default editing: duplicate line: PASS
default editing: duplicate backward selection: PASS
default editing: delete line: PASS
default editing: delete selected lines: PASS
default editing: move line up: PASS
default editing: move line down: PASS
default editing: move final line up: PASS
default editing: join lines: PASS
default editing: join selection whitespace endpoint: PASS
default editing: insert line after: PASS
default editing: insert line before: PASS
default editing: delete hard BOL: PASS
default editing: delete hard EOL: PASS
default editing: delete EOL chord: PASS
default editing: indent selected lines: PASS
default editing: unindent selected lines: PASS
default editing: shift tab: PASS
default editing: unindent mixed space/tab: PASS
default editing: transpose: PASS
default editing: transpose newline: PASS
default editing: uppercase selection: PASS
default editing: lowercase word: PASS
default editing: uppercase Unicode word boundary: PASS
default editing: sort insensitive: PASS
default editing: sort sensitive: PASS
default editing: uppercase expansion: PASS
default editing: lowercase final sigma: PASS
default editing: duplicate CRLF EOF: PASS
default editing: insert CRLF EOF: PASS
default state: subword selection: PASS
default state: overwrite: PASS
default state: set/select/swap/clear mark: PASS
default state: delete mark/yank: PASS
default state: goto bracket: PASS
default state: select brackets: PASS
default editing: unchanged unindent retains revision/history: PASS
default state: soft undo/redo interleaves selection and text: PASS
default editing: copy/cut current line retains caret and undo selection: PASS
default editing: repeat retains chord arguments and groups undo: PASS
default editing: soft-row and hard-line kill macros have distinct boundaries: PASS
default editing: oversized command and every retained capability notice preserve text/selection/history: PASS
default editing: undo preserves a trusted far anchor without a view continuation: PASS
default state: evicted text markers redo before their selection records: PASS
editor_test: separate Shift chords preserve exact ranges, reset after movement/mutation, and allocate nothing
editor_test: Ctrl+Y redoes inserts/deletes, empty redo repeats the previous edit, Ctrl+Shift+Z still redoes
editor_test: panel shortcuts show a generation-only status and restore the bottom row
edit-bml: CRLF current-line copy/cut/paste, undo/redo, byte-exact save passed
edit-bml: small mixed paste follows CRLF/LF, no-ending fallback, majority/tie passed
edit-bml: in-memory CRLF style survives cutting all text passed
edit-bml: bulk LF expansion, split CRLF, lone CR/invalid UTF-8, undo/redo passed
clipboard malloc guard: 0 allocations (ASan-inert); expected zero by law
clipboard: null round trip, UTF-8/CRLF, replacement, cut/undo, alternate keys, PRIMARY passed
clipboard bulk: worker queue, cross-slice CRLF, replacement/undo, journal, mallocs=0 passed
clipboard middle click: deep horizontal position exact
clipboard PRIMARY: queued selection/collapse/middle paste waits for latest capture, mallocs=0 passed
clipboard middle click: no active tab preserves placeholder
P4-3 section 3: mode=COPY choice=Keep child abrupt exit; selected/unrelated/closed recovery exact
ck4 startup: abrupt exit, same-file discovery, recovered bytes/dirty state before first paint passed
edit-ret: clean open/save append reload, copied/mapped and inactive tabs, view preservation/clamping, undo reset and no submitted prompt passed
edit-1um: null source prompt preserves first-line glyphs, uses bottom status, restores row and allocates nothing: PASS
P4-3 section 8: complete-record credit bounded; independent durable image exact with dirty frames and occupied bulk worker
P4-3 section 7: healthy flush survives abrupt child exit with exact selection/tab order/active/window metadata
P4-3 section 7: missing named target association persisted independently of empty content BASE
P4-3 section 10: journal-enabled editor rejects FIFO without a writer; child completes before alarm
P4-3 section 4: mode=COPY choice=Keep child abrupt exit; selected/unrelated/closed recovery exact
P4-3 section 4: mode=COPY choice=Reload child abrupt exit; selected/unrelated/closed recovery exact
P4-3 section 4: mode=mapped choice=Keep child abrupt exit; selected/unrelated/closed recovery exact
P4-3 section 4: mode=mapped choice=Reload child abrupt exit; selected/unrelated/closed recovery exact
P4-3 40: cursor/text frames omit unchanged final tab chrome passed
P4-3 41: closed drag owner and switched minimap capture cancel safely passed
P4-3 22: evicted buffer retains held save lease during unrelated typing passed
P4-3 16: reload preserves logical tab, view and real launcher wait passed
P4-3 14: pre-prompt, modified, repeated and aged recovery keys ignored passed
P4-3 13: suspended source preserves tab keys and tab pointer controls passed
P4-3 12: pending COPY admits controls and retains edit owner passed
P4-3 11: failed full-source identity adoption stays owner-local passed
P4-3 11: deferred COPY and inactive ENOSPC errors preserve unrelated editing passed
review 1: native batched key/close, key/focus, key/resize ordered passed
review 2: native burst beyond both queue capacities preserves all keys passed
review 3/P1.9-2.2: SIGKILL point=0 recovery=x passed
review 3/P1.9-2.2: SIGKILL point=1 recovery=x passed
review 3/P1.9-2.2: SIGKILL point=2 recovery=x passed
review 3/P1.9-2.2: SIGKILL point=3 recovery=x passed
review 4: FULL/append IO retains unaccepted suffix without duplicate accepted records passed
review 4: FULL/append IO retains unaccepted suffix without duplicate accepted records passed
review 4: exit checkpoint after FULL/append IO/sync IO mode=0 restores current tree passed
review 4: exit checkpoint after FULL/append IO/sync IO mode=1 restores current tree passed
review 4: exit checkpoint after FULL/append IO/sync IO mode=2 restores current tree passed
review 4: failure checkpoint preserves named/untitled contents, views and active tab order passed
review 4: failure checkpoint preserves named/untitled contents, views and active tab order passed
review 5/P1.9-2.3: source changed between attachment and BASE registration rejected passed
review 6: submitted mid-row caret/selection/blink/focus cells passed
review 7: multiple long clipped lines preserve subsequent rows in first frame and edit/undo passed
review 8: first viewport precedes stalled remainder work passed
review 10: 10 GiB beginning edits metadata visits max=631 (M)[AC], bound=4096 (G)
review 10: mutation cancels completed backlog without adopting it passed
review 11: indivisible submit observed=2079437 reported=2194779 ns (M)[AC]
review 11: first undo turn longest_slice_ns=146724 (M)[AC], slice limit=500000 ns (G)
review 11: multi-record burst undo/redo yields across turns passed
review 12: journaled large deletion and undo passed
review 15: replay arena growth=0 bytes (M)[AC], bound=65536 bytes (G)
review 13: typing/repeat bursts, movement/focus/timeout boundaries passed
review 16: mapped watch overwrite, focus truncate, edit suspension and reload/keep passed
review 21: native dequeue-to-translation mallocs=0; mutation-to-submit=0; guard=ASan-inert (M)[AC]
review 17: active job idle polls=2 (M)[AC], requested-timeout bound=4 (G)
review 17: queued job idle polls=3 (M)[AC], requested-timeout bound=4 (G)
review 22: allocator positions=21 failures=20; tree/view/history/journal rollback passed (M)[AC]
review 23: backend init success/failure executes on work pool and adopts mailbox result passed
required WM_DELETE_WINDOW: one turn, flushed recovery and shutdown passed
P1-1 section 1: file-backed successive typing keeps bounded index geometry passed
P1-1 section 1: replacement and undo capacity refuse before buffer/history mutation passed
P1-1 section 6: inactive exact counts/jump, bulk cancellation/retry, mallocs=0 guard=ASan-inert passed (M)[AC]
editor_test: partial index deep typing reuses viewport line starts passed
editor_test: default EGL and gl/raster override passed
editor_test: headless EGL injected init failure -> null, one log through typing/close passed
edisublimite: EGL init failed: EGL/GL/Present unavailable (code=-10); using raster; no retry
sublimite: EGL init failed: EGL/GL/Present unavailable (code=-10); using raster; no retry
tor_test: headless EGL injected unsupported failure -> null, one log through typing/close passed
editor_test: default EGL injected init failure -> raster, one log through typing/close passed
editor_test: default EGL dlopen failure -> raster, one log through typing/close passed
editor_test: failed EGL init falls back once and stays raster passed
editor_test: GPU completion progresses and idle disarms polling passed
editor_test: requested=gl actual=cpu-raster 10000 keys mallocs=0 guard=ASan-inert
editor_test: requested=gl actual=cpu-raster idle/unfocused background wakeups=0 passed
editor_test: requested=raster actual=cpu-raster 10000 keys mallocs=0 guard=ASan-inert
editor_test: requested=raster actual=cpu-raster idle/unfocused background wakeups=0 passed
editor_test: scrolled undo/redo viewport repair passed
editor_test: mutation error is negative and stops the loop passed
editor_test: null script and journal byte model passed
editor_test: null 10000 keys mallocs=0 guard=ASan-inert
editor_test: null hl-on 10000 keys mallocs=0 guard=ASan-inert
editor_test: raster script and journal byte model passed
editor_test: raster 10000 keys mallocs=0 guard=ASan-inert
G11 blink: mode=0 jobs=0 cells=1 minimap_fills=0
G11 blink: mode=0 jobs=0 cells=1 minimap_fills=0
G11 blink: mode=1 jobs=0 cells=1 minimap_fills=0
G11 blink: mode=1 jobs=0 cells=1 minimap_fills=0
G11 blink: mode=2 jobs=0 cells=2 minimap_fills=0
G11 blink: mode=2 jobs=0 cells=2 minimap_fills=0
G11 blink: mode=3 jobs=0 cells=1 minimap_fills=0
G11 blink: mode=3 jobs=0 cells=1 minimap_fills=0
editor_test: G11 inline caret damage and sleeping worker pool passed
editor_test: idle blink timeout and unfocused timer disarm passed
editor_test: queue depth one and coalesced edits passed
editor_test: native X11 translation, editor loop and WM close passed
editor_test: all passed
AFTER
Not charging
 09:17:58 up 6 days, 14:06,  1 user,  load average: 40.93, 41.47, 45.37
Filesystem      Size  Used Avail Use% Mounted on
/dev/nvme0n1p5  480G  382G   74G  84% /
RESULT exit=0 wall_ns=725808881792 (M); monotonic end-start
```

## Sanitizers: editor_scroll_test

Evidence: (M)[AC].

```text
BEFORE
Not charging
 09:17:59 up 6 days, 14:06,  1 user,  load average: 40.93, 41.47, 45.37
Filesystem      Size  Used Avail Use% Mounted on
/dev/nvme0n1p5  480G  382G   74G  84% /
COMMAND ['sh', '-c', 'ev0_runtime=$(mktemp -d); trap \'rm -rf "$ev0_runtime"\' EXIT; ASAN_OPTIONS=detect_leaks=1 XDG_RUNTIME_DIR="$ev0_runtime" DISPLAY=:99 sh tools/with_display_lock.sh "$1"', 'sh', 'build/san/tests/editor_scroll_test']
waiting for display :99 (held by 2097262)
editor_scroll_test: display lock timeout: display :99 (held by 2118134)
AFTER
Not charging
 09:27:59 up 6 days, 14:16,  1 user,  load average: 70.90, 59.62, 51.21
Filesystem      Size  Used Avail Use% Mounted on
/dev/nvme0n1p5  480G  384G   72G  85% /
RESULT exit=1 wall_ns=600143403452 (M); monotonic end-start
```

## Sanitizers: editor_large_test

Evidence: (M)[AC].

```text
BEFORE
Not charging
 09:27:59 up 6 days, 14:16,  1 user,  load average: 70.90, 59.62, 51.21
Filesystem      Size  Used Avail Use% Mounted on
/dev/nvme0n1p5  480G  384G   72G  85% /
COMMAND ['sh', '-c', 'ev0_runtime=$(mktemp -d); trap \'rm -rf "$ev0_runtime"\' EXIT; ASAN_OPTIONS=detect_leaks=1 XDG_RUNTIME_DIR="$ev0_runtime" DISPLAY=:99 sh tools/with_display_lock.sh "$1"', 'sh', 'build/san/tests/editor_large_test']
waiting for display :99 (held by 2118134)
editor_large_test: display lock timeout: display :99 (held by 2167252)
AFTER
Not charging
 09:38:00 up 6 days, 14:26,  1 user,  load average: 47.43, 56.42, 55.10
Filesystem      Size  Used Avail Use% Mounted on
/dev/nvme0n1p5  480G  386G   70G  85% /
RESULT exit=1 wall_ns=600401036012 (M); monotonic end-start
```

## Sanitizers: editor_session_test

Evidence: (M)[AC].

```text
BEFORE
Not charging
 09:38:01 up 6 days, 14:26,  1 user,  load average: 47.43, 56.42, 55.10
Filesystem      Size  Used Avail Use% Mounted on
/dev/nvme0n1p5  480G  386G   70G  85% /
COMMAND ['sh', '-c', 'ev0_runtime=$(mktemp -d); trap \'rm -rf "$ev0_runtime"\' EXIT; ASAN_OPTIONS=detect_leaks=1 XDG_RUNTIME_DIR="$ev0_runtime" DISPLAY=:99 sh tools/with_display_lock.sh "$1"', 'sh', 'build/san/tests/editor_session_test']
waiting for display :99 (held by 2167252)
```

## Sanitizers: editor_hot_exit_test

Evidence: (M), power stamps below.

```text
Not started: shared display unavailable.
```

## Sanitizers: cli_test

Evidence: (M), power stamps below.

```text
Not started: shared display unavailable.
```

## Sanitizers: raster_test

Evidence: (M), power stamps below.

```text
Not started: shared display unavailable.
```
