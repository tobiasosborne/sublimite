# edit-dzk1 (s8): landing report

Lander for the parked tree wt/edit-dzk1-s8 (worker commit aab1934). Main was not touched; the coordinator fast-forwards.

## Rebases

The tree was already committed, so `land8.sh dzk1` would have stopped at "nothing to commit". The rebases were done by hand. Main moved three times during verification, so the tree was rebased four times:

| base | result | conflicts and resolution |
|---|---|---|
| 69647e4 | 26d4ae5 | tests/editor_test.c: 2ghs `binary_open_contract` vs dzk1 `dzk1_render_reject` (function bodies and default-suite calls); tests/editor_session_test.c: fkpk/4jhi vs dzk1 selector and default-suite lines. Kept both sides. |
| df277ff (uzrb) | 721f164 | tests/editor_session_test.c: uzrb `pending_open_save` vs the dzk1 test block. Kept both and closed the uzrb function at the separator. |
| 454757e (ze59, 2mhd, 0rge) | e4e6122 | clean |
| 5c1e112 (ut0m, a7fo, 14bi, ci6i, 6yom) | **a61f772** | tests/editor_session_test.c: ut0m `--edit-ut0m` selector vs dzk1 `--edit-dzk1` selector. Kept both. |

Besides the conflicts, one edit: docs/decisions/edit-dzk1.md got the paragraph "Downgrade hazard and install rule": PAINT (type 11) makes an older binary truncate the journal at the first PAINT on recovery, so reinstall the binary in the same step as landing this.

## Commands (on a61f772 unless marked)

All sanitizer runs: `XDG_RUNTIME_DIR=$(mktemp -d) DISPLAY=:95 EDIT_DISPLAY=:95 EDIT_DISPLAY_LOCK_TIMEOUT=1800 ASAN_OPTIONS=detect_leaks=1 sh tools/with_display_lock.sh build/san/tests/<name>`.

| command | exit |
|---|---|
| `touch src/*/*.c src/*/*.h tests/*.c; make -j3 all` (gcc, -Werror) | 0 |
| `make -j3 build/san/tests/{editor,editor_session,editor_hot_exit,journal,render,editor_checkpoint,dataloss,unicode_render,raster}_test build/fuzz/fuzz/journal_fuzz.o` | 0 |
| san `editor_test --dzk1` | 0 |
| san `editor_session_test --edit-dzk1` | 0 |
| san `render_test` | 0 |
| san `journal_test` | 0 |
| san `editor_checkpoint_test` | 0 |
| san `dataloss_test` | 0 |
| san `unicode_render_test` | 0 |
| san `raster_test` | 0 |
| san `editor_hot_exit_test` (100 trials) | 0 |
| san `editor_session_test` (full) | still running at the time of writing; 0 on e4e6122 (base 454757e) and on 721f164 |
| san `editor_test` (full) | still running at the time of writing; 0 on e4e6122 and on 721f164 |
| release `EDITOR_ALLOC_ONLY=null build/tests/editor_test` | still queued; 0 on e4e6122 and 721f164 ("null 10000 keys mallocs=0 guard=active") |
| release `EDITOR_ALLOC_ONLY=raster build/tests/editor_test` | still queued; 0 on e4e6122 and 721f164 ("raster 10000 keys mallocs=0 guard=active") |

On 26d4ae5 (base 69647e4) every suite above was 0 except san `raster_test`, which was 1 twice and 0 on the third run. It failed in `render_test_prepare` (`plat_init`/`work_pool_init`, connecting to :95) before any dzk1 code ran. Under gdb the same call returned 0. A control binary without dzk1 (edit-9f63-s9) passed that part. It passed on every later base, so I put it down to contention on :95 (load average 28).

The first run on a61f772 was killed by SIGTERM (rc 143) while it waited on the :95 lock. Nothing in it failed. It was restarted detached.

## Stamps

- Start: 02:19 AEDT, BAT0 Charging, load average 27.97 / 16.27 / 7.39
- 03:34: Charging, load 18.20
- 04:20: Full, load 2.55
- Report: 05:14, BAT0 Full, load average 6.57 / 7.39 / 9.42

No timings are claimed.

## Unresolved / for the coordinator

