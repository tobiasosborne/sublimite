# edit-73j worker report

The supplied priority wiring is implemented in this worktree. Native acceptance and the next Astra comparison drive remain with the coordinator. No `bd` or `git` command was run, and no other worktree was edited. Ctrl+F/H consumption and the editor font runtime hook were absent in the supplied tree, so this bead implements them directly.

## Changed files and behavior

- `src/editor/input.c`: prepared text hits; click/drag, Shift-click, double-click word and triple-click line selection, including word/line dragging; vertical wheel accumulation and resident navigation; find chord routing and replacement host callbacks/history accounting.
- `src/editor/findpanel.c` (new): reserved findui controller, query/replacement field editing and selection, global chord pass-through, next/previous, options, replacement/cancellation, source leases and bottom-row painting. Off-screen wrapped and clipped matches use resident line anchors and bounded follow queries.
- `src/editor/minimap.c` (new): immutable worker summaries, identity/revision/height-checked mailbox publication, physical completion before source retirement.
- `src/editor/paint.c`: source-free cached minimap painting and approximate byte navigation; binary lookup of find highlights; panel rows; baked ASCII and runtime Unicode title glyphs, including scratch-grid descriptor updates and resumable cold composition.
- `src/editor/private.h`, `buffers.c`, `open.c`, `editor.c`: fixed reservations, owners/leases, mailbox routing, lifecycle, activation/resize/capture reset and continuation wiring. Font preparation/adoption occurs on the worker before backend initialization; both grids and titles bind the owner and backend limits reserve the runtime tables/pages.
- `src/layout/layout.h`, `layout.c`, `wrap.c`: optional prepared cluster/cell hit callback and a bounded visual-row query with a certified logical-line anchor. Existing callers leave the callback unset.
- `src/font/font.h`, `runtime.c`: expose the existing runtime/GL glyph-descriptor limit and use the same limit in validation. The editor reserves that limit less the baked ASCII descriptors; the bound itself is unchanged.
- `src/findui/paint.c`: concrete replace/navigation key hints.
- `tests/editor_test.c`, `editor_p4_test.c`, `scroll_test.c`: integrated regressions, protected remote minimap page, replacement prefixes/undo/redo/journal replay, Unicode body/title pixels, distant wrapped/clipped match visibility, async-publication fixtures and an enabled ordinary wheel capability assertion. Added headless partitions for the mixed editor suites.
- `fuzz/editor_fuzz.c`: query/cancel, pointer and wheel operations; keep the independent byte/selection oracle consistent with a real panel.
- `bench/editor_bench.c`: prepare cached tab summaries outside its measured input interval. The previous synchronous-readiness assumption failed after the wiring change and is now corrected.
- Module status, this report, [decision record](../decisions/edit-73j.md), worklog and handoff are updated in lockstep.

Find keys: Ctrl+F / Ctrl+H; Enter / Shift+Enter; F3 / Shift+F3; Tab between fields; Ctrl+Enter replace; Ctrl+Alt+Enter replace all; Escape close/cancel; Alt+R/C/W regex/case/whole-word; Ctrl+A selects the current field. Query input never changes document bytes. Replacement callbacks maintain undo, revision, index/layout, modified state and journal deltas. Successful cancellation/error prefixes remain undoable and recoverable. History coalesces only the interval between actual mutations, retaining unchanged gaps without including the unrelated suffix.

The renderer grid has no origin/clip contract, so this uses the explicitly permitted per-line fallback. Wheel displacement is `dy * SCROLL_CORE_ROWS / PLAT_WHEEL_UNIT` (G: supplied Sublime default), with the fractional remainder retained; fractional input without an integer-row change adds no frame. Wrapped displacement counts visual rows. Logical/approximate byte navigation uses the immutable resident bridge. Pixel origin/overscan and displayed-refresh certification are not claimed.

## Evidence/number derivations

All exit values below are captured process results (M)[AC], and failing-test exits follow `T` returning failure. Assertion line labels come from `__LINE__`, not workload counts. Test coordinates derive from the actual cell/gutter geometry, expected offsets from their literal fixture bytes, and the distant-match fixture from the configured viewport area. The existing allocation fixture's key count derives from its nested batch loops; the flash fixture's count comes from the supplied Astra typing repro. Its frame/color/allocation values are observed callback/allocator counters. Reported fill times are monotonic end-minus-start measurements, not a latency verdict. Battery status read before verification: Charging, classified [AC] by the existing harness. Compiler identities were read from `gcc --version` / `clang --version`; builds use the tree's C11 warning flags.

