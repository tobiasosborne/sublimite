#include "support/viewport_flat.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

static size_t decoder_bytes, decoder_calls;

int __real_utf8_grapheme_step(utf8_gseg *, const uint8_t *, size_t, size_t, int, size_t *);

int __wrap_utf8_grapheme_step(utf8_gseg *seg, const uint8_t *p, size_t n,
                             size_t budget, int eof, size_t *used)
{
    int result = __real_utf8_grapheme_step(seg, p, n, budget, eof, used);
    decoder_bytes += *used;
    ++decoder_calls;
    return result;
}

#define TEXT(s) (const uint8_t *)(s), (sizeof(s) - 1)

static viewport_intent intent(viewport_action action, uint64_t caret, uint64_t mark)
{
    return (viewport_intent){.key = {1, 2, 3}, .serial = 1, .action = action,
        .selection = {caret, mark, UINT64_MAX}};
}

static void selection(viewport_answer a, uint64_t caret, uint64_t mark, bool trailing)
{
    if (a.selection.caret != caret)
        fprintf(stderr, "caret %" PRIu64 ", expected %" PRIu64 "\n", a.selection.caret, caret);
    assert(a.selection.caret == caret);
    assert(a.selection.mark == mark);
    assert(a.top.visual_end == trailing);
}

static void actions(viewport_motion *m)
{
    /* Three whole rows at ten pixels apiece. */
    viewport_frame frame;
    assert(viewport_plan((viewport_anchor){0}, 80, 30, 10, &frame));
    viewport_flat f = flat_open(TEXT("  foo.bar \r\n12345\nxy\nfinal\n"), 0);
    const struct {
        viewport_action action;
        uint64_t from, to;
    } motions[] = {
        {VIEWPORT_LEFT, 12, 10}, {VIEWPORT_RIGHT, 10, 12},
        {VIEWPORT_WORD_LEFT, 10, 6}, {VIEWPORT_WORD_RIGHT, 0, 5},
        {VIEWPORT_WORD_RIGHT, 10, 12}, {VIEWPORT_HOME, 4, 2},
        {VIEWPORT_HOME, 2, 0}, {VIEWPORT_END, 4, 10},
        {VIEWPORT_DOC_HOME, 10, 0}, {VIEWPORT_DOC_END, 4, 27},
        {VIEWPORT_UP, 16, 4}, {VIEWPORT_DOWN, 4, 16},
        {VIEWPORT_PAGE_DOWN, 4, 25}, {VIEWPORT_PAGE_UP, 25, 4}
    };
    for (size_t i = 0; i < sizeof motions / sizeof *motions; ++i) {
        viewport_intent in = intent(motions[i].action, motions[i].from, motions[i].from);
        viewport_answer a = flat_motion(m, &f, in, frame, 1);
        selection(a, motions[i].to, motions[i].to, false);
        if (in.action == VIEWPORT_PAGE_DOWN)
            assert(a.top.byte == 21);
        if (in.action == VIEWPORT_PAGE_UP)
            assert(a.top.byte == 0);
    }

    viewport_intent in = intent(VIEWPORT_LEFT, 4, 9);
    selection(flat_motion(m, &f, in, frame, 1), 4, 4, false);
    in.action = VIEWPORT_RIGHT;
    selection(flat_motion(m, &f, in, frame, 1), 9, 9, false);
    in.extend = true;
    selection(flat_motion(m, &f, in, frame, 1), 5, 9, false);
    in = intent(VIEWPORT_SELECT_WORD, 4, 4);
    selection(flat_motion(m, &f, in, frame, 1), 5, 2, false);
    in.selection.mark = 1;
    selection(flat_motion(m, &f, in, frame, 1), 4, 1, false);
    in = intent(VIEWPORT_SELECT_WORD, 10, 10);
    selection(flat_motion(m, &f, in, frame, 1), 10, 10, false);
    selection(flat_motion(m, &f, intent(VIEWPORT_SELECT_ALL, 4, 4), frame, 1), 27, 0, false);

    size_t bytes = f.bytes;
    in = intent(VIEWPORT_SELECT_LINE, 12, 3);
    selection(flat_motion(m, &f, in, frame, 1), 12, 0, false);
    in.selection.mark = 0;
    selection(flat_motion(m, &f, in, frame, 1), 18, 0, false);
    assert(f.bytes == bytes);
    f.counted = 12; /* exact ranks and a select blocked after a known LF */
    selection(flat_motion(m, &f, in, frame, 1), 18, 0, false);
    assert(f.bytes - bytes == 6); /* scan only [12,18), no exact-bound rescan */
    f.counted = 15;
    bytes = f.bytes;
    in = intent(VIEWPORT_SELECT_LINE, 12, 12);
    selection(flat_motion(m, &f, in, frame, 1), 18, 12, false);
    assert(f.bytes - bytes == 3); /* inexact select supplies prefix [0,15) */
    f.counted = 0;
    in = intent(VIEWPORT_SELECT_LINE, 15, 13);
    selection(flat_motion(m, &f, in, frame, 1), 18, 12, false);
    size_t ranks = f.calls[VP_LOGICAL_RANK];
    in.selection.mark = in.selection.caret;
    selection(flat_motion(m, &f, in, frame, 1), 18, 12, false);
    assert(f.calls[VP_LOGICAL_RANK] - ranks == 1);

    in = intent(VIEWPORT_RESTORE, 4, 2);
    in.origin.top = (viewport_anchor){UINT64_MAX, 1, 17, true};
    viewport_answer a = flat_motion(m, &f, in, frame, 1);
    assert(a.top.byte == f.length && a.top.subrow_q8 == 17);
    selection(a, 4, 2, true);
    in.origin.top.byte = 12;
    in.action = VIEWPORT_FOLLOW;
    assert(flat_motion(m, &f, in, frame, 1).top.byte == 12);
    in.action = VIEWPORT_SEEK_BYTE;
    in.target = 14;
    assert(flat_motion(m, &f, in, frame, 1).top.byte == 14);
    in.action = VIEWPORT_SEEK_LINE;
    in.target = 2;
    f.counted = f.length;
    assert(flat_motion(m, &f, in, frame, 1).top.byte == 18);
    in.action = VIEWPORT_REFLOW;
    in.selection.preferred_col = 7;
    in.origin.top.subrow_q8 = 10 * 256 + 17;
    a = flat_motion(m, &f, in, frame, 1);
    assert(a.top.byte == 18 && a.top.subrow_q8 == 17 && a.selection.preferred_col == 7);
    in.action = VIEWPORT_SCROLL_Q8;
    in.origin.top.subrow_q8 = 17;
    in.pixels_q8 = -18;
    a = flat_motion(m, &f, in, frame, 1);
    assert(a.top.byte == 0 && a.top.subrow_q8 == 10 * 256 - 1);
    selection(a, 4, 2, true);
    flat_close(&f);
}

