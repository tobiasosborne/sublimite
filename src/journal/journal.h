/* L2 journal: hot-exit and save-transaction record log.
 *
 * WIRE SECTION (L2-2a; the codec API follows it in this header). What
 * "for ever" means: the wire is frozen when Wave 1 exits, the first
 * moment a build can leave a journal on a user's disk; from then a
 * field, type number or rule stated here never changes meaning, and
 * change comes only as a new wire version or a required-feature bit a
 * reader of this version refuses. Until that exit a change is an
 * amendment the advisor reads, with no version bump, because no file of
 * version 1 exists outside test fixtures; L2-3, which writes the SAVE
 * records, may find a field missing -- that amendment is the route.
 * Sources: docs/decisions/wave1-L2-derivations.md Q1, Q5 (its preamble
 * widened with a buffer-id high-water by the L2-2a advisor read), Q14;
 * docs/decisions/wave1-L2-q2-protocol.md "Records for L2-2";
 * docs/decisions/wave0-plan-amendments.md A14, A22, A23;
 * docs/decisions/wave1-piece-pool.md (iii) for the 64 B range record.
 * Quantities carry their tags: (P) layout arithmetic, (G) chosen gate.
 * This section is pure description: no file, thread or piece type appears.
 */
#ifndef JOURNAL_JOURNAL_H
#define JOURNAL_JOURNAL_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/* ---- 1. Conventions ------------------------------------------------------
 * Byte order is little-endian for every integer, fixed here, never the
 * host's. Every field is encoded and decoded explicitly, field by field;
 * no C struct is ever written or read as memory, and no padding exists
 * anywhere except a declared PAD record (section 4). Offsets in section 6
 * are payload offsets (byte 32 of the record). A journal file is one
 * 32 B preamble followed by a record stream. Positions, sequence numbers
 * and PAD alignment are image-local: stream offset 0 is the first record
 * byte (file offset 32 (P, the preamble size)), the first non-PAD record
 * has sequence 1, and a rotation writes a complete new image (new
 * preamble, sequences restart at 1) rather than editing this one. File
 * size and repair arithmetic charge the preamble separately (Q5).
 */

/* ---- 2. Preamble (Q5 as amended, see the head note): 32 B, at file ----
 * offset 0, before any record:
 *   0  4 B  ASCII "SJNL" (bytes 53 4A 4E 4C)
 *   4  u32  wire version; this document is version 1 (G, first SJNL schema)
 *   8  u32  required features; version 1 defines no bit, so 0 (G)
 *  12  u64  generation high-water at creation of this image (section 5)
 *  20  u64  buffer-id high-water at creation of this image (section 5)
 *  28  u32  CRC32C of bytes [0, 28)
 * Total 32 B (P: 4 + 4 + 4 + 8 + 8 + 4). A high-water of 0 means nothing
 * of that kind has been allocated in this lineage yet. A header-only
 * journal is logically empty. The preamble is validated before any
 * writable open, append, replay, repair, discovery or retirement
 * (Q5, section 8).
 */
#define JOURNAL_PREAMBLE_BYTES 32u
#define JOURNAL_WIRE_VERSION 1u
#define JOURNAL_REQUIRED_FEATURES 0u

/* ---- 3. Record header: 32 B before every payload -------------------------
 *   0  4 B  ASCII "SJR1" (bytes 53 4A 52 31), distinct from v1's "JNL1"
 *   4  u32  total length: header + payload, 32..JOURNAL_MAX_RECORD_BYTES
 *   8  u32  record type (section 4)
 *  12  u32  CRC32C, coverage below
 *  16  u64  sequence
 *  24  u64  buffer id
 * Total 32 B (P: 4 + 4 + 4 + 4 + 8 + 8), v1's header size and field
 * offsets, with a v2-only magic. CRC32C is CRC-32/ISCSI (Castagnoli,
 * reflected polynomial 0x82F63B78, initial value and final XOR both
 * 0xFFFFFFFF; check value for ASCII "123456789" is 0xE3069283 (G, the
 * standard's)). A record's CRC covers the whole record -- header bytes
 * [0, 12), four zero bytes in place of the CRC field, then bytes
 * [16, total length) -- exactly v1's crc_record coverage. The preamble
 * CRC instead covers only the 28 B before it (section 2).
 *
 * Sequence: non-PAD records carry previous non-PAD sequence + 1, starting
 * at 1; PAD carries sequence 0 and does not advance it. A sequence of
 * UINT64_MAX can never be followed, so a writer refuses before wrap and a
 * reader treats a successor claim as out-of-sequence. Buffer id 0 is
 * reserved for "no buffer": exactly PAD, GENERATION, GROUP_BEGIN,
 * GROUP_COMMIT and TABS carry 0; every other type carries a nonzero id.
 *
 * Largest legal record: 1 MiB total, a corruption bound checked from
 * the length field before the length may size any read, copy or
 * allocation; the pure decoder sizes nothing by it at all (borrowed
 * caller storage, L2-2a). Both ends of the choice, in v2's own terms:
 * below, it must exceed the largest record a v2 encoder can
 * legitimately emit, which is TABS at the product's capacities --
 * 1,000 live tabs (M, measured live capacity, contract case R4) plus
 * 16 closed-but-kept (v1's retained default, editor.h at the pin)
 * entries of 12 B plus 40 B of framing = 12,232 B (P) -- and any single
 * INSERT, whose slicing is L2-2b encoder policy, not wire law, so the
 * wire maximum must not silently become that policy's ceiling. Above,
 * a file reader must hold one whole record to validate it, so the
 * maximum is exactly one caller buffer of 1,048,576 B per reader (P:
 * the bound itself), the same single-record arena size v1's replay
 * demonstrably allocates (M, replay_file_mode at the pin). Between
 * 12,232 B of need and 1 MiB of stated reader cost, 1 MiB (G) leaves
 * the slicing decision to its owner.
 */
#define JOURNAL_HEADER_BYTES 32u
#define JOURNAL_MAX_RECORD_BYTES (1024u * 1024u)
/* PAD alignment (section 4) and the BASE prefix witness length: one
 * block. Capture and check each read exactly this first block of a
 * source once, so the witness covers min(size, 4096) B (P: one block
 * read, no second), and a synced journal boundary ends on a multiple
 * of it. Wire-fixed, independent of the host page size. */
#define JOURNAL_PAGE_BYTES 4096u
#define JOURNAL_PREFIX_BYTES 4096u
/* Longest path byte string on the wire: paths are length-prefixed raw
 * bytes with no NUL, so a path that fits OS_PATH_MAX including its NUL
 * (os.h, 4096 B) carries at most 4095 B (P: 4096 - 1). */
#define JOURNAL_PATH_MAX_BYTES 4095u

/* ---- 4. Record types ------------------------------------------------------
 * Numbered densely from 0 in section 6's order, PAD first as framing.
 * This format shares no parser with v1, so v1's numbers buy nothing
 * here; a v1 importer maps numbers in its own table. Numbers are never
 * reused (from the Wave 1 freeze; until then, see the head note).
 * Every value above TABS is reserved.
 *
 * PAD (0) is framing, not content: sequence 0, buffer 0, payload all
 * zero, CRC as any record. The invariant it maintains: a synced
 * boundary ends on a JOURNAL_PAGE_BYTES boundary, so a later append
 * never rewrites a block that already holds durable records (a torn
 * writeback of a shared block could otherwise destroy synced bytes
 * while adding unsynced ones). A writer therefore emits exactly one
 * PAD immediately before a sync barrier, and only when the stream
 * offset is not already a multiple of 4096; an aligned boundary needs
 * no PAD. Its length is canonical: the smallest length of at least
 * the 32 B header that lands the offset on the next multiple of 4096
 * -- where the plain remainder is under 32 B, the PAD runs to the
 * multiple after that -- so a PAD is 32..4,127 B (P: 4096 + 32 - 1)
 * and its length is a function of the offset it sits at, which the
 * reader checks. A PAD may appear inside a group; it has no effect.
 * journal_pad_length() below is that function (0 = no PAD needed).
 */
