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

#include "base/base.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ---- 1. Conventions ------------------------------------------------------
 * Byte order is little-endian for every integer, fixed here, never the
 * host's. Every field is encoded and decoded explicitly, field by field;
 * no C struct is ever written or read as memory, and no padding exists
 * anywhere except a declared PAD record (section 4). Offsets in section 6
 * are payload offsets (byte 32 of the record). A journal file is one
 * 32 B preamble followed by a record stream. Stream offset 0 is
 * file offset JOURNAL_PREAMBLE_BYTES. Sequences count all non-PAD
 * records in physical order, including abandoned members. An image
 * is the complete file belonging to this preamble. A seal is a
 * boundary after which a new append starts on a fresh wire page of
 * that file. Rotation starts a new image with sequence 1.
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
 * reader treats a successor claim as out-of-sequence. Only PAD,
 * GENERATION and TABS have owner zero. Every other record, including
 * each marker and member, names its nonzero buffer in the header.
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
 * The type word determines role and payload without consulting a slot.
 * Type numbers through TABS retain their payload families (G, landed
 * assignments); 7 and 8 specialize the former generic markers as
 * command markers. Subsequent numbers are successive unused values
 * (G, one value per additional role below). All unlisted words are invalid.
 *
 * For stream offset x, let r = ((x % P) + H) % P, where P is
 * JOURNAL_PAGE_BYTES and H is JOURNAL_PREAMBLE_BYTES. This computes the
 * absolute file-end residue without overflowing x + H. If r is zero the
 * barrier needs no PAD and a PAD here is invalid. Otherwise d = P - r.
 * Emit exactly one PAD of total length d if d >= JOURNAL_HEADER_BYTES,
 * or d + P otherwise. Its sequence, owner and payload bytes are zero.
 * Verify this same length on read. Checked file-position arithmetic must
 * also accommodate the complete append. P = 4096 (G, existing wire block)
 * and H = 32 (P, preamble layout). At x = 0 the seal costs 4064 bytes
 * (P, P - H). At x = 4064 it costs zero (P, x + H = P).
 */
