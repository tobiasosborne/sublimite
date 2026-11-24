# N2 derivation review

Four MUST-FIX findings and three NOTEs. The first finding changes the certified physical prefix. The other three concern logical decisions on physically complete images. This is a source read of landed 1.27 against the signed wire text and A28.

## Wire conventions for the examples

Each example starts with a valid 32-byte version-1 SJNL preamble, features zero, generation high-water zero unless specified, and buffer high-water 9. Each listed frame has SJR1 magic, the §6 little-endian payload, its exact total length, and a correct record CRC32C. Sequence numbers start at 1 in the order shown. The owner is 9 except for GENERATION, whose owner is 0. Omitted flags, times, identities for ABSENT, and reserved fields are zero. Paths have their indicated byte length and no terminator.

## Findings

### MUST-FIX 1. A nonzero witness for an empty prefix passes physical certification

`src/journal/codec.c:232` checks the prefix length and skips the prefix CRC. The retained GENERATION schema explicitly specifies zero for the CRC of an empty prefix at `src/journal/journal.h:226`. This omission predates N2 and is reachable through its new public reader.

**Input.** One GENERATION, total 106, key 1, identity (volume 1, inode low 2, all other words zero), payload u32s at offsets 56, 60, 64 and 68 equal to 0, 1, 0 and 2 respectively, followed by the bytes `/a`. The frame CRC is correct even though the embedded prefix CRC is 1.

**Result.** The reader returns OK, publishes an eligible declaration, and reports `valid_bytes=138, records=1, last_sequence=1`. The required result is INVALID at file offset 32 with zero records and no declaration. All bytes needed to disprove this witness are in the frame. The empty-source fixture sets both fields to zero and its rejection cases vary the length and flags. See `tests/journal_codec_test.c:263`.

### MUST-FIX 2. PREPARED can allocate a generation key already consumed in the lineage

`src/journal/reader.c:341` checks only the current image's declaration/claim table. The preamble records the allocator high-water at image creation. Keys are strictly monotone across rotations, and ordinary rotation excludes open tokens and pinned cutoffs. A new PREPARED needs a fresh key. See `src/journal/journal.h:180`, `docs/decisions/wave0-plan-amendments.md:415` and `docs/decisions/wave1-journal-wire-amendment.md:102`.

**Input.** Set the preamble generation high-water to 7. Append BASE(key 0, content id 17), SAVE(PREPARED, token 1, previous key 0, new key **7**, s=1, id 17, expected ABSENT, witness volume 1/inode 2/high 0/size 0/CRC 0, path `/a`), GENERATION(7, matching identity, prefix length/CRC 0, path `/a`), and SAVE(FINISHED, token 1, key 7, s=1, id 17, path `/a`).

**Result.** PREPARED is eligible, FINISHED activates key 7 and offers saved evidence, and a following BASE(7,17) gets that activation. The four records should remain in the physical prefix, but the reused new-key claim is unresolved and this FINISHED cannot activate the declaration or provide saved evidence. This example uses the high-water of the *preamble*, independent of the append order of concurrently reserved keys.

### MUST-FIX 3. A SAVE can contradict the text at its explicitly identified BASE boundary

`src/journal/reader.c:332` checks that s names a BASE or command of the saver. For a BASE, the image also specifies the content id and, for key zero, the exact source length. `src/journal/reader.c:375` compares the two SAVE records with one another. Neither check reconciles the snapshot claim with those known BASE fields. The signed meaning of S is the saver's text at s. See `docs/decisions/wave1-journal-wire-amendment.md:102` and `src/journal/journal.h:244`.

**Input.** BASE(0,17) at sequence 1 fixes the text at zero bytes. Append PREPARED(token 1, previous 0, new 1, s=1, id 23, ABSENT, path `/a`, witness volume 1/inode 2/high 0/size **1**/CRC32C of `x`), GENERATION(1, same inode, size 1, prefix length 1, CRC32C of `x`, path `/a`), and FINISHED(token 1, key 1, s=1, id 23, path `/a`). There are no text commands. The installed file can satisfy both recorded witnesses exactly.

**Result.** FINISHED is eligible and is the newest installed origin at cutoff 1 with saved evidence for id 23. Selecting its verified branch yields one byte, `x`. The recorded text at precisely that cutoff is empty with id 17. Binding verification cannot resolve a contradiction between two journal records. The snapshot claim should be unresolved, with the physical prefix and the BASE retained. This case requires neither replay nor opening the old source.

### MUST-FIX 4. CANCELLED consumes a token even when its checkpoint has a provably unusable suffix

`src/journal/reader.c:426` checks the referenced marker, owner and checkpoint operation's own eligibility. It then closes the token at line 435. Pass two suppresses both PREPARED branches at line 663. The signed test is whether the checkpoint **with its complete suffix** secures the preserved state. See `docs/decisions/wave1-journal-wire-amendment.md:141`.

**Input.** Append these frames in sequence order.

