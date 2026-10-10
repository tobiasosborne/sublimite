#include "scroll/scroll.h"
#include <time.h>

static bool source_valid(const scroll_visual_source *source)
{
    return source && source->row_at && source->locate &&
        source->bytes <= LINEIDX_MAX_LEN && source->rows && source->rows <= source->bytes + 1;
}
static bool row_valid(const scroll_visual_source *source, const scroll_visual_row *row)
{
    return (source->exact ? row->ordinal < source->rows : row->ordinal <= source->bytes) && row->byte <= row->end &&
        row->end <= source->bytes && row->line_start <= row->byte &&
        row->line <= source->bytes;
}
static bool visual_valid(const scroll_visual *s)
{
    return s && source_valid(&s->source) && s->viewport.config.rows &&
        s->viewport.config.row_height &&
        s->viewport.config.row_height <= UINT32_MAX / SCROLL_PIXEL_UNIT &&
        s->viewport.extent.bytes == s->source.bytes &&
        s->viewport.extent.lines == s->source.rows && s->viewport.extent.exact == s->source.exact;
}
int scroll_visual_init(scroll_visual *s, scroll_config config,
                       const scroll_visual_source *source, scroll_visual_row first)
{
    if (!s || !source_valid(source) || !row_valid(source, &first) || first.ordinal || first.byte)
        return SCROLL_ERR_ARG;
    scroll_visual next = {.source = *source, .first = first};
    int rc = scroll_init(&next.viewport, config,
                         (scroll_extent){source->bytes, source->rows, source->exact});
    if (rc) return rc;
    next.viewport.approximate = false; /* Ordinals/boundaries are exact, even with unknown extent. */
    *s = next;
    return SCROLL_OK;
}
static void cancel_end(scroll_state *s)
{
    if (s->request == SCROLL_BYTE) {
        s->request = SCROLL_READY;
        s->target_byte = 0;
        s->pending_rows = 0;
        s->pending_up = false;
    }
}
int scroll_visual_wheel(scroll_visual *s, int32_t delta)
{
    if (!visual_valid(s) || s->reflow) return SCROLL_ERR_ARG;
    scroll_state next = s->viewport;
    cancel_end(&next);
    int rc = scroll_wheel(&next, delta);
    if (!rc) s->viewport = next;
    return rc;
}
int scroll_visual_pixels(scroll_visual *s, int64_t delta_q8)
{
    if (!visual_valid(s) || s->reflow) return SCROLL_ERR_ARG;
    scroll_state next = s->viewport;
    cancel_end(&next);
    int rc = scroll_pixels(&next, delta_q8);
    if (!rc) s->viewport = next;
    return rc;
}
int scroll_visual_key_motion(scroll_visual *s, scroll_key key, view_key *motion)
{
    if (!visual_valid(s) || s->reflow) return SCROLL_ERR_ARG;
    scroll_state next = s->viewport;
    cancel_end(&next);
    int rc = scroll_key_motion(&next, key, motion);
    if (!rc) s->viewport = next;
    return rc;
}
int scroll_visual_resize(scroll_visual *s, scroll_config config,
                         const scroll_visual_source *source)
{
    if (!visual_valid(s) || !source_valid(source)) return SCROLL_ERR_ARG;
    scroll_visual next = *s;
    scroll_state check;
    int rc = scroll_init(&check, config, (scroll_extent){source->bytes, source->rows, source->exact});
    if (rc) return rc;
    uint64_t old_height = (uint64_t)s->viewport.config.row_height * SCROLL_PIXEL_UNIT;
    uint64_t new_height = (uint64_t)config.row_height * SCROLL_PIXEL_UNIT;
    uint64_t offset = s->viewport.subrow_q8 + (s->reflow ? s->reflow_rows * old_height : 0);
    next.reflow_rows = offset / new_height;
    next.viewport.subrow_q8 = offset % new_height;
    next.viewport.config = config;
    next.viewport.extent = check.extent;
    next.source = *source;
    next.reflow_byte = s->first.byte < source->bytes ? s->first.byte : source->bytes;
    next.reflow = true;
    *s = next;
    return SCROLL_OK;
}
static uint64_t now_ns(void)
{
    struct timespec ts;
    (void)clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * UINT64_C(1000000000) + (uint64_t)ts.tv_nsec;
}
static int discard(scroll_visual_resolver *r, int rc)
{
    r->active = false;
    return rc;
}
static void adopt_end(scroll_visual *s, uint64_t last)
{
    s->source.rows = last + 1u;
    s->source.exact = true;
    (void)scroll_set_extent(&s->viewport,
                           (scroll_extent){s->source.bytes, s->source.rows, true});
}
/* Compare semantic intent, never C structure padding. */
static bool same_intent(const scroll_visual *a, const scroll_visual *b)
{
    const scroll_state *x = &a->viewport, *y = &b->viewport;
    return x->config.rows == y->config.rows && x->config.row_height == y->config.row_height &&
        x->config.margin == y->config.margin && x->extent.bytes == y->extent.bytes &&
        x->extent.lines == y->extent.lines && x->extent.exact == y->extent.exact &&
        x->first_line == y->first_line && x->first_byte == y->first_byte &&
        x->subrow_q8 == y->subrow_q8 && x->anchor_line == y->anchor_line &&
        x->target_byte == y->target_byte && x->pending_rows == y->pending_rows &&
        x->request == y->request && x->approximate == y->approximate &&
        x->pending_up == y->pending_up && a->first.byte == b->first.byte &&
        a->source.ctx == b->source.ctx && a->source.bytes == b->source.bytes &&
        a->source.rows == b->source.rows && a->source.generation == b->source.generation &&
        a->source.row_at == b->source.row_at && a->source.locate == b->source.locate &&
        a->source.exact == b->source.exact && a->reflow == b->reflow &&
        a->reflow_byte == b->reflow_byte && a->reflow_rows == b->reflow_rows;
}
static int run(scroll_visual *s, scroll_visual_resolver *r, bool following,
               uint64_t cursor_byte, bool trailing, uint32_t budget, uint64_t deadline)
{
    if (!visual_valid(s) || !r || (following && cursor_byte > s->source.bytes))
        return SCROLL_ERR_ARG;
    if (!deadline) {
        uint64_t now = now_ns();
        deadline = now > UINT64_MAX - UINT64_C(500000) ? UINT64_MAX : now + UINT64_C(500000);
    }
    if (!budget) budget = 2;
    if (budget > 256) budget = 256;
    if (!r->active || !same_intent(&r->before, s) ||
        r->following != following || (following &&
            (r->cursor_byte != cursor_byte || r->trailing != trailing))) {
        *r = (scroll_visual_resolver){.before = *s, .next = *s,
            .cursor_byte = cursor_byte, .active = true, .following = following,
            .trailing = trailing};
        r->phase = s->reflow ? 0u : following ? 1u :
            s->viewport.request == SCROLL_BYTE ? 4u : 2u;
    }
    while (r->phase != 3) {
        if (!budget || now_ns() >= deadline) return SCROLL_MORE;
        budget--;
        scroll_visual_row row = {0};
        const scroll_visual_source *source = &r->next.source;
        int rc;
        if (r->phase == 0) rc = source->locate(source->ctx, r->next.reflow_byte, false, &row);
        else if (r->phase == 1) rc = source->locate(source->ctx, cursor_byte, trailing, &row);
        else if (r->phase == 4) rc = source->locate(source->ctx, source->bytes, false, &row);
        else {
            uint64_t ordinal = r->next.viewport.first_line;
            if (r->phase == 5) ordinal += r->next.viewport.config.rows - 1u +
                (r->next.viewport.subrow_q8 != 0 ? 1u : 0u);
            rc = source->row_at(source->ctx, ordinal, &row);
        }
        if (rc == SCROLL_MORE) return rc;
        if (rc == SCROLL_EOF && (r->phase == 2 || r->phase == 5) &&
            row_valid(source, &row) && !source->exact) {
            uint64_t requested = r->next.viewport.first_line;
            if (r->phase == 5) requested += r->next.viewport.config.rows - 1u +
                (r->next.viewport.subrow_q8 != 0 ? 1u : 0u);
            if (row.ordinal >= requested) return discard(r, SCROLL_ERR_SOURCE);
            adopt_end(&r->next, row.ordinal);
            r->phase = 2;
            continue;
        }
        if (rc || !row_valid(source, &row)) return discard(r, SCROLL_ERR_SOURCE);
        if (r->phase == 0) {
            uint64_t subrow = r->next.viewport.subrow_q8;
            uint64_t target = r->next.reflow_rows > UINT64_MAX - row.ordinal ?
                UINT64_MAX : row.ordinal + r->next.reflow_rows;
            (void)scroll_seek_line(&r->next.viewport, target);
            uint64_t count = source->exact ? source->rows : source->bytes + 1u;
            uint64_t end = count > r->next.viewport.config.rows ?
                count - r->next.viewport.config.rows : 0;
            if (r->next.viewport.first_line < end) r->next.viewport.subrow_q8 = subrow;
            r->next.reflow = false;
            r->phase = following ? 1u : 2u;
        } else if (r->phase == 1) {
            cancel_end(&r->next.viewport);
            rc = scroll_follow(&r->next.viewport, row.ordinal);
            if (rc) return discard(r, rc);
            r->phase = 2;
        } else if (r->phase == 4) {
            adopt_end(&r->next, row.ordinal);
            (void)scroll_seek_line(&r->next.viewport, row.ordinal);
            r->phase = 2;
        } else if (r->phase == 5) {
            uint64_t expected = r->next.viewport.first_line + r->next.viewport.config.rows - 1u +
                (r->next.viewport.subrow_q8 != 0 ? 1u : 0u);
            if (row.ordinal != expected) return discard(r, SCROLL_ERR_SOURCE);
            r->phase = 3;
        } else {
            if (row.ordinal != r->next.viewport.first_line) return discard(r, SCROLL_ERR_SOURCE);
            r->next.first = row;
            r->next.viewport.first_byte = row.byte;
            r->next.viewport.anchor_line = row.ordinal;
            r->next.viewport.request = SCROLL_READY;
            r->next.viewport.pending_rows = 0;
            r->next.viewport.pending_up = false;
            r->phase = source->exact ? 3u : 5u;
        }
    }
    *s = r->next;
    return discard(r, SCROLL_OK);
}
int scroll_visual_resolve_slice(scroll_visual *s, scroll_visual_resolver *r,
                                uint32_t query_budget, uint64_t deadline_ns)
{
    return run(s, r, false, 0, false, query_budget, deadline_ns);
}
int scroll_visual_follow_slice(scroll_visual *s, scroll_visual_resolver *r,
                               uint64_t cursor_byte, bool trailing,
                               uint32_t query_budget, uint64_t deadline_ns)
{
    return run(s, r, true, cursor_byte, trailing, query_budget, deadline_ns);
}
