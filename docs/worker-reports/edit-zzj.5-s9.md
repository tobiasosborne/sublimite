# edit-zzj.5 — session 9 M0 benchmark report

Baseline: `c0ae480f06ba409f730cb425facb753f4cab67cf` (`wt/edit-zzj.5`, fresh from main; deep-file fix included). Scope is measurement and documentation only. No source, bench, test, Makefile or STATUS edits; no new globals, git writes, `bd`, HANDOFF or worklog edits.

The commands, every printed verdict and available CI/sample count, before/after BAT0 and uptime/load stamps, gate tables, all misses/refusals and output tails are in [docs/bench/m0.md](../bench/m0.md). The companion record is the measurement evidence; this report lists every miss/refusal again for coordinator filing.

## Scope results

| Required item | Result |
|---|---|
| G1 null/raster/GL-fallback; deep file | Default editor run reached the 0.9 × lines position. Null scenarios printed PASS. Raster stopped on its endpoint/guard assertion before a verdict. The separate GL-requested serial run fell back to raster and completed with a timing MISS plus 3,802 counted allocations (M)[AC], structural FAIL. No native GL acceptance claim. |
| G3 | Editor P4 section and renderer/raster rows are recorded when completed; renderer-only rows do not certify complete G3. Xvfb MSC is synthetic. |
| G4a | X11 platform startup subset is recorded; it excludes exec, back-buffer completion and input acceptance. Full G4a remains unproven. |
| G11 | Separate idle-only section records null REFUSED (too few samples), raster CPU MISS and GL-fallback CPU MISS. All printed wakeup structural checks are OK. |
| editor_large G5/G6/G7 | TRACK campaign completed, including log_1g and sparse_10g. Every miss is listed below even though TRACK exits zero. The sparse G7 comparison is a 1 GiB reference applied to a 10 GiB TRACK row, not a binding 10 GiB gate. |
| piece/scan/lineidx/find/file module benches | Existing .args are used verbatim. Core results and any unfinished required file run are shown in the inventory below. Quick find is scaled TRACK, not a full 1 GiB G6/G6v verdict. |
| Inventory | 31 distinct existing binaries invoked with recorded exits (M)[AC] out of 31 discovered binaries (M)[AC]. Separate section invocations are also included. |
| Not invoked or missing an exit record | None. |

## Red measurement evidence and green verification

TDD red/green fix pairing is not applicable: this bead explicitly forbids source, test and bench changes. No new findings are fixed, and no passing correctness suite is represented as a fix for a performance miss. The observed red benchmark remains red:

```text
DISPLAY=:99 EDIT_DISPLAY=:99 timeout 600 build/bench/editor_bench
POSITION raster byte=966367641 index=progressing workload=serial edits=alternating_insert_backspace
editor_bench:474 failed: !measured.invalid && measured.completed == end && !measured.guard && !measured.suspended
EXIT 1
```

Green release verification: `DISPLAY=:99 EDIT_DISPLAY=:99 make all` exited 0 with GCC 13.3.0 and `-std=c11 -Wall -Wextra -Werror -Wshadow -Wconversion`. Pasted release output tail:

```text
make: Nothing to be done for 'all'.

ALL_EXIT=0
Original full release build also exited 0.
```

Sanitizer command: `DISPLAY=:99 EDIT_DISPLAY=:99 ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 timeout 900 make check` (TMPDIR and XDG_RUNTIME_DIR point to `$REPO/build/m0/runtime`). Clang 18.1.3; completed with exit 0: 60 sanitizer test binaries plus replay CLI passed (M)[AC].

```text
== build/san/tests/x11_xi2_test
x11_xi2_test: ok
check: 60 test binaries passed
== tools/test_replay_cli.sh
ok:   --speed=inf rc=2 replay: --speed must be a finite number > 0
ok:   --speed=nan rc=2 replay: --speed must be a finite number > 0
ok:   --speed=0 rc=2 replay: --speed must be a finite number > 0
ok:   --speed=-1 rc=2 replay: --speed must be a finite number > 0
ok:   --speed=2 rc=0
test_replay_cli: all passed

CHECK_EXIT=0
```

