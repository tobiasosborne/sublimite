# edit-cxho.17 — session s24 worker report

Implemented only the assigned modal-input bead. The session brief's one-click
palette dismissal supersedes the older caret-unchanged acceptance text. No `bd`
or `git` command was run; installed binaries/profiles, the main checkout, other
worktrees and the prohibited coordination documents were untouched.

Files touched:

- `src/editor/private.h`: computed modal enum/API and embedded drop-refusal flag.
- `src/editor/input.c`: field-key ownership, window-command dismissal/dispatch,
  and required-answer protection for explicit command dispatch.
- `src/editor/editor.c`: computed active modal, mouse gating before field/menu/
  clipboard routing, deferred geometry after placement, drop refusal, shared
  status selection in normal submission and render repair.
- `src/editor/buffers.c`: Close/Save remains the owner above menus/transients.
- `src/editor/paint.c`: status-overlay priority and visible drop-refusal wording.
- `src/editor/palettepanel.c`: painted-row hit testing and shared Enter/click
  command execution.
- `src/editor/menubar.c`: outside presses continue to field/document placement;
  the caller updates geometry afterwards.
- `tests/modal_matrix_test.c`: promoted the owned XFAILs; asserted caret placement
  and swallowed PRIMARY; added palette-click, click/type and transient witnesses.
- `tests/editor_test.c`: replaced obsolete menu-click/hidden-close expectations
  with the supplied one-click and required-answer policies.
- `docs/decisions/edit-cxho.17.md`,
  `docs/reviews/use-findings-2026-10-27/interaction-matrix-pinned.md`, this report.

Design: middle-click is swallowed while navigation/path input, Palette or active
Menu owns input. Close/Save swallows mouse input and refuses drops immediately,
showing `Drop refused` in the prompt while preserving its answer. Palette rows
execute on a primary press. Status priority is Close/Save, source answer,
navigation/path input, Palette/Find panel, then ordinary save/error/notice status;
menu chrome retains the underlying status owner. Field no-ops remain local.
No allocation, duration or capacity budget was introduced.

All counts below are structural (P), derived from the matrix enums/witness arrays
and actual output, not estimates or budgets. The owned cells are thirteen Go to
Line plus thirteen Palette plus two Menu plus three Close/Save cells: 31. The
final matrix has thirteen modes times twenty actions (260), two focus states
times six commands times three routes (36), five click/type witnesses, five
transient witnesses and two lifecycle witnesses: 308 total. Thus 307 PASS equals
the preceding 256 PASS plus 31 promoted cells plus 20 added witnesses. One
live-results XFAIL remains. Locator and exit-code numbers follow source/process
conventions (P). Display `:99` and session identities are supplied inputs (E).

Red, before production edits:

The original strict-XFAIL baseline passed outside the sandbox:

```text
modal_matrix_test: 256 PASS, 32 XFAIL, 0 FAIL (P: enum cross product plus command/click/lifecycle witnesses)
exit=0
```

The initial sandbox attempt printed that same result but exited with LeakSanitizer's
ptrace fatal error. Outside the sandbox, display and leak detection both worked.
An intermediate promotion run exited with 253 PASS, one XFAIL and 34 FAIL:
the 31 owned failures plus three overly broad selection expectations. The test
expectations were corrected before implementation: results have different text,
and click-then-type advances the caret by the inserted fixture byte.

The authoritative expanded red used:

```sh
XDG_RUNTIME_DIR=$(mktemp -d) DISPLAY=:99 ASAN_OPTIONS=detect_leaks=1 \
  sh tools/with_display_lock.sh build/san/tests/modal_matrix_test
```

Power/load stamp (M)[AC], copied beside that run:

```text
Not charging
 14:45:00 up 1 day, 16:48,  1 user,  load average: 1.72, 2.57, 2.86
```

