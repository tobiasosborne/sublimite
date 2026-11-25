# Wave 2 viewport and layout

STATUS: SIGNED by the advisor 2026-11-25 22:10 CET as the Wave 2 viewport and layout contract (16ffada). Layout's paint callbacks stay provisional until font is signed.

**Revised proposal for advisor signature · Astra · 2026-11-25**

## S1 · Adopt the absolute byte grid with scalar-safe seams

> **Proposed A29 to PLAN §4.5.** In both modes, break display lines at BOF, after each LF, and at the aligned interior grid starts defined below, at a fixed grid spacing S = 32,768 bytes (maximum display-line span D = 2S+4 = 65,540 bytes). Reset decoder, grapheme, column, indentation and wrap state at each start. Keep the local upward row cache. Absolute grid arithmetic replaces long-line checkpoints. In a row certificate, `line_start` is the current **display-line** start. Define `approximate` as “this display line starts synthetically”. All its rows have exact geometry under this rule. Logical line numbers still count LF only, with knowledge expressed separately by `has_exact_line`. Preserve the byte anchor and certify the same rows on every restore.

For each positive multiple g of S strictly inside the snapshot, test the **raw** interval [g−S,g) for LF. If empty, start at g when it is a strict UTF-8 unit boundary. If a valid scalar straddles g, start immediately after that scalar. Test leads at g−3 through g−1 using the landed strict decoder and actual available bytes. This moves right by at most three bytes (P, four-byte UTF-8). A stray continuation is itself a one-byte invalid unit, including in a run of arbitrary length. Never skip invalid bytes to reach a non-continuation. If the cut would fall between CR and LF, move it after LF and coalesce with that hard start. Suppress synthetic starts at EOF. A hard start at EOF after a final LF still exists. The grid never inserts a document byte.

| Advisor claim | Verdict and derivation |
|---|---|
| (a) Spacing and reverse search | False with scalar alignment. Put LF at g−S, so the hard start is g−S+1 and grid g is blocked. With no more LF, let a four-byte scalar starting at g+S−1 shift the next seam to g+S+3. The span is 2S+2. Use **D = 2S+4** as a conservative span bound (P). From a hard start the next eligible **raw** grid is within 2S unless an earlier LF resets it. From a synthetic start it is within S+3 unless LF intervenes. EOF suppression costs at most the same alignment allowance. For byte t, choose q as the largest grid at or before t−4, clamped to BOF. Scan backwards from t through max(0,q−S) once, stopping early on a sufficient LF. Either q is eligible with its seam before t, or its preceding block contains a later hard start than q−S. Compare all newer eligible seams ≤t and hard starts; retain the latest. The reverse range is at most 2S+3 bytes plus fixed scalar/CRLF probes at the ends. For trailing affinity at a seam resolve the predecessor using t−1. |
| (b) Forward decision | O(1) is correct, the aligned-s test is false. With no LF and seam g shifted to g+1, the test at g+S would fail. Keep the nominal grid coordinate for a synthetic start, or just the last hard start encountered. At g reset iff there was no LF in the raw block, equivalently last hard start ≤ g−S. After resuming at a known synthetic start, initialize “no LF since its nominal g” and walk successive grid events. A CRLF-coalesced start is hard. Soft wraps never update this predicate. |
| (c) Determinism and locality | Yes for determinism. A target row can be reconstructed by finding its start and scanning its bounded display line to its end, including right context. Each of these operations has a D-byte bound plus fixed halo. A uniform 3S neighborhood for all required rows is false. In clip mode with repeated S-byte newline-free ASCII display lines, four preceding rows alone occupy 4S bytes. A whole viewport needs a row-dependent bound (S4). Rank evidence and absolute publication ordinal are accounted for separately. |
| (d) Cost parity | Only the byte-window order can agree, e.g. a single seed scan with S of order RB. The former RB was a cap, and a warm hit may inspect no text. R clipped, LF-free lines can require Θ(RS) bytes when exact ends/columns are required. Seeking a grid is cheap, but decoding an entire segment of combining text and emitting cells have distinct costs. There is no general work or latency parity. |

**Fix S once for this format.** Size the long-line scan for a cold layout against target A's warm-file G5 goal (correct first viewport submitted in 6 ms p50 / 9 ms p99, at any line length). Allow about 1.0 ms for the frame. In wrap, the target display line alone can supply the required rows. Near its start, stage one predecessor if its tail covers the outstanding rows above and the target covers those below. Near its end the neighbor can instead be a successor. For this ordinary long-line case the count is one or two distinct lines. Retain the target's staged bytes and summaries through paint when visiting that neighbor, so neither line needs a second break/width scan. Short lines can require many neighbors, and a short target can require rows on both sides. S4 gives the general bound.

The landed `cstep_prepend_40k` measured 126,425 ns p99 per 4,096-byte grant, about 30.87 ns/byte (M)[AC], load1 1.58–1.75. For two full spans, 2(2S+4) × 30.87 ≤ 5,000,000 ns gives S ≤ 40,490 whole bytes. The largest power of two is **S = 32,768 bytes**, **D = 65,540 bytes**. The scan estimate is 4.046 ms for 131,080 bytes. G5 is a sizing goal. Measure the complete first viewport separately, including discovery, table passes and paint. A scan can resume across UI slices. [G5, §§0.2 and 2.4](</home/tobias/Projects/sublimite-v1-oracle/perf/01-perf-target.md>)

