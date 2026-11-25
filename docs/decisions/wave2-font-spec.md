# Wave 2 font and font_async · glyph admission and retirement

**Proposed for advisor signature · Astra · 2026-11-25.** The byte range, cell advance and hit map of a cluster are settled before its ink is ready. Accepting a frame transfers an obligation to keep its sampled ink alive. These two decisions let a key reach submit while an unfamiliar character is still waiting for a worker.

**Q4.** Shared submit preparation validates one complete sampling footprint, snapshots the mutable frame values, and assigns each distinct sampled rectangle a checked tight R8 offset; both backends use the same row-copy routine and offset table over pinned immutable sources.

**Q9.** Lookup protects a candidate view; submit preparation separately pins the distinct images sampled by the composed frame, including retained rows, chrome and scroll overscan; refusal drops the provisional frame pin, success transfers it through matching T5 or proven device quiescence.

## Invariants

| Structure | Invariant |
|---|---|
| Sized font family | One generation binds immutable validated face bytes, TTC indices, primary metrics, sizing convention, ordered fallback policy and presentation policy. |
| ASCII sheet | The printable ASCII strip, metrics and R8 extents belong to the same sized primary and are immutable for the sheet's lifetime. |
| Glyph key | Equality requires the same generation, exact bounded cluster bytes, cell span and text/emoji presentation. |
| Runtime entry | Each entry has one non-reused incarnation within its owner and can publish at most one settled answer for that key. |
| Image | A sampled image identifies immutable, initialized R8 coverage inside one owned extent and an already placed cell-space rectangle. |
| Request and result | One work lease owns the copied operands, exclusive shaping state, output tile and undelivered result until its final invocation returns and its message is consumed or invalidated. |
| View hold | A hold protects all runtime images named by the reusable cells of one publication or by its builder, even while submission is busy. |
| Frame pin | One owner-qualified token protects the deduplicated sources and their backing generation for exactly one attempt or accepted device obligation. |
| Prepared frame | Every address needed after submit points into prepaid frame storage or the specifically pinned immutable source pixels. |
| Scale request | Only the newest compatible surface, selection and size request may install a fully admitted generation. |

## Font at the layout boundary

L3 owns `font` and `font_async`. The former interprets fonts; the latter exclusively owns runtime lookup, requests, tiles and retention. Keep `render_types < font` and `render_types < render < raster | gl`. Render accepts values and a small pin-provider adapter from its public front door; font and render never include each other's implementation. L5 wires this through the public rendering interface. It implements the signed-pending layout proposal's `layout_paint.glyph(ctx, cluster, len, width, cell)` with a current view hold. Layout retains its pointer-free 20-byte cell (P, render contract), immutable row payloads and full byte hits. The adapter sets only ink identity and slot; layout owns colours, attributes, wide halves and all geometry. On the right half use background-only ink. For an oversized cluster layout emits the replacement at the streamed width and retains the true hit interval. Invalid UTF-8 keeps the inverse question mark per invalid byte. A standalone zero-width cluster consumes bytes without requesting a tile.

Use the unprojected cluster for deciding presentation, before discarding selectors. Apply A16 over the pinned Unicode tables: a bare default-text Extended_Pictographic uses text at width one, with VS16 it uses emoji at width two (G, A16), including ©, ®, ❤ and ‼. Test the property set, plus RI sequences, VS15 text form, combining marks, and joined emoji. Use the L1 cluster width and pass the chosen presentation with the bytes to font. An adapter can derive presentation from the original bounded bytes; if layout passes projected text it must already have preserved the presentation decision. Segmentation class alone loses bare ©. Leave font generation and paint serial outside each cell, in the binding for the directory. A nonblank cell carries an atlas slot and that image's incarnation in its glyph identity field. Resolve both against that generation; exhaust identities by retiring the generation before wrap. The slot is an index, never a font-file glyph ID.

### Bootstrap, validation and face selection

Ship the vendored DejaVu Sans Mono bytes and both checked-in R8 ASCII bakes. In the pin these are face zero, printable U+0020 through U+007E (P, 95 inclusive entries), at 15 and 30 pixel heights (M, the two atlas declarations). Open a bootstrap generation directly over the read-only constants and verified build manifest. It paints on the first frame with discovery held. For a request without a matching sheet, use a coherent bootstrap picture at its own metrics until an exact-size sheet is ready. Do not label it as the requested physical geometry. Runtime reuse of a bake requires matching byte SHA-256, collection index, sizing convention/scale, metrics and pixel recipe. Equal family name, file length or rounded pixel number cannot establish identity. Generate/check the embedded-byte digest, source index, dimensions, metrics and all ASCII pixels from the pinned source, with the same top-left placement; keep the optional trailing C string terminator outside the font extent. Reuse these tables across both backends. Rasterising ASCII during the first key would make startup depend on discovery or outlines.