The new field-ownership witnesses already passed; the added command/mouse
witnesses exposed the remaining failures. Red excerpts:

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
FAIL find field x route=2 Close rc=0
FAIL find field x route=2 New File rc=0
FAIL find field x route=2 Open File rc=0
FAIL find field x route=2 Save rc=0
FAIL find field x route=2 Goto Line rc=0
FAIL find field x route=2 Undo rc=0
FAIL find document x route=2 Close rc=0
FAIL find document x route=2 New File rc=0
FAIL find document x route=2 Open File rc=0
FAIL find document x route=2 Save rc=0
FAIL find document x route=2 Goto Line rc=0
FAIL find document x route=2 Undo rc=0
| Transient witness | Outcome |
|---|---|
| go-to field keys | PASS |
| palette field keys | PASS |
| close prompt swallows menu title | FAIL |
| close prompt refuses drop visibly | FAIL |
| go-to preview click uses painted text | FAIL |

modal_matrix_test: 258 PASS, 1 XFAIL, 49 FAIL (P: enum cross product plus command/click/lifecycle witnesses)
exit=1
```

A separate updated required-answer test also failed before its dispatcher gate:

```sh
XDG_RUNTIME_DIR=$(mktemp -d) DISPLAY=:99 ASAN_OPTIONS=detect_leaks=1 \
  sh tools/with_display_lock.sh build/san/tests/editor_test --close-review-hidden
```

```text
editor_test:7181: FAIL !e->go.active && editor_close_prompt_shown(e) && close_status(e, "Save changes to doc.txt?")
exit=1
```

The first named-suite run then passed the matrix and find UI but stopped at the
older menu assertion that a blank-text click should leave the caret at its
previous position. It was updated to expect the fixture's EOF, as required by
the supplied one-click placement rule. No production change was needed for that
assertion. Its failure was:

```text
editor_test:80: FAIL editor_view(e).selection.cursor == cursor
editor_test:6256: FAIL !e->menu_rows && e->menu.open < 0 && expect(e, initial, 0) == 0
editor_test:7769: FAIL menu_contract() == 0
exit=1
```

Green named suites (final sources):

```sh
make -j4 all
make -j4 build/san/tests/modal_matrix_test build/san/tests/findui_test build/san/tests/editor_test
# Each named suite inherited a private XDG runtime directory:
XDG_RUNTIME_DIR=$(mktemp -d) DISPLAY=:99 ASAN_OPTIONS=detect_leaks=1 \
  sh tools/with_display_lock.sh build/san/tests/<name>
```

The actual runner also isolated XDG config/data, exported
`EDIT_REFUSE_REAL_DISPLAY=1`, cleaned its runtime directory on exit, and ran
outside the ptrace sandbox. Toolchains match the brief's gcc/clang release and
sanitizer configurations; project C11 warning flags, including `-Werror`, stayed
in force. Final `make -j4 all` printed:

```text
make: Nothing to be done for 'all'.
exit=0
```

Power/load stamps (M)[AC], with the actual sanitizer results:

```text
Not charging
 14:54:08 up 1 day, 16:57,  1 user,  load average: 3.39, 3.97, 3.55
modal_matrix_test: 307 PASS, 1 XFAIL, 0 FAIL (P: enum cross product plus command/click/transient/lifecycle witnesses)
exit=0
```

```text
Not charging
 14:54:23 up 1 day, 16:57,  1 user,  load average: 3.39, 3.94, 3.55
findui_test: all passed
exit=0
```

```text
Not charging
 14:54:36 up 1 day, 16:57,  1 user,  load average: 3.44, 3.92, 3.55
