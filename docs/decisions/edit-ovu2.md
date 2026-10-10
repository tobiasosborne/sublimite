# edit-ovu2 — scroll review slice 2

Scope: P4-modules-2 §6–8. The public integration contract is amended in
`docs/decisions/P3.4.md`; renderer/editor/layout implementation is outside this
worker's scope.

## Renderer handoff (§6)

`scroll_plan_frame` produces an immutable, caller-owned Q8 drawing origin,
viewport clip and reservation count. Reserve one overscan row (G) at setup in
all layout storage, including wrap plans; do not increase the visible surface.
The origin is negative fractional displacement, applied identically to text,
gutter, selection and cursor. Fixed tabs/sidebar/chrome keep their surface
coordinates. `scroll_frame_hit` provides the exact inverse transform; damage
must apply the forward transform and round clipped bounds outward to pixels.
A changed byte/row anchor, origin, row geometry or clip requires full viewport
damage. Stable plans still allow content/selection damage from the host.

No changes to frozen render/raster/editor headers were made. The scroll API
is consumable now, but backend translation, overscan layout and actual pixel
verification remain an editor/renderer integration dependency.

## Wrapped adapter (§7)

`scroll_visual` reuses pixel/page/follow policy with **visual** ordinals and
separately carries an exact wrap descriptor (byte range, logical line seed,
columns, indent, continuation/newline flags). A caller-owned source exposes
`row_at` and affinity-aware `locate`. These queries must allocate nothing and read bounded resident
metadata or yield; the host retains their lazy continuation and publishes
worker results through mailboxes. Scroll itself neither allocates nor reads
raw mapped text. Existing `layout_visual_row` may return approximate boundaries
and cannot alone satisfy this exact-source contract for deep wrapped text.
A wrap-boundary metadata producer belongs to the host/layout integration.

Exact total row counts are optional. An estimate does not constrain navigation;
bottom lookahead or End discovers the exact EOF row, clips to a full viewport,
and publishes the exact count. Affinity chooses the preceding visual row for
a trailing cursor at a soft boundary; viewport seeds always use leading
affinity. Width/tab/gutter/indent changes require a new source generation.
Resize maps the previous byte seed into new geometry, retaining fractional
pixel displacement (including whole new-height rows) before EOF clipping.
Edits require the host to transform the seed byte before rebind.

The zero-initialized resolver performs at most the requested callback count
(default two, maximum 256 (G)) and checks a 0.5 ms (G) default deadline between
callbacks. Each callback must itself be bounded/nonblocking. MORE/errors leave
the requested state unchanged; the completed candidate is published atomically.
Changed intents, affinity, source or generation discard old adapter progress.
Wheel/page/follow can cancel a pending unknown-count End request; integer-limit
resize carries saturate before clipping, instead of wrapping the ordinal.
The host must keep the old source alive until its pending producer retires.
Wheel/page events while a reflow is pending must be queued by the host; the
adapter rejects them until the new byte seed is mapped. No cursor/selection
mutation is owned by scroll.

## Display observability (§8)

A public-editor/null-backend probe correlates existing submit/present IDs and
shows that wheel input currently produces no scroll frame. `on_present` is
invoked at present submission, not displayed-refresh observation; null's
completion is synthetic. Therefore no current public editor API can express
the required displayed G3z verdict. The failing integration probe remains
available with `build/tests/scroll_test --require-editor-scroll`.

The exact required host hook and verdict rules are specified in P3.4's
“Required editor displayed-refresh hook” section. The proxy remains labelled
as CPU work plus synthetic completion; its sample counts/timing policy are
unchanged and owned by edit-yqu. No displayed-cadence gate pass is claimed.

Red/green evidence and final build/sanitizer/fuzz status:
`docs/worker-reports/edit-ovu2-s9.md`.
