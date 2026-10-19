# edit-9fzc landing report (s9)

Worktree `.wt/edit-9fzc-s9`, branch `wt/edit-9fzc-s9`. Worker commit rebased
onto main `5211e51` as `fdd1f20`. Main was not modified.

## Worker report review

- Item 1 (clean Save As peer reload off the UI-thread checkpoint): done;
  `reload_source(..., defer_checkpoint)` marks `publish_owed` so the peer
  joins the rebind's worker publish job. Red/green in `--9fzc-peer`.
- Item 2 (no retention submission under a Ctrl+S prepare lease): done;
  `editor_retain_tick` returns under any `editor_save_leased`. Red/green in
  `--9fzc-lease`.
- Item 3 (exit-time owed Save As checkpoint off the UI thread): **not done**.
  The worker kept `editor_flush` synchronous at exit, citing the bead's
  allowance (decision edit-9fzc.md, last paragraph). The landing commit
  message's wording "the exit-time owed checkpoint stay off the UI thread"
  overstates this.
- The worker's own sanitizer runs all exited 1 (LeakSanitizer under ptrace
  in its sandbox) and native editor_test could not reach the display; both
  are superseded by the green runs below.

## Rebase

`build/briefs/land9.sh 9fzc "<msg>"`: exit 0. Rebase clean, no conflicts
(e3z5 is not on main; no peer-reload reconciliation needed). `make -j3 all`
after `touch`: rc 0 (gcc 13, -Werror).

## Verification

Power: `Full` before and after (cat /sys/class/power_supply/BAT0/status).
Builds: `make -j3 build/san/tests/{dataloss,editor_session,editor_checkpoint,journal,savectl,editor}_test build/tests/editor_test`: rc 0.

Each suite: `XDG_RUNTIME_DIR=$(mktemp -d) DISPLAY=:95 EDIT_DISPLAY=:95
EDIT_DISPLAY_LOCK_TIMEOUT=1800 ASAN_OPTIONS=detect_leaks=1 sh
tools/with_display_lock.sh build/san/tests/<name>`, outside the sandbox.

| Suite (sanitizer) | rc | uptime at start (load 1/5/15) |
| --- | --- | --- |
| dataloss_test | 0 | 04:00:10, 15.64 18.04 21.92 |
| editor_session_test | 0 | 04:08:08, 10.44 12.28 17.51 |
| editor_checkpoint_test | 0 | 04:14:45, 7.92 8.57 13.92 |
| journal_test | 0 | 04:16:11, 7.04 8.20 13.32 |
| savectl_test | 0 | 04:17:24, 4.23 7.16 12.59 |
| editor_test (full, native :95) | 0 | 04:20:24, 2.55 5.05 10.78 |

Finished 04:23:04, load 11.15 7.69 10.89. No reruns were needed.

Release malloc guard (`build/tests/editor_test` under the same wrapper):

| EDITOR_ALLOC_ONLY | rc | result |
| --- | --- | --- |
| null | 0 | `null 10000 keys mallocs=0 guard=active` (04:23:12) |
| raster | 0 | `raster 10000 keys mallocs=0 guard=active` (04:23:16) |

Disk (`df -h /`): 395G used / 61G avail before the suites; 397G / 59G after
(editor_session_test includes the GB-scale orphan launch fixture).

Logs: `build/9fzc-land/` in the worktree.
