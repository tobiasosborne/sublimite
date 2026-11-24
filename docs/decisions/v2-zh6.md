# N2 journal wire and pure reader

The signed A28 amendment supplies the wire and plan contract. The one-record
codec continues to borrow payload bytes. The planner exposes the physical
verdict and the plan for its certified prefix. Physical validation is an
internal scan and inspects the wire and slot transitions. Semantic
diagnostics are attached to individual plan operations.

The first pass appends complete operations in final-sequence order. It
indexes owners, generation keys, tokens and text boundaries by ID. A
slot can be replaced in place because its previous list has no effect
until a matching physical COMMIT. A record reference takes a snapshot
of its declaration eligibility. The operation also checks current
eligibility at COMMIT so an earlier valid range cannot cross an embargo.
Unclosed lists remain physically certified and lose their references
from the logical plan.

The second pass links per-owner candidates and shared command lists.
Each candidate carries a cutoff and its selecting decision. SAVE
terminals link back to their matched PREPARED. A FINISHED carries the
activation offset, installed predicate and saved evidence together.
The proof of binding is still required before that evidence provides a
saved id. Peer detach is represented by a shared displaced-binding
predicate referencing PREPARED's path and old expectation. Order it
against the other binding decisions and evaluate it against the
selected branch's binding index. A later independent TARGET wins.
A checkpoint's text remains usable when its restated metadata is stale.

Each record pays for at most one frame reference and one operation. An
operation pays for up to two origin links, covering the expected and
installed branches of a pending save. Each owner pays for one entry
with two slot heads. Distinct keys and tokens each pay for one entry.
There is one index by final sequence and one each for owners, keys and
tokens. Tables double at half load, so a nonempty index with n entries
has a final capacity below 4n and retains fewer than 8n buckets including
old tables (P). Allow up to one alignment charge per entry and arena
allocation. These costs give the conservative checked byte bound from
`journal_plan_workspace_size(R, B, D, T)` (P). The contiguous input is the
record window, with zero additional record-buffer bytes (P). Each frame
is validated once and its fixed fields and offsets are revisited a
bounded number of times. Lookup cost depends on the index.

The workspace fixture uses the wire's existing TABS capacity of 1,000
live and 16 closed-kept buffers. It assigns one token and key per buffer
and five frames per owner (PREPARED, declaration, two BEGINs and one
command member), so R = 5,080 and B = D = T = 1,016 (P). The test checks
the byte bound, a guard byte and rollback to the caller's mark on
exhaustion.
