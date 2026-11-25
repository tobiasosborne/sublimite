#ifndef RENDER_TYPES_H
#define RENDER_TYPES_H

#include <stddef.h>
#include <stdint.h>

#define RENDER_NO_SLOT UINT32_MAX
#define RENDER_NO_ROW UINT32_MAX
#define RENDER_RGB_MASK UINT32_C(0x00ffffff)
#define RENDER_Q8_ONE UINT32_C(256)
#define RENDER_TILE_MAX_SPAN 2u
#define RENDER_ATTR_BOLD      UINT16_C(0x001)
#define RENDER_ATTR_ITALIC    UINT16_C(0x002)
#define RENDER_ATTR_UNDERLINE UINT16_C(0x004)
#define RENDER_ATTR_INVERSE   UINT16_C(0x008)
#define RENDER_ATTR_WIDE_LEFT UINT16_C(0x010)
#define RENDER_ATTR_WIDE_RIGHT UINT16_C(0x020)
#define RENDER_ATTR_CURSOR    UINT16_C(0x040)
#define RENDER_ATTR_SELECTION UINT16_C(0x080)
#define RENDER_ATTR_MASK      UINT16_C(0x0ff)
#define RENDER_ATTR_WIDTH (RENDER_ATTR_WIDE_LEFT | RENDER_ATTR_WIDE_RIGHT)

/* Pin ABI: four words and two halfwords (20 bytes). RGB is opaque
 * 0x00RRGGBB. A nonzero glyph_index is the image version of atlas_slot.
 * The inkless pair is {0, RENDER_NO_SLOT}. Wide halves are adjacent within
 * one row and match in color and non-width attrs. The right half is inkless.
 * Reserved is zero. Selection/cursor colors are resolved by the producer. */
typedef struct render_cell {
    uint32_t glyph_index, atlas_slot, fg, bg;
    uint16_t attrs, reserved;
} render_cell;
_Static_assert(sizeof(render_cell) == 20, "render_cell ABI");

typedef struct render_image_id { uint32_t slot, version; } render_image_id;
typedef struct render_frame_id { uint64_t epoch; uint32_t frame; } render_frame_id;
typedef struct render_generation { uint64_t owner, generation; } render_generation;
typedef struct render_rect { uint32_t x, y, width, height; } render_rect;
typedef struct render_strip { uint32_t first_row, row_count; } render_strip;
typedef struct render_slot_range { uint32_t first, count; } render_slot_range;

/* R visible rows, C columns, and one extra source row at index R if enabled.
 * Client includes margins. Stride is the actual byte pitch of the target.
 * Sheet and runtime slot ranges are disjoint, excluding RENDER_NO_SLOT. */
typedef struct render_binding {
    uint64_t surface, geometry;
    render_generation font;
    uint32_t client_width, client_height, cols, rows, cell_width, cell_height;
    size_t target_stride;
    render_slot_range sheet, runtime;
    uint8_t overscan;
} render_binding;

/* Empty clip disables scroll. source_rows counts visible *text* source rows.
 * The next text row is overscan_row, even when fixed rows follow the text.
 * origin_y_q8 is in (-256*cell_height, 0]. */
typedef struct render_scroll {
    render_rect clip;
    uint32_t first_source_row, source_rows, overscan_row;
    int64_t origin_y_q8;
} render_scroll;

/* Borrowed R8 page and a nonempty top-left rectangle, at the settled span
 * (one or two cells). The page may have padding and the packet never does.
 * A slot's version is never reused within its font owner/generation. */
typedef struct render_tile_view {
    render_image_id image;
    const uint8_t *page;
    size_t length, stride;
    uint32_t page_width, page_height, span;
    render_rect rect;
} render_tile_view;

typedef enum render_region_mode { RENDER_KEEP, RENDER_REPLACE, RENDER_REMOVE } render_region_mode;
typedef struct render_region_view {
    render_rect rect;
    const uint32_t *pixels;
    size_t stride, capacity; /* in RGB32 elements */
    render_region_mode mode;
} render_region_view;

/* One coherent READY publication and its ancestor, including saved top.
 * input_first/input_last are both zero on an input-free repair. */
typedef struct render_stamp {
    uint64_t surface, geometry;
    render_generation font;
    uint64_t buffer, text, layout_owner, layout_serial, layout_top;
    uint64_t paint, composition;
    render_frame_id base;
    uint64_t input_first, input_last;
} render_stamp;

/* Slot/version and sampled extents in an immutable packet. Offset is into
 * tightly packed packet bytes. Uncovered destination texels sample as zero. */
typedef struct render_delta {
    render_image_id image;
    uint32_t width, height;
    size_t offset;
} render_delta;

#endif