static void seams(viewport_motion *m)
{
    /* LF at 0 blocks S; a four-byte scalar at 2S-1 shifts the next
     * eligible seam to 2S+3, producing the S1 span of 2S+2. */
    const size_t seam = 2 * VIEWPORT_GRID_BYTES + 3, len = seam + 4;
    uint8_t *text = malloc(len);
    assert(text);
    memset(text, 'a', len);
    text[0] = '\n';
    memcpy(text + seam - 4, "\xf0\x9f\x98\x80", 4);
    viewport_flat f = flat_open(text, len, 0);
    assert(f.rows == 3 && f.row[1].byte == 1 && f.row[1].next == seam);
    assert(f.row[2].approximate && f.row[2].byte == seam);
    viewport_frame frame;
    assert(viewport_plan((viewport_anchor){0}, 80, 20, 10, &frame));
    selection(flat_motion(m, &f, intent(VIEWPORT_END, 2, 2), frame, 1), seam, seam, true);
    f.window_cap = 3; /* Split a four-byte scalar even at large grants. */
    size_t bytes = f.bytes, windows = f.calls[VP_WINDOW], rows = f.calls[VP_ROW];
    viewport_intent in = intent(VIEWPORT_LEFT, seam, seam);
    selection(flat_motion(m, &f, in, frame, 2), seam - 4, seam - 4, false);
    assert(f.bytes - bytes <= VIEWPORT_DISPLAY_MAX + 2); /* D plus the prefix of one two-byte window */
    assert(f.calls[VP_WINDOW] - windows <= f.bytes - bytes); /* each request delivers at least one byte */
    assert(f.calls[VP_ROW] - rows == 1);
    size_t ranks = f.calls[VP_LOGICAL_RANK], starts = f.calls[VP_LOGICAL_START];
    bytes = f.bytes;
    selection(flat_motion(m, &f, intent(VIEWPORT_HOME, seam + 1, seam + 1), frame, 2),
              seam, seam, false);
    assert(f.calls[VP_LOGICAL_RANK] == ranks && f.calls[VP_LOGICAL_START] == starts);
    assert(f.bytes - bytes <= (len - seam) + 1); /* active row and one aligned-window byte */
    selection(flat_motion(m, &f, intent(VIEWPORT_WORD_LEFT, seam + 2, seam + 2), frame, 9), 1, 1, false);
    selection(flat_motion(m, &f, intent(VIEWPORT_WORD_RIGHT, seam - 4, seam - 4), frame, 9), len, len, false);
    rows = f.calls[VP_ROW];
    selection(flat_motion(m, &f, intent(VIEWPORT_SELECT_WORD, seam, seam), frame, 9), len, 1, false);
    /* M11. Stage each of the two display lines once across both walks. */
    assert(f.calls[VP_ROW] - rows == 2);
    flat_close(&f);

    memset(text, 'a', len);
    text[VIEWPORT_GRID_BYTES - 1] = '\r';
    text[VIEWPORT_GRID_BYTES] = '\n';
    f = flat_open(text, len, 0);
    assert(f.row[0].end == VIEWPORT_GRID_BYTES - 1 && f.row[1].byte == VIEWPORT_GRID_BYTES + 1 && !f.row[1].approximate);
    selection(flat_motion(m, &f, intent(VIEWPORT_RIGHT, VIEWPORT_GRID_BYTES - 1, VIEWPORT_GRID_BYTES - 1), frame, 2),
              VIEWPORT_GRID_BYTES + 1, VIEWPORT_GRID_BYTES + 1, false);
    flat_close(&f);

    memset(text, 0x80, len);
    f = flat_open(text, len, 0);
    assert(f.row[1].byte == VIEWPORT_GRID_BYTES && f.row[1].approximate);
    selection(flat_motion(m, &f, intent(VIEWPORT_LEFT, VIEWPORT_GRID_BYTES + 1, VIEWPORT_GRID_BYTES + 1), frame, 1),
              VIEWPORT_GRID_BYTES, VIEWPORT_GRID_BYTES, false);
    flat_close(&f);
    f = flat_open(text, VIEWPORT_GRID_BYTES, 0);
    assert(f.rows == 1 && f.row[0].next == UINT64_MAX);
    flat_close(&f);

    /* A combining cluster straddles a scalar-safe seam. The two pieces
     * are legal display clusters on their respective sides of the fold. */
    memset(text, 'a', len);
    text[VIEWPORT_GRID_BYTES] = 0xcc;
    text[VIEWPORT_GRID_BYTES + 1] = 0x81;
    f = flat_open(text, VIEWPORT_GRID_BYTES + 2, 0);
    selection(flat_motion(m, &f, intent(VIEWPORT_RIGHT, VIEWPORT_GRID_BYTES - 1, VIEWPORT_GRID_BYTES - 1), frame, 3),
              VIEWPORT_GRID_BYTES, VIEWPORT_GRID_BYTES, false);
    selection(flat_motion(m, &f, intent(VIEWPORT_RIGHT, VIEWPORT_GRID_BYTES, VIEWPORT_GRID_BYTES), frame, 3),
              VIEWPORT_GRID_BYTES + 2, VIEWPORT_GRID_BYTES + 2, false);
    flat_close(&f);
    free(text);
}

static void unicode(viewport_motion *m)
{
    /* The pin's horizontal sequence: tab, wide CJK, combining sequence,
     * emoji ZWJ cluster, malformed standalone byte. */
    viewport_flat f = flat_open(TEXT("\t中é👩‍💻\xff\n123456789"), 0);
    viewport_frame frame;
    assert(viewport_plan((viewport_anchor){0}, 80, 20, 10, &frame));
    const uint64_t edges[] = {0, 1, 4, 7, 18, 19};
    for (size_t i = 1; i < sizeof edges / sizeof *edges; ++i) {
        viewport_intent in = intent(VIEWPORT_RIGHT, edges[i - 1], edges[i - 1]);
        selection(flat_motion(m, &f, in, frame, 1), edges[i], edges[i], false);
        in = intent(VIEWPORT_LEFT, edges[i], edges[i]);
        selection(flat_motion(m, &f, in, frame, 2), edges[i - 1], edges[i - 1], false);
    }
    flat_close(&f);
}

