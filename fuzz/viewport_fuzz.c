#include "../tests/support/viewport_flat.h"

static bool same(viewport_answer a, viewport_answer b)
{
    return a.key.buffer == b.key.buffer && a.key.text == b.key.text &&
        a.key.geometry == b.key.geometry && a.intent_serial == b.intent_serial &&
        a.top.byte == b.top.byte && a.top.hscroll_cols == b.top.hscroll_cols &&
        a.top.subrow_q8 == b.top.subrow_q8 && a.top.visual_end == b.top.visual_end &&
        a.selection.caret == b.selection.caret && a.selection.mark == b.selection.mark &&
        a.selection.preferred_col == b.selection.preferred_col;
}

/* Use the oracle's emitted interval and a whole-input segmentation pass,
 * independent of viewport's grid discovery and split-grant state. */
static uint64_t flat_cluster_move(const viewport_flat *f, uint64_t caret, bool right)
{
    if ((right && caret == f->length) || (!right && !caret))
        return caret;
    viewport_row_cert row = f->row[flat_row_at(f, right ? caret : caret - 1, false)];
    if (row.newline && (right ? caret >= row.end : caret > row.end))
        return right ? row.next : row.end;
    for (uint64_t pos = row.byte; pos < row.end;) {
        utf8_gseg g = {0};
        size_t used, n = (size_t)(row.end - pos);
        int result = utf8_grapheme_step(&g, f->text + pos, n, n, 1, &used);
        assert(result == UTF8_G_END && used);
        uint64_t next = pos + used;
        if (right ? next > caret : next >= caret)
            return right ? next : pos;
        pos = next;
    }
    return row.end;
}

static size_t flat_rank(const viewport_flat *f, uint64_t byte)
{
    size_t line = f->line_count - 1;
    while (f->lines[line] > byte)
        --line;
    return line;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t len)
{
    /* S1: one nominal grid block plus one aligned successor is enough to
     * cover fragmented units; cap the eager oracle at one maximum span. */
    if (len < 4 || len > FLAT_D)
        return 0;
    size_t wraps = data[0] % (len + 1);
    viewport_flat f = flat_open(data + 4, len - 4, wraps);
    viewport_motion *m = malloc(viewport_motion_size());
    viewport_motion *fresh = malloc(viewport_motion_size());
    assert(m && fresh);
    viewport_frame frame;
    assert(viewport_plan((viewport_anchor){0}, (uint32_t)len, data[1], 1, &frame));

    viewport_intent in = {.key = {1, 1, 1}, .serial = 1,
        .action = (viewport_action)(data[0] % (VIEWPORT_SELECT_LINE + 1)),
        .origin.wrap = wraps != 0,
        .target = data[1],
        .pixels_q8 = (int64_t)data[1] - data[2],
        .extend = (data[3] & 1u) != 0};
    size_t i = data[2] % f.rows, j = data[3] % f.rows;
    in.origin.top.byte = f.row[j].byte;
    uint64_t caret = flat_col(&f, f.row[i], f.length, data[1]);
    uint64_t mark = (data[3] & 2u) ? f.row[j].byte : caret;
    in.selection = (viewport_selection){caret, mark, UINT64_MAX};
    if (in.action == VIEWPORT_SEEK_LINE)
        in.target %= f.line_count;
    viewport_answer a = flat_motion(m, &f, in, frame, UTF8_GRAPHEME_BUDGET);
    f.counted = in.action == VIEWPORT_SEEK_LINE ? f.length : data[1] % (f.length + 1);
    f.window_cap = 1 + data[2] % 4; /* force splits within UTF-8 units */
    f.delay = true;
    viewport_answer b = flat_motion(fresh, &f, in, frame, 1 + data[3] % 4);
    assert(same(a, b));
    assert(a.selection.caret <= f.length && a.selection.mark <= f.length && a.top.byte <= f.length);
    uint64_t lo = caret < mark ? caret : mark, hi = caret < mark ? mark : caret;
    if (in.action == VIEWPORT_SELECT_LINE) {
        uint64_t first = f.lines[flat_rank(&f, lo)];
        size_t last = flat_rank(&f, hi);
        if (hi == lo || f.lines[last] != hi)
            ++last;
        uint64_t end = last < f.line_count ? f.lines[last] : f.length;
        if (lo != hi && lo == first && hi == end)
            end = ++last < f.line_count ? f.lines[last] : f.length;
        assert(a.selection.mark == first && a.selection.caret == end);
    }
    if (in.action == VIEWPORT_LEFT || in.action == VIEWPORT_RIGHT) {
        bool right = in.action == VIEWPORT_RIGHT;
        uint64_t dest = lo != hi && !in.extend ? (right ? hi : lo)
                                              : flat_cluster_move(&f, caret, right);
        assert(a.selection.caret == dest && a.selection.mark == (in.extend ? mark : dest));
    }
    free(fresh);
    free(m);
    flat_close(&f);
    return 0;
}
