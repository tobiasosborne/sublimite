# edit-tkw — font review boundary and runtime owner

The interrupted changes are retained. No vendor or editor source is changed.

## Admission and CFF execution

Whole-font length is capped at `INT_MAX` (G admission ceiling). Directory, TTC
and supported cmap subtable offsets are proved in `uint64_t` before stb reads
and narrows them. Every supported cmap record is checked, including a record
that a later selection supersedes. Merely checking stb's resulting index map
was too late: a high-bit relative offset triggers its signed byte shift.

CFF preflight mirrors the vendored Type 2 interpreter inside the font boundary.
Move/line coordinates and both cubic controls are finite and within signed
16-bit vertex range (G destination range) before any vendor conversion. General
execution retains the vendor step budget. Failed interpretation returns
`FONT_ERR_INIT`; a valid empty outline still returns `FONT_OK`. Family lookup
tries subsequent covering faces after an INIT error; an all-invalid family
retains that error rather than caching missing coverage or successful blank ink.

Bitmap endpoints are rounded with stb-compatible float arithmetic, checked
against signed `int` range, and then converted. Spans are computed in `int64_t`.
The general bitmap ceiling is 4096 per axis and 16777216 pixels (G admission
limits), before allocations and raster passes. This intentionally rejects
extreme fonts rather than clamping outlines into incorrect successful content.

## Foreground work

The cache uses the bounded foreground APIs. TT admission allows 256 expanded
points and 16 component visits; CFF allows 2048 steps and 256 vertices; bitmaps
are at most 64 by 64 and outline/box work at most 16384 (G structural limits).
CID FDSelect uses binary search and per-face/private dictionaries are capped
at 256 bytes (G limits). Flattening has at most 4096 subdivision visits and
512 points across both passes; scanline work is checked again after flattening
(G structural limits). Storage uses the existing arenas and existing thread
local assertion boundary; no new global or typing-path malloc is introduced.

Expensive glyphs are explicitly refused on UI, with fallback where available.
General metrics/raster remain available on exclusively owned worker state.
These limits bound CPU work, not time while the OS deschedules the thread.
The loaded-box measurements in the worker report are TRACK observations, not
a verdict that every scheduler phase meets the 0.5 ms UI slice (G). Arbitrary
complex glyph worker preparation/adoption is a future editor integration task.

## Discovery ownership

Each discovery child starts its own process group. Cancellation, invalid
output and the 30-second query deadline (G) apply independently of reaping and
EOF. Cleanup kills that group and closes the read end, then finishes reaping
the direct child. This prevents inherited stdout from pinning pool shutdown.

## Editor hook for the runtime owner

The primary must match the embedded baked font identity and supported bake
size. At INIT allocate disjoint file/cache arenas and a stable `font_runtime`.
Optional fallback discovery must already have been adopted and remain immutable
through preparation. `font_runtime_limits` supplies backend page, glyph and
atlas byte reservations, including the baked page; reserve these before backend
initialization. Its glyph reservation respects the renderer's descriptor cap.

Call `font_runtime_init`, submit `font_runtime_prepare_job` on WORK_BULK, and
retain the owner/config fallback/arenas through physical work completion and
mailbox drain. The worker stages its status and does not touch font/cache state
after publishing. Adopt only from a live work mailbox callback by calling
`font_runtime_event`; never adopt by polling staged fields. Failure is available
in `owner.result`; before adoption, binding/lookup return `FONT_MORE`.

After successful adoption call `font_runtime_bind` on the editor grid and
install `layout_config.glyph = font_runtime_glyph` with `glyph_ctx = &owner`.
Cold append updates the bound grid's descriptor counts. Existing layout
continuations already handle `FONT_MORE`; check input between slices and replay
the pending key. Publish a full relayout/redraw when switching from baked-only
rendering to the owner.

Pixels/slots are append-only for the owner's lifetime. On exhaustion prepare a
separate owner/cache on a worker, keep the old owner through every backend T5
borrow, switch at a safe backend retirement boundary, and fully relayout.
Reclaim old arenas only after their work lease physically finishes, messages
are drained, and all backend borrows retire; quiescent shutdown is another safe
boundary. The font module cannot infer backend retirement or perform teardown
on the editor's behalf. The existing deferred Unicode integration test covers
borrowed atlas lifetime; the new owner test covers mailbox adoption and grid
pixels for covered Unicode. Production editor/native-pixel wiring is deliberately
left to an editor bead, as required by this bead's scope.

## Fuzz control

Raw font inputs always exercise face zero. An optional independent control
prefix selects additional TTC faces. Generated plain OTF seeds cannot select
a face from their final charstring byte. Startup verifies all generated seeds
initialize, rasterize A and reach their intended B path (normal, exhausted
subroutine budget, CID). Both general and foreground raster APIs are fuzzed.
