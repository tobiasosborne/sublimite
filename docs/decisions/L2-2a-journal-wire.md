# L2-2a · journal wire format and pure codec (first half of L2-2)

Brief: `docs/briefs/L2-2a-journal-wire.md`. Settled inputs read in full:
A14, A22, A23 in [wave0-plan-amendments.md](wave0-plan-amendments.md);
harvest §2, derivations Q1/Q5/Q14, q2-protocol "Records for L2-2" (all in
this directory); v1 evidence read-only at the oracle pin. The wire
section went to the advisor first and came back approved with six
amendments (A–F below); the codec then landed red-first.

Landed: `src/journal/journal.h` (wire section + codec API),
`src/journal/codec.c`, `tests/journal_codec_test.c`,
`fuzz/journal_fuzz.c` with seeds under `fuzz/corpus/journal/`,
`base_cpu_has_sse42` in `src/base/` with its `tests/base_test.c` case,
lane rows `tests/journal_codec_test.c := san tsan` and
`fuzz/journal_fuzz.c := fuzz` in `tests/lanes.mk`, and
`LDFLAGS_journal_codec_test` in the Makefile. L2-2b owns everything
with a file, a thread or a piece tree; nothing of it is here.

## The module in one paragraph

**Invariant:** bytes the codec certifies mean exactly what journal.h
§§1–8 say, and bytes it refuses are never partially interpreted. Encode
and decode are field-wise little-endian into caller storage; the codec
allocates nothing, owns no state (the CRC table is built per operation
in the caller's frame), and validates a whole image into a certified
valid prefix with the first failure classified (truncated, invalid,
out-of-sequence, foreign, unsupported). Cross-record semantics —
declaration tables, unresolved keys, token matching, text bounds — are
recovery's, on a prefix this codec has certified.

## Mechanisms: invariant, simplest alternative, defeating counter-example

1. **Versioned `SJNL` preamble (32 B), validated before any record or
   repair.** Invariant: an unsupported or future image is refused
   whole and intact, never truncated by a reader that cannot decode
   it. Alternative: v1's header-free stream with per-record schema
   discrimination. Counter-example (Q5): an older scanner walks a
   valid prefix, meets the first GENERATION record it does not know,
   and its truncating repair destroys a good journal there. The
   amendment-B preamble carries two allocator high-waters (generation,
   buffer id); Q5's 24 B layout widened by the advisor read, inside
   the pre-freeze window the head note defines.
2. **One rule for unknown record types: refuse.** Invariant: unknown
   bytes are preserved and the valid prefix stops before them.
   Alternative: skip by the length field when the preamble marks a
   type optional. Counter-example: the unknown record is a text edit;
   skipping it yields wrong text with a clean bill — the one outcome
   worse than refusing. Future types arrive by version bump or
   required-feature bit, and version-1 readers refuse the whole file.
3. **Never-reused u64 generation keys and buffer ids, monotonic per
   lineage, refuse-before-wrap (amendment B).** Invariant: an id names
   one life; there is no incarnation axis to disagree with it.
   Alternative: v1's reused tab-slot buffer ids plus an incarnation
   counter per record (40 B of incarnation across five record types
   (P: 5 × 8) in the first draft). Counter-example the advisor asked
   for did not survive contact: every reader that must tell two lives
   apart can be given two ids, and the allocator resuming past every
   id the image mentions makes reuse unconstructable rather than
   merely checked.
4. **Identity as an explicit 48 B `os_stat` projection plus a one-block
   prefix witness (Q1/Q14).** Invariant: a replacement file is never
   approved by a weaker comparator than the one that recorded it.
   Alternative: persist `os_stat` as memory, or drop the high inode
   word / ctime. Counter-examples: struct-as-memory changes meaning
   with the ABI; a comparator omitting the high inode word approves a
   replacement the full comparator rejects (Q14); size+mtime alone
   miss a rewrite that restores both, which ctime (unsettable) catches;
   the remaining same-times-different-bytes hole is what the 4,096 B
   prefix CRC closes, for exactly the one block capture already reads.
5. **SAVE(PREPARED)'s witness is deliberately not that identity.** It
   carries volume + inode + size + CRC32C over all of S, no times.
   Invariant: recovery can recognize our own renamed temp. Alternative:
   reuse the 48 B identity for the temp. Counter-example
   (q2-protocol): the rename itself changes the temp's ctime, so the
   pre-rename identity rejects our own save on every crash in the
   window the record exists for. CRC32C is recorded in the header as
   what it is — the accidental-change witness the whole format
   trusts, not collision resistance against a crafted file.
6. **Checkpoint by value = one descriptor + literal INSERT slices in
   one group; no conflict/detached flags on it (amendment C).**
   Invariant: one fact, one home — target binding lives in TARGET,
   restated atomically as the checkpoint group's optional last
   member. Alternative: conflict/detached bits on the descriptor (the
   first draft). Counter-example: descriptor says DETACHED, the
   surviving TARGET says BOUND; recovery must pick, and either pick
   is a guess the format invented. `HAS_SAVED` stays because nothing
   else carries the saved content id.
7. **PAD seals only a sync boundary, to the next 4,096 B block
   (amendment D).** Invariant: a later append never rewrites a block
   already holding durable records, so a torn writeback of new bytes
   cannot take synced bytes with it. Alternative: v1's seal every
   batch, full page when already aligned. Counter-example to the
   alternative: at an aligned boundary durability needs nothing, so
   the full-page PAD is 4,096 B spent on v1's batch shape, not on any
   v2 invariant; the canonical length is instead a checked function
   of stream offset (`journal_pad_length`, 32..4,127 B (P)).
8. **Length is a corruption bound before it is a size.** Invariant: no
   untrusted length field ever sizes a read, copy or allocation; the
   decoder borrows the caller's bytes. The 1 MiB maximum carries its
   two v2 ends in journal.h §3 (amendment E): above the largest
   legitimate record — TABS at 1,000 live (M, contract R4) + 16
   closed-kept entries = 12,232 B (P), pinned by test — and equal to
   the one-record buffer a file reader must hold, the arena size v1's
   replay demonstrably allocates (M). Alternative: size scratch from
   the length field. Counter-example: one flipped length byte demands
   a 4 GiB allocation before a single CRC is checked.
9. **Groups adopt whole or not at all; the valid prefix rolls back to
   GROUP_BEGIN.** Kept from v1's group contract. Alternative:
   streaming delivery as members validate. Counter-example (T2): a
   peer checkpoint torn one INSERT short would publish a prefix of
   the peer's text as its whole text. Stream validation holds the
   counters at BEGIN and restores them on any failure inside.
10. **CRC32C through `base_cpu_has_sse42()`, forced in tests by
    `--wrap`, never by environment.** Invariant: both paths agree bit
    for bit (standard check value 0xE3069283 (G) and a bit-wise
    reference pin them independently). Alternative: v1's
    `EDIT_JOURNAL_PORTABLE_CRC` env override. Counter-example: it is
    exactly the env-var feature switch CLAUDE.md bans, and it makes
    the shipped binary's checksum path a property of its launch
    environment.

## Amendment log against the first wire draft

A. Types renumbered densely 0–12 in §6 order; no numbers are "retired"
   for records v2 never had — a v1 importer maps numbers itself.
B. Incarnation deleted from BASE, TARGET, CHECKPOINT_BY_VALUE, SAVE and
   TABS (8 B each (P)); buffer ids are never reused, high-water in the
   preamble. No counter-example was raised: two lives take two ids.
C. `CONFLICT`/`DETACHED` flags deleted from CHECKPOINT_BY_VALUE; the
   optional trailing TARGET member carries binding. No
   counter-example: a foreign replacement is visible as TARGET's
   expected identity not matching the file.
D. PAD kept with the sync-boundary invariant above; an aligned writer
   emits none. (Justified by the invariant in D's own terms, not
   raised as a counter-example.)
E. Every "(G) v1's" constant re-derived or removed: the 16,384 B slice
   left the wire section (L2-2b encoder policy); the 4,096 B witness
   is one block read; 1 MiB argued from both ends (mechanism 8); the
   256 B VIEW bound stays as stated headroom (4 × v1's 64 B (P)).
F. The head note now defines "for ever" from the Wave 1 exit; until
   then changes are advisor-read amendments with no version bump.
   B's preamble widening is the first use of that route.

## What of v1's wire is kept, and what is cut

Kept: 32 B record header shape and field offsets (magic differs);
CRC32C and its zeroed-field record coverage; sequence +1 chaining with
PAD at sequence 0; group begin/commit atomicity and torn-group drop;
DELETE's 48 B shape; the 4,096 B prefix-witness and PAD lengths as
ceilings re-derived in v2's terms; the truncating-repair *shape* (as
L2-2b's act after validation, never the decoder's).
Cut: no file header (→ SJNL preamble); payload-discrimination schema
evolution (BASE prefix_len bit 31, VIEW/WINDOW dual lengths → one
layout per record, unknown = refuse); NUL-terminated paths
(→ length-prefixed raw bytes); WINDOW and PAINT (no Wave 1 consumer;
a GUI bead takes fresh numbers); per-instance CRC table and its env
override; retention artifacts and their location kind (A23 rule 1);
v1's type numbers.

## Status at the clean stop (2026-11-24 00:23 CET; coordinator quota stop)

**Compile state:** `HOME=$PWD/build/home nice make -j2 all` exits 0
(log `build/make-all-stop.log`, scratch, not shipped). The tree
compiles whole; this bead is resumable exactly here.

**Done:**
- Amendments A–F all applied to journal.h's wire section (see the
  amendment log above; no counter-example raised for B or C, D kept
  by its stated invariant).
