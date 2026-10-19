# edit-9f63 — s9 landing report

Lander (Opus). Branch `wt/edit-9f63-s9`. The fix commit is `e0f2664`, rebased onto main `0b915c3`
("Session 19: worklog 05:55 (dxas landed)"). This report is committed on top of it.
Stamps (M): `cat /sys/class/power_supply/BAT0/status` = `Full` throughout (on AC). Final stamp:
`05:37:12 up 4:02, load average: 5,40, 7,10, 8,21`.

## Rebases and conflicts

1. `build/briefs/land9.sh 9f63 "<msg>"`: commit `7fa840d`, then the rebase onto `5211e51` conflicted
   in `tests/editor_test.c`, and the script aborted (rc=3). Before the commit, the worker's stray
   `build-edit-9f63-*.json` evidence files were moved out of the tree to the lander's scratchpad
   so they were not committed. The worker's `/*.log` files are gitignored.
2. Ran `git rebase main` by hand, which conflicted only in `tests/editor_test.c`:
   - Hunk 1: main's edit-2mhd `minimap_grab_contract()` and this tree's `minimap_text_columns()`
     both end at the same `return 0; }`. I kept both functions in full, 2mhd first, each with
     its own closing.
   - Hunk 2: selector ladder. I kept both: `--minimap-grab-inside/outside` and `--9f63-columns`.
   - The default-suite calls (`minimap_grab_contract(false/true)`, `minimap_text_columns()`) merged
     cleanly. `src/editor/input.c` merged cleanly, and the copy
     `e->v.config.cols = e->lay.text_cols ? e->lay.text_cols : 1u;` stays in `changed()` after
     a successful `layout_edit`.
   - `git rebase --continue` gave `e8aedd9` on `5211e51`.
3. Main moved during verification (14bi, ci6i, 6yom, 6090, 9fzc). 6090 changed `input.c`
   `finish_edit` and `view.c` on the same Return/follow path, so I ran `git rebase main` again.
   It was clean and gave `7e28695` on `593bbfe`. I re-verified.
4. Main moved again (3620, vp4m, y315, 7cj8, dxas). vp4m changed the scroll-follow fast path
   for an unwrapped Return. `git rebase main` was clean and gave **`e0f2664` on `0b915c3`**.
   I re-verified (round 3 below).
5. After that, main moved to `cf416ea` (7cj8 follow-up, dzk1, orchestration item 42 "lander
   protocol v2: rebase once, never chase main"). I stopped chasing: `0b915c3..cf416ea` does not
   touch `src/editor/input.c`, `src/view` or `src/layout`, but it does touch `tests/editor_test.c`.
   The coordinator's final gated rebase covers it.

## Verification (round 3, tree `e0f2664`)

| Command | Exit | Stamp / note |
| --- | --- | --- |
| `touch src/*/*.c src/*/*.h tests/*.c; make -j3 all` (gcc, -Werror) | 0 | rounds 1-3 all 0 |
| `make -j3 build/san/tests/{editor,render,layout,raster}_test build/tests/editor_test` | 0 | |
| san `editor_test` (lock wrapper on :95, `ASAN_OPTIONS=detect_leaks=1`, private XDG_RUNTIME_DIR, unsandboxed) | 0 | 05:32:55, load 6.77; all 16 `--9f63-columns` rows `view_right=256 minimap_left=256` |
| san `render_test` | 0 | 05:33:28, load 6.90 |
| san `layout_test` | 0 | 05:33:29, load 6.90 |
| san `raster_test` | 0 | 05:36:18, load 6.04; `render native resize: PASS` |
| `EDITOR_ALLOC_ONLY=null build/tests/editor_test` (release, lock wrapper) | 0 | `null 10000 keys mallocs=0 guard=active`, 05:36:18 |
| `EDITOR_ALLOC_ONLY=raster build/tests/editor_test` | 0 | `raster 10000 keys mallocs=0 guard=active`, 05:37:07 |

Rounds 1 (`e8aedd9`, 04:00-04:23, load 2.4-13.9) and 2 (`7e28695`, 05:04-05:24, load 4.7-12.5)
ran the same set, and every exit was 0. Raw logs are in the worktree root (`land-*`, `land2-*`, `land3-*`).

The raster_test failure the worker reported (`raster_test:605 render_test_prepare` in the native
resize fixture) did not reproduce in any of the three rounds. That matches the Xvfb
reset-between-clients cause triaged in docs/worker-reports/check18-triage.md. It is not a defect
of this tree.

## Typing latency G1 (worker item "latency neutrality unverified")

`tests/editor_test.c` has no G1 latency selector. Its typing guards are allocation counts, and its
only percentile is the d5o compose cost. The G1 row (`*_injection_T4_G1`, input to T4 submit) is
in `bench/editor_bench.c`. To compare A/B on one base, I copied the tree at `e8aedd9` to the
lander scratchpad, reverted only the `input.c` copy, and built the "before" `editor_bench`
(`make -j3 build/bench/editor_bench`, rc=0). "After" is the worktree's own build. Each pass ran
`EDIT_BACKEND=raster editor_bench --no-idle --no-p4 --serial-only --file=tests/editor_test.c`
inside one display-lock hold on :95, in ABBA order. Each pass is 10000 keys, dropped=0,
allocations=0, structural=OK. The gate is the bench's own `gate_p50=1000000 gate_p99=2000000`
(G). Times are (M)[AC] in ns. Load1 is the bench's own stamp, and other landers were sharing the box.

| Pass | Start | null p50 / p99 | raster p50 / p99 | load1 |
| --- | --- | --- | --- | --- |
| before #1 | 04:23:26 | 2092088 / 5472105 | 33623217 / 67122494 | 11.6 |
| after #1 | 04:30:58 | 2009598 / 2836438 | 33848078 / 66957835 | 11.1 |
| after #2 | 04:38:46 | 932856 / 1940982 (PASS) | 33330612 / 41232376 | 13.1 |
| before #2 | 04:45:23 | 2427775 / 7888562 | 34287857 / 71102191 | 13.3-14.9 |

The first before #2 attempt was killed by the harness's background limit (exit 144) before it
printed any rows. It was rerun alone right after, and that rerun's bench exit was 0. The "exit=0"
lines in `land-g1-ab.log` for the first three passes are grep's status, not the bench's.

Reading: the raster p50 sits at 33.3-34.3 ms for both variants, a spread of about 3 %, so the
change is neutral within run-to-run noise. Null-backend p99 is dominated by box load. Every after
pass is at or below the adjacent before pass, so this shows no regression from the fix, which is
one scalar store per successful incremental edit. Both variants miss G1 on raster at
2880x1800 / font_px=30. That is the pre-existing raster cost and is outside this bead.

## Unresolved

- The branch is based on `0b915c3`. Main is now `cf416ea` (7cj8, dzk1). Per item 42, the
  coordinator does the final rebase. A conflict is possible only in `tests/editor_test.c`
  (selector ladder and default-suite list).
- The real-display trial on Xorg `:0` was not driven (the worker did not drive it either).
