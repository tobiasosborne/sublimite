# edit-whl and edit-2gx — worker result

The authorized brief covers both input issues. Ctrl+Y now uses the existing cooperative redo path. With no redo entry it leaves the document unchanged; **repeat remains missing** under the brief's permitted fallback. Ctrl+Shift+Z still redoes. The editor stores grouped undo deltas rather than a cheaply replayable last insert/delete command; this patch adds no command recorder.

The baseline panel-state test passed before implementation. Ctrl+F / Ctrl+H / F3 / Ctrl+Shift+F / Ctrl+P / Ctrl+G already consumed their bindings, preserved bytes and selection, cleared no extra state, and left `KEYS_CHORD_NONE`. Following the real modifier press/release sequence, `alpha` inserted normally at the cursor with no stray newline. The original complaint is the missing panel, not text redirection or corrupted modifier/prefix state.

These unsupported actions now display `not implemented` for (G) one frame generation, directly required by the brief. The generation is captured after `editor_begin_frame`; composition displays the notice while it matches the grid generation. Retries retain it. The next generation restores the complete bottom document row, including on an otherwise caret-only frame. No timer or immediate expiry frame is scheduled. The existing source-change prompt has priority. Because there is no reserved status-line chrome yet, the notice temporarily overlays the bottom display row; it does not alter document layout, cursor, or selection.

Changed files:

- `src/editor/input.c`: route `KEYS_ACTION_REDO_OR_REPEAT` to history replay; acknowledge the requested unsupported panel/navigation actions.
- `src/editor/private.h`: private notice generation field.
- `src/editor/paint.c`: bottom status overlay and restoration on the next generation.
- `tests/editor_test.c`: default regressions and focused `--redo-or-repeat`, `--panel-state`, `--panel-notice` entry points; previous worker's selection-chord tests preserved.
- `docs/decisions/edit-whl.md` and `docs/decisions/edit-2gx.md`: implementation choices and fallback limits.
- `docs/worker-reports/edit-whl.md`: this report.

No bd or git commands were run. HANDOFF.md, docs/worklog, and the Makefile were not edited. All work and evidence remain in this worktree.

## Derivations and measurement stamps

(G) Buffer lengths and cursor positions use `sizeof` of literal ASCII prefixes minus their terminator: insertion starts at the length of `left`, then advances by the length of `alpha`. Exact expected document bytes are `leftalpha right\nnext`. A statistic increment of one derives from one consumed bound shortcut. Zero mutations for the shortcut, unchanged selection, and empty-redo no-op behavior derive from the brief; zero input-path allocations derives from CLAUDE.md Law 2. Test dimensions use the fixture/status literal's size for width, and a pair of text rows plus the tab row for height. The status palette and ASCII atlas mapping come from the existing source-change prompt, not new numerical settings. No new time budget was introduced.

(M) Exit codes below are captured subprocess/tool return values, not inferred pass counts. Diagnostic line numbers come from the test's `__LINE__` at the run's source state. Make's failure code propagates its recipe failure. Parallel build jobs derive from `getconf _NPROCESSORS_ONLN`. Battery sampling read BAT0 `Discharging` before baseline runs ([bat]), `Charging` before the first green runs and full check ([AC]), and `Discharging` before the final green runs ([bat]). The non-display manifest records the sampled BAT0 status for each suite; power changed during verification. Benchmarks print their own power/load stamps. ASan/UBSan runs set `ASAN_OPTIONS=detect_leaks=0` for the known sandbox ptrace limitation; release runs exercise the active allocation guard.

## Baseline and red run

Written tests first, before production edits. The independent state probe established existing behavior:

```text
$ ASAN_OPTIONS=detect_leaks=0 build/san/tests/editor_test --panel-state
editor_test: panel shortcuts are consumed, preserve selection/prefix state, and subsequent alpha types normally
exit=0

```

Actual baseline failures (no injected faults):

