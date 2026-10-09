/* fuzz/font_cffseed.h - P2.3e (edit-e6x.15): synthetic minimal OpenType/CFF
 * fonts shared by tests/font_test.c (hostile CFF cases) and fuzz/font_fuzz.c
 * (seed corpus). No file is vendored; the bytes are generated here.
 *
 * Three variants, 3 glyphs each (.notdef, 'A'=gid 1, 'B'=gid 2), cmap fmt 6:
 *   0  Type 2 charstrings, Private DICT + local Subrs (glyph B calls subr 0)
 *   1  "subr bomb": 10 local subrs, subr k calls subr k+1 twenty times, so a
 *      glyph B costs 20^9 steps unless the interpreter has a budget
 *   2  CID-keyed: ROS, FDArray (1 font dict), FDSelect fmt 3, per-FD Private
 * The CFF table is the last table so the buffer ends exactly at its end. */
#ifndef FONT_CFFSEED_H
#define FONT_CFFSEED_H
#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef struct cffseed_layout {
    size_t cff_off, cff_len;     /* CFF table inside the font */
    size_t rec_cff;              /* directory record of the CFF table */
    size_t cs_off_pos;           /* top DICT charstrings operand (5-byte int) */
    size_t cs_idx;               /* charstrings INDEX */
    size_t subr_idx;             /* local subrs INDEX (0 if none) */
    size_t priv_pos;             /* Private DICT */
    size_t fdsel_pos, fda_pos;   /* CID only, else 0 */
} cffseed_layout;

typedef struct cffseed_w { uint8_t *b; size_t n, cap; int bad; } cffseed_w;
static inline void cs_w8(cffseed_w *w, unsigned v)
{
    if (w->n < w->cap) w->b[w->n] = (uint8_t)v; else w->bad = 1;
    w->n++;
}
static inline void cs_w16(cffseed_w *w, unsigned v) { cs_w8(w, v >> 8); cs_w8(w, v & 255u); }
static inline void cs_w32(cffseed_w *w, uint32_t v) { cs_w16(w, v >> 16); cs_w16(w, v & 0xffffu); }
static inline void cs_int5(cffseed_w *w, uint32_t v) { cs_w8(w, 29); cs_w32(w, v); }
static inline void cs_wb(cffseed_w *w, const uint8_t *p, size_t n) { for (size_t i = 0; i < n; i++) cs_w8(w, p[i]); }

/* INDEX of items stored back to back in data[], lens[i] each. */
static inline void cs_index(cffseed_w *w, size_t count, const uint8_t *data, const size_t *lens, unsigned offsize)
{
    cs_w16(w, (unsigned)count);
    if (!count) return;
    cs_w8(w, offsize);
    size_t o = 1;
    for (size_t i = 0; i <= count; i++) {
        for (unsigned k = offsize; k-- > 0;) cs_w8(w, (unsigned)(o >> (8 * k)) & 255u);
        if (i < count) o += lens[i];
    }
    size_t tot = 0;
    for (size_t i = 0; i < count; i++) tot += lens[i];
    cs_wb(w, data, tot);
}

