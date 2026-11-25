# Astra viewport round 3 review · v2-1sg

**Disposition — MUST-FIX before sign-off.** The proposed S2 word-motion bounds undercount the first rejected cluster. Two backward row-start mutants survive the submitted unit suite. The public header describes all six callback alternatives and both continuation states, but still leaves independent ports with different interpretations of valid evidence.

Reviewed Sol's changes from `8f35d57` to `afff755`, the round-two and round-three decision reports, the [round-one review](wave2-viewport-astra.md), and the [signed contract including E5–E8](../decisions/wave2-viewport-layout-spec.md). Source lines and test assertions below refer to `afff755`. The checkout's production files were left untouched. Builds, mutants and probes used a private copy of the reviewed revision under this checkout.

## Findings

| Severity | Witness | Result |
|---|---|---|
| **MUST-FIX · S2 bound** | Use one clip row containing `a.`, 4,000 copies of U+0301, then `z`. WORD_RIGHT and SELECT_WORD from byte 0 both end at 1. | Each consumes **8,002 decoder bytes** in 4 positive 4,096-credit steps. The punctuation and all its marks form one rejected cluster outside the word. The measurement excludes unconsumed lookahead and provider work. One complete scalar beyond the displacement cannot account for it. |
| **MUST-FIX · V3 regression test** | LEFT from EOF in S ASCII letters followed by U+0301, in clip and in 80-cell wrap. Change only the backward row-start proof on synthetic rows to BOF (N4). | The submitted suite passes. The mutant returns **32,767** in both modes, joining the mark to a base across the certified seam. The control returns **32,768** in both. The existing forward combining-seam tests do not exercise this backward proof. |
| **MUST-FIX · V3 cost test** | LEFT from EOF in 4,000 copies of U+00E9 at wrap width one. Replace the active row start with its display-line start in the backward probe (N3). | Both return **7,998**. The mutant consumes **8,000 decoder bytes in 10 steps** and passes the submitted suite. The control consumes **2 bytes in one step**. Both obtained one row certificate. Count decoder bytes as well as the result and certificate count. |
| **MUST-FIX · V1 contract** | On an inexact SEEK_LINE, the driver follows the header's requirement to retain a stable snapshot. Piece rank knowledge is frozen on that snapshot. | Reissuing select on that same view after another view gains exact evidence still returns WAIT. Q10 permits a fresh rank snapshot on the same text with the matching request. State which identity must stay stable and which evidence may advance. The flat test mutates `counted` in place. |
| **MUST-FIX · V1 endpoint precondition** | The header permits selection ends in [0,length] and says the walker treats an end as a cluster boundary. Supply caret=mark=1 inside the four-byte UTF-8 encoding of U+1F600. | RIGHT returns **2**, another interior byte. V3's caret-local proof requires a legal display-cluster edge. State the issuer's snap/precondition for content motion, including after edits. VP_ROW itself must accept interior query bytes. These are different domains. |

## V1 · Header-only port audit

**NOTE · present.** The header publishes S=32,768 and D=65,540. The allocator calls `viewport_motion_size()` for the required bytes and provides `max_align_t` alignment. Begin binds the length once, including zero, copies its inputs, clamps the top and may replace a motion. A zero-credit step returns MORE. A positive step updates the shared grant and writes the answer only on DONE. MORE preserves both cursors until another slice. WAIT preserves them until evidence changes and makes the driver responsible for one indexed attempt per new rank observation. Both the port and the motion can return WAIT. The preference sentinel is UINT64_MAX. The rebase rules cover before/after gravity, insertion ties, interior points, the suffix, and the old right edge of a nonempty deletion.

**NOTE · functions.** All eight functions have interface comments. Wrap default documents its path input and basename result. Rebase states representability and returns an offset. Accept checks intent serial and key, preserves both destinations on rejection and clears horizontal origin and preference on a wrap toggle. Plan checks Q8 row height and clip extent, writes a frame on success and handles an empty clip. Pixel inversion expects a planned frame and writable outputs, and preserves them outside the clip. Size, begin and step are covered above. The remaining precondition and result gaps affect interoperability.

**NOTE · evidence fields and lifetime.** WINDOW returns bytes/off/len covering the requested byte below EOF. The motion retains the bytes before another query or yield. ROW returns row and screen_column, including continuation indent. NEIGHBOR returns the clamped displaced row. COLUMN returns point on the named row. LOGICAL_RANK returns count/exact. LOGICAL_START returns off/count/exact. Value fields are consumed by value. There is no usable output on MORE or WAIT. The callback request, result storage and credits pointer are call arguments. A suspended port must retain its own request values.

