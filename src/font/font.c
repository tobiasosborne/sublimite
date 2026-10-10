/* src/font/font.c - see font.h. stb_truetype is vendored in vendor/. */
#include "font/font.h"
#include <setjmp.h>
#include <string.h>

/* stb_truetype allocations go to a per-call arena (Law 2: no libc malloc on
 * the typing path). info->userdata points at a font_stb_ctx that exists only
 * inside font_raster_glyph; otherwise userdata is NULL and stb gets NULL back
 * (metrics paths never allocate in stb). free is a no-op: the caller resets
 * the arena to its mark. On exhaustion, leave stb immediately: its scanline
 * allocation dereferences NULL and its active-edge allocation asserts. */
typedef struct font_stb_ctx {
    edit_arena *arena;
    jmp_buf     nomem;
} font_stb_ctx;

static void *font_stb_alloc(size_t n, void *u)
{
    font_stb_ctx *c = (font_stb_ctx *)u;
    if (!c) return NULL;
    void *p = edit_arena_alloc(c->arena, n ? n : 1u, 16);
    if (!p) longjmp(c->nomem, 1);
    return p;
}
/* stb asserts on outline data that disagrees with its header box (found by
 * font_fuzz). Asserts are fatal by default; route them to the same error
 * boundary. stb's assert macro carries no context, so the active boundary is a
 * thread-local pointer set only for the duration of a stb call (the one
 * deliberate exception to the no-globals rule; see docs/decisions/P2.3c.md). */
static _Thread_local font_stb_ctx *font_stb_cur;
static void font_stb_assert_fail(void)
{
    if (font_stb_cur) longjmp(font_stb_cur->nomem, 2);
    __builtin_trap();
}
#define STBTT_assert(x) ((x) ? (void)0 : font_stb_assert_fail())
#define STBTT_malloc(x, u) font_stb_alloc((size_t)(x), (u))
#define STBTT_free(x, u)   ((void)(x), (void)(u))

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wall"
#pragma GCC diagnostic ignored "-Wextra"
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wsign-conversion"
#pragma GCC diagnostic ignored "-Wshadow"
#pragma GCC diagnostic ignored "-Wcast-qual"
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wfloat-conversion"
#pragma GCC diagnostic ignored "-Wdouble-promotion"
#pragma GCC diagnostic ignored "-Wpedantic"
#define STB_TRUETYPE_IMPLEMENTATION
#include "../../vendor/stb_truetype.h"
#pragma GCC diagnostic pop

#include "font/atlas_ascii.h"
#include "font/atlas_ascii_15.h"

_Static_assert(sizeof(stbtt_fontinfo) <= FONT_OPAQUE_BYTES, "font blob too small");

#define FONT_ASCII_FIRST 0x20u
#define FONT_ASCII_LAST  0x7Eu

static stbtt_fontinfo *info_of(font_t *f) { return (stbtt_fontinfo *)(void *)f->info; }
static const stbtt_fontinfo *cinfo_of(const font_t *f)
{
    return (const stbtt_fontinfo *)(const void *)f->info;
}

static int32_t round_f(float x)
{
    return x >= 0.0f ? (int32_t)(x + 0.5f) : -(int32_t)(-x + 0.5f);
}

static int cp_valid(uint32_t cp)
{
    return cp <= 0x10FFFFu && !(cp >= 0xD800u && cp <= 0xDFFFu);
}

/* ---- Bounded readers. stb_truetype trusts the file; every byte stb can touch
 * on our paths is validated here first (docs/decisions/P2.3c.md). All bounds
 * are against the full buffer length, in uint64_t arithmetic. ---- */
static int in_buf(size_t len, uint64_t off, uint64_t n)
{
    return off <= len && n <= len - off;
}
static uint32_t rd16(const unsigned char *d, uint64_t o) { return (uint32_t)d[o] << 8 | d[o + 1]; }
static uint32_t rd32(const unsigned char *d, uint64_t o)
{
    return (uint32_t)d[o] << 24 | (uint32_t)d[o + 1] << 16 | (uint32_t)d[o + 2] << 8 | d[o + 3];
}
static int32_t rds16(const unsigned char *d, uint64_t o)
{
    uint32_t v = rd16(d, o);
    return v >= 0x8000u ? (int32_t)v - 0x10000 : (int32_t)v;
}

/* First directory record named tag (as stb picks it); table must lie in the
 * buffer. Returns 1 and the extent, 0 if absent or out of bounds. */
static int find_table(const unsigned char *d, size_t len, uint64_t base, const char *tag,
                      uint64_t *off, uint64_t *tlen)
{
    uint32_t n = rd16(d, base + 4);
    for (uint32_t i = 0; i < n; i++) {
        uint64_t rec = base + 12u + 16u * i;
        if (memcmp(d + rec, tag, 4) != 0) continue;
        *off = rd32(d, rec + 8);
        *tlen = rd32(d, rec + 12);
        return in_buf(len, *off, *tlen);
    }
    return 0;
}

