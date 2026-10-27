# edit-cxho.27 (s24): view_wrap_default decides on the basename

Worker: Opus, session 24, ThinkPad, display :95. Power: `Charging` (AC),
`uptime` 14:15:59 load 3.67 / 2.84 / 2.09 at the editor_test run. No timing is
claimed in this report.

## Change

`view_wrap_default` (src/view/view.c) now looks for the extension only in the
basename (after the last `/` or `\`), starting at the basename's second byte.
A name with no extension (README, notes, LICENSE, `.bashrc`) wraps, as
docs/decisions/edit-6090.md says it should; a dot in a directory name no
longer counts. The `.md`, `.txt`, `.tex` mapping, NULL/empty (untitled) =
wrap, and "every other extension does not wrap" are unchanged. Choices
(leading dot, `\` separator, trailing dot, restore and Save As behaviour) are
in docs/decisions/edit-cxho.27.md.

## Files touched

- src/view/view.c: `view_wrap_default` body (basename scan, no-extension case).
- src/view/view.h: comment on `view_wrap_default`.
- tests/view_test.c: new `c27_wrap_default` table (selector `c27`, also in the
  default run): README, notes, dir.v2/notes, a.b/c.md, a.b/c.c, .bashrc,
  NULL, "", the old wrap_test cases, `/home/u/proj.d/LICENSE`,
  `dir.md/code.c`, two Windows-style paths, `/tmp/.hidden/notes`.
- tests/editor_p4_test.c: `ipc_positions` config gets `.wrap_mode = -1` plus a
  comment (its extension-less temp file would now wrap; see Verification).
- tests/editor_test.c: comment only in `cxho25_typing_jump` (its `""`
  extension mode now opens wrapped; unwrapped coverage stays via `.c` and
  `wrap_mode -1`).
- docs/decisions/edit-cxho.27.md (new).

## Red run (test first, old implementation)

```
$ ASAN_OPTIONS=detect_leaks=1 build/san/tests/view_test c27
display guard: real display :0 refused, using :99
edit-cxho.27: view_wrap_default("README") = 0, want 1
edit-cxho.27: view_wrap_default("notes") = 0, want 1
edit-cxho.27: view_wrap_default("dir.v2/notes") = 0, want 1
edit-cxho.27: view_wrap_default(".bashrc") = 0, want 1
edit-cxho.27: view_wrap_default("/home/u/proj.d/LICENSE") = 0, want 1
edit-cxho.27: view_wrap_default("C:\Users\u\dir.v2\notes") = 0, want 1
edit-cxho.27: view_wrap_default("/tmp/.hidden/notes") = 0, want 1
tests/view_test.c:985: assertion failed: bad==0
Aborted (core dumped)
exit 134
```

(`a.b/c.md`, `a.b/c.c`, `dir.md/code.c` already passed before the fix because
`strrchr` finds the last dot; they guard the basename rewrite.)

## Green run

```
$ ASAN_OPTIONS=detect_leaks=1 build/san/tests/view_test c27
edit-cxho.27: wrap default decided on the basename; extension-less names wrap
exit 0
$ ASAN_OPTIONS=detect_leaks=1 build/san/tests/view_test | tail -3
wrap_selection: anchored Shift+Up/Down preserves soft-row affinity and preferred column
wrap_port: long visual-column continuation, anchored soft-end motion and cancellation passed
view_test: all passed
exit 0
$ ASAN_OPTIONS=detect_leaks=1 build/san/tests/wrap_test | tail -2
wrap_test: 10000 typing edits mallocs=0 guard=ASan-inert
wrap_test: all passed
exit 0
```

## Verification

```
$ make -j4 all                                   -> exit 0
$ XDG_RUNTIME_DIR=$(mktemp -d) DISPLAY=:95 ASAN_OPTIONS=detect_leaks=1 \
    sh tools/with_display_lock.sh build/san/tests/editor_test   -> exit 0
  edit-cxho.25: 15013 keys checked, 0 violations
  editor_test: all passed
$ XDG_RUNTIME_DIR=$(mktemp -d) DISPLAY=:95 EDIT_DISPLAY=:95 ASAN_OPTIONS=detect_leaks=1 make -j4 check
check: 112 test binaries passed
display lock contract: owner PID, alias/timeout, make/scripts, nested inheritance, exit status PASS
make check exit 0
```

Stamp: started 14:24:26, load 3.86 / 3.85 / 2.87; ended 14:37:05, load
1.67 / 2.22 / 2.71; power `Not charging` at the end (`Charging` at the start
of the session). The `make[1]: *** ... Error 1` and `display lock timeout`
lines in the log come from the display-lock contract test, which tests that
failure path on purpose and ends in PASS.

Two earlier full runs failed, and I am recording them:

1. Run 1 stopped at `dataloss_test:1839: FAIL trace_now_ns() < limit` (a
   wall-clock-bounded wait in the edit-utin Save As UNREADABLE case, load1
   about 4 with the other lane running). Rerun alone it passed (exit 0), and it
   passed in run 3. Not related to wrap: the case never reaches
   `view_wrap_default`'s result. Probably flaky under load.
2. Run 2 stopped at `editor_p4_test:482: FAIL editor_view(e).selection.cursor
   == 44 && editor_view(e).hscroll > 0`. This one **was caused by this bead**:
   `ipc_positions` opens a `mkstemp` file with no extension, which now opens
   wrapped, so it never scrolls horizontally. The case is about horizontal
   placement from an IPC `path:line:col`, so the test now sets
   `.wrap_mode = -1` (forced unwrapped), which restores the exact setup it had
   before. Afterwards editor_p4_test passed alone on :95 (exit 0) and in run 3.
   (The `cli_test: FAIL: window not created ... [forced test timeout]` line in
   run 2 is cli_test's own negative case and is followed by "§36 forced
   startup timeout correctly failed".)

## Callers checked

open (`src/editor/buffers.c` initial state, `view_wrap_file` at activation)
and preferences reload (`src/editor/preferences.c`, two sites) pass the path
straight through and get the new rule. Session restore uses the journaled
wrap flag (`src/editor/restore.c`, `JOURNAL_VIEW_WRAP`), so restored buffers
keep their saved mode. Save As does not re-evaluate wrap. Untitled stays
wrapped. `wrap_mode` still overrides.

## What remains

Nothing in scope.

## Outside scope (reported, not fixed)

- The match is case-sensitive, so `NOTES.MD` / `README.TXT` do not wrap.
  Unchanged on purpose (brief: keep mappings unchanged).
- Dotfiles such as `.bashrc` now wrap (brief's default). Sublime's `auto`
  would not, because it picks Bash syntax for them; if that matters in use it
  is a one-line exclusion.
- Save As from `code.c` to `notes.md` (or the reverse) keeps the old wrap
  mode (pre-existing behaviour, not checked against Sublime). No change made.