**Remaining V1 gaps**

- **Row and column coordinates.** The header calls col/end_column/indent cell counts. The signed contract measures col and end_column from line_start, uses indent for synthetic cells with no bytes, and numbers ordinal from zero in the publication. Approximate applies throughout a synthetically started display line, including soft continuations. For example, a continuation at logical col 10 with indent 2 must report screen column 3 at logical col 11. Explicitly define the column of an interior scalar/cluster byte and of a byte in a terminator. LEFT at EOF after a four-byte scalar actually requests ROW at the last continuation byte. ROW also accepts EOF, including the empty terminal row. Specify the COLUMN tie at coincident zero-width boundaries. On a row beginning with U+0301 then `b`, the start and the following cluster boundary both project to column zero. The flat fixture chooses the first.
- **Certified row source.** S and D alone do not determine the A29 rows. For instance, with LF at byte 0 the grid at S is blocked. Qualify the callback as returning exactly the emitter's rows in both modes with artificial segmentation boundaries at their starts, and supply the normative construction if the header must stand alone. The shared-seam affinity, terminal sentinel, drawable end and LF/CRLF ownership are already present.
- **Validation and rank.** Require resident, validated bytes and rank evidence on the bound text and geometry. Layout owns pending I/O and backing errors, and cannot recursively start an emitter. Explain the compatible rank-only wake described above. For inexact RANK, count is a lower bound. For inexact START on trusted backing, off is the blocking prefix end and count is the exact number of LFs in [0,off), strictly below the requested line. The header's “known prefix” does not state these conditions. They justify the raw-scan shortcut in viewport.c:547.
- **Grant and lifetime details.** The byte/visit/cell charge and port ownership are public. Make the WINDOW delivery rule explicit per callback invocation. At viewport.c:268, len must fit the credits debited **on the successful call itself**. Prepaying a two-byte return across two one-credit calls and debiting one on DONE would trip the assertion. The S4 references leave out the 4,096-byte decoder ceiling and the per-slice allowance of at most one indivisible piece rank/select, O(H+K). A collapsed exact SELECT_LINE can request rank and successive selects in one step. The driver must yield or serve retained results to respect that allowance. Name the output of motion_step as a placement/selection proposal for layout and L5, and distinguish frozen text from wakeable rank evidence.
- **Input domain.** State the snapped selection precondition above, the invariant that the non-REFLOW frame and normalized origin describe the same geometry, and the snapshot length domain (the signed piece bound keeps the terminal sentinel distinct from a byte offset). Distinguish the pre-reflow saved Q8 offset from the normalized offset passed to plan. The comments otherwise cover the pointer, storage and output requirements.

## V2–V6 · Implementation

**NOTE · V2 passes.** Searching viewport.c for grid/phase arithmetic, alignment, scalar-lead probes, CR and LF found no seam computation. D sizes the summary arrays and checks the supplied scan interval. The modulus operations are pixel arithmetic. Remaining CR/LF tests classify a scalar, scan for physical SELECT_LINE boundaries, or reject ASCII proof bytes. In SEGMENT, a forward walk stops at min(row.next,length). At viewport.c:688 the last unit gets a segment-local EOF. The landed utf8_grapheme_step resets its state on END, including that EOF. A continuing run takes row.next, asks for a leading ROW there, and begins a new segment. A retained window may cover both rows. Only the current row's bytes reach the decoder. N11 specifically preserves live decoder state while still requesting the next certificate.

**NOTE · V3 proofs pass for legal endpoints.** Forward starts at the caret. A cache miss bulk-copies the paid window, and only fragmented units need the four-byte carry. Backward asks for ROW at cursor−1, so a caret at a shared seam searches the predecessor regardless of display affinity. PROBE reads down to row.byte inclusive, starting with an invalid right-byte sentinel. The first eligible pair proves the break **between** its bytes, strictly before the needed endpoint. Both are complete ASCII scalars. The landed grapheme rules classify them as Other or Control and cannot join them there. Both CR and LF are excluded, preserving GB3. A lone ASCII letter can join a preceding U+0600 Prepend. The alternate proof is the row's own start, including a forced synthetic break through a natural cluster and a soft start inside a longer display line. Retaining the right byte seeds the parse at the proved offset. Successive reverse batches end at the previous batch's start. The overlapping probe byte costs bounded work. Tests with one-credit grants exercise stitching, payment and resumption.

