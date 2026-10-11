# edit-d50 fix + rebase onto main bd2ecef

Branch `wt/edit-d50-rb`, worktree `.wt/edit-d50`. Power for every run below:
`/sys/class/power_supply/BAT0/status` = `Not charging` (box on AC) [AC].
Exit codes are the observed process return codes (M).

## Step 1: P1 "C all blank lines" (undo after Ctrl+/ fails in editor_step)

### Root cause

The verifier attributed this to `replay_follow` using `view_restore` instead of
main's `view_command(VIEW_TYPE)`. That does not hold. With main's empty TYPE
put back alone (input.c:283 at d12e6f7), `--default-comments` still exited 1.
Both calls end in the same `normalize` (view.c:845: an empty TYPE on a changed
state calls `view_restore`), and that path already clamps the top to the text
length (view.c:674).

The actual cause is in 3kk's scroll wiring:

- `src/editor/scrollctl.c:171-178` (d12e6f7), `editor_scroll_edit`: on an edit
  it copies `scroll_display` into `scroll` and queues a pending
  `scroll_seek_byte`. The displayed state `scroll_display` keeps its pre-edit
  `first_byte`.
- `src/editor/scrollctl.c:63-67`, `editor_scroll_mirror`: this is called from
  `editor_scroll_follow` after the replay. It copies that stale display byte
  back into `e->v.state`, overwriting the top the view had just normalized.
  The view-adoption path in follow did not take over, because scroll's pure
  policy picked a different row ordinal from the view's.
- `src/editor/editor.c:158`, `editor_full_layout`: it passes that byte to
  `layout_begin`. `begin_common` (layout.c:71) returns `LAYOUT_ERR_ARG` for
  first_byte 6 > total 4, and `editor_step` fails.

Instrumented trace, removed afterwards: at the follow, the view top was line 2
at byte 4 and the scroll top was line 1 at byte 6, with the seek pending.

This hits any undo, redo or replace that removes text at or above the top of an
unwrapped viewport. When the stale byte is still inside the text, the result is
a frame with a mid-line top under the old line label. Main has the same
`editor_scroll_edit`/`editor_scroll_mirror` code, so it is very likely exposed
there too. That is inferred from the code, not run on main.

### Fix (commit 9edd330 after the rebase)

- `scrollctl.c:196` `editor_scroll_edit(e, off, old, add, old_nl, new_nl)` now
  transforms the displayed top with each edit:
  - Edit wholly above the top: exact byte shift plus `new_nl - old_nl` line
    shift.
  - Edit at the top byte: the top is unchanged.
  - Edit that replaces the byte before the top: the top moves onto the edit and
    `scroll_display_stale` is set.

  The transformed byte is also written into `v.state.first_byte`. The pending
  seek target is unchanged (same rebase rule as before).
- `scrollctl.c:182`, `editor_scroll_follow`: while the display is stale and the
  view is idle, it adopts the view's normalized top (a line start with a label)
  until the pending seek resolves. Every place that certifies the display
  clears the flag: init, place, repair-line, resident rows, view adoption,
  slice adoption, and `buffers.c` launch seek.
- `editor.c:161`: a backstop clamp so a top past EOF is never passed to layout.
  The primary fix alone also turns the tests green; checked by disabling the
  clamp.
- `input.c:293`: trusted history follow uses main's empty TYPE again. New edit
  plans keep `view_restore`, because both endpoints need normalizing.
- Decision note appended to `docs/decisions/edit-d50.md`.

### Red / green (sanitizer build `build/san/tests/editor_test`)

| Run | Exit |
| --- | ---: |
| red: `--default-comments` at d12e6f7 (`C all blank lines: FAIL`, editor_test:61 settle, :324 Ctrl+Z) | 1 |
| experiment: same with only `VIEW_TYPE` put back in `replay_follow` | 1 |
| red: new `--undo-shrink-top` before the fix (`follow at EOF: FAIL`, editor_test:433; the two other variants pass and stay as guards) | 1 |
| green: `--undo-shrink-top` (3/3 PASS) | 0 |
| green: `--default-comments` (all PASS) | 0 |
| green with the backstop clamp disabled: both selectors | 0 |

