/* edit-08z: production editor lookup, null backend, and actual atlas pixels. */
#include "editor/private.h"
#include "raster/raster.h"
#include "trace/trace.h"
#include <sched.h>
#include <stdio.h>
#include <string.h>

#define T(c) do { if (!(c)) { fprintf(stderr, "unicode_render_test:%d: FAIL %s\n", __LINE__, #c); return 1; } } while (0)

typedef struct glyph_gate {
    _Atomic bool started, release;
    bool armed, submitted;
    size_t allocations;
    editor *editor;
    bool watch_damage;
    uint64_t damage;
    size_t repaints;
    uint64_t sequence;
} glyph_gate;

static void block_bulk(work_ctx *ctx)
{
    glyph_gate *gate = ctx->arg;
    atomic_store(&gate->started, true);
    while (!atomic_load(&gate->release) && !work_should_stop(ctx)) sched_yield();
}

static void submitted(void *ctx, const editor_frame *frame)
{
    glyph_gate *gate = ctx;
    if (gate->watch_damage) {
        gate->damage |= editor_grid(gate->editor)->dirty[0];
        gate->repaints++;
    }
    if (!gate->armed || frame->last_sequence < gate->sequence) return;
    if (edit_malloc_guard_active()) gate->allocations = edit_malloc_guard_end();
    gate->armed = false; gate->submitted = true;
}

static int settle(editor *e)
{
    /* Completion-driven, no invented elapsed-time or iteration allowance. */
    for (;;) {
        int rc = editor_step(e, 0);
        T(rc == EDITOR_OK || rc == EDITOR_MORE);
        editor_stats stats = editor_get_stats(e);
        if (!stats.pending && !stats.render_active && !font_async_pending(e->fonts)) return 0;
        sched_yield();
    }
}

static int bitmap(const render_grid *g, const render_cell *cell)
{
    T(cell->atlas_slot != RENDER_NO_SLOT && cell->atlas_slot < g->glyph_count);
    const render_glyph *image = &g->glyphs[cell->atlas_slot];
    const render_glyph *question = &g->glyphs['?' - ' '];
    const render_atlas_page *page = &g->pages[image->page];
    const render_atlas_page *baked = &g->pages[question->page];
    bool ink = false, different = false;
    for (uint32_t y = 0; y < image->h; y++) for (uint32_t x = 0; x < image->w; x++) {
        uint8_t value = page->pixels[(size_t)(image->y + y) * page->stride + image->x + x];
        uint8_t fallback = x < question->w && y < question->h
            ? baked->pixels[(size_t)(question->y + y) * baked->stride + question->x + x] : 0;
        ink |= value != 0; different |= value != fallback;
    }
    T(ink);
    T(different);
    return 0;
}

static int combining(const render_grid *g, const render_cell *cell)
{
    const render_glyph *composed = &g->glyphs[cell->atlas_slot], *base = &g->glyphs['e' - ' '];
    const render_atlas_page *page = &g->pages[composed->page], *baked = &g->pages[base->page];
    bool mark = false;
    T(composed->w == base->w && composed->h == base->h);
    for (uint32_t y = 0; y < composed->h; y++) for (uint32_t x = 0; x < composed->w; x++) {
        uint8_t a = page->pixels[(size_t)(composed->y + y) * page->stride + composed->x + x];
        uint8_t b = baked->pixels[(size_t)(base->y + y) * baked->stride + base->x + x];
        T(a >= b); /* coverage union preserves every base pixel */
        mark |= a > b;
    }
    T(mark); /* the combining acute contributes ink within the same cell */
    return 0;
}

