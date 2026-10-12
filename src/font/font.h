/* src/font/font.h - font loading, ASCII atlas lookup, runtime rasteriser and
 * shelf atlas allocator (P2.3, edit-e6x.3, reduced scope: no fontconfig). */
#ifndef EDIT_FONT_FONT_H
#define EDIT_FONT_FONT_H

#include <stddef.h>
#include <stdint.h>
#include <stdatomic.h>
#include <pthread.h>
#include "base/base.h"
#include "work/work.h"
#include "render/render.h"

#define FONT_ATLAS_PAGE_DIM   1024u
#define FONT_ATLAS_MAX_PAGES  8u
#define FONT_OPAQUE_BYTES     256u   /* >= sizeof(stbtt_fontinfo), checked in font.c */

enum {
    FONT_OK          = 0,
    FONT_MORE        = 1,   /* bounded composition slice; retry exact key */
    FONT_ERR_MISSING = -1,  /* codepoint has no glyph in this font */
    FONT_ERR_NOMEM   = -2,  /* arena or atlas exhausted */
    FONT_ERR_INIT    = -3,  /* malformed or unsupported font data */
    FONT_ERR_ARG     = -4
};

typedef struct font_metric {
    int32_t  advance;    /* px, rounded */
    int32_t  bearing_x;  /* px, pen origin to bitmap left */
    int32_t  bearing_y;  /* px, baseline to bitmap top, y down (negative = above) */
    uint32_t w, h;       /* bitmap size in px */
} font_metric;

typedef struct font_cell {
    uint32_t cell_w;     /* max advance over ASCII */
    uint32_t cell_h;     /* ascent + descent */
    uint32_t ascent;
    uint32_t descent;
} font_cell;

/* Baked ASCII atlas (0x20..0x7E) for one pixel size. Static const data. */
typedef struct font_ascii_atlas {
    uint32_t           px;
    font_cell          cell;
    const font_metric *metrics;   /* 95 entries, cp - 0x20 */
    const uint8_t     *pixels;    /* one row of 95 cells */
    size_t             pixels_len;
} font_ascii_atlas;

typedef struct font {
    _Alignas(16) unsigned char info[FONT_OPAQUE_BYTES]; /* opaque stbtt_fontinfo */
    const unsigned char *data;
    size_t   len;
    uint32_t px;
    uint32_t face_index;  /* TTC face selected at init (0 for plain TTF) */
    uint32_t glyf_len;    /* validated glyf table length; 0 for CFF outlines */
    uint32_t bake_identity; /* INIT SHA-256 of immutable bytes and face matches both bakes */
    uint32_t num_glyphs;  /* validated maxp numGlyphs */
    float    scale;
    font_cell cell;
    const font_ascii_atlas *atlas; /* baked atlas matching this font at px, or NULL */
} font_t;

typedef struct font_bitmap {
    uint8_t     *pixels;  /* arena memory, w*h R8 coverage; NULL if empty glyph */
    uint32_t     w, h;
    font_metric  m;
} font_bitmap;

typedef struct font_atlas_page {
    uint32_t used_y;   /* first free row */
    uint32_t shelf_y;  /* top of the open shelf */
    uint32_t shelf_h;  /* height of the open shelf */
    uint32_t cur_x;    /* first free column in the open shelf */
} font_atlas_page;

typedef struct font_atlas {
    uint32_t         npages;
    uint32_t         max_pages;
    font_atlas_page  pages[FONT_ATLAS_MAX_PAGES];
} font_atlas;

/* Loads immutable font bytes (caller keeps them alive). Does not set a size.
 * Every table the raster path reads is bounds-checked against len first
 * (docs/decisions/P2.3c.md); malformed data returns FONT_ERR_INIT and clears f.
 * font_init selects face 0; font_init_index selects face `index` of a TTC
 * (index must be 0 for a plain TTF/OTF). */
