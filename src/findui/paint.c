#include "findui/private.h"
#include <string.h>

typedef struct painter {
    render_grid *grid;
    const findui_style *style;
    uint32_t row;
} painter;
static void cell(painter *paint, uint32_t column, uint8_t byte, bool accent, bool cursor)
{
    if (column >= paint->grid->dims.cols) return;
    render_cell value = {0, RENDER_NO_SLOT, accent ? paint->style->accent : paint->style->foreground,
                         paint->style->background, 0, 0};
    if (byte >= 32 && byte <= 126 && paint->style->ascii_slots) {
        uint32_t slot = paint->style->ascii_slots[byte - 32u];
        if (slot != RENDER_NO_SLOT) {
            value.atlas_slot = slot; value.glyph_index = paint->grid->glyphs[slot].glyph_index;
        }
    }
    if (cursor) {
        value.fg = paint->style->cursor_foreground; value.bg = paint->style->cursor_background;
        value.attrs = RENDER_ATTR_CURSOR;
    }
    paint->grid->cells[(size_t)paint->row * paint->grid->dims.cols + column] = value;
}
static uint32_t text(painter *paint, uint32_t column, const char *bytes, bool accent)
{
    while (*bytes && column < paint->grid->dims.cols) cell(paint, column++, (uint8_t)*bytes++, accent, false);
    return column;
}
static size_t append(char *buffer, size_t at, const char *bytes)
{
    size_t length = strlen(bytes); memcpy(buffer + at, bytes, length); return at + length;
}
static size_t decimal(char *buffer, size_t at, uint64_t number)
{
    char digits[20]; size_t count = 0;
    do { digits[count++] = (char)('0' + number % 10u); number /= 10u; } while (number);
    while (count) buffer[at++] = digits[--count];
    return at;
}
static size_t status(const findui_state *state, char *buffer)
{
    size_t at = 0;
    at = append(buffer, at, state->options.regex ? "[.*] " : "[  ] ");
    at = append(buffer, at, state->options.match_case ? "[Aa] " : "[aa] ");
    at = append(buffer, at, state->options.whole_word ? "[W] " : "[ ] ");
    if (state->replacing) at = append(buffer, at, "replacing");
    else if (state->undo_error) at = append(buffer, at, "undo error");
    else if (state->search_error != FIND_OK) {
        at = append(buffer, at, "pattern error@"); at = decimal(buffer, at, state->error_offset);
    } else {
        at = decimal(buffer, at, state->match_index == FINDUI_NO_INDEX ? 0 : state->match_index + 1);
        buffer[at++] = '/'; at = decimal(buffer, at, state->match_count);
        if (state->searching) buffer[at++] = '+';
        if (state->cache_overflow || state->visible_overflow) buffer[at++] = '!';
    }
    buffer[at] = 0; return at;
}
static size_t escaped(uint8_t byte, uint8_t *output)
{
    if (byte >= 32 && byte <= 126) { output[0] = byte; return 1; }
    const char *digits = "0123456789ABCDEF";
    output[0] = '\\'; output[1] = 'x'; output[2] = (uint8_t)digits[byte >> 4u];
    output[3] = (uint8_t)digits[byte & 15u]; return 4;
}
/* edit-cxho.3 (P): printable ASCII occupies one cell; the existing \\xHH
 * escape occupies sizeof "\\xHH" minus its terminator. Zero is the first
 * cell/absence, one advances a cell or removes a terminator. */
