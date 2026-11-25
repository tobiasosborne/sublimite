#include "viewport/viewport.h"

#include "piece/piece.h"
#include "utf8/utf8.h"

/* S1/A29 and the signed pixel format. */
enum { GRID = 32768, SPAN = 2 * GRID + 4, Q8 = 256, SCALAR = 4 };

typedef enum {
    MOTION_START, SEEK_LINE, TOP_ROW, TOP_NEIGHBOR, ACTIVE_ROW, CARET_NEIGHBOR, COLUMN,
    SEGMENT, STAGE, ALIGN, FORWARD, REVERSE, RANK, LINE_START, RAW_BACK, RAW_NEXT, FINISHED
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
    uint64_t start, grid, cursor, run_start, target, left_seam;
    size_t staged, limit, pos, cluster, summaries, decode_paid;
    utf8_gseg seg;
    word_class kind, run, wanted;
    walk_direction direction;
    bool synthetic, seam;

    uint8_t buffer[UTF8_GRAPHEME_BUDGET], bytes[SPAN];
    uint32_t offsets[SPAN];
    uint8_t classes[SPAN];
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

size_t viewport_motion_size(void) { return sizeof(viewport_motion); }

void viewport_motion_begin(viewport_motion *m, const viewport_intent *in, viewport_frame frame,
                           uint64_t length)
{
    memset(m, 0, offsetof(viewport_motion, buffer));
    m->in = *in;
    m->frame = frame;
    m->length = length;
    m->out = (viewport_answer){in->key, in->serial, in->origin.top, in->selection};
    if (in->action != VIEWPORT_FOLLOW)
        m->out.top.byte = min_u64(m->out.top.byte, length);
    if (in->origin.wrap)
        m->out.top.hscroll_cols = 0;
    m->low = min_u64(in->selection.caret, in->selection.mark);
    m->high = in->selection.caret > in->selection.mark ? in->selection.caret : in->selection.mark;
    m->left_seam = UINT64_MAX;
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
        /* The driver puts the preceding width in target. */
        if (m->in.target != m->frame.width_px)
            m->out.selection.preferred_col = UINT64_MAX;
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
        if ((action == VIEWPORT_LEFT || action == VIEWPORT_RIGHT) && m->low != m->high &&
            !m->in.extend) {
            move(m, left ? m->low : m->high, false);
        } else if (action == VIEWPORT_SELECT_WORD && (m->low != m->high || caret == m->length)) {
            break;
        } else if ((left && !caret) || (!left && caret == m->length)) {
            move(m, caret, false);
        } else {
            m->target = caret;
            m->cursor = left ? caret - 1 : caret;
            m->phase = SEGMENT;
        }
        break;
    }
    }
}

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

static bool take_byte(viewport_motion *m, const viewport_port *port, size_t *credits,
                      uint64_t byte, uint8_t *out, bool backward)
{
    if (byte < m->window_off || byte - m->window_off >= m->window_len) {
        size_t capacity = (size_t)min_u64(*credits, sizeof m->buffer);
        viewport_evidence e;
        viewport_request q = {.kind = VP_WINDOW, .as.window = {byte}};
        if (port->query(port->ctx, &q, credits, &e) == VP_MORE)
            return false;
        ASSERT(byte >= e.off && byte - e.off < e.len);
        size_t at = (size_t)(byte - e.off);
        size_t from = backward ? at - (size_t)min_u64(at, capacity - 1) : at;
        m->window_off = e.off + from;
        m->window_len = (size_t)min_u64(e.len - from, capacity);
        memcpy(m->buffer, e.bytes + from, m->window_len);
    }
    if (!*credits)
        return false;
    --*credits;
    *out = m->buffer[byte - m->window_off];
    return true;
}

static void stage_begin(viewport_motion *m, uint64_t start, uint64_t limit, bool synthetic)
{
    m->start = start;
    m->synthetic = synthetic;
    /* A synthetic start retains its raw grid phase even after alignment. */
    uint64_t seed = start + (synthetic ? 0 : GRID - 1);
    m->grid = (seed / GRID + 1) * GRID;
    m->limit = (size_t)(min_u64(limit, m->grid + SCALAR - 1) - start);
    m->staged = m->pos = m->cluster = m->summaries = 0;
    m->run = CLASS_NONE;
    m->seam = false;
    m->phase = STAGE;
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
    return (size_t)min_u64(available, lead < 0xe0 ? 2u : lead < 0xf0 ? 3u : SCALAR);
}

