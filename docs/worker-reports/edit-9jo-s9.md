# edit-9jo session 9 worker report

Scope: correct the insufficient populations in the null hundred-tab ingress-to-T5,
same-frame minimap, and null G11 process-CPU rows. All three now reach the shared
10000-sample minimum (G); no threshold was lowered. The changes add no globals
and modify no production source. Git was used read-only; no tracking command,
HANDOFF, worklog, or Makefile edits were made.

## Findings and implementation

- `editor_null_100tabs_ingress_T5_G3`: fixed by repeating the same prepared
  hundred-tab Ctrl+Tab/release switch until the shared minimum is reached. Every
  observation retains the same allocation guard and compositor-ready T5 endpoint.
- `editor_null_100tabs_minimap_inside_frame`: fixed by collecting one fresh
  minimap cost from each of those same frames. Freshness and increasing fill count
  are still asserted. Both arrays match the schedule, and append errors are fatal.
  The raster P4 rows use the same increased schedule; their workload is unchanged.
- `editor_null_G11_process_cpu`: now emitted as
  `editor_null_G11_process_cpu_synthetic_due_now`. Each sample runs the normal
  editor poll/blink/caret/submit sequence with the private deadline advanced to
  due-now before measurement, resetting the private input age to avoid timeout.
  Exactly one additional blink is required per observation. This measures process
  CPU per blink at a documented synthetic zero-wait cadence, not default-period
  sleep/wakeup overhead or power. The existing real-cadence timeout, wakeup count,
  quiet observation, and unfocused observation remain. Raster/GL CPU rows retain
  their existing real-cadence schedules and MISS verdicts.

The failing regression was written and run first. It executes the actual null
rows in a child and parses their printed populations; missing or short populations
fail, while loaded-box timing misses do not. Negative row failures remain fatal.
The unchanged shared `bench_judge` tests still check the minimum boundary.

This checkout keeps the P4 tab rows in `bench/editor_bench.c`; the named split
`bench/editor_p4_bench.c` is absent. Changes are confined to `tab_row`, `idle_row`,
and `tests/harness_test.c`. The IPC helper and typing section are untouched.
`bench/interaction.h` needed no change. The named M0 record is absent from the
checkout and was read from `dd84a49:docs/bench/m0.md` (G11/G3 sections).
The editor STATUS was read and left unchanged because production code is outside
scope. Design details are in [the decision](../decisions/edit-9jo.md).

## Red, before schedule changes

Commands:

```sh
DISPLAY=:99 EDIT_DISPLAY=:99 build/bench/editor_bench --p4-only
DISPLAY=:99 EDIT_DISPLAY=:99 build/bench/editor_bench --idle-only
DISPLAY=:99 EDIT_DISPLAY=:99 make build/tests/harness_test
DISPLAY=:99 EDIT_DISPLAY=:99 build/tests/harness_test
```

The sandbox runs preserve the null REFUSED rows below. Their later display-backed
rows could not open the sandbox-hidden Xvfb; authorized execution on the existing
:99 display was then used for full validation. The regression exited nonzero
before fixes. Pasted output (M)[AC], loaded box:

```text
BENCH name=editor_null_100tabs_ingress_T5_G3 n=200 required_n=10000 p50=188581 p99=388336 ci95_p50=[187296,192754] ci95_p99=[373196,417173] gate_p50=5000000 gate_p99=5560000 dropped=0 (M)[AC] power=Not charging load1=2.00 verdict=REFUSED (insufficient samples; no verdict)
BENCH name=editor_null_100tabs_minimap_inside_frame n=200 required_n=10000 p50=5964 p99=8968 ci95_p50=[5951,6085] ci95_p99=[8581,16299] gate_p50=0 gate_p99=500000 dropped=0 (M)[AC] power=Not charging load1=2.00 verdict=REFUSED (insufficient samples; no verdict)
BENCH name=editor_null_G11_process_cpu n=19 required_n=10000 p50=84755 p99=107383 ci95_p50=[81333,98836] ci95_p99=[100531,107383] gate_p50=100000 gate_p99=200000 dropped=0 (M)[AC] power=Not charging load1=2.09 verdict=REFUSED (insufficient samples; no verdict)
FAIL tests/harness_test.c:467: frames >= BENCH_INTERACTION_MIN_N
FAIL tests/harness_test.c:468: maps >= BENCH_INTERACTION_MIN_N
FAIL tests/harness_test.c:469: blinks >= BENCH_INTERACTION_MIN_N
harness_test: 20213 checks, 3 failures
```

