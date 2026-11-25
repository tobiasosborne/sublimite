#ifndef RENDER_FIXTURE_H
#define RENDER_FIXTURE_H

#include <string.h>
#include "render/render.h"

/* Three columns accommodate a wide pair plus an independent cell.
 * Three visible rows allow two disjoint runs. The extra row exercises Q8. */
enum { RF_COLS = 3, RF_ROWS = 3, RF_W = 2, RF_H = 2, RF_SLOTS = 3, RF_FIRST = 10,
       RF_SHEET = 2, RF_TILE = RENDER_TILE_MAX_SPAN * RF_W * RF_H,
       RF_PAGE_STRIDE = 6, RF_PAGE_ROWS = 3 };

typedef struct render_fixture {
    base_arena arena;
    render *r;
    render_binding binding;
    render_setup setup;
    render_source source;
    render_offer offer;
    render_strip runs[2];
    render_cell cells[RF_ROWS + 1][RF_COLS];
    uint8_t pages[RF_SLOTS][RF_PAGE_STRIDE * RF_PAGE_ROWS];
    render_tile_view tiles[RF_SLOTS], sheet_tiles[RF_SHEET];
    render_sheet sheet;
    render_image_state states[RF_SLOTS];
    uint32_t rgb[2 * 3]; /* a two-pixel row plus sentinel pitch padding */
    render_region_view region;
    size_t overscan_count;
    size_t stamp_reads, run_reads, row_reads, region_reads, identity_reads, tile_reads, directory_reads;
    bool stale_on_recheck;
} render_fixture;

static inline render_stamp rf_current(void *ctx)
{
    render_fixture *f = ctx;
    render_stamp s = f->offer.stamp;
    if (++f->stamp_reads == 2 && f->stale_on_recheck) ++s.paint;
    return s;
}
static inline render_rc rf_run(void *ctx, size_t index, render_strip *out)
{
    render_fixture *f = ctx;
    ++f->run_reads;
    if (index >= sizeof f->runs / sizeof f->runs[0]) return RENDER_INVALID;
    *out = f->runs[index];
    return RENDER_OK;
}
static inline render_rc rf_row(void *ctx, uint32_t row, render_cell_span *out)
{
    render_fixture *f = ctx;
    ++f->row_reads;
    if (row > RF_ROWS) return RENDER_INVALID;
    *out = (render_cell_span){f->cells[row], row == RF_ROWS ? f->overscan_count : RF_COLS};
    return RENDER_OK;
}
static inline render_rc rf_region(void *ctx, render_region_view *out)
{
    render_fixture *f = ctx;
    ++f->region_reads;
    *out = f->region;
    return RENDER_OK;
}
static inline render_image_state rf_lookup(render_fixture *f, uint32_t slot, render_image_id *out)
{
    if (slot < RF_FIRST || slot >= RF_FIRST + RF_SLOTS) return RENDER_IMAGE_VACANT;
    size_t i = slot - RF_FIRST;
    *out = f->tiles[i].image;
    return f->states[i];
}
static inline render_image_state rf_identity(void *ctx, uint32_t slot, render_image_id *out)
{
    render_fixture *f = ctx;
    ++f->identity_reads;
    return rf_lookup(f, slot, out);
}
static inline render_image_state rf_directory(void *ctx, render_generation gen,
                                              uint32_t slot, render_image_id *out)
{
    render_fixture *f = ctx;
    ++f->directory_reads;
    if (gen.owner != f->binding.font.owner || gen.generation != f->binding.font.generation)
        return RENDER_IMAGE_VACANT;
    return rf_lookup(f, slot, out);
}
static inline render_rc rf_tile(void *ctx, render_image_id id, render_tile_view *out)
{
    render_fixture *f = ctx;
    ++f->tile_reads;
    if (id.slot < RF_FIRST || id.slot >= RF_FIRST + RF_SLOTS) return RENDER_INVALID;
    *out = f->tiles[id.slot - RF_FIRST];
    return RENDER_OK;
}

