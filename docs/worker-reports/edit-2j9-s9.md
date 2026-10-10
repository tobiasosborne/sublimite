# edit-2j9 — P4-3 bench/fuzz honesty worker report

Public standalone report, session 9, 2026-10-11. Scope: review P4-3 §§34–39.
Git remained read-only; no bd, HANDOFF.md, docs/worklog, src/editor or unrelated
module edits. No new globals. The only production edit is the logical scroll
intent comparison. No allocation was introduced on the typing path.

No new scope was added after minute 30 (G). This report was written before
that cutoff and updated with verification and the lost-completion test repair.

All recorded runs are (M)[AC], power reported Not charging. The box was shared;
TRACK observations and single-sample probes are not performance gate verdicts.
The restored default populations require n >= 10000 (G) by repeating the full
workload. Full populations were not executed here; probes refuse certification.

## §34 — restored workloads; certification remains incomplete

Scroll now repeats a fresh owned, unindexed seek and a new mapping for every
sample, ending at the independently checked submitted viewport at line 10^7
(G). The supplied log has 8,947,842 lines (G, fixture contract); a private worktree
copy extends it with its own prefix instead of shortening lines or moving the
target. Warm and verified cold-source variants exist. Indexed jumps are a
separate regression mode. Displayed G3z remains UNAVAILABLE.

Findui count and paired plain/whole-word rows now scan the complete 1 GiB
fixture (G), with a fresh file mapping, exact totals and first 4096 offsets (G).
The word-pair file is prepared once outside timers. The 64 KiB (G) guard is
separately named and budgeted. Full-source probes completed and exit REFUSED
for the insufficient population, including TRACK.

Savectl restores durable replacement of 1 MiB, 1 GiB warm-source and 1 GiB
verified cold-source fixtures (G), using private worktree storage, full-content
checks and repetition. Small-file guards have separate names. Snapshot/enqueue
is a regression guard: actual saving-frame submission remains UNAVAILABLE and
the benchmark refuses overall certification. Host display integration is
outside this bead and src/editor was not changed.

The editor_p4 tab-switch row already retained the full target-A viewport,
100 tabs (G) and ingress-to-T5 endpoint; its sample-count change was repetition,
so that schedule remains. Editor_idle now repeats real-cadence idle sessions for
binding CPU populations. Due-now null blinks are a separate guard with their
own budget; they cannot qualify G11. Authenticated all-thread wakeup
certification remains UNAVAILABLE.

Red — the current populations qualified easier rows:

```text
PROBE partial_prefix_owned_seek correct=1 ns=75248002 (M)[AC]; outside indexed population
BENCH name=G7j_warm_indexed_null_frame n=10000 required_n=10000 p50=14225 p99=15860 ci95_p50=[14220,14231] ci95_p99=[15672,16008] gate_p50=30000000 gate_p99=50000000 dropped=0 (M)[AC] power=Not charging load1=1.46 verdict=TRACK
```

```text
BENCH name=G6_findui_dense_a_64KiB n=10000 required_n=10000 p50=5399591 p99=6245409 ci95_p50=[5396739,5402779] ci95_p99=[6076444,6427664] gate_p50=80000000 gate_p99=125000000 dropped=0 (M)[AC] power=Not charging load1=- verdict=TRACK
findui_count: exact 64KiB total/first 4096 checked; initial/late/empty windows; mapping=NEW; (M)[AC]
```

```text
POPULATION bytes=4096 n=10000 workers=4 wall_ns=52050869659 (M)[AC]
BENCH name=G8d_4KiB_durable n=10000 required_n=10000 p50=11730875 p99=31505785 ci95_p50=[11683113,11785352] ci95_p99=[31077476,32110467] gate_p50=10000000 gate_p99=50000000 dropped=0 (M)[AC] power=Not charging load1=1.46 verdict=TRACK
```

```text
BENCH name=editor_null_G11_process_cpu_synthetic_due_now n=10000 required_n=10000 p50=25371 p99=52636 ci95_p50=[25357,25387] ci95_p99=[52406,52955] gate_p50=100000 gate_p99=200000 dropped=0 (M)[AC] power=Not charging load1=4.19 verdict=PASS
```
Green — full-source smoke probes, not certified gates:

