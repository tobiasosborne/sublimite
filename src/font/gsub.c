/* src/font/gsub.c - bounded GSUB subset for emoji clusters (edit-v16).
 * docs/decisions/edit-v16.md. Every read is proven against the table length
 * twice: once by the validator (font_gsub_parse, which disables GSUB for the
 * face on any failure) and again at apply time, so a validator gap degrades
 * to "no substitution", never to an out-of-bounds read. */
#include "font/font.h"
#include "utf8/utf8.h"
#include <string.h>

/* (E) Validator budget: a table whose subtables are not shared is walked once,
 * about one step per 16-bit word (len / 2). 4x that admits legitimate sharing
 * (NotoEmoji-wght measured in the decision doc); a table needing more is
 * treated as malformed. Bounds parse time by the table length on hostile
 * input (offset aliasing). */
#define GSUB_PARSE_FACTOR 4u
/* (E) Per-cluster apply budget: 4x the worst work measured on the fixture's
 * heaviest 10-glyph arrays, 5990 ops (M, font_gsub_test 2026-10-16: ten
 * woman glyphs, 392 ligatures each): 4 * 5990 = 23960. Exhaustion means "no
 * substitution": the r8e projection is drawn. */
#define GSUB_APPLY_BUDGET 23960u

enum { TAG_DFLT = 0x44464c54u, TAG_LATN = 0x6c61746eu, TAG_CCMP = 0x63636d70u,
       TAG_LIGA = 0x6c696761u, TAG_RLIG = 0x726c6967u };

typedef struct view {
    const uint8_t *t;
    uint64_t len, reads, budget;
    uint32_t lookup_list, lookup_count;
} view;

static int has(const view *v, uint64_t off, uint64_t n) { return off <= v->len && n <= v->len - off; }
static uint32_t u16(const view *v, uint64_t o) { return (uint32_t)v->t[o] << 8 | v->t[o + 1u]; }
static uint32_t u32(const view *v, uint64_t o) { return u16(v, o) << 16 | u16(v, o + 2u); }
static int spend(view *v, uint64_t n) { v->reads += n; return v->reads <= v->budget; }

/* ---------------------------------------------------------- validation */

static int coverage_ok(view *v, uint64_t at)
{
    if (!has(v, at, 4)) return 0;
    uint32_t fmt = u16(v, at), count = u16(v, at + 2u);
    if (!spend(v, 2u + count)) return 0;
    if (fmt == 1u) {
        if (!has(v, at + 4u, UINT64_C(2) * count)) return 0;
        for (uint32_t i = 1; i < count; i++)
            if (u16(v, at + 4u + 2u * i) <= u16(v, at + 2u + 2u * i)) return 0;
        return 1;
    }
    if (fmt == 2u) {
        if (!has(v, at + 4u, UINT64_C(6) * count)) return 0;
        for (uint32_t i = 0; i < count; i++) {
            uint64_t r = at + 4u + UINT64_C(6) * i;
            if (u16(v, r) > u16(v, r + 2u)) return 0;
            if (i && u16(v, r) <= u16(v, r - 4u)) return 0; /* after previous end */
        }
        return 1;
    }
    return 0;
}

/* Resolve subtable k of the lookup at `lookup`: type 7 unwrapped. */
static int subtable(const view *v, uint64_t lookup, uint32_t k, uint32_t *type, uint64_t *at)
{
    if (!has(v, lookup, 6)) return 0;
    uint32_t count = u16(v, lookup + 4u);
    if (k >= count || !has(v, lookup + 6u, UINT64_C(2) * count)) return 0;
    *type = u16(v, lookup);
    *at = lookup + u16(v, lookup + 6u + UINT64_C(2) * k);
    if (*type != 7u) return 1;
    if (!has(v, *at, 8) || u16(v, *at) != 1u) return 0;
    *type = u16(v, *at + 2u);
    *at += u32(v, *at + 4u);
    return *type != 7u;
}

static int lookup_at(const view *v, uint32_t index, uint64_t *at)
{
    if (index >= v->lookup_count) return 0;
    *at = v->lookup_list + u16(v, v->lookup_list + 2u + UINT64_C(2) * index);
    return has(v, *at, 6);
}

