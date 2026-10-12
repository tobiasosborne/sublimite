# edit-r1l worker report

Implemented the assigned Unicode cache-exhaustion bug. The continuation section below supersedes the initial allocator-verification claim: the lander correctly found a release-only clipboard failure, now covered by a new release regression. Work stayed in this worktree; no `bd` or `git` commands were run. Protected coordination documents were not edited. Finding 34 (review identifier) in `docs/reviews/editor-2.md` has no cross-reference.

## Files touched

- `src/font/discovery.c`: thread-local library exemption for worker spawn setup/cleanup.
- `tests/font_test.c`: first fallback discovery under the real counting guard.
- `src/font/async.c`: populated-prefix pin clearing and ASCII-only frame fast return; exact-key LRU, hash-chain deletion, reusable narrow/wide rectangles, frame pins and pending-request protection; worker composition no longer consumes append-only cache entries.
- `src/font/unicode.c`: expose the existing compositor without key/rectangle admission.
- `src/font/font.h`: compositor and frame-retention contracts.
- `src/editor/editor.c`: retain outgoing grids when beginning a frame.
- `src/editor/fonts.c`: full replacement can reclaim outgoing pins after backend retirement, preserving incoming lookups.
- `tests/unicode_render_test.c`: capacity-derived exhaustion, exact pixels, retained different glyph, eviction/revisit, reverse navigation, maximum-em physical exhaustion and width-changing reuse.
- `docs/decisions/edit-r1l.md`: implementation choices and capacity derivations.
- `docs/worker-reports/edit-r1l-s6.md`: this report.

## Red run

The test was written and run before implementation. Command: `make build/tests/unicode_render_test`, then `build/tests/unicode_render_test`. Compilation passed; the executable failed. Power: BAT0 `Not charging`, interpreted as [AC] by the repository convention. Diagnostics are verbatim (M)[AC]; source-line numbers identify the original red fixture. Capacity is (P) `FONT_RUNTIME_MAX_GLYPHS - LAYOUT_ASCII_GLYPHS`; the returned ASCII slot is (P) `'#' - ' '`.

```text
unicode_render_test: exhaustion visit=2651 capacity=4001 returned ASCII slot=3
unicode_render_test:446: FAIL slot >= LAYOUT_ASCII_GLYPHS && slot < grid.glyph_count
unicode_render_test:619: FAIL cache_exhaustion() == 0
```

The bounded-probe refusal reproduced before the hard descriptor ceiling. The final fixture uses capacity-derived binary default-ignorable suffixes instead of repeating a suffix by ordinal, preserving the same supported accented artwork with compact keys. It adds frame-pin and physical-capacity coverage.

## Green run

`make all` passed with the requested GCC release warning flags. The installed GCC and clang match the user-specified release and sanitizer toolchain generations (M)[AC], verified through their `--version` output. The final release executable passed, including its real malloc guard. All numeric output below is verbatim (M)[AC]; capacity and size choices derive from existing source constants as described in the decision document.

Command: `build/tests/unicode_render_test`.

```text
unicode_render_test: supported clusters beyond descriptor cache capacity recover exact ink on return
unicode_render_test: supported clusters beyond maximum-em cache capacity recover exact ink on return
unicode_render_test: tab title 73j-å文 holds adopted glyphs, 7 cells for 9 bytes
unicode_render_test: emoji first-scalar fallback, invisibles and true missing glyph; input=0 wrap=0 passed
unicode_render_test: emoji first-scalar fallback, invisibles and true missing glyph; input=1 wrap=0 passed
unicode_render_test: emoji first-scalar fallback, invisibles and true missing glyph; input=0 wrap=1 passed
unicode_render_test: emoji first-scalar fallback, invisibles and true missing glyph; input=1 wrap=1 passed
unicode_render_test: minimap completion mode=0 fills=0 damage=4
unicode_render_test: minimap completion mode=1 fills=0 damage=4
unicode_render_test: minimap completion mode=2 fills=0 damage=6
unicode_render_test: minimap completion mode=3 fills=3 damage=7
unicode_render_test: minimap completion mode=4 fills=0 damage=6
unicode_render_test: minimap completion mode=5 fills=0 damage=5
unicode_render_test: DPI 96 zoom/reset matches true-em primary pixels; pending owner retirement passed
unicode_render_test: DPI 243 zoom/reset matches true-em primary pixels; pending owner retirement passed
unicode_render_test: initial and injected é α 日 🙂; widths, combining overlay, atlas pixels and allocation guard passed
```

Sanitizer verification uses `ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1`, preserving ASan and UBSan. LeakSanitizer itself cannot run under this sandbox's ptrace restrictions; the unmodified `make check` stopped in `base_test` with its fatal environment diagnostic. No application code was changed for that restriction.

Command: `ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 build/san/tests/unicode_render_test`. The following green output is verbatim (M)[AC]:

