#include "render/render_submit_prep.h"
#include "render/render_scene.h"
#include "trace/trace.h"
#ifdef RENDER_TEST_HOOKS
#include "render/render_test.h"
#endif

render_rc render_measure(const render_binding *b, const render_limits *l,
                         const render_backend_caps *caps, render_need *need)
{
    render_need local;
    if (!need) need = &local;
    *need = (render_need){0};
    if (!b || !l || !caps)
        return render_reason(need, RENDER_FIELD_BINDING, RENDER_INVALID, 0, 0);
    if (!b->client_width || !b->client_height) return RENDER_WAIT;
    if (!b->surface || !b->geometry || !b->font.owner || !b->font.generation ||
        !b->cols || !b->rows || !b->cell_width || !b->cell_height || b->overscan > 1 ||
        b->rows == RENDER_NO_ROW || b->sheet.count < RENDER_ASCII_COUNT + RENDER_TILE_MAX_SPAN ||
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
    if (__builtin_mul_overflow((size_t)b->client_width, sizeof(uint32_t), &pitch))
        return render_reason(need, RENDER_FIELD_TARGET, RENDER_CAPACITY, SIZE_MAX, b->target_stride);
    if (b->target_stride < pitch)
        return render_reason(need, RENDER_FIELD_TARGET, RENDER_CAPACITY, pitch, b->target_stride);
    if (__builtin_mul_overflow((size_t)b->cols, (size_t)b->rows + b->overscan, &cells) ||
        cells > SIZE_MAX / sizeof(render_cell))
        return render_reason(need, RENDER_FIELD_CELLS, RENDER_CAPACITY, SIZE_MAX, l->cells);
    if (__builtin_mul_overflow((size_t)b->client_height, b->target_stride, &target))
        return render_reason(need, RENDER_FIELD_TARGET, RENDER_CAPACITY, SIZE_MAX, l->target_bytes);
    if (__builtin_mul_overflow((size_t)b->cell_width * RENDER_TILE_MAX_SPAN, b->cell_height, &tile) ||
        __builtin_add_overflow((size_t)b->sheet.count, b->runtime.count, &slots) ||
        __builtin_mul_overflow(slots, tile, &atlas))
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

static bool render_reserve(render *r)
{
    base_arena *a = r->arena;
    const render_limits *l = &r->setup.limits;
    size_t slots, rows, sheet_bytes, tile;
    if (__builtin_add_overflow(l->sheet_slots, l->runtime_slots, &slots) ||
        __builtin_add_overflow((size_t)l->rows, 1u, &rows)) return false;
#define RENDER_RESERVE(member, count, type) do { \
    size_t n = (count); \
    if (n && !(r->member = RENDER_STORAGE(a, n, type))) return false; \
} while (0)
    RENDER_RESERVE(packet.cells, l->cells, render_cell);
    RENDER_RESERVE(packet.runs, rows / 2u, render_strip);
    RENDER_RESERVE(packet.region_rgb, l->region_pixels, uint32_t);
    RENDER_RESERVE(packet.deltas, l->tile_descriptors, render_delta);
    RENDER_RESERVE(packet.r8, l->tile_bytes, uint8_t);
    RENDER_RESERVE(scene.view.cells, l->cells, render_cell);
    RENDER_RESERVE(scene.view.row_frames, rows, uint32_t);
    RENDER_RESERVE(scene.view.region_rgb, l->region_pixels, uint32_t);
    RENDER_RESERVE(scene.view.installed, slots, render_resident);
    RENDER_RESERVE(sheet.tiles, l->sheet_slots, render_tile_view);
    if (__builtin_mul_overflow((size_t)l->cell_width, l->cell_height, &tile) ||
        __builtin_mul_overflow(tile, RENDER_TILE_MAX_SPAN, &tile) ||
        __builtin_mul_overflow(tile, l->sheet_slots, &sheet_bytes) || sheet_bytes > l->atlas_bytes)
        sheet_bytes = l->atlas_bytes;
    RENDER_RESERVE(sheet_bytes, sheet_bytes, uint8_t);
    RENDER_RESERVE(active.nodes, l->runtime_slots, render_node);
    RENDER_RESERVE(staged.nodes, l->runtime_slots, render_node);
#undef RENDER_RESERVE
    return true;
}

render_rc render_open(base_arena *a, const render_setup *setup, render **out)
{
    if (!a || !setup || !out || !setup->wake || !setup->driver.ops) return RENDER_INVALID;
    render *r = *out;
    if (!r) {
        const render_driver_ops *ops = setup->driver.ops;
        const render_limits *l = &setup->limits;
        if (!ops->query_caps || !ops->open || !ops->bind || !ops->consume || !ops->poll ||
            !ops->quiesce || !ops->close || !l->cells || !l->rows || !l->atlas_bytes ||
            l->sheet_slots < RENDER_ASCII_COUNT + RENDER_TILE_MAX_SPAN ||
            (l->runtime_slots && (!setup->directory || !setup->recovery_slots))) return RENDER_INVALID;
        base_arena_mark_t mark = base_arena_mark(a);
        r = RENDER_STORAGE(a, 1, render);
        if (!r) return RENDER_CAPACITY;
        r->arena = a;
        r->mark = mark;
        r->setup = *setup;
        r->caps = ops->query_caps(setup);
        if (!render_reserve(r) || os_wake_init(&r->command_wake) != OS_OK) {
            base_arena_reset_to_mark(a, mark);
            return RENDER_CAPACITY;
        }
        atomic_init(&r->port.command, RENDER_COMMAND_EMPTY);
        r->port.packet = &r->packet;
        r->port.wake = &r->command_wake;
        r->port.ui_wake = setup->wake;
        r->port.scene = &r->scene;
        r->port.apply = render_apply_scene;
        r->stats.state = RENDER_OPENING;
    } else if (r->stats.state != RENDER_OPENING || r->arena != a) return RENDER_INVALID;

    render_rc rc = r->setup.driver.ops->open(&r->driver, a, &r->setup, &r->port);
    r->progress = rc;
    if (rc == RENDER_OK) r->stats.state = RENDER_UNBOUND;
    if (rc != RENDER_OK && rc != RENDER_MORE && rc != RENDER_WAIT) {
        os_wake_close(&r->command_wake);
        base_arena_reset_to_mark(a, r->mark);
        r = NULL;
    }
    *out = r;
    return rc;
}

static render_rc render_quiesce(render *r, render_quiescence mode)
{
    return r->progress = r->setup.driver.ops->quiesce(r->driver, mode);
}

/* UI calls drive a physical wait after its owner wake. No wake is evidence. */
static render_rc render_idle(render *r)
{
    switch (r->stats.state) {
    case RENDER_RETIRING:
        if (render_quiesce(r, RENDER_RETIRE_FRAME) != RENDER_OK) return r->progress;
        if (r->stats.accepted.frame != UINT32_MAX) {
            r->stats.state = RENDER_IDLE;
            return RENDER_OK;
        }
        r->stats.state = RENDER_QUIESCING;
        /* fall through */
    case RENDER_QUIESCING:
        if (render_quiesce(r, RENDER_STOP_EPOCH) != RENDER_OK) return r->progress;
        r->active.epoch = r->staged.epoch = 0;
        r->stats.state = RENDER_UNBOUND;
        return RENDER_OK;
    case RENDER_UNBOUND:
    case RENDER_IDLE_FULL:
    case RENDER_IDLE:
        return RENDER_OK;
    case RENDER_OPENING:
    case RENDER_ACCEPTED:
    case RENDER_ISSUED:
        return RENDER_BUSY;
    case RENDER_CLOSED:
    case RENDER_CLOSING:
        return RENDER_INVALID;
    }
    return RENDER_INVALID;
}

static bool render_sheet_valid(const render_binding *b, const render_sheet *s)
{
    if (!s || !s->tiles || s->count != b->sheet.count || s->ascii_count != RENDER_ASCII_COUNT) return false;
    for (size_t i = 0; i < s->count; ++i) {
        const render_tile_view *v = &s->tiles[i];
        size_t bytes;
        if (!v->image.version || (i < s->ascii_count && v->span != 1) ||
            !render_tile_extent(b, v, &bytes) ||
            !render_tile_page(v, (render_image_id){b->sheet.first + (uint32_t)i, v->image.version}))
            return false;
    }
    for (uint32_t i = 0; i < RENDER_TILE_MAX_SPAN; ++i) {
        render_image_id m = s->marker[i];
        if (!render_has_slot(b->sheet, m.slot) || m.slot - b->sheet.first < s->ascii_count) return false;
        const render_tile_view *v = &s->tiles[m.slot - b->sheet.first];
        if (v->span != i + 1u || v->image.version != m.version) return false;
    }
    return true;
}

render_rc render_bind(render *r, const render_binding *b, const render_sheet *s, render_need *need)
{
    if (!r) return RENDER_INVALID;
    render_rc rc = render_idle(r);
    if (rc != RENDER_OK) return rc;
    render_need local;
    if (!need) need = &local;
    rc = render_measure(b, &r->setup.limits, &r->caps, need);
    if (rc == RENDER_WAIT) r->suspended = true;
    if (rc != RENDER_OK) return rc;
    if (r->next_epoch == UINT64_MAX || r->stats.accepted.frame == UINT32_MAX)
        return RENDER_REFUSE(need, RENDER_FIELD_EPOCH, RENDER_CAPACITY);
    if (!render_sheet_valid(b, s)) return RENDER_REFUSE(need, RENDER_FIELD_SHEET, RENDER_INVALID);

    render_tile_view *views = RENDER_WRITE(render_tile_view, r->sheet.tiles);
    uint8_t *dst = r->sheet_bytes;
    for (size_t i = 0; i < s->count; ++i) {
        const render_tile_view *v = &s->tiles[i];
        uint32_t w = v->rect.width, h = v->rect.height;
        size_t bytes = (size_t)w * h;
        render_copy_tile(dst, v);
        views[i] = (render_tile_view){v->image, dst, bytes, w, w, h, v->span, {0, 0, w, h}};
        dst += bytes;
    }
    r->sheet = *s;
    r->sheet.tiles = views;
    r->staged = (render_bank){.binding = *b, .nodes = r->staged.nodes, .epoch = ++r->next_epoch,
                              .head = RENDER_LIST_END, .tail = RENDER_LIST_END};
    r->suspended = false;
    if (!r->active.epoch) r->stats.state = RENDER_IDLE_FULL;
    return RENDER_OK;
}

static void render_update_slot(const render_setup *s, render_bank *q, uint32_t slot)
{
    uint32_t index = slot - q->binding.runtime.first;
    render_node *n = &q->nodes[index];
    bool current = n->epoch == q->epoch, linked = current && n->ordinal > q->watermark;
    render_image_id id = {0};
    switch (s->directory(s->directory_ctx, q->binding.font, slot, &id)) {
    case RENDER_IMAGE_VACANT:
    case RENDER_IMAGE_QUEUED:
    case RENDER_IMAGE_EMPTY:
        if (linked) {
            if (n->prev == RENDER_LIST_END) q->head = n->next;
            else q->nodes[n->prev].next = n->next;
            if (n->next == RENDER_LIST_END) q->tail = n->prev;
            else q->nodes[n->next].prev = n->prev;
            n->ordinal = 0;
            --q->count;
        }
        if (current) n->version = 0;
        return;
    case RENDER_IMAGE_INK:
        break;
    }
    if (id.slot != slot || !id.version || (current && n->version == id.version)) return;
    if (linked) n->version = id.version;
    else if (q->ordinal != UINT64_MAX) {
        *n = (render_node){q->epoch, ++q->ordinal, q->tail, RENDER_LIST_END, id.version};
        if (q->tail == RENDER_LIST_END) q->head = index;
        else q->nodes[q->tail].next = index;
        q->tail = index;
        ++q->count;
    }
}

void render_image_changed(render *r, render_generation gen, uint32_t slot)
{
    if (!r || r->stats.state == RENDER_QUIESCING || r->stats.state == RENDER_CLOSING) return;
    render_bank *banks[] = {&r->active, &r->staged};
    for (size_t i = 0; i < sizeof banks / sizeof *banks; ++i) {
        render_bank *b = banks[i];
        if (b->epoch && render_same_generation(gen, b->binding.font) && render_has_slot(b->binding.runtime, slot))
            render_update_slot(&r->setup, b, slot);
    }
}

static void render_credit(render_bank *q, render_stage stage)
{
    if (!stage.tiles) return;
    const render_node *n = &q->nodes[stage.endpoint];
    q->head = n->next;
    q->watermark = n->ordinal;
    q->count -= (uint32_t)stage.tiles;
    if (q->head == RENDER_LIST_END) q->tail = RENDER_LIST_END;
    else q->nodes[q->head].prev = RENDER_LIST_END;
}

static void render_retract_command(render_driver_port *port)
{
    render_command_state expected = RENDER_COMMAND_READY;
    (void)atomic_compare_exchange_strong_explicit(&port->command, &expected, RENDER_COMMAND_EMPTY,
                                                  memory_order_relaxed, memory_order_relaxed);
}

static render_rc render_lose(render *r)
{
    render_retract_command(&r->port);
    r->stats.state = RENDER_QUIESCING;
    (void)render_idle(r);
    return RENDER_LOST;
}

static render_rc render_reject(render *r, render_rc rc, render_need reason, render_need *out)
{
    r->stats.rejected = rc;
    r->stats.rejection = reason;
    if (out) *out = reason;
    return rc;
}

render_rc render_submit(render *r, const render_offer *o, const render_source *src,
                        render_frame_id *out, render_need *need)
{
    if (!r) return RENDER_INVALID;
    render_rc rc = render_idle(r);
    if (rc == RENDER_OK && (r->suspended || r->stats.state == RENDER_UNBOUND)) rc = RENDER_WAIT;
    if (rc != RENDER_OK) return render_reject(r, rc, (render_need){0}, need);
    if (!o || !out || !src || !src->current || !src->get_run || !src->get_row ||
        !src->get_region || !src->get_identity || !src->get_tile)
        return render_reject(r, RENDER_INVALID, (render_need){.field = RENDER_FIELD_BINDING}, need);

    bool adopt = o->mode == RENDER_FULL && render_matches(&r->staged, o->stamp);
    render_bank *bank = adopt ? &r->staged : &r->active;
    if (r->stats.accepted.frame == UINT32_MAX || bank->ordinal == UINT64_MAX)
        return render_reject(r, render_lose(r), (render_need){.field = RENDER_FIELD_EPOCH}, need);
    render_stage stage = {0};
    render_need reason = {0};
    rc = render_prepare(r, bank, o, src, &stage, &reason);
    if (rc != RENDER_OK) return render_reject(r, rc, reason, need);

    render_packet *p = &r->packet;
    p->id = (render_frame_id){bank->epoch, r->stats.accepted.frame + 1u};
    p->sheet = adopt ? &r->sheet : NULL;
    render_credit(bank, stage);
    render_slot_range slots = bank->binding.runtime;
    if (!o->stamp.input_first)
        for (size_t i = 0; i < r->setup.recovery_slots && bank->cursor < slots.count; ++i)
            render_update_slot(&r->setup, bank, slots.first + bank->cursor++);
    if (adopt) {
        render_node *spare = r->active.nodes;
        r->active = *bank;
        r->staged = (render_bank){.nodes = spare};
    }
    r->region = p->region;
    r->scroll = o->scroll;
    r->background = o->background;
    r->stats = (render_counters){.state = RENDER_ACCEPTED, .accepted = p->id, .stamp = p->offer.stamp,
        .recovery_inspections = r->active.cursor, .rows = stage.rows, .runs = stage.runs, .cells = stage.cells,
        .region_pixels = stage.region_pixels, .tile_inspections = stage.inspections,
        .tiles = stage.tiles, .tile_bytes = stage.bytes,
        .rejected = r->stats.rejected, .rejection = r->stats.rejection};
    atomic_store_explicit(&r->port.command, RENDER_COMMAND_READY, memory_order_release);
    os_wake_signal(&r->command_wake);
    if (need) *need = (render_need){0};
    *out = p->id;
    return RENDER_OK;
}

static void render_time(render_counters *c, uint64_t *time, render_clock clock, enum trace_ev which)
{
    if (!c->t4 || clock == RENDER_UNTIMED) return;
    if (*time < c->t4) *time = c->t4;
    if (clock == RENDER_MONOTONIC) trace_record_at(*time, which, c->accepted.frame);
}

render_rc render_event(render *r, const render_notice *n)
{
    if (!r || !n || n->kind > RENDER_NOTICE_LOST) return RENDER_INVALID;
    render_counters *c = &r->stats;
    bool flight = c->state == RENDER_ACCEPTED || c->state == RENDER_ISSUED || c->state == RENDER_RETIRING;
    if (n->kind == RENDER_NOTICE_LOST && !n->id.frame && r->active.epoch && n->id.epoch == r->active.epoch &&
        (flight || c->state == RENDER_IDLE)) return render_lose(r);
    if (!flight || !render_same_frame(n->id, c->accepted)) return RENDER_STALE;
    bool timed = n->ns && n->clock == (r->caps.synthetic ? RENDER_SYNTHETIC : RENDER_MONOTONIC);
    switch (n->kind) {
    case RENDER_NOTICE_LOST:
        return render_lose(r);
    case RENDER_NOTICE_ISSUED:
        if (c->state != RENDER_ACCEPTED ||
            atomic_load_explicit(&r->port.command, memory_order_acquire) == RENDER_COMMAND_READY)
            return RENDER_STALE;
        if (!timed) return RENDER_INVALID;
        c->state = RENDER_ISSUED;
        c->t4 = n->ns;
        c->issue_clock = n->clock;
        render_time(c, &c->t4, c->issue_clock, TRACE_T4_PRESENT_SUBMITTED);
        render_time(c, &c->t5, c->device_clock, TRACE_T5_DEVICE_DONE);
        render_time(c, &c->t6, c->presentation_clock, TRACE_T6_PRESENT_COMPLETE);
        break;
    case RENDER_NOTICE_DEVICE_DONE:
        if (c->device != RENDER_WAIT_FENCE) return RENDER_STALE;
        if (!timed) return RENDER_INVALID;
        c->device = RENDER_DEVICE_DONE;
        c->t5 = n->ns;
        c->device_clock = n->clock;
        render_time(c, &c->t5, c->device_clock, TRACE_T5_DEVICE_DONE);
        break;
    case RENDER_NOTICE_PRESENTED:
        if (c->presentation != RENDER_WAIT_DISPLAY) return RENDER_STALE;
        if (!timed && (n->clock != RENDER_UNTIMED || n->ns)) return RENDER_INVALID;
        c->presentation = RENDER_PRESENTED;
        c->t6 = n->ns;
        c->presentation_clock = n->clock;
        render_time(c, &c->t6, c->presentation_clock, TRACE_T6_PRESENT_COMPLETE);
        break;
    case RENDER_NOTICE_DISCARDED:
        if (c->presentation != RENDER_WAIT_DISPLAY) return RENDER_STALE;
        c->presentation = RENDER_DISCARDED;
        break;
    }
    if (c->state == RENDER_ISSUED && c->device == RENDER_DEVICE_DONE && c->presentation != RENDER_WAIT_DISPLAY) {
        c->state = RENDER_RETIRING;
        (void)render_idle(r);
    }
    return RENDER_OK;
}

bool render_pending(const render *r, render_wake *out)
{
    render_wake w = {0};
    if (r) {
        w.wake = r->setup.wake;
        w.feedback = r->port.feedback;
        switch (r->stats.state) {
        case RENDER_IDLE:
        case RENDER_IDLE_FULL:
            if (r->suspended) break;
            w.delivery = (r->active.epoch && r->active.count) || (r->staged.epoch && r->staged.count);
            w.recovery = (r->active.epoch && r->active.cursor < r->active.binding.runtime.count) ||
                (r->staged.epoch && r->staged.cursor < r->staged.binding.runtime.count);
            w.runnable = w.delivery || w.recovery || r->staged.epoch;
            break;
        case RENDER_ACCEPTED:
        case RENDER_ISSUED:
            w.waiting = true;
            break;
        case RENDER_OPENING:
        case RENDER_RETIRING:
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
    if (!r) return;
    const render_bank *b = r->active.epoch ? &r->active : &r->staged;
    if (b->epoch) {
        out->queued = b->count;
        out->watermark = b->watermark;
    }
    if (!out->t4) {
        out->device = RENDER_WAIT_FENCE;
        out->presentation = RENDER_WAIT_DISPLAY;
        out->device_clock = out->presentation_clock = RENDER_UNTIMED;
        out->t5 = out->t6 = 0;
    }
}

render_rc render_close(render *r)
{
    if (!r) return RENDER_OK;
    r->stats.state = RENDER_CLOSING;
    render_retract_command(&r->port);
    if (r->driver) {
        render_rc rc = render_quiesce(r, RENDER_STOP_EPOCH);
        if (rc != RENDER_OK) return rc;
        r->setup.driver.ops->close(r->driver);
    }
    os_wake_close(&r->command_wake);
    base_arena_reset_to_mark(r->arena, r->mark);
    return RENDER_OK;
}

#ifdef RENDER_TEST_HOOKS
render_test_owner render_test_owner_get(render *r)
{
    return (render_test_owner){r->setup.driver, r->driver, &r->port};
}
render_test_view render_test_inspect(render *r)
{
    render_scene *s = &r->scene;
    render_test_view v = {s->view, s->observed_binding, s->scan_cells, s->drawn_rows, s->scan_ns};
    if (!v.scene.region.width) v.scene.region_rgb = NULL;
    return v;
}
#endif
