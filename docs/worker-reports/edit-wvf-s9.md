# edit-wvf — M0 lineidx G7/G7j investigation, session 9

Final status (2026-10-10 15:59 UTC; scope cutoff was 15:49:46 UTC): measurement and comparison implemented; performance misses remain. No production kernel change is justified by the paired results. Release build, full lineidx sanitizer suite, combined ownership and fuzz are green. Full repository sanitizer acceptance is incomplete because the shared display queue exceeded the worker budget; see the exact outcome below. This report is self-contained; build logs are scratch artifacts, not required to read it.

Scope: `bench/lineidx_bench.c` gains only a comparison variant. `src/lineidx`, `tests/lineidx_test.c`, scan and editor production code are unchanged. No new globals or typing-path allocations. Git was read-only; no bd, HANDOFF or worklog edits. Started 2026-10-10 15:19:31 UTC (2026-10-11 Brisbane). All execution uses `DISPLAY=:99 EDIT_DISPLAY=:99`.

Read: CLAUDE.md, the assigned bead, M0's G7/G7j and scan sections, hardware profile scan measurements, P1.8 decision family, relevant lineidx benchmark review sections, lineidx STATUS and compact-ownership decision. The explicit worker instructions override CLAUDE's general handoff/worklog and git restrictions where necessary.

## Findings and disposition

- G7 full-index miss: reproduced by the existing failing gate row before edits. Current and pre-4w1.57 both miss. Original paired prefaulted medians were 81.367 / 81.439 ms (M)[AC], essentially identical. There is no established layout regression to fix. Ordinary G7 additionally charges a new mapping and its faults. The raw scan floor is sometimes below 80 ms (G), so this investigation does **not** prove that G7 is inherently impossible; the current path remains above its p50 gate.
- G7j fresh-index miss: reproduced before edits; both layouts miss. The existing benchmark builds the full index before submitting a correct null-backend viewport, and charges that work. At scan_bench's 15.65–16.75 GB/s (M)[AC], even reading the target's 966,366,840 bytes requires 61.75–57.69 ms (E), above 30/50 ms (G). Its actual full scan requires 68.61–64.10 ms (E), before mapping, worker and viewport costs. At these measured rates the sequential fresh G7j contract is unreachable. This is a conditional scan-throughput estimate, not a universal hardware bound or a proposal to change the gate.
- Partial worker seek miss: reproduced before edits and after. It begins after 536,870,912 bytes and scans 429,506,560 bytes (M)[AC]. At the observed primitive rates that scan costs 27.44–25.64 ms (E), leaving little p50 room against 30 ms (G). The scan floor alone does not prove this row unreachable. The historical kernel also misses; no reproducible scratch-layout cause was found.
- G10f: retain the compact six-byte/chunk scratch design and existing combined-ownership test. No ownership expansion was made.

The raw scan changes with load alongside both layouts; the relative layout ordering changes between rounds. Single timings are not treated as pass/fail evidence. No other hot-path issue is fixed under this bead. Gates are unchanged; Tobias owns any contract decision.

## Measurement method and numbers table

Power was `Not charging`, tagged (M)[AC], throughout. No quiet-box wait. Nine measured samples per lineidx row, with two warm-ups; scan_bench has twenty samples. The second ordinary comparison sequence fits within the same UTC minute: historical 15:21:12.871–19.999, current 15:21:20.000–26.828, raw scan 15:21:26.829–32.204. All use the same current support library and scanner; only the lineidx object changes.

The added `--compare-scan` variant alternates current index build and `scan_count` order each round on exactly the same prefaulted log mapping, verifies newline counts against the independent scalar oracle, and prints per-sample load/power and times. Build timing includes request setup, worker execution and bounded UI adoption; creation/mmap/destruction are outside it. Raw scan excludes those costs. It is explicitly TRACK, and refuses `--cold`. The same benchmark object is linked to the historical lineidx object for the historical variant. Two rounds reverse executable ordering; all four runs fit in UTC 15:22:30.921–43.016.

All table latencies are p50/p99 ms (M)[AC]; load columns are measured load1 (M)[AC]. Gate columns are (G).

