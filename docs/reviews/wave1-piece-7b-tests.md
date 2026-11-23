# L1 piece 7b landing review, part (b): the tests and their execution

Candidate: branch `review/7b` commit `6269bed` applied as `build/review/7b-on-v2.diff` onto v2 `47701d6`
(one `git apply --binary` in this worktree, no other git write). Scope of this part: the suites and the
runs; a separate reviewer owns `src/piece/piece.c`'s derivations. Every command below was preceded by
`date` (Tue 2026-11-24 00:04–00:20 CET, outside the barred 03:45–05:00 window), run `nice -j2` at most,
with `HOME=$PWD/build/home`. Power during `make check`: BAT0 `Not charging`, load 1.37 (M, the gate's own
record). Logs of this review are under `build/review-b/` in the review worktree.

## 1. `tests/piece_shape_test.c` — exact counts vs their derivations

**CONFIRMED.** Each literal was re-derived from the signed geometry (24-entry leaves, 16-child branches,
400 B slots, 10 slots per 4,096 B page) and matched the asserted number; the suite ran green and printed
the same numbers (log `build/review-b/shape.log`). No assertion was found weakened to fit the code.

- `shape_limits`: height 1 at the 25th prepend (P, 24 + 1), height 2 at 205 (P, 25 + 15 × 12). At a lowered
  limit the crossing edit returns `PIECE_ERR_RANGE` and the test checks length (first refusal) and ADD
  length (second) did not move; a root collapse after delete-all-but-one is also checked.
- `packed_interior_delete`: 408 = 17 × 24 chunks (P) pack to 17 leaves + 2 branches + 1 root = 20 slots;
  one interior delete gives root entries 2 → 3, slots 20 → 22, `mapped_reads == 0`. This is the signed
  pool spec's counter-example to the eight-split lemma, by interior delete; `delete_at_limit` adds the
  full-root form (384 = 16 × 24 chunks, RANGE at limit 1 with revision and slots unchanged, height 2
  above it, and an end-trimming delete that still fits at the limit).
- `exact_claims`: at height 2 under a pinned root, insert and delete of the first entry claim exactly
  h + 1 = 3 slots (P): refused with 2 free slots and base refusing, admitted with 3. Exclusive delete
  claims 0 and succeeds with 0 free slots. Refusals assert length, ADD length, revision and the cached
  snapshot pointer unchanged, and `slots_claimed` (the hooks counter) equals the claim. This is the
  "one slot short / exactly enough" pair for single edits.
- `large_replay`: the same pair at bulk scale: 513 chunks replayed over a pinned empty root claim exactly
  27 = 24 + 2 + 1 slots (P, signed review 10a), `PIECE_ERR_NOMEM` at 26 with length, ADD length and the
  cached snapshot unchanged, admitted at 27 with `available_slots == 0` after. The one-span delete reads
  0 mapped bytes; line queries are checked against the model before, between and after (A19's exit
  fixture), and rank/select at EOF assert `query_visits <= height + 1` and reads ≤ 65,536 B.
- `version_headers` (A20): live slots asserted exactly 25,950 (P: 833 + 104 + 12 + 1 = 950 built, height 3
  checked, plus 5,000 × (4 path + 1 header)). Every byte term is written out and tied to a measured hooks
  field, not just to arithmetic: 2,595 extents × 4,096 = 10,629,120 B nodes (`extents == 2595`); ADD one
  chunk of 17 pages = 69,632 B (`add_chunks == 1`, `chunk_pages == 17`); directory 0 (`add_dirs == 0`);
  core + tree + reserve descriptors 3 × 4,096 = 12,288 B (each `*_pages == 1`); reserve target W(3) = 28
  slots → 3 extents + one 17-page boundary chunk = 81,920 B (`free_extents == 3`, `spare_chunks == 1`);
  total 10,792,960 B, under the signed outer bound 11,608,382 B (G), whose formula the test recomputes
  and checks against the literal. v1's 10,648,382 B is printed as comparison only, per A20.
