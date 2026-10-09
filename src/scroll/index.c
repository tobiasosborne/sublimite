#include "scroll/scroll.h"

static int source_span(const lineidx_src *src, uint64_t off, uint64_t max,
                       const uint8_t **bytes, size_t *count)
{
    *count = src->span(src->ctx, off, bytes);
    if (!*count || !*bytes) return SCROLL_ERR_SOURCE;
    uint64_t remaining = src->len - off;
    if (*count > remaining) *count = (size_t)remaining;
    if (*count > max) *count = (size_t)max;
    return SCROLL_OK;
}
/* A real line start <= target. No newline in the bounded suffix: retain the
 * last known earlier boundary (0 is always valid), explicitly approximate.
 * Following requires proof that no intervening newline was skipped. */
static int align_byte(const lineidx_src *src, uint64_t target, uint64_t known,
                      bool allow_coarse, uint64_t *out)
{
    uint64_t lo = target > SCROLL_SCAN_BUDGET ? target - SCROLL_SCAN_BUDGET : 0;
    uint64_t pos = lo, start = known <= lo ? known : 0;
    while (pos < target) {
        const uint8_t *p = NULL; size_t n;
        int rc = source_span(src, pos, target - pos, &p, &n);
        if (rc) return rc;
        for (size_t i = 0; i < n; i++) if (p[i] == '\n') start = pos + i + 1;
        pos += n;
    }
    if (!allow_coarse && start < lo) return SCROLL_MORE;
    *out = start;
    return SCROLL_OK;
}
static uint64_t estimate_byte(const scroll_state *s, uint64_t rows, bool down)
{
    uint64_t density = s->extent.bytes / s->extent.lines;
    if (!density) density = 1;
    uint64_t available = down ? s->extent.bytes - s->first_byte : s->first_byte;
    uint64_t delta = rows > available / density ? available : rows * density;
    return down ? s->first_byte + delta : s->first_byte - delta;
}
static int relative_byte(scroll_state *s, const lineidx_src *src,
                         bool allow_coarse, uint64_t *out)
{
    bool down = !s->pending_up;
    uint64_t rows = s->pending_rows;
    if (!rows) { *out = s->first_byte; return SCROLL_OK; }
    uint64_t pos = s->first_byte, left = rows, spent = 0;
    if (down) {
        while (left && pos < src->len && spent < SCROLL_SCAN_BUDGET) {
            const uint8_t *p = NULL; size_t n;
            int rc = source_span(src, pos, SCROLL_SCAN_BUDGET - spent, &p, &n);
            if (rc) return rc;
            size_t i = 0;
            for (; i < n; i++) if (p[i] == '\n' && --left == 0) { i++; break; }
            pos += i; spent += i;
        }
        if (!left) { *out = pos; return SCROLL_OK; }
        if (pos == src->len) {
            s->first_line = s->first_line > left ? s->first_line - left : 0;
            return align_byte(src, pos, s->first_byte, allow_coarse, out);
        }
    } else {
        if (left && pos) { pos--; spent++; }
        while (left && pos && spent < SCROLL_SCAN_BUDGET) {
            uint64_t nbytes = pos < 4096 ? pos : 4096;
            if (nbytes > SCROLL_SCAN_BUDGET - spent) nbytes = SCROLL_SCAN_BUDGET - spent;
            uint64_t lo = pos - nbytes, ppos = lo, count = 0;
            /* Fragmented source: count a backward block's newlines, then
             * locate the desired newline in forward order. */
            while (ppos < pos) {
                const uint8_t *p = NULL; size_t n;
                int rc = source_span(src, ppos, pos - ppos, &p, &n);
                if (rc) return rc;
                for (size_t i = 0; i < n; i++) if (p[i] == '\n') count++;
                ppos += n;
            }
            if (count >= left) {
                uint64_t nth = count - left; ppos = lo;
                while (ppos < pos) {
                    const uint8_t *p = NULL; size_t n;
                    int rc = source_span(src, ppos, pos - ppos, &p, &n);
                    if (rc) return rc;
                    for (size_t i = 0; i < n; i++) if (p[i] == '\n') {
                        if (!nth) { *out = ppos + i + 1; return SCROLL_OK; }
                        nth--;
                    }
                    ppos += n;
                }
            }
            left -= count; spent += nbytes; pos = lo;
        }
        if (!pos) {
            if (rows && (left > 1 || !s->first_byte)) s->subrow_q8 = 0;
            *out = 0; return SCROLL_OK;
        }
    }
    if (!allow_coarse) return SCROLL_MORE;
    return align_byte(src, estimate_byte(s, rows, down), s->first_byte, true, out);
}
/* Unknown line counts still obey EOF. Keep a full viewport where a bounded
 * suffix scan can prove the end. Idle publication never calls this helper. */
