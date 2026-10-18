# edit-bt1 worker report (session 8)

Bead: editor_step must not reach fail() on recoverable scroll-slice errors.
Decisions: docs/decisions/edit-bt1.md.

## Files touched

- `src/editor/scrollctl.c`: `deferred()` and `defer()`. In
  `editor_scroll_slice`, an install that fails with EDITOR_ERR_MEMORY defers
  to the retry tick and never reaches fail(). The window stays, and a second
  consecutive refusal shows a notice. The install branch is also taken when
  `scroll_snapshot` is NULL, and a successful install clears the retry state.
  `editor_scroll_waiting()` is false while the retry is deferred.
- `src/editor/private.h`: `editor_buffer.scroll_retry_ns` and `scroll_refusals`.
- `src/editor/editor.c`: `wait_timeout()` wakes at a pending scroll retry tick.
- `tests/editor_scroll_test.c`: `snapshot_refused` (`--bt1-snapshot`), added to
  the default run.
- `tests/editor_test.c`: `nbt1_results_geometry` (`--bt1-geometry`), added to
  the default run.
- `docs/decisions/edit-bt1.md`, this report.

## Red (tests first, unmodified src)

(1) Refused snapshot. The first attempts got no hit, because the slice
normally takes no allocator request: session publication or the minimap
caches the revision's snapshot first, and the retired slot is reused (traced
in gdb). The final test, a results tab at 16 columns that retains the
installed snapshots, gives:

    11:44:31 DISPLAY=:99 with_display_lock.sh build/san/tests/editor_scroll_test --bt1-snapshot
    edit-bt1: editor_step rc -101 error_cause -101 (attempt 1, hit 1)
    editor_scroll_test:480: FAIL rc == 0 && !e->error
    exit 1

(-101 = EDITOR_ERR_MEMORY from `install()` through fail().)

(2) Results open, then resize, then re-select. On this tree (edit-yey
included) it passes already:

    11:44:43 build/san/tests/editor_test --bt1-geometry
    edit-bt1: results open, panel close, re-select: the scroll slice keeps the settled geometry passed
    exit 0

With yey's three `editor_scroll_edit` lines (the `scroll_resize` to the
current `config(e)`) removed temporarily, the same test fails:

    11:44:55 build/san/tests/editor_test --bt1-geometry
    edit-5vz: editor_step rc -1 error_cause -1
    editor_test:5161: FAIL n5vz_settle(e) == 0
    exit 1

It is the same defect, fixed by edit-yey, and no second fix was made.
scrollctl.c was restored byte for byte from a copy. In this repro the -1
comes on the first settle after the results append, not only on re-select.

## Green

    11:47:28 build/san/tests/editor_scroll_test --bt1-snapshot
    edit-bt1: landed top 352 (from 370)
    editor_scroll_test: refused scroll snapshot defers the slice, keeps the window, notices persistence PASS
    exit 0

## Verification

Power was `Charging` [AC]. uptime load averages were 20.30 at 11:50 and
6.46 at 11:57. The bt1 case waits on the retry tick (two
JOURNAL_DEFAULT_SYNC_NS intervals), so a full editor_scroll_test run took
about 3-4 s.

| command | exit |
|---|---|
| `make -j3 all` (gcc, -Werror), after the last source change | 0 |
| `ASAN_OPTIONS=detect_leaks=1 XDG_RUNTIME_DIR=$(mktemp -d) EDIT_DISPLAY_LOCK_TIMEOUT=1800 DISPLAY=:99 sh tools/with_display_lock.sh build/san/tests/editor_scroll_test`, run 1 (11:50:39) | 0 |
| same, run 2 (11:50:42) | 0 |
| same, run 3 (11:50:46) | 0 |
| same harness, `build/san/tests/editor_test` (leaks on; n5vz and bt1 cases included), 11:50:57 to 11:52:51 | 0 (`editor_test: all passed`) |
| same harness, release `build/tests/editor_test` (G1 malloc guard), 11:52:51 to 11:57:12 | 0 (`all passed`; `null 10000 keys mallocs=0 guard=active`, `null hl-on 10000 keys mallocs=0 guard=active`, raster/gl the same) |

## What remains

Nothing in scope. Only the snapshot's EDITOR_ERR_MEMORY is deferred. Other
slice errors propagate as before.

## Out of scope (reported, not fixed)

1. `editor_map_service` (src/editor/minimap.c:59-60) returns
   EDITOR_ERR_MEMORY when its `piece_snapshot_take` fails, and `editor_step`
   passes that to fail() (editor.c, the service chain before the
   `if (rc) return fail(e, rc)`). After an edit to a named buffer the
   minimap's take runs before the slice's. So under real arena exhaustion
   this is probably the first fatal path, and it is the same class as this
   bead.
2. `install()` failures still propagate from `editor_scroll_resolve`
   (wrapped find and minimap navigation) and `editor_scroll_init` (buffer
   prepare and activation).