```text
unicode_render_test: supported clusters beyond descriptor cache capacity recover exact ink on return
unicode_render_test: supported clusters beyond maximum-em cache capacity recover exact ink on return
unicode_render_test: tab title 73j-å文 holds adopted glyphs, 7 cells for 9 bytes
unicode_render_test: emoji first-scalar fallback, invisibles and true missing glyph; input=0 wrap=0 passed
unicode_render_test: emoji first-scalar fallback, invisibles and true missing glyph; input=1 wrap=0 passed
unicode_render_test: emoji first-scalar fallback, invisibles and true missing glyph; input=0 wrap=1 passed
unicode_render_test: emoji first-scalar fallback, invisibles and true missing glyph; input=1 wrap=1 passed
unicode_render_test: minimap completion mode=0 fills=0 damage=4
unicode_render_test: minimap completion mode=1 fills=0 damage=4
unicode_render_test: minimap completion mode=2 fills=0 damage=6
unicode_render_test: minimap completion mode=3 fills=3 damage=7
unicode_render_test: minimap completion mode=4 fills=0 damage=6
unicode_render_test: minimap completion mode=5 fills=0 damage=5
unicode_render_test: DPI 96 zoom/reset matches true-em primary pixels; pending owner retirement passed
unicode_render_test: DPI 243 zoom/reset matches true-em primary pixels; pending owner retirement passed
unicode_render_test: initial and injected é α 日 🙂; widths, combining overlay, atlas pixels and allocation guard passed
```

Requested headless suites ran serially. `editor_test --headless`, `editor_session_test`, `journal_test`, and `editor_p4_test --73j-null` passed. The full P4 headless suite passed its initial null indentation checks and then failed at IPC initialization; the sandbox denies Unix socket creation. Font selection and font size suites passed as well. The editor's allocation checks are real in release and explicitly ASan-inert in sanitizer builds.

All additional suites outside the Makefile's display-lane filter ran serially. Results below are (M)[AC], derived from each subprocess exit status. `file_test` passed its isolated rerun; its initial watch-notification failure is recorded under findings below. Native checks embedded in `file_kill_test` and socket checks in `ipc_test` remain blocked by the environment.

| Suite / mode | Result |
| --- | --- |
| `unicode_render_test` | PASS |
| `font_test` | PASS |
| `editor_test --headless` | PASS |
| `editor_session_test` | PASS |
| `editor_p4_test --headless` | FAIL |
| `editor_p4_test --73j-null` | PASS |
| `journal_test` | PASS |
| `base_test` | PASS |
| `config_editor_test` | PASS |
| `config_test` | PASS |
| `config_worker_test` | PASS |
| `file_kill_test` | FAIL; isolated rerun FAIL |
| `file_test` | FAIL; isolated rerun PASS |
| `find_bench_test` | PASS |
| `find_bestof_test` | PASS |
| `find_p1r_test` | PASS |
| `find_simd_filter_verify_test` | PASS |
| `find_test` | PASS |
| `find_timeout_test` | PASS |
| `findui_test` | PASS |
| `font_selection_test` | PASS |
| `font_size_test` | PASS |
| `harness_test` | PASS |
| `hl_parse_test` | PASS |
| `hl_test` | PASS |
| `hl_worker_test` | PASS |
| `indent_test` | PASS |
| `ipc_test` | FAIL |
| `journal_kill_test` | FAIL; isolated rerun FAIL |
| `keys_test` | PASS |
| `layout_test` | PASS |
| `lineidx_test` | PASS |
| `minimap_test` | PASS |
| `piece_bounds_test` | PASS |
| `piece_mem_test` | PASS |
| `piece_mt_test` | PASS |
| `piece_test` | PASS |
| `prewake_test` | PASS |
| `raster_scroll_test` | PASS |
| `render_test` | PASS |
| `replay_test` | PASS |
| `runtime_test` | PASS |
| `savectl_reload_test` | PASS |
| `savectl_test` | PASS |
| `scan_test` | PASS |
| `scroll_display_test` | PASS |
| `scroll_test` | PASS |
| `startup_test` | PASS |
| `tabs_test` | PASS |
| `trace_pairs_test` | PASS |
| `trace_test` | PASS |
| `undo_test` | PASS |
| `utf8_tables_test` | PASS |
| `utf8_test` | PASS |
| `view_test` | PASS |
| `wayland_bench_test` | PASS |
| `wayland_gl_test` | PASS |
| `wayland_test` | PASS |
| `window_size_test` | PASS |
| `work_foreground_test` | PASS |
| `work_test` | PASS |
| `wrap_test` | PASS |

Additional headless/pure checks from the Makefile’s broad display filter ran serially. The initial shared-lock attempts could not acquire another runner’s lock; the pure/mocked renderer checks were then executed directly with X access already proven unavailable. `gl_test` passed its pure checks and explicitly skipped native diagnostics. `refproto_test` also mocks clock messages but still requires a real X window, and failed at display connection. Results are (M)[AC].

| Suite / mode | Result |
| --- | --- |
| `editor_close_test --headless` | PASS |
| `editor_hot_exit_test` | PASS |
| `editor_scroll_test` | PASS |
| `editor_large_test` | BLOCKED: IPC socket |
| `gl_bench_test` | PASS |
| `gl_gate_test` | PASS |
| `gl_scroll_test` | PASS |
| `gl_test` | PASS |
| `refproto_test` | BLOCKED: X socket |
| `test_replay_cli.sh` | PASS |