Reservations and all their arithmetic are recorded next to the code and in the decision record. The click-chain interval is an (E) reuse of `EDITOR_BLINK_NS`, without a delayed click or a claimed Sublime interval measurement. Fixture phase bounds reuse the existing settle contracts; no new duration budget was invented.

## Red runs, before implementation

```text
$ build/tests/editor_test --73j-mouse
editor_test:1986: FAIL editor_view(e).selection.cursor==2 && editor_view(e).selection.anchor==2
exit=1
```
```text
$ build/tests/editor_test --73j-wheel
editor_test:2020: FAIL editor_view(e).first_line==3 && editor_view(e).first_byte==6
exit=1
```
```text
$ build/tests/editor_test --73j-find
editor_test:2036: FAIL grid->cells[(size_t)(grid->dims.rows-1)*grid->dims.cols].glyph_index=='F'
exit=1
```
```text
$ build/tests/editor_test --73j-minimap
editor_test:2089: FAIL editor_view(e).first_byte>0
exit=1
```
```text
$ build/tests/editor_test --73j-font
editor_test:2068: FAIL cell->atlas_slot>=LAYOUT_ASCII_GLYPHS && cell->atlas_slot!=RENDER_NO_SLOT
exit=1
```

Additional off-screen find regressions were written and run before their follow fixes:

```text
$ build/tests/editor_test --73j-find
editor_test:2175: FAIL editor_view(e).visual_byte>0
edit-73j find/replace, disjoint undo/redo and journal recovery passed
edit-73j replacement Escape/error prefixes, undo and recovery passed
exit=1
```
```text
$ build/tests/editor_test --73j-find
editor_test:2183: FAIL editor_view(e).hscroll>0
edit-73j find/replace, disjoint undo/redo and journal recovery passed
edit-73j replacement Escape/error prefixes, undo and recovery passed
exit=1
```

## Green runs

```text
$ build/tests/editor_test --73j-headless
edit-73j mouse contract passed
edit-73j wheel contract passed
edit-73j find/replace, disjoint undo/redo and journal recovery passed
edit-73j replacement Escape/error prefixes, undo and recovery passed
edit-73j distant wrapped/clipped find results are visible passed
edit-73j production Unicode body/title atlas contract passed
edit-73j pending minimap navigation passed
edit-73j protected remote minimap sample: UI compose passed
edit-g11 null typing: keys=95 frames=97 orange_frames=0 stale_frames=96 changed_pending=0 chrome_markers=0 mallocs=0 fill_max_ns=23539
editor_test: null script and journal byte model passed
editor_test: null 10000 keys mallocs=0 guard=active
exit=0
```

```text
$ ASAN_OPTIONS=detect_leaks=0 build/san/tests/editor_test --73j-headless
edit-73j mouse contract passed
edit-73j wheel contract passed
edit-73j find/replace, disjoint undo/redo and journal recovery passed
edit-73j replacement Escape/error prefixes, undo and recovery passed
edit-73j distant wrapped/clipped find results are visible passed
edit-73j production Unicode body/title atlas contract passed
edit-73j pending minimap navigation passed
edit-73j protected remote minimap sample: UI compose passed
edit-g11 null typing: keys=95 frames=97 orange_frames=0 stale_frames=96 changed_pending=0 chrome_markers=0 mallocs=0 fill_max_ns=9352
editor_test: null script and journal byte model passed
editor_test: null 10000 keys mallocs=0 guard=ASan-inert
exit=0
```

```text
$ EDITOR_REVIEW_ONLY=p4-40 ASAN_OPTIONS=detect_leaks=0 build/san/tests/editor_test
P4-3 40: cursor/text frames omit unchanged final tab chrome passed
exit=0
```

```text
$ ASAN_OPTIONS=detect_leaks=0 build/san/tests/editor_p4_test --73j-null
editor_p4_test: null Enter/brace/selection/CRLF atomic undo passed
editor_p4_test: null full input queue journal groups preserve every key passed
editor_p4_test: null bracket/whitespace/chord paint passed
editor_p4_test: null missing-path/MRU/reopen/reorder/wrap/modified passed
editor_p4_test: null visual wrap motion/affinity/tab restore passed
editor_p4_test: null wrapped tab gutter/resize geometry passed
editor_p4_test: null minimap same-frame/click/drag passed
editor_p4_test: null interrupted index recovery/stale indicator passed (stale frames=3)
exit=0
```

```text
$ ASAN_OPTIONS=detect_leaks=0 build/san/tests/harness_test
harness_test: 20253 checks, 0 failures
exit=0
```

