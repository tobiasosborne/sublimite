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
 * newline sum is then below 2^60 too (Lines). ADD's bytes are all stored, so
 * its length stays below the address space, far under bit 63.
 *
 * STORAGE. Tree nodes, snapshot headers and checkpoint restore points occupy
 * interchangeable 400 B slots (P, spec "Choosing the slot") in whole page
 * extents, each owned by one tree's core; text occupies ADD chunks and the
 * private ORIGINAL copy. All of it comes from src/base, which reserves
 * through os_vm_reserve; nothing here calls malloc. Admission is a function,
 * not a state: a call that can need storage computes its own claim from the
 * tree's current shape before it changes anything, and funds it from its
 * core's free slots, then the reserve's whole free extents, then storage
 * already returned by retirement (draining its tree's retirement queue
 * first), then base. It returns PIECE_ERR_NOMEM exactly when base refused the
 * remainder. Nothing latches: the next call tries afresh. A caller asks
 * nothing before an edit; a compound command runs under a checkpoint, whose
 * abort cannot fail. After each submitted command the UI calls
 * piece_replenish, outside the input-to-submit window, so the next ordinary
 * command finds its claim funded and calls base only for new ADD chunks. */
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
 * description's and the journal's unit. Descriptors follow their store's
 * chunk grid, so a span from chunk i to chunk j is a head in chunk i, whole
 * chunks i + 1 .. j - 1 (counted from the store's chunk totals) and a tail
 * in chunk j. head and tail are their newline counts, each at most
 * PIECE_CHUNK, or PIECE_NL_UNKNOWN; a span inside one chunk uses head, and
 * its tail is 0. 8 B (P), so an undo inverse entry is 24 B. A known count
 * was true when it was described and stays true while the backing is
 * trusted: a count belongs to its coordinates. */
typedef struct piece_span_nl {
    uint32_t head;
    uint32_t tail;
} piece_span_nl;

#define PIECE_NL_UNKNOWN UINT32_MAX

/* A mapped ORIGINAL: the view's bytes, its fault registration and the
 * lifetime of the mapping that owns both (os.h, mapped-file fault service).
 * acquire adds one owner and release drops one; either may run on any
 * thread, and the last release retires the fault id and unmaps. The bytes
 * stay mapped while any owner remains. After a fault they read zero from the
 * faulting page on: a reader that must trust mapped bytes brackets its reads
 * with two os_fault_poll calls on `fault` and requires epoch 0 from both. */
typedef struct piece_backing {
    const uint8_t *bytes;
    size_t len;
    os_fault_id fault;
    void *ctx;
    void (*acquire)(void *ctx);
    void (*release)(void *ctx);
} piece_backing;

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
 * bytes may fault them in; piece_iter_next_span never reads a byte. */

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
 * text bytes only to count the newlines of what it creates: one
 * piece_replace reads at most 2 x 32,768 = 65,536 B of mapped text (P, a
 * scan of the shorter half of each of the two descriptors it cuts), and
 * piece_replace_spans none (Lines). h is the tree's current number of branch
 * levels. Slot claims are the spec's scalar table (P). */

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
 * reference: their coordinates are inserted as given, and no mapped byte is
 * copied or read. nl is NULL or count records carried with the spans
 * (piece_span_nl), which keep the inserted text exactly as counted as it
 * was; without them the spans' mapped ends are inserted unknown and
 * piece_lines_resolve heals them (Lines). The undo and redo primitive;
 * journal replay passes NULL. A span is valid for t when it lies
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
 * the spans' lengths is at most PIECE_MAX_LENGTH. */
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
/* Adds an owner and returns s. Any thread, O(1), claims nothing. */
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
 * asks. Allocates nothing. Latency only: admission drains the queue itself
 * before it reports PIECE_ERR_NOMEM, and close drains the rest. */
bool piece_reclaim(piece_tree *t, size_t budget);

/* ---- Lines (DRAFT for review, not frozen) ------------------------------- */

/* A line break is '\n'. Lines are numbered from 0: a text with k newlines
 * has k + 1 lines, line i > 0 starts one past the i-th newline, and a text
 * ending in '\n' ends with an empty line that starts at its length.
 *
 * ANNOTATION. Every leaf entry and branch entry carries (bytes, newlines,
 * unknown); a branch entry sums its subtree's counted newlines and ORs its
 * unknown flag. Every sum is below PIECE_MAX_LENGTH = 2^60 (SIZES), so bit
 * 63 of a branch sum is free for the flag by construction; a leaf count is
 * at most PIECE_CHUNK, so UINT32_MAX is free as the leaf's sentinel.
 *
 * EXACTNESS. A newline count belongs to source coordinates, so every edit,
 * undo and redo keeps exact text exact. Unknown text is only mapped ORIGINAL
 * never counted, or spans inserted without counts. What an edit creates is
 * counted then:
 *  - bytes piece_replace or piece_capture copies, while copying them;
 *  - a span inserted by reference: from its carried piece_span_nl; without
 *    one, whole chunks from the store's chunk totals and partial ends in
 *    private storage (ADD, a private-copy ORIGINAL) by scanning the shorter
 *    part, at most PIECE_CHUNK / 2; a mapped end without a record is
 *    unknown, and so is a whole mapped chunk with no table entry;
 *  - the halves of a cut: a counted descriptor by scanning its shorter half
 *    (at most PIECE_CHUNK / 2 = 32,768 B (P)) and subtracting; the halves of
 *    an unknown descriptor stay unknown and nothing is read.
 * An unknown entry becomes counted only through piece_lines_resolve, once
 * its chunk's table entry is present: a whole chunk from the table, a
 * partial one by a UI scan of at most PIECE_CHUNK. Chunk k of a mapped
 * ORIGINAL is [k x PIECE_CHUNK, min((k + 1) x PIECE_CHUNK, len)); the last,
 * possibly short, is whole for open, adoption and replay, and any
 * subinterval of a chunk is a slice. A private-copy tree counts every chunk
 * while it copies and keeps the totals: it is exact from open on.
 *
 * READS OF MAPPED TEXT (P), counted per call: piece_replace at most 65,536 B;
 * piece_replace_spans none; piece_iter_next_span with a record at most
 * 32,768 B per span end inside a counted descriptor; a query at most
 * PIECE_CHUNK; piece_lines_resolve at most PIECE_CHUNK per partial entry.
 *
 * FAULTS. Every scan of mapped bytes is bracketed by two polls of the
 * backing's fault id; a count from a scan that saw a nonzero epoch is
 * discarded and its entry stays unknown. A query polls once more itself
 * and, at a nonzero epoch, answers inexact with lower bound 0: soundness
 * does not wait for the file owner to notice. Exactness is promised for a
 * trusted backing only; L0 repairs addressability after a fault, and the
 * file owner reloads onto a fresh backing.
 *
 * GUARANTEES (the suite's contract; which answers are exact depends on the
 * hidden shape, like the height limit): soundness, an exact answer is the
 * true count; private-copy trees are always exact; carried counts keep undo
 * and redo exact; completeness, once every chunk of a trusted backing is
 * adopted and piece_lines_resolve reports nothing more, every query on the
 * live view, and on snapshots taken after that, is exact. A snapshot's
 * answers never change. */

/* Rank: newlines in [0, off). An offset on an entry boundary excludes the
 * entry that follows, even an unknown one; an offset inside an entry scans
 * that entry, at most PIECE_CHUNK bytes. True when *count is exact: no
 * unknown entry lies wholly before off and the epoch is 0. Otherwise false,
 * and *count is a lower bound: the counted part, or 0 at a nonzero epoch.
 * O(h). ASSERT: off <= piece_length(v), count != NULL. */
bool piece_lines_before(const piece_view *v, uint64_t off, uint64_t *count);

/* Select: the start of line `line` in *off, and in *before the newlines in
 * [0, *off). Past the last line, *off = piece_length(v) and *before is the
 * total. Descends through an unknown-marked ancestor's exact leading
 * children; locating the newline inside its entry is one pass of at most
 * PIECE_CHUNK bytes (P). True when *off is exact. False when an unknown
 * entry before the line's start blocks the search, which then reads no
 * further unknown entry: *off is that entry's start, *before the exact count
 * before it (less than line), and the line starts after *off. At a nonzero
 * epoch: false with *off = *before = 0, except that line 0 is always
 * (0, 0, true). O(h). ASSERT: off, before != NULL. */
bool piece_lines_start(const piece_view *v, uint64_t line, uint64_t *off,
                       uint64_t *before);

/* Off-path counter. Counts the newlines of chunks [first, first + n) of b
 * into counts[0, n) between two polls of b->fault; false, counts discarded,
 * unless both report epoch 0. Any thread while the caller owns a backing
 * reference; touches no tree, allocates nothing, reads at most
 * n x PIECE_CHUNK bytes. ASSERT: b, counts != NULL;
 * first + n <= ceil(b->len / PIECE_CHUNK). */
bool piece_lines_count(const piece_backing *b, uint64_t first,
                       uint32_t *counts, size_t n);

/* Stores counts from a true piece_lines_count of t's backing in the core's
 * chunk table (4 B per chunk (P), allocated by piece_open_mapped: 65,536 B
 * for 1 GiB) and restarts the resolve sweep. Rewrites no entry. UI only,
 * O(n), allocates nothing. ASSERT: ORIGINAL is mapped; first + n <= its
 * chunk count; each count is at most its chunk's length; a chunk adopted
 * again gets the same count. */
void piece_lines_adopt(piece_tree *t, uint64_t first, const uint32_t *counts,
                       size_t n);

/* The resolve sweep: walks the live tree in document order from its saved
 * cursor, visiting at most budget nodes and entries (misses included), and
 * rewrites each unknown entry its chunk table can count. It copies each
 * shared path before rewriting it, so snapshots keep their answers,
 * inexact ones included: up to one copy of every node, e.g. 730 x 400 =
 * 292,000 B for a packed 1 GiB tree whose old root a snapshot pins (P,
 * 683 leaves + 47 branches). The copies are admitted like an edit's:
 * PIECE_ERR_NOMEM changes nothing and the call can be retried; run it
 * before piece_replenish, whose top-up it would otherwise spend. A call
 * that replaces the root invalidates the snapshot cache and ends the live
 * view's revision (live iterators need a seek; the text is unchanged).
 * *more is false once the sweep has passed the end with nothing left to
 * count. Adopting a batch, an abort and a reinsertion without counts
 * restart it, so no entry is skipped forever. An abort may restore an
 * unknown root: the table stays, and the restart makes that root eligible
 * without allocating. UI only, outside the input-to-submit window.
 * ASSERT: more != NULL. */
piece_err piece_lines_resolve(piece_tree *t, size_t budget, bool *more);

/* ---- Transitional (not part of the frozen API) -------------------------- */

/* The Wave 0 link smoke symbol, still called by src/edit/edit.c and
 * tests/layer_L1_test.c. v2-0fv.7b removes it with the stub. */
size_t piece_app_id_len(void);

#endif
