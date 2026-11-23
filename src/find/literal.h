/* Internal literal plan, shared with the byte-sensitive regex prefix. */
#ifndef EDIT_FIND_LITERAL_H
#define EDIT_FIND_LITERAL_H
#include "find_int.h"

typedef struct find_lit_blk { size_t at; unsigned mask; } find_lit_blk;
typedef struct find_lit_filter {
    uint8_t first, second, first_fold_bit, second_fold_bit;
} find_lit_filter;

typedef struct find_lit {
    find_lit_blk (*scan)(const uint8_t *a, const uint8_t *b, find_lit_filter filter,
                         size_t from, size_t end);
    const uint8_t *nd;
    size_t n, p1, p2;
    find_lit_filter filter;
    find_literal_options options;
    bool tw, tw_ready, periodic;
    size_t ell, per;
    uint64_t scanned, verified;
    meter *m;
    reader before, after;
} find_lit;

/* n >= 1. The plan borrows source and needle. A false return is cancellation.
 * The optional word cursors depend on monotonically increasing candidates. */
bool find_lit_init(find_lit *l, const find_source *source, const uint8_t *needle, size_t n,
                   find_literal_options options, meter *m, bool two_way);

/* Leftmost acceptable start >= from; len is the full source length.
 * Return 1 for a match, 0 for exhaustion, -1 for cancellation. */
int find_lit_seek(find_lit *l, const find_source *s, uint64_t len, uint64_t from, uint64_t *out);

#endif
