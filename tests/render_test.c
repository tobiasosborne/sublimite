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
    CHECK(os_wake_init(&f->wake) == OS_OK);
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
    os_wake_close(&f->wake);
    base_arena_free(&f->arena);
    return 0;
}

static render_rc notice(render_fixture *f, render_frame_id id, render_notice_kind kind,
                        render_clock clock, uint64_t ns)
{
    return render_event(f->r, &(render_notice){.kind = kind, .id = id, .clock = clock, .ns = ns});
}

_Static_assert(_Generic(((render_setup){0}).native, plat_native_binding *:1, default:0),
               "render uses the native platform binding");

static render_rc finish_notices(render_fixture *f, render_test_owner owner)
{
    render_notice n;
    render_rc rc;
    while ((rc = owner.driver.ops->poll(owner.state, &n)) == RENDER_OK) {
        rc = render_event(f->r, &n);
        if (rc != RENDER_OK && rc != RENDER_STALE) return rc;
    }
    return rc == RENDER_WAIT ? RENDER_OK : rc;
}

typedef struct owner_job {
    render_test_owner owner;
    const render_packet *packet;
    render_notice issue;
    render_rc rc;
} owner_job;

static void owner_consume(void *arg)
{
    owner_job *job = arg;
    job->packet = rf_take(job->owner);
    job->rc = rf_dispatch(job->owner, job->packet);
    if (job->rc == RENDER_OK)
        job->rc = job->owner.driver.ops->poll(job->owner.state, &job->issue);
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
    CHECK(c.state == RENDER_ACCEPTED && c.stamp.input_last == 8 && f.stamp_reads == 1);
    CHECK(!render_test_inspect(f.r).binding.cols);
    CHECK(render_submit(f.r, NULL, NULL, NULL, NULL) == RENDER_BUSY && f.stamp_reads == 1);
    CHECK(render_bind(f.r, &f.binding, &f.sheet, NULL) == RENDER_BUSY);
    CHECK(notice(&f, id, RENDER_NOTICE_ISSUED, RENDER_SYNTHETIC, 1) == RENDER_STALE);
    CHECK(notice(&f, id, RENDER_NOTICE_DEVICE_DONE, RENDER_MONOTONIC, 1) == RENDER_INVALID);
    CHECK(notice(&f, id, RENDER_NOTICE_DEVICE_DONE, RENDER_SYNTHETIC, 1) == RENDER_OK);
    CHECK(notice(&f, id, RENDER_NOTICE_DEVICE_DONE, RENDER_SYNTHETIC, 1) == RENDER_STALE);
    CHECK(notice(&f, id, RENDER_NOTICE_DISCARDED, RENDER_UNTIMED, 0) == RENDER_OK);
    render_stats(f.r, &c);
    CHECK(c.device == RENDER_WAIT_FENCE && c.presentation == RENDER_WAIT_DISPLAY && !c.t5 && !c.t6);
    render_wake wake;
    CHECK(render_pending(f.r, &wake) && wake.waiting && !wake.runnable && wake.wake == &f.wake);

    owner_job job = {.owner = render_test_owner_get(f.r)};
    os_thread thread;
    CHECK(os_thread_start(&thread, owner_consume, &job) == OS_OK);
    CHECK(os_thread_join(&thread) == OS_OK && job.rc == RENDER_OK && job.packet && job.packet->sheet);
    CHECK(job.issue.kind == RENDER_NOTICE_ISSUED && render_event(f.r, &job.issue) == RENDER_OK);
    CHECK(finish_notices(&f, job.owner) == RENDER_OK);
    render_stats(f.r, &c);
    CHECK(c.state == RENDER_IDLE && c.device == RENDER_DEVICE_DONE && c.presentation == RENDER_DISCARDED);
    CHECK(c.t4 && c.t5 == c.t4 && !c.t6 && !render_test_inspect(f.r).scan_cells);
    CHECK(notice(&f, id, RENDER_NOTICE_LOST, RENDER_UNTIMED, 0) == RENDER_STALE);

    rf_offer(&f, RENDER_PATCH);
    CHECK(rf_submit(&f, &id, NULL) == RENDER_OK && id.frame == 2);
    CHECK(rf_dispatch(job.owner, rf_take(job.owner)) == RENDER_OK);
    CHECK(job.owner.driver.ops->poll(job.owner.state, &job.issue) == RENDER_OK);
    CHECK(render_event(f.r, &job.issue) == RENDER_OK);
    CHECK(notice(&f, id, RENDER_NOTICE_PRESENTED, RENDER_UNTIMED, 0) == RENDER_OK);
    render_stats(f.r, &c);
    CHECK(c.state == RENDER_ISSUED && c.presentation == RENDER_PRESENTED && c.device == RENDER_WAIT_FENCE);
    CHECK(finish_notices(&f, job.owner) == RENDER_OK);
    render_stats(f.r, &c);
    CHECK(c.state == RENDER_IDLE && c.t5 >= c.t4 && !c.t6);

    CHECK(notice(&f, (render_frame_id){id.epoch, 0}, RENDER_NOTICE_LOST, RENDER_UNTIMED, 0) == RENDER_LOST);
    render_stats(f.r, &c);
    CHECK(c.state == RENDER_UNBOUND && !c.watermark);
    CHECK(render_submit(f.r, NULL, NULL, NULL, NULL) == RENDER_WAIT);
    return stop(&f);
}

