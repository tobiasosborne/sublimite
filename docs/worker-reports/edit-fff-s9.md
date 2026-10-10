# edit-fff — module regression guards, worker report

Scope: bead edit-fff only, branch wt/edit-fff, baseline `cff1a7d5641cc6d35868d5eecd407aaa87cae979`. Git was used only for read-only history/diffs; no bd, HANDOFF, worklog, Makefile, production source, or corpus edits. All displays were explicitly :99.

## Outcome and gate decision

(a) Investigation complete, **original allocation-census gate still fails**. The current tabs-plus-caller-undo reserve is **1,832,480 B (M)[AC]**, against **1,000,000 B (G)**. This is load-independent. The new `tabs_test memory-gate` preserves a reproducible failing check (it is an explicit diagnostic, not added to the passing default `make check` path); the ordinary suite adds census/accounting checks. No production-memory fix or gate waiver is claimed. The bead explicitly permits reporting a gate decision instead of fixing growth: retain the expansion and atomic-replay reserves required by the settled undo safety decisions, report the miss, and leave allocation-budget adjudication to the coordinator. [Decision](../decisions/edit-fff.md).

(b), (c), (d): all three adjacent reruns pass the named numeric thresholds on unchanged production code. The M0 timing misses are classified as load artefacts, with no hot-path changes or timing bisect warranted.

(e): replacement rows pass both numeric thresholds in all three runs. Log document/selection durations also fall below their numeric limits, but **all three runs retain incomplete commands**. Those timings are lower bounds on unfinished work, not complete-command gate passes. This inherited harness/coverage limitation was already recorded in P3.2b and is reported, not fixed. No confirmed source regression was found; view source/header are identical to commit 088dbf3. The earlier P3.2 decision did not record these bulk rows passing, and recorded multi-second log selection work instead, so there is no last passing decision for their completeness condition to bisect against.

Acceptance is therefore partial: build, sanitizer suite and fuzz results are recorded below; the allocation gate remains red and the incomplete log rows remain uncertified. No artificial green run was manufactured for either limitation.

## What changed

- `tests/tabs_test.c`: empty-history census splits tab control/arena, undo control, history reserve, replay reserve and committed undo pages. It validates reserved-byte accounting and zero live/committed empty history. `memory-gate` exits nonzero on the unchanged combined reserve limit; default functional tests continue to pass. Buffer allocation is excluded by the named bench; the census borrows one tree for distinct empty undo logs, whose reserve is independent of tree content. The original bench still uses distinct real buffers and reproduces the same total.
- `bench/utf8_bench.c`: adds load to its existing power stamp.
- `bench/trace_bench.c`: adds a power/load stamp before timing.
- This report and `docs/decisions/edit-fff.md`. No new globals, typing allocation, benchmark workloads, timing logic or thresholds.

## (a) Allocation census and historical attribution

All bytes in the following table are **(M)[AC]**, historical-source layout replay on this box; the original P4.4 and M0 totals also match their recorded live censuses exactly. Page rounding was measured through the actual init routines, not inferred from a timing. The replay combines the P4.4 tabs layout with original P1.5 undo to reconstruct the recorded P4.4 total; it is explicitly a mixed-version reconstruction. The P4.4 committed tree already includes P1.5d undo and does not reproduce its decision document's smaller total. All reconstructions were isolated under ignored build output; the live source and Git state were never switched.

| Version / reconstruction | Tab control B | Tab arena B | Undo controls B | Undo virtual reserve B | Combined B |
|---|---:|---:|---:|---:|---:|
| P4.4 recorded: tabs 8bd26ce + undo 78d2118 | 128 | 557056 | 16800 | 409600 | 983584 |
| P4.4 committed tree 8bd26ce | 128 | 557056 | 22400 | 819200 | 1398784 |
| M0 dd84a49 | 128 | 561152 | 39200 | 1228800 | 1829280 |
| edit-zzj.13 slice 2, 687492f | 128 | 561152 | 39200 | 1228800 | 1829280 |
| Current (includes slice 3 dc4015e) | 128 | 561152 | 42400 | 1228800 | 1832480 |

Current history reserve is **819,200 B (M)[AC]** and replay backup reserve **409,600 B (M)[AC]**. Empty undo committed pages total **0 B (M)[AC]**; tabs plus undo control plus committed undo pages total **603,680 B (M)[AC]**. This alternative accounting is informational, not a replacement or pass for the named reserve gate.

From recorded P4.4 to current: tab page rounding grew **4,096 B (M)[AC]**; undo controls grew **25,600 B (M)[AC]**; P1.5d history reserve grew **409,600 B (M)[AC]**; P1.5e replay backup reserve added **409,600 B (M)[AC]**. Total growth **848,896 B (M)[AC]**. M0 to current adds **3,200 B (M)[AC]**, exactly the slice-3 constant-time undo-history fields. P4.I editor wiring and slice-2 per-buffer slab recycling do not enter this benchmark: it allocates piece/undo resources directly and counts no editor slabs.

`src/undo/undo.h` explicitly distinguishes virtual reserve from committed memory. P1.5d expanded capacity to cover worst-case reference expansion of every admitted insertion; P1.5e added init-reserved backups for atomic failure rollback. Simply truncating either reservation to meet this empty-history reserve guard would revoke those safety guarantees; a different compact design would require separate implementation and review. The decision retains the strict reserve gate and records the miss; it does not silently switch the bench to committed bytes.

Historical census transcript:

```text
BEFORE utc=2026-10-10T16:15:38.439054+00:00 power=Not charging loadavg=3.94 2.56 1.80 4/1354 3 (M)[AC]
recorded-P4.4 tab_control=128 tab_arena=557056 undo_control=16800 undo_reserve=409600 combined=983584
P4.4-commit tab_control=128 tab_arena=557056 undo_control=22400 undo_reserve=819200 combined=1398784
M0-dd84a49 tab_control=128 tab_arena=561152 undo_control=39200 undo_reserve=1228800 combined=1829280
slice2-687492f tab_control=128 tab_arena=561152 undo_control=39200 undo_reserve=1228800 combined=1829280
current tab_control=128 tab_arena=561152 undo_control=42400 undo_reserve=1228800 combined=1832480
AFTER utc=2026-10-10T16:15:38.447306+00:00 power=Not charging loadavg=3.94 2.56 1.80 4/1354 8 (M)[AC]
```

### New failing test, written and run before deciding on any production change

Command: `DISPLAY=:99 EDIT_DISPLAY=:99 build/tests/tabs_test memory-gate`, exit 1 (M)[AC].

```text
tabs_test: census (M)[AC] power=Not charging load1=2.50 tab_control=128 tab_arena=561152 undo_control=42400 history_reserve=819200 replay_reserve=409600 undo_committed=0 combined_reserve=1832480 combined_committed=603680 gate_bytes=1000000(G) reserve=MISS
edit-fff RED: tabs plus caller undo reserve exceeds 1000000 B(G)
```

Existing bench red, exit 1 (M)[AC]:

```text
tabs_bench: memory (M)[AC] load1=4.20 power=Not charging tabs=100 closed_capacity=16 tab_set_bytes=561280 caller_undo_bytes=1271200 own_plus_undo_bytes=1832480 gate_bytes=1000000(G) excluding_buffers=1 memory=MISS verdict=TRACK
tabs_bench: switch+viewport_strip (M)[AC] load1=4.20 power=Not charging tabs=100 cols=240 n=20000 p50_ns=2670 p99_ns=2796 p99_ci95=[2794,2798] gate_p99_ns=500000(G) module_share=PASS verdict=TRACK
tabs_bench: switch+all100_strip (M)[AC] load1=4.20 power=Not charging tabs=100 cols=2400 n=20000 p50_ns=26976 p99_ns=29202 p99_ci95=[29172,29238] gate_p99_ns=500000(G) module_share=PASS verdict=TRACK
```

Functional green (not a reserve-gate green), exit 0 (M)[AC]:

```text
tabs_test: census (M)[AC] power=Not charging load1=3.94 tab_control=128 tab_arena=561152 undo_control=42400 history_reserve=819200 replay_reserve=409600 undo_committed=0 combined_reserve=1832480 combined_committed=603680 gate_bytes=1000000(G) reserve=MISS
tabs_test: sequences vs independent model passed
tabs_test: UTF-8 strip, clipping, damage and hit testing passed
tabs_test: errors, bounded eviction and full-capacity reopen passed
tabs_test: closed buffer, undo history and view retention passed
tabs_test: 10000 switches + strip + MRU mallocs=0 guard=active
tabs_test: all passed
```

## (b)–(e) Numbers table and decision

Every measured p50/p99 entry is **(M)[AC]**. Gate entries are **(G)**. Indent uses microseconds; all other rows use nanoseconds. Zero percentile thresholds are ungated. The replacement miss named as “vs 1 ms” in the bead is its p50 threshold; its p99 threshold is 2 ms (G).

| Finding / row | M0 p50 / p99 (M)[AC] | Run 1 p50 / p99 (M)[AC] | Run 2 p50 / p99 (M)[AC] | Run 3 p50 / p99 (M)[AC] | Thresholds (G) |
|---|---:|---:|---:|---:|---|
| b Enter | 10.210 / 27.151 | 4.784 / 5.101 | 4.785 / 5.100 | 4.785 / 5.370 | 0 / 20 us |
| b brace | 18.685 / 30.041 | 4.782 / 5.095 | 4.782 / 5.095 | 4.667 / 5.095 | 0 / 20 us |
| c UTF-8 | 5280 / 9994 | 1705 / 3232 | 3047 / 5528 | 1714 / 3277 | 5000 / 10000 ns |
| d trace | 51 / 65 | 19 / 62 | 15 / 51 | 16 / 50 | <50 / ungated ns |
| e log document bulk1 | 1839 / 8558034 | 309 / 2564957 | 477 / 2516605 | 570 / 2567291 | 5000000 / 5555555 ns |
| e log selection bulk1 | 3069 / 8522716 | 1116 / 2477717 | 1155 / 2534700 | 1086 / 2451906 | 5000000 / 5555555 ns |
| e oneline replace bulk1 | 1532175 / 1627352 | 598612 / 654277 | 599830 / 652905 | 603315 / 670158 | 1000000 / 2000000 ns |
| e oneline replace bulk3 | 1502584 / 1567799 | 607660 / 707234 | 604888 / 661592 | 602836 / 659604 | 1000000 / 2000000 ns |
| e unicode replace bulk1 | 1188934 / 1341686 | 500540 / 560690 | 506085 / 621772 | 506467 / 613139 | 1000000 / 2000000 ns |
| e unicode replace bulk3 | 1198436 / 1245328 | 501466 / 557756 | 500089 / 596045 | 503216 / 637549 | 1000000 / 2000000 ns |

Runs were adjacent within each bench, in the order indent, UTF-8, trace, view. No `.args` file exists for any of these four benches, so each was invoked with its unmodified defaults. View defaults to TRACK, the supplied corpus, and its standard sample count. No quiet-box wait, CPU pinning, governor change, fixture generation or run discarding. Sanitizer compilation overlapped some UTF-8 runs; the start/end load stamps preserve that fact. These are shared-box comparisons, not end-to-end render gate verdicts.

M0 raw guards were actually stamped at load1 **11.33 (M)[AC]** for indent/trace, **11.28 (M)[AC]** for UTF-8 and **9.87 / 9.48 (M)[AC]** for the named view rows; the bead's campaign-level load description is not substituted for those per-command stamps. Current starts are **2.07 (M)[AC]** for indent, **2.07 / 3.02 / 4.20 (M)[AC]** for UTF-8, and **3.63 (M)[AC]** for trace/view. All current stamps are Not charging [AC]. No single run is used for adjudication.

The trace bare-clock reference drops from **47 / 54 ns (M)[AC]** in M0 to **14 / 15 ns (M)[AC]** in every current run, explaining most of the trace cost change without a source change. UTF-8's last explicit passing decision, P1.1e, recorded decode+width **1.75 / 3.3 us (M)[AC]**; current runs are compatible with that range under changing load. Indent's P4.5b recorded long-line Enter passing and brace slightly missing under load; both now have ample margin in every adjacent run. No hot-path patch is authorized by these results.

Log document/selection still stop at the existing call/setup bounds. P3.2b explicitly described incomplete logical remote/indexing work; the M0 rows also printed incomplete=1. The smaller current duration supports a load explanation for the *timing* miss, while preserving the missing completion evidence as a separate limitation. Fixing that coverage or navigation policy is outside this bead absent a confirmed regression.

### Pasted M0 red rows, existing regression benches (before this work)

All rows below are verbatim from `docs/bench/m0.md`, **(M)[AC]**. Their gates are the existing **(G)** regression guards. These benches predate this bead; no new failing wall-time test was added for a load artefact.

```text
trace_record   n=10^6 batches=1000 x 1000: p50=51 ns p99=65 ns (gate p50 < 50)
BENCH indent_long_line_enter includes_zero_capacity TRACK n=20000 p50=10.210 us p99=27.151 us (M)[AC] load1=11.33 gate_p99<=20.000 us(G) pass=0
BENCH indent_long_line_brace TRACK n=20000 p50=18.685 us p99=30.041 us (M)[AC] load1=11.33 gate_p99<=20.000 us(G) pass=0
BENCH name=utf8_decode_width_500_runs n=20000 p50=5280 p99=9994 ci95=[5248,5312] gate_p50=5000 gate_p99=10000 pass=0 power=[AC]
BENCH view_log_1g.txt_document_bulk1_command mode=TRACK n=31 p50=1839 p99=8558034 ns (M)[AC] load1=9.87 necessary_gate=5000000/5555555 ns(G) verdict=TRACK incomplete=1 dropped=0
BENCH view_log_1g.txt_selection_bulk1_command mode=TRACK n=31 p50=3069 p99=8522716 ns (M)[AC] load1=9.87 necessary_gate=5000000/5555555 ns(G) verdict=TRACK incomplete=1 dropped=0
BENCH view_oneline_1g.txt_selection_1MiB_replace_bulk1_command mode=TRACK n=31 p50=1532175 p99=1627352 ns (M)[AC] load1=9.48 necessary_gate=1000000/2000000 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_oneline_1g.txt_selection_1MiB_replace_bulk3_command mode=TRACK n=31 p50=1502584 p99=1567799 ns (M)[AC] load1=9.48 necessary_gate=1000000/2000000 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_unicode.txt_selection_1MiB_replace_bulk1_command mode=TRACK n=31 p50=1188934 p99=1341686 ns (M)[AC] load1=9.48 necessary_gate=1000000/2000000 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_unicode.txt_selection_1MiB_replace_bulk3_command mode=TRACK n=31 p50=1198436 p99=1245328 ns (M)[AC] load1=9.48 necessary_gate=1000000/2000000 ns(G) verdict=TRACK incomplete=0 dropped=0
```

## Verification

All commands use `DISPLAY=:99 EDIT_DISPLAY=:99`. Release compiler is GCC 13.3.0; sanitizer compiler is Clang 18.1.3. Make's C11 flags include `-Wall -Wextra -Werror -Wshadow -Wconversion`.

- `make all`: exit 0 (M)[AC]. Final source rebuild and sanitizer tabs rebuild passed.
- `build/tests/tabs_test`: exit 0 (M)[AC], includes active zero-malloc switch/render/MRU checks.
- `ASAN_OPTIONS=detect_leaks=0 build/san/tests/tabs_test`: exit 0 (M)[AC]. ASan's malloc guard is explicitly inert; the release guard enforces the allocation rule.
- `ASAN_OPTIONS=detect_leaks=0 make -j4 check`: exit 0 (M)[AC], all 61 sanitizer test binaries, replay CLI and display-lock contract passed. The initial sandbox-only attempt stopped in `cli_test` because it could not connect to Xvfb :99. No test/source workaround was made; the retry uses the existing display socket and shared display lock.
- Tabs fuzz: exit 0 (M)[AC], requested 60 s (G), completion pasted below.
- Undo fuzz: exit 0 (M)[AC], requested 60 s (G), completion pasted below; supplemental because the census identifies undo as the owner.
- Indent, UTF-8, trace and view fuzz: each exits 0 (M)[AC], requested 60 s (G) each; full completion/stamps below cover all named modules.
- `git diff --check`: exit 0 (M), no whitespace errors.

**LeakSanitizer is disabled in this sandbox (`ASAN_OPTIONS=detect_leaks=0`); coordinator must rerun with leaks enabled.** No new production globals or typing-path allocation were introduced.

Tabs fuzz command: `ASAN_OPTIONS=detect_leaks=0 build/fuzz/tabs_fuzz -max_total_time=60 -max_len=4096 -timeout=10 -print_final_stats=1 -artifact_prefix=build/edit-fff/`.
Undo fuzz command: `ASAN_OPTIONS=detect_leaks=0 build/fuzz/undo_fuzz -max_total_time=60 -max_len=2048 -timeout=10 -print_final_stats=1 -artifact_prefix=build/edit-fff/`.
Both are **(M)[AC]**, with no sanitizer/model failures; elapsed completion can overrun the requested campaign limit while finishing an input.

```text
tabs
Done 195081 runs in 61 second(s)
stat::number_of_executed_units: 195081
stat::average_exec_per_sec:     3198
stat::new_units_added:          2228
stat::slowest_unit_time_sec:    0
stat::peak_rss_mb:              274
undo
Done 121412 runs in 61 second(s)
stat::number_of_executed_units: 121412
stat::average_exec_per_sec:     1990
stat::new_units_added:          1208
stat::slowest_unit_time_sec:    0
stat::peak_rss_mb:              99
```

## Final full sanitizer suite result

Command: `DISPLAY=:99 EDIT_DISPLAY=:99 ASAN_OPTIONS=detect_leaks=0 make -j4 check`, exit 0 (M)[AC], using access to the existing Xvfb :99 socket. Expected negative-control failures/timeouts inside the display-lock contract test are retained in its log; the enclosing contract and top-level make succeeded.

Initial sandbox-only environment failure (not a module regression), exit 2 (M)[AC]:

```text
cli_test:61: FAIL c && !xcb_connection_has_error(c)
cli_test:137: FAIL ran == 0
make: *** [Makefile:105: check] Error 1
```

Final green transcript, selected complete verdict lines:

```text
tabs_test: census (M)[AC] power=Not charging load1=1.04 tab_control=128 tab_arena=561152 undo_control=42400 history_reserve=819200 replay_reserve=409600 undo_committed=0 combined_reserve=1832480 combined_committed=603680 gate_bytes=1000000(G) reserve=MISS
tabs_test: sequences vs independent model passed
tabs_test: UTF-8 strip, clipping, damage and hit testing passed
tabs_test: errors, bounded eviction and full-capacity reopen passed
tabs_test: closed buffer, undo history and view retention passed
tabs_test: 10000 switches + strip + MRU mallocs=0 guard=ASan-inert
tabs_test: all passed
check: 61 test binaries passed
test_replay_cli: all passed
display lock contract: owner PID, alias/timeout, make/scripts, nested inheritance, exit status PASS
TOP_LEVEL_EXIT 0
AFTER utc=2026-10-10T16:26:01.681363+00:00 power=Not charging loadavg=2.43 1.67 1.55 2/1345 3 (M)[AC]
```

## Named-module fuzz completion

All four additional campaigns used `DISPLAY=:99 EDIT_DISPLAY=:99 ASAN_OPTIONS=detect_leaks=0`; each process exited zero and reported no sanitizer/oracle failure. All counts and elapsed durations are **(M)[AC]**; requested duration is **60 s (G)** per target. These are validation runs, not performance gate measurements. The output below retains command, before/after load/power stamps and completion statistics.

### indent

```text
COMMAND build/fuzz/indent_fuzz -max_total_time=60 -max_len=8192 -timeout=10 -print_final_stats=1 -artifact_prefix=build/edit-fff/
BEFORE utc=2026-10-10T16:23:13.569151+00:00 power=Not charging loadavg=0.33 1.01 1.35 1/1342 3 (M)[AC]
Done 324583 runs in 61 second(s)
stat::number_of_executed_units: 324583
stat::average_exec_per_sec:     5321
stat::new_units_added:          747
stat::slowest_unit_time_sec:    0
stat::peak_rss_mb:              207
AFTER utc=2026-10-10T16:24:14.637806+00:00 power=Not charging loadavg=1.20 1.11 1.37 4/1334 6 (M)[AC]
EXIT 0
```

### utf8

```text
COMMAND build/fuzz/utf8_fuzz -max_total_time=60 -max_len=8192 -timeout=10 -print_final_stats=1 -artifact_prefix=build/edit-fff/
BEFORE utc=2026-10-10T16:24:14.649677+00:00 power=Not charging loadavg=1.20 1.11 1.37 4/1334 6 (M)[AC]
Done 577643 runs in 61 second(s)
stat::number_of_executed_units: 577643
stat::average_exec_per_sec:     9469
stat::new_units_added:          5209
stat::slowest_unit_time_sec:    0
stat::peak_rss_mb:              833
AFTER utc=2026-10-10T16:25:15.721018+00:00 power=Not charging loadavg=2.56 1.54 1.50 3/1324 9 (M)[AC]
EXIT 0
```

### trace

```text
COMMAND build/fuzz/trace_fuzz -max_total_time=60 -max_len=4096 -timeout=10 -print_final_stats=1 -artifact_prefix=build/edit-fff/
BEFORE utc=2026-10-10T16:25:15.733896+00:00 power=Not charging loadavg=2.56 1.54 1.50 3/1324 9 (M)[AC]
Done 9662400 runs in 61 second(s)
stat::number_of_executed_units: 9662400
stat::average_exec_per_sec:     158400
stat::new_units_added:          188
stat::slowest_unit_time_sec:    0
stat::peak_rss_mb:              397
AFTER utc=2026-10-10T16:26:16.801093+00:00 power=Not charging loadavg=2.25 1.67 1.55 1/1334 12 (M)[AC]
EXIT 0
```

### view

```text
COMMAND build/fuzz/view_fuzz -max_total_time=60 -max_len=4096 -timeout=10 -print_final_stats=1 -artifact_prefix=build/edit-fff/
BEFORE utc=2026-10-10T16:26:16.812255+00:00 power=Not charging loadavg=2.25 1.67 1.55 2/1334 12 (M)[AC]
Done 7621 runs in 61 second(s)
stat::number_of_executed_units: 7621
stat::average_exec_per_sec:     124
stat::new_units_added:          431
stat::slowest_unit_time_sec:    0
stat::peak_rss_mb:              65
AFTER utc=2026-10-10T16:27:18.026885+00:00 power=Not charging loadavg=1.84 1.64 1.55 1/1337 15 (M)[AC]
EXIT 0
```

