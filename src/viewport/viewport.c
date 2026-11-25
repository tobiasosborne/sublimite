#include "viewport/viewport.h"

#include <string.h>

#include "os/os.h"
#include "utf8/utf8.h"

enum { Q8 = 256 };

typedef enum {
    SEEK_LINE, TOP_ROW, TOP_NEIGHBOR, ACTIVE_ROW, CARET_NEIGHBOR, COLUMN,
    ENDPOINT, SCALAR_LEAD, SCALAR_EDGE, SEGMENT, PROBE, FORWARD, REVERSE,
    RANK, LINE_START, RAW_BACK, RAW_NEXT, FINISHED
} motion_phase;
typedef enum { BOUND_LOW, BOUND_HIGH, BOUND_REPEAT } line_bound;
typedef enum { WALK_FORWARD, WALK_BACKWARD } walk_direction;
typedef enum { CLASS_NONE, CLASS_WORD, CLASS_SEPARATOR, CLASS_SPACE, CLASS_NEWLINE } word_class;

struct viewport_motion {
    viewport_intent in;
    viewport_frame frame;
    viewport_answer out;
    viewport_row_cert row;
    motion_phase phase;
    line_bound bound;
    uint64_t length, low, high, hard_start, line, endpoint;
    int64_t displacement;

    uint64_t window_off;
    size_t window_len;
    uint64_t start, cursor, limit, pos, cluster, walk_origin;
    size_t summaries, decode_paid, fragment;
    utf8_gseg seg;
    word_class kind, wanted;
    walk_direction direction;
    uint8_t proof_right;
    bool classified;
    uint8_t unit[4]; /* A UTF-8 scalar is at most four bytes. */

    uint8_t buffer[UTF8_GRAPHEME_BUDGET];
    uint32_t offsets[VIEWPORT_DISPLAY_MAX];
    uint8_t classes[VIEWPORT_DISPLAY_MAX];
};

static uint64_t min_u64(uint64_t a, uint64_t b) { return a < b ? a : b; }

bool viewport_wrap_default(const char *path)
{
    if (!path || !*path)
        return true;
    const char *base = path;
    for (const char *p = path; *p; ++p)
        if (*p == '/' || *p == '\\')
            base = p + 1;
    const char *dot = *base ? strrchr(base + 1, '.') : NULL;
    return !dot || !strcmp(dot, ".md") || !strcmp(dot, ".txt") || !strcmp(dot, ".tex");
}

uint64_t viewport_rebase(uint64_t byte, uint64_t off, uint64_t old_len,
                         uint64_t new_len, bool after_gravity)
{
    if (byte < off)
        return byte;
    if (byte - off > old_len || (old_len && byte - off == old_len))
        return byte - old_len + new_len;
    return off + (after_gravity ? new_len : 0);
}

bool viewport_accept(viewport_state *state, viewport_selection *selection, const viewport_intent *in,
                     const viewport_answer *answer)
{
    if (answer->intent_serial != in->serial || answer->key.buffer != in->key.buffer ||
        answer->key.text != in->key.text || answer->key.geometry != in->key.geometry)
        return false;
    *selection = answer->selection;
    state->top = answer->top;
    if (state->wrap != in->origin.wrap) {
        state->top.hscroll_cols = 0;
        selection->preferred_col = UINT64_MAX;
    }
    state->wrap = in->origin.wrap;
    return true;
}

bool viewport_plan(viewport_anchor top, uint32_t width, uint32_t height, uint32_t row_px,
                   viewport_frame *out)
{
    uint64_t row_q8 = (uint64_t)row_px * Q8;
    if (!out || !row_q8 || row_q8 > (uint64_t)UINT32_MAX + 1 || top.subrow_q8 >= row_q8)
        return false;
    uint64_t rows = width && height
        ? ((uint64_t)height * Q8 + top.subrow_q8 + row_q8 - 1) / row_q8 : 0;
    if (rows > UINT32_MAX)
        return false;
    *out = (viewport_frame){width, height, row_px, (uint32_t)rows, -(int64_t)top.subrow_q8};
    return true;
}

