# edit-9yd session 9 worker report

Bead: P4.10b, distinguish estimated gutter line numbers until index publication.
Baseline: `f6fbce4`, fresh main runtime on `wt/edit-9yd`. Read `CLAUDE.md`, the
supplied bead, `docs/decisions/edit-457.10b.md`, and `src/editor/STATUS.md`;
also consulted PLAN workflow and the binding performance/index contract.
The bead names no additional review-document finding sections.

## Finding and implementation

Done: unindexed mapped-file gutter digits now use a dim colour derived from the
configured gutter foreground/background. The existing decoration relayout
applies it, with no additional row traversal, layout pass, allocation, index
query, worker job, or process global. Exact colour is restored when both the
line total and viewport top are exact. Caret-only frames retain the prepared
gutter. The existing publication/full-layout mechanism supplies the transition;
`large.c` and `large.h` require no change.

Runtime changes are confined to `src/editor/paint.c` and the new small
`src/editor/theme.h`. Tests are in `tests/editor_large_test.c`. No edits to
`editor.c`, `input.c`, `open.c`, the Makefile, HANDOFF, or worklog; no git mutation
or `bd` invocation.

The test reads `/tmp/edit-corpus/log_1g.txt` and asserts its requested mapped
size, 1 GiB (G). After mapping acquisition, it restarts the index behind a
held bulk job before any large-file frame is submitted. This prevents a fast
worker from defeating the unindexed observation. It checks digit colours in
the null backend's submitted cell dump, for initial open, a custom palette,
an unindexed arrow, and a deep byte jump. After releasing the index, every
digit in the first publication submission has exact style; that submission is
the immediately following frame ID, with no intermediate stale frame. The
submit hook rejects an estimated frame when the index is already complete.
Unfocusing before release suppresses unrelated blink frames. Refocusing checks
caret-only exact-style retention. Normal-buffer typing checks exact-style reuse.

## Pasted red run

Tests were written and run before either runtime file was changed. Recorded
output below is (M)[AC]; diagnostic line numbers are from that initial test
revision, before later guard/unfocus assertions were added.

```sh
DISPLAY=:99 EDIT_DISPLAY=:99 make build/tests/editor_large_test
DISPLAY=:99 EDIT_DISPLAY=:99 timeout 60s build/tests/editor_large_test
```

```text
editor_large_test:140: FAIL digit->fg != e->layout_cfg.gutter_fg
editor_large_test:229: FAIL gutter_estimates() == 0
exit=1
```

Raw evidence: `build/edit-9yd-red.log`; build:
`build/edit-9yd-red-build.log`. This is the requested failure of a distinct-style
assertion on an unindexed corpus open against main runtime.

## Pasted green run

Recorded release and final targeted ASan/UBSan output are both (M)[AC].

```sh
DISPLAY=:99 EDIT_DISPLAY=:99 make all
DISPLAY=:99 EDIT_DISPLAY=:99 timeout 60s build/tests/editor_large_test
DISPLAY=:99 EDIT_DISPLAY=:99 ASAN_OPTIONS=detect_leaks=0 make build/san/tests/editor_large_test
DISPLAY=:99 EDIT_DISPLAY=:99 ASAN_OPTIONS=detect_leaks=0 timeout 60s build/san/tests/editor_large_test
```

```text
edit-9yd: unindexed 1 GiB gutter is dim; first publication frame is exact; theme and allocation checks passed
editor_large_test: ok
exit=0
```

Evidence: `build/edit-9yd-green-final.log`, `build/edit-9yd-san-final.log`.
The release test's counting allocator is active; ASan owns malloc and its
counting hook is inert. Release allocation assertions, including the unindexed
arrow and typing cases, observe zero allocations (M)[AC] against zero (G).

## Acceptance verification

- Final `make all` exited zero (M)[AC], gcc 13 release, all requested warning
  flags including `-Werror`, `-Wshadow`, and `-Wconversion`. Evidence:
  `build/edit-9yd-all-final.log`.