**NOTE · V4–V6 pass.** REFLOW calls relative_top with zero delta. It does not read target or compare widths. The issuer owns the width-change preference reset, and accept handles wrap toggles. An inexact LOGICAL_START for SEEK_LINE returns VP_WAIT even with spare credit. A WAIT from the port propagates. A later exact answer completes the same motion. Begin takes min(origin.top.byte,length) before FOLLOW dispatch. With length 1 and saved byte 2, FOLLOW returns 1. The three targeted reversions all failed the shipped suite.

## Proposed S2 bound

**MUST-FIX · revise the word rows or change the stopping rule.** LEFT and RIGHT's segmentation bounds follow for legal starts, with O(1) selection collapse. RIGHT needs at most one complete scalar beyond the accepted cluster. LEFT searches to the nearest eligible proof before the endpoint and parses forward to the endpoint. The signed O(D) worst case is attainable. Window delivery and retention must be accounted for separately when quoting those bounds as total byte work.

For WORD_RIGHT and SELECT_WORD the first scalar gives the cluster class at viewport.c:686–687, but the class decision waits for UTF8_G_END at :691–694. In the first finding the true endpoint is one byte away, and the rejected cluster is 8,001 bytes long. The decoder then examines the next scalar for its break. A bound for the present code includes **the whole first different-class cluster plus a scalar of lookahead** on the forward side. Alternatively, make the class decision on its first scalar where only its start is needed.

For WORD_LEFT in the same 8,003-byte input, motion from EOF to 8,002 consumed **8,002 decoder bytes**. The rejected punctuation cluster starts at byte 1, itself a proved ASCII-pair boundary. Counting displacement plus the distance from that cluster's *start* to the proof misses its full length. For WORD_LEFT and the reverse half of SELECT_WORD, measure from the destination back to the start of the final parsed batch. This includes any rejected cluster and any prefix needed to prove its start. A coarse bound of O(displacement + D) for these word walks remains valid for segmentation and summary visits. The reverse batches partition the inspected interval up to bounded probe overlap. D entries suffice for each batch. Add F for placement.

Report delivered/copied bytes separately from decoder advance. With a 4,096-credit grant the control consumed two decoder bytes on the one-cell é fixture and retained a 3,905-byte window. The public port can return a covering slab beginning before the request. The decision's qualification about extra delivered bytes matters for the byte-work claim.

## Benchmark

**NOTE · the three fixtures and both clocks check out.** I traced the first successful ROW query through the benchmark callback. The 161-byte input queried 160 and got [81,161), the second 80-byte content row. The paragraph queried 2,999 and got [0,3000). The explicit conservative-D fixture queried 65,539 and got [0,65540). The last is a manufactured certificate for the provider limit. Each ends in a space and three ASCII letters. LEFT moves one byte, WORD_LEFT three, and all six cases completed in one positive 4,096-credit step. Construction and warmup precede timing. The motion pass times begin through DONE. A separate pass times each positive step including callback service. Sol's p50/p99 columns each have 1,000 observations per fixture/action. Those timings characterize local ASCII work on the precomputed port. The rejected combining-cluster witness uses a different workload.

## Mutation audit

Each build changes one behavior relative to afff755. M1–M5 now change the flat port's A29 constructor because seam construction left viewport. The assertions and motion code for those five are unchanged. The other builds use an unchanged ASan/UBSan test object, reviewed L1 decoder and isolated viewport source. The unmodified control passed. “Caught” identifies an executed suite check except where the watchdog is explicitly named. Independent probes for survivors were run separately.