```text
$ ASAN_OPTIONS=detect_leaks=0 build/san/tests/editor_test --redo-or-repeat
editor_test:73: FAIL editor_length(e) == n
editor_test:167: FAIL expect(e, text, sizeof text - 1u) == 0
exit=1

$ ASAN_OPTIONS=detect_leaks=0 build/san/tests/editor_test --panel-notice
editor_test:232: FAIL e->grid.cells[row + c].glyph_index == (uint8_t)status[c]
exit=1

```

Review added a further failing regression before fixing prompt-transition expiry. The source-change path bypassed normal text composition, leaving the previous notice row visible in a later generation. (M)[bat], captured process result; the diagnostic line derives from `__LINE__`:

```text
$ ASAN_OPTIONS=detect_leaks=0 build/san/tests/editor_test --panel-notice
editor_test:271: FAIL memcmp(e->grid.cells + bottom, bottom_before, sizeof bottom_before) == 0
exit=1
```

Expiry now restores the cached bottom text/minimap row before composing the source-change prompt. The final green run below includes this regression in both sanitizer and release builds.

## Green run

Final clang sanitizer runs:

```text
BAT0=Discharging
$ ASAN_OPTIONS=detect_leaks=0 build/san/tests/editor_test --redo-or-repeat
editor_test: Ctrl+Y redoes inserts/deletes, empty redo is inert, Ctrl+Shift+Z still redoes
exit=0
BAT0=Discharging
$ ASAN_OPTIONS=detect_leaks=0 build/san/tests/editor_test --panel-notice
editor_test: panel shortcuts show a generation-only status and restore the bottom row
exit=0
BAT0=Discharging
$ ASAN_OPTIONS=detect_leaks=0 build/san/tests/editor_test --selection-chords
editor_test: separate Shift chords preserve exact ranges, reset after movement/mutation, and allocate nothing
exit=0

```

The same focused entry points passed in gcc release, with the counting allocator active; logs are `build/edit-whl/green-rel--*.log`. Regression coverage includes insert undo/redo, no-op Ctrl+Y without redo, delete undo/redo after Ctrl+L and Backspace, unchanged Ctrl+Shift+Z, every requested unsupported chord, preserved empty/nonempty selections, exact subsequent typing, statistic increments, no retained prefix, status-generation retention, full bottom-row restoration including a source-change transition, and no allocation during the notice or subsequent typing. The restoration assertion compares the original rendered row including its gutter.

`make -j "$(getconf _NPROCESSORS_ONLN)" all build/san/tests/editor_test` passed with the required C11 / `-Wall -Wextra -Werror -Wshadow -Wconversion` flags, captured exit=0. This includes the complete `make all` target; build output is `build/edit-whl/all.log`.

## Full acceptance and remaining work

Required full check was run:

```text
$ ASAN_OPTIONS=detect_leaks=0 DISPLAY=:99 EDIT_DISPLAY=:99 make -j "$(getconf _NPROCESSORS_ONLN)" check
== build/san/tests/base_test
base_test: PASS
== build/san/tests/cli_test
cli_test:215: FAIL ipc_server_init_display(&decoy, NULL, getenv("DISPLAY")) == IPC_OK
cli_test: §36 forced startup timeout correctly failed
cli_test:215: FAIL ipc_server_init_display(&decoy, NULL, getenv("DISPLAY")) == IPC_OK
make: *** [Makefile:105: check] Error 1
exit=2
```

After the final source-prompt expiry correction, repeated full `make check`: (M)[bat] captured exit=2 at the same CLI IPC initialization assertion. It rebuilt the sanitizer test executables against the final library. Logs are `build/edit-whl/check.log` and `build/edit-whl/final-check.log`. Native display suites cannot pass in this sandbox because access to the X socket is denied. This environment additionally denies Unix-socket binding, affecting display-independent IPC acceptance. A direct probe at the sandbox display and a writable worktree path confirmed:

```text
X display: [Errno 1] Operation not permitted
local IPC: [Errno 1] Operation not permitted

```

