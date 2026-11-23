/* L1 piece: stub until v2-0fv.7b. It defines every piece.h symbol so that
 * the archive links, refuses every call that could allocate, and so fails
 * tests/piece_test.c. No object can exist, so the other calls are
 * unreachable except through a caller bug, which they ASSERT. */
#include "piece/piece.h"

#include <string.h>

#define STUB_NO_OBJECT 0

piece_err piece_reserve_create(piece_reserve **out)
{
    ASSERT(out != NULL);
    *out = NULL;
    return PIECE_ERR_NOMEM;
}
void piece_reserve_destroy(piece_reserve *r) { (void)r; ASSERT(STUB_NO_OBJECT); }

piece_err piece_open_copy(piece_reserve *r, const uint8_t *bytes, size_t n, piece_tree **out)
{
    (void)r, (void)bytes, (void)n;
    ASSERT(out != NULL);
    *out = NULL;
    return PIECE_ERR_NOMEM;
}
piece_err piece_open_mapped(piece_reserve *r, const piece_backing *b, piece_tree **out)
{
    (void)r, (void)b;
    ASSERT(out != NULL);
    *out = NULL;
    return PIECE_ERR_NOMEM;
}
void piece_close(piece_tree *t) { (void)t; ASSERT(STUB_NO_OBJECT); }
const piece_view *piece_tree_view(const piece_tree *t) { (void)t; ASSERT(STUB_NO_OBJECT); return NULL; }
uint64_t piece_add_length(const piece_tree *t) { (void)t; ASSERT(STUB_NO_OBJECT); return 0; }

uint64_t piece_length(const piece_view *v) { (void)v; ASSERT(STUB_NO_OBJECT); return 0; }
uint8_t piece_byte_at(const piece_view *v, uint64_t off) { (void)v, (void)off; ASSERT(STUB_NO_OBJECT); return 0; }
const piece_backing *piece_view_backing(const piece_view *v) { (void)v; ASSERT(STUB_NO_OBJECT); return NULL; }
void piece_iter_init(piece_iter *it, const piece_view *v, uint64_t off)
{
    (void)it, (void)v, (void)off;
    ASSERT(STUB_NO_OBJECT);
}
void piece_iter_seek(piece_iter *it, uint64_t off) { (void)it, (void)off; ASSERT(STUB_NO_OBJECT); }
size_t piece_iter_next(piece_iter *it, const uint8_t **p) { (void)it, (void)p; ASSERT(STUB_NO_OBJECT); return 0; }
bool piece_iter_next_span(piece_iter *it, uint64_t end, piece_span *out, piece_span_nl *nl)
{
    (void)it, (void)end, (void)out, (void)nl;
    ASSERT(STUB_NO_OBJECT);
    return false;
}

piece_err piece_replace(piece_tree *t, uint64_t off, uint64_t del, const uint8_t *bytes, size_t n)
{
    (void)t, (void)off, (void)del, (void)bytes, (void)n;
    ASSERT(STUB_NO_OBJECT);
    return PIECE_ERR_NOMEM;
}
piece_err piece_replace_spans(piece_tree *t, uint64_t off, uint64_t del, const piece_span *spans,
                              const piece_span_nl *nl, size_t count)
{
    (void)t, (void)off, (void)del, (void)spans, (void)nl, (void)count;
    ASSERT(STUB_NO_OBJECT);
    return PIECE_ERR_NOMEM;
}
piece_err piece_capture(piece_tree *t, uint64_t off, uint64_t len, piece_span *out)
{
    (void)t, (void)off, (void)len, (void)out;
    ASSERT(STUB_NO_OBJECT);
    return PIECE_ERR_NOMEM;
}

piece_err piece_snapshot_take(piece_tree *t, piece_snapshot **out)
{
    (void)t, (void)out;
    ASSERT(STUB_NO_OBJECT);
    return PIECE_ERR_NOMEM;
}
piece_snapshot *piece_snapshot_retain(piece_snapshot *s) { ASSERT(STUB_NO_OBJECT); return s; }
void piece_snapshot_release(piece_snapshot *s) { (void)s; ASSERT(STUB_NO_OBJECT); }
const piece_view *piece_snapshot_view(const piece_snapshot *s) { (void)s; ASSERT(STUB_NO_OBJECT); return NULL; }

piece_err piece_checkpoint_begin(piece_tree *t) { (void)t; ASSERT(STUB_NO_OBJECT); return PIECE_ERR_NOMEM; }
void piece_checkpoint_commit(piece_tree *t) { (void)t; ASSERT(STUB_NO_OBJECT); }
void piece_checkpoint_abort(piece_tree *t) { (void)t; ASSERT(STUB_NO_OBJECT); }

piece_err piece_replenish(piece_tree *t) { (void)t; ASSERT(STUB_NO_OBJECT); return PIECE_ERR_NOMEM; }
bool piece_reclaim(piece_tree *t, size_t budget) { (void)t, (void)budget; ASSERT(STUB_NO_OBJECT); return false; }

bool piece_lines_before(const piece_view *v, uint64_t off, uint64_t *count)
{
    (void)v, (void)off, (void)count;
    ASSERT(STUB_NO_OBJECT);
    return false;
}
bool piece_lines_start(const piece_view *v, uint64_t line, uint64_t *off, uint64_t *before)
{
    (void)v, (void)line, (void)off, (void)before;
    ASSERT(STUB_NO_OBJECT);
    return false;
}
bool piece_lines_count(const piece_backing *b, uint64_t first, uint32_t *counts, size_t n)
{
    (void)b, (void)first, (void)counts, (void)n;
    ASSERT(STUB_NO_OBJECT);
    return false;
}
bool piece_lines_adopt(piece_tree *t, os_fault_id source, uint64_t first, const uint32_t *counts, size_t n)
{
    (void)t, (void)source, (void)first, (void)counts, (void)n;
    ASSERT(STUB_NO_OBJECT);
    return false;
}
piece_err piece_lines_resolve(piece_tree *t, size_t budget, bool *more)
{
    (void)t, (void)budget, (void)more;
    ASSERT(STUB_NO_OBJECT);
    return PIECE_ERR_NOMEM;
}

size_t piece_app_id_len(void)
{
    return strlen(os_app_id());
}

#ifdef PIECE_TEST_HOOKS
/* Test-hooks variant only: the stub has no shape to limit. */
void piece_test_set_height_limit(unsigned levels);
void piece_test_set_height_limit(unsigned levels) { ASSERT(levels <= PIECE_MAX_HEIGHT); }
#endif
