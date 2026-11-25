# v2-1sg implementation and send-back report

Viewport implements every S2 action, resumable motion and selection, anchor rebase, affinity, and Q8 planning and inversion. The send-back applies the four behavioral fixes and signed E5/E6. The public header changes only for E5. The decision record describes the viewport and layout boundary, including how the driver conveys previous width and suppresses same-observation piece work during a SEEK_LINE wait. [Decisions](v2-1sg.md)

## Red and green receipts

| Item | Red | Green |
|---|---|---|
| 1. Clip Home | Before the fix, Home from EOF in S+1 ASCII bytes failed the caret assertion. [Red](../../build/viewport-review/red-home.log) | The caret lands at S. The aligned long-span fixture also checks that Home reads only its active row and issues no rank/select. |
| 2. Reflow preference | The original height-only reflow failed the assertion for retained column 7. [Red](../../build/viewport-review/red-reflow.log) | Same-width reflow keeps column 7. Width change clears it, and wrap-toggle acceptance clears both preference and hscroll. |
| 3. Q8 clamp | In the review's three-line text, the in-range −2,559 movement passed, then −5,119 failed the zero-remainder assertion. [Red](../../build/viewport-review/red-scroll.log) | The positions are (0, 1) and (0, 0). The fixture also checks negative movement from BOF and landing exactly on or overshooting the terminal row. |
| 4. Decoder debit | The test captured a 16,384-byte advance on a 4,096-credit step. [Red](../../build/viewport-review/red-decoder.log) | The same U+E0100 fixture advances at most 4,096 bytes per step and consumes the full S-byte cluster. |
| 5. Reverse debit | M7 removes the debit and fails the reverse-only accounting assertion. | The fixture counts the paid visits after the last decoder call under one-credit steps. |
| 6. Requested page displacement | M15 changes the PageUp top request to INT64_MIN and fails the displacement assertion. | Caret and top each request −R, including when the top at BOF stays put. The two operands are checked independently across simulated stalls. |
| 7. Additional seams | M2 yields 65,538 and M4 yields 32,766 on the review inputs. | RIGHT yields 65,536 at the second grid and 32,769 from the scalar at g−3. The suite also covers g−2. |
| 8. Stale geometry | M14 fails the rejection assertion. | Each of buffer, text, geometry and intent serial is made stale independently. Rejection preserves both installed outputs. |
| E5. Snapshot and column evidence | The empty test found the original metadata query. The E5-column mutant using `count` reaches byte 0 instead of byte 3. [Empty red](../../build/viewport-review/red-empty.log) | Empty DOC_END and SELECT_ALL use zero port queries. The continuation fixture sees a source column of 3 including one synthetic indent cell while `count` is zero. |
| E6. Visual Home | Covered by item 1. | Ordinary Home uses the active visual row in clip and wrap. Piece rank/select is a SELECT_LINE path. |

The focused ASan/UBSan suite passes with the unmodified implementation. It reuses a cursor abandoned during segmentation, then compares every answer field against a fresh cursor and provider on a different motion. SELECT_LINE also reuses one inexact rank for a collapsed selection. The generated fuzzer completed 5,376 executions, using the 21 action tags × 256 byte values as an iteration count. [Focused green](../../build/viewport-review/green-suite.log) · [Generated fuzz](../../build/viewport-review/generated-fuzz.log)

## Mutation table

Each mutant changes only viewport.c and links against the same ASan/UBSan test object and L1 library. All fifteen review mutations and the added E5 carrier mutation reached an assertion. M13 targets the new shared clamp because the old BOF special case was removed. M15 changes only the requested displacement. [Mutant red receipts](../../build/viewport-review/mutants.log)

| Mutant | Assertion that rejects it |
|---|---|
| M1 | Hard LF blocks the first grid and the following combining mark still joins its base. |
| M2 | Shifted synthetic seam keeps the next nominal grid phase. |
| M3 | Nominal cut cannot split an overlapping scalar. |
| M4 | A strict lead at g−3 remains whole. |
| M5 | CRLF crossing a grid stays one terminator. |
| M6 | Exact SELECT_LINE queries require zero raw window bytes. |
| M7 | Reverse visits debit each summary after decoding ceases. |
| M8 | The old right edge of a deletion travels with the suffix. |
| M9 | Pixel inversion includes the Q8 origin. |
| M10 | A usable select prefix limits the raw suffix scan to three bytes on the fixture. |
| M11 | Select Word requests each of its two display lines once. |
| M12 | Vertical motion latches the indented screen column. |
| M13 | Scroll overshoot sets the remainder to zero. |
| M14 | The geometry-stale answer is rejected. |
| M15 | The top PageUp request is within the same R bound as its caret operand. |
| E5-column | The port's screen column is used while its unrelated rank count is zero. |

## Benchmark

The fixture precomputes source columns, cluster edges, rows and ranks outside the measurement. Window bytes, destination-column visits and metadata answers are metered. A separate test runs a four-cluster destination lookup with one credit per callback and observes one visit each. The timed motion is word-left over S+1 ASCII bytes. One motion took 49 positive-credit steps at a grant of 4,096. The motion and step passes time separately to keep the step timers out of the motion measurement.

| Metric | Samples | p50 | p99 |
|---|---:|---:|---:|
| Whole motion | 1,000 | 4,321,373 ns | 60,952,356 ns |
| Positive-credit step | 49,000 | 66,395 ns | 160,760 ns |

Measured (M) with BAT0 Charging [AC] and load1 20.23 → 19.13. The sample counts are 1,000 motions, giving ten observations in the upper 1% tail, and all 49 steps per motion in the separate step pass. The benchmark exited 0. The step result measures this flat port under the recorded load. Production G1 and reader, layout and render costs remain integration measurements. [Benchmark receipt](../../build/viewport-review/bench.log)

## Footprint

The send-back adds 519 lines and removes 107 across code, tests, benchmark and Makefile (M, diff against the pre-review checkout). Across the bead those files and the original build/tool changes add 1,944 lines and remove 15 (M, against the pre-bead checkout). Both counts exclude the decision/report documents and binary seed corpus.

GCC preprocessing with comments and blank lines stripped counts 668 lines in viewport.c and 91 in viewport.h (M). The subtotal of 759 is 59 above the S3 estimate of 700 and 2,016 below the v1 harvest footprint of 2,775.

## Final lanes

| Command | Result |
|---|---|
| `nice make -j2 check CONTRACT_CHECK_DISPLAYS=:180-:189` | Exit 0. Viewport export gate passed, all six isolated layer links passed, all 33 sanitizer binaries and all eight Law 2 suites passed, and the contract harness recorded 64 PASS outcomes. Existing out-of-scope export diagnostics were piece 12, undo 17 and journal 28. [Log](../../build/viewport-review/final-check.log) |
| `nice make -j2 check-tsan` | Exit 0. All 13 suites passed, including viewport. [Log](../../build/viewport-review/final-tsan.log) |
| `nice make -j2 fuzz-smoke` | Exit 0. All 16 fuzzers replayed their seeds, including viewport. [Log](../../build/viewport-review/final-fuzz.log) |
