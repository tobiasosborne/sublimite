# edit-xdro (s9): landing report

Lander, 2026-10-20. Power: Full (`/sys/class/power_supply/BAT0/status`), checked
at 05:48:57 (load 4.33) and again at the end of the run.

## The runnable() condition against e3z5

I checked whether the `runnable()` condition still matches the gate in main's
`editor_check_sources` (buffers.c:1097, after e3z5 landed). That gate is
unchanged:
`if (!e->source_choice || editor_save_active(e) || !editor_checkpoint_due(e)) return 0;`.
xdro's `(e->source_choice && !editor_save_active(e) && editor_checkpoint_due(e))`
is still its exact negation. **No adjustment was needed.**

Note for the coordinator (not changed, out of scope): e3z5 added a re-arm after
the gate. An explicit Reload that `reload_source` reports as
`EDITOR_RELOAD_DEFERRED` sets `source_choice` again. That outcome means
`file_save_busy(b->file)` or `e->op_count`. While either holds and the gate
is open, the choice stays runnable, so the loop polls with a zero timeout for
that window.
- `op_count` is drained by the journal pump in the same loop.
- `file_save_busy` covers the file module's save, commit or abort job and may
  outlast `editor_save_active`.

Copying that condition into `runnable()` would need a const, side-effect-free
form of `file_save_busy`, which pumps today. No test covers it, so it is left
as a possible follow-up.

## Commands

| Command | Exit |
|---|---|
| `build/briefs/land9.sh xdro "<msg>"`: commit, `git rebase main` (clean, no conflict), touch, `make -j3 all` | 0 (rebased 67bb00c onto de0a959; make all rc=0) |
| `make -j3 build/san/tests/editor_session_test build/san/tests/dataloss_test build/san/tests/editor_test` | 0 |
| san editor_session_test (full), 05:51:52 to 05:52:28, load 8.82 to 8.46 | 0 |
| san dataloss_test (full), 05:52:28 to 05:52:30, load 8.46 | **1** |
| san editor_test (full), 05:52:30 to 05:55:51, load 8.46 to 4.97 | 0 |
| san dataloss_test --edit-ivv, the failing case alone, 05:56:03, load 4.50 | 0 |
| san dataloss_test (full) rerun, 05:56:06, load 4.30 | **1** |
| A/B: dataloss_test full with main's runnable() (`|| e->source_choice ||`, hunk reverted in the file temporarily), run 1, load 3.72 | **1** (same failure) |
| A/B: as above, run 2, load 3.58 | **1** (same failure) |
| xdro editor.c restored byte for byte, then `make -j3 all` plus the three san binaries | 0 |
| san dataloss_test, each later case alone, 05:56:50 to 05:57:19, load 3.23 to 2.78: --edit-e3z5, --reload-held, --two-process-reload, --shared-save-dirty, --shared-save-clean, --rewrite-rename, --rewrite-in-place, --retain-lifecycle, --y315, --retain-close, --detached-rewrite, --retain-window, --ze59-stall, --ze59-save-as, --9fzc-peer, --9fzc-lease, --stranded-orphan | 0 each |
| release build/tests/editor_test, EDITOR_ALLOC_ONLY=null: "null 10000 keys mallocs=0 guard=active" | 0 |
| release build/tests/editor_test, EDITOR_ALLOC_ONLY=raster: "raster 10000 keys mallocs=0 guard=active" | 0 |

All display runs used
`XDG_RUNTIME_DIR=$(mktemp -d) DISPLAY=:97 EDIT_DISPLAY=:97 EDIT_DISPLAY_LOCK_TIMEOUT=3600 sh tools/with_display_lock.sh`.
The sanitizer runs also set `ASAN_OPTIONS=detect_leaks=1`.

From the editor_session_test log, both xdro tests passed (M)[AC]:
- edit-xdro (a), choice waiting on a save in flight: steps=9, more=0.
- edit-xdro (b), choice waiting on the journal tick: steps=9, more=0.

## Unresolved: dataloss_test default run, already on main

The full run fails at `dataloss_test:1121`:
`FAIL guard >= 0 && flock(guard, LOCK_EX | LOCK_NB) == 0`.
This is inside `ivv_choice`, called from the default sequence at line 1690.
- The first run failed on `ivv_choice(false)`, the rerun on `ivv_choice(true)`.
- The test's own non-blocking LOCK_EX on the directory guard is refused before
  any choice is pressed, so the editor or something else holds the guard at
  that point.
- `--edit-ivv` alone passes.
- With main's `runnable()` the failure is identical (A/B above). It is not
  caused by xdro.

Because the default run stops there, these cases ran in no form: the cases
after `ivv_choice` that have no command-line option (`save_during_prompt`,
`hot_exit_mixed`, `reload_vs_keystroke`, `two_process_save(false)`,
`torn_undo_group`, two of the three `stranded_orphan` variants). The
`--stranded-orphan` option ran one variant. Every case after the failure that
has its own option passed alone (table above).
