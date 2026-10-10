# edit-dnc — session 9 worker report

Status: **partial; do not close the bead**. The editor now publishes an index
adopted during a frame before that frame can submit, and mapped find avoids
repeated snapshot traversal. G5's sparse-file preview routing remains missing.
The measured G6/G7 rows still exceed their references. No gate acceptance is
claimed from loaded timing samples.

## Scope and decisions

Changed only `src/editor/large.c`, `large.h`, `editor.c` (message/submit routing),
`src/file/file.c`, `file.h` for the documented contiguous-snapshot API gap,
`bench/editor_large_bench.c` for attribution, and `tests/editor_large_test.c`,
plus this report and `docs/decisions/edit-dnc.md`. No new globals. The file
symbol check reports only the existing `file_bus`. `src/lineidx`, `src/find`,
other editor implementation files, Makefile, HANDOFF and worklog are unchanged.
Git was read-only; no bd command was run. Corpus files were read-only.

Design choices are recorded in [edit-dnc](../decisions/edit-dnc.md).
The file API certifies that every immutable snapshot span still references the
same complete original in order; edited, faulted or cancelled snapshots are refused. Worker cancellation is polled between metadata spans, including before and after certification.
Find searches that borrowed source in bounded windows with needle overlap,
nonoverlap carry and fault/cancellation checks. A window whose complete offsets
exceed the result capacity falls back to the existing snapshot kernel. Old
snapshots remain usable after edits. No allocation is added to typing.

## Per finding

* **Sparse G5 first viewport: attributed, not fixed.** Prefix-to-mapping is
  small; eager full-file piece metadata attachment and index setup precede the
  first viewport. Both calls are in `src/editor/buffers.c`, outside the supplied
  file list. A narrowly scoped routing clarification was requested and remains
  unanswered. The existing copy-preview/adoption path could be extended to
  mapped prefix previews there, but it was not changed without authorization.
  Prefetch was investigated and left unchanged because the trace does not
  identify it as the extra wait. The after row still misses G5.
* **Log and sparse G7: publication fixed, launch gap remains.** A test written
  first finishes index adoption during input processing, after the initial
  per-turn publication check. Before the fix, exact state is still false when
  that turn returns. Submit now checks publication before composition and
  after minimap's bounded poll. A required gutter repaint defers submission
  within the same frame ID. The regression checks exact state in the arriving
  turn, the first exact frame ID, gutter cells and zero typing allocations.
  There is no second index content scan. Moving launch ahead of eager setup
  also requires the excluded buffers routing file. The kernel floor already
  exceeds the log p50 reference in these runs.
* **G6 one worker: mapped source implemented; timing remains above reference.**
  The new API had a red contract test before implementation. Additional
  regressions compare exact totals and stored offsets with the public find
  kernel for boundary-spanning overlapping needles, dense single-byte
  saturation and long needles. They deliberately leave the index unbuilt with
  no index worker and prove find completes anyway. Edited snapshots fall back;
  an original snapshot retained across the edit still certifies. A cancellation regression rejects a stale work lease and clears both output fields. A synthetic backing fault rejects certification and the subsequent search returns cancelled with no result. Find's raw
  kernel itself remains above the p50 reference, outside this bead's src/find
  scope.

## Red runs (pasted)

Initial unmodified `bench/editor_large_bench` was run before behavioural fixes.
All ns values below are (M)[AC]; gate fields are (G). BAT0 was Not charging.
The existing harness prints pass bits but the campaign is TRACK on the loaded
box. Sparse indexing has no calibrated G7 gate for this fixture size.

