# Configurable find actions

Find, replace, next match, previous match, regex, case sensitivity and whole-word
toggles use the binding returned by `config_keys_lookup`. The find handler no
longer recognizes their physical shortcuts independently of the keymap. Action
overrides therefore remove the original shortcuts using the same lookup policy
as other editor commands.

Keep `editor_find_key` as the panel-local text editor and cancellation handler.
Add a separate resolved-action dispatcher, called immediately after keymap
lookup in `input.c`. Keep the early panel Escape and the replacement-loop Escape
call sites unchanged to minimize overlap with the parallel cancel/prompt bead.

Focused field editing takes precedence for its reserved physical keys: Escape,
Enter, Tab, cursor motion, deletion, select-all and paste. Other remapped find
actions can execute while the panel is focused; unrelated global editing chords
remain consumed. Reset chord state when the local panel consumes input so a
discarded global prefix cannot affect subsequent field editing.

The toggle actions are `toggle_regex`, `toggle_case_sensitive` and
`toggle_whole_word`, defaulting to Alt+R, Alt+C and Alt+W. Toggles remain no-ops
while the panel is hidden, preserving the existing default behavior. Hidden-panel
match navigation retains its query and does not expose the panel.

Clear the unimplemented capability only for find, replace and next/previous
match default rows, including the dedicated Find keysym. Other find-related
capabilities stay marked unavailable. The unavailable-action regression should
need no special shortcut exclusions.

Tests use the null renderer and parse real keymap configuration into editor
preferences. (P) fixture offsets derive from C string lengths; geometry derives
from the existing minimap and tab slot dimensions plus the document/panel rows.
No new timing deadline, capacity or performance budget is introduced.