```text
WORKLOAD scroll bytes=1207959552 target_line=10000000 target_byte=1200000904 mapping=NEW indexed_guard=0 cold=0 samples=1 (G)
PROBE unindexed_owned_seek correct=1 ns=133703519 (M)[AC]
BENCH name=G7j_warm_unindexed_line_10000000_new_mapping_null_frame n=1 required_n=10000 p50=133703519 p99=133703519 ci95_p50=[133703519,133703519] ci95_p99=[133703519,133703519] gate_p50=30000000 gate_p99=50000000 dropped=0 (M)[AC] power=Not charging load1=1.21 verdict=TRACK
```

```text
WORKLOAD findui bytes=1073741824 mapping=NEW samples=1 kind=G6_full_source (G)
BENCH name=G6_findui_dense_a_1GiB_new_mapping n=1 required_n=10000 p50=160744855 p99=160744855 ci95_p50=[160744855,160744855] ci95_p99=[160744855,160744855] gate_p50=80000000 gate_p99=125000000 dropped=0 (M)[AC] power=Not charging load1=- verdict=TRACK
```

```text
G11 real_cadence_cpu verdict=REFUSED n=19 required_n=10000 (G); separate due-now guard follows
BENCH name=editor_null_blink_cpu_regression_due_now n=10000 required_n=10000 p50=17171 p99=40699 ci95_p50=[17170,17172] ci95_p99=[38443,41229] gate_p50=1000000 gate_p99=2000000 dropped=0 (M)[AC] power=Not charging load1=1.08 verdict=PASS
```
Savectl full-size warm/cold probes completed, checked every saved byte, and
returned REFUSED (exit 3 (G)) for their insufficient populations. Full qualified
populations and real display endpoints remain unmeasured.

```text
POPULATION bytes=1048576 n=1 workers=1 wall_ns=30379137 (M)[AC]
BENCH name=G8d_1MiB_durable n=1 required_n=10000 p50=12939160 p99=12939160 ci95_p50=[12939160,12939160] ci95_p99=[12939160,12939160] gate_p50=10000000 gate_p99=50000000 dropped=0 (M)[AC] power=Not charging load1=1.45 verdict=TRACK
POPULATION bytes=1073741824 n=1 workers=1 wall_ns=3223197408 (M)[AC]
BENCH name=G8d_1GiB_warm_source_durable n=1 required_n=10000 p50=1348466342 p99=1348466342 ci95_p50=[1348466342,1348466342] ci95_p99=[1348466342,1348466342] gate_p50=1500000000 gate_p99=2500000000 dropped=0 (M)[AC] power=Not charging load1=1.45 verdict=TRACK
POPULATION bytes=1073741824 n=1 workers=1 wall_ns=3365365850 (M)[AC]
BENCH name=G8d_1GiB_cold_source_durable n=1 required_n=10000 p50=1548681769 p99=1548681769 ci95_p50=[1548681769,1548681769] ci95_p99=[1548681769,1548681769] gate_p50=2200000000 gate_p99=3000000000 dropped=0 (M)[AC] power=Not charging load1=1.50 verdict=TRACK
G8s saving-frame-submitted endpoint UNAVAILABLE: snapshot/enqueue row is a regression guard only
save full-workload probe exit=3
```

## §35 — shared accounting and fatal structural failures implemented

Editor-large reuses editor_bench per-key injection/dequeue/submit/T4 accounting,
allocation guards and structural verdict policy through editor_accounting.h.
Missing or duplicate frames, missing populations, dropped samples, allocations,
mutation/journal counts, content failures, fixtures and failed jobs are fatal
in TRACK. Journaling is enabled for the large typing fixtures. Deep typing at
0.9 of the actual lines and the newline-free large source replace the line-1000
workaround. The three-job schedule opens a new fixture episode for each sample,
requires all job types outstanding at injection and validates find/save/index
results and the saved bytes afterward. TRACK is explicit.

Editor cached-mapping search is now labelled a regression, and default mode
refuses missing new-mapping G6 certification. The module findui count bench
measures that binding source condition. Attribution-only kernel floors remain
explicitly descriptive.

Red — the old large-row verdict accepted the missing population:

```text
BENCH name=editor_large_self_check_missing_population_fg_ingress_submit n=1 p50=1 p99=1 ci95=[1,1] gate_p50=1000000 gate_p99=2000000 pass=1 power=[AC]
FG self_check_missing_population mix=none samples=1 (G)G1<=1/2ms TRACK
PROBE editor_large missing_population TRACK exit=0 expected=1 (G)
```
Green — actual injection-delay accounting and structural refusal:

```text
editor_large_bench: UNAVAILABLE required fixtures
BENCH name=editor_large_self_check_missing_population_injection_T4_G1 n=1 required_n=10000 p50=1 p99=1 ci95_p50=[1,1] ci95_p99=[1,1] gate_p50=1000000 gate_p99=2000000 dropped=0 (M)[AC] power=Not charging load1=- verdict=TRACK
FG self_check_missing_population mix=none samples=1 (M)[AC] metric=G1 injection_T4 (G)1/2ms mode=TRACK structural=FAIL
PROBE editor_large missing_population TRACK exit=1 expected=1 (G)
PROBE editor_large injection_T4 includes queued delay ns=20237347 (M)[AC]
PROBE editor_large missing fixtures/content/allocation policy checked
```

The limited three-job probe completed with correct accounting/content but
refused its population. Newline-free typing reached the settle deadline;
that editor behavior is outside scope and was not repaired. Full large-row
populations remain unmeasured.

```text
BENCH name=editor_large_typing_deep_three_jobs_injection_T4_G1 n=1 required_n=10000 p50=619687228 p99=619687228 ci95_p50=[619687228,619687228] ci95_p99=[619687228,619687228] gate_p50=1000000 gate_p99=2000000 dropped=0 (M)[AC] power=Not charging load1=- verdict=TRACK
FG typing_deep_three_jobs mix=index+find+save outstanding at each injection samples=1 (G)G1<=1/2ms TRACK structural=FAIL
```

```text
editor_large_bench:98 failed: bench_now_ns() < dl
editor_large_bench:161 failed: settle(e) == 0
editor_large_bench:360 failed: probe(e,&r,b_never,true,&typing,UINT64_C(180000000000))==0
```

## §36 — deadlines and both lost-delivery/retirement checks complete

Savectl fixtures run in isolated child processes. Monotonic phase timers cover
admission, work, publication, retirement and blocking library shutdown/joins.
The outer supervisor bounds setup and termination/reaping. Timeout terminates
the fixture without freeing borrowed worker arguments or attempting a shutdown
join. Structural timeout codes are normalized before bench_merge_exit so TRACK
cannot swallow them. Harness tests cover phase expiry and stuck retirement.

Both self-checks now pass. The lost-completion fixture removes only its own
identity binding through work_mailbox_bind before any owner-thread pumping,
then deliberately discards the terminal message and keeps ticking. The final
run confirms actual terminal loss before supervised termination. The stalled
shutdown fixture holds its borrowed worker argument until the process is killed.

Red — the unsupervised retirement schedule required the external timeout.
The first completion probe only omitted the fallback drain and therefore was
not a deterministic lost-delivery proof: bound receivers could still deliver.
The retirement red is the primary failing regression for this finding. The
final completion self-check above corrects that test setup and demonstrates
actual terminal loss before expiry.

```text
PROBE savectl missing completion enters publication wait
missing completion exit=124
```

```text
PROBE savectl missing retirement entered shutdown
missing retirement exit=124
```
Green retirement — isolated fixture terminates with diagnostics:

```text
PROBE savectl missing retirement entered shutdown
savectl fixture FAIL: savectl missing retirement deadline; terminating fixture (no shutdown join)
savectl fixture FAIL: savectl missing retirement signal=9
PROBE savectl supervised_retirement exit=2 expected=2 (G)
PROBE savectl TRACK timeout remains fatal exit=1 expected=1 (G)
```

Green lost completion — terminal delivery is actually discarded before expiry:

```text
PROBE savectl missing completion enters publication wait
PROGRESS bytes=4096 samples=0 (M)[AC]
PROBE terminal completion discarded; controller still owns borrowed arguments
savectl fixture FAIL: savectl missing completion deadline; terminating fixture (no shutdown join)
savectl fixture FAIL: savectl missing completion signal=9
PROBE savectl supervised_completion exit=2 expected=2 (G)
PROBE savectl TRACK timeout remains fatal exit=1 expected=1 (G)
```

## §37 — staged reload model corrected

The deterministic coherent fixture is above the single-slice limit (G), so it
cannot depend on winning the rewrite race. The fuzzer pumps bounded installation
slices until success/error, checks unchanged tree/view outputs on BUSY, and
services owner events between slices. Additional schedules deliver an edit,
file event or logical close between slices and require suppression of the old
staged generation while preserving the live edited bytes.

Red — deterministic successful acquisition aborted at the old one-call oracle:

```text
fuzz/savectl_fuzz.c:376: assertion failed: savectl_take_reload(s, &replacement, &view) == SAVECTL_OK && replacement
==4== ERROR: libFuzzer: deadly signal
```
Green — deterministic install/edit/event/close schedules:

```text
Running: build/edit-2j9-reload-0
Executed build/edit-2j9-reload-0 in 82 ms
Running: build/edit-2j9-reload-1
Executed build/edit-2j9-reload-1 in 22 ms
Running: build/edit-2j9-reload-2
Executed build/edit-2j9-reload-2 in 22 ms
Running: build/edit-2j9-reload-3
Executed build/edit-2j9-reload-3 in 22 ms
```

## §38 — fixed-defect fuzz invariants enforced

Default folded long literals must complete with the independent correct count
and selected range; FIND_ERR_LIMIT is no longer accepted. A retired controller
must destroy immediately while unrelated work holds the reused physical slots;
BUSY is no longer accepted. Compile-time controlled negatives inject the old
bad result at those assertions; ordinary builds never enable them. They model
historical bad outcomes without editing either production module.

Red — controlled negatives were required to abort (G), but the old fuzzers
accepted both injected historical outcomes (exit zero (M)[AC]):

```text
Executed build/edit-2j9-long-seed in 6 ms
```

```text
Executed build/edit-2j9-reuse-seed in 44 ms
```
Green — revised invariants reject both controlled negatives:

```text
fuzz/findui_fuzz.c:392: assertion failed: state.complete && state.search_error == FIND_OK && state.match_count == 2 && state.selected.start == 0 && state.selected.end == qn
==12== ERROR: libFuzzer: deadly signal
```

```text
fuzz/savectl_fuzz.c:442: assertion failed: code == SAVECTL_OK
==16== ERROR: libFuzzer: deadly signal
```

## §39 — semantic scroll intent comparison fixed

A harness regression changes only extent padding between fragmented small-budget
slices. The one-line production comparison now checks semantic fields rather
than C padding. The test checks completed alignment/ready intent; it does not
invent an exact line number for an unindexed estimate.

Red:

```text
FAIL tests/harness_test.c:581: rc == SCROLL_OK && s.first_byte == 2048 && s.first_line == 1
PROBE logical_padding slices=5000 result=1 first_byte=0 expected=2048 (G)
```
Green:

```text
PROBE logical_padding slices=513 result=0 first_byte=2048 expected=2048 (G)
harness_test: 20253 checks, 0 failures
```

## Verification and remaining acceptance

- GCC 13 release make all: final run exited zero (M)[AC], with the required
  -Wall -Wextra -Werror -Wshadow -Wconversion flags.
- Clang 18 ASan/UBSan harness: passed (M)[AC].
- Full make check: the host-display run completed successfully (M)[AC],
  including 63 sanitizer binaries (M)[AC], replay CLI and display-lock contract checks.
  Additional whole-suite reruns after the final harness additions are still
  active in the shared display lane; the final standalone sanitizer harness
  already includes both new deadline checks and passes. The sandbox-only run
  failed display IPC setup. All display runs use Xvfb :99 exclusively.
- ASAN_OPTIONS=detect_leaks=0 was used for all sanitizer/fuzz runs because
  LeakSanitizer cannot run in this sandbox. Coordinator must rerun leaks on.
- No performance gate pass is claimed from loaded single probes. Full restored
  populations, G8s actual saving-frame submission, displayed G3z and authenticated
  G11 wakeup certification remain missing.
- The newline-free editor settle failure is reported and left outside scope.
- No source editor, harness utility verdict ABI, unrelated module defects,
  HANDOFF or worklog changes were made.

Clean fuzz runs against 60 seconds (G) per affected target:

```text
Done 27061 runs in 61 second(s)
```

```text
Done 829 runs in 61 second(s)
```

```text
Done 1896 runs in 61 second(s)
```

All fuzz completion counts/durations above are (M)[AC]. Decisions are in docs/decisions/edit-2j9.md.

Full-suite completion evidence (M)[AC]:

```text
check: 63 test binaries passed
test_replay_cli: all passed
display lock contract: owner PID, alias/timeout, make/scripts, nested inheritance, exit status PASS
```

Editor module fuzz verification (M)[AC], against 60 seconds (G):

```text
Done 17875 runs in 61 second(s)
```

Final acceptance status: implementation changes for §§34–39 are present;
functional regression evidence, release build and sanitizer/fuzz verification
are recorded. This is not a gate-certification closure: complete restored
benchmark populations and the explicitly unavailable host/display endpoints
remain for the coordinator. Historical injected-outcome negatives cover §38,
without replacing production module implementations. The shared verdict merger handles row codes zero/one/three; raw supervisor
error codes require normalization. This bead normalizes savectl errors locally
and leaves the broader verdict utility ABI unchanged.
