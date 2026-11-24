# v2-0fv.17 · L1 piece 7b: the real tree

The tree behind the frozen `src/piece/piece.h` (v2-0fv.7), built to the signed pool specification
[wave1-piece-pool.md](wave1-piece-pool.md) and the Lines rulings [wave1-piece-lines.md](wave1-piece-lines.md)
(A19, the 7b erratum, A20). v1 at pin `6fec2e8` is evidence only. Tags: (P) derived from the stated layout or
schedule, (M) measured with its stamp, (G) a gate.

**Provenance of the code.** A first worker (another provider) wrote the implementation and most tests against
v2 `6be1cb3` and was stopped by its provider's content filter before anything had run. This worker applied that
patch (`git apply -3`; one Makefile conflict, resolved by keeping the find suites' hooks and seams and dropping
every model line), read it as its owner, ran it, and changed it as listed in §6. The tree passed the shape suite
and, after one fix to the provenance check, the differential suite on the first runs.

## 1. Mechanisms

| Mechanism and invariant | Simplest alternative | Defeating counter-example |
|---|---|---|
| **One 400 B slot** for leaves (16 B prefix + 24 × 16 B), branches (16 B + 16 × 24 B) and snapshot headers; `_Static_assert`s pin both layouts, header fit and alignment. | v1's three slab classes. | A free header slot cannot fund a branch; signed. |
| **Core-owned page extents.** A `base_pool` of ten slots per 4,096 B page, its descriptor and list links in the page tail; a slot finds its extent by masking its address. Partial and full lists per core; whole free pages in the reserve. | Lend single slots across cores. | One survivor per page strands S pages after a tab closes (signed review 9). `hundred_tabs`: 100 empty tabs hold exactly 100 extents, and the shared supply for W(0) one more extent plus one boundary chunk, 483,328 B (P, spec). |
| **Admission is a function.** Each call computes its claim from the current shape and funds it from the core's free slots, the reserve (inbox first), the retirement queue, then base; nothing latches. The scalar claim (2h + 3, 4h + 3, 1) is tried first; if base refuses it, the exact plan is computed and funded. | Always the scalar table, or always the exact plan. | Scalar only: an exclusive delete needs 0 slots and is refused when 11 cannot be had (`exact_claims`). Exact only: every keystroke pays a plan; for a delete that is a shadow run (below). |
| **Exact deletion plan by shadow execution.** The deletion kernel itself runs on scratch copies (at most 4 × 31 + 3 = 127 nodes (P)), charging copies of shared nodes, splits and new roots, crediting frees, and returns the peak; it reads no source byte. | A separate counting function. | A counter written beside the kernel drifts from its merge and collapse choices. Mutant `shadow_ignores_sharing` (shadow treats shared nodes as exclusive) dies in `exact_claims` and in the claim check. Cost: a 51,816 B scratch frame (P, 127 × 408 B) on the UI stack, only on this fallback. |
| **Exact insertion claim by frontier simulation** (signed 10a): one left-tied descent counts the shared copies, then counts and gap indices are simulated batch by batch with the mutation's own upper midpoint and right-active rule. | One shared path per batch, or the packed size. | 513 segments over a pinned empty root: 27 new slots (P, 24 + 2 + 1), packed 25 would underfund; `large_replay` refuses at 26 and admits at 27. |
| **Compound replace splices at its seam.** The deletion runs on an unpublished root with descriptor joining off; the insertion lands on the boundary the deletion left; the result publishes once. | Delete and join, then insert. | Two copies of one counted chunk: joining the survivors and recutting reads a third half chunk, 98,304 B > 65,536 B (P, Astra Q2.4). Mutant `join_before_splice` dies in `mapped_reads`. |
| **Prospective height check.** Insertion: the simulation refuses a root growth past the limit. Deletion: only an interior cut of one descriptor whose whole path is full can grow the root (`piece_delete_grows`, O(h)). | Grow, then roll back. | The full 384-chunk root (16 × 24 (P)): one interior byte deleted is RANGE at limit 1 with nothing changed, height 2 above it (`delete_at_limit`). Mutant `no_delete_range_check` dies on the root-growth ASSERT. |
| **Claim check (test-hooks build only).** Every slot draw ASSERTs live slots ≤ live slots at the claim + the claim. Linked into piece_test, piece_shape_test and piece_fuzz. | Test admission only at an exactly funded boundary. | An under-claim by one passes whenever the reserve holds a spare extent, which the random walk almost always does. Mutants `undercount_insert` and `frontier_no_copies` abort in both suites. |
| **Checkpoint lifecycle is an enum** `NONE → SHARED → PRIVATE → NONE`: PRIVATE once an inside take could see bytes past an unaligned mark; the take then installs a private copy of the tail chunk's prefix in the restore view before publishing. | A checkpoint pointer plus a `private_boundary` bool (the first worker's). | `(no checkpoint, private)` is representable and meaningless; CLAUDE.md: no coordinating booleans, `-Wswitch-enum` sees the enum. Mutant `no_private_boundary` dies in `add_directory_rollback` (a worker reading the inside snapshot sees the regrown byte) and in piece_test. |
| **The last owner frees only ADD mappings.** Nodes and headers leave with their pages as one chain to the reserve inbox; the final walk visits ADD chunks, directory nodes and surviving snapshot headers, never a node. `piece_close` drains the queue only when snapshots survive. | Run the UI's retirement loop with a `finalizing` flag that turns slot returns into no-ops (the first worker's). | That walks every node of the last revision one reference at a time on the releasing thread: a closed packed 10 GiB tab whose find job ends later visits 7,284 slots (P, spec packed table) to discard them; the UI paid the same at every close. |
| **Private text is counted from 512 B block prefixes** (cuts, rank, reference insertion): at most 2 × 511 B read. A chunk total is read only where it is final: the end of a private ORIGINAL chunk, or the end of a full ADD chunk. | Scan the shorter half as for mapped text (the first worker's, for cuts and rank). | A rank inside a private chunk scanned up to 32,768 B: bench line rank p50 3,422 ns → 53 ns (M, §5). Thread safety: a snapshot reader of a partial tail chunk reading its total races with the UI's next append; reading it only at a full chunk does not. Mutants `private_prefix_no_partial`, `original_prefix_no_partial` die. |
| **Mapped cuts scan the shorter half,** then poll once (FAULTS): a cut reads at most 32,768 B (P). | Scan the left part. | `asymmetric_cuts`: describing [100, 200) reads 200 B, deleting [100, 101) reads 101 B, inserting at 50 reads 50 B (P); mutant `cut_longer_half` dies there. (It survived every midpoint test until this one was written.) |
| **Annotation**: leaf (bytes, newlines 32-bit, UINT32_MAX = unknown); branch (bytes, newlines 64-bit, bit 63 = unknown OR); frozen in piece.h. | — | Frozen (Astra 7, A19). |
| **Resolve**: a budgeted document-order sweep with a saved offset; ownership decided on the visited path (round 2, A24-D): entries under exclusively owned nodes rewritten in place, shared paths copied up to the first exclusive ancestor, each copy claimed (one slot, never from the retirement queue, A24-E) before it is made; a copy that cannot be funded stops the call. The offset rewinds on any live edit, abort, or adoption that adds a chunk. | Copy always, or rewind on every adoption. | Copying an exclusive node doubles slots for nothing, and deciding exclusivity for the whole core made a disjoint old snapshot block an exclusive leaf (Astra 7b-D). Rewinding on a duplicate adoption starves the suffix: `duplicate_adoption` ends in exactly 16 slices of budget 2 with an identical re-adoption before each (P, 384 / 24 leaves). Mutant `adopt_no_rewind` dies; `resolve_always_in_place` (in place under a snapshot) dies. |
| **One closing poll** after the last dependent mapped load, for cuts, rank, select, count, resolve and adoption (FAULTS). | Open and close polls. | Cut at the freeze: an epoch never decreases, so epoch 0 at the close proves every earlier load file-backed. Mutant `rank_no_closing_poll` dies in `t_lines_faults`. |
| **Snapshot cache, lock-free inbox.** A take on an unchanged revision returns the cached header; the last release pushes the header with one CAS and then drops its core owner. | A header per take; a mutex. | Unbounded headers for an unchanged text; a preempted worker inside a mutex blocks the UI. Mutant `no_snapshot_cache` dies. |
| **Bounded retirement steps.** One step drops one reference; a branch keeps its remaining children as the continuation. | Recursive destroy. | The UI's first release of a large revision would retire every descendant in one step. |
| **ADD radix directory** (64 pointers per node), shared by reference at checkpoint begin; a new chunk or the private boundary copies one path. | Copy the directory at begin (v1 `add_store_prefix`). | Linear work at begin; A7. `begin_is_constant`: begin over 65 chunks walks 0 directory levels and makes 0 base calls (test-hooks counter `dir_steps`, live: the append made > 0). |
| **`piece_span_nl_join`** (added to piece.h by undo, 1.21): the union's head is what each input holds in the union's first chunk, its tail what each holds in its last chunk; a sum in one chunk is unknown if either part is. Pure, O(1), no tree. | Store a total for the union. | A total cannot be split back into the head and tail the record defines (wave1-piece-lines.md invariant 5). Mutant `join_drops_left_tail` (the left span's part of a shared last chunk) dies in undo_test, `join_ignores_unknown` in piece_test's `t_lines_join`; the sanitizer recount checks every joined record undo replays. |
| **Provenance debug check** (OS_SAN_ASSERT): in the sanitizer lanes `piece_replace_spans` recounts each known head and tail from its bytes, skipping a faulted backing; a one-chunk span's tail must be 0 or UNKNOWN. The release build reads nothing. | Always on. | It would read inserted mapped source on the undo path, against the erratum. Mutant `no_provenance_check` dies in `t_lines_deaths`. |
| **Harness: a cache hit is expected only while the live revision is unchanged** (white-box, test hooks), and a failed edit or a capture must not move the revision. | Skip resolve after the first take (the first worker's fuzzer). | That fuzzer never resolved a pinned root, the one case where resolve must copy. |

## 2. v1 followed and v1 cut

**Followed (by symbol, pin `6fec2e8`):** `splice` and its upper midpoint; boundary deletion `del_rec` with
`maybe_merge`/`merge_children` on the child being merged; the packed `build`; append extension of the preceding
ADD run; 512 B block prefixes per ADD chunk; persistent roots with the snapshot cache (`snapshot_uncache`);
the retirement queue drained on the UI (`reclaim_drain`); root promotion of a sole child, here by transferring
an owner.

**Cut:** typed slabs and `slot_size`/`pool_grow`; the pool mutex; allocator hooks; the staged builder
(`init_promote`); lazy mapped counts written into shared nodes; query-triggered reclamation; `add_store_prefix`'s
linear walk at begin; ORIGINAL materialisation into ADD on delete (`collect`/`add_append`); `PIECE_REF_SPANS`; the
duplicated tree/snapshot query functions; the cursor fast-path state machine (`cursor_insert`, `cursor_delete`,
`cursor_extend`, `cursor_relocate`); per-snapshot backing acquisition; the whole of `src/lineidx` (A19).

**Lines (P, `wc -l` and `grep -v` of blank and comment lines):** v1 `src/piece/piece.c` 1,742 (1,584 code), plus
`src/lineidx/lineidx.c` 1,087 = 2,829. v2 `src/piece/piece.c` 2,475 (2,150 code), with the join: 354 lines fewer
than v1's store and line index together, 733 more than v1's store alone. The grouping below was taken before the
join (34 lines) at 2,441. Grouped by the name of the function each line
belongs to (P, e.g. `lines_`/`resolve`/`private_`/`cut` → Lines, `delete`/`shadow`/`merge`/`splice` →
deletion): Lines 445, storage and admission 444, insertion and edit 340, deletion with the exact plan 268, ADD
243, revisions 230, reading 229, types and constants 181, test hooks 53.

## 3. The memory gate (A20, v1 `version_headers`)

10,000 one-byte prepends, then 5,000 held snapshots each followed by a one-byte insert at 1 + i
(`tests/piece_shape_test.c` `version_headers`, every term asserted as a literal):

| Term | Derivation | Bytes |
|---|---|---:|
| Live slots, exact | 833 leaves (1 + ceil(9,976 / 12)) + 104 (1 + ceil(817 / 8)) + 12 (1 + ceil(88 / 8)) + 1 root = 950; + 5,000 × (4 path + 1 header) = **25,950** (P; measured equal) | |
| Node extents | 25,950 / 10 = 2,595 full pages × 4,096 (P) | 10,629,120 |
| ADD chunk | 15,000 B in chunk 0; 24 + 4 + 256 + 65,536 = 65,820 B → 17 pages (P) | 69,632 |
| ADD directory | chunk 0 is its own root (P) | 0 |
| Core, tree, reserve descriptors | one base mapping each, each under a page (P) | 12,288 |
| Reserve target | W(3) = 1 + 15 + 11 + 1 = 28 slots; the core's pages are full (25,950 mod 10 = 0), so 3 whole extents; plus one spare boundary chunk (17 pages); spare directory nodes for a level-0 directory: none (P) | 81,920 |
| **Total** | (P) | **10,792,960** |
| Outer bound | 65,536 + 4,096 + 96 × 20,000 + 1.25 × 15,000 + 1,920 × 5,000 (G, signed) | 11,608,382 |
| v1 formula at 10,000 pieces | comparison only, not asserted (A20) | 10,648,382 |

## 4. Shape, admission and read fixtures (`tests/piece_shape_test.c`)

- `shape_limits`: height 1 at the 25th prepended run (refused at limit 0), height 2 at 205 = 25 + 15 × 12 (P).
- `packed_interior_delete`: 408 = 17 × 24 chunks (P) pack as 16 + 1 leaves under 2 branches; one interior byte
  deleted splits a leaf and its parent: root 2 → 3 entries, live slots 20 → 22, no mapped byte read.
- `delete_at_limit`: above, §1.
- `exact_claims`: under a pinned root at height 2, insert and delete of the first entry each claim exactly
  h + 1 = 3 (P); 2 free slots: NOMEM with length, ADD length, revision and the cached snapshot unchanged; 3: OK.
  Exclusive: delete with 0 free slots and base refused: OK, claim 0.
- `large_replay` (A19's Wave 1 exit fixture): 513 mapped chunks, adopted and resolved; one-span delete reads 0;
  replay claims 27 (P), refused at 26 with nothing changed; line queries checked against the model before,
  between and after; rank and select visit ≤ h + 1 nodes and read ≤ 65,536 B.
- `mapped_reads`: describe with counts 65,536 + delete 65,536 + undo 0 = 131,072 B (P, A13 (G)); Astra Q2.4's
  compound replace reads 65,536 B.
- `undo_seam` (Astra Q1.1, Q3.2): an ADD span inserted at a counted chunk's midpoint reads 32,768 B of the
  destination and none of its source; describe + delete (the halves join) + undo reads 32,768 B (P).
- `asymmetric_cuts`: above, §1.
- `private_reference_reads`: inserting private spans by reference reads no mapped byte and ≤ 32,768 B per call
  (erratum (G)); with block prefixes it reads ≤ 2 × 511 B per span end (P).
- `resolve_misses_and_slices` (Astra first review 1; second review Q2.5): Astra's three-end trace reads 0 at
  the edit; a non-NULL UNKNOWN record behind a finished sweep is resolved by the next call.
- `resolve_rollback` (Astra 3, 6): a short final chunk is whole; NOMEM changes neither cache nor cursor; the
  snapshot keeps its inexact answer; abort restores the unknown root and the next sweep refines it.
- `duplicate_adoption`, `hundred_tabs`, `begin_is_constant`, `extents_follow_owners`, `add_directory_rollback`
  (a worker reads the inside snapshot while the UI aborts under total refusal and appends at the mark).
- Undo's real-tree assertion (v2-0fv.8, "7b real-tree assertion"), in `tests/undo_test.c` through undo's own
  API, setup and verification excluded: the 513-chunk fixture reads exactly 4 B across describe + delete + undo
  (P, 1 B shorter half per cut, two ends, describe and delete; the undo cuts nothing); `midpoint_reads` erases
  [H, C + H) of two counted chunks and reads exactly 4H = 131,072 B (P), the A13 bound (G) attained.

## 5. Evidence

Stamps: Mon 2026-11-23 CET, ThinkPad, BAT0 `Charging` [AC], `nice`, `-j2`.

- **Red, against the 7a stub** (HEAD `src/piece/piece.c` built with a shim for the new hooks, under build/red):
  `piece_shape_test` exit 1, 20 failures (first: `piece_reserve_create`); `piece_test` exit 1, 36 failures;
  `law2_piece_test` exit 134 (ASSERT at reserve create).
- **Red → green on the real tree.** First `piece_test` run: exit 134, `piece.c` provenance check
  (`t_inverse`'s literal record `{UNKNOWN, UNKNOWN}` on a one-chunk span: the check demanded tail == 0 where
  piece.h compares known counts only). Fixed in the check; green after.
- **Mutants** (`src/piece/piece.c` broken one way each, hooks build, san, linked under both suites; script
  kept in the worker's scratch area, not in tools/): all 15 killed — `undercount_insert`, `cut_longer_half`,
  `join_before_splice`, `no_private_boundary`, `adopt_no_rewind`, `rank_no_closing_poll`,
  `resolve_always_in_place`, `no_snapshot_cache`, `no_provenance_check`, `abort_keeps_tail_total`,
  `shadow_ignores_sharing`, `no_delete_range_check`, `frontier_no_copies`, `private_prefix_no_partial`,
  `original_prefix_no_partial`; after undo landed, also under undo_test: `join_drops_left_tail` and
  `join_ignores_unknown` killed, and `cut_longer_half` killed by undo's 4 B assertion as well.
- **Green, final tree with undo (23:45–23:49 CET, BAT0 Full [AC], load1 1.90):** `nice make -j2 check
  CONTRACT_CHECK_DISPLAYS=:140-:149` exit 0 (28 sanitizer binaries, 7 law2 suites with law2_piece and law2_undo
  active=1 count=0, contract harness PASS); `nice make -j2 check-tsan` exit 0 (11 threaded suites);
  `nice make -j2 fuzz-smoke` exit 0 (11 fuzzers).
- **Fuzz (exploration, not a budget):** piece_fuzz 300 s, 12,474 runs, 730 new units, no crash; 600 s with the
  claim check and resolve after takes, 18,001 runs, no crash; the explored corpus replays clean on the final tree.
  undo_fuzz on the real tree 120 s, 348,615 runs, 1,520 new units, no crash.
- **Bench** `build/rel/bench/piece_bench`, 20,000 samples each (M)[AC] BAT0 Charging, load1 4.43, one niced
  fuzzer running: typed insert p50 430 ns p99 466 ns; delete under a pinned snapshot p50 284 ns p99 337 ns;
  snapshot take p50 64 ns p99 69 ns; line rank in a private chunk p50 53 ns p99 57 ns. No gate (Law 1).

**Landing gate (bead): client suites on the real tree.** piece_test, piece_shape_test (san, tsan), law2_piece
(rel), piece_fuzz; find_test, find_bench_test, find_bestof_test, find_fault_test, find_options_test, find_p1r_test,
find_simd_filter_verify_test, find_timeout_test (san; fault and timeout also tsan), law2_find, find_fuzz; undo_test
(san, tsan), law2_undo, undo_fuzz. Their Makefile model lines are gone; undo_test links the hooks object (it
shares piece_harness.h and carries the read assertion). `tests/piece_model.c`, the model-as-implementation TU, is
now linked by no landed suite.

## 6. What changed from the first worker's patch

Kept: the tree, its storage, admission, frontier simulation, shadow plan, Lines, the shape suite's fixtures, the
law2 suite, the bench, the fuzzer's Lines extension and both fault seeds, the edit and layer smoke calls.
Changed: the provenance check's one-chunk tail rule; the checkpoint enum; the final walk (no `finalizing`
flag; no node visit at the last release; no UI walk at close when nothing survives); private counting from block
prefixes for cuts and rank, and race-free total reads; the claim check and the `dir_steps` counter (hooks only);
the harness's revision-keyed cache expectation and failed-edit revision checks; the fuzzer resolves after takes
and links the hooks; the memory gate asserts literal derived terms; new fixtures `delete_at_limit`,
`exact_claims`, `hundred_tabs`, `begin_is_constant`, `undo_seam`, `asymmetric_cuts`, `duplicate_adoption`;
piece_test.c keeps its 7a header comment, updated; the model's `pm_capture` initialises a pointer gcc -O2 flagged
(`-Wmaybe-uninitialized`, a false positive, in the rel compile of the new suite). After undo landed (1.21): undo's
files and its piece.h, model and piece_test hunks copied from v2 `e59461d`; the real join; undo's Makefile lines
switched to the real tree; undo_test's real-tree read assertions.

## 7. Open points of round 1, as settled

- An empty tab costs three pages (extent, core and tree descriptors): bead v2-0fv.22, not this bead.
- Admission order: kept; its invariants are §8.1 (advisor A24 item 1).
- `piece_core_drop` now calls `base_pool_reset` (new in base, with its own test; A24 item 2).
- `piece_lines_start` now finds its LF with `scan_find_nth_newline` (A24 item 4, §8.4).

## 8. Round 2: the split landing read (A24)

Astra's source derivation (`docs/reviews/wave1-piece-7b-source-astra.md`) held with six MUST-FIX and one SHOULD-FIX;
the Muse test read (`docs/reviews/wave1-piece-7b-tests.md`) landed with three SHOULD-FIX; the advisor's rulings are
A24. Each MUST-FIX trace is a test in `tests/piece_shape_test.c` (`astra_*`), run red on the round-1 tree first
(`build/red_astra.log`: exit 134, the six FAILs and the F ASSERT below), then green.

| Item | Red on the round-1 tree | Fix | Green |
|---|---|---|---|
| A. Admission recounts after a retiring funding attempt | `astra_a`: 169-chunk mapped open, begin on a cache miss, abort, base refused, EOF span insert: NOMEM, though the drained header leaves the path exclusive and the insert needs 0 slots | `piece_admit_delete` / `piece_admit_insert`: plan, fund; a failed attempt has drained the queue, so plan again while an attempt retired anything (`c->retired_steps`); NOMEM exactly when base refused the remainder computed after the last funding step that could change it (piece.h STORAGE) | OK, length n + 1 |
| B. Companion storage drains before NOMEM | `astra_b`: capture, abort, then capture under a base capped at what is mapped: NOMEM with the old chunk queued | `piece_core_memory`: base refused, drain the core's queue, ask again while it retired anything; used by every ADD chunk, directory node and spare boundary allocation | OK; the capped base admits the chunk the drain returned |
| C. Private cuts | none (counts were right; the header bound was wrong) | No shorter-side logic. Header and erratum 2 (`v2-0fv.7.md`): reference insertion reads at most 511 B per span end; a private cut counts its left part from the prefixes, at most 2 × 511 = 1,022 B (P). The ruling's single 511 B per cut is not what the prefixes give: [511, 1,535) needs a partial block at both ends (counter-example in erratum 2) | `private_cut_reads` exact 1,022 B; `private_reference_reads` exact per span, including the two cross-chunk spans (3 + 5 B) |
| D. Resolve decides ownership on the visited path | `astra_d`: 145-chunk trace, a disjoint old snapshot alive, exclusive live leaf: NOMEM | A node is shared when it or a visited ancestor has another owner; exclusive entries are rewritten in place (no slot); shared paths are copied up to the first exclusive ancestor | OK, rank exact (50) |
| E. Resolve never walks the retirement queue | `astra_e`: queued old multi-level root, pinned new leaf, no free slot: OK only by retiring the queue (retire_steps > 0) | Copies funded from free slots, the reserve and base (`piece_claim_unretired`); a copy that cannot be funded stops the call: PIECE_OK with what it published, or NOMEM with nothing changed when it rewrote nothing. Header: the budget sentence holds as written; post-submit order reclaim, resolve, replenish | NOMEM with 0 retire steps and ≤ 1 visit; OK after piece_reclaim |
| F. 64-bit owners | `astra_f_owner_counts`: 2^32 − 2 shifted owners plus one header read 1 (wrapped); `astra_f_snapshot_retains`: ASSERT `old && old != UINT32_MAX` | `piece_owners` is `uint64_t` for core and snapshot counts; every increment ASSERTs `!= UINT64_MAX`; piece.h states the precondition (every owner occupies storage of its own, so 2^64 cannot exist in a 2^47- or 2^56-byte address space (P)) | both read 2^32 + 1 |
| G. Orphaned extents return on replenish | `astra_g`: 41 local holes, a second tree closed with a worker-held snapshot, its last release: the extent stays in the reserve inbox after replenish | `piece_replenish` takes the reserve inbox before funding, so the trim returns it | inbox 0, free extents 0 |

Node reference counts: round 2 left them 32-bit with an ASSERT; the advisor ruled that out (A24 follow-up, §8.7).

**Muse's SHOULD-FIX.** `t_lines_fault_cut_resolve` (piece_test): under each poll mode a cut of a counted mapped
descriptor (an edit, and a description with counts) and a resolve scan leave the entries unknown; trusted again,
an edit rewinds the sweep and it completes exact. Its mutants M5 (cut trust check deleted) and M6 (resolve trust
check deleted) die (§8.5). One cross-chunk private span per store is measured exactly. The duplicate `undo_bench`
rule is gone. `add_directory_rollback` records its checks, stops and joins the reader, then requires.

### 8.1 Admission: exact at quiescence, a sequence (A26)

History. Round 2 asked base for the scalar claim before planning (an extent so bought crowded out the ADD chunk the
command needed: Astra A(2)). Round 3 (A25) funded the exact plan as a fixpoint that went round while any storage
came back; Astra's third read (I) showed that no signal closes the window: a worker can free storage one
instruction after base refused, before any completion signal is visible. A26 replaces the fixpoint and says what
can be promised.

**Contract** (piece.h STORAGE). Exactness is promised at quiescence: with no release by another thread in flight, a
call returns PIECE_ERR_NOMEM only when base refused the exact remainder after every local source was used. While
another thread is releasing storage, a call may be refused that an immediate retry would not be; nothing latches,
the next call tries afresh.

**Procedure** (`piece_edit`, `piece_snapshot_take`, `piece_capture`, `piece_admit`): a sequence, not a loop; at
most two plan computations and one base request per call.

1. **Fast path.** The scalar claim (begin, a deletion 4h + 3, a one-chunk insertion of copied bytes 2h' + 3, a
   take 1) and the companion storage the command needs (room in the ADD tail chunk; the reserve's spare boundary
   chunk and a directory node per level) are both local (`piece_local`, `piece_extra_local`): proceed, no plan,
   no base call. The typing path after a successful replenish. Missing companion storage is not the fast path.
2. **Otherwise the exact plan** (`piece_plan_make`): the deletion on shadow nodes, the insertion's frontier
   simulation, a fixed count for takes and captures, and for a compound replace the insertion simulated on the
   deletion's shadow result as one requirement. Its remainder zero: proceed, no drain.
3. **Otherwise every local source once**: drain this core's retirement queue, take the reserve and its inbox, plan
   again (the drain can remove the sharing that made copies necessary: Astra A); remainder zero: proceed.
4. **Return to base every whole free extent beyond the exact slot need** (Astra J), then ask base for exactly the
   remainder: a fixed list of requests taken from step 3's single plan, the missing extents and then the companion
   objects, each once and in that order, all before publication (A26 step 4 as amended, §8.10): granted, proceed;
   refused, every object this call took goes back to base and the call returns PIECE_ERR_NOMEM with nothing
   changed.

Nothing is bought that the exact plan does not need, so nothing can crowd out the companion storage. Round 2's
invariants 1 and 2 are gone with the code they described; invariant 3 stands:

3. After a successful `piece_replenish` an ordinary command's scalar claims are funded locally: W(h) is the sum of
   the scalar claims of checkpoint begin (1), a deletion (4h + 3), a one-chunk insertion at the height the
   deletion can leave (2 min(31, h + 1) + 3) and a take (1), and each step consumes at most its claim (the claim
   check ASSERTs it). So base is not called on the typing path except for a new ADD chunk (and its directory
   nodes) when the tail chunk fills. `typed_command_after_replenish` runs begin, select-and-type and a take under a
   pinned snapshot at heights 0 and 2 with base refused and counts zero base calls.

**Compound planning.** The deletion runs on shadow nodes exactly as it will run for real, its root owned twice
(the staged deletion keeps the old root alive); after it, every original node under the new root is shared and
every shadow node exclusive, so the insertion's frontier on the shadow result meets exactly the sharing the real
insertion meets. The need is max(the deletion's peak, its final balance + the insertion's slots). Kept as is for
7b (advisor: round 4 is the last design round; restructuring goes to v2-0fv.27's deletion review).

**Tests** (`tests/piece_shape_test.c`). Red on round 3's tree (`build/red_astra3.log`), green on this one:
`astra3_j_surplus_returned` (step 4), `astra3_k1_funded_exact_drains_nothing` (step 2: zero retirement steps),
`astra3_k2_missing_extra_is_not_fast` (step 1). Contract tests, green on both trees (round 3's fixpoint admitted
them on the first call; here the first call is refused and the identical retry admitted):
`astra3_i_release_after_refusal` (Astra I) and `astra2_a1_release_during_refusal` (A(1), the worker's last release
placed inside base's refusal by the harness's `ph_on_refusal` hook). `astra2_a2_no_scalar_purchase` (A(2)) and
round 2's `astra_a_admission_after_retirement` stay green.

### 8.2 A24 item 2: base_pool_reset

`base_pool_reset` returns every block at once and keeps the reservation (O(1), touches no block); `base_test`
`test_pool` checks the reservation, the live count, the free list's disappearance and fresh handout from the
first block. Red: the test did not compile (undeclared). piece's final release calls it per extent.

### 8.3 A24 item 3: the model wrapper

The oracle still runs: `tests/piece_harness.h` drives the flat model's `pm_*` functions beside the real tree after
every operation in piece_test, piece_fuzz and undo_test. Deleted: `tests/piece_model.c`, the model's
`PIECE_MODEL_IMPL` section (173 lines: piece.h on top of the model, now linked by nothing) and the `%_model.o` filter
in the four link rules.

### 8.4 A24 item 4: select

`piece_lines_start` finds the LF with one `scan_find_nth_newline` call; if the entry holds fewer LFs than its
count (a fault or an unsignalled overwrite) it blocks there, inexact. Bench `line start` (line 2, its LF half a
private chunk in), 20,000 samples, (M)[AC] BAT0 Not charging, load1 0.39–0.40: byte loop p50 13,496 ns p99
17,842–26,344 ns; with scan p50 856–1,018 ns p99 1,551–2,629 ns. It is the one call it looks like.

### 8.5 Mutants of this round

`src/piece/piece.c` broken one way each (hooks build, san), linked under piece_shape_test, piece_test and
undo_test: all 27 killed (M, 2026-11-24 00:39–00:55 CET). New this round: `m5_cut_ignores_fault` and
`m6_resolve_ignores_fault` (Muse's M5, M6; killed by piece_test's `t_lines_fault_cut_resolve`), `a_no_replan`,
`b_no_drain`, `d_copy_always`, `e_resolve_retires`, `g_no_inbox_drain` (one per MUST-FIX fix, each killed by its
`astra_*` trace), `replenish_no_fund` (killed by `typed_command_after_replenish`), `private_cut_raw_scan` (killed
by `private_cut_reads`), `start_byte_offset` (select one byte short: killed by all three suites) and
`resolve_in_place_when_shared` (replacing round 1's `resolve_always_in_place`). The 17 of round 1 and the two join
mutants still die.

### 8.6 Lanes of this round

Tree: v2 `62eaa95` plus this bead (the base synced file by file from the main checkout, no git). 00:41–00:45 CET
2026-11-24, BAT0 Not charging [AC], load1 2.04: `nice make -j2 check CONTRACT_CHECK_DISPLAYS=:140-:149` exit 0
(31 sanitizer binaries, 8 law2 suites with law2_piece and law2_undo active=1 count=0, contract harness PASS);
`nice make -j2 check-tsan` exit 0 (11 threaded suites); `nice make -j2 fuzz-smoke` exit 0 (14 fuzzers); no
"overriding recipe" warning. piece_fuzz exploration 120 s, 7,499 runs, no crash; the explored piece and undo
corpora replay clean.

Lines against review/7b `6269bed` (P, `diff`): src/piece/piece.c +253 −120 (2,608 lines), piece.h +43 −24,
piece_test.h +10, base.c +8, base.h +4; tests: piece_shape_test.c +341 −15, piece_test.c +58, piece_harness.h
+22 −1, base_test.c +13, piece_model.h +4 −182, piece_model.c −6 (deleted); bench +12 −2; Makefile +7 −10.

### 8.7 Node reference counts saturate (A24 follow-up, option b)

**The rule.** A bound that justifies an ASSERT is structural or address-space, never installed memory (A15: an ASSERT
only for what no input or resource can produce). Snapshot and core owner counts meet it at 64 bits (§8, F). A node's
reference count did not: 2^32 references to one node need 2^32 distinct parents or views, about 2^32 × 400 B =
1.7 TB of live slots (P), inside a 2^47-byte address space, so only installed memory bounds them.

**Why not option a (widen).** The 16 B node prefix, field by field on the x86-64 ABI the static asserts pin:

| Offset | Width | Field | Meaning and range |
|---:|---:|---|---|
| 0 | 4 B | `refs` | references from parent entries, views and snapshot roots |
| 4 | 2 B | `count` | entries, at most 24 (leaf) or 16 (branch): 5 bits needed |
| 6 | 1 B | `height` | branch levels below, at most 31: 5 bits needed |
| 7 | 1 B | padding | |
| 8 | 8 B | `state` | live: `add_high`, the highest ADD end the subtree names, below 2^56 (ADD bytes are mapped; 5-level paging): 56 bits; queued: the retirement link |

A 64-bit count takes 8 B; the other 8 B must then hold count, height and add_high: 5 + 5 + 56 = 66 bits > 64 (P),
without changing the fanouts (24, 16) or the 400 B slot. (A 54-bit count would fit beside count and height in the
first word and is address-space bounded, at most 2^56 / 8 = 2^53 pointer-sized references (P), at the cost of packing
count and height into bit fields at every use; not the ruling's 64-bit count, so not done; recorded for the advisor.)

**Option b, done.** `refs` saturates at UINT32_MAX and sticks (`PIECE_REFS_STUCK`): an increment stops there, a
decrement of a stuck count is ignored, so a stuck node is never freed while its core lives; its slot returns with the
core's extents at the last release. A leak of one node when about 1.7 TB is in use; never an early free, never an
abort. A stuck node reads as shared (refs > 1) everywhere, so it is copied and never written in place.

**Test** (`node_refs_saturate`, piece_shape_test): two leaves under a root (25 chunks (P)); a hook sets leaf 0's count
to UINT32_MAX − 1, standing for references no test can build; two path copies under pinning snapshots add two
references: it reads UINT32_MAX after each; after every real holder lets go (snapshots, reclaim, delete-all) it still
reads UINT32_MAX and live slots are 2 (P: the new empty root and the stuck leaf). Red on the round-2 tree: exit 134,
`piece.c:358: ASSERT failed: n->refs != UINT32_MAX` (`build/red_refs.log`). Green after. Mutants `refs_unref_not_sticky`
and `refs_get_wraps` die in it.

### 8.8 Astra's re-read: E and H

**E.** piece.h's resolve contract no longer says "visits at most budget nodes": one call descends into at most
budget nodes; each branch it copies also updates the reference counts of that branch's children, at most 16 per
copy (P, the branch fanout), so a call touches at most 17 × budget nodes. The test hook `resolve_touched` counts
entered nodes plus the children whose counts a branch copy updated (a discarded copy's or an attach's decrements
touch nodes already counted). `astra2_e_touched_nodes`: Astra's 25-chunk trace, budget 2, 2 entered + 2 children of
the copied root = 4 (P), ≤ 34. Red: the hook measured entries only (2).

**H.** When a tentative copy is discarded because its parent's copy cannot be funded, the sweep's state is restored
as a whole, by one struct assignment of the state saved before descending into that child (cursor, rewrite count,
the copied flag), then marked stopped. Restoring field by field is how round 2 missed the flag: a state with
several coordinated fields restored piecemeal leaves the one you forgot describing an event that did not happen,
the same family as v1's coordinating-flag bugs (CLAUDE.md: no coordinating booleans). Here the leftover `copied`
published a revision that ended a valid live iterator. `astra2_h_discard_keeps_revision` (Astra's 528-chunk trace,
budget 20): red, the revision moved; green, the revision is unchanged and the live iterator still reads.

**Mutants of this round** (same method as §8.5, three suites each): `h_fieldwise_restore` (round 2's
field-by-field restore) dies in `astra2_h`; `refs_unref_not_sticky` and `refs_get_wraps` die in
`node_refs_saturate` (§8.7). Round 3's admission mutants are dropped with the fixpoint they targeted (A26).

**Lanes of this round** (01:13–01:17 CET 2026-11-24, BAT0 Not charging [AC], load1 1.85): `nice make -j2 check
CONTRACT_CHECK_DISPLAYS=:140-:149` exit 0 (31 sanitizer binaries, 8 law2 suites with law2_piece and law2_undo
active=1 count=0, contract harness PASS); `nice make -j2 check-tsan` exit 0 (11 suites); `nice make -j2 fuzz-smoke`
exit 0 (14 fuzzers). Lines against the round-2 merge `392308f` (P, `diff`): src/piece/piece.c +268 −65 (2,811
lines), piece.h +9 −6, piece_test.h +8; tests/piece_shape_test.c +192, piece_harness.h +20 −2.

### 8.9 Round 4 (A26, Astra's third read I, J, K): state at the pause (2026-11-24 06:46 CET)

**Done.**
- A26's sequence replaces the fixpoint in `src/piece/piece.c`: `piece_fund_exact`, its loop, `piece_supply` and
  its predicates, and `c->retired_steps` are deleted. `piece_admit` runs steps 2–4 (plan; nothing missing:
  proceed; else drain this core's retirement queue and take the reserve and its inbox once, plan again; else trim
  whole free extents beyond the exact slot need and ask base once). Step 1, the fast path, is each caller's: edit,
  take and capture proceed only when the scalar claim AND the companion storage (room in the ADD tail chunk, the
  reserve's spare boundary chunk and directory nodes) are local (`piece_extra_local`). `piece_core_memory`
  (replenish's spare boundary) is a sequence too: base, then once more after one drain.
- A compound replace is planned once per step as one requirement: the deletion on shadow nodes, then the
  insertion's frontier simulated on the shadow result (a shadow node is exclusive, an original shared: exactly the
  sharing the real compound insertion meets), the need max(peak, final balance + insertion). This keeps "at most
  two plan computations and one base request per call" for compound commands too.
- piece.h STORAGE states the A26 contract (exact at quiescence; while another thread releases storage a call may
  be refused that a retry would not be; nothing latches).
- Tests in `tests/piece_shape_test.c`, red on round 3's tree (`build/red_astra3.log`), green now:
  `astra3_j_surplus_returned` (step 4: a surplus extent returned so the ADD chunk fits), `astra3_k1_funded_exact_
  drains_nothing` (step 2: 0 retirement steps), `astra3_k2_missing_extra_is_not_fast` (step 1: the exact path,
  0 slots claimed; the next key, with the tail chunk present, is the fast path). `astra3_i_release_after_refusal`
  and `astra2_a1_release_during_refusal` assert the contract (a refusal during the race changes nothing; the
  identical retry succeeds); both pass on round 3's tree as well (its fixpoint admitted them on the first call),
  and on this tree their first call is refused and the retry admitted. Every earlier trace test stays green.
- Runs on this tree: san and tsan `piece_shape_test`, `piece_test` (random walk 5,571 OK / 42 NOMEM, the
  under-claim ASSERT active), `undo_test`; the explored piece corpus replays clean (920 runs); piece_fuzz 120 s,
  5,349 runs, no crash; `nice make -j2 all` exits 0. Mutants `k2_fast_ignores_extra`, `k1_drain_first`,
  `j_no_trim`, `replace_plan_drops_insert` all killed.
- Lines against round 3's merge `5633ba1` (P, `diff`): src/piece/piece.c +162 −154 (2,819 lines, 8 more than round
  3: the fixpoint's ~60 lines went, the compound shadow plan, `piece_extra_local` and the shadow ownership test
  came), piece.h +16 −9, tests/piece_shape_test.c +139 −4.

**Completed on resume (2026-11-24 evening).** §8.1 rewritten to A26 (contract, the four steps, compound
planning); round 3's admission mutants dropped from §8.8 with the code they targeted. Lanes on this tree
(20:08–20:11 CET, BAT0 Not charging [AC], load1 1.92): `nice make -j2 check CONTRACT_CHECK_DISPLAYS=:140-:149` exit
0 (31 sanitizer binaries, 8 law2 suites with law2_piece and law2_undo active=1 count=0, contract harness PASS);
`nice make -j2 check-tsan` exit 0 (11 suites); `nice make -j2 fuzz-smoke` exit 0 (14 fuzzers). Comment-free non-blank
lines (`gcc -fpreprocessed -dD -E -P | grep -c '[^[:space:]]'`, both `#ifdef PIECE_TEST_HOOKS` branches counted):
piece.c 2,407 (round 3: 2,395), piece.h 84 (unchanged). Round 4 is the last design round for 7b (advisor);
restructuring, including two-stage compound planning, goes to v2-0fv.27's deletion review.

### 8.10 Astra's admission read: step 1 (2026-11-24)

`docs/reviews/wave1-piece-7b-admission-astra.md`, step 1: a deletion passing a non-NULL bytes pointer with n == 0
selected ADD companion storage from the pointer, prepared an ADD reference, appended nothing and returned through
the deletion-only arm still holding it; after an abort the chunk never retired, and a later insert at quiescence was
refused although its exact plan was fundable. Fix (`piece_edit`): companion storage is chosen from what is inserted
(`copies = input.bytes && input.length`), so a deletion prepares nothing; the one preparation exit that releases a
reference (the fast path's height refusal) is inside the insertion arm, where the same condition holds. Test
`astra4_empty_insert_keeps_no_add` (Astra's trace: empty tree, base capped at one ADD chunk; begin; insert; delete
with a non-NULL pointer and n == 0; abort; insert again): red on the round-4 tree (`build/red_astra4.log`, the second
insert NOMEM), green after. Step 4 (a capture that grows the ADD directory makes two base requests) went to the
advisor as a question on A26's wording; unchanged here.

**Step 4 (advisor's ruling: wording, under three conditions).** "Ask base once" was about the plan, not the number
of calls; A26 step 4 and piece.h STORAGE are amended (an erratum, wording only).

1. *A fixed list from the single plan.* `piece_admit` takes the list from step 3's plan: ceil(max(0, S − H) / 10)
   extents (S the planned slots, H the core's free slots), then the companion objects through `piece_extra_make`.
   No plan is made after a request; each object is requested once; the order is fixed.
2. *Refusal returns this call's objects to base.* Extents bought by the call sit at the head of the reserve's free
   list (`piece_extent_new` pushes there) and go back to base by `piece_extent_trim(r, free − bought)`; ADD chunks
   and directory nodes are freed by `piece_add_prepare`'s cleanup; the private boundary's chunk and directory nodes
   now come from base first into the reserve's spares (`piece_spares_make`, which returns what it took on a
   refusal) and are installed from the spares only after all are present. What steps 3 and 4 returned to base on
   purpose before the requests (retired storage, surplus extents) stays returned: base's available bytes are as
   before the call apart from those.
3. *The bound* (P, structural; erratum at landing). The enforced bound on ADD offsets is the ASSERT
   `mark < PIECE_SPAN_ADD - n` in `piece_add_prepare`, PIECE_SPAN_ADD = 2^63, so chunk indices lie below
   2^63 / 2^16 = 2^47, and 64-way directory nodes need at most ceil(47 / 6) = 8 levels (64^7 = 2^42 < 2^47 ≤
   2^48 = 64^8). (The first wording took "below 2^47" from the address space, which nothing enforces.) A new chunk
   is 1 mapping plus at most 8 directory nodes: a path copy of at most the root's level, or, when the root grows,
   the old level below the new top plus the top, at most 8 either way (`piece_dir_append`, `piece_dir_put`). A
   take's private boundary is 1 chunk plus at most 8 directory nodes (the restore directory's level). A call has ADD
   objects (an edit inserting copied bytes, a capture) or boundary objects (a take), never both. Extents come
   e = (page − sizeof(piece_extent)) / PIECE_SLOT slots each (piece.c `piece_reserve_create`), 10 at a 4,096 B page
   with PIECE_SLOT = 400 B (asserted in `tests/piece_shape_test.c`). So one call asks base for
   at most ceil(S / e) + 9 × max(k, 1) objects, k = ceil((a + n) / 65,536) − ceil(a / 65,536) the new ADD chunks
   for n bytes appended at ADD length a.

Tests (`tests/piece_shape_test.c`): `step4_capture_refusal_returns_chunk` (Astra's step-4 trace: a full chunk 0 as
the directory root, base capped so the next chunk is fundable and its level-1 parent is not: NOMEM, base's live
bytes and the ADD length unchanged; one page more and the same capture succeeds) is green on the round-4 tree too:
the tentative chunk already went back to base through the prepare cleanup. `step4_edit_refusal_returns_extent` (169
mapped chunks pinned, an empty reserve; base admits the extent's page but not the ADD chunk) was red on the round-4
tree (`build/red_step4.log`: the bought extent stayed in the reserve, base's live bytes +4,096) and is green after
the rollback. Replenish shares `piece_spares_make` (base, and once more after one drain on a refusal);
`piece_boundary_fund` and `piece_core_memory` are deleted.
