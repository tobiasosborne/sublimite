# edit-7cj8 landing report (s9)

Stamp at write time: battery `Full` ([AC]); ` 05:14:01 up  3:39,  1 user,  load average: 5,31, 7,28, 9,45`.
Suites ran with battery `Full` before and after.

## Rebase

- Worker tree committed by `build/briefs/land9.sh 7cj8 ...` as 72e907b on top of f2c8e5d (2mhd landed).
- land9.sh rebase onto main 1d7d29e (3620 landed) hit a conflict and aborted (rc=3).
- Manual `git rebase main`: one conflict, `src/file/file.h`, comment on
  `file_source_adopt_retired` only. Main (3620 Keep) widened the caller set to
  "a worker after its own replacement, or explicit Keep maintenance while the
  controller is idle"; this bead added "a save caller uses only its own
  successful rename; a read-only recovery checkpoint may use an observed
  external replacement on an independent lease that never authorizes writes".
  Resolution keeps both: main's caller list, then this bead's two sentences.
  No code changed.
- Auto-merged: buffers.c, private.h, retain.c, dataloss_test.c,
  editor_session_test.c, editor_test.c. Checked retain.c against edit-9fzc:
  its `if (editor_save_leased(e)) return;` precedes the whole tick, so this
  bead's detached-checkpoint path is also excluded under every save lease.
- Rebased commit: bf3c858 (bead commit) on main 1d7d29e.

## Commands and exit codes

| Command | Exit |
|---|---|
| `build/briefs/land9.sh 7cj8 "<msg>"` | 3 (rebase conflict, aborted) |
| `git rebase main` (conflict in file.h), resolve, `git rebase --continue` | 0 |
| `touch src/*/*.c src/*/*.h tests/*.c && make -j3 all` (gcc, -Werror) | 0 |
| `make -j3 build/san/tests/{editor_session,dataloss,editor_checkpoint,journal,editor,savectl,journal_retire,file}_test build/tests/editor_test` | 0 |

Each sanitizer suite ran outside the sandbox as
`XDG_RUNTIME_DIR=$(mktemp -d) DISPLAY=:98 EDIT_DISPLAY=:98 EDIT_DISPLAY_LOCK_TIMEOUT=3600 ASAN_OPTIONS=detect_leaks=1 sh tools/with_display_lock.sh build/san/tests/<name>`:

| Suite | Exit | End `uptime` |
|---|---|---|
| editor_session_test | 0 | 05:09:04 load 10.09 11.12 11.11 |
| dataloss_test | 0 | 05:09:43 load 8.60 10.60 10.94 |
| editor_checkpoint_test | 0 | 05:09:50 load 8.19 10.45 10.88 |
| journal_test | 0 | 05:10:02 load 8.10 10.35 10.85 |
| editor_test | 0 | 05:13:17 load 4.30 7.43 9.60 |
| savectl_test | 0 | 05:13:19 load 4.30 7.43 9.60 |
| journal_retire_test | 0 | 05:13:20 load 4.76 7.47 9.61 |
| file_test | 0 | 05:13:24 load 4.76 7.47 9.61 |

dataloss_test printed `edit-7cj8 detached checkpoint=durable`,
`detached recovered original=complete modified=1`, `copy window
checkpoint=durable`, `copy window recovered original=complete modified=1`;
journal_retire_test printed `partial copy live=protected dead=removed`.
No case needed a rerun.

Release malloc guard (`build/tests/editor_test`, same display lock):

| Mode | Exit | Output |
|---|---|---|
| EDITOR_ALLOC_ONLY=null | 0 | `null 10000 keys mallocs=0 guard=active` |
| EDITOR_ALLOC_ONLY=raster | 0 | `raster 10000 keys mallocs=0 guard=active` |

`df -h /` (M)[AC]: before 403G used / 54G avail (89%); after 403G used /
53G avail (89%). GB fixtures come from the suites' existing sparse
constructors and wrapper cleanup; no fixture directories were left in build/.

## Remaining limits (from the worker report)

A crash before source-change detection and durable publication still loses
the edits; a mapped original rewritten or truncated in place before retention
preserved it is unrecoverable (refused, not persisted wrong); native editor
bench rows were not measured.