## Missing / coordinator actions

- The combined reserve gate remains a real census MISS; preserve the failing `tabs_test memory-gate` and `tabs_bench` checks. Coordinator owns any budget/gate amendment or a separately scoped undo-reservation redesign. This worker does not waive it.
- Log document/selection complete-command gate evidence is missing; all reruns report incomplete work. No inferred full-command PASS.
- Leaks-on verification remains mandatory outside the sandbox.
- Whole-editor/display gates are outside these module guard reruns; nothing was opened on :0.

## Complete rerun transcripts

All numeric observations in these blocks inherit their before/after **(M)[AC]** stamps; gate values retain **(G)** meaning even where legacy bench formatting omits the suffix. Each run was retained in full, including unrelated rows and incomplete/setup/allocation diagnostics. All twelve bench processes exit zero; view is TRACK and can return zero with incomplete timing rows.

### indent, adjacent run 1

```text
COMMAND build/bench/indent_bench args_file=absent; defaults
BEFORE utc=2026-10-10T16:10:45.358554+00:00 power=Not charging loadavg=2.07 1.30 1.27 1/1335 3 (M)[AC]
POWER status=Not charging [AC] load1=2.07; indent measurements TRACK only
BENCH indent_on_enter viewport_rows=300 TRACK n=20000 p50=0.547 us p99=0.636 us (M)[AC] load1=2.07 gate_p99<=20.000 us(G) pass=1
POWER status=Not charging [AC] load1=2.07; indent measurements TRACK only
BENCH indent_bracket_match viewport_rows=300 TRACK n=20000 p50=0.102 us p99=1.102 us (M)[AC] load1=2.07 gate_p99<=20.000 us(G) pass=1
typing allocations=0 (M)[AC] load1=2.07 gate_allocations=0(G) guard=active
POWER status=Not charging [AC] load1=2.07; indent measurements TRACK only
BENCH indent_long_line_enter includes_zero_capacity TRACK n=20000 p50=4.784 us p99=5.101 us (M)[AC] load1=2.07 gate_p99<=20.000 us(G) pass=1
POWER status=Not charging [AC] load1=2.07; indent measurements TRACK only
BENCH indent_long_line_brace TRACK n=20000 p50=4.782 us p99=5.095 us (M)[AC] load1=2.07 gate_p99<=20.000 us(G) pass=1
POWER status=Not charging [AC] load1=2.07; indent measurements TRACK only
BENCH indent_detect file=log_1g.txt prefix<=64KiB TRACK n=2000 p50=42.602 us p99=46.067 us (M)[AC] load1=2.07 gate_p99<=1000.000 us(G) pass=1
AFTER utc=2026-10-10T16:10:45.634096+00:00 power=Not charging loadavg=2.07 1.30 1.27 1/1335 4 (M)[AC]
EXIT 0 elapsed_s=0.265017835998151 (M)[AC]
```

### indent, adjacent run 2

```text
COMMAND build/bench/indent_bench args_file=absent; defaults
BEFORE utc=2026-10-10T16:10:45.634319+00:00 power=Not charging loadavg=2.07 1.30 1.27 2/1335 4 (M)[AC]
POWER status=Not charging [AC] load1=2.07; indent measurements TRACK only
BENCH indent_on_enter viewport_rows=300 TRACK n=20000 p50=0.546 us p99=0.619 us (M)[AC] load1=2.07 gate_p99<=20.000 us(G) pass=1
POWER status=Not charging [AC] load1=2.07; indent measurements TRACK only
BENCH indent_bracket_match viewport_rows=300 TRACK n=20000 p50=0.180 us p99=1.657 us (M)[AC] load1=2.07 gate_p99<=20.000 us(G) pass=1
typing allocations=0 (M)[AC] load1=2.07 gate_allocations=0(G) guard=active
POWER status=Not charging [AC] load1=2.07; indent measurements TRACK only
BENCH indent_long_line_enter includes_zero_capacity TRACK n=20000 p50=4.785 us p99=5.100 us (M)[AC] load1=2.07 gate_p99<=20.000 us(G) pass=1
POWER status=Not charging [AC] load1=2.07; indent measurements TRACK only
BENCH indent_long_line_brace TRACK n=20000 p50=4.782 us p99=5.095 us (M)[AC] load1=2.07 gate_p99<=20.000 us(G) pass=1
POWER status=Not charging [AC] load1=2.07; indent measurements TRACK only
BENCH indent_detect file=log_1g.txt prefix<=64KiB TRACK n=2000 p50=42.319 us p99=46.052 us (M)[AC] load1=2.07 gate_p99<=1000.000 us(G) pass=1
AFTER utc=2026-10-10T16:10:45.898985+00:00 power=Not charging loadavg=2.07 1.30 1.27 1/1335 5 (M)[AC]
EXIT 0 elapsed_s=0.2647649100035778 (M)[AC]
```

### indent, adjacent run 3

```text
COMMAND build/bench/indent_bench args_file=absent; defaults
BEFORE utc=2026-10-10T16:10:45.899227+00:00 power=Not charging loadavg=2.07 1.30 1.27 2/1335 5 (M)[AC]
POWER status=Not charging [AC] load1=2.07; indent measurements TRACK only
BENCH indent_on_enter viewport_rows=300 TRACK n=20000 p50=0.313 us p99=0.614 us (M)[AC] load1=2.07 gate_p99<=20.000 us(G) pass=1
POWER status=Not charging [AC] load1=2.07; indent measurements TRACK only
BENCH indent_bracket_match viewport_rows=300 TRACK n=20000 p50=0.102 us p99=1.102 us (M)[AC] load1=2.07 gate_p99<=20.000 us(G) pass=1
typing allocations=0 (M)[AC] load1=2.07 gate_allocations=0(G) guard=active
POWER status=Not charging [AC] load1=2.07; indent measurements TRACK only
BENCH indent_long_line_enter includes_zero_capacity TRACK n=20000 p50=4.785 us p99=5.370 us (M)[AC] load1=2.07 gate_p99<=20.000 us(G) pass=1
POWER status=Not charging [AC] load1=2.07; indent measurements TRACK only
BENCH indent_long_line_brace TRACK n=20000 p50=4.667 us p99=5.095 us (M)[AC] load1=2.07 gate_p99<=20.000 us(G) pass=1
POWER status=Not charging [AC] load1=2.07; indent measurements TRACK only
BENCH indent_detect file=log_1g.txt prefix<=64KiB TRACK n=2000 p50=42.302 us p99=44.807 us (M)[AC] load1=2.07 gate_p99<=1000.000 us(G) pass=1
AFTER utc=2026-10-10T16:10:46.164137+00:00 power=Not charging loadavg=2.07 1.30 1.27 1/1335 6 (M)[AC]
EXIT 0 elapsed_s=0.26496771299571265 (M)[AC]
```

### utf8, adjacent run 1

```text
COMMAND build/bench/utf8_bench args_file=absent; defaults
BEFORE utc=2026-10-10T16:10:46.164341+00:00 power=Not charging loadavg=2.07 1.30 1.27 2/1335 6 (M)[AC]
power: battery=Not charging governor=powersave tag=(M)[AC] load1=2.07
mix runs: 4096 windows x 500 scalars, 1.80 B/scalar
BENCH name=utf8_decode_width_500_runs n=20000 p50=1705 p99=3232 ci95=[1695,1713] gate_p50=5000 gate_p99=10000 pass=1 power=[AC]
  -> p50 3.4 ns/scalar, p99 3232 ns vs 5000/10000 (gated)
  p50 1705 ns ci95 [1695,1713]; p99 3232 ns ci95 [3194,3284]
BENCH name=utf8_grapheme_500_runs n=20000 p50=11122 p99=16047 ci95=[11090,11153] gate_p50=0 gate_p99=0 pass=1 power=[AC]
  -> p50 22.2 ns/scalar (TRACK)
BENCH name=utf8_cluster_500_runs n=20000 p50=16478 p99=31687 ci95=[16376,16566] gate_p50=0 gate_p99=0 pass=1 power=[AC]
  -> p50 33.0 ns/scalar (TRACK)
mix iid: 4096 windows x 500 scalars, 1.80 B/scalar
BENCH name=utf8_decode_width_500_iid n=20000 p50=4538 p99=5644 ci95=[4534,4542] gate_p50=0 gate_p99=0 pass=1 power=[AC]
  -> p50 9.1 ns/scalar, p99 5644 ns vs 5000/10000 (not gated)
  p50 4538 ns ci95 [4534,4542]; p99 5644 ns ci95 [5549,5743]
BENCH name=utf8_grapheme_500_iid n=20000 p50=13791 p99=17703 ci95=[13783,13799] gate_p50=0 gate_p99=0 pass=1 power=[AC]
  -> p50 27.6 ns/scalar (TRACK)
BENCH name=utf8_cluster_500_iid n=20000 p50=23483 p99=28663 ci95=[23465,23503] gate_p50=0 gate_p99=0 pass=1 power=[AC]
  -> p50 47.0 ns/scalar (TRACK)
BENCH name=utf8_ascii_skip_96k n=5000 p50=2115 p99=2442 ci95=[2115,2116] gate_p50=12288 gate_p99=0 pass=1 power=[AC]
  -> throughput at p50 latency 46.48 GB/s (gate >= 8 GB/s); throughput at p99 latency (slow tail) 40.26 GB/s
  p50 2115 ns ci95 [2115,2116]; p99 2442 ns ci95 [2420,2521]
BENCH name=utf8_step_combining_49k n=20000 p50=37635 p99=46626 ci95=[27720,42840] gate_p50=0 gate_p99=500000 pass=1 power=[AC]
  -> 49153 B cluster, budget 4096 B, used 4097 B/call; p99 46626 ns vs 500000 (gated)
  p50 37635 ns ci95 [27720,42840]; p99 46626 ns ci95 [46479,46772]
utf8_step_combining_49k_total: p50 275664 ns for 49153 B = 5.61 ns/byte (TRACK)
utf8_step_combining_49k_unbounded: p50 503059 ns p99 515580 ns for one utf8_grapheme_next call (TRACK; the pre-P1.1d call, cf. 500000 slice)
BENCH name=utf8_cstep_combining_49k n=20000 p50=42944 p99=47293 ci95=[42942,42945] gate_p50=0 gate_p99=500000 pass=1 power=[AC]
  -> 49153 B cluster, budget 4096 B, used 4097 B/call; p99 47293 ns vs 500000 (gated)
  p50 42944 ns ci95 [42942,42945]; p99 47293 ns ci95 [47196,47426]
utf8_cstep_combining_49k_unbounded: p50 916843 ns p99 933422 ns for one utf8_cluster_width call (TRACK; the pre-P1.1e call)
BENCH name=utf8_step_prepend_40k n=20000 p50=52124 p99=56732 ci95=[52118,52130] gate_p50=0 gate_p99=500000 pass=1 power=[AC]
  -> 40001 B cluster, budget 4096 B, used 4096 B/call; p99 56732 ns vs 500000 (gated)
  p50 52124 ns ci95 [52118,52130]; p99 56732 ns ci95 [56572,56910]
utf8_step_prepend_40k_total: p50 512397 ns for 40001 B = 12.81 ns/byte (TRACK)
utf8_step_prepend_40k_unbounded: p50 505137 ns p99 534928 ns for one utf8_grapheme_next call (TRACK; the pre-P1.1d call, cf. 500000 slice)
BENCH name=utf8_cstep_prepend_40k n=20000 p50=59581 p99=112540 ci95=[59550,59614] gate_p50=0 gate_p99=500000 pass=1 power=[AC]
  -> 40001 B cluster, budget 4096 B, used 4096 B/call; p99 112540 ns vs 500000 (gated)
  p50 59581 ns ci95 [59550,59614]; p99 112540 ns ci95 [112063,113057]
utf8_cstep_prepend_40k_unbounded: p50 596443 ns p99 1039985 ns for one utf8_cluster_width call (TRACK; the pre-P1.1e call)
BENCH name=utf8_step_zwj_35k n=20000 p50=17640 p99=37333 ci95=[17545,17716] gate_p50=0 gate_p99=500000 pass=1 power=[AC]
  -> 35004 B cluster, budget 4096 B, used 4099 B/call; p99 37333 ns vs 500000 (gated)
  p50 17640 ns ci95 [17545,17716]; p99 37333 ns ci95 [37228,37461]
utf8_step_zwj_35k_total: p50 140383 ns for 35004 B = 4.01 ns/byte (TRACK)
utf8_step_zwj_35k_unbounded: p50 141131 ns p99 157061 ns for one utf8_grapheme_next call (TRACK; the pre-P1.1d call, cf. 500000 slice)
BENCH name=utf8_cstep_zwj_35k n=20000 p50=16593 p99=18522 ci95=[16584,16602] gate_p50=0 gate_p99=500000 pass=1 power=[AC]
  -> 35004 B cluster, budget 4096 B, used 4099 B/call; p99 18522 ns vs 500000 (gated)
  p50 16593 ns ci95 [16584,16602]; p99 18522 ns ci95 [18502,18537]
utf8_cstep_zwj_35k_unbounded: p50 269483 ns p99 286483 ns for one utf8_cluster_width call (TRACK; the pre-P1.1e call)
BENCH GATE PASSED
AFTER utc=2026-10-10T16:11:01.060536+00:00 power=Not charging loadavg=3.02 1.55 1.35 7/1348 7 (M)[AC]
EXIT 0 elapsed_s=14.908059355999285 (M)[AC]
```

### utf8, adjacent run 2

```text
COMMAND build/bench/utf8_bench args_file=absent; defaults
BEFORE utc=2026-10-10T16:11:01.072659+00:00 power=Not charging loadavg=3.02 1.55 1.35 7/1348 7 (M)[AC]
power: battery=Not charging governor=powersave tag=(M)[AC] load1=3.02
mix runs: 4096 windows x 500 scalars, 1.80 B/scalar
BENCH name=utf8_decode_width_500_runs n=20000 p50=3047 p99=5528 ci95=[3031,3066] gate_p50=5000 gate_p99=10000 pass=1 power=[AC]
  -> p50 6.1 ns/scalar, p99 5528 ns vs 5000/10000 (gated)
  p50 3047 ns ci95 [3031,3066]; p99 5528 ns ci95 [5468,5627]
BENCH name=utf8_grapheme_500_runs n=20000 p50=19634 p99=29049 ci95=[19558,19718] gate_p50=0 gate_p99=0 pass=1 power=[AC]
  -> p50 39.3 ns/scalar (TRACK)
BENCH name=utf8_cluster_500_runs n=20000 p50=31821 p99=57597 ci95=[31652,31985] gate_p50=0 gate_p99=0 pass=1 power=[AC]
  -> p50 63.6 ns/scalar (TRACK)
mix iid: 4096 windows x 500 scalars, 1.80 B/scalar
BENCH name=utf8_decode_width_500_iid n=20000 p50=8600 p99=9899 ci95=[8591,8608] gate_p50=0 gate_p99=0 pass=1 power=[AC]
  -> p50 17.2 ns/scalar, p99 9899 ns vs 5000/10000 (not gated)
  p50 8600 ns ci95 [8591,8608]; p99 9899 ns ci95 [9827,12627]
BENCH name=utf8_grapheme_500_iid n=20000 p50=34804 p99=42720 ci95=[34772,34834] gate_p50=0 gate_p99=0 pass=1 power=[AC]
  -> p50 69.6 ns/scalar (TRACK)
BENCH name=utf8_cluster_500_iid n=20000 p50=62156 p99=71958 ci95=[62099,62214] gate_p50=0 gate_p99=0 pass=1 power=[AC]
  -> p50 124.3 ns/scalar (TRACK)
BENCH name=utf8_ascii_skip_96k n=5000 p50=4945 p99=5286 ci95=[4945,4947] gate_p50=12288 gate_p99=0 pass=1 power=[AC]
  -> throughput at p50 latency 19.88 GB/s (gate >= 8 GB/s); throughput at p99 latency (slow tail) 18.60 GB/s
  p50 4945 ns ci95 [4945,4947]; p99 5286 ns ci95 [5225,5355]
BENCH name=utf8_step_combining_49k n=20000 p50=61998 p99=71125 ci95=[61949,62040] gate_p50=0 gate_p99=500000 pass=1 power=[AC]
  -> 49153 B cluster, budget 4096 B, used 4097 B/call; p99 71125 ns vs 500000 (gated)
  p50 61998 ns ci95 [61949,62040]; p99 71125 ns ci95 [70889,71442]
utf8_step_combining_49k_total: p50 756854 ns for 49153 B = 15.40 ns/byte (TRACK)
utf8_step_combining_49k_unbounded: p50 682994 ns p99 825222 ns for one utf8_grapheme_next call (TRACK; the pre-P1.1d call, cf. 500000 slice)
BENCH name=utf8_cstep_combining_49k n=20000 p50=55811 p99=60843 ci95=[55810,55812] gate_p50=0 gate_p99=500000 pass=1 power=[AC]
  -> 49153 B cluster, budget 4096 B, used 4097 B/call; p99 60843 ns vs 500000 (gated)
  p50 55811 ns ci95 [55810,55812]; p99 60843 ns ci95 [60679,61115]
utf8_cstep_combining_49k_unbounded: p50 1134612 ns p99 1205821 ns for one utf8_cluster_width call (TRACK; the pre-P1.1e call)
BENCH name=utf8_step_prepend_40k n=20000 p50=64300 p99=68831 ci95=[64299,64302] gate_p50=0 gate_p99=500000 pass=1 power=[AC]
  -> 40001 B cluster, budget 4096 B, used 4096 B/call; p99 68831 ns vs 500000 (gated)
  p50 64300 ns ci95 [64299,64302]; p99 68831 ns ci95 [68746,68959]
utf8_step_prepend_40k_total: p50 632004 ns for 40001 B = 15.80 ns/byte (TRACK)
utf8_step_prepend_40k_unbounded: p50 720656 ns p99 730098 ns for one utf8_grapheme_next call (TRACK; the pre-P1.1d call, cf. 500000 slice)
BENCH name=utf8_cstep_prepend_40k n=20000 p50=157405 p99=169075 ci95=[157303,157502] gate_p50=0 gate_p99=500000 pass=1 power=[AC]
  -> 40001 B cluster, budget 4096 B, used 4096 B/call; p99 169075 ns vs 500000 (gated)
  p50 157405 ns ci95 [157303,157502]; p99 169075 ns ci95 [168827,169412]
utf8_cstep_prepend_40k_unbounded: p50 1274089 ns p99 1591746 ns for one utf8_cluster_width call (TRACK; the pre-P1.1e call)
BENCH name=utf8_step_zwj_35k n=20000 p50=39683 p99=47873 ci95=[39648,39719] gate_p50=0 gate_p99=500000 pass=1 power=[AC]
  -> 35004 B cluster, budget 4096 B, used 4099 B/call; p99 47873 ns vs 500000 (gated)
  p50 39683 ns ci95 [39648,39719]; p99 47873 ns ci95 [47589,48205]
utf8_step_zwj_35k_total: p50 192583 ns for 35004 B = 5.50 ns/byte (TRACK)
utf8_step_zwj_35k_unbounded: p50 180649 ns p99 209356 ns for one utf8_grapheme_next call (TRACK; the pre-P1.1d call, cf. 500000 slice)
BENCH name=utf8_cstep_zwj_35k n=20000 p50=18296 p99=23757 ci95=[18290,18302] gate_p50=0 gate_p99=500000 pass=1 power=[AC]
  -> 35004 B cluster, budget 4096 B, used 4099 B/call; p99 23757 ns vs 500000 (gated)
  p50 18296 ns ci95 [18290,18302]; p99 23757 ns ci95 [23329,24192]
utf8_cstep_zwj_35k_unbounded: p50 265683 ns p99 291518 ns for one utf8_cluster_width call (TRACK; the pre-P1.1e call)
BENCH GATE PASSED
AFTER utc=2026-10-10T16:11:24.692085+00:00 power=Not charging loadavg=4.20 1.92 1.48 2/1368 8 (M)[AC]
EXIT 0 elapsed_s=23.61946139900101 (M)[AC]
```

### utf8, adjacent run 3

