#include "render_fixture.h"
#include "render/render_driver.h"
#include <stdio.h>

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "render_test:%d: %s\n", __LINE__, #c); return 1; } } while (0)

static int start(render_fixture *f)
{
    rf_init(f);
    /* One page per live fixture row is ample for the two tiny scene copies,
     * atlas, packet, metadata and setup. */
    CHECK(base_arena_init(&f->arena, (RF_ROWS + 1u) * os_page_size()) == 0);
    CHECK(render_open(&f->arena, &f->setup, &f->r) == RENDER_OK);
    render_counters c;
    render_stats(f->r, &c);
    CHECK(c.state == RENDER_UNBOUND);
    CHECK(render_bind(f->r, &f->binding, &f->sheet, NULL) == RENDER_OK);
    render_stats(f->r, &c);
    CHECK(c.state == RENDER_IDLE_FULL);
    return 0;
}

static int stop(render_fixture *f)
{
    CHECK(render_close(f->r) == RENDER_OK);
    CHECK(base_arena_mark(&f->arena) == 0);
    base_arena_free(&f->arena);
    return 0;
}

static render_rc notice(render_fixture *f, render_frame_id id, render_notice_kind kind,
                        render_clock clock, uint64_t ns)
{
    return render_event(f->r, &(render_notice){.kind = kind, .id = id, .clock = clock, .ns = ns});
}

static int lifecycle(void)
{
    render_fixture f;
    CHECK(start(&f) == 0);
    render_counters c;
    render_frame_id id = {0};
    rf_offer(&f, RENDER_PATCH);
    CHECK(rf_submit(&f, &id, NULL) == RENDER_STALE && id.frame == 0);
    rf_offer(&f, RENDER_FULL);
    f.offer.stamp.input_first = 7;
    f.offer.stamp.input_last = 8;
    CHECK(rf_submit(&f, &id, NULL) == RENDER_OK && id.frame == 1 && id.epoch);
    render_stats(f.r, &c);
    CHECK(c.state == RENDER_ACCEPTED && c.stamp.input_first == 7 && c.stamp.input_last == 8);
    CHECK(render_submit(f.r, NULL, NULL, NULL, NULL) == RENDER_BUSY && f.stamp_reads == 2);
    CHECK(render_bind(f.r, &f.binding, &f.sheet, NULL) == RENDER_BUSY);
    CHECK(notice(&f, id, RENDER_NOTICE_ISSUED, RENDER_SYNTHETIC, 1) == RENDER_STALE);
    CHECK(notice(&f, id, RENDER_NOTICE_DEVICE_DONE, RENDER_MONOTONIC, 1) == RENDER_INVALID);
    CHECK(notice(&f, id, RENDER_NOTICE_DEVICE_DONE, RENDER_SYNTHETIC, 1) == RENDER_OK);
    CHECK(notice(&f, id, RENDER_NOTICE_DEVICE_DONE, RENDER_SYNTHETIC, 1) == RENDER_STALE);
    CHECK(notice(&f, id, RENDER_NOTICE_DISCARDED, RENDER_UNTIMED, 0) == RENDER_OK);
    CHECK(notice(&f, id, RENDER_NOTICE_PRESENTED, RENDER_SYNTHETIC, 1) == RENDER_STALE);
    render_stats(f.r, &c);
    CHECK(c.device == RENDER_WAIT_FENCE && c.presentation == RENDER_WAIT_DISPLAY && !c.t5 && !c.t6);
    CHECK(rf_pump(&f) == RENDER_MORE); /* consume */
    CHECK(rf_pump(&f) == RENDER_MORE); /* real issue within the synthetic driver */
    render_stats(f.r, &c);
    CHECK(c.state == RENDER_ISSUED && c.device == RENDER_DEVICE_DONE && c.presentation == RENDER_DISCARDED);
    CHECK(c.t4 && c.t5 == c.t4 && c.t6 == 0 && c.issue_clock == RENDER_SYNTHETIC);
    CHECK(render_submit(f.r, NULL, NULL, NULL, NULL) == RENDER_BUSY);
    CHECK(rf_finish(&f) == RENDER_OK);
    render_stats(f.r, &c);
    CHECK(c.state == RENDER_IDLE && c.presentation == RENDER_DISCARDED && !c.t6);

    render_frame_id old = id;
    rf_offer(&f, RENDER_PATCH);
    CHECK(rf_submit(&f, &id, NULL) == RENDER_OK && id.frame == old.frame + 1u);
    CHECK(notice(&f, old, RENDER_NOTICE_LOST, RENDER_UNTIMED, 0) == RENDER_STALE);
    CHECK(rf_pump(&f) == RENDER_MORE);
    CHECK(rf_pump(&f) == RENDER_MORE);
    CHECK(notice(&f, id, RENDER_NOTICE_PRESENTED, RENDER_SYNTHETIC, 1) == RENDER_OK);
    render_stats(f.r, &c);
    CHECK(c.state == RENDER_ISSUED && c.presentation == RENDER_PRESENTED &&
          c.device == RENDER_WAIT_FENCE && c.t6 == c.t4);
    CHECK(rf_finish(&f) == RENDER_OK);
    render_stats(f.r, &c);
    CHECK(c.state == RENDER_IDLE && c.t5 >= c.t4 && c.t6 == c.t4 && c.draw.scan_cells == 0);

    rf_offer(&f, RENDER_PATCH);
    CHECK(rf_submit(&f, &id, NULL) == RENDER_OK);
    CHECK(rf_pump(&f) == RENDER_MORE && rf_pump(&f) == RENDER_MORE);
    CHECK(notice(&f, id, RENDER_NOTICE_PRESENTED, RENDER_UNTIMED, 0) == RENDER_OK);
    CHECK(rf_finish(&f) == RENDER_OK);
    render_stats(f.r, &c);
    CHECK(c.presentation == RENDER_PRESENTED && !c.t6 && c.presentation_clock == RENDER_UNTIMED);
    return stop(&f);
}

