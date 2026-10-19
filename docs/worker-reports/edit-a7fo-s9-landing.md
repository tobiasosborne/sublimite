# edit-a7fo landing report (s9)

Stamp at start: power `Charging`; `03:13:57 up 1:39, load average: 21,73, 20,10, 19,64`.
Stamp at end: power `Charging`; `03:37:31 up 2:02, load average: 21,19, 22,18, 22,78`.

## Rebase

- Worker tree committed by `build/briefs/land9.sh a7fo "<msg>"` (exit 0) and rebased
  onto main `3ca3386` (Session 19: worklog 03:08, ze59 landed) with no conflicts.
- Rebased bead commit: `8e4432b` "edit-a7fo: wrapped ASCII spans record per-cell hit
  entries so a mouse click lands under the pointer and a drag selects by character (bd-a7fo)".
- Diff: `src/layout/wrap.c` +5 (per-cell hit records in `ascii_span`), `tests/editor_test.c`
  +43 (`a7fo_click_contract`, five variants), `docs/decisions/edit-a7fo.md`,
  `docs/worker-reports/edit-a7fo-s9.md`.

## Commands and exit codes

| Command | Exit |
|---|---|
| `build/briefs/land9.sh a7fo ...` (commit, rebase onto main, touch sources, `make -j3 all`, gcc -Werror) | 0 (`make all rc=0`) |
| `make -j3 build/san/tests/{editor_test,editor_scroll_test,layout_test,render_test}` | 0 |
| san `editor_test` (`XDG_RUNTIME_DIR=$(mktemp -d) DISPLAY=:95 EDIT_DISPLAY=:95 EDIT_DISPLAY_LOCK_TIMEOUT=1800 ASAN_OPTIONS=detect_leaks=1 sh tools/with_display_lock.sh ...`) | 0 (`editor_test: all passed`; all five `edit-a7fo click placement` lines printed) |
| san `editor_scroll_test` (same wrapper) | 0 |
| san `layout_test` (same wrapper) | 0 (`layout_test: all passed`) |
| san `render_test` (same wrapper) | 0 (`render_test: PASS`) |
| `make -j3 build/tests/editor_test build/tests/layout_test` (release) | 0 |
| release `editor_test` with `EDITOR_ALLOC_ONLY=null` (same wrapper, no ASAN_OPTIONS) | 0 (`null 10000 keys mallocs=0 guard=active`) |
| release `editor_test` with `EDITOR_ALLOC_ONLY=raster` | 0 (`raster 10000 keys mallocs=0 guard=active`) |
| release `layout_test` | 0 (`0 allocations over 10000 relayouts (guard active)`) |

No AddressSanitizer, LeakSanitizer or UBSan report in any log. No case needed a rerun.

## Unresolved

- `raster_test` was not run here: per the coordinator it fails at `render_test_prepare`
  on main as well (build/check18/raster_test.log), so it is not this tree's regression.
