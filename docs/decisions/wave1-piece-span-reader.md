# S3 · Read a source span from a pinned snapshot

**Proposed for advisor signature.** During redo, one splice may insert bytes that a later splice deletes. The candidate snapshot then has no document offset for the insertion, yet the journal must encode it in splice order. Edit can fill the journal's existing record window from the source coordinates returned by `undo_splice_span` if the snapshot holds the complete store prefix those coordinates address. [Signed E3](wave1-edit-spec.md#e3--bytes-through-the-final-append) · [Prepared replay](../../src/undo/undo.h)

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

**Snapshot take and capture.** Add to the snapshot contract that a successful take pins ORIGINAL and exactly the readable ADD interval `[0, piece_add_length(t))` at take time, for the snapshot's entire lifetime, including after tree close. A cached take must also match the current ADD length and lineage. Amend the cache promise in `piece_capture` and `piece_snapshot_take` accordingly. Successful capture advances the store boundary and invalidates the cached take while preserving the live document revision and its iterators. A failed capture preserves both. Existing pins keep their original limit. [Landed capture and snapshot contracts](../../src/piece/piece.h)

**Private checkpoint boundary.** For an open checkpoint at an unaligned ADD mark, the first take exposing any bytes beyond the mark must secure the existing private rollback boundary before returning the pin. Use the captured ADD length to decide this even when the document has no ADD descriptors. Aligned marks start regrowth in a separate chunk. The landed snapshot already retains the core and the ADD directory root. Its header also needs the scalar ADD limit. Today the take tests the document root's `add_high`, and capture can leave the old snapshot cached. Both conditions were sufficient for document reads. The header claim and, when required, the chunk and directory path for the private boundary belong to take's existing A26 admission. Refusal returns `PIECE_ERR_NOMEM` with a null snapshot and preserves the open checkpoint. Reading an acquired pin has no claim. [Piece implementation](../../src/piece/piece.c) · [A26 admission](wave0-plan-amendments.md#a26-piece-admission-exact-at-quiescence-a-sequence-not-a-loop-advisor-2026-11-24-supersedes-a25s-fixpoint)

**Provenance.** Edit pairs the descriptor with its source hold during preparation, while undo's borrowed splice is valid. A descriptor from an earlier revision is usable when the pin covers its range and shares its byte lineage. The numeric checks can reject a bad range, but equal coordinates can name different bytes in different incarnations or on opposite sides of abort and regrowth. Only the owner can establish that association. Abort discards the command's provisional descriptors. A surviving pin can still serve its own historic bytes to a reader already holding it. It grants no authority to replay that command against the regrown store. Lifetimes, provenance and valid C storage are caller invariants. Input arithmetic, missing mapped evidence, file changes and fault service failures are results under A15. [A15](wave0-plan-amendments.md#a15-one-always-on-assert-exhaustion-is-a-return-code-advisor-2026-11-23)

## Closing validation and handoff

A successful mapped slice can fill part of a provisional record. The owner-supplied validator checks live file identity on the pinned backing. Piece's closing poll orders the loads before the epoch observation. A pristine epoch covers earlier loads because epochs are monotone for a registration, and the poll checks the registration generation. A same-size overwrite can pass that poll and fail the identity check. Holding a mapping guarantees addressability through both checks. File supplies the identity evidence. [Backing contract](../../src/piece/piece.h) · [Signed edit validation](wave1-edit-spec.md#e3--bytes-through-the-final-append)

Edit holds the sources and validation context through closing validation. It fills only the available part of the funded record window, possibly over successive reader calls. It finishes all source reads and group member appends while rollback is available, checks journal health, then calls `undo_commit`. Undo owns the final live identity check and closing fault poll on the log's mapped backing after the last dependent read, even if the final slice came from ADD. A slice failure requires edit to abort undo and journal. A closing `UNDO_ERR_CONFLICT` aborts undo internally and edit aborts the journal command. In either case edit discards all logical output of that command, including earlier successful slices, and preserves the prior published boundary. Accepted members keep their physical sequence and have no recovery effect without the group's final marker. Successful commit leaves only the funded standalone record or empty group final to issue. A short final retries its held bytes. There is no postcommit source read or validation that could fail. [Undo commit](../../src/undo/undo.h) · [A28 and the signed edit sequence](wave1-edit-spec.md#contracts-to-check-at-integration)

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
- Start a checkpoint at an unaligned ADD mark. Append then delete the suffix, take an inside pin, abort and regrow different bytes at the same offsets. A worker on the inside pin reads the first suffix while the UI regrows the second. Cover aligned marks, a directory growth boundary, tree close before last read, and balanced backing release after the last pin. Exercise private-boundary admission refusal and retry under A26 and the worker race under TSan.
- Check exact progress and canaries with partial windows, a cross-chunk span, a short final ORIGINAL chunk and an oversized capacity. Exercise zero capacity, the end position, empty span, offset beyond the store, one byte past the captured end, overskip and full-width lengths that would wrap an addition. Reject the entire malformed span even when its first slice fits. Count zero source loads on range errors.
- With the file fault service or its test seam, truncate during a mapped read and observe conflict after the dependent load. Reject both a nonzero epoch and a failed generation poll. Reject an in-place overwrite with epoch zero through the validator. Verify the callback receives the held backing and context, and missing validation fails closed. ADD and private ORIGINAL require no callback or poll.
- Accept a mapped member, then fail identity or the epoch at `undo_commit`. Check rollback, unchanged history and publication, no final and no replay effect even though the member remains physically present. Also inject a conflict on a later reader call. For singleton and group success, delay or short-write the final and verify zero further source reads or validator calls through repair.
- Instrument source bytes and directory steps against the derived limits. With base refusing every request after the pin is acquired, exercise a multi-record restore directly into the funded window. Require zero reader allocation attempts, claims and retirement steps and an active Law 2 guard. [Project laws](../../CLAUDE.md)

**Integration scope.** R4 already calls for landed `undo.h` to use the shared `piece_validate_fn` in place of its local `undo_validate_fn`. Their function signatures agree. The reader requires no further undo API, OS API, journal wire change or source-hold object. The edit integration call list should name this reader for source-coordinate literals and keep document iterators for document reads. File identity policy, journal repair and implementation of the snapshot and reader changes belong to their respective implementation beads. [R4](wave1-journal-wire-amendment.md#send-back-disposition) · [Edit integration](wave1-edit-spec.md#contracts-to-check-at-integration)