| Run | Load | G7 new mapping | Prefaulted build | Fresh G7j + viewport | Partial worker seek + viewport |
|---|---|---|---|---|---|
| Red default current | 2.51–2.75 | 123.113 / 142.833 | 86.083 / 86.518 | 147.462 / 238.973 | 87.421 / 110.896 |
| Pair 1 current | 2.82–2.68 | 117.813 / 121.964 | 81.367 / 87.566 | 121.550 / 141.146 | 58.448 / 59.085 |
| Pair 1 historical | 2.68–2.54 | 116.709 / 139.433 | 81.439 / 90.718 | 116.575 / 116.934 | 52.011 / 58.526 |
| Pair 2 historical | 2.46–2.34 | 118.795 / 132.179 | 96.718 / 99.010 | 136.045 / 138.615 | 51.522 / 59.800 |
| Pair 2 current | 2.34–2.23 | 117.999 / 118.968 | 81.449 / 82.014 | 118.753 / 138.231 | 51.704 / 52.951 |
| After default current | 3.22–3.19 | 121.699 / 130.115 | 91.205 / 109.713 | 163.596 / 195.851 | 97.166 / 136.046 |
| Gate | — | 80 / 125 | TRACK | 30 / 50 | 30 / 50 |

| Same-fixture comparison | Load | Prefaulted index ms (M)[AC] | Raw scan ms (M)[AC] | Raw p50 GB/s (M)[AC], derived from measured latency |
|---|---|---|---|---|
| Round 1 current | 4.08 | 84.178 / 102.041 | 74.003 / 86.289 | 14.509 |
| Round 1 historical | 3.99 | 92.480 / 113.629 | 74.778 / 109.818 | 14.358 |
| Round 2 historical | 3.99 | 101.974 / 110.429 | 82.181 / 107.909 | 13.065 |
| Round 2 current | 3.75 | 106.859 / 121.106 | 84.319 / 93.727 | 12.734 |

Round 1 favors current; round 2 favors historical, while raw scan throughput also falls. This does not establish a layout slowdown. At the round 2 raw medians the scan alone exceeds G7's 80 ms p50 (G); at round 1 it does not. Keep that limitation explicit.

## Red run (before the comparison change)

There is no lineidx .args file in this checkout, so its defaults were used exactly as make bench does. Existing named gate rows provide the bead's requested red before any edit.

```text
COMMAND DISPLAY=:99 EDIT_DISPLAY=:99 timeout 600 build/bench/lineidx_bench
# lineidx_bench file=/tmp/edit-corpus/log_1g.txt reps=9 fresh_index=1 cold=0 track=0 power=Not charging
# line_count=8947842 target_line=8053057 target_byte=966366840
BENCH name=G7_index_1g_fixture_warm n=9 p50=123112650 p99=142833330 ci95=[120138616,142833330] ns (M)[AC] load=2.51 gate_p50=80000000 gate_p99=125000000 ns (G) pass=0
BENCH name=TRACK_build_prefaulted n=9 p50=86082679 p99=86517979 ci95=[84346760,86517979] ns (M)[AC] load=2.51 gate_p50=0 gate_p99=0 ns (G) pass=1
UNVALIDATED G7 cold (G) 600/800 ms; G7j cold (G) 250/350 ms: run --cold with manual verified drop_caches per sample
BENCH name=G7j_unindexed_viewport_90pct_fixture_warm n=9 p50=147462321 p99=238973055 ci95=[117812770,238973055] ns (M)[AC] load=2.47 gate_p50=30000000 gate_p99=50000000 ns (G) pass=0
BENCH name=TRACK_cancel_ack_gate_1ms_5ms n=9 p50=6296 p99=9313 ci95=[5976,9313] ns (M)[AC] load=2.47 gate_p50=0 gate_p99=0 ns (G) pass=1
BENCH name=TRACK_line_to_byte_exact n=200 p50=12259 p99=17865 ci95=[12033,12465] ns (M)[AC] load=2.47 gate_p50=0 gate_p99=0 ns (G) pass=1
BENCH name=TRACK_byte_to_line_exact n=200 p50=6331 p99=10921 ci95=[5749,6991] ns (M)[AC] load=2.47 gate_p50=0 gate_p99=0 ns (G) pass=1
BENCH name=TRACK_edit_refresh_1chunk n=200 p50=19512 p99=31800 ci95=[18990,19990] ns (M)[AC] load=2.47 gate_p50=0 gate_p99=0 ns (G) pass=1
BENCH name=mem_bytes_per_chunk value=16.459 B (M)[AC] load=2.47 gate=16.5 (G) within_limit=1 (269672 B reported, 16384 chunks)
BENCH name=TRACK_G6c_lineidx_cancel_logical n=9 p50=458 p99=648 ci95=[314,648] ns (M)[AC] load=2.47 gate_p50=1000000 gate_p99=5000000 ns (G) pass=1
BENCH name=TRACK_G6c_lineidx_cancel_cpu_slice n=9 p50=2004249 p99=2006199 ci95=[2003742,2006199] ns (M)[AC] load=2.47 gate_p50=5000000 gate_p99=5000000 ns (G) pass=1
BENCH name=TRACK_G7j_worker_partial_seek_viewport_90pct_fixture_warm n=9 p50=87421239 p99=110896035 ci95=[55557375,110896035] ns (M)[AC] load=2.75 gate_p50=30000000 gate_p99=50000000 ns (G) pass=0
lineidx_bench: MISS
EXIT=1
```