/* Unwrapped type of a lookup (0 when unreadable or inconsistent). */
static uint32_t lookup_type(view *v, uint64_t lookup)
{
    uint32_t count = u16(v, lookup + 4u), type = 0, first = 0;
    if (!spend(v, count)) return 0;
    if (!count) return u16(v, lookup) == 7u ? 0u : u16(v, lookup);
    for (uint32_t k = 0; k < count; k++) {
        uint64_t at;
        if (!subtable(v, lookup, k, &type, &at)) return 0;
        if (k == 0) first = type;
        else if (type != first) return 0;
    }
    return first;
}

/* Lookup flags we can honour without GDEF: only RightToLeft (bit 0, GPOS
 * cursive only). Ignore/mark-filtering lookups are skipped. */
static int flags_supported(const view *v, uint64_t lookup) { return (u16(v, lookup + 2u) & ~1u) == 0u; }

static int single_ok(view *v, uint64_t at)
{
    if (!has(v, at, 6) || !coverage_ok(v, at + u16(v, at + 2u))) return 0;
    uint32_t fmt = u16(v, at);
    if (fmt == 1u) return 1;
    return fmt == 2u && has(v, at + 6u, UINT64_C(2) * u16(v, at + 4u)) && spend(v, u16(v, at + 4u));
}

static int multiple_ok(view *v, uint64_t at)
{
    if (!has(v, at, 6) || u16(v, at) != 1u || !coverage_ok(v, at + u16(v, at + 2u))) return 0;
    uint32_t count = u16(v, at + 4u);
    if (!has(v, at + 6u, UINT64_C(2) * count) || !spend(v, count)) return 0;
    for (uint32_t i = 0; i < count; i++) {
        uint64_t seq = at + u16(v, at + 6u + UINT64_C(2) * i);
        if (!has(v, seq, 2) || !has(v, seq + 2u, UINT64_C(2) * u16(v, seq)) || !spend(v, u16(v, seq))) return 0;
    }
    return 1;
}

static int ligature_ok(view *v, uint64_t at)
{
    if (!has(v, at, 6) || u16(v, at) != 1u || !coverage_ok(v, at + u16(v, at + 2u))) return 0;
    uint32_t sets = u16(v, at + 4u);
    if (!has(v, at + 6u, UINT64_C(2) * sets) || !spend(v, sets)) return 0;
    for (uint32_t i = 0; i < sets; i++) {
        uint64_t set = at + u16(v, at + 6u + UINT64_C(2) * i);
        if (!has(v, set, 2)) return 0;
        uint32_t ligs = u16(v, set);
        if (!has(v, set + 2u, UINT64_C(2) * ligs) || !spend(v, ligs)) return 0;
        for (uint32_t j = 0; j < ligs; j++) {
            uint64_t lig = set + u16(v, set + 2u + UINT64_C(2) * j);
            if (!has(v, lig, 4)) return 0;
            uint32_t comps = u16(v, lig + 2u);
            if (!comps || !has(v, lig + 4u, UINT64_C(2) * (comps - 1u)) || !spend(v, comps)) return 0;
        }
    }
    return 1;
}

static int lookup_ok(view *v, uint32_t index, int nested);

/* Chained contextual format 3 (the only format NotoEmoji uses, (M)). */
static int chain3_ok(view *v, uint64_t at)
{
    uint64_t p = at + 2u;
    for (int part = 0; part < 3; part++) {
        if (!has(v, p, 2)) return 0;
        uint32_t count = u16(v, p);
        if ((part == 1 && !count) || !has(v, p + 2u, UINT64_C(2) * count)) return 0;
        for (uint32_t i = 0; i < count; i++)
            if (!coverage_ok(v, at + u16(v, p + 2u + UINT64_C(2) * i))) return 0;
        p += 2u + UINT64_C(2) * count;
    }
    if (!has(v, p, 2)) return 0;
    uint32_t records = u16(v, p);
    if (!has(v, p + 2u, UINT64_C(4) * records) || !spend(v, records)) return 0;
    for (uint32_t i = 0; i < records; i++)
        if (!lookup_ok(v, u16(v, p + 4u + UINT64_C(4) * i), 1)) return 0;
    return 1;
}

