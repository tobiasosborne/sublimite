# Wave 1 piece: line counts in the tree (advisor rulings, 2026-11-23) — UNDER REVIEW (second Astra read pending)

Status: ruled by the advisor at the piece.h freeze read (v2-0fv.7, header commit 2bfcce5 on the 7a branch). The
header's Lines section is frozen only when Astra's review (docs/reviews/wave1-piece-lines-astra.md) has no
counter-example or the advisor has answered it. Supersedes PLAN_V2 §4.3 (amendment A19).

## Question

The signed layout (docs/decisions/wave1-piece-pool.md) stores a newline count in every leaf entry, every branch entry
and per ADD chunk prefix, but the drafted piece.h had no line query, because PLAN §4.3 gave line geometry to a separate
`lineidx`. The 7a worker offered (a) one count query, exact or "unknown", beside lineidx; or (b) cut the counts and widen
the fanouts to 32/24.

## Ruling

Counts stay in the tree, the signed layout and fanouts stand (24-entry leaves, 16-child branches; no fixture recount),
and the tree answers line queries.

**Why not (b).** A separate offset-keyed line table must be shifted by every edit. Counter-example: one insert at offset 0
of a 1 GiB file shifts every line start after it. That shifting is where v1's 512-chunk / 32 MiB incremental cap, its
degraded mode and its regrowth protocol come from, and v1 finding 1 (exit on undo past 32 MiB) lives in that subsystem.

**Why not (a) as offered.** A count query alone cannot do goto-line (that needs select by line number), and branch sums
over mapped leaves not yet counted cannot be exact.

## Invariants (as amended after Astra's review, docs/reviews/wave1-piece-lines-astra.md)

The first ruling's invariants 3 and 4 were wrong as written: they made the edit path scan unknown chunks (Astra 1: a
span insertion reads 3C − 2 = 196,606 B > 131,072 B (P)) and let reinsertion forget counts (Astra 2: delete and undo
before the worker batch arrives returns exact text as unknown). Corrected set:

1. A newline count is a property of source coordinates: nl(store, a, b) never changes, whatever edits do. The tree
   caches it as a monoid annotation (bytes, newlines, unknown) per leaf entry and per branch entry, maintained in O(h)
   by the edit already being made.
2. "Unknown" is a value of the monoid, not a mode: a mapped ORIGINAL entry, whole or partial, whose count has not been
   set; a branch entry carries the OR of its subtree. A leaf count is at most 65,536 (P), so its sentinel fits the
   32-bit field. Text length is held below a named maximum 2^60 by an ASSERT at every edit (the descriptor derivation,
   L / 65,536 descriptors in a 2^47-byte address space, is why it holds on 4-level paging; the ASSERT is what holds it
   elsewhere, since 5-level paging gives about 2^68); so every newline sum is below 2^60 and bit 63 of a branch sum is
   free by construction (answers Astra 7).
3. An unknown entry becomes counted only (i) when its chunk's table entry is present (whole: the table value; partial:
   a UI scan of at most C in the resolve step, budgeted, outside input to submit), (ii) by a cut of a counted parent, or
   (iii) from a carried count. Every other entry is counted when created: inserted bytes while they are copied; ADD
   spans from the signed per-chunk prefix counts of the queried view's ADD store.
4. Edit-path cuts: a counted descriptor is cut by scanning its shorter half, at most C/2 = 32,768 B (P), and
   subtracting; an unknown descriptor's halves stay unknown and nothing is read. piece_replace_spans reads no byte. One
   replace reads at most 2 × 32,768 = 65,536 B of mapped text (P); a one-span delete and its undo together read at most
   65,536 B (the undo reinserts at an entry boundary and cuts nothing).
5. Counts travel with spans, outside piece_span. piece_span stays 16 B: it is the journal's and the description's unit,
   and a cached count never reaches disk. An optional parallel record of 8 B per span, kept only by undo, holds
   (head count, tail count), each at most 65,536 or the unknown sentinel, 2 × 32 bits (P): descriptors are aligned to
   the ORIGINAL chunk grid, so a span [a, b) from chunk i to chunk j is a head [a, (i+1)C), whole chunks (from the
   table) and a tail [jC, b); a single total would leave head and tail as two unknowns in one equation. A span inside one
   chunk uses the head field alone. piece_iter_next_span can return the record; piece_replace_spans takes the array or
   NULL. NULL (journal replay, recovery, any caller without counts) inserts the mapped ends unknown and resolve heals
   them. A carried count was true when described, and a table entry never reverts while the backing is trusted, so a
   tree-known entry always has a table-known chunk.