On INIT/worker, validate ranges using checked offsets before handing immutable bytes to stb: sfnt/TTC index and face offset, directories, cmap and metric counts, loca monotonicity and glyf bounds/components or guarded CFF programs, subroutines and operand stack. Bound recursion, expansion and scratch before allocation or descent. GSUB validates reachable offsets and lookup budgets before use; keep the pin's bounded ccmp/liga/rlig subset, extension unwrapping and contextual/ligature substitutions. If shaping is absent, inapplicable or exceeds its cap, take the pin's projected monochrome artwork path. A malformed required table rejects the face. Parse into temporary state and roll back the arena mark on failure. Place by signed bearing and primary baseline, clip to the requested single/double cell image, and overlay marks at the base pen. A face whose valid glyph produces no coverage settles as EMPTY, including non-space characters. The result carries no image descriptor. Space/EMPTY yields `{glyph_index=0, atlas_slot=RENDER_NO_SLOT}`; backgrounds, underline, selection and caret still paint. A missing printable glyph retains visible replacement art. The producer never constructs a zero-area rectangle.

An explicit file wins over family; parse optional decimal `#index` with bounds and require index zero on an ordinary face. Exact primary family selection must resolve to that family, validate as monospace and fit admitted metrics. Empty selection uses embedded; invalid or missing custom primary selects embedded with a reported selection result. For a different size or face, build the full ASCII sheet on the worker before activation. Resolve configured fallbacks in declaration order, skip/report unusable entries, deduplicate by loaded face identity including collection index. For text, try the primary, then the configured chain, then already loaded compatible faces and a bounded system coverage lookup. Emoji requests try presentation-compatible monochrome emoji art in that order with whole-cluster GSUB first; a text-only primary's base-scalar cmap hit cannot preempt the emoji request. If no substitution exists, project with the pinned artwork rules, preserving the original layout width. Use an exact, generation-scoped negative result only after exhausting eligible faces. Missing system discovery leaves the embedded primary usable. Capacity, cancellation and malformed files have distinct outcomes from absent coverage. Cap the number of faces and *aggregate* loaded bytes; reuse only immutable loaded bytes/index within a generation. Never consult the filesystem or fontconfig from lookup. A two-face CJK/emoji pair cannot represent a configured chain or multiple TTC faces for one script.

## Admission, pressure and physical retirement

The UI alone mutates the lookup index, ready-image directory, holds and free lists. Use one fixed-width double-cell tile per runtime entry. Assign its index arithmetically at setup; a narrow image exposes only its left cell. This costs up to one unused cell per narrow entry (P, two-cell maximum less one-cell use), and removes adjacency searches, compaction, and the case where every other narrow tile is free yet no wide tile fits. A page can be a slab of independent tiles with a checked tile stride. Keep one bounded exact-key search tree, free list, FIFO of pending requests and an intrusive list of **eligible** victims in recency order. A hash alone is insufficient for equality; a balanced tree gives a worst-case comparison bound for adversarial bytes. Touching a held entry or changing its eligibility is constant-time list work. Evict the oldest eligible settled entry only. Pending requests, private output, every kind of hold and an unretired work lease independently exclude reuse. Entries may cache EMPTY and permanent missing outcomes. Fixed tiles make their wasted coverage explicit in the bound below.

| Entry state | Transition and visible result |
|---|---|
| VACANT → QUEUED | On a miss fitting the byte limit, reserve an entry or evict one eligible victim, copy the key once and return the generation's immutable question mark immediately, left-aligned for a wide cluster. |
| QUEUED → RUNNING | Submit the oldest admitted key once; a refused work handle leaves it queued for capacity wake. Repeated lookups share the request. |
| RUNNING → DELIVERED | A matching message identifies fully initialized private output. Record it in O(1); wait for physical retirement before detaching worker storage. |
| DELIVERED → INK / EMPTY / MISSING / LIMITED | Adopt at a UI paint boundary. INK exposes the previously private tile; EMPTY exposes no slot; MISSING shows the marker. LIMITED records an explicit resource/complexity refusal and its retry condition. |
| Any live request → RETIRING | Cancel obsolete requests once; drop late results, retain the argument, font bytes and tile until the handle is physically finished and the message invalidated/drained. |
| Settled → VACANT | Remove the exact key and invalidate the incarnation only when all owners release it. |

The marker comes from the active sheet, with a prevalidated visible bootstrap marker prepared to the same cell dimensions if the custom question mark is itself inkless. It has a real valid rectangle for each allowed span (pad the right half of a wide marker with zero coverage). Pin the marker actually sampled; a pending key refers to a separate, private destination. A completion never rewrites pixels or identity under an old cell. If there is no eligible slot, return the marker and PRESSURE without evicting anything or spinning. Record a coalesced visible repaint need. Oversized keys return LIMITED without hashing/copying their tail. Work-table or tile pressure is recoverable on physical release, a change of visible demand, or admitted capacity growth. Retry unadmitted cells in stable screen order after such a change; an inherently over-capacity view may retain markers. A failed attempt to promote the same unchanged working set is not a wake source.

