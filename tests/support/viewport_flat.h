#ifndef TESTS_SUPPORT_VIEWPORT_FLAT_H
#define TESTS_SUPPORT_VIEWPORT_FLAT_H

#include "viewport/viewport.h"
#include "utf8/utf8.h"

#include <assert.h>
#include <stdlib.h>

/* A29 and S1. The flat oracle eagerly constructs grid/row boundaries once.
 * The metered port then hands out only the evidence asked for by a motion. */
typedef struct {
    size_t row, next;
    uint64_t column;
} viewport_flat_byte;

typedef enum { FLAT_READY, FLAT_DELAYED, FLAT_COLUMN } flat_query_state;

typedef struct {
    const uint8_t *text;
    size_t length, rows, wrap_cols;
    viewport_row_cert *row;
    uint64_t *lines;
    viewport_flat_byte *at;
    size_t line_count, counted, window_cap;
    size_t calls[VP_LOGICAL_START + 1], bytes, row_hops, work;
    size_t column_cursor, column_visits;
    viewport_request neighbors[2]; /* two operands per page (S2) */
    size_t neighbor_requests;
    bool delay;
    flat_query_state state;
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
            for (size_t i = pos; i < pos + used; ++i)
                f->at[i] = (viewport_flat_byte){.next = pos + used, .column = col};
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
    viewport_flat f = {.text = text, .length = len, .wrap_cols = wrap_cols, .window_cap = VIEWPORT_GRID_BYTES};
    f.row = calloc(len + 1, sizeof *f.row);
    f.lines = calloc(len + 1, sizeof *f.lines);
    f.at = calloc(len + 1, sizeof *f.at);
    assert(f.row && f.lines && f.at);
    f.line_count = 1;
    f.counted = len;
    size_t first = 0, last_hard = 0, seam = 0;
    bool synthetic = false;
    for (size_t i = 0; i < len; ++i) {
        if (i && i % VIEWPORT_GRID_BYTES == 0 && last_hard <= i - VIEWPORT_GRID_BYTES) {
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
    for (size_t r = 0; r < f.rows; ++r) {
        uint64_t stop = r + 1 < f.rows ? f.row[r + 1].byte : len + 1;
        for (uint64_t i = f.row[r].byte; i < stop; ++i)
            f.at[i].row = r;
    }
    return f;
}

static inline void flat_close(viewport_flat *f)
{
    free(f->row);
    free(f->lines);
    free(f->at);
}

static inline size_t flat_row_at(const viewport_flat *f, uint64_t byte, bool trailing)
{
    assert(byte <= f->length);
    size_t lo = f->at[byte].row;
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

static inline uint64_t flat_screen_column(const viewport_flat *f, viewport_row_cert row,
                                            uint64_t byte)
{
    uint64_t logical = byte >= row.end ? row.end_column : f->at[byte].column;
    return row.indent + logical - row.col;
}

static inline viewport_progress flat_column_query(viewport_flat *f, const viewport_request *r,
                                                  size_t *credit, viewport_evidence *e, bool resume)
{
    viewport_row_cert row = f->row[flat_row_at(f, r->as.column.row_byte, false)];
    if (!resume)
        f->column_cursor = (size_t)row.byte;
    const uint64_t want = r->as.column.screen_column;
    while (f->column_cursor < row.end) {
        if (!*credit) {
            f->state = FLAT_COLUMN;
            f->pending = *r;
            return VP_MORE;
        }
        --*credit;
        ++f->work;
        ++f->column_visits;
        uint64_t col = flat_screen_column(f, row, f->column_cursor);
        size_t next = f->at[f->column_cursor].next;
        if (col >= want || flat_screen_column(f, row, next) > want)
            break;
        f->column_cursor = next;
    }
    if (row.byte == row.end) {
        --*credit;
        ++f->work;
    }
    e->point = f->column_cursor;
    return VP_DONE;
}

static inline viewport_progress flat_query(void *ctx, const viewport_request *r,
                                           size_t *credit, viewport_evidence *e)
{
    viewport_flat *f = ctx;
    assert(*credit);
    ++f->calls[r->kind];
    flat_query_state previous = f->state;
    if (f->delay && previous == FLAT_READY) {
        f->pending = *r;
        f->state = FLAT_DELAYED;
        return VP_MORE;
    }
    if (previous != FLAT_READY)
        assert(flat_same_request(*r, f->pending));
    f->state = FLAT_READY;
    *e = (viewport_evidence){0};
    size_t i;
    switch (r->kind) {
    case VP_WINDOW: {
        uint64_t byte = r->as.window.byte;
        assert(byte < f->length);
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
        f->work += e->len;
        return VP_DONE;
    }
    case VP_ROW:
        e->row = f->row[flat_row_at(f, r->as.row.byte, r->as.row.trailing)];
        e->screen_column = flat_screen_column(f, e->row, r->as.row.byte);
        break;
    case VP_NEIGHBOR: {
        f->neighbors[f->neighbor_requests++ % 2] = *r;
        size_t src = flat_row_at(f, r->as.neighbor.byte, r->as.neighbor.trailing);
        int64_t delta = r->as.neighbor.displacement;
        uint64_t hops = delta < 0 ? (uint64_t)(-(delta + 1)) + 1 : (uint64_t)delta;
        size_t limit = delta < 0 ? src : f->rows - 1 - src;
        if (hops > limit)
            hops = limit;
        f->row_hops += (size_t)hops;
        i = delta < 0 ? src - (size_t)hops : src + (size_t)hops;
        e->row = f->row[i];
        break;
    }
    case VP_COLUMN:
        return flat_column_query(f, r, credit, e, previous == FLAT_COLUMN);
    case VP_LOGICAL_RANK:
        i = flat_row_at(f, r->as.logical_rank.byte, false);
        e->count = f->row[i].exact_line;
        e->exact = r->as.logical_rank.byte <= f->counted;
        break;
    case VP_LOGICAL_START:
        i = (size_t)r->as.logical_start.line;
        e->off = i < f->line_count ? f->lines[i] : f->length;
        e->count = i < f->line_count ? i : f->line_count - 1;
        e->exact = e->off <= f->counted;
        if (!e->exact) {
            e->off = f->counted;
            i = flat_row_at(f, e->off, false);
            e->count = f->row[i].exact_line;
        }
        break;
    }
    --*credit;
    ++f->work;
    return VP_DONE;
}

static inline viewport_answer flat_motion(viewport_motion *m, viewport_flat *f,
                                          viewport_intent in, viewport_frame frame,
                                          size_t grant)
{
    viewport_motion_begin(m, &in, frame, f->length);
    viewport_port port = {f, flat_query};
    viewport_answer out = {0};
    size_t credit = 0;
    assert(viewport_motion_step(m, &port, &credit, &out) == VP_MORE);
    assert(out.intent_serial == 0);
    for (;;) {
        credit = grant;
        size_t before = f->work;
        viewport_progress progress = viewport_motion_step(m, &port, &credit, &out);
        assert(credit <= grant && f->work - before <= grant - credit);
        if (progress == VP_DONE) {
            assert(out.intent_serial == in.serial);
            return out;
        }
        assert(progress == VP_MORE && out.intent_serial == 0);
    }
}

#endif