```text
STAMP (M)[AC] BAT0=Not charging load1=2.47 loaded TRACK shared box
BENCH name=editor_large_log_1g_G5_open_first_viewport n=7 p50=2501832 p99=4512156 ci95=[2327202,4512156] gate_p50=6000000 gate_p99=9000000 pass=1 power=[AC]
BENCH name=editor_large_log_1g_G7_open_index_exact n=7 p50=129839298 p99=137947485 ci95=[120470090,137947485] gate_p50=80000000 gate_p99=125000000 pass=0 power=[AC]
BENCH name=editor_large_log_1g_G6_find_1worker n=5 p50=130998909 p99=131128941 ci95=[130937796,131128941] gate_p50=80000000 gate_p99=125000000 pass=0 power=[AC]
BENCH name=editor_large_sparse_10g_G5_open_first_viewport n=3 p50=6724358 p99=7491904 ci95=[6232214,7491904] gate_p50=6000000 gate_p99=9000000 pass=0 power=[AC]
BENCH name=editor_large_sparse_10g_G7_open_index_exact n=3 p50=2121378425 p99=2835960711 ci95=[1779150706,2835960711] gate_p50=80000000 gate_p99=125000000 pass=0 power=[AC]
```

G7 regression red, release test exit 1 (M)[AC], before submit publication fix:

```text
editor_large_test:155: FAIL editor_large_status_get(e).lines_exact
editor_large_test:296: FAIL index_arrives_during_frame(path) == 0
edit-9yd: unindexed 1 GiB gutter is dim; first publication frame is exact; theme and allocation checks passed
```

G6 API contract red, release test exit 1 (M)[AC], with the new API's refusal
stub before its implementation (the old source had no such API):

```text
editor_large_test:313: FAIL file_snapshot_bytes(original, &mapped_bytes, &mapped_length) == 1
edit-9yd: unindexed 1 GiB gutter is dim; first publication frame is exact; theme and allocation checks passed
edit-dnc G7: mid-frame index arrival publishes exact gutter in the same frame; zero typing allocations
```


G6 source-certification cancellation red, exit 1 (M)[AC], before adding the
per-span cancellation checks:

```text
editor_large_test:360: FAIL file_snapshot_bytes(original, &cancelled_control, &mapped_bytes, &mapped_length) == 0
edit-dnc G6: mapped find before index, window crossings/nonoverlap, dense saturation and bounded offsets passed
edit-9yd: unindexed 1 GiB gutter is dim; first publication frame is exact; theme and allocation checks passed
edit-dnc G7: mid-frame index arrival publishes exact gutter in the same frame; zero typing allocations
```

## Trace attribution

The bench records operation phase markers in the existing per-thread trace
ring. It dumps only after editor_close joins workers and uses the trace loader
for the phase table. `tools/tracedump` was run on log, sparse and find dumps.
Operation IDs have their high bit set and describe phases, not input frames;
tracedump's combined G1/G3 distributions are therefore not certification.
Owner IDs may be reused: capture one owner lifetime per dump. Worker rings are
finite; use the separate attribute commands rather than expecting all late
worker markers from the full benchmark campaign.

The following table uses the final before/after pair's first sample. Times are
ms (M)[AC]. Acquisition includes startup before prefix receipt; mapping includes
bulk scheduling and mailbox receipt; attachment includes watch/identity setup
and piece construction. These intervals do not pretend to separate hidden
file-worker internals. Index publication intervals include the kernel, mapping
page faults, continuations, mailbox adoption and foreground probes.

| Row / interval | Before | After |
|---|---:|---:|
| log G5/G7 request -> prefix receipt | 1.818 | 2.657 |
| log G5/G7 prefix receipt -> mapping receipt | 0.017 | 0.033 |
| log G5/G7 mapping receipt -> attachment complete | 0.382 | 0.421 |
| log G5/G7 attachment -> index submitted | 0.480 | 0.503 |
| log G5/G7 index submitted -> first viewport | 0.407 | 0.431 |
| log G5/G7 first viewport -> exact publication | 155.672 | 126.585 |
| sparse G5/G7 request -> prefix receipt | 2.573 | 2.643 |
| sparse G5/G7 prefix receipt -> mapping receipt | 0.110 | 0.033 |
| sparse G5/G7 mapping receipt -> attachment complete | 2.673 | 2.982 |
| sparse G5/G7 attachment -> index submitted | 3.522 | 3.784 |
| sparse G5/G7 index submitted -> first viewport | 0.476 | 0.482 |
| sparse G5/G7 first viewport -> exact publication | 1178.169 | 1183.571 |
| G6 queue -> worker start | 0.046 | 0.007 |
| G6 worker search, including mapped certification | 129.514 | 117.505 |
| G6 snapshot release | 0.001 | 0.001 |
| G6 completion -> mailbox adoption | 0.012 | 0.048 |

