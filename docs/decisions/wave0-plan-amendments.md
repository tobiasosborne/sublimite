# Wave 0: amendments to PLAN_V2 (2026-11-22)

Rule (advisor): the plan text is kept true; this file says why each line changed.

## A1. §2 L0 internal order: `base < ipc` (was `os < ipc`)

v1 `src/ipc/ipc.h` includes `base/base.h`: `ipc_args.arena` and `ipc_server.arena` are `edit_arena`, used by
`ipc_parse_args` / `ipc_args_read_stdin` (`src/ipc/args.c`), `ipc_client_send_display` and `setup_runtime`
(`src/ipc/ipc_posix.c`) at `6fec2e8` (docs/decisions/wave1-L0-harvest.md §0.1). An arena is what `base` is for;
rewriting ipc to avoid it would be churn. New order: `os < base < trace < work; base < ipc` (ipc may use os and
base, never trace or work). Changed in PLAN_V2 §2 and `tools/check_dag.py` `INTRA_RULES`. Approved by the advisor.

## A2. §3, §2 L1, §6: the 17-leaf piece root collapse is struck

The deciding test (docs/decisions/wave0-piece-root-probe.md, bead v2-he7.7) ran ten root grow/collapse shapes
against v1 at `6fec2e8` under ASan+UBSan with a reference model: 16→17 grow, left/right/boundary shrinks,
shrink-by-1, repeated grow/shrink cycles, height-2 collapse (257/300 pieces) with a live snapshot, checkpoint,
`insert_ref`, chunk build, and a random walk. All pass; a deliberate one-byte model flip is reported (harness
detects divergence). Outcome B: no bug, so no "failing test first" and no "fix root promotion" in Wave 1. The test
itself becomes an ordinary v2 `piece` regression test. Nobody had pointed at the bug in code, test, bead or review.

## A3. §6 expected-failure list (from M3, docs/decisions/wave0-known-bugs.md)

Two XFAIL specs remain: post-rename save conflict, `LINEIDX_EDIT_MAX_CHUNKS` delete+undo above 32 MiB
(512 × 64 KiB = 33,554,432 B (P)). Wayland clipboard copy is fixed in v1 by `73beeb4` and moves to green-on-v1,
mock-verified only; the real-compositor check stays a Wave 2 gate item. See also
docs/decisions/wave0-beads-and-oracle-pin.md §7.

## A4. §4.1 snapshot prepayment: 1,920 B/edit is (G), not (P)

From the lease specification (docs/decisions/wave1-os-fault-lease.md, "Plan corrections"): v1 `src/piece/piece.h`
MEMORY BOUND gives 1,920 B per edit with a live snapshot as a G10f allowance (G, frozen v1 header), and v1
`docs/decisions/P1.4e.md` ("Unresolved frozen contracts") shows a configured deeper path exceeds it before a private
ADD boundary is funded. Multiplying it by retained revisions proves nothing. The pool is sized by counting distinct
live revisions, their path/reserve classes and the boundary prepayment in `checkpoint_snapshot_storage`. Also, the
plan's `session.c:142` reference names two different retainers at the pin (`publish`'s leaf holds `saved_snapshot`;
`capture` holds `b->save_snapshot`), with different lifetimes.

## A5. §2, §3, §4.1, §6: "pinned os_fault lease" becomes generation-validated fault ids, no pins

PLAN_V2 asked for `os_fault_pin(slot, gen)` / `os_fault_unpin` + `os_fault_poll`. The Wave 1 spec
(docs/decisions/wave1-os-fault-lease.md, revision 40ba37b, signed off by the advisor) delivers validated lock-free
polling with no pins. Reason (Astra's answer to the advisor's question 1, by symbol at the pin): every production
reader of registered document bytes either owns the backing or borrows from an owner through its last access, so a
persistent pin count duplicates the backing refcount and prevents no further failure. Retire is owner-only at final
backing release; an unexpected "still in use" there is a contract violation (ASSERT; release builds keep the mapping).
Generation overflow: one comparison (ASSERT in debug, existing NOSPC in release) — the 584.5-year figure rests on an
(E) rate, so it is not a physical bound. CLAUDE.md's globals line said "pinned `os_fault_service`", meaning the fixed
global; reworded to "the fixed `os_fault_service` (... no pins)" so it cannot be read as the removed API.

