# Wave 1 piece: line counts in the tree (advisor ruling, 2026-11-23) — UNDER REVIEW

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

## Invariants

1. A newline count is a property of source coordinates: nl(store, a, b) never changes, whatever edits do. The tree
   caches it as a monoid annotation (bytes, newlines, unknown) per leaf entry and per branch entry, maintained in O(h)
   by the edit already being made.
2. "Unknown" is a value of the monoid, not a mode. A leaf entry is unknown only if it is a whole, never-cut 64 KiB chunk
   of a mapped ORIGINAL; a branch entry carries the OR of its subtree. A leaf count is at most 65,536 (P), so the
   sentinel fits its 32-bit field; a branch sum uses bit 63.
3. Every other entry is counted when it is created: inserted bytes while they are copied; ADD spans from the signed
   per-chunk prefix counts; the two halves of a cut descriptor by scanning within that one descriptor (at most
   65,536 B, the read A13 already allows: for a counted entry scan the shorter half and subtract, for an unknown one scan
   it whole). Spans reinserted by reference: whole chunks from the core's table if present, else unknown; the two
   partial ends scanned.
4. A query is exact for a prefix iff the prefix contains no unknown entry, decided in the same O(h) descent. Exactness,
   once reached, never degrades; no edit or undo of any size takes it away from counted text.
5. Counting ORIGINAL is off-path and source-keyed: a worker counts newlines per 64 KiB chunk of the backing (a loop over
   scan, bracketed by the fault poll) and hands batches to the UI, which stores them in a core-owned table (4 B per chunk
   (P): 16,384 chunks = 64 KiB for 1 GiB) and resolves the unknown entries for those chunks, O(h) per leaf rewritten, off
   the typing path, budgeted like piece_reclaim. The table is keyed by ORIGINAL chunk, so it is valid for every
   revision: there is no revision to adopt and no protocol. A private-copy open counts while it copies and is exact from
   the start.

## Header additions (7a drafts the names)

Offset → newlines before it, with exact flag and the counted part as a lower bound; line number → offset of its start,
with exact flag (locating the newline inside one entry reads at most 65,536 B (P), stated); the adopt call for a batch
of chunk counts; the resolve step with a budget; a named constant for the 64 KiB chunk (now API). The header states what
resolve does to the snapshot cache and to live iterators.

## Suite contract

Exactness depends on hidden shape, like the height limit, so the model-run suite asserts: soundness (an exact answer
always equals the model's); completeness (after every chunk is adopted and resolve has run, every query is exact);
private-copy trees are always exact. A checkpoint abort may return entries to unknown; the next resolve restores them
from the table.

## Consequences

- PLAN §4.3 is superseded: no incremental cap, no degraded mode, no regrowth, no resumable goto-line continuation (A19).
- `lineidx` (v2-0fv.18) is rescoped to the off-path chunk counter and the estimate used while a prefix is unknown; whether
  it stays a module or becomes a function beside its caller is settled with the 7a worker before .18 dispatches.
  v2-0fv.9 follows whatever .18 becomes.
