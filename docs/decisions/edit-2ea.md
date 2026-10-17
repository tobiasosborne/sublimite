# edit-2ea: empty decoration range for an all-void layout

`editor_paint_prepare` clamps `hi` up to `lo` after bounding it by the
buffer length. (P) Equal endpoints describe an empty half-open byte range;
the subtraction used for the existing decoration budget then cannot wrap
when every clipped row is void and the layout position precedes the top.
The common paint finalization clears bracket/whitespace decorations, marks
paint ready and lays out the void rows for blank-frame submission.

The regression uses the existing trailing-paint fixture geometry and a top
byte following its text prefix, with the top line label at the exact line
count. (P) Every row is therefore past the last valid line label. The view,
layout and displayed scroll state share that top so mirroring preserves the
case. It calls paint preparation directly, then steps the editor to expose any latched
error and checks the submitted text grid, including the empty gutter.

`EDITOR_IDLE_NS` already has the edit-dz0 derivation beside its definition:
(G) the caret-blink stop requirement, PRD section 6 / G11, with its test
watchdog reuse marked (E). The header needs no edit; the value is unchanged.