static size_t byte_cells(uint8_t byte)
{
    return byte >= ' ' && byte <= '~' ? 1u : sizeof "\\xHH" - 1u;
}
static size_t field_scroll(const uint8_t *bytes, size_t cursor, uint32_t width)
{
    size_t cursor_column = 0;
    for (size_t i = 0; i < cursor; i++) cursor_column += byte_cells(bytes[i]);
    return cursor_column >= width ? cursor_column - width + 1u : 0;
}
static void field(painter *paint, uint32_t column, uint32_t width, const uint8_t *bytes,
                   size_t length, size_t cursor, bool focus)
{
    if (!width) return;
    size_t scroll = field_scroll(bytes, cursor, width);
    size_t virtual_column = 0;
    for (size_t i = 0; i <= length; i++) {
        uint8_t output[4];
        size_t count = i == length ? (output[0] = ' ', 1u) : escaped(bytes[i], output);
        for (size_t j = 0; j < count; j++, virtual_column++) {
            if (virtual_column >= scroll && virtual_column - scroll < width)
                cell(paint, column + (uint32_t)(virtual_column - scroll), output[j], false,
                     focus && i == cursor && j == 0);
        }
        if (virtual_column >= scroll + width) break;
    }
}
static uint32_t summary_width(uint32_t remaining, size_t length)
{
    /* (P) retain the painter's two-cell minimum field/gap allowance. */
    return remaining > length + 2u ? (uint32_t)length : 0;
}
findui_code findui_hit_test(const findui_panel *panel, uint32_t columns,
                           uint32_t row_count, uint32_t row, uint32_t column,
                           findui_hit *hit)
{
    const findui_impl *impl = panel ? panel->private_ : NULL;
    if (!impl || !hit || !columns || row >= row_count || column >= columns)
        return FINDUI_ERR_ARGUMENT;
    *hit = (findui_hit){FINDUI_HIT_NONE, 0};
    if (!impl->state.open) return FINDUI_OK;
    const findui_state *state = &impl->state;
    char summary[128]; /* (P) same status buffer as findui_render below */
    size_t length = status(state, summary);
    uint32_t start = (uint32_t)(sizeof "Find: " - 1u);
    if (start > columns) start = columns;
    uint32_t summary_cells = summary_width(columns - start, length);
    uint32_t width = columns - start - (summary_cells ? summary_cells + 1u : 0);
    bool status_row = row == 1u && !state->replace_mode;
    if ((row == 0 && summary_cells && column >= columns - summary_cells) || status_row) {
        uint32_t local = status_row ? column : column - (columns - summary_cells);
        /* (P) toggle spans exclude their separating space, matching status(). */
        const size_t regex_end = sizeof "[  ]" - 1u;
        const size_t case_start = sizeof "[  ] " - 1u;
        const size_t case_end = sizeof "[  ] [aa]" - 1u;
        const size_t word_start = sizeof "[  ] [aa] " - 1u;
        const size_t word_end = sizeof "[  ] [aa] [ ]" - 1u;
        if (local < regex_end) hit->kind = FINDUI_HIT_REGEX;
        else if (local >= case_start && local < case_end) hit->kind = FINDUI_HIT_CASE;
        else if (local >= word_start && local < word_end) hit->kind = FINDUI_HIT_WORD;
        return FINDUI_OK;
    }
    bool replacement = row == 1u && state->replace_mode;
    if (row && !replacement) return FINDUI_OK;
    if (replacement) {
        start = (uint32_t)(sizeof "Replace: " - 1u);
        if (start > columns) start = columns;
        width = columns - start;
    }
    hit->kind = replacement ? FINDUI_HIT_REPLACEMENT : FINDUI_HIT_QUERY;
    const uint8_t *bytes = replacement ? state->replacement : state->query;
    length = replacement ? state->replacement_length : state->query_length;
    size_t cursor = replacement ? state->replacement_cursor : state->query_cursor;
    if (!width) return FINDUI_OK;
    uint32_t local = column > start ? column - start : 0;
    if (local >= width) local = width - 1u;
    size_t target = field_scroll(bytes, cursor, width) + local, at = 0;
    while (hit->cursor < length) {
        size_t cells = byte_cells(bytes[hit->cursor]);
        if (at + cells > target) break;
        at += cells; hit->cursor++;
    }
    return FINDUI_OK;
}
findui_code findui_render(const findui_panel *panel, render_grid *grid, uint32_t first_row,
                         uint32_t row_count, const findui_style *style)
{
    const findui_impl *impl = panel ? panel->private_ : NULL;
    if (!impl || !grid || !style || !grid->begun || !grid->cells || !grid->dirty ||
        !grid->dims.cols || !row_count || first_row >= grid->dims.rows ||
        row_count > grid->dims.rows - first_row ||
        (size_t)grid->dims.rows > SIZE_MAX / grid->dims.cols ||
        grid->cell_capacity < (size_t)grid->dims.rows * grid->dims.cols ||
        grid->dirty_word_capacity < ((size_t)grid->dims.rows + 63u) / 64u ||
        ((style->foreground | style->background | style->accent | style->cursor_foreground |
          style->cursor_background) & UINT32_C(0xff000000))) return FINDUI_ERR_ARGUMENT;
    if (style->ascii_slots) {
        if (style->ascii_slot_count < 95) return FINDUI_ERR_ARGUMENT;
        for (size_t i = 0; i < 95; i++) {
            uint32_t slot = style->ascii_slots[i];
            if (slot != RENDER_NO_SLOT && (!grid->glyphs || slot >= grid->glyph_count)) return FINDUI_ERR_ARGUMENT;
        }
    }
    if (render_mark_rows(grid, first_row, row_count) != RENDER_OK) return FINDUI_ERR_ARGUMENT;
    (void)findui_get_state(panel); /* validate already adopted backing metadata */
    painter paint = {grid, style, first_row};
    for (uint32_t row = first_row; row < first_row + row_count; row++) {
        paint.row = row;
        for (uint32_t column = 0; column < grid->dims.cols; column++) cell(&paint, column, ' ', false, false);
    }
    if (!impl->state.open) return FINDUI_OK;
    paint.row = first_row;
    uint32_t column = text(&paint, 0, "Find: ", true);
    char summary[128]; size_t summary_length = status(&impl->state, summary);
    uint32_t remaining = grid->dims.cols - column;
    uint32_t summary_cells = summary_width(remaining, summary_length);
    uint32_t field_width = remaining - (summary_cells ? summary_cells + 1 : 0);
    field(&paint, column, field_width, impl->query, impl->state.query_length,
          impl->state.query_cursor, !impl->state.replacement_focus);
    if (summary_cells) (void)text(&paint, grid->dims.cols - summary_cells, summary, true);
    if (row_count >= 2) {
        paint.row++;
        if (impl->state.replace_mode) {
            column = text(&paint, 0, "Replace: ", true);
            field(&paint, column, grid->dims.cols - column, impl->replacement,
                  impl->state.replacement_length, impl->state.replacement_cursor, impl->state.replacement_focus);
        } else (void)text(&paint, 0, summary, true);
    }
    if (row_count >= 3) {
        paint.row++;
        (void)text(&paint, 0, impl->state.replace_mode ? "Ctrl+Enter: Replace  Ctrl+Alt+Enter: All  Tab: field  Esc: close"
                                                    : "Enter: next  Shift+Enter: previous  Esc: close", false);
    }
    return FINDUI_OK;
}
