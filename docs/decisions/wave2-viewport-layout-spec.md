# Wave 2 viewport and layout

**Decision proposed for advisor signature · Astra · 2026-11-25.** Implement against PLAN §2 and §4.5 with amendments A11, A16 and A19. The byte under the top of the view survives edits and reflows. A row, a label for that row and a picture on screen have different lifetimes.

## Invariants

| Structure | Invariant |
|---|---|
| `viewport_anchor` | Each tab persists one top byte, horizontal offset in 64-bit columns, vertical remainder in pixel Q8 and the caret's side of a soft seam. |
| `viewport_row_cert` | A certificate describes one **finished** row under the source, seed and geometry of its publication, and states independently whether its logical rank is known and its placement is approximate. |
| `viewport_intent` | A numbered request names a target and its affinity relative to the originating tab and input, and only that request may install its answer. |
| `layout` | One private emitter advances one frontier over an owned snapshot and writes into prepaid row slots in clip and wrap modes alike. |
| Follow continuation | A request owns its seed search, byte window, pending cluster/break state, completed-row ring and remaining work credits across every suspension. |
| Checkpoint | A checkpoint proves a cluster boundary and the incoming column for its source and tab policy, and proves a wrap boundary only when it also contains a matching certified row seed. |
| Published view | An owner and serial identify one immutable ordering of finished row slots, with the cells, hits, row descriptors, labels and pixel transform used for that picture. |
| Window job | An immutable request owns its snapshot and output slab until physical worker retirement, and only its matching completion can supply bytes to its consumer. |

## Row contract and the layer boundary

L4 viewport owns byte placement, affinity, relative scrolling and the decision to move the caret or extend a selection. It persists no tree pointer. L2 edit owns text mutations and the authoritative selection for an edit. L5 passes value inputs, sends layout a viewport intent, then gives viewport the resulting value answer in the same UI step that accepts the picture. L4 layout includes viewport. Viewport includes only standard types. L5 paint and pointer input consume the same published rows. Minimap gets piece's line answers directly. This removes v1's logical, visual, pending and display top mirrors and the `view.c → layout` call. A pending target is a request operand and an old submitted picture is immutable evidence of what was drawn. Neither is a writable top.

`byte` is the first cluster boundary, `end` the drawable byte boundary, and `next` the following row start. At a soft seam `end == next` and both rows own a caret at that byte, the preceding row with trailing affinity, the successor with leading affinity. LF/CRLF have `end` before the terminator and `next` after LF. A lone CR is text. The terminal row has `end == length`, `next == UINT64_MAX` (P, piece caps length at 2^60). A final LF creates its own empty successor at length, including on a short last page. Empty text has one terminal row. Screen padding has no certificate. Nonterminal rows always advance.

`line_start` is zero or the byte after an LF when known. Use the above sentinel for an unknown logical start. `col` and `end_column` count logical cells from that start in an exact row. An approximate local seed without a proven column starts its own relative column at zero and uses zero synthetic indent unless a compatible checkpoint proves the indent. `indent` is the inherited leading indentation in screen cells, synthetic on continuations, and advances no source offset. `newline` means LF consumed, `continuation` means soft continuation (assumed on a local interior seed). `ordinal` counts visual rows **from the first row of this publication**, starting at zero. A caller compares ordinals only in that publication. Store it in the row directory so moving a shared payload costs a descriptor update. Absolute visual rank has no consumer here. `exact_line` is zero-based, readable only under `has_exact_line`. `approximate` marks local break/column/indent provenance. A decoration-only fallback glyph does not change this geometry flag. A hard reset at a witnessed LF restores exact placement on the next row even when rank is unknown.

**Disposition of `last`.** In v1 `layout/wrap.c:end_row` writes the last cluster position and `shift_bytes` rebases it. Neither row type has a semantic reader of that member in the pinned source. `next_seed` uses `next/end_column/indent/newline`; trailing-caret drawing in `end_row` uses the seam byte and the wide-right cell attribute. Omit `last` from both certificate and wire format. Emit cluster byte limits into hits as the cluster is consumed. Clipping, hidden trailing blanks and zero-width clusters make “end minus one” and “last visible hit” invalid recipes for a last source cluster. A pointer uses a visible hit, and End uses the certified end and affinity.

**Geometry.** The width key covers wrap mode, physical cell metrics, grid dimensions, gutter and reserved sidebar, tab stops, continuation-indent and break/width policies, plus the clip-mode horizontal origin for cell/hit reuse. Compute text columns after chrome and gutter. A gutter uses decimal digits plus the v1 two inset and two gap cells, clamped to the available width (P, `layout.c:layout_gutter_columns`). If the gutter occupies the grid, publish chrome/padding only and keep the byte anchor. A wrap toggle clears horizontal scroll and preferred column. Preserve v1's basename defaults for extensionless names, .md, .txt and .tex. A pure wheel moves only the viewport. Page/motion keys carry a selection operation separately. Relative up/down use the original visual preferred column, preserving end affinity across a soft seam. Clamp the top against a known EOF from local predecessors, including short documents. An estimate never clamps a byte request.

