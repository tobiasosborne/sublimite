# edit-ck4 second rebase resolution onto main

Resolved all five conflict hunks in four files, retaining main's clipboard and
panel behavior together with ck4's asynchronous Ctrl+S, startup restore and
journal behavior. No `git` or `bd` command was run; rebase/index bookkeeping is
left to the caller.

The brief's `docs/worker-reports/edit-ck4.md` is absent from this worktree.
Reviewed the available ck4 final reports, prior resolution report,
`docs/decisions/edit-ck4.md`, `docs/worker-reports/edit-cde.md`, and the current
save/session/recovery and clipboard implementation instead.

## Resolution

- `src/editor/buffers.c`, buffer retirement: retain both the clipboard snapshot
  ownership check and the save controller's unresolved recovery-token check.
  Existing save-active/staged-journal guards, file/worker/index lifetime checks,
  session removal and controller destruction remain in place.
- `src/editor/buffers.c`, activation: consider both clipboard commands and the
  save journal lease busy, alongside view continuation and undo replay.
- `src/editor/editor.c`, queued input: retain the save-lease/staged-journal
  barrier before clipboard dispatch; while a clipboard command is busy, admit
  its clipboard completion events and park ordinary input.
- `tests/editor_test.c`, default suite: run all five clipboard checks once
  (the conflicted main side duplicated four checks), followed by ck4 save and
  startup probes. Existing redo/repeat, panel, selection, minimap and other ck4
  tests and focused argument dispatch remain intact.
- `src/editor/STATUS.md`: retain both clipboard and save/startup status entries.

Production edits are limited to the three C conflict hunks in `buffers.c` and
`editor.c`. No changes to `clip.c`, `input.c`, save/session/recovery modules,
panel implementation, Makefile or unrelated editor modules were needed.

## Validation

Both builds exited **0**, without warnings or errors:

```sh
make all
make build/san/tests/editor_test build/san/tests/journal_test build/san/tests/savectl_test
```

Release compilation used GCC; sanitizer compilation used Clang with
`-fsanitize=address,undefined -fno-omit-frame-pointer`. Both used
`-std=c11 -Wall -Wextra -Werror -Wshadow -Wconversion`. The resolved production
files and test file were actually recompiled in both configurations. Build
transcripts: [release](../../build/ck4-resolve2-logs/make-all.log),
[sanitizer](../../build/ck4-resolve2-logs/make-san.log).

Every test below used `ASAN_OPTIONS=detect_leaks=0`. The default editor run used
`DISPLAY=:99 EDIT_DISPLAY_LOCK_TIMEOUT=60` and the repository's shared display
lock; the lock was acquired and the suite actually ran. Focused editor probes
use the null backend and ran directly in separate processes. Tests ran after
both builds finished. No execution timeout or ASan/UBSan diagnostic occurred.

| Command | Exit | Result / transcript |
| --- | --- | --- |
| `sh tools/with_display_lock.sh build/san/tests/editor_test` | 1 | [Full suite invoked; native/launcher assertions fail](../../build/ck4-resolve2-logs/editor-all.log) |
| `build/san/tests/editor_test --ck4-save` | 0 | [Passed](../../build/ck4-resolve2-logs/editor-ck4-save.log) |
| `build/san/tests/editor_test --ck4-startup` | 0 | [Passed](../../build/ck4-resolve2-logs/editor-ck4-startup.log) |
| `build/san/tests/editor_test --clipboard` | 0 | [All five clipboard checks passed](../../build/ck4-resolve2-logs/editor-clipboard.log) |
| `build/san/tests/editor_test --redo-or-repeat` | 0 | [Passed](../../build/ck4-resolve2-logs/editor-redo-or-repeat.log) |
| `build/san/tests/journal_test` | 0 | [Passed](../../build/ck4-resolve2-logs/journal.log) |
| `build/san/tests/savectl_test` | 0 | [Passed](../../build/ck4-resolve2-logs/savectl.log) |

The [result ledger](../../build/ck4-resolve2-logs/results.tsv), per-command
`.exit` files and complete transcripts are retained in
`build/ck4-resolve2-logs/`.

### Full editor result

The default run passed selection chords, redo/repeat, panel notice/restoration,
all clipboard probes, ck4 save/startup, journal credit, session metadata, target
association, FIFO rejection, and COPY/mapped Keep/Reload recovery. Its headless
review checks also passed, including multi-record undo/redo, large journaled
deletion/undo, piece recycling, source-change handling, journal idle polling,
rollback and worker-owned backend initialization. No settle timeout occurred.

The failures were:

```text
reload launcher result=4
editor_test:1234: FAIL waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0
editor_test:625: FAIL editor_open(&e, &cfg, &b) == 0
editor_test:651: FAIL editor_open(&e, &cfg, &b) == 0
editor_test:933: FAIL editor_open(&e, &cfg, &backend) == 0
editor_test:1589: FAIL editor_open(&e, &cfg, &b) == 0
editor_test:2421: FAIL review_suite(NULL) == 0
```

The line-933 stderr assertion interleaves with stdout in the raw transcript;
it is separated above for readability. The four editor-open assertions are in
native order, native burst, native allocation and native close-flush tests.
A direct socket probe independently establishes this sandbox's X11 restriction:

```text
X11 :99 socket connect: errno=1: [Errno 1] Operation not permitted
```

See [environment probe](../../build/ck4-resolve2-logs/environment.log).
Launcher result 4 is `IPC_IO`; its underlying cause was not isolated during this
resolution. The prior ck4 resolution report records the same launcher and
native-open failures, but no pre-resolution baseline run is claimed here.
The top-level `review_suite(NULL)` assertion stops the default suite before
later index/backend/script checks. A full editor-suite pass is therefore **not
established**; native/launcher validation needs an environment where those
checks can complete.

### Focused editor output (each exit 0)

```text
P4-3 section 3: mode=COPY choice=Keep child abrupt exit; selected/unrelated/closed recovery exact
ck4 startup: abrupt exit, same-file discovery, recovered bytes/dirty state before first paint passed
clipboard malloc guard: 0 allocations (ASan-inert); expected zero by law
clipboard: null round trip, UTF-8/CRLF, replacement, cut/undo, alternate keys, PRIMARY passed
clipboard bulk: worker queue, cross-slice CRLF, replacement/undo, journal, mallocs=0 passed
clipboard middle click: deep horizontal position exact
clipboard PRIMARY: queued selection/collapse/middle paste waits for latest capture, mallocs=0 passed
clipboard middle click: no active tab preserves placeholder
editor_test: Ctrl+Y redoes inserts/deletes, empty redo is inert, Ctrl+Shift+Z still redoes
```

The save probe verifies replacement disk bytes, selected clean state, submitted
Saving acknowledgement, and exact replay of post-save edits and unrelated/closed
buffers after abrupt exit. Startup verifies same-file discovery without a
duplicate tab, recovered bytes and restored dirty state. The sanitizer malloc
guard is inert; these runs do not establish a release allocation measurement.

Module final summaries (each exit 0):

```text
journal_test: ok (roundtrip, corruption, torn pages, straddles, base conflict, rotation, allocator, back-pressure)
savectl_test: ok
```

### Marker verification

Both scans returned no matches after resolution:

```sh
rg -n '^(<<<<<<<|=======|>>>>>>>)($| )' src tests
rg -n '^(<<<<<<<|=======|>>>>>>>)($| )' --glob '!build/**' --glob '!.git/**' .
```

No rebase continuation or index operation was attempted.