static void affinity(viewport_motion *m)
{
    viewport_flat f = flat_open(TEXT("abcdef\n123456789\nxyz"), 3);
    viewport_frame frame;
    assert(viewport_plan((viewport_anchor){0}, 30, 30, 10, &frame));
    viewport_intent in = intent(VIEWPORT_END, 1, 1);
    in.origin.wrap = true;
    viewport_answer a = flat_motion(m, &f, in, frame, 2);
    selection(a, 3, 3, true);
    in.origin.top = a.top;
    in.selection = a.selection;
    in.action = VIEWPORT_HOME;
    selection(flat_motion(m, &f, in, frame, 2), 0, 0, false);
    in.origin.top.visual_end = false;
    selection(flat_motion(m, &f, in, frame, 2), 3, 3, false);

    /* A continuation can carry synthetic indentation in its certificate. */
    f.row[1].indent = 1;
    viewport_request source = {.kind = VP_ROW, .as.row = {5, false}};
    viewport_evidence evidence;
    size_t evidence_credit = 1;
    assert(flat_query(&f, &source, &evidence_credit, &evidence) == VP_DONE);
    assert(evidence.screen_column == 3 && evidence.count == 0 && evidence_credit == 0);
    in.action = VIEWPORT_UP;
    in.selection.caret = in.selection.mark = 5;
    in.extend = true;
    a = flat_motion(m, &f, in, frame, 2);
    selection(a, 3, 5, true);
    assert(a.selection.preferred_col == 3);
    in.action = VIEWPORT_DOWN;
    in.origin.top = a.top;
    in.selection = a.selection;
    selection(flat_motion(m, &f, in, frame, 2), 5, 5, false);

    /* One Q8 unit obscures the first row and exposes a bottom overscan row.
     * The page consists of the two whole rows between them. */
    in = intent(VIEWPORT_PAGE_DOWN, 0, 0);
    in.origin.wrap = true;
    in.origin.top.subrow_q8 = 1;
    assert(viewport_plan(in.origin.top, 30, 30, 10, &frame));
    size_t hops = f.row_hops;
    a = flat_motion(m, &f, in, frame, 2);
    assert(a.top.byte == 7 && a.selection.caret == 7 && f.row_hops - hops == 4);
    flat_close(&f);
}

static void frame_math(void)
{
    viewport_frame f;
    viewport_anchor a = {.subrow_q8 = 129}; /* half a pixel plus one Q8 unit */
    assert(viewport_plan(a, 80, 20, 10, &f) && f.layout_rows == 3 && f.origin_y_q8 == -129);
    uint32_t row, within;
    assert(viewport_pixel_row(&f, 0, &row, &within) && row == 0 && within == 129);
    assert(viewport_pixel_row(&f, 10 * 256 - 129, &row, &within) && row == 1 && within == 0);
    assert(viewport_pixel_row(&f, 20 * 256 - 1, &row, &within) && row == 2 && within == 128);
    assert(!viewport_pixel_row(&f, 20 * 256, &row, &within));
    assert(!viewport_pixel_row(&f, -1, &row, &within));
    assert(!viewport_plan(a, 80, 20, 0, &f));
    assert(!viewport_plan(a, 80, 20, UINT32_MAX, &f));
    assert(viewport_plan(a, 0, 20, 10, &f) && f.layout_rows == 0);
    assert(viewport_rebase(5, 5, 0, 2, false) == 5);
    assert(viewport_rebase(5, 5, 0, 2, true) == 7);
    assert(viewport_rebase(7, 5, 2, 1, false) == 6);
    assert(viewport_rebase(6, 5, 2, 1, true) == 6);
    assert(viewport_rebase(8, 5, 2, 1, false) == 7);
    assert(viewport_wrap_default("dir.c/README") && viewport_wrap_default("C:\\dir.c\\.notes"));
    assert(viewport_wrap_default("notes.tex") && !viewport_wrap_default("notes.md.c"));
}

