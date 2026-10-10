# edit-zzj.14 — session 9 worker report

Scope: P3.3c, `docs/reviews/editor-1.md` sections 18, 19 and 20.
Only benchmark/harness/test code and this bead's documentation are changed.
`src/editor` internals, HANDOFF.md and docs/worklog are untouched. Git was
read-only; no bead command was run. All display-backed commands select :99.

## Per-finding implementation

- **18, G1 ingress and contention:** injection time is retained for every
  sequence, separately from the public dequeue callback. Submit and T4 hooks
  attribute a containing frame to every key in its first/last sequence range.
  Each key is sampled once, including keys coalesced into one frame. Injection
  to T4 is G1; injection to submit return and dequeue to submit are explicitly
  descriptive TRACK rows. Default scenarios are serial, queued arrivals while
  a native frame is active, an active index-count kernel, and queued
  index-count/literal-find/save-write kernels. The latter jobs share the
  editor backend's public worker pool, use bounded cooperative continuations,
  and must all make progress. No editor private worker state is inspected.
- **19, target A area:** a shared fixture selects `font_ascii_px()` explicitly
  and derives its cell counts from `font_ascii_cell()`. Both G1 and G11 assert
  the backend's actual configured pixel extent and print that extent. The
  regression opens a real public editor/backend fixture and checks its size.
- **20, verdict honesty:** allocation, mutation/journal totals, journal errors,
  missing/duplicate frame attribution, bulk progress and native active-frame
  arrivals are structural checks. Structural failures exit 1 in TRACK too.
  Idle wake-policy failures also remain fatal. All gated rows use the existing
  shared qualified verdict. The shipped args file no longer supplies TRACK;
  the null G1 reference no longer silently disables its timing verdict.
  `bench_merge_exit` only extends the harness with aggregation: failure 1
  takes precedence over refusal 3, which takes precedence over success 0.
  Existing `bench_judge`, `bench_exit_code` and reporting semantics are unchanged.

Setup reservations hold per-key timestamps and percentile storage before
measurement. Coalesced ingresses do not restart/reset an active allocation
guard. No globals were introduced.

## Red first

The tests were written before the fixes. Behavior-preserving extraction of
fixture/verdict wrappers let the tests exercise the row policy directly.
Command: `DISPLAY=:99 EDIT_DISPLAY=:99 build/tests/harness_test`.
The failing run was (M)[AC]:

```text
FAIL tests/harness_test.c:299: width == 2880 && height == 1800
FAIL tests/harness_test.c:310: reported >= UINT64_C(20000000)
FAIL tests/harness_test.c:316: measured.frame.first_sequence == measured.sequence
FAIL tests/harness_test.c:320: structural_rc == 1
FAIL tests/harness_test.c:325: refused == 3
STAMP (M)[AC] BAT0=Charging load1=2.28 TRACK shared box
PROBE A runtime=1440x900 requested=2880x1800 (G)
PROBE G1 injected_wait=20000000 (G) reported=106521 injection_to_T4=20226744 (M)[AC]
PROBE queued_keys first=2 last=3 dequeue_sequence=3 (M)[AC]
PROBE structural_failure TRACK exit=0 expected=1 (G)
BENCH name=editor_probe_insufficient n=1 p50=1 p99=1 ci95=[1,1] gate_p50=100 gate_p99=100 pass=1 power=[AC]
PROBE insufficient_samples exit=0 expected=3 (G)
harness_test: 10150 checks, 5 failures
exit=1
```

A further red test caught boolean aggregation losing refusal 3 before the
harness extension was implemented:

```text
FAIL tests/harness_test.c:331: bench_merge_exit(0, 3) == 3
FAIL tests/harness_test.c:332: bench_merge_exit(3, 0) == 3
FAIL tests/harness_test.c:333: bench_merge_exit(3, 3) == 3
STAMP (M)[AC] BAT0=Not charging load1=5.89 shared box
PROBE A runtime=2880x1800 requested=2880x1800 (G)
PROBE G1 injected_wait=20000000 (G) reported=20204029 injection_to_T4=20204029 (M)[AC]
PROBE queued_keys first=2 last=3 dequeue_sequence=3 (M)[AC]
PROBE structural_failure TRACK exit=1 expected=1 (G)
BENCH name=editor_probe_insufficient n=1 required_n=10000 p50=1 p99=1 ci95_p50=[1,1] ci95_p99=[1,1] gate_p50=100 gate_p99=100 dropped=0 (M)[AC] power=Not charging load1=5.89 verdict=REFUSED (insufficient samples; no verdict)
PROBE insufficient_samples exit=3 expected=3 (G)
harness_test: 10157 checks, 3 failures
exit=1
```

## Green regression evidence

Command: `DISPLAY=:99 EDIT_DISPLAY=:99 build/tests/harness_test`.
(M)[AC]; the delayed backend also holds one frame active while three more
keys arrive, then checks both containing frames and all injection latencies.
Structural count/allocation/missing-frame/duplicate-frame fault probes remain
fatal under TRACK:

```text
STAMP (M)[AC] BAT0=Not charging load1=22.02 shared box
PROBE A runtime=2880x1800 requested=2880x1800 (G)
PROBE G1 injected_wait=20000000 (G) reported=20291874 injection_to_T4=20291874 (M)[AC]
PROBE queued_keys first=2 last=3 dequeue_sequence=3 (M)[AC]
PROBE structural_failure TRACK exit=1 expected=1 (G)
BENCH name=editor_probe_insufficient n=1 required_n=10000 p50=1 p99=1 ci95_p50=[1,1] ci95_p99=[1,1] gate_p50=100 gate_p99=100 dropped=0 (M)[AC] power=Not charging load1=22.02 verdict=REFUSED (insufficient samples; no verdict)
PROBE insufficient_samples exit=3 expected=3 (G)
PROBE active_frame keys=4 completed=4 coalesced=3 (M)[AC]
PROBE structural counts, allocation, missing/duplicate frame failures remain fatal in TRACK
harness_test: 10192 checks, 0 failures
exit=0
```

The inherited source was also compiled as a separate executable for a
back-to-back check with the fixed probes. Both starts were in the same minute
on the same loaded box (M)[AC]. These verify timestamp inclusion and area;
they are not performance gate acceptance:

```text
2026-10-10T19:58:33.069008+00:00 build/probe-before
STAMP (M)[AC] BAT0=Not charging load1=7.87 TRACK shared box
BEFORE A runtime=1440x900 expected=2880x1800 (G)
BEFORE injected_wait=20000000 (G) reported=115492 injection_to_T4=20233718 (M)[AC]
exit=1
2026-10-10T19:58:33.103324+00:00 build/tests/harness_test
STAMP (M)[AC] BAT0=Not charging load1=7.87 shared box
PROBE A runtime=2880x1800 requested=2880x1800 (G)
PROBE G1 injected_wait=20000000 (G) reported=20246744 injection_to_T4=20246744 (M)[AC]
PROBE queued_keys first=2 last=3 dequeue_sequence=3 (M)[AC]
PROBE structural_failure TRACK exit=1 expected=1 (G)
BENCH name=editor_probe_insufficient n=1 required_n=10000 p50=1 p99=1 ci95_p50=[1,1] ci95_p99=[1,1] gate_p50=100 gate_p99=100 dropped=0 (M)[AC] power=Not charging load1=7.87 verdict=REFUSED (insufficient samples; no verdict)
PROBE insufficient_samples exit=3 expected=3 (G)
PROBE active_frame keys=4 completed=4 coalesced=3 (M)[AC]
PROBE structural counts, allocation, missing/duplicate frame failures remain fatal in TRACK
harness_test: 10192 checks, 0 failures
exit=0
```

