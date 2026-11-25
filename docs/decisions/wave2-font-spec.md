# Wave 2 font and font_async · glyph admission and retirement

STATUS: SIGNED by the advisor 2026-11-25 as the Wave 2 font contract (af0e95e; Law 2 row amended at ae7a836 per A31): font.h six functions, font_async twelve, no holds, no pins. K = 124 (two-sided derivation), P = 4 with ceil(2S/P) buckets, composition on WORK_FOREGROUND with NEED_FACE parking for discovery, atomic composition per key. F7 as written; v1's "?" for pending is recorded as the pin's behaviour.

**Revised proposal for advisor signature · Astra · 2026-11-25.** Layout settles a cluster’s byte range, advance and hits before its ink arrives. An admitted cold glyph paints background at that footprint. Settlement requests a new decoration of the same geometry.

**Q4.** Submit validates changed cells against current identities and copies a bounded FIFO prefix of tight R8 tile deltas into the packet. Each backend applies those deltas to its own atlas before drawing.

**Q9.** Font owns the UI tile and the physical work that fills it. Render owns the accepted bytes. A cell samples only a matching slot and version within its bound generation.

## Invariants

| Structure | Invariant |
|---|---|
| Sized font family | One generation binds immutable validated face bytes, TTC indices, primary metrics, sizing convention, ordered fallback policy and presentation policy. |
| ASCII sheet | The printable ASCII strip, metrics and R8 extents belong to the same sized primary and are immutable for the sheet's lifetime. |
| Glyph key | Equality requires the same generation, exact bounded cluster bytes, cell span and text/emoji presentation. |
| Runtime entry | Each entry has one non-reused incarnation within its owner and can publish at most one settled answer for that key. |
| Image | A sampled image identifies immutable, initialized R8 coverage inside one owned extent and an already placed cell-space rectangle. |
| Request and result | One work lease owns the copied operands, exclusive shaping state, output tile and undelivered result until its final invocation returns and its message is consumed or invalidated. |
| Lookup stamp | Each dynamic lookup records the current paint serial. A pressure sweep refreshes entries named by the live publication. |
| Prepared frame | The accepted packet owns every variable byte needed to update the retained backend scene. |
| Scale request | Only the newest compatible surface, selection and size request may install a fully admitted generation. |

## Font at the layout boundary

L3 owns `font` and `font_async`. The former interprets fonts; the latter exclusively owns runtime lookup, requests, tiles and retention. Keep `render_types < font` and `render_types < render < raster | gl`. Render accepts values and a call-scoped tile source from its public front door; font and render never include each other's implementation. L5 wires this through the public rendering interface. It implements the signed layout contract’s `layout_paint.glyph(ctx, cluster, len, width, cell)` using a generation-bound font service. Layout retains its pointer-free 20-byte cell (P, render contract), immutable row payloads and full byte hits. The adapter sets only ink identity and slot; layout owns colours, attributes, wide halves and all geometry. On the right half use background-only ink. For an oversized cluster layout emits the replacement at the streamed width and retains the true hit interval. Invalid UTF-8 keeps the inverse question mark per invalid byte. A standalone zero-width cluster consumes bytes without requesting a tile.

Use the unprojected cluster for deciding presentation, before discarding selectors. Apply A16 over the pinned Unicode tables: a bare default-text Extended_Pictographic uses text at width one, with VS16 it uses emoji at width two (G, A16), including ©, ®, ❤ and ‼. Test the property set, plus RI sequences, VS15 text form, combining marks, and joined emoji. Use the L1 cluster width and pass the chosen presentation with the bytes to font. An adapter can derive presentation from the original bounded bytes; if layout passes projected text it must already have preserved the presentation decision. Segmentation class alone loses bare ©. Leave font generation and paint serial outside each cell, in the binding for the directory. A nonblank cell carries an atlas slot and that image's incarnation in its glyph identity field. Resolve both against that generation; exhaust identities by retiring the generation before wrap. The slot is an index, never a font-file glyph ID.

### Bootstrap, validation and face selection

