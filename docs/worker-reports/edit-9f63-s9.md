# edit-9f63 — s9 worker report

Geometry fix implemented. Verification results and unresolved acceptance items are recorded below. Work is confined to this bead and this worktree.

## Change and files touched

- `src/editor/input.c`: copy the successful incremental layout's usable text
  columns into the view before cursor repair/follow. This keeps the view out
  of the minimap when a decimal gutter widens while typing. The copy adds no
  allocation, source scan, or error-policy change; existing cursor follow
  performs the required horizontal scroll.
- `tests/editor_test.c`: add `--9f63-columns` and default-suite coverage for
  long lines, fractional window widths, gutter growth, soft-wrap width,
  right-edge cursor visibility, and input-to-submit allocation counting.
- `docs/decisions/edit-9f63.md`: record the geometry derivation and ownership.
- `docs/worker-reports/edit-9f63-s9.md`: this report.

The renderer's minimap boundary and layout's integer column calculation
already agree. The reproducible defect is the stale copy in `view.config.cols`
after `layout_edit` changes the gutter. The regression distinguishes the
counted view cell from the correctly bounded rendered grid.

## Red run

The test was written and failed against the unchanged production code before
implementation. Release target build: `make -j3 build/tests/editor_test`,
exit `0` (M)[AC]. Build parallelism and the display-lock timeout below come from
the supplied brief. Temporary runtime directories are removed on exit.

```sh
XDG_RUNTIME_DIR=$(mktemp -d) DISPLAY=:99 EDIT_DISPLAY_LOCK_TIMEOUT=1800 \
  sh tools/with_display_lock.sh build/tests/editor_test --9f63-columns
```

Captured output (M)[AC]; geometry is computed (P) from the fixture and atlas:

```text
Charging
 02:20:56 up 46 min,  1 user,  load average: 36.24, 21.86, 10.27
waiting for display :99 (held by unknown)
editor_test:389: FAIL fits
edit-9f63 (P): wrap=-1 remainder=1 cell_w=8 view_right=264 minimap_left=256
exit_code=1
```

Derivation (P): the reused fixture has `40` surface columns, and the default
baked atlas has `8` pixels per cell. The chosen window width is
`40 * 8 + 1 = 321` pixels. The existing minimap occupies `8 * 8 = 64` pixels,
so its left edge is `floor((321 - 64) / 8) * 8 = 256`. Inserting LF changes
nine logical lines to ten, widening the gutter from two cells to three.
Layout reduces its text count from thirty to twenty-nine; the stale view
still counts thirty, ending at `(3 + 30) * 8 = 264`, inside the minimap.
Fixture choices are (E) reuse of the existing minimap fixture and default
font; the decimal transition and subsequent arithmetic are (P).

## Green run and verification

The focused release regression passed. This null-backend selector ran directly
because the native display was unreachable inside the sandbox and its shared lock was occupied.
The final test derives screen rows from prepared byte starts: cursor follow
can move the split line to the top while preserving horizontal geometry.

```sh
DISPLAY=:99 build/tests/editor_test --9f63-columns
```

Captured output (M)[AC]; coordinate arithmetic remains (P), as derived above:

```text
Charging
 02:39:52 up  1:05,  1 user,  load average: 24.15, 27.73, 24.79
edit-9f63 (P): wrap=-1 remainder=1 cell_w=8 view_right=256 minimap_left=256
edit-9f63 (P): wrap=-1 remainder=2 cell_w=8 view_right=256 minimap_left=256
edit-9f63 (P): wrap=-1 remainder=3 cell_w=8 view_right=256 minimap_left=256
edit-9f63 (P): wrap=-1 remainder=4 cell_w=8 view_right=256 minimap_left=256
edit-9f63 (P): wrap=-1 remainder=5 cell_w=8 view_right=256 minimap_left=256
edit-9f63 (P): wrap=-1 remainder=6 cell_w=8 view_right=256 minimap_left=256
edit-9f63 (P): wrap=-1 remainder=7 cell_w=8 view_right=256 minimap_left=256
edit-9f63 (P): wrap=-1 remainder=0 cell_w=8 view_right=256 minimap_left=256
edit-9f63 (P): wrap=1 remainder=1 cell_w=8 view_right=256 minimap_left=256
edit-9f63 (P): wrap=1 remainder=2 cell_w=8 view_right=256 minimap_left=256
edit-9f63 (P): wrap=1 remainder=3 cell_w=8 view_right=256 minimap_left=256
edit-9f63 (P): wrap=1 remainder=4 cell_w=8 view_right=256 minimap_left=256
edit-9f63 (P): wrap=1 remainder=5 cell_w=8 view_right=256 minimap_left=256
edit-9f63 (P): wrap=1 remainder=6 cell_w=8 view_right=256 minimap_left=256
edit-9f63 (P): wrap=1 remainder=7 cell_w=8 view_right=256 minimap_left=256
edit-9f63 (P): wrap=1 remainder=0 cell_w=8 view_right=256 minimap_left=256
edit-9f63: whole text cells, view and soft-wrap columns exclude the minimap through gutter growth
exit_code=0
```

## Required checks

Exit statuses below are observed (M)[AC], with zero meaning command success
and one meaning test failure (P: the test harness return-code convention).
Build parallelism and display-lock timeout are supplied by the brief.

| Command | Exit | Result |
| --- | --- | --- |
| `make -j3 build/tests/editor_test build/bench/editor_bench build/san/tests/editor_test build/san/tests/render_test build/san/tests/raster_test build/san/tests/layout_test` | 0 | Final production source linked in release and sanitizer targets. |
| `make -j3 build/tests/editor_test build/san/tests/editor_test` | 0 | Final regression fixture rebuilt. |
| `make -j3 all` | 0 | Required release build, warning flags enforced. |
| `build/tests/editor_test --9f63-columns` | 0 | Green output above. |
| `ASAN_OPTIONS=detect_leaks=1 build/san/tests/editor_test --9f63-columns` | 0 | Green outside the ptrace sandbox. |
| `sh tools/with_display_lock.sh build/san/tests/editor_test` | 0 | Full suite, including native X11 input, passed. |
| `sh tools/with_display_lock.sh build/san/tests/render_test` | 0 | Passed. |
| `sh tools/with_display_lock.sh build/san/tests/layout_test` | 0 | Passed. |
| `sh tools/with_display_lock.sh build/san/tests/raster_test` | 1 | Native resize fixture preparation failure; details below. |
| `EDITOR_ALLOC_ONLY=null build/tests/editor_test` | 0 | Release malloc guard active. |
| `EDITOR_ALLOC_ONLY=raster sh tools/with_display_lock.sh build/tests/editor_test` | 0 | Native release malloc guard active. |

The final locked sanitizer runs used `ASAN_OPTIONS=detect_leaks=1`,
`DISPLAY=:99`, `EDIT_DISPLAY_LOCK_TIMEOUT=1800`, and a private
`XDG_RUNTIME_DIR` removed on exit. They ran outside the sandbox. The outer
lock was retained across these commands; nested wrappers inherited and
validated its descriptor. Raw logs: `build-edit-9f63-native-*.log`.

The initial in-sandbox leak-enabled focused/editor-headless/render/layout/
raster runs each exited `1` (M)[AC]. LeakSanitizer reported:

```text
LeakSanitizer has encountered a fatal error.
HINT: LeakSanitizer does not work under ptrace (strace, gdb, etc)
```

The in-sandbox raster run also failed its `plat_init` assertion because the
display socket was unreachable. The `DISPLAY=:99 xdpyinfo` probe exited `1`
inside the sandbox and `0` outside (M)[AC]. The final checks above
resolve those environment restrictions. During fixture development, the
first cursor assertion assumed a fixed screen row and failed; the final test
locates that row through prepared byte starts after cursor follow.

