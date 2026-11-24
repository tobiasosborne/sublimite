/* L1 piece: the editor's text store. This header is the frozen API
 * (v2-0fv.7); the tree behind it is v2-0fv.7b; its acceptance specification is
 * docs/decisions/wave1-piece-pool.md (signed). Rationale and the v1 exports
 * left behind: docs/decisions/v2-0fv.7.md.
 *
 * MODEL. A text is a sequence of bytes, and every byte has a source
 * coordinate: an offset into ORIGINAL, the immutable text its tree was opened
 * from (a private copy, or a borrowed mapped view), or an offset into ADD, the
 * tree's append-only byte store. Every observable of a text is a function of
 * its coordinate sequence and the two stores: its length, its bytes, and its
 * description, the coordinate sequence cut into maximal runs of one store
 * whose offsets rise by one (piece_span). The description is canonical: it
 * does not depend on how the tree stores its runs, so any two
 * implementations of this header produce the same one.
 *
 * ADD COORDINATES. ADD offsets are logical and contiguous. The n bytes that
 * piece_replace or piece_capture appends occupy [a, a + n), with a the
 * piece_add_length before the call, which then becomes a + n. Only
 * piece_checkpoint_abort lowers it, back to its value at begin.
 *
 * OBJECTS AND THREADS. piece_tree: one buffer's live text and the core that
 * owns its storage; every call taking a piece_tree runs on the UI thread.
 * piece_snapshot: an immutable owned revision; any thread may read, retain
 * and release it, and it outlives its tree. piece_view: what the read calls
 * take, either a tree's live view (UI only; it follows every edit) or a
 * snapshot's view (valid while the snapshot is owned). piece_reserve: the
 * process's spare storage, one per process; the UI creates and destroys it
 * and makes every draw from it. Storage comes back on two paths, neither
 * with a lock, so the UI never waits for another thread:
 *  - while a tree is open, the last release of one of its snapshots, on
 *    any thread, pushes that revision's storage onto the core's retirement
 *    queue (one compare-and-swap); the UI drains it (piece_reclaim);
 *  - once a tree is closed, the thread that drops its core's last owner
 *    walks the core's storage itself, calls the backing's release there,
 *    and pushes the core's extents onto the reserve's inbox as one chain
 *    (one compare-and-swap); the UI takes the whole inbox with one exchange
 *    before it draws.
 *
 * ERRORS (A15). Preconditions are ASSERTed (os.h): only a caller bug breaks
 * them. Results are returned only for what input or exhaustion produces:
 *   PIECE_ERR_NOMEM  base refused storage the call needs (see STORAGE).
 *   PIECE_ERR_RANGE  the result needs more than PIECE_MAX_HEIGHT branch
 *                    levels. Reopening the saved text repacks it (the open
 *                    builder packs).
 * A call that returns an error has changed nothing observable: text,
 * description, ADD length, checkpoint, snapshot cache and every snapshot
 * are as they were.
 *
 * SIZES. A text is at most PIECE_MAX_LENGTH = 2^60 bytes, an invariant
 * every open and edit ASSERTs of its result. Why it holds (P): every
 * descriptor lies inside one PIECE_CHUNK-aligned chunk of its store, so a
 * text of length L needs at least L / 65,536 descriptors of at least 16 B
 * (the signed leaf entry); with 4-level paging the 2^47-byte address space
 * keeps L below 2^47 / 16 x 2^16 = 2^59. 5-level paging would allow about
 * 2^56 / 16 x 2^16 = 2^68, so there the ASSERT is what holds it. Every
 * newline sum is then at most 2^60 too (Lines: one newline per byte). ADD's bytes are all stored, so
 * its length stays below the address space, far under bit 63.
 *
 * STORAGE. Tree nodes, snapshot headers and checkpoint restore points occupy
 * interchangeable 400 B slots (P, spec "Choosing the slot") in whole page
 * extents, each owned by one tree's core; text occupies ADD chunks and the
 * private ORIGINAL copy. All of it comes from src/base, which reserves
 * through os_vm_reserve; nothing here calls malloc. Admission is a function,
 * not a state: a call that can need storage computes its own claim from the
 * tree's current shape before it changes anything (A26): an ordinary command
 * whose scalar claim and companion storage are already local proceeds;
 * otherwise its exact need is planned, then funded from its core's free
 * slots and the reserve's whole free extents, then, once, from its tree's
 * retirement queue and the reserve's inbox, then from base for exactly the
 * remainder, after returning whole free extents beyond the exact slot need
 * (erratum, A26 step 4 as amended): a fixed list of base requests taken from
 * that single plan, the missing extents and then the companion objects (ADD
 * chunks and their directory nodes, or the private boundary's chunk and
 * directory nodes), each once and in that order, all before anything is
 * published, at most ceil(S / e) + 9 x max(k, 1) of them (P: S the planned
 * slots; e the slots per extent, (page - sizeof(piece_extent)) / 400 B,
 * which is 10 at a 4,096 B page; k the new ADD chunks. ADD offsets are below
 * 2^63 by the ASSERT in piece_add_prepare, so chunk indices are below
 * 2^63 / 2^16 = 2^47 and the 64-way ADD directory has at most
 * ceil(47 / 6) = 8 levels: a new chunk costs 1 + 8 objects, and so does a
 * take's private boundary). A refusal returns every object the call took to base
 * and the call returns PIECE_ERR_NOMEM with the tree, and base apart from
 * what those steps returned on purpose, as before. Exactness is promised at
 * quiescence: with no release by
 * another thread in flight, a call returns PIECE_ERR_NOMEM only when base
 * refused the exact remainder after every local source was used. While
 * another thread is releasing storage, a call may be refused that an
 * immediate retry would not be; nothing latches, the next call tries afresh.
 * A caller asks nothing before an edit; a compound command runs under a
 * checkpoint, whose abort cannot fail. After each submitted command the UI
 * calls piece_replenish, outside the input-to-submit window, so the next
 * ordinary command finds its claim funded and calls base only for new ADD
 * chunks. */
