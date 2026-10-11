# edit-a2g worker report

The requested remaining probes are **default and green**. This continuation repairs estimated totals, lazy following-row rendering, cold resident wrapped follow, and findui window/replacement paging. The historical bead remains partial for the inherited deferred-navigation gaps described below. No `bd` or `git` command was run. Source/doc edits stayed in this worktree; native display use was restricted to the requested Xvfb display.

## Changes

- `src/editor/input.c`, `src/editor/editor.c`: keep estimated mutation totals within the empty-file sentinel / newline-per-byte ceiling; carry the estimate flag into layout; run bounded lazy row repair before warming; keep followed view metadata or use the returned visual row's logical metadata for edit completion.
- `src/layout/layout.h`, `layout.c`, `wrap.h`, `wrap.c`: explicit total exactness, checked estimate arithmetic in both wrap modes, and certified resident logical seeds for wrap queries. Edits crossing the logical start invalidate that seed.
- `src/find/find.c`, `visit.h`: private prepaid regex continuation state, retaining candidate lists, best endpoint, prefix matching and retry accounting; optional bulk mask visitor. The frozen public header is unchanged.
- `src/findui/findui.c`, `findui.h`, `private.h`, `literal.c`, `worker.c`, new `plan.c`: compressed ordinal/byte match plan in setup-reserved unlinked descriptors. Workers retrieve windows, uncached ordinals and replacement pages without another source search. Cancelled readers reserve their producer slot until physical retirement. Literal and regex CPU stages and sealed publication yield the bulk lane. Single-byte SIMD masks and the core case-sensitive literal kernels remain in use; folding/word KMP retains progress and cancellation polling through failure chains.
- `tests/editor_large_test.c`, `tests/findui_test.c`: default formerly opt-in probes, delete/undo/redo, cold wrapped editing, protected-source paging, irregular folded literal/regex plans, and a queued bulk marker before matcher completion. Source bytes become PROT_NONE after counting, making retrieval's source-read oracle independent of the scan hook.
- `tests/find_p1r_test.c`, `bench/find_probe.h`: rename the new private entry points in included-kernel fixtures. The initial broad build found duplicate symbols; these shims repair that integration.
- Design record, module STATUS files, HANDOFF and worklog updated with this continuation and its limits.

The near-end prefix imports from edit-2ty in `large.c` were read and preserved. No new globals or typing-path heap allocation were introduced. Match-plan descriptors are admitted at panel initialization; worker I/O uses an owner-only unlinked O_TMPFILE in `/tmp`. Failure to reserve or write that storage uses the existing admission/search memory error. Source leases and generations retain ownership through physical completion.

## Evidence conventions and derivations

Functional runs below are (M)[AC], stamped before execution from BAT0: Charging or Not charging. Process results come from subprocess/tool return codes; a negative subprocess result denotes the terminating signal. Sanitizers used `ASAN_OPTIONS=detect_leaks=0` because this sandbox rejects LeakSanitizer's ptrace requirements.

Fixture quantities (G) inherit the supplied probes. The deletion source is the review's newline-free prefix plus an equally sized newline suffix; unsigned wrap in the red run is `(1 - 100) modulo 2^64`, where the sentinel estimate is one and every suffix byte is a newline. Valid totals derive from `LF count + empty-file sentinel`, hence the ceiling `bytes + 1`. The sparse probe inherits 5000 matches, cache/page capacity 64, and source length `4 * 1024 * 1024` bytes. Its red replacement stages derive from `ceil(5000 / 64) = 79`. New scan stages are the post-request scan-hook count minus its pre-request count; the protected-source green probes additionally forbid every source byte read after counting. Bulk marker values are Boolean observations of worker-owned publication state on the sole bulk lane.

Cold fault counts are `after.ru_minflt - before.ru_minflt` for RUSAGE_THREAD. The loose cap in the pasted output is the unchanged cap from the inherited cold-bracket probe, not a new timing or allocation budget. The corrected wrapped fixture includes all edit continuations in that census; its initial attempt stopped before cursor repair completed and was corrected before the accepted red below.