`build/bench/font_bench` completed successfully on [AC]. Its raster and cluster allocation guards passed and its tracked rows reported success. Counts and samples are inherited from `bench/font_bench.c`; no performance budget or timing threshold was introduced by this bead.

Selected font benchmark measurements (M)[AC], derived from monotonic-clock sample differences and the benchmark harness's sorted percentiles; no new gate is defined. All durations are nanoseconds:

| Existing benchmark row | p50 | p99 |
| --- | --- | --- |
| `font_raster_TRACK` | 3752 | 4078 |
| `font_cluster_cold_TRACK` | 10967 | 19932 |
| `font_cluster_cached_TRACK` | 34 | 37 |

## Full check and environment restrictions

Unmodified `make check` stopped in `base_test` because LeakSanitizer cannot operate under ptrace. Verbatim (M)[AC] diagnostic, omitting the process identifier:

```text
LeakSanitizer has encountered a fatal error.
HINT: LeakSanitizer does not work under ptrace (strace, gdb, etc)
```

Final command: `ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 EDIT_DISPLAY_LOCK_TIMEOUT=0 make check`. The lock setting means no waiting (P: the display is already proven inaccessible), avoiding an earlier wait on another runner's shared display lock. The final run acquired the lock, passed `base_test`, and stopped at the CLI socket check. Output below is verbatim (M)[AC]; diagnostic source-line numbers identify test locations.

```text
== build/san/tests/base_test
0.0.1-dev
arena: ok
malloc guard: SKIP (AddressSanitizer owns malloc)
pool: ok
malloc guard exempt: SKIP (sanitizer owns malloc)
cpu: avx2=1 popcnt=1
base_test: PASS
== build/san/tests/cli_test
cli_test:391: FAIL ipc_server_init_display(&decoy, NULL, getenv("DISPLAY")) == IPC_OK
cli_test: §36 forced startup timeout correctly failed
cli_test:391: FAIL ipc_server_init_display(&decoy, NULL, getenv("DISPLAY")) == IPC_OK
make: *** [Makefile:131: check] Error 1
```

Read-only `DISPLAY=:99 xdpyinfo` failed with `unable to open display ":99"` (M)[AC]; the display identifier is the coordinator-specified Xvfb target. A separate Unix socket probe failed with `Operation not permitted`, matching `ipc_test`'s diagnostic. Therefore this report does not claim a full `make check` pass. Native display suites fail inside this sandbox for lack of the X socket; the coordinator must run them on the supplied Xvfb display.

## Findings outside the bead's scope

- `editor_p4_test --headless`, `editor_large_test`, `ipc_test` and `cli_test` cannot initialize IPC in this sandbox. No socket code was changed. Verbatim (M)[AC] evidence:

```text
init rc=4 errno=1 (Operation not permitted)
FAIL tests/ipc_test.c:130: init_rc==IPC_OK
editor_p4_test:555: FAIL ipc_server_init(&server, runtime) == IPC_OK
```

- `refproto_test` also stops at its real X connection, despite scripting its clock replies. Verbatim (M)[AC] evidence:

```text
refproto_test: window=0x00000000 connect DISPLAY=:99: connection error=1
refproto_test:62: FAIL refproto_display_open(d,"refproto_test",true) == 0
```

- `file_kill_test` fails its viewport initialization and queued viewport children. Its included `bench/file_bench.c` helper calls `plat_init`, so these are native display checks despite the suite not appearing in the Makefile's display filter. The same failures remain on an isolated rerun. No file or display implementation was changed. Verbatim (M)[AC] diagnostics:

```text
file_kill_test:198: FAIL rc == 0
file_kill_test:324: FAIL WIFEXITED(status) && WEXITSTATUS(status) == 0
file_kill_test:324: FAIL WIFEXITED(status) && WEXITSTATUS(status) == 0
file_kill_test: FAIL visibility/cancellation; power-loss barriers unverified
```

- `file_test` initially failed its shared watch-notification assertion; its isolated rerun passed. This is outside glyph rendering and was not fixed. Verbatim (M)[AC] initial diagnostic:

```text
tests/file_test.c:308: FAIL file_changed(files[targets[0]]) && file_changed(files[targets[1]])
file_test: 1 FAILED
```

- `journal_kill_test` fails its kill/replay check on both serial runs. This suite does not use the font owner and its implementation was untouched. This is an outstanding non-display failure for the coordinator/journal worker, rather than a claimed environment skip. Verbatim (M)[AC] isolated-rerun diagnostic:

```text
journal_kill_test: FAIL trial=0 target=1 issued=1 page_cache=1 ack=0 replay=0 rc=6 error=0
```

## What remains

The assigned cache-exhaustion fix and regression are implemented; release builds and affected headless sanitizer suites pass. The coordinator still needs the full display verification on the specified Xvfb target, unrestricted leak detection, IPC verification with permitted Unix sockets, and investigation of the unrelated journal-kill failure. The renderer's existing simultaneous descriptor/pixel ceilings remain as documented in the decision; a document can exceed resident capacity through navigation without changing supported glyphs into `#`.

No additional bead was implemented and no `STATUS.md` was needed for unfinished glyph work.

## Continuation: release allocator rejection