Use one serial glyph worker per owner so stb scratch/userdata is exclusive. Dispatch discovery, reads and face preparation on WORK_BULK; use WORK_BULK continuations for glyph composition as well. This explicitly permits a cold glyph to stay a marker while bulk is held. Navigation and interactive paint must continue. Leave WORK_RASTER for frame raster and WORK_FOREGROUND for bounded interactive CPU work; the latter prohibits blocking I/O. Keep the active operation cursor (face, decoded scalar/GSUB position, composite image and publication phase) across `work_continue`. Check cancellation at work.h's CPU polling boundary. On a full mailbox save the finished result and call `work_continue`, then return; drain wakes that producer and only publication is retried. No recomposition or sleep loop. Route messages through the shared pool drain and verify kind, pool lifetime, handle slot/epoch, request/generation and entry incarnation before adoption. The small message identifies prepaid state; no borrowed cluster or layout pointer crosses threads. `work_handle_finished` and a consumed/invalidated result are both required before reuse, even if cancellation was requested or delivery ran before the worker returned. Drain, check handles/readiness and pending mail before sleeping. Wake the app for ready paint, never synchronously wait in the key path. A suspended or quiescing owner rejects new work and retains old resources until its obligations end.

The service batches settlements, increments a paint serial and requests decoration for all potentially affected visible users, including chrome and overscan. The simple implementation redecorates the complete visible publication once per batch with shared certified geometry/hits; it needs no reverse map from glyph to mutable rows. Preserve the original cell footprint even for EMPTY or permanent missing. A paint begun before settlement may finish with its frozen art; queue a new decoration at the next boundary, without continuously restarting a follow. Make changes visible to lookup only between complete paint generations (staging DELIVERED in the meantime). Reuse of rows across changed paint serials requires redecoration or a proof that their art is unchanged. Never promote a row from approximate to exact merely because ink arrived.

## Q4 · one preparation and one pixel address rule

1. In the non-reentrant UI step, qualify the candidate by owner, atlas/paint generation, geometry, and layout publication. Check BUSY before spending on packing. Walk the final composed grid, including unchanged rows, glyph-bearing chrome and the extra scroll source row. Validate bounds, pairs and identities even on zero damage; reject malformed input at the producer/submit boundary. Begin a provisional pin and gather distinct nonblank images with pin-image, a prepaid mark table and a touched list. Every error from here through handoff unwinds this token. An empty cell contributes zero descriptors. Sample the same overscan columns the Q8 transform may use. Independently validate fixed RGB region position, stride and extent.
2. Preflight canonical strips, actual physical width × height, cell/overscan capacity, source pages, number of unique images, backend table elements/bytes, packed R8 bytes, padded destination texture footprint, region storage and integer representability. For a nonempty rectangle check `x+w <= page.width`, `y+h <= page.height`, `stride >= page.width`, and the last referenced byte end `(y+h-1)*stride+x+w <= pixels_len` with widened checked arithmetic; validate the declared page extent too. Offset is the prefix sum of prior unique `w*h`. Every reference to that image uses that offset. A wide-right cell consumes none. A short row stride, out-of-bounds overscan, arithmetic overflow or a one-byte-short allocation refuses the **whole** attempt with a reason.
3. Freeze the checked (source R8 pointer, row stride, first byte, w, h, destination offset) values under that provisional pin. Snapshot canonical dirty cell spans and any sampler dependencies, overscan, region pixels when changed, transform, strips and compact descriptor mapping into the prepaid frame slot. Preserve the last accepted retained grid in backend-owned storage; apply changes there only on commit. All cells sampled by a full redraw are available there. Keep retained cells in stable source identities and rebuild the per-attempt slot→offset mapping, or translate while making a complete frame-owned snapshot; an old dense offset cannot survive repacking. The simple frozen choice is stable identities in the retained grid. A backend using dense TBO indices builds its table by stable slot, filling entries for every sampled image. Include that indexed table capacity in preflight.
4. For each referenced image, the common packer copies `w` bytes of row `j` from `first+j*src_stride` to `offset+j*w`. Raster indexes this tight plane as `offset+y*w+x`; GL linearizes the same byte index to integer texture coordinates. A texture-row boundary may fall inside a glyph. Both use the common prepared pixels and descriptors. Transfer, strip raster, texture padding and region Y inversion remain backend-specific. Packing may execute after acceptance on a worker using only the frozen values and pinned bytes. Preflight reserves all output before acceptance; a post-accept device failure goes through quiescence. No second planner may enlarge the sample set. Dirty drawing may repack the bounded complete referenced set, even on sparse damage; the price buys a simple proof when a formerly clean row supplies fractional coverage. A zero-sample no-op has an empty pack plan.
5. The successful, indivisible handoff commits the frame and its pin before worker visibility or notification dispatch. A synchronous refusal consumes no frame ID, input sequence, damage or prior displayed serial; release the attempt pin and discard scratch. The candidate and its layout hold remain retryable. Never repair a malformed input by substituting a blank. After success the caller may overwrite cells, descriptors, region or page metadata immediately. A second backend-specific repack from the caller's page table would race that reuse. Copying entire source pages would charge a sparse frame for unused high-DPI capacity.

## Q9 · three distinct lifetimes