static int render_unicode(bool inject)
{
    static const char text[] = "\néα日🙂é";
    glyph_gate gate = {0};
    render_backend backend = {0}; T(render_null_backend(&backend) == RENDER_OK);
    /* Geometry derives from the test text's utf8 widths plus the gutter. */
    editor_config cfg = {.cols = (uint32_t)sizeof text,
        .rows = 3, /* (G) leading-LF text occupies two rows, plus the tab strip */
        .initial = inject ? (const uint8_t *)"\n" : (const uint8_t *)text,
        .initial_len = inject ? sizeof "\n" - 1u : sizeof text - 1u,
        .hook_ctx = &gate, .on_submit = submitted};
    editor *e = NULL; T(editor_open(&e, &cfg, &backend) == EDITOR_OK);
    gate.editor = e;
    T(settle(e) == 0);
    /* Chrome uses a callback with a resident-image contract. Its plain space
     * must remain blank when sharing the runtime lookup with text layout. */
    T(tabs_rename(&e->tabs, tabs_active_index(&e->tabs), "a b", sizeof "a b" - 1u, "", 0) == TABS_OK);
    T(editor_begin_frame(e) == EDITOR_OK); T(settle(e) == 0);
    T(editor_grid(e)->cells[sizeof "a"].glyph_index == ' '); /* title starts after its leading padding cell */
    if (inject) {
        T(editor_set_cursor(e, sizeof "\n" - 1u) == EDITOR_OK); T(settle(e) == 0);
        /* Hold the bulk lane through input->submit. The process-wide malloc
         * guard would otherwise count legitimate worker discovery/file I/O.
         * This also proves pending rasterisation cannot hold up submission. */
        work_handle blocker = work_submit(&e->pool, (work_job){block_bulk, &gate, 0, WORK_BULK});
        T(blocker.epoch);
        while (!atomic_load(&gate.started)) sched_yield();
        for (const char *at = text + sizeof "\n" - 1u; *at;) {
            size_t len = utf8_grapheme_next((const uint8_t *)at, strlen(at));
            T(len <= PLAT_UTF8_MAX);
            plat_event ev = {.kind = PLAT_EV_KEY, .press = true, .utf8_len = (uint8_t)len};
            memcpy(ev.utf8, at, len); at += len;
            gate.armed = true; gate.submitted = false;
            if (edit_malloc_guard_active()) edit_malloc_guard_begin();
            T(editor_inject(e, &ev) == EDITOR_OK);
            gate.sequence = e->event_serial;
            while (!gate.submitted) {
                int rc = editor_step(e, 0); T(rc == EDITOR_OK || rc == EDITOR_MORE);
            }
            T(gate.allocations == 0); /* (G) law: no heap calls from input through submit */
        }
        T(font_async_pending(e->fonts));
        uint64_t expected_damage = 0;
        for (uint32_t row = 0; row < e->grid.dims.rows; row++) for (uint32_t c = 0; c < e->grid.dims.cols; c++) {
            uint32_t slot = e->grid.cells[(size_t)row * e->grid.dims.cols + c].atlas_slot;
            if (slot >= LAYOUT_ASCII_GLYPHS && slot != RENDER_NO_SLOT) expected_damage |= UINT64_C(1) << row;
        }
        gate.watch_damage = true;
        atomic_store(&gate.release, true);
        T(settle(e) == 0);
        T(gate.repaints > 0);
        /* The independently owned stale minimap indicator can clear in the
         * tab strip when the held index job completes. Check text damage
         * separately; this bead deliberately leaves that overlay unchanged. */
        if (e->tab_rows) gate.damage &= ~UINT64_C(1);
        if (gate.damage != expected_damage)
            fprintf(stderr, "Unicode completion damage observed=%llx expected=%llx (M: union of submitted dirty-row bits)\n",
                    (unsigned long long)gate.damage, (unsigned long long)expected_damage);
        T(gate.damage == expected_damage); /* only rows holding the pending glyph cells */
    }
    const render_grid *g = editor_grid(e);
    uint32_t col = layout_gutter_width(&e->lay);
    const char *p = text + sizeof "\n" - 1u;
    uint32_t unicode_row = e->tab_rows;
    while (unicode_row < g->dims.rows) {
        uint32_t slot = g->cells[(size_t)unicode_row * g->dims.cols + col].atlas_slot;
        if (slot != RENDER_NO_SLOT && slot >= LAYOUT_ASCII_GLYPHS) break;
        unicode_row++;
    }
    T(unicode_row < g->dims.rows);
    while (*p) {
        size_t len = utf8_grapheme_next((const uint8_t *)p, strlen(p));
        int width = utf8_cluster_width((const uint8_t *)p, len);
        const render_cell *cell = &g->cells[(size_t)unicode_row * g->dims.cols + col];
        T(bitmap(g, cell) == 0);
        if (*p == 'e') T(combining(g, cell) == 0);
        T((cell->attrs & RENDER_ATTR_WIDE_LEFT) != 0 ? width == 2 : width == 1);
        if (width == 2) T(g->cells[(size_t)unicode_row * g->dims.cols + col + 1u].attrs & RENDER_ATTR_WIDE_RIGHT);
        T(g->glyphs[cell->atlas_slot].w == g->dims.cell_w * (uint32_t)width);
        col += (uint32_t)width; p += len;
    }
    T(render_grid_validate(g) == RENDER_OK);
    size_t pixels = (size_t)g->dims.cols * g->dims.cell_w * g->dims.cell_h;
    uint32_t *scalar = calloc(pixels, sizeof *scalar), *simd = calloc(pixels, sizeof *simd);
    T(scalar && simd);
    raster_scene scene = {g->dims, g->cells, g->glyphs, g->glyph_count, g->pages, g->page_count, 0};
    size_t stride = (size_t)g->dims.cols * g->dims.cell_w;
    raster_row_scalar(&scene, scalar, stride, unicode_row);
    raster_row_sse2(&scene, simd, stride, unicode_row);
    T(!memcmp(scalar, simd, pixels * sizeof *scalar));
    free(simd); free(scalar);
    editor_close(e);
    return 0;
}