1. Main moved again to 8da8b8b (vp4m, y315) after a61f772. That base is not rebased or tested.
2. The worker commit includes `tools/__pycache__/refwin_pairs.cpython-312.pyc`, a stray build artifact. It is left in because a lander may make no other edits. Drop it when squashing.
3. ze59's worker-side publish and rotation (session.c, `editor_publish_start`) do not carry a pending PAINT, like the save-checkpoint limit in the decision doc. A rotation inside a first paint allows one more crash before the guard engages.
4. Install the binary in the same step as fast-forwarding (see the decision doc).

## Rebase over 6090..y315

Lander s8, 2026-10-20. Base 5c1e112 → main 4e97c64 (edit-6090, edit-9fzc, edit-3620, edit-vp4m, edit-y315 and worklog commits). Main was not touched.

`git rebase main` (exit 1 at the bead commit, then 0 after `rebase --continue`). Result: bead commit **8b27346**, this report commit on top.

Resolutions:

- tests/editor_test.c: one conflict where main's 6090/vp4m test functions (ending in `vp4m_return_bottom`) met dzk1's `dzk1_render_reject`. Kept both sides and closed `vp4m_return_bottom` with `}` before the dzk1 block. The selector and default-suite lines merged cleanly: `--dzk1`, `--6090-typing` and `--vp4m-return` are all present.
- All other files merged without conflicts: editor.c, editor.h, private.h, editor_session_test.c, checkpoint.c and session.c. I checked how the dzk1 code fits with the code main took. dzk1's `editor_journal_paint_clear` in `close_result` runs only on `rc == 0` from `editor_flush`. y315's `EDITOR_MORE` (the worker still owns the journal past the exit deadline) therefore appends no PAINT(0), and the y315 early return to `EDITOR_CLOSED` stays below it. `editor_journal_paint` skips while `editor_save_leased`, which matches 9fzc's lease rule. dzk1 code was not changed.
- `tools/__pycache__/refwin_pairs.cpython-312.pyc`: `git rm --cached` during the conflict stop, folded into the bead commit (now 21 files, no pyc). The file stays untracked in the worktree.

Commands (all on 8b27346; sanitizer runs: `XDG_RUNTIME_DIR=$(mktemp -d) DISPLAY=:97 EDIT_DISPLAY=:97 EDIT_DISPLAY_LOCK_TIMEOUT=3600 ASAN_OPTIONS=detect_leaks=1 sh tools/with_display_lock.sh build/san/tests/<name>`, run one after another):

| command | exit | end stamp |
|---|---|---|
| `touch src/*/*.c src/*/*.h tests/*.c; make -j3 all` (gcc, -Werror) | 0 | |
| `make -j3 build/san/tests/{editor,editor_session,journal,editor_checkpoint,dataloss,render,editor_hot_exit}_test build/tests/editor_test` | 0 | |
| san `journal_test` | 0 | 05:24:36, load 7.46 |
| san `render_test` | 0 | 05:24:36, load 7.46 |
| san `editor_checkpoint_test` | 0 | 05:24:47, load 8.46 |
| san `dataloss_test` (default run, includes y315 and 9fzc) | 0 | 05:25:31, load 11.16 |
| san `editor_session_test` (full, includes the dzk1 restore case) | 0 | 05:26:27, load 14.39 |
| san `editor_hot_exit_test` (100 trials) | 0 | 05:27:27, load 15.57 |
| san `editor_test` (full, includes dzk1, 6090, vp4m) | 0 | 05:31:07, load 7.10 |
| release `EDITOR_ALLOC_ONLY=null build/tests/editor_test` (under the :97 lock) | 0, "null 10000 keys mallocs=0 guard=active" | 05:31:07 |
| release `EDITOR_ALLOC_ONLY=raster build/tests/editor_test` | 0, "raster 10000 keys mallocs=0 guard=active" | 05:31:12, load 6.94 |

No case failed, so nothing was rerun.

Stamps: start 05:21 AEDT, BAT0 Full, load average 5.50 / 6.34 / 8.22. End 05:31, BAT0 Full, load average 6.70 / 9.54 / 9.19. No timings are claimed. The y315 test printed its own exit figure (UI exit 1007.034 ms against its 2000 ms (G) bound), sanitized, at load ~11.

Still open: item 3 above (a worker publication does not carry a pending PAINT) and item 4 (install the binary in the same step as the fast-forward). Item 2 (the pyc) is resolved.