```text
COMMAND build/bench/utf8_bench args_file=absent; defaults
BEFORE utc=2026-10-10T16:11:24.692466+00:00 power=Not charging loadavg=4.20 1.92 1.48 2/1368 8 (M)[AC]
power: battery=Not charging governor=powersave tag=(M)[AC] load1=4.20
mix runs: 4096 windows x 500 scalars, 1.80 B/scalar
BENCH name=utf8_decode_width_500_runs n=20000 p50=1714 p99=3277 ci95=[1706,1723] gate_p50=5000 gate_p99=10000 pass=1 power=[AC]
  -> p50 3.4 ns/scalar, p99 3277 ns vs 5000/10000 (gated)
  p50 1714 ns ci95 [1706,1723]; p99 3277 ns ci95 [3245,3320]
BENCH name=utf8_grapheme_500_runs n=20000 p50=12065 p99=23674 ci95=[12025,12102] gate_p50=0 gate_p99=0 pass=1 power=[AC]
  -> p50 24.1 ns/scalar (TRACK)
BENCH name=utf8_cluster_500_runs n=20000 p50=18520 p99=32388 ci95=[18412,18628] gate_p50=0 gate_p99=0 pass=1 power=[AC]
  -> p50 37.0 ns/scalar (TRACK)
mix iid: 4096 windows x 500 scalars, 1.80 B/scalar
BENCH name=utf8_decode_width_500_iid n=20000 p50=5325 p99=6561 ci95=[5321,5329] gate_p50=0 gate_p99=0 pass=1 power=[AC]
  -> p50 10.7 ns/scalar, p99 6561 ns vs 5000/10000 (not gated)
  p50 5325 ns ci95 [5321,5329]; p99 6561 ns ci95 [6421,6731]
BENCH name=utf8_grapheme_500_iid n=20000 p50=15879 p99=20300 ci95=[15867,15890] gate_p50=0 gate_p99=0 pass=1 power=[AC]
  -> p50 31.8 ns/scalar (TRACK)
BENCH name=utf8_cluster_500_iid n=20000 p50=25505 p99=30106 ci95=[25481,25526] gate_p50=0 gate_p99=0 pass=1 power=[AC]
  -> p50 51.0 ns/scalar (TRACK)
BENCH name=utf8_ascii_skip_96k n=5000 p50=3283 p99=3520 ci95=[3283,3284] gate_p50=12288 gate_p99=0 pass=1 power=[AC]
  -> throughput at p50 latency 29.94 GB/s (gate >= 8 GB/s); throughput at p99 latency (slow tail) 27.93 GB/s
  p50 3283 ns ci95 [3283,3284]; p99 3520 ns ci95 [3488,3604]
BENCH name=utf8_step_combining_49k n=20000 p50=25457 p99=29910 ci95=[25456,25459] gate_p50=0 gate_p99=500000 pass=1 power=[AC]
  -> 49153 B cluster, budget 4096 B, used 4097 B/call; p99 29910 ns vs 500000 (gated)
  p50 25457 ns ci95 [25456,25459]; p99 29910 ns ci95 [29768,30008]
utf8_step_combining_49k_total: p50 299392 ns for 49153 B = 6.09 ns/byte (TRACK)
utf8_step_combining_49k_unbounded: p50 270294 ns p99 296519 ns for one utf8_grapheme_next call (TRACK; the pre-P1.1d call, cf. 500000 slice)
BENCH name=utf8_cstep_combining_49k n=20000 p50=22792 p99=25189 ci95=[22791,22794] gate_p50=0 gate_p99=500000 pass=1 power=[AC]
  -> 49153 B cluster, budget 4096 B, used 4097 B/call; p99 25189 ns vs 500000 (gated)
  p50 22792 ns ci95 [22791,22794]; p99 25189 ns ci95 [24552,25428]
utf8_cstep_combining_49k_unbounded: p50 519419 ns p99 545226 ns for one utf8_cluster_width call (TRACK; the pre-P1.1e call)
BENCH name=utf8_step_prepend_40k n=20000 p50=28969 p99=30539 ci95=[28967,28970] gate_p50=0 gate_p99=500000 pass=1 power=[AC]
  -> 40001 B cluster, budget 4096 B, used 4096 B/call; p99 30539 ns vs 500000 (gated)
  p50 28969 ns ci95 [28967,28970]; p99 30539 ns ci95 [30533,30548]
utf8_step_prepend_40k_total: p50 288491 ns for 40001 B = 7.21 ns/byte (TRACK)
utf8_step_prepend_40k_unbounded: p50 277892 ns p99 295559 ns for one utf8_grapheme_next call (TRACK; the pre-P1.1d call, cf. 500000 slice)
BENCH name=utf8_cstep_prepend_40k n=20000 p50=59186 p99=63111 ci95=[59183,59188] gate_p50=0 gate_p99=500000 pass=1 power=[AC]
  -> 40001 B cluster, budget 4096 B, used 4096 B/call; p99 63111 ns vs 500000 (gated)
  p50 59186 ns ci95 [59183,59188]; p99 63111 ns ci95 [62658,63586]
utf8_cstep_prepend_40k_unbounded: p50 565216 ns p99 580167 ns for one utf8_cluster_width call (TRACK; the pre-P1.1e call)
BENCH name=utf8_step_zwj_35k n=20000 p50=17311 p99=19107 ci95=[17307,17318] gate_p50=0 gate_p99=500000 pass=1 power=[AC]
  -> 35004 B cluster, budget 4096 B, used 4099 B/call; p99 19107 ns vs 500000 (gated)
  p50 17311 ns ci95 [17307,17318]; p99 19107 ns ci95 [19093,19116]
utf8_step_zwj_35k_total: p50 140963 ns for 35004 B = 4.03 ns/byte (TRACK)
utf8_step_zwj_35k_unbounded: p50 141135 ns p99 159609 ns for one utf8_grapheme_next call (TRACK; the pre-P1.1d call, cf. 500000 slice)
BENCH name=utf8_cstep_zwj_35k n=20000 p50=17575 p99=18846 ci95=[17530,17577] gate_p50=0 gate_p99=500000 pass=1 power=[AC]
  -> 35004 B cluster, budget 4096 B, used 4099 B/call; p99 18846 ns vs 500000 (gated)
  p50 17575 ns ci95 [17530,17577]; p99 18846 ns ci95 [18844,18901]
utf8_cstep_zwj_35k_unbounded: p50 273180 ns p99 287519 ns for one utf8_cluster_width call (TRACK; the pre-P1.1e call)
BENCH GATE PASSED
AFTER utc=2026-10-10T16:11:35.587428+00:00 power=Not charging loadavg=3.63 1.91 1.48 1/1347 9 (M)[AC]
EXIT 0 elapsed_s=10.9089875819991 (M)[AC]
```

### trace, adjacent run 1

```text
COMMAND build/bench/trace_bench args_file=absent; defaults
BEFORE utc=2026-10-10T16:11:35.601894+00:00 power=Not charging loadavg=3.63 1.91 1.48 3/1347 9 (M)[AC]
POWER status=Not charging (M)[AC] load1=3.63
trace_record   n=10^6 batches=1000 x 1000: p50=19 ns p99=62 ns (gate p50 < 50)
clock_gettime  (reference, same method): p50=14 ns p99=15 ns
trace_input    n=10^6 batches=1000 x 1000: p50=3 ns p99=16 ns (gate p50 < 50) (M)[Not charging]
trace_bench: PASS
AFTER utc=2026-10-10T16:11:35.666734+00:00 power=Not charging loadavg=3.63 1.91 1.48 1/1347 10 (M)[AC]
EXIT 0 elapsed_s=0.06480708700109972 (M)[AC]
```

### trace, adjacent run 2

```text
COMMAND build/bench/trace_bench args_file=absent; defaults
BEFORE utc=2026-10-10T16:11:35.667097+00:00 power=Not charging loadavg=3.63 1.91 1.48 2/1347 10 (M)[AC]
POWER status=Not charging (M)[AC] load1=3.63
trace_record   n=10^6 batches=1000 x 1000: p50=15 ns p99=51 ns (gate p50 < 50)
clock_gettime  (reference, same method): p50=14 ns p99=15 ns
trace_input    n=10^6 batches=1000 x 1000: p50=5 ns p99=16 ns (gate p50 < 50) (M)[Not charging]
trace_bench: PASS
AFTER utc=2026-10-10T16:11:35.731580+00:00 power=Not charging loadavg=3.63 1.91 1.48 1/1347 11 (M)[AC]
EXIT 0 elapsed_s=0.064660788004403 (M)[AC]
```

### trace, adjacent run 3

```text
COMMAND build/bench/trace_bench args_file=absent; defaults
BEFORE utc=2026-10-10T16:11:35.732066+00:00 power=Not charging loadavg=3.63 1.91 1.48 3/1347 11 (M)[AC]
POWER status=Not charging (M)[AC] load1=3.63
trace_record   n=10^6 batches=1000 x 1000: p50=16 ns p99=50 ns (gate p50 < 50)
clock_gettime  (reference, same method): p50=14 ns p99=15 ns
trace_input    n=10^6 batches=1000 x 1000: p50=5 ns p99=13 ns (gate p50 < 50) (M)[Not charging]
trace_bench: PASS
AFTER utc=2026-10-10T16:11:35.796983+00:00 power=Not charging loadavg=3.63 1.91 1.48 1/1347 12 (M)[AC]
EXIT 0 elapsed_s=0.0650835059932433 (M)[AC]
```

### view, adjacent run 1

