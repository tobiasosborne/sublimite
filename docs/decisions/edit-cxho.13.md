# edit-cxho.13 — selection prefill and slurped find commands

Find and Replace copy a non-empty single-line document selection into the
existing arena-owned query, quoting regex metacharacters when regex is active.
An empty or multiline selection retains the query. The focused field selects
its contents in either case. A successful seed requests the saved-origin
preview defined by edit-cxho.12; refocusing retains that origin.

Slurp Find and Slurp Replace use the document selection, falling back to the
view's existing bounded Unicode word resolver at an empty selection. They
retain panel visibility, field focus and the document selection. Replacement
bytes remain literal, matching the existing replacement engine. Query quoting
expands backwards in the existing editor field scratch before calling findui;
oversized input or quoted output returns a limit error and preserves the old
field. No allocator or capacity is added.

Find Under uses that seed and seeks from its end; Find Under Previous seeks
from its start and takes the preceding ordinal. An empty-selection word seed
enables whole-word matching; an explicit selection uses substring matching.
Closed-panel next/previous use the current document range with the retained
query. The seek uses findui's worker-owned immutable match plan and keeps its
pending state separate from the panel's saved preview origin. Queued navigation
advances the pending result once.

The view exposes a sole cursor/anchor pair, so Alt+F3 retains its unimplemented
flag. Multiple selections and find-all belong to edit-cxho.14.

Numeric literals are structural (P): zero is absence/success/first byte; unit
steps walk a half-open range, remove a C terminator, or add a quoting backslash;
negative/positive unit directions follow findui's navigation contract. Test
offsets, counts and lengths derive from their literal strings (P), geometry
reuses the existing find-focus fixture (E), and zero allocations follows the
binding typing-path rule (P). Existing word-resolution scan bounds are reused.