static int delivery(void)
{
    render_fixture f;
    CHECK(start(&f) == 0);
    rf_notify(&f, 0, 10);
    rf_notify(&f, 1, 20);
    rf_notify(&f, 2, 30);
    rf_notify(&f, 1, 21); /* replace in place, ahead of slot 2 */
    render_image_changed(f.r, f.binding.font, f.tiles[1].image.slot); /* deduplicate */
    rf_wide(&f, 0, f.tiles[0].image);
    rf_wide(&f, 1, f.tiles[1].image);
    rf_wide(&f, RF_ROWS, f.tiles[1].image);
    f.cells[2][2].atlas_slot = f.sheet.marker[0].slot;
    f.cells[2][2].glyph_index = f.sheet.marker[0].version;
    rf_offer(&f, RENDER_FULL);
    f.offer.tile_descriptors = RF_SLOTS;
    f.offer.tile_bytes = RF_TILE + 1u; /* inspect next complete tile, leave it owed */
    f.offer.region = RENDER_REPLACE;
    f.rgb[0] = 0x111111;
    f.rgb[3] = 0x222222;
    f.rgb[2] = UINT32_MAX; /* ignored pitch padding */
    f.rgb[5] = UINT32_MAX;
    render_frame_id id = {99, 99};
    render_need need;
    f.stale_on_recheck = true;
    CHECK(rf_submit(&f, &id, &need) == RENDER_STALE && need.field == RENDER_FIELD_STAMP);
    render_counters c;
    render_stats(f.r, &c);
    CHECK(c.state == RENDER_IDLE_FULL && !c.accepted.frame && c.queued == RF_SLOTS && !c.watermark);
    CHECK(id.frame == 99 && f.tile_reads == 2);
    f.stamp_reads = f.tile_reads = 0;
    f.stale_on_recheck = false;
    CHECK(rf_submit(&f, &id, &need) == RENDER_OK && id.frame == 1);
    render_stats(f.r, &c);
    CHECK(c.cells == RF_COLS * (RF_ROWS + 1) && c.rows == RF_ROWS + 1 && c.runs == 1);
    CHECK(c.tiles == 1 && c.tile_bytes == RF_TILE && c.region_pixels == 4 &&
          c.tile_inspections == c.tiles + 1u && c.queued == 2 && c.watermark == 1);

    /* Overwrite every call-scoped payload before the driver sees the packet. */
    render_cell saved = f.cells[0][0];
    f.cells[0][0] = rf_blank();
    memset(f.pages[0], 0xee, sizeof f.pages[0]);
    f.rgb[3] = RENDER_RGB_MASK;
    f.runs[0].row_count = 0;
    CHECK(rf_finish(&f) == RENDER_OK);
    render_stats(f.r, &c);
    CHECK(c.draw.scan_cells == c.cells && c.draw.copied_tile_bytes == RF_TILE);
    CHECK(c.draw.damage.width == f.binding.client_width && c.draw.damage.height == f.binding.client_height);
    CHECK(c.draw.samples[0].cell.glyph_index == saved.glyph_index);
    for (size_t i = 0; i < RF_TILE; ++i) CHECK(c.draw.samples[0].coverage[i] == 10);
    CHECK(c.draw.samples[1].coverage[RF_W] == 10 && c.draw.samples[1].x_offset == RF_W);
    CHECK(!c.draw.samples[RF_COLS].coverage && c.draw.samples[RF_COLS].cell.attrs == f.cells[1][0].attrs);
    CHECK(c.draw.samples[2 * RF_COLS + 2].coverage && c.draw.region_rgb[2] == 0x222222);

    rf_notify(&f, 0, 11); /* recycled slot joins behind prior obligations */
    rf_wide(&f, 0, f.tiles[0].image);
    rf_offer(&f, RENDER_PATCH);
    f.runs[0] = (render_strip){0, 1};
    f.offer.run_count = 1;
    f.offer.tile_descriptors = 1;
    f.offer.tile_bytes = RF_TILE;
    f.stale_on_recheck = true;
    CHECK(rf_submit(&f, &id, &need) == RENDER_STALE && id.frame == 1);
    render_stats(f.r, &c);
    CHECK(c.accepted.frame == 1 && c.watermark == 1 && c.queued == 3 &&
          c.draw.samples[0].coverage[0] == 10 && c.draw.region_rgb[2] == 0x222222);
    f.stale_on_recheck = false;
    CHECK(rf_submit(&f, &id, NULL) == RENDER_OK && id.frame == 2);
    CHECK(rf_finish(&f) == RENDER_OK);
    render_stats(f.r, &c);
    CHECK(c.watermark == 2 && c.queued == 2 && c.draw.scan_cells == RF_COLS * (RF_ROWS + 1));
    CHECK(c.cells == RF_COLS && c.draw.drawn_rows == 3);
    CHECK(!c.draw.samples[0].coverage && !c.draw.samples[1].coverage); /* ABA: reject resident v1 */
    CHECK(c.draw.samples[0].cell.attrs == f.cells[0][0].attrs &&
          c.draw.samples[1].cell.bg == f.cells[0][1].bg && c.draw.samples[1].x_offset == RF_W);
    CHECK(c.draw.samples[RF_COLS].coverage[0] == 21 &&
          c.draw.samples[RF_ROWS * RF_COLS].coverage[0] == 21);
    CHECK(c.draw.region_rgb[2] == 0x222222);

    f.states[2] = RENDER_IMAGE_EMPTY;
    render_image_changed(f.r, f.binding.font, f.tiles[2].image.slot);
    render_wake wake;
    CHECK(render_pending(f.r, &wake) && wake.delivery);
    rf_offer(&f, RENDER_PATCH);
    f.offer.tile_descriptors = 1;
    f.offer.tile_bytes = RF_TILE;
    CHECK(rf_submit(&f, &id, NULL) == RENDER_OK && id.frame == 3);
    CHECK(rf_finish(&f) == RENDER_OK);
    render_stats(f.r, &c);
    CHECK(c.watermark == 4 && !c.queued && !c.cells && !c.runs && c.tiles == 1);
    CHECK(c.draw.samples[0].coverage[0] == 11 && c.draw.samples[1].coverage[RF_W] == 11);
    CHECK(c.draw.scan_cells == RF_COLS * (RF_ROWS + 1) && c.draw.drawn_rows == 1);
    for (size_t i = 0; i < RF_SLOTS; ++i) CHECK(rf_pump(&f) == (i + 1u == RF_SLOTS ? RENDER_OK : RENDER_MORE));
    render_stats(f.r, &c);
    CHECK(c.recovery_inspections == RF_SLOTS && !c.queued && !render_pending(f.r, &wake));

    rf_offer(&f, RENDER_PATCH);
    f.offer.region = RENDER_REMOVE;
    CHECK(rf_submit(&f, &id, NULL) == RENDER_OK && rf_finish(&f) == RENDER_OK);
    render_stats(f.r, &c);
    CHECK(!c.draw.scan_cells && !c.draw.drawn_rows && !c.draw.region_rgb && c.draw.damage.width == 2);
    CHECK(!f.row_reads && !f.identity_reads && !f.tile_reads && !f.region_reads);

    /* Installing new ink damages old-version users even if no row was
     * offered. Their earlier draw records must lose their coverage. */
    rf_notify(&f, 0, 12);
    rf_offer(&f, RENDER_PATCH);
    f.offer.tile_descriptors = 1;
    f.offer.tile_bytes = RF_TILE;
    CHECK(rf_submit(&f, &id, NULL) == RENDER_OK && rf_finish(&f) == RENDER_OK);
    render_stats(f.r, &c);
    CHECK(!c.cells && c.draw.drawn_rows == 1 && c.draw.samples[0].cell.glyph_index == saved.glyph_index + 1u &&
          !c.draw.samples[0].coverage && !c.draw.samples[1].coverage);
    return stop(&f);
}