Editor-module fuzz command: `DISPLAY=:99 EDIT_DISPLAY=:99 ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 timeout 120 build/fuzz/editor_fuzz -max_total_time=60 -max_len=1024`. Requested fuzz duration: 60 seconds (G). Completed cleanly; exit 0.

```text
Done 10457 runs in 61 second(s)

FUZZ_EXIT=0
```

The first sanitizer check exited 2 because the runner-created runtime directory had group/world permissions. IPC correctly rejected it with `IPC startup error 2`; `cli_test:77` and `cli_test:137` failed. The runner directory was changed to mode 0700, then the unchanged suite was rerun. This was an environment correction, not a source fix.

```text
base_test: PASS
== build/san/tests/cli_test
sublimite: IPC startup error 2
cli_test: editor exited or stopped before window (status=512)
cli_test:77: FAIL waited == 0
cli_test:137: FAIL ran == 0
make: *** [Makefile:101: check] Error 1

CHECK_EXIT=2
```

LeakSanitizer cannot run in this worker sandbox: **detect_leaks=0** was used. The coordinator must rerun with leaks enabled. All display-dependent verification uses :99 only.

## Every miss and refusal for coordinator filing

- `editor: process exit 1` — editor_bench:474 failed: !measured.invalid && measured.completed == end && !measured.guard && !measured.suspended Within 20%: n/a; no completed quantified row.
- `editor_large: editor_large_log_1g_G7_open_index_exact` — **MISS**; p50/p99 230853642/314130939 ns (M)[AC], limits 80000000/125000000 ns (G); within 20%: **no**. Measures open/new mapping to exact index publication; 10 GiB rows are TRACK against a 1 GiB reference.
- `editor_large: editor_large_log_1g_G6_find_1worker` — **MISS**; p50/p99 204935299/215390542 ns (M)[AC], limits 80000000/125000000 ns (G); within 20%: **no**. Measures find/publication or primitive scan basis as labeled.
- `editor_large: editor_large_sparse_10g_G5_open_first_viewport` — **MISS**; p50/p99 11934145/13885607 ns (M)[AC], limits 6000000/9000000 ns (G); within 20%: **no**. Measures open request to correct first viewport submission.
- `editor_large: editor_large_sparse_10g_G7_open_index_exact` — **MISS**; p50/p99 3681808226/4310969592 ns (M)[AC], limits 80000000/125000000 ns (G); within 20%: **no**. Measures open/new mapping to exact index publication; 10 GiB rows are TRACK against a 1 GiB reference.
- `piece: oneline_10g/open` — **SKIP; empty-sample MISS**; p50/p99 0/0 ns (M)[AC], limits 0/0 ns (G); within 20%: **n/a**. Measures the named module regression guard; no end-to-end gate claim.
- `scan: scan_count` — **MISS**; p50/p99 5.8/4.47 GB/s (M)[AC], limits 12/12 GB/s (G); within 20%: **no**. Measures find/publication or primitive scan basis as labeled.
- `scan: scan_find_nth_newline` — **MISS**; p50/p99 5.42/2.93 GB/s (M)[AC], limits 12/12 GB/s (G); within 20%: **no**. Measures find/publication or primitive scan basis as labeled.
- `scan: scan_count_G6_basis` — **MISS**; p50/p99 5.8/4.47 GB/s (M)[AC], limits 16/12 GB/s (G); within 20%: **no**. Measures find/publication or primitive scan basis as labeled.
- `scan: process exit 1` — Nonzero exit; inspect raw verdicts/output tail. Within 20%: n/a; no completed quantified row.
- `lineidx: G7_index_1g_fixture_warm` — **MISS**; p50/p99 752117714/1145303783 ns (M)[AC], limits 80000000/125000000 ns (G); within 20%: **no**. Measures open/new mapping to exact index publication; 10 GiB rows are TRACK against a 1 GiB reference.
- `lineidx: G7j_unindexed_viewport_90pct_fixture_warm` — **MISS**; p50/p99 622345725/845344724 ns (M)[AC], limits 30000000/50000000 ns (G); within 20%: **no**. Measures unindexed/indexed jump and viewport/query endpoint as labeled.
- `lineidx: TRACK_G7j_worker_partial_seek_viewport_90pct_fixture_warm` — **MISS**; p50/p99 299434398/391778666 ns (M)[AC], limits 30000000/50000000 ns (G); within 20%: **no**. Measures unindexed/indexed jump and viewport/query endpoint as labeled.
- `lineidx: process exit 1` — Nonzero exit; inspect raw verdicts/output tail. Within 20%: n/a; no completed quantified row.
- `gl: process exit 2` — libEGL warning: DRI3 error: Could not get DRI3 device Within 20%: n/a; no completed quantified row.
- `trace: trace_record` — **MISS**; p50/p99 51/65 ns (M)[AC], limits 50/0 ns (G); within 20%: **yes**. Measures the named module regression guard; no end-to-end gate claim.
- `trace: process exit 1` — Nonzero exit; inspect raw verdicts/output tail. Within 20%: n/a; no completed quantified row.
- `indent: indent_long_line_enter includes_zero_capacity` — **MISS**; p50/p99 10.21/27.151 us (M)[AC], limits 0/20 us (G); within 20%: **no**. Measures the named module regression guard; no end-to-end gate claim.
- `indent: indent_long_line_brace` — **MISS**; p50/p99 18.685/30.041 us (M)[AC], limits 0/20 us (G); within 20%: **no**. Measures the named module regression guard; no end-to-end gate claim.
- `indent: process exit 1` — Nonzero exit; inspect raw verdicts/output tail. Within 20%: n/a; no completed quantified row.
- `tabs: tabs_plus_caller_undo_memory` — **TRACK; memory MISS**; p50/p99 1829280/1829280 B (M)[AC], limits 1000000/1000000 B (G); within 20%: **no**. Measures owned memory bound (module allocation census).
- `tabs: process exit 1` — Nonzero exit; inspect raw verdicts/output tail. Within 20%: n/a; no completed quantified row.
- `utf8: utf8_decode_width_500_runs` — **MISS**; p50/p99 5280/9994 ns (M)[AC], limits 5000/10000 ns (G); within 20%: **yes**. Measures the named module regression guard; no end-to-end gate claim.
- `utf8: process exit 1` — Nonzero exit; inspect raw verdicts/output tail. Within 20%: n/a; no completed quantified row.
- `view: view_log_1g.txt_document_bulk1_command` — **TRACK; threshold MISS**; p50/p99 1839/8558034 ns (M)[AC], limits 5000000/5555555 ns (G); within 20%: **no**. Measures the named module regression guard; no end-to-end gate claim.
- `view: view_log_1g.txt_selection_bulk1_command` — **TRACK; threshold MISS**; p50/p99 3069/8522716 ns (M)[AC], limits 5000000/5555555 ns (G); within 20%: **no**. Measures the named module regression guard; no end-to-end gate claim.
- `view: view_oneline_1g.txt_selection_1MiB_replace_bulk1_command` — **TRACK; threshold MISS**; p50/p99 1532175/1627352 ns (M)[AC], limits 1000000/2000000 ns (G); within 20%: **no**. Measures the named module regression guard; no end-to-end gate claim.
- `view: view_oneline_1g.txt_selection_1MiB_replace_bulk3_command` — **TRACK; threshold MISS**; p50/p99 1502584/1567799 ns (M)[AC], limits 1000000/2000000 ns (G); within 20%: **no**. Measures the named module regression guard; no end-to-end gate claim.
- `view: view_unicode.txt_selection_1MiB_replace_bulk1_command` — **TRACK; threshold MISS**; p50/p99 1188934/1341686 ns (M)[AC], limits 1000000/2000000 ns (G); within 20%: **yes**. Measures the named module regression guard; no end-to-end gate claim.
- `view: view_unicode.txt_selection_1MiB_replace_bulk3_command` — **TRACK; threshold MISS**; p50/p99 1198436/1245328 ns (M)[AC], limits 1000000/2000000 ns (G); within 20%: **yes**. Measures the named module regression guard; no end-to-end gate claim.
- `scroll: process exit 124` — timeout 600 expired before complete coverage/final verdict; JUMP (M)[AC] load1=3.65 power=Not charging sample=4559 ns=192810390 target=8053057 byte=966366840 prefix_chunks=14745 correct=1 Within 20%: n/a; no completed quantified row.
- `editor_idle: editor_null_G11_process_cpu` — **REFUSED**; p50/p99 80985/95730 ns (M)[AC], limits 100000/200000 ns (G); within 20%: **yes**. Measures summed process CPU per blink; wakeup policy reported separately.
- `editor_idle: editor_raster_G11_process_cpu` — **MISS**; p50/p99 159922/173571 ns (M)[AC], limits 100000/200000 ns (G); within 20%: **no**. Measures summed process CPU per blink; wakeup policy reported separately.
- `editor_idle: editor_gl_fallback_raster_G11_process_cpu` — **MISS**; p50/p99 180189/221937 ns (M)[AC], limits 100000/200000 ns (G); within 20%: **no**. Measures summed process CPU per blink; wakeup policy reported separately.
- `editor_idle: process exit 1` — libEGL warning: DRI3 error: Could not get DRI3 device; sublimite: EGL init failed: EGL/GL/Present unavailable (code=-10); using raster; no retry Within 20%: n/a; no completed quantified row.
- `editor_gl_typing: editor_gl_fallback_raster_serial_injection_T4_G1` — **MISS**; p50/p99 17028158/35210473 ns (M)[AC], limits 1000000/2000000 ns (G); within 20%: **no**. Measures input/edit to submit, or the named typing-path component.
- `editor_gl_typing: process exit 1` — libEGL warning: DRI3 error: Could not get DRI3 device; sublimite: EGL init failed: EGL/GL/Present unavailable (code=-10); using raster; no retry; G1 gl_fallback_raster workload=serial keys=10000 completed=10000 arrivals_active=10000 coalesced=0 allocations=3802 mutations=10000 journal=10000 structural=FAIL mode=GATE (M)[AC] Within 20%: n/a; no completed quantified row.
- `editor_p4: editor_null_100tabs_ingress_T5_G3` — **REFUSED**; p50/p99 640152/693554 ns (M)[AC], limits 5000000/5560000 ns (G); within 20%: **no**. Measures full-frame or named frame component through its reported endpoint.
- `editor_p4: editor_null_100tabs_minimap_inside_frame` — **REFUSED**; p50/p99 12959/18991 ns (M)[AC], limits 0/500000 ns (G); within 20%: **no**. Measures the named module regression guard; no end-to-end gate claim.
- `editor_p4: editor_raster_100tabs_ingress_T5_G3` — **MISS**; p50/p99 7585615/9970049 ns (M)[AC], limits 5000000/5560000 ns (G); within 20%: **no**. Measures full-frame or named frame component through its reported endpoint.
- `editor_p4: editor_raster_100tabs_minimap_inside_frame` — **REFUSED**; p50/p99 12560/19705 ns (M)[AC], limits 0/500000 ns (G); within 20%: **no**. Measures the named module regression guard; no end-to-end gate claim.
- `editor_p4: editor_second_invocation_exec_open_ACK_exit` — **REFUSED**; p50/p99 3973042/4366503 ns (M)[AC], limits 0/10000000 ns (G); within 20%: **no**. Measures the named module regression guard; no end-to-end gate claim.
- `editor_p4: process exit 1` — Nonzero exit; inspect raw verdicts/output tail. Within 20%: n/a; no completed quantified row.
- `file_priority: G8d_1MiB_warm_source` — **GATE; verdict not printed; threshold MISS**; p50/p99 13.215/19.875 ms (M)[AC], limits 10/50 ms (G); within 20%: **no**. Measures durable save completion.
- `file_priority: primitive_prefix_log1g_busy0` — **TRACK; verdict not printed; empty-sample MISS**; p50/p99 0/0 ms (M)[AC], limits 0/0 ms (G); within 20%: **n/a**. Measures the named module regression guard; no end-to-end gate claim.
- `file_priority: G5_cold_log1g_busy0` — **GATE; verdict not printed; empty-sample MISS**; p50/p99 0/0 ms (M)[AC], limits 10/110 ms (G); within 20%: **n/a**. Measures open request to correct first viewport submission.
- `file_priority: process exit 1` — FAIL G8d_1MiB_warm_source: missing/failed completion, output, endpoint, or gate; FAIL required cold source residency [temporary path redacted]; FAIL G8d_1GiB_cold_source: missing/failed completion, output, endpoint, or gate Within 20%: n/a; no completed quantified row.
- `savectl: process exit 124` — timeout 600 expired before complete coverage/final verdict; partial output tail is preserved below Within 20%: n/a; no completed quantified row.
- `raster_priority: A_idle_full_frame_warm_15px_subset_TRACK` — **TRACK; G3 reference comparison only; threshold MISS**; p50/p99 6973009/9515770 ns (M)[AC], limits 5000000/5555555 ns (G); within 20%: **no**. Measures full-frame or named frame component through its reported endpoint.
- `raster_priority: process exit 124` — timeout 600 expired before complete coverage/final verdict; partial output tail is preserved below Within 20%: n/a; no completed quantified row.
- `layout: layout_ascii_code.c_360x300` — **MISS**; p50/p99 128512/288024 ns (M)[AC], limits 150000/150000 ns (G); within 20%: **no**. Measures the named module regression guard; no end-to-end gate claim.
- `layout: layout_log_1g.txt_360x300` — **MISS**; p50/p99 234651/254256 ns (M)[AC], limits 150000/150000 ns (G); within 20%: **no**. Measures the named module regression guard; no end-to-end gate claim.
- `layout: layout_wrapped_unicode.txt_360x300` — **MISS**; p50/p99 302590/499921 ns (M)[AC], limits 300000/300000 ns (G); within 20%: **no**. Measures the named module regression guard; no end-to-end gate claim.
- `layout: layout_wrapped_oneline_1g.txt_360x300` — **MISS**; p50/p99 266426/304980 ns (M)[AC], limits 300000/300000 ns (G); within 20%: **yes**. Measures the named module regression guard; no end-to-end gate claim.
- `layout: process exit 1` — Nonzero exit; inspect raw verdicts/output tail. Within 20%: n/a; no completed quantified row.
- `prewake: process exit 1` — usage: build/bench/prewake_bench --protocol|--display-check [--real-display] (commands N/H/I on stdin; caller supplies >=15s idle) Within 20%: n/a; no completed quantified row.
- `zygote: process exit 1` — variants/P4.14/a/open.c:29:10: error: ‘editor’ has no member named ‘index’; variants/P4.14/editor.c:302:96: error: control reaches end of non-void function [-Werror=return-type]; variants/P4.14/editor.c:303:72: error: control reaches end of non-void function [-Werror=return-type] Within 20%: n/a; no completed quantified row.
- `raster: process exit 143` — ORCHESTRATION_ABORT: duplicate raster run cancelled; priority raster measurement already covers this binary. No new file rerun is scheduled. Within 20%: n/a; no completed quantified row.