/* Bounded cmap lookup over the subtable stb selected. Formats 0, 4, 6, 12, 13;
 * anything else (or any read outside the buffer) yields glyph 0. */
static uint32_t cmap_lookup(const font_t *f, uint32_t cp)
{
    const unsigned char *d = f->data;
    size_t len = f->len;
    uint64_t im = (uint32_t)cinfo_of(f)->index_map;
    if (!in_buf(len, im, 4)) return 0;
    switch (rd16(d, im)) {
    case 0: {
        uint32_t bytes = rd16(d, im + 2);
        if (bytes >= 6u && cp < bytes - 6u && in_buf(len, im + 6u + cp, 1)) return d[im + 6u + cp];
        return 0;
    }
    case 6: {
        if (!in_buf(len, im, 10)) return 0;
        uint32_t first = rd16(d, im + 6), count = rd16(d, im + 8);
        if (cp < first || cp - first >= count || !in_buf(len, im + 10u + 2u * (cp - first), 2)) return 0;
        return rd16(d, im + 10u + 2u * (cp - first));
    }
    case 4: {
        if (cp > 0xFFFFu || !in_buf(len, im, 14)) return 0;
        uint32_t sc = rd16(d, im + 6) >> 1;
        if (sc == 0 || !in_buf(len, im + 14, UINT64_C(8) * sc + 2u)) return 0;
        uint64_t endc = im + 14, startc = im + 16 + UINT64_C(2) * sc;
        uint64_t delta = im + 16 + UINT64_C(4) * sc, rngo = im + 16 + UINT64_C(6) * sc;
        uint32_t lo = 0, hi = sc;                 /* first segment with end >= cp */
        while (lo < hi) {
            uint32_t mid = lo + (hi - lo) / 2u;
            if (rd16(d, endc + UINT64_C(2) * mid) < cp) lo = mid + 1u; else hi = mid;
        }
        if (lo >= sc) return 0;
        uint32_t start = rd16(d, startc + UINT64_C(2) * lo);
        if (cp < start) return 0;
        uint32_t ro = rd16(d, rngo + UINT64_C(2) * lo), dl = rd16(d, delta + UINT64_C(2) * lo);
        if (ro == 0) return (cp + dl) & 0xFFFFu;
        uint64_t at = rngo + UINT64_C(2) * lo + ro + UINT64_C(2) * (cp - start);
        if (!in_buf(len, at, 2)) return 0;
        uint32_t g = rd16(d, at);
        return g ? (g + dl) & 0xFFFFu : 0u;
    }
    case 12:
    case 13: {
        if (!in_buf(len, im, 16)) return 0;
        uint32_t fmt = rd16(d, im), ng = rd32(d, im + 12);
        if (!in_buf(len, im + 16, UINT64_C(12) * ng)) return 0;
        uint32_t lo = 0, hi = ng;
        while (lo < hi) {
            uint32_t mid = lo + (hi - lo) / 2u;
            if (rd32(d, im + 16 + UINT64_C(12) * mid + 4) < cp) lo = mid + 1u; else hi = mid;
        }
        if (lo >= ng) return 0;
        uint64_t gp = im + 16 + UINT64_C(12) * lo;
        uint32_t sc = rd32(d, gp), sg = rd32(d, gp + 8);
        if (cp < sc) return 0;
        return fmt == 12 ? sg + (cp - sc) : sg;
    }
    default:
        return 0;
    }
}

/* glyf entry [o, e) of glyph g, relative to the glyf table; loca was validated
 * monotonic and inside glyf at init. */
static void glyf_range(const font_t *f, uint32_t g, uint64_t *o, uint64_t *e)
{
    const stbtt_fontinfo *fi = cinfo_of(f);
    const unsigned char *d = f->data;
    if (fi->indexToLocFormat == 0) {
        *o = UINT64_C(2) * rd16(d, (uint64_t)fi->loca + UINT64_C(2) * g);
        *e = UINT64_C(2) * rd16(d, (uint64_t)fi->loca + UINT64_C(2) * g + 2u);
    } else {
        *o = rd32(d, (uint64_t)fi->loca + UINT64_C(4) * g);
        *e = rd32(d, (uint64_t)fi->loca + UINT64_C(4) * g + 4u);
    }
}

/* Validates the TrueType outline of glyph g exactly as stb will walk it:
 * contour end points, instruction skip, flag run-lengths, x/y delta bytes, and
 * composite components (depth and count bounded, XY-offset form only). All
 * reads stay inside the glyph's own loca range. */
