# edit-y315 (s9) landing report

Power: `cat /sys/class/power_supply/BAT0/status` = Full (04:57 and 05:05).
Display :97 (shared with one other lander), with_display_lock.sh.

## Rebase
- `build/briefs/land9.sh y315 "<bead message>"`: exit 0. Committed the worker tree,
  `git rebase main` clean (no conflicts), rebased onto main 593bbfe (6090, 9fzc).
  `touch src/*/*.c src/*/*.h tests/*.c; make -j3 all` (gcc, -Werror) inside the script: rc=0.
- Selector ladder in tests/dataloss_test.c keeps both sides: `--y315`, `--y315-stall`,
  `--ze59-*`, `--9fzc-peer`, `--9fzc-lease`.
- Doc edit (in scope): docs/decisions/edit-9fzc.md, the paragraph "The exit path remains
  synchronous ..." replaced by one line pointing to edit-y315 (docs/decisions/edit-y315.md).
  Folded into the bead commit (`git commit --amend --no-edit`).

## Suites (sanitized, clang; `XDG_RUNTIME_DIR=$(mktemp -d) DISPLAY=:97 EDIT_DISPLAY=:97
EDIT_DISPLAY_LOCK_TIMEOUT=3600 ASAN_OPTIONS=detect_leaks=1 sh tools/with_display_lock.sh build/san/tests/<name>`)
`make -j3 build/san/tests/{dataloss,editor_checkpoint,journal,editor_close,editor_session,editor}_test build/tests/editor_test`: rc=0.

| suite | rc | uptime at end |
|---|---|---|
| dataloss_test | 0 | 05:00:02 load 9.35, 11.88, 11.27 |
| editor_checkpoint_test | 0 | 05:00:13 load 9.29, 11.79, 11.24 |
| journal_test | 0 | 05:00:28 load 8.92, 11.58, 11.18 |
| editor_close_test | 0 | 05:00:28 load 8.92, 11.58, 11.18 |
| editor_session_test | 0 | 05:00:57 load 8.73, 11.29, 11.10 |
| editor_test | 0 | 05:05:06 load 12.81, 11.49, 11.17 |

dataloss_test y315 lines (M) [Full, sanitized]: healthy: flush=0 owed=0 UI rotations=0
worker rotations=1; stalled rotation: UI exit=1002.564 ms bound=2000.000 ms (G, 2 *
JOURNAL_DEFAULT_SYNC_NS) flush=1 worker rotations=1 timed_out=0 load1=9.56; recovered
1 tab, modified=1. 9fzc lines (peer, lease, resumed retention) also pass.

## Release malloc guard (`build/tests/editor_test`, same display wrapper)
- EDITOR_ALLOC_ONLY=null: rc=0, "null 10000 keys mallocs=0 guard=active" (05:05:21, load 12.43)
- EDITOR_ALLOC_ONLY=raster: rc=0, "raster 10000 keys mallocs=0 guard=active" (05:05:26, load 12.23)

No failures, no reruns. Nothing unresolved from the landing; the worker report's
"Remaining / out of scope" list stands.