## Validation and remaining evidence

Release compiler is gcc 13; sanitizer/fuzzer compiler is clang 18. Strict
C11 warning flags include Wall, Wextra, Werror, Wshadow and Wconversion.
`DISPLAY=:99 EDIT_DISPLAY=:99 make -j4 all` exits 0 (M)[AC], including
rebuilding the final benchmark and harness test with all strict warning flags.
The full `DISPLAY=:99 EDIT_DISPLAY=:99 ASAN_OPTIONS=detect_leaks=0 make -j4 check`
run exits 0 (M)[AC]:

```text
check: 59 test binaries passed
== tools/test_replay_cli.sh
ok:   --speed=inf rc=2 replay: --speed must be a finite number > 0
ok:   --speed=nan rc=2 replay: --speed must be a finite number > 0
ok:   --speed=0 rc=2 replay: --speed must be a finite number > 0
ok:   --speed=-1 rc=2 replay: --speed must be a finite number > 0
ok:   --speed=2 rc=0
test_replay_cli: all passed
```

The final full suite rerun after benchmark failure cleanup also exits 0
(M)[AC], with 59 sanitizer test binaries and replay CLI green. At completion,
BAT0 is Charging and load1 is 34.68 (M)[AC]; no timing gate acceptance is
inferred from that loaded-box run. The final sanitizer harness regression
passes 10192 checks with no failures (M)[AC). Final driver tail:

```text
== build/san/tests/x11_xi2_test
x11_xi2_test: ok
check: 59 test binaries passed
== tools/test_replay_cli.sh
ok:   --speed=inf rc=2 replay: --speed must be a finite number > 0
ok:   --speed=nan rc=2 replay: --speed must be a finite number > 0
ok:   --speed=0 rc=2 replay: --speed must be a finite number > 0
ok:   --speed=-1 rc=2 replay: --speed must be a finite number > 0
ok:   --speed=2 rc=0
test_replay_cli: all passed
exit=0
```
LeakSanitizer is disabled with `ASAN_OPTIONS=detect_leaks=0`, as required by
the sandbox restriction; the coordinator must rerun with leaks enabled.

Editor fuzz command:
`DISPLAY=:99 EDIT_DISPLAY=:99 ASAN_OPTIONS=detect_leaks=0 build/fuzz/editor_fuzz -max_total_time=60 -max_len=1024`.
The budget is 60 seconds (G); the completed run is 61 seconds (M)[AC], exit 0:

```text
#4438 DONE cov: 11933 ft: 30060 corp: 262/1632b lim: 8 exec/s: 72 rss: 361Mb
Done 4438 runs in 61 second(s)
```

## Scoped limitations and out-of-scope findings

- The default `/tmp/edit-corpus/log_1g.txt` near-EOF warmup still times out before collecting
  G1 samples. Back-to-back inherited and revised runs both failed there.
  This is already described in `src/editor/STATUS.md`; editor internals are
  owned by other workers and were not changed. No long-corpus gate is claimed.
- Small-fixture full-area native contention runs expose nonzero guarded
  allocations in some active-frame rows. They are printed and return 1 under
  TRACK. The benchmark does not suppress them or alter production allocation
  behavior. Attribution inside editor/platform code remains out of scope.
- Raster G11 still exceeds the wake-policy bound. TRACK now returns 1 for
  that structural violation. No CPU timing gate is claimed from the loaded box.
- Bulk jobs execute real index scan, literal-find and save-write kernels on
  the same editor pool. The save spool is bounded and does not mutate the
  corpus. This is foreground contention evidence, not complete durable-save
  pipeline/rename/fsync acceptance. End-to-end production pipeline acceptance
  remains unverified through the currently available editor public API.
- G11, G3 and IPC retain their inherited short sample schedules. Default
  qualified reporting cannot certify them with insufficient samples. TRACK
  follows the shared harness's existing opt-out semantics (including refusal);
  this bead deliberately does not change that shared contract.