typedef enum journal_record_type {
    JOURNAL_REC_PAD = 0,
    JOURNAL_REC_GENERATION = 1,
    JOURNAL_REC_BASE = 2,
    JOURNAL_REC_TARGET = 3,
    JOURNAL_REC_INSERT = 4,
    JOURNAL_REC_DELETE = 5,
    JOURNAL_REC_RESTORE_BASE_RANGE = 6,
    JOURNAL_REC_GROUP_BEGIN = 7,
    JOURNAL_REC_GROUP_COMMIT = 8,
    JOURNAL_REC_CHECKPOINT_BY_VALUE = 9,
    JOURNAL_REC_SAVE = 10,
    JOURNAL_REC_VIEW = 11,
    JOURNAL_REC_TABS = 12
} journal_record_type;

/* ---- 5. Identities and generation keys ------------------------------------
 * The wire identity is the explicit projection of os_stat fixed by Q1 and
 * Q14, 48 B wherever it appears (P: 6 x 8), fields in this order:
 *   volume u64, inode low u64, inode high u64, size u64,
 *   mtime_ns i64, ctime_ns i64      (os_identity.index[0], index[1])
 * Why these suffice: volume plus both inode words name one inode (the
 * high word is 0 on Linux today (os.h) but is carried so no comparator
 * can approve a replacement by omitting it, Q14); size and mtime_ns
 * detect ordinary rewriting; ctime_ns detects rewriting that restores
 * size and mtime, because ctime cannot be set back by the file's owner.
 * The remaining hole -- same inode, same size and times, different
 * bytes -- is closed to v1's standard by the prefix witness carried with
 * every GENERATION: CRC32C over the first min(size, 4096) B, exactly
 * the one block capture already reads (section 3). A rename changes the renamed inode's ctime, so strict
 * identity comparison after a rename can only refuse a good file, never
 * accept a wrong one; that safe direction is the rule for GENERATION
 * sources. The one place strict comparison would refuse our own save --
 * the temp renamed over its target -- never uses this identity across
 * the rename: SAVE(PREPARED) carries its own rename-proof witness
 * (section 6). Kind, permission bits and ownership from the full os_stat
 * are deliberately not on the wire (Q14): nothing in recovery may
 * synthesize a complete stat from this projection.
 *
 * A generation key is a u64 naming one source generation within one
 * journal lineage, across buffers and rotations (Q1). 0 means absent /
 * empty origin. Keys are allocated strictly monotonically; the
 * allocation high-water is carried in every rotation image's preamble,
 * and on reopen the allocator first advances past every key mentioned by
 * the image (GENERATION keys and SAVE new-generation keys), then refuses
 * (return code) rather than wrap at UINT64_MAX. A key therefore cannot
 * repeat: reuse would need the monotonic counter to move backwards or
 * wrap, and both are refused before allocation, in every build.
 *
 * Buffer ids follow the same rule, and that is why no record carries a
 * separate incarnation: a buffer id is itself never reused. It is
 * allocated strictly monotonically per lineage (0 reserved, section
 * 3), its high-water rides in the preamble beside the generation
 * high-water, and on reopen the allocator first advances past every
 * buffer id the image mentions (record headers and TABS entries),
 * refusing rather than wrap. Two lives that a reader must tell apart
 * are two buffer ids; a tab slot reused by the UI is a fresh id in
 * the journal. There is no "id matches, incarnation does not" state.
 */
typedef struct journal_wire_identity {
    uint64_t volume;
    uint64_t index_low;
    uint64_t index_high;
    uint64_t size;
    int64_t mtime_ns;
    int64_t ctime_ns;
} journal_wire_identity;
#define JOURNAL_IDENTITY_BYTES 48u
#define JOURNAL_GENERATION_ABSENT UINT64_C(0)

/* ---- 6. Payloads -----------------------------------------------------------
 * Paths are canonical absolute paths as raw bytes: first byte '/', no
 * NUL byte, length from the length field only (never NUL-terminated,
 * unlike v1 BASE). Whether a syntactically absolute path is still
 * canonical is the writer's obligation; a pure decoder cannot ask the
 * filesystem. Fields named "must be zero" are validity conditions.
 *
 * GENERATION (1), buffer 0. Declares a generation before first use.
 * There is exactly one location kind -- the canonical absolute path of a
 * named BASE -- because A23 rule 1 deletes private retention copies and
 * their location kind; no kind field is carried for a second kind that
 * does not exist.
 *    0  u64  key (nonzero)
 *    8  48 B identity (section 5), captured from the held source fd
 *   56  u32  prefix_len: exactly min(size, 4096) of the identity's size
 *   60  u32  prefix_crc32c over the first prefix_len bytes (0 if empty)
 *   64  u32  flags: JOURNAL_GENERATION_VALUE_ONLY, other bits must be 0
 *   68  u32  location_len, 1..4095
 *   72  location bytes
 * Total 104 + location_len B, at most 4,199 B (P: 32 + 72 + 4095).
 * VALUE_ONLY records A23 rule 3 durably: from a save capture of this
 * generation onward, restores from it are journaled by value, never as
 * RESTORE_BASE_RANGE. Redeclaration: a key may be declared again, in the
 * same or a later image, only with identical key, identity, prefix_len,
 * prefix_crc32c and location; flags may gain VALUE_ONLY and never lose
 * it. A conflicting declaration is rejected by recovery when it builds
 * the declaration table (cross-record state; the pure codec validates
 * each declaration on its own). Each checkpoint image redeclares every
 * reachable generation before the records that use it (Q1). A reference
 * (BASE, RESTORE_BASE_RANGE) to a key with no preceding declaration in
 * the image is unresolved, likewise recovery's verdict.
 */
#define JOURNAL_GENERATION_VALUE_ONLY UINT32_C(1)
/* BASE (2). Installs a buffer's origin on open and in checkpoint images:
 *    0  u64  generation key; 0 = empty origin (untitled, length 0)
 *    8  u64  content id of the text at installation (Q10's domain)
 * Total 48 B (P: 32 + 16). Initial text length is the declared
 * generation's identity size (0 for the empty origin); it is not
 * repeated here, so it cannot disagree with the declaration.
 *
 * TARGET (3). The buffer's intended save destination, distinct from the
 * source of its bytes:
 *    0  u32  binding: JOURNAL_TARGET_* below
 *    4  u32  presence: JOURNAL_PRESENT or JOURNAL_ABSENT (Q14)
 *    8  48 B expected identity; all zero unless presence is PRESENT
 *   56  u32  path_len; 0 iff binding is UNTITLED, else 1..4095
 *   60  path bytes (canonical absolute destination)
 * Total 92 + path_len B, at most 4,187 B (P: 32 + 60 + 4095). UNTITLED
 * requires presence ABSENT. DETACHED names a former target a recovered
 * draft still carries without owning the live file identity (A23 rule
 * 6); its path and expected identity are the last bound ones.
 */
typedef enum journal_target_binding {
    JOURNAL_TARGET_UNTITLED = 0,
    JOURNAL_TARGET_BOUND = 1,
    JOURNAL_TARGET_DETACHED = 2
} journal_target_binding;
typedef enum journal_presence {
    JOURNAL_ABSENT = 0,
    JOURNAL_PRESENT = 1
} journal_presence;
/* INSERT (4): 0 u64 destination in current text, 8 literal bytes, at
 * least one (a zero-byte INSERT is not a canonical record). Total
 * 40 + byte count (P: 32 header + 8 destination). How large a slice an
 * encoder emits is L2-2b policy, not wire law (section 3).
 * DELETE (5): 0 u64 destination, 8 u64 length (>= 1, destination +
 * length must not overflow u64). Total 48 B (P: 32 + 16), v1's shape.
 * RESTORE_BASE_RANGE (6): 0 u64 destination, 8 u64 generation key
 * (nonzero; declared and not VALUE_ONLY is recovery's check), 16 u64
 * source offset, 24 u64 length (>= 1; neither source offset + length
 * nor destination + length may overflow u64). Total 64 B
 * (P: 32 + 4 x 8), the signed piece-pool layout. In-range checks
 * against the source and text lengths are recovery's, after the key
 * resolves to a verified source (Q3).
 * GROUP_BEGIN (7) / GROUP_COMMIT (8): empty payload, total 32 B each
 * (P: header alone), so a group frame costs 64 B (P: 2 x 32) plus its
 * members. Groups never nest and are non-empty (section 7).
 */