Index-only attribution pairs flat and snapshot callbacks back to back over the
same prefaulted original and pool, with identical adoption policy. First-sample
times below are ms (M)[AC]; callback counts are (M)[AC]. CPU extent ends at the
last source callback, just before the final chunk scan, so it is a bounded
proxy for kernel/continuation CPU rather than a claimed exact worker completion
stamp. Wall minus CPU also includes descheduling and waits. Tail includes the
final chunk scan and mailbox/metadata adoption, rather than only mailbox time.

| Source / fixture | Queue + setup | Worker wall extent | Worker CPU extent | Source callbacks total | Callback count | Final callback -> exact |
|---|---:|---:|---:|---:|---:|---:|
| flat log | 0.533 | 89.635 | 89.617 | 0.287 | 16384 | 0.028 |
| snapshot log | 0.365 | 95.376 | 95.311 | 1.023 | 16384 | 0.029 |
| flat sparse | 2.361 | 891.091 | 890.493 | 2.864 | 163840 | 0.017 |
| snapshot sparse | 3.493 | 933.197 | 932.745 | 11.161 | 163840 | 0.020 |

Loaded reference rows, all p50/p99 in ms (M)[AC]:

| Attribution row | p50 / p99 | Stamp / interpretation |
|---|---:|---|
| existing lineidx_bench fresh mapping G7 | 136.840 / 141.869 | load1 3.06; existing kernel floor, unchanged source |
| existing lineidx_bench prefaulted build | 87.949 / 94.098 | load1 3.06; excludes initial minor faults |
| editor attribution flat prefaulted index | 90.197 / 93.596 | load1 5.08; paired below |
| editor attribution snapshot prefaulted index | 91.812 / 95.771 | load1 5.08; same mapping/pool, back to back |
| editor attribution flat sparse prefaulted index | 953.727 / 988.013 | load1 5.08; no calibrated sparse gate |
| editor attribution snapshot sparse prefaulted index | 975.724 / 993.261 | load1 5.08; paired with flat |
| raw mapped find kernel | 124.437 / 124.568 | load1 5.35; paired below |
| snapshot find kernel | 142.903 / 150.881 | load1 5.35; same source/pool, back to back |

The log G7 reference is 80/125 ms (G), G6 is 80/125 ms (G), and G5 is
6/9 ms (G). The kernel's fresh-map log G7 p50 and raw find p50 alone exceed
their references here. Prefaulted rows must not be subtracted from new-open
rows as though initial page faults were editor scheduling overhead. The earlier
752 ms (M)[AC] lineidx observation from the bead was not treated as a constant floor
on a different load. No dependency kernel was modified.

## Back-to-back before / after (pasted)

Each pair invokes the preserved pre-fix binary immediately before the final
binary on the same box. The baseline binary includes the identical phase
instrumentation but predates the behavioural fixes. All ns values are (M)[AC];
gate fields are (G). Small samples and changing worker load preclude a gate
verdict or a claim that the G7 timing variation is caused by the publication
fix. The G6 source decision is also supported by the paired raw/snapshot
kernel attribution above. Sparse G5 is explicitly still incomplete.

### log pair

```text
BEFORE
STAMP (M)[AC] BAT0=Not charging load1=1.32 loaded TRACK shared box
BENCH name=editor_large_log_1g_G5_open_first_viewport n=3 p50=2897221 p99=3105268 ci95=[2183258,3105268] gate_p50=6000000 gate_p99=9000000 pass=1 power=[AC]
BENCH name=editor_large_log_1g_G7_open_index_exact n=3 p50=124837264 p99=158986439 ci95=[123197171,158986439] gate_p50=80000000 gate_p99=125000000 pass=0 power=[AC]
AFTER
STAMP (M)[AC] BAT0=Not charging load1=1.32 loaded TRACK shared box
BENCH name=editor_large_log_1g_G5_open_first_viewport n=3 p50=3787929 p99=4043894 ci95=[2472723,4043894] gate_p50=6000000 gate_p99=9000000 pass=1 power=[AC]
BENCH name=editor_large_log_1g_G7_open_index_exact n=3 p50=130303187 p99=133975851 ci95=[125782915,133975851] gate_p50=80000000 gate_p99=125000000 pass=0 power=[AC]
```