bool viewport_pixel_row(const viewport_frame *f, int64_t y, uint32_t *row, uint32_t *within)
{
    int64_t h = (int64_t)f->row_px * Q8;
    if (!h || f->origin_y_q8 > 0 || f->origin_y_q8 <= -h || y < 0 ||
        y >= (int64_t)f->height_px * Q8)
        return false;
    uint64_t offset = (uint64_t)(y - f->origin_y_q8);
    uint64_t index = offset / (uint64_t)h, rem = offset % (uint64_t)h;
    if (index >= f->layout_rows || rem > UINT32_MAX)
        return false;
    *row = (uint32_t)index;
    *within = (uint32_t)rem;
    return true;
}

static void move(viewport_motion *m, uint64_t byte, bool trailing)
{
    m->out.selection.caret = byte;
    if (!m->in.extend)
        m->out.selection.mark = byte;
    m->out.top.visual_end = trailing;
    m->phase = FINISHED;
}

static bool shared_end(viewport_row_cert row, uint64_t byte)
{
    return byte == row.end && row.end == row.next;
}

static bool page(viewport_action action)
{
    return action == VIEWPORT_PAGE_UP || action == VIEWPORT_PAGE_DOWN;
}

static void relative_top(viewport_motion *m, int64_t pixels)
{
    int64_t height = (int64_t)m->frame.row_px * Q8;
    if (!height)
        return;
    /* Split the dividend before adding the saved remainder, even for INT64_MIN. */
    int64_t rest = pixels % height + m->out.top.subrow_q8;
    m->displacement = pixels / height + rest / height;
    rest %= height;
    if (rest < 0) {
        rest += height;
        --m->displacement;
    }
    m->out.top.subrow_q8 = (uint32_t)rest;
    if (m->displacement)
        m->phase = TOP_ROW;
}

static void start_motion(viewport_motion *m)
{
    uint64_t caret = m->in.selection.caret;
    viewport_action action = m->in.action;
    m->phase = FINISHED;
    switch (action) {
    case VIEWPORT_RESTORE: case VIEWPORT_FOLLOW:
        break;
    case VIEWPORT_REFLOW:
        relative_top(m, 0);
        break;
    case VIEWPORT_SEEK_BYTE:
        m->out.top.byte = min_u64(m->in.target, m->length);
        m->out.top.subrow_q8 = 0;
        break;
    case VIEWPORT_SEEK_LINE:
        m->phase = SEEK_LINE;
        break;
    case VIEWPORT_SCROLL_Q8:
        relative_top(m, m->in.pixels_q8);
        break;
    case VIEWPORT_UP: case VIEWPORT_DOWN: case VIEWPORT_PAGE_UP: case VIEWPORT_PAGE_DOWN: {
        int64_t count = 1;
        if (page(action)) {
            uint64_t height = (uint64_t)m->frame.row_px * Q8;
            uint64_t subrow = m->out.top.subrow_q8;
            uint64_t full = height && m->frame.layout_rows
                ? ((uint64_t)m->frame.height_px * Q8 + subrow) / height : 0;
            count = (int64_t)(full - (subrow != 0 && full != 0));
        }
        m->displacement = action == VIEWPORT_UP || action == VIEWPORT_PAGE_UP ? -count : count;
        if (count)
            m->phase = ACTIVE_ROW;
        break;
    }
    case VIEWPORT_HOME: case VIEWPORT_END:
        m->out.selection.preferred_col = UINT64_MAX;
        m->phase = ACTIVE_ROW;
        break;
    case VIEWPORT_DOC_HOME: case VIEWPORT_DOC_END: case VIEWPORT_SELECT_ALL:
        m->out.selection.preferred_col = UINT64_MAX;
        move(m, action == VIEWPORT_DOC_HOME ? 0 : m->length, false);
        if (action == VIEWPORT_SELECT_ALL)
            m->out.selection.mark = 0;
        break;
    case VIEWPORT_SELECT_LINE:
        m->out.selection.preferred_col = UINT64_MAX;
        m->bound = BOUND_LOW;
        m->endpoint = m->low;
        m->phase = RANK;
        break;
    case VIEWPORT_LEFT: case VIEWPORT_RIGHT: case VIEWPORT_WORD_LEFT: case VIEWPORT_WORD_RIGHT:
    case VIEWPORT_SELECT_WORD: {
        m->out.selection.preferred_col = UINT64_MAX;
        bool left = action == VIEWPORT_LEFT || action == VIEWPORT_WORD_LEFT;
        if (action == VIEWPORT_SELECT_WORD && (m->low != m->high || caret == m->length))
            break;
        m->cursor = caret;
        if ((action == VIEWPORT_LEFT || action == VIEWPORT_RIGHT) &&
            m->low != m->high && !m->in.extend)
            m->cursor = left ? m->low : m->high;
        m->direction = left ? WALK_BACKWARD : WALK_FORWARD;
        m->phase = ENDPOINT;
        break;
    }
    }
}

