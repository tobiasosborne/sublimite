# edit-cde worker report

Clipboard integration is implemented. Focused editor acceptance passes under clang ASan/UBSan and gcc release, including the active malloc guard. The release tree builds. Full verification remains restricted by denied X and IPC sockets and a busy shared display lock; native cross-app behavior and latency against Sublime need the coordinator's next drive.

Changed files:

- `src/editor/clip.c`: clipboard orchestration, arena recycler, native/null ownership, selection-driven PRIMARY, worker capture/normalization, grouped replacement and positioned middle-click paste.
- `src/editor/private.h`: per-editor clipboard state and internal declarations.
- `src/editor/input.c`: short action dispatch and shared bounded undo/mutation/journal helpers.
- `src/editor/editor.c`: clipboard events, pending-work polling, input ordering, PRIMARY refresh and withholding paste acknowledgement until mutation completes. Save paths were not changed.
- `src/editor/open.c`: clipboard initialization and shutdown ordering.
- `src/editor/buffers.c`: command busy checks and snapshot-aware buffer retirement.
- `src/x11/clip.c`, `src/x11/clip.h`: owned-buffer allocator/release hooks and length accessor; existing selection/INCR/MULTIPLE semantics and limits retained.
- `tests/editor_test.c`: clipboard acceptance and timing/PRIMARY/pointer regressions, including native opt-in coverage.
- `tests/x11_clip_review_test.c`: allocator-only and native adoption/borrow/release regressions.
- `fuzz/editor_fuzz.c`: independent clipboard byte-model pass.
- `docs/decisions/edit-cde.md`, `src/editor/STATUS.md`, `HANDOFF.md`, `docs/worklog/2026-10-11.md`, and this report.

No `bd` or `git` command was run. No Makefile, `paint.c`, `large.c`, or editor save-path changes were made. Work stayed in this worktree; required tests used their existing temporary fixtures and shared display lock.

The initial red test was written before implementation:

```sh
make -j"$(nproc)" build/san/tests/editor_test
ASAN_OPTIONS=detect_leaks=0 build/san/tests/editor_test --clipboard
```

```text
editor_test:73: FAIL editor_length(e) == n
editor_test:1914: FAIL expect(e, doubled, sizeof doubled - 1) == 0
```

The failure is the required duplicated-text assertion after select/copy/move/paste. Expected lengths derive from `sizeof` the original and normalized fixture strings. An initial undo-caret expectation was subsequently corrected to match existing Select All direction: anchor at the start and cursor at the end. The preceding red copy/paste failure was unchanged.

Additional regressions were added and observed red before their fixes. These cover deep horizontal middle-click placement, a CRLF crossing a scan window, preserving the physical input timestamp, withholding an unchanged paste frame, and middle paste queued behind PRIMARY preparation:

```text
editor_test:1977: FAIL editor_view(e).selection.cursor == target + 1
clipboard malloc guard: 0 allocations (ASan-inert); expected zero by law
clipboard: null round trip, UTF-8/CRLF, replacement, cut/undo, alternate keys, PRIMARY passed
clipboard bulk: worker queue, cross-slice CRLF, replacement/undo, journal, mallocs=0 passed
editor_test:1991: FAIL editor_view(e).selection.cursor == VIEW_WINDOW
clipboard malloc guard: 0 allocations (ASan-inert); expected zero by law
clipboard: null round trip, UTF-8/CRLF, replacement, cut/undo, alternate keys, PRIMARY passed
clipboard bulk: worker queue, cross-slice CRLF, replacement/undo, journal, mallocs=0 passed
editor_test:1913: FAIL e->edit_time_ns == copy.t0_ns
editor_test:2022: FAIL e->stats.submitted_sequence < e->stats.input_sequence
editor_test:2057: FAIL editor_length(e) == 2u * len
```