The new test `undo_shrink_top` (tests/editor_test.c, also in the default list)
uses 40 "lineNN" lines, a C path, 5 text rows, unwrapped. It has three
variants:

1. Select all, comment, view follows to EOF, undo.
2. The same, but wheel up 15 rows into the commented text before undo
   (straddle).
3. Comment lines 0-9, wheel to EOF, undo (text removed wholly above the top).

A submit hook checks every frame's top: inside the text, a line start, and
`first_line == piece_byte_to_line(top)`.

## Step 2: rebase onto main (bd2ecef: 73j, a2g, 5ih, 6di)

Conflicts, resolved semantically:

- `src/editor/buffers.c` `editor_activation_busy`: kept 73j's
  `find_replace_active` and d50's `EDITOR_ACTION_GOTO`.
- `src/editor/buffers.c` activation rows: kept 73j's `panel_rows` exclusion
  and d50's `go.active = false`.
- `src/editor/editor.c` step dispatch: GOTO continuation, then 73j wheel
  continuation, then layout.
- `src/editor/editor.c` `editor_get_stats.pending`: union of 73j's find/wheel
  terms and d50's GOTO.
- `src/editor/input.c` helpers: kept 73j's `pointer_unit_finish` and d50's
  goto prompt functions.
- `src/editor/input.c` `editor_handle_key` head: kept 73j's find-panel Escape
  and d50's goto staleness reset. Semantic addition: while the goto prompt is
  open it takes Escape/digits ahead of the find panel. Without this, typing a
  line number with the panel open went into the find field.
- `src/editor/paint.c`: kept 73j's `editor_hit_record` and d50's
  `editor_goto_overlay`.
- `src/editor/private.h` `struct editor`: kept 73j's find fields and d50's
  `editor_goto go`.
- `tests/editor_test.c` unimplemented-shortcut probe: Ctrl+Shift+F and Ctrl+P.
  Ctrl+F/H/F3 belong to 73j's panel; Ctrl+G is d50's prompt.
- `tests/editor_p4_test.c` decorations probe: Ctrl+F opens find (no notice),
  Escape, then Ctrl+R is the unimplemented probe (Ctrl+T is d50 transpose).
- `docs/worklog/2026-10-11.md`: kept both.

Found after the rebase: `default_notices` walked every
`KEYS_ARG_UNIMPLEMENTED` binding, including Ctrl+F/Ctrl+H/F3. 73j's panel
consumes those before keymap lookup, so editor_test:873 failed. The suite now
skips the bindings the panel owns (commit 896232d).

### Build and serial runs (display :99 via tools/with_display_lock.sh, private XDG_* dirs) [AC]

| Command | Exit |
| --- | ---: |
| `rm -rf build && make all` (gcc 13 release, -Werror) | 0 |
| keys_test sanitizer | 0 |
| keys_test release | 0 |
| editor_test sanitizer (full default; first run failed default_notices, rerun after 896232d) | 0 |
| editor_test gcc release (typing rows: `null/raster/gl 10000 keys mallocs=0 guard=active`, review 21 `mallocs=0 guard=active`) | 0 |
| editor_p4_test sanitizer | 0 |
| findui_test sanitizer | 0 |
| x11_input_test sanitizer | 0 |
| cli_test sanitizer (its FAIL lines are the forced-timeout negative control) | 0 |
| editor_scroll_test sanitizer | 0 |

## Go-to-line in the real app (verifier P3): did NOT scroll before the go-to-line fix (fixed below)

Setup:

- `build/sublimite` on :99, `wrap = false` in a private config.
- A 5000-line file.
- Steps: Ctrl+G, type 4000, Enter, then Down Down.

