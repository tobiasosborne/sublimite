#include "render/render_submit_prep.h"
#include "trace/trace.h"

render_rc render_measure(const render_binding *b, const render_limits *l,
                         const render_backend_caps *caps, render_need *need)
{
    if (need) *need = (render_need){0};
    if (!b || !l || !caps)
        return render_reason(need, RENDER_FIELD_BINDING, RENDER_INVALID, 0, 0);
    if (!b->client_width || !b->client_height) return RENDER_WAIT;
    if (!b->surface || !b->geometry || !b->font.owner || !b->font.generation ||
        !b->cols || !b->rows || !b->cell_width || !b->cell_height || b->overscan > 1 ||
        b->rows == RENDER_NO_ROW || b->sheet.count < RENDER_TILE_MAX_SPAN ||
        b->sheet.count > RENDER_NO_SLOT - b->sheet.first ||
        b->runtime.count > RENDER_NO_SLOT - b->runtime.first)
        return render_reason(need, RENDER_FIELD_BINDING, RENDER_INVALID, 0, 0);
    if (b->runtime.count &&
        (render_has_slot(b->sheet, b->runtime.first) || render_has_slot(b->runtime, b->sheet.first)))
        return render_reason(need, RENDER_FIELD_ATLAS, RENDER_INVALID, 0, 0);

#define RENDER_ADMIT(field, value, local, backend) do { \
    size_t required = (value), available = (local) < (backend) ? (local) : (backend); \
    if (required > available) return render_reason(need, (field), RENDER_CAPACITY, required, available); \
} while (0)
#define RENDER_LIMIT(field, member, value) RENDER_ADMIT(field, value, l->member, caps->limits.member)
    RENDER_LIMIT(RENDER_FIELD_WIDTH, client_width, b->client_width);
    RENDER_LIMIT(RENDER_FIELD_HEIGHT, client_height, b->client_height);
    RENDER_LIMIT(RENDER_FIELD_COLS, cols, b->cols);
    RENDER_LIMIT(RENDER_FIELD_ROWS, rows, b->rows);
    RENDER_LIMIT(RENDER_FIELD_CELL_WIDTH, cell_width, b->cell_width);
    RENDER_LIMIT(RENDER_FIELD_CELL_HEIGHT, cell_height, b->cell_height);
    RENDER_LIMIT(RENDER_FIELD_SHEET, sheet_slots, b->sheet.count);
    RENDER_LIMIT(RENDER_FIELD_RUNTIME, runtime_slots, b->runtime.count);
    RENDER_ADMIT(RENDER_FIELD_WIDTH, b->client_width, (size_t)INT32_MAX, (size_t)INT32_MAX);
    RENDER_ADMIT(RENDER_FIELD_HEIGHT, b->client_height, (size_t)INT32_MAX, (size_t)INT32_MAX);
    if ((uint64_t)b->cols * b->cell_width > b->client_width ||
        (uint64_t)b->rows * b->cell_height > b->client_height)
        return render_reason(need, RENDER_FIELD_BINDING, RENDER_INVALID, 0, 0);
    size_t cells, target, tile, slots, atlas, pitch;
    if (!render_product(b->client_width, sizeof(uint32_t), &pitch))
        return render_reason(need, RENDER_FIELD_TARGET, RENDER_CAPACITY, SIZE_MAX, b->target_stride);
    if (b->target_stride < pitch)
        return render_reason(need, RENDER_FIELD_TARGET, RENDER_CAPACITY, pitch, b->target_stride);
    if (!render_product(b->cols, (size_t)b->rows + b->overscan, &cells) ||
        cells > SIZE_MAX / sizeof(render_cell))
        return render_reason(need, RENDER_FIELD_CELLS, RENDER_CAPACITY, SIZE_MAX, l->cells);
    if (!render_product(b->client_height, b->target_stride, &target))
        return render_reason(need, RENDER_FIELD_TARGET, RENDER_CAPACITY, SIZE_MAX, l->target_bytes);
    if (!render_product((size_t)b->cell_width * RENDER_TILE_MAX_SPAN, b->cell_height, &tile) ||
        __builtin_add_overflow((size_t)b->sheet.count, b->runtime.count, &slots) ||
        !render_product(slots, tile, &atlas))
        return render_reason(need, RENDER_FIELD_ATLAS, RENDER_CAPACITY, SIZE_MAX, l->atlas_bytes);
    RENDER_LIMIT(RENDER_FIELD_CELLS, cells, cells);
    RENDER_LIMIT(RENDER_FIELD_TARGET, target_bytes, target);
    RENDER_LIMIT(RENDER_FIELD_ATLAS, atlas_bytes, atlas);
    RENDER_ADMIT(RENDER_FIELD_REGION_WIDTH, l->region_width, l->region_width, caps->limits.region_width);
    RENDER_ADMIT(RENDER_FIELD_REGION_HEIGHT, l->region_height, l->region_height, caps->limits.region_height);
    RENDER_ADMIT(RENDER_FIELD_REGION_PIXELS, l->region_pixels, l->region_pixels, caps->limits.region_pixels);
    RENDER_ADMIT(RENDER_FIELD_TILE_DESCRIPTORS, l->tile_descriptors, l->tile_descriptors,
                 caps->limits.tile_descriptors);
    RENDER_ADMIT(RENDER_FIELD_TILE_BYTES, l->tile_bytes, l->tile_bytes, caps->limits.tile_bytes);
    if (b->runtime.count) {
        RENDER_LIMIT(RENDER_FIELD_TILE_DESCRIPTORS, tile_descriptors, 1u);
        RENDER_LIMIT(RENDER_FIELD_TILE_BYTES, tile_bytes, tile);
    }
