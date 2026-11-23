Bead L2-2a — L2 journal wire format and pure codec (the first half of dossier bead L2-2; L2-2b is the typing-path append, worker transport, recovery and replay; L2-3 the save transaction). sublimité v2 is a clean-slate C11 editor rebuild; your checkout is a worktree of /home/tobias/Projects/sublimite-v2 (branch v2). One worker, one bead; do exactly this scope, then stop.

THE FORMAT IS FOR EVER. Write src/journal/journal.h's wire section FIRST, send it to the coordinator (path + full text) and STOP until the advisor's word; then implement and test the codec. Pure codec means: encode, decode, validate — no file, no thread, no piece, no allocation beyond caller storage.

Read first (absolute paths under /home/tobias/Projects/sublimite-v2):
1. CLAUDE.md (L2 includes only L0/L1; one public header; journal_ prefix; A15: caller bugs ASSERT, input-producible conditions are return codes; no env-var feature switches; tests link the archive, never #include a .c; every number with an evidence tag and derivation).
2. docs/decisions/wave0-plan-amendments.md A14, A22, A23 (the save protocol and the record set).
3. docs/decisions/wave1-L2-L5-harvest.md §2 (v1's wire facts in §2.4: no header, JNL1 magic, 32 B record header, CRC32C coverage, types, PAD to 4,096 B, the truncating repair, the 1 MiB record limit, BASE payload layout).
4. docs/decisions/wave1-L2-derivations.md Q1 (generation key, declaration, re-declaration), Q5 (24 B SJNL preamble: ASCII "SJNL", LE u32 version, LE u32 required features, LE u64 generation high-water, LE u32 CRC32C), Q14 (ABSENT | PRESENT(os_stat)); docs/decisions/wave1-L2-q2-protocol.md "Records for L2-2" (fields, emission points, recovery use per record).
5. src/os/os.h (os_stat fields), src/base/base.h (CPU predicates for CRC32C selection; add sse42 there if needed, with its own test).
6. v1 evidence, read-only: /home/tobias/Projects/sublimite-v1-oracle/src/journal/journal.c (:12, :101-122, :127-159, :269-316, :712-723) and journal.h (:11-44); fuzz/journal_fuzz.c.

Record set (decided, A23): SJNL preamble + PAD; GENERATION; BASE; TARGET; INSERT; DELETE; RESTORE_BASE_RANGE; GROUP_BEGIN / GROUP_COMMIT; CHECKPOINT_BY_VALUE (followed by literal INSERT slices inside one group); SAVE(PREPARED / FINISHED / CANCELLED); VIEW; TABS. No WINDOW, no PAINT.

What the wire section must show (advisor's list — one read, not three):
- Byte order stated and fixed; every field encoded explicitly field by field (no struct written as memory; no padding other than a declared PAD record).
- The record header: type, length, sequence, buffer id, and exactly what the checksum covers; the largest legal record length with its derivation, so a corrupt length can never size an allocation.
- The rule for an unknown record type inside a known version: ONE rule, stated (refuse; or skip only when the preamble's feature bits mark it optional).
- How identity is encoded (device, inode, size, and which times) and why those suffice (Q14, the v1 BASE prefix CRC, ctime across rename).
- The generation key's width and why it cannot repeat (Q1).
- What a reader does at a torn tail, one sentence per record class.
- How the preamble guarantees a v2 journal is never offered to a v1 parser (A14): magic distinct from v1's "JNL1" record magic, and what a v1 reader does on meeting it.
- VIEW and TABS: unless you can name every field of VIEW from a consumer that exists in Wave 1, VIEW is a bounded opaque payload with its own small version, carried and checksummed but not interpreted by the journal (as undo carries its 16-byte cursor state); the layer that owns views defines the contents later. TABS carries what Wave 1's app_session actually restores (dossier §5) and nothing guessed. A format that freezes someone else's fields is the thing to avoid.
- Sizes of every fixed record with (P) arithmetic; the CHECKPOINT_BY_VALUE slice size and the RESTORE_BASE_RANGE 64 B layout (piece-pool spec) checked.

After the advisor's word, implement:
- src/journal/journal.h (wire section + codec API) and src/journal/codec.c (or journal.c holding only the codec for now): encode into caller storage, decode with full validation, CRC32C with hardware/portable selection through base_cpu_has_* (both paths tested through a --wrap of the CPU predicate; no env switch).
- tests/journal_codec_test.c: round trip of every record kind incl. boundary sizes; rejection of every malformed input class (bad magic/version/required feature, bad CRC, length beyond the maximum or crossing the buffer end, unknown type per the stated rule, out-of-sequence, torn tail per class, arithmetic overflow in RESTORE_BASE_RANGE fields); a v1 JNL1 stream refused as foreign without any repair. Red first.
- fuzz/journal_fuzz.c on decode/validate with seeds in fuzz/corpus/journal/ (preamble/version, GENERATION, RESTORE_BASE_RANGE, CHECKPOINT_BY_VALUE groups, torn tails).
- tests/lanes.mk rows; docs/decisions/L2-2a-journal-wire.md (each mechanism: invariant, simplest alternative, defeating counter-example; what of v1's wire is kept and cut).

Acceptance: `nice make -j2 check` (CONTRACT_CHECK_DISPLAYS=:170-:179), `nice make -j2 check-tsan`, `nice make -j2 fuzz-smoke` exit 0; the codec allocates nothing (caller storage).

Rules: no git (not even git add -N), no bd. No rm -rf, no mktemp with a clean-up trap, nothing outside your checkout; HOME for cache-writing tools under build/; build under build/; at most nice make -j2; no starts Tue 24 Nov 03:45–05:00 CET; run date before each; report rather than run anything needing approval. Report: the wire section first (and stop); later red/green, per-file line counts, anything outside scope (report only).