#define FONT_COMPOSITE_DEPTH  6
#define FONT_COMPOSITE_BUDGET 256
static int shape_ok(const font_t *f, uint32_t g, int depth, int *budget)
{
    if (g >= f->num_glyphs) return 1;         /* stb returns an empty shape */
    uint64_t o, e;
    glyf_range(f, g, &o, &e);
    if (o == e) return 1;
    if (e - o < 10u) return 0;
    const unsigned char *b = f->data + cinfo_of(f)->glyf + o;
    uint64_t n = e - o;
    int32_t nc = rds16(b, 0);
    if (nc == 0) return 1;
    if (nc > 0) {
        uint64_t p = 10u + UINT64_C(2) * (uint32_t)nc + 2u;
        if (p > n) return 0;
        uint32_t prev = 0, npts = 0;
        for (int32_t i = 0; i < nc; i++) {
            uint32_t ep = rd16(b, 10u + UINT64_C(2) * (uint32_t)i);
            if (i > 0 && ep <= prev) return 0;
            prev = ep;
        }
        npts = prev + 1u;
        uint32_t lastcont = nc > 1 ? rd16(b, 10u + UINT64_C(2) * (uint32_t)(nc - 2)) + 1u : 0u;
        p += rd16(b, 10u + UINT64_C(2) * (uint32_t)nc);   /* instructions */
        uint64_t xb = 0, yb = 0;
        uint32_t fc = 0, flags = 0;
        for (uint32_t i = 0; i < npts; i++) {
            if (fc == 0) {
                if (p >= n) return 0;
                flags = b[p++];
                if (flags & 8u) { if (p >= n) return 0; fc = b[p++]; }
            } else {
                fc--;
            }
            xb += (flags & 2u) ? 1u : ((flags & 16u) ? 0u : 2u);
            yb += (flags & 4u) ? 1u : ((flags & 32u) ? 0u : 2u);
            /* stb reads point i+1 when a contour opens off-curve; a one-point
             * final contour that opens off-curve would read past its array. */
            if (i == npts - 1u && i == lastcont && !(flags & 1u)) return 0;
        }
        return p + xb + yb <= n;
    }
    if (depth >= FONT_COMPOSITE_DEPTH) return 0;
    uint64_t p = 10;
    uint32_t cflags;
    do {
        if (p + 4u > n || --*budget < 0) return 0;
        cflags = rd16(b, p);
        uint32_t gi = rd16(b, p + 2);
        p += 4u;
        if (!(cflags & 2u)) return 0;             /* point matching: stb asserts */
        p += (cflags & 1u) ? 4u : 2u;
        if (cflags & 8u) p += 2u;
        else if (cflags & 0x40u) p += 4u;
        else if (cflags & 0x80u) p += 8u;
        if (p > n) return 0;
        if (!shape_ok(f, gi, depth + 1, budget)) return 0;
    } while (cflags & 0x20u);
    return 1;
}

static int glyph_of(const font_t *f, uint32_t cp, int *g)
{
    if (!cp_valid(cp)) return FONT_ERR_MISSING;
    uint32_t id = cmap_lookup(f, cp);
    if (id == 0) return FONT_ERR_MISSING;
    if (id >= f->num_glyphs) return FONT_ERR_MISSING;
    if (f->glyf_len) {                  /* TrueType outlines: stb reads a 10-byte header for the box */
        uint64_t o, e;
        glyf_range(f, id, &o, &e);
        if (o != e && e - o < 10u) return FONT_ERR_INIT;
    }
    *g = (int)id;
    return FONT_OK;
}

/* Metrics read the outline box. TrueType boxes come from the 10-byte glyph
 * header (validated), but the CFF box runs the charstring interpreter, whose
 * asserts (e.g. a hintmask running off its charstring) are routed to a
 * boundary like the raster path: FONT_ERR_INIT, never a trap. */
static int metrics_of_glyph(const font_t *f, int g, font_metric *m)
{
    int adv = 0, lsb = 0, x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    stbtt_GetGlyphHMetrics(cinfo_of(f), g, &adv, &lsb);
    if (f->glyf_len) {
        stbtt_GetGlyphBitmapBox(cinfo_of(f), g, f->scale, f->scale, &x0, &y0, &x1, &y1);
    } else {
        font_stb_ctx ctx;
        ctx.arena = NULL;
        if (setjmp(ctx.nomem)) {
            font_stb_cur = NULL;
            return FONT_ERR_INIT;
        }
        font_stb_cur = &ctx;
        stbtt_GetGlyphBitmapBox(cinfo_of(f), g, f->scale, f->scale, &x0, &y0, &x1, &y1);
        font_stb_cur = NULL;
    }
    m->advance = round_f((float)adv * f->scale);
    m->bearing_x = x0;
    m->bearing_y = y0;
    m->w = x1 > x0 ? (uint32_t)(x1 - x0) : 0u;
    m->h = y1 > y0 ? (uint32_t)(y1 - y0) : 0u;
    return FONT_OK;
}


/* ---- CFF validation (P2.3e, edit-e6x.15). stb's CFF parser bounds itself by a
 * 512 MB constant instead of the table length and asserts on malformed INDEX /
 * DICT data, so before stbtt_InitFont every structure stb will walk (name, top,
 * string, global subr INDEXes, top DICT operands, Private + local Subrs, the
 * FDArray and FDSelect, the charstrings INDEX) is parsed here, in uint64_t,
 * against the table length, and anything stb could assert on or read outside
 * the table is rejected. Charstring bodies are bounded by their INDEX items. ---- */