/* nested: a lookup named by a chain record (types 1, 2, 4 applied there). */
static int lookup_ok(view *v, uint32_t index, int nested)
{
    uint64_t lookup;
    if (!lookup_at(v, index, &lookup)) return 0;
    uint32_t count = u16(v, lookup + 4u);
    if (!has(v, lookup + 6u, UINT64_C(2) * count) || !spend(v, 3u + count)) return 0;
    uint32_t type = lookup_type(v, lookup);
    if (!type) return 0;
    if (!flags_supported(v, lookup)) return 1;  /* skipped, never read */
    for (uint32_t k = 0; k < count; k++) {
        uint64_t at; uint32_t t;
        if (!subtable(v, lookup, k, &t, &at) || !has(v, at, 2)) return 0;
        uint32_t fmt = u16(v, at);
        int ok = 1;
        if (t == 4u) ok = ligature_ok(v, at);
        else if (nested && t == 1u) ok = single_ok(v, at);
        else if (nested && t == 2u) ok = multiple_ok(v, at);
        else if (!nested && t == 6u) ok = fmt == 3u ? chain3_ok(v, at) : fmt == 1u || fmt == 2u;
        if (!ok) return 0;
    }
    return 1;
}

static int insert_lookup(uint16_t *set, size_t *n, size_t cap, uint32_t index)
{
    size_t at = 0;
    while (at < *n && set[at] < index) at++;
    if (at < *n && set[at] == index) return 1;
    if (*n == cap) return 0;
    memmove(set + at + 1u, set + at, (*n - at) * sizeof *set);
    set[at] = (uint16_t)index; (*n)++;
    return 1;
}

static int feature_wanted(uint32_t tag) { return tag == TAG_CCMP || tag == TAG_LIGA || tag == TAG_RLIG; }

static int parse(font_gsub *g, view *v, font_gsub_stats *st)
{
    if (!has(v, 0, 10) || u16(v, 0) != 1u) return FONT_ERR_INIT;
    uint64_t scripts = u16(v, 4), features = u16(v, 6), lookups = u16(v, 8);
    if (!has(v, scripts, 2) || !has(v, features, 2) || !has(v, lookups, 2)) return FONT_ERR_INIT;
    uint32_t nscripts = u16(v, scripts), nfeatures = u16(v, features);
    v->lookup_list = (uint32_t)lookups; v->lookup_count = u16(v, lookups);
    if (!has(v, scripts + 2u, UINT64_C(6) * nscripts) || !has(v, features + 2u, UINT64_C(6) * nfeatures) ||
        !has(v, lookups + 2u, UINT64_C(2) * v->lookup_count) ||
        !spend(v, 5u + 3u * (uint64_t)nscripts + 3u * (uint64_t)nfeatures + v->lookup_count)) return FONT_ERR_INIT;
    st->lookups = v->lookup_count;
    for (uint32_t i = 0; i < v->lookup_count; i++) {
        uint64_t lookup;
        uint32_t type = lookup_at(v, i, &lookup) && has(v, lookup + 6u, UINT64_C(2) * u16(v, lookup + 4u))
            ? lookup_type(v, lookup) : 0u;
        st->by_type[type < 9u ? type : 0u]++;
        if (!spend(v, 3u)) return FONT_ERR_INIT;
    }
    uint64_t script = 0;
    for (uint32_t want = 0; want < 2 && !script; want++)
        for (uint32_t i = 0; i < nscripts; i++)
            if (u32(v, scripts + 2u + UINT64_C(6) * i) == (want ? TAG_LATN : TAG_DFLT)) {
                script = scripts + u16(v, scripts + 6u + UINT64_C(6) * i);
                break;
            }
    if (!script) return FONT_ERR_MISSING;
    if (!has(v, script, 4)) return FONT_ERR_INIT;
    if (!u16(v, script)) return FONT_ERR_MISSING;
    uint64_t langsys = script + u16(v, script);
    if (!has(v, langsys, 6)) return FONT_ERR_INIT;
    uint32_t required = u16(v, langsys + 2u), nindices = u16(v, langsys + 4u);
    if (!has(v, langsys + 6u, UINT64_C(2) * nindices) || !spend(v, nindices)) return FONT_ERR_INIT;
    uint16_t enabled[FONT_GSUB_MAX_LOOKUPS]; size_t nenabled = 0;
    for (uint32_t i = 0; i <= nindices; i++) {
        uint32_t feature = i < nindices ? u16(v, langsys + 6u + UINT64_C(2) * i) : required;
        if (i == nindices && required == 0xffffu) break;
        if (feature >= nfeatures) return FONT_ERR_INIT;
        uint64_t record = features + 2u + UINT64_C(6) * feature;
        if (i < nindices && !feature_wanted(u32(v, record))) continue;
        uint64_t table = features + u16(v, record + 4u);
        if (!has(v, table, 4)) return FONT_ERR_INIT;
        uint32_t count = u16(v, table + 2u);
        if (!has(v, table + 4u, UINT64_C(2) * count) || !spend(v, count)) return FONT_ERR_INIT;
        for (uint32_t k = 0; k < count; k++) {
            uint32_t index = u16(v, table + 4u + UINT64_C(2) * k);
            if (index >= v->lookup_count) return FONT_ERR_INIT;
            if (!insert_lookup(enabled, &nenabled, FONT_GSUB_MAX_LOOKUPS, index)) return FONT_ERR_INIT;
        }
    }
    st->enabled = (uint32_t)nenabled;
    g->count = 0;
    for (size_t i = 0; i < nenabled; i++) {
        uint64_t lookup;
        if (!lookup_at(v, enabled[i], &lookup) || !has(v, lookup + 6u, UINT64_C(2) * u16(v, lookup + 4u)))
            return FONT_ERR_INIT;
        uint32_t type = lookup_type(v, lookup);
        if (!type) return FONT_ERR_INIT;
        st->enabled_by_type[type < 9u ? type : 0u]++;
        if ((type != 4u && type != 6u) || !flags_supported(v, lookup)) continue;
        if (!lookup_ok(v, enabled[i], 0)) return FONT_ERR_INIT;
        g->lookups[g->count++] = enabled[i];
    }
    if (!g->count) return FONT_ERR_MISSING;
    g->lookup_list = v->lookup_list; g->lookup_count = (uint16_t)v->lookup_count;
    return FONT_OK;
}