size_t viewport_motion_size(void) { return sizeof(viewport_motion); }

void viewport_motion_begin(viewport_motion *m, const viewport_intent *in, viewport_frame frame,
                           uint64_t length)
{
    memset(m, 0, offsetof(viewport_motion, buffer));
    m->in = *in;
    m->frame = frame;
    m->length = length;
    m->out = (viewport_answer){in->key, in->serial, in->origin.top, in->selection};
    m->out.top.byte = min_u64(m->out.top.byte, length);
    if (in->origin.wrap)
        m->out.top.hscroll_cols = 0;
    m->low = min_u64(in->selection.caret, in->selection.mark);
    m->high = in->selection.caret > in->selection.mark ? in->selection.caret : in->selection.mark;
    start_motion(m);
}

static void start_walk(viewport_motion *m)
{
    m->walk_origin = m->cursor;
    viewport_action action = m->in.action;
    if (((action == VIEWPORT_LEFT || action == VIEWPORT_RIGHT) &&
         m->low != m->high && !m->in.extend) ||
        (m->direction == WALK_BACKWARD ? m->cursor == 0 : m->cursor == m->length)) {
        move(m, m->cursor, false);
    } else {
        if (action == VIEWPORT_SELECT_WORD && m->cursor != m->in.selection.caret) {
            m->out.selection.caret = m->out.selection.mark = m->cursor;
            m->out.top.visual_end = false;
        }
        m->phase = SEGMENT;
    }
}

static bool continuation(uint8_t byte) { return (byte & 0xc0) == 0x80; }

static word_class classify(utf8_step u)
{
    if (!u.valid)
        return CLASS_WORD;
    uint32_t cp = u.cp;
    if (cp == '\r' || cp == '\n')
        return CLASS_NEWLINE;
    /* The pin's separators and Unicode 15.1 White_Space list. */
    if (cp == ' ' || cp == '\t' || cp == '\v' || cp == '\f' || cp == 0x85 || cp == 0xa0 ||
        cp == 0x1680 || (cp >= 0x2000 && cp <= 0x200a) || cp == 0x2028 || cp == 0x2029 ||
        cp == 0x202f || cp == 0x205f || cp == 0x3000)
        return CLASS_SPACE;
    if (cp && cp < 128 && strchr("./\\()\"'-:,.;<>~!@#$%^&*|+=[]{}`~?", (int)cp))
        return CLASS_SEPARATOR;
    return CLASS_WORD;
}

