# edit-dr3 worker report (s6)

Usage at start: 5-hour 9.0 %, weekly 87.0 %. At end: 5-hour 10.0 %, weekly 87.0 %.
Power during runs: BAT0 "Not charging" (functional tests only, no measurements).

## Change
- src/editor/input.c, `editor_pointer` press branch only: a gutter press sets
  drag mode 3 (whole lines), selects the clicked line with VIEW_SELECT_LINE;
  Shift+gutter press fixes the retained anchor as the drag range and unions the
  clicked line non-initially. Drag/autoscroll (edit-48u) reuse the existing
  mode-3 motion path; click-chain identity is untouched.
- tests/editor_test.c: new `gutter_line_contract` (`--dr3-gutter`, in main
  suite, under the allocation guard): click row 1 -> [L,2L]; drag to row 3 in
  gutter and over text -> [L,4L]; drag to row 0 -> anchor 2L, caret 0; Shift
  +click row 4 -> [L,5L]; Shift+click row 0 -> anchor L, caret 0; "one\ntwo"
  row 1 -> [4,7] (EOF). L = sizeof "alpha beta\n"-1 = 11 (P, literal).
  The three edit-m15 gutter cases now assert line selection instead of caret.
- docs/decisions/edit-dr3.md.

## Red (sanitizer build, before input.c change)
```
== --dr3-gutter
editor_test:3914: FAIL e->v.state.selection.anchor==L && e->v.state.selection.cursor==2u*L
exit 1
== --m15-gutter
editor_test:3814: FAIL editor_view(e).selection.anchor==start && editor_view(e).selection.cursor==next
exit 1
== --m15-gutter-growth
editor_test:3876: FAIL editor_view(e).selection.anchor==start && editor_view(e).selection.cursor==start+sizeof line-1
exit 1
== --m15-gutter-wrap
editor_test:3890: FAIL editor_view(e).selection.anchor==0 && editor_view(e).selection.cursor==sizeof text
exit 1
```

## Green
```
edit-dr3 gutter: click/drag/Shift+click select whole lines passed      exit 0
edit-m15 gutter: filled viewport ignores stale old EOF passed          exit 0
edit-m15 gutter: decimal widening ignores old text hit passed          exit 0
edit-m15 gutter: wrapped continuation uses logical line start passed   exit 0
edit-48u drag autoscroll passed (unwrapped and wrapped), chain identity (buffer, button), overscan: exit 0
```
Full verification (display suites serially via tools/with_display_lock.sh,
DISPLAY=:99 EDIT_DISPLAY=:99, XDG_RUNTIME_DIR=/tmp/claude-1000/xdg-dr3):
- `make all`: exit 0
- san editor_test: exit 0, "editor_test: all passed"
- san x11_input_test: exit 0, "x11_input_test: ok"
- san editor_p4_test: exit 0, "editor_p4_test: all passed"
- release editor_test (allocation guard active): exit 0, "editor_test: all passed"

## Notes
- Sublime's exact Shift+gutter rule when the prior selection spans several
  lines was not re-measured; implemented as "extend from the anchor" (E).
- Besides the worktree add, I ran one read-only `git diff --stat` in the worktree.