Ship the vendored DejaVu Sans Mono bytes and both checked-in R8 ASCII bakes. In the pin these are face zero, printable U+0020 through U+007E (P, 95 inclusive entries), at 15 and 30 pixel heights (M, the two atlas declarations). Open a bootstrap generation directly over the read-only constants and verified build manifest. It paints on the first frame with discovery held. For a request without a matching sheet, use a coherent bootstrap picture at its own metrics until an exact-size sheet is ready. Do not label it as the requested physical geometry. Runtime reuse of a bake requires matching byte SHA-256, collection index, sizing convention/scale, metrics and pixel recipe. Equal family name, file length or rounded pixel number cannot establish identity. Generate/check the embedded-byte digest, source index, dimensions, metrics and all ASCII pixels from the pinned source, with the same top-left placement; keep the optional trailing C string terminator outside the font extent. Reuse these tables across both backends. Rasterising ASCII during the first key would make startup depend on discovery or outlines.

On INIT/worker, validate ranges using checked offsets before handing immutable bytes to stb: sfnt/TTC index and face offset, directories, cmap and metric counts, loca monotonicity and glyf bounds/components or guarded CFF programs, subroutines and operand stack. Bound recursion, expansion and scratch before allocation or descent. GSUB validates reachable offsets and lookup budgets before use; keep the pin's bounded ccmp/liga/rlig subset, extension unwrapping and contextual/ligature substitutions. If shaping is absent, inapplicable or exceeds its cap, take the pin's projected monochrome artwork path. A malformed required table rejects the face. Parse into temporary state and roll back the arena mark on failure. Place by signed bearing and primary baseline, clip to the requested single/double cell image, and overlay marks at the base pen. A face whose valid glyph produces no coverage settles as EMPTY, including non-space characters. The result carries no image descriptor. Space/EMPTY yields `{glyph_index=0, atlas_slot=RENDER_NO_SLOT}`; backgrounds, underline, selection and caret still paint. A missing printable glyph retains visible replacement art. The producer never constructs a zero-area rectangle.

An explicit file wins over family; parse optional decimal `#index` with bounds and require index zero on an ordinary face. Exact primary family selection must resolve to that family, validate as monospace and fit admitted metrics. Empty selection uses embedded; invalid or missing custom primary selects embedded with a reported selection result. For a different size or face, build the full ASCII sheet on the worker before activation. Resolve configured fallbacks in declaration order, skip/report unusable entries, deduplicate by loaded face identity including collection index. For text, try the primary, then the configured chain, then already loaded compatible faces and a bounded system coverage lookup. Emoji requests try presentation-compatible monochrome emoji art in that order with whole-cluster GSUB first; a text-only primary's base-scalar cmap hit cannot preempt the emoji request. If no substitution exists, project with the pinned artwork rules, preserving the original layout width. Use an exact, generation-scoped negative result only after exhausting eligible faces. Missing system discovery leaves the embedded primary usable. Capacity, cancellation and malformed files have distinct outcomes from absent coverage. Cap the number of faces and *aggregate* loaded bytes; reuse only immutable loaded bytes/index within a generation. Never consult the filesystem or fontconfig from lookup. A two-face CJK/emoji pair cannot represent a configured chain or multiple TTC faces for one script.

## Admission, pressure and physical retirement

The UI alone mutates the index, ready-image directory and free list. Reserve one fixed double-cell tile per entry with a checked stride. Narrow images expose only the left cell. Arithmetic slot assignment removes adjacency searches and compaction. EMPTY and permanent missing may occupy key records even though their cells sample no runtime tile.

**F3.** Use buckets of P=4 ways (design choice), with ceil(2S/P) prepaid buckets. Hash the exact key once, inspect only its bucket, and compare bytes only after generation, span, presentation, length and full hash match. A vacant way can be reused immediately. Direct entry-to-way indices allow deletion in O(1) without shifting neighbors. A miss in a full bucket returns PRESSURE without evicting an unrelated entry. Hash collisions cannot establish equality. Keep the pending FIFO and a free-entry stack separately.

| Entry state | Transition and visible result |
|---|---|
| VACANT → QUEUED | Reserve an index way and a free or evictable entry, copy the key once, stamp it and return zero identity/NO_SLOT at its final span. |
| QUEUED → RUNNING | Submit the oldest admitted key once; a refused work handle leaves it queued for capacity wake. Repeated lookups share the request. |
| RUNNING → DELIVERED | A matching message identifies fully initialized private output. Record it in O(1); wait for physical retirement before detaching worker storage. |
| DELIVERED → INK / EMPTY / MISSING / LIMITED | Adopt at a UI paint boundary. INK exposes the previously private tile; EMPTY exposes no slot; MISSING shows the marker. LIMITED records an explicit resource/complexity refusal and its retry condition. |
| Any live request → RETIRING | Cancel obsolete requests once; drop late results, retain the argument, font bytes and tile until the handle is physically finished and the message invalidated/drained. |
| Settled → VACANT | A pressure sweep may reclaim a settled, unstamped entry after physical job retirement. Remove its index way and invalidate its version before any reuse. |

