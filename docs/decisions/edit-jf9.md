# edit-jf9: byte-relative motion during lazy indexing

The editor selects `view_relative_command` for unwrapped lazy-file arrows and
page motions while `lg.lines_exact` is false. The ordinary view path remains
in use after exact publication and for other commands. Wrapped navigation is
outside this bead.

Relative motion proves the cursor's line start by walking backward to LF, then
walks adjacent LF boundaries for vertical displacement. It uses view's existing
cooperative grapheme scanner for preferred columns, tabs, CRLF, horizontal
arrows and horizontal following. A page traverses `config.rows` boundaries (P:
the configured visible row count); an arrow traverses its adjacent row (P).
The scan quantum is the existing line-query quantum, 256 bytes (P: the
`exhausted` check reserves that quantum plus grapheme lookahead within
`VIEW_SCAN_BOUND`). Each continuation retains the existing charged-work and
wall-deadline contract. Scratch belongs to the caller-owned view; there is no
allocation or new worker job on the motion path. Errors are return codes.

No density estimate enters view's line-start or line-query caches. This preserves
its settled invariant, “certified line starts, never approximate counts”. The
motion updates the byte selection and exact columns, leaving viewport ordinals
to the existing resident scroll resolver. That resolver walks real boundaries
using estimated labels while the index is incomplete; existing index publication
and gutter handling relabel them. No estimated-label policy is changed here
(the concurrent edit-xzw investigation stays separate).

The completed relative command retains a runtime flag because `v.line` does not
carry an exact ordinal. `editor_scroll_follow` must not adopt that field if
index publication happens during the command. Ordinary commands, cancellation
and restore/external-edit normalization clear the flag. Selection extension,
preferred-column retention, EOF/BOF clipping and cancellation follow ordinary
view semantics; the headless regression compares their results directly.

## Regression bound

From `docs/decisions/edit-91c.md`:

> G5 warm, column A: open request -> correct first viewport = 64 KiB read + full frame, PB 1.025 ms (E), gate 6 / 9 ms p50/p99 (G).

Its derivation transfers to a resident-window motion because it uses the same
64 KiB read (P: `SCROLL_SCAN_BUDGET`) and full frame. The PB is the existing
warm-read component plus the GPU frame estimate, as recorded in
`perf/01-perf-target.md` section 2.4. The first PageUp after Ctrl+End with the
bulk/index lane held uses that existing p99 value as the bead-requested
regression bound, not a newly invented allowance. Other page timings remain
tracked, and the rest of the fixture retains its existing setup watchdog.
Sanitizer/null-renderer measurements are not release GPU performance claims.

## Remaining scope

`large_doc_end` still resets horizontal scroll. Fixing its long-last-line caret
placement requires discovering and decoding that line's exact visual column,
which can require continuations; it is not a one-line assignment. It is reported
for separate work, as requested. The byte-relative scanner similarly does work
proportional to intervening line bytes for extremely long lines, with cooperative
yields; this bead removes the scan of the unrelated file prefix.