- **View hold.** The previous reusable publication holds its actual images independently of T5. Open a builder hold before the first glyph lookup. Keep each returned dynamic image once, including those in private progress across MORE. Borrow inherited slots under the previous publication's hold; before dropping it, enroll the final candidate's full reusable layout rows (even when occluded), chrome and overscan. Compact the builder hold to exactly these references by a bounded final walk, dropping speculative/offscreen lookups. On cancellation drop only the builder. Promote it to retained on successful submit/accept of the picture; release the former publication once its layout cells are detached. Incompatible pictures still on screen can keep their original hold until replaced.
- **Device pin.** Collect from what can actually be sampled, including reused cells never passed to this frame's lookup. The token owns generation backing plus each distinct actual R8 rectangle, descriptor values and source stride through the accepted frame's T5. The frame owns the descriptor copies; only the pixel spans are borrowed. On BUSY, capacity failure, stale generation, supersession or abandonment release only the provisional token. Never transfer a partially gathered set. Depth one bounds transferred device pins to one. The separate provisional pin may be abandoned without touching it; early BUSY can skip provisional acquisition entirely.
- **Retirement.** Authenticate `(owner incarnation, token serial, backend epoch, frame ID)` against the live ledger. Provisional → transferred → released is single-use; provisional → abandoned is the other legal exit. Duplicate, stale or foreign acknowledgments change no counts. Record an early device notification until T4 ordering permits T5. A Wayland discard completes only the presentation obligation; if the fence is outstanding the pin stays. If T5 arrives first, release device pixels then, preserving depth-one presentation state until displayed/discarded/lost is settled. LOST/teardown requires fenced or joined device quiescence before releasing an unacknowledged pin. A presentation timestamp, a newer frame, cancellation or worker-mailbox delivery cannot substitute for T5. Quiesce backend, cancel/drain/join physical work, detach all views and release generation storage before reinitializing a pool or owner. No wall-time timeout authorizes reuse.

A generation-wide freeze of all tiles through T5 would stall disjoint admissions. A single prior/current boolean would lose the overscan-only reader and cannot express a refused submit while the old frame still owns ink. The separate holds permit eviction of unrelated settled entries throughout.

## Scale and capacity transaction

A coherent `PLAT_EV_SURFACE` names physical client dimensions and DPI/scale, including scale-only changes. Retain the configured point size, derive target em pixels from points × DPI / 72 (P, points per inch) with platform rounding; preserve legacy height sizing for its exact bakes. Use the newest serial, including font selection/chain changes. Keep at most an active generation and one preparing/retiring generation (G, active plus replacement); while both are occupied coalesce newer requests to copied settings and cancel obsolete preparation. Old worker replies cannot activate a generation even if dimensions later recur.

Model each generation bank with FREE → PREPARING → READY → ACTIVE → RETIRING → FREE; bootstrap enters ACTIVE at open. A superseded PREPARING or READY bank goes directly to RETIRING. Service OPEN → CLOSING → CLOSED stops new requests before reclamation. The entry, bank, service and pin ledgers are exhaustive enums under the project switch rules.

Prepare the target ASCII strip, baseline and tile bounds off the input path. Before publishing, intersect raster and baseline GL frame limits with layout and font storage for the target physical surface. Reserve for the whole grid plus overscan, R8/descriptor maxima, texture dimensions, physical drawable, region and the simultaneous old-frame obligations. If a geometry does not fit, report CAPACITY (including required sizes), retain the last admitted generation and request off-path resize/reprovision or leave it visibly pending. Never submit old cells against the new surface serial. On backend idle and admitted geometry, build a full candidate for the new surface and swap binding and layout publication at successful submission. Apply the layout proposal's byte-anchor and pixel-Q8 remainder rules. Changed cell metrics reflow; new art with identical geometry changes paint only. A new surface serial still requires coherent geometry/hit validation and full affected surface damage. Preserve the last actual display serial on refusal. Old generations retire only after both kinds of holds, device pins, private work and mailbox leases drain. An in-place reset can otherwise overwrite a tile while the raster worker reads it.

## Work and storage bounds

Use structural counts. Layer and version numbers, T4/T5/T6, Q8, Unicode scalars, source line locators and witness IDs are identifiers. Let V be the number of cells in the admitted visible grid including fixed chrome plus the sampler's extra row; count a separate reusable layout surface too when sizing view holds. Let D ≤ V be distinct sampled images, S the runtime-entry cap, A the printable sheet count, K the admitted UTF-8 bytes per key, W,H the physical primary cell dimensions and T = 2WH the largest R8 tile in bytes (P, wide-left image). Let P be the sum of actual sampled rectangle areas, Z the number of copied cells including damage dependencies, U the RGB region elements copied and J the indexed glyph-table capacity. All are bounded *inputs to admission*, with checked sums/products before reserving. K can use the pin's 16,384-byte ceiling (G, `FONT_CLUSTER_MAX_BYTES` port limit); larger input takes the already specified marker. Size S from actual reserved tiles, descriptors and exact-key store, taking the smallest supported count. S may be below distinct screen demand; the remainder renders with shared markers. No average typing rate enters these limits.

