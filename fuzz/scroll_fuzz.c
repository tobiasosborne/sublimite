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
/* Pump the caller-owned bounded adapter; no model changes. */
static int resolve(scroll_state *s, lineidx *index, const lineidx_src *src)
{
    scroll_resolver r = {0};
    int rc = SCROLL_MORE;
    for (unsigned i = 0; i < 10000 && rc == SCROLL_MORE; i++)
        rc = scroll_resolve_slice(s, &r, index, src, 0, 0);
    return rc;
}
static int follow_cursor(scroll_state *s, lineidx *index, const lineidx_src *src, uint64_t cursor)
{
    scroll_resolver r = {0};
    int rc = SCROLL_MORE;
    for (unsigned i = 0; i < 10000 && rc == SCROLL_MORE; i++)
        rc = scroll_follow_cursor_slice(s, &r, index, src, cursor, 0, 0);
    return rc;
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
typedef struct source { const uint8_t *bytes; size_t size, fragment, calls, fail_call; } source;
static size_t span(void *ctx, uint64_t off, const uint8_t **out)
{
    source *f = ctx;
    if (++f->calls == f->fail_call || off >= f->size) return 0;
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
    source f = {.bytes=bytes, .size=sizeof bytes, .fragment=(size_t)data[1] + 1};
    lineidx_src src = {&f, sizeof bytes, span, NULL};
    lineidx *index = lineidx_create(sizeof bytes);
    REQUIRE(index);
    scroll_state s;
    REQUIRE(scroll_init(&s, (scroll_config){24, 17, 3}, (scroll_extent){sizeof bytes, sizeof bytes / 40 + 1, false}) == 0);
    /* Start well away from either endpoint and before any index publication. */
    size_t physical = count / 2;
    REQUIRE(scroll_seek_byte(&s, starts[physical]) == 0);
    REQUIRE(resolve(&s, index, &src) == 0 && s.first_byte == starts[physical]);
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
            REQUIRE(follow_cursor(&s, index, &src, starts[cursor]) == 0);
            follow(&m, cursor);
        }
        physical = (size_t)(m.position / ((int64_t)m.height * 256));
        /* Also exercise publication landing after an event, before resolve. */
        if (data[i + 3] & 1u) (void)lineidx_seek_line(index, &src, UINT64_MAX, LINEIDX_CHUNK);
        REQUIRE(resolve(&s, index, &src) == 0);
        REQUIRE(s.first_byte == starts[physical]);
        REQUIRE(s.subrow_q8 == (uint64_t)(m.position % ((int64_t)m.height * 256)));
        REQUIRE(s.first_byte == 0 || bytes[s.first_byte - 1] == '\n');
        lineidx_result exact = lineidx_byte_to_line(index, &src, s.first_byte);
        if (exact.exact) REQUIRE(s.first_line == physical && !s.approximate);
    }
    lineidx_destroy(index);
}
static uint64_t scalar_start(const uint8_t *bytes, size_t at)
{
    while (at && bytes[at - 1] != '\n') at--;
    return at;
}
static uint64_t scalar_line(const uint8_t *bytes, size_t at)
{
    uint64_t line = 0;
    for (size_t i = 0; i < at; i++) if (bytes[i] == '\n') line++;
    return line;
}
static void complete_index(lineidx *index, const lineidx_src *src)
{
    for (size_t i = 0; i < 4096 && !lineidx_complete(index); i++)
        (void)lineidx_seek_line(index, src, UINT64_MAX, LINEIDX_CHUNK);
    REQUIRE(lineidx_complete(index));
}
/* Long-line proof, staged source errors, and independently transformed
 * physical anchors across insertion/deletion. Keep the short-line pixel
 * oracle above too; no production helpers are used for the edit oracle. */
