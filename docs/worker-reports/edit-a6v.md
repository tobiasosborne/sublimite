# edit-a6v worker report

Fixed the headless reproduction of the assigned initial-surface defect. No bd
or git commands were run, and all work stayed in this worktree. Native screenshot
acceptance and the next Astra comparison remain coordinator work.

## What changed

- `src/editor/open.c`: after session recovery, explicit launch width/height
  override historical journal geometry on their respective axes. An equivalent
  queued resize is discarded before the first paint.
- `tests/window_size_test.c`: null backend with the CPU frame-capacity contract;
  asserts initial grid, first/all startup submissions, backend dimensions and
  surface reservations for a larger configured window. Repeats with a legacy
  session journal, checks recovered document bytes, then exercises native-event
  resize and the release counting allocator.
- `src/editor/STATUS.md`, `docs/decisions/edit-a6v.md`, `HANDOFF.md`,
  `docs/worklog/2026-10-11.md`, and this report: scope, decision and validation.

The configured dimensions already reach grid construction, native creation and
raster pixmap/XShm reservation. The failing path was recovery queuing the old
JOURNAL_WINDOW pixels as a renderer resize without resizing the native window.
It produced correct initial dimensions followed by smaller submitted frames.
No x11, raster or backend implementation change was needed.

This is startup-only code: no typing-path allocation, additional frame wait,
font job, native round trip or new timing budget. Existing ConfigureNotify
handling remains in use. Callers without explicit launch pixel dimensions retain
the existing recovery behavior.

Fixture derivation (P/E): the supplied panel is `2880x1800` at `243` DPI;
m3g's policy gives `floor(2880*.60)=1728`, `floor(1800*.70)=1260` pixels.
Expected columns/rows are `floor(window_width/cell_w)` and
`floor(window_height/cell_h)`, using the actual atlas. Legacy pixels derive from
the existing default columns/rows times their baked cell dimensions. Resize uses
the supplied full work area, reserved at INIT. The allocation assertion uses
law 2's zero allocation requirement (G). Endpoint identifiers, dates, bead names,
source locations and protocol/error tags below are identifiers rather than
invented quantitative bounds.

## Red run

The failing test was written and run before production changes. BAT0 reported
`Charging`, so the run is (M)[AC]. Output line numbers derive from `__LINE__` in
that test revision; the assertion macro returns failure on a mismatch.

```sh
make build/san/tests/window_size_test
ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 build/san/tests/window_size_test
```

```text
window_size_test:53: FAIL capture.submitted && !capture.mismatch
window_size_test:86: FAIL sized_window(path) == 0
window_size_test: configured initial grid, submitted frame and resize passed
```

The fresh launch passed; recovered legacy geometry shrank subsequent frames.
Exact [red output](../../build/edit-a6v/red.log) and
[red build](../../build/edit-a6v/red-build.log).

During the green iteration, the initial-size assertions passed but an assertion
requiring every transitional resize frame to have the new size failed. The
existing resize path can retire a queued old-size frame before applying resize.
The final test checks the settled resized frame/backend and still requires every
startup submission to match the configured window.

## Green run

```sh
make build/san/tests/window_size_test
ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 build/san/tests/window_size_test
build/tests/window_size_test
```

Sanitizer and release output:

```text
window_size_test: configured initial grid, submitted frame and resize passed
window_size_test: recovered legacy geometry preserves configured surface and submitted frame size
```

Both returned success, as defined by the test's return value (M)[AC]. BAT0 was
`Charging` for the focused sanitizer run and `Full` for the logged release run.
The release run has the active counting allocator; ASan owns malloc in the
sanitizer run. Exact [sanitizer output](../../build/edit-a6v/green.log),
[release output](../../build/edit-a6v/green-release.log), and
[green build](../../build/edit-a6v/green-build.log).

## Required validation

`make all` passed with GCC release and the tree's C11 warning flags, including
`-Wall -Wextra -Werror -Wshadow -Wconversion`:
[build log](../../build/edit-a6v/make-all.log).

`make check` was run with clang ASan/UBSan and stopped at CLI listener setup:

```text
== build/san/tests/cli_test
cli_test:215: FAIL ipc_server_init_display(&decoy, NULL, getenv("DISPLAY")) == IPC_OK
cli_test: §36 forced startup timeout correctly failed
cli_test:215: FAIL ipc_server_init_display(&decoy, NULL, getenv("DISPLAY")) == IPC_OK
make: *** [Makefile:112: check] Error 1
```

Source locations and the error status above are the recorded compiler/test/make
output (M)[AC], with BAT0 `Full` at invocation. The test's forced-timeout diagnostic
is expected; listener initialization is the actual failure. Full
[check log](../../build/edit-a6v/make-check.log).

The invocation used `DISPLAY=:99 EDIT_DISPLAY=:99 EDIT_DISPLAY_LOCK_TIMEOUT=0`
(the coordinator's supplied safe endpoint; zero means immediate refusal if its
lock is occupied), `ASAN_OPTIONS=detect_leaks=0` (disabled leak checking), and
`UBSAN_OPTIONS=halt_on_error=1` (stop at a sanitizer finding). The coordinator must
rerun with leak checking enabled. Direct probes returned EPERM both for an X
connection and a Unix listener inside the worktree:
[socket evidence](../../build/edit-a6v/socket-probes.log).

