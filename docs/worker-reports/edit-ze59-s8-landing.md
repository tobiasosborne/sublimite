# edit-ze59: landing report (s8)

Lander session 2026-10-20. Worktree .wt/edit-ze59-s8, branch wt/edit-ze59-s8.
Main was not touched; the coordinator fast-forwards.

Stamps: rebase/build at 02:15, `Charging`, `up 40 min, load average: 7,51, 4,00, 1,94`;
suites 02:25-02:53, `Charging`, load1 41.3 -> 11.4 at each suite start (others
share the box and :95); end `up 1:18, load average: 22,63, 17,24, 19,68`.

## Rebase

- Worker commit 6a242be (base f9d23dc = edit-ivv) rebased onto main 69647e4.
- `build/briefs/land8.sh ze59`: rc=1 ("nothing to commit": the tree was already
  committed as 6a242be; the script exits before rebasing). Rebased by hand.
- `git rebase main`: two conflicts, both additive, resolved by keeping both sides:
  - src/editor/private.h: main's edit-9ev6 `editor_retain_quiesce` prototype vs
    ze59's `editor_publish_*` / `editor_save_as_*` prototypes. Kept both.
  - tests/dataloss_test.c default run: main's `T(retain_close() == 0);` vs ze59's
    `T(ze59_retain_stall() == 0); T(ze59_save_as() == 0);`. Kept both.
- checkpoint.c, retain.c, session.c auto-merged. Checked by hand:
  - Close order (open.c `editor_close`): `editor_flush_settle` ->
    `editor_session_close` (waits for the publish job and the Save As write,
    ze59) -> `editor_retain_quiesce` (9ev6) -> `journal_close` -> journal pool
    shutdown. The worker checkpoint finishes before retention quiesces and
    before the journal closes.
  - edit-3f2k: `editor_session_metadata` now gives leaves dense recorded ranks,
    which ze59's plain capture uses for TABS; `editor_publish_start`'s live
    count (placeholder/derived skipped) equals `editor_recorded_rank(e,
    tabs_count)`. The decision doc's known limit (JOURNAL_INVALID with a Find
    Results tab before others) is therefore addressed by 3f2k on this base; no
    dedicated test for that combination was added.
  - edit-4jhi (restore.c, save setup in session.c) and edit-fkpk (savectl)
    touch paths ze59 does not edit textually; covered by the suites below.
- The worker's commit message ("worker-complete snapshot ... NOT yet rebased")
  was replaced with a bead message during the conflicted step.
- Rebased commit: 83cbb89 (edit-ze59 (recovery-3 F10) ... (bd-ze59)).

## Commands and exit codes

| command | rc |
|---|---|
| `touch src/*/*.c src/*/*.h tests/*.c; make -j3 all` (gcc, -Werror) | 0 |
| `make -j3 build/san/tests/{editor,editor_session,editor_checkpoint,dataloss,journal,savectl}_test` | 0 |
| san dataloss_test (02:25-02:32) | 0 |
| san journal_test | 0 |
| san savectl_test | 0 |
| san editor_checkpoint_test | 0 |
| san editor_session_test | 0 |
| san editor_test | 0 ("editor_test: all passed") |
| release build/tests/dataloss_test | 0 |
| release build/tests/editor_test (malloc guard) | 0 ("editor_test: all passed") |

Every test ran as `XDG_RUNTIME_DIR=$(mktemp -d) DISPLAY=:95 EDIT_DISPLAY=:95
EDIT_DISPLAY_LOCK_TIMEOUT=1800 ASAN_OPTIONS=detect_leaks=1 sh
tools/with_display_lock.sh <binary>`. No case failed, so no rerun was needed.

## ze59 lines (M), this box as it was

- release: stall longest UI turn 0.188 ms; keystroke under stalled journal
  worker + READY retention input->submit 0.198 ms against G1 p99 2.000 ms (G);
  Save As 100000000 B sparse: Z input->submit 0.603 ms, command->rebound
  148.208 ms, longest UI turn 0.295 ms [Charging, load1 11.31-11.44].
- sanitized: stall longest UI turn 0.754 ms; keystroke 0.617 ms; Save As Z
  input->submit 6.274 ms, command->rebound 277.650 ms, longest UI turn 4.084 ms
  [Charging, load1 26.58-27.97].

## Unresolved

None from the rebase. The worker report's out-of-scope items 2 and 4 still
apply (peer reload on Save As target checkpoints on the UI; retention copy
submission under a Ctrl+S lease). Item 1 (retention copy outliving the journal
at close) is what edit-9ev6 fixed on main.