## Back-to-back ordinary runs and raw scan_bench

Exact printed rows, with before/after stamps; per-sample chatter omitted. Historical/current runs exit nonzero because the named gates still miss. Raw scan verdicts belong to scan_bench alone.

```text
RUN current-pair1
BEFORE utc=2026-10-10T15:20:53.462372+00:00 power=Not charging load=2.90 1.75 1.33 2/1534 3
BENCH name=G7_index_1g_fixture_warm n=9 p50=117812552 p99=121964358 ci95=[117046064,121964358] ns (M)[AC] load=2.82 gate_p50=80000000 gate_p99=125000000 ns (G) pass=0
BENCH name=TRACK_build_prefaulted n=9 p50=81366797 p99=87566039 ci95=[79779077,87566039] ns (M)[AC] load=2.82 gate_p50=0 gate_p99=0 ns (G) pass=1
BENCH name=G7j_unindexed_viewport_90pct_fixture_warm n=9 p50=121550260 p99=141145548 ci95=[117627938,141145548] ns (M)[AC] load=2.82 gate_p50=30000000 gate_p99=50000000 ns (G) pass=0
BENCH name=TRACK_G7j_worker_partial_seek_viewport_90pct_fixture_warm n=9 p50=58448304 p99=59085153 ci95=[51441758,59085153] ns (M)[AC] load=2.68 gate_p50=30000000 gate_p99=50000000 ns (G) pass=0
AFTER utc=2026-10-10T15:21:00.451167+00:00 power=Not charging load=2.68 1.74 1.33 1/1530 6
EXIT=1
```

```text
RUN pre-pair1
BEFORE utc=2026-10-10T15:21:00.451538+00:00 power=Not charging load=2.68 1.74 1.33 1/1530 6
BENCH name=G7_index_1g_fixture_warm n=9 p50=116709140 p99=139432777 ci95=[115955626,139432777] ns (M)[AC] load=2.68 gate_p50=80000000 gate_p99=125000000 ns (G) pass=0
BENCH name=TRACK_build_prefaulted n=9 p50=81438673 p99=90718391 ci95=[79293326,90718391] ns (M)[AC] load=2.68 gate_p50=0 gate_p99=0 ns (G) pass=1
BENCH name=G7j_unindexed_viewport_90pct_fixture_warm n=9 p50=116574614 p99=116934305 ci95=[116159644,116934305] ns (M)[AC] load=2.54 gate_p50=30000000 gate_p99=50000000 ns (G) pass=0
BENCH name=TRACK_G7j_worker_partial_seek_viewport_90pct_fixture_warm n=9 p50=52011357 p99=58525865 ci95=[50699192,58525865] ns (M)[AC] load=2.54 gate_p50=30000000 gate_p99=50000000 ns (G) pass=0
AFTER utc=2026-10-10T15:21:07.278756+00:00 power=Not charging load=2.54 1.73 1.33 2/1536 9
EXIT=1
```