- `piece`: `piece_bench: row oneline_10g SKIP (needs --huge)`
- `piece`: `open                          0.000        0.000 ns          0  none                     SKIP`
- `lineidx`: `UNVALIDATED G7 cold (G) 600/800 ms; G7j cold (G) 250/350 ms: run --cold with manual verified drop_caches per sample`
- `gl`: `TRACK name=A_egl_scroll_600_15px status=SKIP verdict=TRACK reason=Xvfb_:99_has_no_real_vblank frames=0 requested_frames=600 evidence=(M)[AC] power_status="Not charging" load1=11.36`
- `gl`: `BENCH name=egl_init_15px status=SKIP reason=EGL_or_matching_Present_unsupported result=-10 power=[AC]`
- `editor_gl_typing`: `G1 gl_fallback_raster workload=serial keys=10000 completed=10000 arrivals_active=10000 coalesced=0 allocations=3802 mutations=10000 journal=10000 structural=FAIL mode=GATE (M)[AC]`
- `file_priority`: `FAIL G8d_1MiB_warm_source: missing/failed completion, output, endpoint, or gate`
- `file_priority`: `FAIL required cold source residency /tmp/edit-corpus/log_1g.txt`
- `file_priority`: `FAIL required cold source residency [temporary path redacted]`
- `file_priority`: `FAIL G8d_1GiB_cold_source: missing/failed completion, output, endpoint, or gate`
- `raster_priority`: `TRACK name=A_raster_scroll_600_15px status=SKIP verdict=TRACK reason=Xvfb_:99_has_no_real_vblank frames=0 requested_frames=600 evidence=(M)[AC] power_status="Not charging" load1=3.51`
- `raster_priority`: `TRACK name=A_raster_scroll_600_30px status=SKIP verdict=TRACK reason=Xvfb_:99_has_no_real_vblank frames=0 requested_frames=600 evidence=(M)[AC] power_status="Not charging" load1=3.51`
- `raster_priority`: `TRACK name=B_raster_scroll_600_15px status=SKIP verdict=TRACK reason=Xvfb_:99_has_no_real_vblank frames=0 requested_frames=600 evidence=(M)[AC] power_status="Not charging" load1=3.51`
- `raster_priority`: `TRACK name=B_raster_scroll_600_30px status=SKIP verdict=TRACK reason=Xvfb_:99_has_no_real_vblank frames=0 requested_frames=600 evidence=(M)[AC] power_status="Not charging" load1=3.51`
- `raster_priority`: `SKIP G3/G3i: minimap integration missing; ingress -> T5 subset is TRACK only`
- `raster_priority`: `SKIP G3z: real-vblank fixture unverified (Xvfb :99 has no real vblank); not measured`
- `raster_priority`: `SKIP acceptance contention: index/find/save integration absent; synthetic memory/queue cases are TRACK`
- `raster`: `TRACK name=A_raster_scroll_600_15px status=SKIP verdict=TRACK reason=Xvfb_:99_has_no_real_vblank frames=0 requested_frames=600 evidence=(M)[AC] power_status="Not charging" load1=4.54`
- `raster`: `TRACK name=A_raster_scroll_600_30px status=SKIP verdict=TRACK reason=Xvfb_:99_has_no_real_vblank frames=0 requested_frames=600 evidence=(M)[AC] power_status="Not charging" load1=4.54`
- `raster`: `TRACK name=B_raster_scroll_600_15px status=SKIP verdict=TRACK reason=Xvfb_:99_has_no_real_vblank frames=0 requested_frames=600 evidence=(M)[AC] power_status="Not charging" load1=4.54`
- `raster`: `TRACK name=B_raster_scroll_600_30px status=SKIP verdict=TRACK reason=Xvfb_:99_has_no_real_vblank frames=0 requested_frames=600 evidence=(M)[AC] power_status="Not charging" load1=4.54`
- `raster`: `SKIP G3/G3i: minimap integration missing; ingress -> T5 subset is TRACK only`
- `raster`: `SKIP G3z: real-vblank fixture unverified (Xvfb :99 has no real vblank); not measured`
- `raster`: `SKIP acceptance contention: index/find/save integration absent; synthetic memory/queue cases are TRACK`