/* Proxy exercises WAIT/MORE on the same production FSM and null backing.
 * First-member storage gives the wrapper its own per-service state. */
typedef struct delayed_driver {
    render_driver inner;
    void *state;
    unsigned opening, binding, consuming, quiescing;
} delayed_driver;

static render_rc delayed_open(void **out, base_arena *arena, const render_setup *s)
{
    delayed_driver *d = *out;
    if (!d) {
        d = base_arena_alloc(arena, sizeof *d, _Alignof(delayed_driver));
        if (!d) return RENDER_CAPACITY;
        *d = (delayed_driver){.inner = render_driver_null()};
        *out = d;
    }
    if (d->opening++ == 0) return RENDER_MORE;
    if (d->opening == 2) return RENDER_WAIT;
    return d->inner.ops->open(&d->state, arena, s);
}
static render_rc delayed_bind(void *state, const render_binding *b, const render_sheet *s)
{
    delayed_driver *d = state;
    if (d->binding++ == 0) return RENDER_WAIT;
    if (d->binding == 2) return RENDER_MORE;
    if (d->binding == 4) return RENDER_UNSUPPORTED;
    return d->inner.ops->bind(d->state, b, s);
}
static render_rc delayed_consume(void *state, const render_packet *p)
{
    delayed_driver *d = state;
    if (d->consuming++ == 0) return RENDER_WAIT;
    if (d->consuming == 2) return RENDER_MORE;
    return d->inner.ops->consume(d->state, p);
}
static render_rc delayed_poll(void *state, render_notice *n)
{
    delayed_driver *d = state;
    return d->inner.ops->poll(d->state, n);
}
static render_rc delayed_quiesce(void *state)
{
    delayed_driver *d = state;
    if (d->quiescing++ % 2u == 0) return RENDER_WAIT;
    d->consuming = 0;
    return d->state ? d->inner.ops->quiesce(d->state) : RENDER_OK;
}
static void delayed_close(void *state)
{
    delayed_driver *d = state;
    if (d->state) d->inner.ops->close(d->state);
}