int  font_init(font_t *f, const unsigned char *ttf, size_t len);
int  font_init_index(font_t *f, const unsigned char *ttf, size_t len, uint32_t index);
/* Sets the pixel height and recomputes scale and cell metrics. */
int  font_set_px(font_t *f, uint32_t px);
/* True em size: points * display DPI / points-per-inch, rather than hhea height. */
int font_set_em_px(font_t *f, uint32_t px);
/* Immutable vendored primary, independent of process working directory. */
const unsigned char *font_embedded_data(size_t *len);
int  font_cell_metrics(const font_t *f, font_cell *out);
/* Metrics of a codepoint without rasterising. */
int  font_glyph_metrics(const font_t *f, uint32_t cp, font_metric *out);
/* Rasterises cp into arena memory. FONT_ERR_MISSING for absent glyphs.
 * FONT_ERR_NOMEM leaves pixels NULL and the arena at its entry mark.
 * A font_t must not be rasterised concurrently (stb userdata is per-call). */
/* UI cache admission: at most 256 expanded TT points / 16 components,
 * or 2048 CFF steps / 256 vertices, 64x64 bitmap, bounded outline*box work.
 * Excess complexity returns INIT; use general metrics/raster on a worker
 * for content requiring a larger workload. No elapsed-time guarantee under
 * descheduling; these are structural work bounds measured on the loaded box. */
int font_glyph_ui_metrics(const font_t *f, uint32_t cp, font_metric *out);
/* Same admission plus at most 4096 curve subdivision visits / 512 points.
 * Scratch is rolled back on refusal; only this bounded raster API is UI-safe. */
int font_raster_glyph_ui(font_t *f, uint32_t cp, edit_arena *arena, font_bitmap *out);
int  font_raster_glyph(font_t *f, uint32_t cp, edit_arena *arena, font_bitmap *out);
/* Composites a bitmap into a cell_w x cell_h R8 cell (caller zeroes it). */
int  font_place_in_cell(const font_t *f, const font_bitmap *b, uint8_t *cell,
                        uint32_t cell_w, uint32_t cell_h);

/* Baked ASCII atlas (0x20..0x7E) from src/font/atlas_ascii.h. */
const font_metric *font_ascii_glyph(uint32_t cp);      /* NULL outside ASCII */
const uint8_t     *font_ascii_pixels(size_t *len);     /* one row of cells */
font_cell          font_ascii_cell(void);
uint32_t           font_ascii_px(void);

/* Atlas selection by requested px (15 and 30 are baked); NULL if none. */
const font_ascii_atlas *font_ascii_atlas_for_px(uint32_t px);
const font_metric      *font_ascii_atlas_glyph(const font_ascii_atlas *a, uint32_t cp);

/* ---- Fallback font discovery (fontconfig via dlopen, on a work-pool worker) */
#define FONT_FALLBACK_PATH_MAX 512u
#define FONT_FALLBACK_MSG_KIND 0x46414C42u   /* "FALB" */

typedef struct font_fallback_result {
    char cjk[FONT_FALLBACK_PATH_MAX], emoji[FONT_FALLBACK_PATH_MAX];
    uint32_t cjk_index, emoji_index;
    int have_fontconfig;
    uint64_t elapsed_ns;
    pthread_t worker;
} font_fallback_result;

typedef struct font_fallback {
    _Atomic uint32_t done;            /* 1 after exclusive sync run or live mailbox adoption */
    /* Valid after done == 1: */
    char       cjk[FONT_FALLBACK_PATH_MAX];    /* "" if none */
    char       emoji[FONT_FALLBACK_PATH_MAX];  /* monochrome emoji; "" if none */
    uint32_t   cjk_index;             /* FC_INDEX of cjk: pass to font_init_index */
    uint32_t   emoji_index;
    int        have_fontconfig;       /* libfontconfig.so.1 loaded */
    uint64_t   elapsed_ns;
    pthread_t  worker;                /* thread that ran the discovery */
    font_fallback_result staged;      /* worker-owned; only event() may adopt */
    uint32_t staged_generation, staged_slot, staged_epoch;
    const char *discovery_program; /* INIT test seam: trusted absolute fc-match-compatible executable; NULL=system */
    _Atomic uint32_t child_pid, cancellation_polls; /* discovery lease diagnostics */
} font_fallback;

