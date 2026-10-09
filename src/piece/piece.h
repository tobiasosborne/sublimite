/* piece.h -- piece tree: the editor's text buffer (bead P1.3 spec, frozen before P1.4).
 *
 * Model: content = concatenation of pieces; a piece is a span of the ORIGINAL
 * (copied or externally mapped, immutable) or of the ADD buffer (append-only,
 * chunked, never reallocated or moved). Byte offsets are uint64_t.
 * Lines: a line break is exactly '\n' (0x0A). '\r' is an ordinary byte that
 * belongs to the line it precedes the '\n' of, so CRLF and LF files are counted
 * identically. line_count = (number of '\n') + 1, so an empty buffer has 1 line.
 *
 * THREADS: a piece_tree is owned by ONE thread (the UI thread); every mutating
 * and every non-snapshot call is UI-thread only. A piece_snapshot is immutable;
 * it may be handed to any worker thread and read concurrently by many threads
 * with no locking. piece_snapshot_release may be called from any thread (once
 * per snapshot). A snapshot stays valid after the tree is destroyed.
 * THREADS (P1.4a-b): no thread creation inside piece_*; bulk counting of
 * unindexed mapped bytes belongs to P1.6 lineidx on the src/work pool.
 *
 * ERRORS: functions returning int return 0 on success, non-zero on failure
 * (PIECE_ERR_RANGE: offset/len out of range, PIECE_ERR_NOMEM: allocator
 * returned NULL). On failure the tree content is unchanged. No abort().
 *
 * ALLOCATION (CLAUDE.md law 2): every byte the implementation obtains comes
 * from piece_allocator hooks supplied at piece_create. piece_insert,
 * piece_delete and piece_snapshot are on the typing path; implementations
 * should serve them from pools/arenas the hooks front, so the hooks themselves
 * must not malloc on those calls once warm. piece_default_allocator() wraps
 * malloc/free for tests only. Hooks are called from the owning thread, except
 * that free may be called from the thread releasing a snapshot (hooks used with
 * snapshots must therefore be thread-safe for free).
 *
 * MEMORY BOUND (perf G10f): live bytes obtained through the allocator must
 * satisfy, for a tree made by piece_init_copy of S bytes:
 *     live <= 1.25*S + 64 KiB + 96*piece_count + 1.25*(typed + deleted) + 4 KiB
 * (+ path copies for live snapshots: 1920 B per edit made while one is live).
 * For mapped originals S contributes 0 and the tree adds <= 32 B per 64 KiB.
 * `typed` = bytes passed to piece_insert; `deleted` = bytes removed by
 * piece_delete (upper bound on deleted-original bytes copied to the add buffer).
 * The 4 KiB is fixed overhead (tree header, root, chunk table).
 *
 * TARGET LAYOUT (guidance only, perf s2.6/s3): 16-way B+ tree, 384 B SoA nodes
 * (byte counts 2 lines, newline counts 2 lines, child pointers 2 lines), pooled,
 * persistent path copying for snapshots, leaf entry ~24 B, ~72 B/piece amortized,
 * add buffer in 64 KiB chunks, lazy newline counts for unindexed mapped pieces.
 * Implementations are compared on this API, not on layout.
 *
 * ALLOCATOR BACKING (P1.3b): piece_allocator hooks should be backed by
 * src/base: fixed-size requests (nodes, chunks, snapshot headers) by an
 * edit_pool, variable/bulk requests by an edit_arena (see src/base/base.h),
 * so that no malloc happens on the typing path.
 *
 * APPEND COALESCING (P1.3b, REQUIRED): if an insert's bytes land in the add
 * buffer immediately after the previous insert's bytes (nothing else was
 * appended to the add buffer in between, same add chunk) AND its byte offset
 * equals the end offset of that previous insert (no other mutation in between),
 * the implementation MUST extend the existing piece instead of creating a new
 * one. Typing N single bytes at the cursor therefore yields <= 2 pieces. Any
 * intervening insert elsewhere, delete or insert_ref breaks the run. A piece
 * never spans an add-chunk boundary, so a run may start a new piece there.
 *
 * REINSERT BY REFERENCE (P1.3b): piece_delete may fill a piece_ref describing
 * where the deleted bytes now live in the add buffer; piece_insert_ref later
 * re-creates pieces pointing at them with no byte copying (undo/redo).
 */
#ifndef PIECE_H
#define PIECE_H
#include <stddef.h>
#include <stdint.h>

#define PIECE_OK 0
#define PIECE_ERR_RANGE 1
#define PIECE_ERR_NOMEM 2

typedef struct piece_tree piece_tree;         /* opaque, UI-thread owned */
typedef struct piece_snapshot piece_snapshot; /* opaque, immutable, any thread */

/* Allocator hooks. alloc returns >= size bytes aligned to 16, or NULL.
 * free receives the same size passed to alloc (sized free enables pools). */
typedef struct piece_allocator {
    void *ctx;
    void *(*alloc)(void *ctx, size_t size);
    void (*free)(void *ctx, void *p, size_t size);
} piece_allocator;

/* malloc/free wrapper. TEST ONLY. */
piece_allocator piece_default_allocator(void);

/* Refcount hooks for an externally owned immutable mapping. The tree calls
 * acquire once per owner it creates (the tree itself, each snapshot) and
 * release when that owner dies. The mapping must stay readable and unchanged
 * until the matching release. Either may be called from any thread. */
