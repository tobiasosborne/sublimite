Bead L2-2b-2: journal RECOVERY AND REPAIR on A28. It covers materialising the N2 reader's logical plan into piece,
RESTORE_BASE_RANGE per Q3, repair and set-aside. Written 2026-11-24 from the advisor's split of L2-2b; the advisor
reads it before dispatch. Depends on 2b-1 landed.

sublimité v2 is a clean-slate C11 editor rebuild. Your checkout is a worktree of /home/tobias/Projects/sublimite-v2
(branch v2). One worker, one bead: do exactly this scope, then stop. Routing: Claude or Muse, because the tests kill
processes and tear writes; never Codex.

Order: L2-2b is three beads that land in order (2b-1 writer, 2b-2 recovery and repair, 2b-3 rotation and the
exceptional path), each with its own lanes and owner read (advisor, 2026-11-24). Nothing of L2-2b is dispatched before
the N2 bead (wire amendment + pure two-pass reader, Sol) lands and the advisor signs the runtime section (artifact 1b).

The journal does not know undo exists (advisor). It offers a command group whose final record is reserved at begin and
cannot be refused later. The caller (edit) puts undo_commit between the last member and the final record. Journal
tests stand in for the caller with a hook between the two.

STARTING POINT: v2 after 2b-1. Restart from the wip at a94af15 (branch t3/journal-runtime) only where this KEPT list
applies. The rest is deleted, not adapted.
- KEPT:
  - version and required-feature refusal, with the image set aside intact;
  - CRC and global-sequence checks;
  - damage set-aside;
  - generation verification, verified-ORIGINAL versus by-value replay, and unresolved literals;
  - the stand-alone kill, damage, source-validation and allocation test cases.
