# v2-1sg implementation report

Implemented the signed L4 viewport header and motion cursor, rebase, wrap defaults, acceptance, frame planning and pixel inversion. The viewport calls the tagged provider for source and row evidence. The flat provider models grid and physical boundaries independently, supplies simple wrap certificates for motion, meters requests and bytes, and can delay each request. The decision record lists the seven contract questions, the implementations chosen and their counterexamples. [Decisions](v2-1sg.md)

## Coverage

- The deterministic suite exercises every S2 action, ordinary and Shift selection, repeated and exclusive-end Select Line, exact and inexact rank/select, physical smart Home, clipped and wrapped row affinity, and Q8 remainder and pixel mapping.
- S1 witnesses cover the 2S + 2 span, four-byte alignment across tiny windows, CRLF coalescence, stray continuation runs, combining clusters split at a seam, and suppressed EOF grid starts. Word motion and selection cross synthetic seams. The suite checks requested bytes against the S1 span and grant, and PageUp hops against R per operand.
- R1/R2/R24 are witnessed at the viewport output by the rebased top and distinct committed caret across supersession. R6 uses a delayed flat provider. R7 names an EOF caret and Select All on the S4 10 GiB length-only model with zero text bytes. R8 exercises cold PageUp with counted predecessor displacement. R25 checks untouched output during MORE and rejected stale answers. S9 compares a reused cursor with a fresh cursor and provider.
- The fuzzer compares wide grants and independent tiny-grant visibility, delayed requests and rank fallback. It also checks horizontal and whole-line motion against the flat oracle. A development run completed 5,376 executions (21 action tags × 256 byte values as a run count).
- The release benchmark ran against the flat port for a word-left move across S + 1 bytes (A29 plus one byte beyond the seam). It printed p50 747,421 ns and p99 820,424 ns and exited 0 (M, BAT0 reported Charging, load1 0.82 → 0.84). Percentile indices use nearest rank over 1,000 samples, giving ten samples in the upper 1% tail.

## Footprint

The code, tests and build/tool integration add 1,532 lines and remove 15 (M, line diff against the checkout's original files, counting inserted and removed lines separately, excluding these decision/report documents and the binary corpus). The four seeds total 49 bytes (M, file sizes). The only edits at existing callers replace the eliminated placeholder in the app and isolated L4 smoke paths.

The module has 634 comment-free lines in `viewport.c` and 90 in `viewport.h` (M, the harvest's GCC preprocessing and nonblank line count). The total of 724 exceeds S3's 700-line estimate by 24 and is 2,051 lines under the donor viewport footprint of 2,775 reported by the harvest.

Layout's row-table and F counters, real reader retirement under occupied bulk, backing validation, glyphs/hits, and accepted-frame evidence for the Return and typing witnesses belong to the integration owners. The motion fixtures observe the signed port boundary. The real display pipeline and its full G1/G5 and 10 GiB measurements remain integration evidence.

## Lanes

| Command | Red | Final |
|---|---|---|
| `nice make -j2 check CONTRACT_CHECK_DISPLAYS=:180-:189` | Exit 2, app still called the removed smoke declaration against the signed header ([log](../../build/viewport-evidence/red-check.log)). | Exit 0. Viewport passed the export gate and ASan/UBSan. All isolated layer links and the contract harness passed. The export gate also reported out-of-scope counts for piece (12), undo (17) and journal (28). ([log](../../build/viewport-evidence/final-check.log)) |
| `nice make -j2 check-tsan` | Exit 2, the same smoke caller failed to compile ([log](../../build/viewport-evidence/red-tsan.log)). | Exit 0. All 13 registered TSan suites passed, including viewport. ([log](../../build/viewport-evidence/final-tsan.log)) |
| `nice make -j2 fuzz-smoke` | Exit 2, the same smoke caller failed to compile ([log](../../build/viewport-evidence/red-fuzz.log)). | Exit 0. All 16 registered fuzzers replayed their seeds, including viewport. ([log](../../build/viewport-evidence/final-fuzz.log)) |

After updating the smoke callers, the focused red linked the test against the original stub and failed on the missing viewport API, including `viewport_motion_size` and `viewport_plan`. The focused ASan/UBSan run then built and printed `viewport_test: ok`. [Focused red log](../../build/viewport-evidence/red-targets.log) · [Focused green build](../../build/viewport-evidence/focused-green.log) · [Generated fuzz run](../../build/viewport-evidence/viewport-fuzz-generated.log)
