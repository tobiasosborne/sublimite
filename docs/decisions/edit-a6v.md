# edit-a6v: launch geometry owns the initial painted surface

The startup grid already divides configured window pixels by the atlas cell
dimensions. Native creation uses those same configured pixels. Raster reserves
the configured maximum pixel extents for its pixmap and XShm storage. These
paths need no sizing change.

Session recovery was queuing its historical JOURNAL_WINDOW pixels as a renderer
resize without resizing the native window. A legacy journal could therefore
shrink the painted grid after the correctly sized initial frame. This reproduces
the assigned defect on a headless null backend with the CPU capacity contract.

After recovery, explicit launch pixel dimensions take precedence on their
respective axes. If the resulting recovery request equals the created window,
startup discards the redundant resize. This keeps the initial surface, font's
window dimensions and submitted grid consistent, without another redraw, worker
wait or allocation. Callers without explicit pixel dimensions retain historical
recovery behavior. Later native resize events still use the existing resize path.

The test uses the supplied panel geometry and m3g's launch policy (P/E):
`floor(2880*.60)=1728` pixels wide and `floor(1800*.70)=1260` pixels high,
at the supplied `243` DPI (P, panel input). Expected columns/rows are
`floor(window_width/cell_w)` and `floor(window_height/cell_h)` from the actual
atlas. The legacy journal uses existing default columns/rows and baked cells,
so its geometry derives from `cols*cell_w` and `rows*cell_h`, without a new
fixture budget. The resize fixture uses the supplied full work area, which is
reserved at startup. The allocation assertion follows law 2's existing zero
allocation requirement (G); this bead adds no typing-path code or timing limit.

Native screenshot acceptance remains with the coordinator. See the
[worker report](../worker-reports/edit-a6v.md) for red/green and build evidence.
