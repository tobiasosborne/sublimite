# edit-cxho.2 — window commands with the find panel open

The coordinator's Sublime-like focus rule supersedes the find-chord part of
edit-o20. A focused find/replace field owns printable input, Left/Right/Home/End
(including Ctrl/Shift), BackSpace/Delete, Ctrl+A/C/X/V/Z/Y, Enter/Shift+Enter,
Tab and Escape. Find navigation and options retain Enter, F3/Shift+F3 and
Alt+R/C/W/Enter. Alt+Enter retains its existing field no-op: find-all is outside
this bead. The field has no undo history, so Ctrl+Z/Y remain field no-ops and
cannot undo or repeat a document edit. Copy/cut use the existing field
select-all state and clipboard arena; cut changes the field only after
successful clipboard publication.

The physical-key entry point offers only those editing keys to the field.
Unowned keys keep the normal binding lookup and dispatch, including close,
tab selection/cycling, save/save-as, new/open/reopen, go-to-line, palette/menu,
font size, find in files and quit. Only consumed field input resets a pending
keymap prefix. This replaces the small window-command whitelist and the
consumed-unknown-key fallback, so new window commands inherit normal routing.

Menu and palette commands enter the command dispatcher directly, bypassing
physical field-key interception. The palette overlays the retained panel;
closing the palette restores it. Tab commands call the existing
editor_find_service source reset before returning, so the active source and
search work change in the same turn, even if the switch frame already retired.
Counts and highlights come from that reset, including the existing empty
buffer after the last tab closes. The existing modified-close prompt retains
priority over field input.

Click-based focus transfer is deferred to edit-cxho.3. No allocation, duration
or capacity budget is added. Numeric literals in the scoped implementation
are structural (P): zero is absence, success or a first byte/index; unit steps
and string terminator removal follow existing API/index conventions. Test
geometry and watchdogs reuse the preceding panel fixtures; expected lengths,
match counts and tab counts derive from their strings and explicit operations.
The allocation expectation is zero (P), from the binding no-malloc rule.
