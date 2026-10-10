# edit-zzj.12 session 9 — G11 raster idle

Status: caret-only implementation and focused regressions complete. G11 CPU
acceptance remains open; this bead is not claimed complete. Final verification is complete below.

Read CLAUDE.md, the supplied bead, perf/01-perf-target.md section 0.2 and idle
sections, editor/raster STATUS.md, and the relevant editor review idle/geometry
sections. Git was read-only, used only for diff and baseline source extraction.
No bd, HANDOFF or worklog changes. All live invocation uses Xvfb :99.

## Findings and implementation

- Caret blink work: reuse prepared paint, narrow row composition, explicit
  caret-only hint, inline one-/two-cell SSE2 raster and XShm upload, bounded
  Present update region. Selection, wide glyph and wrapped-row regressions
  check changed cells and unchanged work-slot epochs. No strip/fence worker
  job is submitted for those caret frames. Zero changed pixels skip submission.
- Completion wakeups: no caret fence worker, no completion polling timer. The
  UI drains authentic fence/Present/Idle events. Waiting damage arms the
  private X fd in the existing epoll set; clean frames consume acknowledgements
  on the next blink/input turn. Delayed Present retains the pixmap without an
  arbitrary deadline. Idle/unfocus timer policy is preserved. The general
  full-frame fence worker and destroy/ownership code are unchanged.
- Benchmark settle: submitted undamaged state settles at T4 even while the
  backend retains delayed physical completion. T5 rows explicitly wait for
  readiness; G11 setup and unfocus use the T4 condition. The real-display failure is addressed at this settle condition;
  :0 behavior has not been tested. G11 geometry now asserts the declared A
  viewport; G1 geometry is outside this bead and unchanged.

Design choices and tradeoffs: [decision](../decisions/edit-zzj.12.md).

## Red runs, before the production fix

The caret regression was written and executed first, against baseline code.
The delayed-Present settle self-check was also executed before its fix:

```text
editor_test:155: FAIL m.jobs == 0
G11 blink: jobs=2 minimap_fills=1
editor_bench:74 failed: settled(s, &b)
```

Baseline G11, measured (M)[AC], shared host, with original fixture geometry:

```text
STAMP (M)[AC] BAT0=Charging load1=2.55 TRACK shared box
BACKEND requested=null actual=null init_error=0
BENCH name=editor_null_G11_process_cpu n=19 p50=97074 p99=133057 ci95=[85067,107116] gate_p50=100000 gate_p99=200000 pass=1 power=[AC]
G11 null (M)[AC] load1=2.55 blinks=19 poll_returns=20 wakeups/s=1.998 idle=0 unfocused=0 (G)<=2/s,0,0 mode=GATE
STAMP (M)[AC] BAT0=Charging load1=2.28 TRACK shared box
BACKEND requested=raster actual=cpu-raster init_error=0
BENCH name=editor_raster_G11_process_cpu n=19 p50=811040 p99=1195542 ci95=[735364,884252] gate_p50=100000 gate_p99=200000 pass=0 power=[AC]
G11 raster (M)[AC] load1=2.28 blinks=19 poll_returns=86 wakeups/s=8.586 idle=0 unfocused=0 (G)<=2/s,0,0 mode=GATE
```

## Green focused runs

These checks assert behavior and lease counts, not timing thresholds:

```text
G11 blink: mode=0 jobs=0 cells=1 minimap_fills=0
G11 blink: mode=0 jobs=0 cells=1 minimap_fills=0
G11 blink: mode=1 jobs=0 cells=1 minimap_fills=0
G11 blink: mode=1 jobs=0 cells=1 minimap_fills=0
G11 blink: mode=2 jobs=0 cells=2 minimap_fills=0
G11 blink: mode=2 jobs=0 cells=2 minimap_fills=0
G11 blink: mode=3 jobs=0 cells=1 minimap_fills=0
G11 blink: mode=3 jobs=0 cells=1 minimap_fills=0
editor_test: G11 inline caret damage and sleeping worker pool passed
editor_bench: settle permits undamaged submitted frame with delayed Present completion
G11 UI completion: both event orders, delayed Present, no worker jobs or timer passed
P2.5b section G11: GREEN
```

Native raster pixel comparison is green for inline caret updates and the
entire unchanged window, with release allocation guards active:

```text
raster_test: kernels PASS (SSE2 == scalar on 4000 random grids, 10240 blends, partitions)
raster pixels: PASS inline caret, full-window scalar comparison and zero worker jobs
raster pixels: PASS full + 10 individual partial comparisons (disconnected/boundaries/unchanged rows)
raster_test: PASS (requested conformance, upload ordering, typing allocations, live XShm)
```

## Back-to-back comparison and limitations