| ID | Mutation | Submitted unit suite / observed check |
|---|---|---|
| M1 | Ignore hard-start LF blocking at the grid | Caught, expected three rows and shifted successor at :149 |
| M2 | Measure the exclusion distance from every start, including a shifted synthetic start | Caught, RIGHT 65,538 instead of 65,536 |
| M3 | Cut at the nominal grid through an overlapping valid scalar | Caught, shifted successor at :149 |
| M4 | Probe only g−1 for an overlapping lead | Caught, RIGHT 32,766 instead of 32,769 |
| M5 | Remove CRLF seam coalescing | Caught, end/successor/hard-start assertion at :179 |
| M6 | Force raw fallback after exact SELECT_LINE rank | Caught, nonzero window bytes at :84 |
| M7 | Remove reverse-summary debit | Caught, reverse-only payment at :512 |
| M8 | Treat a nonempty deletion's old right edge as consumed | Caught, before-gravity result at :287 |
| M9 | Ignore frame origin in pixel inversion | Caught, first pixel's within-row offset at :277 |
| M10 | Ignore usable select prefix | Caught, three-byte suffix bound at :92 |
| M11 | Restart SELECT_WORD's left side one byte inside the synthetic row and request it again | Caught, two-certificate assertion at :172 |
| M12 | Latch column zero | Caught, caret 0 instead of 4 |
| M13 | Preserve remainder on an overshooting relative neighbor | Caught, expected (0,0) at :444 |
| M14 | Accept stale geometry | Caught, rejection at :566 |
| M15 | Send INT64_MIN for PageUp's top operand at BOF | Caught, requested displacement at :587 |
| Sol E5 | Read count in place of screen_column | Caught, caret 0 instead of 4 |
| V4 | Reintroduce target/width comparison in REFLOW | Caught, supplied column 7 at :128 |
| V5 | Return MORE on inexact SEEK_LINE | Caught, expected WAIT at :121 |
| V6 | Bypass begin's top clamp for FOLLOW | Caught, expected byte 1 at :725 |
| N1 | Extend forward parse past row.next toward document end, capped at D | Caught, RIGHT 32,770 instead of 32,768 |
| N2 | Extend parse one byte into a successor when wrapping | Caught, three soft-row certificates at :664 |
| N3 | Backward proof at line_start instead of soft row.byte | **Survived**. Independent one-cell-wrap cost probe consumed 8,000 bytes instead of 2 |
| N4 | Let backward search run before a synthetically started row, using BOF as fallback | **Survived**. Independent combining-fold probe returned S−1 instead of S, in clip and wrap |
| N5 | Take the right ASCII byte alone as proof | Caught, U+0600 plus a letter gave 2 instead of 0 |
| N6 | Admit CR and LF to the ASCII-pair proof | Caught, LEFT into CRLF gave 11 instead of 10 |
| N7 | Seed PROBE with a fictitious ASCII right byte, permitting proof at cursor | Caught by 20-second watchdog. On `ab`, LEFT from EOF also returned MORE on all 32 consecutive 32-credit calls. The control completed the first call at byte 1. The empty batch repeats at the unchanged endpoint |
| N8 | Start one byte beyond the certified row-start proof if possible | Caught, reverse-only summary count at :512 |
| N9 | Discard every ASCII-pair proof and parse from row start | Caught, window-byte bound at :158 |
| N10 | Start one byte past a real ASCII-pair proof when still before cursor | Caught, caret 11 instead of 10. Separate LEFT on `ab` + U+0301 returned 3 instead of 1 |
| N11 | Reacquire the successor but carry an unfinished grapheme across next | Caught, combining-fold RIGHT 32,770 instead of 32,768 |

Of 30 mutant builds, 27 failed assertions, one lost progress and two passed the unit suite. Both survivors failed independent checks. The flat oracle's grid results establish viewport behavior given those certificates. Production layout needs its own constructor coverage.

**NOTE · fixture only.** On an empty terminal row (for example, the successor in `a\n`), flat_column_query immediately returns row.byte with its positive grant untouched. The header charges one credit for a bounded metadata answer. The benchmark uses LEFT and WORD_LEFT, so its timers never exercise that discrepancy. The fixture also calls its delay state `VIEWPORT_DISPLAY_MAXELAYED`, an accidental rename visible in this diff.

**NOTE · out of scope.** Actual layout placement F, certification cost, rank wake delivery, reader validation/cancellation, paint, accepted frames and end-to-end G1/G5 are integration work. The benchmark reserves a precomputed port and does not time those operations. No fault subject was exercised.

The findings, V1 gaps, S2 verdict and mutation table above were recorded before the final focused lane run.

## Final focused verification

**NOTE · results.** The unmodified Clang ASan/UBSan viewport unit suite printed `viewport_test: ok`. The viewport fuzzer replayed the four shipped seeds, then completed a separate 5,376-run campaign with seed 1. Both used the project's sanitizer/fuzzer objects and were linked directly to the reviewed viewport object and text/core libraries. I stopped the two unrelated LaTeX grammar compilations still pending in the aggregate Makefile link. The release Makefile benchmark completed all six rows, with 1,000 motion and 1,000 positive-step samples per row.

| Fixture | Action | Whole motion p50 / p99 (ns) | Positive step p50 / p99 (ns) |
|---|---|---:|---:|
| 80-byte lines | LEFT | 204 / 255 | 156 / 233 |
| 80-byte lines | WORD_LEFT | 430 / 501 | 391 / 497 |
| 3,000-byte paragraph | LEFT | 306 / 330 | 259 / 298 |
| 3,000-byte paragraph | WORD_LEFT | 539 / 586 | 496 / 543 |
| 65,540-byte certificate | LEFT | 204 / 216 | 156 / 180 |
| 65,540-byte certificate | WORD_LEFT | 441 / 491 | 396 / 442 |

These independent timings were collected during other compilation. They verify the measurement paths and sample counts. No integrated latency claim relies on them.
