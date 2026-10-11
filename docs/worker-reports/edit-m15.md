# edit-m15 worker report

Implemented the supplied gutter, find-panel chord, and approximate-find-follow
fixes. Gutter clicks place the caret at the logical line start, including
wrapped continuation rows; this is the alternative permitted by the brief.
Ctrl+X in the find panel is a consumed no-op, also permitted by the brief.
No `git` or `bd` command was run.

## Red-green evidence

Tests were added and run against the unchanged implementation before the
production fixes. Outcomes below are (M)[AC], derived from process exit status
and the assertions quoted. BAT0 was `Not charging`; AC `online` was true.
Success status is `0`; the editor test assertion macro returns `1` on failure.
The findui assertion abort produced `134` = shell signal convention `128` +
SIGABRT `6`. These are outcomes, not timing budgets.

| Defect / command | Red | Green |
| --- | --- | --- |
| Gutter: `build/tests/editor_test --m15-gutter` | `1`: `selection.cursor==start && selection.anchor==start` failed after inserting a stale old-EOF hit | `0`: filled viewport ignores stale old EOF |
| Widening: `build/tests/editor_test --m15-gutter-growth` | `1`: `selection.cursor==start` failed in the newly claimed gutter column | `0`: decimal widening ignores old text hit |
| Wrapped gutter: `build/tests/editor_test --m15-gutter-wrap` | `1`: `selection.cursor==0` failed on a continuation | `0`: logical line start and gutter table checked |
| Panel: `EDIT_M15_PANEL=1 build/tests/findui_test` | `134`: document length assertion failed after Ctrl+V | `0`: clipboard/query chords isolated from document |
| Follow: `build/tests/editor_test --m15-follow` | `1`: `editor_step(e,0)==EDITOR_OK` failed with anchor before top byte | `0`: step succeeds, error stays clear, hscroll stays unchanged, next seek repairs top |

The supplied baseline already initializes all columns of unwrapped hit rows
in `layout_run`. Thus the natural short-source/viewport-fill and table checks
already passed before the pointer fix. The stale-hit injection then failed,
proving the gutter consumer still depended on those records. The widening
fixture likewise verifies current gutter metadata before injecting an old
text hit. Its logical-line counts derive from the supplied decimal-width
transition (G). The wrapped case failed without injected hits.

The panel test copies the fixture text, collapses the document selection,
opens find and pastes. It verifies query contents, unchanged document bytes,
revision and selection, word movement/deletion, select-all replacement,
Ctrl+X and unsupported-chord consumption, Unicode grapheme/whitespace handling,
replacement-field paste, capacity rejection, Alt toggles and ordinary document
paste after Escape. The follow fixture injects the adopted approximate state
directly rather than depending on index-worker scheduling.

Raw red and targeted green logs are under `/tmp/edit-m15-red-*.log` and
`/tmp/edit-m15-green-*.log` in this worker environment.

## Validation

Passed (M)[AC], success determined by exit status and suite assertions:

- `make -j4 all` and the requested ASan/UBSan test builds. The final combined
  build was `make -j4 all build/san/tests/findui_test` after the broader build;
  the build log is `/tmp/edit-m15-final-build.log`. A final standalone
  `make all` also passed (`/tmp/edit-m15-make-all.log`).
- `build/tests/editor_test --headless` and `build/tests/findui_test`.
- `ASAN_OPTIONS=detect_leaks=0 build/san/tests/editor_test --headless`.
- `ASAN_OPTIONS=detect_leaks=0 build/san/tests/findui_test`.
- The sanitizer `layout_test`, `wrap_test`, and `scroll_test`, with the same
  leak-detection setting.

The default sanitizer editor/findui runs stopped at LeakSanitizer's sandbox
process/thread-inspection error: `LeakSanitizer does not work under ptrace`.
Disabling only leak detection allowed ASan and UBSan to complete. Native
display suites and leak-enabled verification remain coordinator checks.

Release allocation guards assert no malloc/calloc/realloc during viewport
typing, gutter dispatch, query paste and field editing (G: CLAUDE.md law 2).
The existing full release editor typing guard also passed. Observed allocations
were zero (M)[AC], derived from the interposed allocator count between guard
begin/end; the sanitizer build's guard is intentionally inert.

## Tracked benchmarks

Ran the existing headless layout benches and the editor's null font-baseline
path. All commands completed successfully. Power and shared-load stamps are
in their logs; no before/after performance claim is made.

| Existing row | p50 / p99, microseconds (M)[AC] | Derivation |
| --- | --- | --- |
| Full ASCII layout | 299.175 / 359.771 | Existing layout harness order statistics; nanosecond outputs 299175 / 359771 divided by 1000 |
| Wrapped Unicode layout | 504.046 / 896.285 | Existing wrapped-layout harness order statistics; 504046 / 896285 ns divided by 1000 |
| Null editor serial input to submit | 1802.567 / 3026.103 | Existing editor harness ingress-to-T4 samples; 1802567 / 3026103 ns divided by 1000 |

Commands:

```sh
build/bench/layout_bench /tmp/edit-corpus ascii_code.c
build/bench/layout_bench /tmp/edit-corpus unicode.txt --wrap-only
build/bench/editor_bench --track --font-baseline=monospace --file=/tmp/edit-corpus/ascii_code.c
```

Sample counts and geometry come from the existing benches, with no new limits.
The layout rows printed misses against their existing goals. The editor rows
were TRACK and retained their structural/allocation checks. Benchmarks ran on
the shared box alongside headless verification; these results are tracked
observations under law 1, not blockers. Logs:
`/tmp/edit-m15-layout-bench.log`, `/tmp/edit-m15-wrap-bench.log`, and
`/tmp/edit-m15-editor-bench.log`.

## Files changed

- `src/editor/input.c`: prepared logical-line gutter lookup before text hits;
  gutter clicks bypass text multi-click selection; nonfatal horizontal follow.
- `src/layout/layout.c`: document the existing complete-row hit initialization.
- `src/layout/wrap.c`: refresh logical-line gutter hits on wrapped rows.
- `src/editor/findpanel.c`: bounded field paste, grapheme-aware word commands,
  and focused chord consumption.
- `src/editor/clip.c`: preserve the field destination across clipboard requests
  and route field replies without document mutation staging.
- `src/editor/private.h`: private integration declarations.
- `tests/editor_test.c`, `tests/findui_test.c`: headless regressions and guards.
- `docs/decisions/edit-m15.md`, this report, the session worklog and `HANDOFF.md`.

## Out of scope

Configurable panel key lookup remains edit-x8i's scope. Native X/Wayland
clipboard delivery, rendering and the display suites were not exercised in
this sandbox, as specified by the brief. No additional functional defect was
observed in the completed headless runs. The benchmark goal misses and the
LeakSanitizer environment limitation are recorded above without unrelated
code changes.