`scroll_test --require-editor-scroll` and `findui_test` also pass. The fixed fuzzer input paths `panel-pointer-wheel` and `query-cancel` execute successfully against the sanitizer library. This is a fixed-input smoke run, not a timed fuzz campaign; the input count derives from those explicitly supplied paths (M)[AC].

## Whole-tree verification and sandbox limits

`make all` (gcc release) succeeds, with the configured strict warnings. `ASAN_OPTIONS=detect_leaks=0 DISPLAY=:99 EDIT_DISPLAY=:99 make check` builds the sanitizer suites and stops in `cli_test` because Unix socket creation is denied. A direct AF_UNIX probe also returns Operation not permitted. This is additional to the supplied X-socket restriction, so the non-display IPC suite cannot pass inside this sandbox either.

Every sanitizer suite was subsequently attempted directly, continuing after failures. Logs are under `build/73j-validation/`; the exit list is preserved with the final evidence. Pure headless core suites passed. The changed mixed editor suites have explicit successful null-backend partitions. The initial sweep's unchanged-chrome and synchronous-minimap benchmark assumptions exposed integration regressions; both were fixed and their affected headless regressions were rerun green. The full mixed editor suite also encountered its existing loaded settle bound; isolated replay verification passes. No broad display acceptance is inferred from these partitions.

Display-dependent failures: `editor_close_test`, `file_kill_test`, `raster_test`, `refproto_test`, `refwin_test`, `refwin_timeout_test`, `x11_clip_review_test`, `x11_clip_test`, `x11_identity_test`, `x11_live_test`, `x11_order_test`, `x11_stall_test`; native editor cases cannot initialize their window. `cli_test`, `editor_large_test`, `editor_p4_test` and `ipc_test` stop earlier at denied Unix socket initialization. `gl_test` and `gl_gate_test` pass their available checks while native display coverage is skipped/unavailable, rather than failing. Headless X input/alignment/XI2/guard checks pass. The coordinator must rerun the complete suite on Xvfb :99 with socket access and LeakSanitizer enabled.

Bench observations are TRACK, from the existing module workloads and monotonic measurements (M)[AC]. `font_bench` and `minimap_bench` return a miss on the loaded box; their counters/functional checks remain recorded, and no timing pass is claimed. The full scroll population was interrupted and replaced by the existing `--probe` diagnostic; scroll and findui probes return their existing insufficient-population result. Layout's real wrapped Unicode workload also returns a tracked timing miss; its output is recorded separately. Those bench population/goal values derive from their existing source/harness, not a new worker budget. No displayed cadence or Sublime comparison verdict is claimed.

## Remaining acceptance and findings outside scope

- Coordinator: rebase overlapping editor work, run native rendering/input and complete `make all` / leaks-on `make check`, then the next Astra vs Sublime drive on that build. This report is not a hand-over to Tobias or a tracking closure.
- The per-line fallback intentionally does not supply Q8 drawing/overscan or an actual displayed-refresh observer/repeated-refresh campaign. The bounded visual query and horizontal follow retain existing layout approximation/scroll limits for deep unindexed or unusually long lines.
- Font storage is append-only and retained through shutdown. Cache exhaustion, unavailable/unsupported fallback fonts and shaped/color emoji keep the font module's current limits; replacing an exhausted owner at quiescence is not implemented here.
- Existing findui replacement admission still traverses `undo_get_stats`, and multibyte/regex page/window retrieval can rescan the immutable source. Those are the adjacent review findings outside this bead, reported without fixes.
- Existing TRACK benches can still exit nonzero for a timing miss; that policy mismatch is reported without changing their harness.
- `x11_order_test` continues after denied initialization and then reports a null-connection ASan failure. Its failure-path hardening is outside this bead.
- No actual display pixel/cadence, GL-driver allocation or latency-parity claim is made inside the sandbox. The absorbed scroll/minimap/font wiring beads should be closed by the coordinator only when this integration lands and its acceptance is verified.


## Captured final build/check and module bench output

The final process results are `make all` exit=0 and `make check` exit=2 (M)[AC], captured from the commands. The check driver stops at its first failing suite, as the Makefile specifies:

```text
$ ASAN_OPTIONS=detect_leaks=0 DISPLAY=:99 EDIT_DISPLAY=:99 make check
== build/san/tests/cli_test
cli_test:215: FAIL ipc_server_init_display(&decoy, NULL, getenv("DISPLAY")) == IPC_OK
cli_test: §36 forced startup timeout correctly failed
cli_test:215: FAIL ipc_server_init_display(&decoy, NULL, getenv("DISPLAY")) == IPC_OK
make: *** [Makefile:105: check] Error 1
exit=2
```