| Where | Worst case and derivation |
|---|---|
| One lookup including first-ever key (P) | ASCII is direct O(1). A bounded cluster costs O(K·(1+log(S+1))) byte comparisons for a balanced exact-key tree plus O(K) to copy a new key; free/victim selection, enqueuing and marking the returned image touch a constant number of indices. No worker wait, outline walk, page scan or malloc. PRESSURE also returns in this bound. The submitted cell shows the immutable question mark at the final advance. |
| Visible emission and repaint (P) | For L glyph lookups in a layout slice multiply the preceding bound by L. For one complete unchanged-geometry decoration L ≤ V (one lookup per ink-bearing base cell); for a layout walk with discarded speculative rows charge its actual emitted cells under the layout proposal's separate whole-intent budget. Coalescing settlements yields at most one such decoration per delivered batch, not one grid scan per glyph in that batch. |
| Holds and submit prep (P) | Gathering references costs O(V), acquiring/releasing the exact frame set costs O(D), and compacting a builder costs O(V + S + A) worst case if it touched every slot while following. Mark arrays and unique lists avoid clearing an atlas per cell. D ≤ min(V, S+A+marker variants), P ≤ DT. Preflight and metadata capture take O(V+D+Z+U) when strip/region checks are included; reject immediately on depth-one BUSY. Packer reads and writes P bytes plus O(D) descriptors; destination can use stable slots in a preallocated J-element table with touched-index updates. Changed region copies U elements once. A full high-DPI frame can attain O(V+DT+U); sparse frames still pay the reference walk and the sum of referenced areas. |
| UI service slice (G) | Give `font_async_step` B credits from the app. One credit examines one request/retirement, dispatches one job with prepaid arguments, or records one settled result; each is O(1) with direct slot/work tokens and head queues, and image bytes already in a private tile. Keep cursors across returns. Perform at most B such operations; B=0 advances none. Check input between slices. Mailbox routing is independently limited by work.h's examined-message cap and between-callback deadline. A final non-reentrant submit/hold walk has the separate V,D bound above; it cannot be advertised as a B-operation callback. Releasing a hold walks its distinct list; charge that walk to the caller or retire its cursor across slices, keeping the entries ineligible until visited. |
| Worker slice and whole request (G/P) | Give each worker invocation b primitive CPU credits. Meter scalar decoding, face probing, validated table visits/substitution, outline expansion and scan conversion including row/edge/pixel loops. Persist indices and partial image across continuations. For a non-resumable donor primitive precharge a proved upper bound, admit b large enough for that primitive or split it; an oversized primitive takes LIMITED before entry. Never restart a partly completed raster on every credit refill. Configure total per-key outline/shaping fuel N and face/input caps F and M (face count and aggregate bytes). Reading/indexing admitted bytes is separately bounded by bytes M and validator visit caps; each of ≤ K scalars tries ≤ F faces and at most one bounded discovery answer per unresolved scalar. Charge a capped lookup/parse's visits to N; initialize/check the tile's ≤ T bytes once. Thus CPU steps for one admitted key are O(M + K(F+1) + N + T), and a CPU-only batch of at most D new keys costs at most D times this bound. For n charged splittable steps, exactly ceil(n/b) full-or-final quanta suffice when continuously runnable (P, counting units); bounded atomic units may leave a quantum partly unused. Parking for mailbox space or I/O gives no elapsed-time guarantee. Discovery uses cancellable, reaped helper operations with bounded request/result bytes, off the UI lane. Resource refusal cannot trigger an unbounded automatic reparse. |
| Generation storage (P) | One entry record, balanced-tree links, exact key and fixed tile per runtime slot: S·(K+T+O(1)) bytes. Add ASCII sheet AWH plus O(A) metrics, marker tiles O(T), loaded font bytes M, O(F) face records, bounded parser/outline scratch X, and one serial worker's bounded continuation O(K+X+T). The worker writes directly into its reserved unpublished entry tile and needs no second runtime atlas. Holds use per-slot marks plus touched lists O(S+A) per live builder, publication and attempt. A frame stores O(D) used descriptors, O(J) stable-slot mapping, O(V) retained cell/strip storage, O(Umax) region elements and ≤ min(V,J)·T packed bytes plus at most one physical texture row of padding (P, ceil division by texture width). Size Z/U storage to full admitted frame, not current damage. Two generation banks bound overlap; the depth-one frame scratch is reserved once. Add the immutable embedded data and both bakes once per process. Device surfaces have their own width × height × pixel-format bound. |
| Law 2 gate (G) | Prepay `base_arena` regions and fixed `base_pool` blocks, including tile backing, marks, result staging, job arguments and submit scratch, during setup or off-path reprovision; pre-touch UI-hot storage. No arena growth on a key. Run the release guard from input through successful submit with a cold key, all slots held, delayed publication and pending X packets; require active guard and zero counted malloc/calloc/realloc. The landed guard counts process-wide: preallocate worker scratch too, isolate fontconfig work in a helper process started outside the guarded path, and keep dynamic loader/allocator setup out of overlapping key windows. No thread exemption or pause of the guard. G1 requires its own measurement. |

The caps K,S,F,M,X,N,b and maximum surface are admitted configuration quantities. Record their actual values, resulting bytes and refusals in implementation evidence. A budget in nanoseconds or an expected cache hit ratio cannot prove a byte or operation bound.

