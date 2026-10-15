# edit-n95 (S7): find in files UI, worker report

Bead: edit-n95 (edit-338 part 2). Worktree `.wt/edit-n95-s7`. Design choices:
`docs/decisions/edit-n95.md`. Evidence files: `docs/worker-reports/edit-n95-evidence/`.

## What landed

- **Engine fixes (src/fif), each with its own red test in tests/fif_test.c (`n95_fixes`):**
  1. Symlinked project root is walked (`stat` on the root; entries below keep the lstat skip).
  2. `io_error` only counts `DONE.errors`; `FIF_ERR_IO` only when the root itself cannot be walked
     (`root_error`). The results tab shows `N unreadable entries`.
  3. `fif_context_cut`: a context cut by the 1024-byte cap backs off to a UTF-8 boundary (at most
     three bytes, (P) four-byte scalar maximum); the UI appends Sublime's ` ...`.
     `fuzz/fif_fuzz.c` asserts the cut's invariants.
- **Panel:** Ctrl+Shift+F opens the find panel in find-in-files mode (three rows: Find + toggles,
  `Where:`, hints). Tab cycles Find / Where; Where edits (text, BackSpace/Delete, arrows,
  Home/End, Ctrl+A, Ctrl+V); default Where is the active file's directory or the cwd. Toggles
  map 1:1 to `fif_options`. Return runs the search on WORK_BULK; Escape with nothing running
  closes the mode.
- **Results tab:** a single `Find Results` tab per editor, its buffer swapped for a fresh one per
  search. The new `editor_buffer.read_only` flag refuses typing, delete, line edits, undo/redo,
  cut/paste (key, middle-click, native reply), find-panel replace, as no-ops that latch no error.
  The `derived` flag keeps it out of the journal, checkpoints, TABS/VIEW metadata, session tree
  and save. Text is assembled on the UI thread from the job's stored results, within
  `WORK_DRAIN_BUDGET_NS` per turn, through `piece_insert` plus the shared `changed()`
  bookkeeping (`editor_results_append`). Sublime format: `<path>:`, `    N: context` lines, blank
  line between files, `N matches across M files` plus warning rows (failed, truncated,
  cancelled, unreadable entries). Match ranges use the find-match highlight.
- **Navigation:** Enter or double-click on a hit line opens the file at that line with the caret on
  the hit's byte (a byte-column mode of `position_buffer`, set only by find in files); a header
  line opens the file at line 1. F4 / Shift+F4 step through hits (from the caret in the results
  tab, otherwise from the last visited hit) and wrap.
- On Return the in-buffer finder (findui) stops tracking (`findui_show(false)`), so it does not
  re-search the results buffer on the single bulk lane after every append; Ctrl+F/F3 reopen it.
- **Lifecycle:** current + retiring job slots. A new search cancels a live one, keeps draining,
  ignores stale generations (including the companion DONE) and disposes it once finished.
  Escape or Ctrl+Shift+F during a search cancels it; the partial buffer stays and gets
  `Search cancelled`. `editor_find_close` drains and disposes both slots before the pool goes.
- `KEYS_ARG_UNIMPLEMENTED` cleared for Ctrl+Shift+F, F4, Shift+F4 (keys.c, keys_test.c).

## Files touched

- src/fif/worker.c, src/fif/fif.c, src/fif/fif.h, fuzz/fif_fuzz.c, tests/fif_test.c
- src/editor/fif.c (new), src/editor/private.h, src/editor/findpanel.c, src/editor/input.c,
  src/editor/buffers.c, src/editor/editor.c, src/editor/checkpoint.c, src/editor/session.c,
  src/editor/clip.c
- src/keys/keys.c, tests/keys_test.c
- tests/editor_test.c (new `n95_find_in_files`, `--n95-fif`, added to the main suite; the
  panel-shortcut notice case drops Ctrl+Shift+F, which is now implemented)