## Decisions and limitations

- No implementation design choice or fix was made, so no architecture decision file is required. Existing benchmarks and their verdict rules were preserved.
- The laptop is measured under its current load, without waiting for a quiet window. The main sequence inadvertently started raster again before the priority sequence had fully finalized; the duplicate was cancelled before warm verdicts and further file reruns were prevented. Its command, stamps, init-only output and interruption are retained, not treated as a gate result. The extra section/priority sequence overlaps the long scroll/save sequence; build/check/fuzz also overlap parts of the campaign. Exact stamps and times disclose this additional load. No new implementation variant was created or selected; built-in paired comparisons are preserved verbatim. Isolated timing conclusions are unavailable.
- G1 section reruns recover rows omitted by an earlier assertion; they are not retries to seek a passing timing. Near-gate rows are marked for rerun eligibility in M0. All misses outside the band are retained without a timing-driven fix.
- Legacy harness PASS, undersampled rows, TRACK and renderer/platform subsets stay explicitly qualified. Only existing printed verdicts are quoted; missing confidence intervals are recorded as not printed. Errors and timeouts are never discarded.
- Xvfb :99, synthetic MSC: G2c/optical latency and real displayed-refresh G3z remain unmeasured. GL admission fails here and falls back to raster. Full G4a and manually verified cold coverage remain open where the benches say unvalidated/unmeasured.
- No corpus fixtures were regenerated or changed. The default schedules for scroll/save may exceed the per-binary timeout; the report records partial tails rather than modifying them. Any session-boundary interruption is distinguished from a completed gate verdict.

