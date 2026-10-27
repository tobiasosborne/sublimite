# edit-cxho.3 — session s24 worker report

Implemented only the assigned find-focus bead in this worktree. No `bd` or
`git` commands were run. The installed editor/profile, main checkout, other
worktrees and protected coordination documents were not changed.

## Changes

- `src/editor/private.h`: explicit field/document find focus state and pointer entry point.
- `src/editor/findpanel.c`: focus-aware physical keys and find actions; panel
  click routing, query/replacement/Where caret placement, clickable visible
  regex/case/word brackets, find-only Tab consumption, field caret visibility.
- `src/editor/editor.c`: document caret visibility and pointer focus repaint
  after ordinary mouse placement; text, gutter and minimap presses transfer focus.
- `src/findui/findui.h`, `src/findui/paint.c`: read-only panel hit testing that
  shares status width and horizontal field-scroll calculations with painting.
- `tests/editor_test.c`: injected click/type/refocus, caret visibility,
  F3/Shift+F3, Escape selection retention, document Home/Return and field Tab,
  plus explicit field/document typing malloc guards.
- `tests/findui_test.c`: injected query/replacement clicks, bracket toggles,
  gutter/minimap document focus, and tab-switch focus retention.
- `docs/decisions/edit-cxho.3.md` and this report.

Only the focused caret is drawn. The detailed brief supersedes the older
bead wording about Return/arrows: with document focus these edit/move in the
document; Ctrl+F/H selects and focuses the field. Tab switching retains the
focus kind. Opening Find/Replace always focuses the field.

Numbers below are command/test evidence (M)[AC], with AC established by the
power supply's online flag while BAT0 reports `Not charging`. Diagnostic line
numbers identify source locations. Exit zero means success (P); nonzero exits
are observed failures. Parallelism and display number are assigned by the
brief (P). Test allocation expectations are zero (P), from the no-malloc rule;
fixture offsets/counts derive from the literal strings and explicit operations.

## Red, captured before implementation changes

`make -j4 build/tests/editor_test build/tests/findui_test` — exit 0.

Power/load stamp adjacent to the regression runs:

```text
Not charging
 10:26:42 up 1 day, 12:29,  1 user,  load average: 0.93, 1.00, 1.31
```

```sh
XDG_RUNTIME_DIR=$(mktemp -d) DISPLAY=:99 ASAN_OPTIONS=detect_leaks=1 \
  sh tools/with_display_lock.sh build/tests/editor_test --cxho3-focus
```

```text
editor_test:78: FAIL editor_length(e) == n
editor_test:4524: FAIL expect(e, edited, sizeof edited - 1u) == 0
red editor exit=1
```

The document did not gain the typed byte after a text click.

```sh
EDIT_CXHO_PANEL=focus build/tests/findui_test
```

```text
tests/findui_test.c:990: assertion failed: state.query_length==strlen(text) && state.query_cursor==cursor
red findui exit=134
```

The panel-row click did not transfer field/caret ownership. The assertion
termination is the test harness's SIGABRT exit, not production error handling.

An additional injected refocus case exposed the remaining Ctrl+H edge:
activate the replacement field, click the document, then Ctrl+H and type a
new query. Before the fix, the replacement retained focus. With only that
new test added, `make -j4 build/tests/findui_test` exited 0 and
`EDIT_CXHO_PANEL=focus build/tests/findui_test` produced:

```text
tests/findui_test.c:990: assertion failed: state.query_length==strlen(text) && state.query_cursor==cursor
Ctrl+H refocus red exit=134
```

Find/Replace opening now explicitly selects the query field before selecting
its contents. The same command after the implementation change produced:

```text
edit-cxho.3 find panel: query/replacement cell clicks and option toggles passed
edit-cxho.3 find panel: gutter/minimap document focus and tab focus retention passed
Ctrl+H refocus green exit=0
```

## Green and verification