static inline render_cell rf_blank(void)
{
    return (render_cell){.atlas_slot = RENDER_NO_SLOT, .fg = RENDER_RGB_MASK, .bg = 0x102030};
}
static inline void rf_wide(render_fixture *f, uint32_t row, render_image_id id)
{
    render_cell left = rf_blank(), right = left;
    left.atlas_slot = id.slot;
    left.glyph_index = id.version;
    left.attrs = RENDER_ATTR_WIDE_LEFT | RENDER_ATTR_UNDERLINE | RENDER_ATTR_SELECTION;
    right.attrs = RENDER_ATTR_WIDE_RIGHT | RENDER_ATTR_UNDERLINE | RENDER_ATTR_SELECTION;
    f->cells[row][0] = left;
    f->cells[row][1] = right;
}
static inline void rf_notify(render_fixture *f, uint32_t i, uint8_t value)
{
    render_tile_view *v = &f->tiles[i];
    ++v->image.version;
    f->states[i] = RENDER_IMAGE_INK;
    memset(f->pages[i], 0xff, sizeof f->pages[i]);
    for (uint32_t y = 0; y < RF_H; ++y)
        memset(f->pages[i] + (y + 1u) * RF_PAGE_STRIDE + 1u, value, RENDER_TILE_MAX_SPAN * RF_W);
    render_image_changed(f->r, f->binding.font, v->image.slot);
}
static inline void rf_init(render_fixture *f)
{
    *f = (render_fixture){.overscan_count = RF_COLS};
    f->binding = (render_binding){
        .surface = 1, .geometry = 1, .font = {1, 1},
        .client_width = RF_COLS * RF_W + 1, .client_height = RF_ROWS * RF_H + 1,
        .cols = RF_COLS, .rows = RF_ROWS, .cell_width = RF_W, .cell_height = RF_H,
        .target_stride = (RF_COLS * RF_W + 1) * sizeof(uint32_t),
        .sheet = {1, RF_SHEET}, .runtime = {RF_FIRST, RF_SLOTS}, .overscan = 1
    };
    f->setup = (render_setup){
        .driver = render_driver_null(), .directory_ctx = f, .directory = rf_directory,
        .recovery_slots = 1, /* deliberate one-slot replay steps */
        .limits = {.client_width = f->binding.client_width, .client_height = f->binding.client_height,
            .cols = RF_COLS, .rows = RF_ROWS, .cell_width = RF_W, .cell_height = RF_H,
            .cells = RF_COLS * (RF_ROWS + 1),
            .target_bytes = f->binding.target_stride * f->binding.client_height,
            .sheet_slots = RF_SHEET, .runtime_slots = RF_SLOTS, .atlas_bytes = (RF_SHEET + RF_SLOTS) * RF_TILE,
            .region_width = 2, .region_height = 2, .region_pixels = 4,
            .tile_descriptors = RF_SLOTS, .tile_bytes = RF_SLOTS * RF_TILE}
    };
    f->source = (render_source){f, rf_current, rf_run, rf_row, rf_region, rf_identity, rf_tile};
    for (uint32_t i = 0; i < RF_SLOTS; ++i) {
        f->tiles[i] = (render_tile_view){.image = {RF_FIRST + i, 0}, .page = f->pages[i],
            .length = sizeof f->pages[i], .stride = RF_PAGE_STRIDE, .page_width = RF_PAGE_STRIDE - 1,
            .page_height = RF_PAGE_ROWS, .span = RENDER_TILE_MAX_SPAN,
            .rect = {1, 1, RENDER_TILE_MAX_SPAN * RF_W, RF_H}};
    }
    f->sheet = (render_sheet){.tiles = f->sheet_tiles, .count = RF_SHEET};
    for (uint32_t i = 0; i < RF_SHEET; ++i) {
        f->sheet_tiles[i] = f->tiles[i];
        f->sheet_tiles[i].image = (render_image_id){f->binding.sheet.first + i, 1};
        f->sheet_tiles[i].span = i + 1u;
        f->sheet_tiles[i].rect.width = RF_W * (i + 1u);
        f->sheet.marker[i] = f->sheet_tiles[i].image;
    }
    f->region = (render_region_view){.rect = {1, 1, 2, 2}, .pixels = f->rgb,
        .stride = 3, .capacity = sizeof f->rgb / sizeof f->rgb[0], .mode = RENDER_REPLACE};
    for (size_t y = 0; y <= RF_ROWS; ++y)
        for (size_t x = 0; x < RF_COLS; ++x) f->cells[y][x] = rf_blank();
}

static inline void rf_offer(render_fixture *f, render_mode mode)
{
    render_counters c;
    render_stats(f->r, &c);
    f->offer = (render_offer){
        .stamp = {.surface = f->binding.surface, .geometry = f->binding.geometry, .font = f->binding.font,
            .buffer = 1, .text = 1, .layout_owner = 1, .layout_serial = 1, .paint = 1,
            .composition = c.stamp.composition + 1u, .base = c.accepted},
        .mode = mode, .cols = RF_COLS, .rows = RF_ROWS, .background = 0x334455,
        .run_count = mode == RENDER_FULL, .overscan_dirty = mode == RENDER_FULL,
        .region = mode == RENDER_FULL ? RENDER_REMOVE : RENDER_KEEP,
        .scroll = {.clip = {0, 0, RF_COLS * RF_W, (RF_ROWS - 1) * RF_H},
            .source_rows = RF_ROWS - 1, .overscan_row = RF_ROWS, .origin_y_q8 = -(int64_t)RENDER_Q8_ONE / 2}
    };
    f->runs[0] = (render_strip){0, RF_ROWS};
    f->stamp_reads = f->run_reads = f->row_reads = f->region_reads = f->identity_reads = f->tile_reads = 0;
}
static inline render_rc rf_pump(render_fixture *f)
{
    return render_event(f->r, &(render_notice){.kind = RENDER_PUMP});
}
static inline render_rc rf_finish(render_fixture *f)
{
    /* Null has one consume and four typed notices to retire each frame. */
    render_rc rc = RENDER_MORE;
    for (unsigned i = 0; i < 1u + 4u && rc == RENDER_MORE; ++i) rc = rf_pump(f);
    return rc;
}
static inline render_rc rf_submit(render_fixture *f, render_frame_id *id, render_need *need)
{
    return render_submit(f->r, &f->offer, &f->source, id, need);
}
#endif
