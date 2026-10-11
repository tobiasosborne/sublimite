# edit-3kk: editor scroll ownership and refresh observations

Logical scrolling is adopted by `src/editor/scrollctl.c`. Wheel dispatch stays
in `input.c`. Each buffer owns its immutable piece snapshot, resident windows,
requested scroll state and resolver. The rendered view changes only after a
resident resolver succeeds. Edits cancel continuations and transform the byte
anchor; the old bridge and snapshot remain alive until physical retirement.
Snapshot replacement uses the existing piece arena allocator. Completed view
motions/repairs may certify an EOF anchor without a following row start; that
byte is adopted only when scroll's pure policy selects the same row ordinal.
The held-worker EOF regression prevents a redundant copy from delaying typing.

Pure transitions may reuse line starts already certified in layout's resident
row metadata. This avoids asking warm wheel input or in-viewport typing to wait
for a worker copy. Unprepared boundaries still use the resident resolver;
foreground scroll code never dereferences raw snapshot bytes. A replacement
source is installed lazily when resolution needs it. Shutdown may wait;
ordinary maintenance retains a closing lease and yields. Logical line labels
on index publication belong to scrollctl, rather than large.c's raw query.

The follow margin preserves view's existing clipped-cursor policy. EOF now
clips to the last full viewport, as specified by P3.4. The deep typing fixture
positions its EOF case accordingly, using its LF-derived line count and
surface rows minus chrome; its mutation, row-start and prefix-replay assertions
remain enforced.

Visible geometry and layout reservation are distinct. The extra text row
specified by P3.4 is reserved at setup; fixed chrome and the minimap retain
surface coordinates. Submitted metadata owns a copy of the scroll plan,
selection, source/geometry identity and sidebar identity.

`render_grid.scroll` carries a by-value Q8 origin and text rectangle. Its
overscan row follows the visible composed cells in setup-reserved capacity;
surface dimensions and canonical strip coordinates stay unchanged. Raster
snapshots the plan and extra cells, samples the translated source, and blends
adjacent pixel rows with Q8 coverage. Fractional damage is conservatively full.
GL snapshots the same plan and extra row, applies the exact origin in its
vertex shaders and clips text fragments below fixed chrome. Its native pixel
result requires the coordinator's display run; pure snapshot tests do not
certify a shader or GPU result. Inverse text hit testing uses `scroll_frame_hit`
with the frame's plan; edit-73j owns pointer event dispatch.

The wrapped visual metadata producer from the separate review finding is not
added here. Wrapped view commands retain their existing behavior. The logical
index adapter is never applied to wrapped wheel input. Wrapped submission
metadata identifies that mode and supplies its wrap seed; its scroll plan is
not an exact visual-ordinal plan.

Refresh observations distinguish synthetic null opportunities from native
completion observations. Native Present completion alone cannot certify a
stream of every physical refresh opportunity. Consumers must refuse a physical
cadence verdict when that stream is unsupported; they must not silently count
submissions as refreshes. The synthetic harness checks repeated/missing IDs
against immutable submissions, including a deliberately repeated stream.

`editor_config.on_refresh` receives the displayed ID, sequence, completion
time and immutable metadata. `editor_on_refresh` accepts explicit opportunities
including repeats and missing IDs without releasing the backend slot. Duplicate
or older sequences are rejected. Retained records cover the backend queue slot
and preceding submission; older unknown IDs are reported without metadata.
Native completion hooks retain the adapter's T4/T6 timestamps and MSC and mark
the observation as completion-only. Null opportunities are explicitly
synthetic. This host wiring supplies observability, not an invented physical
refresh stream. Actual repeated/missing physical opportunities remain a backend
producer requirement before a physical G3z verdict can be issued.

Landing note (rebase onto the config module, v0o.7): a config hot reload that
changes wrap or tab width now bumps `geometry_generation` like a tab
activation, and on a switch to unwrapped resizes and reseeks the resident
scroll window to the view's first byte, since that window is not maintained
while wrapped (src/editor/preferences.c).
