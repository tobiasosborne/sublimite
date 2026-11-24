Bead N2: the journal wire amendment and the pure two-pass reader (A28 N2). The advisor signed the wire amendment and the
reader contract on 2026-11-24. sublimité v2 is a clean-slate C11 editor rebuild; your checkout is a worktree of
/home/tobias/Projects/sublimite-v2 (branch v2). One worker, one bead: do exactly this scope, then stop.

Routing: Sol. This bead does no file I/O and has no kill, fault or truncation test. It works on byte buffers only. If a
case seems to need such a test, list it for a Claude worker; do not write it.

Read first (absolute paths under /home/tobias/Projects/sublimite-v2):
1. CLAUDE.md:
   - L2 includes only L0/L1;
   - one public header; the journal_ prefix;
   - A15 (ASSERT only for caller bugs; input is a return code);
   - Law 4;
   - red-green TDD;
   - explicit FSM enums under -Wswitch-enum;
   - no #include of .c files in tests.
2. docs/decisions/wave1-journal-wire-amendment.md, lines 1 to the heading "Proposed runtime section of journal.h". This
   is the SIGNED text: the wire header text, the reader contract, and "Landed tests affected". Implement it as written,
   including the saved-association rule at FINISHED. Do NOT implement the runtime section after it; it is unsigned.
3. docs/decisions/wave0-plan-amendments.md A28 and its additions at signature.
4. docs/decisions/wave1-journal-write-model.md §1.3–1.6 and §2.
5. src/journal/journal.h and src/journal/ (landed 2.1: wire + pure codec), docs/decisions/L2-2a-journal-wire.md,
   tests/journal_codec_test.c, fuzz/journal_fuzz.c, fuzz/corpus/journal/.

Scope:
- The wire amendment in src/journal/journal.h and the codec:
  - typed roles in the type word (standalone, command member, checkpoint member, and their BEGIN/COMMIT);
  - the owner buffer id on every marker and member, with illegal type/role/owner combinations rejected;
  - the slot grammar and supersession;
  - append-only SAVE semantics, with the staged declaration activated at a complete FINISHED;
  - the saved-association rule;
  - the PAD seal aligning the absolute FILE end (the defect in landed 2.1).
- The stream validator: physical framing, CRC and global sequence, kept separate from logical completeness.
- journal_plan_read: a pure two-pass function from image bytes and caller workspace to a logical plan. Its memory bill is
  the signed one (O(R + B + D + T) words; at most 2 slot heads per buffer; one window ≤ 1 MiB). State the workspace
  sizing with its derivation.
- Update the invalidated landed tests, by name:
  - tests/journal_codec_test.c: test_pad, test_checkpoint_save_view_tabs, test_stream_valid,
    test_stream_torn_and_invalid, test_stream_checkpoint_and_save;
  - fuzz/journal_fuzz.c: check_typed, structured, check_prefix;
  - corpus seeds: checkpoint-by-value-group, torn-tail-group.
  Each change follows the reason given in "Landed tests affected".

Acceptance (each a test, red first):
- A FLAT ORACLE for committed effects that never calls the planner. It is a straight-line model: for an image, the set
  of committed commands and checkpoints with their effects. The planner's plan is compared against it on generated
  interleavings.
- Astra's §1.3 sequence (two checkpoint BEGINs on one buffer before one COMMIT). The second BEGIN abandons the first,
  and the plan names exactly one committed instance.
- §1.5 a–f as byte-level images. The plan selects what each K column says. Where M applies, a truncated surviving
  prefix selects what the M column says.
- An incomplete slot has no logical effect, and interleaved committed commands of other buffers survive it.
- A FINISHED whose installed declaration or path verification is absent yields an origin candidate, never a saved id.
- PAD: the sealed end is aligned in absolute file offsets (red against landed 2.1's stream-offset formula).
- Illegal type/role/owner combinations are rejected as damage, with the offset reported.
- Fuzz over interleaved slots and torn tails, with the flat oracle as the property. Extend fuzz/journal_fuzz.c and the
  corpus.
- The workspace bound: a test at the stated R, B, D, T sizes asserts the planner never exceeds the caller workspace,
  with NOMEM on undersize as a return code.

Rules:
- No git (not even git add -N), no bd.
- No rm -rf, no mktemp with a clean-up trap. Nothing outside your checkout is created or removed.
- HOME for any tool that writes caches is under your checkout's build/. Build under build/.
- At most nice make -j2.
- If a command would need approval, do not run it: report it and continue.

Acceptance lanes: nice make -j2 check CONTRACT_CHECK_DISPLAYS=:180-:189, nice make -j2 check-tsan,
nice make -j2 fuzz-smoke.

Report:
- red and green per acceptance line;
- lines removed vs added;
- the comment-free count per file (gcc -fpreprocessed -dD -E -P f | grep -c '[^[:space:]]') beside landed 2.1's codec;
- any test listed for a Claude worker;
- anything outside scope (report only).