static bool stage_finish(viewport_motion *m, size_t *credits, size_t *ceiling)
{
    size_t grid = (size_t)(m->grid - m->start);
    if (grid < m->staged && m->grid < m->length &&
        !(m->bytes[grid - 1] == '\r' && m->bytes[grid] == '\n')) {
        size_t first = grid - (size_t)min_u64(grid, SCALAR - 1);
        /* S1 permits at most three four-byte candidate probes. */
        if (!pay_decode(m, credits, ceiling, (grid - first) * SCALAR))
            return false;
        size_t end = grid;
        for (size_t lead = first; lead < grid; ++lead) {
            utf8_step u = utf8_decode(m->bytes + lead, m->staged - lead);
            if (u.valid && lead + u.len > grid)
                end = lead + u.len;
        }
        m->staged = end;
        m->seam = m->start + end < m->length;
    }
    m->phase = FORWARD;
    return true;
}

static void finish_word(viewport_motion *m, uint64_t end)
{
    if (m->in.action == VIEWPORT_WORD_RIGHT) {
        move(m, end, false);
        return;
    }
    m->out.selection.caret = end;
    m->out.top.visual_end = false;
    m->phase = FINISHED;
    if (m->left_seam != UINT64_MAX) {
        m->direction = WALK_BACKWARD;
        m->target = m->left_seam;
        m->cursor = m->target - 1;
        m->phase = SEGMENT;
    }
}