```text
RUN scan-pair1
BEFORE utc=2026-10-10T15:21:07.279535+00:00 power=Not charging load=2.54 1.73 1.33 2/1536 9
TRACK (M)[AC] load1=2.50 scan_count               p50 15.65 GB/s  p99 15.24 GB/s  gates p50 >= 12.0 / p99 >= 12.0 (G)  PASS
TRACK (M)[AC] load1=2.50 scan_find_nth_newline    p50 19.12 GB/s  p99 18.76 GB/s  gates p50 >= 12.0 / p99 >= 12.0 (G)  PASS
TRACK (M)[AC] load1=2.50 scan_count_G6_basis      p50 15.65 GB/s  p99 15.24 GB/s  gates p50 >= 16.0 / p99 >= 12.0 (G)  MISS
AFTER utc=2026-10-10T15:21:12.856638+00:00 power=Not charging load=2.50 1.73 1.33 1/1545 10
EXIT=1
```

```text
RUN pre-pair2
BEFORE utc=2026-10-10T15:21:12.871464+00:00 power=Not charging load=2.50 1.73 1.33 1/1545 10
BENCH name=G7_index_1g_fixture_warm n=9 p50=118795254 p99=132178741 ci95=[117436066,132178741] ns (M)[AC] load=2.46 gate_p50=80000000 gate_p99=125000000 ns (G) pass=0
BENCH name=TRACK_build_prefaulted n=9 p50=96717644 p99=99009573 ci95=[81142292,99009573] ns (M)[AC] load=2.46 gate_p50=0 gate_p99=0 ns (G) pass=1
BENCH name=G7j_unindexed_viewport_90pct_fixture_warm n=9 p50=136045198 p99=138615277 ci95=[126445547,138615277] ns (M)[AC] load=2.46 gate_p50=30000000 gate_p99=50000000 ns (G) pass=0
BENCH name=TRACK_G7j_worker_partial_seek_viewport_90pct_fixture_warm n=9 p50=51521565 p99=59799976 ci95=[51332536,59799976] ns (M)[AC] load=2.34 gate_p50=30000000 gate_p99=50000000 ns (G) pass=0
AFTER utc=2026-10-10T15:21:19.999534+00:00 power=Not charging load=2.34 1.72 1.33 1/1531 13
EXIT=1
```

```text
RUN current-pair2
BEFORE utc=2026-10-10T15:21:20.000132+00:00 power=Not charging load=2.34 1.72 1.33 1/1531 13
BENCH name=G7_index_1g_fixture_warm n=9 p50=117998602 p99=118968339 ci95=[117669642,118968339] ns (M)[AC] load=2.34 gate_p50=80000000 gate_p99=125000000 ns (G) pass=0
BENCH name=TRACK_build_prefaulted n=9 p50=81449263 p99=82014267 ci95=[80691187,82014267] ns (M)[AC] load=2.34 gate_p50=0 gate_p99=0 ns (G) pass=1
BENCH name=G7j_unindexed_viewport_90pct_fixture_warm n=9 p50=118752790 p99=138231081 ci95=[118086094,138231081] ns (M)[AC] load=2.23 gate_p50=30000000 gate_p99=50000000 ns (G) pass=0
BENCH name=TRACK_G7j_worker_partial_seek_viewport_90pct_fixture_warm n=9 p50=51703743 p99=52950666 ci95=[51347125,52950666] ns (M)[AC] load=2.23 gate_p50=30000000 gate_p99=50000000 ns (G) pass=0
AFTER utc=2026-10-10T15:21:26.828174+00:00 power=Not charging load=2.23 1.71 1.33 1/1530 16
EXIT=1
```