#define JOURNAL_DELETE_BYTES 48u
#define JOURNAL_RESTORE_BASE_RANGE_BYTES 64u
#define JOURNAL_GROUP_MARKER_BYTES 32u
/* CHECKPOINT_BY_VALUE (9) is a descriptor, legal only as the first
 * member of a group, followed inside the same group by INSERT slices
 * that rebuild the whole text from an empty origin (section 7):
 *    0  u64  covered sequence: this image's last sequence whose effect
 *            for this buffer the checkpoint replaces (strictly before
 *            the group's GROUP_BEGIN sequence)
 *    8  u64  content id at the covered boundary
 *   16  u64  saved content id (meaningful only with HAS_SAVED)
 *   24  u64  text length: exact sum of the following slices' literals
 *   32  u32  flags below; other bits must be 0
 * Total 68 B (P: 32 + 36). The group costs text length + 40 B per slice
 * record + 132 B (P: 32 BEGIN + 68 descriptor + 32 COMMIT, each slice
 * adding its 32 B header and 8 B destination), plus a TARGET member
 * when present. Recovery selects a committed checkpoint as the
 * buffer's text at its covered boundary and then applies, in sequence
 * order, that buffer's text records with sequence greater than covered
 * sequence wherever they sit relative to the group (they may precede
 * it physically, q2-protocol T2). Dirty is derived, never stored as
 * its own bit: no HAS_SAVED, or saved content id unequal to the
 * current one (Q10). Target binding and conflict state are TARGET's
 * facts alone, never restated here: a checkpoint that must move or
 * reaffirm the target carries a TARGET record as the last member of
 * its group (section 7), and a foreign replacement shows as the
 * expected identity in that TARGET not matching the file -- two
 * places for one fact could disagree.
 */
#define JOURNAL_CHECKPOINT_HAS_SAVED UINT32_C(1)
/* SAVE (10) is one record type with a phase discriminator; each phase
 * has exactly one payload layout and length rule, and an unknown phase
 * is invalid. Common head: 0 u32 phase, 4 u64 token (nonzero; one
 * token names one transaction of one saver buffer and is matched
 * across phases by (buffer id, token)).
 *
 * PREPARED, appended and synced after the temp is written and synced,
 * before the rename (A23 rule 2). Offsets after the common head:
 *   12  u64  previous generation key (0 = empty origin)
 *   20  u64  new generation key (nonzero, freshly allocated, not yet
 *            declared: it becomes resolvable only when a finish image
 *            declares it from the installed fd, so no RESTORE in this
 *            image may reference it)
 *   28  u64  snapshot sequence s (image-local; S is the saver's text
 *            through its last complete command boundary at capture)
 *   36  u64  saved content id (the content id at s)
 *   44  u32  expected presence at the destination before the rename
 *   48  48 B expected identity (section 5; zero if ABSENT)
 *   96  36 B new-file witness, from the written temp's fd:
 *            volume u64, inode low u64, inode high u64, size u64
 *            (the byte length of S), content_crc32c u32 over all of S
 *  132  u32  destination path_len, 1..4095
 *  136  destination path bytes
 * Total 168 + path_len B, at most 4,263 B (P: 32 + 136 + 4095). The
 * witness is deliberately not a section-5 identity: it carries no times,
 * because the rename itself changes ctime and a pre-rename ctime would
 * reject our own file (q2-protocol); instead volume + inode + size +
 * a CRC32C over the whole saved stream recognize the installed temp.
 * CRC32C here is the same accidental-change witness the rest of the
 * format trusts, not collision resistance against a crafted file.
 * Recovery reads the destination's actual binding: matching the witness
 * selects replay from S, matching the expected identity (or the
 * expected absence) selects the old replay, anything else is an
 * unresolved conflict with bytes preserved (A23, T5 contract).
 *
 * FINISHED, in the durably published finish image (or equivalently
 * closing the token out in a complete CURRENT image):
 *   12  u64  installed generation key (declared in that same image)
 *   20  u64  snapshot sequence (that image's own s)
 *   28  u64  saved content id
 *   36  u32  destination path_len, then path bytes at 40
 * Total 72 + path_len B, at most 4,167 B (P: 32 + 40 + 4095).
 *
 * CANCELLED, after the namespace is reconciled and an independently
 * usable current representation is secured:
 *   12  u64  preserved checkpoint: sequence of its GROUP_COMMIT in
 *            this image, or 0 if the prior representation still serves
 *   20  u64  saved content id (meaningful only with its flag)
 *   28  u32  outcome: JOURNAL_SAVE_CANCEL_* below
 *   32  u32  flags: JOURNAL_SAVE_CANCEL_HAS_SAVED, other bits 0
 * Total 68 B (P: 32 + 36). Cancellation never marks the buffer saved
 * (Q9); dropping the token in a complete CURRENT image carrying the
 * same state is equivalent (q2-protocol).
 */
typedef enum journal_save_phase {
    JOURNAL_SAVE_PREPARED = 1,
    JOURNAL_SAVE_FINISHED = 2,
    JOURNAL_SAVE_CANCELLED = 3
} journal_save_phase;
typedef enum journal_save_cancel_outcome {
    JOURNAL_SAVE_CANCEL_PRIOR_TARGET = 0,
    JOURNAL_SAVE_CANCEL_CONFLICT = 1,
    JOURNAL_SAVE_CANCEL_UNTITLED = 2
} journal_save_cancel_outcome;
#define JOURNAL_SAVE_CANCEL_HAS_SAVED UINT32_C(1)
/* VIEW (11) is a bounded opaque payload, because no Wave 1 consumer
 * defines view fields and freezing the view-owning layer's schema here
 * would freeze someone else's format (compare undo's 16 B transported
 * cursor state, undo.h): 0 u32 view version (owned by the layer that
 * owns views; the journal neither allocates its values nor rejects one
 * it does not know -- that layer ignores or defaults it), then 0..256 B
 * of opaque bytes the journal carries and checksums but never
 * interprets. (G bound: 4 x v1's full VIEW payload of 64 B (P: 8 x 8),
 * headroom stated as a multiple, not a field guess.) Total at most
 * 292 B (P: 32 + 4 + 256).
 */
#define JOURNAL_VIEW_MAX_OPAQUE 256u
/* TABS (12), buffer 0, carries exactly what session restore rebuilds:
 * session order, the active tab, and closed-but-kept membership (Q2
 * record table; harvest section 5 leaves carry {path, slot, origin,
 * view_state} per buffer, which BASE, TARGET and VIEW already carry --
 * TABS adds only the ordering over them, nothing guessed):
 *    0  u32  entry count
 *    4  u32  active index into the entries; UINT32_MAX iff count is 0
 *            or no entry is active; else it names an OPEN entry
 *    8  entries, each 12 B: u64 buffer id (nonzero),
 *            u32 membership (JOURNAL_TABS_OPEN / JOURNAL_TABS_CLOSED_KEPT)
 * Total 40 + 12 x count B (P: 32 + 8 + 12 per entry). Entry order is
 * session order: the OPEN entries in order are the tab order, and a
 * CLOSED_KEPT entry keeps the position it held when closed, so reopen
 * restores its place. The record maximum bounds count to
 * (1048576 - 40) / 12 = 87,378 (P, floored division) by length
 * arithmetic alone -- far above the 1,016 entries of section 3's
 * capacity derivation, which is a product limit, not a wire one.
 * Duplicate buffer ids are a semantic corruption for recovery, not
 * checked by the pure decoder (which does no quadratic scan on
 * hostile input).
 */