/* One outstanding discovery per fb. Reset on the owner before submitting,
 * after physical completion and all previous readers/events are finished.
 * Async results cross only work mailboxes: done stays zero until event adopts
 * a live completion. Keep fb alive through physical completion AND message
 * drain. Do not route stale/handmade messages to font_fallback_event.
 * Synchronous discovery is for exclusively owned INIT storage (not a pooled
 * cancellable job); it publishes done directly and cannot be cancelled. */
void font_fallback_reset(font_fallback *fb);
/* Synchronous discovery with the given soname (test seam). Resets fb, fills it,
 * publishes done (release). */
void font_fallback_discover(font_fallback *fb, const char *soname);
/* work_job fn: ctx->arg is a font_fallback*. Uses isolated /usr/bin/fc-match
 * (unavailable executable gracefully gives empty fallbacks). Stages a result and retries full
 * mailbox publication cancellably. Never sets public done. */
void font_fallback_job(work_ctx *c);
/* UI mailbox callback ONLY: adopt matching live result, once. Returns true
 * on adoption. Family preparation may then read the public fields/done. */
int font_fallback_event(font_fallback *fb, const work_msg *msg);
/* INIT/worker: reads a font file into arena memory. len is required; failure
 * clears it and preserves the entry arena mark. NULL on failure. */
unsigned char *font_load_file(const char *path, edit_arena *arena, size_t *len);

/* Shelf allocator over FONT_ATLAS_PAGE_DIM square R8 pages. */
void font_atlas_init(font_atlas *a, uint32_t max_pages);
/* 0 on success with (page, x, y); FONT_ERR_NOMEM when every page is full. */
int  font_atlas_alloc(font_atlas *a, uint32_t w, uint32_t h,
                      uint32_t *page, uint32_t *x, uint32_t *y);

/* P4.11: monochrome cluster composition. Family preparation is INIT/worker
 * only (file I/O). primary must be sized and have a matching baked atlas.
 * fb must be published (done acquire == 1); NULL selects embedded-only.
 * Immutable font bytes remain borrowed; arena and family outlive the cache.
 * Unsupported fallback files return their error, leaving the primary usable.
 * A family is exclusively owned: raster calls mutate stb's scratch userdata. */
#define FONT_FAMILY_MAX_FACES 3u
#define FONT_CLUSTER_MAX_BYTES 16384u
#define FONT_CACHE_MAX_PROBES 32u
#define FONT_COMPOSE_MAX_GLYPHS 8u
typedef struct font_family {
    font_t faces[FONT_FAMILY_MAX_FACES];
    uint32_t count;
} font_family;
int font_family_load(font_family *family, const font_t *primary,
                     const font_fallback *fb, edit_arena *arena);
/* Sequence controls are invisible, not missing glyphs. This is not a width
 * table; widths/segmentation always come from utf8. */
int font_cluster_ignorable(uint32_t cp);

typedef struct font_cache_entry {
    uint64_t hash;
    size_t key;
    uint32_t len, width, slot;
    int result;
} font_cache_entry;
typedef struct font_cache {
    font_family *family;
    font_cell cell;
    font_atlas atlas;
    render_atlas_page pages[FONT_ATLAS_MAX_PAGES + 1u];
    render_glyph *glyphs;
    font_cache_entry *entries, *negative_entries;
    uint8_t *keys, *image, *negative_keys, *pending_key;
    size_t entry_count, negative_capacity, negative_count, negative_used;
    size_t pending_len, pending_off;
    uint32_t pending_width;
    int32_t pending_advance;
    int resource_error; /* sticky NOMEM until replaced at quiescence; MORE is resumable */
    size_t capacity, key_capacity, key_used, glyph_count;
    edit_arena scratch;
    render_grid *grid;
    uint64_t hits, misses;
    uint64_t probes, rasterizations; /* work counters, including failed calls */
    /* Optional worker-only scalar resolver. With this set, composition uses
     * general rasterisation; never call this cache on the UI thread. */
    int (*resolve)(void *ctx, uint32_t cp, font_t **face);
    void *resolve_ctx;
} font_cache;
/* Compose into cache->image without admitting a key or atlas rectangle.
 * Worker-owned scratch; FONT_MORE resumes the exact key. */