edit-ywd palette: paint/filter/chords, shared Save As, Escape/mutation/focus and malloc guard passed
edit-yey menu bar: bare Alt release shows (held Alt inert), Alt+F, chords, shared Save As, Escape levels, Alt chords, mouse, menu.visible and malloc guard passed
edit-cxho.17: explicit menu command preserves the required close answer: PASS
editor_test: all passed
exit=0
```

The editor suite's EGL-unavailable fallback to raster on Xvfb is expected;
there were no sanitizer reports. The existing allocation guards passed.
No new large fixture was introduced. Disk output before/after the named suites
(M)[AC], copied verbatim; existing suites own their sparse fixture cleanup:

```text
before:
Filesystem               Size  Used Avail Use% Mounted on
/dev/mapper/vgmint-root  1.8T  177G  1.6T  11% /
after:
Filesystem               Size  Used Avail Use% Mounted on
/dev/mapper/vgmint-root  1.8T  177G  1.6T  11% /
```

Full check (final sources):

```sh
XDG_RUNTIME_DIR=$(mktemp -d) DISPLAY=:99 ASAN_OPTIONS=detect_leaks=1 \
  EDIT_REFUSE_REAL_DISPLAY=1 make -j4 check
```

The runner used private XDG config/data directories as well. The Makefile runs
the display suites through `tools/with_display_lock.sh`; all inherited `:99`.
Power/load stamps: start (M)[AC], end (M)[bat]. Power changed during the check;
no performance comparison is inferred. The structural summary (P: Makefile test
glob count) and final output follow. The bare AC value is `/sys/class/power_supply/AC/online`;
one means external power is online (P: sysfs boolean contract).

```text
start:
Not charging
 14:59:26 up 1 day, 17:02,  1 user,  load average: 2.40, 3.40, 3.48
1
end:
Discharging
 15:14:01 up 1 day, 17:17,  1 user,  load average: 3.31, 3.12, 3.19
check: 113 test binaries passed
display lock contract: owner PID, alias/timeout, make/scripts, nested inheritance, exit status PASS
exit=0
```

The OS suite intentionally crashes an unregistered-fault child; its BUS
diagnostic is expected and followed by `fault: ok`. The display-lock contract
also intentionally exercises timeouts and then reports PASS.

Disk output for the full check, verbatim: before (M)[AC], after (M)[bat].

```text
before:
Filesystem               Size  Used Avail Use% Mounted on
/dev/mapper/vgmint-root  1.8T  177G  1.6T  11% /
after:
Filesystem               Size  Used Avail Use% Mounted on
/dev/mapper/vgmint-root  1.8T  179G  1.6T  11% /
```

Verification commands and exit codes (P: process return codes; repeated
runs correspond to changed tests/implementation, not additional review passes):

| Command/run | Exit |
|---|---|
| `make -j4 build/san/tests/modal_matrix_test` — original baseline build | 0 |
| Original matrix in the ptrace sandbox — LeakSanitizer could not finish | 1 |
| Original matrix outside the sandbox, leak detection enabled | 0 |
| Promoted-cell matrix — intermediate selection expectations | 1 |
| Expanded red matrix build | 0 |
| Expanded red matrix on `:99`, leak detection enabled | 1 |
| First implementation sanitizer build and matrix | 0 |
| Updated required-answer test build | 0 |
| `editor_test --close-review-hidden` before its command gate | 1 |
| First `make -j4 all` | 0 |
| First named-suite matrix and find UI runs | 0 |
| First named-suite editor run — obsolete menu-click expectation | 1 |
| `make -j4 all build/san/tests/modal_matrix_test build/san/tests/findui_test build/san/tests/editor_test` | 0 |
| Final `make -j4 all` | 0 |
| Final `modal_matrix_test`, `findui_test`, `editor_test` individually, with leak detection and the display lock on `:99` | 0 each |
| Full `make -j4 check`, leak detection enabled | 0 |

Raw diagnostic logs are in `/tmp/edit-cxho.17-s24-logs`; the decisive red/green
output and disk/power evidence are pasted here. The pinned matrix contains the
complete final tables.

Remaining: no required work remains within this bead. The live Find Results
Ctrl+W lifecycle cell still has its original strict XFAIL and belongs to
`edit-cxho.18`; its implementation and lifecycle test were not changed. No other
outside-scope defect was fixed or new review pass run. No install was performed.