```text
RUN scan-pair2
BEFORE utc=2026-10-10T15:21:26.828541+00:00 power=Not charging load=2.23 1.71 1.33 1/1530 16
TRACK (M)[AC] load1=2.13 scan_count               p50 16.75 GB/s  p99 15.41 GB/s  gates p50 >= 12.0 / p99 >= 12.0 (G)  PASS
TRACK (M)[AC] load1=2.13 scan_find_nth_newline    p50 20.46 GB/s  p99 17.52 GB/s  gates p50 >= 12.0 / p99 >= 12.0 (G)  PASS
TRACK (M)[AC] load1=2.13 scan_count_G6_basis      p50 16.75 GB/s  p99 15.41 GB/s  gates p50 >= 16.0 / p99 >= 12.0 (G)  PASS
AFTER utc=2026-10-10T15:21:32.203628+00:00 power=Not charging load=2.13 1.70 1.33 1/1529 17
EXIT=0
```

## Same-fixture comparison runs (TRACK correctness green)

These exit-zero runs verify comparison correctness; they are not performance acceptance.

```text
RUN compare-current1 --compare-scan
BEFORE utc=2026-10-10T15:22:30.920531+00:00 power=Not charging load=4.08 2.35 1.57 2/1554 3
BENCH name=TRACK_compare_prefaulted_index n=9 p50=84177666 p99=102040641 ci95=[82043707,102040641] ns (M)[AC] load=4.08 gate_p50=0 gate_p99=0 ns (G) pass=1
BENCH name=TRACK_compare_raw_scan n=9 p50=74003015 p99=86288941 ci95=[64958896,86288941] ns (M)[AC] load=4.08 gate_p50=0 gate_p99=0 ns (G) pass=1
# comparison excludes mmap/create; index includes request, worker and UI adoption; raw scan is the payload floor; gates unvalidated
AFTER utc=2026-10-10T15:22:33.702412+00:00 power=Not charging load=4.08 2.35 1.57 2/1553 6
EXIT=0
```

```text
RUN compare-pre1 --compare-scan
BEFORE utc=2026-10-10T15:22:33.702760+00:00 power=Not charging load=4.08 2.35 1.57 2/1553 6
BENCH name=TRACK_compare_prefaulted_index n=9 p50=92480349 p99=113628635 ci95=[90023454,113628635] ns (M)[AC] load=3.99 gate_p50=0 gate_p99=0 ns (G) pass=1
BENCH name=TRACK_compare_raw_scan n=9 p50=74778220 p99=109817953 ci95=[69939713,109817953] ns (M)[AC] load=3.99 gate_p50=0 gate_p99=0 ns (G) pass=1
# comparison excludes mmap/create; index includes request, worker and UI adoption; raw scan is the payload floor; gates unvalidated
AFTER utc=2026-10-10T15:22:36.672717+00:00 power=Not charging load=3.99 2.36 1.58 2/1554 9
EXIT=0
```

```text
RUN compare-pre2 --compare-scan
BEFORE utc=2026-10-10T15:22:36.673085+00:00 power=Not charging load=3.99 2.36 1.58 3/1554 9
BENCH name=TRACK_compare_prefaulted_index n=9 p50=101973567 p99=110428512 ci95=[96728825,110428512] ns (M)[AC] load=3.99 gate_p50=0 gate_p99=0 ns (G) pass=1
BENCH name=TRACK_compare_raw_scan n=9 p50=82181114 p99=107908718 ci95=[72491332,107908718] ns (M)[AC] load=3.99 gate_p50=0 gate_p99=0 ns (G) pass=1
# comparison excludes mmap/create; index includes request, worker and UI adoption; raw scan is the payload floor; gates unvalidated
AFTER utc=2026-10-10T15:22:39.694240+00:00 power=Not charging load=3.75 2.34 1.58 2/1554 12
EXIT=0
```

```text
RUN compare-current2 --compare-scan
BEFORE utc=2026-10-10T15:22:39.694630+00:00 power=Not charging load=3.75 2.34 1.58 2/1554 12
BENCH name=TRACK_compare_prefaulted_index n=9 p50=106859053 p99=121106208 ci95=[103589434,121106208] ns (M)[AC] load=3.75 gate_p50=0 gate_p99=0 ns (G) pass=1
BENCH name=TRACK_compare_raw_scan n=9 p50=84318869 p99=93727108 ci95=[78076702,93727108] ns (M)[AC] load=3.75 gate_p50=0 gate_p99=0 ns (G) pass=1
# comparison excludes mmap/create; index includes request, worker and UI adoption; raw scan is the payload floor; gates unvalidated
AFTER utc=2026-10-10T15:22:43.015748+00:00 power=Not charging load=3.75 2.34 1.58 2/1545 15
EXIT=0
```