Matched A geometry: baseline sources were extracted read-only from HEAD into
build/g11-before and linked as a separate benchmark variant. Only its G11
font selection was corrected to match the new row. Variants were executed
sequentially on the same loaded AC host, with no quiet-box wait. This comparison
overlapped this worker's native test suite; extra display events can contribute
to wakeups. Retaining both raw rows rather than claiming a gate pass:

```text
STAMP (M)[AC] BAT0=Not charging load1=4.58 TRACK shared box
BACKEND requested=null actual=null init_error=0
BENCH name=editor_null_G11_process_cpu n=19 p50=107126 p99=188214 ci95=[88055,116889] gate_p50=100000 gate_p99=200000 pass=0 power=[AC]
G11 null (M)[AC] load1=4.58 blinks=19 poll_returns=20 wakeups/s=1.998 idle=0 unfocused=0 (G)<=2/s,0,0 mode=GATE
STAMP (M)[AC] BAT0=Not charging load1=5.43 TRACK shared box
BACKEND requested=raster actual=cpu-raster init_error=0
BENCH name=editor_raster_G11_process_cpu n=19 p50=760931 p99=1095951 ci95=[679287,889590] gate_p50=100000 gate_p99=200000 pass=0 power=[AC]
G11 raster (M)[AC] load1=5.43 blinks=19 poll_returns=92 wakeups/s=9.200 idle=0 unfocused=0 (G)<=2/s,0,0 mode=GATE
STAMP (M)[AC] BAT0=Not charging load1=7.24 TRACK shared box
BACKEND requested=null actual=null init_error=0
BENCH name=editor_null_G11_process_cpu n=19 p50=106988 p99=125703 ci95=[94218,116809] gate_p50=100000 gate_p99=200000 pass=0 power=[AC]
G11 null (M)[AC] load1=7.24 blinks=19 poll_returns=20 wakeups/s=1.998 idle=0 unfocused=0 (G)<=2/s,0,0 mode=GATE
STAMP (M)[AC] BAT0=Charging load1=7.51 TRACK shared box
BACKEND requested=raster actual=cpu-raster init_error=0
BENCH name=editor_raster_G11_process_cpu n=19 p50=188149 p99=335067 ci95=[177207,234047] gate_p50=100000 gate_p99=200000 pass=0 power=[AC]
G11 raster (M)[AC] load1=7.51 blinks=19 poll_returns=62 wakeups/s=6.081 idle=0 unfocused=0 (G)<=2/s,0,0 mode=GATE
```

Final variants, after the forced release rebuild and without overlapping this
worker's native suite, again back to back on the loaded host (M)[AC]. Both
benchmark processes exited one: the baseline misses CPU/wakeups, and the final
variant retains a CPU median miss. No repeat was used to claim a gate pass.

```text
STAMP (M)[AC] BAT0=Not charging load1=25.65 TRACK shared box
BACKEND requested=null actual=null init_error=0
BENCH name=editor_null_G11_process_cpu n=19 p50=134347 p99=226447 ci95=[127123,161556] gate_p50=100000 gate_p99=200000 pass=0 power=[AC]
G11 null (M)[AC] load1=25.65 blinks=19 poll_returns=20 wakeups/s=1.997 idle=0 unfocused=0 (G)<=2/s,0,0 mode=GATE
STAMP (M)[AC] BAT0=Charging load1=21.76 TRACK shared box
BACKEND requested=raster actual=cpu-raster init_error=0
BENCH name=editor_raster_G11_process_cpu n=19 p50=796851 p99=1085596 ci95=[717311,882573] gate_p50=100000 gate_p99=200000 pass=0 power=[AC]
G11 raster (M)[AC] load1=21.76 blinks=19 poll_returns=89 wakeups/s=8.886 idle=0 unfocused=0 (G)<=2/s,0,0 mode=GATE
STAMP (M)[AC] BAT0=Charging load1=19.59 TRACK shared box
BACKEND requested=null actual=null init_error=0
BENCH name=editor_null_G11_process_cpu n=19 p50=63341 p99=116706 ci95=[60690,64925] gate_p50=100000 gate_p99=200000 pass=1 power=[AC]
G11 null (M)[AC] load1=19.59 blinks=19 poll_returns=20 wakeups/s=1.998 idle=0 unfocused=0 (G)<=2/s,0,0 mode=GATE
G11 null structural worker_jobs=0 inline_frames=0
STAMP (M)[AC] BAT0=Not charging load1=17.58 TRACK shared box
BACKEND requested=raster actual=cpu-raster init_error=0
BENCH name=editor_raster_G11_process_cpu n=19 p50=109487 p99=126305 ci95=[107280,116406] gate_p50=100000 gate_p99=200000 pass=0 power=[AC]
G11 raster (M)[AC] load1=17.58 blinks=19 poll_returns=20 wakeups/s=2.005 idle=0 unfocused=0 (G)<=2/s,0,0 mode=GATE
G11 raster structural worker_jobs=0 inline_frames=19
```

