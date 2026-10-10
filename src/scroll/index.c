#include "scroll/scroll.h"
#include <string.h>
#include <time.h>

#define SPAN_LIMIT 256u
#define SPAN_BYTES 4096u
#define SLICE_NS UINT64_C(500000)
typedef struct slice {
    const lineidx_src *source;
    uint64_t left, deadline;
    unsigned calls;
    int status;
} slice;
static uint64_t now_ns(void)
{
    struct timespec ts;
    (void)clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * UINT64_C(1000000000) + (uint64_t)ts.tv_nsec;
}
static size_t limited_span(void *ctx, uint64_t off, const uint8_t **out)
{
    slice *b = ctx;
    *out = NULL;
    if (b->status) return 0;
    if (!b->left || b->calls == SPAN_LIMIT || now_ns() >= b->deadline) {
        b->status = SCROLL_MORE; return 0;
    }
    b->calls++;
    size_t n = b->source->span(b->source->ctx, off, out);
    if (!n || !*out) { b->status = SCROLL_ERR_SOURCE; return 0; }
    uint64_t limit = b->source->len - off;
    if (limit > b->left) limit = b->left;
    if (limit > SPAN_BYTES) limit = SPAN_BYTES;
    if (n > limit) n = (size_t)limit;
    b->left -= n;
    return n;
}
static int read_span(slice *b, uint64_t off, uint64_t end, const uint8_t **p, size_t *n)
{
    /* Charge only the bytes this operation can examine. */
    uint64_t saved = b->left;
    if (b->left > end - off) b->left = end - off;
    uint64_t allowed = b->left;
    *n = limited_span(b, off, p);
    b->left = saved - (allowed - b->left);
    return b->status;
}
static uint64_t estimate_byte(const scroll_state *s, uint64_t rows, bool down)
{
    uint64_t density = s->extent.bytes / s->extent.lines;
    if (!density) density = 1;
    uint64_t available = down ? s->extent.bytes - s->first_byte : s->first_byte;
    uint64_t delta = rows > available / density ? available : rows * density;
    return down ? s->first_byte + delta : s->first_byte - delta;
}
/* Each walker preserves its exact cursor and counters at a yielded callback. */
enum { ALIGN = 1, DOWN, UP_COUNT, UP_FIND, BOTTOM, COUNT };
static void align_start(scroll_resolver *r, uint64_t target, uint64_t known, bool coarse)
{
    r->pos = target > SCROLL_SCAN_BUDGET ? target - SCROLL_SCAN_BUDGET : 0;
    r->start = known <= r->pos ? known : 0;
    r->end = target; r->coarse = coarse; r->scan_kind = ALIGN;
}
static int align_run(scroll_resolver *r, slice *b, uint64_t *out)
{
    while (r->pos < r->end) {
        const uint8_t *p; size_t n;
        int rc = read_span(b, r->pos, r->end, &p, &n);
        if (rc) return rc;
        for (size_t i = 0; i < n; i++) if (p[i] == '\n') r->start = r->pos + i + 1u;
        r->pos += n;
    }
    uint64_t lo = r->end > SCROLL_SCAN_BUDGET ? r->end - SCROLL_SCAN_BUDGET : 0;
    if (!r->coarse && r->start < lo) return SCROLL_MORE;
    *out = r->start; r->scan_kind = 0; return SCROLL_OK;
}
static void relative_start(scroll_resolver *r, uint64_t rows, bool up, bool coarse)
{
    r->pos = r->next.first_byte; r->left = rows; r->spent = 0;
    r->coarse = coarse; r->walk_rows = rows; r->walk_up = up;
    r->scan_kind = up ? UP_COUNT : DOWN;
    r->block_end = 0;
    if (up && rows && r->pos) { r->pos--; r->spent++; }
}
static int relative_run(scroll_resolver *r, slice *b, uint64_t *out)
{
    if (r->scan_kind == ALIGN) return align_run(r, b, out);
    if (!r->left) { *out = r->next.first_byte; r->scan_kind = 0; return SCROLL_OK; }
    if (r->scan_kind == DOWN) {
        while (r->left && r->pos < r->source.len && r->spent < SCROLL_SCAN_BUDGET) {
            uint64_t end = r->source.len;
            if (end - r->pos > SCROLL_SCAN_BUDGET - r->spent) end = r->pos + SCROLL_SCAN_BUDGET - r->spent;
            const uint8_t *p; size_t n;
            int rc = read_span(b, r->pos, end, &p, &n);
            if (rc) return rc;
            size_t i = 0;
            for (; i < n; i++) if (p[i] == '\n' && --r->left == 0) { i++; break; }
            r->pos += i; r->spent += i;
        }
        if (!r->left) { *out = r->pos; r->scan_kind = 0; return SCROLL_OK; }
        if (r->pos == r->source.len) {
            r->next.first_line = r->next.first_line > r->left ? r->next.first_line - r->left : 0;
            align_start(r, r->pos, r->next.first_byte, r->coarse);
            return align_run(r, b, out);
        }
    } else {
        while ((r->pos || r->block_end) && r->spent < SCROLL_SCAN_BUDGET) {
            if (!r->block_end) {
                uint64_t n = r->pos < SPAN_BYTES ? r->pos : SPAN_BYTES;
                if (n > SCROLL_SCAN_BUDGET - r->spent) n = SCROLL_SCAN_BUDGET - r->spent;
                r->block_end = r->pos; r->start = r->pos - n;
                r->pos = r->start; r->count = 0;
            }
            while (r->pos < r->block_end) {
                const uint8_t *p; size_t n;
                int rc = read_span(b, r->pos, r->block_end, &p, &n);
                if (rc) return rc;
                for (size_t i = 0; i < n; i++) if (p[i] == '\n') {
                    if (r->scan_kind == UP_FIND) {
                        if (!r->nth) { *out = r->pos + i + 1u; r->scan_kind = 0; return SCROLL_OK; }
                        r->nth--;
                    } else r->count++;
                }
                r->pos += n;
            }
            if (r->count >= r->left) {
                r->nth = r->count - r->left; r->pos = r->start; r->scan_kind = UP_FIND;
                continue;
            }
            r->left -= r->count; r->spent += r->block_end - r->start;
            r->pos = r->start; r->block_end = 0;
        }
        if (!r->pos) {
            if (r->left > 1 || !r->next.first_byte) r->next.subrow_q8 = 0;
            *out = 0; r->scan_kind = 0; return SCROLL_OK;
        }
    }
    if (!r->coarse) return SCROLL_MORE;
    uint64_t byte = estimate_byte(&r->next, r->walk_rows, !r->walk_up);
    align_start(r, byte, r->next.first_byte, true);
    return align_run(r, b, out);
}
/* Query index metadata without reading a byte: legacy byte_to_line provides
 * the chunk's baseline count when its first span returns zero. Capture that
 * chunk seed, then count the suffix through our own resumable scanner. The
 * intentionally incomplete legacy result is never published. */