static int restart(void)
{
    render_fixture f;
    rf_init(&f);
    render_driver_ops ops = *f.setup.driver.ops;
    ops.open = delayed_open;
    ops.bind = delayed_bind;
    ops.consume = delayed_consume;
    ops.poll = delayed_poll;
    ops.quiesce = delayed_quiesce;
    ops.close = delayed_close;
    f.setup.driver = (render_driver){&ops};
    CHECK(base_arena_init(&f.arena, (RF_ROWS + 1u) * os_page_size()) == 0);
    CHECK(render_open(&f.arena, &f.setup, &f.r) == RENDER_MORE);
    render_wake wake;
    CHECK(render_pending(f.r, &wake) && wake.runnable);
    CHECK(rf_pump(&f) == RENDER_WAIT && render_pending(f.r, &wake) && wake.waiting);
    CHECK(rf_pump(&f) == RENDER_OK);
    CHECK(render_bind(f.r, &f.binding, &f.sheet, NULL) == RENDER_WAIT);
    CHECK(render_pending(f.r, &wake) && wake.waiting);
    CHECK(render_bind(f.r, &f.binding, &f.sheet, NULL) == RENDER_MORE);
    CHECK(render_pending(f.r, &wake) && wake.runnable);
    CHECK(render_bind(f.r, &f.binding, &f.sheet, NULL) == RENDER_OK);
    CHECK(render_bind(f.r, &f.binding, &f.sheet, NULL) == RENDER_UNSUPPORTED);
    render_counters c;
    render_stats(f.r, &c);
    CHECK(c.state == RENDER_IDLE_FULL);

    rf_notify(&f, 0, 31);
    rf_notify(&f, 1, 32);
    rf_wide(&f, 0, f.tiles[0].image);
    rf_wide(&f, RF_ROWS, f.tiles[1].image);
    rf_offer(&f, RENDER_FULL);
    f.offer.tile_descriptors = 1;
    f.offer.tile_bytes = RF_TILE;
    render_frame_id lost;
    CHECK(rf_submit(&f, &lost, NULL) == RENDER_OK);
    CHECK(rf_pump(&f) == RENDER_WAIT && rf_pump(&f) == RENDER_MORE);
    CHECK(rf_pump(&f) == RENDER_MORE);
    CHECK(notice(&f, lost, RENDER_NOTICE_PRESENTED, RENDER_SYNTHETIC, 1) == RENDER_OK);
    CHECK(notice(&f, lost, RENDER_NOTICE_LOST, RENDER_UNTIMED, 0) == RENDER_LOST);
    CHECK(rf_pump(&f) == RENDER_WAIT && render_pending(f.r, &wake) && wake.waiting);
    CHECK(rf_submit(&f, &lost, NULL) == RENDER_LOST);
    render_stats(f.r, &c);
    CHECK(c.state == RENDER_QUIESCING && !c.t4 && !c.t5 && !c.t6 && c.watermark == 1);
    CHECK(rf_pump(&f) == RENDER_OK);
    render_stats(f.r, &c);
    CHECK(c.state == RENDER_UNBOUND && !c.watermark && !c.queued);
    CHECK(notice(&f, lost, RENDER_NOTICE_RELEASED, RENDER_UNTIMED, 0) == RENDER_STALE);

    CHECK(render_bind(f.r, &f.binding, &f.sheet, NULL) == RENDER_OK);
    CHECK(rf_pump(&f) == RENDER_MORE);
    render_stats(f.r, &c);
    CHECK(c.recovery_inspections == 1 && c.queued == 1);
    render_image_changed(f.r, f.binding.font, f.tiles[0].image.slot);
    CHECK(rf_pump(&f) == RENDER_MORE && rf_pump(&f) == RENDER_OK);
    render_stats(f.r, &c);
    CHECK(c.recovery_inspections == RF_SLOTS && c.queued == 2);
    rf_offer(&f, RENDER_FULL);
    f.offer.tile_descriptors = 1;
    f.offer.tile_bytes = RF_TILE;
    render_frame_id replay;
    CHECK(rf_submit(&f, &replay, NULL) == RENDER_OK && replay.epoch != lost.epoch && replay.frame == 1);
    CHECK(notice(&f, lost, RENDER_NOTICE_DEVICE_DONE, RENDER_SYNTHETIC, 1) == RENDER_STALE);
    CHECK(rf_pump(&f) == RENDER_WAIT && rf_pump(&f) == RENDER_MORE && rf_finish(&f) == RENDER_OK);
    render_stats(f.r, &c);
    CHECK(c.draw.samples[0].coverage[0] == 31 && !c.draw.samples[RF_ROWS * RF_COLS].coverage);

    rf_offer(&f, RENDER_PATCH);
    f.offer.tile_descriptors = 1;
    f.offer.tile_bytes = RF_TILE;
    CHECK(rf_submit(&f, &replay, NULL) == RENDER_OK && rf_finish(&f) == RENDER_OK);
    render_stats(f.r, &c);
    CHECK(c.draw.samples[RF_ROWS * RF_COLS].coverage[0] == 32 && !c.queued &&
          c.draw.damage.y == (RF_ROWS - 1) * RF_H - 1 && c.draw.damage.height == 1);

    rf_offer(&f, RENDER_PATCH);
    CHECK(rf_submit(&f, &replay, NULL) == RENDER_OK && rf_pump(&f) == RENDER_MORE && rf_pump(&f) == RENDER_MORE);
    CHECK(notice(&f, replay, RENDER_NOTICE_LOST, RENDER_UNTIMED, 0) == RENDER_LOST);
    render_stats(f.r, &c);
    CHECK(c.t4 && !c.t5 && !c.t6);
    CHECK(rf_pump(&f) == RENDER_WAIT && rf_pump(&f) == RENDER_OK);
    CHECK(render_bind(f.r, &f.binding, &f.sheet, NULL) == RENDER_OK);
    rf_offer(&f, RENDER_FULL);
    CHECK(rf_submit(&f, &replay, NULL) == RENDER_OK);
    CHECK(rf_pump(&f) == RENDER_WAIT && rf_pump(&f) == RENDER_MORE && rf_pump(&f) == RENDER_MORE);
    CHECK(render_close(f.r) == RENDER_WAIT && render_pending(f.r, &wake) && wake.waiting);
    CHECK(base_arena_mark(&f.arena) != 0);
    CHECK(notice(&f, replay, RENDER_NOTICE_RELEASED, RENDER_UNTIMED, 0) == RENDER_STALE);
    render_stats(f.r, &c);
    CHECK(c.state == RENDER_CLOSING && !c.t4 && !c.t5 && !c.t6);
    return stop(&f);
}