To continue after Make stopped at CLI, ran every sanitizer target outside the Makefile's display classification individually. The classification excludes `cli_test`, `editor*`, `x11*`, `ref*`, `gl*`, and `raster_test`, exactly as the Makefile does. (M)[bat] The selected target count is 42 = the manifest length obtained from those filtered `tests/*_test.c` names. (M)[bat] Passed 40 = 42 selected minus 2 captured failing returns. Per-suite power stamps, returns, and names are in `build/edit-whl/non-display-results.json`; exact output is in `build/edit-whl/suite-*.log`.

| Exception | Captured result and diagnosis |
| --- | --- |
| `file_kill_test` | (M)[AC] exit=1 from the process. The filename falls outside the Makefile display filter, but its viewport/queued open-save checks call the native renderer from the included file bench. Viewport initialization and native child checks failed; pure visibility/cancellation and self-check sections passed. |
| `ipc_test` | (M)[bat] exit=1 from the process. Its diagnostic reports `Operation not permitted` at server initialization, matching the independent Unix-socket probe. |

Thus the requirement that all non-display suites pass cannot be certified in this sandbox: the IPC exception is genuinely display-independent, and the file-kill classification includes display work. Neither suite was modified or hidden from the sweep. All other selected suites passed, including `keys_test`, `view_test`, `undo_test`, `layout_test`, `render_test`, and the existing headless editor/typing checks embedded in `harness_test`.

After the final prompt-transition correction, `make check` passed for the runnable non-display set plus the existing shell contracts. The override contains exactly the passed target names from the manifest; it explicitly excludes the native file-kill and denied IPC exceptions above. (M)[bat] Final captured exit=0; the printed count of 40 derives from Make's `words(TEST_SAN)` over those manifest targets, equivalently 42 filtered names minus the 2 recorded exceptions.

```text
$ ASAN_OPTIONS=detect_leaks=0 DISPLAY=:99 EDIT_DISPLAY=:99 make check TEST_SAN="$(cat build/edit-whl/runnable-targets.txt)"
check: 40 test binaries passed
test_replay_cli: all passed
display lock contract: owner PID, alias/timeout, make/scripts, nested inheritance, exit status PASS
exit=0
```

The final log and process return are `build/edit-whl/final-runnable-check.log` and `build/edit-whl/final-runnable-check-exit.txt`. The lock contract deliberately holds a lock and expects its subprocesses to time out; its passing result does not certify a native X connection. The preceding runnable check also passed; no test implementation or build rule was changed to bypass the environmental exceptions.

The focused native raster editor check also failed at `editor_open`, captured exit=1; `build/edit-whl/native-editor.log` holds the diagnostic. No successful native display coverage is claimed.

The editor benchmark ran with its existing default population and `--track --no-idle --no-p4 --serial-only`, stamped (M)[AC] with the benchmark's sampled load. The null-backend serial typing row completed with structural checks passing and no allocations; its measured tail exceeded the existing G1 reference row on this shared loaded box. The command then failed at native raster `editor_open`, captured exit=1. The complete measured values and load are retained in `build/edit-whl/bench.log`; no performance regression or baseline comparison is inferred from this single run.

Repeat-last-command and real find/replace/navigation panels remain unimplemented. Actual panels belong to edit-73j. The coordinator must rerun full `make check` with X access, Unix-socket binding, and LeakSanitizer available, and run the next Astra comparison on the integrated build before handover. No unrelated failures were fixed.

## Capacity regression continuation (rebased worktree)

The failure comes from the capacity test's private-handler fixture, not the panel notice or Ctrl+Y dispatch. `rc=5` is `VIEW_ERR_BUSY`, not `EDITOR_ERR_CAPACITY` (the latter is negative). Empty-TYPE cursor repair calls `view_restore`, which can yield on the existing view deadline or charged-work bound. The fixture immediately dispatched another key through `editor_handle_key` without completing that repair. The production `editor_step` loop instead calls `editor_continue_action` while the view is busy before dispatching another key. The view header explicitly requires continuation or cancellation before another command.

