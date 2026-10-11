# edit-g11: retain the ready minimap while editing

The edit-triggered amber pulse comes from painting pending line-index state as
a whole-strip tint, plus a bright tab-strip exclamation mark. The null-backend
submitted-frame regression reproduces it before the production change; see
the [worker report](../worker-reports/edit-g11.md) for red and green output.

Retain the ready density summaries and their painted viewport until the index
is ready for the current revision. Pending rendering recomposes that same image
without source reads, so a shared grid on tab return gets the correct buffer's
image. Cached worker fills retain the same summaries while awaiting publication.
Source identity and height checks still prevent reuse across unrelated sources
or incompatible geometry. Stale remains an internal navigation/refill state;
it no longer becomes a transient visual warning during typing.

No frame delay, timer, allocation, or additional wait is added. Fresh fills and
worker publication keep their existing scheduling. This avoids deriving a frame
deferral budget from unavailable raw drive traces. The checked-in Astra review
has video transition timestamps, which measure displayed overlay lifetime,
not minimap computation time. The raw evidence directory is absent from this
worktree; the report distinguishes that evidence from headless fill telemetry.

Without usable summaries, pending open/resize/source identity changes keep the
approximate scrollbar, with a last-column bar rather than a strip-wide tint.
The bar width is (P) the narrowest existing render cell: one strip column,
derived from the cell grid's indivisible column unit. The editor chooses its
colour by the existing per-channel half-contrast theme blend of minimap density
and background, making it desaturated and consistent with estimated gutters.
The tab-strip stale exclamation mark is removed. Existing durable source-change
text still explains reload/keep; it is independent of minimap index maintenance.

The editor paint audit found no other unconditional edit-time intermediate
overlay. Bracket and whitespace decorations are prepared before submit;
modified-tab markers track persistent document state. Estimated gutter treatment
tracks index certainty, and the source-change prompt persists until resolution.
Caret-only frames retain their existing limited damage and blink behaviour.