## A6. §4.1 live revisions: add the edit/replay checkpoint and the current text of a published session leaf

From docs/decisions/wave1-piece-pool.md (correction b, accepted by the advisor): the plan's list of simultaneously
retained revisions also needs the forward-edit / undo-replay checkpoint (v1 `src/undo/undo.c` `begin_replay` is the
only production caller of `piece_checkpoint_begin` outside piece) and the published session leaf's current text.
The v2 holder list itself is being redone from PLAN_V2 §2/§4 (advisor challenge 3), not copied from v1's session graph.

## A7. §2 L1 piece: constant-time add-dir sharing at checkpoint begin is an acceptance condition

From the same file (correction c): at the pin `add_store_prefix` walks the chunk directory during checkpoint
**begin**, not per snapshot retain. O(1) directory sharing at begin is an acceptance condition of the Wave 1 piece
bead, tested so that a single linear-sized allocation or loop cannot hide behind an allocation-call count.

## A8. §6 X3 expected failure: v1 terminates, it does not save empty

Reproduced on the pinned stock build (bead v2-he7.15, docs/decisions/v2-he7.15.md): on a 33,619,968 B file
((512 + 1) × 65,536 (P), `LINEIDX_EDIT_MAX_CHUNKS` × `LINEIDX_CHUNK` + one chunk), Select All, Delete, Ctrl+Z makes
v1 exit with status 1 and print `sublimite: error -105` (over capacity): `replay_history` returns it
(`input.c:718`), the key handler propagates it (`input.c:1281`), the main loop treats any key error as fatal
(`editor.c:1376`), `main.c:342–344` exits 1. M3's "nothing happens, forever" is corrected in wave0-known-bugs.md.
The X3 XFAIL now names exactly this failure: exit status 1, stderr contains "error -105", no write to the file
(bytes and inode metadata unchanged), within the watchdog. Any other v1 outcome (hang, other error, a 0 B save,
harness ERROR) is not the expected failure. v2 expectation unchanged: undo succeeds, bytes round-trip (PASS).
X3 stays the sole XFAIL.

## A9. Wave 2 acceptance line for the input path: events apply in the order sent, across an in-progress open

Observed in W0.6h's red run on the pinned build: while a file is still opening, Ctrl+S can overtake keys v1 holds
back and the save silently does nothing (`editor.c:1064–1086`, `session.c:591`). v2 contract: input events are
applied in the order sent, including across an in-progress open. The contract navigation bead (v2-he7.12) carries
the case once the snapshot adapter exists; no case is added now.

## A10. §6 wave order: L0/L1 harvest starts once the scaffold blockers are closed

Advisor, 2026-11-23 10:10 (Tobias: "so are we working on v2 at all or what?"). The Wave 0 gate exists so behaviour
is specified before it is built. L0 (os, base, trace, work, ipc) and L1 have no behaviour the contract suite
specifies; their acceptance is their own unit suites, the linters and the lanes, and every scaffold finding that
blocked Wave 1 is closed (W0.3a/b/c, W0.4). New order: **L0 and L1 harvest beads may start once the scaffold
blockers are closed; L2 and minimal-L5 beads, and the Wave 1 exit, wait for the Wave 0 gate.** Wave 0 continues
in parallel and is still declared passed only by its gate.

## A11. Wave 2 acceptance line for viewport/layout: caret follow after Select All is independent of document length

From v1 finding 7 (W0.6h, docs/decisions/v2-he7.15.md): v1 spent 0.78 s quiet and 32.6 s under load before saving
an emptied 33.6 MB buffer, 78 % of samples in the caret follow after Select All on a wrapped view
(finish_move → fresh_follow → follow_rows → layout_wrap_follow_edit_anchored → layout_visual_row_fresh → scan_row,
which zeroes a whole layout per one-row query, wrap.c:548). v2 acceptance for the viewport and layout rewrite: the
cost of a caret follow after Select All does not depend on document length — stated as a bound on the number of rows
laid out, and tested by counting rows laid out, not by timing.
