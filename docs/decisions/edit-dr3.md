# edit-dr3: gutter clicks select whole lines (Sublime rule)

Supersedes the caret-placement choice in edit-m15. Astra drive 3
(docs/reviews/astra-vs-sublime-3.md §15) observed Sublime selecting the line on
a gutter click; sublimité now follows it.

- **Click**: a primary press in the gutter selects the clicked logical line,
  anchor at its start, caret after its newline. A last line without a trailing
  newline selects to EOF (VIEW_SELECT_LINE's existing end rule). Wrapped
  continuation gutters still identify the logical line (edit-m15's
  `pointer_byte` is unchanged).
- **Drag**: a gutter press sets the drag unit to mode 3, the same whole-line
  unit the triple click already uses. Motion over gutter or text extends by
  whole lines around the originally selected line through the existing
  `pointer_unit_finish` union; edit-48u's frame-tick autoscroll re-hit uses the
  same motion path, so autoscrolling gutter drags extend by lines too.
- **Shift+click**: the retained anchor becomes the drag's fixed range
  (`text_drag_lo == text_drag_hi == anchor`) and the clicked line is unioned
  non-initially: a line after the anchor puts the caret after its newline, a
  line before puts it at its start, the anchor stays. Holding the button and
  dragging continues by lines from that same anchor. This is the brief's
  "extends to that line"; Sublime's exact Shift rule for a prior multi-line
  range was not re-measured (E).
- **Click chains**: unchanged. Gutter presses never join the text click chain
  (edit-m15), and edit-48u's per-buffer/source/button chain identity is kept.

The change is confined to the press branch of `editor_pointer` in
src/editor/input.c. It reuses `view_command(VIEW_SELECT_LINE)` and the
existing pointer-unit state, so no allocation or new state is added on the
pointer path; the dr3 regression runs under the allocation guard.
