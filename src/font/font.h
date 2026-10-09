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

#define FONT_ATLAS_PAGE_DIM   1024u
#define FONT_ATLAS_MAX_PAGES  8u
#define FONT_OPAQUE_BYTES     256u   /* >= sizeof(stbtt_fontinfo), checked in font.c */

enum {
    FONT_OK          = 0,
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
int  font_cell_metrics(const font_t *f, font_cell *out);
/* Metrics of a codepoint without rasterising. */
int  font_glyph_metrics(const font_t *f, uint32_t cp, font_metric *out);
/* Rasterises cp into arena memory. FONT_ERR_MISSING for absent glyphs.
 * FONT_ERR_NOMEM leaves pixels NULL and the arena at its entry mark.
 * A font_t must not be rasterised concurrently (stb userdata is per-call). */
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

typedef struct font_fallback {
    _Atomic uint32_t done;            /* 1 once discovery finished (release) */
    /* Valid after done == 1: */
    char       cjk[FONT_FALLBACK_PATH_MAX];    /* "" if none */
    char       emoji[FONT_FALLBACK_PATH_MAX];  /* monochrome emoji; "" if none */
    uint32_t   cjk_index;             /* FC_INDEX of cjk: pass to font_init_index */
    uint32_t   emoji_index;
    int        have_fontconfig;       /* libfontconfig.so.1 loaded */
    uint64_t   elapsed_ns;
    pthread_t  worker;                /* thread that ran the discovery */
} font_fallback;

/* Threading rule: one writer. Before (re)submitting font_fallback_job on an fb
 * that has run before, the owner calls font_fallback_reset(fb) on the thread
 * that submits, after every reader of the previous result has finished; that
 * clears done (release) and the results, so a stale done can never be observed
 * while a new run is queued or running. Readers load done with acquire and
 * touch the other fields only if it is 1. Discovery cannot revoke a reader that
 * already saw done == 1: the owner must not reset under a live reader.
 * No concurrent discovery on one fb. */
void font_fallback_reset(font_fallback *fb);
/* Synchronous discovery with the given soname (test seam). Resets fb, fills it,
 * publishes done (release). */
void font_fallback_discover(font_fallback *fb, const char *soname);
/* work_job fn: ctx->arg is a font_fallback*. Publishes FONT_FALLBACK_MSG_KIND. */
void font_fallback_job(work_ctx *c);
/* Reads a font file into arena memory (for the fallback face). NULL on failure. */
unsigned char *font_load_file(const char *path, edit_arena *arena, size_t *len);

/* Shelf allocator over FONT_ATLAS_PAGE_DIM square R8 pages. */
void font_atlas_init(font_atlas *a, uint32_t max_pages);
/* 0 on success with (page, x, y); FONT_ERR_NOMEM when every page is full. */
int  font_atlas_alloc(font_atlas *a, uint32_t w, uint32_t h,
                      uint32_t *page, uint32_t *x, uint32_t *y);

#endif
