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

*Key sequence, amended 2026-11-23 (W0.7-B3/B4, bd-v2-he7.28).* The expected failure is an exit in Undo:
1. Select All, then Delete.
2. **Delete fence.** The session journal commit whose checkpoint holds `DELETE(0, 33,619,968)` for the buffer
   (v1 `checkpoint.c:124`).
3. Ctrl+Z.
4. **Undo fence, or exit.** The editor exits before v1's appended undo group (GROUP_BEGIN, INSERTs restoring
   [0, L), GROUP_COMMIT; `input.c` `replay_slice` `stage_tree`) appears, and before any Ctrl+S is sent. Exit status
   is 1, and the last stderr line is `sublimite: error -105; journal: <its session journal>`.

Ctrl+S goes only to a live editor after the undo group.
- *Why the two fences.* An acknowledged key proves delivery, not that the editor finished the key before it. Two
  stubs that were signed before their fence are now ERROR:
  - one dies processing Delete (signed at 2abe1cf);
  - one journals the undo and dies on Ctrl+S (signed with the Delete fence alone).
- *Re-measured on the pin.* Runs `20261123T200354Z-512694`, `20261123T200410Z-513344` and
  `20261123T200427Z-513937` (BAT0 Not charging [AC], load 4.8-7.4):
  - the over's delete commit came 0.997-1.004 s (M) after Delete;
  - the exit, status 1 with that line, came 21-23 ms (M) after Ctrl+Z, during the undo wait;
  - no Ctrl+S was sent, and the destination identity was unchanged;
  - the twin's undo group committed 2.03-2.21 s (M) after Ctrl+Z, and the twin round-tripped (PASS).

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

## A12. Wave 2 acceptance line for L3 X11 present: no UI-thread read from a server connection between ingress and submit

From v1 finding 8 (W0.9, docs/decisions/v2-he7.23.md): in backend submit, v1's cpu_paint_margins
(src/raster/raster.c:411) calls xcb_flush on the UI thread; libxcb read_packet then mallocs 36 + 16 B per pending
Present packet on the raster connection. v2 rule, owned by the L3 X11 present bead: between ingress and submit the UI
thread performs **no libxcb call that can read from a connection (flush included)** — stated as "no read", and tested
with the live allocation guard, not by inspection. The v1 adapter patch is not changed; on v1 the count stays recorded.

## A13. §6 Wave 1 exit: bounded-copy delete/undo gate replaces ">32 MiB delete+undo"