#ifndef PIECE_PIECE_H
#define PIECE_PIECE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "os/os.h"

typedef enum piece_err {
    PIECE_OK = 0,
    PIECE_ERR_NOMEM,
    PIECE_ERR_RANGE
} piece_err;

/* (G) Most branch levels above the leaves: the last depth a 32-entry,
 * leaf-inclusive cursor stack indexes (spec "Height admission"). An open
 * never reaches it: the packed builder needs at most 15 levels for any
 * length (P, 24 x 16^14 < UINT64_MAX <= 24 x 16^15). */
#define PIECE_MAX_HEIGHT 31u

typedef struct piece_reserve piece_reserve;
typedef struct piece_tree piece_tree;
typedef struct piece_snapshot piece_snapshot;
typedef struct piece_view piece_view;

/* ---- Coordinates -------------------------------------------------------- */

/* (G, the spec's C = CH_SIZE = 2^16) The chunk. Chunk k of a store is its
 * bytes [k x PIECE_CHUNK, (k + 1) x PIECE_CHUNK); no descriptor crosses a
 * chunk boundary. ADD fills its chunks in order; a mapped ORIGINAL has
 * ceil(len / PIECE_CHUNK) chunks, the last possibly short. */
#define PIECE_CHUNK 65536u

/* A run of the description: len >= 1 bytes from coordinate src on. Bit 63 of
 * src selects ADD; the other bits are the offset in that store (below 2^47,
 * SIZES). 16 B (P), the inverse payload unit of the spec. */
typedef struct piece_span {
    uint64_t src;
    uint64_t len;
} piece_span;

#define PIECE_SPAN_ADD (UINT64_C(1) << 63)

/* (G) The largest text (SIZES). */
#define PIECE_MAX_LENGTH (UINT64_C(1) << 60)

/* Newline counts carried beside a span: an optional parallel array that
 * only undo keeps. It never reaches disk; piece_span alone is the
 * description's and the journal's unit. For a span [a, b) of one store, i =
 * floor(a / PIECE_CHUNK) and j = floor((b - 1) / PIECE_CHUNK) are its first
 * and last occupied chunks. When i == j, head counts the whole span and tail
 * is 0. Otherwise head counts its part in chunk i, tail its part in chunk j
 * (a chunk ends at its store's logical end), and whole chunks i + 1 .. j - 1
 * count from the store's chunk totals. Each field is at most PIECE_CHUNK, or
 * PIECE_NL_UNKNOWN: a part is described with a count only when all of it is
 * counted, so a part that mixes counted and unknown text is unknown (Lines,
 * CARRIED EXACTNESS). 8 B (P): an in-memory undo inverse entry is 24 B, the
 * figure undo admits per span; the literal threshold stays 16 x spans >
 * length (A13's copy cap and the journal's choice).
 * Provenance, the precondition of piece_replace_spans: a record is valid only
 * for the exact span it was described with by piece_iter_next_span on a view
 * of the same tree, or for the union produced by piece_span_nl_join from
 * records with this provenance. A caller that clips a span describes it
 * again with end. ORIGINAL must be in the same validated source generation.
 * ADD provenance follows byte lineage and ends when an abort lowers ADD below
 * the span's end, also when described after the abort from a snapshot that
 * survived it. A true count stays true while the backing is trusted: it
 * belongs to its coordinates. */