- Codec implemented in full: CRC32C (hardware/portable via the new
  `base_cpu_has_sse42`, which has its own `tests/base_test.c` case),
  preamble, canonical PAD length, record framing, every typed
  payload encode/decode, whole-image `journal_stream_validate`.
- `tests/journal_codec_test.c`, red first (link failure on every
  `journal_*` symbol, make Error 1, 00:13 CET), then green:
  `build/san/tests/journal_codec_test` prints PASS standalone
  (ASan+UBSan). Lanes row `san tsan`; Makefile wraps
  `base_cpu_has_sse42` for the suite.
- `fuzz/journal_fuzz.c` plus 9 seeds in `fuzz/corpus/journal/`
  (preamble-only, preamble-unknown-version, generation-base-insert,
  restore-base-range, checkpoint-by-value-group, torn-tail-record,
  torn-tail-group, save-prepared, tabs-view), generated
  independently in Python from the wire text (scratch generator in
  `build/`), so seed validity cross-checks the C codec. The fuzzer
  builds and replays all seeds (`-runs=0`, exit 0). Lanes row
  `fuzz` added.
- This decision doc.

**Missing (the bead resumes here):**
- Acceptance runs started but cancelled at the stop order, none
  completed: `nice make -j2 check` with
  `CONTRACT_CHECK_DISPLAYS=:170-:179` was still running when
  terminated (its result is unknown, not a failure on record);
  `nice make -j2 check-tsan` and `nice make -j2 fuzz-smoke` were
  not started. `make all` exit 0 is compile evidence only.
- `base_test` with the new sse42 case compiled into `make all`'s
  rel test binaries but has not been run yet (it runs under `check`).
- No worklog entry or `HANDOFF.md` update: session state is the
  coordinator's, and the brief scopes workers to the bead plus
  this decision doc.