typedef struct cff_view { const unsigned char *d; uint64_t n; } cff_view;
#define CFF_MAX_LEN UINT64_C(0x1fffffff)

static uint64_t cff_rdn(const cff_view *c, uint64_t o, uint32_t n)
{
    uint64_t v = 0;
    for (uint32_t i = 0; i < n; i++) v = v << 8 | c->d[o + i];
    return v;
}

/* Validate an INDEX at pos: header, monotone offsets from 1, data inside the table. */
static int cff_index(const cff_view *c, uint64_t pos, uint64_t *end, uint32_t *count)
{
    if (!in_buf(c->n, pos, 2)) return 0;
    uint32_t cnt = rd16(c->d, pos);
    *count = cnt;
    if (cnt == 0) { *end = pos + 2; return 1; }
    if (!in_buf(c->n, pos + 2, 1)) return 0;
    uint32_t os = c->d[pos + 2];
    if (os < 1u || os > 4u) return 0;
    uint64_t offs = pos + 3, tab = (uint64_t)(cnt + 1u) * os;
    if (!in_buf(c->n, offs, tab)) return 0;
    uint64_t prev = 1;
    for (uint32_t i = 0; i <= cnt; i++) {
        uint64_t v = cff_rdn(c, offs + (uint64_t)i * os, os);
        if (v < prev) return 0;
        prev = v;
    }
    if (!in_buf(c->n, offs + tab, prev - 1u)) return 0;
    *end = offs + tab + (prev - 1u);
    return 1;
}

/* Extent of item i of an INDEX already accepted by cff_index (count >= 1). */
static void cff_item(const cff_view *c, uint64_t pos, uint32_t i, uint64_t *a, uint64_t *b)
{
    uint32_t cnt = rd16(c->d, pos), os = c->d[pos + 2];
    uint64_t base = pos + 3 + (uint64_t)(cnt + 1u) * os - 1u;
    *a = base + cff_rdn(c, pos + 3 + (uint64_t)i * os, os);
    *b = base + cff_rdn(c, pos + 3 + (uint64_t)(i + 1u) * os, os);
}

/* Walk DICT [a,b) strictly (no reserved operand bytes, no truncation). If `key`
 * is present, the first nout operands (integers only, as stb reads them) are
 * stored in out[]; *got is how many operands the first match had. */
static int cff_dict(const cff_view *c, uint64_t a, uint64_t b, uint32_t key, uint32_t nout,
                    uint32_t *out, uint32_t *got)
{
    const unsigned char *d = c->d;
    int found = 0;
    uint64_t p = a;
    *got = 0;
    while (p < b) {
        uint32_t vals[2] = { 0, 0 }, k = 0;
        int isi[2] = { 1, 1 };
        while (p < b && d[p] >= 28) {
            uint32_t b0 = d[p], v = 0;
            int isint = 1;
            if (b0 == 30) {
                isint = 0;
                p++;
                for (;;) {
                    if (p >= b) return 0;
                    uint32_t x = d[p++];
                    if ((x & 0xFu) == 0xFu || (x >> 4) == 0xFu) break;
                }
            } else if (b0 == 28) {
                if (!in_buf(b, p, 3)) return 0;
                v = rd16(d, p + 1); p += 3;
            } else if (b0 == 29) {
                if (!in_buf(b, p, 5)) return 0;
                v = rd32(d, p + 1); p += 5;
            } else if (b0 == 31 || b0 == 255) {
                return 0;
            } else if (b0 <= 246) {
                v = b0 - 139u; p++;
            } else {
                if (!in_buf(b, p, 2)) return 0;
                uint32_t b1 = d[p + 1];
                v = b0 <= 250 ? (b0 - 247u) * 256u + b1 + 108u : 0u - (b0 - 251u) * 256u - b1 - 108u;
                p += 2;
            }
            if (k < 2) { vals[k] = v; isi[k] = isint; }
            k++;
        }
        if (p >= b) return 0;
        uint32_t op = d[p++];
        if (op == 12) {
            if (p >= b) return 0;
            op = d[p++] | 0x100u;
        }
        if (op == key && !found) {
            found = 1;
            *got = k < nout ? k : nout;
            for (uint32_t i = 0; i < *got; i++) {
                if (!isi[i]) return 0;               /* stb would feed a real number to cff_int */
                out[i] = vals[i];
            }
        }
    }
    return 1;
}

/* Private DICT named by the Private operand of the font/top DICT [a,b), and its Subrs INDEX. */
static int cff_private(const cff_view *c, uint64_t a, uint64_t b)
{
    uint32_t loc[2] = { 0, 0 }, got = 0, so = 0;
    if (!cff_dict(c, a, b, 18, 2, loc, &got)) return 0;
    if (!loc[0] || !loc[1]) return 1;
    if (!in_buf(c->n, loc[1], loc[0])) return 0;
    if (!cff_dict(c, loc[1], (uint64_t)loc[1] + loc[0], 19, 1, &so, &got)) return 0;
    if (!so) return 1;
    uint64_t at = (uint64_t)loc[1] + so, e;
    uint32_t cnt;
    return cff_index(c, at, &e, &cnt);
}