int font_gsub_parse(font_gsub *g, const uint8_t *table, size_t len, font_gsub_stats *stats)
{
    font_gsub_stats local;
    font_gsub_stats *st = stats ? stats : &local;
    memset(st, 0, sizeof *st);
    if (!g) return FONT_ERR_ARG;
    memset(g, 0, sizeof *g);
    g->table = table; g->len = len <= UINT32_MAX ? (uint32_t)len : 0u;
    g->state = FONT_GSUB_NONE;
    if (!table || len > UINT32_MAX) return FONT_ERR_INIT;
    view v = {table, len, 0, (uint64_t)GSUB_PARSE_FACTOR * (len / 2u + 1u), 0, 0};
    int rc = parse(g, &v, st);
    st->reads = v.reads; st->budget = v.budget;
    if (rc != FONT_OK) {
        g->count = 0;
        return rc;
    }
    g->state = FONT_GSUB_READY;
    return FONT_OK;
}

/* -------------------------------------------------------------- apply */

typedef struct run {
    view v;
    uint16_t *gl;
    size_t n, cap;
    const uint16_t *skip;
    size_t nskip;
    uint64_t ops;
} run;

static int skippable(const run *r, uint16_t gid)
{
    for (size_t i = 0; i < r->nskip; i++) if (r->skip[i] == gid) return 1;
    return 0;
}

/* Coverage index of gid, or -1. Bounds re-proven (apply-time defence). */
static int32_t coverage(run *r, uint64_t at, uint32_t gid)
{
    const view *v = &r->v;
    if (!has(v, at, 4)) return -1;
    uint32_t fmt = u16(v, at), count = u16(v, at + 2u), lo = 0, hi = count;
    if (fmt == 1u) {
        if (!has(v, at + 4u, UINT64_C(2) * count)) return -1;
        while (lo < hi) {
            uint32_t mid = lo + (hi - lo) / 2u, g = u16(v, at + 4u + UINT64_C(2) * mid);
            r->ops++;
            if (g == gid) return (int32_t)mid;
            if (g < gid) lo = mid + 1u; else hi = mid;
        }
        return -1;
    }
    if (fmt == 2u) {
        if (!has(v, at + 4u, UINT64_C(6) * count)) return -1;
        while (lo < hi) {
            uint32_t mid = lo + (hi - lo) / 2u;
            uint64_t rec = at + 4u + UINT64_C(6) * mid;
            r->ops++;
            if (gid < u16(v, rec)) hi = mid;
            else if (gid > u16(v, rec + 2u)) lo = mid + 1u;
            else return (int32_t)(u16(v, rec + 4u) + gid - u16(v, rec));
        }
    }
    return -1;
}

/* Substitution result in pre-apply coordinates: glyphs [p, consumed) were
 * replaced by [p, produced) in post-apply coordinates. */
