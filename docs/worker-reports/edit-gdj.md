# edit-gdj — selection chord regression verification

Scope: `edit-gdj` only, following the binding worker scope rule in [CLAUDE.md](../../CLAUDE.md). `edit-whl` and `edit-2gx` remain separate. No bd/git commands were run, and no prohibited source files were edited.

The checked-in [Astra review](../reviews/astra-vs-sublime-1.md) corrected the original selection finding: pixel checks and a minimal repeat confirmed preservation of the original anchor. The brief and HANDOFF's earlier summary retain the withdrawn finding. Inspection confirms that `editor_handle_key` passes each movement binding's `KEYS_ARG_EXTEND` to `view_command`, and `set_cursor` changes the anchor only for movement without Shift. Modifier releases leave selection state intact.

Added exact-range editor regressions for separately pressed/released Shift chords, Ctrl+Shift word movement, vertical movement, Home/End, document Home/End, PageUp/PageDown, reversal across a nonempty backward selection, and resets after plain movement and text mutation. Both wrapped and unwrapped configurations run. The ingress/submission counting hooks verify the allocation requirement. No production patch was necessary; the original input source was restored byte-for-byte after controlled fault injection. No event-processing or frame-scheduling change remains.

Changed files:

- `tests/editor_test.c`: default-suite regression plus `--selection-chords` focused entry point.
- `src/editor/STATUS.md`: current worker result, preserving earlier records.
- `docs/decisions/edit-gdj.md`: retain existing semantics and explain the regression strategy.
- `docs/worker-reports/edit-gdj.md`: this report.
- `docs/worklog/2026-10-11.md` and `HANDOFF.md`: required session/handoff evidence.

