#ifndef TESTS_EDIT_FIXTURE_H
#define TESTS_EDIT_FIXTURE_H

#include <stdio.h>
#include <stdlib.h>

#include "edit/edit.h"
#include "edit_journal.h"

#define ECHECK(expr) do { if (!(expr)) { \
    fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expr); \
    exit(EXIT_FAILURE); \
} } while (0)

typedef struct edit_fixture {
    piece_reserve *reserve;
    piece_tree *tree;
    edit_buffer *edit;
    journal *j;
} edit_fixture;

/* One editor owner and a distinct peer ID for interleaving tests. */
enum { EDIT_TEST_OWNER = 1, EDIT_TEST_PEER = 2 };

static inline void ef_open(edit_fixture *f, const uint8_t *s, size_t n,
                           size_t cap, journal *j)
{
    f->j = j;
    ECHECK(piece_reserve_create(&f->reserve) == PIECE_OK);
    ECHECK(piece_open_copy(f->reserve, s, n, &f->tree) == PIECE_OK);
    edit_options options = {.tree = f->tree, .journal = j, .buffer_id = EDIT_TEST_OWNER,
                           .history_cap_bytes = cap, .current_id = 1, .saved_id = 1};
    ECHECK(edit_create(&f->edit, &options) == EDIT_OK);
}

static inline void ef_close(edit_fixture *f)
{
    edit_destroy(f->edit);
    piece_reserve_destroy(f->reserve);
    ej_destroy(f->j);
}

static inline edit_result ef_finish(edit_buffer *b, edit_result r)
{
    while (r == EDIT_MORE) r = edit_continue(b, OS_DEADLINE_NEVER);
    return r;
}

static inline edit_change ef_submit(edit_buffer *b)
{
    /* The headless fixture adopts the eligible snapshot before delivering
     * the reply and consuming the change. */
    edit_boundary published;
    ECHECK(edit_read(b, &published) == EDIT_READY);
    uint64_t length = piece_length(piece_snapshot_view(published.snapshot));
    ECHECK(published.selection.active.cursor <= length);
    edit_change change;
    ECHECK(edit_take_change(b, &change) && change.revision == published.revision);
    return change;
}

static inline void ef_text(const edit_buffer *b, const uint8_t *s, size_t n)
{
    edit_boundary published;
    edit_read(b, &published);
    const piece_view *view = piece_snapshot_view(published.snapshot);
    ECHECK(piece_length(view) == n);
    piece_iter it;
    piece_iter_init(&it, view, 0);
    size_t seen = 0;
    const uint8_t *p;
    size_t k;
    while ((k = piece_iter_next(&it, &p)) != 0) {
        ECHECK(k <= n - seen);
        for (size_t i = 0; i < k; i++) ECHECK(p[i] == s[seen + i]);
        seen += k;
    }
    ECHECK(seen == n);
}

#endif