6. Exactness: a query is exact for a prefix iff the prefix contains no unknown entry, decided in the same O(h) descent.
   Every edit, undo and redo preserves the exactness of text that was exact; the only unknown text is mapped ORIGINAL
   never counted, or spans inserted without counts.
7. Counting ORIGINAL is off-path and source-keyed: a worker counts newlines per 64 KiB chunk of the backing (a loop over
   scan, bracketed by pristine fault polls) and hands batches to the UI, which stores them in a core-owned table (4 B per
   chunk (P): 16,384 chunks = 64 KiB for 1 GiB). The last chunk is [kC, min((k+1)C, len)) and is whole for open,
   adoption and replay; a subinterval of it is a slice (Astra 6). The table is keyed by ORIGINAL chunk and valid for
   every revision. A private-copy open counts while it copies and is exact from the start.
8. Resolve is a UI-only copy-on-write sweep in document order with saved progress (Astra 3, 4). It allocates (up to
   730 × 400 = 292,000 B of slots for a packed 1 GiB tree with the old root pinned (P): 16,384 descriptors, 683 leaves,
   47 branches), so it is admitted through the pool, retryable on refusal, and runs before piece_replenish; it is charged
   per visited node and entry, misses included. It restarts on a new batch adopted, an abort, or a reinsertion without
   counts, so no entry is skipped forever. A successful root replacement invalidates the snapshot cache and requires a
   seek on live iterators (node identity changes, text does not); snapshots keep their answers; a failed admission
   changes nothing; an abort may restore an unknown root, keeps the table, invalidates the sweep cursor and makes that
   root eligible again without allocating. Completeness is scoped to the drained live root and snapshots taken after.
9. Faults (Astra 5): worker and UI scans are bracketed by pristine polls; tainted batches and cut counts are discarded
   before use. The query itself polls the backing's fault id once and answers inexact with lower bound 0 when the
   epoch is nonzero, so soundness never depends on L2 noticing the fault in time (one poll per query, a few tens of
   cycles (E); the gutter asks tens of times per frame). Exactness is promised for a trusted backing only; L0 repairs
   addressability; the file owner reloads onto a fresh backing.
10. Rank and select (Astra 8): rank at an interior offset scans within one entry (at most C); at a boundary it excludes
   the following entry even if unknown. Select may descend an unknown-marked ancestor through its exact leading
   children; once an unresolved preceding entry blocks locating the target it returns inexact without scanning further
   unknowns. The header specifies the inexact offset, the line origin, the empty line after a trailing LF and
   beyond-EOF. A single-pass terminal search reads at most C per query (P); query reads are counted separately from edit
   reads.

Undo literal threshold: stays 16 (literal exactly when 16 × spans > length), so A13's gate text and the journal unit do
not move; the in-memory inverse entry is 24 B (16 B span + 8 B count record). Open for the second Astra read: is a
threshold of 24 strictly better or only different (a derivation, not a preference)?

## Header additions (7a drafts the names)

Offset → newlines before it, with exact flag and the counted part as a lower bound; line number → offset of its start,
with exact flag (locating the newline inside one entry reads at most 65,536 B (P), stated); the adopt call for a batch
of chunk counts; the resolve step with a budget; a named constant for the 64 KiB chunk (now API). The header states what
resolve does to the snapshot cache and to live iterators.

## Suite contract

Exactness depends on hidden shape, like the height limit, so the model-run suite asserts: soundness (an exact answer
always equals the model's); completeness (after every chunk is adopted and the sweep has drained, every query on the
live root is exact); private-copy trees are always exact; carried counts keep undo and redo exact; spans inserted with
NULL counts heal after resolve; a nonzero fault epoch makes every dependent answer inexact with lower bound 0. A
checkpoint abort may return entries to unknown; the next sweep restores them from the table. Mapped-read counting
(at most 65,536 B per replace) is a real-tree-only test (7b).

## Consequences

- PLAN §4.3 is superseded: no incremental cap, no degraded mode, no regrowth, no resumable goto-line continuation (A19).
- `lineidx` (v2-0fv.18) is rescoped to the off-path chunk counter and the estimate used while a prefix is unknown; whether
  it stays a module or becomes a function beside its caller is settled with the 7a worker before .18 dispatches.
  v2-0fv.9 follows whatever .18 becomes.