This choice is independent of later machine speed, geometry and tick grants. LF-only scan_count p99 is 7.70 GB/s on resident contiguous data (M)[AC], load1 1.39→1.36, and cannot price Unicode segmentation. Neither primitive measurement times I/O, fragmented spans, word planning, painting or a complete frame. Yield those operations independently. [Width and break measurements](v2-0fv.11.md) · [LF throughput](v2-0fv.10.md)

**Visible behaviour for Tobias**

- Both modes can start a new row mid-word on a long physical line. With wrap off, horizontal scrolling reveals columns within each folded display line. The first fold depends on the preceding LF and the absolute grid.
- At a fold, tab columns and leading indent start again at zero. A straddling grapheme can display as two clusters, even with different ink or width. A valid scalar and a CRLF remain intact. Home and End use the active visual row. The file, copied bytes and LF-based logical ranks retain their original contents and meaning.
- An insertion before a long line can move its folds. Switching tabs or scrolling away and back to the same anchor reproduces the same rows. A seam just before an LF can leave a newline-only row. Synthetic starts never increment the line number or supply a logical gutter hit.
- Typing indentation uses a known **hard** start or the caret byte as the documented empty-leading-run fallback. This can produce a bare Return on a folded line. A synthetic offset is never passed as a known logical start to L1 indent. [Indent contract](../../src/indent/indent.h)

## Invariants and row contract

**The same (text, geometry, anchor) gives the same rows, whatever came before.** Paint and rank evidence may change colours or labels. Each tab persists one top byte, 64-bit horizontal origin, Q8 pixel remainder and caret affinity. Each intent and publication has an owner and serial.

`byte` begins a row, `end` is its drawable boundary, `next` starts its successor. Soft and synthetic seams share `end == next`: trailing affinity owns the previous row and leading the next. LF/CRLF end before the terminator and advance past LF. A lone CR is text. The terminal row ends at length with next = UINT64_MAX (P, piece length ≤ 2^60). A final LF gets an empty successor, empty text one terminal row. Padding has no certificate. Every nonterminal row advances.

Columns count from `line_start` as defined in A29. Continuation indent consumes cells and no bytes. `newline` means LF consumed, `continuation` means a soft wrap inside a display line. Ordinal is zero-based within this publication. Logical rank is zero-based and readable only under `has_exact_line`. Omit v1's `last`: no semantic reader at the pin. End uses certified end/affinity and pointer uses the emitted cluster interval, including tabs and wide halves.

Geometry includes wrap/break/indent policy, tab stops, cell metrics, grid, gutter/sidebar and clip hscroll. Deduct gutter digits plus the pin's two inset and two gap cells, clamped to available width. A full-width gutter leaves only chrome/padding and preserves the byte anchor. Keep v1 wrap defaults for extensionless names, .md, .txt and .tex. Toggling wrap clears hscroll and preferred column.

Draw row i at text origin + i × row height − remainder, in pixel Q8. Invert inside the text clip using floor((y + remainder)/height), including overscan. Fixed chrome stays put. On zoom divide the old pixel remainder by the new height, advance the quotient and keep the modulus. Resolve top with leading affinity, saturate at the ends, round clipped damage outwards.

## Q10 · Certificate validity and late rank

Authenticate buffer incarnation, committed text serial, geometry serial and publication owner/serial. A row depends on its incoming state and **every inspected byte and gap**: grid predicate and alignment, line prefix/indent, scalar and CRLF context, break lookahead beyond a rewind, EOF. An insertion on either interval edge touches it. An edit at a seam tests both neighbors. Same start/end alone cannot certify reuse.

L5 issues a fresh text serial on committed edit, undo, reload or rebind and retains the committed snapshot. Map the top with before gravity. Bytes outside a replacement retain position or shift by its delta (including the old right edge of a nonempty deletion). Consumed points and insertion ties take the chosen replacement edge. Snap a newly joined cluster to the legal edge for its affinity. Keep this transformed byte as the near-follow origin. A mapped byte alone proves no row.

Use A19's `piece_lines_before(view, top.byte, &count)` once for the first visible row. On false, mark the lower-bound label estimated or withhold digits. Advance labels only on LF. For Go To Line, `piece_lines_start(view,line,&off,&before)` on true gives an exact start or EOF. On false wait for new count evidence; the blocking offset cannot satisfy the request. Byte navigation continues. Check backing before either answer.

Late rank uses a new snapshot with the same text, top and matching rank request. With fixed gutter width relabel and publish another serial. A digit-width or metric change advances geometry and reflows the byte anchor. Freeze gutter width during emission. Paint-only changes invalidate affected cells.

## Q11 · Cold and warm placement

Capture target, affinity, rebased origin and request id once. Follow uses the fully visible margin capped at floor((visible_rows−1)/2) as at the pin. Keep an already valid top for a fully visible caret inside the margin. Use minimum reveal for a near Return and local EOF backfill for a short final page. A miss goes straight to the target display line. Locate earlier display lines by scanning back from the current start, later ones by forward grid/LF events. Stop as soon as enough visual neighbors exist on each required side, or at BOF/EOF. Preserve cursors, accumulated summaries and offsets on MORE. The local cache holds at most R certified starts; eviction only changes the work.

