# v2-1sg round-four report

This round implements E9 and E10, documents same-text rank wakes and interior-byte column evidence, and adds the surviving proof-mutant witnesses. The signed A29 row construction stays in the provider. [Decision record and S2 bounds](v2-1sg.md)

## Red and green

| Item | Red | Green |
|---|---|---|
| 1. E9 cluster admission | On `a.` + 4,000 copies of U+0301 + `z`, round three consumed 8,002 grapheme-decoder bytes in each action. The exact one-byte assertions failed. [WORD_RIGHT](../../build/viewport-r4/baseline-rejected-word.log) · [SELECT_WORD](../../build/viewport-r4/baseline-rejected-select.log) | Both actions finish at byte 1 and consume exactly one accepted decoder byte. SELECT_WORD also leaves its mark at byte zero. The punctuation is classified at its start. |
| 2. N4 synthetic-row proof | The BOF-proof mutant returned 32,767 for LEFT at EOF after S ASCII bytes and U+0301 in both clip and 80-cell wrap. [Clip](../../build/viewport-r4/red-N4-synthetic-clip.log) · [Wrap](../../build/viewport-r4/red-N4-synthetic-wrap.log) | Both modes assert caret 32,768, two decoder bytes, and one row certificate. |
| 3. N3 active-row proof | The display-line-start mutant consumed 8,000 decoder bytes for LEFT in 4,000 U+00E9 at wrap width one. [Red](../../build/viewport-r4/red-N3-active-proof.log) | The caret is 7,998, with two decoder bytes and one row certificate asserted. |
| 4. Rank wake | The negative driver answered from the same frozen inexact view and failed the DONE assertion. [Red](../../build/viewport-r4/red-frozen-rank-rank-wake.log) | The test observes WAIT with one old lookup, releases the old provider, and completes the same intent with one lookup on a separate view of the same text with exact rank. The header specifies this Q10 handoff. |
| 5. E10 interior offsets | Round three returned byte 2 from caret 1 in U+1F600 and from the caret after pasting E2 before 82 AC. [Emoji](../../build/viewport-r4/baseline-emoji-endpoint.log) · [Paste](../../build/viewport-r4/baseline-paste-endpoint.log) | The corresponding right edges are 4 and 3. LEFT and WORD_LEFT reach zero, and WORD_RIGHT reaches the right edge. The tests use one-byte windows and one-credit slices. Additional cases verify moving past the normalized edge, directional collapse and preserving stray continuations. |

The header defines interior-byte projection, terminator and EOF column evidence, the display-to-screen offset and the zero-width COLUMN tie. Tests exercise each of those lookups, and the flat port charges the empty terminal COLUMN answer. The focused suite passed. N3 and N4 each failed again against the final implementation, and the frozen-rank driver remained in WAIT. The revision also matches round three on 31,988 comparisons at legal cluster endpoints, and the seed-1 fuzz run completed 5,376 executions (21 actions × the 256 possible byte selector values as a run count). [Focused green](../../build/viewport-r4/green-suite.log) · [Final mutant receipts](../../build/viewport-r4/final-mutants.log) · [Differential](../../build/viewport-r4/differential.log) · [Generated fuzz](../../build/viewport-r4/generated-fuzz.log)

## Release benchmark

The metered flat port precomputes rows. Ordinary fixtures retain the three ASCII cases with a short word at the end. The rejected cluster is Astra's exact 8,001-byte punctuation cluster. The accepted cluster is a full conservative-D certificate of 65,540 bytes, filled with four-byte U+E0100 units and containing no ASCII proof. The whole-motion and step timers run separate passes at the 4,096-credit grant. Each row has 1,000 whole-motion samples, giving ten observations in the upper 1% tail, and 1,000 times the indicated positive steps.