```text
COMMAND build/bench/view_bench args_file=absent; defaults
BEFORE utc=2026-10-10T16:11:35.797502+00:00 power=Not charging loadavg=3.63 1.91 1.48 2/1347 12 (M)[AC]
POWER Not charging [AC] load1=3.63 mode=TRACK; module-only necessary limits, 50 us remains (E)
FIXTURE /tmp/edit-corpus/log_1g.txt bytes=1073741824(M)[AC] load1=3.63 bulk=one-active source=mapped initial=certified
BENCH view_log_1g.txt_char_bulk1_command mode=TRACK n=31 p50=990 p99=4947 ns (M)[AC] load1=3.63 necessary_gate=5000000/5555555 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_log_1g.txt_char_bulk1_initial mode=TRACK n=31 p50=910 p99=4821 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
SLICE_MAX char 4821 ns(M)[AC] load1=3.63 hard_limit=500000 ns(G) verdict=TRACK incomplete_commands=0
FIXTURE /tmp/edit-corpus/log_1g.txt bytes=1073741824(M)[AC] load1=3.63 bulk=one-active source=mapped initial=certified
BENCH view_log_1g.txt_word_bulk1_command mode=TRACK n=31 p50=715 p99=1362 ns (M)[AC] load1=3.63 necessary_gate=5000000/5555555 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_log_1g.txt_word_bulk1_initial mode=TRACK n=31 p50=683 p99=1307 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
SLICE_MAX word 1307 ns(M)[AC] load1=3.63 hard_limit=500000 ns(G) verdict=TRACK incomplete_commands=0
FIXTURE /tmp/edit-corpus/log_1g.txt bytes=1073741824(M)[AC] load1=3.63 bulk=one-active source=mapped initial=certified
BENCH view_log_1g.txt_vertical_bulk1_command mode=TRACK n=31 p50=933 p99=1893 ns (M)[AC] load1=3.63 necessary_gate=5000000/5555555 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_log_1g.txt_vertical_bulk1_initial mode=TRACK n=31 p50=902 p99=1857 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
SLICE_MAX vertical 1857 ns(M)[AC] load1=3.63 hard_limit=500000 ns(G) verdict=TRACK incomplete_commands=0
FIXTURE /tmp/edit-corpus/log_1g.txt bytes=1073741824(M)[AC] load1=3.63 bulk=one-active source=mapped initial=certified
BENCH view_log_1g.txt_page_bulk1_command mode=TRACK n=31 p50=610 p99=3226 ns (M)[AC] load1=3.63 necessary_gate=5000000/5555555 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_log_1g.txt_page_bulk1_initial mode=TRACK n=31 p50=582 p99=3183 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
SLICE_MAX page 3183 ns(M)[AC] load1=3.63 hard_limit=500000 ns(G) verdict=TRACK incomplete_commands=0
FIXTURE /tmp/edit-corpus/log_1g.txt bytes=1073741824(M)[AC] load1=3.63 bulk=one-active source=mapped initial=certified
BENCH view_log_1g.txt_home_end_bulk1_command mode=TRACK n=31 p50=636 p99=3890 ns (M)[AC] load1=3.63 necessary_gate=5000000/5555555 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_log_1g.txt_home_end_bulk1_initial mode=TRACK n=31 p50=598 p99=3848 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
SLICE_MAX home_end 3848 ns(M)[AC] load1=3.63 hard_limit=500000 ns(G) verdict=TRACK incomplete_commands=0
FIXTURE /tmp/edit-corpus/log_1g.txt bytes=1073741824(M)[AC] load1=3.63 bulk=one-active source=mapped initial=certified
BENCH view_log_1g.txt_document_bulk1_command mode=TRACK n=31 p50=309 p99=2564957 ns (M)[AC] load1=3.63 necessary_gate=5000000/5555555 ns(G) verdict=TRACK incomplete=1 dropped=0
BENCH view_log_1g.txt_document_bulk1_initial mode=TRACK n=31 p50=281 p99=2667 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_log_1g.txt_document_bulk1_continue mode=TRACK n=15360 p50=2371 p99=2557 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
SLICE_MAX document 125221 ns(M)[AC] load1=3.63 hard_limit=500000 ns(G) verdict=TRACK incomplete_commands=15
FIXTURE /tmp/edit-corpus/log_1g.txt bytes=1073741824(M)[AC] load1=3.63 bulk=one-active source=mapped initial=certified
BENCH view_log_1g.txt_selection_bulk1_command mode=TRACK n=31 p50=1116 p99=2477717 ns (M)[AC] load1=3.63 necessary_gate=5000000/5555555 ns(G) verdict=TRACK incomplete=1 dropped=0
BENCH view_log_1g.txt_selection_bulk1_initial mode=TRACK n=31 p50=1084 p99=2714 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_log_1g.txt_selection_bulk1_continue mode=TRACK n=11264 p50=2366 p99=2512 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
SLICE_MAX selection 12158 ns(M)[AC] load1=3.63 hard_limit=500000 ns(G) verdict=TRACK incomplete_commands=11
FIXTURE /tmp/edit-corpus/log_1g.txt bytes=1073741824(M)[AC] load1=3.63 bulk=one-active source=mapped initial=certified
BENCH view_log_1g.txt_edit_bulk1_command mode=TRACK n=31 p50=2337 p99=6905 ns (M)[AC] load1=3.63 necessary_gate=1000000/2000000 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_log_1g.txt_edit_bulk1_initial mode=TRACK n=31 p50=2296 p99=6854 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
SLICE_MAX edit 6854 ns(M)[AC] load1=3.63 hard_limit=500000 ns(G) verdict=TRACK incomplete_commands=0
FIXTURE /tmp/edit-corpus/log_1g.txt bytes=1073741824(M)[AC] load1=3.63 bulk=one-active source=mapped initial=certified
SETUP_LIMIT deep_type skipped=31(M)[AC] load1=3.63 mode=TRACK verdict=TRACK setup_cap=100000000 ns(G)/1024 calls(G)
FIXTURE /tmp/edit-corpus/log_1g.txt bytes=1073741824(M)[AC] load1=3.63 bulk=one-active source=mapped initial=certified
SETUP_LIMIT selection_1MiB_replace skipped=31(M)[AC] load1=3.63 mode=TRACK verdict=TRACK setup_cap=100000000 ns(G)/1024 calls(G)
FIXTURE /tmp/edit-corpus/log_1g.txt bytes=1073741824(M)[AC] load1=3.63 bulk=one-active source=mapped initial=certified
BENCH view_log_1g.txt_unindexed_type_bulk1_command mode=TRACK n=31 p50=2580 p99=7415 ns (M)[AC] load1=3.63 necessary_gate=1000000/2000000 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_log_1g.txt_unindexed_type_bulk1_initial mode=TRACK n=31 p50=2535 p99=7289 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
SLICE_MAX unindexed_type 7289 ns(M)[AC] load1=3.63 hard_limit=500000 ns(G) verdict=TRACK incomplete_commands=0
FIXTURE /tmp/edit-corpus/log_1g.txt bytes=1073741824(M)[AC] load1=3.63 bulk=three-submitted source=mapped initial=certified
BENCH view_log_1g.txt_char_bulk3_command mode=TRACK n=31 p50=569 p99=1620 ns (M)[AC] load1=3.63 necessary_gate=5000000/5555555 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_log_1g.txt_char_bulk3_initial mode=TRACK n=31 p50=535 p99=1578 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
SLICE_MAX char 1578 ns(M)[AC] load1=3.63 hard_limit=500000 ns(G) verdict=TRACK incomplete_commands=0
FIXTURE /tmp/edit-corpus/log_1g.txt bytes=1073741824(M)[AC] load1=3.63 bulk=three-submitted source=mapped initial=certified
BENCH view_log_1g.txt_word_bulk3_command mode=TRACK n=31 p50=678 p99=1116 ns (M)[AC] load1=3.63 necessary_gate=5000000/5555555 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_log_1g.txt_word_bulk3_initial mode=TRACK n=31 p50=645 p99=1084 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
SLICE_MAX word 1084 ns(M)[AC] load1=3.63 hard_limit=500000 ns(G) verdict=TRACK incomplete_commands=0
FIXTURE /tmp/edit-corpus/log_1g.txt bytes=1073741824(M)[AC] load1=3.63 bulk=three-submitted source=mapped initial=certified
BENCH view_log_1g.txt_vertical_bulk3_command mode=TRACK n=31 p50=809 p99=1855 ns (M)[AC] load1=3.63 necessary_gate=5000000/5555555 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_log_1g.txt_vertical_bulk3_initial mode=TRACK n=31 p50=781 p99=1819 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
SLICE_MAX vertical 1819 ns(M)[AC] load1=3.63 hard_limit=500000 ns(G) verdict=TRACK incomplete_commands=0
FIXTURE /tmp/edit-corpus/log_1g.txt bytes=1073741824(M)[AC] load1=3.63 bulk=three-submitted source=mapped initial=certified
BENCH view_log_1g.txt_page_bulk3_command mode=TRACK n=31 p50=697 p99=3183 ns (M)[AC] load1=3.63 necessary_gate=5000000/5555555 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_log_1g.txt_page_bulk3_initial mode=TRACK n=31 p50=658 p99=3146 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
SLICE_MAX page 3146 ns(M)[AC] load1=3.63 hard_limit=500000 ns(G) verdict=TRACK incomplete_commands=0
FIXTURE /tmp/edit-corpus/log_1g.txt bytes=1073741824(M)[AC] load1=3.63 bulk=three-submitted source=mapped initial=certified
BENCH view_log_1g.txt_home_end_bulk3_command mode=TRACK n=31 p50=866 p99=4128 ns (M)[AC] load1=3.63 necessary_gate=5000000/5555555 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_log_1g.txt_home_end_bulk3_initial mode=TRACK n=31 p50=824 p99=4098 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
SLICE_MAX home_end 4098 ns(M)[AC] load1=3.63 hard_limit=500000 ns(G) verdict=TRACK incomplete_commands=0
FIXTURE /tmp/edit-corpus/log_1g.txt bytes=1073741824(M)[AC] load1=3.63 bulk=three-submitted source=mapped initial=certified
BENCH view_log_1g.txt_document_bulk3_command mode=TRACK n=31 p50=328 p99=2452689 ns (M)[AC] load1=3.63 necessary_gate=5000000/5555555 ns(G) verdict=TRACK incomplete=1 dropped=0
BENCH view_log_1g.txt_document_bulk3_initial mode=TRACK n=31 p50=286 p99=2798 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_log_1g.txt_document_bulk3_continue mode=TRACK n=15360 p50=2339 p99=2503 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
SLICE_MAX document 25845 ns(M)[AC] load1=3.63 hard_limit=500000 ns(G) verdict=TRACK incomplete_commands=15
FIXTURE /tmp/edit-corpus/log_1g.txt bytes=1073741824(M)[AC] load1=3.63 bulk=three-submitted source=mapped initial=certified
BENCH view_log_1g.txt_selection_bulk3_command mode=TRACK n=31 p50=1211 p99=2445703 ns (M)[AC] load1=3.63 necessary_gate=5000000/5555555 ns(G) verdict=TRACK incomplete=1 dropped=0
BENCH view_log_1g.txt_selection_bulk3_initial mode=TRACK n=31 p50=1153 p99=2914 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_log_1g.txt_selection_bulk3_continue mode=TRACK n=11264 p50=2343 p99=2488 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
SLICE_MAX selection 12095 ns(M)[AC] load1=3.63 hard_limit=500000 ns(G) verdict=TRACK incomplete_commands=11
FIXTURE /tmp/edit-corpus/log_1g.txt bytes=1073741824(M)[AC] load1=3.63 bulk=three-submitted source=mapped initial=certified
BENCH view_log_1g.txt_edit_bulk3_command mode=TRACK n=31 p50=2273 p99=6291 ns (M)[AC] load1=3.63 necessary_gate=1000000/2000000 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_log_1g.txt_edit_bulk3_initial mode=TRACK n=31 p50=2234 p99=6251 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
SLICE_MAX edit 6251 ns(M)[AC] load1=3.63 hard_limit=500000 ns(G) verdict=TRACK incomplete_commands=0
FIXTURE /tmp/edit-corpus/log_1g.txt bytes=1073741824(M)[AC] load1=3.63 bulk=three-submitted source=mapped initial=certified
SETUP_LIMIT deep_type skipped=31(M)[AC] load1=3.63 mode=TRACK verdict=TRACK setup_cap=100000000 ns(G)/1024 calls(G)
FIXTURE /tmp/edit-corpus/log_1g.txt bytes=1073741824(M)[AC] load1=3.63 bulk=three-submitted source=mapped initial=certified
SETUP_LIMIT selection_1MiB_replace skipped=31(M)[AC] load1=3.63 mode=TRACK verdict=TRACK setup_cap=100000000 ns(G)/1024 calls(G)
FIXTURE /tmp/edit-corpus/log_1g.txt bytes=1073741824(M)[AC] load1=3.63 bulk=three-submitted source=mapped initial=certified
BENCH view_log_1g.txt_unindexed_type_bulk3_command mode=TRACK n=31 p50=2566 p99=7275 ns (M)[AC] load1=3.63 necessary_gate=1000000/2000000 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_log_1g.txt_unindexed_type_bulk3_initial mode=TRACK n=31 p50=2523 p99=7138 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
SLICE_MAX unindexed_type 7138 ns(M)[AC] load1=3.63 hard_limit=500000 ns(G) verdict=TRACK incomplete_commands=0
ALLOCATIONS /tmp/edit-corpus/log_1g.txt mallocs=0(M)[AC] load1=3.63 gate=0(G) guard=active pass=1
FIXTURE /tmp/edit-corpus/oneline_1g.txt bytes=1073741824(M)[AC] load1=3.63 bulk=one-active source=plain-indexed initial=certified
BENCH view_oneline_1g.txt_char_bulk1_command mode=TRACK n=31 p50=541 p99=3233 ns (M)[AC] load1=3.63 necessary_gate=5000000/5555555 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_oneline_1g.txt_char_bulk1_initial mode=TRACK n=31 p50=511 p99=2948 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
SLICE_MAX char 2948 ns(M)[AC] load1=3.63 hard_limit=500000 ns(G) verdict=TRACK incomplete_commands=0
FIXTURE /tmp/edit-corpus/oneline_1g.txt bytes=1073741824(M)[AC] load1=3.63 bulk=one-active source=plain-indexed initial=certified
BENCH view_oneline_1g.txt_word_bulk1_command mode=TRACK n=31 p50=664 p99=1454 ns (M)[AC] load1=3.63 necessary_gate=5000000/5555555 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_oneline_1g.txt_word_bulk1_initial mode=TRACK n=31 p50=632 p99=1433 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
SLICE_MAX word 1433 ns(M)[AC] load1=3.63 hard_limit=500000 ns(G) verdict=TRACK incomplete_commands=0
FIXTURE /tmp/edit-corpus/oneline_1g.txt bytes=1073741824(M)[AC] load1=3.63 bulk=one-active source=plain-indexed initial=certified
BENCH view_oneline_1g.txt_vertical_bulk1_command mode=TRACK n=31 p50=596 p99=1819 ns (M)[AC] load1=3.63 necessary_gate=5000000/5555555 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_oneline_1g.txt_vertical_bulk1_initial mode=TRACK n=31 p50=567 p99=1784 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
SLICE_MAX vertical 1784 ns(M)[AC] load1=3.63 hard_limit=500000 ns(G) verdict=TRACK incomplete_commands=0
FIXTURE /tmp/edit-corpus/oneline_1g.txt bytes=1073741824(M)[AC] load1=3.63 bulk=one-active source=plain-indexed initial=certified
BENCH view_oneline_1g.txt_page_bulk1_command mode=TRACK n=31 p50=567 p99=798 ns (M)[AC] load1=3.63 necessary_gate=5000000/5555555 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_oneline_1g.txt_page_bulk1_initial mode=TRACK n=31 p50=535 p99=754 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
SLICE_MAX page 754 ns(M)[AC] load1=3.63 hard_limit=500000 ns(G) verdict=TRACK incomplete_commands=0
FIXTURE /tmp/edit-corpus/oneline_1g.txt bytes=1073741824(M)[AC] load1=3.63 bulk=one-active source=plain-indexed initial=certified
BENCH view_oneline_1g.txt_home_end_bulk1_command mode=TRACK n=31 p50=527 p99=1146 ns (M)[AC] load1=3.63 necessary_gate=5000000/5555555 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_oneline_1g.txt_home_end_bulk1_initial mode=TRACK n=31 p50=495 p99=1110 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
SLICE_MAX home_end 1110 ns(M)[AC] load1=3.63 hard_limit=500000 ns(G) verdict=TRACK incomplete_commands=0
FIXTURE /tmp/edit-corpus/oneline_1g.txt bytes=1073741824(M)[AC] load1=3.63 bulk=one-active source=plain-indexed initial=certified
BENCH view_oneline_1g.txt_document_bulk1_command mode=TRACK n=31 p50=306 p99=547 ns (M)[AC] load1=3.63 necessary_gate=5000000/5555555 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_oneline_1g.txt_document_bulk1_initial mode=TRACK n=31 p50=270 p99=494 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
SLICE_MAX document 494 ns(M)[AC] load1=3.63 hard_limit=500000 ns(G) verdict=TRACK incomplete_commands=0
FIXTURE /tmp/edit-corpus/oneline_1g.txt bytes=1073741824(M)[AC] load1=3.63 bulk=one-active source=plain-indexed initial=certified
BENCH view_oneline_1g.txt_selection_bulk1_command mode=TRACK n=31 p50=562 p99=2328 ns (M)[AC] load1=3.63 necessary_gate=5000000/5555555 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_oneline_1g.txt_selection_bulk1_initial mode=TRACK n=31 p50=533 p99=2279 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
SLICE_MAX selection 2279 ns(M)[AC] load1=3.63 hard_limit=500000 ns(G) verdict=TRACK incomplete_commands=0
FIXTURE /tmp/edit-corpus/oneline_1g.txt bytes=1073741824(M)[AC] load1=3.63 bulk=one-active source=plain-indexed initial=certified
BENCH view_oneline_1g.txt_edit_bulk1_command mode=TRACK n=31 p50=2267 p99=6945 ns (M)[AC] load1=3.63 necessary_gate=1000000/2000000 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_oneline_1g.txt_edit_bulk1_initial mode=TRACK n=31 p50=2227 p99=6925 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
SLICE_MAX edit 6925 ns(M)[AC] load1=3.63 hard_limit=500000 ns(G) verdict=TRACK incomplete_commands=0
FIXTURE /tmp/edit-corpus/oneline_1g.txt bytes=1073741824(M)[AC] load1=3.63 bulk=one-active source=plain-indexed initial=certified
BENCH view_oneline_1g.txt_deep_type_bulk1_command mode=TRACK n=31 p50=2619 p99=4354 ns (M)[AC] load1=3.63 necessary_gate=1000000/2000000 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_oneline_1g.txt_deep_type_bulk1_initial mode=TRACK n=31 p50=2579 p99=4317 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
SLICE_MAX deep_type 4317 ns(M)[AC] load1=3.63 hard_limit=500000 ns(G) verdict=TRACK incomplete_commands=0
FIXTURE /tmp/edit-corpus/oneline_1g.txt bytes=1073741824(M)[AC] load1=3.63 bulk=one-active source=plain-indexed initial=certified
BENCH view_oneline_1g.txt_selection_1MiB_replace_bulk1_command mode=TRACK n=31 p50=598612 p99=654277 ns (M)[AC] load1=3.63 necessary_gate=1000000/2000000 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_oneline_1g.txt_selection_1MiB_replace_bulk1_initial mode=TRACK n=31 p50=70 p99=372 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_oneline_1g.txt_selection_1MiB_replace_bulk1_continue mode=TRACK n=15903 p50=1176 p99=2360 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
SLICE_MAX selection_1MiB_replace 14898 ns(M)[AC] load1=3.63 hard_limit=500000 ns(G) verdict=TRACK incomplete_commands=0
FIXTURE /tmp/edit-corpus/oneline_1g.txt bytes=1073741824(M)[AC] load1=3.63 bulk=one-active source=plain-indexed initial=certified
BENCH view_oneline_1g.txt_unindexed_type_bulk1_command mode=TRACK n=31 p50=2539 p99=4667 ns (M)[AC] load1=3.63 necessary_gate=1000000/2000000 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_oneline_1g.txt_unindexed_type_bulk1_initial mode=TRACK n=31 p50=2499 p99=4628 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
SLICE_MAX unindexed_type 4628 ns(M)[AC] load1=3.63 hard_limit=500000 ns(G) verdict=TRACK incomplete_commands=0
FIXTURE /tmp/edit-corpus/oneline_1g.txt bytes=1073741824(M)[AC] load1=3.63 bulk=three-submitted source=plain-indexed initial=certified
BENCH view_oneline_1g.txt_char_bulk3_command mode=TRACK n=31 p50=539 p99=1682 ns (M)[AC] load1=3.63 necessary_gate=5000000/5555555 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_oneline_1g.txt_char_bulk3_initial mode=TRACK n=31 p50=507 p99=1648 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
SLICE_MAX char 1648 ns(M)[AC] load1=3.63 hard_limit=500000 ns(G) verdict=TRACK incomplete_commands=0
FIXTURE /tmp/edit-corpus/oneline_1g.txt bytes=1073741824(M)[AC] load1=3.63 bulk=three-submitted source=plain-indexed initial=certified
BENCH view_oneline_1g.txt_word_bulk3_command mode=TRACK n=31 p50=673 p99=1534 ns (M)[AC] load1=3.63 necessary_gate=5000000/5555555 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_oneline_1g.txt_word_bulk3_initial mode=TRACK n=31 p50=642 p99=1495 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
SLICE_MAX word 1495 ns(M)[AC] load1=3.63 hard_limit=500000 ns(G) verdict=TRACK incomplete_commands=0
FIXTURE /tmp/edit-corpus/oneline_1g.txt bytes=1073741824(M)[AC] load1=3.63 bulk=three-submitted source=plain-indexed initial=certified
BENCH view_oneline_1g.txt_vertical_bulk3_command mode=TRACK n=31 p50=587 p99=1529 ns (M)[AC] load1=3.63 necessary_gate=5000000/5555555 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_oneline_1g.txt_vertical_bulk3_initial mode=TRACK n=31 p50=550 p99=1485 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
SLICE_MAX vertical 1485 ns(M)[AC] load1=3.63 hard_limit=500000 ns(G) verdict=TRACK incomplete_commands=0
FIXTURE /tmp/edit-corpus/oneline_1g.txt bytes=1073741824(M)[AC] load1=3.63 bulk=three-submitted source=plain-indexed initial=certified
BENCH view_oneline_1g.txt_page_bulk3_command mode=TRACK n=31 p50=593 p99=763 ns (M)[AC] load1=3.63 necessary_gate=5000000/5555555 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_oneline_1g.txt_page_bulk3_initial mode=TRACK n=31 p50=560 p99=709 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
SLICE_MAX page 709 ns(M)[AC] load1=3.63 hard_limit=500000 ns(G) verdict=TRACK incomplete_commands=0
FIXTURE /tmp/edit-corpus/oneline_1g.txt bytes=1073741824(M)[AC] load1=3.63 bulk=three-submitted source=plain-indexed initial=certified
BENCH view_oneline_1g.txt_home_end_bulk3_command mode=TRACK n=31 p50=542 p99=1013 ns (M)[AC] load1=3.63 necessary_gate=5000000/5555555 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_oneline_1g.txt_home_end_bulk3_initial mode=TRACK n=31 p50=509 p99=994 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
SLICE_MAX home_end 994 ns(M)[AC] load1=3.63 hard_limit=500000 ns(G) verdict=TRACK incomplete_commands=0
FIXTURE /tmp/edit-corpus/oneline_1g.txt bytes=1073741824(M)[AC] load1=3.63 bulk=three-submitted source=plain-indexed initial=certified
BENCH view_oneline_1g.txt_document_bulk3_command mode=TRACK n=31 p50=391 p99=1590 ns (M)[AC] load1=3.63 necessary_gate=5000000/5555555 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_oneline_1g.txt_document_bulk3_initial mode=TRACK n=31 p50=361 p99=1433 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
SLICE_MAX document 1433 ns(M)[AC] load1=3.63 hard_limit=500000 ns(G) verdict=TRACK incomplete_commands=0
FIXTURE /tmp/edit-corpus/oneline_1g.txt bytes=1073741824(M)[AC] load1=3.63 bulk=three-submitted source=plain-indexed initial=certified
BENCH view_oneline_1g.txt_selection_bulk3_command mode=TRACK n=31 p50=552 p99=5153 ns (M)[AC] load1=3.63 necessary_gate=5000000/5555555 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_oneline_1g.txt_selection_bulk3_initial mode=TRACK n=31 p50=520 p99=5116 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
SLICE_MAX selection 5116 ns(M)[AC] load1=3.63 hard_limit=500000 ns(G) verdict=TRACK incomplete_commands=0
FIXTURE /tmp/edit-corpus/oneline_1g.txt bytes=1073741824(M)[AC] load1=3.63 bulk=three-submitted source=plain-indexed initial=certified
BENCH view_oneline_1g.txt_edit_bulk3_command mode=TRACK n=31 p50=2361 p99=6030 ns (M)[AC] load1=3.63 necessary_gate=1000000/2000000 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_oneline_1g.txt_edit_bulk3_initial mode=TRACK n=31 p50=2317 p99=5987 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
SLICE_MAX edit 5987 ns(M)[AC] load1=3.63 hard_limit=500000 ns(G) verdict=TRACK incomplete_commands=0
FIXTURE /tmp/edit-corpus/oneline_1g.txt bytes=1073741824(M)[AC] load1=3.63 bulk=three-submitted source=plain-indexed initial=certified
BENCH view_oneline_1g.txt_deep_type_bulk3_command mode=TRACK n=31 p50=2694 p99=3923 ns (M)[AC] load1=3.63 necessary_gate=1000000/2000000 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_oneline_1g.txt_deep_type_bulk3_initial mode=TRACK n=31 p50=2657 p99=3889 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
SLICE_MAX deep_type 3889 ns(M)[AC] load1=3.63 hard_limit=500000 ns(G) verdict=TRACK incomplete_commands=0
FIXTURE /tmp/edit-corpus/oneline_1g.txt bytes=1073741824(M)[AC] load1=3.63 bulk=three-submitted source=plain-indexed initial=certified
BENCH view_oneline_1g.txt_selection_1MiB_replace_bulk3_command mode=TRACK n=31 p50=607660 p99=707234 ns (M)[AC] load1=3.63 necessary_gate=1000000/2000000 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_oneline_1g.txt_selection_1MiB_replace_bulk3_initial mode=TRACK n=31 p50=70 p99=190 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_oneline_1g.txt_selection_1MiB_replace_bulk3_continue mode=TRACK n=15903 p50=1201 p99=2375 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
SLICE_MAX selection_1MiB_replace 31208 ns(M)[AC] load1=3.63 hard_limit=500000 ns(G) verdict=TRACK incomplete_commands=0
FIXTURE /tmp/edit-corpus/oneline_1g.txt bytes=1073741824(M)[AC] load1=3.63 bulk=three-submitted source=plain-indexed initial=certified
BENCH view_oneline_1g.txt_unindexed_type_bulk3_command mode=TRACK n=31 p50=2572 p99=12624 ns (M)[AC] load1=3.63 necessary_gate=1000000/2000000 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_oneline_1g.txt_unindexed_type_bulk3_initial mode=TRACK n=31 p50=2533 p99=12584 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
SLICE_MAX unindexed_type 12584 ns(M)[AC] load1=3.63 hard_limit=500000 ns(G) verdict=TRACK incomplete_commands=0
ALLOCATIONS /tmp/edit-corpus/oneline_1g.txt mallocs=0(M)[AC] load1=3.63 gate=0(G) guard=active pass=1
FIXTURE /tmp/edit-corpus/unicode.txt bytes=1048576(M)[AC] load1=3.63 bulk=one-active source=mapped initial=certified
BENCH view_unicode.txt_char_bulk1_command mode=TRACK n=31 p50=612 p99=3217 ns (M)[AC] load1=3.63 necessary_gate=5000000/5555555 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_unicode.txt_char_bulk1_initial mode=TRACK n=31 p50=592 p99=3069 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
SLICE_MAX char 3069 ns(M)[AC] load1=3.63 hard_limit=500000 ns(G) verdict=TRACK incomplete_commands=0
FIXTURE /tmp/edit-corpus/unicode.txt bytes=1048576(M)[AC] load1=3.63 bulk=one-active source=mapped initial=certified
BENCH view_unicode.txt_word_bulk1_command mode=TRACK n=31 p50=997 p99=2528 ns (M)[AC] load1=3.63 necessary_gate=5000000/5555555 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_unicode.txt_word_bulk1_initial mode=TRACK n=31 p50=973 p99=2505 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
SLICE_MAX word 2505 ns(M)[AC] load1=3.63 hard_limit=500000 ns(G) verdict=TRACK incomplete_commands=0
FIXTURE /tmp/edit-corpus/unicode.txt bytes=1048576(M)[AC] load1=3.63 bulk=one-active source=mapped initial=certified
BENCH view_unicode.txt_vertical_bulk1_command mode=TRACK n=31 p50=1135 p99=4450 ns (M)[AC] load1=3.63 necessary_gate=5000000/5555555 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_unicode.txt_vertical_bulk1_initial mode=TRACK n=31 p50=1114 p99=4402 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
SLICE_MAX vertical 4402 ns(M)[AC] load1=3.63 hard_limit=500000 ns(G) verdict=TRACK incomplete_commands=0
FIXTURE /tmp/edit-corpus/unicode.txt bytes=1048576(M)[AC] load1=3.63 bulk=one-active source=mapped initial=certified
BENCH view_unicode.txt_page_bulk1_command mode=TRACK n=31 p50=1313 p99=3314 ns (M)[AC] load1=3.63 necessary_gate=5000000/5555555 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_unicode.txt_page_bulk1_initial mode=TRACK n=31 p50=1275 p99=3288 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
SLICE_MAX page 3288 ns(M)[AC] load1=3.63 hard_limit=500000 ns(G) verdict=TRACK incomplete_commands=0
FIXTURE /tmp/edit-corpus/unicode.txt bytes=1048576(M)[AC] load1=3.63 bulk=one-active source=mapped initial=certified
BENCH view_unicode.txt_home_end_bulk1_command mode=TRACK n=31 p50=781 p99=5542 ns (M)[AC] load1=3.63 necessary_gate=5000000/5555555 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_unicode.txt_home_end_bulk1_initial mode=TRACK n=31 p50=760 p99=5522 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
SLICE_MAX home_end 5522 ns(M)[AC] load1=3.63 hard_limit=500000 ns(G) verdict=TRACK incomplete_commands=0
FIXTURE /tmp/edit-corpus/unicode.txt bytes=1048576(M)[AC] load1=3.63 bulk=one-active source=mapped initial=certified
BENCH view_unicode.txt_document_bulk1_command mode=TRACK n=31 p50=338 p99=967943 ns (M)[AC] load1=3.63 necessary_gate=5000000/5555555 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_unicode.txt_document_bulk1_initial mode=TRACK n=31 p50=319 p99=3893 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_unicode.txt_document_bulk1_continue mode=TRACK n=4095 p50=3092 p99=3602 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
SLICE_MAX document 50321 ns(M)[AC] load1=3.63 hard_limit=500000 ns(G) verdict=TRACK incomplete_commands=0
FIXTURE /tmp/edit-corpus/unicode.txt bytes=1048576(M)[AC] load1=3.63 bulk=one-active source=mapped initial=certified
BENCH view_unicode.txt_selection_bulk1_command mode=TRACK n=31 p50=1271 p99=845854 ns (M)[AC] load1=3.63 necessary_gate=5000000/5555555 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_unicode.txt_selection_bulk1_initial mode=TRACK n=31 p50=1251 p99=3562 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_unicode.txt_selection_bulk1_continue mode=TRACK n=3003 p50=3035 p99=3249 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
SLICE_MAX selection 8740 ns(M)[AC] load1=3.63 hard_limit=500000 ns(G) verdict=TRACK incomplete_commands=0
FIXTURE /tmp/edit-corpus/unicode.txt bytes=1048576(M)[AC] load1=3.63 bulk=one-active source=mapped initial=certified
BENCH view_unicode.txt_edit_bulk1_command mode=TRACK n=31 p50=975 p99=4867 ns (M)[AC] load1=3.63 necessary_gate=1000000/2000000 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_unicode.txt_edit_bulk1_initial mode=TRACK n=31 p50=957 p99=4833 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
SLICE_MAX edit 4833 ns(M)[AC] load1=3.63 hard_limit=500000 ns(G) verdict=TRACK incomplete_commands=0
FIXTURE /tmp/edit-corpus/unicode.txt bytes=1048576(M)[AC] load1=3.63 bulk=one-active source=mapped initial=certified
BENCH view_unicode.txt_deep_type_bulk1_command mode=TRACK n=31 p50=1186 p99=2817 ns (M)[AC] load1=3.63 necessary_gate=1000000/2000000 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_unicode.txt_deep_type_bulk1_initial mode=TRACK n=31 p50=1155 p99=2765 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
SLICE_MAX deep_type 2765 ns(M)[AC] load1=3.63 hard_limit=500000 ns(G) verdict=TRACK incomplete_commands=0
FIXTURE /tmp/edit-corpus/unicode.txt bytes=1048576(M)[AC] load1=3.63 bulk=one-active source=mapped initial=certified
BENCH view_unicode.txt_selection_1MiB_replace_bulk1_command mode=TRACK n=31 p50=500540 p99=560690 ns (M)[AC] load1=3.63 necessary_gate=1000000/2000000 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_unicode.txt_selection_1MiB_replace_bulk1_initial mode=TRACK n=31 p50=70 p99=169 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_unicode.txt_selection_1MiB_replace_bulk1_continue mode=TRACK n=15903 p50=679 p99=2139 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
SLICE_MAX selection_1MiB_replace 7051 ns(M)[AC] load1=3.63 hard_limit=500000 ns(G) verdict=TRACK incomplete_commands=0
FIXTURE /tmp/edit-corpus/unicode.txt bytes=1048576(M)[AC] load1=3.63 bulk=one-active source=mapped initial=certified
BENCH view_unicode.txt_unindexed_type_bulk1_command mode=TRACK n=31 p50=630 p99=1971 ns (M)[AC] load1=3.63 necessary_gate=1000000/2000000 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_unicode.txt_unindexed_type_bulk1_initial mode=TRACK n=31 p50=614 p99=1946 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
SLICE_MAX unindexed_type 1946 ns(M)[AC] load1=3.63 hard_limit=500000 ns(G) verdict=TRACK incomplete_commands=0
FIXTURE /tmp/edit-corpus/unicode.txt bytes=1048576(M)[AC] load1=3.63 bulk=three-submitted source=mapped initial=certified
BENCH view_unicode.txt_char_bulk3_command mode=TRACK n=31 p50=493 p99=1664 ns (M)[AC] load1=3.63 necessary_gate=5000000/5555555 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_unicode.txt_char_bulk3_initial mode=TRACK n=31 p50=476 p99=1647 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
SLICE_MAX char 1647 ns(M)[AC] load1=3.63 hard_limit=500000 ns(G) verdict=TRACK incomplete_commands=0
FIXTURE /tmp/edit-corpus/unicode.txt bytes=1048576(M)[AC] load1=3.63 bulk=three-submitted source=mapped initial=certified
BENCH view_unicode.txt_word_bulk3_command mode=TRACK n=31 p50=767 p99=2393 ns (M)[AC] load1=3.63 necessary_gate=5000000/5555555 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_unicode.txt_word_bulk3_initial mode=TRACK n=31 p50=748 p99=2377 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
SLICE_MAX word 2377 ns(M)[AC] load1=3.63 hard_limit=500000 ns(G) verdict=TRACK incomplete_commands=0
FIXTURE /tmp/edit-corpus/unicode.txt bytes=1048576(M)[AC] load1=3.63 bulk=three-submitted source=mapped initial=certified
BENCH view_unicode.txt_vertical_bulk3_command mode=TRACK n=31 p50=1078 p99=3399 ns (M)[AC] load1=3.63 necessary_gate=5000000/5555555 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_unicode.txt_vertical_bulk3_initial mode=TRACK n=31 p50=1062 p99=3379 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
SLICE_MAX vertical 3379 ns(M)[AC] load1=3.63 hard_limit=500000 ns(G) verdict=TRACK incomplete_commands=0
FIXTURE /tmp/edit-corpus/unicode.txt bytes=1048576(M)[AC] load1=3.63 bulk=three-submitted source=mapped initial=certified
BENCH view_unicode.txt_page_bulk3_command mode=TRACK n=31 p50=718 p99=2240 ns (M)[AC] load1=3.63 necessary_gate=5000000/5555555 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_unicode.txt_page_bulk3_initial mode=TRACK n=31 p50=690 p99=2212 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
SLICE_MAX page 2212 ns(M)[AC] load1=3.63 hard_limit=500000 ns(G) verdict=TRACK incomplete_commands=0
FIXTURE /tmp/edit-corpus/unicode.txt bytes=1048576(M)[AC] load1=3.63 bulk=three-submitted source=mapped initial=certified
BENCH view_unicode.txt_home_end_bulk3_command mode=TRACK n=31 p50=668 p99=4873 ns (M)[AC] load1=3.63 necessary_gate=5000000/5555555 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_unicode.txt_home_end_bulk3_initial mode=TRACK n=31 p50=640 p99=4845 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
SLICE_MAX home_end 4845 ns(M)[AC] load1=3.63 hard_limit=500000 ns(G) verdict=TRACK incomplete_commands=0
FIXTURE /tmp/edit-corpus/unicode.txt bytes=1048576(M)[AC] load1=3.63 bulk=three-submitted source=mapped initial=certified
BENCH view_unicode.txt_document_bulk3_command mode=TRACK n=31 p50=299 p99=908516 ns (M)[AC] load1=3.63 necessary_gate=5000000/5555555 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_unicode.txt_document_bulk3_initial mode=TRACK n=31 p50=274 p99=3489 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_unicode.txt_document_bulk3_continue mode=TRACK n=4095 p50=3007 p99=3239 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
SLICE_MAX document 79010 ns(M)[AC] load1=3.63 hard_limit=500000 ns(G) verdict=TRACK incomplete_commands=0
FIXTURE /tmp/edit-corpus/unicode.txt bytes=1048576(M)[AC] load1=3.63 bulk=three-submitted source=mapped initial=certified
BENCH view_unicode.txt_selection_bulk3_command mode=TRACK n=31 p50=1211 p99=842964 ns (M)[AC] load1=3.63 necessary_gate=5000000/5555555 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_unicode.txt_selection_bulk3_initial mode=TRACK n=31 p50=1193 p99=3344 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_unicode.txt_selection_bulk3_continue mode=TRACK n=3003 p50=3006 p99=3228 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
SLICE_MAX selection 14493 ns(M)[AC] load1=3.63 hard_limit=500000 ns(G) verdict=TRACK incomplete_commands=0
FIXTURE /tmp/edit-corpus/unicode.txt bytes=1048576(M)[AC] load1=3.63 bulk=three-submitted source=mapped initial=certified
BENCH view_unicode.txt_edit_bulk3_command mode=TRACK n=31 p50=1007 p99=2875 ns (M)[AC] load1=3.63 necessary_gate=1000000/2000000 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_unicode.txt_edit_bulk3_initial mode=TRACK n=31 p50=990 p99=2834 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
SLICE_MAX edit 2834 ns(M)[AC] load1=3.63 hard_limit=500000 ns(G) verdict=TRACK incomplete_commands=0
FIXTURE /tmp/edit-corpus/unicode.txt bytes=1048576(M)[AC] load1=3.63 bulk=three-submitted source=mapped initial=certified
BENCH view_unicode.txt_deep_type_bulk3_command mode=TRACK n=31 p50=1117 p99=2356 ns (M)[AC] load1=3.63 necessary_gate=1000000/2000000 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_unicode.txt_deep_type_bulk3_initial mode=TRACK n=31 p50=1084 p99=2323 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
SLICE_MAX deep_type 2323 ns(M)[AC] load1=3.63 hard_limit=500000 ns(G) verdict=TRACK incomplete_commands=0
FIXTURE /tmp/edit-corpus/unicode.txt bytes=1048576(M)[AC] load1=3.63 bulk=three-submitted source=mapped initial=certified
BENCH view_unicode.txt_selection_1MiB_replace_bulk3_command mode=TRACK n=31 p50=501466 p99=557756 ns (M)[AC] load1=3.63 necessary_gate=1000000/2000000 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_unicode.txt_selection_1MiB_replace_bulk3_initial mode=TRACK n=31 p50=71 p99=371 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_unicode.txt_selection_1MiB_replace_bulk3_continue mode=TRACK n=15903 p50=689 p99=2131 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
SLICE_MAX selection_1MiB_replace 6246 ns(M)[AC] load1=3.63 hard_limit=500000 ns(G) verdict=TRACK incomplete_commands=0
FIXTURE /tmp/edit-corpus/unicode.txt bytes=1048576(M)[AC] load1=3.63 bulk=three-submitted source=mapped initial=certified
BENCH view_unicode.txt_unindexed_type_bulk3_command mode=TRACK n=31 p50=625 p99=1939 ns (M)[AC] load1=3.63 necessary_gate=1000000/2000000 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_unicode.txt_unindexed_type_bulk3_initial mode=TRACK n=31 p50=609 p99=1913 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
SLICE_MAX unindexed_type 1913 ns(M)[AC] load1=3.63 hard_limit=500000 ns(G) verdict=TRACK incomplete_commands=0
ALLOCATIONS /tmp/edit-corpus/unicode.txt mallocs=0(M)[AC] load1=3.63 gate=0(G) guard=active pass=1
AFTER utc=2026-10-10T16:11:36.915241+00:00 power=Not charging loadavg=3.63 1.91 1.48 1/1339 19 (M)[AC]
EXIT 0 elapsed_s=1.1178573619981762 (M)[AC]
```