- docs/decisions/edit-n95.md, this report, docs/worker-reports/edit-n95-evidence/*

## Red runs

fif_test, one fix at a time (each run fails at the next fix's assertion):

```
# fif-red-1-symlink-root.txt (before fix 1): symlinked root run
tests/fif_test.c:43: assertion failed: got->done.code == FIF_OK && !got->done.cancelled
Aborted (core dumped)   EXIT 134
# fif-red-2-eacces.txt (fix 1 in, before fix 2): gdb puts it at n95_fixes line 207, the EACCES run
tests/fif_test.c:43: assertion failed: got->done.code == FIF_OK && !got->done.cancelled
Aborted (core dumped)   EXIT 134
# fif-red-3-utf8.txt (fixes 1-2 in, before fix 3)
tests/fif_test.c:227: assertion failed: view.context_truncated && view.context_length == 1024u - 1u
Aborted (core dumped)   EXIT 134
```

editor_test (state fields added, no behaviour yet):

```
# editor-red.txt
editor_test:4692: FAIL e->find_visible && e->fif.mode
EXIT 1
```

## Green runs

```
# fif-green.txt (release)
fif_test: filters, search, walker, cancellation, truncation, retry and allocation guard passed
EXIT 0
# editor-green.txt (release, ./build/tests/editor_test --n95-fif)
edit-n95 find in files: panel, results format, open at hit, F4 cycle, cancel, read-only passed
EXIT 0
# editor-san-n95.txt (ASan/UBSan, leaks on, display lock, private XDG_RUNTIME_DIR)
edit-n95 find in files: panel, results format, open at hit, F4 cycle, cancel, read-only passed
EXIT 0
```

The editor test covers the brief's (a)-(f): (a) exact Sublime-format text built from the
tree_fixture.h literals, plus the highlight range of the first hit; (b) Enter on the "scatter"
line opens nested/b.c with the caret at byte 5; (c) F4 x3 (b.c, skip.txt, wrap to a.txt) and
Shift+F4 x2; double-click opens a.txt at its hit; (d) Escape after the engine blocks on a full
mailbox (the `dropped_full` handshake from fif_test) leaves `0 < stored < total` hit lines and
`Search cancelled`; a live search replaced by a new one retires, is disposed, and the new
complete result replaces the tab; (e) q, BackSpace, Delete, Ctrl+Z, Ctrl+Shift+D, Ctrl+X change
neither text nor revision and latch no error; (f) the counted ingress/submit guard sees zero
allocations through the whole case.

## Test commands run (exit codes)

- `make -j4 all` (gcc 13 release): 0
- `./build/tests/fif_test`: 0; `./build/tests/editor_test --n95-fif`: 0; `--panel-notice`: 0
- Release `editor_test` full suite under `DISPLAY=:99 sh tools/with_display_lock.sh` with a
  private XDG_RUNTIME_DIR: 0 (`editor_test: all passed`; G1/typing guards print `mallocs=0`,
  e.g. `null 10000 keys mallocs=0 guard=active`, `edit-g11 null typing ... mallocs=0`)
- Sanitizer binaries, one at a time, display lock + private XDG_RUNTIME_DIR: fif_test 0,
  keys_test 0, config_find_test 0, tabs_test 0, findui_test 0, editor_test `--n95-fif` 0,
  editor_test full suite 0
- `build/fuzz/fif_fuzz -runs=200000 -max_len=12288`: 0 (200000 runs)
- `make check`: 0 (`check: 97 test binaries passed`, replay CLI and display-lock contract PASS).
  It ran before the last change (findui stops tracking on Return; results highlight ranges are
  cleared when another tab is active); after that change `make -j4 all`, the release and the
  sanitizer `editor_test` full suites were rebuilt and rerun: 0, 0, 0.
- Bench (module touched): `build/bench/fif_bench`, stamped BAT0=Not charging, AC online=1,
  load1=4.34 before / 3.39 after: `G6f_walk_search_tracked n=100 p50=413106241 p99=663115121`
  ns against the (E) tracked aspiration p50=27179092 / p99=42467332, `MISS` (a note, not a
  blocker; the box carried other sessions' load). `fif-bench.txt`.

## What remains

Nothing required by the brief is missing. Follow-ups (out of scope, reported): replace in files;
a project/folder sidebar; command palette; `~` and Sublime project-variable expansion in Where;
Sublime's optional context lines and `Searching N files` header; selecting the whole match on
open (the caret lands on its first byte); showing a job-init failure (e.g. a Where filter syntax
error) in the results tab instead of only counting the refusal.

## Found outside the bead (reported, not fixed)

- `docs/decisions/edit-34l.md` says contexts are "truncated from the beginning"; the engine always
  kept the start of the line (as Sublime does). The n95 decision doc records the actual rule; the
  34l doc is unchanged.
- tests/fif_test.c leaves its `/tmp/fif-tree-*` directory behind when an assertion aborts (no
  cleanup on failure). My aborted red runs' trees were removed by hand;
  `/tmp/fif-tree-Td2AiU` (2026-10-16 00:22, before this session) is someone else's and was left.
- Pre-existing: once findui is closed (state.open false) and the panel hidden,
  `editor_find_service` returns early without clearing `e->find_range_count`, so stale ranges
  could paint on the next buffer. Before this bead findui never closed after its first open; n95
  closes it and guards its own ranges (`ranges_owned`), but the general path is unchanged.
- The Find row's status shows findui's in-buffer count in find-in-files mode, not a fif count.