typedef enum journal_tabs_membership {
    JOURNAL_TABS_OPEN = 0,
    JOURNAL_TABS_CLOSED_KEPT = 1
} journal_tabs_membership;
#define JOURNAL_TABS_ENTRY_BYTES 12u
#define JOURNAL_TABS_NO_ACTIVE UINT32_MAX

/* ---- 7. Groups -------------------------------------------------------------
 * GROUP_BEGIN .. GROUP_COMMIT bound one contiguous multi-record command
 * or checkpoint. Groups never nest; BEGIN inside a group, COMMIT
 * without BEGIN, and an empty group (no non-PAD member) are invalid at
 * the offending record. PADs may sit inside a group and are not
 * members. Single-record commands carry no markers. Members take
 * effect whole or not at all: no member is delivered before its COMMIT
 * has validated (v1's group contract, kept). A CHECKPOINT_BY_VALUE
 * group has exactly the descriptor as first member, then INSERT
 * members only -- all for the descriptor's buffer, destinations
 * strictly cumulative from 0 (each INSERT starts where the previous
 * ended), the literal lengths summing -- with checked arithmetic,
 * never wrapping -- to exactly the descriptor's text length; an empty
 * text has no INSERT members -- and optionally one TARGET member for
 * the same buffer, last, restating its binding atomically with the
 * text (section 6). Any other shape makes the checkpoint group
 * invalid. A CHECKPOINT_BY_VALUE outside a group is invalid.
 */

/* ---- 8. Reader rules: validity, unknown things, torn tails, v1 ------------
 * Preamble first, before any record is read and before any repair:
 * fewer than 32 B, a CRC mismatch, a version other than 1, or a nonzero
 * required-feature bit each refuse the whole file, which is set aside
 * intact under its session lease and never truncated (Q5). A first word
 * of "SJNL" names the family even at an unknown version, so discovery
 * and retirement can set such a file aside by name and magic without
 * decoding it. A first word of "JNL1" is a v1 journal: the v2 reader
 * refuses it as foreign, without repair; importing v1 is a separate
 * read-only tool, not this reader (Q5). Anything else is not a journal.
 *
 * Record stream: at each stream offset the reader requires a full
 * header, "SJR1", a length within [32, 1 MiB] and within the remaining
 * input, a matching CRC, the exact next sequence, the section-3
 * buffer-id rule, and a payload valid by section 6 (including every
 * "must be zero" field and every enum's known values). The first record
 * failing any check ends the valid prefix: the reader reports the
 * valid stream length and a corrupt mark, applies nothing at or after
 * the failure, and never resynchronizes by scanning for the next magic
 * -- lengths chain, so a skip could land inside a payload and apply
 * wrong text. Repair of an owned journal (truncation at the valid
 * prefix) is L2-2b's act after this validation, never the decoder's.
 *
 * Unknown record type, ONE rule: refuse -- the record is invalid, the
 * valid prefix ends before it, and the bytes are preserved. There is
 * no skip: an unknown record might be a text edit, and silently passing
 * it would yield wrong text. A future format that adds a record type
 * must either bump the wire version or set a required-feature bit; a
 * version-1 reader then refuses the whole file, intact, instead of a
 * prefix. The same refuse-not-guess rule applies to unknown SAVE
 * phases, bindings, memberships, outcomes and flag bits.
 *
 * Torn tail (input ends mid-record or mid-group; indistinguishable in
 * pure bytes from a corrupt final record, and handled identically --
 * the valid prefix ends before it), one sentence per class:
 * - Preamble: torn means not a usable v2 journal; set aside intact.
 * - PAD: a torn PAD is dropped with no effect, like any PAD.
 * - Ungrouped text/metadata record: the torn record is dropped and the
 *   prefix before it stands (an edit is only ever acknowledged after
 *   its complete record was accepted, so a torn edit was never ACKed).
 * - Grouped records: a group with no validated COMMIT has no effect at
 *   all, including a torn CHECKPOINT_BY_VALUE group, whose buffer then
 *   keeps its prior representation.
 * - SAVE(PREPARED): a torn PREPARED counts as absent, and the rename it
 *   would authorize cannot have happened yet, because the rename waits
 *   for a sync through the complete record (A23 rule 2).
 * - VIEW / TABS: the torn update is lost, earlier metadata stands, and
 *   no text depends on it.
 *
 * Why a v2 journal is never offered to a v1 parser (A14): v2 lives in
 * the sublimite-v2 namespace (CLAUDE.md isolation rule), which v1 never
 * scans, and v1's own retirement probe skips any file whose first four
 * bytes are not "JNL1" (v1 retire.c at the pin). The "SJNL" preamble
 * adds the second direction: a v1 reader explicitly pointed at a v2
 * file fails its magic check at offset 0, so it cannot partially parse,
 * misinterpret or deliver a single v2 record -- and because v1's
 * writable replay truncates a corrupt file at its last valid byte (v1
 * journal.c at the pin), which here is byte 0, the namespace rule is
 * not decoration: it is what keeps that truncation unreachable.
 */

/* ---- Codec API (L2-2a) ----------------------------------------------------
 * The pure codec: encode, decode, validate. No file, no thread, no
 * piece type, and no allocation anywhere -- every output lives in
 * caller storage, and decoded slices borrow the caller's input bytes.
 * Pointer/output arguments are preconditions ASSERTed (A15); every
 * journal_codec_err below reports something input or capacity caused.
 * The codec validates everything one record, or one group's shape,
 * can prove on its own. Cross-record semantics -- generation
 * declaration tables and conflicts, unresolved keys, SAVE token
 * matching, text and source bounds -- are recovery's (L2-2b), on a
 * structurally valid prefix this codec has certified.
 */
typedef enum journal_codec_err {
    JOURNAL_CODEC_OK = 0,
    JOURNAL_CODEC_INVALID,     /* malformed or corrupt bytes, or a
                                  struct that violates section 6 */
    JOURNAL_CODEC_TRUNCATED,   /* input ends inside a preamble/record */
    JOURNAL_CODEC_FOREIGN,     /* first word is not "SJNL" ("JNL1" is
                                  a v1 journal: section 8) */
    JOURNAL_CODEC_UNSUPPORTED, /* "SJNL" at an unknown version, or an
                                  unknown required-feature bit */
    JOURNAL_CODEC_SEQUENCE,    /* sequence is not the expected next */
    JOURNAL_CODEC_NO_SPACE     /* caller output storage too small */
} journal_codec_err;

/* CRC-32/ISCSI of section 3. Hardware (SSE4.2) or portable, selected
 * per call through base_cpu_has_sse42(); both paths are one function
 * to callers and agree bit for bit. data may be NULL only when len
 * is 0 (the CRC of no bytes, 0). */
uint32_t journal_crc32c(const uint8_t *data, size_t len);

typedef struct journal_preamble {
    uint32_t version;
    uint32_t required_features;
    uint64_t generation_high_water;
    uint64_t buffer_high_water;
} journal_preamble;

/* Writes the 32 B preamble of section 2 at this wire version. */
void journal_preamble_encode(uint8_t out[JOURNAL_PREAMBLE_BYTES],
                             uint64_t generation_high_water,
                             uint64_t buffer_high_water);
/* Reads and fully validates a preamble. On any error *out is zeroed:
 * TRUNCATED under 32 B, FOREIGN on a non-SJNL first word, UNSUPPORTED
 * on version/features, INVALID on a CRC mismatch. */
journal_codec_err journal_preamble_decode(const uint8_t *bytes, size_t len,
                                          journal_preamble *out);

/* The canonical PAD length at a stream offset (section 4):
 * 0 when no PAD is needed there. */