## Proposed public headers

C declaration text for the two L3 modules follows. `render_cell` is the shared value type in `render/render_types.h`. Render's public adapter can forward the glyph operations without exposing font, platform or backend private state to the app. All opaque-object storage below is carved from the named caller-owned arena; open/prepare failures roll back to the entry mark. Output borrows include their length and lifetime. All identity fields use nonzero, non-wrapping issuance within the owning service lifetime.

### font.h

```c
#ifndef FONT_FONT_H
#define FONT_FONT_H
#include <stddef.h>
#include <stdint.h>
#include "base/base.h"
#include "work/work.h"

typedef struct font_family font_family;
typedef struct font_cursor font_cursor;
typedef struct font_bytes { const uint8_t *data; size_t len; } font_bytes;
typedef struct font_cell { uint32_t width, height, ascent, descent; } font_cell;
typedef struct font_metric {
    int32_t advance, bearing_x, bearing_y;
    uint32_t width, height;
} font_metric;
typedef struct font_sheet {
    font_bytes r8;
    size_t stride;
    uint32_t px;
    font_cell cell;
    const font_metric *ascii_metrics;
} font_sheet;
typedef enum font_sizing { FONT_PIXEL_HEIGHT, FONT_EM_PIXELS } font_sizing;
typedef enum font_presentation { FONT_TEXT, FONT_EMOJI } font_presentation;
typedef enum font_rc {
    FONT_OK, FONT_MORE, FONT_EMPTY, FONT_MISSING, FONT_INVALID,
    FONT_LIMIT, FONT_CAPACITY, FONT_CANCELLED
} font_rc;
typedef struct font_request {
    font_bytes family, file, fallback_chain;
    uint32_t px;
    font_sizing sizing;
} font_request;
typedef struct font_limits {
    size_t faces, face_bytes, scratch_bytes, key_bytes;
    size_t cpu_quantum, per_key_fuel;
    uint32_t max_cell_width, max_cell_height;
} font_limits;
typedef struct font_notice { font_rc primary; size_t skipped_fallbacks; } font_notice;
typedef struct font_key {
    font_bytes cluster;
    uint32_t span;
    font_presentation presentation;
} font_key;

font_bytes font_embedded_data(void);
const font_sheet *font_bootstrap(uint32_t baked_px);
font_rc font_prepare(base_arena *bank, const font_request *request,
    const font_limits *limits, work_ctx *worker, font_family **out,
    font_notice *notice);
const font_sheet *font_ascii(const font_family *family);
font_rc font_compose_begin(base_arena *scratch, font_family *family,
    font_key key, uint8_t *tile, size_t stride, size_t extent, font_cursor **out);
font_rc font_compose_step(font_cursor *cursor, size_t credits, work_ctx *worker);
void font_compose_end(font_cursor *cursor);
#endif
```

`font_bootstrap` returns a static sheet only for its actual baked height. `font_prepare` is a cancellable bulk/setup operation on exclusive storage, performs selection/validation and sizes the ASCII sheet; its bounded file reads and initialization are outside the UI slice. On failed custom selection, success with `notice.primary` carries the embedded replacement. Missing/unsupported GSUB falls back within a validated face. Keep `font_family` exclusive on composition even though its sheet is a const borrow. Cursor borrows an immutable copied key and a private tile; MORE resumes exactly there. Completed OK means positive-area ink, EMPTY means all-zero coverage, MISSING means absent and LIMIT/CAPACITY are explicit failures. End releases scratch on every exit. Family/file resources must be arena data or closed before prepare returns; teardown resets the exclusive bank only after its final lease. Backend atlas admission and system face discovery have no UI-callable synchronous entry point.

### font_async.h