static int cff_fdselect(const cff_view *c, uint64_t at, uint32_t nfd, uint32_t ng)
{
    if (at >= c->n || nfd == 0) return 0;
    uint32_t fmt = c->d[at];
    if (fmt == 0) {
        if (!in_buf(c->n, at + 1, ng)) return 0;
        for (uint32_t i = 0; i < ng; i++) if (c->d[at + 1 + i] >= nfd) return 0;
        return 1;
    }
    if (fmt != 3 || !in_buf(c->n, at + 1, 2)) return 0;
    uint32_t nr = rd16(c->d, at + 1);
    if (nr == 0 || !in_buf(c->n, at + 3, UINT64_C(3) * nr + 2)) return 0;
    uint32_t start = rd16(c->d, at + 3);
    if (start != 0) return 0;
    for (uint32_t r = 0; r < nr; r++) {
        uint64_t q = at + 3 + UINT64_C(3) * r;
        uint32_t fd = c->d[q + 2], end = rd16(c->d, q + 3);
        if (fd >= nfd || end < start) return 0;
        start = end;
    }
    return start >= ng;
}

static int cff_validate(const unsigned char *cff, uint64_t n, uint32_t ng)
{
    if (n < 4 || n > CFF_MAX_LEN) return 0;
    cff_view c = { cff, n };
    uint64_t p = cff[2], e;
    uint32_t cnt, got;
    if (p < 4 || p > n) return 0;
    if (!cff_index(&c, p, &e, &cnt)) return 0;                     /* Name INDEX */
    p = e;
    if (!cff_index(&c, p, &e, &cnt) || cnt < 1) return 0;          /* Top DICT INDEX */
    uint64_t ta, tb;
    cff_item(&c, p, 0, &ta, &tb);
    p = e;
    if (!cff_index(&c, p, &e, &cnt)) return 0;                     /* String INDEX */
    p = e;
    if (!cff_index(&c, p, &e, &cnt)) return 0;                     /* Global Subrs */

    uint32_t cs = 0, cstype = 2, fda = 0, fdsel = 0;
    if (!cff_dict(&c, ta, tb, 17, 1, &cs, &got)) return 0;
    if (!cff_dict(&c, ta, tb, 0x100u | 6u, 1, &cstype, &got)) return 0;
    if (!cff_dict(&c, ta, tb, 0x100u | 36u, 1, &fda, &got)) return 0;
    if (!cff_dict(&c, ta, tb, 0x100u | 37u, 1, &fdsel, &got)) return 0;
    if (cstype != 2 || cs == 0 || cs >= n) return 0;
    if (!cff_private(&c, ta, tb)) return 0;
    if (fda) {
        if (!fdsel || fda >= n) return 0;
        uint32_t nfd;
        if (!cff_index(&c, fda, &e, &nfd) || nfd == 0) return 0;
        for (uint32_t i = 0; i < nfd; i++) {
            uint64_t fa, fb;
            cff_item(&c, fda, i, &fa, &fb);
            if (!cff_private(&c, fa, fb)) return 0;
        }
        if (!cff_fdselect(&c, fdsel, nfd, ng)) return 0;
    }
    uint32_t ncs;
    if (!cff_index(&c, cs, &e, &ncs) || ncs < ng) return 0;        /* CharStrings: one per glyph */
    return 1;
}

#include "identity.h"

int font_init(font_t *f, const unsigned char *ttf, size_t len)
{
    return font_init_index(f, ttf, len, 0);
}

/* setjmp boundary kept in its own frame so no caller local can be clobbered. */
static int stb_init_guarded(font_t *f, const unsigned char *ttf, int base)
{
    int ok = 0;
    font_stb_ctx ictx;
    ictx.arena = NULL;
    if (setjmp(ictx.nomem) == 0) {
        font_stb_cur = &ictx;
        ok = stbtt_InitFont(info_of(f), ttf, base);
    }
    font_stb_cur = NULL;
    return ok;
}

