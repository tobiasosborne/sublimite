# v2-40h landing review (fresh Opus, adversarial; 2026-11-25)

Reviewer thread: "v2-40h landing review: adversarial, suites + mutation reds (fresh Opus)".

- No MUST-FIX. No input or interleaving was found that breaks the reader, the provisional count, or the owner-abort
  rule.
- Mutations: 20 of 21 buildable mutants were caught by a named test. The survivor is m16b: a retired cell whose last
  name is released is never requeued, so a 400 B slot leaks per commit with surviving promoted pins.

NOTEs, which go to the backlog under Law 8:
1. The lingering-cell requeue is untested (m16b). Suggested test: in t_provisional's worker case, live_slots returns
   to its baseline after the join and a piece_reclaim.
2. The fuzz seeds never reach the reader. PH_OP_READ was added before PH_OP_COUNT, which reshuffles the existing seeds;
   piece_fuzz missed all seven reader mutants. t_random and the dedicated tests catch them.
3. undo_abort on an idle log is now silent, as the contract requires. A stray abort after a successful call is no
   longer caught.
4. A stale line in docs/decisions/v2-40h.md. Fixed at the merge.

Mutation table:

| Mutant | Change | Caught by |
|---|---|---|
| m01 | whole-span check dropped | t_read_span |
| m17 | ADD limit one past | t_read_span |
| m03 | closing poll skipped | t_read_span_mapped |
| m04b | validator skipped | t_read_span_mapped |
| m20 | validator runs before the copy | piece_test :1063 |
| m05 | one byte past the chunk | t_read_span_chunks |
| m19 | zero capacity returns OK | UBSan in t_read_span |
| m06 | decrement omitted | all suites |
| m07 | abort ASSERT skipped | t_provisional_deaths |
| m13 | cache not dropped before the ASSERT | all suites |
| m14 | take never records | piece and undo suites |
| m12 | cache ignores lifetime | piece_test and piece_shape_test |
| m15 | retire frees a named cell | piece_test |
| m11 | capture keeps the cache | t_capture |
| m08 | commit aborts internally | mapped_prepared_validator_refusal |
| m10 | prepare aborts internally | replay_admission |
| m09 | guard released after piece abort | undo_test, undo_fuzz |
| m21 | check_guard leaks | undo_test |
| m18b | wrong cleanup branch | replay_admission |
| m16b | retired cell never requeued | **nothing** |

Lanes on the restored tree: all sub-lanes of check passed except check-exports. That failure was the
reservation/redraw mismatch on v2 at 4e27790, fixed by a53a0f6. check-tsan: 12 suites. fuzz-smoke: 15 fuzzers.
