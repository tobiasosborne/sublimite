# edit-8m3 worker report

**Partial scoped implementation; leave visual acceptance open.** Character
positions, expanded tabs, clipped marks, and a pixel-exact translucent viewport
are available in the minimap module. The running editor still renders the map
through full-size font cells. The colour-only restriction in `paint.c` prevents
binding/composing the pixel raster there. This is not a claim that the editor
now matches Sublime or that the next Astra drive will pass.

## Changed files

- `src/minimap/minimap.c`: bounded character masks during existing preparation;
  individual column marks instead of contiguous ink prefixes; translucent
  viewport wash and perimeter; source-read-free `minimap_pixels` into caller
  storage. Navigation and sampling/source-read bounds are preserved.
- `src/minimap/minimap.h`: caller-owned row masks and the additive pixel API;
  documented cell-resolution limitation.
- `src/editor/paint.c`: only the `minimap_style colors` initializer changed.
  Background, ink and viewport derive from the editor's existing background,
  gutter and cursor colours; the stale colour remains unchanged.
- `tests/minimap_test.c`: character positions/spaces, fragmented tabs, clipping,
  pixel line spacing, half-open viewport, interior wash, perimeter, stride,
  bounded writes, source-read and allocation guards; optional real renderer dump.
  Earlier assertions for the replaced density-prefix rendering were updated.
- `src/minimap/STATUS.md`, `docs/decisions/edit-8m3.md`, `HANDOFF.md`,
  `docs/worklog/2026-10-11.md`, and this report: implementation/evidence/scope
  handoff, as required by CLAUDE.md.

No bead or version-control commands were run. All source, documentation and artifact writes stayed in this worktree; the
existing test/bench programs use their own temporary fixtures.
No allocation was added to preparation, cached fill, navigation or pixel rendering.

## Red first

Ran after adding the character/viewport regression tests and before changing
implementation:

```sh
make -j4 build/san/tests/minimap_test
ASAN_OPTIONS=detect_leaks=0 build/san/tests/minimap_test
```

(G) Build parallelism is a make invocation choice, not a performance budget.
The sanitizer option follows the sandbox's documented LeakSanitizer restriction.
(M)[bat] The failure below is the original test's source-line diagnostic; its
number derives from the compiled failing test, not a budget. The position oracle
comes directly from `ab   cd`.

```text
minimap_test:110: (cells[c].bg != style.background) == marked
```

## Green

```sh
make build/san/tests/minimap_test build/tests/minimap_test
ASAN_OPTIONS=detect_leaks=0 build/san/tests/minimap_test
build/tests/minimap_test
```

(M)[bat] Sanitizer and gcc release runs both passed. The release build activates
the counting allocator; the sanitizer build's allocator guard is inert by design.
Recorded sanitizer output:

```text
edit-8m3: character positions, spaces, tab stops, clipping, malloc guard: ok
edit-8m3: translucent viewport perimeter and half-open geometry: ok
edit-8m3: pixel marks, line spacing, viewport wash/edge, stride, source/allocation guards: ok
review 9: UI deferred fill reads no dispersed samples: ok
review 8: pending index paints current byte scrollbar and drag works: ok
review 5: equal-size tab switch exact byte=1: ok
review 7: cached fill preserves typing/blink damage: ok
minimap_test: density, hit round trip, stale, malloc guard: ok
```

## PRD review PNG and reference frames

[Native minimap dump](../../build/edit-8m3/minimap.png)

![Standalone minimap renderer dump, not an editor screenshot](../../build/edit-8m3/minimap.png)

The dump uses the actual `minimap_pixels` API and editor colours. (G) Width is
120 logical pixels from `perf/01-perf-target.md`'s minimap geometry. Height derives
as fixture line count multiplied by the required two logical pixels per line.
Each non-space character gets a one-pixel mark; the following scanline separates
lines. The viewport covers the fixture loop body, derived by counting newline
bytes before/between its `for` and `return` substrings. Glyph-density differences
between narrow and wide letters are not implemented; this uses the expressly
allowed non-space/space coverage minimum.

Regenerate without opening a display:

```sh
ASAN_OPTIONS=detect_leaks=0 MINIMAP_DUMP_PPM=build/edit-8m3/minimap.ppm build/san/tests/minimap_test
ffmpeg -y -v error -i build/edit-8m3/minimap.ppm -frames:v 1 build/edit-8m3/minimap.png
```