#undef RENDER_LIMIT
#undef RENDER_ADMIT
    return RENDER_OK;
}

static render_rc render_drive_open(render *r)
{
    render_rc rc = r->setup.driver.ops->open(&r->driver, r->arena, &r->setup);
    r->progress = rc;
    if (rc == RENDER_OK) r->stats.state = RENDER_UNBOUND;
    else if (rc != RENDER_MORE && rc != RENDER_WAIT) r->stats.state = RENDER_CLOSING;
    return rc;
}

render_rc render_open(base_arena *arena, const render_setup *setup, render **out)
{
    if (!arena || !setup || !out || !setup->driver.ops) return RENDER_INVALID;
    const render_driver_ops *ops = setup->driver.ops;
    const render_limits *l = &setup->limits;
    if (!ops->query_caps || !ops->open || !ops->bind || !ops->consume || !ops->poll ||
        !ops->quiesce || !ops->close || !l->cells || !l->rows || !l->sheet_slots ||
        (l->runtime_slots && (!setup->directory || !setup->recovery_slots)))
        return RENDER_INVALID;
    base_arena_mark_t mark = base_arena_mark(arena);
    render *r = RENDER_STORAGE(arena, 1, render);
    if (!r) return RENDER_CAPACITY;
    r->arena = arena;
    r->mark = mark;
    r->setup = *setup;
    r->caps = ops->query_caps(setup);
    r->buffers.cells = RENDER_STORAGE(arena, l->cells, render_cell);
    r->buffers.runs = RENDER_STORAGE(arena, l->rows / 2u + l->rows % 2u, render_strip);
    r->buffers.region_rgb = RENDER_STORAGE(arena, l->region_pixels, uint32_t);
    r->buffers.deltas = RENDER_STORAGE(arena, l->tile_descriptors, render_delta);
    r->buffers.r8 = RENDER_STORAGE(arena, l->tile_bytes, uint8_t);
    r->sheet_versions = RENDER_STORAGE(arena, l->sheet_slots, uint32_t);
    r->delivery.nodes = RENDER_STORAGE(arena, l->runtime_slots, render_node);
    if (!r->buffers.cells || !r->buffers.runs || !r->sheet_versions ||
        (l->region_pixels && !r->buffers.region_rgb) || (l->tile_descriptors && !r->buffers.deltas) ||
        (l->tile_bytes && !r->buffers.r8) || (l->runtime_slots && !r->delivery.nodes)) {
        base_arena_reset_to_mark(arena, mark);
        return RENDER_CAPACITY;
    }
    r->packet = (render_packet){.runs = r->buffers.runs, .cells = r->buffers.cells,
        .region_rgb = r->buffers.region_rgb, .deltas = r->buffers.deltas, .r8 = r->buffers.r8};
    atomic_init(&r->command, NULL);
    r->stats.state = RENDER_OPENING;
    render_rc rc = render_drive_open(r);
    if (rc == RENDER_OK || rc == RENDER_MORE || rc == RENDER_WAIT) *out = r;
    else base_arena_reset_to_mark(arena, mark);
    return rc;
}