The lander correctly rejected the initial landing: the sanitizer allocator guard is inert, and release first-glyph discovery overlaps the guarded clipboard window. This continuation keeps first-request/pump scheduling asynchronous and exempts only worker-side libc spawn setup/cleanup with the existing calling-thread exemption. Glyph rendering, parsing, arena operations and publication remain counted. The actions-init error branch balances the exemption. The reasoning and viewport/descriptor-derived frame-work bound are recorded in `docs/decisions/edit-r1l.md`.

Continuation files touched: `src/font/discovery.c`, `src/font/async.c`, `tests/font_test.c`, `docs/decisions/edit-r1l.md`, and this report. The pin clear now touches only admitted descriptors and an ASCII-only owner skips pin traversal. Neither grid scan scales with document length; both remain necessary for borrowed composed and retained text cells.

Power during these runs: BAT0 `Not charging`, the repository's [AC] convention. All numeric transcript output below is verbatim (M)[AC] from the executed binaries: source-line numbers identify assertions; fixture counts and bounds come from the existing suites, and no budget is added. The expected allocation count is zero (P: law 2); the release repetition count comes from the user's verification instruction.

### Correction red run

Before changing implementation, extended `test_fallback` to begin the real guard before first worker submission and end it after authenticated adoption. `make build/tests/font_test` succeeded; the new assertion then failed. Full output:

```text
$ build/tests/font_test
font_test: wide W scratch exhaustion
tests/font_test.c:1147: CHECK failed: allocations == 0
font_test: 1 failure(s)
P2-1 §3 GREEN: sparse oversized font and cmap-relative offsets refused before stb
review §14 GREEN: short reads roll back arena and clear lengths; NULL length rejected
review §1 GREEN: metrics-preserving cmap rejected; every baked ASCII pixel matches live outlines
U+3042 raster result: -1
font_test: wide W exhaustion returns FONT_ERR_NOMEM; normal arena succeeds
font_test: bitmap/scratch exhaustion sweep and arena rollback passed
font_test: mallocs during exhaustion sweep: 0
font_test: mallocs during 600 rasterisations: 0
font_test: hostile font data rejected without out-of-bounds reads
font_test: TTC face index honoured
P2-1 §4 GREEN: extreme coordinates and small-ascent bitmap rejected
P2-1 §30 GREEN: CFF errors rejected, empty glyph preserved, fallback recovers
P2-1 §31 slices (M)[AC], shared loaded box, paired rejection/normal max: 4297/6024 ns; TRACK only
P2-1 §31 GREEN: tiny-bitmap high-outline workload refused on UI
font_test: hostile CFF (byte flips, truncations, offsets, subr bomb) handled cleanly
font_test: Noto CJK CFF faces load; truncated copies rejected cleanly
P2-1 §29 GREEN: worker-prepared owner adopts through mailbox; Unicode grid/pixels match
P2-1 §32 GREEN: cancellation finishes after helper exits with inherited stdout
review work-scan §9 GREEN: production discovery cancels and reaps an unbounded helper; no adoption
review §7 GREEN: cancelled discovery has no adoptable done side channel
review §8 GREEN: full mailbox drains, terminal discovery completion arrives
font_test: worker discovery malloc guard: 4 allocations (active)
font_test: fallback fontconfig=1 cjk='/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc' emoji='/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf'
exit=1
```

The existing production regression also reproduced before implementation. Relevant output:

```text
$ build/tests/editor_test
default editing: uppercase Unicode word editor_test:3448: FAIL allocations == 0
editor_test:4225: FAIL clipboard_test(false) == 0
exit=1
```

### Correction release green runs

`make all` rebuilt successfully with the required GCC warning flags. The requested subsequent invocation was:

```text
$ make all
make: Nothing to be done for 'all'.
```

The new release discovery regression passed. Full output:

```text
$ build/tests/font_test
font_test: wide W scratch exhaustion
P2-1 §3 GREEN: sparse oversized font and cmap-relative offsets refused before stb
review §14 GREEN: short reads roll back arena and clear lengths; NULL length rejected
review §1 GREEN: metrics-preserving cmap rejected; every baked ASCII pixel matches live outlines
U+3042 raster result: -1
font_test: wide W exhaustion returns FONT_ERR_NOMEM; normal arena succeeds
font_test: bitmap/scratch exhaustion sweep and arena rollback passed
font_test: mallocs during exhaustion sweep: 0
font_test: mallocs during 600 rasterisations: 0
font_test: hostile font data rejected without out-of-bounds reads
font_test: TTC face index honoured
P2-1 §4 GREEN: extreme coordinates and small-ascent bitmap rejected
P2-1 §30 GREEN: CFF errors rejected, empty glyph preserved, fallback recovers
P2-1 §31 slices (M)[AC], shared loaded box, paired rejection/normal max: 10206/12959 ns; TRACK only
P2-1 §31 GREEN: tiny-bitmap high-outline workload refused on UI
font_test: hostile CFF (byte flips, truncations, offsets, subr bomb) handled cleanly
font_test: Noto CJK CFF faces load; truncated copies rejected cleanly
P2-1 §29 GREEN: worker-prepared owner adopts through mailbox; Unicode grid/pixels match
P2-1 §32 GREEN: cancellation finishes after helper exits with inherited stdout
review work-scan §9 GREEN: production discovery cancels and reaps an unbounded helper; no adoption
review §7 GREEN: cancelled discovery has no adoptable done side channel
review §8 GREEN: full mailbox drains, terminal discovery completion arrives
font_test: worker discovery malloc guard: 0 allocations (active)
font_test: fallback fontconfig=1 cjk='/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc' emoji='/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf'
font_test: all passed
exit=0
```

