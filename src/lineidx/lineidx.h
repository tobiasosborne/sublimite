/* lineidx.h -- sparse line index (P1.6). See docs/decisions/P1.6.md.
 *
 * One 16-byte entry per ~64 KiB chunk of content: start offset, newline
 * count, flags (built, non-ASCII, edited). A line break is exactly '\n'.
 * line_count = newlines + 1. Content is described by a lineidx_src (a span
 * callback), so the index works over a mapped file, a flat buffer or a
 * piece_snapshot without depending on any of them.
 *
 * THREADS: a lineidx is owned by the UI thread. The only cross-thread data is
 * the per-build job block (results array + published count), written by the
 * single work_pool bulk worker and read by lineidx_poll. A lineidx_src given
 * to lineidx_build_start is read from the worker: it must be immutable (a
 * piece_snapshot, a mapping) and its span callback thread-safe. Its release
 * hook (optional) runs once, after the worker is finished with it.
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

#define LINEIDX_CHUNK 65536u
#define LINEIDX_MSG_PROGRESS 0x4c494458u   /* work_msg.kind: "LIDX" */

/* Contiguous bytes of the content starting at `off`: sets *p and returns the
 * count (>= 1), or 0 when off >= len. */
typedef struct lineidx_src {
    void *ctx;
    uint64_t len;
    size_t (*span)(void *ctx, uint64_t off, const uint8_t **p);
    void (*release)(void *ctx);   /* optional */
} lineidx_src;

typedef struct lineidx lineidx;

/* value is a byte offset or a 0-based line; exact says which kind of answer. */
typedef struct lineidx_result { uint64_t value; bool exact; } lineidx_result;

/* Index of `len` bytes; every chunk starts unbuilt (len == 0: one built chunk). */
lineidx *lineidx_create(uint64_t len);
/* Cancels any build and waits (<= a few ms) for the worker to let go. The
 * work_pool must not have been destroyed with a job still running. */
void lineidx_destroy(lineidx *x);

/* Build job. Starts (or restarts, skipping built chunks) a bulk job over
 * `snap`, which must describe exactly the content the table models. On
 * success (0) the index owns snap->release; on -1 (pool refused) the caller
 * keeps ownership. If nothing is unbuilt it releases at once and returns 0. */
int  lineidx_build_start(lineidx *x, work_pool *pool, const lineidx_src *snap);
void lineidx_build_cancel(lineidx *x);
/* Applies finished chunks from the running job. Returns chunks newly applied. */
size_t lineidx_poll(lineidx *x);
/* True while a started job has not yet stopped (running or queued). */
bool lineidx_building(lineidx *x);

/* Edit the modelled content: [off, off+del) replaced by ins_len bytes.
 * Cancels a running build (poll first so finished work is kept), merges the
 * touched chunks into one unbuilt "edited" chunk, shifts later starts.
 * Returns -1 if off+del > len. */
int  lineidx_edit(lineidx *x, uint64_t off, uint64_t del, uint64_t ins_len);
/* Rescans edited chunks from `cur` (the current content; must have the
 * index's len). Typing path: bounded by the chunk size. Returns chunks scanned. */
size_t lineidx_refresh(lineidx *x, const lineidx_src *cur);

/* State. */
uint64_t lineidx_len(const lineidx *x);
size_t   lineidx_chunk_count(const lineidx *x);
size_t   lineidx_built_prefix(lineidx *x);    /* leading built chunks */
bool     lineidx_complete(lineidx *x);        /* all chunks built: exact everywhere */
bool     lineidx_chunk_nonascii(const lineidx *x, size_t i);
bool     lineidx_any_nonascii(const lineidx *x);  /* over built chunks */
size_t   lineidx_entry_bytes(void);           /* 16 */
size_t   lineidx_mem_bytes(const lineidx *x); /* table + summaries, live */

/* Queries (UI thread, no allocation). Exact when the answer lies in the built
 * prefix (at most one chunk is scanned from `cur`); else an estimate by byte
 * proportion from the prefix density. */
lineidx_result lineidx_line_count(lineidx *x);              /* value = lines */
lineidx_result lineidx_line_to_byte(lineidx *x, const lineidx_src *cur, uint64_t line);
lineidx_result lineidx_byte_to_line(lineidx *x, const lineidx_src *cur, uint64_t off);
/* Partial-index jump: like line_to_byte, but when the line is past the built
 * prefix, first extends the prefix by scanning up to `budget` bytes from `cur`
 * (recording the chunks it counts), so the answer is exact whenever the
 * target lies within the budget. Otherwise the result is an estimate that
 * starts at a real line boundary (value is a line start, exact == false). */
lineidx_result lineidx_seek_line(lineidx *x, const lineidx_src *cur, uint64_t line, uint64_t budget);

#endif
