# edit-m15: gutter hits and find-panel input isolation

Gutter clicks place the caret at the logical line start. This is the
caret-placement alternative explicitly allowed by the worker brief, rather
than Sublime's whole-line selection. Wrapped continuation gutters identify the
same logical line. Empty rows map to EOF. Pointer dispatch branches on the
current gutter width before consulting text hit records, using prepared row
metadata after the existing `scroll_frame_hit` transform. Gutter clicks do not
advance the text multi-click chain; Shift retains its existing anchor behavior.

The supplied baseline already initializes all columns' hit records at the
start of each unwrapped row. Keep that initialization, which covers both newly
filled rows and widened gutters. Wrapped rows initialize their text hits to
the visual row start and then write their gutter hits to the logical line
start. No hit-table clearing or source scan is added to pointer dispatch.

While the visible find panel owns keyboard input, Ctrl+V inserts clipboard
bytes into the focused query or replacement field. The clipboard controller
records that destination across native asynchronous replies; its existing
busy state parks subsequent input until completion. Field replies bypass
document mutation, undo and clipboard mutation staging. The null backend uses
the same receive path with its retained clipboard blob. Field normalization
uses bounded stack storage of `FINDUI_QUERY_BYTES`, the existing field limit
(G); raw replies exceeding that limit or edits exceeding the field capacity
are rejected without changing the document. CRLF normalization matches
document paste.

Ctrl+Backspace and Ctrl+Left/Right operate on grapheme boundaries in the focused
field, using the view's word/separator/whitespace classification. Ctrl+A
selects the whole focused field. Ctrl+X is a consumed no-op, explicitly allowed
by the brief. Unsupported Ctrl/Super/Alt chords are consumed. Existing
Ctrl+F/H, F3/Shift+F3, Escape, Enter/Shift+Enter, replacement Ctrl(+Alt)+Enter,
and Alt+R/C/W behavior is retained. Configurable panel key lookup belongs to
edit-x8i and is unchanged here.

If an approximate scroll adoption puts the top byte after the find anchor,
horizontal follow returns success without changing horizontal scroll. The
next seek can correct the view. That temporary positioning state must not
become the editor's sticky fatal error.

The headless regressions derive byte offsets from their fixture strings and
use existing mouse/panel geometry. The decimal gutter fixture uses the brief's
99-to-100 logical-line transition (G). The approximate-follow fixture injects
the adopted state directly to avoid relying on index-worker timing. Allocation
guards cover typing to fill the viewport, gutter dispatch, clipboard field
insertion and the field-editing chords; the full release editor suite retains
the existing typing guard. No heap allocation or reservation growth is added
on the typing or pointer paths.
