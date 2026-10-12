# edit-g02: find-panel keyboard behavior

Panel Return and keypad Enter advance from findui's query preview, including
the initial preview. Shift reverses the direction; navigation keeps the panel
visible. Preserve the existing hidden-panel/keymap navigation behavior, and
the existing Ctrl+Enter and Ctrl+Alt+Enter replacement paths.

When navigation requests an uncached match ordinal, clear its direction after
the request. (P) Zero means no additional advancement is pending. The completion
selects that ordinal rather than advancing again, including for Shift+Return.

Dispatch `replace_next` through `editor_find_action`, using the existing
replacement continuation when the replacement panel is visible. The default
Ctrl+Shift+H row becomes implemented. Action overrides therefore disable the
default shortcut through the same config lookup used by edit-x8i. Hidden-panel
and find-only invocations consume the action without mutating the document.
Focused field keys retain edit-x8i's physical-key precedence.

Reuse the existing match selection, undo, mutation host, journal, layout and
prepaid find storage. No input or scrolling integration changes, new allocation,
capacity or performance budget are needed.

(P) Test offsets and read sizes derive from fixture string lengths. Zero is
the API success value, empty state or initial byte; one is the C terminator,
single event or API direction magnitude. Fixture columns reuse the minimap's
eight columns plus the tab slot's sixteen columns. Rows are the tab row,
document row and replacement panel's query, replacement and status rows.
Allocation expectations derive from the binding no-malloc rule.
The uncached-match fixture has one literal-byte match per byte, with size equal
to the existing match cache capacity plus the query byte length. Its wrapped
previous match therefore lies just beyond the cached prefix.

The supplied bead defines the Sublime behavior. Its cited Astra review is
absent from this worktree; no review file is reconstructed or edited here.
