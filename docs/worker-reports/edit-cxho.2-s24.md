# edit-cxho.2 — worker report, session s24

Implemented the coordinator's field-owned-key rule. Window chords now dispatch
with the panel open; menu/palette commands bypass physical field interception.
The palette overlays the retained panel. Tab commands start the existing source
reset before returning, refreshing counts/highlights for the new active buffer,
including the empty buffer after the last close. Modified closes retain the
existing save/discard/cancel prompt. Field copy/cut use the existing clipboard
arena and never mutate the document. Ctrl+Z/Y remain field no-ops because the
field has no undo history.

Files touched:

- `src/editor/input.c`: physical field routing, removal of the common-command
  whitelist, source reset after tab commands.
- `src/editor/findpanel.c`: explicit field ownership, field copy/cut, undo/redo
  isolation and preserved printable/clipboard/navigation handling.
- `src/editor/clip.c`, `src/editor/private.h`: field-copy entry point using the
  existing clipboard allocator/publication path.
- `src/editor/palettepanel.c`: preserve the underlying panel while the palette
  is open and dispatch commands through the normal action entry point.
- `tests/findui_test.c`: reverse the old consumed-command list; injected close,
  reopen, MRU/adjacent/digit switching, save, modified-close prompt, go-to-line,
  menu and palette close, typing/clipboard/undo isolation and allocation checks.
- `tests/editor_test.c`: reverse the palette-hides-find expectation and verify
  retained panel state after palette Save As and mutation-driven dismissal.
- `docs/decisions/edit-cxho.2.md`: coordinator rule and implementation choices,
  superseding the find-chord part of edit-o20.
- `docs/worker-reports/edit-cxho.2-s24.md`: this report.

All captured results below are measured (M)[AC]; AC was verified from the
adapter's online status while BAT0 reported `Not charging`. Exit statuses,
assertion source locations and test/command identifiers are observations, not
new budgets. Python subprocess status `-6` means SIGABRT (P: POSIX signal-status
convention); the harness itself finishes after recording each case. Make
parallelism follows the brief. New test expectations derive from fixture
strings and explicit operations; geometry/watchdogs reuse the existing panel
fixture. Allocation expectation is zero (P), from the binding no-malloc rule.

## Red, before implementation

`make -j4 build/san/tests/findui_test` and
`make -j4 build/san/tests/editor_test` each built successfully (M)[AC], exit 0.
The following injected cases then failed against the original implementation:

```text
$ ASAN_OPTIONS=detect_leaks=1 EDIT_CXHO_PANEL=field build/san/tests/findui_test
tests/findui_test.c:990: assertion failed: state.query_length==strlen(text) && state.query_cursor==cursor
exit -6
$ ASAN_OPTIONS=detect_leaks=1 EDIT_CXHO_PANEL=close build/san/tests/findui_test
tests/findui_test.c:1169: assertion failed: tabs_count(&e->tabs)==1 && e->buffer->id==original
exit -6
$ ASAN_OPTIONS=detect_leaks=1 EDIT_CXHO_PANEL=switch build/san/tests/findui_test
tests/findui_test.c:1130: assertion failed: e->buffer->id==original
exit -6
$ ASAN_OPTIONS=detect_leaks=1 EDIT_CXHO_PANEL=save build/san/tests/findui_test
tests/findui_test.c:1190: assertion failed: !editor_tab(e,0)->modified && e->buffer->revision==revision
exit -6
$ ASAN_OPTIONS=detect_leaks=1 EDIT_CXHO_PANEL=goto build/san/tests/findui_test
tests/findui_test.c:1146: assertion failed: e->go.active && e->go.kind==KEYS_ACTION_GOTO_LINE && e->find_visible
exit -6
$ ASAN_OPTIONS=detect_leaks=1 EDIT_CXHO_PANEL=menu build/san/tests/findui_test
tests/findui_test.c:1169: assertion failed: tabs_count(&e->tabs)==1 && e->buffer->id==original
exit -6
$ ASAN_OPTIONS=detect_leaks=1 EDIT_CXHO_PANEL=palette build/san/tests/findui_test
tests/findui_test.c:1160: assertion failed: e->palette_open && e->find_visible
exit -6
```

The existing editor palette test was reversed before implementation too:

```text
$ ASAN_OPTIONS=detect_leaks=1 XDG_RUNTIME_DIR=$(mktemp -d) DISPLAY=:99 sh tools/with_display_lock.sh build/san/tests/editor_test --ywd-palette
editor_test:4134: FAIL e->find_visible
==4==LeakSanitizer has encountered a fatal error.
==4==HINT: For debugging, try setting environment variable LSAN_OPTIONS=verbosity=1:log_threads=1
==4==HINT: LeakSanitizer does not work under ptrace (strace, gdb, etc)
exit 1
```

The assertion failure precedes the sandbox's LeakSanitizer error. Successful
sanitizer reruns below used the approved execution outside the ptrace-restricted
sandbox, preserving `ASAN_OPTIONS=detect_leaks=1`.

While bringing the tests green, close fixtures were corrected to use saved
temporary files: nonempty untitled buffers correctly require the existing
prompt. Save assertions now explicitly drain the asynchronous save before
checking clean state. The modified-tab case verifies that the prompt opens,
Escape cancels it without closing the panel, and S saves and closes. A document
with real undo history verifies that field Ctrl+Z/Y do not undo or repeat it.
The original red output above is preserved verbatim.

## Green

Final `make -j4 build/san/tests/findui_test build/san/tests/editor_test`:
exit 0 (M)[AC]. Full sanitizer find suite:

```text
$ ASAN_OPTIONS=detect_leaks=1 build/san/tests/findui_test
edit-m15 find panel: clipboard/query chords isolated from document passed
edit-cxho.2 find panel: close passed
edit-cxho.2 find panel: switch passed
edit-cxho.2 find panel: goto passed
edit-cxho.2 find panel: menu passed
edit-cxho.2 find panel: palette passed
edit-cxho.2 find panel: save revision/modified state and close prompt passed
findui_test: all passed
exit 0
```

Full sanitizer editor suite, using the assigned display and shared lock:

```text
$ ASAN_OPTIONS=detect_leaks=1 XDG_RUNTIME_DIR=$(mktemp -d) DISPLAY=:99 sh tools/with_display_lock.sh build/san/tests/editor_test
editor_test: all passed
exit 0
```

Release `build/tests/findui_test` also passed (M)[AC], exit 0, with the active
malloc interposer rather than the sanitizer's inert guard:

```text
findui_test: all passed
```

This covers the input-to-frame allocation guard while typing the query and
field clipboard/editing operations. Complete logs are in
`build/edit-cxho.2/findui-san.log`, `editor-san.log`, `findui-release.log`,
`green.log` and `green-editor-palette.log`.

## Verification

- `make -j4 all`: exit 0 (M)[AC], GCC release flags include C11, Wall, Wextra,
  Werror, Wshadow and Wconversion. An initial build returned exit 2 (M)[AC]
  because the new test local `file` shadowed the existing typedef; renamed it
  `file_menu`. Initial diagnostic: `build/edit-cxho.2/all-first.log`.
  Successful build: `build/edit-cxho.2/all.log`.
- Final sanitizer build and full find/editor runs above: exit 0 (M)[AC],
  ASan/UBSan with leak detection enabled. `:99` was reachable.
- Existing tracked editor benchmark: exit 0 (M)[AC]. It used the repository's
  `bench/editor_bench.args`, with a small worktree-local copy of the test source
  as the fixture (`--file=build/edit-cxho.2/bench-fixture.c`). No new sample
  budget was chosen. All backends reported `allocations=0` and `structural=OK`
  (M)[AC]. GL was unavailable and used the existing raster fallback. This was
  tracked data, not a new performance gate. Command and power/load stamp:

```text
$ cat /sys/class/power_supply/BAT0/status
Not charging
$ uptime
 10:04:59 up 1 day, 12:08,  1 user,  load average: 1.25, 2.42, 1.87
$ EDIT_REFUSE_REAL_DISPLAY=1 XDG_RUNTIME_DIR=$(mktemp -d) DISPLAY=:99 sh tools/with_display_lock.sh build/bench/editor_bench --track --no-idle --no-p4 --serial-only --keys=64 --file=build/edit-cxho.2/bench-fixture.c
```

Timing details and their built-in measurement tags are retained in
`build/edit-cxho.2/editor-bench.log`. The sample count follows the existing
benchmark argument file (P: direct reuse), and byte position follows the copied
fixture's length (P).

Final full check completed (M)[AC], exit 0. The wrapper exported
`ASAN_OPTIONS=detect_leaks=1`, `EDIT_REFUSE_REAL_DISPLAY=1`, `DISPLAY=:99`
and a fresh `XDG_RUNTIME_DIR` removed by an exit trap. It ran `make -j4 check`;
the Makefile runs display suites under `tools/with_display_lock.sh`.
Verbatim summary and disk/power stamps (M)[AC]; disk use includes the release
and sanitizer build products, not a new large fixture:

```text
$ cat /sys/class/power_supply/BAT0/status
Not charging
$ uptime
 10:05:26 up 1 day, 12:08,  1 user,  load average: 1.02, 2.27, 1.83
$ df -h /
Filesystem               Size  Used Avail Use% Mounted on
/dev/mapper/vgmint-root  1.8T  138G  1.6T   8% /
$ make -j4 check
check: 110 test binaries passed
display lock contract: owner PID, alias/timeout, make/scripts, nested inheritance, exit status PASS
$ cat /sys/class/power_supply/BAT0/status
Not charging
$ uptime
 10:17:31 up 1 day, 12:20,  1 user,  load average: 1.16, 1.37, 1.59
$ df -h /
Filesystem               Size  Used Avail Use% Mounted on
/dev/mapper/vgmint-root  1.8T  139G  1.6T   8% /
make check exit 0
```

Full output: `build/edit-cxho.2/check.log`. The display-lock contract deliberately
exercises timeout diagnostics; its final PASS and make's successful exit are
included above. All required verification is complete. No implementation work
remains for this bead.

## Remaining and outside scope

Click-to-document focus transfer remains with edit-cxho.3, as instructed.
Field undo history and Alt+Enter find-all are absent already; their chords stay
field no-ops. No new review pass or unrelated fix was performed. GL availability
on the assigned Xvfb display and the sandbox LeakSanitizer restriction were
observed during verification; neither required product changes. No new GB-scale
fixture was introduced. Small temporary close/save files from failed red runs
were removed. The installed editor and profile were not touched; no bd or git
command was run, and no main-checkout/other-worktree files were changed.

Scope-discovery lapse: the initial `find .. -name AGENTS.md -print` traversed
parent-directory metadata outside the assigned worktree. It made no edits;
subsequent searches stayed inside the worktree. The initial research text search
also printed the research's existing empty-tab minimap finding alongside find
routing matches. That unrelated minimap issue was not investigated or fixed;
the last-tab regression here checks find source/count/highlight state only.
