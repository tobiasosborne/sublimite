# v2-0fv.8 · L1 undo

The signed piece-pool decision governs deleted ranges. A13 supplies the bounded-copy and mapped-read fixtures. A19
supplies carried newline counts. A14 assigns durable source generations and journal encoding to L2. The frozen
piece interface supplies all text operations. [Signed piece-pool decision](wave1-piece-pool.md) ·
[Amendments](wave0-plan-amendments.md) · [Piece API rationale](v2-0fv.7.md)

## Design

| Mechanism and invariant | Simplest alternative | Counter-example |
|---|---|---|
| A removed range is described in canonical source order. Select a literal when the span count exceeds `length / sizeof(piece_span)`. Otherwise keep reference pairs and the parallel newline records. Stop the count early when a literal is inevitable. The signed pair width is 16 B (P, two 64-bit fields). Each count record adds 8 B (P, two 32-bit fields). The literal comparison still uses 16 B (G, decision 10b). | Always copy the removed bytes, or switch to a literal when the inline array fills. | A coalesced mapped range spans hundreds of chunks with one pair. A mixed range with nine long runs also meets the reference rule. Either blanket copy defeats A13. Equality at 16 B must retain the source. |
| Inverse entries use parallel inline arrays through eight pairs (G, signed capacity). Larger vectors append to one history-owned extent and are passed to the batch planner as contiguous span and count arrays. Every vector has one claim held by its record. Existing offsets stay fixed. An aborted forward edit drops its unpublished claim and restores the previous append mark. An extent returns to base when its last claim retires. Page-sized extents share small vectors, and a vector larger than a page gets its own rounded extent (P, contiguous-array size and OS release granularity). | Put larger vectors into a resizable flat array or reserve a separate page per vector. | Growth of a flat array copies every retained vector, and compacting out an evicted prefix shifts surviving offsets. A page per vector would spend a page on the nine-entry equality fixture whose arrays occupy only 216 B (P, nine × 24 B). |
| Each successful edit stages its removed range and its original inserted ADD coordinates. A compatible command can extend the published tail using a private draft. A removed literal's bytes are captured once by `piece_capture`. Short forward input is already copied once into immutable ADD by `piece_replace`, and redo can use that same coordinate. Restore uses `piece_replace_spans`. Described spans carry their counts. Literals and forward input use the ADD store's count data via the permitted NULL path. | Derive and store the opposite inverse on first replay. | A first undo can yield after growing ADD and changing history. A later refusal would then need to restore both provisional history and the ADD tail, even though the original edit already put the forward bytes in immutable storage. |
| A log is permanently tied to a live tree and owns that tree's checkpoints. `undo_begin` opens the forward checkpoint, `undo_replace` stages history, and `undo_commit` first closes the piece checkpoint and then publishes. Abort restores piece and the span-store mark, drops pending records, and preserves redo, cap and cursor. A compatible extension stages a copy of only the last published record in the log. Retired extents are kept while a command can rewind to their append mark. | Let L2 open a separate piece checkpoint, amend the published tail in place, or keep a private replacement fast path. | The old private insert/delete trace published an ADD reference under the caller's checkpoint. Abort removed the referenced bytes and regrowth reused the same coordinates with different bytes and identical newline counts. A literal also tried to open a nested piece checkpoint. One owner and one forward path cover both. The first term of piece's `W(h)` already funds a command checkpoint (P, signed replenishment formula). |
| The tree owns the backing generation and the committed ADD prefix. History holds zero tree roots (G, signed ownership table). A mapped command performs the required owner's live identity check and then the fault poll after its final dependent work. Replay validates before applying a carried record and again after a mutation, including after resume. | Rely on the fault epoch or the owner's asynchronous watch, or let only the caller check before a replay batch. | Same-size in-place overwrites can leave a pristine epoch until the owner's watch delivers its event. Sliced replay can publish several groups inside one call and can resume after a source change. Undo needs the callback at those boundaries. The opening replay check keeps a known-invalid record away from the sanitizer planner's recount. |
| One command supplies a before/after cursor pair. The caller supplies `join` and owns burst policy. Undo checks structural adjacency against a still-applied tail group. Adjacent inserts can extend a record when the next write starts at its actual ADD end. A source gap keeps a new record in the group. | Count every key as a new record or join any two commands the caller asks to join. | One slot per key inflated an ordinary typed line. A text-adjacent insertion can have disjoint ADD coordinates. A delete elsewhere or a replacement has a different replay boundary. A stale redo tail cannot extend the applied prefix. |
| A short eligible erase retains its source coordinates and edge counts for promotion at equality. A forward erase extends its captured ADD run. A backspace appends each newly removed part to ADD and sets a bit at its segment boundary, then restores the parts in reverse capture order through the batch planner. Literal extensions require ADD adjacency while the combined inverse stays literal. A forward append after reverse cuts starts a separate record because the last physical capture belongs at the start of the text. | Prepend to the old captured literal, recopy it, or always keep ORIGINAL references for short bursts. | ADD is immutable and append-only, so prepending a key would overwrite earlier storage or recopy its old bytes. Always keeping the source defeats the one-byte literal fixture. |
| `piece_span_nl_join` combines adjacent carried records with approved provenance for the exact union. Undo keeps the resulting record without computing chunk coordinates. Unknown contributions keep a shared edge unknown, and piece supplies whole interior counts. | Manufacture the record in undo, keep either edge count alone, or re-describe the union from the live view. | The old arithmetic gave the correct numbers without approved provenance. A separately described partial edge can lose part of its newline count when used alone. The first deleted fragment is already absent from the live view when the second arrives. |
| The active byte cap charges the actual fixed slot for each group and record, every external entry, and each copied literal's length, including short ADD input. Reclaiming history does not shorten committed ADD. Record and group slots share reusable base arenas. Whole slabs return when their last live slot is released. V1's bounded trim visits at most 16 group boundaries (G, inherited reclaim bound). If still over budget, it detaches the remaining eligible applied prefix in bulk. A redo suffix can be detached in constant time, and retirement releases at most 16 records per automatic pass (G, same v1 bound). Pending records stay outside the published cap until the checkpoint commits. | Cap the number of groups, or synchronously free every record evicted or discarded. | An external vector can have many more entries than a typed edit. Branching after undoing a long burst would synchronously walk all of redo. |
| Replay holds a checkpoint and a provisional record pointer, with the public history cursor and counters unchanged until a group completes. A multi-edit replay takes the render/save view before checkpoint begin. On yield, it takes a separate guard for the intermediate revision. Resume checks guard identity and releases it before mutation. Failure or clear aborts the checkpoint, drops the replay views and leaves the committed cursor untouched. Earlier completed groups stay committed. | Publish the cursor after each step, show the current live tree, or use the pre-group snapshot as the guard. | A later step can fail after a frame has seen half an edit. The pre-group view deliberately differs from the inside revision, so it cannot detect an out-of-band same-length replacement between slices. |
| Group and record pointers stay fixed while a slot is held. Replay can therefore hold the current record directly, and retirement only returns slots. Group and record slots share one size class and one available-slab list (P, same base page capacity can serve either). | Compact by moving the last occupied record into a freed slot, or use separate size classes for records and groups. | Moving a slot requires rewriting backlinks from both group boundaries and neighboring records. Separate classes leave part of both reserved pages unused for a one-record history. |
| Each replay call with work makes one whole mutation before checking its absolute deadline. An expired deadline gives a deterministic single step (G, frozen undo API). The result reports completed groups to the resume loop, dirty bytes to repaint, and cursor state from the last completed group. | Use a mutation budget or interrupt the planner midway. | One batch can take longer than a series of small edits. The piece API exposes an atomic mutation, so a mutation budget adds a second limit without bounding time, and midway interruption has no supported checkpoint boundary. |