| Fixture | Action | Steps per operation | Operation p50 / p99 (ns) | Step p50 / p99 (ns) |
|---|---|---:|---:|---:|
| 80-byte lines | LEFT | 1 | 320 / 385 | 234 / 274 |
| 80-byte lines | WORD_LEFT | 1 | 606 / 753 | 564 / 680 |
| 80-byte lines | WORD_RIGHT | 1 | 604 / 661 | 518 / 578 |
| 80-byte lines | SELECT_WORD | 1 | 706 / 784 | 604 / 670 |
| 3,000-byte paragraph | LEFT | 1 | 393 / 641 | 284 / 472 |
| 3,000-byte paragraph | WORD_LEFT | 1 | 648 / 823 | 607 / 708 |
| 65,540-byte certified row | LEFT | 1 | 287 / 372 | 220 / 267 |
| 65,540-byte certified row | WORD_LEFT | 1 | 617 / 739 | 517 / 662 |
| 8,001-byte rejected cluster | WORD_RIGHT | 2 | 557 / 792 | 259 / 350 |
| 8,001-byte rejected cluster | SELECT_WORD | 2 | 561 / 662 | 220 / 327 |
| 65,540-byte accepted cluster | LEFT | 71 | 2,031,251 / 31,972,524 | 40,340 / 70,527 |
| 65,540-byte accepted cluster | WORD_LEFT | 80 | 2,252,667 / 2,533,383 | 25,613 / 96,327 |
| 65,540-byte accepted cluster | WORD_RIGHT | 39 | 1,463,666 / 27,520,860 | 19,265 / 105,326 |
| 65,540-byte accepted cluster | SELECT_WORD | 39 | 1,434,571 / 6,458,911 | 16,653 / 88,333 |

Measured (M) with AC online and BAT0 reporting Not charging, load1 20.06 → 20.15. The host was busy. Long operations include time between slices. These measurements include the metered port and exclude constructing its directory. The production layout and reader have separate integration timing. [Benchmark receipt](../../build/viewport-r4/bench.log)

## Footprint

The round adds 457 and removes 118 lines across source, header, tests and benchmark (M, line diff against round three, excluding these records). The C source adds 130 and removes 32 raw lines. The comment-free counts (M, GCC preprocessed nonblank) are 758 for viewport.c and 92 for viewport.h, totaling 850 versus 759 in round three.

## Final lanes

| Command | Result |
|---|---|
| `nice make -j2 check CONTRACT_CHECK_DISPLAYS=:180-:189` | Exit 0. Viewport export and dependency gates, six isolated layer links, 33 ASan/UBSan binaries, eight Law 2 suites and all 64 contract harness outcomes passed. [Log](../../build/viewport-r4/final-check.log) |
| `nice make -j2 check-tsan` | Exit 0. All 13 suites passed, including viewport. [Log](../../build/viewport-r4/final-tsan.log) |
| `nice make -j2 fuzz-smoke` | Exit 0. All 16 fuzzers replayed their seeds, including viewport. [Log](../../build/viewport-r4/final-fuzz.log) |

## Round-four recorded mutant rerun

The controls passed viewport_test and the viewport seed smoke. Each row below rebuilt one mutant in isolation and ran both commands. N7 uses the same 20-second progress watchdog as the re-review. M1 through M5 now mutate the independent flat constructor in copied test and fuzz sources because the production seam rule left viewport. M11 uses the normalized walk origin and forces the old redundant query; M13 uses the current relative-scroll clamp, with the narrower round-one and wider round-three variants both exercised. N11 seeks the successor with live grapheme state. Repeated M1–M15 and E5 entries across the two reviews share a run except for the distinct M13 variants. All copies and receipts are under build/viewport-mutants. A final cmp verified that src/viewport/viewport.c is byte-identical to the round-four input.