**F7.** QUEUED, RUNNING and DELIVERED paint background only, including both cells of a wide cluster. EMPTY also has zero identity/NO_SLOT. MISSING, LIMITED and PRESSURE use the active sheet’s settled question mark at the requested span, with a transparent right half when wide. Prepare a visible marker at the same metrics if the custom question mark is inkless. At the v1 pin, `font_async_glyph` copies `owner->glyphs['?' - ' ']` into the newly queued slot before pumping the worker (`font/async.c`, lines 615–622). Pending therefore draws a literal question mark in v1. Keep the marker for settled refusals only in v2. Retry PRESSURE in stable screen order after capacity, eligible demand or publication changes. An unchanged refusal creates no wake.

**F5/F6.** Use a foreground-enabled work pool. Dispatch one key per WORK_FOREGROUND job, using resident, validated bytes, private shaping scratch and its reserved output tile. Check `work_should_stop` before and after that key. `font_compose` completes atomically. Selection, `fc-match` via the bounded, cancellable, reaped helper, reads and validation run on WORK_BULK. When loaded faces cannot settle coverage, return NEED_FACE to the service, park only that key, obtain a bounded system answer on BULK, and retry on FOREGROUND with the new immutable face set or settle MISSING after exhaustion. Other resident keys can run while discovery waits. Publish new faces only when readers can take a stable snapshot. A bounded discovery answer is tried at most once per queried scalar in a request.

On a full mailbox retain the completed result, call `work_continue` and return. Retry only publication after drain. Match pool lifetime, handle slot/epoch, request, generation and entry incarnation before adoption. Keep the key, face snapshot, tile and argument until `work_handle_finished` and message consumption/invalidation. Cancel obsolete work once. Drain and check physical finish before sleep and teardown. Worker messages never carry a layout pointer.

Coalesce settlements at a UI boundary, advance the paint serial and redecorate all potentially affected users, including chrome and overscan. Layout can reuse certified geometry and hits. Reuse of ink across serials requires redecoration or proof that it remains current. Settlement cannot certify approximate geometry.

## R-C · eviction and publication

Lookup records the current paint serial and a monotonically issued touch order to break ties. If the index has an insertion way and the free list is empty, walk the live accepted publication once through the row adapter. Include reusable text, fixed chrome and scroll overscan. For each matching owner/generation and slot/version, refresh its paint stamp and mark it with this sweep’s unique ordinal. Inspect the S entries once and evict the oldest settled entry unmarked by this sweep (compare last paint and touch order). This distinguishes a speculative lookup earlier in the same paint from actual screen use. Exclude all queued, private, delivered-but-unretired and retiring work. If every slot is on screen or otherwise ineligible, return PRESSURE and the settled marker. Static marker and empty cells name no runtime tile. No sweep runs on a hit or free-slot admission.

Eviction invalidates the old identity immediately, removes or supersedes its delivery obligation, advances the paint serial and schedules redecoration. Lookup stamps the admitted replacement with the resulting serial. Never recycle a slot version within a generation. A builder spanning MORE keeps only cell values. Its original paint stamp remains on its candidate: submit rejects a superseded stamp before using even inherited rows and validates changed cell identities. Redecorate against current entries before retrying. A backend already displaying an earlier accepted packet owns its own bytes. After replacement it draws background wherever expected and installed versions differ.

## F1 · submit and retirement

The UI notifies render of each slot settlement, reassignment or removal. Render keeps the epoch-qualified delivery FIFO and accepted watermark. One non-reentrant `render_submit` checks BUSY, binding/paint/base, canonical dirty runs, changed cell pairs, overscan and region, then copies selected whole tiles into prepaid packet storage. For a current INK slot validate dimensions, stride, last addressed byte and tight destination offset with checked arithmetic. Copy exactly w bytes for each of h rows. Sheet slots are installed at bind. Validate unchanged rows through accepted ancestry and current paint stamp. Invalid producer input or refusal spends no ID, delivery credit or damage.