static viewport_progress window(viewport_motion *m, const viewport_port *port, size_t *credits,
                                uint64_t byte)
{
    if (byte >= m->window_off && byte - m->window_off < m->window_len)
        return VP_DONE;
    size_t grant = (size_t)min_u64(*credits, sizeof m->buffer), remaining = grant;
    viewport_evidence e;
    viewport_request q = {.kind = VP_WINDOW, .as.window = {byte}};
    viewport_progress result = port->query(port->ctx, &q, &remaining, &e);
    *credits -= grant - remaining;
    if (result != VP_DONE)
        return result;
    ASSERT(byte >= e.off && byte - e.off < e.len && e.len <= grant - remaining);
    memcpy(m->buffer, e.bytes, e.len);
    m->window_off = e.off;
    m->window_len = e.len;
    return VP_DONE;
}

static viewport_progress take_byte(viewport_motion *m, const viewport_port *port, size_t *credits,
                                   uint64_t byte, uint8_t *out)
{
    viewport_progress result = window(m, port, credits, byte);
    if (result != VP_DONE || !*credits)
        return result == VP_DONE ? VP_MORE : result;
    --*credits;
    *out = m->buffer[byte - m->window_off];
    return VP_DONE;
}

static void scan_begin(viewport_motion *m, uint64_t start, uint64_t end)
{
    ASSERT(start <= end && end - start <= VIEWPORT_DISPLAY_MAX);
    m->start = m->pos = m->cluster = start;
    m->limit = end;
    m->summaries = 0;
    m->phase = FORWARD;
}

static void scan_back(viewport_motion *m)
{
    m->phase = FINISHED;
    if (!m->cursor || (m->cursor == m->row.byte && m->row.byte == m->row.line_start &&
                       !m->row.approximate)) {
        if (m->in.action == VIEWPORT_WORD_LEFT)
            move(m, m->cursor, false);
    } else if (m->cursor > m->row.byte && m->cursor <= min_u64(m->row.next, m->length)) {
        m->pos = m->cursor;
        m->proof_right = 0x80;
        m->phase = PROBE;
    } else
        m->phase = SEGMENT;
}

/* Reserve one complete unit across grants. Already paid bytes count
 * toward the decoder ceiling on the call that completes the unit. */
static bool pay_decode(viewport_motion *m, size_t *credits, size_t *ceiling, size_t cost)
{
    size_t grant = (size_t)min_u64(cost - m->decode_paid, min_u64(*credits, *ceiling));
    *credits -= grant;
    *ceiling -= grant;
    m->decode_paid += grant;
    if (m->decode_paid != cost)
        return false;
    m->decode_paid = 0;
    return true;
}

static size_t scalar_cost(uint8_t lead, size_t available)
{
    if (lead < 0xc2 || lead > 0xf4)
        return 1;
    return (size_t)min_u64(available, lead < 0xe0 ? 2u : lead < 0xf0 ? 3u : 4u);
}

/* Decode straight from the retained window, stitching only split units. */
static viewport_progress scalar(viewport_motion *m, const viewport_port *port, size_t *credits,
                                const uint8_t **p, size_t *n)
{
    *p = m->unit;
    uint64_t left = m->limit - m->pos;
    *n = m->fragment ? scalar_cost(m->unit[0], (size_t)left) : 0;
    while (left && (!*n || m->fragment < *n)) {
        if (!*credits)
            return VP_MORE;
        viewport_progress result = window(m, port, credits, m->pos + m->fragment);
        if (result != VP_DONE)
            return result;
        size_t at = (size_t)(m->pos + m->fragment - m->window_off);
        const uint8_t *bytes = m->buffer + at;
        size_t available = m->window_len - at;
        if (!*n)
            *n = scalar_cost(*bytes, (size_t)left);
        if (!m->fragment && available >= *n) {
            *p = bytes;
            return VP_DONE;
        }
        size_t copy = (size_t)min_u64(*n - m->fragment, min_u64(available, *credits));
        memcpy(m->unit + m->fragment, bytes, copy);
        m->fragment += copy;
        *credits -= copy;
    }
    return VP_DONE;
}

