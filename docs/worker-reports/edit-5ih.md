# Worker report: edit-5ih

Implemented primary font selection in this worktree only. DejaVu Sans Mono
remains the embedded default and guaranteed ASCII fallback. No `bd` or `git`
commands were run, and no other bead was worked.

## Files changed

- `src/font/selection.c`, `src/font/selection.h`: copied family/file/TTC syntax,
  worker file loading and ASCII raster, mailbox receipt, ownership and cleanup.
- `src/font/fallback.c`: primary pattern matching via the existing dlopen API,
  with concrete-family validation so a missing family restores embedded DejaVu.
- `src/font/font.c`: unrounded printable-ASCII monospace advance validation.
- `src/editor/font.c`, `src/editor/editor.c`, `src/editor/open.c`,
  `src/editor/editor.h`, `src/editor/private.h`: startup environment source,
  allocation-free request hook, frame-boundary adoption, geometry/layout update,
  retained pixel viewport, fixed resource reservations and quiescent teardown.
- `tests/font_selection_test.c`: parse/bounds, installed family, file precedence,
  TTC selection, malformed/missing/proportional fallback, raw widths hidden by
  pixel rounding, worker/baked atlas pixel equality, held backend/worker leases,
  input preservation, allocation guard and optional retained mid-typing trace.
- `fuzz/font_fuzz.c`: selection syntax and monospace admission properties.
- `bench/editor_bench.c`: optional NEW post-swap G1/G3 baseline mode, preserving
  the standard campaign and existing harness callers.
- `src/font/STATUS.md`, `src/editor/STATUS.md`, `HANDOFF.md`,
  `docs/worklog/2026-10-11.md`, `docs/decisions/edit-5ih.md`, and this report.

## Source and design

P6.4 (`edit-v0o.7`) is absent: there is no `src/config` loader in this snapshot.
The authorised interim source is `EDIT_FONT_FAMILY` / `EDIT_FONT_FILE` at
startup. `editor_font_request` is the future parser/inotify hook; a loader must
retry `EDITOR_MORE`. File wins over family. Use `path#decimal` for a physical
TTC face. Empty values restore embedded DejaVu. The missing zoom rebuild path
was replaced with the existing mailbox/owner preparation pattern.

Resolution, font I/O, width validation and stb_truetype rasterisation run on
WORK_BULK. Mailbox receipt marks readiness, and the editor swaps only after the
old backend frame retires and before a new input/layout turn. It requests
full layout once and recalculates text/view/minimap geometry. Publication is
the worker's last write; adoption does not wait for physical return. Reuse and
release still wait for that lease, preventing a missed-wakeup frame delay.
A failed selection restores the embedded atlas and clears released face bytes.
Worker diagnostics are suppressed after the first selection failure. Invalid
startup syntax also queues its diagnostic on a worker; the standalone
invalid-environment probe retains the embedded atlas and observes completion.

ASCII atlas count is (P) inclusive printable range `0x7e - 0x20 + 1 = 95`.
File/atlas allocations derive from file length and cell geometry; raster
scratch/retry policy is inherited from atlasbake/font runtime, not a newly
invented timing budget. The test geometry reuses the editor allocation
fixture, with doubled cell reservations for the synthetic ascent-doubling
case. The (G) typing allocation count is zero, derived from CLAUDE Law 2.
Resource contracts, family alias limits, cell reservation caps, and the
**Iosevka Fixed/Term, no ligatures/calt/HarfBuzz** guidance are documented in
[the decision](../decisions/edit-5ih.md).

## Red run

The failing test was written and executed before the implementation. It waits
for pool/mailbox quiescence and checks whether the configured family replaced
the embedded atlas. The command's parallelism derives from the OS CPU count.
The test failed; the printed source location is its then-current assertion
line, not a timing/count estimate.

```sh
make -j"$(getconf _NPROCESSORS_ONLN)" build/tests/font_selection_test
build/tests/font_selection_test
```

```text
font_selection_test:35: FAIL e->grid.pages[0].pixels != embedded

```

Final ownership review added another failing assertion before clearing the
failed face descriptor. (M) its test exit was failure, from the test macro's
failure return:

```text
sublimite: font selection failed; using embedded DejaVu Sans Mono
font_selection_test:236: FAIL e->fonts[e->font_active].face.data == NULL
font_selection_test: family/file parsing, precedence inputs, TTC index and bounds passed
font_selection_test: proportional design advances rejected despite equal rounded pixel widths
font_selection_test: frame boundary, input sequences, worker identity, mallocs=0 guard=active

```

## Green runs

The release guard is active; zero allocations below is (M)[AC], the sum of
allocator interpositions from input ingress to submit over the test's typed
inputs. The required zero is (G), CLAUDE Law 2. Frame/lease checks are causal:
old pixels survive a held submitted frame; publication is adoptable while a
worker remains held after publishing; input sequences all reach submission.