`make -j4 all` — exit 0, GCC release with the required strict warnings.
`make -j4 build/san/tests/findui_test build/san/tests/editor_test` — exit 0,
Clang ASan/UBSan. Toolchain versions reported by the installed tools are GCC
13.3.0 and Clang 18.1.3 (M)[AC].

The final release regression selector:

```sh
XDG_RUNTIME_DIR=$(mktemp -d) DISPLAY=:99 \
  sh tools/with_display_lock.sh build/tests/editor_test --cxho3-focus
```

```text
edit-cxho.3 focus: document/query typing, panel caret, Tab, F3/previous, Escape, Return, Ctrl+F/H passed; field mallocs=0 document mallocs=0 guard=active
green editor exit=0
```

The additional release injection tests:

```sh
EDIT_CXHO_PANEL=focus build/tests/findui_test
```

```text
edit-cxho.3 find panel: query/replacement cell clicks and option toggles passed
edit-cxho.3 find panel: gutter/minimap document focus and tab focus retention passed
green findui exit=0
```

The release guard is active. The sanitizer build's malloc interposer is
intentionally inert because ASan owns allocation; the release run above is
the allocation evidence.

The first test-build attempt exited 2 for a test-local name shadowing the
existing `minimap` type. The first sanitizer build exited 2 for Clang's enum
signedness warning. Both were corrected before the successful builds. An
initial sandboxed sanitizer run exited 1 at LeakSanitizer shutdown with
`LeakSanitizer has encountered a fatal error`; the required runs were then
executed outside the sandbox, retaining `ASAN_OPTIONS=detect_leaks=1`.

Complete sanitizer suite commands, with runtime directories removed on exit:

```sh
ASAN_OPTIONS=detect_leaks=1 build/san/tests/findui_test
XDG_RUNTIME_DIR=$(mktemp -d) DISPLAY=:99 ASAN_OPTIONS=detect_leaks=1 \
  EDIT_REFUSE_REAL_DISPLAY=1 \
  sh tools/with_display_lock.sh build/san/tests/editor_test
```

Sanitizer findui result:

```text
findui_test: all passed
san findui_test exit=0
```

Sanitizer editor result:

```text
edit-cxho.3 focus: document/query typing, panel caret, Tab, F3/previous, Escape, Return, Ctrl+F/H passed; field mallocs=0 document mallocs=0 guard=ASan-inert
editor_test: all passed
san editor_test exit=0
```

Adjacent sanitizer run power/load stamps (M)[AC]:

```text
Not charging
 10:33:57 up 1 day, 12:37,  1 user,  load average: 1.90, 1.80, 1.52
Not charging
 10:37:03 up 1 day, 12:40,  1 user,  load average: 1.99, 2.22, 1.75
```

The full check uses a temporary `XDG_RUNTIME_DIR`, `DISPLAY=:99`,
`EDIT_DISPLAY=:99`, `EDIT_REFUSE_REAL_DISPLAY=1`, and
`ASAN_OPTIONS=detect_leaks=1`; the existing make recipe runs display suites
through `tools/with_display_lock.sh`. No new large fixture is introduced;
existing large-fixture suites retain their sparse-file cleanup.

Initial full-check start power/load and disk stamp (M)[AC]:

```text
Not charging
 10:34:44 up 1 day, 12:37,  1 user,  load average: 1.66, 1.74, 1.51
Filesystem               Size  Used Avail Use% Mounted on
/dev/mapper/vgmint-root  1.8T  140G  1.6T   9% /
```

The initial full check completed successfully; it began before the final
Ctrl+H refocus fix, so the full command is repeated against the final build.
Initial result (M)[AC], with the binary count supplied by the Makefile's test
discovery:

```text
check: 110 test binaries passed
display lock contract: owner PID, alias/timeout, make/scripts, nested inheritance, exit status PASS
make -j4 check exit=0
Not charging
 10:49:16 up 1 day, 12:52,  1 user,  load average: 3.05, 2.87, 2.29
Filesystem               Size  Used Avail Use% Mounted on
/dev/mapper/vgmint-root  1.8T  144G  1.6T   9% /
```