L5 initializes **one process-wide single-thread pool** at startup with `work_pool_init(&reader, 1, 0)` and shares it across tabs. Route potentially faulting reads and line queries to its bulk worker. The ordinary bulk lane may be occupied indefinitely and WORK_FOREGROUND admits bounded CPU work only. Use already validated resident windows inline. One active read with a prepaid slab, plus the newest queued demand, suffices for the active view. Readiness wakes the UI. Cancel on superseding input or binding/geometry change. A cancelled job retains its snapshot/slab until physical retirement; compare owner, key, range and intent before adopting bytes. Continue to drain stale deliveries. Keep a compatible walk on rank-only notification. Progress assumes read completion or reported backing conflict.

## S2 · Motion and selection ownership

Viewport computes the motion target and the proposed selection. Layout supplies geometry and certified byte windows, then places the requested top. L5 calls the existing edit owner to install a selection change and commits the matching presentation. Viewport never includes layout. Its resumable motion function consumes a read-only, viewport-declared provider of text windows, row intervals and column-to-byte answers. Layout drives that function and suspends it on missing evidence. The provider uses the same grid and boundary rules as emission. No callback can start a nested emitter. L5 constructs the intent by value.

In the table **keep** preserves selection. **Move** applies the computed byte/affinity, collapses the mark for plain motion and preserves the original mark on Shift. Horizontal motion clears preferred column. Vertical motion latches the original screen column including continuation indent. V means viewport, L means layout. F is one bounded placement in S4. Bounds below are target work before F. Text motion consumes only validated bytes and resumes its cursor across slices.

| viewport_action | Target owner and inputs | Selection owner and operation | Bound (P) |
|---|---|---|---|
| RESTORE | V clamps saved top byte to current length. L resolves row and clamps at EOF. | V keep | O(1) + F |
| REFLOW | V maps saved byte and Q8 remainder into new metrics. L resolves and advances the quotient. | V keep, reset preference on width change | F plus rows crossed by quotient |
| FOLLOW | V supplies committed caret, affinity, transformed top and margin. L resolves minimum reveal. | V keep | F |
| SEEK_BYTE | V clamps absolute top request. L resolves leading row. | V keep | O(1) + F |
| SEEK_LINE | V requests logical start from piece select via reader. L resolves exact answer. | V keep | O(H+K) per new rank observation + F |
| SCROLL_Q8 | V divides signed pixel displacement plus remainder by row height. L locates clamped top. | V keep | O(1) arithmetic, walk up to absolute quotient rows, then F |
| LEFT | V picks preceding display cluster from certified segment, or low selection edge on unshifted collapse. | V move | O(D) + F |
| RIGHT | V picks next cluster or high selection edge on collapse. | V move | O(D) + F |
| WORD_LEFT | V scans cluster classes backwards to the pin's run start, crossing synthetic seams within a physical line. | V move | O(D + bytes traversed) + F |
| WORD_RIGHT | V runs the pin's forward class transition, crossing synthetic seams, handles physical EOL. | V move | O(D + bytes traversed) + F |
| UP | L supplies preceding row and byte at preferred screen column. V chooses destination/edge affinity. | V move | one neighboring row + F |
| DOWN | L supplies following row and column lookup. V chooses destination/edge affinity. | V move | one neighboring row + F |
| PAGE_UP | V requests a fully visible page upward for caret and top. L resolves rows and preferred column. | V move | ≤ R row displacement per operand + F |
| PAGE_DOWN | V requests a fully visible page downward for caret and top. L resolves rows and preferred column. | V move | ≤ R row displacement per operand + F |
| HOME | V uses L's active row, first nonblank and current caret for smart Home. On a soft continuation use row.byte. | V move, leading at start | O(D) for visual Home + F |
| END | V uses L's active row.end and successor kind. | V move, trailing at shared seam | O(D) + F |
| DOC_HOME | V chooses BOF. | V move | O(1) + F |
| DOC_END | V reads snapshot length. | V move | O(1) + F |
| SELECT_ALL | V uses zero and snapshot length. | V sets mark at zero and caret at length | O(1) + F |
| SELECT_WORD | V takes clustered class run around caret with the pin's existing-selection/EOL rule. | V sets both ends | O(D + bytes traversed) + F |
| SELECT_LINE | V uses piece rank/select for the physical LF bounds of both selection edges, excludes an end exactly at the next start, extends one line on repeat. | V sets mark/start and caret/next, or EOF | O(H+K) per indexed query + fallback bytes (worst O(length)) + F |

Word/line selection still acts on file content. For SELECT_LINE, rank an endpoint with `piece_lines_before`. On an exact rank n, use `piece_lines_start(n)` for the hard start and select the needed successor rank for the exclusive selection end (EOF past the last line). Use each exact answer immediately. On inexact rank, find the required LF bounds around that byte with resumable raw scans. On an inexact select, scan locally from the endpoint for the missing boundary. The returned (off, before) is an exact prefix on valid backing and can shorten a forward LF count. Keep the repeat and exclusive-end rules above. A backing conflict follows Q13. Text-run and fallback LF walks can outlast a slice. Reverse word scan stages each preceding display line forward once and visits its summaries backwards. Report these command-specific costs separately from caret-follow, under the same input serial.