Signed piece-pool spec (docs/decisions/wave1-piece-pool.md, 10(b)): delete and undo of a mapped range of any length
copy at most min(length, 16 × spans) ORIGINAL bytes and complete with O(spans) inverse storage, with a one-byte
literal and a one-span large range as discriminating fixtures; the 513-chunk `lineidx` degraded/regrowth regression is
kept. Advisor addition: for a one-span delete and its undo on a mapped range, the UI thread reads at most the two
partial boundary chunks of ORIGINAL, ≤ 2 × 65,536 = 131,072 B (P, one partial chunk at each end; interior chunks take
newline counts from the index or the inexact geometry of §4.3), tested by counting mapped bytes read — otherwise an
implementation could copy nothing and still fault in 10 GiB (v1's delete_part scans newlines before history capture).

## A14. Consequences of the span-sequence inverse outside L1 (acceptance lines for the owning L2 beads)

(a) journal: a versioned record for a BASE-range restore plus the durable generation declaration; the v2 journal is
never offered to a v1 parser. (b) savectl/edit: generation retention across saves — an undo naming a pre-save
generation needs that generation durable before it is acknowledged, which can cost a file-sized on-disk copy off the
typing path. Option for the owning bead to evaluate (not a requirement): before the save's rename replaces the path,
hard-link the old inode into a retention directory on the same filesystem (O(1), no copy); fall back to reflink, then
copy, across filesystems; the bead states which and why. (c) checkpoint-diff consumers can no longer assume ORIGINAL
spans appear once in increasing order. (d) the L1 dossier's piece and undo sections are superseded where the spec
says so. Each becomes an acceptance line of the L2 bead that owns it (journal, savectl or edit), written when those
beads are prepared after the Wave 0 gate.

## A15. One always-on ASSERT; exhaustion is a return code (advisor, 2026-11-23)

Raised by L0-2 (fault ids): "ASSERT in debug, NOSPC in release" had no meaning because no lane defines NDEBUG, and os
sits below base. Rule (now in CLAUDE.md code conventions): one ASSERT macro for the whole tree, defined in os, always
compiled in; a failure writes a fixed message async-signal-safely and aborts (an invariant violation means memory
state is not what the code believes; aborting leaves the last journaled state for recovery). ASSERT only for
conditions no input, file content, resource exhaustion or timing can produce; everything else is a return code.
Expensive checks use a second macro compiled only in the sanitizer lanes, each use saying why. base's own assert is
removed or becomes this one; only os imports abort (the per-module allowlist expresses it). Consequence for the fault
spec: generation overflow is one comparison returning OS_E_NOSPC in every build (no ASSERT), testable at full width
through a test-build-only setter for the next generation (one registration at UINT64_MAX − 1).

## A16. Wave 2 acceptance line for font and layout: text presentation by default for Emoji_Presentation=No

From L1 utf8 (v2-0fv.11): v2 now classifies © U+00A9 and ® U+00AE as Extended_Pictographic. That is a segmentation
property, not a presentation property; both have Emoji_Presentation = No, so their default presentation is text.
Acceptance for the Wave 2 font and layout beads: a bare © or ® (no U+FE0F) is drawn from the text font at width 1;
it takes the emoji font and width 2 only when followed by U+FE0F. The same rule holds for every Extended_Pictographic
code point whose Emoji_Presentation is No (❤ and ‼ among them), and the test enumerates that set from the tables,
not from a hand list. If v1 at the pin sends bare ❤ or ‼ to the emoji font, that is a v1 rendering difference to
record, not a behaviour to carry.

## A17. Wave 3 acceptance line for session restore and tabs: opening or restoring N tabs is O(N) total

From R4 (v2-he7.17, docs/decisions/v2-he7.17.md, measured (M) [AC, load 0.67]): v1's per-open handoff cost grows
linearly with the live tab count — 128-path handoffs took 0.3, 0.6, 0.9, 1.2, 1.5 and about 1.8 s at 1, 129, 257, 385,
513 and 641 tabs, and timed out at 769 — so filling 1000 tabs is quadratic. The worker measured the growth but did not
name the per-tab operation responsible; the Wave 3 session-restore/tabs bead first identifies it by symbol at the pin
(starting from src/editor/buffers.c editor_open_request), then states the acceptance: opening or restoring N tabs
performs O(N) work in total, tested by counting that per-tab operation, not by timing.

## A18. Fault-service chain and install cut to their one client; seq_cst throughout (advisor, 2026-11-23)

The advisor's earlier signature of the spec's chain section "as in v1" was v1 structure by the back door; Astra's
must-fixes 1 and 4 on the L0-2 diff were its price. The chain keeps only what its one named client needs (a callable
prior SIGBUS handler: the sanitizer runtime and libFuzzer); SIG_IGN emulation, SA_RESETHAND emulation, mask/flag
inheritance, the second cell/selector and the install-race machinery are cut. Memory orders in the fault service are
seq_cst throughout with no _explicit forms (x86-64 cost derivation in the spec). Spec rewritten:
docs/decisions/wave1-os-fault-lease.md, section "Chain and install (A18)".

## A19. Line counts live in the piece tree; PLAN §4.3 superseded (advisor, 2026-11-23, at the piece.h freeze read)

Newline counts are a monoid annotation (bytes, newlines, unknown) on the piece tree's leaf and branch entries, keyed by
source coordinates and maintained in O(h) by each edit; ORIGINAL is counted off path per 64 KiB chunk into a core-owned,
revision-independent table. Design, invariants and suite contract: docs/decisions/wave1-piece-lines.md (Lines section
under Astra review until frozen). Consequences:
- PLAN §4.3 is superseded: no incremental cap (`LINEIDX_EDIT_MAX_CHUNKS`), no degraded mode, no off-path regrowth, no
  resumable goto-line continuation.
- §6 Wave 1 exit (A13's last sentence): the 513-chunk `lineidx` degraded/regrowth regression is replaced by: "after
  ORIGINAL is counted, line queries are exact after any edit or undo of any size, in O(h) node visits, counted; fixture:
  the 513-chunk delete and its undo, with line queries checked against the model before, between and after".
- `lineidx` (v2-0fv.18) is rescoped to the off-path chunk counter and the estimate used while a prefix is unknown; v2-0fv.9
  follows it.
- After Astra's review (docs/reviews/wave1-piece-lines-astra.md; advisor rulings the same day): counts travel with
  spans in an optional 8 B (head, tail) record kept by undo, never inside the 16 B piece_span; piece_replace_spans reads
  no byte; edit-path cuts scan only the shorter half of a counted descriptor. One replace reads at most 2 × 32,768 =
  65,536 B of mapped text (P). A one-span delete plus its undo reads at most 131,072 B, A13's signed figure (advisor,
  withdrawing an unforced tightening to 65,536): undo describes the range first, scanning up to 32,768 B at each end
  inside a counted descriptor, and the delete repeats that scan, so bytes read are at most 2 × 2 × 32,768 = 131,072 (P)
  while distinct mapped bytes touched stay at most 65,536 (P). Removing the repeat would need a combined
  describe-and-delete primitive or a cut that is an edit of its own, reopening the frozen Editing section for no
  input-visible gain. The test counts bytes read against 131,072. The text length is held at or below a named maximum 2^60 by an ASSERT at every edit
  (the 2^47-byte address-space argument holds only on 4-level paging). Undo literal threshold stays 16 × spans > length;
  the in-memory inverse entry is 24 B.
- At the Lines freeze (advisor, 1.18): `lineidx` has nothing left to own and is removed from L1 (mk/layers.mk, the DAG
  rules, PLAN §2/§3); v2-0fv.18 closed as superseded. Its two remaining duties go to their callers: scheduling the
  piece_lines_count job is an acceptance line of the L5 buffers bead (after the Wave 0 gate); the estimate shown while a
  prefix is unknown belongs to the layout bead, with one line: one rank per frame for the first visible row, never one
  per row. v2-0fv.9 is held until L5 buffers lands the counting job. Post-submit maintenance (resolve, replenish,
  reclaim, in that order, from one function that is their only caller) is an acceptance line of v2-0fv.15.
- The fault-read rule is one closing poll after the last dependent load (epoch 0, generation match); the opening poll
  was cut at the freeze (wave1-os-fault-lease.md "Read bytes", amended).

## A20. Memory and count gates carried from v1 are replaced by v2's own exact derivation (advisor, 2026-11-23, at 7b)

A memory or count gate carried from a v1 fixture is replaced by the v2 design's own exact (P) derivation for that
fixture, asserted exactly where the quantity is a count (slots, extents) and by stated derivation where it is bytes; the
v1 figure becomes a recorded comparison, not an assertion. A loose outer (G) bound may stay as a second check but never
as the only one: a bound with large slack lets a real leak pass. First application, v1 `version_headers` under 7b: live
slots == 950 + 5,000 × 5 = 25,950 (P: 833 leaves + 104 + 12 + 1 branches for 10,000 uncoalescable pieces at the signed
fanouts; one leaf-to-root path of 4 slots and one header per held version); extents ceil(25,950 / 10) × 4,096 =
10,629,120 B, plus the page-rounded ADD chunk 69,632 B, plus the fixed terms each with its own derivation; the signed
11,608,382 B outer bound also holds; v1's runtime formula at the actual 10,000 pieces (10,648,382 B) is recorded as a
comparison (v2 is 0.47 % above it: the 96 B per page a 400 B slot leaves). Re-deriving the slot geometry to win that
50 KB, or charging requested bytes instead of mappings, were both rejected.

## A21. Later-wave leaves may start early; the TOML parser owns no pool (advisor, 2026-11-23, after the Wave 0 gate)

- Extension of A10: a later-wave leaf module may start when everything it includes has landed on v2 and its dossier
  section exists (docs/decisions/wave1-L2-L5-harvest.md §8). Its wave's exit gate still gates integration, and it is in
  that wave's whole-wave Astra read like the rest. Nothing that touches a display, and nothing whose design the gate or
  the piece implementation could still change. First uses: LF-1 `dnd` (L4) and LF-2 the config TOML parser.
- PLAN §2's "transactional TOML parser + private pool" was written before anyone read the parser. If v1's parser
  allocates nothing (it reports into caller storage or through callbacks), it is harvested that way with no pool; if it
  needs memory for the parsed result, the caller passes a base arena and the parser owns nothing. In neither case does
  the parser own a pool (a pool it does not need is state). PLAN §2 corrected.

## A22. Rulings on the L2 derivations (advisor, 2026-11-23; docs/decisions/wave1-L2-derivations.md)

- Q6 accepted as the design: one os_file_pwrite_once per encoded journal record on the UI thread (the contract is that a
  drawn edit survives a kill; a worker design must fence the draw on the worker's write anyway). Wave 1 exit number:
  input-to-submit median and tail with the append, during ordinary typing and under heavy writeback from another
  process, stamped [bat]/[AC] + load. If the tail fails G1, the comparison is a preallocated shared mapping of the
  journal segment, not a worker.
- Q12 accepted: on OS_E_NOSPC a mapped open or reload is refused visibly. OS_FAULT_SLOTS = 256 is derived from the copy
  threshold (only files at or above it are mapped; v1 256 MiB, so a 257th distinct mapping needs at least 64 GiB of
  open files (P)), written beside the constant in os.h; L2 file's copy threshold carries the note that the slot count
  is derived from it.
- Q3 accepted as the design; its 10 GiB measurement (peak ADD extents, nodes, scratch, RSS, temporary disk) is an
  acceptance line of L2-2.
- Q2, Q4, Q11 NOT settled: challenged by the advisor (no retention of the old file at all: (a) a synced save-prepared
  record naming the written temporary's identity and the journal position of the save snapshot; (b) from the moment a
  save of generation G is prepared, restores from G are journaled by value; (c) in-place rewrite of a mapped file as
  already decided). Back to Astra for a kill-at-every-step trace. os_file_clone and os_link_at are not built until it
  is decided. It gates L2-2's record set and L2-3, not L2-0a or L2-1.
- The other ten (Q1, Q5, Q7, Q8, Q9, Q10, Q13, Q14, Q15, Q16) accepted as one-liners; each is read again in its
  bead's brief.

## A23. The save protocol: no retention; rules 1–7; T5 is a stated product contract (advisor, 2026-11-23)

Astra's two crash-consistency passes (docs/decisions/wave1-L2-q2-crash-consistency.md, wave1-L2-q2-protocol.md).
Rules, decided:
1. No retention of a replaced file: no clone, no retention artifacts, no startup retirement scan of them. os_file_clone
   goes on v2-0fv.21's deletion list; os_link_at is not built.
2. The saving buffer: one synced SAVE(PREPARED) record before the rename carries the synced temporary's identity (fd
   witness) and the journal position of the save snapshot S; recovery picks old replay or replay from S by the target's
   actual binding.
3. From the capture of S for a save of generation G, and ever after, a restore by reference from G is journaled by value.
4. Before a rename replaces G, every other journal-recorded buffer state whose BASE is G, live or closed-but-kept, has a
   durable self-contained checkpoint (CHECKPOINT_BY_VALUE, synced). Worst case file-sized, paid only when such a state
   exists.
5. The same self-contained checkpoint is what a buffer writes when the file owner detects a foreign replacement of its
   BASE: one mechanism, two triggers.
6. One buffer per file identity (device and inode) for open, Save As, restore and reopen, through one index; recovery may
   still produce two drafts of one target (v1 at the pin deliberately does), and those are peers under rule 4.
7. In-place rewrite of a mapped inode: detected, conflicted, reload into a fresh tree and history; zero bytes copied at
   detection.
T1–T4 hold (Astra). T5 (a foreign rename after our rename, then our death before the conflict checkpoint) breaks; ruled
(b), a stated product contract, with a shrinking rule:
- Self-contained size: a buffer whose text is at or below a stated size is made self-contained in the journal
  (CHECKPOINT_BY_VALUE of its text) once it has an acknowledged edit, written off the typing path by the worker; from then
  on nothing it needs can be replaced by anyone. Only larger buffers stay BASE-relative and carry T5's exposure and the
  ordinary external-replacement one. The size is derived (measured journal append rate and what a worker may write per
  checkpoint without delaying other journal work; v1's whole-text versus delta split at the pin cited as evidence), in
  L2-2's brief. Until that checkpoint is durable the first edits are BASE-relative as designed; the typing path does not
  change.
- Contract sentence (PLAN and Tobias): "For a file above the self-contained size, if another program replaces the file
  and the editor is killed before it has written its conflict checkpoint, the unsaved difference can be lost; the
  conflict is always detected and reported, never silently applied to the wrong file."
- Recovery refuses to replay BASE-relative records onto a file whose identity does not match, in every path; L2-2
  acceptance line with T5 as its test (expect a reported unresolved BASE and the literal bytes the journal does hold
  offered to the user).
Consequences: Q2 (retention) answered by rules 1–5; Q4 (ack across a save) by rule 3 (acknowledge an undo from G once its
by-value group is accepted); Q11 (buffers sharing a path) by rules 4 and 6. L2-3 is ordered preparation, peer
checkpointing, finish and cancel. L2-2's record set: SJNL preamble + PAD, GENERATION, BASE, TARGET, INSERT, DELETE,
RESTORE_BASE_RANGE, GROUP_BEGIN/COMMIT, CHECKPOINT_BY_VALUE, SAVE(PREPARED/FINISHED/CANCELLED), VIEW, TABS.

## A24. 7b source-read rulings (advisor, 2026-11-24; docs/reviews/wave1-piece-7b-source-astra.md)

- E: piece_lines_resolve funds its copies from the core's free slots, the reserve and base, and never walks the
  retirement queue; when those do not suffice it returns PIECE_ERR_NOMEM with nothing changed, and the caller's next
  piece_reclaim (its own budget) frees what is queued. "One call visits at most budget nodes" then holds as written.
  The post-submit maintenance order becomes: piece_reclaim, piece_lines_resolve, piece_replenish, then undo_maintain,
  from one function that is their only caller (corrects A19's "resolve, replenish, reclaim"; v2-0fv.15).
- D: ownership decided on the visited path, so an update that needs no slot never asks for one.
- F: core ownership and snapshot owner counts become 64-bit; every owner occupies storage of its own, so 2^64 owners
  cannot exist in a 2^47 or 2^56-byte address space (P); the overflow ASSERT stays as the statement of that bound,
  with the precondition in the header. No saturation logic.
- C: no shorter-side logic for private cuts; a cut inside private storage scans at most 511 B (P, one 512 B prefix block
  less one); the Lines text is corrected where it says PIECE_CHUNK / 2 per end for private sources.
- A, B, G as Astra states them; for A the rule beside the code is the header's sentence: NOMEM exactly when base refused
  the remainder, the remainder computed after the last funding step that can change it.

## A25. Piece admission as a fixpoint over the exact plan; resolve's time bound (advisor, 2026-11-24; Astra's 7b re-read)

- Admission: base is never asked for the scalar over-estimate. (1) Fast path: the scalar claim from local sources only
  (core free slots, then the reserve's whole extents); if they cover it, proceed with no plan and no base call — the
  typing path after a successful replenish. (2) Otherwise the exact plan: slots and companion storage (ADD chunks, the
  boundary chunk, directory nodes) as one requirement. (3) Funded as a fixpoint: remainder from current availability;
  zero -> proceed; drain retirement, take the reserve's offer, recompute; ask base for exactly the remainder; on refusal
  re-read availability and go round again if anything changed since the remainder was computed; PIECE_ERR_NOMEM only
  when base refused a remainder computed from availability that did not change afterwards. Terminates: each round
  reduces the remainder or ends. Replaces A24's "keep the admission order" and the decision file's invariants 1 and 2
  (invariant 3 stands); the scalar-claim extent purchase and its "returned by the next replenish" excuse are deleted.
- Resolve's contract: one call descends into at most budget nodes; each branch it copies also updates the reference counts
  of that branch's children, at most 16 per copy (P, the branch fanout), so a call touches at most 17 x budget nodes.
- The sweep's state is restored as a whole (copy, count, cursor, flag) when a tentative copy is discarded.
- A bound that justifies an ASSERT is structural or address-space, never installed memory (node reference counts:
  widened if the signed node prefix allows, else saturating at UINT32_MAX — a never-freed node, never an abort).

## A26. Piece admission: exact at quiescence, a sequence not a loop (advisor, 2026-11-24; supersedes A25's fixpoint)

Reason: a point-in-time refusal cannot be made exact against concurrent release — a worker can always free storage one
instruction after base has refused us (Astra's third 7b read, finding I); no ordering of signals and no supply epoch
closes that window, a fixpoint only narrows it. Four rounds on A25's rule were the signal.
Contract (piece.h STORAGE): exactness is promised at quiescence. With no release by another thread in flight, a call
returns PIECE_ERR_NOMEM only when base refused the exact remainder after every local source was used. While another
thread is releasing storage, a call may be refused that an immediate retry would not be; nothing latches, the next call
tries afresh.
Procedure (at most two plan computations and one base request per call):
1. Fast path: the scalar claim AND the companion storage the command needs (ADD chunks, the boundary chunk) are both
   present locally: proceed, no plan, no base call.
2. Otherwise compute the exact plan; if its remainder is zero, proceed (no drain).
3. Otherwise use every local source once: drain this tree's retirement queue, take what the reserve and its inbox offer,
   then compute the exact plan again, once (the drain can remove the sharing that made copies necessary); if the
   remainder is now zero, proceed.
4. Return to base every whole free extent beyond the exact slot need, then ask base once for exactly the remainder:
   granted -> proceed; refused -> PIECE_ERR_NOMEM, nothing changed.
Tests: I's and A(1)'s traces assert the contract (refused during the race, the identical retry succeeds, nothing
latched); J and K test steps 4, 2 and 1. piece_fund_exact's loop and its predicates are deleted (a deletion round).

