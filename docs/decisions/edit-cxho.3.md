# edit-cxho.3 — find field and document focus

The panel owns an explicit field/document focus kind. Opening Find, Replace
or Find in Files focuses the field and selects its contents. A mouse press
in the text, gutter or minimap focuses the document while retaining the panel,
query, count and highlights. Tab changes retain the focus kind. Panel clicks
restore field focus; query and replacement clicks place the byte caret using
the painter's cell geometry, including escaped bytes and horizontal scroll.
The existing Where field uses its own painted byte/cell mapping.

The detailed session brief governs the older bead description: document
focus sends Return, arrows, Tab, clipboard and undo keys through ordinary
document dispatch. Escape still closes the panel without changing selection;
F3/Shift+F3 navigate matches; Ctrl+F/H selects and focuses the field. Field
keys keep the ownership established by edit-cxho.2. Plain Tab in find-only
mode is consumed even when the platform supplies a literal tab byte.

The painter shares its status width and field scroll calculations with a
read-only hit test. Visible bracket cells toggle regex, case and whole-word
options; separating spaces and other status cells only focus the field.
Caret visibility follows the focus kind: the document layout suppresses its
caret during field focus, and panel composition restores ordinary ink over
the field caret during document focus. Only the focused caret is drawn.

Pointer focus changes repaint after normal pointer dispatch has used the
current hit map, so the focus repaint cannot invalidate document placement.
The enum and hit-test result use existing editor/stack storage. No allocator,
pool capacity or timing budget is added. Numeric literals are structural (P):
zero denotes absence/success/first index; unit steps advance cells or remove
C terminators; toggle spans derive from their painted string literals.
The status scratch size reuses the existing painter buffer. Test geometry
reuses the existing panel fixtures (E); offsets, lengths and match counts
derive from fixture strings and explicit operations (P). The allocation
expectation is zero (P), from the binding no-malloc typing rule.
