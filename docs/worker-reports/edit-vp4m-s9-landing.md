# edit-vp4m landing report (s9, 2026-10-20)

Branch `wt/edit-vp4m-s9`, worktree `.wt/edit-vp4m-s9`. Power: `Full`
(`/sys/class/power_supply/BAT0/status`). Display :98.

## Rebase

- `build/briefs/land9.sh vp4m "<msg>"`: committed the worker tree (ac28ed6),
  rebase onto main (593bbfe) conflicted in `tests/editor_test.c`; script
  aborted the rebase, rc=3.
- `git rebase main` by hand. Conflicts, all in `tests/editor_test.c`, all
  purely additive (edit-6090 and edit-vp4m both insert at the same three
  spots); resolved by keeping both, 6090 first then vp4m:
  1. test bodies after `binary_open_contract`: `n6090_*` block, then
     `vp4m_visible` / `vp4m_case` / `vp4m_return_bottom`;
  2. selectors: `--6090-typing`, then `--vp4m-return`;
  3. suite list after `n3f2k_save_with_results`: `n6090_typing_top()`, then
     `vp4m_return_bottom()`.
- `src/editor/scrollctl.c` auto-merged; checked against ci6i (refuse/defer
  split lives outside `editor_scroll_follow`'s fast path, which the vp4m
  hunk alone touches): no semantic overlap.
- Rebased commit: `a5f51e7` on main `593bbfe`.

## Verification

| command | exit | uptime load1 |
|---|---|---|
| `touch src/*/*.c src/*/*.h tests/*.c; make -j3 all` (gcc 13, -Werror) | 0 | 13.91 -> 14.94 |
| `make -j3 build/san/tests/{editor_scroll_test,view_test,editor_test}` | 0 | |
| `XDG_RUNTIME_DIR=$(mktemp -d) DISPLAY=:98 EDIT_DISPLAY=:98 EDIT_DISPLAY_LOCK_TIMEOUT=3600 ASAN_OPTIONS=detect_leaks=1 sh tools/with_display_lock.sh build/san/tests/editor_scroll_test` | 0 | 11.26 |
| same wrapper, `build/san/tests/view_test` ("view_test: all passed") | 0 | 11.08 |
| same wrapper, `build/san/tests/editor_test` (full suite, includes edit-6090 and edit-vp4m lines, "editor_test: all passed") | 0 | 11.08 -> 10.39 |
| `make -j3 build/tests/editor_test` (release) | 0 | |
| same wrapper without ASAN, `EDITOR_ALLOC_ONLY=null build/tests/editor_test` ("null 10000 keys mallocs=0 guard=active") | 0 | 9.95 |
| same, `EDITOR_ALLOC_ONLY=raster` ("raster 10000 keys mallocs=0 guard=active") | 0 | 9.95 -> 10.68 |

No failures, no reruns. Nothing unresolved.
