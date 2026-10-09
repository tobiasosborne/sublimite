#include "scroll/scroll.h"

static bool config_valid(scroll_config c)
{ return c.rows != 0 && c.row_height != 0 && c.row_height <= UINT32_MAX / SCROLL_PIXEL_UNIT; }
static bool extent_valid(scroll_extent e)
{ return e.bytes <= LINEIDX_MAX_LEN && e.lines != 0 && e.lines <= e.bytes + 1; }
static bool valid(const scroll_state *s)
{ return s && config_valid(s->config) && extent_valid(s->extent); }
static uint64_t max_top(const scroll_state *s)
{
    uint64_t lines = s->extent.exact ? s->extent.lines : s->extent.bytes + 1;
    return lines > s->config.rows ? lines - s->config.rows : 0;
}
static void changed(scroll_state *s, uint64_t rows, bool up)
{
    if (s->request != SCROLL_LINE && s->request != SCROLL_BYTE) {
        if (s->pending_up == up) {
            s->pending_rows = rows > UINT64_MAX - s->pending_rows ? UINT64_MAX : s->pending_rows + rows;
        } else if (rows >= s->pending_rows) {
            s->pending_rows = rows - s->pending_rows; s->pending_up = up;
        } else s->pending_rows -= rows;
        s->request = SCROLL_RELATIVE;
    }
}
static void clamp(scroll_state *s)
{
    uint64_t end = max_top(s);
    if (s->first_line >= end) {
        bool move = s->first_line != end;
        s->first_line = end; s->subrow_q8 = 0;
        if (move) s->request = SCROLL_LINE;
    }
}
int scroll_init(scroll_state *s, scroll_config config, scroll_extent extent)
{
    if (!s || !config_valid(config) || !extent_valid(extent)) return SCROLL_ERR_ARG;
    *s = (scroll_state){.config = config, .extent = extent, .approximate = !extent.exact};
    return SCROLL_OK;
}
int scroll_resize(scroll_state *s, scroll_config config)
{
    if (!valid(s) || !config_valid(config)) return SCROLL_ERR_ARG;
    uint64_t height = (uint64_t)config.row_height * SCROLL_PIXEL_UNIT;
    uint64_t carry = s->subrow_q8 / height;
    s->config = config;
    s->subrow_q8 %= height;
    s->first_line = carry > UINT64_MAX - s->first_line ? UINT64_MAX : s->first_line + carry;
    changed(s, carry, false);
    clamp(s);
    return SCROLL_OK;
}
int scroll_set_extent(scroll_state *s, scroll_extent extent)
{
    if (!valid(s) || !extent_valid(extent)) return SCROLL_ERR_ARG;
    s->extent = extent;
    clamp(s);
    return SCROLL_OK;
}
int scroll_pixels(scroll_state *s, int64_t delta_q8)
{
    if (!valid(s) || s->request == SCROLL_BYTE) return SCROLL_ERR_ARG;
    uint64_t height = (uint64_t)s->config.row_height * SCROLL_PIXEL_UNIT;
    uint64_t end = max_top(s), old_line = s->first_line, old_sub = s->subrow_q8;
    bool physical = !s->extent.exact && s->approximate && s->request != SCROLL_LINE;
    /* No line*height product; unsigned magnitude handles INT64_MIN. */
    uint64_t amount = delta_q8 < 0 ? UINT64_C(0) - (uint64_t)delta_q8 : (uint64_t)delta_q8;
    uint64_t rows = amount / height, pixels = amount % height;
    if (delta_q8 >= 0) {
        pixels += s->subrow_q8;
        if (pixels >= height) { rows++; pixels -= height; }
        if (s->first_line >= end || rows >= end - s->first_line) {
            s->first_line = end; s->subrow_q8 = 0;
        } else { s->first_line += rows; s->subrow_q8 = pixels; }
    } else {
        if (pixels > s->subrow_q8) { rows++; pixels = height - (pixels - s->subrow_q8); }
        else pixels = s->subrow_q8 - pixels;
        if (rows > s->first_line) {
            s->first_line = 0;
            s->subrow_q8 = physical && s->first_byte ? pixels : 0;
        }
        else { s->first_line -= rows; s->subrow_q8 = pixels; }
    }
    if (physical) changed(s, rows, delta_q8 < 0);
    else if (old_line != s->first_line || old_sub != s->subrow_q8)
        changed(s, old_line > s->first_line ? old_line - s->first_line : s->first_line - old_line, old_line > s->first_line);
    return SCROLL_OK;
}
int scroll_wheel(scroll_state *s, int32_t delta)
{
    if (!valid(s)) return SCROLL_ERR_ARG;
    return scroll_pixels(s, (int64_t)delta * SCROLL_CORE_ROWS * s->config.row_height);
}
int scroll_seek_line(scroll_state *s, uint64_t line)
{
    if (!valid(s)) return SCROLL_ERR_ARG;
    uint64_t end = max_top(s);
    s->first_line = line < end ? line : end;
    s->subrow_q8 = 0; s->request = SCROLL_LINE; s->pending_rows = 0;
    return SCROLL_OK;
}
int scroll_seek_byte(scroll_state *s, uint64_t byte)
{
    if (!valid(s)) return SCROLL_ERR_ARG;
    s->target_byte = byte < s->extent.bytes ? byte : s->extent.bytes;
    s->subrow_q8 = 0; s->request = SCROLL_BYTE; s->pending_rows = 0;
    return SCROLL_OK;
}
int scroll_key_motion(scroll_state *s, scroll_key key, view_key *motion)
{
    if (!valid(s) || !motion || key < SCROLL_PAGE_UP || key > SCROLL_END || s->request == SCROLL_BYTE)
        return SCROLL_ERR_ARG;
    uint64_t old_line = s->first_line, target = old_line, end = max_top(s);
    view_key mapped;
    switch (key) {
    case SCROLL_PAGE_UP:
        mapped = VIEW_PAGE_UP;
        target = target > s->config.rows ? target - s->config.rows : 0;
        break;
    case SCROLL_PAGE_DOWN:
        mapped = VIEW_PAGE_DOWN;
        target = target >= end || s->config.rows >= end - target ? end : target + s->config.rows;
        break;
    case SCROLL_HOME: mapped = VIEW_DOC_HOME; target = 0; break;
    case SCROLL_END:
        mapped = VIEW_DOC_END;
        if (!s->extent.exact) {
            (void)scroll_seek_byte(s, s->extent.bytes);
            *motion = mapped;
            return SCROLL_OK;
        }
        target = end; break;
    default: return SCROLL_ERR_ARG;
    }
    s->first_line = target; s->subrow_q8 = 0;
    if (key == SCROLL_HOME || key == SCROLL_END) s->request = SCROLL_LINE;
    else if (!s->extent.exact) changed(s, s->config.rows, key == SCROLL_PAGE_UP);
    else changed(s, old_line > target ? old_line - target : target - old_line, old_line > target);
    *motion = mapped;
    return SCROLL_OK;
}
int scroll_follow(scroll_state *s, uint64_t cursor_line)
{
    if (!valid(s) || s->request == SCROLL_BYTE) return SCROLL_ERR_ARG;
    uint64_t last = s->extent.exact ? s->extent.lines - 1 : s->extent.bytes;
    if (cursor_line > last) cursor_line = last;
    uint32_t margin = s->config.margin, limit = (s->config.rows - 1) / 2;
    if (margin > limit) margin = limit;
    uint64_t target = s->first_line;
    bool above = cursor_line < target || cursor_line - target < margin ||
        (cursor_line - target == margin && s->subrow_q8 != 0);
    if (above) target = cursor_line > margin ? cursor_line - margin : 0;
    else if (cursor_line - target > s->config.rows - 1u - margin)
        target = cursor_line - (s->config.rows - 1u - margin);
    else return SCROLL_OK;
    uint64_t end = max_top(s);
    if (target > end) target = end;
    uint64_t old_line = s->first_line;
    s->first_line = target; s->subrow_q8 = 0;
    changed(s, old_line > target ? old_line - target : target - old_line, old_line > target);
    return SCROLL_OK;
}