static int staged_binding(void)
{
    render_fixture f;
    CHECK(start(&f) == 0);
    rf_notify(&f, 0, 21);
    rf_wide(&f, 0, f.tiles[0].image);
    rf_offer(&f, RENDER_FULL);
    f.offer.tile_descriptors = 1;
    f.offer.tile_bytes = RF_TILE;
    render_frame_id old;
    CHECK(rf_submit(&f, &old, NULL) == RENDER_OK && rf_finish(&f) == RENDER_OK);
    render_binding first_binding = f.binding;
    ++f.binding.geometry;

    render_need need;
    render_sheet bad = f.sheet;
    ++bad.marker[0].version;
    CHECK(render_bind(f.r, &f.binding, &bad, &need) == RENDER_INVALID && need.field == RENDER_FIELD_SHEET);
    bad = f.sheet;
    --bad.ascii_count;
    CHECK(render_bind(f.r, &f.binding, &bad, &need) == RENDER_INVALID);
    CHECK(render_bind(f.r, &f.binding, &f.sheet, NULL) == RENDER_OK);
    render_counters c;
    render_stats(f.r, &c);
    CHECK(c.accepted.frame == old.frame && c.stamp.geometry == first_binding.geometry &&
          c.watermark == 1 && rf_sample(&f, 0).coverage[0] == 21);

    rf_offer(&f, RENDER_FULL);
    f.stale_stamp = true;
    render_frame_id next = old;
    CHECK(rf_submit(&f, &next, NULL) == RENDER_STALE && next.frame == old.frame);
    render_stats(f.r, &c);
    CHECK(c.accepted.frame == old.frame && c.stamp.geometry == first_binding.geometry &&
          c.rejected == RENDER_STALE && c.rejection.field == RENDER_FIELD_STAMP);
    f.stale_stamp = false;

    render_binding staged = f.binding;
    f.binding = first_binding;
    rf_offer(&f, RENDER_PATCH);
    CHECK(rf_submit(&f, &next, NULL) == RENDER_OK && next.epoch == old.epoch &&
          next.frame == old.frame + 1u && rf_finish(&f) == RENDER_OK);
    f.binding = staged;
    rf_offer(&f, RENDER_FULL);
    CHECK(rf_submit(&f, &next, NULL) == RENDER_OK && next.epoch != old.epoch && next.frame == old.frame + 2u);
    CHECK(notice(&f, (render_frame_id){old.epoch, 0}, RENDER_NOTICE_LOST, RENDER_UNTIMED, 0) == RENDER_STALE);
    render_stats(f.r, &c);
    CHECK(c.stamp.geometry == staged.geometry && c.recovery_inspections == 1 && c.queued == 1);
    CHECK(rf_finish(&f) == RENDER_OK && !rf_sample(&f, 0).coverage);
    rf_offer(&f, RENDER_PATCH);
    f.offer.tile_descriptors = 1;
    f.offer.tile_bytes = RF_TILE;
    CHECK(rf_submit(&f, &next, NULL) == RENDER_OK && rf_finish(&f) == RENDER_OK);
    CHECK(rf_sample(&f, 0).coverage[0] == 21);
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
    f.stale_stamp = true;
    CHECK(rf_submit(&f, &id, &need) == RENDER_STALE && need.field == RENDER_FIELD_STAMP);
    render_counters c;
    render_stats(f.r, &c);
    CHECK(c.state == RENDER_IDLE_FULL && !c.accepted.frame && c.queued == RF_SLOTS && !c.watermark);
    CHECK(id.frame == 99 && !f.tile_reads && f.stamp_reads == 1);
    f.stale_stamp = false;

    /* A malformed selected second tile refuses the whole copied prefix. */
    f.offer.tile_bytes = 2u * RF_TILE;
    size_t saved_length = f.tiles[1].length;
    f.tiles[1].length = 0;
    CHECK(rf_submit(&f, &id, &need) == RENDER_INVALID && id.frame == 99);
    render_stats(f.r, &c);
    CHECK(!c.watermark && c.queued == RF_SLOTS && c.rejected == RENDER_INVALID &&
          c.rejection.field == RENDER_FIELD_TILE);
    /* The same head is only a byte-grant lookahead this time. */
    f.offer.tile_bytes = RF_TILE + 1u;
    CHECK(rf_submit(&f, &id, &need) == RENDER_OK && id.frame == 1);
    f.tiles[1].length = saved_length;
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
    CHECK(render_test_inspect(f.r).scan_cells == c.cells);
    CHECK(render_test_inspect(f.r).scene.damage.width == f.binding.client_width && render_test_inspect(f.r).scene.damage.height == f.binding.client_height);
    CHECK(rf_sample(&f, 0).cell.glyph_index == saved.glyph_index);
    for (size_t i = 0; i < RF_TILE; ++i) CHECK(rf_sample(&f, 0).coverage[i] == 10);
    CHECK(rf_sample(&f, 1).coverage[RF_W] == 10 && rf_sample(&f, 1).x_offset == RF_W);
    CHECK(!rf_sample(&f, RF_COLS).coverage && rf_sample(&f, RF_COLS).cell.attrs == f.cells[1][0].attrs);
    CHECK(rf_sample(&f, 2 * RF_COLS + 2).coverage && render_test_inspect(f.r).scene.region_rgb[2] == 0x222222);

    rf_notify(&f, 0, 11); /* recycled slot joins behind prior obligations */
    rf_wide(&f, 0, f.tiles[0].image);
    rf_offer(&f, RENDER_PATCH);
    f.runs[0] = (render_strip){0, 1};
    f.offer.run_count = 1;
    f.offer.tile_descriptors = 1;
    f.offer.tile_bytes = RF_TILE;
    f.stale_stamp = true;
    CHECK(rf_submit(&f, &id, &need) == RENDER_STALE && id.frame == 1);
    render_stats(f.r, &c);
    CHECK(c.accepted.frame == 1 && c.watermark == 1 && c.queued == 3 &&
          rf_sample(&f, 0).coverage[0] == 10 && render_test_inspect(f.r).scene.region_rgb[2] == 0x222222);
    f.stale_stamp = false;
    CHECK(rf_submit(&f, &id, NULL) == RENDER_OK && id.frame == 2);
    CHECK(rf_finish(&f) == RENDER_OK);
    render_stats(f.r, &c);
    CHECK(c.watermark == 2 && c.queued == 2 && render_test_inspect(f.r).scan_cells == RF_COLS * (RF_ROWS + 1));
    CHECK(c.cells == RF_COLS && render_test_inspect(f.r).drawn_rows == 3);
    CHECK(!rf_sample(&f, 0).coverage && !rf_sample(&f, 1).coverage); /* ABA: reject resident v1 */
    CHECK(rf_sample(&f, 0).cell.attrs == f.cells[0][0].attrs &&
          rf_sample(&f, 1).cell.bg == f.cells[0][1].bg && rf_sample(&f, 1).x_offset == RF_W);
    CHECK(rf_sample(&f, RF_COLS).coverage[0] == 21 &&
          rf_sample(&f, RF_ROWS * RF_COLS).coverage[0] == 21);
    CHECK(render_test_inspect(f.r).scene.region_rgb[2] == 0x222222);

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
    CHECK(rf_sample(&f, 0).coverage[0] == 11 && rf_sample(&f, 1).coverage[RF_W] == 11);
    CHECK(render_test_inspect(f.r).scan_cells == RF_COLS * (RF_ROWS + 1) && render_test_inspect(f.r).drawn_rows == 1);
    render_stats(f.r, &c);
    CHECK(c.recovery_inspections == RF_SLOTS && !c.queued && !render_pending(f.r, &wake));

    rf_offer(&f, RENDER_PATCH);
    f.offer.region = RENDER_REMOVE;
    CHECK(rf_submit(&f, &id, NULL) == RENDER_OK && rf_finish(&f) == RENDER_OK);
    render_stats(f.r, &c);
    CHECK(!render_test_inspect(f.r).scan_cells && !render_test_inspect(f.r).drawn_rows && !render_test_inspect(f.r).scene.region_rgb && render_test_inspect(f.r).scene.damage.width == 2);
    CHECK(!f.row_reads && !f.identity_reads && !f.tile_reads && !f.region_reads);

    /* Installing new ink damages old-version users even if no row was
     * offered. Their earlier draw records must lose their coverage. */
    rf_notify(&f, 0, 12);
    rf_offer(&f, RENDER_PATCH);
    f.offer.tile_descriptors = 1;
    f.offer.tile_bytes = RF_TILE;
    CHECK(rf_submit(&f, &id, NULL) == RENDER_OK && rf_finish(&f) == RENDER_OK);
    render_stats(f.r, &c);
    CHECK(!c.cells && render_test_inspect(f.r).drawn_rows == 1 && rf_sample(&f, 0).cell.glyph_index == saved.glyph_index + 1u &&
          !rf_sample(&f, 0).coverage && !rf_sample(&f, 1).coverage);
    rf_notify(&f, 0, 13);
    f.tiles[0].rect.width = RF_W;
    rf_wide(&f, 0, f.tiles[0].image);
    rf_offer(&f, RENDER_PATCH);
    f.offer.run_count = 1;
    f.runs[0] = (render_strip){0, 1};
    f.offer.tile_descriptors = 1;
    f.offer.tile_bytes = RF_TILE;
    CHECK(rf_submit(&f, &id, NULL) == RENDER_OK && rf_finish(&f) == RENDER_OK);
    CHECK(rf_sample(&f, 0).coverage && !rf_sample(&f, 1).coverage &&
          rf_sample(&f, 1).cell.attrs == f.cells[0][1].attrs && rf_sample(&f, 1).x_offset == RF_W);
    render_frame_id lost = id;
    CHECK(notice(&f, (render_frame_id){lost.epoch, 0}, RENDER_NOTICE_LOST, RENDER_UNTIMED, 0) == RENDER_LOST);
    CHECK(render_bind(f.r, &f.binding, &f.sheet, NULL) == RENDER_OK);
    rf_offer(&f, RENDER_FULL);
    CHECK(rf_submit(&f, &id, NULL) == RENDER_OK && id.epoch != lost.epoch && id.frame == lost.frame + 1u);
    CHECK(rf_finish(&f) == RENDER_OK && !rf_sample(&f, 0).coverage);
    rf_offer(&f, RENDER_PATCH);
    f.offer.tile_descriptors = 1;
    f.offer.tile_bytes = RF_TILE;
    CHECK(rf_submit(&f, &id, NULL) == RENDER_OK && rf_finish(&f) == RENDER_OK);
    CHECK(rf_sample(&f, 0).coverage && rf_sample(&f, 0).coverage[0] == 13 && !rf_sample(&f, 1).coverage);
    return stop(&f);
}