typedef struct piece_map_hooks {
    void *ctx;
    void (*acquire)(void *ctx);
    void (*release)(void *ctx);
} piece_map_hooks;

/* Lifetime. piece_create makes an empty tree (len 0). piece_destroy frees it;
 * live snapshots remain valid. */
piece_tree *piece_create(const piece_allocator *a);
void piece_destroy(piece_tree *t);

/* Set initial content; only valid on an empty, never-edited tree.
 * init_copy: tree copies [data, data+len) (caller may free it after return).
 * init_mapped: tree references `mapped` without copying; caller guarantees it
 * is immutable while any acquire is outstanding. Counts for mapped bytes may be
 * computed lazily/in the background but results must always be exact. */
int piece_init_copy(piece_tree *t, const uint8_t *data, size_t len);
int piece_init_mapped(piece_tree *t, const uint8_t *mapped, size_t len,
                      const piece_map_hooks *hooks);

/* Location of deleted bytes in the add buffer (offsets are logical add-buffer
 * offsets, chunk-spanning allowed). Always complete; see piece_delete. */
#define PIECE_REF_SPANS 8
typedef struct piece_ref {
    uint32_t nspans;
    uint64_t len;                       /* total bytes = sum of span lens */
    struct { uint64_t add_off, len; } span[PIECE_REF_SPANS];
} piece_ref;

/* Mutation. O(log n) expected (+ O(len) copy of inserted bytes).
 * insert: off in [0, piece_len]. Bytes are copied into the add buffer.
 * delete: off+len <= piece_len. Any ORIGINAL-sourced bytes in the deleted range
 * are copied into the add buffer at delete time, so the tree never again needs
 * the mapping for them (undo stays byte-exact even if the mapping changes).
 * ref (may be NULL): if non-NULL it is filled with the add-buffer location of
 * the deleted bytes, in content order (original bytes were copied there; add
 * bytes already live there, possibly as several spans). If more than
 * PIECE_REF_SPANS spans would be needed, the whole deleted range is instead
 * copied into ONE new add-buffer span, so the ref is always complete. The
 * G10f bound is unchanged (a delete adds at most `len` add bytes). len == 0
 * is a successful no-op with an empty ref (nspans == 0). */
int piece_insert(piece_tree *t, uint64_t off, const uint8_t *data, size_t len);
int piece_delete(piece_tree *t, uint64_t off, uint64_t len, piece_ref *ref);
/* Reinsert the bytes described by `ref` (from a piece_delete on this tree) at
 * byte offset `off` without copying them. Add-buffer bytes are never freed or
 * moved while the tree lives, so a ref stays valid for the tree's lifetime.
 * PIECE_ERR_RANGE if off > piece_len or a span lies outside the add buffer. */
int piece_insert_ref(piece_tree *t, uint64_t off, const piece_ref *ref);

/* Queries. */
uint64_t piece_len(const piece_tree *t);
uint64_t piece_piece_count(const piece_tree *t);  /* number of pieces (for G10f) */
uint64_t piece_line_count(const piece_tree *t);   /* >= 1 */
/* Copy [off, off+len) to dst. O(log n + len). PIECE_ERR_RANGE if past the end. */
int piece_read(const piece_tree *t, uint64_t off, uint8_t *dst, size_t len);
/* Byte offset of the first byte of 0-based `line`. line >= line_count yields
 * piece_len(t). O(log n) via per-node newline counts. */
uint64_t piece_line_to_byte(const piece_tree *t, uint64_t line);
/* 0-based line containing byte `off` = number of '\n' in [0, off). off is
 * clamped to piece_len. O(log n). */
uint64_t piece_byte_to_line(const piece_tree *t, uint64_t off);

/* Snapshot: O(1) (persistent path sharing), refcounted, immutable, later edits
 * to the tree are invisible to it. Returns NULL on NOMEM. Release exactly once
 * per snapshot; piece_snapshot_retain adds an owner (atomic) for fan-out. */
piece_snapshot *piece_snapshot_take(piece_tree *t);
piece_snapshot *piece_snapshot_retain(piece_snapshot *s);
void piece_snapshot_release(piece_snapshot *s);
uint64_t piece_snapshot_len(const piece_snapshot *s);
uint64_t piece_snapshot_line_count(const piece_snapshot *s);
int piece_snapshot_read(const piece_snapshot *s, uint64_t off, uint8_t *dst, size_t len);
uint64_t piece_snapshot_line_to_byte(const piece_snapshot *s, uint64_t line);
uint64_t piece_snapshot_byte_to_line(const piece_snapshot *s, uint64_t off);

/* Forward iterator over contiguous spans, zero copy. The returned pointer
 * points into the original/mapping or the add buffer and is valid until the
 * tree is next mutated (tree iterators) or the snapshot is released (snapshot
 * iterators). The struct is caller-allocated; fields are private. Spans may be
 * any size >= 1 (adjacent pieces need not be merged) and concatenate to the
 * content from `off` to the end. */
typedef struct piece_iter {
    const void *src;      /* tree or snapshot */
    int is_snap;
    uint64_t pos;         /* next content offset */
    uint64_t priv[4];     /* implementation scratch */
} piece_iter;

void piece_iter_begin(piece_iter *it, const piece_tree *t, uint64_t off);
void piece_iter_begin_snapshot(piece_iter *it, const piece_snapshot *s, uint64_t off);
/* Returns 1 and sets *p,*n for the next span, or 0 at end. */
int piece_iter_next(piece_iter *it, const uint8_t **p, size_t *n);

#endif