typedef enum journal_record_type {
    JOURNAL_REC_PAD = 0,
    JOURNAL_REC_GENERATION = 1,
    JOURNAL_REC_BASE = 2,
    JOURNAL_REC_TARGET = 3,
    JOURNAL_REC_INSERT = 4,
    JOURNAL_REC_DELETE = 5,
    JOURNAL_REC_RESTORE_BASE_RANGE = 6,
    JOURNAL_REC_COMMAND_BEGIN = 7,
    JOURNAL_REC_COMMAND_COMMIT = 8,
    JOURNAL_REC_CHECKPOINT_BY_VALUE = 9,
    JOURNAL_REC_SAVE = 10,
    JOURNAL_REC_VIEW = 11,
    JOURNAL_REC_TABS = 12,
    JOURNAL_REC_COMMAND_INSERT = 13,
    JOURNAL_REC_COMMAND_DELETE = 14,
    JOURNAL_REC_COMMAND_RESTORE_BASE_RANGE = 15,
    JOURNAL_REC_CHECKPOINT_BEGIN = 16,
    JOURNAL_REC_CHECKPOINT_COMMIT = 17,
    JOURNAL_REC_CHECKPOINT_INSERT = 18,
    JOURNAL_REC_CHECKPOINT_TARGET = 19
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
 * the image (including declaration, operand and SAVE keys), then refuses
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
 * COMMAND_BEGIN / COMMAND_COMMIT and CHECKPOINT_BEGIN /
 * CHECKPOINT_COMMIT: empty payload, total 32 B each (P: header alone).
 */
#define JOURNAL_DELETE_BYTES 48u
#define JOURNAL_RESTORE_BASE_RANGE_BYTES 64u
#define JOURNAL_MARKER_BYTES 32u
/* CHECKPOINT_BY_VALUE (9) is a descriptor, legal only as the first
 * member of a checkpoint slot, followed by CHECKPOINT_INSERT slices
 * that rebuild the whole text from an empty origin (section 7):
 *    0  u64  covered sequence: this image's last sequence whose effect
 *            for this buffer the checkpoint replaces (strictly before
 *            CHECKPOINT_BEGIN)
 *    8  u64  content id at the covered boundary
 *   16  u64  saved content id (meaningful only with HAS_SAVED)
 *   24  u64  text length: exact sum of the following slices' literals
 *   32  u32  flags below; other bits must be 0
 * Total 68 B (P: 32 + 36). The group costs text length + 40 B per slice
 * record + 132 B (P: 32 BEGIN + 68 descriptor + 32 COMMIT, each slice
 * adding its 32 B header and 8 B destination), plus a CHECKPOINT_TARGET member
 * when present. Recovery selects a committed checkpoint as the
 * buffer's text at its covered boundary and then applies, in sequence
 * order, that buffer's whole commands with final sequence greater than
 * covered, even when finalized before CHECKPOINT_COMMIT (q2-protocol T2). Dirty is derived, never stored as
 * its own bit: no HAS_SAVED, or saved content id unequal to the
 * current one (Q10). Target binding and conflict state are TARGET's
 * facts alone, never restated here: a checkpoint that must move or
 * reaffirm the target carries a CHECKPOINT_TARGET as the last
 * member of its slot (section 7), and a foreign replacement shows as the
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
 *            declared at PREPARED; a subsequent declaration is staged
 *            until the matching complete FINISHED)
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
 * Replay selects prior text on matching old expectation, S and its
 * suffix on matching installed witness, or independent checkpoint
 * and suffix when available on foreign binding (section 7).
 *
 * FINISHED, appended after the verified installed GENERATION:
 *   12  u64  installed generation key
 *   20  u64  snapshot sequence s from PREPARED
 *   28  u64  saved content id
 *   36  u32  destination path_len, then path bytes at 40
 * Total 72 + path_len B, at most 4,167 B (P: 32 + 40 + 4095).
 *
 * CANCELLED, after the namespace is reconciled and an independently
 * usable current representation is secured:
 *   12  u64  preserved checkpoint: sequence of its CHECKPOINT_COMMIT in
 *            this image, or 0 if the prior representation still serves
 *   20  u64  saved content id (meaningful only with its flag)
 *   28  u32  outcome: JOURNAL_SAVE_CANCEL_* below
 *   32  u32  flags: JOURNAL_SAVE_CANCEL_HAS_SAVED, other bits 0
 * Total 68 B (P: 32 + 36). Cancellation never marks the buffer saved
 * (Q9). Select only a verified prior saved association.
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

/* ---- 7. Slots and SAVE decisions ----------------------------------------- */
/* Markers have empty payloads. A role-specific INSERT, DELETE, range
 * restore or TARGET uses exactly its standalone payload schema. A typed
 * decoder accepts precisely the types assigned its schema. The frame
 * always exposes the actual type. CHECKPOINT_BY_VALUE is exclusively the
 * checkpoint descriptor. BASE, GENERATION, SAVE, VIEW and TABS always
 * stand alone. A lone text operation uses a standalone text type.
 *
 * A slot is a pending list for one (buffer, kind), where kind is command
 * or checkpoint. BEGIN replaces that slot alone. The BEGIN sequence
 * identifies its instance during the scan. A member requires an open
 * slot of its owner and kind. COMMIT closes the latest such BEGIN. Read
 * the following grammars on that projection of the stream:
 *
 * command     = COMMAND_BEGIN command-member+ COMMAND_COMMIT
 * checkpoint  = CHECKPOINT_BEGIN CHECKPOINT_BY_VALUE
 *               CHECKPOINT_INSERT* CHECKPOINT_TARGET? CHECKPOINT_COMMIT
 *
 * A command member is COMMAND_INSERT, COMMAND_DELETE or
 * COMMAND_RESTORE_BASE_RANGE. Apply its list in member order, atomically
 * at COMMIT. A standalone text operation also retires a leftover command
 * slot on its own buffer. Only the producer may abandon a live command.
 * An abort needs no record. Its bytes can survive until replacement or EOF.
 * PAD and unrelated records do not enter the list. In particular a key
 * on the checkpoint's own buffer can complete between any two members.
 *
 * At checkpoint BEGIN the writer pins the published text and its last
 * complete text boundary on that buffer, with current/saved identities
 * and binding evidence. A provisional edit is ineligible. A descriptor
 * repeats this covered sequence (zero for the initial empty boundary),
 * strictly earlier than BEGIN. The slices start at zero and each starts
 * at the checked sum of preceding literal lengths. They rebuild exactly
 * the declared text length. Empty text has no slices. The optional last
 * TARGET is atomic with completion. The text replaces history only
 * through the covered boundary; complete suffix commands are retained,
 * including commands finalized before checkpoint COMMIT. Metadata has its
 * own effective order. Before COMMIT omit an obsolete optional restatement
 * if still possible, otherwise supersede the checkpoint. A stale saved
 * association cannot override a newer save decision.
 *
 * Each buffer has at most one active checkpoint producer. A new request
 * joins it if its pinned coverage and binding satisfy the request;
 * otherwise a fresh BEGIN supersedes it. No request waits in a queue.
 * The UI compares producer identity before appending any delivery,
 * including a final marker. A retired worker may release its pins but
 * can never close the replacement's slot. No member carries an instance
 * field. Additional framing is zero bytes (P, roles reuse the type word
 * and owners reuse the header id). A one-byte key costs 41 bytes
 * (P, 32-byte header plus 8-byte destination plus one literal).
 *
 * A token is the pair (saver buffer, nonzero SAVE token), unique for the
 * transaction. PREPARED reserves its fresh new generation key. It records
 * S, the synced temp's text, by its whole-content witness, the saver's
 * covered boundary s and content id, and the prior destination expectation.
 * At an open token the old expectation selects prior replay. A matching
 * installed temp selects S plus commands finalized after s. A foreign
 * binding selects independently usable checkpoint text and suffix when
 * available, otherwise unresolved source and salvageable literal bytes.
 * The reader represents these as conditional branches until observation.
 *
 * Append GENERATION for PREPARED's new key from the verified installed
 * descriptor before FINISHED. Stage it under the unique new-key claim;
 * until the matching final it reserves a key only. Neither BASE nor a
 * range restore may use that staged key. FINISHED must match the token,
 * key, s, content id and path and the preceding declaration must agree
 * with the path and witness inode and size. Complete FINISHED activates
 * the declaration and the saver's origin, binding and saved id at s in
 * one plan operation. A saved association is adopted only together with a
 * verified binding, live and in recovery (advisor, at signature): a
 * complete FINISHED whose installed file is not at the path when it is
 * verified yields an origin candidate, never a saved id. The live outcome
 * is SAVE_FAILED with CHANGED: the saved id unknown, the buffer dirty and
 * the conflict checkpoint written. There is no second terminal record.
 * Dirty costs the user a prompt; clean could cost the text, because a
 * clean buffer whose file changed is eligible for the silent reload of
 * unmodified text. A later range needs the active, verified generation
 * and must obey the permanent VALUE_ONLY policy. Ordinary redeclarations
 * retain their strict identity and monotone flags. A pending or abandoned
 * installed declaration never enables an operand. The allocator advances
 * beyond all mentioned keys even if their declaration never activates.
 *
 * Derive affected peer bindings from the transaction's displaced target
 * expectation and the session's binding index at the decision. On the
 * installed branch detach still-displaced peers without replacing their
 * independent text, suffix, view or saved identity with the saver's.
 * On the prior branch keep prior bindings. A later independent binding
 * update wins. An unconditional peer TARGET before FINISHED cannot stage
 * this decision. For Keep or conflict use the checkpoint's atomic TARGET.
 *
 * CANCELLED resolves only its matching open token after namespace
 * reconciliation. Its nonzero checkpoint sequence must name an earlier
 * complete checkpoint COMMIT of that buffer that secures the preserved
 * state (with its complete suffix and applicable binding). Zero selects
 * a still-usable prior representation. Preserve only a verified prior
 * saved association. Cancellation never installs the requested saved id.
 * A complete terminal is effective for process recovery. A live success
 * or irreversible dependent step needs verification and the matching
 * durable barrier. Plan successive transactions before requiring any
 * source that a later usable origin supersedes. Keep an independently
 * usable checkpoint on foreign binding even after a previous finish.
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
 * Record stream: validate the framing, CRC, global sequence, owner and
 * payload, then check the slot transition. Stop before the first bad
 * record. The prefix is physical and retains all preceding complete
 * frames, even where a slot remains open. Do not resynchronize.
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
 * EOF at a complete record boundary is OK, even with open slots.
 * A partial last record yields TRUNCATED at its start. A complete bad
 * record yields INVALID or SEQUENCE at its start. Preserve damage
 * intact; only a demonstrably torn last record admits in-place repair.
 * An incomplete slot has no logical effect. Semantic failures never
 * authorize truncation of a physically valid prefix.
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
 * The single-record codec allocates nothing and borrows caller bytes.
 * The physical scanner and logical reader use caller arena storage.
 * Pointer/output arguments are preconditions ASSERTed (A15); input and
 * capacity failures are return codes. Neither API performs I/O.
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

/* Length to seal the absolute file end at stream_offset (section 4).
 * Zero means the end is already aligned. */
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
 * that a non-PAD sequence is nonzero (PAD's is 0). The full append
 * must fit in a u64 file position including the preamble. Sequence chaining
 * across records is the caller's (and journal_plan_read's).
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
 * capacity (NO_SPACE). *_decode accepts exactly the roles assigned its
 * schema and revalidates the payload. The frame exposes the actual type; on error *out is untouched. A caller
 * composes a whole record as payload-encode + journal_record_encode,
 * or reads one as journal_record_decode + payload-decode. PAD and
 * markers have no payload struct. Slot membership is validated by
 * the whole-image scan. */
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

/* Certifies the physical prefix, including slot grammar (sections 7–8).
 * valid_bytes counts absolute file bytes up to the failing record or
 * EOF. records and last_sequence include every accepted non-PAD, even
 * abandoned members. */
typedef struct journal_stream_result {
    journal_preamble preamble;
    size_t valid_bytes;
    uint64_t records;
    uint64_t last_sequence;
} journal_stream_result;
/* The plan borrows its image and uses only the caller's workspace.
 * Each operation is indexed once in final-sequence order. Its frame
 * list names the exact payload frames in application order; final
 * names the standalone frame or closing marker. Candidate links in
 * each owner's plan run newest first. The source identifies a BASE,
 * checkpoint, PREPARED or FINISHED by file offset. A candidate's
 * covered sequence selects a suffix from the shared command list.
 * Predicates select a verified declaration, matching old binding,
 * matching installed witness or independent literals. A checkpoint
 * remains usable only with a verified or independent suffix. Metadata
 * and token operations in the same plan retain their effective order
 * and ownership; resolve the selected branches before applying them.
 */
/* ELIGIBLE permits evaluation on its selected branch. STAGED marks an
 * installed declaration whose activation belongs to FINISHED. UNRESOLVED
 * preserves a complete operation with inconsistent token, declaration
 * or operand evidence. It cannot authorize replay or saved status.
 */
typedef enum journal_use {
    JOURNAL_ELIGIBLE, JOURNAL_STAGED, JOURNAL_UNRESOLVED
} journal_use;
/* Per-frame use is captured when the frame appears. An unresolved
 * operation can also reflect an embargo added before its COMMIT.
 * declaration names the required generation frame. A nonzero
 * activation also requires the matching FINISHED's verified binding. */
typedef struct journal_frame_ref {
    size_t offset;
    const struct journal_frame_ref *next;
    journal_use use;
    size_t declaration, activation;
} journal_frame_ref;
typedef enum journal_origin_test {
    JOURNAL_VERIFIED_BASE, JOURNAL_EXPECTED_BINDING,
    JOURNAL_INSTALLED_WITNESS, JOURNAL_INDEPENDENT_TEXT
} journal_origin_test;
/* Offsets are absolute; zero means absent. binding and saved name
 * effective metadata evidence, guarded by the decision phase and, for
 * saved, saved_test. A checkpoint descriptor with no HAS_SAVED carries
 * an unknown association. CANCELLED can preserve only an already
 * verified prior association with the same id. FINISHED requires the
 * installed witness and declaration to verify at the path before its
 * saved evidence can supply an id.
 *
 * terminal is set on PREPARED only for a matching accepted terminal.
 * displaced names PREPARED's destination expectation for a conditional
 * peer detach. At an open PREPARED's installed branch or its matching
 * FINISHED, match still-bound peers by path and displaced identity at
 * the decision, leaving their own text, view and saved identity intact.
 * Apply eligible TARGETs in operation order. A checkpoint binding or
 * saved restatement requires comparing its begin_sequence with the
 * most recent effective binding and saved decisions for that owner.
 */
typedef struct journal_operation {
    journal_use use;
    uint64_t owner, final_sequence, begin_sequence;
    size_t final;
    const journal_frame_ref *frames;
    const struct journal_operation *next;
    const struct journal_operation *next_owner_command;
    const struct journal_operation *prepared, *terminal, *checkpoint;
    size_t binding, saved, declaration, displaced;
    journal_origin_test saved_test;
} journal_operation;
/* A PREPARED expected-binding branch delegates to the older origins
 * and their cutoffs. A foreign-binding branch considers independently
 * usable checkpoints and suffixes before reporting unresolved source
 * with literal evidence. A BASE candidate whose source is UNRESOLVED
 * carries that dependency, including when no earlier origin exists.
 * decision names the operation selecting this source, such as a
 * CANCELLED that names a preserved checkpoint. */
typedef struct journal_origin {
    journal_origin_test test;
    const journal_operation *source, *decision;
    uint64_t covered;
    const struct journal_origin *older;
} journal_origin;
typedef struct journal_owner_plan {
    uint64_t owner;
    const journal_origin *origins;
    const journal_operation *commands;
    const struct journal_owner_plan *next;
} journal_owner_plan;
typedef struct journal_logical_plan {
    journal_stream_result prefix;
    uint64_t generation_high_water, buffer_high_water;
    const journal_operation *operations;
    const journal_owner_plan *owners;
} journal_logical_plan;

/* Pass one certifies the prefix and indexes slots, owners, operations,
 * declarations and tokens. Pass two describes origins and shares each
 * owner's ordered command list between candidates. The borrowed
 * image needs zero extra record window bytes (a file-backed reader
 * may use one window up to JOURNAL_MAX_RECORD_BYTES). Each pass
 * visits O(R) frames and O(image bytes) data, apart from index lookup.
 *
 * The planner returns the physical verdict with a plan for the certified prefix.
 * Semantic problems are represented by unsatisfied dependencies on
 * their operations; e.g. a mismatched FINISHED supplies no origin.
 * On preamble refusal or NO_SPACE, restore the workspace mark and
 * clear the output. On success or prefix damage the caller owns the
 * plan until it resets the arena. Every result can be obtained from
 * the same bytes and adequate workspace, without I/O or service state.
 */
journal_codec_err journal_plan_read(const uint8_t *image, size_t length,
                                    base_arena *workspace,
                                    journal_logical_plan *out);

/* Conservative additional arena bytes for the given upper bounds.
 * R includes physical non-PAD frames, B includes owners, D includes
 * distinct declared or claimed keys and T includes distinct tokens.
 * R pays for frame refs, operations, candidate links and the index
 * by final sequence. B pays for one owner entry with two slot heads
 * and its index. D and T pay for entries and their indexes. Tables
 * grow geometrically. Storage is O(R + B + D + T) words, including
 * alignment. Returns 0 if the bound is unrepresentable. */
size_t journal_plan_workspace_size(size_t records, size_t buffers,
                                   size_t keys, size_t tokens);

#endif
