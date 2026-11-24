# S3 · Read a source span from a pinned snapshot

**Signed reader and snapshot errata.** During redo, one splice may insert bytes that a later splice deletes. The candidate snapshot then has no document offset for the insertion, yet the journal must encode it in splice order. Edit can fill the journal's existing record window from the source coordinates returned by `undo_splice_span` if the snapshot holds the complete store prefix those coordinates address. [Signed E3](wave1-edit-spec.md#e3--bytes-through-the-final-append) · [Prepared replay](../../src/undo/undo.h)

## Proposed piece.h text

Place the shared validator after `piece_backing`, as approved in the journal runtime amendment. Place the new result and reader under Snapshots. The result type gives this read its own range and source errors. Existing document readers and mutation results keep their contracts. [Shared validator decision, R4](wave1-journal-wire-amendment.md#send-back-disposition)

```c
/* The owner checks the live identity of this mapped backing. True means
 * verified match. The check runs on the calling thread, leaves piece and
 * its owners untouched, and completes without waiting. The owner keeps
 * ctx alive through the last use by the borrowing operation. */
typedef bool (*piece_validate_fn)(void *ctx, const piece_backing *backing);

typedef enum piece_read_result {
    PIECE_READ_OK, PIECE_READ_RANGE, PIECE_READ_CONFLICT
} piece_read_result;

/* Copy the next slice of span starting at byte skip within the span.
 * s is an owned snapshot. The caller binds span to its tree, ORIGINAL
 * generation or ADD checkpoint lineage. Store coordinates have the
 * piece_span encoding. Any byte of the selected store prefix can be
 * read, including bytes absent from the snapshot's text.
 *
 * Check the WHOLE span before copying. Let a = src & ~PIECE_SPAN_ADD,
 * L = ORIGINAL length or s's captured ADD length according to the tag.
 * RANGE if len == 0, a > L, len > L - a, or skip > len, in that order
 * before forming a + skip. At skip == len return OK with *written = 0.
 * Before the end, zero capacity returns RANGE. Otherwise copy exactly
 * min(len - skip, capacity, PIECE_CHUNK - ((a + skip) % PIECE_CHUNK))
 * bytes into dst. On OK, *written is that count. Advance skip by it.
 *
 * A nonempty mapped ORIGINAL slice requires validate. On the held
 * backing, after copying, check live identity and close with a fault
 * poll after all dependent loads. OK requires validate(ctx, backing)
 * true and os_fault_poll(backing->fault, ...) == OS_OK with epoch zero.
 * Missing validation, a rejected identity or a bad poll gives CONFLICT.
 * ADD, private ORIGINAL and the end position require neither check.
 * On either error *written = 0. RANGE leaves dst untouched. On CONFLICT
 * the attempted slice may have been overwritten and must be discarded.
 * The caller must also close validation for the enclosing operation
 * before publishing any result that depends on mapped reads.
 *
 * Any thread holding s may call, with a validator suitable for that
 * thread and allocation-free on the typing path. written is a live
 * output pointer. If capacity is positive,
 * dst designates that much writable storage. Outputs are disjoint from
 * each other and from the stores. NULL dst is allowed at zero capacity.
 * No allocations, tree walk or shared-state mutation by the reader. */
piece_read_result piece_snapshot_read_span(const piece_snapshot *s,
                                           piece_span span, uint64_t skip,
                                           uint8_t *dst, size_t capacity,
                                           size_t *written,
                                           piece_validate_fn validate, void *ctx);
```

## Snapshot errata

**(i) Snapshot take and capture.** Add to the snapshot contract that a successful take pins ORIGINAL and exactly the readable ADD interval `[0, piece_add_length(t))` at take time, for the snapshot's entire lifetime, including after tree close. A cached take must also match the current ADD length and lineage. Amend the cache promise in `piece_capture` and `piece_snapshot_take` accordingly. Successful capture advances the store boundary and invalidates the cached take while preserving the live document revision and its iterators. A failed capture preserves both. Existing pins keep their original limit. [Landed capture and snapshot contracts](../../src/piece/piece.h)

**(ii) Provisional snapshots (D1′, supersedes the private checkpoint boundary).** Every take while a checkpoint is open is provisional until that checkpoint commits. The owner releases all such snapshots before abort. Piece checks their live count at abort and can then reuse the restored ADD tail in place. Erratum (i) still supplies the captured length and the capture-only cache invalidation. A cache hit must also have the lifetime of the requested take. [D1′ pass](#d1-pass)

**Provenance.** Edit pairs the descriptor with its source hold during preparation, while undo's borrowed splice is valid. A descriptor from an earlier revision is usable when the pin covers its range and shares its byte lineage. The numeric checks can reject a bad range, but equal coordinates can name different bytes in different incarnations or on opposite sides of abort and regrowth. Only the owner can establish that association. Abort discards the command's provisional descriptors. Pre-begin pins still serve their own prefix after abort. Commit lets surviving inside pins serve their captured prefix as ordinary pins. Lifetimes, provenance and valid C storage are caller invariants. Input arithmetic, missing mapped evidence, file changes and fault service failures are results under A15. [A15](wave0-plan-amendments.md#a15-one-always-on-assert-exhaustion-is-a-return-code-advisor-2026-11-23)

## Closing validation and handoff

A successful mapped slice can fill part of a provisional record. The owner-supplied validator checks live file identity on the pinned backing. Piece's closing poll orders the loads before the epoch observation. A pristine epoch covers earlier loads because epochs are monotone for a registration, and the poll checks the registration generation. A same-size overwrite can pass that poll and fail the identity check. Holding a mapping guarantees addressability through both checks. File supplies the identity evidence. [Backing contract](../../src/piece/piece.h) · [Signed edit validation](wave1-edit-spec.md#e3--bytes-through-the-final-append)

Edit holds the sources and validation context through closing validation. It fills only the available part of the funded record window, possibly over successive reader calls. It finishes all source reads and group member appends while rollback is available, checks journal health, then calls `undo_commit`. Undo owns the final live identity check and closing fault poll on the log's mapped backing after the last dependent read, even if the final slice came from ADD. On a slice failure or closing `UNDO_ERR_CONFLICT`, edit abandons the open journal command, releases its provisional pins, then calls `undo_abort` in the same dispatch. In either case edit discards all logical output of that command, including earlier successful slices, and preserves the prior published boundary. Accepted members keep their physical sequence and have no recovery effect without the group's final marker. Successful commit leaves only the funded standalone record or empty group final to issue. A short final retries its held bytes. There is no postcommit source read or validation that could fail. [Undo commit](../../src/undo/undo.h) · [A28 and the signed edit sequence](wave1-edit-spec.md#contracts-to-check-at-integration)

## Choices and bounds

| Invariant | Simplest alternative | Defeating case |
|---|---|---|
| A source offset resolves in the owning snapshot's store. | Seek the document iterator to the insertion. | Insert then delete in one group. Redo needs the insertion even when the candidate text is empty. |
| A pin freezes its entire readable ADD prefix, including its partial tail. | Use the live length, or the largest visible ADD endpoint. | Capture advances ADD without changing text. At an unaligned mark a later abort and append can overwrite an invisible suffix in the same chunk. |
| The owner supplies lineage and piece checks the full numeric interval before access. | Accept any offset below today's ADD length, or check only the requested slice. | Regrowth reuses a stale coordinate. A malformed length can have a valid first byte and an overflowing end. |
| A successful nonterminal call fills a positive, bounded prefix directly in the caller's buffer. | Copy the entire run or return a raw store pointer. | A canonical run can exceed the record window and cross separately allocated ADD chunks. A caller loading from a pointer after piece returns also moves the fault boundary past piece's poll. |
| Both identity and epoch close mapped reads. Command publication has its own final check. | Trust the pin, poll before copying, or rely solely on successful slice checks. | In-place overwrite can leave epoch zero. Truncation can fault during the copy. A later conflict after an accepted member must still abort the whole group. |
| Input and backing failures have distinct results and zero usable bytes. | Assert bounds or treat failure as end of span. | A bad range can terminate the editor, or truncation can silently turn a missing suffix into a successful short restore. |

Let `n` be the minimum in the declaration and `d` the pinned ADD directory depth. The payload bound is **at most min(capacity, remaining span bytes, remaining bytes in the source chunk)**, at most **65,536 B** (P, one `PIECE_CHUNK = 2^16` from the landed layout). The caller passes the remaining record capacity, so even a span crossing many records needs only the current record as payload scratch. Piece uses O(1) scalar workspace and copies each of the `n` bytes directly once. It reads at most `n` source bytes even on conflict. Range and end checks use O(1) work and read zero payload bytes (P, scalar subtraction and comparisons).

ORIGINAL takes O(1 + n) piece work (P, direct address and copy). A successful mapped copy adds one live validator invocation and one fault poll (P, the two required checks). ADD takes O(1 + d + n) (P, one indexed child per directory level and one chunk copy). Its enforced offset limit is below `2^63`, so chunk indices are below `2^(63−16)`; a 64-way node consumes six index bits and `d ≤ ceil(47/6) = 8` (P, A26). Neither document height nor total span length appears in either lookup cost. These are work and byte bounds. Elapsed time also depends on the validator and mapped page faults. [Store layout and fault ordering](../../src/piece/piece.h)

## Acceptance for the implementer

Claude or Muse owns implementation and fault-path testing. The following are proposed acceptance cases.

- Replay an insertion deleted later in the same group. Read the undo descriptor from a candidate with empty text. Decode the insertion member and compare its literal byte for byte with the original insertion, then verify the following deletion and group final. Checking only the empty recovered text would miss a lost insertion. Include a deleted mapped ORIGINAL range and a private ORIGINAL.
- Hold a cached snapshot across a capture-only append. The old pin rejects the new span, a fresh take reads it, and a repeated unchanged take hits the cache. Keep a live iterator across capture. Check a failed capture preserves the cached pin.
- With a live provisional pin, `piece_checkpoint_abort` must die at its entry ASSERT before restoring the mark, even if no append follows. Exercise successful commit and owner-ordered closing conflict separately, as specified in the [D1′ pass](#d1-pass).
- Check exact progress and canaries with partial windows, a cross-chunk span, a short final ORIGINAL chunk and an oversized capacity. Exercise zero capacity, the end position, empty span, offset beyond the store, one byte past the captured end, overskip and full-width lengths that would wrap an addition. Reject the entire malformed span even when its first slice fits. Count zero source loads on range errors.
- With the file fault service or its test seam, truncate during a mapped read and observe conflict after the dependent load. Reject both a nonzero epoch and a failed generation poll. Reject an in-place overwrite with epoch zero through the validator. Verify the callback receives the held backing and context, and missing validation fails closed. ADD and private ORIGINAL require no callback or poll.
- Accept a mapped member, then fail identity or the epoch at `undo_commit`. Check rollback, unchanged history and publication, no final and no replay effect even though the member remains physically present. Also inject a conflict on a later reader call. For singleton and group success, delay or short-write the final and verify zero further source reads or validator calls through repair.
- Instrument source bytes and directory steps against the derived limits. With base refusing every request after the pin is acquired, exercise a multi-record restore directly into the funded window. Require zero reader allocation attempts, claims and retirement steps and an active Law 2 guard. [Project laws](../../CLAUDE.md)

**Integration scope.** R4 already calls for landed `undo.h` to use the shared `piece_validate_fn` in place of its local `undo_validate_fn`. Their function signatures agree. D1′ changes undo failure ownership. The reader uses the existing OS API, journal wire and source holds. The edit integration call list should name this reader for source-coordinate literals and keep document iterators for document reads. File identity policy, journal repair and implementation of the snapshot and reader changes belong to their respective implementation beads. [R4](wave1-journal-wire-amendment.md#send-back-disposition) · [Edit integration](wave1-edit-spec.md#contracts-to-check-at-integration)

## D1 answer

*Historical answer under the original E1. The D1′ pass below supersedes its disposition and its third acceptance case.*

**(a) The holder is edit.** In the signed forward path, `edit_apply_plan` owns the candidate when it calls `undo_commit`. Edit needs that same pin for publication on success, including across a short final write. Undo takes only the log and cursor state. On closing conflict it calls `undo_abort`, which calls `piece_checkpoint_abort` before returning control to edit. The signed failure path then has edit call `journal_command_abort` and release its candidate. [Edit, “Final and publication”](wave1-edit-spec.md#command-publication-and-failure) · [Undo commit contract](../../src/undo/undo.h) · [Undo implementation](../../src/undo/undo.c)

1. Start with mapped ORIGINAL text at an unaligned ADD mark `m`. An earlier capture can establish that mark while leaving only ORIGINAL in the document. In one forward plan insert a byte at ADD offset `m`, then delete it. Edit takes the candidate under the open checkpoint. Its document has no ADD run, but its captured ADD length exceeds `m` and its insertion descriptor names that byte. [Signed E3](wave1-edit-spec.md#e3--bytes-through-the-final-append)
2. Edit emits the group members, observes a healthy journal and calls `undo_commit` with the candidate still owned. An external overwrite can now fail undo’s closing identity check on the mapped backing. Undo restores ADD length `m` while edit still holds the pin, then returns `UNDO_ERR_CONFLICT`. At the proposed assertion inside abort, the count of live provisional candidates is at least one. A15 requires a conflict result for this file event. [Undo validation](../../src/undo/undo.h) · [A15](wave0-plan-amendments.md#a15-one-always-on-assert-exhaustion-is-a-return-code-advisor-2026-11-23)
3. In the same UI dispatch edit abandons the journal command and drops the pin. This path requires no edit read after rollback and gives no worker the candidate. The distinction is the instant of release. D1 requires release **before abort**. Edit guarantees release **before regrowth** on this path. The signed reader separately guarantees the captured bytes for the lifetime of an owned pin. The third acceptance case exercises that piece guarantee with concurrent regrowth. [Piece snapshot contract](../../src/piece/piece.h) · [Published checkpoint eligibility, A28 rule 5](wave0-plan-amendments.md#a28-the-journal-write-model-advisor-2026-11-24-w1-with-repair-a-docsdecisionswave1-journal-write-modelmd)

Keep erratum (ii). In `piece_snapshot_take`, secure the restore boundary when `t->cp == PIECE_CP_SHARED && (t->add_mark & PIECE_MASK) != 0 && t->add_length > t->add_mark`. The last comparison uses the length this take captures. The insertion in step 1 is invisible to the landed `root->state.add_high` test. At `piece_checkpoint_abort` keep `ASSERT(t && t->cp != PIECE_CP_NONE);` as the entry check. D1 deletes **0 comment-free lines** of `piece.c` and removes no tests. A narrower lifetime ending at the next regrowth, or a change to who owns the candidate at closing validation, would require a separate contract decision.

## D1' pass

**Disposition.** D1′ fixes closing validation for both forward commands and prepared replay. It also works for piece, provided undo gives the owner the same opportunity to release a pin on a failed **resumption**. The three signed-in-principle changes alone are insufficient for the landed replay API. In `sliced_abort`, after MORE the caller takes `inside` on text `"b"`, resumes, and gets NOMEM. Undo currently aborts before the caller can release `inside`. The separately retained `pre` reads `"bc"` and predates the checkpoint. Only the former prevents abort. The replay amendment below needs the advisor’s ruling before implementation. [Undo test](../../tests/undo_test.c) · [E1](wave1-edit-spec.md#e1--signed-undoh-erratum)

### Abort-site audit

The only production call to `piece_checkpoint_abort` is in `undo_abort`. Its internal callers are `undo_commit`, the `abort_group` exit of `undo_replay_prepare` and `undo_clear` (also reached through `undo_destroy`). The ownership at each edge determines which call may still close the checkpoint. [Undo source](../../src/undo/undo.c) · [Piece source](../../src/piece/piece.c)

| Site | Pin at rollback and required disposition |
|---|---|
| `undo_begin` and `piece_checkpoint_begin` | Safe. The restore take happens before the checkpoint opens. A refused take leaves the log idle and no checkpoint to abort. Once begin succeeds its remaining setup cannot fail. |
| `undo_replace`, including failed history admission, inverse capture or piece mutation | Safe from implicit abort. Failure marks the forward command failed and returns. A successful capture may already have extended ADD. A caller can hold a pin from a preceding splice and must release it before the required owner `undo_abort`. Dropping a refused operation’s local storage does not restore the command. |
| First `undo_replay_prepare`, before begin | Safe. Empty history, failed initial validation and refusal in `begin_replay` leave no open checkpoint. The optional multi-record `replay.view` is taken before begin and released if begin fails. An owner retaining that pre-group view may keep it across rollback. |
| First prepare, after begin and before returning to the caller | Safe to roll back a failed apply (NOMEM or RANGE), a failed trust check (CONFLICT), or a refused yield-guard take. The UI has had no turn to take an inside pin, the validator leaves piece untouched, and a refused take yields NULL. A successful yield take returns MORE immediately with the checkpoint open. |
| Resumed prepare after MORE | **Live caller pin possible** through every failure exit, including a failed guard check/take, before- or after-apply CONFLICT, apply NOMEM/RANGE, or refusal of the next yield guard. Successful `check_guard` releases both its temporary take and undo’s old guard. It cannot release `sliced_abort.inside`, even if that pin shares the guard header. If the initial take in `check_guard` fails, undo still owns the old guard. Defer this rollback to the owner. A guard identity mismatch is already an A15 call-sequence violation. |
| `undo_commit` after a forward command or prepared replay | **Live edit candidate.** Closing validation returns CONFLICT with the checkpoint and work intact. Edit abandons the journal command, releases its pins, then calls `undo_abort`. |
| `undo_clear` and `undo_destroy` via clear | Idle is safe. OPEN can have an owner’s candidate. REPLAYING after MORE has undo’s provisional guard and can have an independent caller pin. Clear and destroy are deliberate owner requests that can roll back. Require release of all caller-owned provisional pins **before calling either** in those phases. Undo must release its own guard before piece aborts. Clear already excludes FAILED and PREPARED, including via destroy. A closing or resumed failure requiring owner abort excludes clear and destroy as well. |
| Explicit `undo_abort` | The owner releases its provisional pins before the call. In applying replay, undo itself owns `replay.guard` across MORE. Move replay-view cleanup ahead of `piece_checkpoint_abort` for replay phases, including a failed replay. The pre-group view may safely be released there too. Keep the forward pending-record cleanup distinct from replay cleanup, even when failure blocks further log calls. |
| Piece edits, capture, take, admission, reclaim, replenish and close | Safe from implicit abort. Refusals unwind temporary claims, directories or speculative edit roots. None invokes checkpoint abort. Commit closes successfully. Close ASSERTs that there is no open checkpoint. The only restore of the saved root, ADD mark and tail count is explicit checkpoint abort. |

**Proposed additional E1 replay text.** “NOMEM, RANGE and CONFLICT clear `*target`. A call entered from idle leaves the log idle on error, rolling back any work it started. A call resumed after MORE leaves the checkpoint open on error. The only legal call on the log afterward is `undo_abort`, by the owner. Release all caller-owned provisional snapshots before that call.” The caller knows which rule applies from its preceding MORE, including when a first call fails after several mutations. This preserves the prepare error code and permits cleanup without a phase query. For clear/destroy add “If this call aborts an open command or applying replay, release all caller-owned provisional snapshots first.” A failed replay must retain its replay cleanup state. Reusing the forward `UNDO_FAILED` branch as written would discard the wrong pending work.

### Replacement contract text

**undo.h, final sentence of the cost paragraph for `undo_commit`.**

> On closing CONFLICT, return UNDO_ERR_CONFLICT with the checkpoint open and the provisional text, ADD length, pending work and published history unchanged. The only legal call on the log afterward is undo_abort, by the owner.

**Edit failure path** (also applied to the edit spec’s MODEL, E1 and publication paragraphs).

> On closing `UNDO_ERR_CONFLICT`, in the same UI dispatch call `journal_command_abort`, release the candidate and all other provisional snapshot references held by edit, then call `undo_abort`. Keep the prior published boundary and issue neither final nor ACK.

Accepted members keep their sequence numbers. The absent final makes the abandoned group ineffective in recovery. Workers use published boundaries under A28 rule 5. Successful RAM commit makes the candidate ordinary, including while edit holds it in FINAL_PENDING. [Signed publication sequence](wave1-edit-spec.md#command-publication-and-failure) · [A28](wave0-plan-amendments.md#a28-the-journal-write-model-advisor-2026-11-24-w1-with-repair-a-docsdecisionswave1-journal-write-modelmd)

**piece.h, Snapshots and Checkpoint.** Proposed header text for the implementing bead:

```c
/* A take pins the current text, ORIGINAL and the readable ADD prefix
 * [0, piece_add_length(t)) at the instant of the take. A take while a
 * checkpoint is open is provisional for that checkpoint. Retains share
 * that lifetime. Release every such ownership before aborting it.
 * Commit makes all its surviving snapshots ordinary. Snapshots taken
 * before begin, including the restore view, remain ordinary.
 *
 * A cache hit requires the same revision, ADD length, lineage and
 * lifetime (ordinary or provisional for this open checkpoint). A hit
 * retains the header and claims nothing. A miss claims one header slot.
 * On refusal return PIECE_ERR_NOMEM with *out NULL. A successful capture
 * invalidates the cached take and preserves the live iterator revision.
 * A refused capture preserves both. */

/* Begin records the restore view and ADD mark before opening the
 * checkpoint. Commit keeps the current text and ADD, promotes all its
 * provisional snapshots and closes the checkpoint. */

/* Abort restores the saved text, description, ADD mark and partial-tail
 * newline total, invalidates the cache, ends the live revision and closes
 * the checkpoint. Regrowth may reuse the restored tail in place.
 * ADD spans ending above the restored mark lose their lineage.
 * O(1), allocation-free. ASSERT: a checkpoint is open and its count of
 * live provisional snapshots is zero before restoring any state. */
```

The count covers every header of this checkpoint with an outstanding caller or undo pin, including uncached intermediate headers and worker retains. Multiple takes or retains of one provisional header keep it live through its last such release. A cache-only reference and a queued header have no reader. Discount the tree’s cache ownership or drop it before the count check. Observe completed releases at logical release, independently of physical reclamation. A take that would otherwise hit the pre-begin cache must distinguish the two lifetimes without relabelling an already held ordinary header. Repeated inside takes still hit. A release of a pin promoted by an earlier commit cannot decrement a later checkpoint’s count. Preserve bounded, allocation-free commit and abort, including when a worker releases concurrently with commit. [Landed cache, release and retirement paths](../../src/piece/piece.c)

**Pool decision note** (applied above its historical description).

> **Private ADD boundary. Superseded by D1' (signed in principle).** An inside take is provisional until commit. The owner releases every provisional pin before abort. Abort checks the open checkpoint’s live provisional count, restores the ADD mark and tail newline total, and permits in-place regrowth. This supersedes the private chunk and directory prepayment, its reserve charges and its surviving-inside-snapshot tests throughout this decision.

### Deletions and request bound

| Landed symbols or storage | Implementing change and test consequence |
|---|---|
| `piece_private_boundary`, `PIECE_CP_PRIVATE`, `PIECE_CP_SHARED` | Delete the tail copy and private-directory exchange. The restore snapshot can identify an open checkpoint and supply its saved ADD root (retain it when restoring). Remove the separate exchangeable `restore_add` reference. Replace the inside-survival assertions in `t_checkpoint` and `add_directory_rollback`, including `rollback_reader` and `read_rollback_snapshot`. Keep unaligned rollback, newline restoration and directory-growth coverage. |
| `restore_tail`, `add_mark`, `mark_count` | **Keep.** After crossing a chunk boundary the current tail can differ from the one at begin. Abort uses the saved pointer to restore the tail and repair its total without a directory walk. The saved directory owns the chunk. `begin_is_constant` checks zero directory steps through abort. |
| `piece_reserve.boundary`, `dirs`, `free_dirs`, `piece_spares_make` | Delete the spare chunk, spare-node list, purchase/unwind loop, reserve destruction and test statistics `spare_chunks/spare_dirs`. Drop the `spare` argument from `piece_add_new` and `piece_dir_put`. Preserve append-directory allocation, sharing and the core's separate retirement list. In `version_headers` the reserve becomes 12,288 B and the total 10,723,328 B. In `hundred_tabs` the 101 extent mappings cost 413,696 B (P, 4,096 B/page). Ordinary ADD chunk charges still apply. |
| `PIECE_EXTRA_BOUNDARY`, `piece_extra_local`, `piece_extra_make`, `piece_snapshot_take`, `piece_replenish` | Delete the boundary companion in admission, the take’s boundary test and the top-up/retry for spares. Retain the header claim, the slot target W(h), and exact admission for ordinary ADD growth. Drop the two-directory-node prepayment check in `add_directory_rollback`. Keep the funded header and refusal checks in `typed_command_after_replenish` and ordinary ADD refusal tests. Update the spare explanation in `t_nomem`. |
| `piece_node.state.add_high`, `piece_add_high` | The sole consumer was the boundary test in take. Delete the subtree maximum, its initialization, copy, recomputation and update calls, including in shadow planning. Preserve the retirement link and signed slot layout. Source reads use the snapshot’s scalar ADD length, even with an empty document root. |

Erratum (i) still applies in full. Capture alone can grow the readable prefix. An older pin must reject that new interval while a fresh pin can read it. Test `t_capture` and the model’s cache expectation accordingly. The provisional/ordinary distinction adds one cache-eligibility condition.

**Replacement A26 request bound** for the STORAGE paragraph and step 4(c):

> From the final exact plan, request missing extents first, then the new ADD chunks and their directory nodes, each once before publication. The list contains at most ceil(S / e) + 9 × k objects (P), where S is the planned slot count, e the slots per extent and k the number of new ADD chunks. ADD offsets are below 2^63, so 2^16-byte chunk indices use at most 47 bits. A 64-way directory needs at most ceil(47 / 6) = 8 levels. Each new chunk can require one chunk and up to eight directory nodes. For k = 0 only the extent term remains.

A cache miss therefore has S = 1 and k = 0 (P). It may need one missing extent. Keep A26’s single exact request list, refusal unwind and quiescent admission rule. [A26](wave0-plan-amendments.md#a26-piece-admission-exact-at-quiescence-a-sequence-not-a-loop-advisor-2026-11-24-supersedes-a25s-fixpoint)

**Estimated deletion: 122 comment-free lines of piece.c (E, gross).** Counted nonblank physical source lines, including braces and lines with trailing comments, excluding comment-only lines. In this checkout the two complete functions at lines 801–822 and 828–854 contribute 48. Reserve fields, spare draw/teardown and test-hook reporting contribute 21. Checkpoint/companion tags, take selection, spare top-up and the two admission switch arms contribute 31. The obsolete ADD maximum and its maintenance contribute 22. This is a scoped count of old lines removed or replaced. It excludes mechanical signature/call-site changes, further restore-state simplifications and new lifetime accounting. A net implementation diff requires the bead. [Piece source](../../src/piece/piece.c)

### Tests and acceptance

- **Closing conflict.** In `mapped_prepared_validator_refusal` and the closing-poll arm of `mapped_fault_poll_conflicts`, preparation produced `"a\nb"`. Check that text through piece after the failed commit. Release the candidate and call owner abort before checking restored `"x\nby"`, history, idle replay and retry. In the forward arm of `late_fault_rollback`, text is still `"x\nb"` and ADD length is two at CONFLICT. Owner abort restores `"a\nb"` and ADD length zero. No log observation is legal between closing CONFLICT and owner abort. Helpers `replace_cmd` and `replay_commit` that promise a completed command must own cleanup on a closing error.
- **Resumed preparation, subject to the additional E1 ruling.** In the two resumed-poll arms of `mapped_fault_poll_conflicts`, the resumed arms of `one_span_mapped` and `late_fault_rollback`, both refusals in `replay_admission` (NOMEM and RANGE), and both directions in `sliced_abort`, require NULL target and owner abort before checking rollback or retrying. In `sliced_abort` read and release `inside` before abort and keep `pre` across it. The initial trust refusal in `one_span_mapped` and the uninterrupted later-record failure in `later_group_refusal` still return idle. Exercise guard cleanup on explicit cancellation immediately after MORE, including clear and destroy.
- **Other lifetime tests.** In `owner_replay` and `typing_abort_reuse`, move the candidate byte checks and release ahead of explicit abort. In `t_threads` release `mid` before abort and keep the pre-begin workers. In the Law 2 piece test’s `main`, read and release `inside` under the guard, then abort, regrow and check live bytes/counts under the same guard. In `v_record_add_lineage` describe the span/count before release and abort so the sanitizer test still reaches the stale-span insertion. Adapt `t_nomem` and `resolve_rollback` to the ordinary/provisional cache distinction, including a refused first inside header with no free slot. The model/harness must release each inside owner before `ph_op_end` aborts or `ph_close` implicitly aborts, and promote its tags on commit. Preserve checks on pre-begin pins.
- **Replacement for acceptance case three, piece.** Use the existing child-process ASSERT harness. Begin at an unaligned ADD mark, append then delete the suffix, take a provisional snapshot, retain it and drop the first reference. Abort with the last reference live. Require process death at the zero-live-provisional ASSERT in that call, with no later append needed. Repeat on an unchanged text/ADD prefix with an ordinary pre-begin pin. In positive controls release the last inside pin before abort, refuse all allocation, restore and regrow different bytes, and check the ordinary pin’s original prefix. Commit with a live inside pin, keep it across a later abort and tree close, and verify bytes and balanced backing releases. Cover aligned marks, a directory growth boundary, cache-only and queued headers, and a worker releasing a promoted pin during a later checkpoint.
- **Replacement for acceptance case three, edit.** With a live candidate and an accepted member, inject a final identity refusal or a faulted closing poll. Trace one UI dispatch in order: `undo_commit → UNDO_ERR_CONFLICT`, `journal_command_abort`, last provisional release, `undo_abort → piece_checkpoint_abort` with count zero. Require normal return of EDIT_CONFLICT, the previous text/selection/history/ADD and published pin, no new frame or ACK, no final marker, and no recovery effect from the accepted member. Run on both forward and prepared replay, then admit a fitting command. A misplaced piece ASSERT would kill this test on an ordinary file conflict. [A15](wave0-plan-amendments.md#a15-one-always-on-assert-exhaustion-is-a-return-code-advisor-2026-11-23)

The replay failure contract beyond closing commit still requires disposition. The L2 derivations also repeat the old internal-abort rule, and the edit integration table still describes A26 step 4 as one base request. The decision owner can reconcile those references with the final text. Header/source changes, test migration, the central A26 amendment and measurements belong to the implementing bead or decision owner. [L2 derivations](wave1-L2-derivations.md) · [Integration table](wave1-edit-spec.md#contracts-to-check-at-integration)
