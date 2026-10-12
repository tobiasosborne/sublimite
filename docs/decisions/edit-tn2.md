# edit-tn2: extent changes and certified wrapped byte placement

An estimated ordinal is a label attached to a certified byte anchor, rather
than a line to seek. `scroll_set_extent` now stores the new extent and clamps
only when both the existing label and the new count are exact. This preserves
the anchor, fractional offset and pending request while the resolver obtains
the exact label. Explicit seeks, wheel policy and resize policy are unchanged.
The regression covers a shrinking inexact extent, publication of an exact
count, a pending relative motion, and the existing exact-label EOF clamp.

Wrapped byte jumps use `editor_scroll_place` and therefore reach
`editor_refresh_placed`, as unwrapped jumps do. A certified logical line start
is a leading visual boundary: placement assigns the logical label and byte
to the wrapped view, sets its approximation flag, resets horizontal position
and trailing affinity, and leaves the buffer's logical scroll state untouched.
Layout derives the wrapped rows from that seed. Passing the logical label
through `scroll_seek_line` would apply logical EOF geometry to visual rows.

The wrapped index-publication branch in `large.c` remains necessary. A partial
logical index supplies no global visual ordinal or visual extent, and
`editor_scroll_slice` deliberately excludes wrapped views. On complete index
publication the existing branch relabels the logical start without moving
its byte or visual anchor; it now also clears the view's approximation flag.
This bead does not introduce a visual-row index or alter page motions.

The existing scrollctl extent workaround is retained to keep this change local
while complete editor integration remains restricted by the sandbox. Its
READY/approximate case is now also handled by the scroll module, and the pure
regression proves byte-anchor preservation and exact relabelling. Removal is
optional cleanup after the coordinator's unrestricted integration run.

The working tree already contained bead-specific tests without these production
fixes. Assertions were strengthened before implementation; acquisition and
completion setup were corrected when the integration fixture was exercised.
The wrapped integration fixture forces mapped acquisition before holding the
bulk lane; otherwise it parks acquisition behind its own hold. It waits for index publication, rather
than completion of a find it never started. Its existing viewport geometry and
watchdog are reused (E: from `gutter_estimates`); its byte target and expected
ordinal derive from `write_fixture`'s line lengths (P: preceding A lines plus
the following B line). No performance budget or new allocation is introduced.