The backend installs deltas before drawing and checks slot/version on every sample. A packet with tiles scans retained cells once to damage all affected rows. Presentation and device retirement own backend resources only. After loss quiesce the old epoch and resend current INK in bounded batches with a full new base. Font jobs and call-scoped UI reads have their own physical lifetimes. Neither T5 nor presentation feedback enters font_async.

## Scale and capacity transaction

A coherent `PLAT_EV_SURFACE` names physical client dimensions and DPI/scale, including scale-only changes. Retain the configured point size, derive target em pixels from points × DPI / 72 (P, points per inch) with platform rounding; preserve legacy height sizing for its exact bakes. Use the newest serial, including font selection/chain changes. Keep at most an active generation and one preparing/retiring generation (G, active plus replacement); while both are occupied coalesce newer requests to copied settings and cancel obsolete preparation. Old worker replies cannot activate a generation even if dimensions later recur.

Model each generation bank with FREE → PREPARING → READY → ACTIVE → RETIRING → FREE. Bootstrap enters ACTIVE at open. A superseded PREPARING or READY bank goes directly to RETIRING. Service OPEN → CLOSING → CLOSED stops new requests before reclamation.

Prepare the target ASCII strip, baseline and tile bounds off the input path. Before publishing, intersect raster and baseline GL frame limits with layout and font storage for the target physical surface. Reserve for the whole grid plus overscan, R8/descriptor maxima, texture dimensions, physical drawable, region and the simultaneous old-frame obligations. If a geometry does not fit, report CAPACITY (including required sizes), retain the last admitted generation and request off-path resize/reprovision or leave it visibly pending. Never submit old cells against the new surface serial. On backend idle and admitted geometry, build a full candidate for the new surface and swap binding and layout publication at successful submission. Apply the layout proposal's byte-anchor and pixel-Q8 remainder rules. Changed cell metrics reflow; new art with identical geometry changes paint only. A new surface serial still requires coherent geometry/hit validation and full affected surface damage. Preserve the last actual display serial on refusal. Old generations retire only after UI consumers detach, and private jobs and mailbox leases drain. The backend owns every accepted byte.

## Work and storage bounds

Let V include all cells visited in the live-publication sweep, counting reusable text, fixed chrome and overscan. S is the admitted runtime-entry cap, W,H the physical cell dimensions, T=2WH bytes per maximum R8 tile, A the printable sheet count. P=4 bounds probes. Reserve ceil(2S/P) buckets so the free-entry and pressure paths can both operate. S includes all states.

**F4.** Adopt K=max(4 + 30×4, 35)=**124 UTF-8 bytes**. One starter and the stream-safe limit of 30 consecutive non-starters at four bytes each covers the base-plus-marks drawing case. UAX #15 counts non-starters after NFKD. This is an artwork admission policy: extended graphemes can contain multiple starters and can exceed K even in stream-safe text. Keep original bytes as the key. The repository pins Unicode 15.1.0 property tables. Its emoji-data.txt contains scalar properties. Counting encoded bytes of the first field of all 1,468 RGI records in the matching Emoji 15.1 ZWJ sequence file gives a maximum of 35 bytes (M, static inventory), e.g. 1F468 1F3FB 200D 2764 FE0F 200D 1F48B 200D 1F468 1F3FB. Recheck the sequence inventory on a Unicode upgrade. If length exceeds K, return LIMITED before reading, hashing, copying or enqueuing any bytes. Width and hits still span the complete cluster. [UAX #15 §13](https://www.unicode.org/reports/tr15/#Stream_Safe_Text_Format) · [Emoji 15.1 sequence data](https://www.unicode.org/Public/emoji/15.1/emoji-zwj-sequences.txt)