int font_cache_render(font_cache *cache, const uint8_t *bytes, size_t len, uint32_t width);
/* INIT only: reserve all pages, exact keys, glyphs, cell image and scratch.
 * Append-only cache, no eviction: rectangles remain immutable through T5.
 * Exhaustion returns NOMEM; existing entries stay usable. init failure rolls
 * back arena. At most FONT_CACHE_MAX_PROBES per table. Negatives use separate
 * capped storage, preserving positive capacity. Scratch needs no malloc.
 * Keep exclusive UI ownership after worker preparation/handoff. */
int font_cache_init(font_cache *cache, font_family *family, edit_arena *arena,
                    uint32_t max_pages, size_t entries, size_t key_bytes,
                    size_t scratch_bytes);
/* UI: bind one grid with matching cell dimensions. Updates its table counts
 * as glyphs are appended; bind again to switch grids. No allocation. */
int font_cache_bind(font_cache *cache, render_grid *grid);
/* layout_glyph_fn-compatible. width (1/2) comes from utf8_cluster; exact UTF-8
 * key plus width identifies a precomposed image. Invalid bytes return ARG;
 * layout draws one inverse '?' per invalid byte without calling this API.
 * Zero-width marks merge at the base pen, all faces use the primary baseline.
 * Scalar outlines are overlaid, not shaped. Layout projects ZWJ emoji and
 * invisible presentation modifiers with utf8_cluster_artwork before lookup.
 * Cold composition performs at most FONT_COMPOSE_MAX_GLYPHS raster calls and
 * checks a 250 us cooperative deadline between scalars. FONT_MORE leaves slot
 * unchanged: retry the exact key after checking input. One pending image per
 * cache; another cold key abandons its partial image, cache hits preserve it.
 * NOMEM sets resource_error: retain this cache through T5, then replace storage
 * and fully relayout. Neither MORE nor NOMEM is a missing-font verdict.
 * No fontconfig, I/O, malloc, or arena growth, including on cold misses. */
int font_cache_glyph(void *ctx, const uint8_t *cluster, size_t len,
                     uint32_t width, uint32_t *slot);

/* Worker-prepared runtime owner. Construct at INIT with exclusive, disjoint
 * file/cache arenas. Config fallback (if present) must already be adopted and
 * remain immutable until physical job completion. Submit prepare_job once;
 * adopt only in a live work-mailbox callback. No owner/cache use by UI until
 * adoption; prepare_job writes nothing after publishing. All font bytes,
 * owner, arenas and atlas descriptors survive every borrowing backend T5 and
 * physical work completion/message drain. Rebuild in a separate owner, then
 * switch at a safe backend boundary with full relayout; reclaim at quiescence.
 * No teardown API can prove the editor's backend retirement on its behalf. */
#define FONT_RUNTIME_MAX_GLYPHS 4096u /* G: existing GL frame cap, gl/gl.h */
#define FONT_RUNTIME_MSG_KIND 0xF002u
typedef struct font_runtime_config {
    /* Must identify the baked embedded primary; sizes supported by its atlas. */
    const char *primary_path;
    const font_fallback *fallback;
    uint32_t px, max_pages;
    size_t entries, key_bytes, scratch_bytes;
} font_runtime_config;
typedef struct font_runtime {
    font_family family;
    font_cache cache;
    font_runtime_config config;
    char primary_path[FONT_FALLBACK_PATH_MAX];
    edit_arena *files, *storage;
    pthread_t worker;
    int adopted, result, staged_result, fallback_error;
    uint32_t staged_generation, staged_slot, staged_epoch;
} font_runtime;
/* Backend reservation before submission: baked page + every reserved runtime
 * page, entries+95 descriptors; rejected configs exceed renderer glyph cap. */
