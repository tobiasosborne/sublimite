# Modal interaction matrix

Use a separate automatically discovered `modal_matrix_test` binary with the
existing editor private header and null renderer. Add it to the Makefile's
display-lock list. No production changes or new state accessors are needed.
Every cell creates fresh file-backed tabs, a saved document edit with undo
history, a retained closed tab, and PRIMARY copied through editor commands.
Input enters through `editor_inject` and settles through `editor_step`.
The headless drop uses the existing borrowed-payload test seam.

The landed find decisions govern field undo, incremental search, prefill and
focus. A field owns its undo chord; document focus owns document undo. Global
commands retain the find panel and its field/document focus. Escape removes
the topmost transient; the menu closes its dropdown before its latched bar.
Required-answer prompts retain keyboard ownership. A go-to-line prompt owns
line-entry text and query undo; window commands should dismiss it before
dispatch, and a primary text click should cancel it and focus the document.
The current swallowed global chords are XFAILs, like the palette's swallowed
chords, rather than exceptions to the global-command rule.
Mouse gating and conflicting status-row ownership are recorded as expected
failures for the follow-up bead, rather than changed here.

Keep a distinct intended-state calculation and an explicit XFAIL table with
an observed-state calculation. Every XFAIL must differ from the intended
state and match its recorded failure; XPASS is an error. This pins defects
without allowing unrelated regressions or error returns. Paint inspection
checks which prompt/panel owns the row, alongside stored visibility and
keyboard focus. Mouse-gated cases also compare selection endpoints.

Supplement the cross product with menu/palette command-intent equivalence,
click-then-type, and a live Find Results job held by mailbox backpressure.
The live-job witnesses use the existing private dispatcher before worker
mail is drained, matching the established find-in-files integration seam.
Closing live results currently leaves the search running; record that for
the lifecycle bead. The main results row uses a completed no-match search.
Replace denotes the editable Replace panel; active replace-all parking and
queue saturation retain their existing integration coverage in `editor_test`.

The required-answer close prompt blocks keyboard window commands. A drop
currently activates another tab and cancels that unanswered prompt, so it
is an XFAIL, not an accepted dismissal policy. Reload/Keep permits the
existing window command whitelist and refuses Save until answered.
Palette query undo is a no-op, as no query history exists. Explicit menu
and palette Undo target the document, even with find field focus, matching
the landed command-dispatch decision rather than the physical field chord.

Numeric literals in the test are structural (P): enum/array-derived counts,
zero for success/absence/first index, unit steps and removal of C terminators.
File contents, field buffers and byte offsets derive from literal strings.
Geometry reuses the existing find-in-files fixture (E); resize adds a cell to
each extent (P: witness a changed geometry). The settle watchdog reuses
`EDITOR_IDLE_NS` (E, existing test convention); there is no new timing gate.