Work/storage bounds derive from existing types/constants: the spool buffer is `floor(FIND_POLL_UNITS / sizeof(findui_plan_run))` records; each record contains fixed-width ordinal/count/endpoints/stride. Single-byte slices process at most `FIND_POLL_UNITS` masks, each containing the existing four-vector SSE2 scan (`4 * sizeof(__m128i) = 64` source bytes), or `FIND_POLL_UNITS * 64` source bytes per slice; cancellation is checked before each mask. Case-sensitive windows also cap dense matches from the existing FIND_MAX_OFFSETS capacity and preserve crossing matches through query-length overlap. Regex/folded steps use the existing polling quantum and complete a bounded byte/state boundary before yielding. No new duration or gate was introduced.

## Red runs, before implementation

All commands use `DISPLAY=:99 EDIT_DISPLAY=:99 ASAN_OPTIONS=detect_leaks=0`; the display number is the user's required coordinator display. Each selected test is also in its ordinary default suite.

### Estimated deletion, unwrapped

`P4_ESTIMATED_DELETE=1 build/san/tests/editor_large_test`; return `1` (M)[AC], captured process result.

```text
editor_large_test:688: FAIL editor_line_count(e) >= 1 && editor_line_count(e) <= editor_length(e) + 1u
P4-3 section 19: post-delete estimated lines=18446744073709551517 bytes=100 (M)[AC]
```

### Estimated deletion, wrapped

`P4_ESTIMATED_DELETE=1 P4_ESTIMATED_WRAP=1 build/san/tests/editor_large_test`; return `1` (M)[AC], captured process result.

```text
editor_large_test:688: FAIL editor_line_count(e) >= 1 && editor_line_count(e) <= editor_length(e) + 1u
P4-3 section 19: post-delete estimated lines=18446744073709551517 bytes=100 (M)[AC]
```

### Lazy first mapped viewport

`P4_LAZY_LONG_LINE=1 build/san/tests/editor_large_test`; return `1` (M)[AC], captured process result.

```text
editor_large_test:739: FAIL e->row_byte[1] == n + 1u
```

### Sparse window retrieval

`P4_SPARSE_WINDOW=1 build/san/tests/findui_test`; return `-6` (M)[AC], captured process result.

```text
P4-3 section 29: sparse window new source-search stages=1 (M)[AC]
tests/findui_test.c:831: assertion failed: after == before
```

### Sparse replacement pages

`P4_SPARSE_PAGES=1 build/san/tests/findui_test`; return `-6` (M)[AC], captured process result.

```text
P4-3 sparse replacement new source-search stages=79 (M)=scan-hook delta
tests/findui_test.c:858: assertion failed: after == before
```

### Matcher CPU-stage yield

`P4_SEARCH_YIELD=1 build/san/tests/findui_test`; return `-6` (M)[AC], captured process result.

```text
P4-3 matcher yields before completion: pattern=ab unfinished=0 (M)=bulk marker
tests/findui_test.c:896: assertion failed: __c11_atomic_load(&p.unfinished, 5)
```

### Cold wrapped edit completion

`P4_COLD_WRAP=1 build/san/tests/editor_large_test`; return `1` (M)[AC], captured process result.

```text
editor_large_test:670: FAIL faults < 128
P4-3 section 20: cold deep bracket paint minor faults=1470 (M)[AC], loose bound=128 (G)
```

## Green runs

The same selected cases passed after their implementations. Final-source default `findui_test` passes in both sanitizer and GCC release builds. Final-source `editor_large_test` passes its headless regressions before its inherited IPC fixture is denied. Cold wrapped editing uses the same helper/output label as the bracket probe; the wrapped branch is selected by P4_COLD_WRAP.

```text
P4-3 section 19: post-delete estimated lines=1 bytes=100 (M)[AC]
P4-3 estimated newline delete/undo/redo: passed
P4-3 section 19: post-delete estimated lines=1 bytes=100 (M)[AC]
P4-3 estimated newline delete/undo/redo: passed
P4-3 lazy first viewport following row: passed
P4-3 section 20: cold deep bracket paint minor faults=50 (M)[AC], loose bound=128 (G)
P4-3 section 29: sparse window new source-search stages=0 (M)[AC]
P4-3 sparse replacement new source-search stages=0 (M)=scan-hook delta
P4-3 irregular folded literal/regex plan: protected source window and ordinal passed
P4-3 matcher yields before completion: pattern=ab unfinished=1 (M)=bulk marker
P4-3 matcher yields before completion: pattern=[a]b unfinished=1 (M)=bulk marker
P4-3 matcher yields before completion: pattern=x+ unfinished=1 (M)=bulk marker
findui_test: all passed
```