static inline size_t cffseed_build(uint8_t *out, size_t cap, int variant, cffseed_layout *lay)
{
    cffseed_layout L;
    memset(&L, 0, sizeof L);
    uint8_t csd[512], sub[512];
    size_t cl[3], sl[10], nsub = 1, sdl = 0, cdl = 0;
    static const uint8_t g0[] = { 0x0E };
    static const uint8_t g1[] = { 159, 159, 0x15, 239, 139, 139, 239, 39, 39, 0x05, 0x0E };
    static const uint8_t g2[] = { 32, 0x0A, 0x0E };
    static const uint8_t s0[] = { 159, 159, 0x15, 189, 139, 139, 189, 89, 89, 0x05, 0x0B };
    memcpy(csd, g0, sizeof g0); cl[0] = sizeof g0; cdl = sizeof g0;
    memcpy(csd + cdl, g1, sizeof g1); cl[1] = sizeof g1; cdl += sizeof g1;
    memcpy(csd + cdl, g2, sizeof g2); cl[2] = sizeof g2; cdl += sizeof g2;
    if (variant == 1) {
        nsub = 10;
        for (size_t k = 0; k < 10; k++) {
            sl[k] = 0;
            if (k < 9) for (int c = 0; c < 20; c++) { sub[sdl++] = (uint8_t)(k + 1 + 32); sub[sdl++] = 0x0A; sl[k] += 2; }
            sub[sdl++] = 0x0B; sl[k] += 1;
        }
    } else {
        memcpy(sub, s0, sizeof s0); sl[0] = sizeof s0; sdl = sizeof s0;
    }
    uint8_t cs_i[600], sub_i[600], fdsel[8];
    cffseed_w wc = { cs_i, 0, sizeof cs_i, 0 }, ws = { sub_i, 0, sizeof sub_i, 0 };
    cs_index(&wc, 3, csd, cl, 2);
    cs_index(&ws, nsub, sub, sl, 2);
    cffseed_w wf = { fdsel, 0, sizeof fdsel, 0 };
    cs_w8(&wf, 3); cs_w16(&wf, 1); cs_w16(&wf, 0); cs_w8(&wf, 0); cs_w16(&wf, 3);

    uint8_t cff[2048];
    cffseed_w w = { cff, 0, sizeof cff, 0 };
    cs_w8(&w, 1); cs_w8(&w, 0); cs_w8(&w, 4); cs_w8(&w, 1);
    { const uint8_t nm[] = { 'X' }; const size_t nl[] = { 1 }; cs_index(&w, 1, nm, nl, 1); }
    size_t dictlen = variant == 2 ? 25u : 17u;
    size_t head_len = 4 + 6 + (2 + 1 + 2 + dictlen) + 2 + 2;
    size_t cs_at = head_len;
    size_t priv_at, subr_at, fdsel_at = 0, fda_at = 0;
    if (variant == 2) {
        fdsel_at = cs_at + wc.n;
        fda_at = fdsel_at + sizeof fdsel;
        priv_at = fda_at + (2 + 1 + 2 + 11);
    } else {
        priv_at = cs_at + wc.n;
    }
    subr_at = priv_at + 6;
    L.cs_off_pos = 0; /* patched below */
    /* top DICT INDEX */
    cs_w16(&w, 1); cs_w8(&w, 1); cs_w8(&w, 1); cs_w8(&w, (unsigned)(1 + dictlen));
    if (variant == 2) {
        cs_w8(&w, 139); cs_w8(&w, 139); cs_w8(&w, 139); cs_w8(&w, 12); cs_w8(&w, 30);   /* ROS */
        cs_int5(&w, (uint32_t)fda_at); cs_w8(&w, 12); cs_w8(&w, 36);
        cs_int5(&w, (uint32_t)fdsel_at); cs_w8(&w, 12); cs_w8(&w, 37);
        L.cs_off_pos = w.n + 1;
        cs_int5(&w, (uint32_t)cs_at); cs_w8(&w, 17);
    } else {
        L.cs_off_pos = w.n + 1;
        cs_int5(&w, (uint32_t)cs_at); cs_w8(&w, 17);
        cs_int5(&w, 6); cs_int5(&w, (uint32_t)priv_at); cs_w8(&w, 18);
    }
    cs_w16(&w, 0);   /* string INDEX */
    cs_w16(&w, 0);   /* global subrs */
    L.cs_idx = w.n;
    cs_wb(&w, cs_i, wc.n);
    if (variant == 2) {
        L.fdsel_pos = w.n;
        cs_wb(&w, fdsel, sizeof fdsel);
        L.fda_pos = w.n;
        { /* FDArray: one font dict with Private (size 6, offset priv_at) */
            cs_w16(&w, 1); cs_w8(&w, 1); cs_w8(&w, 1); cs_w8(&w, 12);
            cs_int5(&w, 6); cs_int5(&w, (uint32_t)priv_at); cs_w8(&w, 18);
        }
    }
    L.priv_pos = w.n;
    cs_int5(&w, 6); cs_w8(&w, 19);                       /* Subrs at +6 */
    L.subr_idx = w.n;
    if (L.priv_pos != priv_at || L.subr_idx != subr_at || L.cs_idx != cs_at) return 0;
    cs_wb(&w, sub_i, ws.n);
    if (w.bad || wc.bad || ws.bad || wf.bad) return 0;
    size_t cff_len = w.n;

    /* sfnt */
    uint8_t head[54], hhea[36], hmtx[12], maxp[6];
    memset(head, 0, sizeof head); memset(hhea, 0, sizeof hhea); memset(hmtx, 0, sizeof hmtx);
    uint8_t cm[26];
    cffseed_w tc = { cm, 0, sizeof cm, 0 };
    cs_w16(&tc, 0); cs_w16(&tc, 1); cs_w16(&tc, 3); cs_w16(&tc, 1); cs_w32(&tc, 12);
    cs_w16(&tc, 6); cs_w16(&tc, 14); cs_w16(&tc, 0); cs_w16(&tc, 65); cs_w16(&tc, 2); cs_w16(&tc, 1); cs_w16(&tc, 2);
    head[18] = 0x03; head[19] = 0xE8;                    /* unitsPerEm 1000 */
    hhea[4] = 0x03; hhea[5] = 0x20;                      /* ascent 800 */
    hhea[6] = 0xFF; hhea[7] = 0x38;                      /* descent -200 */
    hhea[35] = 3;                                        /* numberOfHMetrics */
    for (int i = 0; i < 3; i++) { hmtx[4 * i] = 0x01; hmtx[4 * i + 1] = 0xF4; }
    maxp[0] = 0; maxp[1] = 0; maxp[2] = 0x50; maxp[3] = 0; maxp[4] = 0; maxp[5] = 3;

    const struct { const char *tag; const uint8_t *p; size_t n; } tabs[6] = {
        { "cmap", cm, sizeof cm }, { "head", head, sizeof head }, { "hhea", hhea, sizeof hhea },
        { "hmtx", hmtx, sizeof hmtx }, { "maxp", maxp, sizeof maxp }, { "CFF ", cff, cff_len } };
    size_t at = 12 + 16 * 6;
    size_t offs[6];
    for (int i = 0; i < 6; i++) { offs[i] = at; at += tabs[i].n; if (i < 5) at = (at + 3u) & ~(size_t)3; }
    if (at > cap) return 0;
    memset(out, 0, at);
    cffseed_w o = { out, 0, cap, 0 };
    cs_w32(&o, 0x4F54544Fu); cs_w16(&o, 6); cs_w16(&o, 64); cs_w16(&o, 2); cs_w16(&o, 32);
    static const int order[6] = { 5, 0, 1, 2, 3, 4 };      /* sorted by tag: "CFF " first */
    for (int k = 0; k < 6; k++) {
        int i = order[k];
        for (int c = 0; c < 4; c++) cs_w8(&o, (uint8_t)tabs[i].tag[c]);
        if (i == 5) L.rec_cff = o.n;
        cs_w32(&o, 0); cs_w32(&o, (uint32_t)offs[i]); cs_w32(&o, (uint32_t)tabs[i].n);
    }
    if (L.rec_cff) L.rec_cff -= 4;
    for (int i = 0; i < 6; i++) memcpy(out + offs[i], tabs[i].p, tabs[i].n);
    L.cff_off = offs[5];
    L.cff_len = cff_len;
    L.cs_off_pos += L.cff_off;
    L.cs_idx += L.cff_off; L.subr_idx += L.cff_off; L.priv_pos += L.cff_off;
    if (L.fdsel_pos) { L.fdsel_pos += L.cff_off; L.fda_pos += L.cff_off; }
    if (lay) *lay = L;
    return at;
}
#endif
