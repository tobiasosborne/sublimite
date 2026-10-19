# edit-ci6i (s9): landing report

Lander: Claude Opus. Worktree: .wt/edit-ci6i-s9, branch wt/edit-ci6i-s9.
Power: `Full` (/sys/class/power_supply/BAT0/status, checked 04:09:21 and 04:14:55).

## Rebase

The worker tree branched at f436eb3. `build/briefs/land9.sh ci6i` committed it and
rebased it onto main 5ba8c5c (14bi landed) with no conflicts. Rebased commit:
47e5aa8. Diff against main: editor.c, input.c, minimap.c, private.h, scrollctl.c,
editor_test.c, the decision doc and the worker report (383 lines added, 10 removed).

## Commands

| time | command | exit |
|---|---|---|
| 04:0x | `build/briefs/land9.sh ci6i "<msg>"` (commit, rebase onto main, `touch`, gcc `make -j3 all`, -Werror) | 0 (`make all rc=0`) |
| 04:0x | `make -j3 build/san/tests/editor_test build/san/tests/editor_scroll_test build/tests/editor_test` | 0 |
| 04:09:21-04:09:51, load 9.51 -> 9.10 | san editor_scroll_test, leaks on, :95 lock | 0 (bt1's refused-snapshot case PASS) |
| 04:09:57-04:14:15, load 9.02 -> 7.04 | san editor_test (full), leaks on, :95 lock | 0 (`editor_test: all passed`; both ci6i cases passed) |
| 04:10:07, load 8.39 | release editor_test, EDITOR_ALLOC_ONLY=null | 0, `null 10000 keys mallocs=0 guard=active` |
| 04:14:46, load 7.92 | release editor_test, EDITOR_ALLOC_ONLY=raster | 0, `raster 10000 keys mallocs=0 guard=active` |

Harness for every suite run: `XDG_RUNTIME_DIR=$(mktemp -d) DISPLAY=:95 EDIT_DISPLAY=:95
EDIT_DISPLAY_LOCK_TIMEOUT=3600 ASAN_OPTIONS=detect_leaks=1 sh tools/with_display_lock.sh <bin>`
(ASAN_OPTIONS dropped for the release binary), run outside the sandbox.

ci6i lines from the full run:

    edit-ci6i: wrapped seek landed top 0 (from 1014, target 0)
    edit-ci6i: refused minimap snapshot defers the map, keeps the editor, notices persistence passed
    edit-ci6i: refused wrapped seek window defers the seek, keeps the view, notices persistence passed

No reruns were needed. Nothing is unresolved.
