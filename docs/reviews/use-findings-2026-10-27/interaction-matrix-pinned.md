# Pinned modal interaction matrix

This is the output of `tests/modal_matrix_test.c` on the assigned display
`:99` (E: supplied brief). It supersedes the behavior observations in
`interaction-matrix.md`; the original research is unchanged. No production
code changed in this bead.

PASS means the full resulting state matched the intended rule. A required
answer prompt can legitimately consume a chord: PASS does not necessarily
mean the chord executed. XFAIL means the intended rule fails and the test
also asserts the exact observed behavior. Any XPASS or changed failure
fails the suite. Errors are never expected failures.

Counts derive from the source tables (P): thirteen mode rows times twenty
actions gives 260 cells; two find focus states times six commands times two
menu/palette routes adds 24; two click-then-type witnesses and two live-job
witnesses give 288 total. The result is 256 PASS plus 32 XFAIL. These counts
are structural, not budgets. All test-output numbers below are derived from
those tables (P). The display identity and toolchain are the supplied brief's
inputs (E); execution was on AC (M)[AC], BAT0 `Charging`.

State assertions cover tab count, active index and document identity, active
and original-document revision, modified state, panel/replace/Where modes,
retained query and field text, field/document and effective keyboard focus,
palette/menu/go-to/close/source-change visibility, painted status-row owner,
results activation, pointer captures, geometry, and successful return codes.
Open checks the intermediate Open File prompt before cancelling it. Save
starts from a dirty document where applicable. Modal click witnesses also
assert the selection, exposing caret motion without a document edit.

The fixtures keep Find beneath Go to Line, Palette, Menu and Close/Save to
exercise overlay priority. Replace means the editable Replace panel; active
replace-all parking and overload remain covered by the landed parked-input
suite in `editor_test`. Results in the main table are completed no-match
results; the live-job table separately holds a real search on mailbox
backpressure. The ordinary matrix injects platform events through the editor
loop. Live-job witnesses use the existing private command entry point before
draining worker completion, as the existing find-in-files harness does.

The standalone menu/palette command table compares command intent with its
document chord. Physical Ctrl+Z in a find field remains a field no-op; an
explicit menu/palette Undo intentionally targets the document, per the landed
window-command decision. Click-then-type must edit the document and preserve
the query.

Prompt policies pinned here: Close/Save owns keyboard chords until answered;
Escape dismisses it and reveals the retained Find panel. Go to Line should
dismiss for global window chords; line-entry text and query undo remain
field-owned. A primary text click should cancel it and
focus the document. Reload/Keep admits existing window commands, ignores
ordinary editing, and refuses Save while the source answer is pending.
Palette query undo has no history and remains a no-op. Escape closes the
highest transient; an open menu first loses its dropdown, retaining the bar.

Mouse gating failures and the dropped unanswered close prompt belong to
`edit-cxho.17`; a live search surviving results-tab close belongs to
`edit-cxho.18`. The exact affected cells and one-line reasons follow.

| Mode / Action | Ctrl+W | Ctrl+Tab | Ctrl+Shift+Tab | Ctrl+PgUp | Ctrl+PgDn | Ctrl+N | Ctrl+O then cancel | Ctrl+S | Ctrl+Shift+T | Ctrl+G | Ctrl+F | Ctrl+Shift+P | Alt | Escape | click text | middle paste | type x | Ctrl+Z | resize | drop |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| none | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS |
| find field | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS |
| find document | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS |
| replace field | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS |
| find in files Where | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS |
| go to line | XFAIL | XFAIL | XFAIL | XFAIL | XFAIL | XFAIL | XFAIL | XFAIL | XFAIL | PASS | XFAIL | XFAIL | PASS | PASS | XFAIL | XFAIL | PASS | PASS | PASS | PASS |
| command palette | XFAIL | XFAIL | XFAIL | XFAIL | XFAIL | XFAIL | XFAIL | XFAIL | XFAIL | XFAIL | XFAIL | PASS | PASS | PASS | XFAIL | XFAIL | PASS | PASS | PASS | PASS |
| menu open | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | XFAIL | XFAIL | PASS | PASS | PASS | PASS |
| close/save prompt | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | XFAIL | XFAIL | PASS | PASS | PASS | XFAIL |
| Reload/Keep prompt | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS |
| find results active | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS |
| minimap drag | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS |
| text drag | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS |

| Find focus | Command | Menu | Palette |
|---|---|---|---|
| find field | Close | PASS | PASS |
| find field | New File | PASS | PASS |
| find field | Open File | PASS | PASS |
| find field | Save | PASS | PASS |
| find field | Goto Line | PASS | PASS |
| find field | Undo | PASS | PASS |
| find document | Close | PASS | PASS |
| find document | New File | PASS | PASS |
| find document | Open File | PASS | PASS |
| find document | Save | PASS | PASS |
| find document | Goto Line | PASS | PASS |
| find document | Undo | PASS | PASS |

| Find focus | click then type x |
|---|---|
| find field | PASS |
| find document | PASS |

| Running Find Results | Outcome |
|---|---|
| Ctrl+W | XFAIL |
| Escape | PASS |

XFAIL table (strict observed-state pins; XPASS fails):

| Mode | Action | Reason |
|---|---|---|
| go to line | Ctrl+W | edit-cxho.17: go-to swallows window Close chord |
| go to line | Ctrl+Tab | edit-cxho.17: go-to swallows next MRU tab chord |
| go to line | Ctrl+Shift+Tab | edit-cxho.17: go-to swallows previous MRU tab chord |
| go to line | Ctrl+PgUp | edit-cxho.17: go-to swallows previous adjacent tab chord |
| go to line | Ctrl+PgDn | edit-cxho.17: go-to swallows next adjacent tab chord |
| go to line | Ctrl+N | edit-cxho.17: go-to swallows New File chord |
| go to line | Ctrl+O then cancel | edit-cxho.17: go-to swallows Open File chord; no open prompt appears |
| go to line | Ctrl+S | edit-cxho.17: go-to swallows Save chord; dirty document stays dirty |
| go to line | Ctrl+Shift+T | edit-cxho.17: go-to swallows Reopen File chord |
| go to line | Ctrl+F | edit-cxho.17: go-to swallows Find chord |
| go to line | Ctrl+Shift+P | edit-cxho.17: go-to swallows Command Palette chord |
| go to line | click text | edit-cxho.17: click cancels go-to but leaves find field focus and caret unchanged |
| go to line | middle paste | edit-cxho.17: middle paste edits the document behind go-to |
| command palette | Ctrl+W | edit-cxho.17: palette swallows window Close chord |
| command palette | Ctrl+Tab | edit-cxho.17: palette swallows next MRU tab chord |
| command palette | Ctrl+Shift+Tab | edit-cxho.17: palette swallows previous MRU tab chord |
| command palette | Ctrl+PgUp | edit-cxho.17: palette swallows previous adjacent tab chord |
| command palette | Ctrl+PgDn | edit-cxho.17: palette swallows next adjacent tab chord |
| command palette | Ctrl+N | edit-cxho.17: palette swallows New File chord |
| command palette | Ctrl+O then cancel | edit-cxho.17: palette swallows Open File chord; no prompt appears |
| command palette | Ctrl+S | edit-cxho.17: palette swallows Save chord; dirty document stays dirty |
| command palette | Ctrl+Shift+T | edit-cxho.17: palette swallows Reopen File chord |
| command palette | Ctrl+G | edit-cxho.17: palette swallows Go to Line chord |
| command palette | Ctrl+F | edit-cxho.17: palette swallows Find chord |
| command palette | click text | edit-cxho.17: primary click moves the document caret behind palette |
| command palette | middle paste | edit-cxho.17: middle paste edits document and closes palette |
| menu open | click text | edit-cxho.17: text click dismisses menu but retains find field focus |
| menu open | middle paste | edit-cxho.17: middle paste edits behind open menu without transferring focus |
| close/save prompt | click text | edit-cxho.17: primary click moves caret and find focus behind close prompt |
| close/save prompt | middle paste | edit-cxho.17: middle paste edits document behind unanswered close prompt |
| close/save prompt | drop | edit-cxho.17: drop activates a new tab and silently cancels unanswered close prompt |
| running results | Ctrl+W | edit-cxho.18: closing live Find Results leaves its search running |
modal_matrix_test: 256 PASS, 32 XFAIL, 0 FAIL (P: enum cross product plus command/click/lifecycle witnesses)
