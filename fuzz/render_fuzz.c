#include "../tests/render_fixture.h"

typedef struct render_model {
    uint32_t installed[RF_SLOTS];
    uint8_t coverage[RF_SLOTS], source[RF_SLOTS];
    uint32_t fifo[RF_SLOTS];
    size_t count;
    uint32_t cursor;
    render_image_id scene[RF_ROWS + 1];
} render_model;

static void model_publish(render_model *m, uint32_t slot)
{
    for (size_t i = 0; i < m->count; ++i)
        if (m->fifo[i] == slot) return;
    ASSERT(m->count < RF_SLOTS);
    m->fifo[m->count++] = slot;
}

static void model_frame(render_fixture *f, render_model *m, uint8_t arg)
{
    render_counters before, after;
    render_stats(f->r, &before);
    bool full = before.state == RENDER_IDLE_FULL;
    rf_offer(f, full ? RENDER_FULL : RENDER_PATCH);
    uint32_t row = (uint32_t)(arg >> 2) % (RF_ROWS + 1u);
    uint32_t slot = arg % RF_SLOTS;
    bool patch = (arg & 0x10u) != 0;
    f->offer.tile_descriptors = (size_t)(arg & 3u);
    f->offer.tile_bytes = (size_t)((arg >> 5) & 3u) * RF_TILE;
    render_image_id proposed = f->states[slot] == RENDER_IMAGE_INK ? f->tiles[slot].image :
        (render_image_id){RENDER_NO_SLOT, 0};
    if (patch) {
        rf_wide(f, row, proposed);
        if (!full && row == RF_ROWS) f->offer.overscan_dirty = true;
        else if (!full) {
            f->offer.run_count = 1;
            f->runs[0] = (render_strip){row, 1};
        }
    }
    if (full)
        for (uint32_t i = 0; i <= RF_ROWS; ++i) rf_wide(f, i, proposed);
    f->stale_stamp = (arg & 0x80u) != 0;
    render_frame_id id = {0};
    render_rc rc = rf_submit(f, &id, NULL);
    if (f->stale_stamp) {
        render_stats(f->r, &after);
        ASSERT(rc == RENDER_STALE && !id.frame && before.accepted.frame == after.accepted.frame);
        ASSERT(before.watermark == after.watermark && before.queued == after.queued);
        return;
    }
    ASSERT(rc == RENDER_OK && id.frame == before.accepted.frame + 1u);
    ASSERT(render_submit(f->r, NULL, NULL, NULL, NULL) == RENDER_BUSY);
    size_t sent = f->offer.tile_bytes / RF_TILE;
    if (sent > f->offer.tile_descriptors) sent = f->offer.tile_descriptors;
    if (sent > m->count) sent = m->count;
    for (size_t i = 0; i < sent; ++i) {
        uint32_t index = m->fifo[i];
        m->installed[index] = f->tiles[index].image.version;
        m->coverage[index] = m->source[index];
    }
    m->count -= sent;
    memmove(m->fifo, m->fifo + sent, m->count * sizeof *m->fifo);
    /* Accepted input-free offers replay exactly the fixture's one-slot grant. */
    if (m->cursor < RF_SLOTS) {
        uint32_t i = m->cursor++;
        if (f->states[i] == RENDER_IMAGE_INK && m->installed[i] != f->tiles[i].image.version)
            model_publish(m, i);
    }
    if (full)
        for (uint32_t i = 0; i <= RF_ROWS; ++i) m->scene[i] = proposed;
    else if (patch) m->scene[row] = proposed;
    ASSERT(rf_finish(f) == RENDER_OK);
    render_stats(f->r, &after);
    ASSERT(after.tiles == sent && after.tile_bytes == sent * RF_TILE && after.queued == m->count);
    ASSERT(after.tile_inspections <= sent + 1u);
    render_test_view view = render_test_inspect(f->r);
    ASSERT(view.scan_cells == (sent ? RF_COLS * (RF_ROWS + 1u) : 0u));
    for (size_t i = 0; i <= RF_ROWS; ++i) {
        render_image_id image = m->scene[i];
        render_sample left = rf_sample(f, i * RF_COLS), right = rf_sample(f, i * RF_COLS + 1);
        bool ink = image.slot != RENDER_NO_SLOT && m->installed[image.slot - RF_FIRST] == image.version;
        ASSERT(left.cell.glyph_index == image.version && left.cell.atlas_slot == image.slot);
        ASSERT((left.coverage != NULL) == ink && (right.coverage != NULL) == ink);
        if (ink) ASSERT(left.coverage[0] == m->coverage[image.slot - RF_FIRST]);
    }
}

static void model_restart(render_fixture *f, render_model *m)
{
    render_counters c;
    render_stats(f->r, &c);
    rf_offer(f, c.state == RENDER_IDLE_FULL ? RENDER_FULL : RENDER_PATCH);
    if (c.state == RENDER_IDLE_FULL) {
        for (uint32_t i = 0; i <= RF_ROWS; ++i)
            rf_wide(f, i, (render_image_id){RENDER_NO_SLOT, 0});
    }
    f->stale_stamp = false;
    render_frame_id id;
    ASSERT(rf_submit(f, &id, NULL) == RENDER_OK);
    render_test_owner owner = render_test_owner_get(f->r);
    ASSERT(rf_dispatch(owner, rf_take(owner)) == RENDER_OK);
    ASSERT(render_event(f->r, &(render_notice){.kind = RENDER_NOTICE_LOST, .id = id}) == RENDER_LOST);
    ASSERT(render_bind(f->r, &f->binding, &f->sheet, NULL) == RENDER_OK);
    m->cursor = 0;
    m->count = 0;
    memset(m->installed, 0, sizeof m->installed);
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    render_fixture f;
    render_model m = {0};
    rf_init(&f);
    f.setup.driver = render_test_manual_null();
    if (base_arena_init(&f.arena, (RF_ROWS + 1u) * os_page_size()) != 0) return 0;
    ASSERT(os_wake_init(&f.wake) == OS_OK);
    ASSERT(render_open(&f.arena, &f.setup, &f.r) == RENDER_OK);
    ASSERT(render_bind(f.r, &f.binding, &f.sheet, NULL) == RENDER_OK);
    for (size_t i = 0; i < size / 2u; ++i) {
        uint8_t op = data[2u * i], arg = data[2u * i + 1u];
        uint32_t slot = arg % RF_SLOTS;
        switch (op % 5u) {
        case 0:
            rf_notify(&f, slot, arg);
            m.source[slot] = arg;
            model_publish(&m, slot);
            break;
        case 1:
        case 2:
            model_frame(&f, &m, arg);
            break;
        case 3:
            model_restart(&f, &m);
            break;
        case 4:
            rf_offer(&f, RENDER_FULL);
            f.runs[0].row_count = 0;
            render_frame_id id = {0};
            ASSERT(rf_submit(&f, &id, NULL) == RENDER_INVALID && !id.frame && !f.row_reads);
            break;
        }
    }
    ASSERT(render_close(f.r) == RENDER_OK);
    os_wake_close(&f.wake);
    base_arena_free(&f.arena);
    return 0;
}