| Where | Worst case and derivation |
|---|---|
| Lookup (P) | Printable singleton ASCII and overlength refusal take O(1). At most K bytes hashed and copied, P probes and P·K compared bytes, including a full-hash collision. Ordinary time O(K+P·K). No free entry adds at most one V-cell sweep and one S-entry victim selection. Worst O(K+P·K+V+S) per pressure miss. Index exhaustion alone returns PRESSURE after P probes. |
| Decoration (P) | For L eligible lookups and q free-list exhaustions charge O(L·(K+P·K)+q·(V+S)). Count actual calls across layout MORE. One settled batch requests one redecoration, preserving hits and widths. |
| One submit (P) | With J canonical runs, Z changed cells, U copied RGB elements, N selected deltas and Lᵦ tight R8 bytes, UI work O(1+J+Z+U+N+Lᵦ), plus row-bitset enumeration if used. Payload 20Z+4U+Lᵦ plus O(J+N) records, Lᵦ ≤ min(B,N·T) for packet byte cap B. BUSY is O(1). A resident ASCII edit with no owed tile copies zero R8 bytes. With N>0 the backend adds one O(V) scan. |
| UI service (G) | `font_async_step` examines at most its granted number of request/retirement heads, dispatches or records of completed jobs. Each uses indexed tokens and a prepaid message. The separate lookup pressure sweep and single render_submit have the bounds above. |
| Atomic key (P/G) | For at most K input scalars and F loaded faces, charge bounded cmap probes per face and validated GSUB visits G. Cap drawable outlines D, expanded/flattened edges E per outline (including components/CFF and subdivision), and scratch X. Bound sort conservatively by O(E²), scan/coverage by O((1+E)·T) per outline including blending. With bounded cmap cost C the total is O(KFC+G+D·(E²+(1+E)T)+T). Include composite/CFF expansion visits (also empty components) in G. Reject excess expansion, scratch or unbounded scaled bitmap extents before entering the donor primitive. Rasterize only the clipped, admitted tile. Cap and measure worst admitted inputs including maximum tile and outline complexity. Boundary polls require ≤5 ms CPU between them (work.h). Elapsed scheduling time is separate. If measurements exceed that interval, reduce admission caps or reopen the atomic design before shipping. Bulk preparation charges aggregate loaded bytes M and bounded validator visits separately. |
| Storage (P) | Per bank S·(K+T+O(1)) for exact keys, fixed tiles, stamps, work records and O(S) hash ways, plus AWH sheet bytes, O(A) metrics, O(T) markers, M loaded bytes, O(F) face records and bounded validation/composition scratch X per concurrent job. Write directly into unpublished slots. Two banks bound overlap. Count embedded bytes/bakes once. Separately render reserves backend ST bytes, installation/version and delivery metadata O(S), O(V) retained cells, and one packet for 20V+4Umax+B bytes plus capped run/tile records. Charge sheet, surface, alignment and rebind overlap per render admission. |
| Law 2 gate (G) | Prepay `base_arena` regions and fixed `base_pool` blocks, including tile backing, marks, result staging, job arguments and submit scratch, during setup or off-path reprovision; pre-touch UI-hot storage. No arena growth on a key. Run the release guard from ingest of prepaid records through successful submit with a cold key, all slots on screen, delayed publication and pending X packets. Under proposed A31, require an active guard and zero counted malloc/calloc/realloc on the UI thread. Record and report other threads’ counts for that window. The design permits no worker job that a key’s frame must wait for before submit. Any such job introduced later joins the enforced set. G1 requires its own measurement. |

Record the actual slot count, validator, geometry, byte and grant caps with their refusal counts. Maximum observed CPU time per admitted key and full-frame submit timings are implementation evidence.

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
#define FONT_KEY_MAX_BYTES 124u
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
    font_bytes marker_r8[2]; /* tight span × cell width × height */
} font_sheet;
typedef enum font_sizing { FONT_PIXEL_HEIGHT, FONT_EM_PIXELS } font_sizing;
typedef enum font_presentation { FONT_TEXT, FONT_EMOJI } font_presentation;
typedef enum font_rc {
    FONT_OK, FONT_NEED_FACE, FONT_EMPTY, FONT_MISSING, FONT_INVALID,
    FONT_LIMIT, FONT_CAPACITY, FONT_CANCELLED
} font_rc;
typedef struct font_request {
    font_bytes family, file, fallback_chain;
    uint32_t px;
    font_sizing sizing;
} font_request;
typedef struct font_limits {
    size_t faces, face_bytes, scratch_bytes;
    size_t max_outlines, max_edges, max_shape_visits;
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
font_rc font_resolve(base_arena *bank, const font_family *loaded, font_key key,
    work_ctx *bulk, const font_family **extended);
font_rc font_compose(base_arena *scratch, const font_family *loaded, font_key key,
    uint8_t *tile, size_t stride, size_t extent);
#endif
```

`font_bootstrap` returns a static sheet only for its baked height. `font_prepare` on BULK validates a complete selected family and sizes its ASCII sheet. A failed custom selection can return an embedded family with `notice.primary`. `font_resolve` obtains eligible system faces through fc-match into exclusive bank storage on BULK. On success return an immutable extended family snapshot sharing the existing face bytes, indices and metrics. Publish it on UI after retirement. Use an append-only array of immutable validated face records with a snapshot count. Serialize additions on BULK. Record exhausted queries for the request. A change to the priority or eligibility of already settled artwork requires a fresh generation. `font_compose` uses only the supplied loaded snapshot, exclusive scratch and tile. It releases its scratch mark on every exit. OK denotes initialized ink at the key’s fixed span, EMPTY denotes no coverage, NEED_FACE requests outstanding coverage discovery. The service converts exhausted coverage to MISSING. LIMIT/CAPACITY report explicit refusals. File/helper resources close on all exits.

### font_async.h

```c
#ifndef FONT_ASYNC_H
#define FONT_ASYNC_H
#include <stdbool.h>
#include "font/font.h"
#include "render/render_types.h"

typedef struct font_async font_async;
typedef struct font_async_version { uint64_t owner, generation; } font_async_version;
typedef struct font_async_sources {
    void *ctx;
    size_t (*live_rows)(void *, font_async_version);
    const render_cell *(*live_row)(void *, font_async_version, size_t row, size_t *count);
    void (*changed)(void *, font_async_version, uint32_t slot);
} font_async_sources;
typedef struct font_async_caps {
    font_limits font;
    size_t slots, live_cells;
} font_async_caps;
typedef struct font_async_size {
    size_t bank_bytes, service_bytes, source_bytes;
} font_async_size;
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
    const font_async_caps *caps, font_async_sources sources,
    const font_sheet *bootstrap, font_async **out);