- Full `make check` with clang 18 ASan/UBSan and
  `ASAN_OPTIONS=detect_leaks=0` exited zero: 60 test binaries and replay CLI
  passed (M)[AC]. The final targeted sanitizer gutter run also covers the
  subsequent test-only unfocus assertion. Evidence:
  `build/edit-9yd-check-display.log`, `build/edit-9yd-san-final.log`.

  ```text
  check: 60 test binaries passed
  == tools/test_replay_cli.sh
  ok:   --speed=inf rc=2 replay: --speed must be a finite number > 0
  ok:   --speed=nan rc=2 replay: --speed must be a finite number > 0
  ok:   --speed=0 rc=2 replay: --speed must be a finite number > 0
  ok:   --speed=-1 rc=2 replay: --speed must be a finite number > 0
  ok:   --speed=2 rc=0
  test_replay_cli: all passed
  exit=0
  ```
- The first sandbox-only check failed to connect to Xvfb in `cli_test`.
  The authorized rerun has access to the X11 socket and uses only `:99`.
  That environment failure is recorded in `build/edit-9yd-check.log`.
- Release `build/tests/editor_test` exited zero (M)[AC]. Its active guards
  report zero mallocs (M)[AC] over 10,000 keys (M)[AC] for null, raster, and
  EGL-requested/raster-fallback selections. Evidence:
  `build/edit-9yd-editor-release.log`.
- Editor fuzz exited zero after 23,705 runs in 61 seconds (M)[AC], against
  60 seconds (G):

  ```sh
  DISPLAY=:99 EDIT_DISPLAY=:99 ASAN_OPTIONS=detect_leaks=0 build/fuzz/editor_fuzz -max_total_time=60 -max_len=1024 -artifact_prefix=build/
  ```

  ```text
  Done 23705 runs in 61 second(s)
  exit=0
  ```

  BAT0 was `Not charging`, load1 4.49 (M)[AC] at start. Evidence:
  `build/edit-9yd-fuzz.log`, `build/edit-9yd-fuzz-power.log`.
- `git diff --check` passed. No new globals were added.

## Loaded timing comparison and limits

Existing editor typing rows were run as TRACK, using a generated fixture under
`build/`, with original-main paint linked as an override object versus current
paint. The complete null/raster driver exceeded its 90-second per-variant
execution limit (G) in raster for both variants; no raster result is claimed.
No raster tuning was attempted.

To compare the affected scalar paint choice without waiting for those native
rows, a build-only wrapper calls the existing `typing_row` for null only. Both
variants ran back to back, each with 10,000 keys (M)[AC], zero allocations
(M)[AC], and structural OK. Raw outputs:
`build/edit-9yd-null-main.log`, `build/edit-9yd-null-final.log`.

| Variant | Injection-to-T4 p50 / p99, ms (M)[AC] | Load1 (M)[AC] | Initial index state |
|---|---:|---:|---|
| Main paint | 1.904 / 6.199 | 3.03 | published |
| Current paint | 2.689 / 3.293 | 5.60 | progressing |

The load and background index state differ; these numbers cannot establish a
latency regression, improvement, or gate verdict. The zero-allocation and
submitted-cell assertions are the relevant structural evidence. No new timing
gate pass is claimed on this shared box.

## Decisions, missing work, and scope

Design choices are recorded in
[docs/decisions/edit-9yd.md](../decisions/edit-9yd.md): theme-derived dimming,
preserved geometry, existing relayout/publication ownership, and deterministic
test indexing/blink control.

All requested local correctness/build/fuzz checks are complete. LeakSanitizer
cannot run in this sandbox: leaks are disabled as instructed, and the
coordinator must rerun with leaks on. Native raster timing remains unmeasured
after the execution limits; it is not fixed within this bead. No additional
runtime finding was repaired outside bead scope.