int font_init_index(font_t *f, const unsigned char *ttf, size_t len, uint32_t index)
{
    if (!f) return FONT_ERR_ARG;
    memset(f, 0, sizeof *f);
    if (!ttf || len < 12) return FONT_ERR_ARG;
    uint64_t base = 0;
    uint32_t sfnt = rd32(ttf, 0);
    if (sfnt == 0x74746366u) {                  /* "ttcf" */
        uint32_t ver = rd32(ttf, 4), nf = rd32(ttf, 8);
        if (ver != 0x00010000u && ver != 0x00020000u) return FONT_ERR_INIT;
        if (nf == 0 || nf > (len - 12u) / 4u || index >= nf) return FONT_ERR_INIT;
        base = rd32(ttf, 12u + UINT64_C(4) * index);
        if (!in_buf(len, base, 12)) return FONT_ERR_INIT;
        sfnt = rd32(ttf, base);
    } else if (index != 0) {
        return FONT_ERR_INIT;
    }
    if (sfnt != 0x00010000u && sfnt != 0x74727565u && sfnt != 0x4F54544Fu && sfnt != 0x74797031u)
        return FONT_ERR_INIT;                   /* 1.0, "true", "OTTO", "typ1" */
    uint32_t nt = rd16(ttf, base + 4);
    if (!in_buf(len, base + 12, UINT64_C(16) * nt)) return FONT_ERR_INIT;

    uint64_t cmap, cmap_n, head, head_n, hhea, hhea_n, hmtx, hmtx_n, maxp, maxp_n;
    if (!find_table(ttf, len, base, "cmap", &cmap, &cmap_n) || cmap_n < 4) return FONT_ERR_INIT;
    if (!find_table(ttf, len, base, "head", &head, &head_n) || head_n < 54) return FONT_ERR_INIT;
    if (!find_table(ttf, len, base, "hhea", &hhea, &hhea_n) || hhea_n < 36) return FONT_ERR_INIT;
    if (!find_table(ttf, len, base, "hmtx", &hmtx, &hmtx_n)) return FONT_ERR_INIT;
    if (!find_table(ttf, len, base, "maxp", &maxp, &maxp_n) || maxp_n < 6) return FONT_ERR_INIT;
    uint64_t glyf = 0, glyf_n = 0, loca = 0, loca_n = 0, cff = 0, cff_n = 0;
    int have_glyf = find_table(ttf, len, base, "glyf", &glyf, &glyf_n);
    int have_loca = find_table(ttf, len, base, "loca", &loca, &loca_n);
    if (have_glyf != have_loca) return FONT_ERR_INIT;
    if (have_glyf && glyf_n == 0) return FONT_ERR_INIT;
    if (!have_glyf && (!find_table(ttf, len, base, "CFF ", &cff, &cff_n) || cff_n < 4))
        return FONT_ERR_INIT;

    uint32_t ng = rd16(ttf, maxp + 4);
    if (ng == 0) return FONT_ERR_INIT;
    if (!have_glyf && !cff_validate(ttf + cff, cff_n, ng)) return FONT_ERR_INIT;
    if (rd16(ttf, head + 18) == 0) return FONT_ERR_INIT;           /* unitsPerEm */
    int32_t asc = rds16(ttf, hhea + 4), dsc = rds16(ttf, hhea + 6);
    if (asc <= 0 || dsc > 0) return FONT_ERR_INIT;                 /* cell_h = asc - dsc > 0 */
    if (cmap_n < 4u + UINT64_C(8) * rd16(ttf, cmap + 2)) return FONT_ERR_INIT;
    uint32_t nhm = rd16(ttf, hhea + 34);
    if (nhm == 0 || nhm > ng || hmtx_n < UINT64_C(4) * nhm + UINT64_C(2) * (ng - nhm)) return FONT_ERR_INIT;
    if (have_glyf) {
        uint32_t fmt = rd16(ttf, head + 50);
        if (fmt > 1u) return FONT_ERR_INIT;
        uint64_t esz = fmt ? 4u : 2u;
        if (loca_n < (ng + UINT64_C(1)) * esz) return FONT_ERR_INIT;
        uint64_t prev = 0;
        for (uint32_t i = 0; i <= ng; i++) {   /* monotonic and inside glyf */
            uint64_t v = fmt ? rd32(ttf, loca + UINT64_C(4) * i) : UINT64_C(2) * rd16(ttf, loca + UINT64_C(2) * i);
            if (v < prev || v > glyf_n) return FONT_ERR_INIT;
            prev = v;
        }
    }

    /* The validators above should make every stb assert unreachable; the
     * boundary is the backstop so a missed case is an init error, not a trap. */
    int stb_ok = stb_init_guarded(f, ttf, (int)base);
    if (!stb_ok) {
        memset(f, 0, sizeof *f);
        return FONT_ERR_INIT;
    }
    info_of(f)->userdata = NULL;
    /* Belt and braces: stb's view must agree with ours on the tables we proved. */
    if (info_of(f)->numGlyphs != (int)ng || (uint64_t)info_of(f)->loca != loca ||
        (uint64_t)info_of(f)->glyf != glyf || info_of(f)->index_map <= 0 ||
        !in_buf(len, (uint32_t)info_of(f)->index_map, 4)) {
        memset(f, 0, sizeof *f);
        return FONT_ERR_INIT;
    }
    f->data = ttf;
    f->len = len;
    f->face_index = index;
    f->glyf_len = (uint32_t)glyf_n;
    f->num_glyphs = ng;
    f->bake_identity = (uint32_t)font_identity_matches(ttf, len, index);
    return FONT_OK;
}

static const font_ascii_atlas *atlas_matching(const font_t *f);