static void cluster_found(viewport_motion *m)
{
    uint64_t start = m->start + m->cluster, end = m->start + m->pos;
    word_class kind = m->kind;
    switch (m->in.action) {
    case VIEWPORT_HOME:
        if (kind != CLASS_SPACE)
            move(m, start == m->in.selection.caret ? m->row.byte : start, false);
        break;
    case VIEWPORT_LEFT:
        if (end >= m->target)
            move(m, start, false);
        break;
    case VIEWPORT_RIGHT:
        if (end > m->target)
            move(m, end, false);
        break;
    case VIEWPORT_WORD_RIGHT:
        if (end <= m->target)
            break;
        if (kind == CLASS_NEWLINE)
            move(m, start <= m->target ? end : start, false);
        else if (m->wanted != CLASS_NONE && m->wanted != kind)
            move(m, start, false);
        else if (kind != CLASS_SPACE)
            m->wanted = kind;
        break;
    case VIEWPORT_WORD_LEFT:
    case VIEWPORT_SELECT_WORD:
        if (m->in.action == VIEWPORT_WORD_LEFT || m->direction == WALK_BACKWARD) {
            m->offsets[m->summaries] = (uint32_t)m->cluster;
            m->classes[m->summaries++] = (uint8_t)kind;
            if (end >= m->target)
                m->phase = REVERSE;
            break;
        }
        if (kind == CLASS_NEWLINE && (m->bytes[m->cluster] == '\n' || end - start > 1)) {
            if (m->wanted != CLASS_NONE)
                finish_word(m, start);
            else
                m->phase = FINISHED;
            break;
        }
        if (m->wanted != CLASS_NONE) {
            if (kind != m->wanted)
                finish_word(m, start);
            break;
        }
        if (kind != m->run)
            m->run_start = start;
        m->run = kind;
        if (end > m->target) {
            m->wanted = kind;
            m->out.selection.mark = m->run_start;
            if (m->synthetic && m->run_start == m->start)
                m->left_seam = m->start;
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
    uint64_t end = m->start + m->staged;
    if (m->in.action == VIEWPORT_HOME) {
        uint64_t dest = end == m->in.selection.caret ? m->row.byte : end;
        move(m, dest, dest != m->row.byte && shared_end(m->row, dest));
    } else if (m->in.action == VIEWPORT_WORD_LEFT || m->direction == WALK_BACKWARD) {
        m->phase = REVERSE;
    } else if (m->seam && m->in.action != VIEWPORT_RIGHT) {
        stage_begin(m, end, m->length, true);
    } else if (m->wanted == CLASS_NONE && m->in.action == VIEWPORT_SELECT_WORD) {
        m->phase = FINISHED;
    } else {
        finish_word(m, end);
    }
}

static void reverse_cluster(viewport_motion *m)
{
    if (m->summaries) {
        size_t i = --m->summaries;
        word_class kind = (word_class)m->classes[i];
        uint64_t end = m->start + m->pos;
        m->pos = m->offsets[i];
        uint64_t byte = m->start + m->pos;
        if (m->in.action == VIEWPORT_SELECT_WORD) {
            if (kind == m->wanted)
                m->out.selection.mark = byte;
            else
                m->phase = FINISHED;
        } else if (m->wanted != CLASS_NONE && kind != m->wanted) {
            move(m, end, false);
        } else if (kind == CLASS_NEWLINE) {
            move(m, end >= m->target ? byte : end, false);
        } else if (kind != CLASS_SPACE) {
            m->wanted = kind;
        }
    } else if (m->synthetic) {
        m->cursor = m->start - 1;
        m->phase = SEGMENT;
    } else if (m->in.action == VIEWPORT_WORD_LEFT) {
        move(m, m->start, false);
    } else {
        m->phase = FINISHED;
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
            stage_begin(m, e.row.byte, e.row.end, false);
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
        uint64_t caret = m->in.selection.caret;
        uint8_t byte;
        switch (m->phase) {
        case MOTION_START:
            start_motion(m);
            break;
        case SEEK_LINE:
        case LINE_START:
            q = (viewport_request){.kind = VP_LOGICAL_START,
                .as.logical_start = {m->phase == SEEK_LINE ? m->in.target : m->line}};
            if (port->query(port->ctx, &q, credits, &e) == VP_MORE)
                return VP_MORE;
            if (m->phase == SEEK_LINE) {
                if (!e.exact)
                    return VP_MORE;
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
            if (port->query(port->ctx, &q, credits, &e) == VP_MORE)
                return VP_MORE;
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
            if (!take_byte(m, port, credits, m->cursor - 1, &byte, true))
                return VP_MORE;
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
            if (!take_byte(m, port, credits, m->cursor, &byte, false))
                return VP_MORE;
            ++m->cursor;
            if (byte == '\n')
                line_found(m, m->cursor);
            break;
        case TOP_ROW:
        case ACTIVE_ROW:
        case SEGMENT: {
            bool segment = m->phase == SEGMENT;
            bool top = m->phase == TOP_ROW;
            q = (viewport_request){.kind = VP_ROW, .as.row = {
                top ? m->out.top.byte : segment ? m->cursor : caret,
                !top && !segment && m->in.origin.top.visual_end}};
            if (port->query(port->ctx, &q, credits, &e) == VP_MORE)
                return VP_MORE;
            if (top) {
                m->out.top.byte = e.row.byte;
                m->phase = TOP_NEIGHBOR;
            } else if (segment)
                stage_begin(m, e.row.line_start, m->length, e.row.approximate);
            else
                active_row(m, e);
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
            if (port->query(port->ctx, &q, credits, &e) == VP_MORE)
                return VP_MORE;
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
            if (port->query(port->ctx, &q, credits, &e) == VP_MORE)
                return VP_MORE;
            move(m, e.point, shared_end(m->row, e.point));
            if (page(m->in.action))
                m->phase = TOP_NEIGHBOR;
            break;
        case STAGE:
            if (m->staged == m->limit) {
                m->phase = ALIGN;
                break;
            }
            if (!take_byte(m, port, credits, m->start + m->staged, &byte, false))
                return VP_MORE;
            m->bytes[m->staged++] = byte;
            if (byte == '\n')
                m->phase = ALIGN;
            break;
        case ALIGN:
            if (!stage_finish(m, credits, &ceiling))
                return VP_MORE;
            break;
        case FORWARD: {
            if (m->cluster == m->staged) {
                finish_segment(m);
                break;
            }
            size_t used, n = m->staged - m->pos;
            const uint8_t *p = m->bytes + m->pos;
            bool first = m->pos == m->cluster && m->in.action != VIEWPORT_LEFT &&
                         m->in.action != VIEWPORT_RIGHT;
            size_t cost = n ? scalar_cost(*p, n) : 1;
            if (!pay_decode(m, credits, &ceiling, cost * (first ? 2u : 1u)))
                return VP_MORE;
            if (first)
                m->kind = classify(utf8_decode(p, n));
            int result = utf8_grapheme_step(&m->seg, p, n, 1, 1, &used);
            m->pos += used;
            if (result == UTF8_G_END) {
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
