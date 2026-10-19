# edit-2mhd: minimap viewport grab

A primary press inside the drawn viewport rectangle captures the pointer's
pixel offset from its top and leaves the viewport in place. A press outside
captures half the rectangle's pixel height and scrolls immediately, centring
the rectangle on the pointer subject to the existing endpoint clamps. Motion
subtracts the captured offset before resolving the minimap row. The offset
stays fixed until release or the existing drag-owner cancellation.

Use the rectangle actually painted: compatible retained summaries retain
their painted viewport during pending indexing, following edit-g11 and
edit-d5o. Without compatible summaries, use the current approximate scrollbar
geometry. Wrapped views count the logical lines represented by their prepared
visible rows. (P) The centre is half the painted height; summary spacing comes
from the existing minimap grid's pixel cell height.

The offset is embedded scalar editor state. The drag path allocates no memory,
reads no additional source bytes, and keeps `minimap_hit` and
`minimap_hit_approx` byte-to-line resolution and scrolling continuations.