typedef struct piece_span_nl {
    uint32_t head;
    uint32_t tail;
} piece_span_nl;

#define PIECE_NL_UNKNOWN UINT32_MAX

/* Joins the records for two source-adjacent spans, in source order. The
 * result describes {left.src, left.len + right.len}. Both records must have
 * the provenance above for the same tree and source generation. Only the
 * union's head and tail are stored. Their contributions are added within
 * the same chunk, with an unknown contribution making that edge unknown.
 * The piece store supplies whole interior chunk counts. Pure and O(1).
 * ASSERT: both spans are nonempty, adjacent and in the same store. */
piece_span_nl piece_span_nl_join(piece_span left, piece_span_nl left_nl,
                                 piece_span right, piece_span_nl right_nl);

/* A mapped ORIGINAL: the view's bytes, its fault registration and the
 * lifetime of the mapping that owns both (os.h, mapped-file fault service).
 * acquire adds one owner and release drops one; either may run on any
 * thread, and the last release retires the fault id and unmaps. The bytes
 * stay mapped while any owner remains. After a fault they read zero from the
 * faulting page on: a reader that must trust mapped bytes polls `fault`
 * once with os_fault_poll after the last load its result depends on and
 * requires epoch 0 (one poll suffices: a registration's epoch never
 * decreases, so epoch 0 at the closing poll means epoch 0 at every earlier
 * load; a retire and re-registration fails the generation check). */
typedef struct piece_backing {
    const uint8_t *bytes;
    size_t len;
    os_fault_id fault;
    void *ctx;
    void (*acquire)(void *ctx);
    void (*release)(void *ctx);
} piece_backing;

/* The owner checks the live identity of this mapped backing. The check
 * returns true on a verified match. It must leave the tree and its
 * owners untouched and complete without waiting. The owner keeps ctx
 * alive for the borrowing log or request, including retired work. */
typedef bool (*piece_validate_fn)(void *ctx, const piece_backing *backing);

/* ---- Reserve and trees -------------------------------------------------- */

/* The UI-owned process reserve: whole free page extents that any core may
 * draw, and the spare private ADD boundary chunk (spec "Extent ownership").
 * Create allocates its descriptor from base: PIECE_ERR_NOMEM.
 * ASSERT: out != NULL. */
piece_err piece_reserve_create(piece_reserve **out);
/* Returns every extent to base. ASSERT: every tree opened on r is closed and
 * every snapshot of them released. */
void piece_reserve_destroy(piece_reserve *r);

/* Opens a tree over a private copy of bytes[0, n): ORIGINAL = those bytes,
 * ADD empty, text = ORIGINAL. The caller may free bytes on return. Packed
 * build; O(n) to copy and count (Lines). PIECE_ERR_NOMEM.
 * ASSERT: r, out != NULL; bytes != NULL when n > 0; n <= PIECE_MAX_LENGTH. */
piece_err piece_open_copy(piece_reserve *r, const uint8_t *bytes, size_t n,
                          piece_tree **out);
/* Opens a tree over b's view without copying or reading it: ORIGINAL =
 * b->bytes[0, b->len). The core copies *b and acquires one owner, which it
 * releases when its last owner (tree or snapshot) dies, on that thread; the
 * caller keeps its own. Packed build; O(b->len / PIECE_CHUNK) descriptors,
 * one per chunk, plus the chunk count table (Lines); no byte read.
 * PIECE_ERR_NOMEM.
 * ASSERT: r, b, out, b->bytes, acquire, release != NULL; b->fault is an
 * issued id; 0 < b->len <= PIECE_MAX_LENGTH. */
piece_err piece_open_mapped(piece_reserve *r, const piece_backing *b,
                            piece_tree **out);
/* Ends the tree: its live view and every iterator over it die, and its
 * retirement queue drains. Snapshots live on; each owns the text it shows.
 * When the last of them is released the core's extents return to the
 * reserve on the releasing thread. ASSERT: no checkpoint is open. */
void piece_close(piece_tree *t);

/* The tree's live view: the current text, UI only. The pointer is fixed
 * until close; what it shows follows every edit. */
const piece_view *piece_tree_view(const piece_tree *t);
/* Length of the tree's ADD store, the coordinate the next appended byte
 * takes (see ADD COORDINATES). O(1). */
uint64_t piece_add_length(const piece_tree *t);

/* ---- Reading ------------------------------------------------------------ */

/* Every read is total on its view: no allocation, no error, no write to any
 * shared state, so readers on several threads never contend. Reading mapped
 * bytes may fault them in; piece_iter_next_span with nl == NULL never reads
 * a byte. */