static void witness(viewport_motion *m)
{
    /* R8 cold PageUp: three fully visible rows per operand, plus overscan
     * in R. Delay each request independently (R6's viewport boundary). */
    viewport_flat f = flat_open(TEXT("0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ\n"), 3);
    viewport_frame frame;
    assert(viewport_plan((viewport_anchor){0}, 30, 30, 10, &frame));
    f.delay = true;
    viewport_intent in = intent(VIEWPORT_PAGE_UP, 24, 24);
    in.origin.wrap = true;
    in.origin.top.byte = 21;
    viewport_answer a = flat_motion(m, &f, in, frame, 1);
    selection(a, 15, 15, false);
    assert(a.top.byte == 12 && f.row_hops == 6 && f.row_hops <= 2 * frame.layout_rows);
    assert(f.calls[VP_NEIGHBOR] == 4 && f.bytes == 0); /* two operands, one stall each */

    /* Leave a read pending on the prior intent, then let the owner retire it. */
    viewport_port port = {&f, flat_query};
    viewport_motion_begin(m, &in, frame, f.length);
    size_t credit = 1;
    viewport_answer pending = a;
    assert(viewport_motion_step(m, &port, &credit, &pending) == VP_MORE);
    assert(f.state == FLAT_DELAYED && pending.top.byte == a.top.byte);
    f.state = FLAT_READY;

    /* R1/R2/R24: L5's transformed origin and committed caret pass through
     * together, including after supersession. R25: MORE leaves output alone. */
    in = intent(VIEWPORT_FOLLOW, 27, 27);
    in.serial = 2;
    in.origin.top.byte = viewport_rebase(21, 15, 0, 3, false);
    a = flat_motion(m, &f, in, frame, 1);
    assert(a.top.byte == 24 && a.selection.caret == 27);
    viewport_state state = in.origin;
    viewport_selection installed = {0};
    viewport_intent current = in;
    ++current.serial;
    assert(!viewport_accept(&state, &installed, &current, &a) && installed.caret == 0);
    current = in;
    ++current.key.text;
    assert(!viewport_accept(&state, &installed, &current, &a) && installed.caret == 0);
    assert(viewport_accept(&state, &installed, &in, &a) && installed.caret == 27);
    current = in;
    current.origin.wrap = true;
    a.top.hscroll_cols = 9;
    a.selection.preferred_col = 4;
    assert(viewport_accept(&state, &installed, &current, &a));
    assert(state.wrap && state.top.hscroll_cols == 0 && installed.preferred_col == UINT64_MAX);
    a = flat_motion(m, &f, in, frame, 1);

    /* R7 and S4's 10 GiB gate at the viewport boundary. Supply a length
     * without storage so an accidental text/rank scan cannot pass. */
    size_t extent = (size_t)(UINT64_C(10) << 30);
    viewport_flat virtual_file = {.length = extent};
    selection(flat_motion(m, &virtual_file, intent(VIEWPORT_DOC_END, 0, 0), frame, 1),
              extent, extent, false);
    selection(flat_motion(m, &virtual_file, intent(VIEWPORT_SELECT_ALL, 0, 0), frame, 1),
              extent, 0, false);
    assert(!virtual_file.bytes && !virtual_file.calls[VP_LOGICAL_RANK]);
    assert(!virtual_file.calls[VP_WINDOW] && !virtual_file.calls[VP_ROW]);

    /* S9: abandon a decoder mid-walk, then compare a different motion
     * in the reused cursor to the same request in a fresh one. */
    f.delay = false;
    viewport_intent interrupted = intent(VIEWPORT_WORD_LEFT, f.length - 1, f.length - 1);
    viewport_motion_begin(m, &interrupted, frame, f.length);
    size_t decoded = decoder_calls;
    do {
        credit = 1;
        assert(viewport_motion_step(m, &port, &credit, &pending) == VP_MORE);
    } while (decoded == decoder_calls);
    in.action = VIEWPORT_SELECT_WORD;
    a = flat_motion(m, &f, in, frame, 1);
    viewport_flat fresh = flat_open(f.text, f.length, f.wrap_cols);
    viewport_motion *new_cursor = malloc(viewport_motion_size());
    assert(new_cursor);
    viewport_answer b = flat_motion(new_cursor, &fresh, in, frame, UTF8_GRAPHEME_BUDGET);
    assert(a.key.buffer == b.key.buffer && a.key.text == b.key.text && a.key.geometry == b.key.geometry);
    assert(a.intent_serial == b.intent_serial);
    assert(a.top.byte == b.top.byte && a.top.subrow_q8 == b.top.subrow_q8 &&
           a.top.visual_end == b.top.visual_end && a.top.hscroll_cols == b.top.hscroll_cols);
    assert(a.selection.caret == b.selection.caret && a.selection.mark == b.selection.mark &&
           a.selection.preferred_col == b.selection.preferred_col);
    free(new_cursor);
    flat_close(&fresh);
    flat_close(&f);
}

static void empty_snapshot(viewport_motion *m)
{
    viewport_flat empty = {0};
    viewport_frame frame;
    assert(viewport_plan((viewport_anchor){0}, 80, 20, 10, &frame));
    selection(flat_motion(m, &empty, intent(VIEWPORT_DOC_END, 0, 0), frame, 1), 0, 0, false);
    selection(flat_motion(m, &empty, intent(VIEWPORT_SELECT_ALL, 0, 0), frame, 1), 0, 0, false);
    for (size_t i = 0; i < sizeof empty.calls / sizeof *empty.calls; ++i)
        assert(!empty.calls[i]);
}

static void home_review(viewport_motion *m)
{
    /* S1: the first eligible seam starts the second row in S+1 ASCII bytes. */
    uint8_t text[VIEWPORT_GRID_BYTES + 1];
    memset(text, 'a', sizeof text);
    viewport_flat f = flat_open(text, sizeof text, 0);
    viewport_frame frame;
    assert(viewport_plan((viewport_anchor){0}, 80, 20, 10, &frame));
    selection(flat_motion(m, &f, intent(VIEWPORT_HOME, sizeof text, sizeof text), frame, 1),
              VIEWPORT_GRID_BYTES, VIEWPORT_GRID_BYTES, false);
    flat_close(&f);
}

static void reflow_review(viewport_motion *m)
{
    viewport_flat f = flat_open(TEXT("a\nb\nc\n"), 0);
    viewport_frame height_only, wider;
    /* Three ten-pixel rows become two, then the width changes by one pixel. */
    assert(viewport_plan((viewport_anchor){0}, 80, 20, 10, &height_only));
    assert(viewport_plan((viewport_anchor){0}, 81, 20, 10, &wider));
    viewport_intent in = intent(VIEWPORT_REFLOW, 2, 2);
    in.target = UINT64_MAX; /* E7: this field has no REFLOW meaning. */
    in.selection.preferred_col = 7;
    in.origin.top.hscroll_cols = 1;
    viewport_answer a = flat_motion(m, &f, in, height_only, 1);
    assert(a.selection.preferred_col == 7 && a.top.hscroll_cols == 1);
    in.selection.preferred_col = UINT64_MAX; /* Width issuer's E7 reset. */
    a = flat_motion(m, &f, in, wider, 1);
    assert(a.selection.preferred_col == UINT64_MAX);
    in.selection.preferred_col = 7;
    viewport_state installed = in.origin;
    viewport_selection sel = in.selection;
    in.origin.wrap = true;
    a = flat_motion(m, &f, in, height_only, 1);
    assert(viewport_accept(&installed, &sel, &in, &a));
    assert(installed.wrap && installed.top.hscroll_cols == 0 && sel.preferred_col == UINT64_MAX);
    flat_close(&f);
}