static int glyph_minimap_repaint(void)
{
    for (unsigned mode = 0; mode < 6; mode++) {
        glyph_gate gate = {0};
        render_backend backend = {0}; T(render_null_backend(&backend) == RENDER_OK);
        editor_config cfg = {.cols = 40, .rows = 6,
            .initial = (const uint8_t *)"\naé日\nz\n", .initial_len = sizeof "\naé日\nz\n" - 1u,
            .hook_ctx = &gate, .on_submit = submitted};
        editor *e = NULL; T(editor_open(&e, &cfg, &backend) == EDITOR_OK); gate.editor = e;
        work_handle blocker = work_submit(&e->pool, (work_job){block_bulk, &gate, 0, WORK_BULK});
        T(blocker.epoch);
        while (!atomic_load(&gate.started)) sched_yield();
        if (mode == 5) T(tabs_rename(&e->tabs, tabs_active_index(&e->tabs), "é", sizeof "é" - 1u, "", 0) == TABS_OK);
        /* Prepare the placeholder frame and index, keeping glyph work held. */
        do {
            int rc = editor_step(e, 0); T(rc == EDITOR_OK || rc == EDITOR_MORE);
        } while (editor_get_stats(e).pending || e->buffer->map.stale);
        T(font_async_pending(e->fonts));
        render_cell before[40 * 6], map[8 * 5];
        memcpy(before, e->grid.cells, sizeof before);
        memcpy(map, e->map_grid.cells, sizeof map);
        uint64_t fills = e->stats.minimap_fills, revision = e->buffer->map.revision;
        uint64_t expected = UINT64_C(1) << (e->tab_rows + 1u);
        if (mode == 5) expected |= UINT64_C(1); /* same pending image in the title */
        gate.watch_damage = true;
        atomic_store(&gate.release, true);
        /* Wait for publication without adopting it: force the completion to
         * meet a clean frame, a pending caret frame, a blink, or real input. */
        while (!work_mailbox_pending(&e->pool)) sched_yield();
        if (mode == 1) { T(editor_begin_frame(e) == EDITOR_OK); e->caret_only = true; }
        if (mode == 2) e->next_blink = trace_now_ns();
        if (mode == 3) {
            plat_event ev = {.kind = PLAT_EV_KEY, .press = true, .utf8_len = 1, .utf8 = {'x'}};
            T(editor_inject(e, &ev) == EDITOR_OK);
        }
        if (mode == 4) {
            plat_event ev = {.kind = PLAT_EV_FOCUS, .focused = false};
            T(editor_inject(e, &ev) == EDITOR_OK);
        }
        T(settle(e) == 0);
        T(gate.repaints > 0);
        if (mode == 3) {
            T(e->stats.minimap_fills > fills); /* input still takes normal composition */
            T(e->buffer->revision != revision);
        } else {
            if (e->stats.minimap_fills != fills)
                fprintf(stderr, "Unicode completion mode=%u extra minimap fills=%llu\n", mode,
                    (unsigned long long)(e->stats.minimap_fills - fills));
            T(e->stats.minimap_fills == fills);
            T(!e->buffer->map.stale && e->buffer->map.revision == revision);
            T(!memcmp(map, e->map_grid.cells, sizeof map));
            if (mode == 2 || mode == 4) expected |= UINT64_C(1) << e->tab_rows;
            T(gate.damage == expected);
            for (uint32_t r = 0; r < e->grid.dims.rows; r++) {
                if ((mode == 2 || mode == 4) && r == e->tab_rows) continue; /* caret visibility */
                T(!memcmp(before + (size_t)r * e->grid.dims.cols,
                    e->grid.cells + (size_t)r * e->grid.dims.cols,
                    (size_t)e->grid.dims.cols * sizeof(render_cell)));
            }
        }
        printf("unicode_render_test: minimap completion mode=%u fills=%llu damage=%llx\n",
            mode, (unsigned long long)(e->stats.minimap_fills - fills), (unsigned long long)gate.damage);
        if (mode != 3) {
            e->next_blink = trace_now_ns(); T(editor_step(e, 0) >= 0);
            T(e->stats.minimap_fills == fills);
        }
        editor_close(e);
    }
    return 0;
}