uint64_t piece_length(const piece_view *v); /* O(1) */
/* O(h). ASSERT: off < piece_length(v). */
uint8_t piece_byte_at(const piece_view *v, uint64_t off);
/* The mapped ORIGINAL's backing, or NULL when ORIGINAL is a private copy.
 * The pointer is valid while v is; a reader that needs the mapping beyond
 * that copies the struct and calls acquire. O(1). */
const piece_backing *piece_view_backing(const piece_view *v);

/* Caller-allocated cursor; the words are private. Size (P): view, position
 * and revision (3 words), one node pointer per level, leaves included
 * (PIECE_MAX_HEIGHT + 1 words), one child index byte per level (4 words),
 * and the current run's pointer and remaining length (2 words). */
#define PIECE_ITER_WORDS (3u + (PIECE_MAX_HEIGHT + 1u) + (PIECE_MAX_HEIGHT + 1u) / 8u + 2u)
typedef struct piece_iter {
    uint64_t priv[PIECE_ITER_WORDS];
} piece_iter;

/* Binds it to v at off. An iterator over a live view reads the revision
 * current at its last init or seek: an edit of the tree ends that revision,
 * and next or next_span before the following seek is an ASSERT. An iterator
 * over a snapshot view lives while the snapshot is owned. O(h).
 * ASSERT: off <= piece_length(v). */
void piece_iter_init(piece_iter *it, const piece_view *v, uint64_t off);
/* Repositions on the view's current revision. O(h).
 * ASSERT: off <= piece_length(view). */
void piece_iter_seek(piece_iter *it, uint64_t off);
/* Points *p at the next run of bytes and returns its length n >= 1, or
 * returns 0 at the end. Runs concatenate to the text from the position on;
 * each lies inside one span of the description and may have any length.
 * The bytes live as long as the iterator's revision. Amortised O(1) per run.
 * This backs indent_text (src/indent/indent.h) with two one-line adapters:
 *   static void seek(void *it, uint64_t off) { piece_iter_seek(it, off); }
 *   static size_t next(void *it, const uint8_t **p) { return piece_iter_next(it, p); }
 * with iter = an initialised piece_iter and length = piece_length(view). */
size_t piece_iter_next(piece_iter *it, const uint8_t **p);
/* Stores in *out the part before end of the description's span that holds
 * the position, and steps past it; returns false when the position is at
 * end or at the text's end. Successive spans are never mergeable, except
 * that the last one is cut at end. With nl == NULL it reads no text byte,
 * so describing a mapped range faults nothing in; with nl it also stores
 * the span's counts (piece_span_nl), reading only where a span end falls
 * inside a counted descriptor (Lines). O(descriptors the span covers).
 * Describing [off, off + len) is init at off, then this loop with end =
 * off + len; counting its spans is how undo chooses a literal or spans.
 * ASSERT: out != NULL. */
bool piece_iter_next_span(piece_iter *it, uint64_t end, piece_span *out,
                          piece_span_nl *nl);

/* ---- Editing ------------------------------------------------------------ */

/* Every edit is atomic: it returns PIECE_OK with the edit made, or an error
 * with nothing changed. The empty edit (del == 0 and nothing inserted)
 * changes nothing, claims nothing and returns PIECE_OK. Any other successful
 * replace ends the live view's revision (live iterators need a seek) and
 * invalidates the snapshot cache; snapshots are unaffected. An edit reads
 * mapped text only to count the newlines of what it creates (P): none of its
 * inserted source, and at most 2 x 32,768 = 65,536 B for the at most two
 * destination descriptors it cuts (the shorter half of each counted one; an
 * unknown one is not read). A replace splices its insertion at the seam its
 * deletion made before it merges neighbours, so it never cuts again what it
 * has just joined (that order would read 3 x 32,768 B). h is the tree's
 * current number of branch levels. Slot claims are the spec's scalar table
 * (P). */

/* Replaces [off, off + del) with a copy of bytes[0, n). The bytes are
 * appended to ADD at [a, a + n), a = piece_add_length(t), and the text
 * becomes text[0, off) . ADD[a, a + n) . text[off + del, length). del == 0
 * inserts, n == 0 deletes. bytes must not point into this tree's storage
 * (not checkable: ADD is not one range). No caller needs that: Enter's
 * indentation run (indent.h) goes in by reference with piece_replace_spans,
 * or from a copy.
 * Claim: at most 4h + 3 slots for the deletion; for the insertion 2h' + 3,
 * h' = min(PIECE_MAX_HEIGHT, h + 1), when it stays in one ADD chunk, else
 * the exact contiguous batch claim; ADD chunks for the bytes.
 * Cost: O(h) per boundary, O(n) to copy and count, O(n / PIECE_CHUNK) batch
 * steps, plus releasing the removed nodes.
 * PIECE_ERR_NOMEM; PIECE_ERR_RANGE.
 * ASSERT: off <= length, del <= length - off, bytes != NULL when n > 0,
 * length - del + n <= PIECE_MAX_LENGTH. */