size_t journal_pad_length(uint64_t stream_offset);

/* One decoded record: header fields plus a borrowed payload slice. */
typedef struct journal_frame {
    journal_record_type type;
    uint64_t sequence;
    uint64_t buffer_id;
    const uint8_t *payload;
    size_t payload_len;
    size_t total_len;
} journal_frame;

/* Frames header + payload into out, computing the CRC. Validates the
 * payload by section 6, the section-3 buffer-id rule, that a PAD's
 * length is canonical at stream_offset with an all-zero payload, and
 * that a non-PAD sequence is nonzero (PAD's is 0). Sequence chaining
 * across records is the caller's (and journal_stream_validate's).
 * On OK *out_len is the total record length. */
journal_codec_err journal_record_encode(uint8_t *out, size_t cap,
                                        size_t *out_len,
                                        journal_record_type type,
                                        uint64_t sequence, uint64_t buffer_id,
                                        const uint8_t *payload,
                                        size_t payload_len,
                                        uint64_t stream_offset);
/* Decodes and fully validates the record at the head of bytes[0..len),
 * sitting at stream_offset, whose non-PAD sequence must equal
 * expected_sequence (>= 1). On OK out borrows bytes. A length past
 * the record maximum is INVALID even if it also passes len; a length
 * within the maximum but past len is TRUNCATED. */
journal_codec_err journal_record_decode(const uint8_t *bytes, size_t len,
                                        uint64_t stream_offset,
                                        uint64_t expected_sequence,
                                        journal_frame *out);

/* Typed payloads: one struct per section-6 payload, slices borrowed.
 * *_encode writes the payload bytes (no header) into out and sets
 * *out_len, validating the struct by section 6 (INVALID) and the
 * capacity (NO_SPACE). *_decode requires frame->type to match and
 * revalidates the payload; on error *out is untouched. A caller
 * composes a whole record as payload-encode + journal_record_encode,
 * or reads one as journal_record_decode + payload-decode. PAD and
 * the group markers have no payload struct. CHECKPOINT_BY_VALUE's
 * rule of living only inside a group is a stream property (above),
 * not a payload one. */
typedef struct journal_generation {
    uint64_t key;
    journal_wire_identity identity;
    uint32_t prefix_len;
    uint32_t prefix_crc32c;
    uint32_t flags;
    const uint8_t *location;
    size_t location_len;
} journal_generation;
journal_codec_err journal_generation_encode(uint8_t *out, size_t cap,
                                            size_t *out_len,
                                            const journal_generation *in);
journal_codec_err journal_generation_decode(const journal_frame *frame,
                                            journal_generation *out);

typedef struct journal_base {
    uint64_t generation_key;
    uint64_t content_id;
} journal_base;
journal_codec_err journal_base_encode(uint8_t *out, size_t cap,
                                      size_t *out_len,
                                      const journal_base *in);
journal_codec_err journal_base_decode(const journal_frame *frame,
                                      journal_base *out);

typedef struct journal_target {
    journal_target_binding binding;
    journal_presence presence;
    journal_wire_identity expected;
    const uint8_t *path;
    size_t path_len;
} journal_target;
journal_codec_err journal_target_encode(uint8_t *out, size_t cap,
                                        size_t *out_len,
                                        const journal_target *in);
journal_codec_err journal_target_decode(const journal_frame *frame,
                                        journal_target *out);

typedef struct journal_insert {
    uint64_t destination;
    const uint8_t *bytes;
    size_t len;
} journal_insert;
journal_codec_err journal_insert_encode(uint8_t *out, size_t cap,
                                        size_t *out_len,
                                        const journal_insert *in);
journal_codec_err journal_insert_decode(const journal_frame *frame,
                                        journal_insert *out);

typedef struct journal_delete {
    uint64_t destination;
    uint64_t len;
} journal_delete;
journal_codec_err journal_delete_encode(uint8_t *out, size_t cap,
                                        size_t *out_len,
                                        const journal_delete *in);
journal_codec_err journal_delete_decode(const journal_frame *frame,
                                        journal_delete *out);

typedef struct journal_restore {
    uint64_t destination;
    uint64_t generation_key;
    uint64_t source_offset;
    uint64_t len;
} journal_restore;
journal_codec_err journal_restore_encode(uint8_t *out, size_t cap,
                                         size_t *out_len,
                                         const journal_restore *in);
journal_codec_err journal_restore_decode(const journal_frame *frame,
                                         journal_restore *out);

typedef struct journal_checkpoint {
    uint64_t covered_sequence;
    uint64_t content_id;
    uint64_t saved_content_id;
    uint64_t text_len;
    uint32_t flags;
} journal_checkpoint;
journal_codec_err journal_checkpoint_encode(uint8_t *out, size_t cap,
                                            size_t *out_len,
                                            const journal_checkpoint *in);
journal_codec_err journal_checkpoint_decode(const journal_frame *frame,
                                            journal_checkpoint *out);

typedef struct journal_save_witness {
    uint64_t volume;
    uint64_t index_low;
    uint64_t index_high;
    uint64_t size;
    uint32_t content_crc32c;
} journal_save_witness;
typedef struct journal_save {
    journal_save_phase phase;
    uint64_t token;
    union {
        struct journal_save_prepared {
            uint64_t previous_key;
            uint64_t new_key;
            uint64_t snapshot_sequence;
            uint64_t saved_content_id;
            journal_presence expected_presence;
            journal_wire_identity expected;
            journal_save_witness witness;
            const uint8_t *path;
            size_t path_len;
        } prepared;
        struct journal_save_finished {
            uint64_t installed_key;
            uint64_t snapshot_sequence;
            uint64_t saved_content_id;
            const uint8_t *path;
            size_t path_len;
        } finished;
        struct journal_save_cancelled {
            uint64_t checkpoint_commit_sequence;
            uint64_t saved_content_id;
            journal_save_cancel_outcome outcome;
            uint32_t flags;
        } cancelled;
    };
} journal_save;
journal_codec_err journal_save_encode(uint8_t *out, size_t cap,
                                      size_t *out_len,
                                      const journal_save *in);
journal_codec_err journal_save_decode(const journal_frame *frame,
                                      journal_save *out);

typedef struct journal_view {
    uint32_t version;
    const uint8_t *opaque;
    size_t opaque_len;
} journal_view;
journal_codec_err journal_view_encode(uint8_t *out, size_t cap,
                                      size_t *out_len,
                                      const journal_view *in);
journal_codec_err journal_view_decode(const journal_frame *frame,
                                      journal_view *out);

typedef struct journal_tabs_entry {
    uint64_t buffer_id;
    journal_tabs_membership membership;
} journal_tabs_entry;
/* TABS entries are caller arrays on both sides. Decode with too
 * small a cap returns NO_SPACE and still sets *count_out (and
 * *active_out) so the caller can size a second call. */
journal_codec_err journal_tabs_encode(uint8_t *out, size_t cap,
                                      size_t *out_len,
                                      const journal_tabs_entry *entries,
                                      uint32_t count, uint32_t active);
journal_codec_err journal_tabs_decode(const journal_frame *frame,
                                      journal_tabs_entry *entries,
                                      uint32_t cap, uint32_t *count_out,
                                      uint32_t *active_out);

/* Validates a whole image (preamble + record stream) structurally:
 * preamble, then per record as journal_record_decode, plus section
 * 7's group shape and checkpoint coverage. On a preamble error that
 * error returns and *out is zero. Otherwise *out always describes
 * the valid prefix: OK when the whole input is one valid image
 * ending outside a group; TRUNCATED at a torn tail (valid_bytes ends
 * before the partial record, or before an unterminated group's
 * BEGIN); INVALID or SEQUENCE at the first bad record (before it, or
 * before its group's BEGIN). valid_bytes counts file bytes including
 * the preamble; records counts non-PAD records in the valid prefix.
 * Nothing is applied, written or repaired: this is the read a repair
 * (L2-2b) is allowed to trust. */