static void finish_word(viewport_motion *m, uint64_t end)
{
    if (m->in.action == VIEWPORT_WORD_RIGHT) {
        move(m, end, false);
    } else {
        m->out.selection.caret = end;
        m->out.top.visual_end = false;
        m->direction = WALK_BACKWARD;
        m->cursor = m->walk_origin;
        scan_back(m);
    }
}

/* The previous grapheme step has proved this boundary. Classify before
 * handing any bytes of the next cluster to the grapheme decoder. */
static bool admit_cluster(viewport_motion *m, utf8_step unit)
{
    word_class kind = m->kind = classify(unit);
    bool different = m->wanted != CLASS_NONE && m->wanted != kind;
    if (m->direction == WALK_BACKWARD) {
        /* Nontrivial reverse batches replay from a proof. At their right
         * edge a final standalone unit already has both boundaries. */
        if (different && m->pos + unit.len == m->limit) {
            if (m->in.action == VIEWPORT_WORD_LEFT)
                move(m, m->limit, false);
            else
                m->phase = FINISHED;
            return false;
        }
    } else if (m->in.action == VIEWPORT_WORD_RIGHT) {
        if (kind == CLASS_NEWLINE) {
            if (m->pos == m->walk_origin)
                return true;
            move(m, m->pos, false);
            return false;
        }
        if (different) {
            move(m, m->pos, false);
            return false;
        }
        if (kind != CLASS_SPACE)
            m->wanted = kind;
    } else if (m->in.action == VIEWPORT_SELECT_WORD) {
        if (different || (kind == CLASS_NEWLINE && m->row.newline && m->pos >= m->row.end)) {
            if (m->wanted != CLASS_NONE)
                finish_word(m, m->pos);
            else
                m->phase = FINISHED;
            return false;
        }
        if (m->wanted == CLASS_NONE) {
            m->wanted = kind;
            m->out.selection.mark = m->pos;
        }
    }
    return true;
}

static void cluster_found(viewport_motion *m)
{
    uint64_t start = m->cluster, end = m->pos, caret = m->in.selection.caret;
    word_class kind = m->kind;
    switch (m->in.action) {
    case VIEWPORT_HOME:
        if (kind != CLASS_SPACE)
            move(m, start == caret ? m->row.byte : start, false);
        break;
    case VIEWPORT_LEFT: case VIEWPORT_RIGHT:
        if (m->direction == WALK_FORWARD || end == m->cursor)
            move(m, m->direction == WALK_FORWARD ? end : start, false);
        break;
    case VIEWPORT_WORD_RIGHT:
        if (kind == CLASS_NEWLINE)
            move(m, end, false);
        break;
    case VIEWPORT_WORD_LEFT:
    case VIEWPORT_SELECT_WORD:
        if (m->direction == WALK_BACKWARD) {
            m->offsets[m->summaries] = (uint32_t)(start - m->start);
            m->classes[m->summaries++] = (uint8_t)kind;
        }
        break;
    case VIEWPORT_RESTORE: case VIEWPORT_REFLOW: case VIEWPORT_FOLLOW:
    case VIEWPORT_SEEK_BYTE: case VIEWPORT_SEEK_LINE: case VIEWPORT_SCROLL_Q8:
    case VIEWPORT_UP: case VIEWPORT_DOWN: case VIEWPORT_PAGE_UP: case VIEWPORT_PAGE_DOWN:
    case VIEWPORT_END: case VIEWPORT_DOC_HOME: case VIEWPORT_DOC_END:
    case VIEWPORT_SELECT_ALL: case VIEWPORT_SELECT_LINE:
        ASSERT(false);
        break;
    }
}

static void finish_segment(viewport_motion *m)
{
    if (m->in.action == VIEWPORT_HOME) {
        uint64_t dest = m->limit == m->in.selection.caret ? m->row.byte : m->limit;
        move(m, dest, dest != m->row.byte && shared_end(m->row, dest));
    } else if (m->direction == WALK_BACKWARD) {
        m->phase = REVERSE;
    } else if (!m->row.newline && m->row.next != UINT64_MAX) {
        m->cursor = m->row.next;
        m->phase = SEGMENT;
    } else {
        finish_word(m, m->limit);
    }
}

