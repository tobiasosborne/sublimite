/* src/font/gsub.h - bounded OpenType GSUB ligature subset for emoji clusters
 * (edit-v16, docs/decisions/edit-v16.md). Not a shaping engine: it maps one
 * grapheme cluster's glyph ids through the face's enabled ccmp/liga/rlig
 * lookups of type 4 (ligature) and type 6 format 3 (chained contextual,
 * nested types 1, 2 and 4), with type 7 extension unwrapping. Every other
 * lookup type or format is skipped. Pure functions over immutable table
 * bytes: no allocation, no globals. */
#ifndef EDIT_FONT_GSUB_H
#define EDIT_FONT_GSUB_H

#include <stddef.h>
#include <stdint.h>

/* (P) Longest RGI emoji sequence (Unicode 15.1 emoji-zwj-sequences.txt):
 * kiss / couple with heart with two skin tones, e.g. 1F469 1F3FB 200D 2764
 * FE0F 200D 1F48B 200D 1F468 1F3FC = 10 scalars. Longer clusters keep the
 * edit-r8e projection. Also the glyph-array capacity during substitution. */
#define FONT_GSUB_MAX_GLYPHS 10u
/* (E) Enabled-lookup cap per face: NotoEmoji-wght and NotoColorEmoji 2.047
 * each enable 4 ccmp lookups (M, fontTools dump 2026-10-16); 4x that.
 * A face over the cap has GSUB disabled (r8e fallback), never truncated. */
#define FONT_GSUB_MAX_LOOKUPS 16u

enum {
    FONT_GSUB_UNPARSED = 0, /* table extent known, not yet validated */
    FONT_GSUB_READY    = 1, /* validated; lookups[] usable */
    FONT_GSUB_NONE     = 2  /* absent, unusable (no feature) or malformed */
};

typedef struct font_gsub {
    const uint8_t *table;   /* borrowed from the font bytes; NULL if absent */
    uint32_t len;
    uint32_t lookup_list;   /* validated LookupList offset */
    uint16_t lookup_count;  /* LookupList count */
    uint16_t lookups[FONT_GSUB_MAX_LOOKUPS]; /* enabled supported, ascending */
    uint8_t count, state;
} font_gsub;

/* Diagnostics of one parse (tests, fuzz, report). by_type counts LookupList
 * entries by their resolved type (type 7 unwrapped), index 0 = unknown.
 * reads counts validator steps (16-bit fields, coverage entries) against
 * the parse budget. */
typedef struct font_gsub_stats {
    uint32_t lookups, by_type[9];
    uint32_t enabled, enabled_by_type[9];
    uint64_t reads, budget;
} font_gsub_stats;

/* Validate every structure reachable from the enabled supported lookups
 * (DFLT, else latn, default LangSys; features ccmp/liga/rlig plus the
 * required feature). Every offset and count is checked against len before
 * use; validator work is bounded by a budget proportional to len. Returns
 * FONT_OK with state READY, or FONT_ERR_INIT (malformed or over a cap) /
 * FONT_ERR_MISSING (well formed, nothing enabled) with state NONE.
 * stats may be NULL. Worker only: linear in the table. */
int font_gsub_parse(font_gsub *g, const uint8_t *table, size_t len, font_gsub_stats *stats);

/* Apply g's lookups in LookupList order to glyphs[0..*n), *n <= cap <=
 * FONT_GSUB_MAX_GLYPHS. Glyphs equal to one of skip[0..nskip) (the face's
 * variation selectors) are skipped while matching unless they match, as
 * HarfBuzz skips default ignorables; a matched span absorbs them. Returns
 * the number of substitutions (>= 0) or FONT_ERR_NOMEM when the per-cluster
 * work budget runs out (glyphs then unspecified; callers fall back). ops,
 * if not NULL, receives the work spent. Allocation-free. */
int font_gsub_apply(const font_gsub *g, uint16_t *glyphs, size_t *n, size_t cap,
                    const uint16_t *skip, size_t nskip, uint32_t *ops);

struct font;
/* One grapheme cluster (UTF-8, already segmented) to glyph ids of face f.
 * Applies when the cluster's first nonzero-width scalar is
 * Extended_Pictographic or Regional_Indicator and it has at most
 * FONT_GSUB_MAX_GLYPHS scalars, all mapped by f's cmap. Validates f's GSUB
 * on first use (worker). After substitution the edit-r8e rule applies to
 * the glyph stream: leftover skin tones and selectors are dropped and a
 * leftover ZWJ ends a pictographic cluster. FONT_OK with out[0..*n) when at
 * least one substitution happened and every remaining glyph is nonzero;
 * FONT_ERR_MISSING otherwise (caller keeps the r8e projection).
 * Allocation-free after the first-use parse, which allocates nothing either. */
int font_gsub_cluster(struct font *f, const uint8_t *bytes, size_t len,
                      uint16_t *out, size_t *n);

#endif