static bool render_sheet_valid(const render_binding *b, const render_sheet *s)
{
    if (!s || !s->tiles || s->count != b->sheet.count) return false;
    for (size_t i = 0; i < s->count; ++i) {
        const render_tile_view *v = &s->tiles[i];
        size_t bytes;
        if (!v->image.version ||
            !render_tile_size(b, v, (render_image_id){b->sheet.first + (uint32_t)i, v->image.version}, &bytes))
            return false;
    }
    for (uint32_t i = 0; i < RENDER_TILE_MAX_SPAN; ++i) {
        render_image_id marker = s->marker[i];
        if (!render_has_slot(b->sheet, marker.slot)) return false;
        const render_tile_view *v = &s->tiles[marker.slot - b->sheet.first];
        if (v->span != i + 1u || v->image.version != marker.version) return false;
    }
    return true;
}

static void render_clear_delivery(render *r)
{
    r->delivery = (render_delivery){.nodes = r->delivery.nodes, .head = RENDER_LIST_END,
                                    .tail = RENDER_LIST_END};
}

render_rc render_bind(render *r, const render_binding *b, const render_sheet *s, render_need *need)
{
    if (!r) return RENDER_INVALID;
    switch (r->stats.state) {
    case RENDER_UNBOUND:
    case RENDER_IDLE_FULL:
    case RENDER_IDLE:
        r->bind_previous = r->stats.state;
        break;
    case RENDER_BINDING:
        break;
    case RENDER_CLOSED:
    case RENDER_OPENING:
    case RENDER_ACCEPTED:
    case RENDER_ISSUED:
    case RENDER_QUIESCING:
    case RENDER_CLOSING:
        return RENDER_BUSY;
    }
    render_rc rc = render_measure(b, &r->setup.limits, &r->caps, need);
    if (rc == RENDER_WAIT) {
        r->stats.state = RENDER_BINDING;
        r->progress = RENDER_WAIT;
    }
    if (rc != RENDER_OK) return rc;
    if (r->epoch == UINT64_MAX)
        return render_reason(need, RENDER_FIELD_EPOCH, RENDER_CAPACITY, SIZE_MAX, SIZE_MAX);
    if (!render_sheet_valid(b, s))
        return render_reason(need, RENDER_FIELD_SHEET, RENDER_INVALID, 0, 0);
    rc = r->setup.driver.ops->bind(r->driver, b, s);
    r->progress = rc;
    if (rc == RENDER_MORE || rc == RENDER_WAIT) r->stats.state = RENDER_BINDING;
    else if (rc != RENDER_OK) r->stats.state = r->bind_previous;
    if (rc != RENDER_OK) return rc;

    r->binding = *b;
    ++r->epoch;
    for (size_t i = 0; i < s->count; ++i) r->sheet_versions[i] = s->tiles[i].image.version;
    render_clear_delivery(r);
    r->stats = (render_counters){.state = RENDER_IDLE_FULL};
    r->region = (render_rect){0};
    r->scroll = (render_scroll){.overscan_row = RENDER_NO_ROW};
    return RENDER_OK;
}

static void render_unlink(render_delivery *q, render_node *n)
{
    if (n->prev == RENDER_LIST_END) q->head = n->next;
    else q->nodes[n->prev].next = n->next;
    if (n->next == RENDER_LIST_END) q->tail = n->prev;
    else q->nodes[n->next].prev = n->prev;
    n->ordinal = 0;
    --q->count;
}

static void render_update_slot(render *r, uint32_t slot)
{
    render_delivery *q = &r->delivery;
    uint32_t index = slot - r->binding.runtime.first;
    render_node *n = &q->nodes[index];
    bool current = n->epoch == r->epoch;
    bool linked = current && n->ordinal > q->watermark;
    render_image_id id = {0};
    render_image_state state = r->setup.directory(r->setup.directory_ctx, r->binding.font, slot, &id);
    switch (state) {
    case RENDER_IMAGE_VACANT:
    case RENDER_IMAGE_QUEUED:
    case RENDER_IMAGE_EMPTY:
        if (linked) render_unlink(q, n);
        if (current) n->version = 0;
        return;
    case RENDER_IMAGE_INK:
        break;
    }
    if (id.slot != slot || !id.version || (current && n->version == id.version)) return;
    if (linked) {
        n->version = id.version;
        return;
    }
    if (q->ordinal == UINT64_MAX) return;
    *n = (render_node){r->epoch, ++q->ordinal, q->tail, RENDER_LIST_END, id.version};
    if (q->tail == RENDER_LIST_END) q->head = index;
    else q->nodes[q->tail].next = index;
    q->tail = index;
    ++q->count;
}