- Native EGL acceptance is unavailable on this Xvfb; no GPU gate is claimed.

Design choices and exact fixture/verdict boundaries are recorded in
`docs/decisions/edit-zzj.14.md`. Corpus fixtures are read, never regenerated.
Runtime scratch created by the revised benchmark stays beneath `build/`.

## Whole-process verdict evidence

The inherited G11 TRACK run returned 0 despite the wake-policy violation.
The revised run returns 1. These are structural-verdict reproductions on the
loaded box, not timing gate acceptance. Inherited output (M)[AC]:

```text
STAMP (M)[AC] BAT0=Not charging load1=8.04 TRACK shared box
BACKEND requested=null actual=null init_error=0
BENCH name=editor_null_G11_process_cpu n=19 p50=99870 p99=136371 ci95=[94539,106883] gate_p50=100000 gate_p99=200000 pass=1 power=[AC]
G11 null (M)[AC] load1=8.04 blinks=19 poll_returns=20 wakeups/s=1.998 idle=0 unfocused=0 (G)<=2/s,0,0 mode=TRACK
STAMP (M)[AC] BAT0=Not charging load1=7.65 TRACK shared box
BACKEND requested=raster actual=cpu-raster init_error=0
BENCH name=editor_raster_G11_process_cpu n=19 p50=563542 p99=760195 ci95=[502053,626413] gate_p50=100000 gate_p99=200000 pass=0 power=[AC]
G11 raster (M)[AC] load1=7.65 blinks=19 poll_returns=88 wakeups/s=8.783 idle=0 unfocused=0 (G)<=2/s,0,0 mode=TRACK
exit=0
```

Revised output (M)[AC]:

```text
STAMP (M)[AC] BAT0=Not charging load1=4.94 shared box
TARGET A=2880x1800 font_px=30 (M)[AC]
BACKEND requested=null actual=null init_error=0
BENCH name=editor_null_G11_process_cpu n=19 required_n=10000 p50=116143 p99=128271 ci95_p50=[99544,122604] ci95_p99=[127373,128271] gate_p50=100000 gate_p99=200000 dropped=0 (M)[AC] power=Not charging load1=4.94 verdict=TRACK
G11 null (M)[AC] load1=4.94 blinks=19 poll_returns=20 wakeups/s=1.997 idle=0 unfocused=0 (G)<=2/s,0,0 mode=TRACK
STAMP (M)[AC] BAT0=Not charging load1=5.87 shared box
TARGET A=2880x1800 font_px=30 (M)[AC]
BACKEND requested=raster actual=cpu-raster init_error=0
BENCH name=editor_raster_G11_process_cpu n=21 required_n=10000 p50=980582 p99=1376115 ci95_p50=[849155,1076410] ci95_p99=[1163146,1376115] gate_p50=100000 gate_p99=200000 dropped=0 (M)[AC] power=Not charging load1=5.87 verdict=TRACK
G11 raster (M)[AC] load1=5.87 blinks=21 poll_returns=146 wakeups/s=12.920 idle=0 unfocused=0 (G)<=2/s,0,0 mode=TRACK
exit=1
```

An artificial ingress-wait row also verifies the actual command-line gate
policy. Both variants ran back to back within the same minute (M)[AC]. The
configured delay is 20 ms (G); this intentionally cannot pass the G1 bound.
These diagnostic fixtures have 64 keys (M)[AC], not the required interaction
sample count of 10000 (G). Shared semantics prioritize the deliberate miss
over short-sample refusal. TRACK returns 0; the default returns 1:

```text
2026-10-10T20:03:30.422776+00:00 build/bench/editor_bench --no-idle --no-p4 --keys=64 --serial-only --ingress-delay-ms=20 --file=build/editor-bench-fixture.txt --track
STAMP (M)[AC] BAT0=Not charging load1=6.22 shared box
BACKEND requested=null actual=null init_error=0 TARGET A=2880x1800 font_px=30 (M)[AC]
POSITION null byte=10598 index=published workload=serial edits=alternating_insert_backspace
BENCH name=editor_null_serial_injection_submit n=64 required_n=10000 p50=20597073 p99=22751938 ci95_p50=[20593236,20611450] ci95_p99=[20782752,22751938] gate_p50=0 gate_p99=0 dropped=0 (M)[AC] power=Not charging load1=6.22 verdict=TRACK
BENCH name=editor_null_serial_dequeue_submit n=64 required_n=10000 p50=518459 p99=2431544 ci95_p50=[514551,528378] ci95_p99=[665699,2431544] gate_p50=0 gate_p99=0 dropped=0 (M)[AC] power=Not charging load1=6.22 verdict=TRACK
BENCH name=editor_null_serial_injection_T4_G1 n=64 required_n=10000 p50=20597762 p99=22752601 ci95_p50=[20593796,20612007] ci95_p99=[20783435,22752601] gate_p50=1000000 gate_p99=2000000 dropped=0 (M)[AC] power=Not charging load1=6.22 verdict=TRACK
G1 null workload=serial keys=64 completed=64 arrivals_active=0 coalesced=0 allocations=0 mutations=64 journal=64 structural=OK mode=TRACK (M)[AC]
STAMP (M)[AC] BAT0=Not charging load1=6.22 shared box
BACKEND requested=raster actual=cpu-raster init_error=0 TARGET A=2880x1800 font_px=30 (M)[AC]
POSITION raster byte=10598 index=published workload=serial edits=alternating_insert_backspace
BENCH name=editor_raster_serial_injection_submit n=64 required_n=10000 p50=20642524 p99=21827200 ci95_p50=[20635592,20672049] ci95_p99=[21115916,21827200] gate_p50=0 gate_p99=0 dropped=0 (M)[AC] power=Not charging load1=6.22 verdict=TRACK
BENCH name=editor_raster_serial_dequeue_submit n=64 required_n=10000 p50=560028 p99=1736429 ci95_p50=[552542,587589] ci95_p99=[928042,1736429] gate_p50=0 gate_p99=0 dropped=0 (M)[AC] power=Not charging load1=6.22 verdict=TRACK
BENCH name=editor_raster_serial_injection_T4_G1 n=64 required_n=10000 p50=21610700 p99=25124318 ci95_p50=[21575644,21678771] ci95_p99=[22554377,25124318] gate_p50=1000000 gate_p99=2000000 dropped=0 (M)[AC] power=Not charging load1=6.22 verdict=TRACK
G1 raster workload=serial keys=64 completed=64 arrivals_active=0 coalesced=0 allocations=0 mutations=64 journal=64 structural=OK mode=TRACK (M)[AC]
exit=0
2026-10-10T20:03:37.484980+00:00 build/bench/editor_bench --no-idle --no-p4 --keys=64 --serial-only --ingress-delay-ms=20 --file=build/editor-bench-fixture.txt
STAMP (M)[AC] BAT0=Not charging load1=6.44 shared box
BACKEND requested=null actual=null init_error=0 TARGET A=2880x1800 font_px=30 (M)[AC]
POSITION null byte=10598 index=published workload=serial edits=alternating_insert_backspace
BENCH name=editor_null_serial_injection_submit n=64 required_n=10000 p50=20868390 p99=24860790 ci95_p50=[20714762,21321753] ci95_p99=[23005386,24860790] gate_p50=0 gate_p99=0 dropped=0 (M)[AC] power=Not charging load1=6.44 verdict=TRACK
BENCH name=editor_null_serial_dequeue_submit n=64 required_n=10000 p50=627319 p99=2403506 ci95_p50=[618413,632461] ci95_p99=[1003488,2403506] gate_p50=0 gate_p99=0 dropped=0 (M)[AC] power=Not charging load1=6.44 verdict=TRACK
BENCH name=editor_null_serial_injection_T4_G1 n=64 required_n=10000 p50=20868995 p99=24861485 ci95_p50=[20715591,21323140] ci95_p99=[23005986,24861485] gate_p50=1000000 gate_p99=2000000 dropped=0 (M)[AC] power=Not charging load1=6.44 verdict=MISS
G1 null workload=serial keys=64 completed=64 arrivals_active=0 coalesced=0 allocations=0 mutations=64 journal=64 structural=OK mode=GATE (M)[AC]
STAMP (M)[AC] BAT0=Not charging load1=6.44 shared box
BACKEND requested=raster actual=cpu-raster init_error=0 TARGET A=2880x1800 font_px=30 (M)[AC]
POSITION raster byte=10598 index=published workload=serial edits=alternating_insert_backspace
BENCH name=editor_raster_serial_injection_submit n=64 required_n=10000 p50=20684141 p99=23293117 ci95_p50=[20660254,20723085] ci95_p99=[22007491,23293117] gate_p50=0 gate_p99=0 dropped=0 (M)[AC] power=Not charging load1=6.44 verdict=TRACK
BENCH name=editor_raster_serial_dequeue_submit n=64 required_n=10000 p50=599598 p99=3213318 ci95_p50=[576662,635311] ci95_p99=[1916612,3213318] gate_p50=0 gate_p99=0 dropped=0 (M)[AC] power=Not charging load1=6.44 verdict=TRACK
BENCH name=editor_raster_serial_injection_T4_G1 n=64 required_n=10000 p50=21600970 p99=25506016 ci95_p50=[21549573,21708363] ci95_p99=[23993643,25506016] gate_p50=1000000 gate_p99=2000000 dropped=0 (M)[AC] power=Not charging load1=6.44 verdict=MISS
G1 raster workload=serial keys=64 completed=64 arrivals_active=0 coalesced=0 allocations=0 mutations=64 journal=64 structural=OK mode=GATE (M)[AC]
exit=1
```