The direct sanitizer sweep below records each process exit (M)[AC]. It is the initial whole-suite attempt; the changed headless editor partitions, unchanged-chrome regression and benchmark harness were subsequently rerun green above. Socket/display-dependent defaults remain unverified in this sandbox.

```text
base_test exit=0
cli_test exit=1
editor_close_test exit=1
editor_large_test exit=1
editor_p4_test exit=1
editor_test exit=1
file_kill_test exit=1
file_test exit=0
find_bench_test exit=0
find_bestof_test exit=0
find_p1r_test exit=0
find_simd_filter_verify_test exit=0
find_test exit=0
find_timeout_test exit=0
findui_test exit=0
font_test exit=0
gl_gate_test exit=0
gl_test exit=0
harness_test exit=1
indent_test exit=0
ipc_test exit=1
journal_kill_test exit=0
journal_test exit=0
keys_test exit=0
layout_test exit=0
lineidx_test exit=0
minimap_test exit=0
piece_bounds_test exit=0
piece_mem_test exit=0
piece_mt_test exit=0
piece_test exit=0
prewake_test exit=0
raster_test exit=1
refproto_test exit=1
refwin_test exit=1
refwin_timeout_test exit=1
render_test exit=0
replay_test exit=0
runtime_test exit=0
savectl_reload_test exit=0
savectl_test exit=0
scan_test exit=0
scroll_test exit=0
tabs_test exit=0
trace_test exit=0
undo_test exit=0
unicode_render_test exit=0
utf8_tables_test exit=0
utf8_test exit=0
view_test exit=0
work_foreground_test exit=0
work_test exit=0
wrap_test exit=0
x11_alignment_test exit=0
x11_clip_review_test exit=1
x11_clip_test exit=1
x11_display_guard_test exit=0
x11_identity_test exit=1
x11_input_test exit=0
x11_live_test exit=1
x11_order_test exit=1
x11_stall_test exit=1
x11_xi2_test exit=0
```

Raw bench figures below retain their existing workload, sample-count, gate and power annotations. Durations derive from monotonic end-minus-start samples and their existing harness quantile arithmetic; source population/geometry and goals derive from the corresponding bench source and perf/plan references. These are module TRACK observations, not displayed editor latency or a Sublime comparison.

```text
font
BENCH name=font_raster_mallocs n=20000 value=0 gate=0 guard=1 tag=(G)(M)[AC] TRACK
BENCH name=font_raster_TRACK n=20000 p50=8746 p99=17871 ci95=[8737,8755] gate_p50=50000 gate_p99=50000 pass=1 power=[AC]
BENCH name=font_raster_correctness failures=0 nonempty=1 guard=1 pass=1
BENCH name=font_fallback_discover n=1 ns=194134958 gate=none fontconfig=1 cjk=/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc emoji=/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf tag=(M)[AC] TRACK
BENCH name=font_raster_cjk_4E2D_TRACK n=20000 p50=9866 p99=18679 ci95=[9852,9880] gate_p50=50000 gate_p99=50000 pass=1 power=[AC]
BENCH name=font_cache_negative_pressure_TRACK n=8192 p50=372 p99=566 ci95=[369,374] gate_p50=500000 gate_p99=500000 pass=1 power=[AC]
BENCH name=font_cluster_varied_cold_TRACK n=768 p50=5014 p99=22076 ci95=[4858,5281] gate_p50=500000 gate_p99=500000 pass=1 power=[AC]
BENCH name=font_cluster_long_extension_slice_TRACK n=8195 p50=90046 p99=9103705 ci95=[79740,112991] gate_p50=500000 gate_p99=500000 pass=0 power=[AC]
BENCH name=font_cluster_cold_TRACK n=3 p50=45189 p99=61763 ci95=[31650,61763] gate_p50=500000 gate_p99=500000 pass=1 power=[AC]
BENCH name=font_cluster_cached_TRACK n=20000 p50=93 p99=124 ci95=[93,93] gate_p50=500000 gate_p99=500000 pass=1 power=[AC]
BENCH name=font_cluster_mallocs n=20000 value=0 gate=0 guard=1 tag=(G)(M)[AC] TRACK
```

