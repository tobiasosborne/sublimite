# Active modal input and status ownership

The session brief supersedes the older palette-click acceptance text: a primary
text click dismisses Go to Line, Palette or Menu, focuses the document, and places
the caret in that click. Hit testing uses the painted geometry before removing
panel/menu rows. A Go to Line preview click adopts the painted viewport; Escape
and window commands retain cancellation's restoration of the original viewport.

`editor_active_modal` computes ownership from the existing buffer, prompt, panel
and menu state. It adds no cached modal state. Close/Save takes priority, followed
by a stale-source answer, navigation/path input, palette, active menu, pending
open and find/find-in-files. A pinned idle menu bar does not take input ownership.

Go to Line and Palette retain their physical field keys, including clipboard and
undo chords; existing field no-ops remain local. Window bindings dismiss the
transient before ordinary command dispatch. This also covers configured bindings
and preserves keymap prefixes for window commands. Reopening the same transient
dispatches its opening command again. Explicit palette/menu commands keep their
command intent, including document Undo beneath a retained find field.

Middle-click is swallowed while Go to Line, Palette or an active Menu is open;
it retains that transient and never pastes behind it. This chooses the swallow
policy explicitly permitted by the brief; no new Sublime measurement is claimed.
Wheel and motion also stay with those transients. Palette query/blank rows retain
focus; clicking a visible command row selects and executes it through the same
dispatcher as Enter, using the painter's scrolling offset.

Close/Save swallows mouse input, including tab/menu/minimap presses, middle-click
and wheel input. Neither physical window chords nor explicit menu dispatch can
replace the unanswered prompt. Drops are refused immediately rather than queued:
the borrowed platform payload needs no extra lifetime or allocation. The prompt
itself displays `Drop refused` alongside its existing answer choices and remains
answerable. An embedded boolean records that notice until the prompt ends.

The last status row has a single overlay selector: Close/Save wins, then the
source-change answer, navigation/path input, Palette/Find panel, and ordinary
save/error/recovery status. Menu chrome uses its own rows and retains the
underlying status owner. Hidden ordinary notices cannot overwrite a field.

Numeric constants in the scoped code/tests are structural (P): zero means
success, absence or first index; unit steps advance rows/bytes or remove C
terminators; PRIMARY/primary button identities follow the platform API. Hit
coordinates and visible command indices derive from painted grid dimensions and
scroll offsets. Fixture geometry/watchdogs reuse the preceding matrix (E).
Expected positions derive from fixture string lengths (P), including the trailing
newline's next line. No allocation, duration or capacity budget is introduced.
