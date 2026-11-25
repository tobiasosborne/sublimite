#ifndef TESTS_SUPPORT_VIEWPORT_FLAT_H
#define TESTS_SUPPORT_VIEWPORT_FLAT_H

#include "viewport/viewport.h"
#include "utf8/utf8.h"

#include <assert.h>
#include <stdlib.h>

/* A29 and S1. The flat oracle eagerly constructs grid/row boundaries once.
 * The metered port then hands out only the evidence asked for by a motion. */
enum { FLAT_S = 32768, FLAT_D = 2 * FLAT_S + 4 };
typedef struct {
    const uint8_t *text;
    size_t length, rows, wrap_cols;
    viewport_row_cert *row;
    uint64_t *lines;
    size_t line_count, counted, window_cap;
    size_t calls[VP_LOGICAL_START + 1], bytes, row_hops;
    bool delay, waiting;
    viewport_request pending;
} viewport_flat;

static inline void flat_rows(viewport_flat *f, size_t first, size_t stop, bool synthetic,
                             bool hard, size_t rank)
{
    size_t end = hard ? stop - 1 : stop;
    if (hard && end > first && f->text[end - 1] == '\r')
        --end;
    size_t row = first, pos = first, col = 0, row_col = 0;
    do {
        if (pos < end) {
            utf8_cseg seg = {0};
            size_t used;
            int width;
            int rc = utf8_cluster_step(&seg, f->text + pos, end - pos, end - pos,
                                       1, &used, &width);
            assert(rc == UTF8_G_END && used);
            size_t cells = f->text[pos] == '\t' ? 4 - col % 4 : (size_t)width;
            if (f->wrap_cols && pos > row && col - row_col + cells > f->wrap_cols) {
                f->row[f->rows] = (viewport_row_cert){
                    .byte = row, .end = pos, .next = pos, .line_start = first,
                    .col = row_col, .end_column = col, .ordinal = f->rows,
                    .exact_line = rank, .has_exact_line = true,
                    .continuation = row != first, .approximate = synthetic
                };
                ++f->rows;
                row = pos;
                row_col = col;
            }
            pos += used;
            col += cells;
        }
    } while (pos < end);
    f->row[f->rows] = (viewport_row_cert){
        .byte = row, .end = end, .next = hard || stop < f->length ? stop : UINT64_MAX,
        .line_start = first, .col = row_col, .end_column = col, .ordinal = f->rows,
        .exact_line = rank, .has_exact_line = true, .newline = hard,
        .continuation = row != first, .approximate = synthetic
    };
    ++f->rows;
}

static inline viewport_flat flat_open(const uint8_t *text, size_t len, size_t wrap_cols)
{
    viewport_flat f = {.text = text, .length = len, .wrap_cols = wrap_cols, .window_cap = FLAT_S};
    f.row = calloc(len + 1, sizeof *f.row);
    f.lines = calloc(len + 1, sizeof *f.lines);
    assert(f.row && f.lines);
    f.line_count = 1;
    f.counted = len;
    size_t first = 0, last_hard = 0, seam = 0;
    bool synthetic = false;
    for (size_t i = 0; i < len; ++i) {
        if (i && i % FLAT_S == 0 && last_hard <= i - FLAT_S) {
            seam = i;
            for (size_t lead = i - 3; lead < i; ++lead) {
                utf8_step u = utf8_decode(text + lead, len - lead);
                if (u.valid && lead + u.len > i)
                    seam = lead + u.len;
            }
            if (text[seam - 1] == '\r' && seam < len && text[seam] == '\n')
                ++seam;
        }
        if (i && i == seam && i != first) {
            flat_rows(&f, first, i, synthetic, false, f.line_count - 1);
            first = i;
            synthetic = true;
        }
        if (text[i] == '\n') {
            flat_rows(&f, first, i + 1, synthetic, true, f.line_count - 1);
            f.lines[f.line_count++] = i + 1;
            first = last_hard = i + 1;
            synthetic = false;
        }
    }
    flat_rows(&f, first, len, synthetic, false, f.line_count - 1);
    return f;
}

static inline void flat_close(viewport_flat *f)
{
    free(f->row);
    free(f->lines);
}

static inline size_t flat_row_at(const viewport_flat *f, uint64_t byte, bool trailing)
{
    size_t lo = 0, hi = f->rows;
    while (hi - lo > 1) {
        size_t mid = lo + (hi - lo) / 2;
        if (f->row[mid].byte <= byte)
            lo = mid;
        else
            hi = mid;
    }
    if (trailing && lo && f->row[lo].byte == byte && f->row[lo - 1].end == byte)
        --lo;
    return lo;
}

static inline uint64_t flat_col(const viewport_flat *f, viewport_row_cert row,
                                uint64_t byte, uint64_t want)
{
    uint64_t pos = row.byte, col = row.indent;
    while (pos < row.end && pos < byte) {
        utf8_cseg s = {0};
        size_t used;
        int width;
        size_t n = (size_t)(row.end - pos);
        int rc = utf8_cluster_step(&s, f->text + pos, n, n, 1, &used, &width);
        assert(rc == UTF8_G_END && used);
        uint64_t cells = f->text[pos] == '\t' ? 4 - (row.col + col - row.indent) % 4
                                              : (uint64_t)width;
        if (want != UINT64_MAX && (col >= want || cells > want - col))
            return pos;
        pos += used;
        col += cells;
    }
    return want == UINT64_MAX ? col : pos;
}

