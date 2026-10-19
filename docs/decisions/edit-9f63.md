# edit-9f63: keep the view inside the minimap boundary

The composed minimap already starts at a whole-cell boundary. Its open-time
`map_width` reservation uses the largest prepaid font; its painted width uses
the current font. The reservation is capacity, not the painted boundary.

Geometry rule (P): let `W` be the window width, `c` the current cell width,
`G = floor(W / c)` the full grid columns, `m` the reserved minimap columns,
and `g` the gutter columns. The painted minimap starts at `L = (G - m) * c`.
The usable text count is
`T = floor((W - m * c) / c) - g = G - m - g`.
Writing `W = G * c + r`, with `0 <= r < c` (P: integer division), proves
that the remainder cannot admit another text cell: `(g + T) * c = L`.
The native remainder stays beyond the grid; no partial text cell is counted.
If the prepaid grid capacity caps `G` below `floor(W / c)`, use that smaller
`G` in `L` and `T`; the window-based floor is then an upper bound (P), and
the text still ends at the actual painted minimap boundary.

The failing case changes the decimal gutter while typing. `layout_edit`
recomputes `layout.text_cols`, but the editor previously copied that count to
`view.config.cols` only during a full layout. The stale view could count a
cell inside the minimap. Copy the successful incremental layout's text count
before cursor repair/follow uses it, with the existing positive-width view
fallback (P: one cell is the view API's minimum). Layout remains the owner of
the count for both wrapping modes. The synchronization is a scalar copy;
existing cursor follow uses it to scroll at the last fully visible cell.

The regression uses the existing minimap fixture's surface and default baked
font (E: fixture reuse), sweeps every possible pixel remainder (P: the integer
residues modulo cell width), and splits a long ASCII line with Return.
The decimal transition is nine to ten logical lines (P: eight LF separators
plus the final line, then the inserted LF). It checks the view's right edge,
the last rendered text cell, and the hard break of a soft-wrapped long word
against the same usable column count. Allocation hooks cover input to submit.
Screen rows come from prepared byte starts, since cursor follow can move the
split line to the viewport top without changing its horizontal geometry.

Red evidence and verification are in
[the worker report](../worker-reports/edit-9f63-s9.md).