typedef struct seed { uint64_t start; bool requested; } seed;
static size_t seed_span(void *ctx, uint64_t off, const uint8_t **out)
{
    seed *q = ctx; q->start = off; q->requested = true; *out = NULL; return 0;
}
static int byte_line(scroll_resolver *r, slice *b, uint64_t byte, lineidx_result *out)
{
    if (!r->query_active) {
        seed q = {0, false};
        lineidx_src src = {&q, r->source.len, seed_span, NULL};
        lineidx_result value = lineidx_byte_to_line(r->index, &src, byte);
        if (!q.requested) { *out = value; return SCROLL_OK; }
        r->query_active = true; r->query_byte = q.start; r->query_line = value.value;
        r->query_exact = value.exact;
    }
    while (r->query_byte < byte) {
        const uint8_t *p; size_t n;
        int rc = read_span(b, r->query_byte, byte, &p, &n);
        if (rc) return rc;
        for (size_t i = 0; i < n; i++) if (p[i] == '\n') r->query_line++;
        r->query_byte += n;
    }
    *out = (lineidx_result){r->query_line, r->query_exact};
    r->query_active = false; return SCROLL_OK;
}
static void bottom_start(scroll_resolver *r, bool coarse)
{
    r->pos = r->next.first_byte; r->spent = r->count = 0;
    r->coarse = coarse; r->scan_kind = BOTTOM;
}
static int bottom_run(scroll_resolver *r, slice *b)
{
    if (r->scan_kind != BOTTOM) {
        int rc = relative_run(r, b, &r->next.first_byte);
        if (rc) return rc;
        r->next.subrow_q8 = 0; return SCROLL_OK;
    }
    while (r->pos < r->source.len && r->spent < SCROLL_SCAN_BUDGET && r->count < r->next.config.rows) {
        uint64_t end = r->source.len;
        if (end - r->pos > SCROLL_SCAN_BUDGET - r->spent) end = r->pos + SCROLL_SCAN_BUDGET - r->spent;
        const uint8_t *p; size_t n;
        int rc = read_span(b, r->pos, end, &p, &n);
        if (rc) return rc;
        size_t i = 0;
        for (; i < n; i++) if (p[i] == '\n' && ++r->count == r->next.config.rows) { i++; break; }
        r->pos += i; r->spent += i;
    }
    if (r->pos < r->source.len || r->count >= r->next.config.rows) {
        r->scan_kind = 0; return SCROLL_OK;
    }
    uint64_t missing = (uint64_t)r->next.config.rows - 1u - r->count;
    r->next.first_line = r->next.first_line > missing ? r->next.first_line - missing : 0;
    r->next.subrow_q8 = 0;
    if (!missing) { r->scan_kind = 0; return SCROLL_OK; }
    relative_start(r, missing, true, r->coarse);
    return relative_run(r, b, &r->next.first_byte);
}
static int resolve_run(scroll_resolver *r, slice *b)
{
    scroll_state *s = &r->next;
    for (;;) {
        if (r->phase == 0) {
            lineidx_result count = lineidx_line_count(r->index);
            s->extent = (scroll_extent){r->source.len, count.value, count.exact};
            if (s->request == SCROLL_BYTE) { align_start(r, s->target_byte, s->first_byte, true); r->phase = 1; }
            else if (s->request == SCROLL_RELATIVE && s->approximate) {
                relative_start(r, s->pending_rows, s->pending_up, true); r->phase = 2;
            } else r->phase = s->request == SCROLL_READY ? 5u : 3u;
        } else if (r->phase == 1 || r->phase == 2) {
            int rc = r->phase == 1 ? align_run(r, b, &s->first_byte) : relative_run(r, b, &s->first_byte);
            if (rc) return rc;
            r->phase = 4;
        } else if (r->phase == 3) {
            lineidx_src src = {b, r->source.len, limited_span, NULL};
            lineidx_result byte = lineidx_seek_line(r->index, &src, s->first_line, b->left);
            if (b->status) return b->status;
            if (!byte.exact) return SCROLL_MORE;
            s->first_byte = byte.value; r->phase = 4;
        } else if (r->phase == 4) {
            if (!r->before.extent.exact || s->request == SCROLL_BYTE) { bottom_start(r, true); r->phase = 6; }
            else r->phase = 5;
        } else if (r->phase == 6) {
            int rc = bottom_run(r, b); if (rc) return rc; r->phase = 5;
        } else {
            lineidx_result line;
            int rc = byte_line(r, b, s->first_byte, &line); if (rc) return rc;
            if (line.exact || s->request == SCROLL_BYTE) s->first_line = line.value;
            s->approximate = !line.exact;
            if (s->request != SCROLL_READY || line.exact) s->anchor_line = s->first_line;
            s->request = SCROLL_READY; s->pending_rows = 0;
            lineidx_result count = lineidx_line_count(r->index);
            s->extent = (scroll_extent){r->source.len, count.value, count.exact};
            r->phase = 0; return SCROLL_OK;
        }
    }
}
static int follow_run(scroll_resolver *r, slice *b)
{
    scroll_state *s = &r->next;
    for (;;) {
        int rc;
        if (r->follow_phase == 0) {
            rc = resolve_run(r, b); if (rc) return rc; r->follow_phase = 1;
        } else if (r->follow_phase == 1) {
            lineidx_result cursor;
            rc = byte_line(r, b, r->cursor, &cursor); if (rc) return rc;
            r->query_line = cursor.value; r->query_exact = cursor.exact;
            if (cursor.exact && !s->approximate) {
                rc = scroll_follow(s, cursor.value); if (rc) return rc;
                r->follow_phase = 8; continue;
            }
            uint32_t margin = s->config.margin, limit = (s->config.rows - 1u) / 2u;
            if (margin > limit) margin = limit;
            r->slot = margin;
            if (r->cursor >= s->first_byte) {
                if (r->cursor - s->first_byte <= SCROLL_SCAN_BUDGET) {
                    r->pos = s->first_byte; r->end = r->cursor; r->count = 0;
                    r->follow_phase = 2; continue;
                }
                r->slot = s->config.rows - margin - 1u;
            }
            r->follow_phase = 3;
        } else if (r->follow_phase == 2) {
            while (r->pos < r->end) {
                const uint8_t *p; size_t n;
                rc = read_span(b, r->pos, r->end, &p, &n); if (rc) return rc;
                for (size_t i = 0; i < n; i++) if (p[i] == '\n') r->count++;
                r->pos += n;
            }
            uint64_t margin = r->slot;
            if (r->count >= margin && r->count < s->config.rows - margin &&
                (r->count != margin || !s->subrow_q8)) return SCROLL_OK;
            if (r->count >= s->config.rows - margin) r->slot = s->config.rows - margin - 1u;
            r->follow_phase = 3;
        } else if (r->follow_phase == 3) {
            if (r->query_exact) r->follow_phase = 4;
            else { align_start(r, r->cursor, s->first_byte, false); r->follow_phase = 5; }
        } else if (r->follow_phase == 4) {
            lineidx_src src = {b, r->source.len, limited_span, NULL};
            lineidx_result byte = lineidx_seek_line(r->index, &src, r->query_line, b->left);
            if (b->status) return b->status;
            if (!byte.exact) return SCROLL_MORE;
            s->first_byte = byte.value; r->follow_phase = 6;
        } else if (r->follow_phase == 5) {
            rc = align_run(r, b, &s->first_byte); if (rc) return rc; r->follow_phase = 6;
        } else if (r->follow_phase == 6) {
            s->first_line = r->query_line > r->slot ? r->query_line - r->slot : 0;
            s->pending_rows = r->slot; s->pending_up = true; s->subrow_q8 = 0;
            relative_start(r, r->slot, true, false); r->follow_phase = 7;
        } else if (r->follow_phase == 7) {
            rc = relative_run(r, b, &s->first_byte); if (rc) return rc;
            bottom_start(r, false); r->follow_phase = 9;
        } else if (r->follow_phase == 8) return resolve_run(r, b);
        else if (r->follow_phase == 9) {
            rc = bottom_run(r, b); if (rc) return rc; r->follow_phase = 10;
        } else {
            lineidx_result top;
            rc = byte_line(r, b, s->first_byte, &top); if (rc) return rc;
            s->first_line = s->anchor_line = top.value; s->approximate = !top.exact;
            s->pending_rows = 0; s->request = SCROLL_READY; return SCROLL_OK;
        }
    }
}
static int run_slice(scroll_state *s, scroll_resolver *r, lineidx *index,
                     const lineidx_src *source, uint64_t cursor, bool following,
                     uint64_t budget, uint64_t deadline)
{
    if (!s || !r || !index || !source || !source->span || source->len != lineidx_len(index) ||
        source->len != s->extent.bytes || budget > SCROLL_SCAN_BUDGET ||
        (s->request != SCROLL_LINE && s->request != SCROLL_BYTE && s->first_byte > source->len) ||
        (s->request == SCROLL_BYTE && s->target_byte > source->len) ||
        !s->config.rows || !s->config.row_height ||
        (following && (cursor > source->len || s->request != SCROLL_READY))) return SCROLL_ERR_ARG;
    if (!r->active || r->following != following || r->cursor != cursor || r->index != index ||
        r->source.ctx != source->ctx || r->source.span != source->span || r->source.len != source->len ||
        memcmp(&r->before, s, sizeof *s)) {
        memset(r, 0, sizeof *r); r->active = true; r->before = r->next = *s;
        r->index = index; r->source = *source; r->cursor = cursor; r->following = following;
    }
    uint64_t current = now_ns();
    slice b = {source, budget ? budget : SCROLL_SCAN_BUDGET,
        deadline ? deadline : current + SLICE_NS, 0, SCROLL_OK};
    int rc = following ? follow_run(r, &b) : resolve_run(r, &b);
    if (b.status) rc = b.status;
    if (rc == SCROLL_OK) *s = r->next;
    if (rc != SCROLL_MORE || !b.status) r->active = false;
    return rc;
}
int scroll_resolve_slice(scroll_state *s, scroll_resolver *r, lineidx *index,
                         const lineidx_src *source, uint64_t budget, uint64_t deadline_ns)
{ return run_slice(s, r, index, source, 0, false, budget, deadline_ns); }
int scroll_follow_cursor_slice(scroll_state *s, scroll_resolver *r, lineidx *index,
                               const lineidx_src *source, uint64_t cursor_byte,
                               uint64_t budget, uint64_t deadline_ns)
{ return run_slice(s, r, index, source, cursor_byte, true, budget, deadline_ns); }
int scroll_resolve(scroll_state *s, lineidx *index, const lineidx_src *source, uint64_t budget)
{
    scroll_resolver r = {0};
    return scroll_resolve_slice(s, &r, index, source, budget, 0);
}
int scroll_follow_cursor(scroll_state *s, lineidx *index, const lineidx_src *source, uint64_t cursor_byte)
{
    scroll_resolver r = {0};
    return scroll_follow_cursor_slice(s, &r, index, source, cursor_byte, 0, 0);
}