int font_runtime_limits(const font_runtime_config *config, uint32_t *pages,
                        size_t *glyphs, size_t *pixel_bytes);
int font_runtime_init(font_runtime *owner, const font_runtime_config *config,
                      edit_arena *files, edit_arena *storage);
void font_runtime_prepare_job(work_ctx *ctx);
int font_runtime_event(font_runtime *owner, const work_msg *msg);
int font_runtime_bind(font_runtime *owner, render_grid *grid);
/* layout callback; MORE until mailbox adoption, then bounded cache lookup. */
int font_runtime_glyph(void *ctx, const uint8_t *bytes, size_t len,
                       uint32_t width, uint32_t *slot);

/* Production asynchronous owner. INIT reserves UI keys/descriptors and worker
 * atlas storage. Cold glyph lookup enqueues immutable requests without waiting
 * or allocating; FONT_OK can denote a pending placeholder slot. Slots/identity
 * remain stable when mailbox adoption replaces its descriptor. Rasterisation,
 * fontconfig discovery and face file reads happen exclusively on WORK_BULK.
 * Backend descriptor copies permit UI adoption during an older frame. LRU
 * reuse excludes retained cells and pending requests. Close after backend shutdown
 * and work_pool_shutdown/message drain, never from a mailbox callback. */
typedef struct font_async font_async;
#define FONT_ASYNC_MSG_KIND UINT32_C(0x46474c59) /* FGLY */
int font_async_open(font_async **out, uint32_t px, work_pool *pool);
void font_async_close(font_async *owner);
void font_async_limits(uint32_t *pages, size_t *glyphs, size_t *pixels, uint32_t px);
/* Dynamic ASCII atlas borrowed from the editor's worker-prepared version.
 * Reserve the maximum cell at INIT; em selects true-em primary/fallback sizing.
 * Atlas bytes and descriptors survive backend retirement and work completion. */
void font_async_atlas_limits(uint32_t *pages, size_t *glyphs, size_t *pixels,
                             size_t ascii_bytes);
int font_async_open_atlas(font_async **out, const font_ascii_atlas *atlas, bool em,
                          font_cell max_cell, work_pool *pool);
/* UI quiescence check includes mailbox adoption AND physical job completion.
 * Reset on a worker only while idle, detached from UI and retired by backend.
 * Reuses INIT reservations; no owner/storage allocations during zoom. */
bool font_async_idle(const font_async *owner);
int font_async_reset_atlas(font_async *owner, const font_ascii_atlas *atlas, bool em);
/* Worker-only, detached idle version: copy a primary descriptor whose font
 * bytes remain borrowed until reset/shutdown and backend retirement. */
int font_async_set_primary(font_async *owner, const font_t *primary);
int font_async_bind(font_async *owner, render_grid *grid);
/* UI, at frame begin: retain both existing grids and all subsequent lookups.
 * active preserves the preceding frame's pins until backend retirement.
 * Grids are inspected during this call only. Standalone users without frame
 * borrows may omit it. Slots stay stable while retained or pending. */
void font_async_frame(font_async *owner, const render_grid *grid,
                      const render_grid *text, bool active);
/* UI: only for a full replacement, after backend retirement. Drop outgoing
 * frame pins, retaining all lookups made for the incoming frame. */
void font_async_reclaim(font_async *owner);
int font_async_glyph(void *ctx, const uint8_t *bytes, size_t len,
                     uint32_t width, uint32_t *slot);
/* UI: retry queued requests after checking input/pumping the shared pool. */
void font_async_pump(font_async *owner);
bool font_async_pending(const font_async *owner);
/* Live mailbox callback only; returns adopted slot, or RENDER_NO_SLOT. The
 * caller damages cells using that slot, including pixels-only changes. */
uint32_t font_async_event(font_async *owner, const work_msg *msg);

#endif