Define row height in Q8 as pixel height times 256 (P, eight fractional bits). Draw row i at text-origin + i × height − remainder, with fixed chrome outside the text clip. Invert only inside that clip with floor((local_y + remainder)/height), including bottom overscan. On zoom divide the **old pixel remainder** by the new height, advance the quotient in the new geometry, and keep the modulus. Resolve the old top byte with leading affinity. Caret affinity is separate. Saturate at document/grid ends, use checked widened arithmetic, and round damage outward after clipping.

## Q10 · current geometry and late rank

Authenticate by buffer incarnation, committed text serial, layout geometry serial, and publication owner/serial. Internally each row also has an incoming seed proof and the *entire* interval of source dependencies, including the decoder's left context, first right-context scalar, CR/LF partner, word-break lookahead past a rewind, EOF test and leading indentation. Record inspected gaps as well as bytes. An insertion on either dependency boundary is a touch. Local approximate rows retain the same local seed identity through reuse. Changing seed provenance requires emission or a proven hard reset. Comparing start/end alone fails on a space inserted just beyond a wrapped word.

L5 issues a new non-reused text serial on committed edit, undo, reload or binding replacement. Take a snapshot after commit, retain for work and never issue jobs on provisional state that could abort. A snapshot pointer, generation of a file mapping, or edit delta alone is insufficient. On a replace, map the top byte with before gravity. A point before the interval stays put, a point beyond it (including the old right edge of a nonempty deletion) shifts by inserted minus removed, and a point consumed by the edit or at a pure insertion takes the chosen left/right edge. Cursor and selection use the edit command's independently specified gravities. Resolve a transformed byte inside a newly joined cluster to the corresponding legal edge. Transforming a byte grants no certificate. At a soft seam invalidate both adjacent dependency records, repair from the preceding proven seed and apply visual affinity again. Keep the transformed top as the near-follow origin for Return. A crossed anchor gets the replacement boundary as its intent, never an implicit jump to zero or EOF.

Use A19's `piece_lines_before(piece_snapshot_view(s), top.byte, &count)` at most once for the first visible row in a frame. On false, count is a lower bound. Display an explicitly approximate label from that lower bound (or suppress the digits during a pending read). Advance other labels by observed LF, never by soft wraps. For an explicit Go To Line, `piece_lines_start` on true gives the exact start or EOF. On false its blocking offset is a search limit, never the requested line. Park that *line* request on new count evidence. Byte, page and caret navigation proceed independently. Treat failed backing validation as conflict before using either answer.

An adoption or resolve may change line knowledge with identical text. Bind the new rank result to a newly obtained snapshot of the same committed text, top byte and rank-request serial. Older owned snapshots keep their old answers. Reject stale deliveries even when their numerical answer fits. If gutter width stays fixed, update only labels/gutter paint and publish a new picture serial with existing text/cell geometry. If width changes, advance geometry serial, retain the byte anchor and reflow. Relabelling cannot promote approximate wrap phase, absolute column or unknown line start. Freeze the chosen gutter width for an emission; consume later rank evidence at the next frame boundary. A font/paint change with identical metrics invalidates affected paint, while a metric change requires reflow.

## Q11 · one follow request across slices