font_async_rc font_async_request(font_async *a, uint64_t surface_request,
    const font_request *request);
bool font_async_event(font_async *a, const work_msg *msg);
font_async_rc font_async_step(font_async *a, size_t credits, font_async_update *out);
uint64_t font_async_paint_serial(const font_async *a, font_async_version version);
font_async_rc font_async_lookup(font_async *a, font_async_version version,
    font_key key, render_cell *cell);
const font_sheet *font_async_sheet(const font_async *a, font_async_version version);
bool font_async_identity(const font_async *a, font_async_version version,
    uint32_t slot, render_image_id *out);
font_async_rc font_async_tile(const font_async *a, font_async_version version,
    render_image_id expected, render_tile_view *out);
void font_async_activate(font_async *a, font_async_version ready);
font_async_rc font_async_close(font_async *a);
#endif
```

`measure` checks products and sizes both banks and the service at admitted maxima. `open` binds a nonreused owner, the bootstrap and UI source callbacks. The adapter supplies bounded constant-time row spans for the currently live publication of the requested generation (zero rows if absent), and forwards slot changes to render. Spans are call-scoped. `request` copies bounded settings. `event` authenticates pool messages. `step` returns MORE for runnable work, WAIT for a wake, or OK with the bootstrap/current or newest ready generation, request serial, metrics, paint serial and selection result. It reports preparation failure against the requesting serial. `sheet` borrows the sized ASCII and marker binding through setup/copy or the end of the bank's UI use. `activate` is the infallible commit for the prechecked READY generation in the same non-reentrant UI step as successful full submit and layout publication. A refused submit retains the old active binding.

`paint_serial` gives the synchronously current stamp for submit validation (zero for an obsolete generation). `lookup` uses the service’s current paint serial for the named generation and changes only the cell’s ink pair. INVALID/STALE leave the cell untouched. `identity` returns true and the current sheet/runtime identity for exportable ink in O(1), or false for absent ink or an obsolete binding. The render adapter represents those outcomes in its own enum. `tile` lends matched, settled INK to the synchronous packet copy, valid only within that non-reentrant UI call. Neither routine changes recency. `close` cancels pending work, stops callbacks after detachment and returns WAIT until all physical leases and messages are retired. Release the owner and arena only on final OK.

## Witnesses for signature

These are implementation acceptance cases. The font row of the harvest's integration table is R5. Adjacent rows below identify how to observe the same lifetime and paint contract across modules.

| Witness | Failure or interleaving | Design step and assertion |
|---|---|---|
| **R5** · `editor_test:binary_open_contract`, `unicode_render_test`, v2 `tests/contract/cases/files.py` | Opening or reactivating the binary/PDF tab discovers inkless fallback coverage, including the pin's private-use U+F227 case. | Force the worker to return a valid all-zero bitmap, settle, switch away/back and reopen. Inspect the **producer's** cell before submit: zero identity/NO_SLOT, intact width and hits, zero rectangles emitted for it. Require successful frames on null, raster and GL; an unrelated malformed descriptor must still be refused. |
| A16, `font_gsub_test`, `unicode_render_test`, layout R11 `a7fo_click_contract` | Wrong face from segmentation class, or changing a marker to wide/blank art moves the click target. | Enumerate default-text pictographs bare/VS16; compare face/presentation, span and byte hits before/after settlement, including wrap, wide-right, overscan and synthetic chrome boundaries. |
| `font_test:test_font_bounds/test_cff_errors/test_ttc/test_bake_consistency/test_atlas_identity`, font/GSUB fuzz | Truncated tables reach stb, or a lookalike primary uses vendored pixels. | Exercise malformed extents, swapped TTC index and changed bytes/scale; require clean rollback and exact ASCII metrics/pixels for both true bakes. |
| `font_selection_test`, `font_system_test`, `font_size_test`, size fuzz | Missing family silently substitutes, fallback order changes, or late zoom wins. | File/index precedence, missing notice, ordered coverage and missing service; delay the old result across scale-only changes, enforce newest serial and check one-byte-short packed/surface caps leave old binding intact. |
| `work_foreground_test`, occupied-bulk R6, live cold-key guard | Discovery holds the bulk worker while resident ink is requested. | Submit the cold cell background-only, run resident composition on FOREGROUND and navigate. Defer only keys needing discovery. Saturate the mailbox, release space and verify single adoption, unchanged hits and zero guarded allocations. Measure worst admitted CPU per key. |
| Bounded lookup and pressure | Colliding hashes, exact-limit and overlength input, reused rows across MORE. | Fill one bucket with distinct equal-hash keys, check exact-byte hits and PRESSURE at P. Accept 124 bytes, refuse 125 before copying, and keep original hits. Touch a speculative entry, force pressure with an overscan-only live user, and count one sweep. Check victim order, new paint serial and stale submit. With every slot on screen, require stable markers and no eviction loop. |
| `render_test`, `gl_scroll_test`, `raster_scroll_test`, `pointer_overscan_contract` | Mutable metadata alias, padding bleed, or eviction of the only glyph in the Q8 overscan row. | Pad source rows with sentinel values, overwrite caller descriptors/regions after accept, pressure all slots, compare backend readback across the scroll seam; the overscan image stays byte-identical through T5. |
| `editor_test:dzk1_render_reject`, `wayland_gl_test`, slot reuse and loss | BUSY, stale candidate across MORE, duplicate T5 or late event after restart. | Evict between slices, reject the stale paint and redecorate. Refuse after copying, verify no ID/credit spent, retry. Overwrite the source after accept, deliver discard/fence in either order and verify original packet ink and next-frame progress. |
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
| Proposed runtime owner and font-specific source adapter (E) | **650** = 130 fixed index/pressure sweep + 170 dispatch, settlement and lease retirement + 180 bank/request lifecycle + 100 lookup, tile/identity and notifications + 70 service declarations/admission. |
| Proposed total (E) | **1,950 + 650 + 650 = 3,250 code**. With 14,495 data lines the total is **17,745**, down 300 code lines from the first proposal. |

Retire `font_cache` and its append-only shelf/image cache in favor of fixed unpublished tiles and one runtime index; retire `font_runtime` and its parallel binding in favor of generation banks in the same owner. Replace the historical two-slot fallback with the bounded ordered face chain. Remove UI outline raster, polling sleeps, `font_async_frame(active)`, `font_async_repair`, render-grid repair and Windows discovery/DirectWrite from the Linux implementation. Keep the validator, bounded shaping, file/index semantics, baseline placement and reproducible data. A process-wide mutable glyph cache would couple independent surfaces and make stale worker messages ambiguous.

**Signature scope.** Approval freezes the two font interfaces, pending art, admission bounds, eviction, tile-delta source and two-bank transaction. Target timings, hardware selection, live display/allocator witnesses and final implementation line counts remain implementation work.

**Source record.** Wave 2 harvest and briefs, companion render errata, signed viewport/layout S7 and E5–E8, PLAN_V2 §§2 and 4.6, A16, landed `work/work.h` and `base/base.h`, Unicode 15.1.0, and v1 pin `6fec2e8` (`font/{font,async,selection,discovery_posix,gsub}.c`, `font/{identity,cff_guard}.h`, `editor/font_size.c`).

**Advisor signature** ____________________  **Date** ____________________