typedef struct owner_receiver { font_async *owner; render_grid *grid; bool duplicate; } owner_receiver;
static void adopt(const work_msg *msg, void *ctx)
{
    owner_receiver *receiver = ctx;
    if (font_async_event(receiver->owner, msg) != RENDER_NO_SLOT) {
        receiver->duplicate |= font_async_event(receiver->owner, msg) != RENDER_NO_SLOT;
        (void)font_async_bind(receiver->owner, receiver->grid);
    }
}

static void complete(work_pool *pool, owner_receiver *receiver)
{
    while (font_async_pending(receiver->owner)) {
        (void)work_mailbox_drain(pool, adopt, receiver);
        font_async_pump(receiver->owner);
        sched_yield();
    }
}

static int owner_mailbox(void)
{
    work_pool *pool = aligned_alloc(_Alignof(work_pool), sizeof *pool);
    T(pool && work_pool_init(pool, 1, 0) == 0);
    font_async *owner = NULL; T(font_async_open(&owner, font_ascii_px(), pool) == FONT_OK);
    font_cell cell = font_ascii_cell();
    render_cell cells[1] = {{.atlas_slot = RENDER_NO_SLOT}}; uint64_t dirty = 0;
    render_grid grid;
    T(render_grid_init(&grid, (render_dims){1, 1, cell.cell_w, cell.cell_h}, cells,
                       sizeof cells / sizeof cells[0], &dirty, 1) == RENDER_OK);
    T(font_async_bind(owner, &grid) == FONT_OK);
    uint32_t first, second;
    if (edit_malloc_guard_active()) edit_malloc_guard_begin();
    T(font_async_glyph(owner, (const uint8_t *)"é", sizeof "é" - 1u, 1, &first) == FONT_OK);
    if (edit_malloc_guard_active()) T(edit_malloc_guard_end() == 0);
    T(font_async_pending(owner));
    owner_receiver receiver = {owner, &grid, false};
    complete(pool, &receiver); T(!receiver.duplicate);
    T(bitmap(&grid, &(render_cell){.atlas_slot = first}) == 0);
    render_glyph image = grid.glyphs[first];
    render_atlas_page page = grid.pages[image.page];
    uint8_t *saved = malloc((size_t)image.w * image.h); T(saved);
    for (uint32_t y = 0; y < image.h; y++)
        memcpy(saved + (size_t)y * image.w, page.pixels + (size_t)(image.y + y) * page.stride + image.x, image.w);
    work_msg foreign = {.kind = FONT_ASYNC_MSG_KIND};
    T(font_async_event(owner, &foreign) == RENDER_NO_SLOT);
    T(font_async_glyph(owner, (const uint8_t *)"α", sizeof "α" - 1u, 1, &second) == FONT_OK);
    T(second != first); complete(pool, &receiver); T(!receiver.duplicate);
    T(bitmap(&grid, &(render_cell){.atlas_slot = second}) == 0);
    for (uint32_t y = 0; y < image.h; y++)
        T(!memcmp(saved + (size_t)y * image.w, page.pixels + (size_t)(image.y + y) * page.stride + image.x, image.w));
    uint32_t cached;
    if (edit_malloc_guard_active()) edit_malloc_guard_begin();
    T(font_async_glyph(owner, (const uint8_t *)"é", sizeof "é" - 1u, 1, &cached) == FONT_OK);
    if (edit_malloc_guard_active()) T(edit_malloc_guard_end() == 0);
    T(cached == first && !font_async_pending(owner));
    free(saved); work_pool_shutdown(pool); font_async_close(owner); free(pool);
    return 0;
}

int main(void)
{
    trace_init(); T(trace_thread_register() >= 0);
    T(render_unicode(false) == 0);
    T(render_unicode(true) == 0);
    T(glyph_minimap_repaint() == 0);
    T(owner_mailbox() == 0);
    puts("unicode_render_test: initial and injected é α 日 🙂; widths, combining overlay, atlas pixels and allocation guard passed");
    return 0;
}