(G) One output frame follows from exporting a single static module dump.
Reference frames were extracted with ffmpeg from the main repository's
`build/astra/video/13-minimap-sublime.mkv`, without writing to that repository:
[earlier frame](../../build/edit-8m3/sublime-reference.png),
[later frame](../../build/edit-8m3/sublime-reference-later.png),
[minimap crop](../../build/edit-8m3/sublime-minimap-crop.png).
(M) ffprobe reported a 2880 by 1800 recording raster. Extraction positions:
4 seconds (M: beginning of the review's task interval) and 10 seconds
(E: rounded midpoint of that review interval, (4 + 15.5) / 2 rounded to 10).
(M) Crop bounds (707,60,80,520) are the measured Sublime minimap ROI in
`docs/reviews/astra-vs-sublime-1.md`. The frames show miniature individual marks with indentation, spaces and a quiet
viewport overlay; syntax colour is outside this bead. The editor window occupies
only part of the recorded desktop, so recording dimensions alone do not establish
an editor window at that size.

## Paired minimap_bench

The baseline executable was saved before implementation, then run immediately
before the final rebuilt executable. (M)[bat] Both starts and finishes are in
the same clock minute, directly evidenced below. BAT0/status was read immediately
before each run. Load readings come from `/proc/loadavg` and the existing bench
harness. These are shared-box, battery measurements, not isolated throughput.

Every bench time is (M)[bat], derived from `bench_now_ns` differences around
cached fills, or publication plus fill after edits. Worker preparation/index
refresh are outside the existing timer. Quantiles use the harness's nearest-rank
selection over the printed sample counts. Geometry, corpus bytes/line counts,
sample counts and the tracked target derive from the unchanged
`bench/minimap_bench.c`, its fixture/index, and the existing minimap contract.
The existing target is 500000 ns (G: inherited bench value, tracked aspiration
under CLAUDE.md; no new time budget was invented).

```text
before start=2026-10-10T23:57:07.138489+00:00 power=Discharging loadavg=37.36 25.65 13.90 37/2518 3
minimap_small_64k incomplete-index sidebar / byte drag: ok
BENCH minimap_small_64k TRACK (M)[bat] load1=37.36 p50_ns=184974 p99_ns=9221440 (G)gate_p99_ns=500000 pass=0 height_px=1800 width_cols=6 lines=1009 sampled=0 n=2000
BENCH minimap_small_64k_after_edit TRACK (M)[bat] load1=37.36 p50_ns=191028 p99_ns=6230404 (G)gate_p99_ns=500000 pass=0 n=200 index_refresh_and_worker_prepare_outside_timer=1 publish_inside_timer=1
minimap_small_64k stale-after-edit / fresh-refill-reference: ok [bat] load1=37.36
minimap_log_1g incomplete-index sidebar / byte drag: ok
BENCH minimap_log_1g TRACK (M)[bat] load1=37.36 p50_ns=145118 p99_ns=13172631 (G)gate_p99_ns=500000 pass=0 height_px=1800 width_cols=6 lines=8947842 sampled=1 n=2000
BENCH minimap_log_1g_after_edit TRACK (M)[bat] load1=37.36 p50_ns=144385 p99_ns=3527684 (G)gate_p99_ns=500000 pass=0 n=200 index_refresh_and_worker_prepare_outside_timer=1 publish_inside_timer=1
minimap_log_1g stale-after-edit / fresh-refill-reference: ok [bat] load1=37.36
minimap_cold_samples TRACK foreground_major_faults=0 source_calls=0 (M)[bat] load1=38.05 eviction=requested pass=1
before end=2026-10-10T23:57:12.746575+00:00 exit=1
after start=2026-10-10T23:57:12.746635+00:00 power=Discharging loadavg=38.05 25.99 14.08 40/2533 5
minimap_small_64k incomplete-index sidebar / byte drag: ok
BENCH minimap_small_64k TRACK (M)[bat] load1=38.05 p50_ns=184339 p99_ns=10219276 (G)gate_p99_ns=500000 pass=0 height_px=1800 width_cols=6 lines=1009 sampled=0 n=2000
BENCH minimap_small_64k_after_edit TRACK (M)[bat] load1=38.05 p50_ns=199705 p99_ns=6240609 (G)gate_p99_ns=500000 pass=0 n=200 index_refresh_and_worker_prepare_outside_timer=1 publish_inside_timer=1
minimap_small_64k stale-after-edit / fresh-refill-reference: ok [bat] load1=38.05
minimap_log_1g incomplete-index sidebar / byte drag: ok
BENCH minimap_log_1g TRACK (M)[bat] load1=37.81 p50_ns=139890 p99_ns=9166535 (G)gate_p99_ns=500000 pass=0 height_px=1800 width_cols=6 lines=8947842 sampled=1 n=2000
BENCH minimap_log_1g_after_edit TRACK (M)[bat] load1=37.81 p50_ns=154348 p99_ns=3946390 (G)gate_p99_ns=500000 pass=0 n=200 index_refresh_and_worker_prepare_outside_timer=1 publish_inside_timer=1
minimap_log_1g stale-after-edit / fresh-refill-reference: ok [bat] load1=37.81
minimap_cold_samples TRACK foreground_major_faults=0 source_calls=0 (M)[bat] load1=37.81 eviction=requested pass=1
after end=2026-10-10T23:57:17.983294+00:00 exit=1
```

Warm medians were close to the baseline. Tail timings exceeded the inherited
tracked target in both versions. The measured changing shared load does not
support attributing tail differences solely to this patch, and the requested
budget cannot be certified on these runs. No editor-frame latency claim is made.
The bench still returns failure when its target is missed, contrary to the
repository's current tracked-number policy; that existing bench behavior is
reported outside scope, not changed.

## What remains

Pixel-raster/atlas storage and publication, composition into the editor frame,
and matching pointer coordinates require editor/render integration outside the
colour initializer. The existing editor reserves only font-row map summaries,
so its capacity/geometry also needs deliberate integration. The cell adapter
uses character columns but cannot produce sub-cell marks or a physical pixel
edge. Grouped and sampled rows remain approximate representatives. The next
Astra comparison drive must verify recognizability, preserved click/drag behavior,
and same-frame latency on the integrated build.

## Build and full-suite verification

(M)[bat] `make all` (gcc release, C11 and all requested warning flags) passed,
with the recorded successful exit in `build/edit-8m3/make-all-result.txt`.
The initial `ASAN_OPTIONS=detect_leaks=0 DISPLAY=:99 EDIT_DISPLAY=:99 make check`
built the sanitizer suites and stopped at the CLI socket setup failure.
The recorded make exit is 2 (M: make's returned failure code, not a budget).
A final attempt rebuilt the sanitizer targets, then waited for another process's
display lock and was interrupted; that lock was not changed.

A direct sequential inventory ran the remaining suites under the same sanitizer
options, in independent module groups so a blocked display suite did not suppress
headless coverage. (M)[bat] Status below derives from each process return code;
interrupted suites are explicitly distinguished. `gl_test` passed its pure checks
but skipped native X/EGL stages. Full leaks-on `make check` still requires the
coordinator outside this sandbox, with Xvfb :99 and local IPC sockets permitted.

(M) Inventory: 63 suites, from enumerating `tests/*_test.c`; 45 successful returns, from counting the recorded zero exits (native GL stages remain skipped).

| Suite | Recorded result |
| --- | --- |
| `base_test` | PASS |
| `cli_test` | Local IPC bind denied; failed |
| `editor_close_test` | Display unavailable; failed |
| `editor_large_test` | Local IPC bind denied; failed |
| `editor_p4_test` | Local IPC bind denied; failed |
| `editor_test` | interrupted after failed checks |
| `file_kill_test` | Display unavailable; failed |
| `file_test` | PASS |
| `find_bench_test` | PASS |
| `find_bestof_test` | PASS |
| `find_p1r_test` | PASS |
| `find_simd_filter_verify_test` | PASS |
| `find_test` | PASS |
| `find_timeout_test` | PASS |
| `findui_test` | PASS |
| `font_test` | PASS |
| `gl_gate_test` | PASS |
| `gl_test` | PASS |
| `harness_test` | PASS |
| `indent_test` | PASS |
| `ipc_test` | Local IPC bind denied; failed |
| `journal_kill_test` | PASS |
| `journal_test` | PASS |
| `keys_test` | PASS |
| `layout_test` | PASS |
| `lineidx_test` | PASS |
| `minimap_test` | PASS |
| `piece_bounds_test` | PASS |
| `piece_mem_test` | PASS |
| `piece_mt_test` | PASS |
| `piece_test` | PASS |
| `prewake_test` | PASS |
| `raster_test` | Display unavailable; failed |
| `refproto_test` | Display unavailable; failed |
| `refwin_test` | blocked on external display lock; interrupted |
| `refwin_timeout_test` | Display unavailable; failed |
| `render_test` | PASS |
| `replay_test` | PASS |
| `runtime_test` | PASS |
| `savectl_reload_test` | PASS |
| `savectl_test` | PASS |
| `scan_test` | PASS |
| `scroll_test` | PASS |
| `tabs_test` | PASS |
| `trace_test` | PASS |
| `undo_test` | PASS |
| `unicode_render_test` | PASS |
| `utf8_tables_test` | PASS |
| `utf8_test` | PASS |
| `view_test` | Headless word-left assertion; failed |
| `work_foreground_test` | PASS |
| `work_test` | PASS |
| `wrap_test` | PASS |
| `x11_alignment_test` | PASS |
| `x11_clip_review_test` | Display unavailable; failed |
| `x11_clip_test` | Display unavailable; failed |
| `x11_display_guard_test` | PASS |
| `x11_identity_test` | Display unavailable; failed |
| `x11_input_test` | PASS |
| `x11_live_test` | Display unavailable; failed |
| `x11_order_test` | Display unavailable; failed |
| `x11_stall_test` | Display unavailable; failed |
| `x11_xi2_test` | PASS |

(M)[bat] The replay CLI shell checks passed with LeakSanitizer disabled. The
minimap fuzzer built with clang ASan/UBSan; deterministic seed replay passed.
Seed line lengths derive from the existing summary cap and its uint64_t word
boundary, plus the existing fuzzer's line-length limit; header controls use the
byte-domain endpoints. `-runs=0` requests initial-corpus replay without mutations,
not a timing budget. This is seed validation, not a sustained fuzzing campaign.
The power read immediately before replay is `build/edit-8m3/fuzz-power.txt`.

## Outside-scope findings and required coordinator checks

- `view_test` has a headless assertion at word-left on its long-line fixture.
  Its binary does not link minimap symbols (checked with `nm`), and view code
  was not edited. This prevents claiming every headless suite passed.
- `editor_large_test` initially reached its existing cooperative-job deadline;
  a full rerun passed that stage and the subsequent headless checks, then
  stopped on denied IPC startup. Both logs are retained.
- `work_foreground_test` initially failed `editor_open`, then continued through
  a null editor in its test harness and triggered ASan/UBSan. An isolated open
  probe and a full suite rerun passed without code changes. The suite's printed
  power label is hardcoded AC; it is not a verified power measurement here.
- `editor_test` reported launcher-wait and native backend-open failures before
  becoming stuck. It was interrupted after those recorded failures. Its full
  completion remains unverified.
- X-dependent failure handling in `x11_order_test` continues into a null X
  connection after denied fixture setup, producing a sanitizer diagnostic.
  This is retained as a test-harness finding, not fixed in this minimap bead.
- The existing stale-map `!` chrome/amber overlay remains outside the permitted
  colour initializer edit. It still needs its separately scoped editor fix.
- The benchmark's inherited nonzero exit on target misses remains outside scope,
  as described above.

Direct socket probes returned `Operation not permitted` for connecting to the
X socket and for binding a worktree-local Unix socket. The failed display suites
are listed individually in the table; `refwin_test` additionally waited on the
external display lock. These results require coordinator reruns on Xvfb :99
outside the sandbox. The headless view assertion also needs coordinator triage;
no outside-module fix was attempted.

Selected raw diagnostics below are (M)[bat] process output; source-line numbers,
error codes and assertion operands derive directly from the recorded binaries.
Several suite logs print hardcoded AC labels (including editor-large/work);
actual BAT0 status was Discharging. Those labels are not verified power-stamped
performance evidence.

`view_test`:

```text
tests/view_test.c:202: assertion failed: view_command(&f.v,VIEW_WORD_LEFT,0,((void*)0),0,&word_change)==VIEW_OK
```

`ipc_test`:

```text
init rc=4 errno=1 (Operation not permitted)
FAIL tests/ipc_test.c:130: init_rc==IPC_OK
```

`editor_large_test-rerun`:

```text
editor_large_test:210: FAIL ipc_server_init(&server, runtime) == IPC_OK
editor_large_test:800: FAIL mapped_startup_ipc() == 0
P4-3 section 28: sealed find publication yields behind a saturated mailbox passed
P4-3 section 28: production find/warm yield bulk lane before completion passed
P4-3 section 21: warm completion requests an idle correcting frame passed
P4-3 section 20: cold deep bracket paint minor faults=3 (M)[AC], loose bound=128 (G)
P4-3 section 18: long-line start and UTF-8/combining/CRLF byte boundaries passed
P4-3 section 17: COPY coordinates and misleading mapped prefix resolve exactly passed
P4-3 section 15: Keep restarts incomplete index and cancelled warm lease passed
P4-3 section 1: held cancelled find survives Keep and restart passed
```

`refproto_test`:

```text
refproto_test: window=0x00000000 connect DISPLAY=:99: connection error=1
refproto_test:62: FAIL refproto_display_open(d,"refproto_test",true) == 0
```

`refwin_test`:

```text
keyinject: --pairs must be nonzero
waiting for display :99 (held by unknown)
keyinject: --pairs must be nonzero
refwin_test: measured period arithmetic and 90/60 Hz pair rejection PASS
diagnostic-test: window=0x00000000 connect DISPLAY=:99: connection error=1
```

`refwin_timeout_test`:

```text
refwin_timeout_test: RED missing input-stage diagnosis; actual log:
refwin: window=0x00000000 connect DISPLAY=:99: connection error=1
```