Commands were `editor_test --clipboard` for pointer/ingress regressions, `editor_test --clipboard-bulk` for frame acknowledgement, and `editor_test --clipboard-primary` for queued PRIMARY. Their exact logs are under `build/cde/*-red.log`. Assertion line numbers are compiler `__LINE__` values from each red build. Positions and lengths derive from fixture strings and existing view scan/window bounds; the doubled length is source plus pasted selection.

Real text-area coordinates in native coverage also exposed an unnecessary viewport jump from the setup/navigation cursor helper. The clicked line and horizontal viewport must stay put while these fixture rows fit. The viewport regression was observed red before click placement was changed to preserve the current viewport; a further red check showed that a negative/outside pointer position incorrectly pasted at the current cursor. Outside positions now do nothing. The empty-tab placeholder regression was already green and remains so.

```text
editor_test:1941: FAIL editor_view(e).first_line == click_view.first_line
editor_test:73: FAIL editor_length(e) == n
editor_test:1959: FAIL expect(e, doubled, sizeof doubled - 1) == 0
```

Final green run:

```sh
make -j"$(nproc)" all build/san/tests/editor_test build/san/tests/x11_clip_review_test build/fuzz/editor_fuzz
ASAN_OPTIONS=detect_leaks=0 build/san/tests/editor_test --clipboard
```

```text
clipboard malloc guard: 0 allocations (ASan-inert); expected zero by law
clipboard: null round trip, UTF-8/CRLF, replacement, cut/undo, alternate keys, PRIMARY passed
clipboard bulk: worker queue, cross-slice CRLF, replacement/undo, journal, mallocs=0 passed
clipboard middle click: deep horizontal position exact
clipboard PRIMARY: queued selection/collapse/middle paste waits for latest capture, mallocs=0 passed
clipboard middle click: no active tab preserves placeholder
```

Release verification:

```sh
make all
build/tests/editor_test --clipboard
```

```text
clipboard malloc guard: 0 allocations (active); expected zero by law
clipboard: null round trip, UTF-8/CRLF, replacement, cut/undo, alternate keys, PRIMARY passed
clipboard bulk: worker queue, cross-slice CRLF, replacement/undo, journal, mallocs=0 passed
clipboard middle click: deep horizontal position exact
clipboard PRIMARY: queued selection/collapse/middle paste waits for latest capture, mallocs=0 passed
clipboard middle click: no active tab preserves placeholder
```

The allocation result is zero (M)[bat], derived from `edit_malloc_guard_end()` over the tested command sequences, as required by the no-malloc law. BAT0 reported `Discharging` immediately before this release run (`build/cde/power.txt`). ASan's guard is inert; the release guard is active. Legacy broader-suite output sometimes hard-codes `[AC]`; that is not this session's power classification.

The allocator-only sanitizer check passes:

```sh
ASAN_OPTIONS=detect_leaks=0 build/san/tests/x11_clip_review_test --allocator-only
```

```text
clipboard arena allocator: PASS
```

Clipboard replacement and cut share an explicit undo group, including worker-normalized large paste and replacement over selection. Mutations and journal bytes are processed in existing bounded view chunks; partial replacement frames are withheld. PRIMARY preparation does not park ordinary typing. A middle paste waits for its latest pending PRIMARY capture, preserves the viewport at its clicked location, and ignores outside positions. Native opt-in allocation tracking uses the existing IO boundary to exclude platform packets. Immutable worker snapshots retain their buffer arenas through physical lease completion, and native allocator callbacks remain valid through X shutdown. The bulk crossover and memory reservations are derived from the supplied bead and existing platform/view limits in [the decision record](../decisions/edit-cde.md); no new timing budget or performance verdict is claimed.

The clipboard fuzzer oracle's deterministic seed smoke passes:

```sh
ASAN_OPTIONS=detect_leaks=0 build/fuzz/editor_fuzz -runs=0 build/cde/fuzz-seeds
```

The seed can be regenerated from its fixture without an invented run budget:

```python
fixture = b"clipboard\r\ntext"
seed = bytes((0, 0, len(fixture), 0, 2, len(fixture), len(fixture), 0,
              1, 0, len(b"clipboard"), 1))
```

The operation identifiers and undo flag come from the clipboard oracle; the four fields per operation follow the existing input record layout.

This is libFuzzer initialization/seed regression only, not a duration-based campaign. The seed encodes copy, paste and cut/undo using the fixture `clipboard\r\ntext`; positions derive from fixture lengths and its record width follows the existing editor model. [The complete output](../../build/cde/fuzz.log) is retained. The replay CLI shell contract also passes (`sh tools/test_replay_cli.sh build`).

`make check` was run, including after the final build:

```sh
ASAN_OPTIONS=detect_leaks=0 EDIT_DISPLAY_LOCK_TIMEOUT=0 make -j"$(nproc)" check
```

The lock option selects the existing wrapper's nonblocking behavior; it is not a latency target. The final check stops at CLI's unavailable display lock:

```text
== build/san/tests/base_test
0.0.1-dev
arena: ok
malloc guard: SKIP (AddressSanitizer owns malloc)
pool: ok
cpu: avx2=1 popcnt=1
base_test: PASS
== build/san/tests/cli_test
waiting for display :99 (held by unknown)
cli_test: display lock timeout: display :99 (held by unknown)
make: *** [Makefile:105: check] Error 1
```

An earlier check reached CLI and failed `ipc_server_init_display`. Direct probes establish the sandbox restrictions independently:

```text
X socket: [Errno 1] Operation not permitted
IPC bind: [Errno 1] Operation not permitted
```

Remaining suite binaries were run individually after the fail-fast check. There are 63 registered suite names (M)[bat], derived from the sorted `build/san/tests/*_test` inventory, and 44 successful suite exits (M)[bat], derived by counting successful entries in [the result ledger](../../build/cde/suite-results.tsv). All non-display module suites pass except `ipc_test`, which needs the denied local socket bind. The mixed default editor suite was interrupted after native/IPC failures and an existing settle timeout; its ledger entry marks it unsuccessful, not a completed full-suite result. Isolated undo replay subsequently passed (`EDITOR_REVIEW_ONLY=11-replay`), and final focused clipboard tests pass.

Display-related failed or unavailable suites: `editor_close_test`, `file_kill_test`, `gl_gate_test`, `gl_test`, `raster_test`, `refproto_test`, `refwin_test`, `refwin_timeout_test`, `x11_clip_review_test`, `x11_clip_test`, `x11_identity_test`, `x11_live_test`, `x11_order_test`, and `x11_stall_test`. Some fail display initialization directly; others stop at the busy display lock. Native portions of `editor_test` are also unavailable. `cli_test`, `editor_large_test`, `editor_p4_test`, and `ipc_test` fail socket initialization; their display/IPC coverage needs the coordinator. Exact per-suite outputs are `build/cde/<suite>.log`; `check.log` and `final-check.log` retain the aggregate attempts.

The editor bench and `editor_test --clipboard-native` were attempted through the display-lock wrapper and stopped at the busy lock. No native latency result is claimed. LeakSanitizer is disabled because of the documented sandbox limitation; the coordinator must rerun with leaks enabled.

What remains: coordinator `make all` and full leaks-enabled `make check` on Xvfb :99, `editor_test --clipboard-native`, the extended X clipboard review suite, and the next Astra cross-app/latency comparison against Sublime. The display number is the coordinator's supplied Xvfb endpoint. Nothing is ready for a Tobias trial before that comparison verdict.

Outside scope, reported without fixing: sandbox socket denial also prevents nominally headless IPC acceptance; the mixed editor suite hit its existing settle guard before the isolated replay rerun passed. `file_kill_test` opens a display through the included file-bench viewport helper, but the Makefile display-lock filter omits that suite. The coordinator should account for its display requirement when rerunning. General text-area caret/drag/wheel and other inert editor actions remain their assigned beads.