### find pair

```text
BEFORE
STAMP (M)[AC] BAT0=Not charging load1=1.32 loaded TRACK shared box
BENCH name=editor_large_log_1g_G6_find_1worker n=5 p50=129341425 p99=129608554 ci95=[129036977,129608554] gate_p50=80000000 gate_p99=125000000 pass=0 power=[AC]
AFTER
STAMP (M)[AC] BAT0=Not charging load1=1.32 loaded TRACK shared box
BENCH name=editor_large_log_1g_G6_find_1worker n=5 p50=117344005 p99=124899850 ci95=[117144137,124899850] gate_p50=80000000 gate_p99=125000000 pass=0 power=[AC]
```

### sparse pair

```text
BEFORE
STAMP (M)[AC] BAT0=Not charging load1=1.32 loaded TRACK shared box
BENCH name=editor_large_sparse_10g_G5_open_first_viewport n=3 p50=8168158 p99=9353468 ci95=[7130662,9353468] gate_p50=6000000 gate_p99=9000000 pass=0 power=[AC]
BENCH name=editor_large_sparse_10g_G7_open_index_exact n=3 p50=1187812042 p99=1215240941 ci95=[1184849200,1215240941] gate_p50=80000000 gate_p99=125000000 pass=0 power=[AC]
AFTER
STAMP (M)[AC] BAT0=Not charging load1=1.50 loaded TRACK shared box
BENCH name=editor_large_sparse_10g_G5_open_first_viewport n=3 p50=6605296 p99=9924731 ci95=[4280513,9924731] gate_p50=6000000 gate_p99=9000000 pass=0 power=[AC]
BENCH name=editor_large_sparse_10g_G7_open_index_exact n=3 p50=1191286017 p99=1196859164 ci95=[1190537640,1196859164] gate_p50=80000000 gate_p99=125000000 pass=0 power=[AC]
```

## Complete benchmark pair / foreground preservation

After source freeze, both full default benchmark invocations completed with
exit 0 (M)[AC]. This includes the find/save/index overlap rows omitted by the
narrow attribution invocations. All ns fields below are (M)[AC]; gate fields
are (G). No source change removes the sparse attachment/setup dependency:
the full after sample's printed G5 pass is retained as an observation, while
the narrower final-source pair above still misses G5. Neither run certifies
that architectural acceptance item or a timing gate on its own.