- DELETED:
  - contiguous checkpoint replay (in_checkpoint, the one pending descriptor, the global touched-tree group);
  - single-global-group-phase stream validation and last-valid-before-BEGIN truncation (journal_stream_next/validate,
    recover_image's group phase);
  - the global-prefix tests.
- The model-to-real-tree move for every test this bead touches comes first.

Read first (absolute paths under /home/tobias/Projects/sublimite-v2):
1. CLAUDE.md.
   - Layering: L2 includes only L0/L1.
   - One public header; the journal_ prefix.
   - A15 (ASSERT only for caller bugs); Law 2 on the typing path.
   - Explicit FSM enums under -Wswitch-enum, with no coordinating booleans.
   - Checked allocations.
   - Tests link the archive, never #include a .c, and inject syscalls with -Wl,--wrap.
2. docs/decisions/wave0-plan-amendments.md: A28 (the write model, rules 1–8; binding), A13, A14, A19, A20, A22 (Q7's
   exclusive sections are superseded by A28), A23.
3. docs/decisions/wave1-journal-write-model.md in full (Astra; the section numbers below refer to it).
4. src/journal/journal.h: the wire sections as amended by the N2 bead, and the RUNTIME section as signed (artifact 1b of
   docs/decisions/wave1-journal-wire-amendment.md). Implement the signed header. Do not invent API; a missing call goes
   to the coordinator as an amendment request.
5. docs/decisions/wave1-L2-L5-harvest.md §0 and §2. v1 structure NOT to carry: unchecked arena sites, the sync spin, the
   private pool and message handler, the env CRC switch, seven coordinating booleans.
6. Headers: src/piece/piece.h, src/os/os.h, src/work/work.h, src/base/base.h.
7. The decision files of the earlier L2-2b beads, if any have landed.
8. Q3 in docs/decisions/wave1-L2-derivations.md (RESTORE_BASE_RANGE by reference only onto a verified ORIGINAL, else
   by value; a worker reads, the UI applies), and docs/decisions/wave1-L2-q2-protocol.md (K and M; T1–T5). v1 evidence,
   read-only: /home/tobias/Projects/sublimite-v1-oracle/src/journal/journal.c replay :269-316 and apply_piece :409-417,
   plus v1's journal_kill_test, journal_hot_exit_test and journal_safety_test.

Scope:
- RECOVERY. Call the N2 pure reader for the logical plan, then materialise it into piece.
  - Choose origins per buffer before opening any BASE.
  - Apply whole validated commands under the buffer's piece checkpoint, in final-sequence order. Checkpoint slices never
    enter command replay.
  - RESTORE_BASE_RANGE per Q3.
  - Refuse BASE-relative replay onto a file whose identity does not match, on every path. Report an unresolved BASE and
    offer the journal's literal bytes (A23).
  - Trees without history; piece is called directly.
- REPAIR (§2.7; the advisor's rule that repair must not destroy what it cannot explain).
  - Physical framing is tracked separately from logical completeness.
  - Orphan members are kept whole at EOF, and the allocator resumes past every retained record.
  - Only a demonstrably torn final record is truncated.
  - Complete corruption followed by more bytes sets the image aside intact under a set-aside name, and a new image is
    written from the valid prefix.
  - Never seek past corruption to apply a later marker.
  - Nobody deletes a set-aside image automatically; the user is told where it is.

SIGNED L5 ERRATA FOR THIS BEAD (advisor 2026-11-25, verbatim from docs/decisions/wave1-app-spec.md at a53a0f6):

**journal.h runtime.** Replace the BUFFER member in `journal_recovery_item.value` and extend its ownership comment.

```c
struct {
    uint64_t owner, current_id, saved_id;
    piece_tree *tree;
    journal_cutoff *pending;
} buffer;
```

> BUFFER transfers the original writer cutoff for a selected open PREPARED, otherwise NULL. Match it to the selected CONTROL by owner/token. Preserve its image, reserved closing credits, embargo and evidence. The cursor releases untaken trees and cutoffs. A taken cutoff transfers once to the recovering disposition owner. A mapped BUFFER tree uses a file-created piece_backing whose context owns the file_source for that mapping. Private and unresolved text require no mapped source. Retain EVIDENCE for each unresolved origin.

Erratum 2's restart sentence: "On restart advance past the preamble high-waters and every retained mention, including discarded owners, orphan declarations and TABS entries."

Acceptance (each a test, red first):
- PLAN §4.2 at journal level. Kill at every member and marker of a command group and of a checkpoint group, with
  acknowledged keys of the same and other buffers interleaved. Before ACK: pre-edit or whole-edit; after ACK: whole-edit.
  Port v1's journal_kill_test and journal_hot_exit_test.
- Astra's §1.5 cuts a–f as executable tests under K, and under M where a sync point exists:
  - a same-buffer key between checkpoint members;
  - a kill between BEGIN and final with acknowledged keys;
  - a sync through final, then a lost later key;
  - two buffers' checkpoints, the second closing first;
  - B's command among A's members;
  - abort, then a new command.
- Unknown version or required feature: refused and set aside without truncation.
- THE ORACLE COVERS THE WIRE (advisor, from 1.29's lesson: the oracle shared all four N2 defects because its generator
  never emitted GENERATION, BASE or SAVE): the flat oracle's generator emits every record type and role of the signed
  wire, and the report lists, per type, how many generated cases contained it. A type with zero is a failed
  acceptance line.
- Repair:
  - a torn final record is repaired in place;
  - a complete record with one flipped bit, followed by valid records, leaves the original bytes byte-identical under
    a set-aside name, recovery reports it, and the session continues on a new image;
  - interleaved orphan members are kept, and later committed records of other buffers survive.
- A23 T5 at journal level: a BASE replaced by a foreign file recovers as "unresolved BASE" with literals offered, never
  applied to the wrong file. A self-contained buffer recovers exactly after its BASE is deleted or replaced.
- Measurement 2 (A22, Q3): on the 10 GiB class, peak ADD extents, piece nodes, scratch, RSS and temporary disk for
  recovery of a mixed-generation checkpoint with repeated ranges.
- DISK HYGIENE for Measurement 2: run df on the build filesystem before the run, and refuse to start without the fixture
  size plus a stated margin free. Fixtures only under the checkout's build/, removed on success and on failure. Sizes
  carry derivations.
- check-tsan on the journal suites. Fuzz: append/recover sequences with interleaved groups and torn tails (the N2
  reader's fuzz covers the pure reader).

Rules:
- No git (not even git add -N), no bd.
- No rm -rf, no mktemp with a clean-up trap. Nothing outside your checkout, fixtures included.
- HOME for cache-writing tools under build/. Build under build/.
- At most nice make -j2. Run date before each.
- Report rather than run anything needing approval.

Acceptance lanes: nice make -j2 check (CONTRACT_CHECK_DISPLAYS from the coordinator), check-tsan, fuzz-smoke.

Report:
- red/green per acceptance line;
- the wip symbols kept and deleted;
- lines removed vs added;
- the comment-free count per file (gcc -fpreprocessed -dD -E -P f | grep -c '[^[:space:]]') against v1's journal
  (/home/tobias/Projects/sublimite-v1-oracle/src/journal/);
- anything outside scope (report only).