```c
#ifndef FONT_ASYNC_H
#define FONT_ASYNC_H
#include <stdbool.h>
#include "font/font.h"
#include "render/render_types.h"

typedef struct font_async font_async;
typedef struct font_async_version { uint64_t owner, generation; } font_async_version;
typedef struct font_async_hold { font_async_version version; uint64_t serial; } font_async_hold;
typedef struct font_async_pin { font_async_version version; uint64_t serial; } font_async_pin;
typedef struct font_async_frame { uint64_t backend_epoch, frame_id; } font_async_frame;
typedef struct font_async_caps {
    font_limits font;
    size_t slots, cells_with_overscan, pages, table_entries, packed_bytes;
    size_t region_elements;
    uint32_t surface_width, surface_height, texture_width, texture_height;
} font_async_caps;
typedef struct font_async_size {
    size_t bank_bytes, service_bytes, source_bytes, table_entries, packed_bytes;
} font_async_size;
typedef struct font_async_image {
    const uint8_t *pixels;
    size_t extent, stride;
    uint32_t page_width, page_height, x, y, width, height;
} font_async_image;
typedef enum font_async_rc {
    FONT_ASYNC_OK, FONT_ASYNC_MORE, FONT_ASYNC_WAIT, FONT_ASYNC_PRESSURE,
    FONT_ASYNC_LIMIT, FONT_ASYNC_CAPACITY, FONT_ASYNC_INVALID, FONT_ASYNC_STALE
} font_async_rc;
typedef struct font_async_update {
    font_async_version version;
    font_cell cell;
    font_notice notice;
    uint64_t request_serial, paint_serial;
} font_async_update;

font_async_rc font_async_measure(const font_async_caps *caps, font_async_size *out);
font_async_rc font_async_open(base_arena *arena, work_pool *pool, uint64_t owner,
    const font_async_caps *caps, const font_sheet *bootstrap, font_async **out);
font_async_rc font_async_request(font_async *a, uint64_t surface_request,
    const font_request *request);
bool font_async_event(font_async *a, const work_msg *msg);
font_async_rc font_async_step(font_async *a, size_t credits, font_async_update *out);
font_async_rc font_async_hold_begin(font_async *a, font_async_version version,
    font_async_hold *out);
font_async_rc font_async_lookup(font_async *a, font_async_hold builder,
    font_key key, render_cell *cell);
font_async_rc font_async_hold_keep(font_async *a, font_async_hold hold,
    const render_cell *cell);
font_async_rc font_async_hold_end(font_async *a, font_async_hold hold);
font_async_rc font_async_pin_begin(font_async *a, font_async_version version,
    font_async_pin *out);
font_async_rc font_async_pin_image(font_async *a, font_async_pin pin,
    const render_cell *cell, font_async_image *out);
font_async_rc font_async_pin_abort(font_async *a, font_async_pin pin);
font_async_rc font_async_pin_commit(font_async *a, font_async_pin pin,
    font_async_frame frame);
font_async_rc font_async_pin_retire(font_async *a, font_async_pin pin,
    font_async_frame frame);
font_async_rc font_async_close(font_async *a);
#endif
```

Measure uses maxima, including derived physical wide tiles and worst-case packed bytes, and returns checked requirements before reserving; unrepresentable products return CAPACITY. The backend supplies the common supported limits. Open takes an owner ID issued by the caller that cannot be reused while any old tokens or feedback can arrive. Request copies bounded settings immediately; stale request serials fail. Step distinguishes progress needing another slice (MORE), a wake dependency (WAIT), and a ready/current version with metrics, selection notice and paint serial (OK). Initial OK supplies the bootstrap version. The app can build a candidate for a ready replacement; the first successful pin commit for it activates that version atomically with the frame. Step also reports terminal preparation failures against the requesting serial. Frozen paint lookups occur in one builder at a time; service can drain messages while it spans slices and stages later changes until the generation boundary.

Lookup writes a valid ink pair on hit, queued miss, pressure or limit. The latter two return the marker with their status. Programmer/binding errors return INVALID/STALE without changing the cell. Hold-keep imports inherited cells after identity validation. To compact without a grid pointer in this API, start a fresh final hold, call hold-keep for every reusable cell while builder/previous holds still exist, then end the speculative hold. Reserve for that transient hold as well (P, old retained + builder + compacted candidate); adoption moves the final hold by value. A refused submit leaves that hold with the candidate. Pin-image validates the matching identity, enrolls it once and returns a descriptor by value; background-only is valid with zero output bytes and produces no plan entry. A returned R8 pointer lasts only for the provisional or transferred pin. Commit checks all token/frame preconditions before the single irreversible handoff; success cannot subsequently be refused as BUSY. Abort/retire require the corresponding ledger state. Only the render lifecycle may invoke retire, after T5 or authenticated quiescence for that frame. Close cancels outstanding work and returns WAIT while any physical lease, live hold or device pin survives; keep draining and keep the owner/arena live. The final OK detaches bank storage. No callback enters layout or mutates cells.

## Witnesses for signature

These are implementation acceptance cases. The font row of the harvest's integration table is R5. Adjacent rows below identify how to observe the same lifetime and paint contract across modules.