## Q12 · one emitter and visible repair

**S5 · Clip.** Clip filters each display line by its 64-bit `hscroll_cols`. Resolve the target column from its canonical start before publishing. From the canonical start, stage widths up to the right clip edge or drawable end, including the complete cluster crossing the edge and its break lookahead. Determine `end`, `newline` and `next` independently by raw LF scan, grid arithmetic and the S1 alignment probes. A row's next is the aligned seam, the byte after LF, or the terminal sentinel, even when LF lies far outside the window. End moves to the certified drawable end, trailing at a synthetic seam. The exact final column is needed only for horizontal reveal of an End/EOL caret and for a horizontal-origin clamp that asks for the active line's width. Resolve it on demand for that row, resuming the prefix scan.

Publish `end_column` on a clip row as the exact column when the clip prefix reaches drawable end, otherwise UINT64_MAX. An on-demand answer stays private so identical geometry always publishes identical certificates. Wrapped rows retain their exact field. Pointer, selection and caret paint use emitted hit intervals and byte ends. Render consumes cell spans and damage. None reads the clip row's final column. Smart Home toggles first nonblank/start. Each scan requires at most O(D) source bytes; column ≤ D × max(2, tab_cols) (P). An origin beyond end shows blank cells. v1's view.c uses smart Home and End before LF/CRLF; its deep-column benchmark uses checkpoints to recover physical columns. A29 gives a proven segment-relative column in both modes.

Use one cluster stream. Wrap at the last legal whitespace break, swallow trailing wrap whitespace as in the pin, otherwise break a long word at a cluster. Tabs advance to logical stops. Continuation indent is the leading SP/TAB width capped at half the text width, zero at widths ≤2 (P, pinned wrap.c). Consume an oversized first cluster with clipped fallback, zero-width clusters by byte length and invalid input one byte at a time. Segment to a proven display-line boundary, distinguish actual EOF, and carry split scalar/CR state. ASCII batching verifies right context; preserve A16 presentation.

Write a cell and hit together. Each visible part of a tab or wide cluster retains its entire byte interval and full width for midpoint testing. Synthetic indent hits row start, trailing blanks hit end with affinity. A hard-line gutter hits its logical start. Hidden whitespace advances the column.

**S6 · Emit through visible bottom.** Reuse the prefix whose dependencies and paint remain valid at identical absolute positions. Emit from the first touched dependency through the bottom. Include the incoming state proof for edits before the viewport. Rebuild the visible range when it shifts. No hard-start suffix splice and no directory offset translation. A directory still shares unchanged slots and swaps complete pictures. Any future suffix splice needs equal incoming state and either delta ≡ 0 mod S or proof that both suffixes have no synthetic seam. Decorate-only changes use certified hits and repaint the affected slots.

At the pin, `bench/layout_bench.c` warms the map and times full layout at its largest tested size, 360 columns × 300 rows. Touching the top dependency makes the no-splice alternative a full visible emission. Recorded p50/p99 in µs (M)[AC], launch load1 4.24, wrapped 4.54, are 128.5/288.0 for clipped ASCII, 234.7/254.3 for clipped log, 302.6/499.9 for wrapped Unicode and 266.4/305.0 for wrapped one-line text. The loaded P3.1c run also records 884.7/1,333.0 for wrapped Unicode ([AC], wrapped load1 6.91). Compare G1's 1,000/2,000 µs **whole** input-to-submit budget. These layout-only samples do not establish a G1 pass or require a splice. Typing-row samples use v1 incremental layout. Actual admission depends on prepaid dimensions. Missing (M): measure first-visible-row edit through bottom at the largest admitted geometry including overscan, in clip/wrap with real glyphs, styles and hits. Record layout and whole G1 p50/p99 on the same keys with power/load and live allocator guard. [Benchmark](</home/tobias/Projects/sublimite-v1-oracle/bench/layout_bench.c>) · [M0 raw rows](</home/tobias/Projects/sublimite-v1-oracle/docs/bench/m0.md>) · [P3.1c full run](</home/tobias/Projects/sublimite-v1-oracle/docs/decisions/P3.1c.md>)

**S8 · Source changes.** `layout_input.change` carries the signed `edit_change {off, old_len, new_len, revision}` by value for EDIT. Each committed text command produces the bounding replacement from its previous published snapshot, even for a multi-splice plan with untouched gaps. Require consecutive revisions and matching source incarnation for reuse. L5 rebases the saved anchor in command order, using actual splices when available and conservative before-gravity on the envelope otherwise. Selection comes from edit. If multiple commands committed between begins, issue BIND on the newest snapshot with that transformed anchor and rebuild. Never apply the last envelope to an older frame. `edit_take_change` consumes the slot **after** submit or headless reply. L5 can compute the identical envelope from its known command plan for first-frame EDIT. With only `edit_read` (e.g. opaque replay), use fresh BIND and follow the committed caret from the clamped saved top. Exact transport of an old top through an unobserved splice is unavailable before that first submit. Consume/distribute the change once after delivery, retaining its predecessor revision. [Signed edit protocol](wave1-edit-spec.md)