The original focused test reproduced the coordinator's failure without any production edits. (M)[AC] Recorded key positions and return codes come from the test diagnostic; tree/index sizes come from their length accessors. Different failure positions demonstrate the timing dependence. The second run is retained in `build/edit-whl/continuation-red-index-capacity.log`:

```text
$ ASAN_OPTIONS=detect_leaks=0 build/san/tests/editor_test --index-capacity
capacity typing: key=63 rc=5 tree=65599 index=65599
editor_test:1833: FAIL rc == 0
exit=1
```

A temporary diagnostic checked the view before dispatching each key. (M)[AC] The action value names `EDITOR_ACTION_REPAIR`; frame and notice values are the editor's actual fields, and scanned work is the view's charged-work counter. It confirmed that the preceding key had already yielded, with no panel notice involved:

```text
capacity pre-key: key=119 action=3 scanned=633 frame=3 notice=0
capacity typing: key=119 rc=5 tree=65655 index=65655
editor_test:1836: FAIL rc == 0
exit=1
```

This probe is recorded in `build/edit-whl/continuation-red-probe.log` and removed from the final source. No test-merge damage or production shortcut defect was found.

Changed only `tests/editor_test.c:index_capacity_typing`: after each successful direct key call, resume pending cursor repair through `editor_continue_action`. Assert that repair completes, its action clears, the cursor advances by the literal typed byte length, and index/tree lengths still match. The completion deadline reuses `settle()`'s existing bound; no new timing limit is introduced. Crucially, this does **not** call `settle` or `editor_step` between keys: index maintenance remains paused, preserving the original successive reserved-capacity edits and final bounded-chunk assertion. The existing refusal test remains unchanged and passes. No return code is ignored or converted to success; production code is unchanged.

Final focused results below are (M)[AC], captured process returns. The existing capacity fixture's key count and chunk bound remain unchanged; the new cursor assertion derives from the number of successful one-byte `x` insertions. Sanitizer logs are `build/edit-whl/continuation-san--*.log`:

```text
$ ASAN_OPTIONS=detect_leaks=0 build/san/tests/editor_test --index-capacity
P1-1 section 1: file-backed successive typing keeps bounded index geometry passed
exit=0
$ ASAN_OPTIONS=detect_leaks=0 build/san/tests/editor_test --index-refusal
P1-1 section 1: replacement and undo capacity refuse before buffer/history mutation passed
exit=0
$ ASAN_OPTIONS=detect_leaks=0 build/san/tests/editor_test --redo-or-repeat
editor_test: Ctrl+Y redoes inserts/deletes, empty redo is inert, Ctrl+Shift+Z still redoes
exit=0
$ ASAN_OPTIONS=detect_leaks=0 build/san/tests/editor_test --panel-state
editor_test: panel shortcuts are consumed, preserve selection/prefix state, and subsequent alpha types normally
exit=0
$ ASAN_OPTIONS=detect_leaks=0 build/san/tests/editor_test --panel-notice
editor_test: panel shortcuts show a generation-only status and restore the bottom row
exit=0
$ ASAN_OPTIONS=detect_leaks=0 build/san/tests/editor_test --selection-chords
editor_test: separate Shift chords preserve exact ranges, reset after movement/mutation, and allocate nothing
exit=0
$ ASAN_OPTIONS=detect_leaks=0 build/san/tests/editor_test --minimap-flash
edit-g11 null typing: keys=95 frames=96 orange_frames=0 stale_frames=93 changed_pending=0 chrome_markers=0 mallocs=0 fill_max_ns=14870
exit=0
```

The minimap line is the test's measured output, not a new performance comparison; counts derive from its supplied typing fixture and submitted-frame probe, and the maximum fill time comes from its trace clock. Release `--index-capacity` and `--index-refusal` also print their passing messages and exit successfully; logs are `build/edit-whl/continuation-rel--*.log`.