static void reverse_cluster(viewport_motion *m)
{
    if (!m->summaries) {
        m->cursor = m->start;
        scan_back(m);
        return;
    }
    size_t i = --m->summaries;
    word_class kind = (word_class)m->classes[i];
    uint64_t end = m->pos;
    uint64_t byte = m->pos = m->start + m->offsets[i];
    if (m->in.action == VIEWPORT_SELECT_WORD) {
        if (kind == m->wanted)
            m->out.selection.mark = byte;
        else
            m->phase = FINISHED;
    } else if (m->wanted != CLASS_NONE && kind != m->wanted) {
        move(m, end, false);
    } else if (kind == CLASS_NEWLINE) {
        move(m, end >= m->walk_origin ? byte : end, false);
    } else if (kind != CLASS_SPACE) {
        m->wanted = kind;
    }
}

static void line_found(viewport_motion *m, uint64_t off)
{
    switch (m->bound) {
    case BOUND_LOW:
        m->hard_start = off;
        m->bound = BOUND_HIGH;
        if (m->low != m->high) {
            m->endpoint = m->high - 1;
            m->phase = RANK;
        } else if (m->line != UINT64_MAX) {
            ++m->line;
            m->phase = LINE_START;
        } else {
            m->cursor = m->endpoint;
            m->phase = RAW_NEXT;
        }
        return;
    case BOUND_HIGH:
        if (off == m->high && off < m->length && m->low == m->hard_start && m->high > m->low) {
            m->bound = BOUND_REPEAT;
            m->endpoint = off;
            m->cursor = off;
            if (m->line != UINT64_MAX)
                ++m->line;
            m->phase = m->line == UINT64_MAX ? RAW_NEXT : LINE_START;
            return;
        }
        break;
    case BOUND_REPEAT:
        break;
    }
    m->out.selection.mark = m->hard_start;
    m->out.selection.caret = off;
    m->out.top.visual_end = false;
    m->phase = FINISHED;
}

static void active_row(viewport_motion *m, viewport_evidence e)
{
    m->row = e.row;
    if (m->in.action == VIEWPORT_HOME) {
        if (e.row.continuation)
            move(m, e.row.byte, false);
        else
            scan_begin(m, e.row.byte, e.row.end);
    } else if (m->in.action == VIEWPORT_END) {
        move(m, e.row.end, shared_end(e.row, e.row.end));
    } else {
        if (m->out.selection.preferred_col == UINT64_MAX)
            m->out.selection.preferred_col = e.screen_column;
        m->phase = CARET_NEIGHBOR;
    }
}

