/* lineidx.h -- sparse line index. See docs/decisions/P1.6{b,c}.md.
 *
 * One 16-byte entry per chunk of at most 64 KiB: relative length, newline
 * count, flags and next ID. Summarized blocks avoid whole-table maintenance.
 * A line break is exactly '\n'.
 * line_count = newlines + 1. Content is described by a lineidx_src (a span
 * callback), so the index works over a mapped file, a flat buffer or a
 * piece_snapshot without depending on any of them.
 *
 * THREADS: a lineidx is owned by the UI thread. The only cross-thread data is
 * sealed per-build result ranges, published and generation/lease validated by
 * work mailboxes before UI adoption. Poll selectively receives this build's
 * messages; ordinary shared-pool drains invoke its registered receiver too.
 * Physical source retirement and CPU diagnostics use work's completion
 * acknowledgement. A lineidx_src given
 * to lineidx_build_start is read from the worker: it must be immutable (a
 * piece_snapshot, a mapping) and its span callback thread-safe. Its release
 * hook (optional) runs once on the UI during maintenance/destruction, after
 * physical completion. Logical cancellation never invokes it.
 *
 * ALLOCATION: lineidx_create / lineidx_build_start allocate (open path, not
 * typing path). lineidx_edit, lineidx_refresh, lineidx_poll and every query
 * never allocate (the table has fixed capacity). */
#ifndef LINEIDX_H
#define LINEIDX_H
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "work/work.h"
#include "piece/piece.h"

#define LINEIDX_CHUNK 65536u
#define LINEIDX_MAX_LEN (UINT64_MAX - 1u) /* leaves room for newlines + 1 */
#define LINEIDX_MSG_PROGRESS 0x4c494458u   /* work_msg.kind: "LIDX" */
#define LINEIDX_EDIT_MAX_CHUNKS 512u /* foreground edit admission limit */

/* Contiguous bytes starting at `off`: sets *p and returns count (>= 1), or 0
 * when off >= len. Each callback is indivisible and must itself bound CPU:
 * <= 4 ms for a worker source (leaving CPU for scanner/polling), and within
 * the caller's slice for a UI source.
 * Blocking I/O affects physical completion, not logical cancellation. */
typedef struct lineidx_src {
    void *ctx;
    uint64_t len;
    size_t (*span)(void *ctx, uint64_t off, const uint8_t **p);
    void (*release)(void *ctx);   /* optional */
} lineidx_src;

typedef struct lineidx lineidx;

/* value is a byte offset or a 0-based line; exact says which kind of answer. */
typedef struct lineidx_result { uint64_t value; bool exact; } lineidx_result;

/* Index of `len` bytes; every chunk starts unbuilt (len == 0: one built chunk).
 * Rejects len > LINEIDX_MAX_LEN, chunk capacity >= UINT32_MAX (private pool
 * IDs), or unrepresentable allocation sizes. */
lineidx *lineidx_create(uint64_t len);
/* Bind the original's file backing before any scan/build. Retains only its
 * lifetime token, so completed metadata remains invalidatable after snapshot
 * retirement/file close. Refuses rebinding or an already started index.
 * A fault makes all answers inexact and cancels pending adoption; reload uses
 * a new index. Non-file/private snapshots need no token. No allocation. */
int lineidx_bind_snapshot(lineidx *x, const piece_snapshot *snapshot);
/* Allocating path: reserve additional bounded chunks for later edits. The
 * ordinary create reserves 64. All foreground operations still allocate zero. */
lineidx *lineidx_create_reserved(uint64_t len, size_t extra_chunks);
/* Cancels and waits for physical source retirement. Queued builds are removed
 * immediately; an active source may still be in blocking I/O. Release hooks
 * must be bounded for UI destruction. The work_pool must outlive its jobs. */
void lineidx_destroy(lineidx *x);

/* Build job. Starts (or restarts, skipping built chunks) a bulk job over
 * `snap`, which must describe exactly the content the table models. On
 * success (0) the index owns snap->release; on -1 (refusal, invalid source or
 * allocation failure) the caller keeps ownership. With no retiring lease,
 * if nothing is unbuilt it releases at once and returns 0. */
int  lineidx_build_start(lineidx *x, work_pool *pool, const lineidx_src *snap);
/* Interactive build for an unindexed jump. Requires a foreground-enabled
 * pool and a resident/nonblocking source. Same ownership as build_start;
 * yields between bounded batches and on mailbox backpressure. Ordinary
 * background indexing and sources requiring blocking I/O use build_start. */
int  lineidx_build_start_foreground(lineidx *x, work_pool *pool, const lineidx_src *snap);
/* Upgrade a live background build when a resident-source interactive jump
 * needs it. Same snapshot/scratch/work lease; queued work moves immediately,
 * running work moves at its next bounded continuation. Caller must guarantee
 * span does not block on I/O. Returns -1 if no live build can be prioritized. */
int  lineidx_build_prioritize(lineidx *x);
/* As above, with the bytes retained by the source lease included in memory
 * accounting. Use 0 for borrowed storage. The ordinary start uses SIZE_MAX
 * (unknown) when snap has a release hook. A retiring lease refuses a restart
 * with -1 until the worker lets go; caller retains the refused source. */