static void scroll_review(viewport_motion *m)
{
    viewport_flat f = flat_open(TEXT("a\nb\nc\n"), 0);
    viewport_frame frame;
    assert(viewport_plan((viewport_anchor){0}, 80, 30, 10, &frame));
    enum { ROW_Q8 = 10 * 256 }; /* ten pixels in Q8 */
    viewport_intent in = intent(VIEWPORT_SCROLL_Q8, 2, 2);
    in.origin.top.byte = 2;
    in.pixels_q8 = 1 - ROW_Q8; /* one Q8 unit inside the first row */
    viewport_answer a = flat_motion(m, &f, in, frame, 1);
    assert(a.top.byte == 0 && a.top.subrow_q8 == 1);
    in.pixels_q8 -= ROW_Q8; /* one more row would cross BOF */
    a = flat_motion(m, &f, in, frame, 1);
    assert(a.top.byte == 0 && a.top.subrow_q8 == 0);
    in.origin.top.byte = 0;
    in.pixels_q8 = -1; /* M13: one Q8 unit above BOF */
    a = flat_motion(m, &f, in, frame, 1);
    assert(a.top.byte == 0 && a.top.subrow_q8 == 0);
    in.origin.top.byte = 4;
    in.pixels_q8 = ROW_Q8 + 1;
    a = flat_motion(m, &f, in, frame, 1);
    assert(a.top.byte == f.length && a.top.subrow_q8 == 1);
    in.pixels_q8 += ROW_Q8;
    a = flat_motion(m, &f, in, frame, 1);
    assert(a.top.byte == f.length && a.top.subrow_q8 == 0);
    flat_close(&f);
}

static void decoder_review(viewport_motion *m)
{
    /* One S-byte cluster consisting of four-byte U+E0100 units. */
    uint8_t text[VIEWPORT_GRID_BYTES];
    for (size_t i = 0; i < sizeof text; i += sizeof "\xf3\xa0\x84\x80" - 1)
        memcpy(text + i, "\xf3\xa0\x84\x80", sizeof "\xf3\xa0\x84\x80" - 1);
    viewport_flat f = flat_open(text, sizeof text, 0);
    viewport_frame frame;
    assert(viewport_plan((viewport_anchor){0}, 80, 20, 10, &frame));
    viewport_intent in = intent(VIEWPORT_RIGHT, 0, 0);
    viewport_port port = {&f, flat_query};
    viewport_motion_begin(m, &in, frame, f.length);
    viewport_answer a;
    size_t total = 0, max_decoded = 0;
    viewport_progress progress;
    do {
        size_t grant = UTF8_GRAPHEME_BUDGET, before = decoder_bytes;
        progress = viewport_motion_step(m, &port, &grant, &a);
        size_t decoded = decoder_bytes - before;
        if (decoded > max_decoded)
            max_decoded = decoded;
        total += decoded;
    } while (progress == VP_MORE);
    if (max_decoded > UTF8_GRAPHEME_BUDGET)
        fprintf(stderr, "decoder advanced %zu bytes on a %u-byte grant\n", max_decoded,
                UTF8_GRAPHEME_BUDGET);
    assert(max_decoded <= UTF8_GRAPHEME_BUDGET);
    assert(total == sizeof text);
    selection(a, sizeof text, sizeof text, false);
    flat_close(&f);
}

static void reverse_review(viewport_motion *m)
{
    /* No adjacent ASCII proof: four two-byte clusters share one reverse batch. */
    const uint8_t text[] = "éééé";
    viewport_flat f = flat_open(text, sizeof text - 1, 0);
    viewport_frame frame;
    assert(viewport_plan((viewport_anchor){0}, 80, 20, 10, &frame));
    viewport_intent in = intent(VIEWPORT_WORD_LEFT, f.length, f.length);
    viewport_port port = {&f, flat_query};
    viewport_motion_begin(m, &in, frame, f.length);
    viewport_answer a;
    viewport_progress progress;
    size_t after_decode = 0;
    do {
        size_t credits = 1, before = decoder_calls;
        progress = viewport_motion_step(m, &port, &credits, &a);
        if (decoder_calls != before)
            after_decode = 0;
        else
            after_decode += 1 - credits;
    } while (progress == VP_MORE);
    assert(after_decode >= f.length / (sizeof "é" - 1));
    selection(a, 0, 0, false);
    flat_close(&f);
}

static void grids_review(viewport_motion *m)
{
    /* M2. A two-byte scalar shifts the first seam by one byte. A combining
     * mark after the second grid must stay separate from the preceding a. */
    uint8_t text[2 * VIEWPORT_GRID_BYTES + 2];
    memset(text, 'a', sizeof text);
    memcpy(text + VIEWPORT_GRID_BYTES - 1, "\xc2\xa9", sizeof "\xc2\xa9" - 1);
    memcpy(text + 2 * VIEWPORT_GRID_BYTES, "\xcc\x81", sizeof "\xcc\x81" - 1);
    viewport_flat f = flat_open(text, sizeof text, 0);
    viewport_frame frame;
    assert(viewport_plan((viewport_anchor){0}, 80, 20, 10, &frame));
    uint64_t caret = 2 * VIEWPORT_GRID_BYTES - 1;
    selection(flat_motion(m, &f, intent(VIEWPORT_RIGHT, caret, caret), frame, 1),
              caret + 1, caret + 1, false);
    flat_close(&f);

    /* M1. LF at the start blocks the first grid, so the mark at S
     * still joins the ASCII base at S-1. */
    memset(text, 'a', sizeof text);
    text[0] = '\n';
    memcpy(text + VIEWPORT_GRID_BYTES, "\xcc\x81", sizeof "\xcc\x81" - 1);
    f = flat_open(text, sizeof text, 0);
    caret = VIEWPORT_GRID_BYTES - 1;
    selection(flat_motion(m, &f, intent(VIEWPORT_RIGHT, caret, caret), frame, 1),
              caret + 3, caret + 3, false);
    flat_close(&f);

    /* M4. Each strict candidate lead can overlap the grid. */
    for (size_t gap = 3; gap >= 2; --gap) {
        memset(text, 'a', sizeof text);
        caret = VIEWPORT_GRID_BYTES - gap;
        memcpy(text + caret, "\xf0\x9f\x98\x80", sizeof "\xf0\x9f\x98\x80" - 1);
        f = flat_open(text, sizeof text, 0);
        uint64_t next = caret + sizeof "\xf0\x9f\x98\x80" - 1;
        selection(flat_motion(m, &f, intent(VIEWPORT_RIGHT, caret, caret), frame, 1),
                  next, next, false);
        flat_close(&f);
    }
}

static void accept_review(void)
{
    const viewport_intent in = intent(VIEWPORT_FOLLOW, 1, 1);
    viewport_answer a = {in.key, in.serial, {.byte = 1}, in.selection};
    uint64_t *const keys[] = {&a.key.buffer, &a.key.text, &a.key.geometry, &a.intent_serial};
    for (size_t i = 0; i < sizeof keys / sizeof *keys; ++i) {
        viewport_state state = {{.byte = 2, .hscroll_cols = 3, .subrow_q8 = 4, .visual_end = true}, true};
        viewport_selection sel = {2, 3, 4};
        ++*keys[i];
        assert(!viewport_accept(&state, &sel, &in, &a));
        assert(state.top.byte == 2 && state.top.hscroll_cols == 3 && state.top.subrow_q8 == 4 &&
               state.top.visual_end && state.wrap);
        assert(sel.caret == 2 && sel.mark == 3 && sel.preferred_col == 4);
        --*keys[i];
    }
}