On the current LP64 ABI (P), the inverse has 192 B of inline arrays (eight × 24 B), plus a 16 B discriminator/count
prefix (4 B enum, 4 B alignment, 8 B count). A record adds 48 B (two links, offset, removed length, forward pair);
the slot adds an 8 B slab owner, so its width is 264 B. A group uses that same slot. The first one-byte key
charges 529 B (P, two slots plus the byte). Each following one-byte key in the same contiguous burst adds 1 B
(P, its short input charge with no new slot). An individual large write keeps its existing input charge rule.
For 100,000 one-byte keys in one contiguous burst, the active history charge is **100,528 B** (P, 100,000 +
2 × 264 B), compared with 26,500,264 B at the first owner read (P, 100,000 × 265 B + 264 B). With B separate
contiguous bursts the charge is `100,000 + 528 × B` B (P, one record and one group slot per burst).
An external inverse adds 24 B per entry (P, both arrays). A one-run erase pays its copied bytes while literal,
then drops that charge as it reaches the 16 B threshold (G, equality uses spans). The piece core keeps the bytes
already appended. An oversized edit can succeed and evict its own group at the boundary, as in v1.

Keep the fixed slots. The unused 192 B of inline inverse space in a typing record is paid once per contiguous
burst (P, eight × 24 B). The common case already reuses one record, and a variable-size slot would add allocation
classes and promotion work when an erase changes representation. Physical history pages and retired slots are
reported separately by `storage_bytes`. The piece core accounts for ADD storage.

