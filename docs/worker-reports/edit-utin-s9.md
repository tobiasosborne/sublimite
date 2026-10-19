# edit-utin worker report: a peer reload that fails after Save As wrote the file is owed, not "save as failed"

The design is in docs/decisions/edit-utin.md. It builds on edit-e3z5, which
is not yet in this tree's base (see "Rebase onto edit-e3z5" below).

## Cause

`commands.c` `save_as_adopt` runs after the Save As write has landed. It stages
the new BASE on every buffer of the target path, which marks them saved_lost
and modified. It then reloads the clean peer tabs. With e3z5, a REFUSED reload
(JOURNAL_BUSY) became a clean, stale peer. Every other non-DONE outcome still
did `rc = EDITOR_ERR_IO`. That made `editor_save_as_tick` show "save as failed"
although the file was written, and left the peer modified with the overwritten
text. The outcomes are DEFERRED, NO_SLOT, and FAILED (for example, the written
target is unreadable).

Fix: every non-DONE outcome makes the peer clean and stale, exactly like
REFUSED. The existing clean refresh retries it, and if the cause persists the
stale prompt shows. Only REFUSED stops further reloads within the adoption.

## Files touched

- src/editor/commands.c (`save_as_adopt`): one line removed, one changed, and
  a comment.
- tests/dataloss_test.c:
  - `#include "editor/large.h"`.
  - `utin_save_as(UTIN_NO_SLOT | UTIN_UNREADABLE)` and `utin_deferred`.
  - Selector `--edit-utin`; the cases also run in the default run.
- docs/decisions/edit-utin.md and this report.
- Copied verbatim from .wt/edit-e3z5-s9 (see the next section):
  src/editor/buffers.c, src/editor/private.h, and the e3z5 parts of
  commands.c and dataloss_test.c.

## Rebase onto edit-e3z5 (duplicates to drop)

src/editor/buffers.c and src/editor/private.h are byte-identical to the
.wt/edit-e3z5-s9 versions. The whole diff of each against this branch's base
is e3z5's: the `editor_reload_outcome` enum, the new `editor_reload_source`
signature, the outcome reporting, the Reload notices and the re-arm. Take
main's copy of both files.

commands.c and dataloss_test.c are e3z5's versions plus the edit-utin hunks
below. Resolve them as "e3z5 as landed, plus":

- commands.c `save_as_adopt`, the peer-reload loop:
  - Four comment lines are appended to the e3z5 comment, starting with
    "edit-utin: the other outcomes".
  - The line `if (outcome != EDITOR_RELOAD_REFUSED) { rc = EDITOR_ERR_IO; break; }`
    is deleted.
  - `refused = true;` becomes `refused |= outcome == EDITOR_RELOAD_REFUSED;`.
- dataloss_test.c:
  - Line 13 is `#include "editor/large.h"`.
  - The block from "/* edit-utin (a)" through the end of `utin_deferred`,
    just before `main`.
  - The `--edit-utin` selector after `--edit-e3z5`.
  - One default-run line after the e3z5 line:
    `T(utin_save_as(UTIN_NO_SLOT) == 0); T(utin_save_as(UTIN_UNREADABLE) == 0); T(utin_deferred() == 0);`.
- Everything else in those two files is e3z5's (the `e3z5_*` helpers and tests,
  and the e3z5 `save_as_adopt` loop). It duplicates main once e3z5 lands.
  The edit-utin tests reuse `e3z5_buffer`, `e3z5_tab_holds` and `e3z5_tab_is`.

## Red run

Clang ASan/UBSan, `ASAN_OPTIONS=detect_leaks=1 build/san/tests/dataloss_test --edit-utin`,
with e3z5's code in place and before the commands.c change. BAT0 Full,
load 5.97.

```
dataloss_test: edit-utin save_as(no slot): notice=save as failed path_moved=1 owner_modified=0 peer_stale=0 peer_modified=1
dataloss_test:1365: FAIL !e->recovery_notice
dataloss_test: edit-utin save_as(unreadable): notice=save as failed path_moved=1 owner_modified=0 peer_stale=0 peer_modified=1
dataloss_test:1365: FAIL !e->recovery_notice
dataloss_test: edit-utin reload under a file save in flight: outcome=3
dataloss_test: edit-utin reload with staged journal ops: outcome=3
exit=1
```

The two `utin_deferred` lines cover existing branches, which were already
correct, so these tests were green from the start. Outcome 3 is
`EDITOR_RELOAD_DEFERRED`.

## Green run

Same command, BAT0 Full, load 6.08:

```
dataloss_test: edit-utin save_as(no slot): notice=(none) path_moved=1 owner_modified=0 peer_stale=1 peer_modified=0
dataloss_test: edit-utin save_as(no slot): peer refreshed once the cause cleared
dataloss_test: edit-utin save_as(unreadable): notice=(none) path_moved=1 owner_modified=0 peer_stale=1 peer_modified=0
dataloss_test: edit-utin save_as(unreadable): peer refreshed once the cause cleared
dataloss_test: edit-utin reload under a file save in flight: outcome=3
dataloss_test: edit-utin reload with staged journal ops: outcome=3
exit=0
```

## Verification

Box state: BAT0 Full, load 14.60 at the start and 8.39 at the end (shared
box). None of these runs is a timing claim. All sanitizer runs were made as
`XDG_RUNTIME_DIR=$(mktemp -d) DISPLAY=:99 EDIT_DISPLAY=:99 EDIT_DISPLAY_LOCK_TIMEOUT=3600 ASAN_OPTIONS=detect_leaks=1 sh tools/with_display_lock.sh <bin>`,
outside any sandbox.

| Command | Exit |
|---|---|
| `make -j3 all` (gcc 13, -Werror) | 0 (after renaming a test local that shadowed `journal`) |
| build/san/tests/dataloss_test (full default run, includes e3z5 and utin) | 0 |
| build/san/tests/editor_test | 0 |
| build/san/tests/editor_session_test | 0 |
| build/san/tests/savectl_test | 0 |
| build/san/tests/editor_checkpoint_test | 0 |
| build/tests/editor_test (release; malloc guard lines "guard=active", "malloc guard passed") | 0 |

## Remaining / out of scope (reported, not fixed)

1. **Other post-write failures still say "save as failed".** These also happen
   after the file is written:
   - With a journal, a failed BASE capture of the written target
     (`a->want_base && a->base_rc` in `editor_save_as_tick`), for example when
     the target is unreadable.
   - A failure inside `save_as_adopt` before the peers are reached:
     `savectl_create`, `tabs_rename`, `file_watch_path_start` or `epoll_ctl`.
   - A non-zero return from `editor_reload_source`.

   The owner then keeps its old binding, although the target holds its bytes.
2. **Repeated acquisition attempts.** A clean, stale buffer whose file stays
   unreadable is retried by the clean refresh on every maintenance turn
   (`editor_check_sources`). Each retry is a full `editor_buffer_prepare`
   acquisition on the UI thread. This predates the bead; it now also applies
   to such a Save As peer.
3. **The save-in-flight branch is reached only from tests.** The `file_save_busy`
   branch of `reload_source` can be reached only through the file-level save
   (`editor_large_save_begin`), and only tests call that.