void render_image_changed(render *r, render_generation gen, uint32_t slot)
{
    if (!r || !render_same_generation(gen, r->binding.font) ||
        !render_has_slot(r->binding.runtime, slot)) return;
    switch (r->stats.state) {
    case RENDER_IDLE_FULL:
    case RENDER_IDLE:
    case RENDER_ACCEPTED:
    case RENDER_ISSUED:
        render_update_slot(r, slot);
        break;
    case RENDER_BINDING:
        if (r->bind_previous != RENDER_UNBOUND) render_update_slot(r, slot);
        break;
    case RENDER_CLOSED:
    case RENDER_OPENING:
    case RENDER_UNBOUND:
    case RENDER_QUIESCING:
    case RENDER_CLOSING:
        break;
    }
}

static render_rc render_recover(render *r)
{
    render_delivery *q = &r->delivery;
    const render_slot_range slots = r->binding.runtime;
    size_t grant = r->setup.recovery_slots;
    while (q->cursor < slots.count && grant--) {
        render_update_slot(r, slots.first + q->cursor++);
        ++r->stats.recovery_inspections;
    }
    return q->cursor < slots.count ? RENDER_MORE : RENDER_OK;
}

static void render_credit(render_delivery *q, uint32_t endpoint, size_t count)
{
    if (!count) return;
    const render_node *n = &q->nodes[endpoint];
    q->head = n->next;
    q->watermark = n->ordinal;
    q->count -= (uint32_t)count;
    if (q->head == RENDER_LIST_END) q->tail = RENDER_LIST_END;
    else q->nodes[q->head].prev = RENDER_LIST_END;
}

static bool render_exhausted(const render *r)
{
    return r->stats.accepted.frame == UINT32_MAX || r->delivery.ordinal == UINT64_MAX;
}

render_rc render_submit(render *r, const render_offer *o, const render_source *src,
                        render_frame_id *out, render_need *need)
{
    if (need) *need = (render_need){0};
    if (!r) return RENDER_INVALID;
    switch (r->stats.state) {
    case RENDER_IDLE_FULL:
    case RENDER_IDLE:
        break;
    case RENDER_OPENING:
    case RENDER_BINDING:
    case RENDER_ACCEPTED:
    case RENDER_ISSUED:
        return RENDER_BUSY;
    case RENDER_UNBOUND:
        return RENDER_WAIT;
    case RENDER_QUIESCING:
        return RENDER_LOST;
    case RENDER_CLOSED:
    case RENDER_CLOSING:
        return RENDER_INVALID;
    }
    if (render_exhausted(r)) return render_reason(need, RENDER_FIELD_EPOCH, RENDER_LOST, 0, 0);
    if (!o || !src || !out || !src->current || !src->get_run || !src->get_row ||
        !src->get_region || !src->get_identity || !src->get_tile)
        return RENDER_INVALID;

    uint64_t epoch = r->epoch;
    render_stage stage = {0};
    render_rc rc = render_prepare(r, o, src, &stage, need);
    if (rc != RENDER_OK) return rc;
    render_packet *p = &r->packet;
    if (epoch != r->epoch || !render_same_stamp(p->offer.stamp, o->stamp) ||
        !render_stamp_current(r, &p->offer, src))
        return render_reason(need, RENDER_FIELD_STAMP, RENDER_STALE, 0, 0);

    p->id = (render_frame_id){r->epoch, r->stats.accepted.frame + 1u};
    p->binding = &r->binding;
    render_credit(&r->delivery, stage.endpoint, stage.tiles);
    r->region = p->region;
    r->scroll = p->offer.scroll;
    r->background = p->offer.background;
    r->device = RENDER_WAIT_FENCE;
    r->presentation = RENDER_WAIT_DISPLAY;
    r->lease = RENDER_LEASE_LIVE;
    r->stats = (render_counters){
        .state = RENDER_ACCEPTED, .accepted = p->id, .stamp = p->offer.stamp,
        .recovery_inspections = r->stats.recovery_inspections,
        .rows = stage.rows, .runs = stage.runs, .cells = stage.cells,
        .region_pixels = stage.region_pixels, .tile_inspections = stage.inspections,
        .tiles = stage.tiles, .tile_bytes = stage.bytes
    };
    r->progress = RENDER_MORE;
    atomic_store_explicit(&r->command, p, memory_order_release);
    *out = p->id;
    return RENDER_OK;
}

