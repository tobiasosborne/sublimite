# edit-08z: asynchronous Unicode glyph ownership

The editor now owns `font_async`, a production adapter around the existing
cluster cache. The legacy `font_runtime` preparation API remains compatible
with its callers. Production lookup never invokes its foreground raster path.

The primary face is immutable data generated from `vendor/DejaVuSansMono.ttf`,
under `vendor/DejaVu-LICENSE`, rather than a runtime path relative to the launch
directory. `python3 tools/embed_font.py` regenerates the data. Its extent derives
from the C array size, excluding the string terminator. Worker initialisation
validates the face and its baked identity through the existing font API.

INIT reserves exact-key storage, a bounded hash table, UI descriptors and a
separate worker cache arena. The descriptor ceiling is the inherited raster/GL
ceiling (G): `4096`. Printable ASCII consumes `95 = 0x7e - 0x20 + 1`
descriptors; therefore runtime requests can consume `4001 = 4096 - 95`.
Keys reserve `entries * FONT_CLUSTER_MAX_BYTES`, the existing cluster admission
bound, on each side of the ownership boundary. Cache hash capacity is rounded
up from `(requests + supported_widths) * 4 / 3`, derived from its existing
quarter-empty admission rule; the additional keys reserve narrow and wide
replacement images. Alignment allowance is one maximum alignment per named
arena allocation. Scratch and each atlas page derive from
`FONT_ATLAS_PAGE_DIM * FONT_ATLAS_PAGE_DIM`; page capacity inherits
`FONT_ATLAS_MAX_PAGES`. These are structural storage reservations, not latency
or memory-performance targets. Only requested keys are touched.

Font requests remain background work: they do not set the editor's pending-UI
status or make an idle editor busy-wait. A client validating final glyph pixels
can explicitly query the font owner; ordinary input/submit quiescence remains
independent of a bulk worker deliberately held by another subsystem.

A cold UI lookup copies the exact cluster into reserved storage, creates a
stable descriptor pointing at baked question-mark artwork, and submits an
immutable job argument through `src/work`. It returns a usable slot immediately;
it does not return `FONT_MORE`, wait for a font, grow an arena, or allocate.
Repeated lookups share the request. Submission refusal retains queued work for
the next pump. Publication/adoption admits the next job; unique immutable
request arguments survive physical completion. The bulk lane serialises all
font/cache mutations. No owner writes follow successful publication.

On that worker, each scalar first tries the embedded face, then already loaded
fallback faces. A missing scalar uses the existing cancellable isolated
fontconfig query, now parameterised by the exact scalar. Charset coverage is
checked, monochrome faces are requested, and file/index pairs are shared.
Scalar resolutions, including missing coverage, are cached on the worker.
General glyph rasterisation replaces the foreground outline-admission path for
this owner; existing baseline placement and combining-mark union are reused.

Workers append disjoint atlas rectangles. UI descriptors and page counts are
separate from worker cache metadata. A live mailbox completion authenticates
lease and request generation, copies the completed descriptor, and leaves its
slot/identity stable. The editor records requesting rows in INIT-reserved bits
and checks only those rows for still-visible users before damaging them. Pixel
changes therefore repaint even when cell metadata is unchanged, without a full
relayout. Chrome uses the same lookup, with explicit tab-row registration.
Its resident-image callback maps empty coverage to the baked space image;
plain spaces and invisible artwork therefore remain blank in tab titles.

The raster and GL backend implementations already reserve capacity at INIT,
copy descriptors before submit returns, and support append-only pixel storage
through their existing borrowing boundary. The editor now supplies their full
runtime limits. No renderer implementation change is needed, and no GL texture
allocation or atlas-growth allocation is added to the typing path.

Completed missing/unsupported glyphs use pre-reserved replacement artwork.
Hash/descriptor admission refusal uses a baked missing marker. Pending question
marks therefore do not masquerade as permanently completed glyphs. This remains
an append-only cache with inherited ceilings; there is no automatic retirement
or eviction. Shutdown retires backend borrows, joins the shared worker pool,
then releases fallback files and cache storage.

The headless regression checks initial and injected Unicode pixels, narrow/wide
cells, preserved base ink with added combining ink, scalar/SIMD raster agreement,
typing allocation guards, submission with the bulk lane held, and text-row
damage on completion. The separately owned stale minimap indicator can clear
in the tab strip when the held index job completes; the damage assertion
explicitly excludes that chrome effect. The paint-file diff is confined to the
glyph lookup and its row registration.

Full bidi/contextual shaping and colour emoji remain outside this bead. Native
display validation and the next Astra comparison drive belong to the coordinator.