Green process result is zero (M)[AC], captured return code for each focused run, the default findui suites, and the relevant layout/wrap/view/core find suites. Complete logs are under `build/worker-logs/edit-a2g-green-*`, `edit-a2g-release-*`, and `edit-a2g-last-*`.

Release near-end regressions retained their existing input-to-submit guard and active allocator:

```text
edit-2ty typing_path_allocations=0 (M)[AC]=counting allocator; required=0 (G)=CLAUDE law 2 guard=active
edit-2ty near-end input/submit: ok
edit-2ty typing_path_allocations=0 (M)[AC]=counting allocator; required=0 (G)=CLAUDE law 2 guard=active
edit-2ty near-end input/submit: ok
```

Allocation zero (M)[AC] is the active counting allocator's result; required zero (G) is CLAUDE law 2. Timing is T4 minus T0 from the trace; its existing comparison bound is EDITOR_SLICE_NS. This is headless ingress-to-submit verification; the next drive must verify displayed behavior against Sublime.

## Required build and full sanitizer census

Final `make all` passed with GCC release and the repository C11 strict warnings. Log: `build/worker-logs/edit-a2g-all-last.log`. Earlier duplicate-entry-point link failures were fixed, not suppressed.

Final `ASAN_OPTIONS=detect_leaks=0 DISPLAY=:99 EDIT_DISPLAY=:99 make check` builds the sanitizer suites and stops at denied CLI Unix-socket setup. Actual make return is two (M)[AC], captured tool result. Log: `build/worker-logs/edit-a2g-check-last.log`:

```text
== build/san/tests/cli_test
waiting for display :99 (held by unknown)
cli_test:215: FAIL ipc_server_init_display(&decoy, NULL, getenv("DISPLAY")) == IPC_OK
cli_test: §36 forced startup timeout correctly failed
cli_test:215: FAIL ipc_server_init_display(&decoy, NULL, getenv("DISPLAY")) == IPC_OK
make: *** [Makefile:105: check] Error 1
```

The separate full sweep has **66 suites (M)[AC] = TSV rows**, **49 passed (M)[AC] = zero-return rows**, and **17 restricted failures (M)[AC] = nonzero-return rows**. Census: `build/worker-logs/edit-a2g-suite-results-final.tsv`; individual logs: `build/worker-logs/edit-a2g-suites-final/`. Every runnable suite without native X/IPC prerequisites passed. Following the last cancellation-poll refactor, the affected sanitizer core/panel tests, release panel test and independent panel replay were rerun and passed; unrelated successful suites were not repeatedly rerun.

| Restriction | Suites requiring coordinator rerun |
|---|---|
| Native X socket / platform setup | `editor_close_test`, `file_kill_test`, `raster_test`, `refproto_test`, `refwin_test`, `refwin_timeout_test`, `x11_clip_review_test`, `x11_clip_test`, `x11_identity_test`, `x11_live_test`, `x11_order_test`, `x11_stall_test` |
| Unix-socket bind | `cli_test`, `editor_large_test`, `editor_p4_test`, `ipc_test` |
| Native X and launcher IPC portions | `editor_test` |

`gl_test`, `gl_gate_test`, and `gl_bench_test` pass available pure cases but skip native GL prerequisites. Their success is not native GL verification. Pure X input/alignment/display-guard/XI2 and the simulated scroll display suite pass.

`tools/test_replay_cli.sh build` passes with the same leak-disabled environment. Its first manual invocation omitted that flag and hit the sandbox's LeakSanitizer fatal error; the corrected rerun is green. The display-lock/refwin shell contract and native suites still need the coordinator's Xvfb rerun on the supplied display, with leak detection enabled outside this sandbox.

## Independent replay and tracked benches

