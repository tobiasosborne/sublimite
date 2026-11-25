# v2-1sg owner send-back report

This round integrates signed E7/E8 and makes layout the sole source of row-end and seam evidence. The published API documents cursor allocation, callback inputs and outputs, the two continuation statuses, and credit ownership. The decision record includes the proposed S2 bound text and carries forward E5/E6. [Decision record](v2-1sg.md)

| Item | Red | Green |
|---|---|---|
| V1. Interface and shared constants | Source contract review. | All functions document inputs, returned values and output timing. Requests list the populated fields and ownership of bytes and credits. S and D have one public definition used by implementation, tests, fuzz and bench. The implementation explicitly includes string and ASSERT declarations. |
| V2. Certified seams | A word split by two soft wraps used one row lookup and failed the three-certificate assertion. [Red](../../build/viewport-owner/red-rows.log) | The walker gets all three certificates. The independent flat oracle exercises shifted, CRLF, invalid and blocked A29 seams. Viewport has no grid arithmetic or alignment phase. |
| V3. Local arrow work | Deep LEFT failed the decoder-byte assertion at the byte-60,000 witness. [Red](../../build/viewport-owner/red-arrows.log) | LEFT and RIGHT each consume one decoded byte there. All five local motions stay within two four-byte fixture windows. The Unicode Prepend test requires the row-start proof for lone ASCII and the nearer break for an ASCII pair. Fragmented decoding and reverse-credit tests pass. |
| V4. E7 | Height-only REFLOW with a non-width target failed to preserve column 7. [Red](../../build/viewport-owner/red-reflow.log) | REFLOW preserves a supplied column and an issuer-cleared preference. Wrap-toggle acceptance retains its existing reset. |
| V5. E8 | Inexact SEEK_LINE returned VP_MORE and failed the required third-status assertion. [Red](../../build/viewport-owner/red-wait.log) | It returns VP_WAIT with positive credit and output untouched. Independent VP_ROW and VP_WINDOW stalls also propagate WAIT and resume after new evidence. |
| V6. FOLLOW clamp | A saved top beyond snapshot length passed through FOLLOW and failed the clamp assertion. [Red](../../build/viewport-owner/red-follow.log) | Begin clamps the saved top for FOLLOW as for every other action. |

The full focused ASan/UBSan suite passed with the revised cursor. A separate differential run agreed with the pre-review cursor on 31,988 legal-endpoint comparisons over fixed generated ASCII, malformed, newline and Unicode inputs in clip and wrap. The count is the actual set of legal endpoints × tested actions. Generated fuzzer coverage ran for 5,376 executions (21 action tags × 256 byte values as an iteration count). [Focused green](../../build/viewport-owner/green-suite.log) · [Differential](../../build/viewport-owner/differential.log) · [Generated fuzz](../../build/viewport-owner/generated-fuzz.log)

## Release benchmark

All motions start at the final byte boundary. The 80-byte fixture has two content rows with an LF between them. The paragraph has 3,000 bytes. The largest fixture supplies an explicit single 65,540-byte certificate at conservative D to exercise the provider bound. The flat oracle's naturally aligned rows are shorter. The fixtures end with an ASCII three-letter word after a space. Both LEFT and nearby WORD_LEFT complete in one positive 4,096-credit step on each fixture. Flat rows and column caches are built before timing.

| Fixture | Action | Motion p50 ns | Motion p99 ns | Step p50 ns | Step p99 ns |
|---|---|---:|---:|---:|---:|
| 80-byte lines | LEFT | 196 | 215 | 150 | 170 |
| 80-byte lines | WORD_LEFT | 422 | 465 | 378 | 501 |
| 3,000-byte paragraph | LEFT | 296 | 316 | 250 | 273 |
| 3,000-byte paragraph | WORD_LEFT | 512 | 586 | 462 | 524 |
| 65,540-byte certified row | LEFT | 195 | 221 | 149 | 174 |
| 65,540-byte certified row | WORD_LEFT | 422 | 471 | 380 | 432 |

Measured (M) with BAT0 Charging [AC] and load1 20.46 → 20.46. The host was busy. Each row uses 1,000 complete-motion samples and 1,000 separately timed positive steps, giving ten observations in the upper 1% tail. The benchmark exited 0. The short local motions and precomputed port measure cursor work. Integrated certification, real reader service and the production input-to-submit budget have their own evidence. [Benchmark receipt](../../build/viewport-owner/bench.log)

## Footprint

This owner round added 523 lines and removed 268 across implementation, header, tests, fuzz and benchmark (M, line diff against the pre-owner checkout, excluding these records). The viewport C source adds 201 and removes 202 raw lines. The C source has 667 comment-free lines, down from 668, and the header has 92, up from 91 (M, GCC preprocessed nonblank count). The total is 759 versus the S3 estimate of 700 and the v1 harvest footprint of 2,775.

## Final lanes

| Command | Result |
|---|---|
| `nice make -j2 check CONTRACT_CHECK_DISPLAYS=:180-:189` | Exit 0. Viewport exports and all six isolated links passed. The 33 ASan/UBSan binaries and eight Law 2 suites passed, and the contract harness recorded 64 PASS outcomes. [Log](../../build/viewport-owner/final-check.log) |
| `nice make -j2 check-tsan` | Exit 0. All 13 suites passed, including viewport. [Log](../../build/viewport-owner/final-tsan.log) |
| `nice make -j2 fuzz-smoke` | Exit 0. All 16 fuzzers replayed their seeds, including viewport. [Log](../../build/viewport-owner/final-fuzz.log) |
