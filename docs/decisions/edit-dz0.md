# edit-dz0: typing into an unindexed large file repairs byte-relatively

## Root cause

Every typed key ends in `src/editor/input.c` `repair()`, which asks the view to
repair cluster boundaries and follow the cursor: legacy empty `VIEW_TYPE`, which
becomes `view_restore` because the length changed. `view_restore` runs
`P_NORMAL_SETUP` (`src/view/view.c`), whose first act is
`line_at(first_byte)`: a cooperative `line_query` that counts LFs from the
nearest certified anchor. While the index is incomplete and the top is a density
estimate, `editor_seed_view_lines` (`src/editor/editor.c`) installs no anchor
(edit-jf9: the view caches hold certified lines only), so the nearest anchor is
byte 0. After Ctrl+End on a 1 GiB file that is a UI-thread scan of the whole
file, sliced at VIEW_SCAN_BOUND per call.

Measured with the red test (below), release, bulk lane held: line_query counted
1,073,741,825 bytes (the file plus the typed byte) and the key settled in
910.662 ms (M)[AC, BAT0 "Not charging", load1 8.66]; an earlier run of the same
binary gave 919.694 ms at load1 3.28. edit-mmp measured 0.8 to 2.1 s on its
fixture (docs/decisions/edit-mmp.md).

## Decision

1. **Byte-relative restore in view.** New `view_relative_restore(v, state,
   change)` (`src/view/view.h`): `view_restore` for an unwrapped buffer whose
   external line count is inexact. In `P_NORMAL_SETUP` it proves only the top's
   line start, walking back to LF with edit-jf9's `relative_start`, keeps the
   host's `first_line` as a label, and marks the state `approximate`. Endpoint
   repair is unchanged (`repair_begin`, local `near_seed` scans). Following is
   the jf9 byte-relative follow (`P_REL_FOLLOW`: walk back to the cursor's line
   start, exact grapheme column, horizontal scroll). `relative` stays set on
   completion, so `editor_scroll_follow` does not adopt `v->line`
   (scrollctl.c already guards with `!e->v.relative`). No line query runs on
   this path and no estimate enters the exact caches. Same scan, deadline and
   continuation contract; scratch is the caller-owned view; no allocation.
2. **Editor routing.** `relative_repair(e)` = `!wrap && lg.lazy &&
   !lg.lines_exact`. `repair()` (typing, deletes, plan edits) and
   `replay_follow()` (undo/redo) call `view_relative_restore` when it holds,
   and exactly the previous calls otherwise. Small files are not lazy, so their
   typing path executes the same calls as before (coordinator decision 3).
3. **Estimated top (coordinator decision 2), carried as a label, not a cache
   seed.** The brief asked `editor_seed_view_lines` to accept an estimated top
   marked as an estimate. Seeding a density estimate into `v->lines` would make
   every later `line_query` answer inherit it while looking certified; after
   publication `editor_scroll_follow` adopts `v->line` whenever the count is
   exact and `!relative` (scrollctl.c), so a stale estimated ordinal could be
   shown as exact. That would break edit-jf9's settled invariant ("No density
   estimate enters view's line-start or line-query caches", law 5). Instead the
   estimated top travels in the view state: `first_line` is the host's label and
   `approximate` (the existing estimate marker, no new flag) is set. The index
   publication corrects it through the existing paths: the scroll resolver
   relabels when `count.exact` flips (edit-agh), `index_progress` seeds exact
   navigation anchors, and `lines_exact` turns the exact repair back on.
   `editor_seed_view_lines` is unchanged; on the relative path it is harmless
   (no active query to redirect).
4. **Wrapped buffers keep the exact repair.** Measured: the wrapped variant of
   the red test counts 3840 bytes (M), exactly one call's line_query budget
   (P: 256-byte reads continue while scanned + 264 <= 4096, i.e. from
   scanned <= 3832, so the last read starts at 3584 and ends at 3840) before
   `editor_seed_view_lines`
   installs the laid-out top and the query restarts there. It is within the
   same bound, so no wrapped change was made.

## Bound

The structural property asserted: during the key, line_query counts at most
VIEW_SCAN_BOUND = 4096 bytes (P: one view call's charged scan work,
`src/view/view.h`). Green counts 0. The byte-relative walks are proportional to
the bytes of the top's line and the cursor's line, never the file prefix; a
very long line still costs its own length in cooperative slices, as edit-jf9
records for relative motion.

## G1 and allocation (law 2)

The typing path of ordinary buffers is unchanged (the routing predicate is
false). Release `build/tests/editor_test` before and after, same box, same
session: every `mallocs=0` line unchanged (10000-key null/raster/hl-on cases,
clipboard, review 21 "mutation-to-submit=0", edit-g11 typing) and the suite
passes; tracked prints in docs/worker-reports/edit-dz0-s7.md. The relative
restore runs under `edit_malloc_guard` in `tests/view_test.c` with zero
allocations.

## EDITOR_IDLE_NS

Value unchanged. Its derivation is now next to it in `src/editor/editor.h`:
(G) the caret blink stops after 10 s idle, PRD.md section 6 and gate G11 in
perf/01-perf-target.md section 0.2 (0 wakeups/s after 10 s). Its reuse by tests
as a settle watchdog is (E) with no gate behind it: no gate bounds a settle.

## Test

`tests/editor_large_test.c` `dz0_key_at_end(wrap)`: mmp_fixture's sparse 1 GiB
layout, mapped, no journal, private XDG_DATA_HOME, root removed on every exit
path; bulk lane held from open so the index cannot complete; Ctrl+End; one key;
assert line_query bytes <= VIEW_SCAN_BOUND, text and cursor; settle is a tracked
print. The fixture's last two 1 MiB lines are read once first (warm page cache,
as for a log just written): scroll's resident follow takes the foreground lane
only for windows in core (edit-91c), and a cold sparse hole would otherwise
queue it behind the held bulk lane forever. The pre-fix repair warmed those
pages as a side effect of its scan. `tests/view_test.c` `relative_restore` and a
`fuzz/view_fuzz.c` probe compare `view_relative_restore` with `view_restore`
(selection, horizontal scroll), check the label, the proven top line start and
zero line_query bytes.
