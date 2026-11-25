# Wave 2 font revision and render errata · Astra

25 November 2026. Revised the font proposal from `t3/wave2-font-spec` and the signed-in-substance render text from `fb7776c`, the baseline identified in the errata. The named render branch did not resolve. Both proposals are in `docs/decisions/`.

## Render

- **R-A adopted.** Install all tile deltas, apply cell patches, then scan the retained cells once against slots tagged by this packet. N=0 skips the scan. Text, chrome and overscan contribute row damage. A v1 43,200-cell grid traversal gives a historical scale of 67.9/125.6 µs p50/p99 [AC]. Its operation was filling cells and extracting runs. The new backend scan needs a target-box timing.
- **R-B adopted.** One non-reentrant `render_submit` checks the offer and sources, copies into the prepaid packet, and returns the accepted ID. Refusal preserves delivery credit, input association, damage, prior picture and next ID. At 360×300 plus overscan, 2,167,200 cell bytes cost an estimated 238 µs to copy. Add 108,360 cell validations plus run, region, tile and commit costs. Even before extras, 500 µs allows only 2.42 ns/cell of incremental validation. Calibrate whole-tile grants at ordinary geometry and measure fused validation/copy at maximum admission.
- **Production budget (E).** Shared render and prep 710 lines, backend integration 305 and L5 paint seam 100. Total 1,115, previously 1,255.

## Font

| Item | Decision |
|---|---|
| R-C | Stamp each lookup. On free-slot exhaustion sweep the live publication once, evict the oldest unmarked settled entry, and advance the paint serial. All slots live gives PRESSURE. A stale candidate across MORE is redecorated. Font exports neither holds nor device pins. |
| F3 | Four-way buckets bound each lookup at four probes. Compare full key bytes after full-hash, length and scalar-field matches. Bucket exhaustion gives PRESSURE. |
| F4 | K=124 UTF-8 bytes. One starter plus 30 non-starters prices the base-plus-marks drawing case. The longest of 1,468 Emoji 15.1 RGI ZWJ records is 35 bytes. Longer-than-K clusters settle LIMITED without storage, at their true width and hits. [Sequence table](https://www.unicode.org/Public/emoji/15.1/emoji-zwj-sequences.txt) |
| F5 | Compose from resident validated bytes on WORK_FOREGROUND. Run fc-match, file reads and validation on WORK_BULK. Only the unresolved key waits for discovery. |
| F6 | One atomic font_compose call per key attempt, with cancellation checked at key boundaries. Validator caps and tile area bound work. Measure the worst admitted key against the 5 ms CPU polling contract. |
| F7 | Pending is background-only at settled width. MISSING, LIMITED and PRESSURE use the settled marker. The v1 pin puts the ASCII question mark into each newly queued glyph descriptor. |

**Public functions**

- `font.h` (6) — `font_embedded_data`, `font_bootstrap`, `font_prepare`, `font_ascii`, `font_resolve`, `font_compose`.
- `font_async.h` (12) — `font_async_measure`, `font_async_open`, `font_async_request`, `font_async_event`, `font_async_step`, `font_async_paint_serial`, `font_async_lookup`, `font_async_sheet`, `font_async_identity`, `font_async_tile`, `font_async_activate`, `font_async_close`.

**F8 bounds**

| Operation | Bound |
|---|---|
| Lookup | ASCII and overlength refusal O(1). Ordinary bounded keys O(K+P·K), P=4. Free-entry exhaustion adds O(V+S) for V live cells and S entries. A full hash bucket refuses after P probes. |
| Composition | O(KFC+G+D·(E²+(1+E)T)+T) per foreground attempt for F faces, bounded cmap cost C, shape/expansion visit cap G, D outlines, E flattened edges per outline and T=2WH tile bytes. |
| Storage | S·(124+T+O(1)) bytes per bank, plus sheet/markers, loaded faces and bounded scratch. Two banks during replacement. The backend owns a separate ST runtime atlas, retained scene and one prepaid packet. |
| Submit | O(1+J+Z+U+N+L) on canonical runs, for J runs, Z cells, U region pixels, N records and L tight tile bytes. Payload 20Z+4U+L, L ≤ min(B,NT). Add bitset enumeration if used. Backend scans V cells once iff N>0. |
| Production budget (E) | 1,950 font core and declarations + 650 selection and bulk preparation + 650 runtime and source adapter = **3,250 code lines**. With 14,495 data lines, **17,745 total**, down 300 from the previous estimate. |

**Outside this pass.** The signed layout S7 still mentions a provisional view hold. The revised font contract supplies that callback with a generation-bound lookup. E5–E8 retain their viewport/layout ownership. Applying the PLAN amendment, implementation, real-display and allocation evidence, calibrated grants, scan and validation latency at maximum geometry, worst-key CPU timings and actual production counts belong to their respective implementation or signature steps.