Quantities and evidence: expected offsets are (G) exact ASCII fixture prefix lengths. File start is the empty prefix; first Right is `sizeof "a" - 1`; word end is `sizeof "alpha" - 1`. After Down, the selected range is (G) `[0, 29)`: first-line word letters = 5 (`alpha`) + 4 (`beta`) + 5 (`gamma`) + 5 (`delta`) = 19; first-line prefix = 19 letters + 3 spaces + 1 exclamation + 1 newline = 24 bytes; next-line column = 5 bytes (`alpha`'s length); endpoint = 24 + 5 = 29. The viewport text-row pair plus its tab row determines page movement. Test expectations use these literal-prefix expressions, not unexplained offset constants. The allocation expectation is (G) zero, required by the repository allocation law. No new duration or performance budget was introduced.

Runs below are (M)[bat]: BAT0 reported `Discharging` before the runs. Exit codes are captured process/tool return values; diagnostic line numbers derive from the test's `__LINE__`. Build parallelism uses `getconf _NPROCESSORS_ONLN`. LeakSanitizer was disabled because it fails under sandbox ptrace; the coordinator must rerun with leaks enabled.

The unchanged baseline passed the new assertions. Consequently, the red run is a **controlled regression-sensitivity check**, not evidence of a failing production baseline. After writing the test, temporarily inserted this fault immediately before movement dispatch:

```c
if (moving) e->v.state.selection.anchor = e->v.state.selection.cursor;
```

Red run, with that fault:

```text
$ ASAN_OPTIONS=detect_leaks=0 build/san/tests/editor_test --selection-chords
selection chord: got cursor=5 anchor=1 expected cursor=5 anchor=0
editor_test:99: FAIL s.anchor == anchor
editor_test:122: FAIL selection_chord(e, XKB_KEY_Right, ctrl_shift, word, start) == 0
exit=1
```

Restored `src/editor/input.c` from the saved original and verified identical bytes, then rebuilt the sanitizer target. Green run:

```text
$ ASAN_OPTIONS=detect_leaks=0 build/san/tests/editor_test --selection-chords
editor_test: separate Shift chords preserve exact ranges, reset after movement/mutation, and allocate nothing
exit=0
```

GCC release green run, with the active counting allocator:

```text
$ build/tests/editor_test --selection-chords
editor_test: separate Shift chords preserve exact ranges, reset after movement/mutation, and allocate nothing
exit=0
```

`make -j "$(getconf _NPROCESSORS_ONLN)" all` passed, (M)[bat] exit 0 from the build tool. It uses the required C11 warning flags, including `-Wshadow` and `-Wconversion`. The full existing view suite passed in both clang ASan/UBSan and gcc release; the release view allocation guard was active. The replay CLI shell contract also passed with the sandbox LeakSanitizer setting. Logs are under `build/edit-gdj/` in this worktree.

Required full check was run against the final test/source state, with the sandbox LeakSanitizer setting and the coordinator-specified Xvfb display. (M)[bat] build tool exit 2: Make propagates the failing suite's status. Relevant terminal tail:

```text
$ ASAN_OPTIONS=detect_leaks=0 DISPLAY=:99 EDIT_DISPLAY=:99 make -j "$(getconf _NPROCESSORS_ONLN)" check
cpu: avx2=1 popcnt=1
base_test: PASS
== build/san/tests/cli_test
waiting for display :99 (held by unknown)
cli_test:215: FAIL ipc_server_init_display(&decoy, NULL, getenv("DISPLAY")) == IPC_OK
cli_test: §36 forced startup timeout correctly failed
cli_test:215: FAIL ipc_server_init_display(&decoy, NULL, getenv("DISPLAY")) == IPC_OK
make: *** [Makefile:105: check] Error 1
exit=2
```

Because Make stops at that failure, ran the sanitizer binaries individually and checked that the result manifest covers every `tests/*_test.c` target. The full editor suite was interrupted after recorded native/reload and settle failures, then the remaining suites were resumed. Results and exact diagnostics are in `build/edit-gdj/sweep.json` and the per-suite logs. All results in this table are (M)[bat], derived from those captured process returns/diagnostics.

| Suites | Result and reason |
| --- | --- |
| `editor_close_test`, `file_kill_test`, `raster_test` | Failed native platform/viewport initialization; their display-independent checks also emitted passing results. The file kill suite imports the native viewport from `bench/file_bench.c`. |
| `refproto_test`, `refwin_test`, `refwin_timeout_test` | Failed with X connection errors on the coordinator-specified display. The reference period arithmetic checks passed. |
| `x11_clip_review_test`, `x11_clip_test`, `x11_identity_test`, `x11_live_test`, `x11_order_test`, `x11_stall_test` | Failed display/fixture initialization. The clipboard suite's attempted private Xvfb also could not supply a usable connection. |
| `cli_test`, `editor_p4_test`, `ipc_test` | Failed Unix-socket server initialization. The IPC suite explicitly reported `Operation not permitted`; this restriction also affects display-independent IPC tests. |
| `editor_large_test` | Initial cooperative-job deadline assertion failed. Dedicated retry passed the cooperative warm/find checks and subsequent headless cases, then failed at `mapped_startup_ipc` server initialization. |
| `editor_test` full default run | Native editor setup and reload-launcher failures, followed by a replay settle assertion; interrupted when it stopped making observable progress. The isolated existing replay case passed on retry. The new selection entry point passed independently in sanitizer and release builds. Full default acceptance remains for the coordinator. |
| `gl_test`, `gl_gate_test` | Passed their available pure checks; `gl_test` explicitly skipped native diagnostics. No native GL acceptance is claimed. |
| All remaining sanitizer binaries | Passed in the individual sweep. |

The Unix-socket restriction was separately verified at a writable worktree path, (M)[bat]:

```text
bind(AF_UNIX, worktree path): [Errno 1] Operation not permitted
```

Existing replay retry command: `ASAN_OPTIONS=detect_leaks=0 EDITOR_REVIEW_ONLY=11-replay build/san/tests/editor_test`; it passed, (M)[bat] exit 0, captured process return. Large-file retry command: `ASAN_OPTIONS=detect_leaks=0 DISPLAY=:99 EDIT_DISPLAY=:99 build/san/tests/editor_large_test`; it failed at IPC setup, (M)[bat] exit 1, captured process return, after the previously failing warm/find section passed. No test deadlines or source implementations were changed to obtain these outcomes.

Remaining verification: coordinator runs full `make check` with Xvfb at the display specified in the brief, Unix-socket binding permitted, and LeakSanitizer enabled; then verifies keyboard selection in the next Astra drive on the integrated build. This worktree cannot certify the unavailable native/default suite paths. The initial warm/replay deadline failures are reported above; they passed their dedicated retry paths and were not fixed as part of this bead.

Outside scope, reported without fixes:

- `edit-whl`: binding inspection confirms Ctrl+Y uses `KEYS_ACTION_REDO_OR_REPEAT`, while input dispatch recognizes `KEYS_ACTION_REDO`. No redo/repeat implementation or test was added here.
- `edit-2gx`: unsupported bound actions already take the unimplemented-action return branch in input dispatch. Find/panel chord behavior and its requested tests remain for that bead; no panel or key-state change was made here.
- Existing retry diagnostics hardcode `(M)[AC]` in places even though BAT0 read `Discharging`. The worker's external `[bat]` stamp is authoritative; those unrelated diagnostic strings were left unchanged.
- The initial existing deadline failures, restricted IPC tests, and unavailable native coverage remain coordinator acceptance evidence, not additional fixes in this worktree.

The coordinator can disposition the original selection finding as withdrawn, with the new regression coverage preserving the existing anchor semantics. Runtime source bytes remain unchanged.