```sh
FONT_SELECTION_TRACE=build/edit-5ih-selection.trc build/tests/font_selection_test
ASAN_OPTIONS=detect_leaks=0 build/san/tests/font_selection_test
```

```text
sublimite: font selection failed; using embedded DejaVu Sans Mono
font_selection_test: family/file parsing, precedence inputs, TTC index and bounds passed
font_selection_test: proportional design advances rejected despite equal rounded pixel widths
font_selection_test: frame boundary, input sequences, worker identity, mallocs=0 guard=active
font_selection_test: publication swaps without waiting for worker return; input remains runnable
font_selection_test: TTC face selection, geometry recomputation and single adoption passed
font_selection_test: retained mid-typing trace written after input/submit guard closed
font_selection_test: environment, installed family, failure fallback, proportional rejection, file precedence and restore passed

```

```text
sublimite: font selection failed; using embedded DejaVu Sans Mono
font_selection_test: family/file parsing, precedence inputs, TTC index and bounds passed
font_selection_test: proportional design advances rejected despite equal rounded pixel widths
font_selection_test: frame boundary, input sequences, worker identity, mallocs=0 guard=ASan-inert
font_selection_test: publication swaps without waiting for worker return; input remains runnable
font_selection_test: TTC face selection, geometry recomputation and single adoption passed
font_selection_test: environment, installed family, failure fallback, proportional rejection, file precedence and restore passed

```

The ASan guard is explicitly inert; the release run supplies the allocation
proof. Captured stderr contains a single worker warning across the deliberate
selection failures, derived by counting that diagnostic's occurrences.
The worker raster of vendor DejaVu matches the embedded bake byte for byte.
The retained trace is decoded by `build/tools/tracedump`; trace sample counts
and percentiles derive from matched event pairs and nearest-rank statistics,
and its milliseconds derive from nanosecond differences divided by the
SI conversion. It is an artificial null-backend completion trace, not native
frame-drop or optical evidence.

(M)[AC] retained mid-typing trace decoder output (derivations above):

```text
G1 T4-T0     n=7 p50=0.051 ms p99=0.062 ms
G3 T5-T0     n=7 p50=0.051 ms p99=0.062 ms
T6-T0        n=7 p50=0.051 ms p99=0.062 ms
battery: Charging
```

## Build and complete sanitizer inventory

Strict GCC `make all` passes. Normal `make check` was run and stopped at
LeakSanitizer's ptrace restriction. With leak detection disabled, ASan and
UBSan remain enabled; `make check` passes base_test and stops at CLI socket
setup. No Makefile or test expectations were weakened.

```text
LeakSanitizer has encountered a fatal error.
HINT: LeakSanitizer does not work under ptrace (strace, gdb, etc)

== build/san/tests/cli_test
cli_test:215: FAIL ipc_server_init_display(&decoy, NULL, getenv("DISPLAY")) == IPC_OK
make: *** [Makefile:105: check] Error 1
```

All suites were then run separately with `ASAN_OPTIONS=detect_leaks=0` and
`DISPLAY=:99`. Results below derive from each subprocess return status; PASS
means success, not that every conditional native branch executed. The final
serial file test passes. The rebuilt editor passes its headless minimap,
selection, clipboard/recovery and allocation sections before reaching native
and IPC restrictions. The final large-editor serial run passes its headless
cooperative section before failing IPC setup. Initial file-watch/cooperative
failures did not recur on these serial runs. A stale intermediate test object
from an overlapping header build was rebuilt before final verification.