static render_rc render_quiesce(render *r)
{
    r->progress = r->setup.driver.ops->quiesce(r->driver);
    return r->progress;
}

static render_rc render_lose(render *r)
{
    r->stats.state = RENDER_QUIESCING;
    r->progress = RENDER_MORE;
    atomic_store_explicit(&r->command, NULL, memory_order_release);
    return RENDER_LOST;
}

static void render_publish_evidence(render *r)
{
    render_counters *c = &r->stats;
    if (c->state != RENDER_ISSUED) return;
    switch (r->device) {
    case RENDER_WAIT_FENCE:
        break;
    case RENDER_DEVICE_DONE:
        if (c->device == RENDER_WAIT_FENCE) {
            c->device = RENDER_DEVICE_DONE;
            c->device_clock = r->fence.clock;
            c->t5 = r->fence.ns > c->t4 ? r->fence.ns : c->t4;
            if (c->device_clock == RENDER_MONOTONIC)
                trace_record_at(c->t5, TRACE_T5_DEVICE_DONE, c->accepted.frame);
        }
        break;
    }
    switch (r->presentation) {
    case RENDER_WAIT_DISPLAY:
        break;
    case RENDER_PRESENTED:
        if (c->presentation == RENDER_WAIT_DISPLAY) {
            c->presentation = RENDER_PRESENTED;
            c->presentation_clock = r->display.clock;
            if (r->display.clock != RENDER_UNTIMED)
                c->t6 = r->display.ns > c->t4 ? r->display.ns : c->t4;
            if (c->presentation_clock == RENDER_MONOTONIC)
                trace_record_at(c->t6, TRACE_T6_PRESENT_COMPLETE, c->accepted.frame);
        }
        break;
    case RENDER_DISCARDED:
        c->presentation = RENDER_DISCARDED;
        break;
    }
    if (c->device == RENDER_DEVICE_DONE && c->presentation != RENDER_WAIT_DISPLAY &&
        r->lease == RENDER_LEASE_RELEASED)
        c->state = RENDER_IDLE;
}

static bool render_notice_timed(const render *r, const render_notice *n)
{
    return n->ns && n->clock == (r->caps.synthetic ? RENDER_SYNTHETIC : RENDER_MONOTONIC);
}

static render_rc render_evidence_event(render *r, const render_notice *n)
{
    render_counters *c = &r->stats;
    if (n->kind > RENDER_NOTICE_LOST) return RENDER_INVALID;
    if ((c->state != RENDER_ACCEPTED && c->state != RENDER_ISSUED) ||
        !render_same_frame(n->id, c->accepted)) return RENDER_STALE;
    switch (n->kind) {
    case RENDER_PUMP:
        return RENDER_INVALID;
    case RENDER_NOTICE_LOST:
        return render_lose(r);
    case RENDER_NOTICE_ISSUED:
        if (c->state != RENDER_ACCEPTED ||
            atomic_load_explicit(&r->command, memory_order_acquire)) return RENDER_STALE;
        if (!render_notice_timed(r, n)) return RENDER_INVALID;
        c->state = RENDER_ISSUED;
        c->issue_clock = n->clock;
        c->t4 = n->ns;
        c->draw = n->draw;
        if (c->issue_clock == RENDER_MONOTONIC)
            trace_record_at(c->t4, TRACE_T4_PRESENT_SUBMITTED, c->accepted.frame);
        break;
    case RENDER_NOTICE_DEVICE_DONE:
        if (r->device != RENDER_WAIT_FENCE) return RENDER_STALE;
        if (!render_notice_timed(r, n)) return RENDER_INVALID;
        r->fence = (render_evidence){n->ns, n->clock};
        r->device = RENDER_DEVICE_DONE;
        break;
    case RENDER_NOTICE_PRESENTED:
        if (r->presentation != RENDER_WAIT_DISPLAY) return RENDER_STALE;
        if (!render_notice_timed(r, n) && (n->clock != RENDER_UNTIMED || n->ns))
            return RENDER_INVALID;
        r->display = (render_evidence){n->ns, n->clock};
        r->presentation = RENDER_PRESENTED;
        break;
    case RENDER_NOTICE_DISCARDED:
        if (r->presentation != RENDER_WAIT_DISPLAY) return RENDER_STALE;
        r->presentation = RENDER_DISCARDED;
        break;
    case RENDER_NOTICE_RELEASED:
        if (r->lease == RENDER_LEASE_RELEASED ||
            atomic_load_explicit(&r->command, memory_order_acquire)) return RENDER_STALE;
        r->lease = RENDER_LEASE_RELEASED;
        break;
    }
    render_publish_evidence(r);
    return RENDER_OK;
}