typedef struct journal_stream_result {
    journal_preamble preamble;
    size_t valid_bytes;
    uint64_t records;
    uint64_t last_sequence;
} journal_stream_result;
journal_codec_err journal_stream_validate(const uint8_t *bytes, size_t len,
                                          journal_stream_result *out);

/* Incremental form of the same rules (L2-2b), for a reader that streams
 * a file through one record buffer: journal_stream_validate is exactly
 * a loop of journal_stream_next over its input, so the two can never
 * disagree. The group position is one explicit state, never a set of
 * booleans (CLAUDE.md, explicit FSMs). Offsets are stream offsets. */
typedef enum journal_stream_phase {
    JOURNAL_STREAM_OUTSIDE = 0,       /* no group open */
    JOURNAL_STREAM_GROUP_EMPTY,       /* after BEGIN, before any member */
    JOURNAL_STREAM_GROUP,             /* ordinary group with a member */
    JOURNAL_STREAM_CHECKPOINT,        /* descriptor seen; INSERT slices */
    JOURNAL_STREAM_CHECKPOINT_TARGET  /* its trailing TARGET seen */
} journal_stream_phase;
typedef struct journal_stream_state {
    journal_stream_phase phase;
    uint64_t offset;          /* where the next record starts */
    uint64_t expected;        /* next non-PAD sequence; 0: none can follow */
    uint64_t valid;           /* end of the committed prefix */
    uint64_t records, last_sequence;           /* committed prefix */
    uint64_t open_records, open_last_sequence; /* including an open group */
    uint64_t group_at, group_sequence;         /* open group's BEGIN */
    uint64_t chk_buffer, chk_text_len, chk_sum;
} journal_stream_state;
void journal_stream_init(journal_stream_state *s);
/* Decodes the record at the head of bytes[0, len), which sits at stream
 * offset s->offset, and checks it against section 7 given everything
 * fed before. OK: *out borrows bytes and s moves past the record (a PAD
 * included). Otherwise s is unchanged and the result is
 * journal_record_decode's (TRUNCATED, INVALID, SEQUENCE), INVALID for
 * a section-7 violation, or SEQUENCE when the last sequence was
 * UINT64_MAX. A group still open when the input ends is a torn tail:
 * the committed prefix (s->valid) stops at its BEGIN. */
journal_codec_err journal_stream_next(journal_stream_state *s,
                                      const uint8_t *bytes, size_t len,
                                      journal_frame *out);

/* ==== Runtime API (L2-2b) ==================================================
 * One live journal per session, owned by the UI thread; its worker steps
 * run as jobs on the application's shared work pool (Q7). Sources:
 * docs/decisions/L2-2b-journal-runtime.md (mechanisms, derivations, the
 * two state axes), wave1-L2-derivations.md Q3, Q6, Q7, A23.
 *
 * WRITE PATH. The UI encodes each record into one fixed buffer and
 * writes it with exactly one os_file_pwrite_once at the append position
 * (Q6): no allocation, lock, sync or job submission. JOURNAL_OK means
 * the whole record is in the named journal's page cache, which is what a
 * process kill needs (K). Durability against machine loss (M) comes from
 * sync barriers that journal_pump runs on a worker: the UI first writes
 * one canonical PAD so the barrier ends on a 4,096 B block (wire section
 * 4), then a job fdatasyncs a duplicate descriptor while the UI keeps
 * appending after the PAD. The worker never writes where the UI is
 * appending, except under a lease (below), during which appends return
 * BUSY.
 *
 * RESULTS of an append:
 *   JOURNAL_OK            written to the page cache.
 *   JOURNAL_MORE          journal_insert_step only: one slice written,
 *                         more remain (*progress advanced).
 *   JOURNAL_ERR_IO        accepted into RAM but the write failed, was
 *                         short or wrote nothing: the whole record and
 *                         its offset are held, suspension latches
 *                         APPEND_GAP, and journal_pump rewrites it on the
 *                         worker. Withhold success; never append it again
 *                         (progress includes it).
 *   JOURNAL_ERR_BUSY      nothing accepted; the journal makes progress by
 *                         itself (a lease, a gap or a directory barrier
 *                         being repaired): stage the record, pump, retry.
 *   JOURNAL_ERR_SUSPENDED nothing accepted; recovery is suspended until a
 *                         complete rotation from live state
 *                         (journal_rotate_start). Keep editing; show it.
 *   JOURNAL_ERR_FULL      nothing accepted: the image budget or the
 *                         sequence space is exhausted; suspension latches
 *                         NEEDS_ROTATION.
 * Caller errors (wrong group nesting, PAD or markers through
 * journal_append, NULL arguments) are ASSERTed (A15). */
#include "base/base.h"
#include "os/os.h"
#include "piece/piece.h"
#include "work/work.h"

typedef enum journal_err {
    JOURNAL_OK = 0,
    JOURNAL_MORE,
    JOURNAL_ERR_IO,
    JOURNAL_ERR_BUSY,
    JOURNAL_ERR_SUSPENDED,
    JOURNAL_ERR_FULL,
    JOURNAL_ERR_NOMEM,
    JOURNAL_ERR_EXISTS,   /* journal_open on a name that holds an image no
                             recovery handed over */
    JOURNAL_ERR_REFUSED   /* journal_open on a recovery that could not set
                             its damaged image aside (verdict DAMAGED) */
} journal_err;

/* Axis 1, the worker transport: who may touch the file now. Replaces
 * v1's active/failed booleans (derivation in the decision doc).
 *   IDLE      no job; the UI owns the append position.
 *   SYNC      a barrier job runs; the UI still appends after the PAD.
 *   LEASE     a job owns the append position (gap repair, self-contained
 *             checkpoint, rotation): appends return BUSY.
 *   RETIRING  the job's result is adopted, its slot not yet physically
 *             finished; the UI owns the append position, no new job. */
typedef enum journal_transport {
    JOURNAL_TRANSPORT_IDLE = 0,
    JOURNAL_TRANSPORT_SYNC,
    JOURNAL_TRANSPORT_LEASE,
    JOURNAL_TRANSPORT_RETIRING
} journal_transport;

/* Axis 2, recovery suspension: may the journal accept records, and what
 * repairs it. Replaces v1's append_failed, append_pending,
 * writeback_failed, directory_pending, recovery_pending and the sticky
 * FULL error.
 *   HEALTHY            appends accepted.
 *   APPEND_GAP         a UI write failed; the held record is rewritten
 *                      by a lease job (appends BUSY meanwhile).
 *   DIRECTORY_PENDING  a rotation's rename landed but its directory
 *                      barrier failed; retried by a job (appends BUSY).
 *   WRITEBACK_LOST     a sync barrier failed: pages since the last good
 *                      barrier may be lost on disk, and no rewrite can
 *                      prove otherwise; only a rotation repairs it.
 *   NEEDS_ROTATION     budget or sequence space exhausted, or a lease
 *                      write left bytes it could not remove: only a
 *                      rotation repairs it. */
typedef enum journal_suspension {
    JOURNAL_HEALTHY = 0,
    JOURNAL_APPEND_GAP,
    JOURNAL_DIRECTORY_PENDING,
    JOURNAL_WRITEBACK_LOST,
    JOURNAL_NEEDS_ROTATION
} journal_suspension;

typedef struct journal_status {
    journal_transport transport;
    journal_suspension suspension;
    uint64_t accepted_sequence; /* last sequence accepted (RAM) */
    uint64_t written_sequence;  /* last sequence wholly in the page cache */
    uint64_t durable_sequence;  /* last sequence behind a completed barrier */
    uint64_t file_bytes;        /* append position, preamble included */
    uint64_t durable_bytes;     /* end of the last completed barrier */
    os_err last_error;          /* the last failed write, sync or rename */
    int last_native_error;
} journal_status;

