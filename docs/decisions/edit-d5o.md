# edit-d5o: compose miniature pixels

The editor composes a caller-owned RGB pixel region over the reserved minimap
strip. Its summary height derives from ceil(strip pixel height / line spacing).
The spacing is (G) two pixels per summary line, as required by the supplied bead
and the existing minimap pixel renderer. The strip retains its existing text
column reservation; marks and the translucent viewport perimeter now resolve
individual pixels. Click and drag coordinates use that same summary geometry.

The cell contract stays intact. A separate optional frame region carries pixel
storage, geometry and damage. Frame begin clears region damage; cell row damage
does not stand in for a minimap change. Backend submit copies changed region
pixels into storage reserved at initialization. Retained pixels are reapplied
where text row rendering touches the strip. A map-only frame updates the region
without rendering text rows.

Raster workers blit into the retained back buffer. The existing UI completion
path stages finished pixels into the native pixmap. GL uses a small RGB texture
with a shared upload context on a work lane; the mailbox hands completion back
to the UI, and a GPU fence orders the region blit before presentation. Neither
backend borrows mutable editor pixels after submit. No allocation, source read,
timer or deliberate frame deferral is introduced by pixel composition.

Pending indexing preserves the ready image and its viewport, following
[edit-g11](edit-g11.md). Pending geometry without compatible summaries uses
the existing approximate scrollbar. Source summaries retain the existing
preparation policy; this bead does not change indexing or glyph lookup.

Verification, measured compose timings, prerequisite discrepancies and required
display reruns are recorded in [the worker report](../worker-reports/edit-d5o.md).