Release allocation evidence (M)[AC]; the key count is the existing allocator
fixture's loop, and allocation totals are counted between ingress and submit:

```text
Charging
 02:45:10 up  1:10,  1 user,  load average: 19.08, 22.75, 23.57
editor_test: null 10000 keys mallocs=0 guard=active
exit_code=0
Charging
 02:52:52 up  1:18,  1 user,  load average: 21.27, 16.29, 19.52
editor_test: raster 10000 keys mallocs=0 guard=active
exit_code=0
```

## Typing latency

The pre-fix benchmark binary was built before implementation and saved as
`build/editor_bench-edit-9f63-before`. Both versions read the same
`tests/editor_test.c` source and use the existing benchmark's default sample
count. Each completed row reports `10000` keys, no dropped samples, no foreign
input, `structural=OK`, and `allocations=0` (M)[AC]; counts come from the
existing harness, not a new budget. Durations/percentiles below are the
existing reporter's input-to-T4 trace timestamp differences (M)[AC], in ns.

Commands, outside the sandbox with private runtime directories and the shared
`:99` display lock (`EDIT_DISPLAY_LOCK_TIMEOUT=1800`, supplied by the brief):

```sh
sh tools/with_display_lock.sh build/editor_bench-edit-9f63-before --no-idle --no-p4 --serial-only --file=tests/editor_test.c
EDIT_BACKEND=raster sh tools/with_display_lock.sh build/bench/editor_bench --no-idle --no-p4 --serial-only --file=tests/editor_test.c
```

The baseline completed null and native raster rows. Its subsequent GL request
resolved to raster fallback; that duplicate row was interrupted (M)[AC], driver
exit `130`. The owned temporary journal was removed. The current run used the
existing raster filter to collect the matched null/raster rows and exited
`0` (M)[AC]. The reporter still prints timing `MISS`; process success is
not a G1 pass. The GL fallback row is excluded from this comparison.

Before power/load stamp (M)[AC], from the requested status/uptime commands:

```text
Charging
 02:57:08 up  1:22,  1 user,  load average: 29.52, 23.35, 21.63
```

Current power/load stamp (M)[AC]:

```text
Charging
 03:21:30 up  1:46,  1 user,  load average: 18.54, 17.55, 18.51
```

| Backend | Before p50 / p99, ns | Current p50 / p99, ns | Before / current load1 | Verdict |
| --- | --- | --- | --- | --- |
| null | 5405513 / 12508475 | 2751424 / 10420063 | 29.52 / 18.54 | MISS / MISS |
| raster | 33898150 / 78317744 | 57067991 / 118016503 | 29.69 / 22.63 | MISS / MISS |

The current null observations decreased, while current raster observations
increased. The runs had changing shared load and were separated in time;
this comparison does **not establish latency neutrality or G1 compliance**.
No latency limit, sample count or production timing policy was changed.
Raw evidence: `build-edit-9f63-bench-before.log` and
`build-edit-9f63-bench-after.log`.

## What remains and outside scope

The untouched native raster resize fixture failed before its geometry checks
(M)[AC]:

```text
Charging
 02:50:02 up  1:15,  1 user,  load average: 7.74, 14.46, 19.84
raster_test:605: FAIL render_test_prepare(&b,&cfg)==RENDER_OK
exit_code=1
```

The raster suite had passed its miniature pixel region, kernels, conformance,
live pixels and fence checks before this final fixture. Its preparation
failure needs coordinator triage/rerun outside this bead; raster source and
tests were left unchanged. No further review pass or unrelated fix was made.
The no-latency-regression acceptance item remains unverified: both versions
miss G1, and the current raster observations are higher. A controlled typing
latency check is still required; this bead makes no performance-compliance
claim. The real-display trial on Xorg `:0` was not driven.

The new fixture is inline; no GB file fixture was created. No tracking or
version-control command was run, and the protected handoff, worklog, rules,
plan and requirements files were not edited. Build products and raw logs
remain in this worktree.