```text
BEFORE
STAMP (M)[AC] BAT0=Not charging load1=0.46 loaded TRACK shared box
BENCH name=editor_large_log_1g_G5_open_first_viewport n=7 p50=2390271 p99=5786833 ci95=[1844266,5786833] gate_p50=6000000 gate_p99=9000000 pass=1 power=[AC]
BENCH name=editor_large_log_1g_G7_open_index_exact n=7 p50=120500282 p99=142400457 ci95=[119829146,142400457] gate_p50=80000000 gate_p99=125000000 pass=0 power=[AC]
BENCH name=editor_large_log_1g_fg_ingress_submit n=874 p50=106999 p99=267259 ci95=[106620,107389] gate_p50=1000000 gate_p99=2000000 pass=1 power=[AC]
BENCH name=editor_large_log_1g_G6_find_1worker n=5 p50=127719567 p99=127903287 ci95=[127300999,127903287] gate_p50=80000000 gate_p99=125000000 pass=0 power=[AC]
BENCH name=editor_large_find_1worker_arrows_fg_ingress_submit n=39 p50=211031 p99=292057 ci95=[131124,216240] gate_p50=1000000 gate_p99=2000000 pass=1 power=[AC]
BENCH name=editor_large_typing_line1000_find_1worker_fg_ingress_submit n=500 p50=121791 p99=224429 ci95=[121514,122145] gate_p50=1000000 gate_p99=2000000 pass=1 power=[AC]
ALLOC typing_1worker allocations=0 (G)=0
BENCH name=editor_large_arrows_3workers_fg_ingress_submit n=5034 p50=81672 p99=185964 ci95=[81627,81720] gate_p50=1000000 gate_p99=2000000 pass=1 power=[AC]
BENCH name=editor_large_typing_line1000_find+save_fg_ingress_submit n=500 p50=89387 p99=100625 ci95=[89293,89535] gate_p50=1000000 gate_p99=2000000 pass=1 power=[AC]
ALLOC typing_3workers allocations=0 (G)=0
BENCH name=editor_large_sparse_10g_G5_open_first_viewport n=3 p50=7278821 p99=8149189 ci95=[5434407,8149189] gate_p50=6000000 gate_p99=9000000 pass=0 power=[AC]
BENCH name=editor_large_sparse_10g_G7_open_index_exact n=3 p50=1161846653 p99=1174842711 ci95=[1159632568,1174842711] gate_p50=80000000 gate_p99=125000000 pass=0 power=[AC]
BENCH name=editor_large_sparse_10g_fg_ingress_submit n=8150 p50=103083 p99=303620 ci95=[103042,103129] gate_p50=1000000 gate_p99=2000000 pass=1 power=[AC]
AFTER
STAMP (M)[AC] BAT0=Not charging load1=0.61 loaded TRACK shared box
BENCH name=editor_large_log_1g_G5_open_first_viewport n=7 p50=2354092 p99=4297282 ci95=[1841281,4297282] gate_p50=6000000 gate_p99=9000000 pass=1 power=[AC]
BENCH name=editor_large_log_1g_G7_open_index_exact n=7 p50=140608698 p99=151694325 ci95=[120253185,151694325] gate_p50=80000000 gate_p99=125000000 pass=0 power=[AC]
BENCH name=editor_large_log_1g_fg_ingress_submit n=884 p50=109224 p99=246466 ci95=[109071,109422] gate_p50=1000000 gate_p99=2000000 pass=1 power=[AC]
BENCH name=editor_large_log_1g_G6_find_1worker n=5 p50=115902284 p99=116011543 ci95=[115767860,116011543] gate_p50=80000000 gate_p99=125000000 pass=0 power=[AC]
BENCH name=editor_large_find_1worker_arrows_fg_ingress_submit n=35 p50=263464 p99=362096 ci95=[136281,270511] gate_p50=1000000 gate_p99=2000000 pass=1 power=[AC]
BENCH name=editor_large_typing_line1000_find_1worker_fg_ingress_submit n=500 p50=123717 p99=241271 ci95=[123560,123899] gate_p50=1000000 gate_p99=2000000 pass=1 power=[AC]
ALLOC typing_1worker allocations=0 (G)=0
BENCH name=editor_large_arrows_3workers_fg_ingress_submit n=4819 p50=86403 p99=168187 ci95=[86306,86497] gate_p50=1000000 gate_p99=2000000 pass=1 power=[AC]
BENCH name=editor_large_typing_line1000_find+save_fg_ingress_submit n=500 p50=93358 p99=100005 ci95=[93075,93676] gate_p50=1000000 gate_p99=2000000 pass=1 power=[AC]
ALLOC typing_3workers allocations=0 (G)=0
BENCH name=editor_large_sparse_10g_G5_open_first_viewport n=3 p50=5817249 p99=7136048 ci95=[5405749,7136048] gate_p50=6000000 gate_p99=9000000 pass=1 power=[AC]
BENCH name=editor_large_sparse_10g_G7_open_index_exact n=3 p50=1203042125 p99=1356349766 ci95=[1201522603,1356349766] gate_p50=80000000 gate_p99=125000000 pass=0 power=[AC]
BENCH name=editor_large_sparse_10g_fg_ingress_submit n=8334 p50=103263 p99=313663 ci95=[103224,103295] gate_p50=1000000 gate_p99=2000000 pass=1 power=[AC]
```

