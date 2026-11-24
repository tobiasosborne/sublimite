# Journal wire amendment and runtime header for joint signature

STATUS (advisor, 2026-11-24): lines 1–286 (wire amendment and pure reader contract) SIGNED, with the saved-association rule added at FINISHED. The runtime section (from "Proposed runtime section") is NOT signed: R1–R6 pending.

This replaces the indicated paragraphs and declarations in `src/journal/journal.h` before the first on-disk release. Retain the preamble, field offsets, sizes, endian encoding, CRC coverage, identity witnesses, payload encoders and family/version refusal rules. Keep wire version 1 (G, pre-freeze amendment procedure). The following text belongs in the header. Numbers in names and section citations identify existing contracts. (G) denotes a format or product choice and (P) a consequence of the stated construction. [Signed A28](wave0-plan-amendments.md#a28-the-journal-write-model-advisor-2026-11-24-w1-with-repair-a-docsdecisionswave1-journal-write-modelmd) · [Landed header](../../src/journal/journal.h)

## Wire header text

Replace the position convention in §1, the owner rule in §3 and the type and PAD rules in §4. Use the following contract also at the PAD helper and the record encoder/decoder. Their position argument remains a **stream** offset.

```c
/* The stream begins immediately after the preamble. Sequences count all
 * non-PAD records in physical order, including abandoned members. An image
 * is the complete file belonging to this preamble. A seal is a boundary
 * after which a new append starts on a fresh wire page of that file.
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
 *
 * Only PAD, GENERATION and TABS have owner zero. Every other record,
 * including every marker, names its nonzero buffer in the header. The
 * type word determines role and payload without consulting a slot.
 * Type numbers through TABS retain their payload families (G, landed
 * assignments); 7 and 8 specialize the former generic markers as
 * command markers. Subsequent numbers are successive unused values
 * (G, one value per additional role below). All unlisted words are invalid.
 */
typedef enum journal_record_type {
    JOURNAL_REC_PAD = 0, JOURNAL_REC_GENERATION = 1, JOURNAL_REC_BASE = 2,
    JOURNAL_REC_TARGET = 3, JOURNAL_REC_INSERT = 4, JOURNAL_REC_DELETE = 5,
    JOURNAL_REC_RESTORE_BASE_RANGE = 6, JOURNAL_REC_COMMAND_BEGIN = 7,
    JOURNAL_REC_COMMAND_COMMIT = 8, JOURNAL_REC_CHECKPOINT_BY_VALUE = 9,
    JOURNAL_REC_SAVE = 10, JOURNAL_REC_VIEW = 11, JOURNAL_REC_TABS = 12,
    JOURNAL_REC_COMMAND_INSERT = 13, JOURNAL_REC_COMMAND_DELETE = 14,
    JOURNAL_REC_COMMAND_RESTORE_BASE_RANGE = 15,
    JOURNAL_REC_CHECKPOINT_BEGIN = 16, JOURNAL_REC_CHECKPOINT_COMMIT = 17,
    JOURNAL_REC_CHECKPOINT_INSERT = 18, JOURNAL_REC_CHECKPOINT_TARGET = 19
} journal_record_type;
size_t journal_pad_length(uint64_t stream_offset);
```

Replace the marker, descriptor placement, generation eligibility and SAVE interpretation prose in §§5–6 and the contiguous-group rule in §7. Keep the existing payload layouts, including the checkpoint descriptor's covered sequence and CANCELLED's checkpoint reference. This text also replaces “type must match” in the typed decoder contract.

```c
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
```

Replace the group/prefix rules in §8 and the contract of `journal_stream_validate` at the end of the codec API. Single-record codec calls still borrow caller storage. Whole-image planning has a separate storage contract.

```c
/* The first pass of journal_plan_read certifies the physical prefix.
 * The valid_bytes field of journal_stream_result
 * is an absolute file end, records counts all accepted non-PAD records
 * and last_sequence includes abandoned records. A bad preamble yields
 * no prefix. Stop before the first bad frame, sequence or illegal slot
 * transition. Never skip ahead or retract to BEGIN. At a clean record
 * boundary EOF is OK even with pending slots; those slots have no effect.
 * A partial last record returns TRUNCATED at its start. A complete bad
 * record returns INVALID or SEQUENCE there. Preserve damage intact;
 * only a demonstrably torn last record admits in-place tail repair.
 * Semantic failures such as an unresolved key are plan diagnostics,
 * never instructions to truncate a physically valid image.
 *
 * Planning is a pure two-pass function of immutable image bytes. The
 * logical plan describes conditional origins, whole commands, bindings,
 * views, membership, tokens, allocator high-waters and unresolved
 * dependencies. Its references are file offsets into that same image.
 * Pass one validates the physical prefix. Index per-slot member offsets,
 * complete operations by final sequence, per-use declaration/embargo
 * eligibility, origin and metadata candidates, token and new-key claims.
 * Drop superseded and unclosed lists logically. Collect high-waters from
 * the preamble and every retained mention including TABS entries.
 * Snapshot policy at a command's logical final too: an earlier member
 * cannot smuggle a now-embargoed or then-undeclared range through COMMIT.
 * Record orphan, conflicting and mismatched transaction evidence without
 * activating it. Select candidate origins newest-applicable-first, with
 * old/installed/foreign predicates as required. Physical record order
 * supplies the logical order of final markers without sorting.
 *
 * Pass two uses indexed offsets to describe each candidate's source or
 * exact checkpoint literals, complete ordered suffix and independently
 * effective metadata. Share ordered command lists and refer to a suffix
 * by cutoff; do not expand a suffix once per possible save outcome.
 * Never substitute a checkpoint slice for a command. No path lookup,
 * clock, mutation, repair, piece operation or filesystem input occurs in
 * either pass. Runtime binds predicates to actual observations, verifies
 * only needed sources and applies a command under one rollback boundary.
 * On semantic failure retain evidence and report unresolved text.
 *
 * Checked workspace exhaustion is NO_SPACE and certifies no repair
 * boundary. R physical records require O(R) index words (one offset,
 * list link and bounded descriptors per record). B distinct buffer ids
 * require at most 2B slot heads (P, one per kind); D distinct keys and T
 * tokens add O(D + T) entries. Index paths, TABS payloads and literals by
 * offset and share descriptors. Together this costs O(R + B + D + T)
 * words beyond the input and one record window of at most 1 MiB
 * (G, existing maximum record). A borrowed contiguous image needs no
 * second copy of its N bytes. Exclude recovered trees and output bytes.
 * With indexed tables and member lists the passes inspect O(N) bytes
 * and visit O(R) frames, including indexed rereads; table lookup costs
 * depend on the index. Never multiply these bounds by open groups.
 */
#include "base/base.h"

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
typedef struct journal_frame_ref {
    size_t offset;
    const struct journal_frame_ref *next;
} journal_frame_ref;
/* ELIGIBLE permits evaluation on its selected branch. STAGED marks an
 * installed declaration whose activation belongs to FINISHED. UNRESOLVED
 * preserves a complete operation with inconsistent token, declaration
 * or operand evidence. It cannot authorize replay or saved status.
 */
typedef enum journal_use {
    JOURNAL_ELIGIBLE, JOURNAL_STAGED, JOURNAL_UNRESOLVED
} journal_use;
typedef struct journal_operation {
    journal_use use;
    uint64_t owner, final_sequence;
    size_t final;
    const journal_frame_ref *frames;
    const struct journal_operation *next;
    const struct journal_operation *next_owner_command;
} journal_operation;
typedef enum journal_origin_test {
    JOURNAL_VERIFIED_BASE, JOURNAL_EXPECTED_BINDING,
    JOURNAL_INSTALLED_WITNESS, JOURNAL_INDEPENDENT_TEXT
} journal_origin_test;
typedef struct journal_origin {
    journal_origin_test test;
    const journal_operation *source;
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

/* Replace journal_stream_validate with this entry. plan_read performs
 * the physical scan and indexed logical pass described above. It
 * returns the physical verdict with a plan for the certified prefix.
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
```

## Landed tests affected

The following are source-read findings about particular assertions and generators. [Unit tests](../../tests/journal_codec_test.c) · [Fuzz target](../../fuzz/journal_fuzz.c) · [Corpus](../../fuzz/corpus/journal)

| File and test | Required change |
|---|---|
| `tests/journal_codec_test.c` · `test_pad` | Recompute stream-offset vectors with the preamble included. At stream zero a PAD can be legal. Test both sides of the short-remainder case. |
| `tests/journal_codec_test.c` · `test_checkpoint_save_view_tabs` | Its empty-marker round trips use owner zero and generic marker types. Exercise both typed pairs with their buffer id. The payload round trips remain applicable. |
| `tests/journal_codec_test.c` · `test_stream_valid` | Encode owned command markers and members. Assert absolute `img_len` alignment after PAD. |
| `tests/journal_codec_test.c` · `test_stream_torn_and_invalid` | Replace rewinding to BEGIN, clean EOF classified torn, and the nested-BEGIN rejection. Keep stray/empty COMMIT invalid, locating failure at the offending record. Preserve unrelated completions. |
| `tests/journal_codec_test.c` · `test_stream_checkpoint_and_save` | Update `img_checkpoint`, the empty and incomplete fixtures, typed slices/TARGET and failure offsets. PREPARED + FINISHED without an installed declaration can pass physical framing but cannot finish in the plan. Add a declaration to the positive logical case. |
| `fuzz/journal_fuzz.c` · `LLVMFuzzerTestOneInput` | Update `check_typed` for all roles, and `structured` for the new highest type and marker owners. In `check_prefix` revalidation means physical validity including open slots. Add a separate oracle for committed effects. CRC and byte-round-trip properties survive. |

Regenerate the owned-group seeds `checkpoint-by-value-group` and `torn-tail-group`. The latter ends after a whole DELETE with no COMMIT. Under the amendment its entire physical prefix survives and its command has no effect. Ordinary standalone payload and unknown-type tests still exercise their existing rules.

## Proposed runtime section of journal.h

Place this section after the codec. Runtime callers include the base, piece, OS and shared-work headers used here. Wire decoding and planning remain usable without starting the runtime.

```c
#include "os/os.h"
#include "piece/piece.h"
#include "work/work.h"

typedef struct journal journal;
typedef struct journal_command journal_command;
typedef struct journal_cutoff journal_cutoff;
typedef struct journal_request journal_request;

/* All calls belong to the UI thread. A journal is the writer for one
 * owned, named image. Recover opens or creates that image under the
 * session's ownership. The shared pool and reserve outlive the writer.
 * Opening schedules validation/replay before admission. The UI routes
 * messages through receive and drives progress through service. Closing
 * succeeds only after physical retirement and release of all requests,
 * cutoffs and commands. Diagnostics preserve the native OS failure.
 *
 * OK means this call succeeded. WAIT means service or a dependency must
 * progress. SUPERSEDED identifies a retired checkpoint request. LIMIT
 * covers checked capacity/sequence exhaustion, NOMEM a refused storage
 * claim, SOURCE failed validation, DAMAGED preserved recovery evidence,
 * and IO a storage failure. Accepted asynchronous operations return OK
 * and an owned request; inspect its result to distinguish completion.
 * A refused admission transfers no ownership. Input views are borrowed
 * for the call. An accepted job retains immutable inputs until retired.
 */
typedef enum journal_err {
    JOURNAL_OK, JOURNAL_WAIT, JOURNAL_SUPERSEDED, JOURNAL_LIMIT,
    JOURNAL_NOMEM, JOURNAL_SOURCE, JOURNAL_DAMAGED, JOURNAL_IO
} journal_err;
typedef struct journal_position {
    uint64_t image_epoch, sequence, file_end;
} journal_position;

/* Recovery presents one reconstructed owner at a time. A take transfers
 * its unlogged tree, or NULL for an unresolved source, and its verified
 * current/saved associations. Zero denotes an unknown saved id.
 * When an applied suffix has no recorded current id, assign a fresh
 * nonzero identity outside all retained identities before handing off
 * the recovered tree. The immutable input
 * image and plan retain literal evidence, metadata and open transactions
 * for the session owner, even for unresolved or closed-kept drafts.
 * The visitor runs on the UI after worker validation. Frame payloads
 * and image bytes borrow the recovery result through visitor return;
 * retain needed values. Each control is delivered only on its selected
 * branch, in effective order. The diagnostic receives the preserved
 * name and image bytes on damage or unresolved source.
 */
typedef struct journal_replay_sink {
    void *ctx;
    void (*take)(void *, uint64_t owner, piece_tree *, uint64_t current_id,
                 uint64_t saved_id);
    void (*control)(void *, const journal_frame *);
    void (*evidence)(void *, journal_err, const char *name,
                     const uint8_t *image, size_t size);
} journal_replay_sink;
journal_err journal_recover(journal **out, const char *path, work_pool *pool,
                            piece_reserve *reserve, journal_replay_sink sink);
journal_err journal_close(journal *j);

/* A record argument supplies the known type, owner and encoded payload.
 * The writer assigns sequence, offset and CRC only when it issues the
 * record. append submits one independent control (including GENERATION
 * and SAVE phases) and reports its publication through the returned
 * request. SAVE identifies its reservation by owner/token and the
 * installed declaration by captured new key. Their reserved close
 * credits follow them across retries. A short, zero or uncertain write is an admitted request with
 * retained bytes. Never submit the same logical effect as a fresh append.
 */
typedef struct journal_record {
    journal_record_type type;
    uint64_t owner;
    const uint8_t *payload;
    size_t payload_len;
} journal_record;
journal_err journal_append(journal *j, const journal_record *record,
                           journal_request **out);

/* The journal owns the literal slicing policy and the declaration/restore
 * policy. A caller sizes descriptors with the actual maximum literal
 * byte count returned here. range_allowed tests active, declared, unembargoed
 * eligibility on this image. Source provenance and byte verification are
 * additional caller obligations. A save capture must settle or abort and
 * restage any provisional plan depending on its displaced generation.
 */
size_t journal_literal_limit(const journal *j);
bool journal_range_allowed(const journal *j, uint64_t generation);

/* A command claim counts the exact typed members between its markers,
 * their total framed bytes and the largest framed member. These are
 * separate quantities: sequence credits, byte credits and scratch size.
 * For a singleton all three are zero and final_type names its standalone
 * text type. For a group final_type is the empty COMMAND_COMMIT. The
 * final payload length funds the singleton or is zero for the marker.
 * Begin reserves the final frame, one reusable record window, request,
 * pins against rotation, and funds all bytes and sequences including both
 * markers before issuing BEGIN. It returns the command and admits the
 * claim atomically. A singleton writes nothing at begin. Its exact wire
 * charge is its header plus final payload. A group's charge is the sum
 * of framed member bytes plus both marker headers. Its sequence charge
 * is member count plus both markers. The window is the maximum of one
 * marker, the largest member and the final frame. Neither the typing
 * path nor final retries allocate via libc, block on a worker, or sync.
 * A possibly written BEGIN is admitted with a suspended
 * writer. Other producers cannot spend these credits. They can append at
 * intervening positions. No future physical offsets are booked.
 */
typedef struct journal_member_claim {
    uint64_t count, framed_bytes;
    size_t largest_frame;
} journal_member_claim;
journal_err journal_command_begin(journal *j, uint64_t owner,
                                  journal_member_claim members,
                                  journal_record_type final_type,
                                  size_t final_payload_len,
                                  journal_command **out);
/* payload lends the payload area of the reserved record window. Fill it
 * directly with encoded fields and validated literal bytes. Its writable
 * size is the largest funded frame minus the header. Borrow it only
 * while ready for a new record. For a group, member validates and issues
 * the next typed member. The handle supplies its owner.
 * One successful return consumes one member. On a short write the
 * runtime owns the exact frame and position; consult reconciliation
 * before advancing. The owner can abort before finalization, releasing
 * unused claims and keeping all physically accepted bytes consumed.
 */
uint8_t *journal_command_payload(journal_command *command);
journal_err journal_command_member(journal_command *command,
                                   journal_record_type type,
                                   size_t length);
void journal_command_abort(journal_command *command);
/* Seal validates the singleton's filled final payload in place or checks
 * that the exact grouped member claim has been consumed. It confirms
 * the writer can issue the reserved final. A gap returns WAIT before
 * the caller's RAM transition. After success the caller commits or
 * aborts in this dispatch, with no other writer action in between.
 * All non-I/O final failure conditions have been settled at this cut.
 */
journal_err journal_command_seal(journal_command *command);
/* The caller closes its own provisional mutation after every preceding
 * record has been accepted and before calling final. A conforming final
 * is infallibly admitted. It consumes the command and returns the
 * reserved request even on a failed physical write. Only framing from
 * writer-owned scalars and one single-attempt write remain. There is
 * no allocation, source load, submission or further member to emit.
 * The caller performs its close and calls final in one UI dispatch once
 * the writer is healthy. A final already issued survives reconciliation
 * under its original request. Command publication invokes no caller hook.
 */
journal_request *journal_command_final(journal_command *command);

/* A cutoff pins the current image and the saver's published boundary
 * until its transaction has definitively settled. Capture returns that
 * position, establishes the permanent by-value restore policy for the
 * displaced key (zero means no displacement), and reserves close credits
 * before a temp can start. It returns the unique token and fresh installed
 * generation key from the journal's lineage allocators and the saver's
 * prior origin key from its index. The destination byte length determines the
 * path-dependent SAVE closing charge. Budget includes legal terminal,
 * prerequisite declaration and seals; reserve independent checkpoints at
 * their starts. Issue VALUE_ONLY before any durable dependency, and keep
 * inherited keys of uncertain policy value-only on recovery. Release
 * after definite pre-prepare failure or durable verified settlement.
 */
journal_err journal_capture(journal *j, uint64_t owner, uint64_t displaced_key,
                            size_t destination_bytes, journal_position *position,
                            uint64_t *prior_key, uint64_t *token,
                            uint64_t *new_key, journal_cutoff **out);
void journal_cutoff_release(journal_cutoff *cutoff);

/* A checkpoint rebuilds published text by value. The goal supplies the
 * required minimum covered position (all zero when any independent
 * boundary suffices) and current/saved content ids of the offered
 * published snapshot (zero saved id means unknown). A nonempty minimum
 * belongs to this image and owner. The writer derives the descriptor's
 * actual coverage from the owner's last complete boundary and its length
 * from the snapshot. It captures text, ids and optional TARGET together
 * when BEGIN is accepted. A source
 * hold keeps the validation context alive through every worker read;
 * release may run after supersession. validate checks the pinned backing
 * after dependent loads and before the writer closes the group. Omit
 * the hold callbacks together only for independently owned literal text.
 * The request copies borrowed metadata. Its owner must submit only a
 * published snapshot with this coverage, ids and binding. A newer
 * coverage satisfies an older requested minimum when a complete
 * independent suffix and the required binding/saved decision still hold.
 * Reuse a satisfying producer and give each caller an owned reference.
 * Otherwise fund a new instance and emit fresh BEGIN, retiring stale
 * deliveries by producer identity. Journal compares the binding and
 * saved-decision epochs at completion. The session submits a replacement
 * request when an external observation invalidates an in-flight binding
 * decision before it appears in the journal. Revoke stale delivery
 * authority on that notification even if funding the replacement fails.
 * The old bytes then form an abandoned slot. A refused request leaves
 * a still-valid producer usable for its original callers. Return the
 * admission failure to the new caller without queuing its request.
 * A physical gap must be reconciled before issuing another BEGIN.
 */
typedef struct journal_source_hold {
    void *ctx;
    void (*retain)(void *);
    bool (*validate)(void *);
    void (*release)(void *);
} journal_source_hold;
typedef struct journal_checkpoint_goal {
    journal_position minimum;
    uint64_t current_id, saved_id;
} journal_checkpoint_goal;
journal_err journal_checkpoint_request(journal *j, uint64_t owner,
                                      piece_snapshot *text,
                                      journal_checkpoint_goal goal,
                                      const journal_target *target,
                                      journal_source_hold source,
                                      journal_request **out);

/* A receipt is for one sealed image and the absolute end actually
 * submitted on its held file descriptor. sync requests durability at
 * least through a position returned by the writer. Seal the current
 * prefix using canonical PAD if needed. A worker syncs only the pinned
 * inode; the UI credits at most the submitted watermark, even when later
 * appends share its fd. Result's fd is borrowed from the receipt's pin
 * through request release. Match epoch, inode and submitted end before
 * granting durability. Failed barriers keep the previous durable watermark.
 * New-name durability additionally requires the journal-parent barrier.
 */
typedef struct journal_receipt {
    journal_position sealed;
    os_file pinned_file;
} journal_receipt;
journal_err journal_sync(journal *j, journal_position through,
                         journal_request **out);

/* A request carries its producer identity across reuse of worker slots.
 * ABSENT proves the effect did not reach the image, POSSIBLE requires
 * reconciliation, VISIBLE identifies a complete accepted final, and
 * DURABLE requires the matching barrier. In-flight requests report WAIT.
 * The position is meaningful at VISIBLE or DURABLE. A DURABLE sync has
 * a receipt. native_error retains the service's OS diagnostic. result
 * copies this observation without driving I/O. release drops the
 * caller's reference; physical delivery/retirement owns its own reference.
 */
typedef enum journal_publication {
    JOURNAL_ABSENT_EFFECT, JOURNAL_POSSIBLE, JOURNAL_VISIBLE, JOURNAL_DURABLE
} journal_publication;
typedef struct journal_result {
    journal_err status;
    journal_publication publication;
    journal_position position;
    journal_receipt receipt;
    int native_error;
} journal_result;
journal_result journal_request_result(const journal_request *request);
void journal_request_release(journal_request *request);

/* A gap is the held frame at its intended file offset on the pinned
 * inode. No following record can pass it. state reports the current
 * writer condition, including a pending parent barrier or writeback
 * loss. reconcile drives inspection and UI-owned retry of that exact
 * frame; WAIT retains ownership. On OK the held append is accounted
 * exactly once. An aborted producer's physically completed member
 * stays inert. Other callers observe the original request/result or
 * the command's accepted member count before advancing. After loss
 * suspend ACKs, retain all evidence and require a fresh complete image
 * and its barriers before restoring service. state projects the most
 * urgent condition: writeback loss, uncertain name, incomplete frame,
 * recovery in progress, pending parent barrier, then healthy. Clearing
 * one obligation reveals any remaining obligation. A stale completion
 * never clears a newer failure.
 */
typedef enum journal_condition {
    JOURNAL_RECOVERING, JOURNAL_HEALTHY, JOURNAL_GAP, JOURNAL_WRITEBACK_LOST,
    JOURNAL_NAME_UNCERTAIN, JOURNAL_PARENT_PENDING
} journal_condition;
journal_condition journal_state(const journal *j);
journal_err journal_reconcile(journal *j);
uint64_t journal_command_accepted(const journal_command *command);

/* The shared dispatcher routes immutable messages here, including stale
 * ones requiring retirement. Each turn serves input first, then advances
 * at least one ready record of each producer class in rotation, rotating
 * among ready instances, then ready barriers. This is a progress rule
 * (G, fair service), conditional on storage and workers progressing.
 * service runs the journal's share of that turn. Bounded mailbox credits
 * protect held payloads through physical retirement. Only the UI issues
 * single-attempt record writes or repairs/truncates the named inode.
 */
bool journal_receive(journal *j, const work_msg *msg);
void journal_service(journal *j);

/* rotate requests ordinary compaction. Defer while a cutoff, save token,
 * command, checkpoint or old-inode durability promise is live. Build and
 * sync a private replay-equivalent image from the indexed session. At
 * switch compare source inode/epoch, complete physical watermark and
 * membership/policy versions with the writer's current values. A miss
 * discards or rebuilds the candidate. On exact match the UI alone renames
 * and switches routing in one serialized action. Require journal-parent
 * sync before crediting new durability. Reconciliation of writeback loss
 * while ACKs are suspended is exceptional recovery. A routine save,
 * cancellation, Keep or conflict never requests an image switch.
 */
journal_err journal_rotate(journal *j);
```

The journal test harness acts as a caller. Its hook runs after the last accepted member (or singleton reservation) and before `journal_command_final`. At that cut it can abort, terminate, or complete its simulated RAM transition and issue the funded final. No history type, callback or test-only mode enters the runtime API. [Signed write model §1.8](wave1-journal-write-model.md)