typedef struct span { size_t consumed, produced; } span;

static int apply_single(run *r, uint64_t at, size_t p, span *s)
{
    const view *v = &r->v;
    if (!has(v, at, 6)) return 0;
    int32_t index = coverage(r, at + u16(v, at + 2u), r->gl[p]);
    if (index < 0) return 0;
    if (u16(v, at) == 1u) r->gl[p] = (uint16_t)(r->gl[p] + u16(v, at + 4u));
    else if (u16(v, at) == 2u && (uint32_t)index < u16(v, at + 4u) &&
             has(v, at + 6u + UINT64_C(2) * (uint32_t)index, 2))
        r->gl[p] = (uint16_t)u16(v, at + 6u + UINT64_C(2) * (uint32_t)index);
    else return 0;
    s->consumed = s->produced = p + 1u;
    return 1;
}

static int apply_multiple(run *r, uint64_t at, size_t p, span *s)
{
    const view *v = &r->v;
    if (!has(v, at, 6) || u16(v, at) != 1u) return 0;
    int32_t index = coverage(r, at + u16(v, at + 2u), r->gl[p]);
    if (index < 0 || (uint32_t)index >= u16(v, at + 4u)) return 0;
    uint64_t seq = at + u16(v, at + 6u + UINT64_C(2) * (uint32_t)index);
    if (!has(v, seq, 2)) return 0;
    size_t count = u16(v, seq);
    if (!has(v, seq + 2u, UINT64_C(2) * count) || r->n - 1u + count > r->cap) return 0;
    memmove(r->gl + p + count, r->gl + p + 1u, (r->n - p - 1u) * sizeof *r->gl);
    for (size_t i = 0; i < count; i++) r->gl[p + i] = (uint16_t)u16(v, seq + 2u + UINT64_C(2) * i);
    r->n = r->n - 1u + count; r->ops += count;
    s->consumed = p + 1u; s->produced = p + count;
    return 1;
}

static int apply_ligature(run *r, uint64_t at, size_t p, span *s)
{
    const view *v = &r->v;
    if (!has(v, at, 6) || u16(v, at) != 1u) return 0;
    int32_t index = coverage(r, at + u16(v, at + 2u), r->gl[p]);
    if (index < 0 || (uint32_t)index >= u16(v, at + 4u)) return 0;
    uint64_t set = at + u16(v, at + 6u + UINT64_C(2) * (uint32_t)index);
    if (!has(v, set, 2)) return 0;
    uint32_t ligs = u16(v, set);
    if (!has(v, set + 2u, UINT64_C(2) * ligs)) return 0;
    for (uint32_t k = 0; k < ligs; k++) {
        uint64_t lig = set + u16(v, set + 2u + UINT64_C(2) * k);
        r->ops++;
        if (!has(v, lig, 4)) continue;
        uint32_t comps = u16(v, lig + 2u);
        if (!comps || comps > r->n - p || !has(v, lig + 4u, UINT64_C(2) * (comps - 1u))) continue;
        size_t j = p + 1u;
        uint32_t c = 1;
        for (; c < comps; c++, j++) {
            uint16_t want = (uint16_t)u16(v, lig + 4u + UINT64_C(2) * (c - 1u));
            while (j < r->n && r->gl[j] != want && skippable(r, r->gl[j])) { j++; r->ops++; }
            r->ops++;
            if (j >= r->n || r->gl[j] != want) break;
        }
        if (c < comps) continue;
        r->gl[p] = (uint16_t)u16(v, lig);
        memmove(r->gl + p + 1u, r->gl + j, (r->n - j) * sizeof *r->gl);
        r->n -= j - p - 1u;
        s->consumed = j; s->produced = p + 1u;
        return 1;
    }
    return 0;
}

/* Nested lookup (chain record) at position p: first applicable subtable. */
static int apply_nested(run *r, uint32_t index, size_t p, span *s)
{
    uint64_t lookup;
    if (!lookup_at(&r->v, index, &lookup) || !flags_supported(&r->v, lookup)) return 0;
    uint32_t count = u16(&r->v, lookup + 4u);
    for (uint32_t k = 0; k < count && p < r->n; k++) {
        uint64_t at; uint32_t type;
        r->ops++;
        if (!subtable(&r->v, lookup, k, &type, &at)) return 0;
        if ((type == 1u && apply_single(r, at, p, s)) || (type == 2u && apply_multiple(r, at, p, s)) ||
            (type == 4u && apply_ligature(r, at, p, s))) return 1;
    }
    return 0;
}