The regression's null G11 timing happened to print MISS under its concurrent
load; its population still failed. The standalone pre-fix row above printed
REFUSED. No timing selection or quiet-box retry was used to decide the fix.

## Green, after schedule changes

Full row commands use the existing Xvfb :99, outside the sandbox that hides its
socket. No window was opened on :0. Output is measured (M)[AC], BAT0 Not charging,
on the shared loaded box. Printed percentiles and verdicts below are descriptive;
they do not certify performance gates or establish a speedup. The acceptance
change is the measured population and zero dropped samples, not a timing result.

```sh
DISPLAY=:99 EDIT_DISPLAY=:99 build/bench/editor_bench --p4-only
DISPLAY=:99 EDIT_DISPLAY=:99 build/bench/editor_bench --idle-only
DISPLAY=:99 EDIT_DISPLAY=:99 build/tests/harness_test
```

```text
BENCH name=editor_null_100tabs_ingress_T5_G3 n=10000 required_n=10000 p50=270117 p99=529791 ci95_p50=[269731,270708] ci95_p99=[526689,533190] gate_p50=5000000 gate_p99=5560000 dropped=0 (M)[AC] power=Not charging load1=4.35 verdict=PASS
BENCH name=editor_null_100tabs_minimap_inside_frame n=10000 required_n=10000 p50=9050 p99=11253 ci95_p50=[9049,9051] ci95_p99=[11206,11507] gate_p50=0 gate_p99=500000 dropped=0 (M)[AC] power=Not charging load1=4.35 verdict=PASS
G3 null (M)[AC] load1=4.35 tabs=100 A=2880x1800 switches=10000 allocations=0 minimap_fills=10001 stale=0 mode=GATE
BENCH name=editor_raster_100tabs_ingress_T5_G3 n=10000 required_n=10000 p50=6601948 p99=12243839 ci95_p50=[6578282,6623167] ci95_p99=[11921685,12712313] gate_p50=5000000 gate_p99=5560000 dropped=0 (M)[AC] power=Not charging load1=4.72 verdict=MISS
BENCH name=editor_raster_100tabs_minimap_inside_frame n=10000 required_n=10000 p50=12681 p99=23786 ci95_p50=[12591,12792] ci95_p99=[22842,25525] gate_p50=0 gate_p99=500000 dropped=0 (M)[AC] power=Not charging load1=4.72 verdict=PASS
G3 raster (M)[AC] load1=4.72 tabs=100 A=2880x1800 switches=10000 allocations=0 minimap_fills=10001 stale=0 mode=GATE
BENCH name=editor_second_invocation_exec_open_ACK_exit n=64 required_n=10000 p50=2988241 p99=3338387 ci95_p50=[2966186,3011226] ci95_p99=[3155086,3338387] gate_p50=0 gate_p99=10000000 dropped=0 (M)[AC] power=Not charging load1=1.88 verdict=REFUSED (insufficient samples; no verdict)
IPC (M)[AC] load1=1.88 invocations=64 opened_tabs=64 includes=exec,parse,open,ACK,exit,reap mode=GATE
BENCH name=editor_null_G11_process_cpu_synthetic_due_now n=10000 required_n=10000 p50=66341 p99=87657 ci95_p50=[66235,66456] ci95_p99=[87501,87979] gate_p50=100000 gate_p99=200000 dropped=0 (M)[AC] power=Not charging load1=4.72 verdict=PASS
G11 null (M)[AC] load1=4.72 blinks=19 poll_returns=20 wakeups/s=1.998 idle=0 unfocused=0 (G)<=2/s,0,0 mode=GATE structural=OK
G11 null structural worker_jobs=0 inline_frames=0
BENCH name=editor_raster_G11_process_cpu n=19 required_n=10000 p50=216246 p99=272138 ci95_p50=[200988,228715] ci95_p99=[246568,272138] gate_p50=100000 gate_p99=200000 dropped=0 (M)[AC] power=Not charging load1=5.06 verdict=MISS
G11 raster (M)[AC] load1=5.06 blinks=19 poll_returns=20 wakeups/s=2.005 idle=0 unfocused=0 (G)<=2/s,0,0 mode=GATE structural=OK
G11 raster structural worker_jobs=0 inline_frames=19
BENCH name=editor_gl_fallback_raster_G11_process_cpu n=19 required_n=10000 p50=202716 p99=243481 ci95_p50=[177442,223175] ci95_p99=[228816,243481] gate_p50=100000 gate_p99=200000 dropped=0 (M)[AC] power=Not charging load1=5.87 verdict=MISS
G11 gl_fallback_raster (M)[AC] load1=5.87 blinks=19 poll_returns=20 wakeups/s=2.009 idle=0 unfocused=0 (G)<=2/s,0,0 mode=GATE structural=OK
G11 gl_fallback_raster structural worker_jobs=0 inline_frames=19
harness_test: 20213 checks, 0 failures
```

