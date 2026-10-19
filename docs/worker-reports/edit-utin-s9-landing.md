# edit-utin landing report (s9)

The worker tree branched before e3z5 and 9fzc landed. It was rebased once onto
main de0a959, which is "Session 19: worklog 06:50 (e3z5 landed)". Main was not
fast-forwarded. The rebased bead commit is aa82541.

## What survived

- **(a) Survived.** A peer reload that leaves the peer in place after the Save
  As write produced "save as failed" on main. That covers NO_SLOT, an
  unreadable target, and DEFERRED.
  - On main, `save_as_adopt` calls `editor_reload_saved_peer` (edit-9fzc,
    deferred checkpoint), so REFUSED can no longer occur there.
  - Main still had `if (!rc && !other->retired) rc = EDITOR_ERR_IO;`.
  - Red run on main's commands.c with the adapted tests:
    `notice=save as failed ... peer_stale=0 peer_modified=1` for both
    no slot and unreadable, rc=1.
- **(b) Survived, adapted.** The two DEFERRED branches (`file_save_busy`,
  `op_count`) of the static `reload_source` still exist. They are reached
  through `editor_reload_saved_peer`, the only exported entry. A deferral now
  shows as rc 0 with the buffer neither retired nor replaced. The `outcome`
  enum is not exported for this caller.
- **(c) Nothing else.** The worker's buffers.c, private.h and e3z5 hunks were
  e3z5's pre-landing shape and are dropped.

## How resolved

- **src/editor/buffers.c, src/editor/private.h:** main's version, so no diff
  against main.
- **src/editor/commands.c:** main's `save_as_adopt`. In the peer loop,
  `if (rc || other->retired) continue;`. A peer left in place is made clean
  (`saved_lost = false`, tab not modified), stale (`source_stale = true`,
  `prompt_ns = 0`) and republished. This is utin's rule without the `refused`
  latch, which has no case on main. A non-zero rc is still `EDITOR_ERR_IO`.
- **tests/dataloss_test.c:** main's ladder plus utin's cases, with
  `#include "editor/large.h"`, the `--edit-utin` selector and a default-run
  line.
  - `utin_save_as` now also checks `publish_owed || publish` after the
    NO_SLOT (journaled) adoption.
  - `utin_deferred` calls `editor_reload_saved_peer`. Its final DONE reload
    asserts the buffer was replaced with `publish_owed` set, then waits for the
    publication.
- **docs/decisions/edit-utin.md:** a "Rebase onto edit-e3z5 and edit-9fzc
  (landing, s9)" section was appended.
- **Procedure note.** I resolved the conflicts with
  `git checkout --ours -- <file>`, which is main's side during a rebase. One
  mistaken `git checkout --theirs -- buffers.c private.h` came first and was
  immediately overwritten by `--ours`. The final diff of both files against
  main is empty.

## Commands (BAT0 Full throughout)

| Command | rc | uptime load (1m) |
|---|---|---|
| `build/briefs/land9.sh utin "<msg>"` | 3 (REBASE CONFLICT in buffers.c, commands.c, private.h, dataloss_test.c; aborted by script) | |
| `git rebase main` (manual), resolved, `git rebase --continue` | 0 | |
| red: `make -j3 build/san/tests/dataloss_test` with main's commands.c | 0 | 5.60 (05:50) |
| red: `dataloss_test --edit-utin` (san, display lock) | 1 (no slot, unreadable: "save as failed") | 9.52 (05:51) |
| green: same with the fix | 0 | 8.82 (05:51) |
| `touch ...; make -j3 all` (gcc 13, -Werror) | 0 | 7.64 (05:52) |
| `make -j3 build/san/tests/{dataloss,editor_session,editor}_test` | 0 | 6.72 (05:54) |
| build/san/tests/dataloss_test (full default run) | 0 | 6.50 -> 5.34 (05:54-05:55) |
| build/san/tests/editor_session_test | 0 | 5.34 -> 5.22 (05:55) |
| build/san/tests/editor_test | 0 | 5.22 -> 2.36 (05:55-05:58) |
| `EDITOR_ALLOC_ONLY=null build/tests/editor_test` | 0 ("null 10000 keys mallocs=0 guard=active") | 2.23 (05:58) |
| `EDITOR_ALLOC_ONLY=raster build/tests/editor_test` | 0 ("raster 10000 keys mallocs=0 guard=active") | 2.23 (05:58) |

All sanitizer runs used `XDG_RUNTIME_DIR=$(mktemp -d) DISPLAY=:98 EDIT_DISPLAY=:98
EDIT_DISPLAY_LOCK_TIMEOUT=3600 ASAN_OPTIONS=detect_leaks=1 sh
tools/with_display_lock.sh <bin>`, outside any sandbox. No failing case needed
a rerun. None of these runs makes a timing claim.

## Unresolved

Nothing in this bead's scope. Items 1 to 3 of the worker report's "Remaining"
list still apply on main and were not touched:

1. Other post-write failure paths still say "save as failed".
2. A clean, stale peer whose file stays unreadable is retried on every
   maintenance turn.
3. The `file_save_busy` deferral is reachable only from tests.