static void page_review(viewport_motion *m)
{
    /* M15. Three full rows, caret and top distinct, top already at BOF. */
    viewport_flat f = flat_open(TEXT("a\nb\nc\n"), 0);
    viewport_frame frame;
    assert(viewport_plan((viewport_anchor){0}, 80, 30, 10, &frame) && frame.layout_rows == 3);
    viewport_intent in = intent(VIEWPORT_PAGE_UP, 4, 4);
    f.delay = true;
    viewport_answer a = flat_motion(m, &f, in, frame, 1);
    assert(a.top.byte == 0 && a.selection.caret == 0);
    assert(f.neighbor_requests == 2 && f.calls[VP_NEIGHBOR] == 4);
    assert(f.neighbors[0].as.neighbor.byte == 4 && f.neighbors[1].as.neighbor.byte == 0);
    for (size_t i = 0; i < f.neighbor_requests; ++i)
        assert(f.neighbors[i].as.neighbor.displacement == -(int64_t)frame.layout_rows);
    flat_close(&f);
}

static void flat_credit(void)
{
    viewport_flat f = flat_open(TEXT("abcd"), 0);
    viewport_request request = {.kind = VP_COLUMN, .as.column = {0, 4}};
    viewport_evidence e;
    viewport_progress progress;
    do {
        size_t grant = 1, before = f.column_visits;
        progress = flat_query(&f, &request, &grant, &e);
        assert(grant == 0 && f.column_visits - before == 1);
    } while (progress == VP_MORE);
    assert(f.column_visits == f.length && e.point == f.length);
    flat_close(&f);
}

static void arrow_cost(viewport_motion *m)
{
    uint8_t text[2 * VIEWPORT_GRID_BYTES];
    memset(text, 'x', sizeof text);
    text[0] = '\n'; /* Blocks the first grid. The ASCII word is deep in one row. */
    const uint64_t at = 60000; /* Owner's deep-Left witness. */
    memcpy(text + at - 2, " abc ", sizeof " abc " - 1);
    viewport_flat f = flat_open(text, sizeof text, 0);
    f.window_cap = 4; /* Split reads at most one UTF-8 scalar wide. */
    viewport_frame frame;
    assert(viewport_plan((viewport_anchor){0}, 80, 20, 10, &frame));
    const struct {
        viewport_action action;
        uint64_t caret, destination, mark;
        size_t decodes;
    } cases[] = {
        {VIEWPORT_LEFT, at, at - 1, at - 1, 1},
        {VIEWPORT_RIGHT, at, at + 1, at + 1, 1},
        {VIEWPORT_WORD_LEFT, at + 2, at - 1, at - 1, 3},
        {VIEWPORT_WORD_RIGHT, at - 1, at + 2, at + 2, 3},
        {VIEWPORT_SELECT_WORD, at, at + 2, at - 1, 3}
    };
    for (size_t i = 0; i < sizeof cases / sizeof *cases; ++i) {
        size_t bytes = f.bytes, decoded = decoder_bytes;
        viewport_intent in = intent(cases[i].action, cases[i].caret, cases[i].caret);
        selection(flat_motion(m, &f, in, frame, UTF8_GRAPHEME_BUDGET), cases[i].destination, cases[i].mark, false);
        assert(decoder_bytes - decoded == cases[i].decodes);
        assert(f.bytes - bytes <= 2 * f.window_cap); /* one aligned window on each side */
    }
    flat_close(&f);
}

static void proof_context(viewport_motion *m)
{
    /* U+0600 is Prepend. A lone ASCII byte does not prove a break before
     * it. In the second input the following ASCII pair does prove one. */
    const uint8_t single[] = "\xd8\x80" "a";
    const uint8_t pair[] = "\xd8\x80" "ab";
    viewport_frame frame;
    assert(viewport_plan((viewport_anchor){0}, 80, 20, 10, &frame));
    viewport_flat f = flat_open(single, sizeof single - 1, 0);
    viewport_intent in = intent(VIEWPORT_LEFT, f.length, f.length);
    selection(flat_motion(m, &f, in, frame, 1), 0, 0, false);
    flat_close(&f);
    f = flat_open(pair, sizeof pair - 1, 0);
    in = intent(VIEWPORT_LEFT, f.length, f.length);
    selection(flat_motion(m, &f, in, frame, 1), f.length - 1, f.length - 1, false);
    flat_close(&f);
}

static void port_rows(viewport_motion *m)
{
    viewport_flat f = flat_open(TEXT("abcdef."), 3);
    viewport_frame frame;
    assert(viewport_plan((viewport_anchor){0}, 80, 20, 10, &frame));
    viewport_intent in = intent(VIEWPORT_WORD_RIGHT, 2, 2);
    in.origin.wrap = true;
    selection(flat_motion(m, &f, in, frame, 1), 6, 6, false);
    assert(f.calls[VP_ROW] == 3); /* one initial certificate and two soft successors */
    flat_close(&f);
}

typedef struct {
    viewport_flat *flat;
    viewport_query blocked;
    bool ready;
} waiting_port;

static viewport_progress wait_for_evidence(void *ctx, const viewport_request *request,
                                           size_t *credits, viewport_evidence *e)
{
    waiting_port *wait = ctx;
    if (!wait->ready && request->kind == wait->blocked)
        return VP_WAIT;
    return flat_query(wait->flat, request, credits, e);
}

static void evidence_wait(viewport_motion *m)
{
    viewport_flat f = flat_open(TEXT("a\nb"), 0);
    viewport_frame frame;
    assert(viewport_plan((viewport_anchor){0}, 80, 20, 10, &frame));
    viewport_intent in;
    viewport_port port;
    viewport_answer a;
    size_t credit;
    const viewport_query blocked[] = {VP_ROW, VP_WINDOW};
    for (size_t i = 0; i < sizeof blocked / sizeof *blocked; ++i) {
        waiting_port wait = {&f, blocked[i], false};
        port = (viewport_port){&wait, wait_for_evidence};
        in = intent(VIEWPORT_RIGHT, 0, 0);
        viewport_motion_begin(m, &in, frame, f.length);
        a = (viewport_answer){0};
        credit = UTF8_GRAPHEME_BUDGET;
        assert(viewport_motion_step(m, &port, &credit, &a) == VP_WAIT);
        assert(credit && !a.intent_serial);
        wait.ready = true;
        assert(viewport_motion_step(m, &port, &credit, &a) == VP_DONE);
        selection(a, 1, 1, false);
    }
    flat_close(&f);
}