typedef struct delayed {
    const render_driver_ops *inner;
    void *state;
    bool hold;
} delayed;

static render_rc delayed_open(void **out, base_arena *arena, const render_setup *s, render_driver_port *port)
{
    delayed *d = *out;
    if (d) return d->inner->open(&d->state, arena, s, port);
    d = base_arena_alloc(arena, sizeof *d, _Alignof(delayed));
    if (!d) return RENDER_CAPACITY;
    *d = (delayed){.inner = render_driver_null().ops};
    *out = d;
    return RENDER_WAIT;
}
static render_rc delayed_bind(void *state, const render_binding *b, const render_sheet *s)
{
    delayed *d = state;
    return d->inner->bind(d->state, b, s);
}
static render_rc delayed_consume(void *state, const render_packet *p)
{
    delayed *d = state;
    return d->inner->consume(d->state, p);
}
static render_rc delayed_poll(void *state, render_notice *n)
{
    delayed *d = state;
    return d->inner->poll(d->state, n);
}
static render_rc delayed_quiesce(void *state, render_quiescence mode)
{
    delayed *d = state;
    return d->hold ? RENDER_WAIT : d->inner->quiesce(d->state, mode);
}
static void delayed_close(void *state)
{
    delayed *d = state;
    if (d->state) d->inner->close(d->state);
}