Default corpus setup failure, inherited followed immediately by revised,
(M)[AC], both exit 1 before samples (outside-scope editor behavior):

```text
STAMP (M)[AC] BAT0=Charging load1=24.81 TRACK shared box
BACKEND requested=null actual=null init_error=0
POSITION null lines=8947842 target_line=8053057 byte=966366840 index=published A=2880x1800 workload=alternating_insert_backspace
editor_bench:74 failed: bench_now_ns() < deadline
editor_bench:176 failed: settle(e) == 0
exit=1
STAMP (M)[AC] BAT0=Not charging load1=20.43 shared box
BACKEND requested=null actual=null init_error=0 TARGET A=2880x1800 font_px=30 (M)[AC]
POSITION null byte=966367641 index=progressing workload=serial edits=alternating_insert_backspace
editor_bench:115 failed: bench_now_ns() < deadline
editor_bench:334 failed: settle(e) == 0
exit=1
```

For the small diagnostic corpus, create `build/editor-bench-fixture.txt` with
512 copies (G) of `alpha ERROR beta gamma` followed by a newline. Run:

```sh
DISPLAY=:99 EDIT_DISPLAY=:99 EDIT_BACKEND=raster build/bench/editor_bench --track --no-idle --no-p4 --keys=64 --file=build/editor-bench-fixture.txt
```

The contention fixture verifies native arrivals during an active frame and
coalesced key attribution, with all jobs progressing on the shared editor
pool. It remains allowed to fail when the real allocation guard fires.
Current diagnostic evidence (M)[AC]:

```text
G1 null workload=serial keys=64 completed=64 arrivals_active=0 coalesced=0 allocations=0 mutations=64 journal=64 structural=OK mode=TRACK (M)[AC]
G1 null workload=queued_active_frame keys=64 completed=64 arrivals_active=0 coalesced=48 allocations=0 mutations=64 journal=64 structural=OK mode=TRACK (M)[AC]
BULK active_index kernel=index_scan chunks=1510 max_chunk_cpu_ns=40526 (M)[AC] shared_editor_pool=1
G1 null workload=active_index keys=64 completed=64 arrivals_active=0 coalesced=48 allocations=0 mutations=64 journal=64 structural=OK mode=TRACK (M)[AC]
BULK queued_index_find_save kernel=index_scan chunks=417 max_chunk_cpu_ns=73500 (M)[AC] shared_editor_pool=1
BULK queued_index_find_save kernel=find_literal chunks=418 max_chunk_cpu_ns=21130 (M)[AC] shared_editor_pool=1
BULK queued_index_find_save kernel=save_write chunks=417 max_chunk_cpu_ns=59308 (M)[AC] shared_editor_pool=1
G1 null workload=queued_index_find_save keys=64 completed=64 arrivals_active=0 coalesced=48 allocations=0 mutations=64 journal=64 structural=OK mode=TRACK (M)[AC]
G1 raster workload=serial keys=64 completed=64 arrivals_active=0 coalesced=0 allocations=0 mutations=64 journal=64 structural=OK mode=TRACK (M)[AC]
G1 raster workload=queued_active_frame keys=64 completed=64 arrivals_active=56 coalesced=48 allocations=6 mutations=64 journal=64 structural=FAIL mode=TRACK (M)[AC]
BULK active_index kernel=index_scan chunks=50713 max_chunk_cpu_ns=74484 (M)[AC] shared_editor_pool=1
G1 raster workload=active_index keys=64 completed=64 arrivals_active=56 coalesced=48 allocations=6 mutations=64 journal=64 structural=FAIL mode=TRACK (M)[AC]
BULK queued_index_find_save kernel=index_scan chunks=6845 max_chunk_cpu_ns=91443 (M)[AC] shared_editor_pool=1
BULK queued_index_find_save kernel=find_literal chunks=6845 max_chunk_cpu_ns=67898 (M)[AC] shared_editor_pool=1
BULK queued_index_find_save kernel=save_write chunks=6845 max_chunk_cpu_ns=176601 (M)[AC] shared_editor_pool=1
G1 raster workload=queued_index_find_save keys=64 completed=64 arrivals_active=56 coalesced=48 allocations=12 mutations=64 journal=64 structural=FAIL mode=TRACK (M)[AC]
exit=1
```

Successful and failed typing rows cancel bulk leases and wait for physical
acknowledgement before releasing argument/mapping storage; they also disarm
the allocator guard, close the editor, remove their journal and free samples.
This teardown prevents benchmark structural failures from stranding live
workers with expired stack arguments.

The final IPC scratch/verdict smoke run also exits 0 in explicit TRACK
(M)[AC], with the worktree-local scratch directory removed:

```text
STAMP (M)[AC] BAT0=Not charging load1=12.99 shared box
BENCH name=editor_second_invocation_exec_open_ACK_exit n=64 required_n=10000 p50=3419391 p99=9779321 ci95_p50=[3348989,3580269] ci95_p99=[4588938,9779321] gate_p50=0 gate_p99=10000000 dropped=0 (M)[AC] power=Not charging load1=12.99 verdict=TRACK
IPC (M)[AC] load1=12.99 invocations=64 opened_tabs=64 includes=exec,parse,open,ACK,exit,reap mode=TRACK
exit=0
```

Final state: release all exits 0; sanitizer check exits 0; editor fuzz exits 0
(M)[AC], with LeakSanitizer explicitly disabled. Named benchmark-honesty fixes
and their regressions are implemented. Large-corpus G1 measurement, native
allocation attribution, idle wake acceptance and complete production bulk
pipeline acceptance remain limited as detailed above. Native diagnostic rows
return 1 for real guarded allocations, as intended. No editor production
changes were made. The worker stops with no Git mutations.