int  lineidx_build_start_owned(lineidx *x, work_pool *pool,
                              const lineidx_src *snap, size_t source_bytes);
/* Logical acknowledgement only: cancel/invalidate/unbind before any adoption
 * or source cleanup. Keeps already adopted chunks; cleanup is deferred. */
void lineidx_build_cancel(lineidx *x);
/* Stage at most four examined mailbox messages; adopt at most 64 entries,
 * checking a 0.5 ms CPU deadline between entries. Returns newly built chunks.
 * Ordinary shared-pool callbacks only stage availability (O(1)). Physical
 * source release is maintenance work; its hook must be bounded by the caller. */
size_t lineidx_poll(lineidx *x);
/* True while queued/running, retiring, or with staged results awaiting adoption.
 * Does not pump mailboxes; call poll when awaiting index completion. */
bool lineidx_building(lineidx *x);

/* Edit the modelled content: [off, off+del) replaced by ins_len bytes.
 * Cancels a running build first, keeps adopted counts, and replaces touched
 * chunks with bounded unbuilt "edited" chunks. Offsets are derived from
 * relative lengths; suffix entries are not shifted or moved.
 * Returns -1 before cancellation/mutation for invalid bounds, length overflow
 * or exhausted fixed chunk/block capacity, including a replacement exceeding
 * LINEIDX_EDIT_MAX_CHUNKS. Larger replacements need an index recreated for
 * the resulting content on the allocating path. No mutation on refusal. */
int  lineidx_edit(lineidx *x, uint64_t off, uint64_t del, uint64_t ins_len);
/* Rescans edited chunks from `cur` (the current content; must have the
 * index's len). Scans at most one chunk per call; repeat off the typing path
 * to drain edited chunks. At most 256 spans / 0.5 ms CPU between callbacks;
 * partial counts are retained. Returns 1 for a completed chunk, otherwise 0
 * (including a yielded partial chunk). Complete via further calls or build. */
size_t lineidx_refresh(lineidx *x, const lineidx_src *cur);

/* State. */
uint64_t lineidx_len(const lineidx *x);
size_t   lineidx_chunk_count(const lineidx *x);
size_t   lineidx_built_prefix(lineidx *x);    /* leading built chunks */
bool     lineidx_complete(lineidx *x);        /* all chunks built: exact everywhere */
bool     lineidx_chunk_nonascii(const lineidx *x, size_t i);
bool     lineidx_any_nonascii(const lineidx *x);  /* over built chunks */
size_t   lineidx_entry_bytes(void);           /* 16 */
size_t   lineidx_mem_bytes(const lineidx *x); /* object, capacity, summaries,
                                              jobs and declared source bytes;
                                              SIZE_MAX if unknown/overflow */
/* Cumulative foreground metadata visits, for deterministic slice checks. */
uint64_t lineidx_foreground_work(const lineidx *x);
uint64_t lineidx_scanned_bytes(const lineidx *x);
/* Cancellation's worker CPU interval from its preceding polling checkpoint.
 * Available after physical completion; zero if unfinished/no worker stop.
 * Metadata is read through work's completion acknowledgement, not a separate
 * result channel. Includes indivisible span callback CPU, excludes I/O waits. */
uint64_t lineidx_cancel_cpu_ns(const lineidx *x);

/* Queries (UI thread, no allocation). Exact when the answer lies in the built
 * prefix (at most one chunk is scanned from `cur`); else an estimate by byte
 * proportion from the prefix density. */
lineidx_result lineidx_line_count(lineidx *x);              /* value = lines */
lineidx_result lineidx_line_to_byte(lineidx *x, const lineidx_src *cur, uint64_t line);
lineidx_result lineidx_byte_to_line(lineidx *x, const lineidx_src *cur, uint64_t off);
/* Synchronous seek continuation: at most min(budget, 64 KiB) unindexed bytes,
 * 256 span callbacks, and 0.5 ms CPU between callbacks per call. Partial counts
 * persist without marking a partial chunk built. Exact if the target is found
 * within this slice; otherwise a proven line anchor with exact=false. A target
 * already indexed costs at most one separately bounded chunk query. Repeat
 * after checking input, or use the worker API for a bulk jump. */
lineidx_result lineidx_seek_line(lineidx *x, const lineidx_src *cur, uint64_t line, uint64_t budget);
/* Allocating request path: worker continuation over an immutable source.
 * Ownership/refusal rules match build_start_owned. Stops at the requested
 * line, and publishes its exact offset through work before UI adoption.
 * Shares the index's single active/retiring source lease. */
int lineidx_seek_start_owned(lineidx *x, work_pool *pool, const lineidx_src *snap,
                             uint64_t line, size_t source_bytes);
/* Bounded poll, then return true if this request's exact offset is available.
 * Cancellation/edit/replacement suppresses the result. */
bool lineidx_seek_result(lineidx *x, lineidx_result *result);

#endif