- Read fixtures: `mapped_reads` asserts equality, not just the bound — describe 65,536 B + delete 65,536 B
  + undo 0 = 131,072 B (P, Astra-2 Q3.1's attainable case), and Astra Q2.4's compound replace reads exactly
  65,536 B (P) per replace. `undo_seam` (Q1.1, Q3.2): 32,768 B to insert at a counted midpoint, 32,768 B
  for describe + delete + undo across the rejoined seam. `asymmetric_cuts` pins the shorter-half rule at
  200 / 101 / 50 B (P). `private_reference_reads`: 0 mapped bytes and ≤ 32,768 B private per call.
- Resolve fixtures cover the Astra traces: `resolve_misses_and_slices` (first review 1: two near-whole
  source ends plus a third-chunk cut read 0 at the edit; second review Q2.5: a non-NULL UNKNOWN record
  behind a finished sweep is resolved next call), `resolve_rollback` (first review 3 and 6: short final
  chunk whole; NOMEM leaves cache and answers unchanged; snapshot keeps its inexact answer; abort restores
  the unknown root and the next sweep refines it), `duplicate_adoption` (Q2.5 starvation: exactly 16
  slices of budget 2 with an identical re-adoption before each (P, 384 / 24 leaves), visits ≤ budget).
  `hundred_tabs` (483,328 B (P)), `begin_is_constant` (A7: 0 directory steps, 0 reserve calls, with the
  counter proven live by the preceding append), `extents_follow_owners`, `add_directory_rollback` (worker
  reading the inside snapshot across abort + regrowth under total refusal) all match their spec cases.

**FINDING (SHOULD-FIX, coverage only).** `private_reference_reads` exercises only spans contained in one
chunk (one counted end each). No fixture counts private reads for a span crossing a chunk boundary, where
the header allows 32,768 B per end, two ends. Correctness of such spans is covered against the model in
`piece_test`; only the read count is unmeasured for that shape. Add one cross-chunk private span (each
store) asserting ≤ 2 × 32,768 B and 0 mapped bytes.

## 2. Fault paths

**CONFIRMED for count / adopt / rank / select; FINDING below for cuts and resolve.**

Simulated fault: `piece_harness.h` wraps `os_fault_poll` (`-Wl,--wrap=os_fault_poll` on `piece_test`,
`piece_shape_test`, `piece_fuzz`, `undo_test`). `PH_POLL_FAULTED` adds 1 to the real epoch;
`PH_POLL_ERROR` returns `OS_E_NOENT`. `t_lines_faults` asserts, for both modes: `piece_lines_adopt`
returns false for a batch counted pristine; rank answers `(false, 0)`; select(1) answers
`(false, 0, 0)`; select(0) stays `(true, 0, 0)`; `piece_lines_count` refuses. Foreign-generation and
disagreeing batches are also refused. Unchanged state is asserted the strong way: after the tainted
attempts the same tree adopts the clean batch, completes, and after the fault interludes answers exact
again against the model ("trusted again").

Real fault: `fuzz/piece_fuzz.c` `finish_mapped` runs `ftruncate(memfd, 0)` and then, with no mapped load
in between, requires `piece_lines_count` itself to fault on its scan and refuse, `piece_lines_adopt` to
refuse the pristine batch, and rank/select to answer inexact with lower bound 0 (line 0 exact). Both seeds
decode to that path (flag byte 0x03 = mapped + real, 0x05 = mapped + simulated; length bytes give a
3-chunk ORIGINAL) and both pass on the candidate. That they genuinely reach the fault code, and do not
pass vacuously, is proven by mutation M3 in §4: with the closing poll stubbed to always report pristine,
both seeds fail exactly at the fault assertions (`fuzz real fault count`, `fuzz real fault adoption`,
`fuzz tainted rank`; exit 77 from the harness trap (M, this review)).

**FINDING (SHOULD-FIX, coverage only).** No test arms a fault around a *cut* (an edit or describe with
counts over a counted mapped descriptor) or around a *resolve* scan, so the FAULTS clause "leaves a cut's
or a resolve's entry unknown" (`piece.h`) has no executing check. Mutants M5 and M6 in §4, which delete
exactly those two trust checks in `piece.c`, survive `piece_test` (san), both fault seeds and the whole
piece corpus. The checks exist in the candidate (`piece.c:1276` discards both cut halves as UNKNOWN when
untrusted; `piece.c:2327` publishes resolve counts only when trusted), so this is a regression-protection
gap, not a code defect found. Add to `t_lines_faults`: under each poll mode, cut a counted descriptor and
resolve a partial unknown entry, then assert the answers stay inexact (and become exact again, unarmed,
after resolve).

## 3. Differential, Law-2 and client suites on the real tree

**CONFIRMED.** All green in this review's runs (commands and tallies in §6).

- `tests/piece_test.c` runs the real tree against the flat model as reference only: the Makefile model
  lines are gone, the suite links `piece_test_hooks.o`, and `nm` shows one `piece_replace`. The harness
  compares length, bytes run by run, canonical spans, ADD length, snapshot identity and cache behaviour
  after every operation, with base refusal injected on one operation in eight (48 random seeds:
  5,575 OK, 38 NOMEM, 0 RANGE (M)). Lines: 7,245 exact and 4,201 inexact answers checked against the
  byte truth (M), with the suite failing unless both kinds occurred. Carried exactness, the Astra-2 Q2.2
  mixed-end trace, snapshot stability, fault answers, and the provenance deaths (`t_lines_deaths`:
  wrong-count record and ADD lineage across an abort die by `OS_SAN_ASSERT` in the sanitizer lanes) are
  all present and passing.
- `tests/law2_piece_test.c`: release build (`gcc -O2`), guard reported `active=1 count=0` (M) over a cold
  ADD admission, checkpoint begin, an inside take that installs the private boundary, abort, and further
  edits — and the inside snapshot's isolation (count 2, last byte LF) is asserted inside the same window.
  The lane fails closed on an inert guard (`base_malloc_guard_active()` is a compile-time property of the
  interposer, checked after `end`).
- Find on the real tree: `find_test`, `find_bench_test`, `find_bestof_test`, `find_fault_test`,
  `find_options_test`, `find_p1r_test`, `find_simd_filter_verify_test`, `find_timeout_test` (san; fault
  and timeout also tsan), `law2_find_test` (`active=1 count=0` (M)), `find_fuzz` seed replay. No find suite
  links the model any more.
- Undo on the real tree: `undo_test` (san and tsan), `law2_undo_test`, `undo_fuzz` seed replay. The 7b
  read assertion is in `undo_test` twice over, through undo's own API with setup and verification outside
  the counted window: the 513-chunk fixture reads exactly 4 B across describe + delete + undo (P, 1 B
  shorter half × 2 ends × describe and delete), and `midpoint_reads` attains exactly 131,072 B =
  2 × `PIECE_CHUNK` (P), each also asserted ≤ the A13 (G) bound. Run output: "undo mapped reads (P):
  describe + delete + undo 131072 B <= 131,072 B (G, A13)" (M).
- Red side of Law 9: the worker's red runs were against the 7a stub; that stub is the base commit's
  `src/piece/piece.c` in this checkout (139 lines, refuses at `piece_reserve_create`), which corroborates
  the claimed first failure. The red numbers themselves are the worker's record in
  `docs/decisions/v2-0fv.17.md` §5, not re-run here.

## 4. Mutation sanity (this review's mutants)

Six one-line breaks, each in its own copy of `piece.c` under `build/review-b/` (`make_mutants.py`,
`m_*.c`), compiled with `PIECE_TEST_HOOKS` and linked in place of the hooks object. Results (M):

| Mutant (invariant broken) | Result |
|---|---|
| M1 `m_abort` — checkpoint abort keeps the grown ADD length (drops `t->add_length = t->add_mark`) | **Killed** by `piece_test` ("abort restores the ADD length", `tests/piece_test.c:240`; then ASSERT abort, exit 134). Survives `piece_shape_test` (its abort fixtures check bytes and counts, not the mark). |
| M2 `m_rank` — exact rank returns sum + 1 | **Killed** by both: `piece_test` 4,049 failures (exit 1); `piece_shape_test` fails in `add_directory_rollback` and `large_replay` (see §5 note). |
| M3 `m_poll` — every fault poll reports pristine | **Killed** by `piece_test` (7 failures, all in `t_lines_faults`) and by both fuzz fault seeds (exit 77; first failures at the fault assertions). |
| M4 `m_nomem` — a delete NOMEM invalidates the snapshot cache (an error changes observable state) | **Killed** by `piece_shape_test` (`exact_claims` → `refused_edit`: the take after refusal is not the cached snapshot, `tests/piece_shape_test.c:604`; then ASSERT abort, exit 134). Survives `piece_test`'s 48-seed walk (its 38 NOMEMs never combine an exact-delete refusal with a cache-hit take), which is why the deterministic fixture matters. |
| M5 `m_cutpoll` — cut of a counted descriptor ignores the trust check (`piece.c:1276`) | **Survives** `piece_test`, both fault seeds, whole piece corpus. Finding in §2. |
| M6 `m_resolvepoll` — resolve publishes counts without the trust check (`piece.c:2327`) | **Survives** the same. Finding in §2. |

The three brief-named invariants (NOMEM changes nothing, abort restores the ADD length, exact rank equals
the true count) are each killed deterministically. The worker's 17 mutants in `v2-0fv.17.md` §5 were not
re-run; M1–M4 are this review's independent picks and overlap none of them except in spirit.

## 5. Passes for the wrong reason (fail-open hunt)

- **FINDING (SHOULD-FIX): M5/M6 above.** The FAULTS protection for cuts and resolve is the one clause of
  the frozen header that no suite can see. Concrete case: with M5, a cut under a faulted backing publishes
  newline counts scanned from L0's zero-substituted pages as exact; every suite above still passes.
- **FINDING (SHOULD-FIX, build hygiene): duplicate bench rule.** The diff adds the `undo_bench` rule a
  second time (`Makefile:270` and `Makefile:273`, identical recipes). Every `make` invocation now warns
  "overriding recipe for target 'build/rel/bench/undo_bench'" (M, seen in all three gate logs). Harmless
  to outputs, noisy forever; delete one copy.
- **FINDING (SHOULD-FIX, test hygiene): failure-path thread leak in `add_directory_rollback`.** The
  rollback reader thread is stopped and joined only on the success path; any `REQUIRE` failure between
  `os_thread_start` and the join returns with the worker still writing to the stack `reader`. Observed
  under M2 (M): after the intended FAIL, ASan reports a stack-use-after-scope from the worker thread,
  turning a clean test failure into memory corruption of the test process. Green runs are unaffected.
- Checked and **not** fail-open:
  - The `mapped_reads` / `private_reads` ledgers the read gates assert on are charged inside
    `piece_note_read` at every production scan site (`piece_read_count` for cuts, rank, resolve and
    reference counting; explicit charges in `piece_lines_start`, `piece_byte_at`, `piece_capture`). The
    only uncharged mapped scans are the two documented exclusions: the off-path worker counter
    `piece_lines_count` (excluded by definition in `piece_test.h`, and tests reset the ledger after using
    it) and the sanitizer-only provenance recount, which ships nothing. A scan that forgot to charge
    would still be a part-(a) code question; by inspection of every `piece_scan` call site there is none.
  - `ph_spans_check` compares a carried count only when both sides are known. That is the suite's stated
    soundness oracle; exactness is pinned separately where the header promises it (private trees exact,
    carried undo exact, completeness after adopt + drain), so an implementation that returned UNKNOWN
    wholesale still fails (`t_lines_carried(true)`, `t_lines_join`, `t_lines_private`).
  - Cache-hit expectations in the harness are keyed to the live revision from the hooks object, and every
    failed edit asserts the revision did not move (`ph_check_kept`); a resolve landing after a take is
    exercised by the fuzzer (`step_lines` after each step, takes included).
  - The fuzz fault cases cannot pass by skipping: `finish_mapped` is unconditional for mapped inputs, the
    `ftruncate` result is checked, and M3 proves the assertions bite.

## 6. Commands and results (all `date` first, `nice -j2`, `HOME=$PWD/build/home`)

| Command | Result (M) |
|---|---|
| `git apply --binary /home/tobias/Projects/sublimite-v2/build/review/7b-on-v2.diff` | exit 0; 11 files modified, 7 added |
| `make -j2 build/san/tests/piece_test build/san/tests/piece_shape_test build/rel/tests/law2_piece_test build/fuzz/piece_fuzz` | exit 0 |
| `./build/san/tests/piece_shape_test` | exit 0; height 1 at 25, 2 at 205; slots=25950 total=10792960 outer=11608382; describe=65536 delete=65536 round trip=131072; 16 slices of budget 2 |
| `./build/san/tests/piece_test` | exit 0; 5,575 OK / 38 NOMEM; 7,245 exact + 4,201 inexact line answers |
| `./build/rel/tests/law2_piece_test` | exit 0; `active=1 count=0` |
| `./build/fuzz/piece_fuzz -runs=1 fuzz/corpus/piece/lines-simulated-fault` and `.../lines-real-fault` | both executed clean (25 ms / 18 ms) |
| `make -j2 check CONTRACT_CHECK_DISPLAYS=:150-:159` | exit 0, `check: ok` (30 san suites, 8 law2 suites incl. piece/find/undo, contract harness PASS 64; BAT0 `Not charging`, load 1.37) |
| `make -j2 check-tsan` | exit 0 (11 threaded suites incl. `piece_test`, `piece_shape_test`, `undo_test`, `find_fault_test`) |
| `make -j2 fuzz-smoke` | exit 0; 13 fuzzers replayed their seeds |
| `./build/fuzz/piece_fuzz fuzz/corpus/piece -runs=1` | 11 runs clean |
| Mutants M1–M6 (build + link + run, §4) | M1–M4 killed as tabled; M5, M6 survive |

No command needed approval. Nothing was run Tue 03:45–05:00 CET. No `bd`; the only git write was the one
apply. Review artifacts (mutant copies, objects, logs) are under `build/review-b/` in this worktree.

## Verdict: LAND

The suites assert the numbers the spec and the decision doc derive — exactly, not within slack — and the
fault, client and Law-2 lanes reach the code they claim on the real tree, with all three gates green.
The two surviving mutants (M5, M6) remove checks that are present and correct in the candidate; they mark
where regression protection is missing, not where the tree misbehaves, so they do not hold the landing.
Three SHOULD-FIX items for follow-up beads: cut/resolve fault coverage in `t_lines_faults` (plus the
cross-chunk private-span read case), the duplicate `undo_bench` Makefile rule, and the
`add_directory_rollback` failure-path join.