A one-run literal is at most 15 B (P, 16 B threshold minus one), so a backspace record needs at most 15 temporary
pairs when expanding its capture boundaries on the replay stack (P, every captured part has at least one byte).
This costs no extra history slot or dynamic vector. Promotion keeps the stored source counts and consumes no
additional ADD bytes.

The model's span description can recompute newline counts, so the undo test wraps `piece_replace_spans` and compares
the arrays passed to the planner against a fresh description. It also checks the restored coordinates and counts. The
513-chunk mapped fixture (G, A13 and A19) clips one byte from each end (P, interior cuts). It measures the ADD delta
across delete and undo, which must be zero (G, one eligible span). Rank and select compare with the byte model
before deletion, after deletion and after undo once mapped counts have resolved. Its one-byte companion grows ADD
by one byte once (P, one copied literal). **7b real-tree assertion** (G, A13). After adopting counts and resolving the source,
measure mapped bytes read across describe + delete + undo, excluding setup and verification, and require
`mapped_bytes_read <= 2 * PIECE_CHUNK = 131072` B (P, two ends × 32768 B × describe and delete, see the
counter-example in the piece API rationale). The model-run suite uses the frozen iterators and edit operations. [A13 and A19](wave0-plan-amendments.md) · [Pair read derivation](v2-0fv.7.md)

The cross-boundary fixture starts at ADD mark `PIECE_CHUNK - 1` (P, seeded input length) and captures a two-byte
literal across it (P, one byte on each side). A forward refusal after capture restores the old ADD mark. A multi-edit
replay then refuses a step after yielding in each direction and checks exact text, counts, history and retry.
Separately retained pre-group and intermediate snapshots survive abort. An identical-byte replacement during a
yield changes the snapshot identity and triggers the guard assertion on resume. A refusal in a later group keeps
the prior completed group's cursor state and dirty interval in the returned change.

## Integration and open questions

- Which byte cap will L2 use with the new slot and literal charges?
- L2 stages its journal after the last `undo_replace` and before `undo_commit`. Undo validates and publishes text
  and history at commit. L2 publishes the preflighted journal staging only after commit succeeds, and discards it
  when the command aborts. File generation retention and wire encoding stay with A14. Bead v2-0fv.15 owns the
  inherited monotone timing and 300 ms burst gap (G, v1 `src/undo/undo.h` at the oracle pin, `UNDO_BURST_NS`).
  L2 supplies the join decision and calls `undo_maintain` in its post-submit maintenance path.
- L2 and L5 still own the editor-level branch contract that v1 covered in `editor_undo_branch_test.c`. The headless
  L1 suite exercises branch invalidation and failure preservation at the undo boundary.
- The checkout has the piece stub. The bench links the public text archive and its required lower core archive
  against that stub. Real-tree mapped-read counts, production rank/select and p50/p99 samples remain with 7b.
  The mapped-read gate uses the current piece API's seam and boundary derivation.