piece_err piece_replace(piece_tree *t, uint64_t off, uint64_t del,
                        const uint8_t *bytes, size_t n);

/* Replaces [off, off + del) with the bytes that spans[0, count) name, by
 * reference: their coordinates are inserted as given, no byte of the
 * inserted source is copied, and no mapped byte of it is read; a private
 * source (ADD, a private-copy ORIGINAL) may be scanned within the bounds
 * stated under Lines, at most 511 B per span end (P, the end's partial
 * 512 B prefix block), to count it (errata at 7b, advisor 2026-11-23 and A24:
 * the guarantee was always about mapped bytes, which can fault and block on
 * storage; private memory can do neither). Its destination cuts read as the Editing preamble states. nl is NULL or count records carried with the spans
 * (piece_span_nl; its provenance rule is a precondition of this call). A
 * counted part goes in counted; a whole chunk whose count the core's table
 * holds is counted from the table whatever the record says; private storage
 * (ADD, a private-copy ORIGINAL) is always counted; any other mapped part,
 * every one when nl is NULL, goes in unknown and is eligible for
 * piece_lines_resolve at once (Lines). The undo and redo primitive; journal
 * replay passes NULL. Debug check of the provenance precondition: in the
 * sanitizer lanes only (OS_SAN_ASSERT, os.h), each known head and tail is
 * recounted from the bytes it names and compared, unless a poll shows the
 * backing faulted; the shipped build reads nothing. A span is valid for t
 * when it lies
 * inside ORIGINAL, or inside ADD below piece_add_length(t) and came from t
 * (described from t's live view or a snapshot of t, returned by
 * piece_capture, or the coordinates of a piece_replace) with no checkpoint
 * abort since that lowered the ADD length below its end. The bounds half of
 * that is ASSERTed; the provenance half is not checkable: an ADD span from an
 * aborted checkpoint that lies below the regrown ADD length names other
 * bytes, and passing it is a caller bug.
 * Claim: the exact contiguous batch claim over the spans' descriptors (spec
 * "Bulk insertion"), plus at most 4h + 3 slots for the deletion. No ADD.
 * Cost: O(h) per boundary plus O(descriptors inserted).
 * PIECE_ERR_NOMEM; PIECE_ERR_RANGE.
 * ASSERT: off <= length, del <= length - off, spans != NULL when count > 0,
 * every span has len >= 1 and lies inside its store, and length - del plus
 * the spans' lengths is at most PIECE_MAX_LENGTH (checked span by span against
 * the remaining capacity, so no sum wraps). */
piece_err piece_replace_spans(piece_tree *t, uint64_t off, uint64_t del,
                              const piece_span *spans, const piece_span_nl *nl,
                              size_t count);

/* Appends a copy of text[off, off + len) to ADD and stores its coordinates in
 * *out, {PIECE_SPAN_ADD | a, len}; the text is unchanged, so neither the
 * cache nor live iterators are affected. How an undo literal is captured:
 * once, into immutable storage, reinserted later with piece_replace_spans.
 * Reads the len bytes; for mapped bytes, validate with the backing's fault id
 * around the call. Claim: ADD chunks for len bytes, no slots. Cost O(h + len).
 * PIECE_ERR_NOMEM.
 * ASSERT: len >= 1, off + len <= length, out != NULL. */
piece_err piece_capture(piece_tree *t, uint64_t off, uint64_t len,
                        piece_span *out);

/* ---- Snapshots ---------------------------------------------------------- */

/* Takes an owned, immutable snapshot of the current text and stores it in
 * *out. With no successful non-empty replace and no abort since the previous
 * take, this is a cache hit: *out is that same snapshot with one more owner, and
 * the take claims nothing and returns PIECE_OK. Otherwise it claims one slot
 * for the header (P); inside a checkpoint, the first take that can observe
 * text appended past an unaligned ADD mark also installs one private ADD
 * boundary chunk (spec "Private ADD boundary"). O(1); the tree shares its
 * nodes with the snapshot and copies none. PIECE_ERR_NOMEM, with *out NULL.
 * ASSERT: out != NULL. */