### view, adjacent run 2

```text
COMMAND build/bench/view_bench args_file=absent; defaults
BEFORE utc=2026-10-10T16:11:36.915658+00:00 power=Not charging loadavg=3.63 1.91 1.48 1/1339 19 (M)[AC]
POWER Not charging [AC] load1=3.63 mode=TRACK; module-only necessary limits, 50 us remains (E)
FIXTURE /tmp/edit-corpus/log_1g.txt bytes=1073741824(M)[AC] load1=3.63 bulk=one-active source=mapped initial=certified
BENCH view_log_1g.txt_char_bulk1_command mode=TRACK n=31 p50=583 p99=2642 ns (M)[AC] load1=3.63 necessary_gate=5000000/5555555 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_log_1g.txt_char_bulk1_initial mode=TRACK n=31 p50=550 p99=2585 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
SLICE_MAX char 2585 ns(M)[AC] load1=3.63 hard_limit=500000 ns(G) verdict=TRACK incomplete_commands=0
FIXTURE /tmp/edit-corpus/log_1g.txt bytes=1073741824(M)[AC] load1=3.63 bulk=one-active source=mapped initial=certified
BENCH view_log_1g.txt_word_bulk1_command mode=TRACK n=31 p50=668 p99=1492 ns (M)[AC] load1=3.63 necessary_gate=5000000/5555555 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_log_1g.txt_word_bulk1_initial mode=TRACK n=31 p50=637 p99=1454 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
SLICE_MAX word 1454 ns(M)[AC] load1=3.63 hard_limit=500000 ns(G) verdict=TRACK incomplete_commands=0
FIXTURE /tmp/edit-corpus/log_1g.txt bytes=1073741824(M)[AC] load1=3.63 bulk=one-active source=mapped initial=certified
BENCH view_log_1g.txt_vertical_bulk1_command mode=TRACK n=31 p50=898 p99=1967 ns (M)[AC] load1=3.63 necessary_gate=5000000/5555555 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_log_1g.txt_vertical_bulk1_initial mode=TRACK n=31 p50=866 p99=1905 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
SLICE_MAX vertical 1905 ns(M)[AC] load1=3.63 hard_limit=500000 ns(G) verdict=TRACK incomplete_commands=0
FIXTURE /tmp/edit-corpus/log_1g.txt bytes=1073741824(M)[AC] load1=3.63 bulk=one-active source=mapped initial=certified
BENCH view_log_1g.txt_page_bulk1_command mode=TRACK n=31 p50=688 p99=3260 ns (M)[AC] load1=3.63 necessary_gate=5000000/5555555 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_log_1g.txt_page_bulk1_initial mode=TRACK n=31 p50=656 p99=3214 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
SLICE_MAX page 3214 ns(M)[AC] load1=3.63 hard_limit=500000 ns(G) verdict=TRACK incomplete_commands=0
FIXTURE /tmp/edit-corpus/log_1g.txt bytes=1073741824(M)[AC] load1=3.63 bulk=one-active source=mapped initial=certified
BENCH view_log_1g.txt_home_end_bulk1_command mode=TRACK n=31 p50=779 p99=4484 ns (M)[AC] load1=3.63 necessary_gate=5000000/5555555 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_log_1g.txt_home_end_bulk1_initial mode=TRACK n=31 p50=762 p99=4427 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
SLICE_MAX home_end 4427 ns(M)[AC] load1=3.63 hard_limit=500000 ns(G) verdict=TRACK incomplete_commands=0
FIXTURE /tmp/edit-corpus/log_1g.txt bytes=1073741824(M)[AC] load1=3.63 bulk=one-active source=mapped initial=certified
BENCH view_log_1g.txt_document_bulk1_command mode=TRACK n=31 p50=477 p99=2516605 ns (M)[AC] load1=3.63 necessary_gate=5000000/5555555 ns(G) verdict=TRACK incomplete=1 dropped=0
BENCH view_log_1g.txt_document_bulk1_initial mode=TRACK n=31 p50=345 p99=2843 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_log_1g.txt_document_bulk1_continue mode=TRACK n=15360 p50=2391 p99=2604 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
SLICE_MAX document 16040 ns(M)[AC] load1=3.63 hard_limit=500000 ns(G) verdict=TRACK incomplete_commands=15
FIXTURE /tmp/edit-corpus/log_1g.txt bytes=1073741824(M)[AC] load1=3.63 bulk=one-active source=mapped initial=certified
BENCH view_log_1g.txt_selection_bulk1_command mode=TRACK n=31 p50=1155 p99=2534700 ns (M)[AC] load1=3.63 necessary_gate=5000000/5555555 ns(G) verdict=TRACK incomplete=1 dropped=0
BENCH view_log_1g.txt_selection_bulk1_initial mode=TRACK n=31 p50=1110 p99=3167 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_log_1g.txt_selection_bulk1_continue mode=TRACK n=11264 p50=2392 p99=2587 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
SLICE_MAX selection 20124 ns(M)[AC] load1=3.63 hard_limit=500000 ns(G) verdict=TRACK incomplete_commands=11
FIXTURE /tmp/edit-corpus/log_1g.txt bytes=1073741824(M)[AC] load1=3.63 bulk=one-active source=mapped initial=certified
BENCH view_log_1g.txt_edit_bulk1_command mode=TRACK n=31 p50=2301 p99=6508 ns (M)[AC] load1=3.63 necessary_gate=1000000/2000000 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_log_1g.txt_edit_bulk1_initial mode=TRACK n=31 p50=2257 p99=6473 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
SLICE_MAX edit 6473 ns(M)[AC] load1=3.63 hard_limit=500000 ns(G) verdict=TRACK incomplete_commands=0
FIXTURE /tmp/edit-corpus/log_1g.txt bytes=1073741824(M)[AC] load1=3.63 bulk=one-active source=mapped initial=certified
SETUP_LIMIT deep_type skipped=31(M)[AC] load1=3.63 mode=TRACK verdict=TRACK setup_cap=100000000 ns(G)/1024 calls(G)
FIXTURE /tmp/edit-corpus/log_1g.txt bytes=1073741824(M)[AC] load1=3.63 bulk=one-active source=mapped initial=certified
SETUP_LIMIT selection_1MiB_replace skipped=31(M)[AC] load1=3.63 mode=TRACK verdict=TRACK setup_cap=100000000 ns(G)/1024 calls(G)
FIXTURE /tmp/edit-corpus/log_1g.txt bytes=1073741824(M)[AC] load1=3.63 bulk=one-active source=mapped initial=certified
BENCH view_log_1g.txt_unindexed_type_bulk1_command mode=TRACK n=31 p50=2543 p99=6830 ns (M)[AC] load1=3.63 necessary_gate=1000000/2000000 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_log_1g.txt_unindexed_type_bulk1_initial mode=TRACK n=31 p50=2487 p99=6794 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
SLICE_MAX unindexed_type 6794 ns(M)[AC] load1=3.63 hard_limit=500000 ns(G) verdict=TRACK incomplete_commands=0
FIXTURE /tmp/edit-corpus/log_1g.txt bytes=1073741824(M)[AC] load1=3.63 bulk=three-submitted source=mapped initial=certified
BENCH view_log_1g.txt_char_bulk3_command mode=TRACK n=31 p50=530 p99=1579 ns (M)[AC] load1=3.63 necessary_gate=5000000/5555555 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_log_1g.txt_char_bulk3_initial mode=TRACK n=31 p50=498 p99=1534 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
SLICE_MAX char 1534 ns(M)[AC] load1=3.63 hard_limit=500000 ns(G) verdict=TRACK incomplete_commands=0
FIXTURE /tmp/edit-corpus/log_1g.txt bytes=1073741824(M)[AC] load1=3.63 bulk=three-submitted source=mapped initial=certified
BENCH view_log_1g.txt_word_bulk3_command mode=TRACK n=31 p50=640 p99=2015 ns (M)[AC] load1=3.63 necessary_gate=5000000/5555555 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_log_1g.txt_word_bulk3_initial mode=TRACK n=31 p50=608 p99=1983 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
SLICE_MAX word 1983 ns(M)[AC] load1=3.63 hard_limit=500000 ns(G) verdict=TRACK incomplete_commands=0
FIXTURE /tmp/edit-corpus/log_1g.txt bytes=1073741824(M)[AC] load1=3.63 bulk=three-submitted source=mapped initial=certified
BENCH view_log_1g.txt_vertical_bulk3_command mode=TRACK n=31 p50=891 p99=1980 ns (M)[AC] load1=3.63 necessary_gate=5000000/5555555 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_log_1g.txt_vertical_bulk3_initial mode=TRACK n=31 p50=858 p99=1935 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
SLICE_MAX vertical 1935 ns(M)[AC] load1=3.63 hard_limit=500000 ns(G) verdict=TRACK incomplete_commands=0
FIXTURE /tmp/edit-corpus/log_1g.txt bytes=1073741824(M)[AC] load1=3.63 bulk=three-submitted source=mapped initial=certified
BENCH view_log_1g.txt_page_bulk3_command mode=TRACK n=31 p50=742 p99=3185 ns (M)[AC] load1=3.63 necessary_gate=5000000/5555555 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_log_1g.txt_page_bulk3_initial mode=TRACK n=31 p50=692 p99=3128 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
SLICE_MAX page 3128 ns(M)[AC] load1=3.63 hard_limit=500000 ns(G) verdict=TRACK incomplete_commands=0
FIXTURE /tmp/edit-corpus/log_1g.txt bytes=1073741824(M)[AC] load1=3.63 bulk=three-submitted source=mapped initial=certified
BENCH view_log_1g.txt_home_end_bulk3_command mode=TRACK n=31 p50=841 p99=4493 ns (M)[AC] load1=3.63 necessary_gate=5000000/5555555 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_log_1g.txt_home_end_bulk3_initial mode=TRACK n=31 p50=810 p99=4447 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
SLICE_MAX home_end 4447 ns(M)[AC] load1=3.63 hard_limit=500000 ns(G) verdict=TRACK incomplete_commands=0
FIXTURE /tmp/edit-corpus/log_1g.txt bytes=1073741824(M)[AC] load1=3.63 bulk=three-submitted source=mapped initial=certified
BENCH view_log_1g.txt_document_bulk3_command mode=TRACK n=31 p50=425 p99=2532708 ns (M)[AC] load1=3.63 necessary_gate=5000000/5555555 ns(G) verdict=TRACK incomplete=1 dropped=0
BENCH view_log_1g.txt_document_bulk3_initial mode=TRACK n=31 p50=369 p99=2801 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_log_1g.txt_document_bulk3_continue mode=TRACK n=15360 p50=2393 p99=2632 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
SLICE_MAX document 20618 ns(M)[AC] load1=3.63 hard_limit=500000 ns(G) verdict=TRACK incomplete_commands=15
FIXTURE /tmp/edit-corpus/log_1g.txt bytes=1073741824(M)[AC] load1=3.63 bulk=three-submitted source=mapped initial=certified
BENCH view_log_1g.txt_selection_bulk3_command mode=TRACK n=31 p50=1312 p99=2525513 ns (M)[AC] load1=3.63 necessary_gate=5000000/5555555 ns(G) verdict=TRACK incomplete=1 dropped=0
BENCH view_log_1g.txt_selection_bulk3_initial mode=TRACK n=31 p50=1280 p99=2911 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_log_1g.txt_selection_bulk3_continue mode=TRACK n=11264 p50=2392 p99=2610 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
SLICE_MAX selection 11263 ns(M)[AC] load1=3.63 hard_limit=500000 ns(G) verdict=TRACK incomplete_commands=11
FIXTURE /tmp/edit-corpus/log_1g.txt bytes=1073741824(M)[AC] load1=3.63 bulk=three-submitted source=mapped initial=certified
BENCH view_log_1g.txt_edit_bulk3_command mode=TRACK n=31 p50=2256 p99=6878 ns (M)[AC] load1=3.63 necessary_gate=1000000/2000000 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_log_1g.txt_edit_bulk3_initial mode=TRACK n=31 p50=2206 p99=6836 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
SLICE_MAX edit 6836 ns(M)[AC] load1=3.63 hard_limit=500000 ns(G) verdict=TRACK incomplete_commands=0
FIXTURE /tmp/edit-corpus/log_1g.txt bytes=1073741824(M)[AC] load1=3.63 bulk=three-submitted source=mapped initial=certified
SETUP_LIMIT deep_type skipped=31(M)[AC] load1=3.63 mode=TRACK verdict=TRACK setup_cap=100000000 ns(G)/1024 calls(G)
FIXTURE /tmp/edit-corpus/log_1g.txt bytes=1073741824(M)[AC] load1=3.63 bulk=three-submitted source=mapped initial=certified
SETUP_LIMIT selection_1MiB_replace skipped=31(M)[AC] load1=3.63 mode=TRACK verdict=TRACK setup_cap=100000000 ns(G)/1024 calls(G)
FIXTURE /tmp/edit-corpus/log_1g.txt bytes=1073741824(M)[AC] load1=3.63 bulk=three-submitted source=mapped initial=certified
BENCH view_log_1g.txt_unindexed_type_bulk3_command mode=TRACK n=31 p50=2567 p99=6065 ns (M)[AC] load1=3.63 necessary_gate=1000000/2000000 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_log_1g.txt_unindexed_type_bulk3_initial mode=TRACK n=31 p50=2521 p99=6019 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
SLICE_MAX unindexed_type 6019 ns(M)[AC] load1=3.63 hard_limit=500000 ns(G) verdict=TRACK incomplete_commands=0
ALLOCATIONS /tmp/edit-corpus/log_1g.txt mallocs=0(M)[AC] load1=3.63 gate=0(G) guard=active pass=1
FIXTURE /tmp/edit-corpus/oneline_1g.txt bytes=1073741824(M)[AC] load1=3.63 bulk=one-active source=plain-indexed initial=certified
BENCH view_oneline_1g.txt_char_bulk1_command mode=TRACK n=31 p50=1081 p99=3841 ns (M)[AC] load1=3.63 necessary_gate=5000000/5555555 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_oneline_1g.txt_char_bulk1_initial mode=TRACK n=31 p50=1023 p99=3681 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
SLICE_MAX char 3681 ns(M)[AC] load1=3.63 hard_limit=500000 ns(G) verdict=TRACK incomplete_commands=0
FIXTURE /tmp/edit-corpus/oneline_1g.txt bytes=1073741824(M)[AC] load1=3.63 bulk=one-active source=plain-indexed initial=certified
BENCH view_oneline_1g.txt_word_bulk1_command mode=TRACK n=31 p50=690 p99=2939 ns (M)[AC] load1=3.63 necessary_gate=5000000/5555555 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_oneline_1g.txt_word_bulk1_initial mode=TRACK n=31 p50=656 p99=2861 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
SLICE_MAX word 2861 ns(M)[AC] load1=3.63 hard_limit=500000 ns(G) verdict=TRACK incomplete_commands=0
FIXTURE /tmp/edit-corpus/oneline_1g.txt bytes=1073741824(M)[AC] load1=3.63 bulk=one-active source=plain-indexed initial=certified
BENCH view_oneline_1g.txt_vertical_bulk1_command mode=TRACK n=31 p50=560 p99=1389 ns (M)[AC] load1=3.63 necessary_gate=5000000/5555555 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_oneline_1g.txt_vertical_bulk1_initial mode=TRACK n=31 p50=524 p99=1345 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
SLICE_MAX vertical 1345 ns(M)[AC] load1=3.63 hard_limit=500000 ns(G) verdict=TRACK incomplete_commands=0
FIXTURE /tmp/edit-corpus/oneline_1g.txt bytes=1073741824(M)[AC] load1=3.63 bulk=one-active source=plain-indexed initial=certified
BENCH view_oneline_1g.txt_page_bulk1_command mode=TRACK n=31 p50=523 p99=746 ns (M)[AC] load1=3.63 necessary_gate=5000000/5555555 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_oneline_1g.txt_page_bulk1_initial mode=TRACK n=31 p50=493 p99=702 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
SLICE_MAX page 702 ns(M)[AC] load1=3.63 hard_limit=500000 ns(G) verdict=TRACK incomplete_commands=0
FIXTURE /tmp/edit-corpus/oneline_1g.txt bytes=1073741824(M)[AC] load1=3.63 bulk=one-active source=plain-indexed initial=certified
BENCH view_oneline_1g.txt_home_end_bulk1_command mode=TRACK n=31 p50=502 p99=844 ns (M)[AC] load1=3.63 necessary_gate=5000000/5555555 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_oneline_1g.txt_home_end_bulk1_initial mode=TRACK n=31 p50=470 p99=799 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
SLICE_MAX home_end 799 ns(M)[AC] load1=3.63 hard_limit=500000 ns(G) verdict=TRACK incomplete_commands=0
FIXTURE /tmp/edit-corpus/oneline_1g.txt bytes=1073741824(M)[AC] load1=3.63 bulk=one-active source=plain-indexed initial=certified
BENCH view_oneline_1g.txt_document_bulk1_command mode=TRACK n=31 p50=332 p99=602 ns (M)[AC] load1=3.63 necessary_gate=5000000/5555555 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_oneline_1g.txt_document_bulk1_initial mode=TRACK n=31 p50=300 p99=550 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
SLICE_MAX document 550 ns(M)[AC] load1=3.63 hard_limit=500000 ns(G) verdict=TRACK incomplete_commands=0
FIXTURE /tmp/edit-corpus/oneline_1g.txt bytes=1073741824(M)[AC] load1=3.63 bulk=one-active source=plain-indexed initial=certified
BENCH view_oneline_1g.txt_selection_bulk1_command mode=TRACK n=31 p50=462 p99=1702 ns (M)[AC] load1=3.63 necessary_gate=5000000/5555555 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_oneline_1g.txt_selection_bulk1_initial mode=TRACK n=31 p50=431 p99=1665 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
SLICE_MAX selection 1665 ns(M)[AC] load1=3.63 hard_limit=500000 ns(G) verdict=TRACK incomplete_commands=0
FIXTURE /tmp/edit-corpus/oneline_1g.txt bytes=1073741824(M)[AC] load1=3.63 bulk=one-active source=plain-indexed initial=certified
BENCH view_oneline_1g.txt_edit_bulk1_command mode=TRACK n=31 p50=2251 p99=6029 ns (M)[AC] load1=3.63 necessary_gate=1000000/2000000 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_oneline_1g.txt_edit_bulk1_initial mode=TRACK n=31 p50=2209 p99=6008 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
SLICE_MAX edit 6008 ns(M)[AC] load1=3.63 hard_limit=500000 ns(G) verdict=TRACK incomplete_commands=0
FIXTURE /tmp/edit-corpus/oneline_1g.txt bytes=1073741824(M)[AC] load1=3.63 bulk=one-active source=plain-indexed initial=certified
BENCH view_oneline_1g.txt_deep_type_bulk1_command mode=TRACK n=31 p50=2598 p99=4229 ns (M)[AC] load1=3.63 necessary_gate=1000000/2000000 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_oneline_1g.txt_deep_type_bulk1_initial mode=TRACK n=31 p50=2566 p99=4196 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
SLICE_MAX deep_type 4196 ns(M)[AC] load1=3.63 hard_limit=500000 ns(G) verdict=TRACK incomplete_commands=0
FIXTURE /tmp/edit-corpus/oneline_1g.txt bytes=1073741824(M)[AC] load1=3.63 bulk=one-active source=plain-indexed initial=certified
BENCH view_oneline_1g.txt_selection_1MiB_replace_bulk1_command mode=TRACK n=31 p50=599830 p99=652905 ns (M)[AC] load1=3.63 necessary_gate=1000000/2000000 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_oneline_1g.txt_selection_1MiB_replace_bulk1_initial mode=TRACK n=31 p50=71 p99=142 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_oneline_1g.txt_selection_1MiB_replace_bulk1_continue mode=TRACK n=15903 p50=1150 p99=2375 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
SLICE_MAX selection_1MiB_replace 23437 ns(M)[AC] load1=3.63 hard_limit=500000 ns(G) verdict=TRACK incomplete_commands=0
FIXTURE /tmp/edit-corpus/oneline_1g.txt bytes=1073741824(M)[AC] load1=3.63 bulk=one-active source=plain-indexed initial=certified
BENCH view_oneline_1g.txt_unindexed_type_bulk1_command mode=TRACK n=31 p50=2492 p99=4755 ns (M)[AC] load1=3.63 necessary_gate=1000000/2000000 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_oneline_1g.txt_unindexed_type_bulk1_initial mode=TRACK n=31 p50=2447 p99=4717 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
SLICE_MAX unindexed_type 4717 ns(M)[AC] load1=3.63 hard_limit=500000 ns(G) verdict=TRACK incomplete_commands=0
FIXTURE /tmp/edit-corpus/oneline_1g.txt bytes=1073741824(M)[AC] load1=3.63 bulk=three-submitted source=plain-indexed initial=certified
BENCH view_oneline_1g.txt_char_bulk3_command mode=TRACK n=31 p50=493 p99=1263 ns (M)[AC] load1=3.63 necessary_gate=5000000/5555555 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_oneline_1g.txt_char_bulk3_initial mode=TRACK n=31 p50=461 p99=1231 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
SLICE_MAX char 1231 ns(M)[AC] load1=3.63 hard_limit=500000 ns(G) verdict=TRACK incomplete_commands=0
FIXTURE /tmp/edit-corpus/oneline_1g.txt bytes=1073741824(M)[AC] load1=3.63 bulk=three-submitted source=plain-indexed initial=certified
BENCH view_oneline_1g.txt_word_bulk3_command mode=TRACK n=31 p50=605 p99=1349 ns (M)[AC] load1=3.63 necessary_gate=5000000/5555555 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_oneline_1g.txt_word_bulk3_initial mode=TRACK n=31 p50=569 p99=1318 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
SLICE_MAX word 1318 ns(M)[AC] load1=3.63 hard_limit=500000 ns(G) verdict=TRACK incomplete_commands=0
FIXTURE /tmp/edit-corpus/oneline_1g.txt bytes=1073741824(M)[AC] load1=3.63 bulk=three-submitted source=plain-indexed initial=certified
BENCH view_oneline_1g.txt_vertical_bulk3_command mode=TRACK n=31 p50=535 p99=1579 ns (M)[AC] load1=3.63 necessary_gate=5000000/5555555 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_oneline_1g.txt_vertical_bulk3_initial mode=TRACK n=31 p50=503 p99=1525 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
SLICE_MAX vertical 1525 ns(M)[AC] load1=3.63 hard_limit=500000 ns(G) verdict=TRACK incomplete_commands=0
FIXTURE /tmp/edit-corpus/oneline_1g.txt bytes=1073741824(M)[AC] load1=3.63 bulk=three-submitted source=plain-indexed initial=certified
BENCH view_oneline_1g.txt_page_bulk3_command mode=TRACK n=31 p50=545 p99=818 ns (M)[AC] load1=3.63 necessary_gate=5000000/5555555 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_oneline_1g.txt_page_bulk3_initial mode=TRACK n=31 p50=514 p99=766 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
SLICE_MAX page 766 ns(M)[AC] load1=3.63 hard_limit=500000 ns(G) verdict=TRACK incomplete_commands=0
FIXTURE /tmp/edit-corpus/oneline_1g.txt bytes=1073741824(M)[AC] load1=3.63 bulk=three-submitted source=plain-indexed initial=certified
BENCH view_oneline_1g.txt_home_end_bulk3_command mode=TRACK n=31 p50=500 p99=1078 ns (M)[AC] load1=3.63 necessary_gate=5000000/5555555 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_oneline_1g.txt_home_end_bulk3_initial mode=TRACK n=31 p50=468 p99=1022 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
SLICE_MAX home_end 1022 ns(M)[AC] load1=3.63 hard_limit=500000 ns(G) verdict=TRACK incomplete_commands=0
FIXTURE /tmp/edit-corpus/oneline_1g.txt bytes=1073741824(M)[AC] load1=3.63 bulk=three-submitted source=plain-indexed initial=certified
BENCH view_oneline_1g.txt_document_bulk3_command mode=TRACK n=31 p50=299 p99=603 ns (M)[AC] load1=3.63 necessary_gate=5000000/5555555 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_oneline_1g.txt_document_bulk3_initial mode=TRACK n=31 p50=266 p99=545 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
SLICE_MAX document 545 ns(M)[AC] load1=3.63 hard_limit=500000 ns(G) verdict=TRACK incomplete_commands=0
FIXTURE /tmp/edit-corpus/oneline_1g.txt bytes=1073741824(M)[AC] load1=3.63 bulk=three-submitted source=plain-indexed initial=certified
BENCH view_oneline_1g.txt_selection_bulk3_command mode=TRACK n=31 p50=484 p99=1677 ns (M)[AC] load1=3.63 necessary_gate=5000000/5555555 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_oneline_1g.txt_selection_bulk3_initial mode=TRACK n=31 p50=443 p99=1640 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
SLICE_MAX selection 1640 ns(M)[AC] load1=3.63 hard_limit=500000 ns(G) verdict=TRACK incomplete_commands=0
FIXTURE /tmp/edit-corpus/oneline_1g.txt bytes=1073741824(M)[AC] load1=3.63 bulk=three-submitted source=plain-indexed initial=certified
BENCH view_oneline_1g.txt_edit_bulk3_command mode=TRACK n=31 p50=2264 p99=6377 ns (M)[AC] load1=3.63 necessary_gate=1000000/2000000 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_oneline_1g.txt_edit_bulk3_initial mode=TRACK n=31 p50=2219 p99=6337 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
SLICE_MAX edit 6337 ns(M)[AC] load1=3.63 hard_limit=500000 ns(G) verdict=TRACK incomplete_commands=0
FIXTURE /tmp/edit-corpus/oneline_1g.txt bytes=1073741824(M)[AC] load1=3.63 bulk=three-submitted source=plain-indexed initial=certified
BENCH view_oneline_1g.txt_deep_type_bulk3_command mode=TRACK n=31 p50=2654 p99=3743 ns (M)[AC] load1=3.63 necessary_gate=1000000/2000000 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_oneline_1g.txt_deep_type_bulk3_initial mode=TRACK n=31 p50=2621 p99=3714 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
SLICE_MAX deep_type 3714 ns(M)[AC] load1=3.63 hard_limit=500000 ns(G) verdict=TRACK incomplete_commands=0
FIXTURE /tmp/edit-corpus/oneline_1g.txt bytes=1073741824(M)[AC] load1=3.63 bulk=three-submitted source=plain-indexed initial=certified
BENCH view_oneline_1g.txt_selection_1MiB_replace_bulk3_command mode=TRACK n=31 p50=604888 p99=661592 ns (M)[AC] load1=3.63 necessary_gate=1000000/2000000 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_oneline_1g.txt_selection_1MiB_replace_bulk3_initial mode=TRACK n=31 p50=74 p99=273 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_oneline_1g.txt_selection_1MiB_replace_bulk3_continue mode=TRACK n=15903 p50=1162 p99=2488 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
SLICE_MAX selection_1MiB_replace 20643 ns(M)[AC] load1=3.63 hard_limit=500000 ns(G) verdict=TRACK incomplete_commands=0
FIXTURE /tmp/edit-corpus/oneline_1g.txt bytes=1073741824(M)[AC] load1=3.63 bulk=three-submitted source=plain-indexed initial=certified
BENCH view_oneline_1g.txt_unindexed_type_bulk3_command mode=TRACK n=31 p50=2498 p99=4833 ns (M)[AC] load1=3.63 necessary_gate=1000000/2000000 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_oneline_1g.txt_unindexed_type_bulk3_initial mode=TRACK n=31 p50=2454 p99=4811 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
SLICE_MAX unindexed_type 4811 ns(M)[AC] load1=3.63 hard_limit=500000 ns(G) verdict=TRACK incomplete_commands=0
ALLOCATIONS /tmp/edit-corpus/oneline_1g.txt mallocs=0(M)[AC] load1=3.63 gate=0(G) guard=active pass=1
FIXTURE /tmp/edit-corpus/unicode.txt bytes=1048576(M)[AC] load1=3.63 bulk=one-active source=mapped initial=certified
BENCH view_unicode.txt_char_bulk1_command mode=TRACK n=31 p50=582 p99=3112 ns (M)[AC] load1=3.63 necessary_gate=5000000/5555555 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_unicode.txt_char_bulk1_initial mode=TRACK n=31 p50=563 p99=2957 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
SLICE_MAX char 2957 ns(M)[AC] load1=3.63 hard_limit=500000 ns(G) verdict=TRACK incomplete_commands=0
FIXTURE /tmp/edit-corpus/unicode.txt bytes=1048576(M)[AC] load1=3.63 bulk=one-active source=mapped initial=certified
BENCH view_unicode.txt_word_bulk1_command mode=TRACK n=31 p50=876 p99=2292 ns (M)[AC] load1=3.63 necessary_gate=5000000/5555555 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_unicode.txt_word_bulk1_initial mode=TRACK n=31 p50=857 p99=2271 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
SLICE_MAX word 2271 ns(M)[AC] load1=3.63 hard_limit=500000 ns(G) verdict=TRACK incomplete_commands=0
FIXTURE /tmp/edit-corpus/unicode.txt bytes=1048576(M)[AC] load1=3.63 bulk=one-active source=mapped initial=certified
BENCH view_unicode.txt_vertical_bulk1_command mode=TRACK n=31 p50=891 p99=3639 ns (M)[AC] load1=3.63 necessary_gate=5000000/5555555 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_unicode.txt_vertical_bulk1_initial mode=TRACK n=31 p50=857 p99=3614 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
SLICE_MAX vertical 3614 ns(M)[AC] load1=3.63 hard_limit=500000 ns(G) verdict=TRACK incomplete_commands=0
FIXTURE /tmp/edit-corpus/unicode.txt bytes=1048576(M)[AC] load1=3.63 bulk=one-active source=mapped initial=certified
BENCH view_unicode.txt_page_bulk1_command mode=TRACK n=31 p50=705 p99=2932 ns (M)[AC] load1=3.63 necessary_gate=5000000/5555555 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_unicode.txt_page_bulk1_initial mode=TRACK n=31 p50=671 p99=2913 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
SLICE_MAX page 2913 ns(M)[AC] load1=3.63 hard_limit=500000 ns(G) verdict=TRACK incomplete_commands=0
FIXTURE /tmp/edit-corpus/unicode.txt bytes=1048576(M)[AC] load1=3.63 bulk=one-active source=mapped initial=certified
BENCH view_unicode.txt_home_end_bulk1_command mode=TRACK n=31 p50=746 p99=5098 ns (M)[AC] load1=3.63 necessary_gate=5000000/5555555 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_unicode.txt_home_end_bulk1_initial mode=TRACK n=31 p50=714 p99=5076 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
SLICE_MAX home_end 5076 ns(M)[AC] load1=3.63 hard_limit=500000 ns(G) verdict=TRACK incomplete_commands=0
FIXTURE /tmp/edit-corpus/unicode.txt bytes=1048576(M)[AC] load1=3.63 bulk=one-active source=mapped initial=certified
BENCH view_unicode.txt_document_bulk1_command mode=TRACK n=31 p50=280 p99=959183 ns (M)[AC] load1=3.63 necessary_gate=5000000/5555555 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_unicode.txt_document_bulk1_initial mode=TRACK n=31 p50=263 p99=3886 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_unicode.txt_document_bulk1_continue mode=TRACK n=4095 p50=3134 p99=3592 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
SLICE_MAX document 61594 ns(M)[AC] load1=3.63 hard_limit=500000 ns(G) verdict=TRACK incomplete_commands=0
FIXTURE /tmp/edit-corpus/unicode.txt bytes=1048576(M)[AC] load1=3.63 bulk=one-active source=mapped initial=certified
BENCH view_unicode.txt_selection_bulk1_command mode=TRACK n=31 p50=1178 p99=864457 ns (M)[AC] load1=3.63 necessary_gate=5000000/5555555 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_unicode.txt_selection_bulk1_initial mode=TRACK n=31 p50=1158 p99=3273 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_unicode.txt_selection_bulk1_continue mode=TRACK n=3003 p50=3092 p99=3310 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
SLICE_MAX selection 8913 ns(M)[AC] load1=3.63 hard_limit=500000 ns(G) verdict=TRACK incomplete_commands=0
FIXTURE /tmp/edit-corpus/unicode.txt bytes=1048576(M)[AC] load1=3.63 bulk=one-active source=mapped initial=certified
BENCH view_unicode.txt_edit_bulk1_command mode=TRACK n=31 p50=958 p99=3910 ns (M)[AC] load1=3.63 necessary_gate=1000000/2000000 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_unicode.txt_edit_bulk1_initial mode=TRACK n=31 p50=940 p99=3881 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
SLICE_MAX edit 3881 ns(M)[AC] load1=3.63 hard_limit=500000 ns(G) verdict=TRACK incomplete_commands=0
FIXTURE /tmp/edit-corpus/unicode.txt bytes=1048576(M)[AC] load1=3.63 bulk=one-active source=mapped initial=certified
BENCH view_unicode.txt_deep_type_bulk1_command mode=TRACK n=31 p50=1146 p99=2543 ns (M)[AC] load1=3.63 necessary_gate=1000000/2000000 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_unicode.txt_deep_type_bulk1_initial mode=TRACK n=31 p50=1115 p99=2500 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
SLICE_MAX deep_type 2500 ns(M)[AC] load1=3.63 hard_limit=500000 ns(G) verdict=TRACK incomplete_commands=0
FIXTURE /tmp/edit-corpus/unicode.txt bytes=1048576(M)[AC] load1=3.63 bulk=one-active source=mapped initial=certified
BENCH view_unicode.txt_selection_1MiB_replace_bulk1_command mode=TRACK n=31 p50=506085 p99=621772 ns (M)[AC] load1=3.63 necessary_gate=1000000/2000000 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_unicode.txt_selection_1MiB_replace_bulk1_initial mode=TRACK n=31 p50=71 p99=359 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_unicode.txt_selection_1MiB_replace_bulk1_continue mode=TRACK n=15903 p50=1015 p99=2224 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
SLICE_MAX selection_1MiB_replace 58070 ns(M)[AC] load1=3.63 hard_limit=500000 ns(G) verdict=TRACK incomplete_commands=0
FIXTURE /tmp/edit-corpus/unicode.txt bytes=1048576(M)[AC] load1=3.63 bulk=one-active source=mapped initial=certified
BENCH view_unicode.txt_unindexed_type_bulk1_command mode=TRACK n=31 p50=603 p99=2391 ns (M)[AC] load1=3.63 necessary_gate=1000000/2000000 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_unicode.txt_unindexed_type_bulk1_initial mode=TRACK n=31 p50=586 p99=2345 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
SLICE_MAX unindexed_type 2345 ns(M)[AC] load1=3.63 hard_limit=500000 ns(G) verdict=TRACK incomplete_commands=0
FIXTURE /tmp/edit-corpus/unicode.txt bytes=1048576(M)[AC] load1=3.63 bulk=three-submitted source=mapped initial=certified
BENCH view_unicode.txt_char_bulk3_command mode=TRACK n=31 p50=478 p99=2122 ns (M)[AC] load1=3.63 necessary_gate=5000000/5555555 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_unicode.txt_char_bulk3_initial mode=TRACK n=31 p50=463 p99=2102 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
SLICE_MAX char 2102 ns(M)[AC] load1=3.63 hard_limit=500000 ns(G) verdict=TRACK incomplete_commands=0
FIXTURE /tmp/edit-corpus/unicode.txt bytes=1048576(M)[AC] load1=3.63 bulk=three-submitted source=mapped initial=certified
BENCH view_unicode.txt_word_bulk3_command mode=TRACK n=31 p50=741 p99=2925 ns (M)[AC] load1=3.63 necessary_gate=5000000/5555555 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_unicode.txt_word_bulk3_initial mode=TRACK n=31 p50=726 p99=2908 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
SLICE_MAX word 2908 ns(M)[AC] load1=3.63 hard_limit=500000 ns(G) verdict=TRACK incomplete_commands=0
FIXTURE /tmp/edit-corpus/unicode.txt bytes=1048576(M)[AC] load1=3.63 bulk=three-submitted source=mapped initial=certified
BENCH view_unicode.txt_vertical_bulk3_command mode=TRACK n=31 p50=819 p99=4085 ns (M)[AC] load1=3.63 necessary_gate=5000000/5555555 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_unicode.txt_vertical_bulk3_initial mode=TRACK n=31 p50=799 p99=4056 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
SLICE_MAX vertical 4056 ns(M)[AC] load1=3.63 hard_limit=500000 ns(G) verdict=TRACK incomplete_commands=0
FIXTURE /tmp/edit-corpus/unicode.txt bytes=1048576(M)[AC] load1=3.63 bulk=three-submitted source=mapped initial=certified
BENCH view_unicode.txt_page_bulk3_command mode=TRACK n=31 p50=486 p99=2133 ns (M)[AC] load1=3.63 necessary_gate=5000000/5555555 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_unicode.txt_page_bulk3_initial mode=TRACK n=31 p50=459 p99=2103 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
SLICE_MAX page 2103 ns(M)[AC] load1=3.63 hard_limit=500000 ns(G) verdict=TRACK incomplete_commands=0
FIXTURE /tmp/edit-corpus/unicode.txt bytes=1048576(M)[AC] load1=3.63 bulk=three-submitted source=mapped initial=certified
BENCH view_unicode.txt_home_end_bulk3_command mode=TRACK n=31 p50=537 p99=4786 ns (M)[AC] load1=3.63 necessary_gate=5000000/5555555 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_unicode.txt_home_end_bulk3_initial mode=TRACK n=31 p50=515 p99=4769 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
SLICE_MAX home_end 4769 ns(M)[AC] load1=3.63 hard_limit=500000 ns(G) verdict=TRACK incomplete_commands=0
FIXTURE /tmp/edit-corpus/unicode.txt bytes=1048576(M)[AC] load1=3.63 bulk=three-submitted source=mapped initial=certified
BENCH view_unicode.txt_document_bulk3_command mode=TRACK n=31 p50=353 p99=925894 ns (M)[AC] load1=3.63 necessary_gate=5000000/5555555 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_unicode.txt_document_bulk3_initial mode=TRACK n=31 p50=334 p99=3656 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_unicode.txt_document_bulk3_continue mode=TRACK n=4095 p50=3087 p99=3319 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
SLICE_MAX document 73497 ns(M)[AC] load1=3.63 hard_limit=500000 ns(G) verdict=TRACK incomplete_commands=0
FIXTURE /tmp/edit-corpus/unicode.txt bytes=1048576(M)[AC] load1=3.63 bulk=three-submitted source=mapped initial=certified
BENCH view_unicode.txt_selection_bulk3_command mode=TRACK n=31 p50=1291 p99=867938 ns (M)[AC] load1=3.63 necessary_gate=5000000/5555555 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_unicode.txt_selection_bulk3_initial mode=TRACK n=31 p50=1271 p99=3622 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_unicode.txt_selection_bulk3_continue mode=TRACK n=3003 p50=3091 p99=3347 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
SLICE_MAX selection 8022 ns(M)[AC] load1=3.63 hard_limit=500000 ns(G) verdict=TRACK incomplete_commands=0
FIXTURE /tmp/edit-corpus/unicode.txt bytes=1048576(M)[AC] load1=3.63 bulk=three-submitted source=mapped initial=certified
BENCH view_unicode.txt_edit_bulk3_command mode=TRACK n=31 p50=975 p99=3097 ns (M)[AC] load1=3.63 necessary_gate=1000000/2000000 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_unicode.txt_edit_bulk3_initial mode=TRACK n=31 p50=956 p99=3062 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
SLICE_MAX edit 3062 ns(M)[AC] load1=3.63 hard_limit=500000 ns(G) verdict=TRACK incomplete_commands=0
FIXTURE /tmp/edit-corpus/unicode.txt bytes=1048576(M)[AC] load1=3.63 bulk=three-submitted source=mapped initial=certified
BENCH view_unicode.txt_deep_type_bulk3_command mode=TRACK n=31 p50=1140 p99=2677 ns (M)[AC] load1=3.63 necessary_gate=1000000/2000000 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_unicode.txt_deep_type_bulk3_initial mode=TRACK n=31 p50=1109 p99=2632 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
SLICE_MAX deep_type 2632 ns(M)[AC] load1=3.63 hard_limit=500000 ns(G) verdict=TRACK incomplete_commands=0
FIXTURE /tmp/edit-corpus/unicode.txt bytes=1048576(M)[AC] load1=3.63 bulk=three-submitted source=mapped initial=certified
BENCH view_unicode.txt_selection_1MiB_replace_bulk3_command mode=TRACK n=31 p50=500089 p99=596045 ns (M)[AC] load1=3.63 necessary_gate=1000000/2000000 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_unicode.txt_selection_1MiB_replace_bulk3_initial mode=TRACK n=31 p50=70 p99=230 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_unicode.txt_selection_1MiB_replace_bulk3_continue mode=TRACK n=15903 p50=692 p99=2134 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
SLICE_MAX selection_1MiB_replace 14612 ns(M)[AC] load1=3.63 hard_limit=500000 ns(G) verdict=TRACK incomplete_commands=0
FIXTURE /tmp/edit-corpus/unicode.txt bytes=1048576(M)[AC] load1=3.63 bulk=three-submitted source=mapped initial=certified
BENCH view_unicode.txt_unindexed_type_bulk3_command mode=TRACK n=31 p50=618 p99=1796 ns (M)[AC] load1=3.63 necessary_gate=1000000/2000000 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_unicode.txt_unindexed_type_bulk3_initial mode=TRACK n=31 p50=599 p99=1757 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
SLICE_MAX unindexed_type 1757 ns(M)[AC] load1=3.63 hard_limit=500000 ns(G) verdict=TRACK incomplete_commands=0
ALLOCATIONS /tmp/edit-corpus/unicode.txt mallocs=0(M)[AC] load1=3.63 gate=0(G) guard=active pass=1
AFTER utc=2026-10-10T16:11:38.032540+00:00 power=Not charging loadavg=3.63 1.91 1.48 1/1346 26 (M)[AC]
EXIT 0 elapsed_s=1.116978822996316 (M)[AC]
```