## Q13 · Publication and source lifetime

Swap the complete row directory only after real rows, EOF padding and final backing validation are settled. Stage changed slots and share unchanged ones. Two directories and two surfaces' worth of slots cover displayed and candidate rows. L5 commits top and accepted picture together after submit. During MORE or refused submit keep the displayed serial; retry or discard the candidate. A displayed hit requires matching owner, serial, current buffer/text/geometry, saved pixel transform and an in-range real cell. Stale hits leave selection unchanged. A borrowed row lasts through the non-reentrant UI read/submit-preparation step. Submit captures longer-lived inputs before return.

Use `piece_iter_init` and `piece_iter_next_span` on a bounded end with NULL newline output. Copy with `piece_snapshot_read_span`, observing returned count/chunk boundaries. Discard the attempted destination and dependent staging on CONFLICT; RANGE is an error. For mapped ORIGINAL supply a live validator and close the enclosing operation with another identity check and generation-matching zero-epoch fault poll after dependent loads. Blocking identity work belongs on the reader; UI checks its allocation-free binding token. Prepay and touch all storage before input. Refusal preserves the prior publication. Gate zero allocations through submit under the active release guard.

## Public header proposals

Types are public values unless marked opaque. Serial exhaustion retires the owner. Header sketches omit include guards and standard includes.

**viewport/viewport.h** declares the following value types. The action names are the S2 table entries prefixed with `VIEWPORT_`.

| Type | Members in declaration order |
|---|---|
| viewport_anchor | uint64_t byte, hscroll_cols · uint32_t subrow_q8 · bool visual_end (caret affinity, top leading) |
| viewport_row_cert | uint64_t byte, end, next, line_start, col, end_column, indent, ordinal, exact_line · bool has_exact_line, newline, continuation, approximate |
| viewport_key | uint64_t buffer, text, geometry |
| viewport_selection | uint64_t caret, mark, preferred_col (UINT64_MAX means unset preference) |
| viewport_state | viewport_anchor top · bool wrap |
| viewport_intent | viewport_key key · uint64_t serial, target · viewport_action action · viewport_state origin · viewport_selection selection · int64_t pixels_q8 · bool extend |
| viewport_answer | viewport_key key · uint64_t intent_serial · viewport_anchor top · viewport_selection selection |
| viewport_frame | uint32_t width_px, height_px, row_px, layout_rows · int64_t origin_y_q8 (clip-local) |

```c
typedef struct viewport_motion viewport_motion;
typedef enum { VP_DONE, VP_MORE } viewport_progress;
typedef enum { VP_WINDOW, VP_ROW, VP_NEIGHBOR, VP_COLUMN,
               VP_LOGICAL_RANK, VP_LOGICAL_START } viewport_query;
typedef struct viewport_request {
    viewport_query kind;
    union {
        struct { uint64_t byte; } window;
        struct { uint64_t byte; bool trailing; } row;
        struct { uint64_t byte; bool trailing; int64_t displacement; } neighbor;
        struct { uint64_t row_byte, screen_column; } column;
        struct { uint64_t byte; } logical_rank;
        struct { uint64_t line; } logical_start;
    } as;
} viewport_request;
typedef struct viewport_evidence {
    const uint8_t *bytes; size_t len; /* resident window starting at off */
    uint64_t off, length, point;     /* snapshot length and resolved byte */
    viewport_row_cert row;
    uint64_t count; bool exact;      /* rank or select prefix count; select offset in off */
} viewport_evidence;
typedef struct viewport_port {
    void *ctx;
    viewport_progress (*query)(void *, const viewport_request *,
                               size_t *credits, viewport_evidence *);
} viewport_port;
size_t viewport_motion_size(void);
void viewport_motion_begin(viewport_motion *, const viewport_intent *, viewport_frame);
viewport_progress viewport_motion_step(viewport_motion *, const viewport_port *,
                                       size_t *credits, viewport_answer *);
bool viewport_wrap_default(const char *path);
uint64_t viewport_rebase(uint64_t byte, uint64_t off, uint64_t old_len,
                         uint64_t new_len, bool after_gravity);
bool viewport_accept(viewport_state *, viewport_selection *, const viewport_intent *,
                     const viewport_answer *);
bool viewport_plan(viewport_anchor, uint32_t width_px, uint32_t height_px,
                   uint32_t row_px, viewport_frame *);
bool viewport_pixel_row(const viewport_frame *, int64_t y_q8, uint32_t *row, uint32_t *within_q8);
```

Allocate one max_align_t-aligned motion cursor in layout's arena. The query returns resident windows, active/neighbor row and column answers, or completed piece rank/select evidence (including inexact results) by value; a window borrow lasts through consumption in this step. Debit the shared grant and preserve both cursors on MORE. L owns pending I/O/errors. L5 converts wheel input to signed Q8 once (G, the pin's notch is three row heights). Accept requires the current key and intent serial. L5 installs a motion selection via edit in ordered UI dispatch. Frame planning checks row height, extents and widened products.

**layout/layout.h** uses the public base, piece, work, edit, render-types and viewport headers. The layout handle is opaque.

