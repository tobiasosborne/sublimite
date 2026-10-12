# edit-xzw worker report

Reproduced the original assertion headlessly. It observes exact index
publication while viewport relabeling is still pending on a foreground
resident window. Analysis and a loaded green reproduction show automatic
recovery without scroll input. Production code is unchanged.

Files touched:

- `tests/editor_large_test.c`: deterministic between-step index completion
  regression, headless fixture selector, pending-state diagnostic, settle
  tolerance for the original race, and quiescent trace dump on unresolved
  relabeling.
- `docs/decisions/edit-xzw.md`: ordering derivation and test choices.
- `docs/worker-reports/edit-xzw-s6.md`: this report.

No `bd` or `git` commands were run. Work stayed in this worktree; temporary
run logs were stored under `/tmp`. Protected coordinator documents were
not edited.

## Red

Built `build/san/tests/editor_large_test` with the Makefile sanitizer flags
and ran the selected existing null-backend fixture while looping the
release `scroll_bench --track --probe`. This reproduced the reported
assertion before changing its completion tolerance:

```text
ASAN_OPTIONS=detect_leaks=0 DISPLAY=:99 EDIT_XZW_REPRO=1 build/san/tests/editor_large_test
edit-xzw completion: estimated=1 pending=1 resident=1 exact_extent=1 approximate=1 view_busy=0 full_pending=0
editor_large_test:1204: FAIL !s.top_estimated
edit-dnc G7: mid-frame index arrival publishes exact gutter in the same frame; zero typing allocations
```

Then added the deterministic test before implementing the tolerance. It
holds the bulk index build, primes a resident byte seek, and polls the index
to completion between editor steps. Its immediate-label assertion fails:

```text
make build/san/tests/editor_large_test
ASAN_OPTIONS=detect_leaks=0 DISPLAY=:99 EDIT_XZW_REPRO=1 build/san/tests/editor_large_test
editor_large_test:341: FAIL !editor_large_status_get(e).top_estimated
editor_large_test:1210: FAIL index_completes_between_steps(path) == 0
```

The reported source locations and Boolean flags are captured output
(M)[AC], not new test counts or bounds. The display identifier is the
brief's Xvfb endpoint. `detect_leaks=0` disables LeakSanitizer only (P:
ASan option semantics); ASan and UBSan remain enabled. Default LeakSanitizer
cannot run in this sandbox and reports its ptrace restriction.

## Green

The deterministic test now takes the next editor turn and asserts the exact
top, retained byte anchor, independent newline-count ordinal, actual exact
gutter submission and no guarded allocations. The original fixture waits
for its existing settle milestone when job completion precedes relabeling.

```text
make build/san/tests/editor_large_test build/tests/editor_large_test
ASAN_OPTIONS=detect_leaks=0 DISPLAY=:99 EDIT_XZW_REPRO=1 build/san/tests/editor_large_test
edit-xzw: index completed between steps; exact gutter retains byte anchor
edit-dnc G7: mid-frame index arrival publishes exact gutter in the same frame; zero typing allocations
editor_large_test: ok

DISPLAY=:99 EDIT_XZW_REPRO=1 build/tests/editor_large_test
edit-xzw: index completed between steps; exact gutter retains byte anchor
edit-dnc G7: mid-frame index arrival publishes exact gutter in the same frame; zero typing allocations
editor_large_test: ok
```

The release run exercises the real malloc guard; sanitizer runs identify
that guard as inert because ASan owns allocation interception.

Loaded sanitizer recovery, with the release scroll bench running in a
background loop, captured the same pending state and finished green:

```text
Not charging
23.04 10.39 11.26 40/2236 172
edit-xzw completion: estimated=1 pending=1 resident=1 exact_extent=1 approximate=1 view_busy=0 full_pending=0
edit-xzw: index completed between steps; exact gutter retains byte anchor
edit-dnc G7: mid-frame index arrival publishes exact gutter in the same frame; zero typing allocations
editor_large_test: ok
```

All load/process values above are M[AC] from `/proc/loadavg`; BAT0 was read
immediately before printing that sample. AC follows the repository's
`Not charging` convention. Boolean values are M[AC] state snapshots.

The brief suggested an extra step for a benign delay. The ready-window
deterministic test verifies that behavior. In the original cold-window
fixture, an extra nonblocking step and an extra step using the existing
blocking-pump timeout both still failed under load while the resident
completion was pending. The final test therefore uses the existing settle
watchdog, with no new scheduling count or duration. A failure after that
watchdog writes `build/edit-xzw-stale.trace` after quiescing workers.
An intermediate failed extra-step experiment produced that trace locally.

The exact count transition forces the resolver out of its fast path;
`SCROLL_MORE` retains `scroll_pending`; worker completion resumes it; and
`submit` rejects frames while `editor_scroll_waiting` is true (P: source
control flow). This is delayed adoption, not a permanently stale label.
See the decision document for the full derivation.