Screenshots: `build/design/d50-goto-1-preview.png`, `-2-accepted.png`,
`-3-after-down.png`.

Result:

- The prompt reads `Go to line: :4000`, but neither the preview nor Enter
  scrolls the view; lines 1-77 stay on screen with no caret visible.
- The caret did move: after Down Down the view jumps to lines 3926-4002.

Cause (not fixed, as instructed): `goto_continue` (input.c) writes
`v.state.first_byte/first_line` and calls `editor_full_layout` without seeking
or placing the 3kk scroll state. `editor_scroll_mirror` then restores the old
display top. The fix would be `editor_scroll_place` (or `editor_scroll_seek`)
in `goto_continue`, as d12e6f7 did for Ctrl+K Ctrl+C. This affects the trial
(go-to-line looks like it does nothing).

## Added scope: go-to-line scrolling fixed

The coordinator added this to the scope. Fix in `src/editor/input.c`:

- `goto_scroll_to` places or seeks the 3kk scroll state to the target line
  start on preview and on accept.
- `goto_restore` is used by Escape and by an invalid entry. It restores the
  view state plus the `scroll_display` snapshot taken when the prompt opened
  (`editor_goto.original_scroll`, private.h).

Red/green: new `editor_test --goto-scroll` (in the default list). Setup: 400
lines, 5 text rows, unwrapped. The wheel puts the pre-prompt top at line 30;
Ctrl+G, then type 300.

- Preview and Enter: the submitted top must contain line 299 and the view must
  agree with the layout. After Enter, one Right motion must keep the top there.
- Escape: the layout and view top must return to line 30 and byte 240, with
  the caret back at 0.
- The frame-top probe from `undo_shrink_top` must report no bad tops and no
  mallocs.

| Run | Exit |
| --- | ---: |
| red: `build/san/tests/editor_test --goto-scroll` before the fix (editor_test:500: the view top was mirrored back to the stale display; both cases FAIL) | 1 |
| green: same after the fix (2/2 PASS); release `--goto-scroll`, `--default-goto`, `--default-goto-cold`, `--undo-shrink-top`, `--default-comments`, `--default-center` | 0 |

Serial runs after this change (:99, display lock, private XDG dirs). Power
changed during the session: [bat] Discharging for the keys and editor runs,
[AC] Not charging for the rest.

| Command | Exit |
| --- | ---: |
| `make all` (incremental, gcc release -Werror) | 0 |
| keys_test sanitizer / release [bat] | 0 / 0 |
| editor_test sanitizer, full [bat] | 0 |
| editor_test gcc release, full [bat]: `null/raster/gl 10000 keys mallocs=0 guard=active`, review 21 `mallocs=0 guard=active` | 0 |
| editor_p4_test sanitizer [AC] | 0 |
| editor_scroll_test sanitizer [AC] | 0 |
| cli_test sanitizer [AC] | 0 |

Real app on :99. Setup: 5000-line file, `wrap = false`. Steps: Ctrl+G, type
4000, Enter.

- `build/design/d50-goto-4-fixed.png`: the top is line 4000.
- Ctrl+G 5000 preview (`d50-goto-5-eof-preview.png`): the top clips to line
  4925 and line 5000 is the last row, via the seek path.
- Escape (`d50-goto-6-escape.png`): returns to the pre-prompt top, line 4000.
- No caret is visible at column 0 in these captures (also true before the
  fix). It may be the blink phase; not investigated.

## Out of scope (reported, not fixed)

- The goto overlay draws on the last grid row. With 73j's find panel open,
  that is the panel's last row; the prompt covers it while active.
- Replace-all that straddles the top now shows the edit's line under an unknown
  label until the pending seek resolves (no view normalization on that path).
  Before, it showed the stale byte. No error either way.
- LeakSanitizer ran normally on these runs (no ptrace).