static int clamp_bottom(scroll_state *s, const lineidx_src *src, bool allow_coarse)
{
    uint64_t pos = s->first_byte, spent = 0, newlines = 0;
    while (pos < src->len && spent < SCROLL_SCAN_BUDGET && newlines < s->config.rows) {
        const uint8_t *p = NULL; size_t n;
        int rc = source_span(src, pos, SCROLL_SCAN_BUDGET - spent, &p, &n);
        if (rc) return rc;
        size_t i = 0;
        for (; i < n; i++) if (p[i] == '\n' && ++newlines == s->config.rows) { i++; break; }
        pos += i; spent += i;
    }
    if (pos < src->len || newlines >= s->config.rows) return SCROLL_OK;
    uint64_t missing = (uint64_t)s->config.rows - 1 - newlines;
    if (missing) {
        scroll_state back = *s;
        back.pending_rows = missing; back.pending_up = true;
        uint64_t byte;
        int rc = relative_byte(&back, src, allow_coarse, &byte);
        if (rc) return rc;
        s->first_byte = byte;
        s->first_line = s->first_line > missing ? s->first_line - missing : 0;
    }
    s->subrow_q8 = 0;
    return SCROLL_OK;
}
int scroll_resolve(scroll_state *s, lineidx *index, const lineidx_src *source, uint64_t budget)
{
    if (!s || !index || !source || !source->span || source->len != lineidx_len(index) ||
        source->len != s->extent.bytes || budget > SCROLL_SCAN_BUDGET ||
        (s->request != SCROLL_LINE && s->request != SCROLL_BYTE && s->first_byte > source->len) ||
        (s->request == SCROLL_BYTE && s->target_byte > source->len) ||
        !s->config.rows || !s->config.row_height) return SCROLL_ERR_ARG;
    scroll_state next = *s;
    lineidx_result count = lineidx_line_count(index);
    next.extent = (scroll_extent){source->len, count.value, count.exact};
    int rc = SCROLL_OK;
    bool new_byte = s->request != SCROLL_READY;
    if (s->request == SCROLL_BYTE) {
        rc = align_byte(source, s->target_byte, s->first_byte, true, &next.first_byte);
    } else if (s->request == SCROLL_RELATIVE && s->approximate) {
        /* A just-published prefix must not reinterpret a pending relative
         * delta as an absolute request in the old estimated line space. */
        rc = relative_byte(&next, source, true, &next.first_byte);
    } else if (s->request != SCROLL_READY) {
        lineidx_result byte = budget ? lineidx_seek_line(index, source, s->first_line, budget) :
            lineidx_line_to_byte(index, source, s->first_line);
        if (byte.exact || s->request == SCROLL_LINE) next.first_byte = byte.value;
        else rc = relative_byte(&next, source, true, &next.first_byte);
    }
    if (rc) return rc;
    if (new_byte && (!s->extent.exact || s->request == SCROLL_BYTE)) {
        rc = clamp_bottom(&next, source, true);
        if (rc) return rc;
    }
    lineidx_result line = lineidx_byte_to_line(index, source, next.first_byte);
    if (line.exact || s->request == SCROLL_BYTE) next.first_line = line.value;
    next.approximate = !line.exact;
    /* Publication relabels the physical viewport, never seeks from its old
     * density estimate and never follows a cursor after pure scrolling. */
    if (new_byte || line.exact) next.anchor_line = next.first_line;
    next.request = SCROLL_READY;
    next.pending_rows = 0;
    count = lineidx_line_count(index);
    next.extent = (scroll_extent){source->len, count.value, count.exact};
    *s = next;
    return SCROLL_OK;
}
int scroll_follow_cursor(scroll_state *s, lineidx *index,
                         const lineidx_src *source, uint64_t cursor_byte)
{
    if (!s || !source || cursor_byte > source->len || s->request != SCROLL_READY) return SCROLL_ERR_ARG;
    scroll_state next = *s;
    int rc = scroll_resolve(&next, index, source, 0);
    if (rc) return rc;
    lineidx_result cursor = lineidx_byte_to_line(index, source, cursor_byte);
    if (cursor.exact && !next.approximate) {
        rc = scroll_follow(&next, cursor.value);
        if (!rc) rc = scroll_resolve(&next, index, source, 0);
        if (!rc) *s = next;
        return rc;
    }
    uint32_t margin = next.config.margin, limit = (next.config.rows - 1) / 2;
    if (margin > limit) margin = limit;
    uint64_t slot = margin;
    if (cursor_byte >= next.first_byte) {
        uint64_t distance = cursor_byte - next.first_byte, rows = 0;
        if (distance <= SCROLL_SCAN_BUDGET) {
            uint64_t pos = next.first_byte;
            while (pos < cursor_byte) {
                const uint8_t *p = NULL; size_t n;
                rc = source_span(source, pos, cursor_byte - pos, &p, &n);
                if (rc) return rc;
                for (size_t i = 0; i < n; i++) if (p[i] == '\n') rows++;
                pos += n;
            }
            if (rows >= margin && rows < next.config.rows - margin &&
                (rows != margin || !next.subrow_q8)) { *s = next; return SCROLL_OK; }
            if (rows >= next.config.rows - margin) slot = next.config.rows - margin - 1u;
        } else slot = next.config.rows - margin - 1u;
    }
    uint64_t start;
    if (cursor.exact) start = lineidx_line_to_byte(index, source, cursor.value).value;
    else {
        rc = align_byte(source, cursor_byte, next.first_byte, false, &start);
        if (rc) return rc;
    }
    next.first_byte = start;
    next.first_line = cursor.value > slot ? cursor.value - slot : 0;
    next.pending_rows = slot; next.pending_up = true; next.subrow_q8 = 0;
    rc = relative_byte(&next, source, false, &next.first_byte);
    if (!rc) rc = clamp_bottom(&next, source, false);
    if (rc) return rc;
    lineidx_result top = lineidx_byte_to_line(index, source, next.first_byte);
    next.first_line = top.value; next.anchor_line = top.value; next.approximate = !top.exact;
    next.pending_rows = 0; next.request = SCROLL_READY;
    *s = next;
    return SCROLL_OK;
}