1. BASE(0,17).
2. CHECKPOINT_BEGIN.
3. CHECKPOINT_BY_VALUE(covered 1, current id 17, saved id 0, text length 0, flags 0).
4. CHECKPOINT_COMMIT.
5. RESTORE_BASE_RANGE(destination 0, key 7, source 0, length 1). Key 7 has no declaration anywhere in the image.
6. PREPARED(token 1, previous 0, new 8, s=5, id 18, ABSENT, path `/a`, witness volume 1/inode 2/high 0/size 1/CRC32C of `x`).
7. CANCELLED(token 1, checkpoint commit 4, saved id 0, outcome CONFLICT, flags 0).

**Result.** Operation 5 already carries UNRESOLVED. Nevertheless operation 7 is ELIGIBLE, links itself as the terminal of 6, and promotes checkpoint 4 at cutoff 1. Its required suffix includes operation 5. Even with a matching installed file at `/a`, the plan has discarded the PREPARED installed candidate at cutoff 5 that could supply the byte independently. The cancellation should remain unresolved, preserving that conditional branch and all seven physical records. The positive cancellation fixture has an empty suffix on the saver. See `tests/journal_codec_test.c:1182`.

### NOTE 1. TRUNCATED alone supplies insufficient evidence for repair

`src/journal/codec.c:351` classifies any tail shorter than 32 bytes as TRUNCATED. For example, a lone zero byte and the first byte of SJR1 receive the same verdict and prefix. The unit test at `tests/journal_codec_test.c:720` also expects TRUNCATED for short garbage. A consumer deciding on in-place repair still needs the signed demonstrably-torn-tail check. N2 itself never repairs.

### NOTE 2. Competing new-key claims have a first-claim-wins interpretation

`src/journal/reader.c:342` diagnoses a second PREPARED for an occupied key and leaves the first claim intact. A later declaration and FINISHED matching the first transaction can still activate it. The signed phrase “unique new-key claim” deserves an explicit collision fixture specifying whether uniqueness counts accepted claims or all competing evidence. The current SAVE fixtures exercise missing declarations and orphan terminals. See `tests/journal_codec_test.c:835` and `tests/journal_codec_test.c:1189`.

### NOTE 3. One PREPARED can be reread for arbitrarily many mismatched finals

`src/journal/reader.c:408` decodes the same PREPARED for each same-token FINISHED until one matches. Repeating an otherwise well-framed FINISHED with a different installed key leaves the token open each time. Total frame visits remain O(R), and paths have a fixed wire limit, but the per-frame bounded-revisit statement in `docs/decisions/v2-zh6.md:39` is stronger than the implementation.

## Confirmed

- **Physical prefix and slots.** The first illegal transition stops at its own offset. Owner and kind select independent slots, replacement BEGIN resets only that slot, standalone text retires its owner's command, and clean EOF leaves pending members unapplied. See `src/journal/reader.c:211` and `src/journal/reader.c:572`.
- **Complete operations.** A command COMMIT publishes its ordered member list once. Checkpoint slices have their own contiguous, length-checked list, and complete suffix commands survive interleaving. The flat oracle independently reconstructs the projections on its two-owner scripts. See `src/journal/reader.c:245`, `tests/support/journal_oracle.h:20` and `fuzz/journal_fuzz.c:225`.
- **Staging and saved evidence.** For a fresh, consistent transaction, a staged key cannot serve BASE or a range. A matching complete FINISHED records the activation, declaration and installed-witness saved predicate together. An orphaned staged declaration stays unusable. The saved field is an evidence offset and requires binding verification. See `src/journal/reader.c:259`, `src/journal/reader.c:416`, and `src/journal/journal.h:852`.
- **Operand policy and high-waters.** Ranges are checked at member and final, VALUE_ONLY is monotone, and physical mentions in abandoned range members and TABS advance the returned maxima. See `src/journal/reader.c:265`, `src/journal/reader.c:462` and `src/journal/reader.c:542`.
- **PAD.** Stream offsets 0, 4032, 4033, 4063 and 4064 require 4064, 32, 4127, 4097 and 0 bytes respectively. Reducing before adding the preamble prevents residue overflow. Both codec directions check the full absolute end by subtraction. See `src/journal/codec.c:188`, `src/journal/codec.c:326`, and `src/journal/codec.c:356`.
- **Workspace.** At half load, retained arrays use fewer than eight 24-byte buckets per indexed entry, plus at most seven alignment bytes per entry for table allocations. The resulting table charge is 199. R adds 167 for an indexed operation, 47 for a frame ref and twice 47 for origins. B, D and T add 215, 55 and 23 respectively. The four tables therefore fit **1 + 507R + 414B + 254D + 222T** on this ABI, including abandoned allocations. TABS-only ids need no entry. Size overflow returns zero, and arena exhaustion clears the plan and restores the mark. See `src/journal/reader.c:111` and `src/journal/reader.c:677`.

## Scope

File observation and saved-id adoption, producer identity checks, barrier durability, actual tail repair, and piece replay belong to the downstream runtime. All results above are derived from the source and test text. No builds, tests or journal programs were run.

## Coordinator note

The Codex run ended on a cyber-filter stop after this file was written; its last message matched MUST-FIX 1 and 2. The Workspace line above is cut mid-reference in the original. Triage: MUST-FIX 1–4 filed as beads; NOTEs 1–3 in one backlog bead.