| Witness | Failure or interleaving | Design step and assertion |
|---|---|---|
| **R5** · `editor_test:binary_open_contract`, `unicode_render_test`, v2 `tests/contract/cases/files.py` | Opening or reactivating the binary/PDF tab discovers inkless fallback coverage, including the pin's private-use U+F227 case. | Force the worker to return a valid all-zero bitmap, settle, switch away/back and reopen. Inspect the **producer's** cell before submit: zero identity/NO_SLOT, intact width and hits, zero rectangles emitted for it. Require successful frames on null, raster and GL; an unrelated malformed descriptor must still be refused. |
| A16, `font_gsub_test`, `unicode_render_test`, layout R11 `a7fo_click_contract` | Wrong face from segmentation class, or changing a marker to wide/blank art moves the click target. | Enumerate default-text pictographs bare/VS16; compare face/presentation, span and byte hits before/after settlement, including wrap, wide-right, overscan and synthetic chrome boundaries. |
| `font_test:test_font_bounds/test_cff_errors/test_ttc/test_bake_consistency/test_atlas_identity`, font/GSUB fuzz | Truncated tables reach stb, or a lookalike primary uses vendored pixels. | Exercise malformed extents, swapped TTC index and changed bytes/scale; require clean rollback and exact ASCII metrics/pixels for both true bakes. |
| `font_selection_test`, `font_system_test`, `font_size_test`, size fuzz | Missing family silently substitutes, fallback order changes, or late zoom wins. | File/index precedence, missing notice, ordered coverage and missing service; delay the old result across scale-only changes, enforce newest serial and check one-byte-short packed/surface caps leave old binding intact. |
| `work_foreground_test`, occupied-bulk R6, live cold-key guard | Fontconfig blocks the interactive lane or cold glyph waits on the worker. | Hold bulk discovery, submit a key with the marker and make navigation progress; saturate the mailbox, release space, verify a single adoption with no reraster and zero guarded allocations. |
| `render_test`, `gl_scroll_test`, `raster_scroll_test`, `pointer_overscan_contract` | Mutable metadata alias, padding bleed, or eviction of the only glyph in the Q8 overscan row. | Pad source rows with sentinel values, overwrite caller descriptors/regions after accept, pressure all slots, compare backend readback across the scroll seam; the overscan image stays byte-identical through T5. |
| `editor_test:dzk1_render_reject`, `wayland_gl_test`, new pin ledger cases | BUSY/refusal leaks pins; discard, duplicate T5, stale backend epoch or shutdown frees another frame's tile. | Refuse after provisional acquisition, assert prior/candidate holds intact and no frame ID spent; retry successfully. Deliver discard/fence in both orders and delayed retire after restart. Show eligibility only after the final independent hold and physical lease release, then present the next frame. |
| `editor_test:review_chrome_damage/raster_blink_damage`, `unicode_render_test` | Late art damages one occurrence, or a stale decoration lands on a new tab. | Repeat one key in multiple rows, chrome and overscan; coalesce adoption, compare all pixels to fresh composition, keep same hits/anchor and reject the obsolete publication. |

## Donor footprint and proposed line budget

Counts use the harvest's comment/blank-stripped C-and-header method at pin `6fec2e8`. (M) here is static source inventory; power/load do not apply. Estimates below count both proposed public headers and their private production code, including the font-specific adapter work. They exclude generic render/backend implementations and test sources as in the donor comparison.

| Inventory | Lines and derivation |
|---|---|
| v1 total (M) | 18,945, all font and named editor donors in the harvest. |
| Data, carried separately (M baseline; E proposal) | 10,726 embedded font + 2,953 larger ASCII bake + 816 smaller bake = **14,495**. Budget the same 14,495 with the same representation; regenerated manifests count separately as code/tooling if added. |
| v1 code baseline (M) | 18,945 − 14,495 = **4,450**, including 458 Windows-only discovery/DirectWrite lines (227 + 9 + 16 + 206). Linux-relevant mixed donor inventory = 3,992. |
| Proposed validator, metrics, identity, GSUB, placement and headers (E) | **1,950** rounded from donor font.c 881 + CFF 286 + GSUB 506 + 31 + identity 55 + remaining room for placement and the reduced public declarations. |
| Proposed selection, fallback and preparation (E) | **650** from POSIX discovery 330 + selection 65 + 19 + fallback 111 + about 125 for bounds, sizing and asynchronous preparation glue. |
| Proposed runtime owner and font-specific render adapter (E) | **950** from async.c 575 + roughly 375 for independent view/frame ledgers, two-generation retirement, checked packing inputs, continuations and service declarations. |
| Proposed total (E) | **3,550 code + 14,495 data = 18,045**. Code allowance is 900 below the whole v1 inventory and 442 below its Linux-relevant inventory. Local growth over async.c pays for refusal rollback, overscan coverage and authenticated physical retirement. Verify by the same counting rule after implementation. |

Retire `font_cache` and its append-only shelf/image cache in favor of fixed unpublished tiles and one runtime index; retire `font_runtime` and its parallel binding in favor of generation banks in the same owner. Replace the historical two-slot fallback with the bounded ordered face chain. Remove UI outline raster, polling sleeps, `font_async_frame(active)`, `font_async_repair`, render-grid repair and Windows discovery/DirectWrite from the Linux implementation. Keep the validator, bounded shaping, file/index semantics, baseline placement and reproducible data. A process-wide mutable glyph cache would couple independent surfaces and make stale worker or T5 messages ambiguous.

**Signature scope.** Approval freezes the two module contracts, fallback while pending, pressure behavior, shared R8 addressing, independent retention and the scale admission transaction. Backend upload-mode selection, fractional blend math, raster X connection ownership, platform DPI policy, layout's row algorithms, real-display results, release G1/allocator measurements and final source counts belong to their respective implementation gates. No Wave 1 API amendment is requested.

**Source record.** Wave 2 harvest, font/render/raster sections and integration table; PLAN_V2 §§2 and 4.6; A16; viewport/layout proposal on branch `t3/wave2-viewport-layout-spec`, especially its paint callback and publication rules; landed `work/work.h` and `base/base.h`; pin `6fec2e8`, principally `font/{font,async,selection,discovery_posix,gsub}.c`, `font/{identity,cff_guard}.h`, `editor/font_size.c`, `render/render.h` and `gl/renderer.inc`.

**Advisor signature** ____________________  **Date** ____________________