piece_err piece_snapshot_take(piece_tree *t, piece_snapshot **out);
/* Adds an owner and returns s. Any thread, O(1), claims nothing.
 * ASSERT: s is owned, and it has fewer than 2^64 - 1 owners, a bound no
 * program reaches: every owner occupies storage of its own, so 2^64 of them
 * cannot exist in a 2^47- or 2^56-byte address space (P). The same bound
 * holds a core's owners, its tree and one per live snapshot header. */
piece_snapshot *piece_snapshot_retain(piece_snapshot *s);
/* Drops one owner, exactly once per owner. Any thread, O(1). The last
 * release of an open tree's snapshot queues its exclusive storage for
 * piece_reclaim; after close it returns that storage itself. */
void piece_snapshot_release(piece_snapshot *s);
/* The snapshot's view, valid while s is owned. O(1). */
const piece_view *piece_snapshot_view(const piece_snapshot *s);

/* ---- Checkpoint --------------------------------------------------------- */

/* One atomic group of edits per tree, at most one open (spec "Private ADD
 * boundary"; PLAN §4.2). Begin records a restore point: the current text,
 * description and ADD length. Snapshots and spans obtained before begin stay
 * valid across commit and abort.
 * Begin claims one slot on a snapshot-cache miss, none on a hit; O(1): the
 * ADD directory is shared, not copied (A7). PIECE_ERR_NOMEM.
 * ASSERT: no checkpoint is open. */
piece_err piece_checkpoint_begin(piece_tree *t);
/* Keeps the text and closes the checkpoint. Allocates nothing; cannot fail.
 * ASSERT: a checkpoint is open. */
void piece_checkpoint_commit(piece_tree *t);
/* Restores the text, description and ADD length recorded at begin and closes
 * the checkpoint. Allocates nothing and cannot fail, even if every base
 * request since begin was refused. Snapshots taken inside keep their own
 * text; ADD spans obtained inside that end above the restored ADD length
 * become invalid. Invalidates the snapshot cache and ends the live view's
 * revision. O(1). ASSERT: a checkpoint is open. */
void piece_checkpoint_abort(piece_tree *t);

/* ---- Maintenance (UI, outside the input-to-submit window) --------------- */

/* Post-submit top-up (spec "Synchronous admission"). Brings what t's next
 * command can draw without base to its target, using returned storage
 * before base: W(h) free slots in t's core and the reserve's whole extents
 * together, W(h) = 1 + (4h + 3) + (2 min(PIECE_MAX_HEIGHT, h + 1) + 3) + 1
 * (P: checkpoint begin, a deletion, a one-chunk insertion, a take;
 * W(0) = 10, W(31) = 194), and one spare private ADD boundary chunk in the
 * reserve. Then returns whole free extents above that target to base. Call
 * after every submitted command, after open, and on switching the active
 * tree. PIECE_ERR_NOMEM when base refused: nothing else changed, and no
 * command is refused because of it, since every command admits itself. */
piece_err piece_replenish(piece_tree *t);
/* Physical retirement. A snapshot whose last owner let go while t is open
 * leaves its exclusive storage queued on t. Performs at most budget steps,
 * each dropping one reference to a slot or chunk, and returns whole empty
 * extents to the reserve; true while queued work remains. budget 0 only
 * asks. Allocates nothing. For edits, takes and their companion storage it
 * is latency only: their admission drains the queue itself before it
 * reports PIECE_ERR_NOMEM, and close drains the rest. piece_lines_resolve
 * never drains it (A24): the post-submit order is piece_reclaim,
 * piece_lines_resolve, piece_replenish. */
bool piece_reclaim(piece_tree *t, size_t budget);

/* ---- Lines (frozen at the advisor's read, 2026-11-23) ------------------- */