The final raster row has zero worker jobs for its nineteen caret frames (M)[AC],
twenty UI poll returns (M)[AC], and no after-idle/unfocused background returns
(M)[AC]. The printed rate is slightly above two because the initial setup trims
the first partial blink interval; the inherited gate implementation checks the
policy's twenty returns over its ten-second blink window, including the terminal
idle timeout. The raw printed rate is retained above.

G11 limits: CPU p50/p99 100000/200000 ns (G), blinking wakeups at most two per
second (G), none after ten seconds idle or unfocused (G). The final raster CPU
median is 109487 ns (M)[AC], above the 100000 ns (G) median limit; p99 is
126305 ns (M)[AC]. CPU acceptance remains open. The inherited render adapter
validates all cells on each submission; changing that frozen contract is outside
the supplied source scope. This is a remaining cost candidate, not a separately
measured attribution. Journal idle polling from editor review section 17 is a
separate finding and is not changed. No real-display, leak-enabled, or optical
acceptance is claimed. LeakSanitizer is disabled with detect_leaks=0 as required;
the coordinator reruns with leaks enabled.

## Final verification

GCC 13 release compilation passed with C11, -Wall -Wextra -Werror -Wshadow
-Wconversion. A forced full `make -B -j4 all` exited zero after the external
wall clock moved backward: ordinary make timestamps were not sufficient to
prove the latest benchmark was rebuilt. Make reports clock skew; actual forced
compiler/link commands completed successfully. No source edits were made to
other modules to accommodate this clock change.

The earlier sanitizer `make check` passed all 59 test binaries and replay CLI
(M)[AC], with LeakSanitizer disabled. The final-source run passed editor/raster
and then failed in the unchanged view suite. This is outside the bead, so no
view source or test was changed. Direct rerun is green:

```text
utf8_test: all passed
== build/san/tests/view_test
tests/view_test.c:202: assertion failed: view_command(&f.v,VIEW_WORD_LEFT,0,((void*)0),0,&word_change)==VIEW_OK
Aborted (core dumped)
make: *** [Makefile:101: check] Error 134
review_15: insert/delete/prefix allocation failures and retry passed
view_test: 10000 keys mallocs=0 guard=ASan-inert
wrap_selection: anchored Shift+Up/Down preserves soft-row affinity and preferred column
wrap_port: long visual-column continuation, anchored soft-end motion and cancellation passed
view_test: all passed
```

Final release typing guard, (M)[AC]:

```text
editor_test: raster 10000 keys mallocs=0 guard=active

```

Final editor fuzz, (M)[AC], start BAT0=Not charging, load1=5.96; monotonic
elapsed 61.141 seconds against 60 seconds (G), exit zero:

```text
###### End of recommended dictionary. ######
Done 11322 runs in 61 second(s)
stat::number_of_executed_units: 11322
stat::average_exec_per_sec:     185
stat::new_units_added:          506
stat::slowest_unit_time_sec:    0
stat::peak_rss_mb:              363
```

Raster fuzz, (M)[AC], start BAT0=Charging, load1=8.40; exit zero:

```text
###### End of recommended dictionary. ######
Done 53491 runs in 61 second(s)
stat::number_of_executed_units: 53491
stat::average_exec_per_sec:     876
stat::new_units_added:          326
stat::slowest_unit_time_sec:    0
stat::peak_rss_mb:              340
```

Both fuzzers use ASan/UBSan and detect_leaks=0. The first editor fuzz invocation
was clean but its elapsed-time output was invalidated by the external wall-clock
jump; it is not used for the duration claim above. Required leak-enabled rerun
belongs to the coordinator. The general fence job, resource release and
shutdown functions were compared read-only with HEAD and are byte-for-byte
unchanged. No new production or test globals were added.

Final complete sanitizer retry: exit zero, (M)[AC], with
DISPLAY=:99 EDIT_DISPLAY=:99 ASAN_OPTIONS=detect_leaks=0 make -j4 check.
The unchanged view suite passes in this complete retry; its earlier failure
above is retained as an out-of-scope transient finding, with no view edits.

```text
check: 59 test binaries passed
test_replay_cli: all passed
```

Final required results: forced GCC 13 `make all` exit zero; clang 18 ASan/UBSan
`make check` exit zero; editor and raster fuzz both clean for the required
60-second budget (G); strict compiler warnings clean; active release typing
allocation guard reports zero. `git diff --check` is clean. Diagnostic logs are
retained under build/g11-evidence, and the evidence needed to review the change
is pasted in this report independently of those ignored build artifacts.

What remains missing: G11 CPU median acceptance (the final paired result still
exceeds its 0.1 ms gate), literal raw-rate/hardware acceptance beyond the
window-count cadence check, a permitted real-display verification of the settle
change, and the coordinator's leak-enabled run. The bead remains open. No
production scope was added after the supplied cutoff. No unrelated source was
fixed, no new globals were added, and fence/destruction ownership was left to
its assigned worker.
