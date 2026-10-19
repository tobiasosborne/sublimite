# edit-uzrb — s8 landing report

Branch `wt/edit-uzrb-s8`, worker commit 39df4d9, rebased onto main 69647e4.
Rebased bead commit: **b744a00**. Main itself was not modified.

## Stamps (M)

- Start: BAT0 `Charging`; `02:15:09 up 40 min, load average: 6,69, 3,78, 1,86`; `/` 77G free.
- Suites: first suite started at `02:25:30` (BAT0 `Charging`, load 41,40). Load stayed between 18 and 41 because other landers share the box and display :95.
- End: BAT0 `Charging`; `02:44:03 up 1:09, load average: 18,61, 23,73, 23,93`.

## Rebase

`build/briefs/land8.sh uzrb`: exit 1 ("nothing to commit"). The worker tree was
already committed, so the script stopped before its rebase step. `git rebase main` then
conflicted in two files:

- `src/editor/session.c`: the semantic conflict with edit-3f2k (f436eb3).
  The worker had added local `recorded()`/`recorded_count()` helpers because
  `editor_recorded_rank` was missing from its base. Resolution: there is now one
  mapping. `editor_recorded_rank` (checkpoint.c) skips `pending_open` tabs as well as
  placeholder and derived tabs. It does this through one predicate,
  `editor_tab_recorded` (static inline, private.h). The local helpers are gone.
  `publish` uses the predicate. The worker's adoption refresh in `editor_session_publish`
  (a first publication of a recorded buffer goes through `editor_session_metadata`) is
  kept, and its fallback rank stays edit-3f2k's `editor_recorded_rank(active)`.
  `editor_session_metadata` keeps edit-3f2k's loop: live leaves take the dense rank
  and closed leaves keep their closed-stack index. The rank now advances on
  `editor_tab_recorded`. Save admission compares `live_count` with
  `editor_recorded_rank(e, tabs_count)`. The UI checkpoint's `recorded_tabs` is
  unchanged and still keeps pending-open tabs, as `editor_checkpoint_test`'s pending
  recovery cases require. `docs/decisions/edit-uzrb.md` now records this in place of
  the worker's "helper absent" paragraph.
- `tests/editor_session_test.c`: the selector ladder. Both sides are kept: main's
  `--edit-fkpk`, `--edit-djg` and `--edit-4jhi` plus the worker's `--edit-uzrb` selector,
  and the full-run `pending_open_save(false/true)` alongside `retained_lazy()`.

`git rebase --continue`: exit 0.

## Commands and exit codes

| Command | Exit |
| --- | --- |
| `build/briefs/land8.sh uzrb` | 1 (nothing to commit) |
| `git rebase main` | 1 (conflicts above, resolved by hand) |
| `git rebase --continue` | 0 |
| `touch src/*/*.c src/*/*.h tests/*.c && make -j3 all` (gcc, -Werror) | 0 |
| `make -j3 build/san/tests/{editor_test,editor_session_test,editor_checkpoint_test,dataloss_test,journal_test}` | 0 |
| san `editor_session_test --edit-uzrb` | 0 |
| san `editor_test` | 0 |
| san `editor_session_test` | 0 |
| san `editor_checkpoint_test` | 0 |
| san `dataloss_test` | 0 |
| san `journal_test` | 0 |
| release `editor_session_test --edit-uzrb` | 0 |
| release `EDITOR_ALLOC_ONLY=null editor_test` | 0 (`null 10000 keys mallocs=0 guard=active`) |
| release `EDITOR_ALLOC_ONLY=raster editor_test` | 0 (`raster 10000 keys mallocs=0 guard=active`) |

Every run used `XDG_RUNTIME_DIR=$(mktemp -d) DISPLAY=:95 EDIT_DISPLAY=:95
EDIT_DISPLAY_LOCK_TIMEOUT=1800 ASAN_OPTIONS=detect_leaks=1 sh
tools/with_display_lock.sh <binary>`. The runs were outside the sandbox so that
LeakSanitizer could inspect threads. Each suite passed on its first run, so none was rerun.
The worker's intermittent `existing_journal()` `rmdir(root)` failure did not
recur here. It is still untriaged (see the worker report).
