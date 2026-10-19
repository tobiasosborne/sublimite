# edit-14bi landing report (s9)

Rebased worker commit: `9e5d10d` onto main `6b0cfe7` (Session 19: worklog 03:20, 2mhd landed).
All measurements (M) on this shared box; BAT0 read `Charging` until 03:43, then `Full` (AC throughout).
Stamp at report time: `Full`, ` 03:45:33 up  2:10,  1 user,  load average: 33,50, 30,07, 26,29`.

## Commands and exit codes

| Command | rc |
|---|---|
| `build/briefs/land9.sh 14bi "<msg>"` (commit 437f9ca, rebase) | 3 (REBASE CONFLICT, script aborted rebase) |
| `git rebase main` (manual) | conflict in tests/editor_session_test.c |
| resolve, `git add`, `git rebase --continue` | 0 -> 9e5d10d |
| `touch src/*/*.c src/*/*.h tests/*.c; make -j3 all` (gcc, -Werror) | 0 |
| `make -j3 build/san/tests/{editor_session,editor_checkpoint,dataloss,journal,editor}_test build/tests/editor_test` | 0 |
| san editor_session_test | 0 |
| san editor_checkpoint_test | 0 |
| san dataloss_test | 0 |
| san journal_test | 0 |
| san editor_test | 0 |
| release editor_test, EDITOR_ALLOC_ONLY=null | 0, `null 10000 keys mallocs=0 guard=active` |
| release editor_test, EDITOR_ALLOC_ONLY=raster | 0, `raster 10000 keys mallocs=0 guard=active` |

Every suite ran as `XDG_RUNTIME_DIR=$(mktemp -d) DISPLAY=:95 EDIT_DISPLAY=:95 EDIT_DISPLAY_LOCK_TIMEOUT=1800 ASAN_OPTIONS=detect_leaks=1 sh tools/with_display_lock.sh <bin>`, outside the sandbox (LeakSanitizer active). No sanitizer or leak diagnostics. No case failed, so none was rerun.

Key lines from editor_session_test: `edit-4jhi: target=rewritten acquisition=COPY length=grown checkpoint relaunch preserved BASE edits and TARGET` (the 14bi case); the MMAP/grown and COPY/unchanged cases pass too; `edit-uzrb: Ctrl+S admitted during pending open; saved bytes and clean tab verified` (both variants).

## Conflict and resolution

The only conflict was the main() selector ladder in `tests/editor_session_test.c`: main (uzrb) added `--edit-uzrb` plus `T(pending_open_save(false/true) == 0)`, and 14bi added `--edit-14bi`. I kept both. The `--edit-14bi` selector sits after `--edit-uzrb` and before the full-run `T()` lines, so every early-return selector still precedes the full run. `src/editor/buffers.c` auto-merged: the 14bi change is the one-line adoption condition `MMAP || lineidx_len(b->index) != piece_len(b->tree)` in `editor_adopt_open`. There was no semantic conflict.

## Disk (df -h /) and stamps

| Run | before | after |
|---|---|---|
| editor_session_test | 03:29:35 Charging, 393G used / 63G avail | 03:32:08, 394G / 62G |
| editor_checkpoint_test | 03:32:08, 394G / 62G | 03:32:34, 394G / 62G |
| dataloss_test | 03:32:34, 394G / 62G | 03:38:01, 395G / 61G |
| journal_test | 03:38:01, 395G / 61G | 03:38:35, 395G / 61G |
| editor_test | 03:38:35 Charging, 395G / 61G | 03:43:59 Full, 393G / 64G |
| guard null | 03:44:11 Full, 392G / 64G | 03:44:35, 393G / 64G |
| guard raster | 03:44:35, 393G / 64G | 03:45:14, 393G / 64G |

Load average was 19-34 throughout (shared box, other landers). After the runs, `find build -maxdepth 1 -type d \( -name 'editor-*' -o -name 'dataloss-*' -o -name 'journal-*' \)` printed nothing and there were no leftover `build/*.c` fixtures. Disk drift comes from other activity on the box; the suites' fixtures are sparse and were removed.

## Unresolved

None.