int font_set_px(font_t *f, uint32_t px)
{
    if (!f || !f->data || px == 0 || px > 4096u) return FONT_ERR_ARG;
    int a = 0, d = 0, gap = 0;
    f->px = px;
    f->scale = stbtt_ScaleForPixelHeight(cinfo_of(f), (float)px);
    stbtt_GetFontVMetrics(cinfo_of(f), &a, &d, &gap);
    int32_t asc = round_f((float)a * f->scale);
    int32_t dsc = round_f((float)(-d) * f->scale);
    f->cell.ascent = (uint32_t)asc;
    f->cell.descent = (uint32_t)dsc;
    f->cell.cell_h = (uint32_t)(asc + dsc);
    uint32_t maxadv = 0;
    for (uint32_t cp = FONT_ASCII_FIRST; cp <= FONT_ASCII_LAST; cp++) {
        int g = 0;
        font_metric m;
        if (glyph_of(f, cp, &g) != FONT_OK) continue;
        if (metrics_of_glyph(f, g, &m) != FONT_OK) continue;
        if (m.advance > 0 && (uint32_t)m.advance > maxadv) maxadv = (uint32_t)m.advance;
    }
    f->cell.cell_w = maxadv;
    f->atlas = atlas_matching(f);
    return FONT_OK;
}

int font_cell_metrics(const font_t *f, font_cell *out)
{
    if (!f || !out || !f->data || f->px == 0) return FONT_ERR_ARG;
    *out = f->cell;
    return FONT_OK;
}

int font_glyph_metrics(const font_t *f, uint32_t cp, font_metric *out)
{
    int g = 0;
    if (!f || !out || !f->data || f->px == 0) return FONT_ERR_ARG;
    int r = glyph_of(f, cp, &g);
    if (r != FONT_OK) return r;
    return metrics_of_glyph(f, g, out);
}

int font_raster_glyph(font_t *f, uint32_t cp, edit_arena *arena, font_bitmap *out)
{
    int g = 0;
    if (!f || !arena || !out || !f->data || f->px == 0) return FONT_ERR_ARG;
    out->pixels = NULL;
    out->w = 0;
    out->h = 0;
    memset(&out->m, 0, sizeof out->m);
    int r = glyph_of(f, cp, &g);
    if (r != FONT_OK) return r;
    if (f->glyf_len) {      /* stb walks the outline unchecked: prove it in-bounds first */
        int budget = FONT_COMPOSITE_BUDGET;
        if (!shape_ok(f, (uint32_t)g, 0, &budget)) return FONT_ERR_INIT;
    }
    r = metrics_of_glyph(f, g, &out->m);
    if (r != FONT_OK) { out->m.w = out->m.h = 0; return r; }
    out->w = out->m.w;
    out->h = out->m.h;
    if (out->w == 0 || out->h == 0) return FONT_OK;
    if ((size_t)out->w > SIZE_MAX / out->h) return FONT_ERR_NOMEM;
    edit_arena_mark_t start = edit_arena_mark(arena);
    uint8_t *px = edit_arena_alloc(arena, (size_t)out->w * out->h, 1);
    if (!px) return FONT_ERR_NOMEM;
    memset(px, 0, (size_t)out->w * out->h);
    /* stb scratch (vertices, edges, scanlines) comes from the same arena after
     * the bitmap and is released by rewinding to this mark. */
    edit_arena_mark_t mk = edit_arena_mark(arena);
    font_stb_ctx ctx;
    ctx.arena = arena;
    /* All stb-owned storage is in this arena and STBTT_free is a no-op, so
     * unwinding on allocation failure needs only the arena rewind. start is
     * unchanged after setjmp; do not read modified automatic locals here. */
    int fail = 0;
    switch (setjmp(ctx.nomem)) {
    case 0:
        break;
    case 2:
        fail = FONT_ERR_INIT;      /* stb assertion: inconsistent outline */
        /* fallthrough */
    default:
        if (!fail) fail = FONT_ERR_NOMEM;
        font_stb_cur = NULL;
        info_of(f)->userdata = NULL;
        edit_arena_reset_to_mark(arena, start);
        return fail;
    }
    font_stb_cur = &ctx;
    info_of(f)->userdata = &ctx;
    /* stb writes a box-sized bitmap whose origin is the box's top-left. */
    stbtt_MakeGlyphBitmap(cinfo_of(f), px, (int)out->w, (int)out->h, (int)out->w,
                          f->scale, f->scale, g);
    font_stb_cur = NULL;
    info_of(f)->userdata = NULL;
    edit_arena_reset_to_mark(arena, mk);
    out->pixels = px;
    return FONT_OK;
}

int font_place_in_cell(const font_t *f, const font_bitmap *b, uint8_t *cell,
                       uint32_t cell_w, uint32_t cell_h)
{
    if (!f || !b || !cell || f->px == 0) return FONT_ERR_ARG;
    if (!b->pixels) return FONT_OK;
    for (uint32_t y = 0; y < b->h; y++) {
        int64_t cy = (int64_t)f->cell.ascent + b->m.bearing_y + (int64_t)y;
        if (cy < 0 || cy >= (int64_t)cell_h) continue;
        for (uint32_t x = 0; x < b->w; x++) {
            int64_t cx = (int64_t)b->m.bearing_x + (int64_t)x;
            if (cx < 0 || cx >= (int64_t)cell_w) continue;
            cell[(size_t)cy * cell_w + (size_t)cx] = b->pixels[(size_t)y * b->w + x];
        }
    }
    return FONT_OK;
}