Existing fuzzers were built and run as seed replays, not a timed fuzz campaign. The panel matrix contains **203 seeds (G, derived) = the existing 25-pattern table * regex/case/word Boolean combinations + the three existing lifecycle entry masks**. LibFuzzer reports **204 executions (M)[AC] = seeds plus its initial empty input**, with the independent match/replacement/history oracle green. Layout replay has **24 seeds (G, derived) = four text fixtures * wrap choices * gutter/slice choices**, and **25 executions (M)[AC] = seeds plus empty input**. Editor replay has **16 seeds (G, derived) = the existing eight-text repertoire * Shift Boolean**, and **17 executions (M)[AC] = seeds plus empty input**. Final panel replay log: `edit-a2g-findui-fuzz-final.log`; other replay logs and generated corpora are alongside it.

Bench quantities below are (M)[AC] from the existing harness: durations are completion/request or layout-run timestamp differences; p50/p99 are its sorted sample quantiles; n is its recorded sample population. Existing workload sizes, geometry and printed goal bounds come from each bench's constants and mode. No timing row closes a milestone or substitutes for the drive.

- `findui_bench --count --guard --track` completed its existing regression population and exact-count/cache checks. Its source size is the existing guard mode, not a large-source qualification. The diagnostic before/after mask probes used the bench's existing probe mode; load differed, so they do not establish a causal speedup or a variant pick.
- `layout_bench /tmp/edit-corpus ascii_code.c` completed; its full-layout p99 printed a goal miss. This is tracked under CLAUDE law 1.
- `find_bench --quick --row regex_no_prefix` printed correctness PASS / quick TRACK. It does not qualify the full-source scenario.
- `editor_large_bench --attribute-log` printed its existing insufficient-population structural refusal during the navigation row. This same limitation was already reported by edit-2ty; it was not fixed here.

Selected exact benchmark output:

```text
BENCH name=findui_count_64KiB_regression n=10000 required_n=10000 p50=5560482 p99=19917387 ci95_p50=[5556086,5564652] ci95_p99=[18932608,22000559] gate_p50=10000000 gate_p99=20000000 dropped=0 (M)[AC] power=Charging load1=- verdict=TRACK
BENCH name=layout_ascii_code.c_360x300 n=2000 p50=91410 p99=151641 ci95=[91217,91707] gate_p50=150000 gate_p99=150000 pass=0 power=[AC]
BENCH name=regex_no_prefix n=3 p50=474809982 p99=560491475 ci95=[472927307,560491475] gate_p50=0 gate_p99=0 pass=1 power=[AC]
FIND regex_no_prefix TRACK correctness=PASS (M)[AC]; limits=(G); unit=ns
BENCH name=editor_large_log_1g_navigation_regression n=516 required_n=10000 p50=1348828 p99=1853706 ci95_p50=[1327479,1360624] ci95_p99=[1807435,3076466] gate_p50=1000000 gate_p99=2000000 dropped=0 (M)[AC] power=Not charging load1=- verdict=TRACK
FG log_1g mix=index_active samples=516 (M)[AC] metric=navigation regression (G)1/2ms mode=TRACK structural=FAIL
editor_large_bench:246 failed: report_fg(name,&fg,"index_active",track,false) == 0
```

## What remains and findings outside this continuation

- Review §17/§18 retain synchronous explicit-coordinate/byte-seek waits, global helpers in `editor_jump_line` / `editor_set_cursor`, bounded-column admission limits, and incomplete worker-owned column/horizontal/visual-target following. Their existing correctness regressions pass; fully deferred navigation was not redesigned. These remain backlog under the supplied trial/fuzzer/trace triage rule.
- Resident cold wrapped follow is repaired; queries outside a certified resident anchor still have the inherited global-query fallback. Giant-line/nonresident wrap following remains unqualified. Estimated viewport numbering before the anchor still has the existing exact-anchor rebasing assumption; this continuation checks estimated **totals**. That additional numbering concern was found by inspection, without a new trial/fuzzer/trace trigger, and is recorded here rather than expanded into another work item.
- The initial broad-build symbol collision was in this continuation and fixed. Outside scope, `x11_order_test` continues into `plat_map` after denied initialization and reaches a null XCB connection, producing an ASan SEGV; its native setup failure is the prerequisite cause. Do not hide that diagnostic as a clean native pass. Its test/error-path fix belongs to the coordinator's triage.
- Native X/IPC/GL, leak-on checks, display-lock/refwin contracts and the next same-build Astra comparison drive remain for the coordinator. No window or comparison drive could be verified in this socket-denied sandbox. No bead closure or user handover is claimed.

One bead only; stopping with the tree building and module status/report artifacts written.
