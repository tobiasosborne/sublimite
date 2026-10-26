/* edit-08z: production editor lookup, null backend, and actual atlas pixels. */
#include "editor/private.h"
#include "raster/raster.h"
#include "trace/trace.h"
#include <limits.h>
#include <sched.h>
#include <stdio.h>
#include <string.h>
#include "../vendor/xkbcommon/xkbcommon-keysyms.h"

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

static int same_bitmap(const render_grid *g, uint32_t a, uint32_t b)
{
    T(a != RENDER_NO_SLOT && b != RENDER_NO_SLOT && a < g->glyph_count && b < g->glyph_count);
    const render_glyph *left = &g->glyphs[a], *right = &g->glyphs[b];
    T(left->w == right->w && left->h == right->h);
    const render_atlas_page *lp = &g->pages[left->page], *rp = &g->pages[right->page];
    for (uint32_t y = 0; y < left->h; y++)
        T(!memcmp(lp->pixels + (size_t)(left->y + y) * lp->stride + left->x,
                  rp->pixels + (size_t)(right->y + y) * rp->stride + right->x, left->w));
    return 0;
}

static int emoji_fallback(bool inject, bool wrapping)
{
    static const char text[] = "\n👩‍💻x\n😀x\n😀‍💻x\n😀︎x\n😀️x\n😀🏻x\n😀🏼x\n😀🏽x\n😀🏾x\n😀🏿x\n‍︎️🏻🏼🏽🏾🏿x\n\xcd\xb8x";
    /* edit-v16: key is the whole emoji cluster layout now requests; only
     * 👩‍💻 is an RGI ligature, so every other row must still draw exactly
     * its first emoji (the r8e projection inside font_cache_render). */
    static const struct { const char *key, *base; uint32_t width; bool ligature; } expected[] = {
        {"👩‍💻", "👩", 2, true}, {"😀", "😀", 2, false}, {"😀‍💻", "😀", 2, false},
        {"😀︎", "😀", 2, false}, {"😀️", "😀", 2, false},
        {"😀🏻", "😀", 2, false}, {"😀🏼", "😀", 2, false}, {"😀🏽", "😀", 2, false},
        {"😀🏾", "😀", 2, false}, {"😀🏿", "😀", 2, false},
        {"", "", 0, false}, {"\xcd\xb8", "\xcd\xb8", 1, false}, /* U+0378: no covering face in the test environment */
    };
    glyph_gate gate = {0};
    render_backend backend = {0}; T(render_null_backend(&backend) == RENDER_OK);
    /* (G) Byte extent bounds the text columns; each expected item has its own
     * row, plus the leading blank line and tab strip. */
    editor_config cfg = {.cols = (uint32_t)sizeof text,
        .rows = (uint32_t)(sizeof expected / sizeof expected[0]) + 1u + 1u,
        .initial = (const uint8_t *)(inject ? "\n" : text),
        .initial_len = inject ? sizeof "\n" - 1u : sizeof text - 1u,
        .hook_ctx = &gate, .on_submit = submitted};
    editor *e = NULL; T(editor_open(&e, &cfg, &backend) == EDITOR_OK); gate.editor = e;
    T(settle(e) == 0);
    if (wrapping) {
        T(layout_set_wrap(&e->lay, true) == LAYOUT_DONE);
        T(editor_begin_frame(e) == EDITOR_OK); T(settle(e) == 0);
    }
    if (inject) {
        T(editor_set_cursor(e, sizeof "\n" - 1u) == EDITOR_OK); T(settle(e) == 0);
        work_handle blocker = work_submit(&e->pool, (work_job){block_bulk, &gate, 0, WORK_BULK});
        T(blocker.epoch); while (!atomic_load(&gate.started)) sched_yield();
        /* Input events contain scalars, so the cluster changes while typing
         * its ZWJ, modifiers and final emoji. Submission cannot wait for ink. */
        for (size_t off = sizeof "\n" - 1u; off < sizeof text - 1u;) {
            utf8_step step = utf8_decode((const uint8_t *)text + off, sizeof text - 1u - off);
            T(step.valid && step.len <= PLAT_UTF8_MAX);
            plat_event ev = {.kind = PLAT_EV_KEY, .press = true, .utf8_len = step.len};
            memcpy(ev.utf8, text + off, step.len); off += step.len;
            gate.armed = true; gate.submitted = false;
            if (edit_malloc_guard_active()) edit_malloc_guard_begin();
            T(editor_inject(e, &ev) == EDITOR_OK); gate.sequence = e->event_serial;
            while (!gate.submitted) T(editor_step(e, 0) >= 0);
            T(gate.allocations == 0); /* (G) law 2: input through submit */
        }
        atomic_store(&gate.release, true); T(settle(e) == 0);
    }
    /* Typing follows the final caret; inspect the fixture from its start. */
    T(editor_set_cursor(e, 0) == EDITOR_OK); T(settle(e) == 0);
    T(editor_length(e) == sizeof text - 1u);
    uint8_t saved[sizeof text - 1u];
    T(editor_read(e, 0, saved, sizeof saved) == EDITOR_OK);
    T(!memcmp(saved, text, sizeof saved)); /* complete ZWJ/modifier bytes survive */
    const render_grid *g = editor_grid(e);
    uint32_t col = layout_gutter_width(&e->lay);
    for (size_t i = 0; i < sizeof expected / sizeof expected[0]; i++) {
        uint32_t row = e->tab_rows + 1u + (uint32_t)i;
        const render_cell *cell = &g->cells[(size_t)row * g->dims.cols + col];
        if (expected[i].width) {
            uint32_t base, key;
            T(editor_font_glyph(e, (const uint8_t *)expected[i].base, strlen(expected[i].base), expected[i].width, &base) == FONT_OK);
            T(settle(e) == 0);
            T(editor_font_glyph(e, (const uint8_t *)expected[i].key, strlen(expected[i].key), expected[i].width, &key) == FONT_OK);
            T(settle(e) == 0);
            /* The cell holds the whole cluster's image. Without a ligature it
             * is exactly the first emoji's ink: no joined laptop, selector or
             * skin modifier can overlay or replace it. */
            if (cell->atlas_slot != key)
                fprintf(stderr, "emoji input=%u wrap=%u item=%zu row=%u col=%u slot=%u key=%u byte=%llu\n",
                    inject, wrapping, i, row, col, cell->atlas_slot, key,
                    (unsigned long long)e->row_byte[row - e->tab_rows]);
            T(cell->atlas_slot == key);
            T(bitmap(g, cell) == 0);
            if (!expected[i].ligature) T(same_bitmap(g, cell->atlas_slot, base) == 0);
            else if (!inject && !wrapping) {
                /* Informational: depends on the installed emoji face. */
                const render_glyph *l = &g->glyphs[cell->atlas_slot], *r = &g->glyphs[base];
                bool same = l->w == r->w && l->h == r->h;
                for (uint32_t y = 0; same && y < l->h; y++)
                    same = !memcmp(g->pages[l->page].pixels + (size_t)(l->y + y) * g->pages[l->page].stride + l->x,
                                   g->pages[r->page].pixels + (size_t)(r->y + y) * g->pages[r->page].stride + r->x, l->w);
                printf("unicode_render_test: %s drawn as %s\n", expected[i].key,
                       same ? "its first emoji (no GSUB ligature in the resolved face)" : "a distinct ligature image");
            }
            if (expected[i].width == 2) {
                T(cell->attrs & RENDER_ATTR_WIDE_LEFT);
                T(g->cells[(size_t)row * g->dims.cols + col + 1u].attrs & RENDER_ATTR_WIDE_RIGHT);
            } else {
                uint32_t missing;
                T(editor_font_glyph(e, (const uint8_t *)"�", sizeof "�" - 1u, 1, &missing) == FONT_OK);
                T(settle(e) == 0); T(same_bitmap(g, base, missing) == 0);
            }
        }
        T(g->cells[(size_t)row * g->dims.cols + col + expected[i].width].glyph_index == 'x');
    }
    T(render_grid_validate(g) == RENDER_OK);
    editor_close(e);
    printf("unicode_render_test: emoji first-scalar fallback, invisibles and true missing glyph; input=%u wrap=%u passed\n", inject, wrapping);
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

static int blank_descriptors(void)
{
    /* edit-2ghs: a settled image without ink, and a slot vacated by
     * eviction, must leave the bound table valid under render.h (no
     * zero-sized rectangles); lookups of the blank key answer NO_SLOT.
     * NBSP U+00A0 and IDEOGRAPHIC SPACE U+3000 (wide) are blank scalars. */
    work_pool *pool = aligned_alloc(_Alignof(work_pool), sizeof *pool);
    T(pool && work_pool_init(pool, 1, 0) == 0);
    font_async *owner = NULL; T(font_async_open(&owner, font_ascii_px(), pool) == FONT_OK);
    font_cell cell = font_ascii_cell();
    render_cell cells[1] = {{.atlas_slot = RENDER_NO_SLOT}}; uint64_t dirty = 0;
    render_grid grid;
    T(render_grid_init(&grid, (render_dims){1, 1, cell.cell_w, cell.cell_h}, cells, 1, &dirty, 1) == RENDER_OK);
    T(font_async_bind(owner, &grid) == FONT_OK);
    owner_receiver receiver = {owner, &grid, false};
    static const char *const blanks[] = {"\xc2\xa0", "\xe3\x80\x80"};
    for (uint32_t i = 0; i < 2u; i++) {
        uint32_t slot, again;
        T(font_async_glyph(owner, (const uint8_t *)blanks[i], strlen(blanks[i]), i + 1u, &slot) == FONT_OK);
        T(slot >= LAYOUT_ASCII_GLYPHS && font_async_inked(owner, slot)); /* placeholder while pending */
        complete(pool, &receiver);
        T(!font_async_inked(owner, slot));
        T(render_grid_validate(&grid) == RENDER_OK);
        T(font_async_glyph(owner, (const uint8_t *)blanks[i], strlen(blanks[i]), i + 1u, &again) == FONT_OK);
        T(again == RENDER_NO_SLOT);
    }
    T(!receiver.duplicate);
    work_pool_shutdown(pool); font_async_close(owner); free(pool);
    puts("unicode_render_test: blank images keep a valid descriptor table");
    return 0;
}

static size_t exhaustion_key(uint8_t *key, size_t value, size_t digits)
{
    /* (P) Binary digits select Unicode default-ignorables U+034F / U+FE0F.
     * Their UTF-8 extents come from the literals; all keys retain é's ink. */
    static const uint8_t base[] = "é", zero[] = "\xcd\x8f", one[] = "\xef\xb8\x8f";
    size_t len = sizeof base - 1u;
    memcpy(key, base, len);
    for (size_t digit = 0; digit < digits; digit++) {
        const uint8_t *bytes = value & 1u ? one : zero;
        size_t n = value & 1u ? sizeof one - 1u : sizeof zero - 1u;
        memcpy(key + len, bytes, n); len += n; value >>= 1u;
    }
    return len;
}

static int cache_exhaustion(bool large)
{
    /* (P) One bulk worker serializes the owner; one cell models a scrolling
     * viewport. The request count is the renderer's capacity plus one, so it
     * necessarily exceeds the cache without an invented fixture count. */
    work_pool *pool = aligned_alloc(_Alignof(work_pool), sizeof *pool);
    T(pool && work_pool_init(pool, 1, 0) == 0);
    font_async *owner = NULL;
    font_cell cell = font_ascii_cell();
    font_t primary; font_ascii_atlas ascii = {0}; uint8_t *ascii_pixels = NULL;
    if (large) {
        /* (P) Use the editor's existing maximum em, not an invented size.
         * Blank ASCII storage suffices: the regression inspects runtime ink. */
        size_t len; const unsigned char *bytes = font_embedded_data(&len);
        T(font_init(&primary, bytes, len) == FONT_OK);
        T(font_set_em_px(&primary, EDITOR_FONT_MAX_PX) == FONT_OK);
        cell = primary.cell;
        size_t bytes_len = (size_t)LAYOUT_ASCII_GLYPHS * cell.cell_w * cell.cell_h;
        ascii_pixels = calloc(bytes_len, 1); T(ascii_pixels);
        ascii = (font_ascii_atlas){primary.px, cell, NULL, ascii_pixels, bytes_len};
        T(font_async_open_atlas(&owner, &ascii, true, cell, pool) == FONT_OK);
        T(font_async_set_primary(owner, &primary) == FONT_OK);
    } else T(font_async_open(&owner, font_ascii_px(), pool) == FONT_OK);
    uint32_t pages; size_t glyphs, pixels;
    font_async_limits(&pages, &glyphs, &pixels, font_ascii_px());
    size_t capacity = glyphs - LAYOUT_ASCII_GLYPHS;
    /* (P) Large cells can exhaust physical narrow rectangles before the
     * descriptor ceiling: pages * columns per page * rows per page. */
    size_t physical = (size_t)FONT_ATLAS_MAX_PAGES * (FONT_ATLAS_PAGE_DIM / cell.cell_w) *
                      (FONT_ATLAS_PAGE_DIM / cell.cell_h);
    if (physical < capacity) capacity = physical;
    render_cell cells[1] = {{.atlas_slot = RENDER_NO_SLOT}}; uint64_t dirty = 0;
    render_grid grid;
    T(render_grid_init(&grid, (render_dims){1, 1, cell.cell_w, cell.cell_h},
                       cells, 1, &dirty, 1) == RENDER_OK);
    T(font_async_bind(owner, &grid) == FONT_OK);
    owner_receiver receiver = {owner, &grid, false};
    uint32_t retained_slot;
    T(font_async_glyph(owner, (const uint8_t *)"α", sizeof "α" - 1u, 1, &retained_slot) == FONT_OK);
    complete(pool, &receiver);
    cells[0] = (render_cell){.atlas_slot = retained_slot,
        .glyph_index = grid.glyphs[retained_slot].glyph_index};
    render_glyph retained_image = grid.glyphs[retained_slot];
    const render_atlas_page *retained_page = &grid.pages[retained_image.page];
    uint8_t *retained_pixels = malloc((size_t)cell.cell_w * cell.cell_h); T(retained_pixels);
    for (uint32_t y = 0; y < cell.cell_h; y++)
        memcpy(retained_pixels + (size_t)y * cell.cell_w,
            retained_page->pixels + (size_t)(retained_image.y + y) * retained_page->stride + retained_image.x, cell.cell_w);
    /* (P) The capacity determines the binary key's digit count. Storage is
     * bounded by size_t's bit width times the longest ignorable UTF-8 span. */
    size_t digits = 0;
    for (size_t value = capacity; value; value >>= 1u) digits++;
    uint8_t key[sizeof "é" - 1u + sizeof(size_t) * CHAR_BIT * (sizeof "\xef\xb8\x8f" - 1u)];
    T(sizeof key <= FONT_CLUSTER_MAX_BYTES);
    uint8_t *reference = malloc((size_t)cell.cell_w * cell.cell_h); T(reference);
    for (size_t visit = 0; visit <= capacity; visit++) {
        font_async_frame(owner, &grid, NULL, false);
        size_t len = exhaustion_key(key, visit, digits);
        uint32_t slot;
        if (edit_malloc_guard_active()) edit_malloc_guard_begin();
        T(font_async_glyph(owner, key, len, 1, &slot) == FONT_OK);
        if (edit_malloc_guard_active()) T(edit_malloc_guard_end() == 0);
        complete(pool, &receiver);
        if (slot < LAYOUT_ASCII_GLYPHS)
            fprintf(stderr, "unicode_render_test: exhaustion visit=%zu capacity=%zu returned ASCII slot=%u\n", visit, capacity, slot);
        T(slot >= LAYOUT_ASCII_GLYPHS && slot < grid.glyph_count);
        const render_glyph *image = &grid.glyphs[slot];
        const render_atlas_page *page = &grid.pages[image->page];
        T(image->w == cell.cell_w && image->h == cell.cell_h);
        for (uint32_t y = 0; y < image->h; y++) {
            const uint8_t *row = page->pixels + (size_t)(image->y + y) * page->stride + image->x;
            if (!visit) memcpy(reference + (size_t)y * image->w, row, image->w);
            else T(!memcmp(reference + (size_t)y * image->w, row, image->w));
            T(!memcmp(retained_pixels + (size_t)y * cell.cell_w,
                retained_page->pixels + (size_t)(retained_image.y + y) * retained_page->stride + retained_image.x, cell.cell_w));
        }
    }
    /* Scrolling back must recover an evicted key's exact pixels too. */
    uint32_t slot;
    font_async_frame(owner, &grid, NULL, false);
    size_t first_len = exhaustion_key(key, 0, digits);
    T(font_async_glyph(owner, key, first_len, 1, &slot) == FONT_OK);
    T(font_async_pending(owner)); /* oldest unretained key was evicted */
    complete(pool, &receiver);
    T(slot >= LAYOUT_ASCII_GLYPHS);
    const render_glyph *image = &grid.glyphs[slot];
    const render_atlas_page *page = &grid.pages[image->page];
    for (uint32_t y = 0; y < image->h; y++)
        T(!memcmp(reference + (size_t)y * image->w,
                  page->pixels + (size_t)(image->y + y) * page->stride + image->x, image->w));
    T(font_async_glyph(owner, (const uint8_t *)"α", sizeof "α" - 1u, 1, &slot) == FONT_OK);
    T(slot == retained_slot && !font_async_pending(owner));
    /* (P) A synthetic outgoing frame references every resident slot. No
     * eviction is legal until it retires; reclamation keeps incoming hits. */
    render_cell *borrowed_cells = calloc(capacity, sizeof *borrowed_cells); T(borrowed_cells);
    for (size_t i = 0; i < capacity; i++) {
        uint32_t borrowed_slot = (uint32_t)(LAYOUT_ASCII_GLYPHS + i);
        borrowed_cells[i] = (render_cell){.atlas_slot = borrowed_slot,
            .glyph_index = grid.glyphs[borrowed_slot].glyph_index};
    }
    render_grid borrowed = grid;
    borrowed.dims.cols = (uint32_t)capacity; borrowed.cells = borrowed_cells;
    borrowed.cell_capacity = capacity;
    font_async_frame(owner, &borrowed, NULL, true);
    /* (P) A wide image consumes adjacent narrow tiles; reuse changes width. */
    T(font_async_glyph(owner, (const uint8_t *)"β", sizeof "β" - 1u, 2, &slot) == FONT_MORE);
    T(!font_async_pending(owner));
    T(font_async_glyph(owner, (const uint8_t *)"α", sizeof "α" - 1u, 1, &slot) == FONT_OK);
    font_async_reclaim(owner); /* outgoing synthetic frame has retired */
    T(font_async_glyph(owner, (const uint8_t *)"β", sizeof "β" - 1u, 2, &slot) == FONT_OK);
    T(slot != retained_slot); complete(pool, &receiver);
    T(grid.glyphs[slot].w == cell.cell_w * 2u);
    /* edit-2ghs: the wide request may vacate more than one narrow slot;
     * vacant slots stay in the bound table and must still validate. */
    T(render_grid_validate(&grid) == RENDER_OK);
    for (uint32_t y = 0; y < cell.cell_h; y++)
        T(!memcmp(retained_pixels + (size_t)y * cell.cell_w,
            retained_page->pixels + (size_t)(retained_image.y + y) * retained_page->stride + retained_image.x, cell.cell_w));
    free(borrowed_cells);
    /* Reverse navigation repeatedly deletes collision-chain entries. */
    for (size_t visit = capacity; visit > 0; visit--) {
        font_async_frame(owner, &grid, NULL, false);
        size_t len = exhaustion_key(key, visit - 1u, digits);
        T(font_async_glyph(owner, key, len, 1, &slot) == FONT_OK);
        complete(pool, &receiver);
        T(slot >= LAYOUT_ASCII_GLYPHS && slot != retained_slot);
        T(bitmap(&grid, &(render_cell){.atlas_slot = slot}) == 0);
    }
    T(!receiver.duplicate);
    T(render_grid_validate(&grid) == RENDER_OK);
    free(retained_pixels); free(reference); work_pool_shutdown(pool); font_async_close(owner); free(pool); free(ascii_pixels);
    printf("unicode_render_test: supported clusters beyond %s cache capacity recover exact ink on return\n",
           large ? "maximum-em" : "descriptor");
    return 0;
}

static int sized_primary(editor *e)
{
    const render_grid *g = editor_grid(e);
    uint32_t col = layout_gutter_width(&e->lay), row = e->tab_rows;
    uint32_t cps[] = {0xe9u, 0x3b1u};
    size_t len; const unsigned char *bytes = font_embedded_data(&len);
    font_t primary; T(font_init(&primary, bytes, len) == FONT_OK);
    T(font_set_em_px(&primary, e->font.applied_px) == FONT_OK);
    T(primary.cell.cell_w == g->dims.cell_w && primary.cell.cell_h == g->dims.cell_h);
    edit_arena scratch; T(edit_arena_init(&scratch, 1u << 20) == 0);
    uint8_t *reference = calloc((size_t)g->dims.cell_w * g->dims.cell_h, 1); T(reference);
    for (size_t i = 0; i < sizeof cps / sizeof cps[0]; i++, col++) {
        const render_cell *cell = &g->cells[(size_t)row * g->dims.cols + col];
        T(bitmap(g, cell) == 0);
        const render_glyph *glyph = &g->glyphs[cell->atlas_slot];
        const render_atlas_page *page = &g->pages[glyph->page];
        T(glyph->w == g->dims.cell_w && glyph->h == g->dims.cell_h);
        edit_arena_reset(&scratch); font_bitmap image;
        T(font_raster_glyph(&primary, cps[i], &scratch, &image) == FONT_OK);
        memset(reference, 0, (size_t)g->dims.cell_w * g->dims.cell_h);
        T(font_place_in_cell(&primary, &image, reference, g->dims.cell_w, g->dims.cell_h) == FONT_OK);
        for (uint32_t y = 0; y < glyph->h; y++)
            T(!memcmp(reference + (size_t)y * glyph->w,
                      page->pixels + (size_t)(glyph->y + y) * page->stride + glyph->x, glyph->w));
    }
    T(combining(g, &g->cells[(size_t)row * g->dims.cols + col]) == 0);
    T(render_grid_validate(g) == RENDER_OK);
    free(reference); edit_arena_free(&scratch);
    return 0;
}

static int dpi_zoom_unicode(double dpi)
{
    render_backend backend = {0}; T(render_null_backend(&backend) == RENDER_OK);
    editor_config cfg = {.font_dpi = dpi, .cols = 40, .rows = 8,
        .initial = (const uint8_t *)"éαé", .initial_len = sizeof "éαé" - 1u};
    editor *e = NULL; T(editor_open(&e, &cfg, &backend) == EDITOR_OK);
    T(settle(e) == 0); T(sized_primary(e) == 0);
    font_async *original = e->fonts;
    glyph_gate gate = {0};
    work_handle blocker = work_submit(&e->pool, (work_job){block_bulk, &gate, 0, WORK_BULK});
    T(blocker.epoch); while (!atomic_load(&gate.started)) sched_yield();
    /* Two switches leave glyphs pending in both versions. A third request
     * must wait for the detached owner to finish, while input keeps draining. */
    plat_event key = {.kind = PLAT_EV_KEY, .press = true, .keysym = XKB_KEY_equal, .mods = PLAT_MOD_CTRL};
    for (unsigned i = 0; i < 2; i++) {
        T(editor_inject(e, &key) == EDITOR_OK);
        do { T(editor_step(e, 0) >= 0); } while (editor_get_stats(e).pending);
        T(font_async_pending(e->fonts));
    }
    T(e->fonts == original);
    key.keysym = XKB_KEY_0; T(editor_inject(e, &key) == EDITOR_OK);
    while (e->queue_count) T(editor_step(e, 0) >= 0);
    T(e->font.requested_px == e->font.default_px && e->font.applied_px != e->font.default_px);
    T(!e->font.handle.epoch); /* still-borrowed worker version cannot be reset */
    atomic_store(&gate.release, true);
    T(settle(e) == 0); T(e->font.applied_px == e->font.default_px); T(sized_primary(e) == 0);
    key.keysym = XKB_KEY_equal; T(editor_inject(e, &key) == EDITOR_OK);
    T(settle(e) == 0); T(sized_primary(e) == 0);
    key.keysym = XKB_KEY_0; T(editor_inject(e, &key) == EDITOR_OK);
    T(settle(e) == 0); T(sized_primary(e) == 0);
    T(editor_length(e) == cfg.initial_len);
    editor_close(e);
    printf("unicode_render_test: DPI %.0f zoom/reset matches true-em primary pixels; pending owner retirement passed\n", dpi);
    return 0;
}

/* edit-6di: a file named "build/73j-å文" painted "73j-" then blank cells in
 * its tab. The title path must reach the same async lookup and adoption as
 * text: once glyphs settle, each title cell holds real ink (not the pending
 * '?' artwork, not blank) and widths are counted per cluster in cells. */
static int tab_title_unicode(void)
{
    static const char path[] = "build/73j-\xc3\xa5\xe6\x96\x87"; /* build/73j-å文 */
    static const char title[] = "73j-\xc3\xa5\xe6\x96\x87";
    render_backend backend = {0}; T(render_null_backend(&backend) == RENDER_OK);
    /* (G) 40 columns hold two 16-column tab slots: the initial buffer and this file. */
    glyph_gate gate = {0};
    editor_config cfg = {.cols = 40, .rows = 4,
        .initial = (const uint8_t *)"x\n", .initial_len = sizeof "x\n" - 1u,
        .hook_ctx = &gate, .on_submit = submitted};
    editor *e = NULL; T(editor_open(&e, &cfg, &backend) == EDITOR_OK);
    gate.editor = e;
    T(settle(e) == 0);
    uint64_t id = 0;
    T(editor_add_buffer(e, path, (const uint8_t *)"y\n", sizeof "y\n" - 1u, &id) == EDITOR_OK);
    T(settle(e) == 0);
    const tabs_tab *t = tabs_active(&e->tabs);
    T(t && t->title_len == sizeof title - 1u && !memcmp(t->title, title, sizeof title - 1u));
    /* (P) 7 cells = "73j-" 4 narrow + å 1 narrow + 文 2 wide (UAX #11); bytes are 9. */
    T(t->title_cells == 7u && t->title_clusters == 6u);
    const render_grid *g = editor_grid(e);
    size_t active = tabs_active_index(&e->tabs);
    uint32_t col = e->strip.first_col + (uint32_t)(active - e->strip.first_tab) * e->strip.tab_cols + 1u;
    for (const char *p = title; *p;) {
        size_t len = utf8_grapheme_next((const uint8_t *)p, strlen(p));
        int width = utf8_cluster_width((const uint8_t *)p, len);
        const render_cell *cell = &g->cells[col];
        if (cell->atlas_slot == RENDER_NO_SLOT || cell->atlas_slot >= g->glyph_count)
            fprintf(stderr, "unicode_render_test: tab title col %u for '%.*s' is blank (slot %u)\n",
                col, (int)len, p, cell->atlas_slot);
        T(cell->atlas_slot != RENDER_NO_SLOT && cell->atlas_slot < g->glyph_count);
        if ((uint8_t)*p >= 0x80u) {
            T(bitmap(g, cell) == 0); /* real ink, not blank and not the pending '?' */
            T(g->glyphs[cell->atlas_slot].w == g->dims.cell_w * (uint32_t)width);
        } else {
            T(cell->atlas_slot == (uint32_t)(*p - ' '));
        }
        T((cell->attrs & RENDER_ATTR_WIDE_LEFT) != 0 ? width == 2 : width == 1);
        if (width == 2) T(g->cells[col + 1u].attrs & RENDER_ATTR_WIDE_RIGHT);
        col += (uint32_t)width; p += len;
    }
    T(render_grid_validate(g) == RENDER_OK);
    /* Adoption: with the bulk lane held, a fresh title's first frame already
     * carries the pending slots (the '?' artwork), and completion repaints
     * the tab row through the same slot/row registration as text. */
    static const char fresh[] = "\xc3\xb8\xe8\xaa\x9e"; /* ø語, never requested above */
    work_handle blocker = work_submit(&e->pool, (work_job){block_bulk, &gate, 0, WORK_BULK});
    T(blocker.epoch);
    while (!atomic_load(&gate.started)) sched_yield();
    T(tabs_rename(&e->tabs, active, fresh, sizeof fresh - 1u, path, sizeof path - 1u) == TABS_OK);
    T(editor_begin_frame(e) == EDITOR_OK);
    do { int rc = editor_step(e, 0); T(rc == EDITOR_OK || rc == EDITOR_MORE); } while (editor_get_stats(e).pending);
    T(font_async_pending(e->fonts));
    col = e->strip.first_col + (uint32_t)(active - e->strip.first_tab) * e->strip.tab_cols + 1u;
    uint32_t held[2] = {g->cells[col].atlas_slot, g->cells[col + 1u].atlas_slot};
    for (size_t i = 0; i < 2; i++) {
        if (held[i] == RENDER_NO_SLOT || held[i] < LAYOUT_ASCII_GLYPHS)
            fprintf(stderr, "unicode_render_test: held tab title cell %zu has no pending slot (%u)\n", i, held[i]);
        T(held[i] != RENDER_NO_SLOT && held[i] < g->glyph_count && held[i] >= LAYOUT_ASCII_GLYPHS);
    }
    T(g->cells[col + 1u].attrs & RENDER_ATTR_WIDE_LEFT);
    gate.watch_damage = true;
    atomic_store(&gate.release, true);
    T(settle(e) == 0);
    T(gate.damage & UINT64_C(1)); /* tab row repainted on adoption */
    T(g->cells[col].atlas_slot == held[0] && g->cells[col + 1u].atlas_slot == held[1]); /* stable slots */
    T(bitmap(g, &g->cells[col]) == 0); T(bitmap(g, &g->cells[col + 1u]) == 0);
    T(g->glyphs[held[1]].w == 2u * g->dims.cell_w);
    editor_close(e);
    puts("unicode_render_test: tab title 73j-å文 holds adopted glyphs, 7 cells for 9 bytes");
    return 0;
}

int main(void)
{
    trace_init(); T(trace_thread_register() >= 0);
    T(blank_descriptors() == 0);
    T(cache_exhaustion(false) == 0);
    T(cache_exhaustion(true) == 0);
    T(tab_title_unicode() == 0);
    T(emoji_fallback(false, false) == 0);
    T(emoji_fallback(true, false) == 0);
    T(emoji_fallback(false, true) == 0);
    T(emoji_fallback(true, true) == 0);
    T(render_unicode(false) == 0);
    T(render_unicode(true) == 0);
    T(glyph_minimap_repaint() == 0);
    T(owner_mailbox() == 0);
    T(dpi_zoom_unicode(96.0) == 0);
    T(dpi_zoom_unicode(243.0) == 0);
    puts("unicode_render_test: initial and injected é α 日 🙂; widths, combining overlay, atlas pixels and allocation guard passed");
    return 0;
}