| Mutant | Semantic change | viewport_test | Seed smoke | Result |
|---|---|---|---|---|
| M1 | Ignore LF blocking at a hard start | [unit: build/viewport-mutants/M1/tests/viewport_test.c:141: void seams(viewport_motion *): Assertion `f.rows == 3 && f.row[1].byte == 1 && f.row[1].next == seam' failed.](../../build/viewport-mutants/M1/unit.log) | Pass | Killed |
| M2 | Measure synthetic grid spacing from the shifted start | [unit: build/viewport-mutants/M2/tests/viewport_test.c:32: void selection(viewport_answer, uint64_t, uint64_t, _Bool): Assertion `a.selection.caret == caret' failed.](../../build/viewport-mutants/M2/unit.log) | Pass | Killed |
| M3 | Cut overlapping scalars at the nominal grid | [unit: build/viewport-mutants/M3/tests/viewport_test.c:141: void seams(viewport_motion *): Assertion `f.rows == 3 && f.row[1].byte == 1 && f.row[1].next == seam' failed.](../../build/viewport-mutants/M3/unit.log) | Pass | Killed |
| M4 | Use only the last candidate lead | [unit: build/viewport-mutants/M4/tests/viewport_test.c:32: void selection(viewport_answer, uint64_t, uint64_t, _Bool): Assertion `a.selection.caret == caret' failed.](../../build/viewport-mutants/M4/unit.log) | Pass | Killed |
| M5 | Omit CRLF coalescence | [unit: build/viewport-mutants/M5/tests/viewport_test.c:171: void seams(viewport_motion *): Assertion `f.row[0].end == VIEWPORT_GRID_BYTES - 1 && f.row[1].byte == VIEWPORT_GRID_BYTES + 1 && !f.row[1].approximate' failed.](../../build/viewport-mutants/M5/unit.log) | Pass | Killed |
| M6 | Take raw fallback after exact rank | [viewport_test.c:84: void actions(viewport_motion *): Assertion `f.bytes == bytes' failed.](../../build/viewport-mutants/M6/unit.log) | Pass | Killed |
| M7 | Omit reverse-summary credit | [viewport_test.c:504: void reverse_review(viewport_motion *): Assertion `after_decode >= f.length / (sizeof "é" - 1)' failed.](../../build/viewport-mutants/M7/unit.log) | Pass | Killed |
| M8 | Treat old deletion right edge as consumed | [viewport_test.c:279: void frame_math(void): Assertion `viewport_rebase(7, 5, 2, 1, false) == 6' failed.](../../build/viewport-mutants/M8/unit.log) | Pass | Killed |
| M9 | Ignore the Q8 origin | [viewport_test.c:269: void frame_math(void): Assertion `viewport_pixel_row(&f, 0, &row, &within) && row == 0 && within == 129' failed.](../../build/viewport-mutants/M9/unit.log) | Pass | Killed |
| M10 | Ignore useful prefix from inexact select | [viewport_test.c:92: void actions(viewport_motion *): Assertion `f.bytes - bytes == 3' failed.](../../build/viewport-mutants/M10/unit.log) | Pass | Killed |
| M11 | Restart Select Word inside the synthetic row and re-query it | [viewport_test.c:164: void seams(viewport_motion *): Assertion `f.calls[VP_ROW] - rows == 2' failed.](../../build/viewport-mutants/M11/unit.log) | Pass | Killed |
| M12 | Latch column zero | [viewport_test.c:32: void selection(viewport_answer, uint64_t, uint64_t, _Bool): Assertion `a.selection.caret == caret' failed.](../../build/viewport-mutants/M12/unit.log) | Pass | Killed |
| M13-r1 | Preserve remainder on negative scroll starting at BOF | [viewport_test.c:440: void scroll_review(viewport_motion *): Assertion `a.top.byte == 0 && a.top.subrow_q8 == 0' failed.](../../build/viewport-mutants/M13-r1/unit.log) | Pass | Killed |
| M13-r3 | Preserve remainder on any overshooting relative neighbor | [viewport_test.c:436: void scroll_review(viewport_motion *): Assertion `a.top.byte == 0 && a.top.subrow_q8 == 0' failed.](../../build/viewport-mutants/M13-r3/unit.log) | Pass | Killed |
| M14 | Accept stale geometry | [viewport_test.c:558: void accept_review(void): Assertion `!viewport_accept(&state, &sel, &in, &a)' failed.](../../build/viewport-mutants/M14/unit.log) | Pass | Killed |
| M15 | Request INT64_MIN for PageUp top at BOF | [viewport_test.c:579: void page_review(viewport_motion *): Assertion `f.neighbors[i].as.neighbor.displacement == -(int64_t)frame.layout_rows' failed.](../../build/viewport-mutants/M15/unit.log) | Pass | Killed |
| E5-column | Use count as the source column | [viewport_test.c:32: void selection(viewport_answer, uint64_t, uint64_t, _Bool): Assertion `a.selection.caret == caret' failed.](../../build/viewport-mutants/E5-column/unit.log) | Pass | Killed |
| V4 | Reintroduce REFLOW target/width comparison | [viewport_test.c:120: void actions(viewport_motion *): Assertion `a.top.byte == 18 && a.top.subrow_q8 == 17 && a.selection.preferred_col == 7' failed.](../../build/viewport-mutants/V4/unit.log) | Pass | Killed |
| V5 | Yield MORE on inexact seek | [viewport_test.c:801: void newer_rank(viewport_motion *): Assertion `viewport_motion_step(m, &port, &credit, &a) == VP_WAIT' failed.](../../build/viewport-mutants/V5/unit.log) | Pass | Killed |
| V6 | Skip the FOLLOW top clamp | [viewport_test.c:709: void follow_clamp(viewport_motion *): Assertion `a.top.byte == 1' failed.](../../build/viewport-mutants/V6/unit.log) | Pass | Killed |
| N1 | Parse to document end, capped at D | [viewport_test.c:32: void selection(viewport_answer, uint64_t, uint64_t, _Bool): Assertion `a.selection.caret == caret' failed.](../../build/viewport-mutants/N1/unit.log) | Pass | Killed |
| N2 | Parse one byte into a wrapped successor | [viewport_test.c:656: void port_rows(viewport_motion *): Assertion `f.calls[VP_ROW] == 3' failed.](../../build/viewport-mutants/N2/unit.log) | Pass | Killed |
| N3 | Use display-line start for reverse proof | [viewport_test.c:781: void active_row_proof(viewport_motion *): Assertion `consumed == sizeof letter - 1' failed.](../../build/viewport-mutants/N3/unit.log) | Pass | Killed |
| N4 | Use BOF proof for synthetic rows | [viewport_test.c:32: void selection(viewport_answer, uint64_t, uint64_t, _Bool): Assertion `a.selection.caret == caret' failed.](../../build/viewport-mutants/N4/unit.log) | Pass | Killed |
| N5 | Use a lone ASCII byte as proof | [viewport_test.c:32: void selection(viewport_answer, uint64_t, uint64_t, _Bool): Assertion `a.selection.caret == caret' failed.](../../build/viewport-mutants/N5/unit.log) | Pass | Killed |
| N6 | Allow CR and LF in the pair | [viewport_test.c:32: void selection(viewport_answer, uint64_t, uint64_t, _Bool): Assertion `a.selection.caret == caret' failed.](../../build/viewport-mutants/N6/unit.log) | Pass | Killed |
| N7 | Preseed a fictitious ASCII byte | [watchdog](../../build/viewport-mutants/N7/unit.log) | Pass | Killed |
| N8 | Skew certified row-start proof right | [viewport_test.c:504: void reverse_review(viewport_motion *): Assertion `after_decode >= f.length / (sizeof "é" - 1)' failed.](../../build/viewport-mutants/N8/unit.log) | Pass | Killed |
| N9 | Disable the ASCII proof | [viewport_test.c:150: void seams(viewport_motion *): Assertion `f.bytes - bytes <= VIEWPORT_DISPLAY_MAX + 2' failed.](../../build/viewport-mutants/N9/unit.log) | Pass | Killed |
| N10 | Skew a genuine ASCII pair proof right | Pass | Pass | Survived |
| N11 | Re-query the successor while retaining an unfinished cluster | [viewport_test.c:32: void selection(viewport_answer, uint64_t, uint64_t, _Bool): Assertion `a.selection.caret == caret' failed.](../../build/viewport-mutants/N11/unit.log) | Pass | Killed |

Thirty of 31 mutants were killed. N10 survived the full deterministic suite and the four-seed smoke. Every seed replay passed, including on the mutants that the unit suite killed.