static inline bool flat_same_request(viewport_request a, viewport_request b)
{
    if (a.kind != b.kind)
        return false;
    switch (a.kind) {
    case VP_WINDOW: return a.as.window.byte == b.as.window.byte;
    case VP_ROW: return a.as.row.byte == b.as.row.byte && a.as.row.trailing == b.as.row.trailing;
    case VP_NEIGHBOR: return a.as.neighbor.byte == b.as.neighbor.byte &&
        a.as.neighbor.trailing == b.as.neighbor.trailing &&
        a.as.neighbor.displacement == b.as.neighbor.displacement;
    case VP_COLUMN: return a.as.column.row_byte == b.as.column.row_byte &&
        a.as.column.screen_column == b.as.column.screen_column;
    case VP_LOGICAL_RANK: return a.as.logical_rank.byte == b.as.logical_rank.byte;
    case VP_LOGICAL_START: return a.as.logical_start.line == b.as.logical_start.line;
    }
    return false;
}

static inline viewport_progress flat_query(void *ctx, const viewport_request *r,
                                           size_t *credit, viewport_evidence *e)
{
    viewport_flat *f = ctx;
    assert(*credit);
    ++f->calls[r->kind];
    if (f->delay && !f->waiting) {
        f->pending = *r;
        f->waiting = true;
        return VP_MORE;
    }
    if (f->waiting)
        assert(flat_same_request(*r, f->pending));
    f->waiting = false;
    *e = (viewport_evidence){.length = f->length};
    size_t i;
    switch (r->kind) {
    case VP_WINDOW: {
        uint64_t byte = r->as.window.byte;
        if (byte >= f->length) {
            --*credit;
            e->off = f->length;
            return VP_DONE;
        }
        /* Keep a window aligned to its admitted size. Tests vary both the
         * cap and the grant, including windows of a single byte. */
        size_t n = *credit < f->window_cap ? *credit : f->window_cap;
        e->off = byte - byte % n;
        e->len = (size_t)(f->length - e->off);
        if (e->len > n)
            e->len = n;
        e->bytes = f->text + e->off;
        *credit -= e->len;
        f->bytes += e->len;
        return VP_DONE;
    }
    case VP_ROW:
        e->row = f->row[flat_row_at(f, r->as.row.byte, r->as.row.trailing)];
        e->count = flat_col(f, e->row, r->as.row.byte, UINT64_MAX);
        break;
    case VP_NEIGHBOR: {
        size_t src = flat_row_at(f, r->as.neighbor.byte, r->as.neighbor.trailing);
        int64_t delta = r->as.neighbor.displacement;
        uint64_t hops = delta < 0 ? (uint64_t)(-(delta + 1)) + 1 : (uint64_t)delta;
        size_t limit = delta < 0 ? src : f->rows - 1 - src;
        if (hops > limit)
            hops = limit;
        f->row_hops += (size_t)hops;
        i = delta < 0 ? src - (size_t)hops : src + (size_t)hops;
        e->row = f->row[i];
        e->point = e->row.byte;
        break;
    }
    case VP_COLUMN:
        i = flat_row_at(f, r->as.column.row_byte, false);
        e->point = flat_col(f, f->row[i], f->length, r->as.column.screen_column);
        break;
    case VP_LOGICAL_RANK:
        for (i = 1; i < f->line_count && f->lines[i] <= r->as.logical_rank.byte; ++i) {}
        e->count = i - 1;
        e->exact = r->as.logical_rank.byte <= f->counted;
        break;
    case VP_LOGICAL_START:
        i = (size_t)r->as.logical_start.line;
        e->off = i < f->line_count ? f->lines[i] : f->length;
        e->count = i < f->line_count ? i : f->line_count - 1;
        e->exact = e->off <= f->counted;
        if (!e->exact) {
            e->off = f->counted;
            for (i = 1; i < f->line_count && f->lines[i] <= e->off; ++i) {}
            e->count = i - 1;
        }
        break;
    }
    --*credit;
    return VP_DONE;
}

static inline viewport_answer flat_motion(viewport_motion *m, viewport_flat *f,
                                          viewport_intent in, viewport_frame frame,
                                          size_t grant)
{
    viewport_motion_begin(m, &in, frame);
    viewport_port port = {f, flat_query};
    viewport_answer out = {0};
    size_t credit = 0;
    assert(viewport_motion_step(m, &port, &credit, &out) == VP_MORE);
    assert(out.intent_serial == 0);
    for (;;) {
        credit = grant;
        size_t before = f->bytes;
        viewport_progress progress = viewport_motion_step(m, &port, &credit, &out);
        assert(credit <= grant && f->bytes - before <= grant - credit);
        if (progress == VP_DONE) {
            assert(out.intent_serial == in.serial);
            return out;
        }
        assert(out.intent_serial == 0);
    }
}

#endif