| Type | Members or enum values |
|---|---|
| layout_result | READY, MORE, STALE, CONFLICT, CAPACITY (LAYOUT_ prefix) |
| layout_update | BIND, EDIT, REFLOW, DECORATE, RELABEL (LAYOUT_ prefix) |
| layout_limits | uint32_t rows, cols · size_t quantum |
| layout_geometry | uint32_t cols, wrap_cols, gutter_cols, sidebar_cols, tab_cols, cell_px, row_px, width_px, height_px, margin_rows |
| layout_source | piece_snapshot *snapshot · piece_validate_fn validate · void *ctx · uint64_t rank_serial |
| layout_input | viewport_intent intent · layout_source source · layout_geometry geometry · layout_paint paint · layout_update update · edit_change change (EDIT only) |
| layout_view | const layout *owner · viewport_key key · uint64_t serial · viewport_frame frame · uint32_t rows, cols |
| layout_hit | uint64_t lo, hi (relative to row.byte for TEXT) · uint32_t part, width · enum kind (TEXT, INDENT, GUTTER, EOL) |
| layout_row | viewport_row_cert cert · const render_cell *cells · const layout_hit *hits |

```c
typedef struct layout_paint {
    void *ctx;
    uint64_t serial;
    const render_cell *ascii; /* generation's printable ASCII ink templates */
    uint64_t (*style_run)(void *, uint64_t byte, uint64_t limit, render_cell *style);
    void (*glyph)(void *, const uint8_t *cluster, size_t len, uint32_t width, render_cell *);
} layout_paint;
layout_result layout_open(base_arena *, layout_limits, work_pool *navigation_reader, layout **);
layout_result layout_begin(layout *, const layout_input *);
layout_result layout_step(layout *, size_t credit, viewport_answer *, layout_view *candidate);
bool layout_row_at(const layout_view *, uint32_t row, layout_row *);
bool layout_hit_at(const layout_view *displayed, viewport_key current,
                   int64_t x_q8, int64_t y_q8, uint64_t *byte, bool *trailing);
bool layout_present(layout *, const layout_view *accepted);
void layout_cancel(layout *);
layout_result layout_close(layout *);
```

**S7.** Style by the cluster's first byte. The callback fills style fields and returns the exclusive run end in (byte, limit]. Reuse those fields up to that end. A singleton printable ASCII cluster indexes the immutable ASCII sheet directly. An ASCII base with a joining mark uses the general glyph callback. Pass original bounded bytes and width under the view hold. The adapter sets only ink identity/slot. Zero-width clusters request no ink. Oversize or invalid input uses the font marker with its real byte extent. Both callbacks are provisional until font and render are signed.

Open checks products and rolls back its arena mark on refusal. Replace capacity off-path. Begin copies values and retains the snapshot; validation and paint contexts outlive all users. Step retains search, segmentation, placement and fill cursors on MORE, returns outputs only on READY. Compatible relabel can wait for the next frame boundary. Cancel retires private work. Close returns MORE until physical retirement; drain on wake before releasing the arena.

## S4 · Count the original intent across all slices

R counts admitted visible slots including overscan, C their width. Empty clips emit no rows; the follow bounds assume R ≥ 1. H = PIECE_MAX_HEIGHT + 1 and K = PIECE_CHUNK. D bounds one display line. B is an independent per-slice grant of byte, summary, directory or cell operations. Use `UTF8_GRAPHEME_BUDGET` (4,096 bytes, G from the landed p99 measurement) as the maximum decoder grant, check the G1 slice deadline between bounded units, and split wide rows, clears and writes across grants. Grant zero performs no work. B may vary each tick; S is fixed.

**Stage summaries.** In wrap, segment each distinct display line forward into (byte range, width, class, legal break, cumulative logical columns). Widths include actual tab advances. Close a cluster at the artificial end of its segment. Carry up to three trailing bytes across refills and keep one complete lookahead unit before each decode, even across tiny grants. A wrap rewind uses a summary index. The suggested three-examination replay argument counts row visits. It cannot bound decoder calls inside L1 or account for rereading partial scalars. Staging makes narrower continuation widths irrelevant to decoding. In a single staged pass a unit is decoded at most once as previous-cluster lookahead, once by the consuming grapheme step and once by the width step (P, `utf8_cluster_step`). ASCII spans can bypass all three. There is no extra source pass per soft break.

**Skip offscreen rows without repeatedly laying them out.** In wrap, for a cold target inside a display line or an unknown predecessor line, compute the following temporary table over *cluster indices*, only for that line. For each possible row start i find its successor j using the same breaker as emit. With fixed continuation width, cumulative columns and a sliding right edge locate the last fitting cluster for all i in linear time. Precompute the preceding legal break and end of each whitespace run; select the break and swallowed bytes in O(1), forcing advance past an oversized first cluster. Handle first-row width once, separately. Zero-width runs advance indices. Include the empty hard/terminal row explicitly. This table holds integer offsets, never cells, hits, certificates or an emitter per probe.

Every j exceeds i or ends the segment. In descending index order compute the number of remaining rows as one plus the value at j. For the line containing t also propagate the depth of its owning row: test the row interval/affinity, else inherit from j toward t. Now the true first row gives both the line's row count and the target's depth. To retrieve a suffix or a band around the target, choose its first retained depth and in one descending pass set each deeper entry's answer to its successor's answer, seeding entries at the chosen depth with themselves (depth here is remaining row count). Start from the true first-row entry's answer and enumerate just the retained band. This avoids walking a chain of thousands of one-cell rows merely to discard its prefix. Earlier lines need only enough of their tail to satisfy outstanding predecessor demand. Share the row-successor primitive with warm repair and output. A forward visible fill from a certified row needs no all-starts table.