## Missing acceptance / incomplete coverage

- Every discovered benchmark binary was invoked. Full green M0 acceptance is not established: recorded misses/refusals remain open for coordinator filing.
- Scroll and savectl reached timeout 600 without final aggregate coverage. Raster reached timeout 600 after the A warm full-frame, partial-row and paired-kernel records; after-idle, remaining contention and B rows are incomplete.
- Zygote produced no launch distributions because its legacy variants fail to compile against the current private editor structure. Prewake default discovery produced usage rejection; the stdin/idle protocol campaign is not performed.
- Native GL and optical/cadence acceptance, full G4a, full qualifying G3/G11 schedules, all G1 raster/fallback scenarios and cold-source coverage after residency failures remain incomplete. Scaled quick find does not prove the full 1 GiB G6/G6v gates.

## Completed run inventory

| Run | Exact binary command | Exit | Duration (M)[AC] |
|---|---|---|---|
| `x11` | `DISPLAY=:99 EDIT_DISPLAY=:99 timeout 600 build/bench/x11_bench` | 0 | 1.395 s |
| `editor` | `DISPLAY=:99 EDIT_DISPLAY=:99 timeout 600 build/bench/editor_bench` | 1 | 113.824 s |
| `editor_large` | `DISPLAY=:99 EDIT_DISPLAY=:99 timeout 600 build/bench/editor_large_bench --track` | 0 | 26.789 s |
| `piece` | `DISPLAY=:99 EDIT_DISPLAY=:99 timeout 600 build/bench/piece_bench --quick` | 0 | 5.913 s |
| `scan` | `DISPLAY=:99 EDIT_DISPLAY=:99 timeout 600 build/bench/scan_bench` | 1 | 16.746 s |
| `lineidx` | `DISPLAY=:99 EDIT_DISPLAY=:99 timeout 600 build/bench/lineidx_bench` | 1 | 36.396 s |
| `find` | `DISPLAY=:99 EDIT_DISPLAY=:99 timeout 600 build/bench/find_bench --quick` | 0 | 12.010 s |
| `gl` | `DISPLAY=:99 EDIT_DISPLAY=:99 timeout 600 build/bench/gl_bench` | 2 | 1.808 s |
| `base` | `DISPLAY=:99 EDIT_DISPLAY=:99 timeout 600 build/bench/base_bench` | 0 | 0.314 s |
| `trace` | `DISPLAY=:99 EDIT_DISPLAY=:99 timeout 600 build/bench/trace_bench` | 1 | 0.124 s |
| `font` | `DISPLAY=:99 EDIT_DISPLAY=:99 timeout 600 build/bench/font_bench` | 0 | 0.813 s |
| `keys` | `DISPLAY=:99 EDIT_DISPLAY=:99 timeout 600 build/bench/keys_bench` | 0 | 0.329 s |
| `indent` | `DISPLAY=:99 EDIT_DISPLAY=:99 timeout 600 build/bench/indent_bench` | 1 | 0.877 s |
| `findui` | `DISPLAY=:99 EDIT_DISPLAY=:99 timeout 600 build/bench/findui_bench` | 0 | 0.812 s |
| `minimap` | `DISPLAY=:99 EDIT_DISPLAY=:99 timeout 600 build/bench/minimap_bench` | 0 | 1.164 s |
| `tabs` | `DISPLAY=:99 EDIT_DISPLAY=:99 timeout 600 build/bench/tabs_bench` | 1 | 1.261 s |
| `ipc` | `DISPLAY=:99 EDIT_DISPLAY=:99 timeout 600 build/bench/ipc_bench` | 0 | 0.098 s |
| `work` | `DISPLAY=:99 EDIT_DISPLAY=:99 timeout 600 build/bench/work_bench` | 0 | 3.718 s |
| `piece_reclaim` | `DISPLAY=:99 EDIT_DISPLAY=:99 timeout 600 build/bench/piece_reclaim_bench --quick` | 0 | 0.219 s |
| `undo` | `DISPLAY=:99 EDIT_DISPLAY=:99 timeout 600 build/bench/undo_bench` | 0 | 0.830 s |
| `utf8` | `DISPLAY=:99 EDIT_DISPLAY=:99 timeout 600 build/bench/utf8_bench` | 1 | 22.312 s |
| `view` | `DISPLAY=:99 EDIT_DISPLAY=:99 timeout 600 build/bench/view_bench` | 0 | 2.156 s |
| `render` | `DISPLAY=:99 EDIT_DISPLAY=:99 timeout 600 build/bench/render_bench` | 0 | 0.929 s |
| `journal` | `DISPLAY=:99 EDIT_DISPLAY=:99 timeout 600 build/bench/journal_bench` | 0 | 8.502 s |
| `scroll` | `DISPLAY=:99 EDIT_DISPLAY=:99 timeout 600 build/bench/scroll_bench` | 124 | 600.071 s |
| `editor_idle` | `DISPLAY=:99 EDIT_DISPLAY=:99 timeout 600 build/bench/editor_bench --idle-only` | 1 | 37.790 s |
| `editor_gl_typing` | `DISPLAY=:99 EDIT_DISPLAY=:99 EDIT_BACKEND=gl timeout 600 build/bench/editor_bench --no-idle --no-p4 --serial-only` | 1 | 213.265 s |
| `editor_p4` | `DISPLAY=:99 EDIT_DISPLAY=:99 timeout 600 build/bench/editor_bench --p4-only` | 1 | 6.367 s |
| `file_priority` | `DISPLAY=:99 EDIT_DISPLAY=:99 timeout 600 build/bench/file_bench` | 1 | 224.720 s |
| `savectl` | `DISPLAY=:99 EDIT_DISPLAY=:99 timeout 600 build/bench/savectl_bench` | 124 | 600.188 s |
| `raster_priority` | `DISPLAY=:99 EDIT_DISPLAY=:99 timeout 600 build/bench/raster_bench` | 124 | 600.018 s |
| `layout` | `DISPLAY=:99 EDIT_DISPLAY=:99 timeout 600 build/bench/layout_bench` | 1 | 7.939 s |
| `prewake` | `DISPLAY=:99 EDIT_DISPLAY=:99 timeout 600 build/bench/prewake_bench` | 1 | 0.013 s |
| `zygote` | `DISPLAY=:99 EDIT_DISPLAY=:99 timeout 600 build/bench/zygote_bench` | 1 | 0.251 s |
| `raster` | `DISPLAY=:99 EDIT_DISPLAY=:99 timeout 600 build/bench/raster_bench` | 143 | 75.054 s |

Missing acceptance is explicit above; coordinator filing and leak-on verification remain coordinator work. No scope expansion or source repair is authorized.
