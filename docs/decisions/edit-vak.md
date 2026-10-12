# edit-vak: config reload scroll regression

Keep the regression in `tests/config_editor_test.c` so it exercises the actual
file watcher, preferences publication, geometry adoption, layout and submission
with the null renderer. The fixture opens a file, rather than installing scroll
state directly, and uses wheel input while wrapped to leave the dormant logical
scroll state behind the displayed viewport.

The file has a leading tab and a repeated line-specific ASCII character on each
line. Its geometry reuses the existing config fixture: (P) minimap width plus a
tab slot gives the surface columns; chrome plus the existing text rows gives
the surface rows. A surface-width character run necessarily wraps in the
narrower text area. (E) the source has two surface heights plus the public wheel
notch displacement in logical lines, providing room for a scrolled viewport
without selecting an EOF boundary. Source bytes bound the search for a wrapped
continuation row; no new duration or retry budget is introduced.

On wrap-off reload, preserve the logical line start (`first_byte`); the wrapped
continuation's `visual_byte` is normalized to that anchor by the existing view
policy. The resident logical scroll state and displayed scroll state must agree
with that anchor before submission. Here “resident window origin” means the
owned logical viewport, not the worker byte-copy cache's aligned start.

Check the submission that adopts the new preferences generation, using the
existing IO/submission probe. (P) The required frame is the current begun frame
or its immediate successor when idle, as specified by the bead. Compare the
geometry generation with its immediate successor, the submitted scroll origin,
view/layout row starts, visible line identities after tab expansion, and exact
label state. This detects an incorrect submitted frame even if later work
repairs the view. Check the settled state as well.

Exercise wrap off, tab-width change while unwrapped, wrap on, tab-width change
while wrapped, and wrap off again. Tab widths reuse the existing narrow/wide
config cases. Temporarily disable only the preferences resync block for the red
probe, then restore the original source for green verification. No new runtime
allocation or production behavior is intended.
