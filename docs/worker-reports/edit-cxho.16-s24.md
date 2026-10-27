# edit-cxho.16 — modal interaction matrix

One bead in the assigned worktree. No production code changes or new state
accessors; the existing private editor header and headless drop seam suffice.
No `bd` or `git`, installation, owner-profile changes, coordinator-document
edits, refactors, or additional review pass.

Evidence notation: raw run output and process exit statuses below are
(M)[AC]. Display `:99`, parallelism and compiler versions come from the brief
(E). Structural counts (P) derive from source tables: thirteen modes times
twenty actions; two focus states times six commands times two routes; two
click/type and two live-job witnesses. Geometry/watchdogs reuse existing
fixtures (E); bytes, index steps, C terminators and button identities follow
the fixture strings and APIs (P). There are no new budgets or GB fixtures.

## Files touched

- `tests/modal_matrix_test.c`: independent fixtures, mode/action table,
  complete state expectations, strict XFAIL pins, command equivalence,
  click/type, and live Find Results lifecycle witnesses.
- `Makefile`: add the automatically discovered binary to display-lock routing.
- `docs/decisions/edit-cxho.16.md`: scoped test design and ownership policies.
- `docs/reviews/use-findings-2026-10-27/interaction-matrix-pinned.md`: final
  PASS/XFAIL matrix, supplemental tables and one-line XFAIL reasons.
- This report. Raw evidence is retained under `build/cxho16-s24/`.

## Test design and pinned policies

Each cell starts from private file-backed documents, a saved edit with undo
history, a retained closed tab for reopen, and PRIMARY captured by editor
selection/copy commands. Open checks its intermediate prompt before Escape;
Save has a dirty edit to save. Both tab index and retained document identity
are checked, along with revisions, modified state, panel visibility, field
text, actual field/document focus, effective keyboard owner, go-to/palette/
menu/close/source prompts, painted row owner, results activation, captures,
geometry and error-free input/settle. Mouse-modal cells also check selection
endpoints, detecting caret movement without mutation.

Find/Replace fields own physical query undo. Explicit menu/palette Undo
retains document-command intent, matching the landed window-command decision.
The completed Find Results row uses a real no-match search. Mailbox
backpressure separately holds a real live search while Close or Escape
reaches the private dispatcher before completion can mask the outcome.
Replace means its editable panel; active replace-all parking/overflow retain
the landed editor-suite coverage. All ordinary matrix cells use injected
platform events through the editor loop.

The close/save prompt retains keyboard window commands until answered;
Escape closes that prompt and reveals Find. Dropping another document
currently loses the unanswered prompt: this is an XFAIL. Reload/Keep is
reached by a real external rewrite of a dirty copied file. It permits the
existing window-command whitelist, ignores ordinary edits and refuses Save
until answered. Go to Line should dismiss for global commands; its current
swallowed chords are XFAILs. Text and query undo remain field-owned.
Escape removes only the highest transient; the menu first loses its dropdown,
retaining its bar. No inconsistent behavior was fixed in this bead.

## Red runs

The first valid complete run preceded XFAIL annotations. The find window
commands and click/type already passed because the dependency fixes had landed.
The intended-state failures were observed, then annotated and pinned rather
than repaired. Setup bring-up corrected an oversized synthetic text event and
menu hit geometry before this recorded run; these were test-fixture mistakes.

Build: `make -j4 build/san/tests/modal_matrix_test`, exit 0 (M)[AC].
Run (same invocation for each recorded matrix execution):

```sh
task_runtime=$(mktemp -d)
XDG_RUNTIME_DIR="$task_runtime" DISPLAY=:99 EDIT_DISPLAY=:99   EDIT_REFUSE_REAL_DISPLAY=1 ASAN_OPTIONS=detect_leaks=1   sh tools/with_display_lock.sh build/san/tests/modal_matrix_test
rmdir "$task_runtime"
```

The test command's exit was 1 (M)[AC]. Failure lines and summary:

```text
FAIL go to line x click text rc=0
FAIL go to line x middle paste rc=0
FAIL command palette x Ctrl+W rc=0
FAIL command palette x Ctrl+Tab rc=0
FAIL command palette x Ctrl+Shift+Tab rc=0
FAIL command palette x Ctrl+PgUp rc=0
FAIL command palette x Ctrl+PgDn rc=0
FAIL command palette x Ctrl+N rc=0
FAIL command palette x Ctrl+O then cancel rc=0
FAIL command palette x Ctrl+S rc=0
FAIL command palette x Ctrl+Shift+T rc=0
FAIL command palette x Ctrl+G rc=0
FAIL command palette x Ctrl+F rc=0
FAIL command palette x click text rc=0
FAIL command palette x middle paste rc=0
FAIL menu open x click text rc=0
FAIL menu open x middle paste rc=0
FAIL close/save prompt x click text rc=0
FAIL close/save prompt x middle paste rc=0
FAIL close/save prompt x drop rc=0
FAIL running results x Ctrl+W rc=0 running=1 (want cancelled)
modal_matrix_test: 267 PASS, 21 FAIL (P: enum cross product plus command/click witnesses)
```

The initial policy model treated Go to Line as a keyboard-blocking prompt.
Tightening it to the brief's global-command rule produced an additional red
run before annotating those cells. The same sanitizer build/run commands
returned 0 for the build and 1 for the test (M)[AC]:

```text
FAIL go to line x Ctrl+W rc=0
FAIL go to line x Ctrl+Tab rc=0
FAIL go to line x Ctrl+Shift+Tab rc=0
FAIL go to line x Ctrl+PgUp rc=0
FAIL go to line x Ctrl+PgDn rc=0
FAIL go to line x Ctrl+N rc=0
FAIL go to line x Ctrl+O then cancel rc=0
FAIL go to line x Ctrl+S rc=0
FAIL go to line x Ctrl+Shift+T rc=0
FAIL go to line x Ctrl+F rc=0
FAIL go to line x Ctrl+Shift+P rc=0
modal_matrix_test: 256 PASS, 21 XFAIL, 11 FAIL (P: enum cross product plus command/click/lifecycle witnesses)
```

## Green run and verification

After adding the explicit observed-state XFAIL table, `make -j4 all` exited
0, `make -j4 build/san/tests/modal_matrix_test` exited 0, and the matrix
invocation above exited 0 under ASan/UBSan with `detect_leaks=1` (M)[AC].
Sanitizer stderr was empty. Full green matrix output is copied verbatim to
`docs/reviews/use-findings-2026-10-27/interaction-matrix-pinned.md`.
The green XFAIL table and summary are:

```text
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
```

All supplemental menu/palette command and click/type witnesses passed. The
running-search Escape witness passed; closing its results tab is the lifecycle
XFAIL. Source-table arithmetic (P): 260 matrix cells + 24 command routes +
2 click/type + 2 lifecycle = 288 witnesses; 256 PASS + 32 XFAIL = 288.
The XFAIL list splits into 31 modal-routing/mouse cases and the sole lifecycle
case (P: count the corresponding bead identifiers in the explicit table).

Full verification completed on the final matrix binary. Commands and process
results (M)[AC]:

| Command | Exit |
|---|---|
| `make -j4 all` (gcc release, C11 and required warning flags) | 0 |
| `make -j4 build/san/tests/modal_matrix_test` (clang ASan/UBSan) | 0 |
| `ASAN_OPTIONS=detect_leaks=1 DISPLAY=:99 EDIT_DISPLAY=:99 EDIT_REFUSE_REAL_DISPLAY=1 XDG_RUNTIME_DIR=<fresh private directory> sh tools/with_display_lock.sh build/san/tests/modal_matrix_test` | 0 |
| `ASAN_OPTIONS=detect_leaks=1 DISPLAY=:99 EDIT_DISPLAY=:99 EDIT_REFUSE_REAL_DISPLAY=1 XDG_RUNTIME_DIR=<fresh private directory> make -j4 check` | 0 |

The Makefile routed the display suites, including the new matrix and the
brief's editor/scroll/large/session/hot-exit/CLI/raster suites, through
`tools/with_display_lock.sh`. The full run's summary (M)[AC]; its binary
count is derived from the Makefile's discovered test list (P):

```text
modal_matrix_test: 256 PASS, 32 XFAIL, 0 FAIL (P: enum cross product plus command/click/lifecycle witnesses)
check: 113 test binaries passed
display lock contract: owner PID, alias/timeout, make/scripts, nested inheritance, exit status PASS
make -j4 check exit=0
```

The replay CLI shell contract also passed. Native EGL/GL/Present cases
reported their existing unsupported-Xvfb skips; raster and the other display
suites ran on the reachable assigned socket. The display-lock contract's
intentional timeout messages were followed by its PASS result.

Power/load and `df -h /` directly before the full check (M)[AC]:

```text
Charging
 14:20:01 up 1 day, 16:23,  1 user,  load average: 5.01, 3.27, 2.38
Filesystem               Size  Used Avail Use% Mounted on
/dev/mapper/vgmint-root  1.8T  173G  1.6T  10% /
```

Power/load and disk directly after the full check (M)[AC]:

```text
make -j4 check exit=0
Charging
 14:34:20 up 1 day, 16:37,  1 user,  load average: 2.21, 3.00, 3.03
Filesystem               Size  Used Avail Use% Mounted on
/dev/mapper/vgmint-root  1.8T  175G  1.6T  11% /
```

The AC supply reported online (`1`, P: the kernel's boolean encoding;
M)[AC]. No GB-scale fixture was introduced by this bead. New temporary
fixtures are removed after each cell, including setup/assertion failure paths.
Private runtime directories were removed after each run.

## Scope boundaries and execution notes

The XFAIL table is the follow-up list: palette/go-to chord routing, prompt
and menu mouse/focus gating, and the dropped unanswered close prompt belong
to `edit-cxho.17`; a live search surviving results close belongs to
`edit-cxho.18`. XFAIL requires both disagreement with the intended state and
an exact match to the observed failure. XPASS or a changed failure is red;
errors never qualify. The final pinned matrix contains every affected cell.

An initial sandboxed sanitizer run could not complete LeakSanitizer's thread
inspection. Required runs use the approved process-sandbox escalation, with
leak detection enabled and only the assigned display. No automatic approval
review rejection occurred.

A preparatory recursive test-build command accidentally included Windows-only
fixtures and exited 2 because `windows.h` is unavailable to the Linux compiler
(M)[AC]. This was an invocation error; no Windows fixture was changed. The
actual `make check` uses only its normal top-level test glob.

Nothing remains within this test-only bead. The explicitly listed XFAILs
remain for the supplied follow-up beads; no out-of-scope fixes were made.
No additional issue was found beyond those pinned modal/lifecycle behaviors
and the execution/environment notes above.