1. L5 captures the committed caret byte, affinity, transformed top, bounds and request id once. Quantize the configured follow margin to fully visible rows, capped at floor((visible_rows − 1)/2) as in the pin. On a current visible hit retain the top if the caret is fully visible, else scroll the minimum number of rows that reveals it. For a near edit choose the transformed top as the initial seed when its incoming state can be proved and it is inside the target neighborhood. Decide this from retained dependency records before emitting any row. A miss goes directly to the target neighborhood. Moving to document end after Select All is a byte seek; it never asks for the last logical or visual rank.
2. Probe the local ring once. On a miss choose **one finite neighborhood around the target**. Let R include the visible rows plus one overscan slot. Let B be the configured normal work quantum, admitted at setup to cover at least one screen row of cells. The predecessor search extends at most R × B bytes before the target, the successor search at most R × B after it (G, one quantum of source allowance per row per side). Truncate at BOF/EOF. Seek directly to those byte coordinates. Prefer the nearest preceding hard-line start that leaves enough preceding rows for minimal reveal, or a same-key cached exact seed within these limits. For many short lines trim the left window at the R-th preceding LF when available. Walk the search cursor once, retaining it on a cache miss. Reserve boundary lookaround inside these limits, or use actual BOF/EOF.
3. With no usable hard start/row seed, choose an independently proved grapheme break in that window before the target, preferably near its left edge. For example two adjacent printable ASCII scalars prove a break between them. A known column checkpoint can supply an exact column at a break. Mark a synthetic first wrap there approximate, suppress an unproved line-start gutter hit, and carry its local phase forward. Never treat an arbitrary continuation byte, a combining mark, RI parity, or an index stride as proof. A global wrap phase can depend on a tab or space outside the window. This explicit local answer is the price of a document-independent cold jump.
4. Emit forward once from the chosen seed. Retain the window cursor, partial scalar, `utf8_cseg`, pending CR, word break and right lookahead across slice boundaries. Carry text scanned past a soft break into the next row as staged cluster summaries, or charge each replay inside the finite work cap below. Keep the last R **completed row slots**, including cells and hits. Once the target's owning row is known, choose the minimal reveal from that ring, backfill at EOF from it, and fill forward up to the clip. Install the slots directly. No emitter runs merely to ask for a predecessor. Counters include exploratory completions, misses, overscan and the final paint.
5. On genuinely cold text, queue one bounded read on an independently serviceable, I/O-capable navigation reader. Use prepaid validated resident windows inline. A worker releases results by mailbox and the UI waits for readiness, never spins or waits for the general bulk queue. A new wheel, target, edit, tab, or reflow cancels the old intent. Hold a cancelled job's slab/snapshot until `work_handle_finished`; a stale completion cannot install an anchor or a view. Coalesce the newest unstarted target while the old read retires. A rank-only notification leaves a compatible walk intact.
6. A pathological window can contain only the middle of a single unbounded grapheme, no LF, or an unending clipped tail. If the frontier reaches the finite neighborhood edge without the evidence to complete the target row, return **LIMIT** with the target byte preserved, retain the previous picture, and expose pending location to L5. No periodic restart. Retry only on changed evidence (e.g. an independently advanced exact checkpoint) or new input. MORE is reserved for an advancing continuation or an outstanding job with a wake. A caller can show a location-pending cue, but cannot derive an editable hit from missing evidence. Exact segmentation for arbitrary such input and a fixed whole-intent work cap cannot both be promised: two texts identical throughout the inspected window may differ in preceding RI parity. Tests distinguish LIMIT from success and from a hung MORE.

Warm and cold paths use the same row emitter. Searching from BOF is exact but the number of rows before the target is unrestricted. A per-tick predecessor quota still repeats all completed rows if each tick starts a new emitter. A full visual index costs a new index per wrap geometry and cannot seed a cold open before construction.

## Q12 · one emitter and a provable splice

