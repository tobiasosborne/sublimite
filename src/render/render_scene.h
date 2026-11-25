#ifndef RENDER_SCENE_H
#define RENDER_SCENE_H

#include "render/render_internal.h"

static render_rect render_row_damage(const render_packet *p, uint32_t row)
{
    const render_binding *b = &p->binding;
    render_scroll s = p->offer.scroll;
    render_rect damage = row < b->rows ?
        (render_rect){0, row * b->cell_height, b->client_width, b->cell_height} : (render_rect){0};
    uint32_t text = row - s.first_source_row;
    if (!s.clip.width || (row != s.overscan_row && text >= s.source_rows)) return damage;
    if (row == s.overscan_row) text = s.source_rows;
    int64_t q = -s.origin_y_q8, y = (int64_t)text * b->cell_height;
    int64_t top = y - (q + RENDER_Q8_ONE - 1u) / RENDER_Q8_ONE;
    int64_t bottom = y + b->cell_height - q / RENDER_Q8_ONE;
    if (top < 0) top = 0;
    if (bottom > s.clip.height) bottom = s.clip.height;
    if (bottom <= top) return damage;
    return render_damage_union(damage, (render_rect){s.clip.x, s.clip.y + (uint32_t)top,
                               s.clip.width, (uint32_t)(bottom - top)});
}

static void render_mark_row(render_scene *s, const render_packet *p, uint32_t row)
{
    uint32_t *rows = RENDER_WRITE(uint32_t, s->view.row_frames);
    if (rows[row] == p->id.frame) return;
    rows[row] = p->id.frame;
    s->view.damage = render_damage_union(s->view.damage, render_row_damage(p, row));
#ifdef RENDER_TEST_HOOKS
    ++s->drawn_rows;
#endif
}

static void render_patch_scene(render_scene *s, const render_packet *p)
{
    const render_binding *b = &p->binding;
    render_cell *cells = RENDER_WRITE(render_cell, s->view.cells);
    size_t offset = 0;
    for (size_t i = 0; i < p->offer.run_count; ++i) {
        render_strip run = p->runs[i];
        size_t n = (size_t)run.row_count * b->cols;
        memcpy(cells + (size_t)run.first_row * b->cols, p->cells + offset, n * sizeof *p->cells);
        offset += n;
        for (uint32_t j = 0; j < run.row_count; ++j) render_mark_row(s, p, run.first_row + j);
    }
    if (p->offer.overscan_dirty || (b->overscan && s->overscan_count < b->cols &&
                                    s->background != p->offer.background)) {
        render_cell *extra = cells + (size_t)b->rows * b->cols;
        if (p->offer.overscan_dirty) {
            memcpy(extra, p->cells + offset, p->overscan_count * sizeof *extra);
            s->overscan_count = p->overscan_count;
        }
        for (size_t i = s->overscan_count; i < b->cols; ++i)
            extra[i] = (render_cell){.atlas_slot = RENDER_NO_SLOT, .bg = p->offer.background};
        render_mark_row(s, p, b->rows);
    }
    s->background = p->offer.background;
    if (p->offer.region == RENDER_REPLACE)
        memcpy(RENDER_WRITE(uint32_t, s->view.region_rgb), p->region_rgb,
               (size_t)p->region.width * p->region.height * sizeof(uint32_t));

    if (!p->tiles) return;
#ifdef RENDER_TEST_HOOKS
    uint64_t start = os_now_ns();
#endif
    for (uint32_t y = 0; y < b->rows + b->overscan; ++y) {
        bool redraw = false;
        for (uint32_t x = 0; x < b->cols; ++x) {
            uint32_t slot = cells[(size_t)y * b->cols + x].atlas_slot;
            redraw |= render_has_slot(b->runtime, slot) &&
                s->view.installed[render_atlas_index(b, slot)].frame == p->id.frame;
#ifdef RENDER_TEST_HOOKS
            ++s->scan_cells;
#endif
        }
        if (redraw) render_mark_row(s, p, y);
    }
#ifdef RENDER_TEST_HOOKS
    s->scan_ns = os_now_ns() - start;
#endif
}

static render_rc render_apply_scene(render_scene *s, const render_packet *p, render_install install,
                                    void *ctx, render_scene_view *out)
{
    const render_binding *b = &p->binding;
    render_resident *installed = RENDER_WRITE(render_resident, s->view.installed);
    if (p->sheet) {
        memset(installed, 0, ((size_t)b->sheet.count + b->runtime.count) * sizeof *installed);
        for (size_t i = 0; i < p->sheet->count; ++i) {
            const render_tile_view *v = &p->sheet->tiles[i];
            render_delta d = {v->image, v->rect.width, v->rect.height, 0};
            if (install(ctx, i, d, v->page) != RENDER_OK) return RENDER_LOST;
            installed[i] = (render_resident){d.image.version, d.width, d.height, 0};
        }
    }
    for (size_t i = 0; i < p->tiles; ++i) {
        render_delta d = p->deltas[i];
        size_t slot = render_atlas_index(b, d.image.slot);
        if (install(ctx, slot, d, p->r8 + d.offset) != RENDER_OK) return RENDER_LOST;
        installed[slot] = (render_resident){d.image.version, d.width, d.height, p->id.frame};
    }
#ifdef RENDER_TEST_HOOKS
    s->scan_cells = s->drawn_rows = 0;
    s->scan_ns = 0;
    s->observed_binding = *b;
#endif
    s->view.damage = p->damage;
    s->view.region = p->region;
    render_patch_scene(s, p);
    *out = s->view;
    return RENDER_OK;
}
#endif