Final full-check start stamp (M)[AC]:

```text
Not charging
 10:49:50 up 1 day, 12:52,  1 user,  load average: 2.81, 2.81, 2.29
Filesystem               Size  Used Avail Use% Mounted on
/dev/mapper/vgmint-root  1.8T  144G  1.6T   9% /
```

Final full-check invocation (the temporary runtime directory is removed by
the surrounding shell's exit trap):

```sh
XDG_RUNTIME_DIR="$cxho_runtime" DISPLAY=:99 EDIT_DISPLAY=:99 \
  EDIT_REFUSE_REAL_DISPLAY=1 ASAN_OPTIONS=detect_leaks=1 make -j4 check
```

Final result, including the final-source complete sanitizer `findui_test`
and `editor_test`, all requested display suites on the assigned display,
and the replay/display-lock shell contracts (M)[AC]. The binary count is the
Makefile's discovered suite count:

```text
findui_test: all passed
editor_test: all passed
check: 110 test binaries passed
display lock contract: owner PID, alias/timeout, make/scripts, nested inheritance, exit status PASS
final make -j4 check exit=0
Charging
 11:01:08 up 1 day, 13:04,  1 user,  load average: 0.46, 1.36, 1.93
Filesystem               Size  Used Avail Use% Mounted on
/dev/mapper/vgmint-root  1.8T  144G  1.6T   9% /
```

The lock contract intentionally emits child timeout/error diagnostics while
checking refusal and exit-status propagation; its own result and the outer
`make check` result above are successful. The final rebuilt sanitizer focus
selectors also passed separately with leak detection enabled, and the final
release selectors passed with the active malloc guard. No source changes
followed these final builds and checks.

Editor benchmark — exit 0, using the unchanged arguments from
`bench/editor_bench.args` plus `--file` naming a temporary small file containing
the regression's literal `alpha beta alpha` and a newline. The file and runtime
directory were removed by an exit trap. The argument file supplies the sample
count (E, inherited smoke workload); no new count or performance gate was
chosen. Timing samples are TRACK data only. The GL request fell back to raster
on the assigned display. Full timing output is in `build/cxho3-editor-bench.log`.
The standalone `findui_bench` requires its fixed external corpus; no external
corpus was created or changed for this focus/paint bead.

Adjacent benchmark power/load stamps (M)[AC]:

```text
Not charging
 10:41:56 up 1 day, 12:45,  1 user,  load average: 2.11, 2.10, 1.81
Not charging
 10:42:26 up 1 day, 12:45,  1 user,  load average: 1.73, 2.01, 1.79
```

Structural/allocation results, with counts derived from the checked-in
argument file and its alternating edit workload (M)[AC]:

```text
G1 null workload=serial keys=64 completed=64 arrivals_active=0 coalesced=0 foreign_input=0 allocations=0 mutations=64 journal=64 structural=OK mode=TRACK (M)[AC]
G1 raster workload=serial keys=64 completed=64 arrivals_active=64 coalesced=0 foreign_input=0 allocations=0 mutations=64 journal=64 structural=OK mode=TRACK (M)[AC]
G1 gl_fallback_raster workload=serial keys=64 completed=64 arrivals_active=64 coalesced=0 foreign_input=0 allocations=0 mutations=64 journal=64 structural=OK mode=TRACK (M)[AC]
```

## Remaining and findings outside scope

No focus or toggle work remains. During the document-edit regression, the
search match count and the editor's cached visible highlight ranges briefly
differed: cached ranges were empty after the document mutation while the
fixture's match count was still present.
The existing find-service/layout ordering is outside this focus bead; it was
not changed. The click-only regression verifies that transferring focus
preserves the panel's existing highlight ranges.