static int bounds(void)
{
    render_fixture f;
    CHECK(start(&f) == 0);
    render_need need;
    render_limits l = f.setup.limits;
    render_backend_caps caps = f.setup.driver.ops->query_caps(&f.setup);
    CHECK(render_measure(&f.binding, &l, &caps, &need) == RENDER_OK);
    l.tile_bytes = RF_TILE - 1;
    CHECK(render_measure(&f.binding, &l, &caps, &need) == RENDER_CAPACITY &&
          need.field == RENDER_FIELD_TILE_BYTES && need.required == RF_TILE);
    l = f.setup.limits;
    --caps.limits.target_bytes;
    CHECK(render_measure(&f.binding, &l, &caps, &need) == RENDER_CAPACITY &&
          need.field == RENDER_FIELD_TARGET && need.required == l.target_bytes);
    caps.limits = l;
    render_binding b = f.binding;
    b.target_stride = SIZE_MAX;
    CHECK(render_measure(&b, &l, &caps, &need) == RENDER_CAPACITY && need.field == RENDER_FIELD_TARGET);
    b.target_stride = f.binding.target_stride - 1u;
    CHECK(render_measure(&b, &l, &caps, &need) == RENDER_CAPACITY &&
          need.field == RENDER_FIELD_TARGET && need.required == f.binding.target_stride);
    b = f.binding;
    b.client_width = 0;
    CHECK(render_bind(f.r, &b, &f.sheet, &need) == RENDER_WAIT);
    CHECK(render_submit(f.r, NULL, NULL, NULL, NULL) == RENDER_BUSY);
    CHECK(render_bind(f.r, &f.binding, &f.sheet, &need) == RENDER_OK);
    l.cells--;
    CHECK(render_measure(&f.binding, &l, &caps, &need) == RENDER_CAPACITY && need.field == RENDER_FIELD_CELLS);
    b = f.binding;
    b.runtime.first = b.sheet.first;
    CHECK(render_bind(f.r, &b, &f.sheet, &need) == RENDER_INVALID);

    render_frame_id id = {9, 9};
    rf_offer(&f, RENDER_FULL);
    f.offer.overscan_dirty = false;
    CHECK(rf_submit(&f, &id, &need) == RENDER_INVALID && need.field == RENDER_FIELD_OVERSCAN);
    f.offer.overscan_dirty = true;
    f.offer.tile_bytes = f.setup.limits.tile_bytes + 1u;
    CHECK(rf_submit(&f, &id, &need) == RENDER_CAPACITY && need.field == RENDER_FIELD_TILE_BYTES);
    f.offer.tile_bytes = 0;
    f.offer.tile_descriptors = f.setup.limits.tile_descriptors + 1u;
    CHECK(rf_submit(&f, &id, &need) == RENDER_CAPACITY && need.field == RENDER_FIELD_TILE_DESCRIPTORS);
    f.offer.tile_descriptors = 0;
    f.runs[0].row_count = UINT32_MAX;
    CHECK(rf_submit(&f, &id, &need) == RENDER_INVALID && need.field == RENDER_FIELD_RUNS && !f.row_reads);
    f.runs[0].row_count = RF_ROWS;
    f.cells[0][0].attrs = RENDER_ATTR_WIDE_RIGHT;
    CHECK(rf_submit(&f, &id, &need) == RENDER_INVALID && need.field == RENDER_FIELD_CELL);
    f.cells[0][0].attrs = RENDER_ATTR_WIDE_LEFT;
    CHECK(rf_submit(&f, &id, &need) == RENDER_INVALID && need.field == RENDER_FIELD_CELL);
    f.cells[0][0] = rf_blank();
    f.cells[0][0].reserved = 1;
    CHECK(rf_submit(&f, &id, &need) == RENDER_INVALID && need.field == RENDER_FIELD_CELL);
    f.cells[0][0] = rf_blank();
    f.cells[0][0].bg = RENDER_RGB_MASK + 1u;
    CHECK(rf_submit(&f, &id, &need) == RENDER_INVALID && need.field == RENDER_FIELD_CELL);
    f.cells[0][0] = rf_blank();
    f.cells[0][0].atlas_slot = RF_FIRST;
    f.cells[0][0].glyph_index = 3;
    CHECK(rf_submit(&f, &id, &need) == RENDER_INVALID && need.field == RENDER_FIELD_CELL);
    f.cells[0][0] = rf_blank();
    f.offer.region = RENDER_REPLACE;
    f.region.stride = SIZE_MAX;
    CHECK(rf_submit(&f, &id, &need) == RENDER_INVALID && need.field == RENDER_FIELD_REGION);
    f.region.stride = 3;
    f.region.rect.width = 3;
    CHECK(rf_submit(&f, &id, &need) == RENDER_CAPACITY && need.field == RENDER_FIELD_REGION_WIDTH);
    f.region.rect.width = 2;
    f.rgb[0] = UINT32_MAX;
    CHECK(rf_submit(&f, &id, &need) == RENDER_INVALID && need.field == RENDER_FIELD_REGION);
    f.rgb[0] = 0;
    rf_notify(&f, 0, 1);
    f.offer.tile_bytes = RF_TILE;
    f.offer.tile_descriptors = 1;
    f.tiles[0].stride = SIZE_MAX;
    CHECK(rf_submit(&f, &id, &need) == RENDER_INVALID && need.field == RENDER_FIELD_TILE);
    f.tiles[0].stride = RF_PAGE_STRIDE;
    f.tiles[0].length = 1;
    CHECK(rf_submit(&f, &id, &need) == RENDER_INVALID && need.field == RENDER_FIELD_TILE);
    f.tiles[0].length = sizeof f.pages[0];
    CHECK(id.frame == 9);
    CHECK(rf_submit(&f, &id, &need) == RENDER_OK && id.frame == 1 && rf_finish(&f) == RENDER_OK);

    rf_offer(&f, RENDER_PATCH);
    f.offer.run_count = 2;
    f.runs[0] = (render_strip){0, 1};
    f.runs[1] = (render_strip){1, 1};
    CHECK(rf_submit(&f, &id, &need) == RENDER_INVALID && need.field == RENDER_FIELD_RUNS && !f.row_reads);
    f.runs[1].first_row = 2;
    CHECK(rf_submit(&f, &id, &need) == RENDER_OK && id.frame == 2 && rf_finish(&f) == RENDER_OK);
    render_counters c;
    render_stats(f.r, &c);
    CHECK(c.cells == 2 * RF_COLS && c.runs == 2 && !c.draw.scan_cells && !c.tile_inspections &&
          f.row_reads == 2 && f.identity_reads == 0);

    rf_offer(&f, RENDER_PATCH);
    f.offer.overscan_dirty = true;
    f.overscan_count = 1;
    CHECK(rf_submit(&f, &id, NULL) == RENDER_OK && rf_finish(&f) == RENDER_OK);
    render_stats(f.r, &c);
    CHECK(c.cells == 1 && !c.draw.scan_cells && c.draw.drawn_rows == 1);
    rf_offer(&f, RENDER_PATCH);
    f.offer.background = 0x123456;
    CHECK(rf_submit(&f, &id, NULL) == RENDER_OK && rf_finish(&f) == RENDER_OK);
    render_stats(f.r, &c);
    CHECK(!c.cells && c.draw.samples[RF_ROWS * RF_COLS + 1].cell.bg == 0x123456 &&
          !c.draw.scan_cells);
    CHECK(stop(&f) == 0);

    /* A borrowed reservation smaller than the allocations must roll back. */
    CHECK(base_arena_init(&f.arena, os_page_size()) == 0);
    CHECK(base_arena_alloc(&f.arena, os_page_size() - 1u, 1) != NULL);
    size_t mark = base_arena_mark(&f.arena);
    CHECK(render_open(&f.arena, &f.setup, &f.r) == RENDER_CAPACITY && base_arena_mark(&f.arena) == mark);
    base_arena_free(&f.arena);
    return 0;
}

int main(void)
{
    return lifecycle() || delivery() || restart() || bounds();
}