### view, adjacent run 3

```text
COMMAND build/bench/view_bench args_file=absent; defaults
BEFORE utc=2026-10-10T16:11:38.032947+00:00 power=Not charging loadavg=3.63 1.91 1.48 1/1346 26 (M)[AC]
POWER Not charging [AC] load1=3.63 mode=TRACK; module-only necessary limits, 50 us remains (E)
FIXTURE /tmp/edit-corpus/log_1g.txt bytes=1073741824(M)[AC] load1=3.63 bulk=one-active source=mapped initial=certified
BENCH view_log_1g.txt_char_bulk1_command mode=TRACK n=31 p50=1330 p99=6973 ns (M)[AC] load1=3.63 necessary_gate=5000000/5555555 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_log_1g.txt_char_bulk1_initial mode=TRACK n=31 p50=1268 p99=6920 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
SLICE_MAX char 6920 ns(M)[AC] load1=3.63 hard_limit=500000 ns(G) verdict=TRACK incomplete_commands=0
FIXTURE /tmp/edit-corpus/log_1g.txt bytes=1073741824(M)[AC] load1=3.63 bulk=one-active source=mapped initial=certified
BENCH view_log_1g.txt_word_bulk1_command mode=TRACK n=31 p50=1188 p99=3297 ns (M)[AC] load1=3.63 necessary_gate=5000000/5555555 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_log_1g.txt_word_bulk1_initial mode=TRACK n=31 p50=1123 p99=3137 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
SLICE_MAX word 3137 ns(M)[AC] load1=3.63 hard_limit=500000 ns(G) verdict=TRACK incomplete_commands=0
FIXTURE /tmp/edit-corpus/log_1g.txt bytes=1073741824(M)[AC] load1=3.63 bulk=one-active source=mapped initial=certified
BENCH view_log_1g.txt_vertical_bulk1_command mode=TRACK n=31 p50=856 p99=1669 ns (M)[AC] load1=3.63 necessary_gate=5000000/5555555 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_log_1g.txt_vertical_bulk1_initial mode=TRACK n=31 p50=826 p99=1636 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
SLICE_MAX vertical 1636 ns(M)[AC] load1=3.63 hard_limit=500000 ns(G) verdict=TRACK incomplete_commands=0
FIXTURE /tmp/edit-corpus/log_1g.txt bytes=1073741824(M)[AC] load1=3.63 bulk=one-active source=mapped initial=certified
BENCH view_log_1g.txt_page_bulk1_command mode=TRACK n=31 p50=692 p99=3167 ns (M)[AC] load1=3.63 necessary_gate=5000000/5555555 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_log_1g.txt_page_bulk1_initial mode=TRACK n=31 p50=654 p99=3123 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
SLICE_MAX page 3123 ns(M)[AC] load1=3.63 hard_limit=500000 ns(G) verdict=TRACK incomplete_commands=0
FIXTURE /tmp/edit-corpus/log_1g.txt bytes=1073741824(M)[AC] load1=3.63 bulk=one-active source=mapped initial=certified
BENCH view_log_1g.txt_home_end_bulk1_command mode=TRACK n=31 p50=637 p99=4202 ns (M)[AC] load1=3.63 necessary_gate=5000000/5555555 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_log_1g.txt_home_end_bulk1_initial mode=TRACK n=31 p50=608 p99=4158 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
SLICE_MAX home_end 4158 ns(M)[AC] load1=3.63 hard_limit=500000 ns(G) verdict=TRACK incomplete_commands=0
FIXTURE /tmp/edit-corpus/log_1g.txt bytes=1073741824(M)[AC] load1=3.63 bulk=one-active source=mapped initial=certified
BENCH view_log_1g.txt_document_bulk1_command mode=TRACK n=31 p50=570 p99=2567291 ns (M)[AC] load1=3.63 necessary_gate=5000000/5555555 ns(G) verdict=TRACK incomplete=1 dropped=0
BENCH view_log_1g.txt_document_bulk1_initial mode=TRACK n=31 p50=455 p99=2769 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_log_1g.txt_document_bulk1_continue mode=TRACK n=15360 p50=2341 p99=2549 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
SLICE_MAX document 31942 ns(M)[AC] load1=3.63 hard_limit=500000 ns(G) verdict=TRACK incomplete_commands=15
FIXTURE /tmp/edit-corpus/log_1g.txt bytes=1073741824(M)[AC] load1=3.63 bulk=one-active source=mapped initial=certified
BENCH view_log_1g.txt_selection_bulk1_command mode=TRACK n=31 p50=1086 p99=2451906 ns (M)[AC] load1=3.63 necessary_gate=5000000/5555555 ns(G) verdict=TRACK incomplete=1 dropped=0
BENCH view_log_1g.txt_selection_bulk1_initial mode=TRACK n=31 p50=1055 p99=2770 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_log_1g.txt_selection_bulk1_continue mode=TRACK n=11264 p50=2337 p99=2492 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
SLICE_MAX selection 16525 ns(M)[AC] load1=3.63 hard_limit=500000 ns(G) verdict=TRACK incomplete_commands=11
FIXTURE /tmp/edit-corpus/log_1g.txt bytes=1073741824(M)[AC] load1=3.63 bulk=one-active source=mapped initial=certified
BENCH view_log_1g.txt_edit_bulk1_command mode=TRACK n=31 p50=2272 p99=6573 ns (M)[AC] load1=3.63 necessary_gate=1000000/2000000 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_log_1g.txt_edit_bulk1_initial mode=TRACK n=31 p50=2229 p99=6526 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
SLICE_MAX edit 6526 ns(M)[AC] load1=3.63 hard_limit=500000 ns(G) verdict=TRACK incomplete_commands=0
FIXTURE /tmp/edit-corpus/log_1g.txt bytes=1073741824(M)[AC] load1=3.63 bulk=one-active source=mapped initial=certified
SETUP_LIMIT deep_type skipped=31(M)[AC] load1=3.63 mode=TRACK verdict=TRACK setup_cap=100000000 ns(G)/1024 calls(G)
FIXTURE /tmp/edit-corpus/log_1g.txt bytes=1073741824(M)[AC] load1=3.63 bulk=one-active source=mapped initial=certified
SETUP_LIMIT selection_1MiB_replace skipped=31(M)[AC] load1=3.63 mode=TRACK verdict=TRACK setup_cap=100000000 ns(G)/1024 calls(G)
FIXTURE /tmp/edit-corpus/log_1g.txt bytes=1073741824(M)[AC] load1=3.63 bulk=one-active source=mapped initial=certified
BENCH view_log_1g.txt_unindexed_type_bulk1_command mode=TRACK n=31 p50=2553 p99=6303 ns (M)[AC] load1=3.63 necessary_gate=1000000/2000000 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_log_1g.txt_unindexed_type_bulk1_initial mode=TRACK n=31 p50=2509 p99=6213 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
SLICE_MAX unindexed_type 6213 ns(M)[AC] load1=3.63 hard_limit=500000 ns(G) verdict=TRACK incomplete_commands=0
FIXTURE /tmp/edit-corpus/log_1g.txt bytes=1073741824(M)[AC] load1=3.63 bulk=three-submitted source=mapped initial=certified
BENCH view_log_1g.txt_char_bulk3_command mode=TRACK n=31 p50=536 p99=1280 ns (M)[AC] load1=3.63 necessary_gate=5000000/5555555 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_log_1g.txt_char_bulk3_initial mode=TRACK n=31 p50=505 p99=1251 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
SLICE_MAX char 1251 ns(M)[AC] load1=3.63 hard_limit=500000 ns(G) verdict=TRACK incomplete_commands=0
FIXTURE /tmp/edit-corpus/log_1g.txt bytes=1073741824(M)[AC] load1=3.63 bulk=three-submitted source=mapped initial=certified
BENCH view_log_1g.txt_word_bulk3_command mode=TRACK n=31 p50=655 p99=1200 ns (M)[AC] load1=3.63 necessary_gate=5000000/5555555 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_log_1g.txt_word_bulk3_initial mode=TRACK n=31 p50=622 p99=1165 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
SLICE_MAX word 1165 ns(M)[AC] load1=3.63 hard_limit=500000 ns(G) verdict=TRACK incomplete_commands=0
FIXTURE /tmp/edit-corpus/log_1g.txt bytes=1073741824(M)[AC] load1=3.63 bulk=three-submitted source=mapped initial=certified
BENCH view_log_1g.txt_vertical_bulk3_command mode=TRACK n=31 p50=935 p99=2110 ns (M)[AC] load1=3.63 necessary_gate=5000000/5555555 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_log_1g.txt_vertical_bulk3_initial mode=TRACK n=31 p50=901 p99=2066 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
SLICE_MAX vertical 2066 ns(M)[AC] load1=3.63 hard_limit=500000 ns(G) verdict=TRACK incomplete_commands=0
FIXTURE /tmp/edit-corpus/log_1g.txt bytes=1073741824(M)[AC] load1=3.63 bulk=three-submitted source=mapped initial=certified
BENCH view_log_1g.txt_page_bulk3_command mode=TRACK n=31 p50=680 p99=3345 ns (M)[AC] load1=3.63 necessary_gate=5000000/5555555 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_log_1g.txt_page_bulk3_initial mode=TRACK n=31 p50=635 p99=3302 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
SLICE_MAX page 3302 ns(M)[AC] load1=3.63 hard_limit=500000 ns(G) verdict=TRACK incomplete_commands=0
FIXTURE /tmp/edit-corpus/log_1g.txt bytes=1073741824(M)[AC] load1=3.63 bulk=three-submitted source=mapped initial=certified
BENCH view_log_1g.txt_home_end_bulk3_command mode=TRACK n=31 p50=894 p99=4445 ns (M)[AC] load1=3.63 necessary_gate=5000000/5555555 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_log_1g.txt_home_end_bulk3_initial mode=TRACK n=31 p50=875 p99=4403 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
SLICE_MAX home_end 4403 ns(M)[AC] load1=3.63 hard_limit=500000 ns(G) verdict=TRACK incomplete_commands=0
FIXTURE /tmp/edit-corpus/log_1g.txt bytes=1073741824(M)[AC] load1=3.63 bulk=three-submitted source=mapped initial=certified
BENCH view_log_1g.txt_document_bulk3_command mode=TRACK n=31 p50=317 p99=2453957 ns (M)[AC] load1=3.63 necessary_gate=5000000/5555555 ns(G) verdict=TRACK incomplete=1 dropped=0
BENCH view_log_1g.txt_document_bulk3_initial mode=TRACK n=31 p50=296 p99=2820 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_log_1g.txt_document_bulk3_continue mode=TRACK n=15360 p50=2338 p99=2498 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
SLICE_MAX document 28118 ns(M)[AC] load1=3.63 hard_limit=500000 ns(G) verdict=TRACK incomplete_commands=15
FIXTURE /tmp/edit-corpus/log_1g.txt bytes=1073741824(M)[AC] load1=3.63 bulk=three-submitted source=mapped initial=certified
BENCH view_log_1g.txt_selection_bulk3_command mode=TRACK n=31 p50=1101 p99=2441662 ns (M)[AC] load1=3.63 necessary_gate=5000000/5555555 ns(G) verdict=TRACK incomplete=1 dropped=0
BENCH view_log_1g.txt_selection_bulk3_initial mode=TRACK n=31 p50=1069 p99=2914 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_log_1g.txt_selection_bulk3_continue mode=TRACK n=11264 p50=2344 p99=2486 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
SLICE_MAX selection 11864 ns(M)[AC] load1=3.63 hard_limit=500000 ns(G) verdict=TRACK incomplete_commands=11
FIXTURE /tmp/edit-corpus/log_1g.txt bytes=1073741824(M)[AC] load1=3.63 bulk=three-submitted source=mapped initial=certified
BENCH view_log_1g.txt_edit_bulk3_command mode=TRACK n=31 p50=2286 p99=6589 ns (M)[AC] load1=3.63 necessary_gate=1000000/2000000 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_log_1g.txt_edit_bulk3_initial mode=TRACK n=31 p50=2244 p99=6559 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
SLICE_MAX edit 6559 ns(M)[AC] load1=3.63 hard_limit=500000 ns(G) verdict=TRACK incomplete_commands=0
FIXTURE /tmp/edit-corpus/log_1g.txt bytes=1073741824(M)[AC] load1=3.63 bulk=three-submitted source=mapped initial=certified
SETUP_LIMIT deep_type skipped=31(M)[AC] load1=3.63 mode=TRACK verdict=TRACK setup_cap=100000000 ns(G)/1024 calls(G)
FIXTURE /tmp/edit-corpus/log_1g.txt bytes=1073741824(M)[AC] load1=3.63 bulk=three-submitted source=mapped initial=certified
SETUP_LIMIT selection_1MiB_replace skipped=31(M)[AC] load1=3.63 mode=TRACK verdict=TRACK setup_cap=100000000 ns(G)/1024 calls(G)
FIXTURE /tmp/edit-corpus/log_1g.txt bytes=1073741824(M)[AC] load1=3.63 bulk=three-submitted source=mapped initial=certified
BENCH view_log_1g.txt_unindexed_type_bulk3_command mode=TRACK n=31 p50=2521 p99=4798 ns (M)[AC] load1=3.63 necessary_gate=1000000/2000000 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_log_1g.txt_unindexed_type_bulk3_initial mode=TRACK n=31 p50=2478 p99=4760 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
SLICE_MAX unindexed_type 4760 ns(M)[AC] load1=3.63 hard_limit=500000 ns(G) verdict=TRACK incomplete_commands=0
ALLOCATIONS /tmp/edit-corpus/log_1g.txt mallocs=0(M)[AC] load1=3.63 gate=0(G) guard=active pass=1
FIXTURE /tmp/edit-corpus/oneline_1g.txt bytes=1073741824(M)[AC] load1=3.63 bulk=one-active source=plain-indexed initial=certified
BENCH view_oneline_1g.txt_char_bulk1_command mode=TRACK n=31 p50=1187 p99=3991 ns (M)[AC] load1=3.63 necessary_gate=5000000/5555555 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_oneline_1g.txt_char_bulk1_initial mode=TRACK n=31 p50=1121 p99=3909 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
SLICE_MAX char 3909 ns(M)[AC] load1=3.63 hard_limit=500000 ns(G) verdict=TRACK incomplete_commands=0
FIXTURE /tmp/edit-corpus/oneline_1g.txt bytes=1073741824(M)[AC] load1=3.63 bulk=one-active source=plain-indexed initial=certified
BENCH view_oneline_1g.txt_word_bulk1_command mode=TRACK n=31 p50=848 p99=4152 ns (M)[AC] load1=3.63 necessary_gate=5000000/5555555 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_oneline_1g.txt_word_bulk1_initial mode=TRACK n=31 p50=807 p99=4074 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
SLICE_MAX word 4074 ns(M)[AC] load1=3.63 hard_limit=500000 ns(G) verdict=TRACK incomplete_commands=0
FIXTURE /tmp/edit-corpus/oneline_1g.txt bytes=1073741824(M)[AC] load1=3.63 bulk=one-active source=plain-indexed initial=certified
BENCH view_oneline_1g.txt_vertical_bulk1_command mode=TRACK n=31 p50=574 p99=1276 ns (M)[AC] load1=3.63 necessary_gate=5000000/5555555 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_oneline_1g.txt_vertical_bulk1_initial mode=TRACK n=31 p50=542 p99=1239 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
SLICE_MAX vertical 1239 ns(M)[AC] load1=3.63 hard_limit=500000 ns(G) verdict=TRACK incomplete_commands=0
FIXTURE /tmp/edit-corpus/oneline_1g.txt bytes=1073741824(M)[AC] load1=3.63 bulk=one-active source=plain-indexed initial=certified
BENCH view_oneline_1g.txt_page_bulk1_command mode=TRACK n=31 p50=564 p99=772 ns (M)[AC] load1=3.63 necessary_gate=5000000/5555555 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_oneline_1g.txt_page_bulk1_initial mode=TRACK n=31 p50=532 p99=729 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
SLICE_MAX page 729 ns(M)[AC] load1=3.63 hard_limit=500000 ns(G) verdict=TRACK incomplete_commands=0
FIXTURE /tmp/edit-corpus/oneline_1g.txt bytes=1073741824(M)[AC] load1=3.63 bulk=one-active source=plain-indexed initial=certified
BENCH view_oneline_1g.txt_home_end_bulk1_command mode=TRACK n=31 p50=513 p99=926 ns (M)[AC] load1=3.63 necessary_gate=5000000/5555555 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_oneline_1g.txt_home_end_bulk1_initial mode=TRACK n=31 p50=481 p99=883 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
SLICE_MAX home_end 883 ns(M)[AC] load1=3.63 hard_limit=500000 ns(G) verdict=TRACK incomplete_commands=0
FIXTURE /tmp/edit-corpus/oneline_1g.txt bytes=1073741824(M)[AC] load1=3.63 bulk=one-active source=plain-indexed initial=certified
BENCH view_oneline_1g.txt_document_bulk1_command mode=TRACK n=31 p50=264 p99=511 ns (M)[AC] load1=3.63 necessary_gate=5000000/5555555 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_oneline_1g.txt_document_bulk1_initial mode=TRACK n=31 p50=241 p99=467 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
SLICE_MAX document 467 ns(M)[AC] load1=3.63 hard_limit=500000 ns(G) verdict=TRACK incomplete_commands=0
FIXTURE /tmp/edit-corpus/oneline_1g.txt bytes=1073741824(M)[AC] load1=3.63 bulk=one-active source=plain-indexed initial=certified
BENCH view_oneline_1g.txt_selection_bulk1_command mode=TRACK n=31 p50=511 p99=1453 ns (M)[AC] load1=3.63 necessary_gate=5000000/5555555 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_oneline_1g.txt_selection_bulk1_initial mode=TRACK n=31 p50=479 p99=1414 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
SLICE_MAX selection 1414 ns(M)[AC] load1=3.63 hard_limit=500000 ns(G) verdict=TRACK incomplete_commands=0
FIXTURE /tmp/edit-corpus/oneline_1g.txt bytes=1073741824(M)[AC] load1=3.63 bulk=one-active source=plain-indexed initial=certified
BENCH view_oneline_1g.txt_edit_bulk1_command mode=TRACK n=31 p50=2306 p99=6246 ns (M)[AC] load1=3.63 necessary_gate=1000000/2000000 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_oneline_1g.txt_edit_bulk1_initial mode=TRACK n=31 p50=2265 p99=6227 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
SLICE_MAX edit 6227 ns(M)[AC] load1=3.63 hard_limit=500000 ns(G) verdict=TRACK incomplete_commands=0
FIXTURE /tmp/edit-corpus/oneline_1g.txt bytes=1073741824(M)[AC] load1=3.63 bulk=one-active source=plain-indexed initial=certified
BENCH view_oneline_1g.txt_deep_type_bulk1_command mode=TRACK n=31 p50=2664 p99=4176 ns (M)[AC] load1=3.63 necessary_gate=1000000/2000000 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_oneline_1g.txt_deep_type_bulk1_initial mode=TRACK n=31 p50=2612 p99=4158 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
SLICE_MAX deep_type 4158 ns(M)[AC] load1=3.63 hard_limit=500000 ns(G) verdict=TRACK incomplete_commands=0
FIXTURE /tmp/edit-corpus/oneline_1g.txt bytes=1073741824(M)[AC] load1=3.63 bulk=one-active source=plain-indexed initial=certified
BENCH view_oneline_1g.txt_selection_1MiB_replace_bulk1_command mode=TRACK n=31 p50=603315 p99=670158 ns (M)[AC] load1=3.63 necessary_gate=1000000/2000000 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_oneline_1g.txt_selection_1MiB_replace_bulk1_initial mode=TRACK n=31 p50=71 p99=192 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_oneline_1g.txt_selection_1MiB_replace_bulk1_continue mode=TRACK n=15903 p50=1283 p99=2378 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
SLICE_MAX selection_1MiB_replace 14538 ns(M)[AC] load1=3.63 hard_limit=500000 ns(G) verdict=TRACK incomplete_commands=0
FIXTURE /tmp/edit-corpus/oneline_1g.txt bytes=1073741824(M)[AC] load1=3.63 bulk=one-active source=plain-indexed initial=certified
BENCH view_oneline_1g.txt_unindexed_type_bulk1_command mode=TRACK n=31 p50=2519 p99=3974 ns (M)[AC] load1=3.63 necessary_gate=1000000/2000000 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_oneline_1g.txt_unindexed_type_bulk1_initial mode=TRACK n=31 p50=2475 p99=3930 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
SLICE_MAX unindexed_type 3930 ns(M)[AC] load1=3.63 hard_limit=500000 ns(G) verdict=TRACK incomplete_commands=0
FIXTURE /tmp/edit-corpus/oneline_1g.txt bytes=1073741824(M)[AC] load1=3.63 bulk=three-submitted source=plain-indexed initial=certified
BENCH view_oneline_1g.txt_char_bulk3_command mode=TRACK n=31 p50=524 p99=1189 ns (M)[AC] load1=3.63 necessary_gate=5000000/5555555 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_oneline_1g.txt_char_bulk3_initial mode=TRACK n=31 p50=493 p99=1169 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
SLICE_MAX char 1169 ns(M)[AC] load1=3.63 hard_limit=500000 ns(G) verdict=TRACK incomplete_commands=0
FIXTURE /tmp/edit-corpus/oneline_1g.txt bytes=1073741824(M)[AC] load1=3.63 bulk=three-submitted source=plain-indexed initial=certified
BENCH view_oneline_1g.txt_word_bulk3_command mode=TRACK n=31 p50=651 p99=1358 ns (M)[AC] load1=3.63 necessary_gate=5000000/5555555 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_oneline_1g.txt_word_bulk3_initial mode=TRACK n=31 p50=618 p99=1325 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
SLICE_MAX word 1325 ns(M)[AC] load1=3.63 hard_limit=500000 ns(G) verdict=TRACK incomplete_commands=0
FIXTURE /tmp/edit-corpus/oneline_1g.txt bytes=1073741824(M)[AC] load1=3.63 bulk=three-submitted source=plain-indexed initial=certified
BENCH view_oneline_1g.txt_vertical_bulk3_command mode=TRACK n=31 p50=578 p99=1199 ns (M)[AC] load1=3.63 necessary_gate=5000000/5555555 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_oneline_1g.txt_vertical_bulk3_initial mode=TRACK n=31 p50=546 p99=1167 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
SLICE_MAX vertical 1167 ns(M)[AC] load1=3.63 hard_limit=500000 ns(G) verdict=TRACK incomplete_commands=0
FIXTURE /tmp/edit-corpus/oneline_1g.txt bytes=1073741824(M)[AC] load1=3.63 bulk=three-submitted source=plain-indexed initial=certified
BENCH view_oneline_1g.txt_page_bulk3_command mode=TRACK n=31 p50=567 p99=771 ns (M)[AC] load1=3.63 necessary_gate=5000000/5555555 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_oneline_1g.txt_page_bulk3_initial mode=TRACK n=31 p50=521 p99=728 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
SLICE_MAX page 728 ns(M)[AC] load1=3.63 hard_limit=500000 ns(G) verdict=TRACK incomplete_commands=0
FIXTURE /tmp/edit-corpus/oneline_1g.txt bytes=1073741824(M)[AC] load1=3.63 bulk=three-submitted source=plain-indexed initial=certified
BENCH view_oneline_1g.txt_home_end_bulk3_command mode=TRACK n=31 p50=530 p99=1317 ns (M)[AC] load1=3.63 necessary_gate=5000000/5555555 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_oneline_1g.txt_home_end_bulk3_initial mode=TRACK n=31 p50=495 p99=1258 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
SLICE_MAX home_end 1258 ns(M)[AC] load1=3.63 hard_limit=500000 ns(G) verdict=TRACK incomplete_commands=0
FIXTURE /tmp/edit-corpus/oneline_1g.txt bytes=1073741824(M)[AC] load1=3.63 bulk=three-submitted source=plain-indexed initial=certified
BENCH view_oneline_1g.txt_document_bulk3_command mode=TRACK n=31 p50=351 p99=564 ns (M)[AC] load1=3.63 necessary_gate=5000000/5555555 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_oneline_1g.txt_document_bulk3_initial mode=TRACK n=31 p50=330 p99=512 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
SLICE_MAX document 512 ns(M)[AC] load1=3.63 hard_limit=500000 ns(G) verdict=TRACK incomplete_commands=0
FIXTURE /tmp/edit-corpus/oneline_1g.txt bytes=1073741824(M)[AC] load1=3.63 bulk=three-submitted source=plain-indexed initial=certified
BENCH view_oneline_1g.txt_selection_bulk3_command mode=TRACK n=31 p50=504 p99=1534 ns (M)[AC] load1=3.63 necessary_gate=5000000/5555555 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_oneline_1g.txt_selection_bulk3_initial mode=TRACK n=31 p50=473 p99=1496 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
SLICE_MAX selection 1496 ns(M)[AC] load1=3.63 hard_limit=500000 ns(G) verdict=TRACK incomplete_commands=0
FIXTURE /tmp/edit-corpus/oneline_1g.txt bytes=1073741824(M)[AC] load1=3.63 bulk=three-submitted source=plain-indexed initial=certified
BENCH view_oneline_1g.txt_edit_bulk3_command mode=TRACK n=31 p50=2290 p99=6039 ns (M)[AC] load1=3.63 necessary_gate=1000000/2000000 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_oneline_1g.txt_edit_bulk3_initial mode=TRACK n=31 p50=2248 p99=5992 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
SLICE_MAX edit 5992 ns(M)[AC] load1=3.63 hard_limit=500000 ns(G) verdict=TRACK incomplete_commands=0
FIXTURE /tmp/edit-corpus/oneline_1g.txt bytes=1073741824(M)[AC] load1=3.63 bulk=three-submitted source=plain-indexed initial=certified
BENCH view_oneline_1g.txt_deep_type_bulk3_command mode=TRACK n=31 p50=2650 p99=3715 ns (M)[AC] load1=3.63 necessary_gate=1000000/2000000 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_oneline_1g.txt_deep_type_bulk3_initial mode=TRACK n=31 p50=2603 p99=3686 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
SLICE_MAX deep_type 3686 ns(M)[AC] load1=3.63 hard_limit=500000 ns(G) verdict=TRACK incomplete_commands=0
FIXTURE /tmp/edit-corpus/oneline_1g.txt bytes=1073741824(M)[AC] load1=3.63 bulk=three-submitted source=plain-indexed initial=certified
BENCH view_oneline_1g.txt_selection_1MiB_replace_bulk3_command mode=TRACK n=31 p50=602836 p99=659604 ns (M)[AC] load1=3.63 necessary_gate=1000000/2000000 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_oneline_1g.txt_selection_1MiB_replace_bulk3_initial mode=TRACK n=31 p50=70 p99=157 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_oneline_1g.txt_selection_1MiB_replace_bulk3_continue mode=TRACK n=15903 p50=1287 p99=2393 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
SLICE_MAX selection_1MiB_replace 16781 ns(M)[AC] load1=3.63 hard_limit=500000 ns(G) verdict=TRACK incomplete_commands=0
FIXTURE /tmp/edit-corpus/oneline_1g.txt bytes=1073741824(M)[AC] load1=3.63 bulk=three-submitted source=plain-indexed initial=certified
BENCH view_oneline_1g.txt_unindexed_type_bulk3_command mode=TRACK n=31 p50=2541 p99=4045 ns (M)[AC] load1=3.63 necessary_gate=1000000/2000000 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_oneline_1g.txt_unindexed_type_bulk3_initial mode=TRACK n=31 p50=2498 p99=4012 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
SLICE_MAX unindexed_type 4012 ns(M)[AC] load1=3.63 hard_limit=500000 ns(G) verdict=TRACK incomplete_commands=0
ALLOCATIONS /tmp/edit-corpus/oneline_1g.txt mallocs=0(M)[AC] load1=3.63 gate=0(G) guard=active pass=1
FIXTURE /tmp/edit-corpus/unicode.txt bytes=1048576(M)[AC] load1=3.63 bulk=one-active source=mapped initial=certified
BENCH view_unicode.txt_char_bulk1_command mode=TRACK n=31 p50=587 p99=4019 ns (M)[AC] load1=3.63 necessary_gate=5000000/5555555 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_unicode.txt_char_bulk1_initial mode=TRACK n=31 p50=565 p99=3895 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
SLICE_MAX char 3895 ns(M)[AC] load1=3.63 hard_limit=500000 ns(G) verdict=TRACK incomplete_commands=0
FIXTURE /tmp/edit-corpus/unicode.txt bytes=1048576(M)[AC] load1=3.63 bulk=one-active source=mapped initial=certified
BENCH view_unicode.txt_word_bulk1_command mode=TRACK n=31 p50=934 p99=2346 ns (M)[AC] load1=3.63 necessary_gate=5000000/5555555 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_unicode.txt_word_bulk1_initial mode=TRACK n=31 p50=911 p99=2315 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
SLICE_MAX word 2315 ns(M)[AC] load1=3.63 hard_limit=500000 ns(G) verdict=TRACK incomplete_commands=0
FIXTURE /tmp/edit-corpus/unicode.txt bytes=1048576(M)[AC] load1=3.63 bulk=one-active source=mapped initial=certified
BENCH view_unicode.txt_vertical_bulk1_command mode=TRACK n=31 p50=889 p99=3435 ns (M)[AC] load1=3.63 necessary_gate=5000000/5555555 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_unicode.txt_vertical_bulk1_initial mode=TRACK n=31 p50=858 p99=3400 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
SLICE_MAX vertical 3400 ns(M)[AC] load1=3.63 hard_limit=500000 ns(G) verdict=TRACK incomplete_commands=0
FIXTURE /tmp/edit-corpus/unicode.txt bytes=1048576(M)[AC] load1=3.63 bulk=one-active source=mapped initial=certified
BENCH view_unicode.txt_page_bulk1_command mode=TRACK n=31 p50=1035 p99=2827 ns (M)[AC] load1=3.63 necessary_gate=5000000/5555555 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_unicode.txt_page_bulk1_initial mode=TRACK n=31 p50=1000 p99=2801 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
SLICE_MAX page 2801 ns(M)[AC] load1=3.63 hard_limit=500000 ns(G) verdict=TRACK incomplete_commands=0
FIXTURE /tmp/edit-corpus/unicode.txt bytes=1048576(M)[AC] load1=3.63 bulk=one-active source=mapped initial=certified
BENCH view_unicode.txt_home_end_bulk1_command mode=TRACK n=31 p50=718 p99=5582 ns (M)[AC] load1=3.63 necessary_gate=5000000/5555555 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_unicode.txt_home_end_bulk1_initial mode=TRACK n=31 p50=697 p99=5562 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
SLICE_MAX home_end 5562 ns(M)[AC] load1=3.63 hard_limit=500000 ns(G) verdict=TRACK incomplete_commands=0
FIXTURE /tmp/edit-corpus/unicode.txt bytes=1048576(M)[AC] load1=3.63 bulk=one-active source=mapped initial=certified
BENCH view_unicode.txt_document_bulk1_command mode=TRACK n=31 p50=314 p99=955267 ns (M)[AC] load1=3.63 necessary_gate=5000000/5555555 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_unicode.txt_document_bulk1_initial mode=TRACK n=31 p50=293 p99=4170 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_unicode.txt_document_bulk1_continue mode=TRACK n=4095 p50=3049 p99=3637 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
SLICE_MAX document 12633 ns(M)[AC] load1=3.63 hard_limit=500000 ns(G) verdict=TRACK incomplete_commands=0
FIXTURE /tmp/edit-corpus/unicode.txt bytes=1048576(M)[AC] load1=3.63 bulk=one-active source=mapped initial=certified
BENCH view_unicode.txt_selection_bulk1_command mode=TRACK n=31 p50=1236 p99=890624 ns (M)[AC] load1=3.63 necessary_gate=5000000/5555555 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_unicode.txt_selection_bulk1_initial mode=TRACK n=31 p50=1217 p99=3523 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_unicode.txt_selection_bulk1_continue mode=TRACK n=3003 p50=3010 p99=3236 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
SLICE_MAX selection 53842 ns(M)[AC] load1=3.63 hard_limit=500000 ns(G) verdict=TRACK incomplete_commands=0
FIXTURE /tmp/edit-corpus/unicode.txt bytes=1048576(M)[AC] load1=3.63 bulk=one-active source=mapped initial=certified
BENCH view_unicode.txt_edit_bulk1_command mode=TRACK n=31 p50=1017 p99=6983 ns (M)[AC] load1=3.63 necessary_gate=1000000/2000000 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_unicode.txt_edit_bulk1_initial mode=TRACK n=31 p50=998 p99=6949 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
SLICE_MAX edit 6949 ns(M)[AC] load1=3.63 hard_limit=500000 ns(G) verdict=TRACK incomplete_commands=0
FIXTURE /tmp/edit-corpus/unicode.txt bytes=1048576(M)[AC] load1=3.63 bulk=one-active source=mapped initial=certified
BENCH view_unicode.txt_deep_type_bulk1_command mode=TRACK n=31 p50=1180 p99=2502 ns (M)[AC] load1=3.63 necessary_gate=1000000/2000000 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_unicode.txt_deep_type_bulk1_initial mode=TRACK n=31 p50=1147 p99=2456 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
SLICE_MAX deep_type 2456 ns(M)[AC] load1=3.63 hard_limit=500000 ns(G) verdict=TRACK incomplete_commands=0
FIXTURE /tmp/edit-corpus/unicode.txt bytes=1048576(M)[AC] load1=3.63 bulk=one-active source=mapped initial=certified
BENCH view_unicode.txt_selection_1MiB_replace_bulk1_command mode=TRACK n=31 p50=506467 p99=613139 ns (M)[AC] load1=3.63 necessary_gate=1000000/2000000 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_unicode.txt_selection_1MiB_replace_bulk1_initial mode=TRACK n=31 p50=71 p99=209 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_unicode.txt_selection_1MiB_replace_bulk1_continue mode=TRACK n=15903 p50=990 p99=2168 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
SLICE_MAX selection_1MiB_replace 14237 ns(M)[AC] load1=3.63 hard_limit=500000 ns(G) verdict=TRACK incomplete_commands=0
FIXTURE /tmp/edit-corpus/unicode.txt bytes=1048576(M)[AC] load1=3.63 bulk=one-active source=mapped initial=certified
BENCH view_unicode.txt_unindexed_type_bulk1_command mode=TRACK n=31 p50=625 p99=1644 ns (M)[AC] load1=3.63 necessary_gate=1000000/2000000 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_unicode.txt_unindexed_type_bulk1_initial mode=TRACK n=31 p50=607 p99=1606 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
SLICE_MAX unindexed_type 1606 ns(M)[AC] load1=3.63 hard_limit=500000 ns(G) verdict=TRACK incomplete_commands=0
FIXTURE /tmp/edit-corpus/unicode.txt bytes=1048576(M)[AC] load1=3.63 bulk=three-submitted source=mapped initial=certified
BENCH view_unicode.txt_char_bulk3_command mode=TRACK n=31 p50=501 p99=1135 ns (M)[AC] load1=3.63 necessary_gate=5000000/5555555 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_unicode.txt_char_bulk3_initial mode=TRACK n=31 p50=483 p99=1102 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
SLICE_MAX char 1102 ns(M)[AC] load1=3.63 hard_limit=500000 ns(G) verdict=TRACK incomplete_commands=0
FIXTURE /tmp/edit-corpus/unicode.txt bytes=1048576(M)[AC] load1=3.63 bulk=three-submitted source=mapped initial=certified
BENCH view_unicode.txt_word_bulk3_command mode=TRACK n=31 p50=753 p99=1704 ns (M)[AC] load1=3.63 necessary_gate=5000000/5555555 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_unicode.txt_word_bulk3_initial mode=TRACK n=31 p50=733 p99=1675 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
SLICE_MAX word 1675 ns(M)[AC] load1=3.63 hard_limit=500000 ns(G) verdict=TRACK incomplete_commands=0
FIXTURE /tmp/edit-corpus/unicode.txt bytes=1048576(M)[AC] load1=3.63 bulk=three-submitted source=mapped initial=certified
BENCH view_unicode.txt_vertical_bulk3_command mode=TRACK n=31 p50=1080 p99=3242 ns (M)[AC] load1=3.63 necessary_gate=5000000/5555555 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_unicode.txt_vertical_bulk3_initial mode=TRACK n=31 p50=1060 p99=3214 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
SLICE_MAX vertical 3214 ns(M)[AC] load1=3.63 hard_limit=500000 ns(G) verdict=TRACK incomplete_commands=0
FIXTURE /tmp/edit-corpus/unicode.txt bytes=1048576(M)[AC] load1=3.63 bulk=three-submitted source=mapped initial=certified
BENCH view_unicode.txt_page_bulk3_command mode=TRACK n=31 p50=507 p99=2179 ns (M)[AC] load1=3.63 necessary_gate=5000000/5555555 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_unicode.txt_page_bulk3_initial mode=TRACK n=31 p50=478 p99=2152 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
SLICE_MAX page 2152 ns(M)[AC] load1=3.63 hard_limit=500000 ns(G) verdict=TRACK incomplete_commands=0
FIXTURE /tmp/edit-corpus/unicode.txt bytes=1048576(M)[AC] load1=3.63 bulk=three-submitted source=mapped initial=certified
BENCH view_unicode.txt_home_end_bulk3_command mode=TRACK n=31 p50=656 p99=4026 ns (M)[AC] load1=3.63 necessary_gate=5000000/5555555 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_unicode.txt_home_end_bulk3_initial mode=TRACK n=31 p50=628 p99=4008 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
SLICE_MAX home_end 4008 ns(M)[AC] load1=3.63 hard_limit=500000 ns(G) verdict=TRACK incomplete_commands=0
FIXTURE /tmp/edit-corpus/unicode.txt bytes=1048576(M)[AC] load1=3.63 bulk=three-submitted source=mapped initial=certified
BENCH view_unicode.txt_document_bulk3_command mode=TRACK n=31 p50=469 p99=893970 ns (M)[AC] load1=3.63 necessary_gate=5000000/5555555 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_unicode.txt_document_bulk3_initial mode=TRACK n=31 p50=423 p99=3352 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_unicode.txt_document_bulk3_continue mode=TRACK n=4095 p50=3010 p99=3675 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
SLICE_MAX document 24563 ns(M)[AC] load1=3.63 hard_limit=500000 ns(G) verdict=TRACK incomplete_commands=0
FIXTURE /tmp/edit-corpus/unicode.txt bytes=1048576(M)[AC] load1=3.63 bulk=three-submitted source=mapped initial=certified
BENCH view_unicode.txt_selection_bulk3_command mode=TRACK n=31 p50=1319 p99=851193 ns (M)[AC] load1=3.63 necessary_gate=5000000/5555555 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_unicode.txt_selection_bulk3_initial mode=TRACK n=31 p50=1299 p99=3723 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_unicode.txt_selection_bulk3_continue mode=TRACK n=3003 p50=3001 p99=3260 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
SLICE_MAX selection 21133 ns(M)[AC] load1=3.63 hard_limit=500000 ns(G) verdict=TRACK incomplete_commands=0
FIXTURE /tmp/edit-corpus/unicode.txt bytes=1048576(M)[AC] load1=3.63 bulk=three-submitted source=mapped initial=certified
BENCH view_unicode.txt_edit_bulk3_command mode=TRACK n=31 p50=1001 p99=2896 ns (M)[AC] load1=3.63 necessary_gate=1000000/2000000 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_unicode.txt_edit_bulk3_initial mode=TRACK n=31 p50=985 p99=2846 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
SLICE_MAX edit 2846 ns(M)[AC] load1=3.63 hard_limit=500000 ns(G) verdict=TRACK incomplete_commands=0
FIXTURE /tmp/edit-corpus/unicode.txt bytes=1048576(M)[AC] load1=3.63 bulk=three-submitted source=mapped initial=certified
BENCH view_unicode.txt_deep_type_bulk3_command mode=TRACK n=31 p50=1120 p99=2359 ns (M)[AC] load1=3.63 necessary_gate=1000000/2000000 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_unicode.txt_deep_type_bulk3_initial mode=TRACK n=31 p50=1089 p99=2312 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
SLICE_MAX deep_type 2312 ns(M)[AC] load1=3.63 hard_limit=500000 ns(G) verdict=TRACK incomplete_commands=0
FIXTURE /tmp/edit-corpus/unicode.txt bytes=1048576(M)[AC] load1=3.63 bulk=three-submitted source=mapped initial=certified
BENCH view_unicode.txt_selection_1MiB_replace_bulk3_command mode=TRACK n=31 p50=503216 p99=637549 ns (M)[AC] load1=3.63 necessary_gate=1000000/2000000 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_unicode.txt_selection_1MiB_replace_bulk3_initial mode=TRACK n=31 p50=70 p99=144 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_unicode.txt_selection_1MiB_replace_bulk3_continue mode=TRACK n=15903 p50=882 p99=2154 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
SLICE_MAX selection_1MiB_replace 14377 ns(M)[AC] load1=3.63 hard_limit=500000 ns(G) verdict=TRACK incomplete_commands=0
FIXTURE /tmp/edit-corpus/unicode.txt bytes=1048576(M)[AC] load1=3.63 bulk=three-submitted source=mapped initial=certified
BENCH view_unicode.txt_unindexed_type_bulk3_command mode=TRACK n=31 p50=617 p99=1771 ns (M)[AC] load1=3.63 necessary_gate=1000000/2000000 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_unicode.txt_unindexed_type_bulk3_initial mode=TRACK n=31 p50=601 p99=1732 ns (M)[AC] load1=3.63 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
SLICE_MAX unindexed_type 1732 ns(M)[AC] load1=3.63 hard_limit=500000 ns(G) verdict=TRACK incomplete_commands=0
ALLOCATIONS /tmp/edit-corpus/unicode.txt mallocs=0(M)[AC] load1=3.63 gate=0(G) guard=active pass=1
AFTER utc=2026-10-10T16:11:39.150703+00:00 power=Not charging loadavg=3.63 1.91 1.48 1/1344 33 (M)[AC]
EXIT 0 elapsed_s=1.1179179179962375 (M)[AC]
```