## Initial verification

All gates use the repository's Clang 18 sanitizer compiler (G, Makefile pin). The contract run uses the assigned
display range `:160-:169` (G, worker brief). The test environment keeps temporary files, HOME and the X11 socket
directory under this checkout.

| Run | Result |
|---|---|
| Law 9 initial red. `nice make -j2 CC_SAN=/usr/bin/clang-18 "$PWD/build/san/tests/undo_test" B="$PWD/build"` with the suite and header present before the implementation | Link failed with `undefined reference to undo_create`, `undo_delete` and `undo_undo`. |
| Law 9 behavioral red. `nice make -j2 CC_SAN=/usr/bin/clang-18 build/san/tests/undo_test` followed by `build/san/tests/undo_test`, with the initial comparison treating equality as literal | `ASSERT failed: piece_add_length(f.tree) == copied` at the equality case. |
| Focused green. Same build and command, using the strict literal comparison | `undo_test: PASS`. |
| `nice make -j2 CC_SAN=/usr/bin/clang-18 check CONTRACT_CHECK_DISPLAYS=:160-:169` | Exit 0. `undo_test: PASS`, `law2_undo_test: active=1 count=0`, contract `PASS`, and `check: ok`. The guard's zero-allocation result is the Law 2 gate (G). |
| `nice make -j2 CC_SAN=/usr/bin/clang-18 check-tsan` | Exit 0. `undo_test: PASS` and `check-tsan: 8 threaded suites passed` (P, registered TSan suites). |
| `nice make -j2 CC_SAN=/usr/bin/clang-18 fuzz-smoke` | Exit 0. `fuzz-smoke: 10 fuzzers replayed their seeds` (P, registered fuzzers), including all three undo seeds (P, corpus files). |
| `nice make -j2 CC_SAN=/usr/bin/clang-18 all build/rel/bench/undo_bench`, then `build/rel/bench/undo_bench` | Both exited 0. `undo_bench p50=n/a p99=n/a (note: piece reserve unavailable)`. The launch recorded (M) [bat], discharging, one-minute load 8.93. With the stub there were no latency samples. A real run takes 100 samples per operation (P, smallest count with integral p50 and p99 ranks and a separate maximum). |

## Owner-read follow-up before the landing review

| Run | Result |
|---|---|
| Red. `nice make -j2 CC_SAN=/usr/bin/clang-18 build/san/tests/undo_test` followed by `build/san/tests/undo_test` | `ASSERT failed: s.records == 1` on the second one-byte key (P, the first extension). |
| Focused green. Same command with burst extension | `undo_test: PASS`. The typing fixture crosses the signed threshold and maintains the per-key charge. Directed erase fixtures cover both directions, carried edge counts, promotion at equality, and the fallback on source and ADD gaps. |
| `nice make -j2 CC_SAN=/usr/bin/clang-18 check CONTRACT_CHECK_DISPLAYS=:160-:169` | Exit 0. `check: ok` and `law2_undo_test: active=1 count=0` (G, active guard and zero calls). |
| `nice make -j2 CC_SAN=/usr/bin/clang-18 check-tsan` | Exit 0. `check-tsan: 8 threaded suites passed` (P, registered suites). |
| `nice make -j2 CC_SAN=/usr/bin/clang-18 fuzz-smoke` | Exit 0. `fuzz-smoke: 10 fuzzers replayed their seeds` (P, registered fuzzers). |
| `nice make -j2 CC_SAN=/usr/bin/clang-18 all build/rel/bench/undo_bench`, then `build/rel/bench/undo_bench` | Both exited 0. The piece stub still reports the unavailable-reserve note. The launch recorded (M) [AC], charging, one-minute load 7.13. |

That round's source delta against the original owner-read revision (P, unified diff line counts, excluding this decision) is 328 added
and 43 removed. The implementation adds 195 and removes 29, the public header adds 4 and removes 2, the undo suite
adds 122 and removes 8, and the release guard suite adds 7. The tools registry drops 4 lines of benchmark prose.

## Astra landing review