| Per caret-follow, including cold placement F | Bound and falsifiable counters (P unless marked) |
|---|---|
| Display lines and bytes | Locate the target plus at most R−1 predecessor and R−1 successor visual rows, enough for reveal, short-last-page clamping and overscan. Each further display line supplies at least one row, so at most 2R−1 distinct lines. Seek once at the target and grow monotone frontiers. At most (2R−1)D bytes of line content, plus at most D at either outside edge for a start proof and fixed scalar/CRLF halos. Stop backward discovery at an LF once it settles the nearest start. Use bounded resident slabs, cache grid predicates and continue the same search across misses. Zero deep-prefix walks. |
| Completed rows | Enumerate at most **2R−1 actual row intervals** around the target, including discarded candidates. Materialize at most **R** finished cell/hit rows after top selection. Even counting both operations as completions gives **3R−1** in all slices together (239 for R=80). Warm repair visits at most R prefix/dirty rows and emits at most R. Count every actual interval and every row-slot init/seal, including scratch work and misses. Separately count hypothetical successor probes: at most one continuation entry per staged cluster and one first-width evaluation per display line per planning pass. No hypothetical entry initializes a row or writes a grid. A predecessor query cannot restart the table. |
| Decodes and total work | Keep offset plans for the selected band. Reuse the successor/depth workspace per line. Keep the first two wrapped lines' summaries and source until paint or until a third line is needed, then recycle scratch as lines are visited. For final paint restage each evicted selected line at most once, giving ≤2 staging passes and **≤6 L1 unit decodes** per unit per follow. Check seams once per nominal grid with ≤3 strict candidate-lead decodes (disjoint halos). Budget these separately, also count raw LF inspection/copy, paint/ink calls, touched dependencies and all scratch-table passes. Font lookup is once per visible eligible cluster, with its own bounded key cost. Ordinary CPU O(H·RD + RC), linear in the bytes actually inspected, summaries visited and cells touched with bounded tree height. In clip, charge raw scans/probes separately from segmentation. For the R painted rows decode only the prefixes through hscroll_cols + text width (saturating), plus crossing clusters/lookahead and any active-row column demand. Other placement rows need only raw boundaries. At zero origin this prices R clip widths plus raw scans in ordinary positive-width text. A deep origin, zero-width runs or a single long cluster can still cost D decoded bytes per row. At most O(HB) per slice plus one indivisible piece rank/select of O(H+K) when needed. A19 allows at most one first-visible-row rank per frame. A faulting read has no OS latency bound. |
| Storage and independence | With D = 65,540, prepay up to 2D = 131,080 staged source bytes plus fixed halos, two sets of at most D cluster summaries (one per byte), and one reusable successor/depth workspace for at most D+1 indices including the terminal entry. Size each array by its entry width. This is O(D) scratch, with O(R) saved row offsets, proof intervals, starts and directories, O(RC) old/new prepaid cells and hits. Replay for final paint groups rows by display line, using the saved row starts. Stream probes across ticks and check allocation products and backing before publishing. Neither a longer file nor a smaller grant increases the number of scans per line. Count a cancelled intent through physical retirement separately from its successor. |

A text motion that visits those same bytes pays separately: stage each traversed line once per directed endpoint walk; SELECT_LINE uses indexed line answers and scans raw LF only for missing bounds. Use disjoint endpoint cursors or share their overlaps. Add at most one such segmentation pass per endpoint to the placement count. Relative scroll by a very large Q8 delta also pays for the visual displacement it requests. The A11 bound above applies to the follow of an already named caret, including Select All/Doc End, across *every* grant and worker wake.

**10 GiB gates (G, PLAN Wave 2).** Try both one LF-free logical line and many short lines ending with LF. Ctrl+End and Select All take length directly. On the single line, exercise clip and wrap at one cell, tabs, Unicode, all-combining input, malformed continuations and giant hscroll. On dense LF, count the trailing empty row and backfill only available rows. Compare fresh and incremental at the same anchor. First cold PageUp, Return and typing must produce usable target text, exact caret/hits and the minimum necessary top movement while ordinary bulk is held. Inject a delayed independent read, cancel it, switch tabs, then deliver the old completion. Check original-intent rows **and** byte/summary counters under tiny grants, active release malloc guard and the integrated G1 lane. A retained old picture never counts as the frame carrying the new key.

## Integration witnesses to port

