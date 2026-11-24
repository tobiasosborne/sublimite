#include "undo/undo.h"

#include <string.h>

/* (P) One group per byte value. Sixteen pair-widths allow nine long runs,
 * including alternating ORIGINAL and ADD, in the dense text oracle. */
enum { GROUPS = UINT8_MAX, TEXT = 16 * sizeof(piece_span) };

typedef struct image {
    size_t len;
    uint8_t bytes[TEXT];
} image;

typedef struct group {
    image before, after;
    undo_state first, last;
} group;

typedef struct model {
    image current;
    group groups[GROUPS];
    size_t applied, total;
    bool retain;
} model;

static void check_view(const piece_view *v, const image *img)
{
    ASSERT(piece_length(v) == img->len);
    piece_iter it;
    piece_iter_init(&it, v, 0);
    const uint8_t *bytes;
    size_t n, off = 0;
    while ((n = piece_iter_next(&it, &bytes)) != 0) {
        ASSERT(n <= img->len - off && memcmp(bytes, img->bytes + off, n) == 0);
        off += n;
    }
    ASSERT(off == img->len);
}

static void replace(image *img, size_t off, size_t del, const uint8_t *bytes, size_t len)
{
    memmove(img->bytes + off + len, img->bytes + off + del, img->len - off - del);
    memcpy(img->bytes + off, bytes, len);
    img->len = img->len - del + len;
}

static void edit(undo_log *u, model *m, const uint8_t *input, bool burst)
{
    group g = {.before = m->current, .first = {{input[0]}}, .last = {{input[1]}}};
    image *img = &m->current;
    if (burst) {
        if (TEXT - img->len < 2) return; /* (P) The two adjacent burst edits. */
        undo_state middle = {0};
        for (size_t i = 0; i < 2; i++) {
            ASSERT(undo_begin(u, i == 0 ? &g.first : &middle, i != 0) == UNDO_OK);
            ASSERT(undo_replace(u, img->len, 0, input + i, 1) == UNDO_OK);
            ASSERT(undo_commit(u, i == 0 ? &middle : &g.last) == UNDO_OK);
            img->bytes[img->len++] = input[i];
        }
    } else {
        ASSERT(undo_begin(u, &g.first, false) == UNDO_OK);
        bool changed = false;
        /* (P) Two independent member edits per explicit group, using the
         * three operands of each tuple as offset, length seed and data. */
        for (size_t i = 0; i < 2; i++, input += 3) {
            size_t off = input[0] % (img->len + 1);
            size_t del = input[1] % (img->len - off + 1);
            size_t room = TEXT - (img->len - del);
            uint8_t bytes[sizeof(piece_span)];
            size_t len = room < sizeof bytes ? room : sizeof bytes;
            len = input[2] % (len + 1);
            memset(bytes, input[2], len);
            ASSERT(undo_replace(u, off, del, bytes, len) == UNDO_OK);
            replace(img, off, del, bytes, len);
            changed |= del != 0 || len != 0;
        }
        ASSERT(undo_commit(u, &g.last) == UNDO_OK);
        if (!changed) return;
    }
    if (!m->retain) return;
    g.after = *img;
    m->groups[m->applied++] = g;
    m->total = m->applied;
}

static void replay(undo_log *u, model *m, bool reverse, bool clear_at_yield)
{
    undo_replay_direction direction = reverse ? UNDO_REPLAY_UNDO : UNDO_REPLAY_REDO;
    const undo_state *target;
    /* (P) Yield after one operation when replaying a multi-edit group. */
    undo_err e = undo_replay_prepare(u, direction, 0, &target);
    if (reverse ? m->applied == 0 : m->applied == m->total) {
        ASSERT(e == UNDO_OK && target == NULL);
        return;
    }
    const group *g = &m->groups[reverse ? m->applied - 1 : m->applied];
    if (e == UNDO_MORE) {
        ASSERT(target == NULL);
        check_view(piece_snapshot_view(undo_replay_snapshot(u)), &m->current);
        undo_history h = undo_get_history(u);
        ASSERT(h.undo_groups == m->applied && h.redo_groups == m->total - m->applied);
        if (clear_at_yield) {
            undo_clear(u);
            m->applied = m->total = 0;
            return;
        }
        e = undo_replay_prepare(u, direction, 0, &target);
    }
    ASSERT(e == UNDO_OK && target);
    const undo_state *want = reverse ? &g->first : &g->last;
    ASSERT(memcmp(target, want, sizeof *want) == 0);
    ASSERT(undo_commit(u, target) == UNDO_OK);
    m->current = reverse ? g->before : g->after;
    if (reverse) m->applied--;
    else m->applied++;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t len)
{
    piece_reserve *r;
    piece_tree *t;
    undo_log *u;
    /* (P) Seed half the image with ORIGINAL, leaving room for ADD runs. */
    uint8_t original[TEXT / 2];
    memset(original, '\n', sizeof original);
    ASSERT(piece_reserve_create(&r) == PIECE_OK);
    ASSERT(piece_open_copy(r, original, sizeof original, &t) == PIECE_OK);
    ASSERT(undo_create(&u, t, SIZE_MAX, NULL, NULL) == UNDO_OK);
    model m = {.current.len = sizeof original, .retain = true};
    memcpy(m.current.bytes, original, sizeof original);

    /* (P) One opcode and two (offset, length, data) triples. The iteration
     * bound gives the dense model space for one new group per command. */
    enum { tuple = 1 + 2 * 3, ops = 6 };
    for (size_t i = 0; len >= tuple && i < GROUPS; i++, data += tuple, len -= tuple) {
        switch (*data % ops) {
        case 0: edit(u, &m, data + 1, false); break;
        case 1: edit(u, &m, data + 1, true); break;
        case 2: replay(u, &m, true, data[1] != 0); break;
        case 3: replay(u, &m, false, data[1] != 0); break;
        case 4:
            /* (P) The smallest nonzero byte cap cannot fund any record. */
            m.retain = data[1] != 0;
            undo_set_cap(u, m.retain ? SIZE_MAX : 1);
            if (!m.retain) m.applied = m.total = 0;
            break;
        case 5: (void)undo_maintain(u, data[1]); break;
        }
        undo_history h = undo_get_history(u);
        ASSERT(h.undo_groups == m.applied && h.redo_groups == m.total - m.applied);
        check_view(piece_tree_view(t), &m.current);
    }
    undo_destroy(u);
    piece_close(t);
    piece_reserve_destroy(r);
    return 0;
}