## After rows (performance remains red)

```text
COMMAND DISPLAY=:99 EDIT_DISPLAY=:99 timeout 600 build/bench/lineidx_bench
# lineidx_bench file=/tmp/edit-corpus/log_1g.txt reps=9 fresh_index=1 cold=0 track=0 power=Not charging
# line_count=8947842 target_line=8053057 target_byte=966366840
BENCH name=G7_index_1g_fixture_warm n=9 p50=121698550 p99=130114694 ci95=[119855262,130114694] ns (M)[AC] load=3.22 gate_p50=80000000 gate_p99=125000000 ns (G) pass=0
BENCH name=TRACK_build_prefaulted n=9 p50=91205276 p99=109713302 ci95=[83580110,109713302] ns (M)[AC] load=3.22 gate_p50=0 gate_p99=0 ns (G) pass=1
UNVALIDATED G7 cold (G) 600/800 ms; G7j cold (G) 250/350 ms: run --cold with manual verified drop_caches per sample
BENCH name=G7j_unindexed_viewport_90pct_fixture_warm n=9 p50=163595762 p99=195850663 ci95=[153805811,195850663] ns (M)[AC] load=3.12 gate_p50=30000000 gate_p99=50000000 ns (G) pass=0
BENCH name=TRACK_cancel_ack_gate_1ms_5ms n=9 p50=16686 p99=22239 ci95=[12074,22239] ns (M)[AC] load=3.12 gate_p50=0 gate_p99=0 ns (G) pass=1
BENCH name=TRACK_line_to_byte_exact n=200 p50=9116 p99=19721 ci95=[8960,9352] ns (M)[AC] load=3.12 gate_p50=0 gate_p99=0 ns (G) pass=1
BENCH name=TRACK_byte_to_line_exact n=200 p50=4903 p99=8772 ci95=[4551,5523] ns (M)[AC] load=3.12 gate_p50=0 gate_p99=0 ns (G) pass=1
BENCH name=TRACK_edit_refresh_1chunk n=200 p50=16847 p99=20558 ci95=[16324,17363] ns (M)[AC] load=3.12 gate_p50=0 gate_p99=0 ns (G) pass=1
BENCH name=mem_bytes_per_chunk value=16.459 B (M)[AC] load=3.12 gate=16.5 (G) within_limit=1 (269672 B reported, 16384 chunks)
BENCH name=TRACK_G6c_lineidx_cancel_logical n=9 p50=721 p99=1022 ci95=[419,1022] ns (M)[AC] load=3.12 gate_p50=1000000 gate_p99=5000000 ns (G) pass=1
BENCH name=TRACK_G6c_lineidx_cancel_cpu_slice n=9 p50=2006104 p99=2008991 ci95=[2005469,2008991] ns (M)[AC] load=3.12 gate_p50=5000000 gate_p99=5000000 ns (G) pass=1
BENCH name=TRACK_G7j_worker_partial_seek_viewport_90pct_fixture_warm n=9 p50=97166353 p99=136046439 ci95=[75790563,136046439] ns (M)[AC] load=3.19 gate_p50=30000000 gate_p99=50000000 ns (G) pass=0
lineidx_bench: MISS
EXIT=1
```

## Functional green and acceptance

GCC 13.3 release build with `-std=c11 -Wall -Wextra -Werror -Wshadow -Wconversion` passes. Clang 18.1 ASan/UBSan combined-ownership guard passes. Seeded lineidx libFuzzer ran for 61 seconds (M)[AC] against 60 seconds (G), 3,304 executions (M)[AC], exit zero. Release typing guard passes with zero allocations for 10,000 keys (M)[AC]. Full lineidx sanitizer suite also passes. The outside-sandbox full repository sanitizer make check passed through GL, lineidx, piece and prewake, but remained queued before raster_test at the wall-clock cutoff. Only this worker's verified make-check process group was stopped with SIGTERM to obey the budget. No full make-check exit-zero result is claimed. At the earlier scope cutoff it had passed CLI/editor/file/find/font and was queued before GL. The sandbox cannot connect to :99; an approved outside-sandbox probe connects successfully. Leak detection remains disabled there too.