static size_t remap(size_t pos, const span *s, size_t p)
{
    if (pos == SIZE_MAX || pos < p) return pos;
    if (pos == p) return s->produced > p ? p : SIZE_MAX;   /* deleted when nothing produced */
    if (pos < s->consumed) return SIZE_MAX;
    return pos - s->consumed + s->produced;
}

static int apply_chain3(run *r, uint64_t at, size_t p, size_t *end, int *applied)
{
    *applied = 0;
    const view *v = &r->v;
    if (!has(v, at, 4) || u16(v, at) != 3u) return 0;
    uint64_t back = at + 2u;
    uint32_t nback = u16(v, back);
    uint64_t in = back + 2u + UINT64_C(2) * nback;
    if (!has(v, in, 2)) return 0;
    uint32_t nin = u16(v, in);
    uint64_t ahead = in + 2u + UINT64_C(2) * nin;
    if (!has(v, ahead, 2)) return 0;
    uint32_t nahead = u16(v, ahead);
    uint64_t recs = ahead + 2u + UINT64_C(2) * nahead;
    if (!nin || nin > r->n - p || !has(v, recs, 2)) return 0;
    uint32_t nrecs = u16(v, recs);
    if (!has(v, back + 2u, UINT64_C(2) * nback) || !has(v, in + 2u, UINT64_C(2) * nin) ||
        !has(v, ahead + 2u, UINT64_C(2) * nahead) || !has(v, recs + 2u, UINT64_C(4) * nrecs)) return 0;
    size_t pos[FONT_GSUB_MAX_GLYPHS];
    size_t j = p;
    for (uint32_t k = 0; k < nin; k++, j++) {
        uint64_t cov = at + u16(v, in + 2u + UINT64_C(2) * k);
        if (k) while (j < r->n && coverage(r, cov, r->gl[j]) < 0 && skippable(r, r->gl[j])) j++;
        if (j >= r->n || coverage(r, cov, r->gl[j]) < 0) return 0;
        pos[k] = j;
    }
    size_t after = j;
    for (uint32_t k = 0; k < nahead; k++, j++) {
        uint64_t cov = at + u16(v, ahead + 2u + UINT64_C(2) * k);
        while (j < r->n && coverage(r, cov, r->gl[j]) < 0 && skippable(r, r->gl[j])) j++;
        if (j >= r->n || coverage(r, cov, r->gl[j]) < 0) return 0;
    }
    size_t b = p;
    for (uint32_t k = 0; k < nback; k++) {
        uint64_t cov = at + u16(v, back + 2u + UINT64_C(2) * k);
        while (b > 0 && coverage(r, cov, r->gl[b - 1u]) < 0 && skippable(r, r->gl[b - 1u])) b--;
        if (b == 0 || coverage(r, cov, r->gl[b - 1u]) < 0) return 0;
        b--;
    }
    for (uint32_t k = 0; k < nrecs; k++) {
        uint32_t seq = u16(v, recs + 2u + UINT64_C(4) * k), index = u16(v, recs + 4u + UINT64_C(4) * k);
        r->ops++;
        if (r->ops > GSUB_APPLY_BUDGET) return 0;
        if (seq >= nin || pos[seq] == SIZE_MAX) continue;
        size_t q = pos[seq];
        span s;
        if (!apply_nested(r, index, q, &s)) continue;
        (*applied)++;
        for (uint32_t i = 0; i < nin; i++) pos[i] = remap(pos[i], &s, q);
        after = after >= s.consumed ? after - s.consumed + s.produced : s.produced;
    }
    *end = after > p ? after : p + 1u;
    if (*end > r->n) *end = r->n;
    return 1;
}