/* Caller policy, no defaults here (the owner of the session decides and
 * derives them): budget_bytes is the image size at which an ungrouped
 * record or a GROUP_BEGIN reports FULL (members of an open group are
 * admitted past it so a group can always commit); a barrier starts when
 * sync_bytes have been written since the last one, or sync_interval_ns
 * after the first write it does not yet cover. */
typedef struct journal_options {
    uint64_t budget_bytes;
    uint64_t sync_bytes;
    uint64_t sync_interval_ns;
} journal_options;

typedef struct journal journal;
typedef struct journal_recovery journal_recovery;

/* Opens the live journal `name` (one component) in the pinned directory
 * dir for appending, and makes its starting point a synced boundary on a
 * 4,096 B block (PAD, then fsync) before returning.
 *   rec == NULL, or rec reports ABSENT or REFUSED: creates a fresh image
 *     (preamble written and synced under a temporary name, renamed into
 *     place with NOREPLACE, directory synced) carrying rec's allocator
 *     high-waters; JOURNAL_ERR_EXISTS if the name holds a file.
 *   rec reports CLEAN, REPAIRED or SET_ASIDE: continues that image at its
 *     valid end and next sequence, after checking it is the same inode
 *     and size recovery left (else JOURNAL_ERR_IO).
 *   rec reports DAMAGED: JOURNAL_ERR_REFUSED (the image could not be set
 *     aside, so nothing may append to it).
 * The pool must outlive the journal; it needs one bulk worker. Setup
 * only: allocates (base arena) and blocks on storage. */
journal_err journal_open(journal **out, const os_dir *dir, const char *name,
                         work_pool *pool, const journal_recovery *rec,
                         const journal_options *opt);
/* JOURNAL_ERR_BUSY while a job runs (pump, then retry); otherwise closes
 * and frees. Requests no sync: make the state durable first if wanted. */
journal_err journal_close(journal *j);
journal_status journal_get_status(const journal *j);

/* Allocators of section 5: strictly monotonic, never 0, refusing before
 * wrap (JOURNAL_ERR_FULL). The image's preamble and every mention in it
 * seed them on recovery. */
journal_err journal_new_generation_key(journal *j, uint64_t *key);
journal_err journal_new_buffer_id(journal *j, uint64_t *id);

/* ---- Typing path ----
 * INSERT slicing is encoder policy, not wire law (wire section 3). v2's
 * reason for a slice at all: one journal_insert_step call is one unit of
 * UI-thread work between input checks, and the UI slice is 0.5 ms (G,
 * v1 perf/01-perf-target.md G1 row, kept by v2). The call gets half of
 * it, 250 us; the other half is the loop's own work and the margin for
 * regimes not measured ([bat], cold caches). Measured (M)[AC] load1
 * 3.09, 2026-11-24 01:17 CET, bench/journal_bench.c, n = 1,000 each:
 * a record of 256 KiB costs p99 196 us (encode + CRC32C + one
 * pwrite_once), one insert_step of 256 KiB p99 212 us, a record of
 * 512 KiB p99 368 us. The slice is the largest power of two within
 * 250 us at p99: 256 KiB. (v1's 16 KiB was an inherited ceiling, not a
 * derivation; the wire's 1 MiB record maximum is not this policy's
 * ceiling, section 3.) Re-derive when the bench moves. */
#define JOURNAL_INSERT_SLICE_BYTES (256u * 1024u)
journal_err journal_group_begin(journal *j);
journal_err journal_group_commit(journal *j);
/* Initialize *progress to 0 and keep bytes stable until JOURNAL_OK.
 * Each call writes INSERT(dest + *progress, next slice) and advances
 * *progress by the slice (also on JOURNAL_ERR_IO). len >= 1. An insert of
 * more than one slice is a multi-record command: inside
 * journal_group_begin/commit, or a kill between slices recovers a prefix
 * of it (wire section 7). */
journal_err journal_insert_step(journal *j, uint64_t buffer_id, uint64_t dest,
                                const uint8_t *bytes, size_t len,
                                size_t *progress);
journal_err journal_append_delete(journal *j, uint64_t buffer_id,
                                  uint64_t dest, uint64_t len);
journal_err journal_append_restore(journal *j, uint64_t buffer_id,
                                   const journal_restore *r);
/* Any other record by its codec payload: GENERATION, BASE, TARGET, VIEW,
 * TABS, SAVE, INSERT, DELETE, RESTORE_BASE_RANGE. ASSERT: not PAD, a
 * group marker or CHECKPOINT_BY_VALUE (those are the writer's). */
journal_err journal_append(journal *j, journal_record_type type,
                           uint64_t buffer_id, const uint8_t *payload,
                           size_t payload_len);

/* ---- Worker transport (UI, outside input-to-submit) ----
 * Adopts a finished job, then starts at most one job, in this order:
 * rewrite a held record (APPEND_GAP), retry a directory barrier
 * (DIRECTORY_PENDING), or a sync barrier when sync_bytes are unsynced,
 * the oldest unsynced write is sync_interval_ns old, or a sync was
 * requested. Never waits; no drain loop. The job's completion message is
 * bound to the journal (work_mailbox_bind), so the application's
 * ordinary drain delivers it here and nowhere else. Returns the first
 * error of a UI write it made (a PAD: JOURNAL_ERR_IO, held as above), else
 * JOURNAL_OK. */
journal_err journal_pump(journal *j, uint64_t now_ns);
/* The next journal_pump starts a barrier even below the thresholds. */
void journal_request_sync(journal *j);

/* ---- Self-contained checkpoint (A23) ----
 * A buffer whose text is at or below this size is made self-contained,
 * a CHECKPOINT_BY_VALUE group of its whole text, once it has an
 * acknowledged edit; until that group is durable its first edits stay
 * BASE-relative. After it, its restores are journaled by value (the
 * caller's encoder).
 * Derivation: the worker writes the group under a lease, and every
 * append in the meantime returns BUSY, so the lease delays other journal
 * work by its length. Budget, as for the slice above: half the 0.5 ms
 * UI slice at p99, 250 us. The lease from journal_checkpoint_start to the
 * adoption that lets appends through, measured (M)[AC] load1 3.08,
 * 2026-11-24 01:17 CET, bench/journal_bench.c "checkpoint_lease", piece
 * model, n = 1,000 each: p99 244 us at 256 KiB, 470 us at 512 KiB
 * (498 us in a run at load1 2.41), 921 us at 1 MiB; about 1.25 B/ns
 * plus a scheduling floor near 60 us. The size is the largest power of
 * two within 250 us at p99: 256 KiB, half the budget whole. v1's split
 * at the pin, for comparison: a modified text stayed whole-text up to
 * FILE_PREFIX_MAX, 1 MiB (v1 file.h:86, docs/decisions/edit-28u.md
 * decision 5), a number tied to its synchronous-open prefix, not to a
 * write budget. Re-derive with the real piece tree. */
#define JOURNAL_SELF_CONTAINED_BYTES (256u * 1024u)
typedef struct journal_checkpoint_request {
    uint64_t buffer_id;
    piece_snapshot *text;      /* the text at covered_sequence; the journal
                                  retains it until the job finishes */
    uint64_t covered_sequence; /* < the next sequence (ASSERT) */
    uint64_t content_id;
    uint64_t saved_content_id; /* with JOURNAL_CHECKPOINT_HAS_SAVED */
    uint32_t flags;
    const journal_target *target; /* NULL, or the group's last member
                                     (copied at the call) */
} journal_checkpoint_request;
/* Leases the append position to a worker job that writes the group at
 * the end of the image. BUSY while another job runs or a group is open;
 * SUSPENDED unless HEALTHY. Once the job is adopted, the group's COMMIT
 * sequence is journal_checkpoint_committed(). */
journal_err journal_checkpoint_start(journal *j,
                                     const journal_checkpoint_request *req);
/* The COMMIT sequence of the last adopted checkpoint group, 0 if none:
 * the buffer is self-contained once durable_sequence reaches it. */
