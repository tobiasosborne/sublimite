/* Internal to src/find (tests may include it): literal kernel. */
#ifndef EDIT_FIND_LITERAL_H
#define EDIT_FIND_LITERAL_H
#include "find_int.h"

/* Literal search plan: rare-byte pair filter (positions p1,p2 chosen by a
 * static byte-frequency table), verify, and a lazily prepared Two-Way
 * fallback entered when verification work exceeds a linear budget. */
typedef struct find_lit_blk { size_t at; unsigned mask; } find_lit_blk;
typedef struct find_lit {
    find_lit_blk (*scan)(const uint8_t *a, const uint8_t *b, uint8_t c1, uint8_t c2, size_t i, size_t e);
    const uint8_t *nd;
    size_t n, p1, p2;
    uint8_t c1, c2;
    bool tw, tw_ready, periodic;
    size_t ell, per;
    uint64_t scanned, verified;
    meter *m;
} find_lit;

/* false only when cancelled (m->stopped). n >= 1; two_way forces Two-Way. */
bool find_lit_init(find_lit *l, const uint8_t *needle, size_t n, meter *m, bool two_way);
/* Leftmost match start >= from over the whole source (len = its length).
 * 1 found (*out), 0 none, -1 cancelled. No allocation. */
int find_lit_seek(find_lit *l, const find_source *s, uint64_t len, uint64_t from, uint64_t *out);

/* mode 0 = production (SIMD filter + verify, Two-Way after budget);
 * mode 1 = Two-Way only. Same semantics as find_literal. */
find_code find_literal_mode(const find_source *source, const uint8_t *needle,
                            size_t needle_length, const find_control *control,
                            find_result *result, int mode);
#endif
