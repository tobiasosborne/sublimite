# edit-48u: pointer geometry and drag frames

Pointer acceptance is clipped in surface coordinates before converting to a
layout row. The inverse scroll transform can legitimately produce the bottom
overscan row. Panel-open and wrapped text use their unshifted drawing origin.
An empty text viewport cancels text dragging before any last-row arithmetic.

Click chains belong to a buffer identity, source generation and button as well
as the existing cell and time interval. An intervening different button ends
the primary-button chain, including the clipboard-owned middle-button path.
The existing blink-derived click interval is retained (E, edit-73j decision).

Dragging stores the last pointer position in the editor's reserved object.
The ordinary editor frame turn advances scrolling after a completed frame,
then extends selection using newly prepared edge hit metadata. It waits for
layout, view actions and backend retirement, and yields to queued input.
Release, focus/resize cancellation, a different source, and source suspension
stop the drag. No timer or allocation is introduced.

Rate policy (E): let h be the current row height in pixels, d the signed
distance beyond the text viewport edge, and dt the interval between completed
drag frames. Displacement per frame is d pixels, or d/h rows; the resulting
rate is d/(h*dt) rows per unit time (P, distance divided by interval).
Thus a pointer a row beyond the edge advances a row per completed frame;
doubling the distance doubles displacement. The frame interval comes from
the existing backend progression; this does not claim a measured Sublime
rate. Unwrapped displacement uses scroll's pixel units. Wrapped displacement
accumulates a pixel remainder and advances existing visual-row continuations.
At a document boundary, frames can keep accepting drag input without timers.

Text pointer selection repaint uses the already placed viewport rather than
caret-follow: following a hit in the partially displayed overscan row would
move the viewport away from the pixels clicked and fight edge autoscrolling.

Panel geometry recomputes desired rows from the open panel mode each time
text geometry changes, instead of treating the previous clipped row count as
the desired size. The row derivation remains the existing query row, plus
replacement and hints rows in replace mode (P, visible elements).

Escape deliberately retains the selected match while closing the find panel.
The supplied brief states this Sublime behavior; a firsthand explanation on
the [Sublime forum](https://forum.sublimetext.com/t/funny-search-select-behavior/8031)
also describes Escape returning focus to the document with the found text
selected and the caret at its end. This is published behavior confirmation,
not a live display measurement in this sandbox. The regression preserves both
selection endpoints and keeps subsequent search navigation available.

Test geometry reuses the existing mouse fixture (E); byte expectations derive
from literal fixture strings and layout row starts (P). Structural empty,
first, adjacent and midpoint constants are annotated beside the fixtures.
Readiness deadlines reuse the existing settle contract (E), without adding a
duration or performance budget.