static int physical_retirement(void)
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
    f.setup.wake = NULL;
    CHECK(render_open(&f.arena, &f.setup, &f.r) == RENDER_INVALID && !f.r);
    CHECK(os_wake_init(&f.wake) == OS_OK);
    f.setup.wake = &f.wake;
    CHECK(render_open(&f.arena, &f.setup, &f.r) == RENDER_WAIT);
    render_wake wake;
    CHECK(render_pending(f.r, &wake) && wake.waiting && wake.wake == &f.wake);
    CHECK(render_open(&f.arena, &f.setup, &f.r) == RENDER_OK);
    CHECK(render_bind(f.r, &f.binding, &f.sheet, NULL) == RENDER_OK);
    delayed *d = render_test_owner_get(f.r).state;
    rf_offer(&f, RENDER_FULL);
    render_frame_id id;
    CHECK(rf_submit(&f, &id, NULL) == RENDER_OK);
    d->hold = true;
    CHECK(rf_finish(&f) == RENDER_OK);
    render_counters c;
    render_stats(f.r, &c);
    CHECK(c.state == RENDER_RETIRING && c.t5 && c.t6);
    CHECK(render_pending(f.r, &wake) && wake.waiting && wake.wake == &f.wake);
    rf_offer(&f, RENDER_PATCH);
    CHECK(rf_submit(&f, &id, NULL) == RENDER_WAIT && f.stamp_reads == 0);
    d->hold = false;
    CHECK(rf_submit(&f, &id, NULL) == RENDER_OK);

    d->hold = true;
    CHECK(notice(&f, id, RENDER_NOTICE_PRESENTED, RENDER_SYNTHETIC, 1) == RENDER_OK);
    CHECK(notice(&f, id, RENDER_NOTICE_LOST, RENDER_UNTIMED, 0) == RENDER_LOST);
    render_stats(f.r, &c);
    CHECK(c.state == RENDER_QUIESCING && !c.t4 && !c.t5 && !c.t6);
    CHECK(render_bind(f.r, &f.binding, &f.sheet, NULL) == RENDER_WAIT);
    CHECK(notice(&f, id, RENDER_NOTICE_DEVICE_DONE, RENDER_SYNTHETIC, 1) == RENDER_STALE);
    d->hold = false;
    CHECK(render_bind(f.r, &f.binding, &f.sheet, NULL) == RENDER_OK);

    d->hold = true;
    CHECK(render_close(f.r) == RENDER_WAIT && render_pending(f.r, &wake) && wake.waiting);
    CHECK(base_arena_mark(&f.arena) != 0);
    d->hold = false;
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
    CHECK(render_submit(f.r, NULL, NULL, NULL, NULL) == RENDER_WAIT);
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
    f.cells[0][0].atlas_slot = RF_FIRST + RF_SLOTS;
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
    f.cells[0][0].bg = 0x0a0a0a;
    f.cells[2][0].bg = 0x0b0b0b;
    f.cells[RF_ROWS][0].bg = 0x0c0c0c;
    f.overscan_count = 1;
    f.offer.overscan_dirty = true;
    CHECK(rf_submit(&f, &id, &need) == RENDER_OK && id.frame == 2);
    render_test_owner owner = render_test_owner_get(f.r);
    const render_packet *p = rf_take(owner);
    CHECK(p && !p->sheet && p->overscan_count == 1 && p->cells[0].bg == 0x0a0a0a &&
          p->cells[RF_COLS].bg == 0x0b0b0b && p->cells[2 * RF_COLS].bg == 0x0c0c0c);
    CHECK(rf_dispatch(owner, p) == RENDER_OK && finish_notices(&f, owner) == RENDER_OK);
    render_counters c;
    render_stats(f.r, &c);
    CHECK(c.cells == 2 * RF_COLS + 1 && c.runs == 2 && !render_test_inspect(f.r).scan_cells && !c.tile_inspections &&
          f.row_reads == 3 && f.identity_reads == 0);

    rf_offer(&f, RENDER_PATCH);
    f.offer.overscan_dirty = true;
    f.overscan_count = 1;
    CHECK(rf_submit(&f, &id, NULL) == RENDER_OK && rf_finish(&f) == RENDER_OK);
    render_stats(f.r, &c);
    CHECK(c.cells == 1 && !render_test_inspect(f.r).scan_cells && render_test_inspect(f.r).drawn_rows == 1);
    rf_offer(&f, RENDER_PATCH);
    f.offer.background = 0x123456;
    CHECK(rf_submit(&f, &id, NULL) == RENDER_OK && rf_finish(&f) == RENDER_OK);
    render_stats(f.r, &c);
    CHECK(!c.cells && rf_sample(&f, RF_ROWS * RF_COLS + 1).cell.bg == 0x123456 &&
          !render_test_inspect(f.r).scan_cells);
    CHECK(stop(&f) == 0);

    /* A borrowed reservation smaller than the allocations must roll back. */
    f.r = NULL;
    CHECK(os_wake_init(&f.wake) == OS_OK);
    CHECK(base_arena_init(&f.arena, os_page_size()) == 0);
    CHECK(base_arena_alloc(&f.arena, os_page_size() - 1u, 1) != NULL);
    size_t mark = base_arena_mark(&f.arena);
    CHECK(render_open(&f.arena, &f.setup, &f.r) == RENDER_CAPACITY && base_arena_mark(&f.arena) == mark);
    base_arena_free(&f.arena);
    os_wake_close(&f.wake);
    return 0;
}

static int verify_identity_policy(void)
{
    render_fixture f;
    CHECK(start(&f) == 0);
    rf_notify(&f, 0, 13);
    rf_wide(&f, 0, f.tiles[0].image);
    ++f.cells[0][0].glyph_index;
    rf_offer(&f, RENDER_FULL);
    render_frame_id id = {0};
#ifdef RENDER_ASSERT_IDENTITIES
    CHECK(rf_submit(&f, &id, NULL) == RENDER_INVALID && !id.frame && f.identity_reads == 1);
#else
    CHECK(rf_submit(&f, &id, NULL) == RENDER_OK && id.frame == 1 && !f.identity_reads);
    CHECK(rf_finish(&f) == RENDER_OK && !rf_sample(&f, 0).coverage);
#endif
    return stop(&f);
}

int main(void)
{
    return lifecycle() || staged_binding() || delivery() || physical_retirement() || bounds() ||
        verify_identity_policy();
}