Walk one cluster stream. Clip (`wrap_cols == 0`) filters logical columns by 64-bit hscroll and ends rows on LF/EOF. In wrap mode positive width includes synthetic continuation indent within the text area. Advance a tab to its next logical stop; retain the last legal break after whitespace, swallow trailing wrap whitespace as in the pin and wrap a long word at a cluster boundary. Derive continuation indent from leading SP/TAB cells capped at half the text width, zero for widths at most two (P, the pin's `wrap.c:start_row/ascii_span` rule). A cluster wider than an empty row consumes its full byte span and uses the pin's clipped fallback. A zero-width cluster consumes bytes. Invalid UTF-8 consumes one invalid byte. Require a real EOF flag at the document end, including across split CRLF and split UTF-8 tails. Use `utf8_cluster_step` incrementally. Accumulate bounded glyph scratch; an oversized cluster gets the fallback art and its actual streamed width/byte limits. A16 text/emoji presentation remains the glyph lookup contract. Printable ASCII batching must leave the last scalar for segmentation when the following scalar may join it.

Emit a paint cell and its hit in one slot-write operation. Each visible part of a wide cluster or tab keeps the whole cluster interval and its position within the unclipped span for left/right-half decisions. Hidden whitespace still advances the logical column. Synthetic indent hits the row start, gutter hits its proven logical start, trailing blanks hit the end with the appropriate affinity. A gutter of unknown origin has no actionable line hit. Selection, style, cursor, ASCII, fallback and overscan all use this mapping. L1 scan works on validated, bounded resident bytes. For typing indentation pass a proven `line_start` to L1 indent, or the caret byte for its documented empty-leading-run fallback.

On edit keep the old publication immutable. Re-certify the unchanged prefix from its previous proof. Start at the earliest dependency touched, including lookahead of a preceding row. At a logical LF whose transformed successor is an unchanged hard start, full decoder/break state resets. Splice the old suffix **only** with equal geometry and proved identical incoming seed and untouched remaining dependencies under the single replacement. Equivalence includes source/column/indent, continuation and local seed provenance; ranks are derived separately. Conservatively use the first such hard start; otherwise emit through the visible bottom. Do not plan the whole offscreen logical line. Shift payloads by references to slots and checked byte-offset deltas in directory entries, covering every hit and certificate field. Consume absolute positions through the directory translation. Rerender affected decoration/gutter cells if their inputs changed. Fill newly exposed bottom rows. When edits overlap unfinished work, discard private progress; skip reuse unless the complete edit chain from the published source is known. Gutter or wrap change starts a fresh generation. At a stable geometry, cursor/selection/style-only relayout uses existing cluster intervals and dirties affected slots without rerunning the breaker. Localized repair and bounded re-emission suffice; comparison of successor byte alone loses CRLF, combining and offscreen word context.

## Q13 · publication, hits and source ownership

Publish a **completed viewport** by swapping a row directory after all real rows and EOF padding are settled and final backing validation succeeds. Stage changed slots only, share unchanged payloads and translate byte fields in their directory. Metadata and labels can change without copying a cell grid. Two directories and at most two surfaces' worth of slots suffice (P, old displayed rows plus new rows under construction). The scratch ring recycles dropped, unpublished rows. The candidate top and picture swap together in L5. A prefix-only alternative needs another frontier-aware input policy and can show a new top above an old caret. It buys nothing for the Return frame witness.

During MORE the compositor can retain the last submitted picture. Its frame id authenticates what the pointer saw. Accept a pointer only for the still-displayed generation, matching current buffer/text/geometry and in-range row/column. During an edit/reflow gap reject an incompatible displayed-generation hit as stale and leave selection unchanged; never return an old byte as current. A borrowed row lasts through the current non-reentrant UI read/submit-preparation step. No raw pointer survives the next mutating layout call. L3 submit prep must own/capture everything it needs beyond return. A rejected submit keeps the previous displayed serial; L5 retries the candidate or explicitly discards it before reuse. Staging/borrowed/submitted are separate events. Deliver the frame carrying a local key only after its current caret and top are ready. A retained fallback cannot be counted as that key's frame.

Obtain source coordinates via `piece_iter_init` on the snapshot view and `piece_iter_next_span` with a bounded end and NULL newline output. A canonical span can cross many descriptors, so never pass document end to describe one window. Copy slices with `piece_snapshot_read_span`, retaining skip and respecting the returned count and chunk boundary. Discard attempted destination bytes on CONFLICT, including earlier dependent staging; do not reinterpret RANGE as EOF. For mapped ORIGINAL supply the live identity validator, then close the enclosing operation with another identity check and generation-matching zero-epoch fault poll after the final dependent load and before publication. Use a trusted, allocation-free binding check; perform blocking identity work on the reader and invalidate its token on owner conflict. A rank job that may scan a mapped descriptor takes the same cold path. Checkpoint delivery authenticates text, backing, tab policy, geometry for wrap seeds, requested interval and request id. Keep a bounded ring of at most R long-line checkpoints and R local prior row starts. Advance a saved checkpoint cursor from a proved break in slices; replace old entries rather than allocating an index proportional to the longest line. Losing a checkpoint loses an optimization.

Scratch, directories, row slots, hit intervals, checkpoint slots and worker arguments are admitted/touched from base storage before input. Refused capacity preserves the prior view. Law 2 gate is the active release allocator guard through submit with zero allocations (G, CLAUDE Law 2).

## Proposed public headers

These declarations are the signature surface. Types in the proposed L3 render-types header follow PLAN §2. All serials are issued by their owner, increase on replacement, and cannot wrap into a live identity. On exhaustion retire the owner. A newly created layout has no displayed generation. UI ownership is single-threaded.

**viewport/viewport.h**

```c
#ifndef VIEWPORT_VIEWPORT_H
#define VIEWPORT_VIEWPORT_H
#include <stdbool.h>
#include <stdint.h>

#define VIEWPORT_UNKNOWN UINT64_MAX
typedef struct viewport_anchor {
    uint64_t byte, hscroll_cols;
    uint32_t subrow_q8;
    bool visual_end;                 /* caret affinity, top always leading */
} viewport_anchor;
typedef struct viewport_row_cert {
    uint64_t byte, end, next, line_start, col, end_column, indent;
    uint64_t ordinal, exact_line;
    bool has_exact_line, newline, continuation, approximate;
} viewport_row_cert;
typedef struct viewport_key {
    uint64_t buffer, text, geometry;
} viewport_key;
typedef struct viewport_selection {
    uint64_t caret, mark, preferred_col;   /* UNKNOWN clears preference */
} viewport_selection;
typedef struct viewport_state {
    viewport_anchor top;
    bool wrap;
} viewport_state;
typedef struct viewport_change {
    uint64_t at, removed, inserted;
} viewport_change;
typedef enum viewport_gravity {
    VIEWPORT_BEFORE, VIEWPORT_AFTER
} viewport_gravity;
typedef enum viewport_action {
    VIEWPORT_RESTORE, VIEWPORT_REFLOW, VIEWPORT_FOLLOW, VIEWPORT_SEEK_BYTE,
    VIEWPORT_SEEK_LINE, VIEWPORT_SCROLL_Q8, VIEWPORT_LEFT, VIEWPORT_RIGHT,
    VIEWPORT_WORD_LEFT, VIEWPORT_WORD_RIGHT, VIEWPORT_UP, VIEWPORT_DOWN,
    VIEWPORT_PAGE_UP, VIEWPORT_PAGE_DOWN, VIEWPORT_HOME, VIEWPORT_END,
    VIEWPORT_DOC_HOME, VIEWPORT_DOC_END, VIEWPORT_SELECT_ALL,
    VIEWPORT_SELECT_WORD, VIEWPORT_SELECT_LINE
} viewport_action;
typedef struct viewport_intent {
    viewport_key key;
    uint64_t serial, target;          /* absolute byte or logical line */
    viewport_action action;
    viewport_state origin;
    viewport_selection selection;
    int64_t pixels_q8;               /* signed delta for SCROLL_Q8 */
    bool extend;                     /* Shift selection */
} viewport_intent;
typedef struct viewport_answer {
    viewport_key key;
    uint64_t intent_serial;
    viewport_anchor top;
    viewport_selection selection;
} viewport_answer;
typedef struct viewport_frame {
    uint32_t width_px, height_px, row_px, layout_rows;
    int64_t origin_y_q8;             /* text-clip local */
} viewport_frame;

bool viewport_wrap_default(const char *path);
uint64_t viewport_rebase(uint64_t byte, viewport_change edit,
                         viewport_gravity gravity);
viewport_intent viewport_request(viewport_state origin, viewport_key key,
                                 uint64_t serial, viewport_selection sel,
                                 viewport_action action, uint64_t target,
                                 int64_t pixels_q8, bool extend);
bool viewport_accept(viewport_state *state, viewport_selection *out,
                     const viewport_intent *current, const viewport_answer *answer);
bool viewport_plan(viewport_anchor top, uint32_t width_px, uint32_t height_px,
                   uint32_t row_px, viewport_frame *out);
bool viewport_pixel_row(const viewport_frame *frame, int64_t y_q8,
                        uint32_t *row, uint32_t *within_row_q8);
#endif
```

The caller restores a saved pointer-free state by issuing RESTORE with byte values clamped to the current length. It passes wrap and tab changes via REFLOW. For wheel input L5 converts platform wheel units to a signed pixel delta once (the pin's wheel notch is three row heights, G, `scroll/scroll.c:scroll_wheel`). `viewport_accept` requires matching key/intent and commits top plus selection together, returning false with outputs untouched for a superseded answer. Under RESTORE and pure scrolling preserve selection; non-extending motions collapse/reset mark per the v1 motion, Shift keeps the original mark, Select All sets mark at zero and caret at length. L5 applies the returned selection to L2 before the next ordered input. A motion may consume budgeted text work to *find* a word boundary; its subsequent *follow* is one distinct, counted operation. `viewport_plan` validates nonzero row height, widened multiplication and capacities; an empty surface has no hittable rows.

**layout/layout.h**

```c
#ifndef LAYOUT_LAYOUT_H
#define LAYOUT_LAYOUT_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "base/base.h"
#include "piece/piece.h"
#include "render/render_types.h"
#include "viewport/viewport.h"
#include "work/work.h"

typedef struct layout layout;
typedef enum layout_result {
    LAYOUT_READY, LAYOUT_MORE, LAYOUT_LIMIT, LAYOUT_STALE,
    LAYOUT_CONFLICT, LAYOUT_CAPACITY
} layout_result;
typedef enum layout_update {
    LAYOUT_BIND, LAYOUT_EDIT, LAYOUT_REFLOW, LAYOUT_DECORATE, LAYOUT_RELABEL
} layout_update;
typedef struct layout_limits {
    uint32_t rows, cols;              /* prepaid, rows includes overscan */
    size_t quantum;                   /* B below */
} layout_limits;
typedef struct layout_geometry {
    uint32_t cols, wrap_cols, gutter_cols, sidebar_cols, tab_cols;
    uint32_t cell_px, row_px, width_px, height_px, margin_rows;
} layout_geometry;
typedef struct layout_source {
    piece_snapshot *snapshot;
    piece_validate_fn validate;
    void *ctx;
    uint64_t rank_serial;
} layout_source;
typedef struct layout_paint {
    void *ctx;
    uint64_t serial;
    void (*style)(void *ctx, uint64_t byte, render_cell *cell);
    void (*glyph)(void *ctx, const uint8_t *cluster, size_t len,
                  uint32_t width, render_cell *cell);
} layout_paint;
typedef struct layout_input {
    viewport_intent intent;
    layout_source source;
    layout_geometry geometry;
    layout_paint paint;
    layout_update update;
    viewport_change change;           /* one replace from previous text */
} layout_input;
typedef struct layout_view {
    const layout *owner;
    viewport_key key;
    uint64_t serial;
    viewport_frame frame;
    uint32_t rows, cols;              /* real rows only, clip supplies padding */
} layout_view;
typedef struct layout_hit {
    uint64_t lo, hi;                  /* relative to row.byte for TEXT */
    uint32_t part, width;             /* cell offset within full cluster */
    enum { LAYOUT_TEXT, LAYOUT_INDENT, LAYOUT_GUTTER, LAYOUT_EOL } kind;
} layout_hit;
typedef struct layout_row {
    viewport_row_cert cert;           /* translated absolute bytes */
    const render_cell *cells;
    const layout_hit *hits;           /* one per allocated display column */
} layout_row;

layout_result layout_open(base_arena *arena, layout_limits limits,
                          work_pool *navigation_reader, layout **out);
layout_result layout_begin(layout *l, const layout_input *input);
layout_result layout_step(layout *l, size_t credit, viewport_answer *answer,
                          layout_view *candidate);
bool layout_row_at(const layout_view *view, uint32_t row, layout_row *out);
bool layout_hit_at(const layout_view *displayed, viewport_key current,
                   int64_t x_q8, int64_t y_q8, uint64_t *byte, bool *trailing);
bool layout_present(layout *l, const layout_view *accepted);
void layout_cancel(layout *l);
layout_result layout_close(layout *l);
#endif
```

Open checks arena products, rolls back its mark on refusal and prepays the maximum geometry. L5 grows capacity by replacing an instance outside input. `layout_begin` copies input values and retains the snapshot. Paint/validation contexts outlive their operations and physical jobs; callbacks have bounded cost, cannot re-enter and must supply a usable placeholder for missing glyphs. Paint serial covers palette, glyph availability and style dependencies; a moved row with position-dependent style needs redecoration. Geometry must agree with the intent's wrap state and key. `layout_step` polls a bounded mailbox or advances the FSM, producing answer and candidate only on READY. On a zero-credit call perform no work. L5 calls `layout_present` and `viewport_accept` as one non-reentrant commit after submit accepts, subject to the same still-current intent. `layout_row_at` checks owner, serial and extent before issuing a borrow. Padding and stale rows fail without changing outputs. `layout_hit_at` additionally requires the displayed serial and caller's current key and uses the saved pixel transform. Hit tests in a partially clipped cluster compare the absolute half-cell position with the midpoint of the full cluster. `layout_cancel` retires private work. `layout_close` returns MORE while a worker still owns resources; the caller keeps the instance/arena alive and drains on wake.

Use explicit, exhaustively switched enums internally, without a default branch. The request phases are Idle → SeekSeed → Emit → Place → Fill → Candidate → Idle on accepted submit. MORE keeps the exact phase and cursor; LIMIT/CONFLICT park the request until an explicit begin. A discarded candidate returns to Idle. Window ownership is a separate Free → InFlight → Available or Retiring → Free axis. Physical completion precedes reuse on every path. The emission substate distinguishes start, pending scalar/cluster, pending CR, staged break and sealing a row. A compatible RELABEL begin records the new rank source alongside the current walk for its next publication. Any begin requiring replacement supersedes through cancel; equality of text alone never resurrects a cancelled request.

## Counted work and the 10 GiB gate

Use counters in the acceptance lane. R is the prepaid visible row capacity including one extra row for a partial top (P, translating a clip shorter than or equal to the nominal row grid can expose one more row). B is the configured positive work quantum in byte/cell operations, chosen at admission at least as large as a physical display row in cells; tick grants may be smaller and never exceed B. This makes the width factor explicit: a grid of arbitrary width cannot be written at unit cost per row. Charge every inspected/copied byte including rereads, cell/hit write, dependency probe, row-slot probe, mailbox action and empty/EOF transition. Piece constants H = PIECE_MAX_HEIGHT + 1 path nodes and K = PIECE_CHUNK bytes come from the landed header. They are fixed for all admitted documents. These are operation and storage bounds, not wall-time predictions.

| Count | Derivation and acceptance |
|---|---|
| One UI or reader slice | At most B ordinary charged steps, allowing a fixed scalar lookahead of at most four bytes (P, UTF-8 scalar maximum) per decode step. A piece seek or bounded describe step costs at most O(H) per descriptor; cap end to remaining byte credit, hence at most B descriptors even with one byte per piece. One indivisible rank or select may additionally visit O(H) nodes and read ≤ K bytes (P, piece.h Lines). Issue the latter on the reader when residency is unknown. Thus O(HB + K) per slice (P); at most one first-row rank per frame (G, A19). Charge actual bytes separately so rank scanning cannot hide in the emitter counter. |
| Whole caret-follow, including cold misses | Admit at most R × B bytes to the left and R × B to the right of the target, clipped by file endpoints (G, one window quantum per potential row per side). Warm seeds must be inside that interval or directly prove the target with cached rows. A cache probe examines at most R starts and R checkpoints once. A monotone emission completes at most one nonterminal row per consumed source byte, plus one EOF row. Thus ≤ 2RB + 1 emitted rows for the **original intent**, including discarded rows (P); a warm visible page with a proven seed needs at most R new final rows (P, number of slots). Reaching the edge is LIMIT, never another seed-search pass. Stamp every started/finished row with the intent and count all slices, including failed searches. |
| Whole ordinary CPU work for that follow | Probe at most 2RB source bytes once, admit/copy each source window once and bound per-row planning by the same finite neighborhood even if a wrap rewind re-examines it. At most (2RB + 1) row attempts each inspect at most 2RB bytes and write at most B screen cells, plus one partial frontier. This gives the conservative O((RB + 1)²) upper bound (P), with O(H) fixed tree cost per charged byte. Cache speculative cluster summaries to avoid replay of oversized clusters. Rank/select is bounded separately as above; at most one outstanding byte-window job and no retry polling on an unchanged miss. Prefer the measured counters to this loose worst case. No row may be completed twice for the same intent. |
| Storage, publication and incremental repair | O(RB) prepaid cell/hit slots, staged cluster summaries and source neighborhood, O(R) directories/checkpoints/row starts (P, rows × cols with cols ≤ B, plus the two bounded source windows and old/new directories). Rebinding a generation copies zero bytes of the published grid (G, §4.5). A directory splice inspects at most R entries; repainting a whole visible surface writes at most RB cells (P). Slice the work including clearing slots. A paint-only change inspects at most that surface. A repair scanning beyond local evidence uses the same finite neighborhood/cap as a fresh placement. Zero hot allocations (G, Law 2). |

Limit the whole follow to one pending/answered rank on its first row; other frames with unchanged evidence reuse the label. Selection changes, EOF clamping, layout retries and paint do not manufacture fresh intent ids. In counters charge a missing row lookup as a probe even if it emitted zero rows. A no-progress worker wait is wake-driven. Count stale work up to cancellation and its bounded outstanding slice separately. Do not amortize it against a later successful request.

| Gate input | Bound and observable result |
|---|---|
| **10 GiB, one logical line** (G, PLAN Wave 2 size with a single-line adversary) | Cold Ctrl+End/Select All targets length directly. On ordinary printable ASCII, use the fixture of repeated printable non-space ASCII. Probe the last min(R × text_cols, RB) bytes first. After the first proved interior ASCII break, each full local row consumes text_cols bytes (synthetic seed indent zero) and at most one row ends short at EOF. Thus at most R completed rows in this fixture (P). Retain the target at EOF with explicitly local wrap phase and show the caret on the last real row. For general wrapped content, at most 2RB source positions and 2RB + 1 completed rows across all slices, same O(HB + K) slice cap (P, window proof above). A tab or combining sequence invalidates byte-as-column arithmetic. Include the all-one-cluster case and assert LIMIT rather than an invented hit if neither edge is provable within budget. |
| **10 GiB, many short lines** (G, same PLAN gate with dense LF) | Start near the target byte. For the fixture use non-wrapping short lines with their terminators fitting B bytes per line. Trim at the R-th preceding LF; at EOF this yields at most R completed rows including a trailing empty row (P, one row per line in the retained suffix). Local LF boundaries suffice for exact breaks and minimal EOF reveal even while the logical label is estimated. Each LF consumes at least one byte, so even all-empty lines satisfy 2RB + 1 rows and O(HB + K) work per slice (P). Count zero walks from byte zero on a deep jump or first PageUp. Hold the ordinary bulk worker through navigation, wrapped Return and typing; assert useful target text and the correct caret on the submitted key frame. Run both shapes under the live release malloc guard and the project's G1 lane. |

The independent cold-reader requirement is real: `piece_snapshot_read_span` validates bytes but can fault, and `WORK_FOREGROUND` is reserved for bounded CPU work. If the host has only the normal occupied bulk worker, give the active editor a preinitialized `work_pool_init(..., 1, 0)` reader shared across tabs, with that sole bulk worker reserved for interactive cold windows (P, one in-flight request). This uses the existing work API. A foreground mailbox alone cannot discharge a genuinely nonresident read. Gate the reader with a held ordinary bulk lane, a delayed navigation read, cancellation and a late stale delivery. Eventual progress assumes the independent read returns or reports conflict; no OS I/O latency bound follows from these counts.

## Integration witnesses to port

| Boundary and regression | Preventing step and observation |
|---|---|
| Viewport → layout → paint · R1 | Wrapped Return uses the rebased local top and repaired successor; `editor_test:n6090_typing_top` checks the top advances only by necessary rows on the key frame. |
| Viewport → layout → paint · R2 | Clip and wrap share the current-row publication fence; `vp4m_return_bottom` must see the new caret on the first frame carrying Return. |
| Piece → viewport · R6 | Independent navigation reader owns the outstanding window and wake; `editor_large_test:starved_follows` asserts movement and typing while the general bulk lane is still held. |
| Piece → viewport · R7 | Seek by EOF byte, derive top from the retained window and label once; `near_end_repro` and the external navigation case assert bottom-row caret, local reads and no deep-prefix count. |
| Piece → viewport · R8 | PageUp reuses the local row-start ring or scans a bounded window *above the previous top* once; `wrap_test:above_top_queries` checks first cold PageUp, target text and counted probes. |
| Layout → pointer · R11 | One slot-write path records hits on printable ASCII, wide/tab halves and synthetic cells; `editor_test:a7fo_click_contract` checks click and drag against painted bytes, repeated with partial scroll. |
| Viewport → layout → paint · R24 | Burst edits supersede unfinished repair with transformed byte origin; `cxho25_typing_jump` asserts the top and caret through multi-key growth and paste on actual accepted frames. |
| Viewport → layout → paint · R25 | Only completed, in-range rows can be borrowed. `wrap_test:c26_restart_rows/c26_follow_truth/c26_reset_rows` queries every frontier while busy and detects any fabricated byte-zero answer. |

Also compare incremental cells, absolute hits, affinity and top against fresh emission from the **same provenance** across repeated replacements, split CRLF, split combining/ZWJ sequences, tab-width and gutter digit changes, offscreen lookahead words and both insertion gravities at a soft seam. Run tiny grants to split every phase, check the completed-row identity counter across MORE and the displayed serial across rejected submit. Test stale rank after tab reuse, stale checkpoint after reflow, broken backing at the final poll, large horizontal scroll, restore, short final page, unknown labels becoming exact and no-allocation fallback. Relevant pin suites include view/scroll/display, layout/wrap tests and fuzzers; the process-level motion check is `tests/contract/cases/navigation.py`. These are acceptance specifications for implementation.

## Footprint and signature

Count comment-free source plus public headers, using the harvest's static file count (M; source enumeration, independent of power or machine load).

| Module | Measured v1 | Proposed allocation (E) | Derivation |
|---|---:|---:|---|
| viewport | 2,775 | 2,215 | Sum of view, scroll, scrollctl in the harvest, less 373 lines of editor scroll mirroring/orchestration and 187 lines for resident reads now owned by layout. The remaining donor allowance covers the value protocol and motion/restore. |
| layout | 1,655 | 1,842 | Sum of layout, wrap, checkpoints and headers in the harvest, plus the transferred 187-line window-reader allowance. A shared breaker and deletion of the one-row emitter finance slot publication, dependency records and the per-intent ring. |
| Combined | 4,430 | 4,057 | 2,775 + 1,655 measured; 2,215 + 1,842 proposed. Growth in layout buys source-window ownership once. These are review estimates, not a reason to compress implementation or omit a witness. |

The donor counts and named cases come from the [Wave 2 harvest](wave2-L3-L4-harvest.md). Architecture and rank ownership follow the [plan](../PLAN_V2.md) and [amendments](wave0-plan-amendments.md). The implementations that define byte reads, line answers and segmentation are specified by [piece.h](../../src/piece/piece.h) and [utf8.h](../../src/utf8/utf8.h).

**Signature scope.** This approves the row contract, local approximation/LIMIT policy, complete-frame publication, and navigation-reader requirement. L5 reader provisioning, input ordering and submit acknowledgement, L3 glyph/pin and render lifetimes, the A19 counting scheduler, tabs/session persistence and minimap implementation have their own owners. No Wave 1 API change is required. The two 10 GiB fixtures, live allocator/G1 observations, ported witnesses and final line counts are implementation evidence still to collect.

Advisor signature ____________________    Date ____________________