Each requested unqualified release editor invocation now passes the clipboard assertion with the guard active, then fails in later IPC/native-display review cases. These are environment-blocked full-suite runs, not claimed full passes. Relevant output from each invocation:

```text
$ build/tests/editor_test (requested invocation 1)
default editing: uppercase Unicode word reload launcher result=4
editor_test:2256: FAIL waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0
editor_test:1539: FAIL editor_open(&e, &cfg, &b) == 0
editor_test:1565: FAIL editor_open(&e, &cfg, &b) == 0
clipboard malloc guard: 0 allocations (active); expected zero by law
revieeditor_test:1849: FAIL editor_open(&e, &cfg, &backend) == 0
editor_test:2611: FAIL editor_open(&e, &cfg, &b) == 0
editor_test:4230: FAIL review_suite(headless ? "headless" : NULL) == 0
exit=1
```

```text
$ build/tests/editor_test (requested invocation 2)
default editing: uppercase Unicode word reload launcher result=4
editor_test:2256: FAIL waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0
editor_test:1539: FAIL editor_open(&e, &cfg, &b) == 0
editor_test:1565: FAIL editor_open(&e, &cfg, &b) == 0
clipboard malloc guard: 0 allocations (active); expected zero by law
revieeditor_test:1849: FAIL editor_open(&e, &cfg, &backend) == 0
editor_test:2611: FAIL editor_open(&e, &cfg, &b) == 0
editor_test:4230: FAIL review_suite(headless ? "headless" : NULL) == 0
exit=1
```

```text
$ build/tests/editor_test (requested invocation 3)
default editing: uppercase Unicode word reload launcher result=4
editor_test:2256: FAIL waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0
editor_test:1539: FAIL editor_open(&e, &cfg, &b) == 0
editor_test:1565: FAIL editor_open(&e, &cfg, &b) == 0
clipboard malloc guard: 0 allocations (active); expected zero by law
revieeditor_test:1849: FAIL editor_open(&e, &cfg, &backend) == 0
editor_test:2611: FAIL editor_open(&e, &cfg, &b) == 0
editor_test:4230: FAIL review_suite(headless ? "headless" : NULL) == 0
exit=1
```

The headless mode runs the production clipboard regression and the remaining headless editor cases with the real release guard active. Every requested repetition passed; relevant output:

```text
$ build/tests/editor_test --headless (invocation 1, same user-derived repetition count)
edit-g11 null typing: keys=95 frames=97 orange_frames=0 stale_frames=96 changed_pending=0 chrome_markers=0 mallocs=0 fill_max_ns=5113
clipboard malloc guard: 0 allocations (active); expected zero by law
clipboard bulk: worker queue, cross-slice CRLF, replacement/undo, journal, mallocs=0 passed
clipboard PRIMARY: queued selection/collapse/middle paste waits for latest capture, mallocs=0 passed
P1-1 section 6: inactive exact counts/jump, bulk cancellation/retry, mallocs=0 guard=active passed (M)[AC]
editor_test: null 10000 keys mallocs=0 guard=active
editor_test: headless passed
exit=0
```

```text
$ build/tests/editor_test --headless (invocation 2, same user-derived repetition count)
edit-g11 null typing: keys=95 frames=97 orange_frames=0 stale_frames=96 changed_pending=0 chrome_markers=0 mallocs=0 fill_max_ns=5214
clipboard malloc guard: 0 allocations (active); expected zero by law
clipboard bulk: worker queue, cross-slice CRLF, replacement/undo, journal, mallocs=0 passed
clipboard PRIMARY: queued selection/collapse/middle paste waits for latest capture, mallocs=0 passed
P1-1 section 6: inactive exact counts/jump, bulk cancellation/retry, mallocs=0 guard=active passed (M)[AC]
editor_test: null 10000 keys mallocs=0 guard=active
editor_test: headless passed
exit=0
```

```text
$ build/tests/editor_test --headless (invocation 3, same user-derived repetition count)
edit-g11 null typing: keys=95 frames=97 orange_frames=0 stale_frames=96 changed_pending=0 chrome_markers=0 mallocs=0 fill_max_ns=4576
clipboard malloc guard: 0 allocations (active); expected zero by law
clipboard bulk: worker queue, cross-slice CRLF, replacement/undo, journal, mallocs=0 passed
clipboard PRIMARY: queued selection/collapse/middle paste waits for latest capture, mallocs=0 passed
P1-1 section 6: inactive exact counts/jump, bulk cancellation/retry, mallocs=0 guard=active passed (M)[AC]
editor_test: null 10000 keys mallocs=0 guard=active
editor_test: headless passed
exit=0
```

### Correction sanitizer green runs

Compiler generations were checked again with `gcc --version` and `clang --version` and match the user-specified toolchain (M)[AC]. Ran the following suites serially with `ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1`; leak detection remains disabled solely for the previously documented sandbox ptrace limitation. Full requested font and Unicode outputs:

```text
$ ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 build/san/tests/unicode_render_test
unicode_render_test: supported clusters beyond descriptor cache capacity recover exact ink on return
unicode_render_test: supported clusters beyond maximum-em cache capacity recover exact ink on return
unicode_render_test: tab title 73j-å文 holds adopted glyphs, 7 cells for 9 bytes
unicode_render_test: emoji first-scalar fallback, invisibles and true missing glyph; input=0 wrap=0 passed
unicode_render_test: emoji first-scalar fallback, invisibles and true missing glyph; input=1 wrap=0 passed
unicode_render_test: emoji first-scalar fallback, invisibles and true missing glyph; input=0 wrap=1 passed
unicode_render_test: emoji first-scalar fallback, invisibles and true missing glyph; input=1 wrap=1 passed
unicode_render_test: minimap completion mode=0 fills=0 damage=4
unicode_render_test: minimap completion mode=1 fills=0 damage=4
unicode_render_test: minimap completion mode=2 fills=0 damage=6
unicode_render_test: minimap completion mode=3 fills=3 damage=7
unicode_render_test: minimap completion mode=4 fills=0 damage=6
unicode_render_test: minimap completion mode=5 fills=0 damage=5
unicode_render_test: DPI 96 zoom/reset matches true-em primary pixels; pending owner retirement passed
unicode_render_test: DPI 243 zoom/reset matches true-em primary pixels; pending owner retirement passed
unicode_render_test: initial and injected é α 日 🙂; widths, combining overlay, atlas pixels and allocation guard passed
exit=0
```

```text
$ ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 build/san/tests/font_test
font_test: wide W scratch exhaustion
P2-1 §3 GREEN: sparse oversized font and cmap-relative offsets refused before stb
review §14 GREEN: short reads roll back arena and clear lengths; NULL length rejected
review §1 GREEN: metrics-preserving cmap rejected; every baked ASCII pixel matches live outlines
U+3042 raster result: -1
font_test: wide W exhaustion returns FONT_ERR_NOMEM; normal arena succeeds
font_test: bitmap/scratch exhaustion sweep and arena rollback passed
font_test: malloc guard inactive (ASan build), no-malloc check skipped
font_test: hostile font data rejected without out-of-bounds reads
font_test: TTC face index honoured
P2-1 §4 GREEN: extreme coordinates and small-ascent bitmap rejected
P2-1 §30 GREEN: CFF errors rejected, empty glyph preserved, fallback recovers
P2-1 §31 slices (M)[AC], shared loaded box, paired rejection/normal max: 17416/13983 ns; TRACK only
P2-1 §31 GREEN: tiny-bitmap high-outline workload refused on UI
font_test: hostile CFF (byte flips, truncations, offsets, subr bomb) handled cleanly
font_test: Noto CJK CFF faces load; truncated copies rejected cleanly
P2-1 §29 GREEN: worker-prepared owner adopts through mailbox; Unicode grid/pixels match
P2-1 §32 GREEN: cancellation finishes after helper exits with inherited stdout
review work-scan §9 GREEN: production discovery cancels and reaps an unbounded helper; no adoption
review §7 GREEN: cancelled discovery has no adoptable done side channel
review §8 GREEN: full mailbox drains, terminal discovery completion arrives
font_test: worker discovery malloc guard: 0 allocations (ASan-inert)
font_test: fallback fontconfig=1 cjk='/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc' emoji='/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf'
font_test: all passed
exit=0
```

`editor_test --headless`, `editor_session_test`, `editor_p4_test --73j-null` and `journal_test` also passed serially under ASan/UBSan. The full `editor_p4_test --headless` still fails at IPC initialization; its initial null indentation/allocation checks pass.

Full-check attempt:

```text
$ ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 EDIT_DISPLAY_LOCK_TIMEOUT=0 make check
== build/san/tests/base_test
0.0.1-dev
arena: ok
malloc guard: SKIP (AddressSanitizer owns malloc)
pool: ok
malloc guard exempt: SKIP (sanitizer owns malloc)
cpu: avx2=1 popcnt=1
base_test: PASS
== build/san/tests/cli_test
cli_test:391: FAIL ipc_server_init_display(&decoy, NULL, getenv("DISPLAY")) == IPC_OK
cli_test: §36 forced startup timeout correctly failed
cli_test:391: FAIL ipc_server_init_display(&decoy, NULL, getenv("DISPLAY")) == IPC_OK
make: *** [Makefile:206: check] Error 1
exit=2
```

Display access was verified directly:

```text
$ DISPLAY=:99 xdpyinfo
xdpyinfo:  unable to open display ":99".
exit=1
```

The coordinator must rerun display suites on Xvfb :99 (P: the user-supplied display target); this sandbox lacks access to the X socket. Unqualified release editor runs also encounter the existing real-launcher IPC case, while the headless repetitions bypass those native cases. This report does not claim a full `make check` pass.

### Remaining non-display sweep and outside-scope findings

After the full-check IPC stop, every binary outside the Makefile's display-lane filter was run serially, along with the explicit headless editor modes above. Current results are derived from subprocess exit statuses (M)[AC]:

| Command | Result |
| --- | --- |
| `build/san/tests/unicode_render_test` | PASS |
| `build/san/tests/font_test` | PASS |
| `build/san/tests/editor_test --headless` | PASS |
| `build/san/tests/editor_session_test` | PASS |
| `build/san/tests/editor_p4_test --headless` | FAIL; see diagnostic below |
| `build/san/tests/editor_p4_test --73j-null` | PASS |
| `build/san/tests/journal_test` | PASS |
| `build/san/tests/base_test` | PASS |
| `build/san/tests/config_editor_test` | PASS |
| `build/san/tests/config_find_test` | PASS |
| `build/san/tests/config_test` | PASS |
| `build/san/tests/config_worker_test` | PASS |
| `build/san/tests/dataloss_test` | PASS |
| `build/san/tests/file_kill_test` | FAIL; see diagnostic below |
| `build/san/tests/file_test` | PASS |
| `build/san/tests/find_bench_test` | PASS |
| `build/san/tests/find_bestof_test` | PASS |
| `build/san/tests/find_p1r_test` | PASS |
| `build/san/tests/find_simd_filter_verify_test` | PASS |
| `build/san/tests/find_test` | PASS |
| `build/san/tests/find_timeout_test` | PASS |
| `build/san/tests/findui_test` | PASS |
| `build/san/tests/font_selection_test` | PASS |
| `build/san/tests/font_size_test` | PASS |
| `build/san/tests/harness_test` | PASS |
| `build/san/tests/hl_parse_test` | PASS |
| `build/san/tests/hl_test` | PASS |
| `build/san/tests/hl_worker_test` | PASS |
| `build/san/tests/indent_test` | PASS |
| `build/san/tests/ipc_test` | FAIL; see diagnostic below |
| `build/san/tests/journal_group_test` | PASS |
| `build/san/tests/journal_kill_test` | FAIL; see diagnostic below |
| `build/san/tests/journal_retire_test` | PASS |
| `build/san/tests/keys_test` | PASS |
| `build/san/tests/layout_test` | PASS |
| `build/san/tests/lineidx_test` | PASS |
| `build/san/tests/minimap_test` | PASS |
| `build/san/tests/piece_bounds_test` | PASS |
| `build/san/tests/piece_mem_test` | PASS |
| `build/san/tests/piece_mt_test` | PASS |
| `build/san/tests/piece_test` | PASS |
| `build/san/tests/prewake_test` | PASS |
| `build/san/tests/raster_scroll_test` | PASS |
| `build/san/tests/render_test` | PASS |
| `build/san/tests/replay_test` | PASS |
| `build/san/tests/runtime_test` | PASS |
| `build/san/tests/savectl_reload_test` | PASS |
| `build/san/tests/savectl_test` | PASS |
| `build/san/tests/scan_test` | PASS |
| `build/san/tests/scroll_display_test` | PASS |
| `build/san/tests/scroll_test` | PASS |
| `build/san/tests/startup_test` | PASS |
| `build/san/tests/tabs_test` | PASS |
| `build/san/tests/trace_pairs_test` | PASS |
| `build/san/tests/trace_test` | PASS |
| `build/san/tests/undo_test` | PASS |
| `build/san/tests/utf8_tables_test` | PASS |
| `build/san/tests/utf8_test` | PASS |
| `build/san/tests/view_test` | PASS |
| `build/san/tests/wayland_bench_test` | PASS |
| `build/san/tests/wayland_gl_test` | PASS |
| `build/san/tests/wayland_test` | PASS |
| `build/san/tests/window_size_test` | PASS |
| `build/san/tests/work_foreground_test` | PASS |
| `build/san/tests/work_test` | PASS |
| `build/san/tests/wrap_test` | PASS |

Current failed-run diagnostics (M)[AC]; no unrelated code was changed:

```text
$ build/san/tests/editor_p4_test --headless
editor_p4_test:555: FAIL ipc_server_init(&server, runtime) == IPC_OK
editor_p4_test: null Enter selection LIMIT scenario=0 and synchronous retry preserve bytes/history/cursor passed
editor_p4_test: null Enter selection LIMIT scenario=1 and synchronous retry preserve bytes/history/cursor passed
editor_p4_test: null Enter selection LIMIT scenario=2 and synchronous retry preserve bytes/history/cursor passed
editor_p4_test: null Enter giant selection shortening succeeds atomically mallocs=0 guard=ASan-inert
editor_p4_test: null brace selection LIMIT scenario=0 and synchronous retry preserve bytes/history/cursor passed
editor_p4_test: null brace selection LIMIT scenario=1 and synchronous retry preserve bytes/history/cursor passed
editor_p4_test: null brace selection LIMIT scenario=2 and synchronous retry preserve bytes/history/cursor passed
editor_p4_test: null brace giant selection shortening succeeds atomically mallocs=0 guard=ASan-inert
exit=1
```

```text
$ build/san/tests/file_kill_test
file_kill_test:198: FAIL rc == 0
file_kill_test:324: FAIL WIFEXITED(status) && WEXITSTATUS(status) == 0
file_kill_test:324: FAIL WIFEXITED(status) && WEXITSTATUS(status) == 0
SELF_CHECK section=26 PASS
SELF_CHECK section=27 PASS
SELF_CHECK section=28 PASS
file_cancelled_open: PASS queued/pending-prefix/pending-map getters and filtered delivery
file_save_alloc: PASS pooled fresh ack guard=ASan-inert (release required)
file_kill_test: FAIL visibility/cancellation; power-loss barriers unverified
exit=1
```