static void follow_clamp(viewport_motion *m)
{
    viewport_flat f = {.length = 1};
    viewport_frame frame;
    assert(viewport_plan((viewport_anchor){0}, 80, 20, 10, &frame));
    viewport_intent in = intent(VIEWPORT_FOLLOW, 1, 1);
    in.origin.top.byte = 2;
    viewport_answer a = flat_motion(m, &f, in, frame, 1);
    assert(a.top.byte == 1);
}

/* Astra's rejected cluster has one punctuation lead and 4,000
 * two-byte combining units. Only the initial ASCII a is accepted. */
static void rejected_cluster(viewport_motion *m, viewport_action action)
{
    enum { COPIES = 4000 };
    const char mark[] = "\xcc\x81";
    uint8_t text[sizeof "a.z" - 1 + COPIES * (sizeof mark - 1)];
    text[0] = 'a';
    text[1] = '.';
    for (size_t i = 2; i < sizeof text - 1; i += sizeof mark - 1)
        memcpy(text + i, mark, sizeof mark - 1);
    text[sizeof text - 1] = 'z';
    viewport_flat f = flat_open(text, sizeof text, 0);
    viewport_frame frame;
    assert(viewport_plan((viewport_anchor){0}, 80, 20, 10, &frame));
    size_t before = decoder_bytes;
    viewport_intent in = intent(action, 0, 0);
    selection(flat_motion(m, &f, in, frame, UTF8_GRAPHEME_BUDGET),
              1, action == VIEWPORT_SELECT_WORD ? 0 : 1, false);
    size_t consumed = decoder_bytes - before;
    if (consumed != 1)
        fprintf(stderr, "decoder consumed %zu bytes, expected the accepted ASCII byte\n", consumed);
    assert(consumed == 1);
    flat_close(&f);
}

static void rejected_word(viewport_motion *m) { rejected_cluster(m, VIEWPORT_WORD_RIGHT); }
static void rejected_select(viewport_motion *m) { rejected_cluster(m, VIEWPORT_SELECT_WORD); }

static void synthetic_proof(viewport_motion *m, size_t wrap)
{
    const char mark[] = "\xcc\x81";
    uint8_t text[VIEWPORT_GRID_BYTES + sizeof mark - 1];
    memset(text, 'a', VIEWPORT_GRID_BYTES);
    memcpy(text + VIEWPORT_GRID_BYTES, mark, sizeof mark - 1);
    viewport_flat f = flat_open(text, sizeof text, wrap);
    viewport_frame frame;
    assert(viewport_plan((viewport_anchor){0}, 80, 20, 10, &frame));
    viewport_intent in = intent(VIEWPORT_LEFT, sizeof text, sizeof text);
    in.origin.wrap = wrap != 0;
    size_t before = decoder_bytes;
    selection(flat_motion(m, &f, in, frame, UTF8_GRAPHEME_BUDGET),
              VIEWPORT_GRID_BYTES, VIEWPORT_GRID_BYTES, false);
    assert(decoder_bytes - before == sizeof mark - 1);
    assert(f.calls[VP_ROW] == 1);
    flat_close(&f);
}

static void synthetic_clip(viewport_motion *m) { synthetic_proof(m, 0); }
static void synthetic_wrap(viewport_motion *m) { synthetic_proof(m, 80); }

static void active_row_proof(viewport_motion *m)
{
    enum { COPIES = 4000 }; /* Astra's two-byte units, one per soft row. */
    const char letter[] = "\xc3\xa9";
    uint8_t text[COPIES * (sizeof letter - 1)];
    for (size_t i = 0; i < sizeof text; i += sizeof letter - 1)
        memcpy(text + i, letter, sizeof letter - 1);
    viewport_flat f = flat_open(text, sizeof text, 1);
    viewport_frame frame;
    assert(viewport_plan((viewport_anchor){0}, 1, 20, 10, &frame));
    viewport_intent in = intent(VIEWPORT_LEFT, sizeof text, sizeof text);
    in.origin.wrap = true;
    size_t before = decoder_bytes;
    uint64_t last = sizeof text - (sizeof letter - 1);
    selection(flat_motion(m, &f, in, frame, UTF8_GRAPHEME_BUDGET), last, last, false);
    size_t consumed = decoder_bytes - before;
    if (consumed != sizeof letter - 1)
        fprintf(stderr, "decoder consumed %zu bytes for a %zu-byte LEFT\n", consumed, sizeof letter - 1);
    assert(consumed == sizeof letter - 1);
    assert(f.calls[VP_ROW] == 1);
    flat_close(&f);
}

static void newer_rank(viewport_motion *m)
{
    const uint8_t original[] = "a\nb";
    const uint8_t newer[] = "a\nb";
    viewport_flat first = flat_open(original, sizeof original - 1, 0);
    viewport_flat second = flat_open(newer, sizeof newer - 1, 0);
    first.counted = 0; /* Frozen rank knowledge of the original snapshot. */
    viewport_frame frame;
    assert(viewport_plan((viewport_anchor){0}, 80, 20, 10, &frame));
    viewport_intent in = intent(VIEWPORT_SEEK_LINE, 0, 0);
    in.target = 1;
    viewport_motion_begin(m, &in, frame, first.length);
    viewport_port port = {&first, flat_query};
    viewport_answer a = {0};
    size_t credit = UTF8_GRAPHEME_BUDGET;
    assert(viewport_motion_step(m, &port, &credit, &a) == VP_WAIT);
    assert(a.intent_serial == 0 && first.calls[VP_LOGICAL_START] == 1 && first.counted == 0);
    flat_close(&first); /* Release the old view before the Q10 wake. */
    port.ctx = &second;
    assert(viewport_motion_step(m, &port, &credit, &a) == VP_DONE && a.top.byte == 2);
    assert(a.key.text == in.key.text && a.intent_serial == in.serial &&
           second.calls[VP_LOGICAL_START] == 1 && !second.bytes);
    flat_close(&second);
}