Build results are (M)[AC], captured Make process returns. `getconf _NPROCESSORS_ONLN` supplies parallelism, as in the original report:

```text
$ make -j "$(getconf _NPROCESSORS_ONLN)" all build/san/tests/editor_test
exit=0
$ make all
make: Nothing to be done for 'all'.
exit=0
```

The first command rebuilt both release and sanitizer editor tests with the required warning flags. Complete output and exit files are `build/edit-whl/continuation-all.log`, `continuation-all-exit.txt`, `continuation-make-all.log`, and `continuation-make-all-exit.txt`. No `git` or `bd` commands were run, and no build rules, production files, or unrelated tests were edited.

The complete sanitizer editor binary was also run without an option/filter. (M)[AC] Its direct process return was failure in `review_suite`, before the capacity tests in default-main order. The full log is `build/edit-whl/continuation-direct-full-editor.log`; the captured return is in `continuation-direct-full-editor-exit.txt`. Relevant failure output:

```text
$ ASAN_OPTIONS=detect_leaks=0 DISPLAY=:99 EDIT_DISPLAY=:99 build/san/tests/editor_test
editor_test:1195: FAIL editor_open(&e, &cfg, &backend) == 0
reload launcher result=4
editor_test:1234: FAIL waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0
editor_test:625: FAIL editor_open(&e, &cfg, &b) == 0
editor_test:651: FAIL editor_open(&e, &cfg, &b) == 0
editor_test:819: FAIL editor_buffer_prepare(e, source, NULL, 0, &b) == 0
editor_test:64: FAIL false
editor_test:973: FAIL settle(e) == 0
editor_test:933: FAIL editor_open(&e, &cfg, &backend) == 0
editor_test:1589: FAIL editor_open(&e, &cfg, &b) == 0
editor_test:2219: FAIL review_suite(NULL) == 0
exit=1
```

This is not a successful full-suite certification. An independent socket probe returned `Operation not permitted` connecting to `/tmp/.X11-unix/X99`, confirming the native-display restriction noted in the original report. The file-preparation assertion and journaled replay fixture's settle timeout are also recorded, without assigning an unverified cause or changing their tests. The full log includes passing shortcut/selection/minimap tests and other successful headless review cases; no ASan/UBSan diagnostic was printed. Focused capacity/refusal acceptance above is independent of the default suite stopping before those cases.

The full invocation through the repository display lock also completed after obtaining the shared lock. (M)[AC] It exited unsuccessfully at the same default `review_suite` boundary, with native initialization failures, a journal checkpoint fixture's editor-open failure, and the replay settle timeout. Complete stdout/stderr and captured process return are `build/edit-whl/continuation-full-editor.log` and `continuation-full-editor-exit.txt`:

```text
$ ASAN_OPTIONS=detect_leaks=0 DISPLAY=:99 EDIT_DISPLAY=:99 sh tools/with_display_lock.sh build/san/tests/editor_test
waiting for display :99 (held by unknown)
reload launcher result=4
editor_test:1234: FAIL waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0
editor_test:625: FAIL editor_open(&e, &cfg, &b) == 0
editor_test:651: FAIL editor_open(&e, &cfg, &b) == 0
editor_test:795: FAIL editor_open(&e, &cfg, &b) == 0
editor_test:64: FAIL false
editor_test:973: FAIL settle(e) == 0
editor_test:933: FAIL editor_open(&e, &cfg, &backend) == 0
editor_test:1589: FAIL editor_open(&e, &cfg, &b) == 0
editor_test:2219: FAIL review_suite(NULL) == 0
exit=1
```

The full suite therefore still needs the coordinator's run with native display access and investigation of any remaining checkpoint/replay fixture failures there. This continuation fixes the capacity fixture contract and demonstrates every requested focused regression passing; it does not claim the complete editor suite passed in this sandbox.