| Suite | Final result | Detail |
|---|---|---|
| base_test | PASS |  |
| cli_test | BLOCKED | IPC socket setup denied |
| editor_close_test | BLOCKED | Display connection / native fixture unavailable |
| editor_large_test | BLOCKED | IPC socket setup denied |
| editor_p4_test | BLOCKED | IPC socket setup denied |
| editor_session_test | PASS |  |
| editor_test | BLOCKED | Final rerun: native editor setup and IPC reload child |
| file_kill_test | BLOCKED | Display connection / native fixture unavailable |
| file_test | PASS | Serial rerun; shared watcher assertion passed |
| find_bench_test | PASS |  |
| find_bestof_test | PASS |  |
| find_p1r_test | PASS |  |
| find_simd_filter_verify_test | PASS |  |
| find_test | PASS |  |
| find_timeout_test | PASS |  |
| findui_test | PASS |  |
| font_selection_test | PASS |  |
| font_test | PASS |  |
| gl_bench_test | PASS | Pure checks pass; native EGL/Present branches SKIP |
| gl_gate_test | PASS |  |
| gl_test | PASS | Pure checks pass; native EGL/Present branches SKIP |
| harness_test | PASS |  |
| indent_test | PASS |  |
| ipc_test | BLOCKED | IPC socket setup denied |
| journal_kill_test | PASS |  |
| journal_test | PASS |  |
| keys_test | PASS |  |
| layout_test | PASS |  |
| lineidx_test | PASS |  |
| minimap_test | PASS |  |
| piece_bounds_test | PASS |  |
| piece_mem_test | PASS |  |
| piece_mt_test | PASS |  |
| piece_test | PASS |  |
| prewake_test | PASS |  |
| raster_test | BLOCKED | Display connection / native fixture unavailable |
| refproto_test | BLOCKED | Display connection / native fixture unavailable |
| refwin_test | BLOCKED | Display connection / native fixture unavailable |
| refwin_timeout_test | BLOCKED | Display connection / native fixture unavailable |
| render_test | PASS |  |
| replay_test | PASS |  |
| runtime_test | PASS |  |
| savectl_reload_test | PASS |  |
| savectl_test | PASS |  |
| scan_test | PASS |  |
| scroll_display_test | PASS |  |
| scroll_test | PASS |  |
| tabs_test | PASS |  |
| trace_test | PASS |  |
| undo_test | PASS |  |
| unicode_render_test | PASS |  |
| utf8_tables_test | PASS |  |
| utf8_test | PASS |  |
| view_test | PASS |  |
| work_foreground_test | PASS |  |
| work_test | PASS |  |
| wrap_test | PASS |  |
| x11_alignment_test | PASS |  |
| x11_clip_review_test | BLOCKED | Display connection / native fixture unavailable |
| x11_clip_test | BLOCKED | Display connection / native fixture unavailable |
| x11_display_guard_test | PASS |  |
| x11_identity_test | BLOCKED | Display connection / native fixture unavailable |
| x11_input_test | PASS |  |
| x11_live_test | BLOCKED | Display connection / native fixture unavailable |
| x11_order_test | BLOCKED | Display connection / native fixture unavailable |
| x11_stall_test | BLOCKED | Display connection / native fixture unavailable |
| x11_xi2_test | PASS |  |

The explicit display-failure list is the BLOCKED display rows above:
editor_close_test, file_kill_test, raster_test, refproto_test, refwin_test,
refwin_timeout_test, x11_clip_review_test, x11_clip_test, x11_identity_test,
x11_live_test, x11_order_test, x11_stall_test; editor_test also has blocked
native cases. GL pure suites pass with native branches skipped. CLI and the
IPC-dependent editor cases stop earlier at socket setup; ipc_test reports
`errno=EPERM (Operation not permitted)`. The coordinator must rerun these
outside this sandbox on Xvfb :99, and rerun leak detection where ptrace permits.
This report does **not** claim unrestricted make check passed.

The existing release allocation fixture also passes:

```text
editor_test: null 10000 keys mallocs=0 guard=active
```

Its key count is (G) the existing allocation fixture's loop count; mallocs are
(M)[AC] from its active allocator guard and must be zero by CLAUDE Law 2.
The font fuzzer builds under the strict sanitizer flags and replays the
embedded font plus the decimal-overflow pattern seed. This is seed replay,
not a timed fuzz campaign.

## NEW baseline and what remains

[docs/decisions/edit-5ih.md](../decisions/edit-5ih.md) records the post-swap G1
and G3 rows, power/load stamps, cell geometries, derived counts/statistics and
trace summaries. These are NEW baselines; changed cell/grid sizes are not a
regression comparison. Null-backend completed input and allocation counters
pass. Native raster/GL and the next Astra vs Sublime comparison drive remain
unverified here and must run on the coordinator's display environment.

Config-file parsing and inotify hot reload await the absent P6.4 loader; the
interim source and request hook are implemented. Native swaps, rendering and
Sublime frame timing remain for the next comparison drive. The editor's fixed
cell reservation caps still apply after font selection. This snapshot's
existing non-ASCII editor wiring is unchanged; reconcile the selected primary
with the Unicode/zoom workers when those dependent changes land.

Generated build/test/fuzz logs are retained under
`build/edit-5ih-evidence/`, and individual suite logs under
`build/edit-5ih-suites/`. The mid-typing and post-swap benchmark traces are
`build/edit-5ih-selection.trc` and `build/edit-5ih-final-font.trc`.
The red/green and NEW baseline text is also pasted here and in the decision,
so that evidence survives without copying generated binaries.

Outside scope: the existing GPU fallback setup path in `src/editor/open.c`
still writes its diagnostic on the UI/setup thread. It was left unchanged;
font-selection diagnostics run on workers.

No unrelated defect was fixed. Initial watcher/deadline failures were reported
and rerun; their serial headless sections pass. Remaining test exceptions are
display/socket/ptrace restrictions. The tree builds and module STATUS files
are updated. This worker stops after edit-5ih.
