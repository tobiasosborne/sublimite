# edit-vp4m worker report (session s9, 2026-10-20)

Bead edit-vp4m: unwrapped Return typed on the bottom visible row left the
caret on the hidden row below the viewport for one frame. Worktree
`/home/tobias/Projects/sublimite/.wt/edit-vp4m-s9`. Decision doc:
`docs/decisions/edit-vp4m.md`.

## Cause

`editor_scroll_follow`'s resident fast path (src/editor/scrollctl.c) runs
after `layout_edit` and before relayout. For a newline-changing edit
`layout_edit` queues rows `r..rows` and leaves their `row_byte[]` starts
pre-edit, so a mid-line Return on the bottom row left the stale bottom row
spanning the caret; the fast path kept the top and the caret's new row was
laid on the hidden row. (End-of-line Return escaped by coincidence: the
stale next row starts at the caret and is outside the visible rows.)

## Fix

The fast path trusts a row only when its start and end are certified:
while the layout is busy, `row_byte[k]` with `lay.row < k < lay.row_end` and
the hidden row's `lay.pos` end are skipped. Otherwise the existing view-
certified follow or the resolver decides. Same-newline typing queues only its
own row, so the fast path still adopts the top on ordinary keys. No
allocation; one comparison pair per row.

## Files touched

- `src/editor/scrollctl.c` (`editor_scroll_follow` fast path)
- `tests/editor_test.c` (`vp4m_visible`, `vp4m_case`, `vp4m_return_bottom`;
  selector `--vp4m-return`; in the suite after `n3f2k_save_with_results`)
- `docs/decisions/edit-vp4m.md`, this report.

## Red run (test in, fix absent; ASan/UBSan, leaks on, :97; power Full, load1 26.55)

```
edit-vp4m mid-line: caret line 11 row 11, top 0 (view 0), rows 11
edit-vp4m: Return 0 moved the top 0 -> 0, not by one row
edit-vp4m mid-line: caret line 13 row 11, top 2 (view 2), rows 11
edit-vp4m: Return 2 moved the top 0 -> 2, not by one row
editor_test:5579: FAIL failed == 0
red exit=1
```

## Green run (fix in; same build flags, :97; power Full, load1 26.92)

```
edit-vp4m: unwrapped Return on the bottom row shows the caret's row in the same frame
exit=0
```

## Verification (power Full; load1 13.8-28.8 stamped in the logs)

| command | exit |
|---|---|
| `make -j3 all` (gcc 13, -Werror; rerun after the last test-comment edit) | 0 |
| `make -j3 build/san/tests/{editor_test,editor_scroll_test,view_test}` | 0 |
| `XDG_RUNTIME_DIR=$(mktemp -d) DISPLAY=:97 EDIT_DISPLAY=:97 EDIT_DISPLAY_LOCK_TIMEOUT=1800 ASAN_OPTIONS=detect_leaks=1 sh tools/with_display_lock.sh build/san/tests/editor_test` (full suite, 2m45s, "editor_test: all passed") | 0 |
| same wrapper, `build/san/tests/editor_scroll_test` | 0 |
| `ASAN_OPTIONS=detect_leaks=1 build/san/tests/view_test` | 0 |
| same wrapper, `EDITOR_ALLOC_ONLY=null build/tests/editor_test` (release malloc guard: "null 10000 keys mallocs=0 guard=active") | 0 |
| same wrapper, `build/tests/editor_test --vp4m-return` (release) | 0 |

The first release runs were started without the wrapper and the display
guard picked :99 (both exit 0); they were re-run on :97 under the lock as above.

## What remains / found outside the bead (not fixed)

- `view.c relative_wrap_place` (lazy large files, inexact count) has the
  same compare-against-stale-rows pattern (already noted by edit-6090).
- With an inexact line count (large file still indexing) the fallback after
  a skipped fast path is the asynchronous resolver, so the caret's row may
  appear one frame later there; not exercised by this test.
- `tests/editor_test.c` will need a trivial merge with edit-6090's test
  additions (both insert before `main` and after `n3f2k_save_with_results`
  in the suite list).

## Process notes

No `bd`, no `git`, no writes outside the worktree. No GB fixtures.