## Green behavioural run and validation

Release `tests/editor_large_test` exit 0 (M)[AC], active allocation guards:

```text
edit-dnc G6: mapped find before index, window crossings/nonoverlap, dense saturation, bounded offsets and fault refusal passed
edit-9yd: unindexed 1 GiB gutter is dim; first publication frame is exact; theme and allocation checks passed
edit-dnc G7: mid-frame index arrival publishes exact gutter in the same frame; zero typing allocations
editor_large_test: ok
```

Targeted Clang sanitizer large-file suite also exits 0 (M)[AC] with the same
regression output. LeakSanitizer is disabled with
`ASAN_OPTIONS=detect_leaks=0` because it cannot run in this sandbox; the
coordinator must rerun with leaks enabled. All display-dependent execution
uses DISPLAY=:99 and EDIT_DISPLAY=:99. Bench and editor fuzz use the null
backend. No window was opened on :0.

GCC release `make all` exits 0 (M)[AC], with the required C11, Wall, Wextra,
Werror, Wshadow and Wconversion flags. Final incremental make all also exits
0 (M)[AC]. GCC is 13.3.0 and Clang is 18.1.3 (M)[AC]. `git diff --check`
passes. The production file object's global check prints:

```text
finding10: ok mutable globals=1 file_bus
```

Editor fuzz requested 60 seconds (G) and completed cleanly:

```text
Done 25064 runs in 61 second(s)
```

Both quantities are (M)[AC]; exit 0. The file API gap also received the existing
file fuzz target for 60 seconds (G):

```text
Done 8501 runs in 61 second(s)
```

Both quantities are (M)[AC]; exit 0. Existing editor fuzz exercises the editor
mutation/submit path; the new mapped-source branches have deterministic
large-file contract coverage, not newly added fuzz operations.

The initial sandboxed full `make check` exits 2 (M)[AC] at CLI's XCB
connection preflight after the shared display-lock wait:

```text
cli_test:61: FAIL c && !xcb_connection_has_error(c)
cli_test:137: FAIL ran == 0
make: *** [Makefile:105: check] Error 1
```

`src/editor/STATUS.md` documents execution outside the sandbox for the Xvfb
and local Unix socket tests. The authorized escalation was accepted; a native
Xvfb :99 reachability check exits 0 (M)[AC]. That approved full check exits 2
(M)[AC] after the shared display lock times out:

```text
cli_test: display lock timeout: display :99 (held by 3989351)
make: *** [Makefile:105: check] Error 1
```

A final full-check attempt uses the repository's display wrapper around the
entire suite, so its documented inherited lock is reused by individual tests.
It has a bounded 180-second wait (G) for the outer lock. This final attempt
also fails before the suite can run, wrapper exit 1 (M)[AC]:

```text
waiting for display :99 (held by 3989351)
make: display lock timeout: display :99 (held by 3990114)
```

**Full make check acceptance is missing.** The targeted final sanitizer
large-file suite is green, but it is not substituted for the full suite.
The coordinator must rerun make check when the shared display lane is available,
using the documented local-socket execution route and with leaks enabled.
All worker full-check attempts use Clang ASan/UBSan and
`ASAN_OPTIONS=detect_leaks=0`. No other worker's lock or process was disturbed.

## Missing acceptance / coordinator action

The bead remains open: sparse G5 prefix-first routing, launching the initial
index before eager setup, the G6/G7 timing gates, and a successful full
make check remain incomplete.
`src/editor/buffers.c` owns the first two routing gaps. Kernel work is outside
this worker's scope: lineidx belongs to edit-wvf, and find's raw kernel remains
above its p50 reference in the paired attribution. No other module finding was
fixed. The real-display or unloaded timing verdict is not claimed; this task
requires loaded comparisons and those are preserved above.

Source and test edits stopped before the scope cutoff. Final make all and
diff whitespace checks exit 0 (M)[AC]; the report is complete at handoff.