static void difficult_model(const uint8_t *data, size_t size)
{
    if (size < 4) return;
    uint8_t bytes[FILE_BYTES + 64];
    memset(bytes, 'x', sizeof bytes);
    size_t anchor = 1u + data[0];
    size_t gap = SCROLL_SCAN_BUDGET + 2u + (size_t)data[1] * 128u;
    REQUIRE(gap > SCROLL_SCAN_BUDGET);
    bytes[anchor - 1] = '\n'; bytes[anchor + gap] = '\n';
    source f = {.bytes=bytes, .size=FILE_BYTES, .fragment=1u + (size_t)data[2] * 17u};
    lineidx_src src = {&f, f.size, span, NULL};
    lineidx *index = lineidx_create(f.size); REQUIRE(index);
    scroll_state s;
    REQUIRE(scroll_init(&s, (scroll_config){1, 17, 0}, (scroll_extent){f.size, 100, false}) == 0);
    REQUIRE(scroll_seek_byte(&s, anchor) == 0 && scroll_resolve(&s, index, &src, 0) == 0);
    REQUIRE(s.first_byte == anchor);
    scroll_state before = s;
    size_t cursor = anchor + gap - 1;
    REQUIRE(scroll_follow_cursor(&s, index, &src, cursor) == SCROLL_MORE);
    REQUIRE(memcmp(&s, &before, sizeof s) == 0);
    REQUIRE(scroll_follow_cursor(&s, index, &src, anchor + SCROLL_SCAN_BUDGET) == 0);
    REQUIRE(s.first_byte == anchor);
    /* Fail on a staged callback inside the requested suffix, with unchanged
     * pending intent as well as unchanged viewport. */
    REQUIRE(scroll_seek_byte(&s, cursor) == 0); before = s;
    f.calls = 0; f.fail_call = 1u + data[3] % 8u;
    REQUIRE(scroll_resolve(&s, index, &src, 0) == SCROLL_ERR_SOURCE);
    REQUIRE(memcmp(&s, &before, sizeof s) == 0);
    f.fail_call = 0;
    REQUIRE(scroll_resolve(&s, index, &src, 0) == 0);
    complete_index(index, &src);
    REQUIRE(scroll_follow_cursor(&s, index, &src, cursor) == 0);
    REQUIRE(s.first_byte == scalar_start(bytes, cursor));
    REQUIRE(s.first_line == scalar_line(bytes, cursor) && !s.approximate);
    for (size_t i = 0; i < size && i < 16; i++) {
        size_t off = (data[i] & 1u) ? (size_t)s.first_byte : (size_t)s.first_byte - (s.first_byte != 0);
        bool insertion = (data[i] & 2u) != 0;
        uint64_t transformed = s.first_byte;
        if (insertion) {
            REQUIRE(lineidx_edit(index, off, 0, 1) == 0);
            memmove(bytes + off + 1, bytes + off, f.size - off); bytes[off] = '\n'; f.size++;
            if (off <= transformed) transformed++;
            if (off <= cursor) cursor++;
        } else {
            REQUIRE(lineidx_edit(index, off, 1, 0) == 0);
            memmove(bytes + off, bytes + off + 1, f.size - off - 1); f.size--;
            if (off < transformed) transformed--;
            if (off < cursor) cursor--;
        }
        src.len = f.size;
        /* The host owns byte-anchor transformation after an edit. Deleting
         * its preceding newline merges rows: independently find the new seed. */
        s.first_byte = scalar_start(bytes, (size_t)transformed);
        REQUIRE(scroll_set_extent(&s, (scroll_extent){f.size, 100, false}) == 0);
        complete_index(index, &src);
        REQUIRE(scroll_resolve(&s, index, &src, 0) == 0);
        REQUIRE(scroll_follow_cursor(&s, index, &src, cursor) == 0);
        REQUIRE(s.first_byte == scalar_start(bytes, cursor));
        REQUIRE(s.first_line == scalar_line(bytes, cursor) && !s.approximate);
    }
    lineidx_destroy(index);
}
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    difficult_model(data, size);
    core_model(data, size);
    index_model(data, size);
    return 0;
}