viewport_progress viewport_motion_step(viewport_motion *m, const viewport_port *port,
                                       size_t *credits, viewport_answer *out)
{
    if (!*credits)
        return VP_MORE;
    size_t ceiling = UTF8_GRAPHEME_BUDGET - m->decode_paid;
    while (m->phase != FINISHED && *credits) {
        viewport_evidence e;
        viewport_request q;
        viewport_progress result;
        uint64_t caret = m->in.selection.caret;
        uint8_t byte;
        switch (m->phase) {
        case SEEK_LINE:
        case LINE_START:
            q = (viewport_request){.kind = VP_LOGICAL_START,
                .as.logical_start = {m->phase == SEEK_LINE ? m->in.target : m->line}};
            if ((result = port->query(port->ctx, &q, credits, &e)) != VP_DONE)
                return result;
            if (m->phase == SEEK_LINE) {
                if (!e.exact)
                    return VP_WAIT;
                m->out.top.byte = e.off;
                m->out.top.subrow_q8 = 0;
                m->phase = FINISHED;
            } else if (e.exact) {
                line_found(m, e.off);
            } else if (m->bound == BOUND_LOW) {
                m->cursor = m->endpoint;
                m->phase = RAW_BACK;
            } else {
                m->cursor = m->endpoint;
                if (e.count + 1 == m->line && e.off > m->endpoint)
                    m->cursor = e.off;
                m->line = UINT64_MAX;
                m->phase = RAW_NEXT;
            }
            break;
        case RANK:
            q = (viewport_request){.kind = VP_LOGICAL_RANK, .as.logical_rank = {m->endpoint}};
            if ((result = port->query(port->ctx, &q, credits, &e)) != VP_DONE)
                return result;
            if (e.exact) {
                m->line = e.count + (m->bound == BOUND_HIGH);
                m->phase = LINE_START;
            } else {
                m->line = UINT64_MAX;
                m->cursor = m->endpoint;
                m->phase = m->bound == BOUND_HIGH ? RAW_NEXT : RAW_BACK;
            }
            break;
        case RAW_BACK:
            if (!m->cursor) {
                line_found(m, 0);
                break;
            }
            if ((result = take_byte(m, port, credits, m->cursor - 1, &byte)) != VP_DONE)
                return result;
            if (byte == '\n')
                line_found(m, m->cursor);
            else
                --m->cursor;
            break;
        case RAW_NEXT:
            if (m->cursor == m->length) {
                line_found(m, m->cursor);
                break;
            }
            if ((result = take_byte(m, port, credits, m->cursor, &byte)) != VP_DONE)
                return result;
            ++m->cursor;
            if (byte == '\n')
                line_found(m, m->cursor);
            break;
        case ENDPOINT:
            if (!m->cursor || m->cursor == m->length) {
                start_walk(m);
                break;
            }
            if ((result = take_byte(m, port, credits, m->cursor, &byte)) != VP_DONE)
                return result;
            if (continuation(byte)) {
                m->pos = m->cursor;
                m->start = m->cursor - min_u64(m->cursor, sizeof m->unit - 1);
                m->phase = SCALAR_LEAD;
            } else {
                start_walk(m);
            }
            break;
        case SCALAR_LEAD:
            if ((result = take_byte(m, port, credits, m->pos - 1, &byte)) != VP_DONE)
                return result;
            --m->pos;
            if (!continuation(byte)) {
                m->limit = m->length;
                m->phase = SCALAR_EDGE;
            } else if (m->pos == m->start) {
                start_walk(m);
            }
            break;
        case SCALAR_EDGE: {
            const uint8_t *p;
            size_t n;
            if ((result = scalar(m, port, credits, &p, &n)) != VP_DONE)
                return result;
            if (!pay_decode(m, credits, &ceiling, n))
                return VP_MORE;
            utf8_step unit = utf8_decode(p, n);
            uint64_t end = m->pos + unit.len;
            if (unit.valid && end > m->cursor)
                m->cursor = m->direction == WALK_BACKWARD ? m->pos : end;
            m->fragment = 0;
            start_walk(m);
            break;
        }
        case TOP_ROW:
        case ACTIVE_ROW:
        case SEGMENT: {
            bool segment = m->phase == SEGMENT;
            bool top = m->phase == TOP_ROW;
            uint64_t point = segment ? m->cursor - (m->direction == WALK_BACKWARD) : caret;
            q = (viewport_request){.kind = VP_ROW, .as.row = {
                top ? m->out.top.byte : point, !top && !segment && m->in.origin.top.visual_end}};
            if ((result = port->query(port->ctx, &q, credits, &e)) != VP_DONE)
                return result;
            if (top) {
                m->out.top.byte = e.row.byte;
                m->phase = TOP_NEIGHBOR;
            } else if (!segment) {
                active_row(m, e);
            } else {
                m->row = e.row;
                if (m->direction == WALK_FORWARD)
                    scan_begin(m, m->cursor, min_u64(m->row.next, m->length));
                else if (m->in.action == VIEWPORT_SELECT_WORD && e.row.newline && m->cursor > e.row.end)
                    m->phase = FINISHED;
                else
                    scan_back(m);
            }
            break;
        }
        case TOP_NEIGHBOR:
        case CARET_NEIGHBOR: {
            bool top = m->phase == TOP_NEIGHBOR;
            bool relative = top && !page(m->in.action);
            int64_t delta = m->displacement;
            if (relative)
                delta = delta < 0 ? -1 : 1;
            q = (viewport_request){.kind = VP_NEIGHBOR, .as.neighbor = {
                top ? m->out.top.byte : caret, !top && m->in.origin.top.visual_end, delta}};
            if ((result = port->query(port->ctx, &q, credits, &e)) != VP_DONE)
                return result;
            if (top) {
                /* A unit step returning its source proves a clamp. A final
                 * successful step onto a boundary preserves the modulus. */
                if (relative && e.row.byte == m->out.top.byte) {
                    m->out.top.subrow_q8 = 0;
                    m->phase = FINISHED;
                } else {
                    m->out.top.byte = e.row.byte;
                    m->displacement -= delta;
                    if (!m->displacement)
                        m->phase = FINISHED;
                }
            } else if (e.row.byte == m->row.byte) {
                move(m, m->displacement < 0 ? 0 : m->length, false);
                if (page(m->in.action))
                    m->phase = TOP_NEIGHBOR;
            } else {
                m->row = e.row;
                m->phase = COLUMN;
            }
            break;
        }
        case COLUMN:
            q = (viewport_request){.kind = VP_COLUMN,
                .as.column = {m->row.byte, m->out.selection.preferred_col}};
            if ((result = port->query(port->ctx, &q, credits, &e)) != VP_DONE)
                return result;
            move(m, e.point, shared_end(m->row, e.point));
            if (page(m->in.action))
                m->phase = TOP_NEIGHBOR;
            break;
        case PROBE: {
            if ((result = take_byte(m, port, credits, m->pos - 1, &byte)) != VP_DONE)
                return result;
            --m->pos;
            bool ascii = byte < 0x80 && byte != '\r' && byte != '\n';
            if (ascii && m->proof_right < 0x80) {
                scan_begin(m, m->pos + 1, m->cursor);
                m->unit[0] = m->proof_right; /* Already read by the proof. */
                m->fragment = 1;
            } else if (m->pos == m->row.byte) {
                scan_begin(m, m->pos, m->cursor);
            }
            m->proof_right = ascii ? byte : 0x80;
            break;
        }
        case FORWARD: {
            if (m->cluster == m->limit) {
                finish_segment(m);
                break;
            }
            const uint8_t *p;
            size_t used, n;
            result = scalar(m, port, credits, &p, &n);
            if (result != VP_DONE)
                return result;
            bool first = m->pos == m->cluster && m->in.action != VIEWPORT_LEFT &&
                         m->in.action != VIEWPORT_RIGHT;
            if (first && !m->classified) {
                if (!pay_decode(m, credits, &ceiling, n))
                    return VP_MORE;
                if (!admit_cluster(m, utf8_decode(p, n))) {
                    m->fragment = 0;
                    break;
                }
                m->classified = true;
            }
            if (!pay_decode(m, credits, &ceiling, n ? n : 1))
                return VP_MORE;
            int decoded = utf8_grapheme_step(&m->seg, p, n, 1, n == m->limit - m->pos, &used);
            m->fragment = 0;
            m->pos += used;
            if (decoded == UTF8_G_END) {
                m->classified = false;
                cluster_found(m);
                m->cluster = m->pos;
            }
            break;
        }
        case REVERSE:
            --*credits;
            reverse_cluster(m);
            break;
        case FINISHED:
            break;
        }
    }
    if (m->phase != FINISHED)
        return VP_MORE;
    *out = m->out;
    return VP_DONE;
}