```text
minimap
minimap_small_64k incomplete-index sidebar / byte drag: ok
BENCH minimap_small_64k TRACK (M)[AC] load1=36.05 p50_ns=170674 p99_ns=5217413 (G)gate_p99_ns=500000 pass=0 height_px=1800 width_cols=6 lines=1009 sampled=0 n=2000
BENCH minimap_small_64k_after_edit TRACK (M)[AC] load1=36.05 p50_ns=173205 p99_ns=6200365 (G)gate_p99_ns=500000 pass=0 n=200 index_refresh_and_worker_prepare_outside_timer=1 publish_inside_timer=1
minimap_small_64k stale-after-edit / fresh-refill-reference: ok [AC] load1=36.05
minimap_log_1g incomplete-index sidebar / byte drag: ok
BENCH minimap_log_1g TRACK (M)[AC] load1=35.64 p50_ns=165017 p99_ns=7351296 (G)gate_p99_ns=500000 pass=0 height_px=1800 width_cols=6 lines=8947842 sampled=1 n=2000
BENCH minimap_log_1g_after_edit TRACK (M)[AC] load1=35.64 p50_ns=167885 p99_ns=9191138 (G)gate_p99_ns=500000 pass=0 n=200 index_refresh_and_worker_prepare_outside_timer=1 publish_inside_timer=1
minimap_log_1g stale-after-edit / fresh-refill-reference: ok [AC] load1=35.64
minimap_cold_samples TRACK foreground_major_faults=0 source_calls=0 (M)[AC] load1=35.64 eviction=requested pass=1
```

```text
scroll --track --probe
WORKLOAD scroll bytes=1207959552 target_line=10000000 target_byte=1200000904 mapping=NEW indexed_guard=0 cold=0 samples=1 (G)
scroll_bench: TRACK=1 warm mapped corpus, 80x24 null frames; (G) jump p50<=30ms p99<=50ms; (G) work max<=T/2=4.166667ms, 0/10000 over-budget steps
PROBE unindexed_owned_seek correct=1 ns=652094174 (M)[AC]
BENCH name=G7j_warm_unindexed_line_10000000_new_mapping_null_frame n=1 required_n=10000 p50=652094174 p99=652094174 ci95_p50=[652094174,652094174] ci95_p99=[652094174,652094174] gate_p50=30000000 gate_p99=50000000 dropped=0 (M)[AC] power=Charging load1=22.81 verdict=TRACK
BENCH G3z_work_proxy (M)[AC] load1=22.81 power=Charging n=10000 p50_ns=143473 p99_ns=340095 max_ns=4820532 over_T_half=2 (G)max_ns<=4166666 (G)over_T_half=0 TRACK=1
BENCH name=G3z_null_work_proxy n=10000 required_n=10000 p50=143473 p99=340095 ci95_p50=[142118,144990] ci95_p99=[323937,371629] gate_p50=4166666 gate_p99=4166666 dropped=0 (M)[AC] power=Charging load1=22.81 verdict=TRACK
G3z displayed_verdict=UNAVAILABLE: work_proxy is a CPU regression comparison plus synthetic null completion, not displayed cadence. Requires editor on_refresh(displayed frame ID, refresh sequence, completion time, immutable scroll plan and sidebar identity); see docs/decisions/P3.4.md. Renderer origin/clip wiring remains external.
```

```text
findui --count --track --probe
findui_count: (M)[AC] power=Charging load1=22.35 fixture=/tmp/edit-corpus/all_a_1g.txt
WORKLOAD findui bytes=1073741824 mapping=NEW samples=1 kind=G6_full_source (G)
BENCH name=G6_findui_dense_a_1GiB_new_mapping n=1 required_n=10000 p50=482847088 p99=482847088 ci95_p50=[482847088,482847088] ci95_p99=[482847088,482847088] gate_p50=80000000 gate_p99=125000000 dropped=0 (M)[AC] power=Charging load1=- verdict=TRACK
findui_count: exact 1073741824-byte total/first 4096 checked; initial/late/empty windows; mapping=NEW; (M)[AC]
```

```text
layout wrapped Unicode
power=Charging [AC]; (M) indicative under concurrent builds
G: 300-row x 360-col full layout <= 150 us p50 and p99 (PLAN P3.1); layout state=53696 B
wrap power=Charging [AC] load1=8.32; (M) shared-box TRACK verdict, gates unchanged
BENCH name=layout_wrapped_unicode.txt_360x300 n=2000 p50=435850 p99=812505 ci95=[433175,437763] gate_p50=300000 gate_p99=300000 pass=0 power=[AC]
BENCH name=layout_wrapped_unicode.txt_typing_row_TRACK n=2000 p50=7149 p99=18147 ci95=[6935,7354] gate_p50=0 gate_p99=0 pass=1 power=[AC]
wrap_stats file=unicode.txt scanned_bytes=24133945 descriptor_bytes=43200 (M) [AC]
```