## Validation and remaining work

`make all` passes using the specified GCC release toolchain and strict
Makefile warnings. The final verification reports:

```text
make all
make: Nothing to be done for 'all'.
```

The targeted sanitizer suites pass headlessly with LeakSanitizer disabled:

```text
editor_scroll_test: null fractional/core wheel, overscan, immutable refresh/repeat/missing/stale correlation PASS
editor_scroll_test: held worker cancellation retains snapshot, typing submits, allocations=0 guard=sanitizer-inert PASS
editor_scroll_test: fractional resize rebuilds clip/overscan and preserves origin PASS
editor_scroll_test: wrap-off EOF typing submits while bulk worker is held PASS
scroll_test: PASS
```

The allocation value in this captured output is M[AC], from the existing
suite instrumentation; its sanitizer caveat is retained.

Full `editor_large_test` reaches `mapped_startup_ipc` before the selected
fixture and fails on sandbox-denied socket creation. The selector validates
the bead's deterministic test and the original failing fixture, not every
case in that suite. Coordinator must rerun the default complete suite.

Plain `make check` builds the sanitizer binaries but stops at `base_test`
because LeakSanitizer cannot inspect threads under this sandbox's ptrace
restriction. With LeakSanitizer disabled, the driver stops at the shared
display lock for `cli_test`. An immediate-lock attempt avoids waiting on
the coordinator's display lane. A direct `x11_live_test` confirms
`private display connection failed`: display suites cannot reach the X
socket here. Coordinator reruns display suites on the brief's Xvfb `:99`.
Pure `gl_test` checks pass while native X11/EGL cases explicitly skip.

An independent serial sweep of the Makefile's non-display binary set was
run so its fail-fast driver would not hide later suites. Results and
outside-scope findings are recorded below. Full `make check` is not claimed
green in this sandbox.

`tools/test_replay_cli.sh build` also passes with LeakSanitizer disabled.

The non-display sweep passed the following binaries (also retained as raw
per-binary output and exit status in `build/edit-xzw-validation/`):

```text
base_test config_editor_test config_test config_worker_test dataloss_test
file_test find_bench_test find_bestof_test find_p1r_test
find_simd_filter_verify_test find_test find_timeout_test
font_selection_test font_size_test font_test harness_test
hl_parse_test hl_test hl_worker_test indent_test journal_test keys_test
layout_test lineidx_test minimap_test piece_bounds_test piece_mem_test
piece_mt_test piece_test prewake_test raster_scroll_test render_test
replay_test runtime_test savectl_reload_test savectl_test scan_test
scroll_display_test scroll_test startup_test tabs_test trace_pairs_test
trace_test undo_test unicode_render_test utf8_tables_test utf8_test
view_test wayland_bench_test wayland_gl_test wayland_test window_size_test
work_foreground_test work_test wrap_test
```

`findui_test` failed its existing worker-completion assertion during the
loaded sweep and passed a serial retry unchanged. The remaining failures
are outside this bead's scope and were not fixed:

- `file_kill_test` fails `file_test_viewport` initialization. That fixture
  calls its real-display view initializer despite belonging to the
  Makefile's nominal non-display set; it needs the unavailable X socket.
- `ipc_test` reports `Operation not permitted` initializing its Unix
  socket. This agrees with the default large-suite IPC failure.
- `journal_kill_test` fails both the sweep and serial retry, with the same
  first-trial recovery state. Root cause was not investigated in this
  editor bead. The ordinary `journal_test` passes.

Captured retry output (M)[AC]; source locations, flag values and return
codes are observed test output, not new counts or scheduling limits:

```text
findui_test: all passed
EXIT 0
== journal_kill_test
Not charging
14.08 15.25 13.83 10/2231 72
journal_kill_test: FAIL trial=0 target=1 issued=1 page_cache=1 ack=0 replay=0 rc=6 error=0
EXIT 1
```

The exit statuses have P: process-status semantics; the load/process
sample is M[AC] read before the retry. The recovery counters and error
code are M[AC] instrumentation output from the existing fixture.

The release `scroll_bench --track --probe` completes successfully. Its
unindexed owned seek took **343721405 ns (M)[AC]**: monotonic time after
viewport resolution and null-frame production minus time before the seek,
as calculated in `bench/scroll_bench.c`. The work proxy's reported p50,
p99 and maximum were **62460 / 139038 / 220961 ns (M)[AC]**, derived by
the existing benchmark from elapsed work samples; load1 **15.89 (M)[AC]**
was its `/proc/loadavg` stamp. These are tracked measurements on the
shared box, not new budgets. Physical display cadence remains unavailable.

Remaining coordinator work: rerun the default complete editor-large
suite and display suites on Xvfb `:99`, run the standard check with a
working LeakSanitizer environment, and triage the unchanged journal
recovery failure and non-display classification/socket restrictions.
The bead's implementation and red/green evidence are complete; no module
STATUS document is needed because the work stayed within this bead.