```text
$ build/san/tests/ipc_test
init rc=4 errno=1 (Operation not permitted)
FAIL tests/ipc_test.c:130: init_rc==IPC_OK
exit=1
```

```text
$ build/san/tests/journal_kill_test
journal_kill_test: FAIL trial=0 target=1 issued=1 page_cache=1 ack=0 replay=0 rc=6 error=0
exit=1
```

`editor_p4_test` and `ipc_test` cannot create their Unix sockets in this sandbox. `file_kill_test` embeds native viewport checks and remains blocked by display access despite falling outside the Makefile display filter. The unrelated `journal_kill_test` failure reproduces again and is still an outstanding non-display failure for the coordinator/journal worker; no font owner is involved in that suite. `file_test` passed on this continuation.

### Continuation completion and remaining verification

The release allocator correction and regression are complete. The new real-guard font regression passes; all requested headless release editor repetitions pass with zero allocations; sanitizer Unicode and font tests pass, as do the affected headless editor, session, null P4 and journal suites. Full unqualified release editor runs pass the formerly failing clipboard check and then encounter the sandbox's IPC/display restrictions. Full `make check` remains blocked at CLI IPC setup, and the unrelated journal-kill failure is reported above rather than fixed. The coordinator must rerun full native display/IPC suites on Xvfb :99 with permitted sockets and unrestricted leak detection. No additional bead or unfinished module work was introduced.

Complete continuation transcripts are copied into `build/edit-r1l-verification/` in this worktree; the excerpts above retain the assertion failures, guard results and suite outcomes.

Continuation font benchmark also completed successfully on [AC]. The following values are (M)[AC] from the existing harness's monotonic samples and percentile calculations; sample counts and tracked bounds are inherited from `bench/font_bench.c`, with no new gate or budget:

```text
$ build/bench/font_bench
BENCH name=font_raster_mallocs n=20000 value=0 gate=0 guard=1 tag=(G)(M)[AC] TRACK
BENCH name=font_raster_TRACK n=20000 p50=2388 p99=5062 ci95=[2385,2390] gate_p50=0 gate_p99=0 pass=1 power=[AC]
BENCH name=font_raster_correctness failures=0 nonempty=1 guard=1 pass=1
BENCH name=font_fallback_discover n=1 ns=31157462 gate=none fontconfig=1 cjk=/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc emoji=/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf tag=(M)[AC] TRACK
BENCH name=font_raster_cjk_4E2D_TRACK n=20000 p50=2639 p99=3032 ci95=[2638,2640] gate_p50=0 gate_p99=0 pass=1 power=[AC]
BENCH name=font_cache_negative_pressure_TRACK n=8192 p50=129 p99=158 ci95=[129,129] gate_p50=500000 gate_p99=500000 pass=1 power=[AC]
BENCH name=font_cluster_varied_cold_TRACK n=768 p50=1350 p99=3384 ci95=[1331,1370] gate_p50=500000 gate_p99=500000 pass=1 power=[AC]
BENCH name=font_cluster_long_extension_slice_TRACK n=8192 p50=66171 p99=70506 ci95=[66163,66177] gate_p50=500000 gate_p99=500000 pass=1 power=[AC]
BENCH name=font_cluster_cold_TRACK n=3 p50=11548 p99=18814 ci95=[8715,18814] gate_p50=500000 gate_p99=500000 pass=1 power=[AC]
BENCH name=font_cluster_cached_TRACK n=20000 p50=34 p99=37 ci95=[34,34] gate_p50=500000 gate_p99=500000 pass=1 power=[AC]
BENCH name=font_cluster_mallocs n=20000 value=0 gate=0 guard=1 tag=(G)(M)[AC] TRACK
exit=0
```

Additional headless/pure suites inside the broad Makefile display filter also ran serially, using the same sanitizer environment (M)[AC], subprocess exit status:

| Command | Result |
| --- | --- |
| `build/san/tests/editor_close_test --headless` | PASS |
| `build/san/tests/editor_hot_exit_test` | PASS |
| `build/san/tests/editor_scroll_test` | PASS |
| `build/san/tests/editor_large_test` | BLOCKED: IPC initialization |
| `build/san/tests/gl_bench_test` | PASS |
| `build/san/tests/gl_gate_test` | PASS |
| `build/san/tests/gl_scroll_test` | PASS |
| `build/san/tests/gl_test` | PASS |
| `sh tools/test_replay_cli.sh build` | PASS |

The additional `editor_large_test` run passes its navigation checks and then hits the same denied IPC socket. Relevant verbatim diagnostic (M)[AC]; surrounding fixture bounds are inherited from the test:

```text
edit-2ty action=0 Ctrl+End input_submit_ns=389844 (M)[AC]=T4-T0 bound_ns=500000editor_large_test:215: FAIL ipc_server_init(&server, runtime) == IPC_OK
editor_large_test:1147: FAIL mapped_startup_ipc() == 0
```