| Boundary and regression | Preventing step and observation |
|---|---|
| Viewport → layout → paint · R1 | Wrapped Return uses the rebased local top and re-emitted successor; `editor_test:n6090_typing_top` checks the top advances only by necessary rows on the key frame. |
| Viewport → layout → paint · R2 | Clip and wrap share the current-row publication fence; `vp4m_return_bottom` must see the new caret on the first frame carrying Return. |
| Piece → viewport · R6 | Independent navigation reader owns the outstanding window and wake; `editor_large_test:starved_follows` asserts movement and typing while the general bulk lane is still held. |
| Piece → viewport · R7 | Seek by EOF byte, derive top from the retained window and label once; `near_end_repro` and the external navigation case assert bottom-row caret, local reads and no deep-prefix count. |
| Piece → viewport · R8 | PageUp reuses the local row-start ring or scans a bounded window *above the previous top* once; `wrap_test:above_top_queries` checks first cold PageUp, target text and counted probes. |
| Layout → pointer · R11 | One slot-write path records hits on printable ASCII, wide/tab halves and synthetic cells; `editor_test:a7fo_click_contract` checks click and drag against painted bytes, repeated with partial scroll. |
| Viewport → layout → paint · R24 | Burst edits supersede unfinished repair with transformed byte origin; `cxho25_typing_jump` asserts the top and caret through multi-key growth and paste on actual accepted frames. |
| Viewport → layout → paint · R25 | Only completed, in-range rows can be borrowed. `wrap_test:c26_restart_rows/c26_follow_truth/c26_reset_rows` queries every frontier while busy and detects any fabricated byte-zero answer. |

**S9.** Compare every incremental result to fresh empty-cache emission from identical text, geometry and anchor. Compare row fields, cells, absolute hits, affinity and top with identical rank/paint inputs. Cover re-edits, lookahead, both seam gravities, tabs, gutter digits, CRLF, invalid bytes, combining/ZWJ, scalar alignment, EOF on a grid and restore after eviction. Split every phase across grants. Check stale rank, final backing fault, refused submit, short final page and the release allocation guard. Port view/scroll/display and layout/wrap suites and fuzzers, plus the external navigation case.

## S3 · Footprint and signature

Budget comment-free C plus public headers. Each group is an implementation estimate (E) for the listed responsibilities. The donor footprint measured by the harvest (M, static count) is 2,775 viewport and 1,655 layout lines. It is a comparison point only.

| Module and function groups | Lines (E) |
|---|---:|
| viewport declarations, motion state and provider contract | 105 |
| wrap defaults, rebase, frame plan and inverse pixel mapping | 110 |
| motion dispatch, acceptance, affinity, Shift and preferred column | 140 |
| cluster/word/whole-line target and selection walkers | 215 |
| visual neighbors, page, wheel, restore and reveal policy | 130 |
| **viewport subtotal** | **700** |
| layout declarations, open/close, checked storage | 175 |
| begin/step/cancel, source windows, reader validation and rank delivery | 235 |
| grid discovery, segmentation, summary/whitespace and successor tables | 285 |
| target depth, retained rows, follow, EOF fill and provider queries | 225 |
| clip/wrap slot emission, paint runs, hits and decoration | 245 |
| dependency repair, immutable directories, present, row/pointer lookup | 185 |
| **layout subtotal** | **1,350** |
| **Combined** | **2,050** |

A29 awaits signature. L5 lifecycle, render admission, font holds, tabs and minimap remain with their owners. This checkout’s `src/edit/edit.h` is a smoke stub. S8 uses the signed Wave 1 declaration. Implementation counts and timing gates remain to be measured.

Advisor signature ____________________    Date ____________________

## Errata after signature (advisor, 2026-11-25)
E5 (port contract; from v2-1sg Q1). There are no local conventions.
- (a) The length is given once, by the driver: viewport_motion_begin(viewport_motion *, const viewport_intent *,
  viewport_frame, uint64_t length). Layout passes the length of the snapshot it has bound, and it is valid for an empty
  snapshot. `length` is removed from viewport_evidence. DOC_END and SELECT_ALL use the value from begin and need no
  query.
- (b) viewport_evidence gains `uint64_t screen_column`, defined for VP_ROW only. It is the screen column of the queried
  byte with the queried affinity, counted in cells from the left edge of the row's text area, including synthetic
  continuation indent. Vertical motion latches its preferred column from it. `count` keeps its one meaning (a rank or a
  select prefix count). UINT64_MAX is not a signal in any evidence field.
- Layout's side of the contract: its port fills screen_column on every VP_ROW answer and never reports a length.
E6 (Home; from v2-1sg Q2). There is no physical Home in Wave 2. HOME is visual Home in both modes (§S1, §S2, §S5). The
words "physical-line Home" (from E4) named an action that does not exist and are struck from the HOME row and from the
paragraph under the table. The rank/select algorithm stays for SELECT_LINE only. A distinct logical-line Home bound to a
key at the v1 pin, if any, is a harvest finding for the Wave 3 keys work.
E7 (REFLOW; viewport owner read, V4). REFLOW does not carry the previous width in `target`, a field that means a byte or
a line everywhere else. The S2 table's REFLOW row reads: "V keeps the selection it is given; the issuer clears the
preference on a width change". Whoever changes the width (L5, where the geometry serial is minted) clears
preferred_col in the selection it puts into the REFLOW intent. Viewport's REFLOW never inspects `target` and never
compares widths.
E8 (VP_WAIT; viewport owner read, V5). viewport_progress gains VP_WAIT: "blocked on evidence only the driver can obtain;
call again when that evidence changes". The step returns it for an inexact SEEK_LINE answer, and the port may return it
too. The one-indexed-attempt-per-rank-observation rule belongs to the driver that receives VP_WAIT, and the header says
so.

