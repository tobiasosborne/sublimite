# edit-dz0 worker report (S7)

Bead: typing into a 1 GiB mapped file before the line index completes took
0.8 to 2.1 s per key because the view's edit repair counted LFs from byte 0.
Fixed: a key in a lazy file with an inexact count now repairs byte-relatively;
line_query counts 0 bytes for that key (was 1,073,741,825), settle 0.670 ms (M)
release (was 910.662 ms (M)), same box, same session, bulk lane held.
Design and derivations: docs/decisions/edit-dz0.md.

## Files touched

- `src/view/view.h`, `src/view/view.c`: `view_relative_restore` (new API);
  `normalize` takes a `relative` flag; `P_NORMAL_SETUP` relative branch (walk
  back to the top's LF, keep the host label, mark approximate); probe counter
  `line_scanned` (cumulative bytes line_query read; one add per 256-byte read,
  no branch).
- `src/editor/input.c`: `relative_repair(e)`; `repair()` and `replay_follow()`
  route to `view_relative_restore` for lazy, inexact, unwrapped buffers.
- `src/editor/editor.h`: derivation comment for `EDITOR_IDLE_NS` (value unchanged).
- `tests/editor_large_test.c`: `dz0_key_at_end(wrap)` (red/green case, both
  unwrapped and wrapped; in the default run and via `EDIT_DZ0_REPRO=1
  [EDIT_DZ0_WRAP=1]`).
- `tests/view_test.c`: `relative_restore` headless case (`view_test dz0`).
- `fuzz/view_fuzz.c`: `relative_restore_probe`.
- `docs/decisions/edit-dz0.md`, this report.

## Red run (pre-fix routing, final test)

The view change is additive; the red run used the final test binary with the
editor routing disabled by a temporary `DZ0_OLD` switch (removed afterwards),
which executes exactly the pre-fix calls. The first red run, before the test's
tail warm-up existed, showed the same: line_query_bytes=1073741825,
key_settle=919.694 ms (M), load1 3.28.

```
$ df -h /
/dev/nvme0n1p5  480G  359G   97G  79% /
BAT0=Not charging loadavg=8.66 8.04 4.83 2/2557 951679
$ DZ0_OLD=1 EDIT_DZ0_REPRO=1 build/tests/editor_large_test   # pre-fix routing
editor_large_test: edit-dz0 key at EOF before index size=1073741824 line_query_bytes=1073741825 bound=4096 (P: VIEW_SCAN_BOUND) key_settle=910.662 ms (M) overdue=0 power="Not charging" load1=8.66
editor_large_test:1695: FAIL r.line_bytes <= VIEW_SCAN_BOUND
exit=1
$ df -h /
/dev/nvme0n1p5  480G  359G   97G  79% /
```

## Green run

```
$ df -h /
/dev/nvme0n1p5  480G  359G   97G  79% /
BAT0=Not charging loadavg=7.88 7.89 4.82 2/2564 952052
$ EDIT_DZ0_REPRO=1 build/tests/editor_large_test
editor_large_test: edit-dz0 key at EOF before index size=1073741824 line_query_bytes=0 bound=4096 (P: VIEW_SCAN_BOUND) key_settle=0.670 ms (M) overdue=0 power="Not charging" load1=7.88
exit=0
$ df -h /
/dev/nvme0n1p5  480G  359G   97G  79% /
```

Bound: line_query bytes <= VIEW_SCAN_BOUND = 4096 (P: one view call's charged
scan work). Unwrapped green counts 0. The wrapped variant (added afterwards
as a second case) keeps the exact repair and counts 3840 (M) = one call's
line_query budget (derivation in the decision), then the editor's seed takes
over; wrapped settle 0.076 to 0.380 ms (M) across the runs.

Brief's repro after the fix (40 opens, sparse 1 GiB, no journal):
`EDIT_MMP_REPRO=1 EDIT_MMP_MIB=1024 build/tests/editor_large_test` ->
`runs=40 failed=0 overdue=0 max_key_settle=3.930 ms (M) mode=mmap`, load1 9.23,
exit 0 (edit-mmp recorded 0.8 to 2.1 s per key). df 96G free before, 97G after.

## Typing numbers, release editor_test (tracked, law 1)

Before (load1 11.02) / after (load1 7.53 to 10.31; box shared):
- 10000-key cases (gl->cpu-raster, raster, null, null hl-on, raster): mallocs=0 guard=active, before and after.
- edit-g11 null typing: keys=95 frames=97 mallocs=0, fill_max_ns 14432 -> 8403 (M)[AC].
- review 21: mutation-to-submit=0 mallocs=0, before and after.
- review 11 indivisible submit (undo burst, not the typing path): 2.06 ms -> 3.01 ms (M)[AC], load-dependent; this path executes identical calls (non-lazy buffer).
- `build/tests/editor_test` exit 0 before and after.

## Commands and exit codes

| Command | Exit |
|---|---|
| `make -j4 all` (gcc release, -Werror) | 0 |
| `EDIT_DZ0_REPRO=1 build/tests/editor_large_test` (red, pre-fix routing) | 1 |
| `EDIT_DZ0_REPRO=1 build/tests/editor_large_test` (green) | 0 |
| `EDIT_DZ0_WRAP=1 EDIT_DZ0_REPRO=1 build/tests/editor_large_test` | 0 |
| `build/tests/editor_large_test` (release, full) | 0 |
| `EDIT_MMP_REPRO=1 EDIT_MMP_MIB=1024 build/tests/editor_large_test` | 0 |
| `build/tests/editor_test` (release; malloc guard) | 0 |
| `build/tests/view_test` (release) | 0 |
| `ASAN_OPTIONS=detect_leaks=1 build/san/tests/view_test` | 0 |
| `ASAN_OPTIONS=detect_leaks=1 build/san/tests/lineidx_test` | 0 |
| `ASAN_OPTIONS=detect_leaks=1 build/san/tests/scroll_test` | 0 |
| `XDG_RUNTIME_DIR=$(mktemp -d) DISPLAY=:99 ASAN_OPTIONS=detect_leaks=1 sh tools/with_display_lock.sh build/san/tests/editor_large_test` | 0 |
| same lane, `build/san/tests/editor_test` | 0 |
| same lane, `build/san/tests/editor_scroll_test` | 1 once at load1 ~10, then 0 and 0 (see findings) |
| `build/fuzz/view_fuzz -max_total_time=60 -max_len=4096 -timeout=10 -print_final_stats=1` (edit-fff's command) | 0 (7792 runs, no crash) |

Display-lane suites ran on Xvfb :99 here (the display guard found it); the
coordinator may still want to re-run them under its own lock. The full
`make check` was not run; the brief's six suites were.

Every GB fixture run had `df -h /` before and after (above and in the scratch
logs); `build/editor-large-dz0-*` was absent after every run. Other sessions
moved the disk from 99G to 90G free during this bead; this bead's fixtures are
sparse and removed.

## What remains

Nothing in scope. The relative walks still cost the length of the top's and
the cursor's own lines (a single 1 GiB line would be walked in cooperative
slices), as edit-jf9 records for relative motion.

## Found, not fixed (out of scope)

1. **Cold follow window waits on the bulk lane after an edit at EOF.** After
   the key, scroll's resident follow asks for the window [EOF-128 KiB,
   EOF-64 KiB) (seen with a temporary trace). `window_class` (src/scroll/resident.c)
   sends a window that is not in core to WORK_BULK, behind index/find/warm
   continuations; with the lane held (tests) it never settles. The pre-fix
   repair hid this by scanning (faulting in) the whole file. In real use the
   bulk jobs yield, so this is added latency, not a hang, and only when those
   pages are cold. The dz0 test warms the fixture's tail to stay deterministic.
2. **editor_scroll_test flake under load.** `build/san/tests/editor_scroll_test`
   failed once at line 117 (`cancellation`: typing while a resident window is
   held, an in-memory non-lazy buffer, so not this bead's path) at load1 about
   10; it passed twice on rerun at load1 2 to 3. Its `settle` is bounded by a
   step count (PROBE_RETRIES), not time.
3. **edit-mmp's power stamp** prints `power=Not` (`fscanf("%15s")` stops at the
   space of "Not charging"); the dz0 print uses fgets. Cosmetic.
4. Not touched, per the brief: paint.c `hi - lo` clamp (edit-2ea), edit-qct, edit-f3i.