static void interior_walk(viewport_motion *m, const uint8_t *text, size_t len)
{
    viewport_flat f = flat_open(text, len, 0);
    f.window_cap = 1; /* Force the scalar probe to resume across borrows. */
    viewport_frame frame;
    assert(viewport_plan((viewport_anchor){0}, 80, 20, 10, &frame));
    viewport_intent in = intent(VIEWPORT_RIGHT, 1, 1);
    selection(flat_motion(m, &f, in, frame, 1), len, len, false);
    in.action = VIEWPORT_LEFT;
    selection(flat_motion(m, &f, in, frame, 1), 0, 0, false);
    in.action = VIEWPORT_WORD_RIGHT;
    selection(flat_motion(m, &f, in, frame, 1), len, len, false);
    in.action = VIEWPORT_WORD_LEFT;
    selection(flat_motion(m, &f, in, frame, 1), 0, 0, false);
    flat_close(&f);
}

static void interior_emoji(viewport_motion *m)
{
    interior_walk(m, TEXT("\xf0\x9f\x98\x80")); /* U+1F600 */
}

static void interior_paste(viewport_motion *m)
{
    /* A paste of E2 before the pre-existing stray bytes makes U+20AC.
     * The insertion endpoint is the gap at byte one. */
    uint8_t text[] = {0, 0x82, 0xac};
    assert(!utf8_decode(text + 1, sizeof text - 1).valid);
    text[0] = 0xe2;
    interior_walk(m, text, sizeof text);
}

/* The proof between a and b starts the whole b+mark cluster. Advancing
 * that proof by one byte would treat the combining mark as independent. */
static void ascii_pair_proof_offset(viewport_motion *m)
{
    viewport_flat f = flat_open(TEXT("ab\xcc\x81"), 0);
    viewport_frame frame;
    assert(viewport_plan((viewport_anchor){0}, 80, 20, 10, &frame));
    viewport_intent in = intent(VIEWPORT_LEFT, f.length, f.length);
    selection(flat_motion(m, &f, in, frame, 1), 1, 1, false);
    flat_close(&f);
}

static void scalar_edges(viewport_motion *m)
{
    viewport_frame frame;
    assert(viewport_plan((viewport_anchor){0}, 80, 20, 10, &frame));
    viewport_flat f = flat_open(TEXT("x\xf0\x9f\x98\x80" "z"), 0);
    viewport_intent in = intent(VIEWPORT_RIGHT, 2, 2);
    selection(flat_motion(m, &f, in, frame, 1), 6, 6, false);
    in.action = VIEWPORT_LEFT;
    selection(flat_motion(m, &f, in, frame, 1), 0, 0, false);
    /* Collapse to a chosen end without another cluster move. */
    in.selection.mark = 6;
    selection(flat_motion(m, &f, in, frame, 1), 1, 1, false);
    flat_close(&f);

    f = flat_open(TEXT("\x82\xac" "x"), 0);
    in = intent(VIEWPORT_RIGHT, 1, 1);
    selection(flat_motion(m, &f, in, frame, 1), 2, 2, false);
    flat_close(&f);
}

static void interior_columns(void)
{
    /* The orphan combining mark adds zero cells. b ends at byte 3
     * before CRLF, then a four-byte emoji starts at byte 5. */
    viewport_flat f = flat_open(TEXT("\xcc\x81" "b\r\n\xf0\x9f\x98\x80z\n"), 0);
    const struct { uint64_t byte, row, column; } cases[] = {
        {1, 0, 0}, {3, 0, 1}, {4, 0, 1}, {7, 5, 0}, {11, 11, 0}
    };
    viewport_evidence e;
    for (size_t i = 0; i < sizeof cases / sizeof *cases; ++i) {
        viewport_request q = {.kind = VP_ROW, .as.row = {cases[i].byte, false}};
        size_t credit = 1;
        assert(flat_query(&f, &q, &credit, &e) == VP_DONE);
        assert(!credit && e.row.byte == cases[i].row && e.screen_column == cases[i].column);
    }
    viewport_request q = {.kind = VP_COLUMN, .as.column = {0, 0}};
    size_t credit = 1;
    assert(flat_query(&f, &q, &credit, &e) == VP_DONE && e.point == 0 && !credit);
    q.as.column.row_byte = f.length;
    credit = 1;
    assert(flat_query(&f, &q, &credit, &e) == VP_DONE && e.point == f.length && !credit);
    flat_close(&f);
}

int main(int argc, char **argv)
{
    viewport_motion *m = malloc(viewport_motion_size());
    assert(m);
    const struct { const char *name; void (*run)(viewport_motion *); } cases[] = {
        {"actions", actions}, {"seams", seams}, {"unicode", unicode}, {"affinity", affinity},
        {"witness", witness}, {"home", home_review}, {"reflow", reflow_review},
        {"scroll", scroll_review}, {"decoder", decoder_review}, {"reverse", reverse_review},
        {"grids", grids_review}, {"page", page_review}, {"empty", empty_snapshot},
        {"arrows", arrow_cost}, {"proof", proof_context}, {"rows", port_rows}, {"wait", evidence_wait},
        {"follow", follow_clamp}, {"rejected-word", rejected_word}, {"rejected-select", rejected_select},
        {"synthetic-clip", synthetic_clip}, {"synthetic-wrap", synthetic_wrap}, {"active-proof", active_row_proof},
        {"rank-wake", newer_rank}, {"emoji-endpoint", interior_emoji}, {"paste-endpoint", interior_paste},
        {"scalar-edges", scalar_edges}, {"ascii-pair-proof-offset", ascii_pair_proof_offset}
    };
    bool matched = argc == 1;
    if (argc == 1 || (argc == 2 && !strcmp(argv[1], "accept"))) {
        matched = true;
        frame_math();
        accept_review();
        flat_credit();
        interior_columns();
    }
    for (size_t i = 0; i < sizeof cases / sizeof *cases; ++i)
        if (argc == 1 || (argc == 2 && !strcmp(argv[1], cases[i].name))) {
            cases[i].run(m);
            matched = true;
        }
    free(m);
    if (!matched) {
        fputs("viewport_test: unknown test case\n", stderr);
        return 1;
    }
    puts("viewport_test: ok");
    return 0;
}