int font_gsub_apply(const font_gsub *g, uint16_t *glyphs, size_t *n, size_t cap,
                    const uint16_t *skip, size_t nskip, uint32_t *ops)
{
    if (ops) *ops = 0;
    if (!g || !glyphs || !n || *n > cap || cap > FONT_GSUB_MAX_GLYPHS || (nskip && !skip)) return FONT_ERR_ARG;
    if (g->state != FONT_GSUB_READY || !g->table) return 0;
    run r = {{g->table, g->len, 0, 0, g->lookup_list, g->lookup_count}, glyphs, *n, cap, skip, nskip, 0};
    if (!has(&r.v, r.v.lookup_list, 2u + UINT64_C(2) * r.v.lookup_count)) return 0;
    int subs = 0;
    for (uint32_t l = 0; l < g->count && l < FONT_GSUB_MAX_LOOKUPS; l++) {
        uint64_t lookup;
        if (!lookup_at(&r.v, g->lookups[l], &lookup)) continue;
        uint32_t count = u16(&r.v, lookup + 4u);
        for (size_t i = 0; i < r.n;) {
            size_t next = i + 1u;
            for (uint32_t k = 0; k < count; k++) {
                uint64_t at; uint32_t type; span s;
                r.ops++;
                if (!subtable(&r.v, lookup, k, &type, &at)) break;
                if (type == 4u && apply_ligature(&r, at, i, &s)) { subs++; next = s.produced; break; }
                size_t end; int applied;
                if (type == 6u && apply_chain3(&r, at, i, &end, &applied)) {
                    subs += applied; next = end;
                    break;
                }
            }
            if (r.ops > GSUB_APPLY_BUDGET) { if (ops) *ops = (uint32_t)r.ops; return FONT_ERR_NOMEM; }
            /* next > i, or next == r.n after deletions: the scan terminates. */
            i = next;
        }
    }
    *n = r.n;
    if (ops) *ops = (uint32_t)r.ops;
    return subs;
}

/* ------------------------------------------------------------ cluster */

int font_gsub_cluster(struct font *f, const uint8_t *bytes, size_t len, uint16_t *out, size_t *n)
{
    if (!f || !bytes || !out || !n) return FONT_ERR_ARG;
    *n = 0;
    uint32_t cps[FONT_GSUB_MAX_GLYPHS];
    size_t count = 0;
    int kind = 0, based = 0;
    for (size_t off = 0; off < len;) {
        utf8_step step = utf8_decode(bytes + off, len - off);
        if (!step.valid || count == FONT_GSUB_MAX_GLYPHS) return FONT_ERR_MISSING;
        cps[count++] = step.cp; off += step.len;
        if (!based && utf8_cell_width(step.cp)) { based = 1; kind = utf8_emoji_class(step.cp); }
    }
    if (!kind) return FONT_ERR_MISSING;
    font_gsub *g = &f->gsub;
    if (g->state == FONT_GSUB_UNPARSED) {
        if (!g->table) g->state = FONT_GSUB_NONE;
        else (void)font_gsub_parse(g, g->table, g->len, NULL); /* NONE on failure */
    }
    if (g->state != FONT_GSUB_READY) return FONT_ERR_MISSING;
    uint16_t gl[FONT_GSUB_MAX_GLYPHS];
    for (size_t i = 0; i < count; i++) {
        uint32_t gid = font_glyph_id(f, cps[i]);
        if (!gid || gid > 0xffffu) return FONT_ERR_MISSING;
        gl[i] = (uint16_t)gid;
    }
    uint16_t skip[2]; size_t nskip = 0;
    uint32_t vs[2] = {font_glyph_id(f, 0xfe0eu), font_glyph_id(f, 0xfe0fu)};
    for (size_t i = 0; i < 2; i++) if (vs[i] && vs[i] <= 0xffffu) skip[nskip++] = (uint16_t)vs[i];
    size_t m = count;
    if (font_gsub_apply(g, gl, &m, FONT_GSUB_MAX_GLYPHS, skip, nskip, NULL) <= 0) return FONT_ERR_MISSING;
    /* edit-r8e on the glyph stream: what GSUB did not absorb is dropped. */
    uint32_t zwj = font_glyph_id(f, 0x200du), tone[5];
    for (uint32_t i = 0; i < 5u; i++) tone[i] = font_glyph_id(f, 0x1f3fbu + i);
    for (size_t i = 0; i < m; i++) {
        uint32_t gid = gl[i];
        if (zwj && gid == zwj) { if (kind == UTF8_EMOJI_PICT) break; continue; }
        int drop = (vs[0] && gid == vs[0]) || (vs[1] && gid == vs[1]);
        for (uint32_t t = 0; t < 5u; t++) drop |= tone[t] && gid == tone[t];
        if (drop) continue;
        if (!gid) return FONT_ERR_MISSING;
        out[(*n)++] = (uint16_t)gid;
    }
    if (!*n) return FONT_ERR_MISSING;
    return FONT_OK;
}
