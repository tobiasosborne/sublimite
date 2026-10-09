/* Independent absolute-pixel and physical-line models. No worker/globals.
 * Random core wheel, XI2 deltas, pixels, page/doc keys, resize, follow,
 * byte seeking and prefix publication, including event/publication races. */
#include "scroll/scroll.h"
#include <limits.h>
#include <string.h>

#define REQUIRE(x) do { if (!(x)) __builtin_trap(); } while (0)
#define FILE_BYTES (3u * LINEIDX_CHUNK)
#define MAX_LINES (FILE_BYTES / 7u + 2u)

typedef struct model {
    int64_t position;
    uint32_t rows, height, margin;
    uint64_t lines;
} model;
static int64_t maximum(const model *m)
{ return m->lines > m->rows ? (int64_t)(m->lines - m->rows) * m->height * 256 : 0; }
static void move(model *m, int64_t delta)
{
    int64_t end = maximum(m);
    if (delta > 0 && delta > end - m->position) m->position = end;
    else if (delta < 0 && delta < -m->position) m->position = 0;
    else m->position += delta;
}
static void follow(model *m, uint64_t cursor)
{
    if (cursor >= m->lines) cursor = m->lines - 1;
    uint32_t margin = m->margin < (m->rows - 1) / 2 ? m->margin : (m->rows - 1) / 2;
    int64_t pixel = (int64_t)cursor * m->height * 256;
    int64_t upper = m->position + (int64_t)margin * m->height * 256;
    int64_t lower = m->position + (int64_t)(m->rows - margin) * m->height * 256;
    if (pixel < upper) m->position = cursor > margin ? (int64_t)(cursor - margin) * m->height * 256 : 0;
    else if (pixel + (int64_t)m->height * 256 > lower)
        m->position = (int64_t)(cursor - (m->rows - margin - 1)) * m->height * 256;
    if (m->position > maximum(m)) m->position = maximum(m);
}
static void core_model(const uint8_t *data, size_t size)
{
    scroll_state s;
    model m = {.rows = 24, .height = 17, .margin = 3, .lines = 1234};
    REQUIRE(scroll_init(&s, (scroll_config){m.rows, m.height, m.margin}, (scroll_extent){100000, m.lines, true}) == 0);
    for (size_t i = 0; i + 3 < size; i += 4) {
        uint32_t value = (uint32_t)data[i + 1] * 256 + data[i + 2];
        int64_t delta = (int64_t)value - 32768;
        unsigned op = data[i] % 7u;
        if (op == 0 || op == 1) {
            if (op == 0) delta = (data[i + 3] & 1u) ? 256 : -256;
            REQUIRE(scroll_wheel(&s, (int32_t)delta) == 0);
            move(&m, delta * 3 * m.height);
        } else if (op == 2) {
            if (data[i + 3] == 0) delta = INT64_MIN;
            if (data[i + 3] == 255) delta = INT64_MAX;
            REQUIRE(scroll_pixels(&s, delta) == 0);
            move(&m, delta);
        } else if (op == 3) {
            scroll_key key = (scroll_key)(data[i + 3] % 4u); view_key motion;
            REQUIRE(scroll_key_motion(&s, key, &motion) == 0);
            REQUIRE(motion == (key == SCROLL_PAGE_UP ? VIEW_PAGE_UP : key == SCROLL_PAGE_DOWN ? VIEW_PAGE_DOWN : key == SCROLL_HOME ? VIEW_DOC_HOME : VIEW_DOC_END));
            m.position = m.position / ((int64_t)m.height * 256) * m.height * 256;
            if (key == SCROLL_HOME) m.position = 0;
            else if (key == SCROLL_END) m.position = maximum(&m);
            else move(&m, (int64_t)m.rows * m.height * 256 * (key == SCROLL_PAGE_UP ? -1 : 1));
        } else if (op == 4) {
            uint32_t rows = data[i + 1] % 64u + 1, height = data[i + 2] % 32u + 1, margin = data[i + 3];
            int64_t row = m.position / ((int64_t)m.height * 256), fraction = m.position % ((int64_t)m.height * 256);
            REQUIRE(scroll_resize(&s, (scroll_config){rows, height, margin}) == 0);
            m.rows = rows; m.height = height; m.margin = margin;
            m.position = row * height * 256 + fraction;
            if (m.position > maximum(&m)) m.position = maximum(&m);
        } else if (op == 5) {
            uint64_t cursor = value % (m.lines + 4);
            if (cursor >= m.lines) cursor = m.lines - 1;
            follow(&m, cursor);
            REQUIRE(scroll_follow(&s, cursor) == 0);
        } else {
            m.lines = value % 1234u + 1;
            REQUIRE(scroll_set_extent(&s, (scroll_extent){100000, m.lines, true}) == 0);
            if (m.position > maximum(&m)) m.position = maximum(&m);
        }
        REQUIRE(s.first_line * m.height * 256 + s.subrow_q8 == (uint64_t)m.position);
    }
}
typedef struct source { const uint8_t *bytes; size_t size, fragment; } source;
static size_t span(void *ctx, uint64_t off, const uint8_t **out)
{
    source *f = ctx;
    if (off >= f->size) return 0;
    *out = f->bytes + off;
    size_t n = f->size - (size_t)off;
    return n > f->fragment ? f->fragment : n;
}
static size_t row_at(const uint64_t *starts, size_t count, uint64_t byte)
{
    size_t lo = 0, hi = count;
    while (lo + 1 < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (starts[mid] > byte) hi = mid; else lo = mid;
    }
    return lo;
}
static void index_model(const uint8_t *data, size_t size)
{
    if (size < 4) return;
    uint8_t bytes[FILE_BYTES]; uint64_t starts[MAX_LINES]; size_t count = 1;
    starts[0] = 0;
    memset(bytes, 'x', sizeof bytes);
    size_t pos = 0, serial = 0;
    while (pos < sizeof bytes) {
        size_t length = 7u + (data[serial % size] % 53u);
        if (length > sizeof bytes - pos) break;
        pos += length; bytes[pos - 1] = '\n'; starts[count++] = pos;
        serial++;
    }
    source f = {bytes, sizeof bytes, (size_t)data[1] + 1};
    lineidx_src src = {&f, sizeof bytes, span, NULL};
    lineidx *index = lineidx_create(sizeof bytes);
    REQUIRE(index);
    scroll_state s;
    REQUIRE(scroll_init(&s, (scroll_config){24, 17, 3}, (scroll_extent){sizeof bytes, sizeof bytes / 40 + 1, false}) == 0);
    /* Start well away from either endpoint and before any index publication. */
    size_t physical = count / 2;
    REQUIRE(scroll_seek_byte(&s, starts[physical]) == 0);
    REQUIRE(scroll_resolve(&s, index, &src, 0) == 0 && s.first_byte == starts[physical]);
    model m = {.position = (int64_t)physical * 17 * 256, .rows = 24, .height = 17, .margin = 3, .lines = count};
    for (size_t i = 0; i + 3 < size && i < 512; i += 4) {
        unsigned op = data[i] % 7u;
        if (op < 2) {
            int32_t delta = op == 0 ? ((data[i + 1] & 1u) ? 256 : -256) : (int32_t)data[i + 1] - 128;
            REQUIRE(scroll_wheel(&s, delta) == 0);
            move(&m, (int64_t)delta * 3 * m.height);
        } else if (op == 2) {
            view_key motion;
            REQUIRE(scroll_key_motion(&s, (data[i + 1] & 1u) ? SCROLL_PAGE_UP : SCROLL_PAGE_DOWN, &motion) == 0);
            m.position = m.position / ((int64_t)m.height * 256) * m.height * 256;
            move(&m, (int64_t)m.rows * m.height * 256 * ((data[i + 1] & 1u) ? -1 : 1));
        } else if (op == 3) {
            uint32_t rows = data[i + 1] % 48u + 1u, height = data[i + 2] % 24u + 1u;
            int64_t row = m.position / ((int64_t)m.height * 256), fraction = m.position % ((int64_t)m.height * 256);
            REQUIRE(scroll_resize(&s, (scroll_config){rows, height, 3}) == 0);
            m.rows = rows; m.height = height;
            m.position = row * height * 256 + fraction;
            if (m.position > maximum(&m)) m.position = maximum(&m);
        } else if (op == 4) {
            uint64_t byte = (uint64_t)data[i + 1] * 512 + 32768;
            physical = row_at(starts, count, byte);
            REQUIRE(scroll_seek_byte(&s, byte) == 0);
            m.position = (int64_t)physical * m.height * 256;
            if (m.position > maximum(&m)) m.position = maximum(&m);
        } else if (op == 5) {
            (void)lineidx_seek_line(index, &src, UINT64_MAX, LINEIDX_CHUNK);
        } else {
            size_t cursor = (size_t)data[i + 1] * (count - 1) / 255;
            REQUIRE(scroll_follow_cursor(&s, index, &src, starts[cursor]) == 0);
            follow(&m, cursor);
        }
        physical = (size_t)(m.position / ((int64_t)m.height * 256));
        /* Also exercise publication landing after an event, before resolve. */
        if (data[i + 3] & 1u) (void)lineidx_seek_line(index, &src, UINT64_MAX, LINEIDX_CHUNK);
        REQUIRE(scroll_resolve(&s, index, &src, 0) == 0);
        REQUIRE(s.first_byte == starts[physical]);
        REQUIRE(s.subrow_q8 == (uint64_t)(m.position % ((int64_t)m.height * 256)));
        REQUIRE(s.first_byte == 0 || bytes[s.first_byte - 1] == '\n');
        lineidx_result exact = lineidx_byte_to_line(index, &src, s.first_byte);
        if (exact.exact) REQUIRE(s.first_line == physical && !s.approximate);
    }
    lineidx_destroy(index);
}
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    core_model(data, size);
    index_model(data, size);
    return 0;
}
