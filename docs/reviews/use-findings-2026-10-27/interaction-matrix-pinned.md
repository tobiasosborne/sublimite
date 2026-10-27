# Pinned modal interaction matrix

This is the output of `tests/modal_matrix_test.c` on the assigned display
`:99` (E: supplied brief), updated by `edit-cxho.17`. It supersedes the older
behavior observations in `interaction-matrix.md`; original research is unchanged.

PASS means the complete resulting state matched the intended rule. A required
answer prompt legitimately consumes window chords. The remaining XFAIL requires
both the intended-rule mismatch and the pinned running-job state; XPASS fails.
Errors are never expected failures.

Counts derive from source enums and witness arrays (P): thirteen modes times
twenty actions gives 260 cells; two find focus states times six commands times
three routes (menu, palette Enter, palette click) adds 36; five click-then-type
witnesses, five transient witnesses and two lifecycle witnesses give 308 total.
The result is 307 PASS plus one XFAIL: the preceding 256 PASS plus the promoted
31 cells plus 20 added witnesses. These are structural counts, not budgets.

The run used leak detection. Power/load stamp (M)[AC], copied from
`cat /sys/class/power_supply/BAT0/status` and `uptime` beside the run:

```text
Not charging
 14:54:08 up 1 day, 16:57,  1 user,  load average: 3.39, 3.97, 3.55
```

State assertions cover tabs/active document, document revisions/modified state,
retained find/replace/Where query and focus, palette/menu/navigation/close/source
visibility, painted status ownership, results activation, pointer captures,
geometry, selection and return codes. Open checks its intermediate path prompt
before Escape. Save starts dirty where applicable. Fixtures keep Find beneath
Go to Line, Palette, Menu and Close/Save to exercise overlay priority.

Go to Line and Palette retain field editing and query undo chords; window
commands dismiss them and dispatch normally. A primary text click dismisses
Go to Line, Palette or Menu, focuses the document and places the caret in that
click. This follows the session brief over the older palette-click swallow
acceptance text. Click-then-type edits the document and retains the find query.
A preview click uses the painted text before changing geometry. Palette rows
select and execute commands, with the same command intent as Enter/menu routes.

Middle paste is swallowed while navigation/path input, Palette or active Menu
owns input. Close/Save swallows mouse input and preserves its required answer;
drops are refused visibly with `Drop refused` in that prompt. Keyboard window
chords with Close/Save open retain their pinned behavior. Explicit menu dispatch
also cannot replace the answer. Reload/Keep retains the existing window-command
policy and refuses Save while its answer is pending.

Status priority is Close/Save, source-change answer, navigation/path input,
Palette/Find panel, ordinary save/error/notice status. Menu chrome retains the
underlying status owner. Escape removes the highest transient, with the menu
losing its dropdown before its latched bar. Field undo remains a local no-op;
explicit menu/palette Undo targets the document per the landed find decision.

The live-results Ctrl+W cell remains XFAIL for `edit-cxho.18`. Its worker is
held by mailbox backpressure; the ordinary results row uses completed results.
The ordinary matrix enters through `editor_inject`/`editor_step`; live-job and
explicit-command witnesses use the existing private dispatcher seam.

| Mode / Action | Ctrl+W | Ctrl+Tab | Ctrl+Shift+Tab | Ctrl+PgUp | Ctrl+PgDn | Ctrl+N | Ctrl+O then cancel | Ctrl+S | Ctrl+Shift+T | Ctrl+G | Ctrl+F | Ctrl+Shift+P | Alt | Escape | click text | middle paste | type x | Ctrl+Z | resize | drop |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| none | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS |
| find field | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS |
| find document | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS |
| replace field | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS |
| find in files Where | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS |
| go to line | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS |
| command palette | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS |
| menu open | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS |
| close/save prompt | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS |
| Reload/Keep prompt | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS |
| find results active | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS |
| minimap drag | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS |
| text drag | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS |

| Find focus | Command | Menu | Palette | Palette click |
|---|---|---|---|---|
| find field | Close | PASS | PASS | PASS |
| find field | New File | PASS | PASS | PASS |
| find field | Open File | PASS | PASS | PASS |
| find field | Save | PASS | PASS | PASS |
| find field | Goto Line | PASS | PASS | PASS |
| find field | Undo | PASS | PASS | PASS |
| find document | Close | PASS | PASS | PASS |
| find document | New File | PASS | PASS | PASS |
| find document | Open File | PASS | PASS | PASS |
| find document | Save | PASS | PASS | PASS |
| find document | Goto Line | PASS | PASS | PASS |
| find document | Undo | PASS | PASS | PASS |

| Find focus | click then type x |
|---|---|
| find field | PASS |
| find document | PASS |
| go to line | PASS |
| command palette | PASS |
| menu open | PASS |

| Transient witness | Outcome |
|---|---|
| go-to field keys | PASS |
| palette field keys | PASS |
| close prompt swallows menu title | PASS |
| close prompt refuses drop visibly | PASS |
| go-to preview click uses painted text | PASS |

| Running Find Results | Outcome |
|---|---|
| Ctrl+W | XFAIL |
| Escape | PASS |

XFAIL table (strict observed-state pins; XPASS fails):

| Mode | Action | Reason |
|---|---|---|
| running results | Ctrl+W | edit-cxho.18: closing live Find Results leaves its search running |
modal_matrix_test: 307 PASS, 1 XFAIL, 0 FAIL (P: enum cross product plus command/click/transient/lifecycle witnesses)