static render_rc render_pump_flight(render *r)
{
    const render_driver_ops *ops = r->setup.driver.ops;
    const render_packet *p = atomic_load_explicit(&r->command, memory_order_acquire);
    if (p) {
        render_rc rc = ops->consume(r->driver, p);
        if (rc == RENDER_OK) {
            atomic_store_explicit(&r->command, NULL, memory_order_release);
            return RENDER_MORE;
        }
        if (rc != RENDER_MORE && rc != RENDER_WAIT) return render_lose(r);
        return rc;
    }
    render_notice n = {0};
    render_rc rc = ops->poll(r->driver, &n);
    if (rc == RENDER_MORE || rc == RENDER_WAIT) return rc;
    if (rc != RENDER_OK) return render_lose(r);
    rc = render_evidence_event(r, &n);
    if (rc == RENDER_LOST) return rc;
    return r->stats.state == RENDER_IDLE ? RENDER_OK : RENDER_MORE;
}

render_rc render_event(render *r, const render_notice *notice)
{
    if (!r || !notice) return RENDER_INVALID;
    if (notice->kind != RENDER_PUMP) return render_evidence_event(r, notice);
    switch (r->stats.state) {
    case RENDER_OPENING:
        return render_drive_open(r);
    case RENDER_IDLE_FULL:
    case RENDER_IDLE:
        if (render_exhausted(r)) return render_lose(r);
        return render_recover(r);
    case RENDER_ACCEPTED:
    case RENDER_ISSUED:
        r->progress = render_pump_flight(r);
        if (r->progress == RENDER_LOST) r->progress = RENDER_MORE;
        return r->stats.state == RENDER_QUIESCING ? RENDER_LOST : r->progress;
    case RENDER_QUIESCING:
        if (render_quiesce(r) != RENDER_OK) return r->progress;
        r->stats.state = RENDER_UNBOUND;
        r->stats.draw = (render_draw_report){0};
        render_clear_delivery(r);
        return RENDER_OK;
    case RENDER_BINDING:
    case RENDER_UNBOUND:
    case RENDER_CLOSING:
        return RENDER_WAIT;
    case RENDER_CLOSED:
        return RENDER_INVALID;
    }
    return RENDER_INVALID;
}

bool render_pending(const render *r, render_wake *out)
{
    render_wake w = {0};
    if (r) {
        w.wake = r->setup.wake;
        switch (r->stats.state) {
        case RENDER_IDLE_FULL:
        case RENDER_IDLE:
            w.recovery = r->delivery.cursor < r->binding.runtime.count;
            w.delivery = r->delivery.count != 0;
            w.runnable = w.recovery || w.delivery || render_exhausted(r);
            break;
        case RENDER_OPENING:
        case RENDER_BINDING:
        case RENDER_ACCEPTED:
        case RENDER_ISSUED:
        case RENDER_QUIESCING:
        case RENDER_CLOSING:
            w.waiting = r->progress == RENDER_WAIT;
            w.runnable = !w.waiting;
            break;
        case RENDER_UNBOUND:
        case RENDER_CLOSED:
            break;
        }
    }
    if (out) *out = w;
    return w.runnable || w.waiting;
}

void render_stats(const render *r, render_counters *out)
{
    if (!out) return;
    *out = r ? r->stats : (render_counters){0};
    if (r) {
        out->queued = r->delivery.count;
        out->watermark = r->delivery.watermark;
    }
}

render_rc render_close(render *r)
{
    if (!r) return RENDER_OK;
    r->stats.state = RENDER_CLOSING;
    atomic_store_explicit(&r->command, NULL, memory_order_release);
    if (r->driver) {
        render_rc rc = render_quiesce(r);
        if (rc != RENDER_OK) return rc;
        r->setup.driver.ops->close(r->driver);
    }
    base_arena_reset_to_mark(r->arena, r->mark);
    return RENDER_OK;
}