static const font_ascii_atlas atlases[2] = {
    { 15u, { FONT_ATLAS_CELL_W_15, FONT_ATLAS_CELL_H_15, FONT_ATLAS_ASCENT_15,
             FONT_ATLAS_CELL_H_15 - FONT_ATLAS_ASCENT_15 },
      font_atlas_metrics_15, font_atlas_pixels_15, sizeof font_atlas_pixels_15 },
    { 30u, { FONT_ATLAS_CELL_W, FONT_ATLAS_CELL_H, FONT_ATLAS_ASCENT,
             FONT_ATLAS_CELL_H - FONT_ATLAS_ASCENT },
      font_atlas_metrics, font_atlas_pixels, sizeof font_atlas_pixels },
};

const font_ascii_atlas *font_ascii_atlas_for_px(uint32_t px)
{
    for (size_t i = 0; i < 2; i++)
        if (atlases[i].px == px) return &atlases[i];
    return NULL;
}

const font_metric *font_ascii_atlas_glyph(const font_ascii_atlas *a, uint32_t cp)
{
    if (!a || cp < FONT_ASCII_FIRST || cp > FONT_ASCII_LAST) return NULL;
    return &a->metrics[cp - FONT_ASCII_FIRST];
}

/* Content/face identity is required; metrics additionally catch stale bakes. */
static const font_ascii_atlas *atlas_matching(const font_t *f)
{
    const font_ascii_atlas *a = font_ascii_atlas_for_px(f->px);
    if (!a || !f->bake_identity) return NULL;
    if (a->cell.cell_w != f->cell.cell_w || a->cell.cell_h != f->cell.cell_h ||
        a->cell.ascent != f->cell.ascent)
        return NULL;
    for (uint32_t cp = FONT_ASCII_FIRST; cp <= FONT_ASCII_LAST; cp++) {
        int g = 0;
        font_metric m;
        const font_metric *b = &a->metrics[cp - FONT_ASCII_FIRST];
        if (glyph_of(f, cp, &g) != FONT_OK) return NULL;
        if (metrics_of_glyph(f, g, &m) != FONT_OK) return NULL;
        if (m.advance != b->advance || m.bearing_x != b->bearing_x ||
            m.bearing_y != b->bearing_y || m.w != b->w || m.h != b->h)
            return NULL;
    }
    return a;
}

const font_metric *font_ascii_glyph(uint32_t cp)
{
    return font_ascii_atlas_glyph(&atlases[1], cp);
}

const uint8_t *font_ascii_pixels(size_t *len)
{
    if (len) *len = atlases[1].pixels_len;
    return atlases[1].pixels;
}

font_cell font_ascii_cell(void) { return atlases[1].cell; }

uint32_t font_ascii_px(void) { return atlases[1].px; }

void font_atlas_init(font_atlas *a, uint32_t max_pages)
{
    memset(a, 0, sizeof *a);
    a->max_pages = max_pages > FONT_ATLAS_MAX_PAGES ? FONT_ATLAS_MAX_PAGES : max_pages;
}

static void place(font_atlas *a, uint32_t p, uint32_t w, uint32_t h,
                  uint32_t *page, uint32_t *x, uint32_t *y, int new_shelf)
{
    font_atlas_page *pg = &a->pages[p];
    if (new_shelf) {
        pg->shelf_y = pg->used_y;
        pg->shelf_h = h;
        pg->cur_x = 0;
        pg->used_y += h;
    }
    *page = p;
    *x = pg->cur_x;
    *y = pg->shelf_y;
    pg->cur_x += w;
}

int font_atlas_alloc(font_atlas *a, uint32_t w, uint32_t h,
                     uint32_t *page, uint32_t *x, uint32_t *y)
{
    if (!a || !page || !x || !y) return FONT_ERR_ARG;
    if (w == 0 || h == 0 || w > FONT_ATLAS_PAGE_DIM || h > FONT_ATLAS_PAGE_DIM)
        return FONT_ERR_NOMEM;
    for (uint32_t p = 0; p < a->npages; p++) {
        font_atlas_page *pg = &a->pages[p];
        if (h <= pg->shelf_h && pg->cur_x + w <= FONT_ATLAS_PAGE_DIM) {
            place(a, p, w, h, page, x, y, 0);
            return FONT_OK;
        }
        if (pg->used_y + h <= FONT_ATLAS_PAGE_DIM) {
            place(a, p, w, h, page, x, y, 1);
            return FONT_OK;
        }
    }
    if (a->npages < a->max_pages) {
        uint32_t p = a->npages++;
        place(a, p, w, h, page, x, y, 1);
        return FONT_OK;
    }
    return FONT_ERR_NOMEM;
}
