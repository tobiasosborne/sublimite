PROVISIONAL (until undo v2-0fv.8 lands with its frozen-as-amended header, and the journal beads L2-2/L2-3 land; then re-read and dispatch)

Bead v2-0fv.15 — W1 L2 edit: the layout-free edit kernel (dossier bead L2-5; the command plans from v1 view/edits.c are L2-6, a separate bead). sublimité v2 is a clean-slate C11 editor rebuild; your checkout is a worktree of /home/tobias/Projects/sublimite-v2 (branch v2). One worker, one bead; do exactly this scope, then stop.

Read first, in order (absolute paths under /home/tobias/Projects/sublimite-v2):
1. CLAUDE.md (Law 2: no malloc on the typing path; A15: caller bugs ASSERT, exhaustion is a return code; explicit FSMs under -Wswitch-enum; one public header per module).
2. docs/decisions/wave1-L2-L5-harvest.md §0 and §4 in full (sources, API proposal, dependencies, the acceptance lines already filed, the v1 structure you must NOT carry (a)–(g), tests to port), and §6 answers as settled in docs/decisions/wave1-L2-derivations.md (Q7 work pool, Q8 transaction, Q10 dirty state / content ids, Q15 input order).
3. docs/PLAN_V2.md §4.2 (edit ownership and the ack fence, steps 1–4); docs/decisions/wave0-plan-amendments.md A9, A12, A13, A14, A19, A20.
4. The frozen headers: src/piece/piece.h (incl. Lines and the erratum), src/undo/undo.h (as landed), src/journal/journal.h (as landed), src/os/os.h, src/work/work.h.
5. docs/decisions/wave1-piece-pool.md "Synchronous admission" and "Acceptance at the edit boundary".
6. Evidence only: v1 /home/tobias/Projects/sublimite-v1-oracle/src/editor/input.c (mutate_text, delete_part, stage_*, editor_journal_staged, remember, end_edit_group, finish_edit) and src/view/view.h (selection and plan types). Read-only.

The contract you build on (settled):
- Undo owns a logged tree's checkpoint. A forward command is undo_begin(log, before, join) -> undo_replace(...)* -> [journal staging: prepare the command's journal records from the staged text, private until success] -> undo_commit(log, after) -> publish the journal staging. After an undo_replace error the only legal call is undo_abort, and the journal staging is discarded. undo_commit's only error is CONFLICT. Edit never calls piece_checkpoint_* on a logged tree.
- Burst policy is edit's, as product behaviour: join = true when the command is the same kind as the previous one (typing, forward delete, backspace), adjacent, and within 300 ms on the monotonic clock ((G) inherited v1 product behaviour: v1 undo.h UNDO_BURST_NS at the oracle pin, cite it); a paste, a command of another kind, a selection change, or a save starts a new group (join = false). Undo checks only structural adjacency.
- One post-submit maintenance function, the only caller of the four: piece_lines_resolve, then piece_replenish, then piece_reclaim, then undo_maintain; outside the input-to-submit window; no resolve while an undo replay is yielded (undo.h names the calls forbidden during a yield).
- Every compound command is atomic: on any refusal nothing changed — bytes, selection, history cursor, save cutoff and journal sequence all as before — and the refusal is visible ("Edit not applied — buffer memory is full."), never a silent count.
- No newline scan on the edit path (line counts live in piece, A19); no history ring in edit (undo's group_serial and content identity are the source of truth for "modified", per the Q10 answer).
- Selection, transaction state and the journal staging queue live in edit_buffer; the transaction state is an explicit enum (e.g. IDLE, OPEN, STAGING, REPLAY_YIELDED, SUSPENDED — whether OPEN and STAGING are separate is yours to derive), handled by switch under -Wswitch-enum.
- Output is a change record {off, old_len, new_len, revision} per applied command, polled by L4/L5 after submit — never a call up into layout, scroll or frames.
- Layout-free: soft-row commands take [start, end) from the caller; comment commands take the comment style as an argument.

Acceptance (all filed lines, each a test, red first):
- PLAN §6 Wave 1 exit: the edit runs under the live release malloc guard (active && count == 0) for typing, delete, compound replace, undo and redo.
- A13: delete and undo of a mapped range of any length copy at most min(length, 16 x spans) ORIGINAL bytes with O(spans) inverse storage (one-byte literal, one-span large range); a one-span delete + its undo read at most 131,072 B of mapped ORIGINAL (P, 2 x 2 x 32,768), counted with piece's read counter, not timed.
- A19: after ORIGINAL is counted, line queries are exact after any edit or undo of any size, in O(h) node visits, counted; fixture: the 513-chunk delete and its undo, line queries checked against the model before, between and after.
- The maintenance-order line above, with a test that fails if any of the four is called from elsewhere or out of order.
- Piece-pool synchronous admission: a failed post-submit warm-up followed by a fitting key; exact synchronous top-up inside the active guard; an injected necessary base refusal (visible refusal, nothing changed); a later successful command. Back-to-back keys, compound replace, tab switch.
- Undo review MUST-FIX 2 trace at the edit level (private tree, insert then delete, abort, capture, undo) and a literal capture under the same command.
- Ported: v1 journal_group_test (crash at AFTER_DELETE / BEFORE_COMMIT / COMPLETE x UNDO/REDO/REPLACE/PASTE), dataloss_test torn_undo_group (a 256 KiB replacement undo killed mid-staging restores to length 1 or 262,144, never a prefix), editor_undo_branch_test re-targeted at edit + savectl. Crash cases must NOT be routed to Sol (cyber filter): this bead goes to Opus.
- Bench bench/edit_bench.c: typing begin-replace-commit p50/p99, stamped [bat]/[AC] + load; a miss is a note.

Open until settled (do not improvise; report if the settled answer is missing): the byte cap undo gets (undo's open question), the content-identity owner (Q10), the input order across an open (Q15).

Rules: no git, no bd; no rm -rf; no mktemp with a clean-up trap; nothing outside your checkout; HOME for cache-writing tools under build/; build under build/; at most nice make -j2; no starts Tue 24 Nov 03:45–05:00 CET; report rather than run anything needing approval. Acceptance lanes: nice make -j2 check (CONTRACT_CHECK_DISPLAYS from the coordinator), check-tsan, fuzz-smoke. Report red/green per acceptance line, lines removed vs added, anything outside scope (report only).