/* A line break is '\n'. Lines are numbered from 0: a text with k newlines
 * has k + 1 lines, line i > 0 starts one past the i-th newline, and a text
 * ending in '\n' ends with an empty line that starts at its length.
 *
 * ANNOTATION. Every leaf entry and branch entry carries (bytes, newlines,
 * unknown); a branch entry sums its subtree's counted newlines and ORs its
 * unknown flag. A sum is at most the text's length, so at most
 * PIECE_MAX_LENGTH = 2^60 (SIZES; one newline per byte at most), and bit 63
 * of a branch sum is free for the flag by construction; a leaf count is at
 * most PIECE_CHUNK, so UINT32_MAX is free as the leaf's sentinel.
 *
 * KNOWLEDGE. A newline count belongs to source coordinates. Private storage
 * (ADD, and a private-copy ORIGINAL, which keeps the chunk totals it counted
 * while copying) is always counted; only mapped ORIGINAL can be unknown. What
 * an edit creates is counted then:
 *  - bytes piece_replace or piece_capture copies, while copying them;
 *  - a span inserted by reference: known carried counts; whole chunks from
 *    the core's chunk table, which overrides an UNKNOWN record; in private
 *    storage the store's chunk totals and 512 B block prefix counts,
 *    scanning at most 511 B per end (P, one partial block); any other
 *    mapped part goes in unknown;
 *  - the halves of a cut: a counted mapped descriptor by scanning its
 *    shorter half (at most PIECE_CHUNK / 2 = 32,768 B (P)) and subtracting;
 *    a private one by counting its left part from the block prefixes, at
 *    most the partial block at each end of that part, 2 x 511 = 1,022 B (P;
 *    no shorter-side choice: private memory cannot fault or block, A24);
 *    the halves of an unknown descriptor stay unknown and nothing is read.
 * An unknown entry becomes counted only through piece_lines_resolve once its
 * chunk's table entry is present: a whole chunk from the table, a partial one
 * by a UI scan of at most PIECE_CHUNK. Every counted mapped entry descends
 * from a table entry, so an unknown piece of a chunk that also holds counted
 * text is eligible at once. Chunk k of a mapped ORIGINAL is
 * [k x PIECE_CHUNK, min((k + 1) x PIECE_CHUNK, len)); the last, possibly
 * short, is whole for open, adoption and replay, and any subinterval of a
 * chunk is a slice.
 *
 * CARRIED EXACTNESS. An edit, undo or redo keeps exact every piece whose
 * record was counted when it was described. A record's head or tail that
 * mixes counted and unknown text (a counted slice and a source-contiguous
 * neighbour inserted without counts, joined into one span) is described
 * unknown, goes back in unknown, and is eligible for resolve at once.
 *
 * READS OF MAPPED TEXT (P), counted per call, query reads apart from edit
 * reads. An edit: none of its inserted source, at most 65,536 B of
 * destination cuts (Editing). piece_iter_next_span: none with nl == NULL;
 * with nl, at most 32,768 B per span end inside a counted descriptor. A
 * one-span delete, its description with counts and its undo read at most
 * 4 x 32,768 = 131,072 B together (A13): description and deletion each scan
 * the k <= 2 interior endpoint cuts once; with k = 2 the survivors' sources
 * have a gap, so they cannot have merged and the undo cuts nothing; with
 * k <= 1 an undo that splits a merged seam reads at most 32,768 B more. A
 * query reads at most PIECE_CHUNK; piece_lines_resolve at most PIECE_CHUNK
 * per partial entry.
 *
 * FAULTS. Every scan of mapped bytes (a cut, a resolve scan, a scanning
 * query) is followed by one poll of the backing's fault id, after every load
 * the result depends on (the poll's fence orders those loads before its
 * epoch load); the result is used only when it reports epoch 0. One poll
 * suffices because a registration's epoch never decreases: epoch 0 then was
 * epoch 0 at every earlier load, so no recovery had run and every byte read
 * was file-backed; a retire and re-registration in between fails the
 * generation check, closed. An implementation may also poll first as an
 * early-out; that is not part of this contract. A query that reads no mapped
 * byte still polls once. A nonzero epoch
 * or a failed poll makes a query answer inexact with lower bound 0 (line 0
 * excepted) and leaves a cut's or a resolve's entry unknown. A batch is
 * revalidated at adoption (source identity and epoch) and discarded if
 * tainted. Exactness is promised for a trusted backing only: L0 repairs
 * addressability after a fault, the file owner reloads onto a fresh backing,
 * and an unsignalled overwrite falls under the file owner's validation.
 *
 * GUARANTEES (the suite's contract; which answers are exact depends on the
 * hidden shape, like the height limit): soundness, an exact answer is the
 * true count; private-copy trees are always exact; carried exactness as
 * above; completeness, once every chunk of a trusted backing is adopted and
 * piece_lines_resolve reports no work left, every query on the live view,
 * and on snapshots taken after that, is exact. A snapshot's answers do not
 * change while its backing is trusted. */

/* Rank: newlines in [0, off). An offset on an entry boundary excludes the
 * entry that follows, even an unknown one; an offset inside an entry scans
 * that entry's part before off, at most PIECE_CHUNK bytes, then polls once
 * (FAULTS); a rank that scans nothing polls once. True when *count is exact:
 * no unknown entry lies wholly before off and every poll reports epoch 0.
 * Otherwise false, and *count is a lower bound: the counted part, or 0 when
 * a poll reports a nonzero epoch or fails. O(h).
 * ASSERT: off <= piece_length(v), count != NULL. */
bool piece_lines_before(const piece_view *v, uint64_t off, uint64_t *count);