## Validation and remaining work

The full P4 invocation exited nonzero because the retained raster G3 row is
MISS and the out-of-scope IPC row is REFUSED. The idle invocation exited nonzero
because raster and GL-fallback G11 remain MISS. All targeted rows have their
required populations; no TRACK option hid those verdicts. The P4 output-log
window spans 281.54 seconds (M)[AC] on the loaded box, including its existing
IPC section; the named null P4 rows were emitted within 3.23 seconds (M)[AC]
of the log creation. These are elapsed log timestamps, not performance gates.

The release `make all`, complete sanitizer-suite rerun, and editor fuzz run
exited zero. The sanitizer run passed 60 test binaries and the replay CLI
(M)[AC]. Logs are under `build/edit-9jo-evidence/`. Release and sanitizer builds
used gcc 13 and clang 18 with `-std=c11 -Wall -Wextra -Werror -Wshadow
-Wconversion`. The scoped diff also passes `git diff --check`.

```sh
DISPLAY=:99 EDIT_DISPLAY=:99 make all
DISPLAY=:99 EDIT_DISPLAY=:99 ASAN_OPTIONS=detect_leaks=0 make -j4 check
```

Pasted final build/suite output (M)[AC], both commands exit zero:

```text
make: Nothing to be done for 'all'.
harness_test: 20213 checks, 0 failures
work_foreground_test: ok
check: 60 test binaries passed
== tools/test_replay_cli.sh
test_replay_cli: all passed
``` LeakSanitizer
is disabled with `ASAN_OPTIONS=detect_leaks=0`; the coordinator must rerun with
leaks enabled. The editor fuzz target exceeded its 60-second minimum (G), exiting zero:

```sh
DISPLAY=:99 EDIT_DISPLAY=:99 ASAN_OPTIONS=detect_leaks=0 build/fuzz/editor_fuzz -max_total_time=60 -max_len=1024 -artifact_prefix=build/edit-9jo-evidence/
```

Pasted fuzz output (M)[AC]:

```text
#15876 DONE   cov: 12817 ft: 37435 corp: 496/4437b lim: 14 exec/s: 260 rss: 377Mb
Done 15876 runs in 61 second(s)
```

Known out-of-scope work: raster/GL G11 timing MISS remains edit-zzj.12, real native
EGL is unavailable on Xvfb, and the P4 IPC launch row's schedule belongs to its
separate worker. No production fix or unrelated benchmark change is included.


### Unrelated first-suite failure, retained

The first full `make check` exited nonzero at the unchanged foreground-work
fixture. Pasted output:

```text
work_foreground_test:224: FAIL ready && result.exact && result.value == expected
work_foreground_test: FAIL
make: *** [Makefile:101: check] Error 1
```

No source or assertion was changed. An immediate isolated rerun exited zero:

```sh
DISPLAY=:99 EDIT_DISPLAY=:99 ASAN_OPTIONS=detect_leaks=0 build/san/tests/work_foreground_test
```

```text
work_foreground_test: fragmented worker seeks zero/partial/EOF + cancellation suppression checked
work_foreground_test: running_index_yields fragment_spans_before_reply=64 bound=64 (G) lease_released=1
work_foreground_test: editor_resident_jump_before_bulk_release checked
work_foreground_test: ok
```

The intermittent failure belongs to foreground/index work outside this bead.
Both `check-first.log` and `work-foreground-rerun.log` are preserved under the
worktree evidence directory. The complete unmodified-suite rerun exited zero, including this fixture;
its final summary is pasted above. No claim that the unrelated failure
has been fixed is made.


## Handoff

All named insufficient-population findings are implemented and verified with
red/green evidence above. No scoped work is missing. The coordinator still owns
leak-enabled verification and any follow-up for the recorded intermittent
foreground/index failure, raster/GL G11 MISS, raster G3 MISS, and IPC sample
schedule. None of those production or IPC findings was fixed in this bead.
The design decision is `docs/decisions/edit-9jo.md`; the only implementation/test
changes are `bench/editor_bench.c` and `tests/harness_test.c`.