LeakSanitizer cannot run in this sandbox: sanitizer/fuzz executions use `ASAN_OPTIONS=detect_leaks=0`; the coordinator must rerun with leaks enabled.

```text
COMMAND DISPLAY=:99 EDIT_DISPLAY=:99 make -j4 all build/fuzz/lineidx_fuzz
EXIT=0
COMMAND DISPLAY=:99 EDIT_DISPLAY=:99 make all
make: Nothing to be done for 'all'.
EXIT=0
COMMAND DISPLAY=:99 EDIT_DISPLAY=:99 build/tests/lineidx_test --review=alloc
lineidx typing: keys=10000 allocations=0 guard=active
lineidx_test: ok
EXIT=0
COMMAND ASAN_OPTIONS=detect_leaks=0 DISPLAY=:99 EDIT_DISPLAY=:99 build/san/tests/lineidx_test --review=combined
G10f combined sparse_10g.bin (M)[AC]: open=1063432 attached_find_save=3236000 index_queued=6898952 peak=6898952 gate=7242880 (G)
lineidx_test: ok
EXIT=0
COMMAND ASAN_OPTIONS=detect_leaks=0 DISPLAY=:99 EDIT_DISPLAY=:99 build/san/tests/lineidx_test
lineidx_test: ok
EXIT=0
COMMAND ASAN_OPTIONS=detect_leaks=0 DISPLAY=:99 EDIT_DISPLAY=:99 build/fuzz/lineidx_fuzz build/wvf/fuzz-corpus -max_total_time=60 -timeout=15
Done 3304 runs in 61 second(s)
EXIT=0
```

Benchmark self-check green (all existing gate/oracle/wait protections):

```text
cold sample refused: resident_pages=1 of 1
SELF_CHECK 15 PASS fresh_prefix=0 viewport_submitted=1
SELF_CHECK 16 PASS independent_oracle=1 rejects_tiny_fixture=1
SELF_CHECK 17 PASS cold_index_miss=1 cold_jump_miss=1 unvalidated_cold_fails=1
SELF_CHECK 18 PASS rejects_invalid_workloads=1
SELF_CHECK 18 waiting for a stranded, unsubmitted index (must return a failure)
SELF_CHECK 18 unsubmitted: completion failure prefix=0/1 complete=0 building=0 deadline_expired=0
SELF_CHECK 18 stranded worker: completion failure prefix=0/1 complete=0 building=1 deadline_expired=1
SELF_CHECK 18 stranded retirement: completion failure prefix=0/1 complete=0 building=1 deadline_expired=1
SELF_CHECK 18 PASS bounded_stranded_wait=1 active_deadline=1 retirement_deadline=1
SELF_CHECK 18 PASS final_publication_adopted=1
SELF_CHECK pending PASS completed=1 pending_before=256 adopted=1 exact=1
EXIT=0
```

Cold comparison refusal (expected exit 2, never silently labelled warm):

```text
COMMAND DISPLAY=:99 EDIT_DISPLAY=:99 build/bench/lineidx_bench --compare-scan --cold
invalid workload: use --file=PATH --reps=3..200 --track --cold --compare-scan --timeout-ms=1..600000; comparison requires warm input; --partial removed (fresh index required)
EXIT=2
```

## Reproduction