Every sanitizer binary was additionally attempted directly, serially on the
safe endpoint, continuing after failures so later headless suites were reached.
The [manifest](../../build/edit-a6v/suites.tsv) records each program's exit value;
per-suite output is under `build/edit-a6v/suites/`. BAT0 was `Not charging`
at the start, hence (M)[AC]. The runner's success does not imply all suites passed.

| Outcome | Suites |
| --- | --- |
| Display setup failed | `editor_close_test`, `file_kill_test`, `raster_test`, `refproto_test`, `refwin_test`, `refwin_timeout_test`, `x11_clip_review_test`, `x11_clip_test`, `x11_font_size_test`, `x11_identity_test`, `x11_live_test`, `x11_order_test`, `x11_stall_test` |
| Unix listener denied | `cli_test`, `editor_large_test`, `editor_p4_test`, `ipc_test` |
| Mixed suite failed | `editor_test`: native open failures and the independent reload/launcher-wait failure described below |
| Pure GL checks passed; native coverage skipped | `gl_test`; native X/EGL prerequisites were unavailable. `gl_gate_test` and `gl_bench_test` passed their pure contracts. |
| Remaining binaries passed | Every other suite in the manifest, including `window_size_test`, `font_size_test`, `editor_session_test`, `startup_test`, font, render, layout, view, line index, work, and the pure x11 alignment/input/XI checks |

This is **not a full headless acceptance pass**: the non-display IPC suite is
blocked by EPERM, and the mixed editor suite has an unrelated headless failure.
There were no sanitizer findings in the successful headless suites. The failed
x11 order fixture did produce a sanitizer crash after denied native setup; see
outside scope below.

The remaining shell contracts from `make check` also passed independently:
[replay CLI](../../build/edit-a6v/replay-cli.log) and
[display-lock contract](../../build/edit-a6v/display-lock.log). Expected deliberate
timeout diagnostics in the latter are part of its assertion, not suite failures.

The existing release typing allocation regression passed:

```sh
DISPLAY=:99 EDIT_DISPLAY=:99 EDITOR_ALLOC_ONLY=null build/tests/editor_test
```

```text
editor_test: null 10000 keys mallocs=0 guard=active
```

The key count derives from `allocation_test`'s existing workload loop; the
allocation count is the counting guard's sum, required to be zero by law 2 (G).
This run is (M)[AC], BAT0 `Not charging`:
[allocation output](../../build/edit-a6v/allocation-release.log).

The editor benchmark was attempted on the same release build:

```sh
DISPLAY=:99 EDIT_DISPLAY=:99 build/bench/editor_bench --track --no-idle --no-p4 --serial-only --file=tests/window_size_test.c
```

Its null serial workload completed with structural checks and the active
allocation guard passing; the following raster row failed at native editor open.
No native timing or Sublime frame-parity result is claimed. The workload comes
from the existing benchmark defaults, and the input is the checked-in regression
source rather than a newly invented corpus/budget. Power/load are stamped by
the benchmark and recorded with its measured distributions in
[benchmark output](../../build/edit-a6v/editor-bench.log).

## What remains / coordinator acceptance

Re-run `make check` with leak checking enabled outside the sandbox on Xvfb `:99`
(the supplied coordinator endpoint), including the display/IPC suites above.
The full check remains unpassed here. Rerun `make all` after integration.

On Xvfb `:98` (the bead's screenshot endpoint), use the supplied panel geometry
`2880x1800` and `243` DPI (P, supplied inputs). Verify both a fresh session and
startup with a retained legacy session journal. Screenshot the first painted
frame and the settled window at the derived `1728x1260` launch size (P/E,
`floor(2880*.60)` by `floor(1800*.70)`). Text/chrome/minimap must occupy the
full cell-aligned extent, with only fractional-cell margins smaller than their
cell dimensions; the old large black remainder must be gone. Then resize and
capture again to verify ConfigureNotify updates the grid and submitted surface.
The headless test cannot certify actual XShm/pixmap upload or native paint.

Run the next Astra vs Sublime drive on the integrated build and inspect startup,
wrapping, resize and typing. This worker does not infer frame parity from its
functional/benchmark checks or hand the build to Tobias before that drive.

## Outside scope, reported without fixes

- `editor_test` reports `reload launcher result=4` and fails the child-success
  assertion in `review_reload_identity`. Here `4` derives from the public
  `IPC_IO` enum value, and its location is recorded by the assertion's
  `__LINE__` (M)[AC]. This uses the isolated server/socketpair path, so listener
  denial does not establish its cause. It was also reported in
  [edit-2ts](edit-2ts.md). This fixture has no journal configured and does not
  enter the changed startup branch.
- `x11_order_test` continues after failed native fixture setup and calls
  `plat_map` with an unavailable connection. ASan reports a null-address read
  in `xcb_send_request_with_fds64` via `xcb_map_window` and
  `test_startup_trace`. Exact [output](../../build/edit-a6v/suites/x11_order_test.log).
  No x11 test-harness repair was made.
- Legacy callers without explicit launch pixel dimensions still honor journal
  geometry through the existing renderer-only recovery resize. Native window
  restoration for those callers was not changed; main supplies both dimensions.

The implementation is complete for the headless reproduction; native acceptance
remains unverified. This worker stops after edit-a6v.