/* Select: the start of line `line` in *off, and in *before the newlines in
 * [0, *off). Past the last line, *off = piece_length(v) and *before is the
 * total. Descends through an unknown-marked ancestor's exact leading
 * children; locating the newline inside its entry is one pass of at most
 * PIECE_CHUNK bytes (P), followed by the FAULTS poll. True when *off is exact.
 * False when an unknown entry before the line's start blocks the search,
 * which then reads no further unknown entry: *off is that entry's start,
 * *before the exact count before it (less than line), and the line starts
 * after *off. When a poll reports a nonzero epoch or fails: false with
 * *off = *before = 0; line 0 is always (0, 0, true) and reads nothing.
 * O(h). ASSERT: off, before != NULL. */
bool piece_lines_start(const piece_view *v, uint64_t line, uint64_t *off,
                       uint64_t *before);

/* Off-path counter. Counts the newlines of chunks [first, first + n) of b
 * into counts[0, n), then polls b->fault once (FAULTS); false, counts
 * discarded, unless it reports epoch 0. Any thread while the caller
 * owns a backing reference; touches no tree, allocates nothing, reads at
 * most n x PIECE_CHUNK bytes. ASSERT: b, counts != NULL;
 * first + n <= ceil(b->len / PIECE_CHUNK). */
bool piece_lines_count(const piece_backing *b, uint64_t first,
                       uint32_t *counts, size_t n);

/* Revalidates a batch that piece_lines_count produced on the backing whose
 * fault id is source, then stores it in t's core chunk table (4 B per chunk
 * (P), allocated by piece_open_mapped: 65,536 B for 1 GiB). Returns false and
 * stores nothing when the batch is tainted: source is not t's backing (a
 * batch for a closed or reloaded buffer), a poll of it reports a nonzero
 * epoch or fails, or a count disagrees with one the table holds (the source
 * changed; the file owner's validation decides). A batch that adds a chunk
 * invalidates the resolve cursor; an identical re-adoption changes nothing.
 * Rewrites no entry. UI only, O(n), allocates nothing.
 * ASSERT: ORIGINAL is mapped; first + n <= its chunk count; each count is at
 * most its chunk's length. */
bool piece_lines_adopt(piece_tree *t, os_fault_id source, uint64_t first,
                       const uint32_t *counts, size_t n);

/* The resolve sweep: walks the live tree in document order from its cursor
 * and rewrites each unknown entry the chunk table can count, copying each
 * shared path before it publishes, so snapshots keep their answers, inexact
 * ones included. Ownership is decided on the visited path: an entry whose
 * leaf and ancestors the live tree alone owns is rewritten in place and
 * needs no slot. One call descends into at most budget nodes (ancestors
 * walked for a copy and misses included); each branch it copies also updates
 * the reference counts of that branch's children, at most 16 per copy (P,
 * the branch fanout), so a call touches at most 17 x budget nodes, whatever
 * the tree holds. It funds its copies from the core's free slots, the
 * reserve and base, and never walks the retirement queue (A24; piece_reclaim
 * frees that, run it first). A tree of unknown entries whose chunks are not
 * yet adopted therefore costs a bounded amount per call and nothing on the
 * typing path. The copies cost up to one
 * copy of every node, e.g. 730 x 400 = 292,000 B for a packed 1 GiB tree
 * whose old root a snapshot pins (P, 683 leaves + 47 branches). When a copy
 * cannot be funded the call stops there: PIECE_OK if it rewrote something
 * before (published, the cursor before the first entry it could not
 * publish), else PIECE_ERR_NOMEM, which changes nothing, cursor included,
 * and can be retried; run it before piece_replenish, whose top-up it would
 * otherwise spend. On PIECE_OK the cursor moves past the entries it
 * rewrote, now published, and past the ineligible ones it visited. Any successful live
 * edit, any abort, and an adoption that adds a chunk invalidate the cursor,
 * so no eligible entry is skipped forever; an identical re-adoption does not.
 * *more is false when no eligible work remains for the current root and
 * table (a later batch can make more eligible). Liveness: with root and
 * table unchanged and admission succeeding, a call with budget >= h + 1 (one
 * root-to-leaf walk) reports *more false or moves the cursor forward. A call
 * that replaces a node by a copy (the root, or a shared node under an
 * exclusive root) invalidates the snapshot cache and ends the live view's
 * revision (live iterators need a seek; the text is unchanged); a rewrite
 * in place ends neither. An
 * abort may restore an unknown root: the table stays, and the invalidated
 * cursor makes that root eligible without allocating. UI only, outside the
 * input-to-submit window. ASSERT: more != NULL. */
piece_err piece_lines_resolve(piece_tree *t, size_t budget, bool *more);

#endif