```sh
mkdir -p build/wvf
make -j4 build/bench/lineidx_bench build/bench/scan_bench
git show 1247762^:src/lineidx/lineidx.c > build/wvf/pre-4w1.57.c
gcc -std=c11 -Wall -Wextra -Werror -Wshadow -Wconversion -D_GNU_SOURCE -pthread -O2 -g -msse2 -Isrc -Isrc/lineidx -c build/wvf/pre-4w1.57.c -o build/wvf/pre-4w1.57.o
gcc -O2 -g -pthread build/rel/bench/lineidx_bench.o build/wvf/pre-4w1.57.o build/libedit.a -lm -ldl $(cat src/*/LDLIBS) -o build/wvf/lineidx_pre-4w1.57
DISPLAY=:99 EDIT_DISPLAY=:99 build/bench/lineidx_bench
DISPLAY=:99 EDIT_DISPLAY=:99 build/wvf/lineidx_pre-4w1.57
DISPLAY=:99 EDIT_DISPLAY=:99 build/bench/scan_bench
DISPLAY=:99 EDIT_DISPLAY=:99 build/bench/lineidx_bench --compare-scan
DISPLAY=:99 EDIT_DISPLAY=:99 build/wvf/lineidx_pre-4w1.57 --compare-scan
```

## Display environment and supplemental check

A supplemental run of remaining tests inside the sandbox stopped at
`file_kill_test` because its viewport initialization cannot access :99.
The source was only read to identify the failed initialization; no file/view
code was changed. The same file test later passes in the outside-sandbox full
check (`file_kill_test: PASS visibility/cancellation; power-loss barriers
unverified`). This confirms the access limitation rather than a production
regression:

```text
file_kill_test:198: FAIL rc == 0
file_kill_test:324: FAIL WIFEXITED(status) && WEXITSTATUS(status) == 0
file_kill_test:324: FAIL WIFEXITED(status) && WEXITSTATUS(status) == 0
file_kill_test: FAIL visibility/cancellation; power-loss barriers unverified
COMMAND DISPLAY=:99 xdpyinfo (inside sandbox)
xdpyinfo:  unable to open display ":99".
EXIT=1
COMMAND DISPLAY=:99 EDIT_DISPLAY=:99 xdpyinfo (approved outside sandbox)
name of display:    :99
version number:    11.0
vendor string:    The X.Org Foundation
EXIT=0
```

The first sandbox `make check` reached the CLI test and failed after the shared
lock timeout, before opening a window:

```text
COMMAND ASAN_OPTIONS=detect_leaks=0 DISPLAY=:99 EDIT_DISPLAY=:99 make -j4 check (sandbox)
base_test: PASS
== build/san/tests/cli_test
waiting for display :99 (held by unknown)
cli_test: display lock timeout: display :99 (held by unknown)
make: *** [Makefile:105: check] Error 1
EXIT=2
```

The outside-sandbox `make check` on the authorized :99 lane with
`ASAN_OPTIONS=detect_leaks=0` passed through GL, lineidx, piece and prewake.
It remained queued before raster at the final budget cutoff, and only this
worker's verified process group was stopped. No other worker was signalled.
The tool reported exit 143 (M)[AC] after SIGTERM; this is a budget termination,
not a test assertion failure. No display :0 access occurred.

```text
COMMAND ASAN_OPTIONS=detect_leaks=0 DISPLAY=:99 EDIT_DISPLAY=:99 make -j4 check (approved outside sandbox)
file_kill_test: PASS visibility/cancellation; power-loss barriers unverified
lineidx_test: ok
piece_test: all passed
== build/san/tests/prewake_test
prewake_test: variant=a PASS idle threshold, three triggers, debounce, errors, spin budget
prewake_test: variant=b PASS idle threshold, three triggers, debounce, errors, spin budget
prewake_test: variant=c PASS idle threshold, three triggers, debounce, errors, spin budget
== build/san/tests/raster_test
waiting for display :99 (held by another worker)
STOPPED: own process group only; shared display queue exhausted worker budget
EXIT=143 (SIGTERM)
```

## Missing and out of scope

Missing: complete green repository `make check`. The coordinator must rerun it on :99 outside the sandbox, with leaks enabled, when the shared display lane is available. No code was added after the scope cutoff. No green G7/G7j performance run is claimed. Cold eviction and a real-display endpoint remain unvalidated; viewport rows use the existing null backend. The scan-throughput question belongs to edit-2hc. Other request/worker/adoption/mapping overhead and the existing full-build fresh-jump benchmark are reported, not changed. The pre-4w1.57 comparison also excludes later prepaid restart changes by construction; the tested initial-build path does not use that API. No header/API, allocator, result ownership, repair/restart or gate changes. Design choice: `docs/decisions/edit-wvf.md`.