The advisor froze the amended undo header and approved the piece-owned span-count join. Current v2 supplied the
piece header, model, harness and suite, the Makefile, and their existing find and contract-harness dependencies.
The previous branch's empty `src/lineidx` placeholder was removed from the working source tree during the sync.
The new code in piece is confined to the approved API addition and its model, stub link symbol and tests.
[Piece join rationale](v2-0fv.7.md)

The quiet-box hold preceded all builds and execution. For the reds, build the current model and core with
`nice make -j2 CC_SAN=/usr/bin/clang-18 build/san/tests/piece_model.o build/san/libcore.a`. Compile the saved
candidate `build/undo-review-before/undo.c` and `review_red.c` with Clang 18 ASan/UBSan, using the candidate
header, current model and core. The link wraps `os_vm_reserve`, `os_fault_poll` and `piece_span_nl_join`.
Run `build/undo-review-before/review_red` with each argument below. Build the green targets with
`nice make -j2 CC_SAN=/usr/bin/clang-18 build/san/tests/undo_test build/san/tests/piece_test` and run both.

| Finding | Red on the saved candidate | Green on the corrected code |
|---|---|---|
| Joined provenance | `join` printed `RED piece-owned descriptions joined=0` and failed `joins == 1`. The old count values alone gave no evidence of approved provenance. | Both erase directions use the piece join, including promotion at equality. `piece_test: ok` covers chunk seam geometry and unknown contributions. The undo test compares the actual carried edges with a fresh description. |
| Forward ownership | `rewind` printed `RED stale ADD replay: length=32, appended=a` (P, the original 16-byte range and the stale 16-byte restore). `literal` died on `t->cp == PM_CP_NONE` (G, piece's single checkpoint precondition). | Insert, reference-backed delete and literal capture fit inside one undo-owned checkpoint. After abort and ADD regrowth, only the subsequent accepted command can replay. The abandoned branch preserves redo, cap and serial. |
| Mapped validator | `validator` reported an ordinary child exit when the test expected an ASSERT. | The child dies at `!piece_view_backing(piece_tree_view(tree)) || validate`. Forward commit and resumed replay exercise the closing fault poll and rollback. |

The closing-fault tests arm the wrapper after a successful piece mutation and query text, ADD, line state and history
after rollback. The counted mapped fixture compares rank/select with the byte model at all required boundaries and
checks the records offered to the planner. The model's chunk knowledge provides the byte oracle. Real-tree entry
exactness and mapped-byte instrumentation still belong to 7b.

Additional red controls use a focused copy of the final suite under `build/undo-review-probes`. The command
`bash build/undo-review-probes/run.sh` compiles each single-mutation variant with Clang 18 ASan/UBSan against
the current model and runs only its matching fixture. All variants reached the expected assertion. The normal
suite uses the unmodified archive for the green run.

| Control | Red diagnostic | Green witness |
|---|---|---|
| Skip closing forward validation | `undo_commit(f.log, &last) == UNDO_ERR_CONFLICT` | Simulated nonzero epoch after successful forward mutation and a failed poll after a resumed splice both roll back. |
| Skip the opening replay check | `expect_replay == &original` | A known invalid backing never reaches the sanitizer planner. |
| Drop carried counts | `count == expect_replay->n && nl != NULL` | The batch receives carried counts, and counted mapped rank/select matches the byte model before, between and after. |
| Skip the yielded guard | The identical-byte replacement child exits normally and fails `assert_death`. | The child aborts at `now == u->replay.guard`. |
| Erase the completed group's count from an error result | `c.groups == 1 && c.has_state && c.state.bytes[0] == 'z'` | The prior group stays committed with its dirty interval and cursor state after a later refusal. |
| Drop the group-slot charge | `undo_get_stats(f.log).history_bytes == (cap == key_bytes ? key_bytes : 0)` | The exact-fit group is kept and a one-byte-short cap evicts it. |

The A20 fixture independently charges 264 B per slot (P, layout above), 529 B for the first one-byte key (P, two
slots and the byte), and 24 B per external entry (P, pair and record). The key and external-vector cases both exercise
the exact charge and a cap one byte smaller.

Final archive and lane runs used the synced v2 baseline. The initial full run found the removed upstream
`lineidx` placeholder during layer lint. Moving that empty placeholder out of the source tree completed the sync.

| Command | Final result |
|---|---|
| `nice make -j2 CC_SAN=/usr/bin/clang-18 check CONTRACT_CHECK_DISPLAYS=:160-:169` | Exit 0. `undo_test: PASS`, `piece_test: ok`, `law2_undo_test: active=1 count=0` (G, active guard and zero calls), `outcomes: PASS 64` (P, contract harness records), and `check: ok`. Log `build/undo-landing-verified-check.log`. |
| `nice make -j2 CC_SAN=/usr/bin/clang-18 check-tsan` | Exit 0. `check-tsan: 10 threaded suites passed` (P, registered suites). Log `build/undo-landing-tsan.log`. |
| `nice make -j2 CC_SAN=/usr/bin/clang-18 fuzz-smoke` | Exit 0. `fuzz-smoke: 11 fuzzers replayed their seeds` (P, registered fuzzers). Log `build/undo-landing-fuzz.log`. |
| `nice make -j2 CC_SAN=/usr/bin/clang-18 all build/rel/bench/undo_bench` | Exit 0. The archive benchmark run exited 0 with p50/p99 unavailable at the piece stub, (M) [AC] charging, one-minute load 6.75. |

The release guard window includes command begin, replacement and commit for the joined typing path, literal
capture and external vector allocation. It also includes undo and redo through the model. Production
input-to-submit validation and measured latency remain with the real-tree integration.

Review-round source delta (P, unified diff against the reviewed undo sources and the synced piece baseline),
excluding this decision and the unchanged upstream sync. The Makefile replaces one wrapper line to add
`--wrap=piece_span_nl_join`, keeping the coordinator's fault-poll wrap.

| File | Added | Removed |
|---|---:|---:|
| `src/undo/undo.c` | 240 | 272 |
| `src/undo/undo.h` | 109 | 63 |
| `tests/undo_test.c` | 414 | 102 |
| `tests/law2_undo_test.c` | 17 | 7 |
| `fuzz/undo_fuzz.c` | 13 | 13 |
| `bench/undo_bench.c` | 8 | 4 |
| `src/piece/piece.c` | 8 | 0 |
| `src/piece/piece.h` | 14 | 3 |
| `tests/piece_model.h` | 25 | 0 |
| `tests/piece_test.c` | 72 | 0 |
| `docs/decisions/v2-0fv.7.md` | 14 | 0 |
| `Makefile` | 1 | 1 |
| Total | 935 | 465 |

## Source accounting

Counts are physical source lines, including comments and blank lines (P, `wc -l`). The pin is the v1 oracle named in
the brief. Harvest is behavioral. The implementation and harnesses were rewritten from the frozen contracts, with
zero verbatim v1 implementation or test blocks. The old counted source below was superseded in this bead's scope.

| File | v1 lines superseded | v2 lines written |
|---|---:|---:|
| `src/undo/undo.h` | 144 | 152 |
| `src/undo/undo.c` | 492 | 898 |
| `tests/undo_test.c` | 623 | 825 |
| `tests/law2_undo_test.c` | — | 58 |
| `fuzz/undo_fuzz.c` | 226 | 157 |
| `bench/undo_bench.c` | 163 | 94 |
| Total | 1648 | 2184 |

V1 behavior harvested includes burst adjacency, state transport, bounded whole-group eviction and
retirement, sliced replay, group rollback, and the pre-group view and resume guard. Cut mechanisms include fixed
ADD-only ref expansion, packed record relocation, group metadata hidden in selection bytes, replay capture and its
backup mapping, and the source-including mock benchmark. Dedicated real-tree and editor work are listed above.

The three corpus files are binary opcode streams (P, seven bytes per tuple). `burst-branch` has seven
tuples and 49 B, `cap-and-cancel` ten and 70 B, and `external-vector` eight and 56 B.

This decision has 211 physical lines (P, `wc -l`).