uint64_t journal_checkpoint_committed(const journal *j);

/* ---- Rotation ----
 * A complete new image from live state: preamble with the allocators'
 * high-waters, every generation declared, each buffer's text (BY_VALUE:
 * a CHECKPOINT_BY_VALUE group; BY_REFERENCE: BASE then DELETE / INSERT /
 * RESTORE_BASE_RANGE records describing the text over its ORIGINAL,
 * whose generation must be declared here), its TARGET and VIEW, then
 * TABS. Written and synced under a temporary name by a lease job,
 * renamed over the journal, the directory synced; the UI then appends to
 * the new image. Clears every suspension. Everything r points to (and
 * the snapshots) must stay valid and unchanged until the job is adopted
 * (journal_get_status shows a transport other than LEASE). */
typedef enum journal_rotation_text {
    JOURNAL_ROTATE_BY_VALUE = 0,
    JOURNAL_ROTATE_BY_REFERENCE = 1
} journal_rotation_text;
typedef struct journal_rotation_buffer {
    uint64_t buffer_id;
    piece_snapshot *text;
    journal_rotation_text mode;
    uint64_t base_key;         /* BY_REFERENCE: ORIGINAL's generation (0:
                                  empty origin) */
    uint64_t content_id, saved_content_id;
    uint32_t flags;            /* JOURNAL_CHECKPOINT_HAS_SAVED */
    const journal_target *target; /* NULL: none */
    const journal_view *view;     /* NULL: none */
} journal_rotation_buffer;
typedef struct journal_rotation {
    const journal_generation *generations;
    size_t generation_count;
    const journal_rotation_buffer *buffers;
    size_t buffer_count;
    const journal_tabs_entry *tabs; /* NULL: no TABS record */
    uint32_t tab_count, tab_active;
} journal_rotation;
/* BUSY while another job runs or a group is open. */
journal_err journal_rotate_start(journal *j, const journal_rotation *r);

/* ---- Sources ----
 * Fills g->identity, prefix_len and prefix_crc32c from an open regular
 * file: stat, then one read of its first min(size, 4096) B, then stat
 * again, refusing (OS_E_BUSY) a file that changed across the read. The
 * caller sets key, flags and location. */
os_err journal_capture(os_file *fd, journal_generation *g);

/* Opens a tree whose ORIGINAL is the size bytes of fd, which recovery has
 * verified against its declaration (and re-verifies after the call).
 * NULL in journal_recover selects the built-in opener: a private copy
 * read in full (piece_open_copy). A mapped opener belongs to L2 file. */
typedef piece_err (*journal_open_source_fn)(void *ctx, piece_reserve *r,
                                            os_file *fd, uint64_t size,
                                            piece_tree **out);

/* ---- Recovery ----
 * Validate, then repair, then plan, then apply: forward reads of the
 * image through one 1 MiB record buffer (wire section 3's reader cost).
 *  1. The preamble first: refused (TRUNCATED, FOREIGN, UNSUPPORTED,
 *     INVALID) means the file is renamed aside intact and nothing is
 *     read further (REFUSED). Never truncated.
 *  2. The record stream (journal_stream_next) to its first failure.
 *     A TORN TAIL -- the input ends inside the failing record or inside
 *     an open group, and no CRC-valid record starts after the failing
 *     header -- was never acknowledged (wire section 8): truncated in
 *     place (REPAIRED). ANYTHING ELSE (a complete record failing, or
 *     valid records after a "torn" one) may hide acknowledged edits:
 *     the whole file is copied aside byte for byte (synced), a new image
 *     holding exactly the valid prefix replaces the name (SET_ASIDE).
 *     If the copy cannot be made the file is left untouched (DAMAGED).
 *  3. Plan over the committed prefix: the declaration table (a
 *     conflicting redeclaration distrusts its key), each buffer's origin
 *     (the last BASE, or the committed CHECKPOINT_BY_VALUE with the
 *     greatest covered sequence when that is not before the BASE), last
 *     TARGET, VIEW and TABS.
 *  4. Install origins: an empty origin, a checkpoint's literal text, or
 *     a BASE generation opened by its declared location and verified
 *     (identity projection and prefix witness, before and after). A
 *     buffer whose origin is a checkpoint never opens its BASE.
 *  5. Apply each buffer's text records after its origin in sequence
 *     order, a group's members under one piece checkpoint per tree.
 *     RESTORE_BASE_RANGE by reference when its key is the tree's
 *     verified ORIGINAL, else by value from its own verified source.
 * A BASE (or by-value restore source) that is missing or not its
 * declared identity makes the buffer UNRESOLVED_BASE, in every path:
 * nothing is applied to that file, and every literal the journal holds
 * for the buffer after its origin is offered. A record that cannot apply
 * (bounds, no origin, undeclared or distrusted key) makes it
 * INCONSISTENT, likewise with literals. Trees without history: piece is
 * called directly. Setup only (allocates, blocks). */
typedef enum journal_image_verdict {
    JOURNAL_IMAGE_ABSENT = 0, /* no file under the name */
    JOURNAL_IMAGE_CLEAN,
    JOURNAL_IMAGE_REPAIRED,   /* torn tail truncated in place */
    JOURNAL_IMAGE_SET_ASIDE,  /* original kept as set_aside; name holds
                                 the valid prefix */
    JOURNAL_IMAGE_DAMAGED,    /* damage, and it could not be set aside */
    JOURNAL_IMAGE_REFUSED     /* preamble refused; kept as set_aside */
} journal_image_verdict;
typedef enum journal_buffer_verdict {
    JOURNAL_BUFFER_RECOVERED = 0,
    JOURNAL_BUFFER_UNRESOLVED_BASE,
    JOURNAL_BUFFER_INCONSISTENT
} journal_buffer_verdict;
typedef enum journal_origin_kind {
    JOURNAL_ORIGIN_NONE = 0,
    JOURNAL_ORIGIN_BASE,
    JOURNAL_ORIGIN_CHECKPOINT
} journal_origin_kind;
typedef struct journal_literal {
    uint64_t sequence, destination;
    const uint8_t *bytes;   /* owned by the recovery */
    size_t len;
} journal_literal;
typedef struct journal_recovered_buffer {
    uint64_t id;
    journal_buffer_verdict verdict;
    piece_tree *tree;       /* RECOVERED; take it by setting this NULL */
    journal_origin_kind origin;
    uint64_t base_key;      /* ORIGIN_BASE: the tree's ORIGINAL */
    uint64_t origin_sequence; /* BASE's sequence or the covered sequence */
    uint64_t content_id;      /* at the origin */
    uint64_t saved_content_id;
    uint32_t checkpoint_flags;
    const journal_literal *literals; /* not RECOVERED */
    size_t literal_count;
    bool has_target;
    journal_target target;  /* path owned by the recovery */
    bool has_view;
    journal_view view;      /* opaque bytes owned by the recovery */
} journal_recovered_buffer;
struct journal_recovery {
    journal_image_verdict verdict;
    journal_codec_err failure;      /* the first failure (OK if none) */
    char set_aside[OS_NAME_MAX];    /* SET_ASIDE / REFUSED: name in dir */
    uint64_t file_bytes;            /* the file as found */
    uint64_t valid_bytes;           /* committed prefix, preamble included */
    uint64_t last_sequence;
    os_identity image;              /* the inode journal_open continues */
    uint64_t generation_high_water, buffer_high_water;
    journal_recovered_buffer *buffers;
    size_t buffer_count;
    bool has_tabs;
    journal_tabs_entry *tabs;
    uint32_t tab_count, tab_active;
    base_arena arena_;              /* private */
};
journal_err journal_recover(journal_recovery *out, const os_dir *dir,
                            const char *name, piece_reserve *reserve,
                            journal_open_source_fn open_source, void *ctx);
/* Closes every tree not taken and frees the recovery's storage. */
void journal_recovery_release(journal_recovery *rec);

#endif



