# edit-48u worker report — s6

Implemented the supplied pointer/find-panel backlog within this worktree. No
tracking or version-control command was run; protected coordinator documents
were not edited. Implementation work is complete; full-suite acceptance has
external and unrelated failures below.

Files touched:

- `src/editor/input.c`: surface-clipped inverse hits, overscan acceptance,
  empty-geometry guard, buffer/source/button click identity, saved-pointer
  frame autoscrolling, and viewport-preserving pointer selection repaint.
- `src/editor/private.h`: reserved pointer state and internal frame helpers.
- `src/editor/editor.c`: frame-turn scheduling, mixed-button chain reset
  before clipboard dispatch, and panel-mode geometry recomputation.
- `src/editor/findpanel.c`: panel opening/closing shares the geometry refresh.
- `tests/editor_test.c`: null-backend pointer, resize and Escape regressions;
  focused selectors and inclusion in the normal/headless editor suite.
- [Decision](../decisions/edit-48u.md): geometry, frame rate derivation,
  selection-follow policy and deliberate Escape retention.
- This report. Build/test artifacts are under `build/edit-48u/`.

The pointer portions of input dispatch were changed; key binding dispatch and
`src/editor/scrollctl.c` were left untouched. Escape retains the match,
consistent with the supplied Sublime behavior and the firsthand
[Sublime behavior explanation](https://forum.sublimetext.com/t/funny-search-select-behavior/8031).
This confirms published behavior; a live Sublime comparison remains for the
coordinator's display drive.

Rate derivation (E): signed outside distance d pixels yields d/h rows per
completed frame, with current physical row height h. For backend frame
interval dt the rate is d/(h*dt) rows per unit time (P). No interval is invented;
no timer or allocation is added. Regression expectations derive from a row
height outside the bottom edge, continued held-pointer frames, and twice that
height above the top edge. Wrapped text retains fractional pixel displacement
and uses the existing visual-row continuation. Selection extends from fresh
edge hit records while retaining its original anchor.

Evidence convention: run outputs below are verbatim observations (M)[AC],
with BAT0 `Not charging` checked before execution. Diagnostic source locations
are compiler source-map identifiers; exit values derive from shell success,
failure and signal conventions. Fixture geometry reuses the existing mouse
fixture (E); byte offsets and row displacements derive from fixture strings,
prepared layout metadata and physical row height (P). No new budget, duration
or workload count is introduced.

Red run, before implementing the fixes:

```sh
make build/tests/editor_test
for case in overscan buffer button empty resize drag drag-wrap escape; do
    echo "== --48u-$case"
    build/tests/editor_test "--48u-$case"
    echo "exit=$?"
done
```

```text
== --48u-overscan
editor_test:3488: FAIL e->v.state.selection.cursor==byte
exit=1
== --48u-buffer
editor_test:3510: FAIL e->v.state.selection.cursor==e->v.state.selection.anchor
exit=1
== --48u-button
editor_test:3510: FAIL e->v.state.selection.cursor==e->v.state.selection.anchor
exit=1
== --48u-empty
/bin/bash: line 1:    40 Segmentation fault      (core dumped) build/tests/editor_test "--48u-$case"
exit=139
== --48u-resize
editor_test:3536: FAIL e->panel_rows==wanted
exit=1
== --48u-drag
editor_test:3552: FAIL trace_now_ns()<deadline
exit=1
== --48u-drag-wrap
editor_test:3552: FAIL trace_now_ns()<deadline
exit=1
== --48u-escape
edit-48u Escape retains match passed
exit=0
```

The buffer predicate fixture deliberately restores the prior timestamp after
a normal switch: switching already clears timing, but the hit predicate had
no buffer identity. The Escape assertion was already green; keeping the match
is deliberate parity, so no behavior change was manufactured to make it red.
The empty-row test exposed the out-of-range read as a process crash.

Green runs, after implementation and expanded autoscroll assertions:

```sh
# gcc release; the drag assertions include the active malloc guard.
for case in overscan buffer button empty resize drag drag-wrap escape; do
    echo "== --48u-$case"
    build/tests/editor_test "--48u-$case"
    echo "exit=$?"
done

# clang ASan/UBSan; same selectors and identical output.
export ASAN_OPTIONS=detect_leaks=0
for case in overscan buffer button empty resize drag drag-wrap escape; do
    echo "== --48u-$case"
    build/san/tests/editor_test "--48u-$case"
    echo "exit=$?"
done
```

Both runs produced exactly this output:

```text
== --48u-overscan
edit-48u overscan passed
exit=0
== --48u-buffer
edit-48u chain identity passed
exit=0
== --48u-button
edit-48u chain identity passed
exit=0
== --48u-empty
edit-48u empty rows passed
exit=0
== --48u-resize
edit-48u panel resize passed
exit=0
== --48u-drag
edit-48u drag autoscroll passed
exit=0
== --48u-drag-wrap
edit-48u drag autoscroll passed
exit=0
== --48u-escape
edit-48u Escape retains match passed
exit=0
```

LeakSanitizer cannot finish under this sandbox's process tracing and reports
that it does not work under ptrace. Sanitizer checks therefore use
`ASAN_OPTIONS=detect_leaks=0`; ASan and UBSan stay enabled. Release drag tests
exercise the functioning malloc counter; sanitizer malloc guards are inert.

Build and wider validation:

```text
$ make all
make: Nothing to be done for 'all'.
exit=0

$ ASAN_OPTIONS=detect_leaks=0 build/san/tests/editor_test --headless
editor_test: null script and journal byte model passed
editor_test: null 10000 keys mallocs=0 guard=ASan-inert
editor_test: idle blink timeout and unfocused timer disarm passed
editor_test: queue depth one and coalesced edits passed
editor_test: headless passed
exit=0
```

The typing workload count above is the existing allocation fixture's count,
not a new requirement. Builds used the installed gcc release and clang
sanitizer toolchains with the repository's strict C11 warnings. The complete
headless editor suite passed again after the final code/test changes.
`scroll_test`, `editor_scroll_test`, `editor_session_test`,
`editor_hot_exit_test`, headless `editor_close_test`, and replay CLI shell
checks passed. All other Makefile-listed non-display binaries passed except
the failures listed below. The first `scroll_test` attempt collided with a
relink and reported a busy executable; its serial rerun passed.

Full `make check` was attempted. The shared display wrapper reached its
existing lock timeout and failed. To expose the actual sandbox failure without waiting on another
worker's display runner, the same recipe was also invoked with
`DISPLAY_TEST_SAN=` and the sanitizer environment above; it stopped at CLI
socket setup:

```text
base_test: PASS
== build/san/tests/cli_test
cli_test:391: FAIL ipc_server_init_display(&decoy, NULL, getenv("DISPLAY")) == IPC_OK
cli_test: §36 forced startup timeout correctly failed
cli_test:391: FAIL ipc_server_init_display(&decoy, NULL, getenv("DISPLAY")) == IPC_OK
make: *** [Makefile:133: check] Error 1
```

Display suites cannot connect to the X socket in this sandbox. A direct native
probe showed:

```text
FAIL tests/x11_live_test.c:118: §22 colormap protocol error returned -1
FAIL tests/x11_live_test.c:118: §22 window protocol error returned -1
FAIL tests/x11_live_test.c:118: §22 present protocol error returned -1
x11_live_test: private display connection failed
§22 startup errors: FAILED
exit=1
```

The coordinator must rerun display suites on Xvfb :99, and rerun with leak
checking enabled outside the tracing sandbox. This worker does not claim a
complete `make check` pass.

Outside this bead's scope, reported without fixes:

- `file_kill_test` fails its viewport initializer. Although the Makefile does
  not classify it as a display suite, it includes `bench/file_bench.c`, whose
  viewport initializer calls `plat_init`; this needs the unavailable X socket.
  Its cancellation and pooled-save subchecks pass. Rerun on the display.
- `ipc_test` reports socket initialization denied with `Operation not
  permitted`. CLI, `editor_large_test` and `editor_p4_test` similarly stop at
  IPC server initialization. The large-file and editing subchecks before those
  failures pass. Rerun with local Unix-socket access.
- `journal_kill_test` fails the initial kill-before-pump replay with
  `JOURNAL_CALLBACK`. This standalone journal replay/model test does not use
  the pointer or panel code changed here; its serial rerun fails identically.
  This is an unrelated remaining acceptance failure, not attributed to X.

Exact serial failure diagnostics (M)[AC]:

```text
file_kill_test:198: FAIL rc == 0
file_kill_test:324: FAIL WIFEXITED(status) && WEXITSTATUS(status) == 0
init rc=4 errno=1 (Operation not permitted)
FAIL tests/ipc_test.c:130: init_rc==IPC_OK
journal_kill_test: FAIL trial=0 target=1 issued=1 page_cache=1 ack=0 replay=0 rc=6 error=0
```

Here the journal callback value derives from `journal_error` enumeration
order; trial/record values derive from that existing fixture's initial case.
No unrelated implementation was changed.

The editor module bench was run using its existing serial workload and sample
count (M)[AC], with `--track --no-idle --no-p4 --serial-only`. Its null-backend
structural checks and real release malloc guard passed; tracked timing goals
were missed. The raster row could not open its display. Full output is in
`build/edit-48u/build-48u-bench.log`; this is tracking evidence, not a new
performance bead or blocker invented by this worker.

What remains: coordinator display/IPC/leak-check reruns, the unrelated journal
replay/model failure, and live Sublime comparison of the derived drag feel and
Escape behavior. No edit-48u implementation is left pending. Stop after this
bead.
