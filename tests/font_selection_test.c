/* edit-5ih: fixture geometry reuses editor allocation test; ASCII endpoints
 * derive from the printable ASCII range. Zero allocations is CLAUDE Law 2.
 * All numeric parsing expectations below derive from the input strings. */
#include "editor/private.h"
#include "font/font.h"
#include "trace/trace.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sched.h>
#include <unistd.h>
#include <xkbcommon/xkbcommon-keysyms.h>
#define T(c) do { if (!(c)) { fprintf(stderr, "font_selection_test:%d: FAIL %s\n", __LINE__, #c); return 1; } } while (0)
typedef struct guard { bool active; size_t allocations, submits; } guard;
static void ingress(void *ctx, uint64_t sequence, uint64_t ns)
{
    (void)sequence; (void)ns; guard *g = ctx;
    if (!g->active) { edit_malloc_guard_begin(); g->active = true; }
}
static void submitted(void *ctx, const editor_frame *frame)
{
    (void)frame; guard *g = ctx;
    if (g->active) { g->allocations += edit_malloc_guard_end(); g->active = false; }
    g->submits++;
}
static int settle(editor *e)
{
    while (editor_get_stats(e).pending || editor_get_stats(e).render_active) T(editor_step(e, 0) >= 0);
    return 0;
}
static int prepared(editor *e)
{
    editor_font_size *f = &e->font;
    T(f->handle.epoch);
    while (!f->received || !work_handle_finished(&e->pool, f->handle)) {
        (void)work_mailbox_drain(&e->pool, editor_route_work, e); sched_yield();
    }
    T(!pthread_equal(f->worker, pthread_self())); return 0;
}
static int adopted(editor *e)
{
    while (editor_get_stats(e).font_pending) { T(editor_step(e, 0) >= 0); sched_yield(); }
    return settle(e);
}
static int type(editor *e, guard *g)
{
    plat_event ev = {.kind=PLAT_EV_KEY, .press=true, .keysym=XKB_KEY_a, .utf8_len=1, .utf8={'a'}};
    size_t submits = g->submits; uint64_t n = editor_length(e);
    T(editor_inject(e, &ev) == 0); T(settle(e) == 0);
    T(g->submits > submits && !g->active && g->allocations == 0);
    T(editor_length(e) == n + sizeof ev.utf8[0]);
    T(editor_get_stats(e).submitted_sequence == editor_get_stats(e).input_sequence);
    return 0;
}
static int parsing(void)
{
    font_selection s = {0};
    T(font_selection_parse(&s, "Iosevka Fixed:semibold", "/tmp/font.ttc#12") == FONT_OK);
    T(!strcmp(s.family, "Iosevka Fixed:semibold") && !strcmp(s.file, "/tmp/font.ttc") && s.index == 12);
    T(font_selection_parse(&s, NULL, "vendor/DejaVuSansMono.ttf#0") == FONT_OK && s.index == 0);
    T(font_selection_parse(&s, NULL, "x#65536") == FONT_ERR_ARG); /* low TTC index field max + one */
    T(font_selection_parse(&s, NULL, "x#-1") == FONT_ERR_ARG);
    T(font_selection_parse(&s, NULL, "x#") == FONT_ERR_ARG);
    T(font_selection_parse(&s, NULL, "x#abc") == FONT_ERR_ARG);
    char large[FONT_FALLBACK_PATH_MAX + 1u]; memset(large, 'x', sizeof large); large[sizeof large-1u]=0;
    T(font_selection_parse(&s, large, NULL) == FONT_ERR_ARG);
    T(font_selection_parse(&s, NULL, NULL) == FONT_OK && !s.family[0] && !s.file[0] && !s.index);
    puts("font_selection_test: family/file parsing, precedence inputs, TTC index and bounds passed"); return 0;
}

static uint32_t be16(const unsigned char *p) { return (uint32_t)p[0] * 256u + p[1]; }
static uint32_t be32(const unsigned char *p) { return be16(p) * 65536u + be16(p + 2); }
static void put32(unsigned char *p, uint32_t v)
{
    p[0]=(unsigned char)(v >> 24); p[1]=(unsigned char)(v >> 16);
    p[2]=(unsigned char)(v >> 8); p[3]=(unsigned char)v;
}
/* TTC offsets derive from its header: tag/version/count plus two face offsets.
 * Both faces borrow the same valid sfnt; selecting the latter proves indexing.
 * Raising ascent by itself doubles ascent, changing scale and cell width. */
static int ttc_fixture(char *path)
{
    edit_arena arena = {0};
    /* Fixture reservation is the font-test's existing embedded-file arena. */
    T(edit_arena_init(&arena, 1u << 20) == 0);
    size_t len = 0; unsigned char *font = font_load_file("vendor/DejaVuSansMono.ttf", &arena, &len); T(font);
    const size_t hdr = 5u * sizeof(uint32_t);
    unsigned char *bytes = calloc(hdr + len, 1); T(bytes);
    memcpy(bytes, "ttcf", 4); put32(bytes + 4, 0x00010000u); put32(bytes + 8, 2u);
    put32(bytes + 12, (uint32_t)hdr); put32(bytes + 16, (uint32_t)hdr);
    memcpy(bytes + hdr, font, len);
    for (size_t i = 0; i < be16(font + 4); i++) {
        unsigned char *rec = bytes + hdr + 12u + i * 16u;
        size_t off = be32(rec + 8);
        put32(rec + 8, (uint32_t)(off + hdr));
        if (!memcmp(rec, "hhea", 4)) {
            unsigned char *ascent = bytes + hdr + off + 4;
            uint32_t value = be16(ascent) * 2u;
            ascent[0] = (unsigned char)(value >> 8); ascent[1] = (unsigned char)value;
        }
    }
    int fd = mkstemp(path); T(fd >= 0);
    T(write(fd, bytes, hdr + len) == (ssize_t)(hdr + len)); T(close(fd) == 0);
    free(bytes); edit_arena_free(&arena); return 0;
}

static int rounded_proportional(void)
{
    edit_arena arena = {0}; T(edit_arena_init(&arena, 1u << 20) == 0);
    size_t len = 0; unsigned char *bytes = font_load_file("vendor/DejaVuSansMono.ttf", &arena, &len); T(bytes);
    font_t mono; T(font_init(&mono, bytes, len) == FONT_OK && font_set_px(&mono, 15) == FONT_OK);
    font_metric original; T(font_glyph_metrics(&mono, ' ', &original) == FONT_OK);
    size_t metrics = 0, count = 0, glyphs = 0, metrics_len = 0;
    for (size_t i = 0; i < be16(bytes + 4); i++) {
        const unsigned char *rec = bytes + 12u + i * 16u;
        if (!memcmp(rec, "hmtx", 4)) { metrics = be32(rec + 8); metrics_len = be32(rec + 12); }
        if (!memcmp(rec, "hhea", 4)) count = be16(bytes + be32(rec + 8) + 34u);
        if (!memcmp(rec, "maxp", 4)) glyphs = be16(bytes + be32(rec + 8) + 4u);
    }
    T(metrics && count && glyphs >= count);
    /* Embedded Mono shares its last full metric across later glyphs. Expand
     * the hmtx shorthand to one full advance/bearing pair per glyph first.
     * Growth derives from new full pairs minus the original table bytes. */
    size_t full_len = glyphs * 4u, growth = (full_len - metrics_len + 3u) & ~(size_t)3u; /* sfnt table alignment */
    unsigned char *copy = malloc(len + growth); T(copy);
    memset(copy, 0, len + growth);
    memcpy(copy, bytes, metrics);
    memcpy(copy + metrics + metrics_len + growth, bytes + metrics + metrics_len, len - metrics - metrics_len);
    for (size_t i = 0; i < glyphs; i++) {
        size_t full = i < count ? i : count - 1u;
        uint32_t advance = be16(bytes + metrics + full * 4u) + (uint32_t)(i % 2u);
        size_t bearing = i < count ? metrics + i * 4u + 2u : metrics + count * 4u + (i - count) * 2u;
        copy[metrics + i * 4u] = (unsigned char)(advance >> 8);
        copy[metrics + i * 4u + 1u] = (unsigned char)advance;
        memcpy(copy + metrics + i * 4u + 2u, bytes + bearing, 2u);
    }
    for (size_t i = 0; i < be16(copy + 4); i++) {
        unsigned char *rec = copy + 12u + i * 16u;
        size_t off = be32(rec + 8);
        if (off >= metrics + metrics_len) { off += growth; put32(rec + 8, (uint32_t)off); }
        if (!memcmp(rec, "hmtx", 4)) put32(rec + 12, (uint32_t)full_len);
        if (!memcmp(rec, "hhea", 4)) {
            copy[off + 34u] = (unsigned char)(glyphs >> 8); copy[off + 35u] = (unsigned char)glyphs;
        }
    }
    font_t altered; T(font_init(&altered, copy, len + growth) == FONT_OK && font_set_px(&altered, 15) == FONT_OK);
    for (uint32_t cp = 0x20u; cp <= 0x7eu; cp++) {
        font_metric m; T(font_glyph_metrics(&altered, cp, &m) == FONT_OK && m.advance == original.advance);
    }
    T(font_monospace(&altered) == FONT_ERR_INIT);
    free(copy); edit_arena_free(&arena);
    puts("font_selection_test: proportional design advances rejected despite equal rounded pixel widths"); return 0;
}

static int hold_present(render_backend *backend, uint32_t frame)
{
    (void)backend; (void)frame; return RENDER_OK;
}
static int settle_glyphs(editor *e)
{
    do { T(editor_step(e, 0) >= 0); sched_yield(); }
    while (editor_get_stats(e).pending || editor_get_stats(e).render_active || font_async_pending(e->fonts));
    return 0;
}
static int selected_pixels(editor *e)
{
    const render_grid *grid = editor_grid(e);
    font_t face = e->font.atlas[e->font.current].selection.face;
    font_t *primary = &face;
    T(primary->data && primary->cell.cell_w == grid->dims.cell_w && primary->cell.cell_h == grid->dims.cell_h);
    edit_arena scratch = {0}; T(edit_arena_init(&scratch, 1u << 20) == 0);
    size_t cell_bytes = (size_t)grid->dims.cell_w * grid->dims.cell_h;
    uint8_t *reference = calloc(cell_bytes, 1); T(reference);
    uint32_t cps[] = {0xe9u, 0x3b1u};
    uint32_t col = layout_gutter_width(&e->lay), row = e->tab_rows;
    for (size_t i = 0; i < sizeof cps / sizeof cps[0]; i++, col++) {
        const render_cell *cell = &grid->cells[(size_t)row * grid->dims.cols + col];
        T(cell->atlas_slot >= LAYOUT_ASCII_GLYPHS && cell->atlas_slot < grid->glyph_count);
        const render_glyph *glyph = &grid->glyphs[cell->atlas_slot];
        T(glyph->page != 0 && glyph->w == grid->dims.cell_w && glyph->h == grid->dims.cell_h);
        const render_atlas_page *page = &grid->pages[glyph->page];
        edit_arena_reset(&scratch); font_bitmap image;
        T(font_raster_glyph(primary, cps[i], &scratch, &image) == FONT_OK);
        memset(reference, 0, cell_bytes);
        T(font_place_in_cell(primary, &image, reference, grid->dims.cell_w, grid->dims.cell_h) == FONT_OK);
        for (uint32_t y = 0; y < glyph->h; y++)
            T(!memcmp(reference + (size_t)y * glyph->w,
                      page->pixels + (size_t)(glyph->y + y) * page->stride + glyph->x, glyph->w));
    }
    free(reference); edit_arena_free(&scratch);
    T(render_grid_validate(grid) == RENDER_OK); return 0;
}
typedef struct glyph_hold { _Atomic bool started, release; } glyph_hold;
static void hold_glyph_worker(work_ctx *ctx)
{
    glyph_hold *hold = ctx->arg; atomic_store(&hold->started, true);
    while (!atomic_load(&hold->release) && !work_should_stop(ctx)) sched_yield();
}
static int selected_zoom_unicode(double dpi)
{
    render_backend backend = {0}; T(render_null_backend(&backend) == 0);
    editor_config cfg = {.font_dpi = dpi, .cols = 40, .rows = 8,
        .initial = (const uint8_t *)"éαé", .initial_len = sizeof "éαé" - 1u};
    editor *e = NULL; T(editor_open(&e, &cfg, &backend) == 0); T(settle_glyphs(e) == 0);
    T(editor_font_request(e, "DejaVu Sans Mono:style=Bold", NULL) == 0);
    T(prepared(e) == 0); /* fontconfig setup is outside keyboard ingress */
    if (edit_malloc_guard_active()) edit_malloc_guard_begin();
    T(settle_glyphs(e) == 0 && e->stats.font_error == FONT_OK);
    size_t selection_allocations = edit_malloc_guard_active() ? edit_malloc_guard_end() : 0;
    T(selection_allocations == 0); T(selected_pixels(e) == 0);
    const unsigned char *custom = e->font.atlas[e->font.current].selection.face.data;
    size_t embedded_len; T(custom != font_embedded_data(&embedded_len));
    uint32_t default_px = e->font.default_px;
    uint32_t symbols[] = {XKB_KEY_equal, XKB_KEY_minus, XKB_KEY_equal, XKB_KEY_0};
    for (size_t i = 0; i < sizeof symbols / sizeof symbols[0]; i++) {
        plat_event key = {.kind = PLAT_EV_KEY, .press = true, .keysym = symbols[i], .mods = PLAT_MOD_CTRL};
        if (edit_malloc_guard_active()) edit_malloc_guard_begin();
        T(editor_inject(e, &key) == 0); T(settle_glyphs(e) == 0);
        size_t allocations = edit_malloc_guard_active() ? edit_malloc_guard_end() : 0;
        T(allocations == 0); T(selected_pixels(e) == 0);
        T(e->font.atlas[e->font.current].selection.files.base != NULL);
    }
    T(e->font.applied_px == default_px && e->stats.font_changes == 1u);
    /* Two family swaps can leave Unicode jobs in both versions. A third must
     * preserve their font bytes until physical completion and mailbox drain,
     * while ordinary input can still submit using the adopted owner. */
    glyph_hold hold = {0};
    work_handle blocker = work_submit(&e->pool, (work_job){hold_glyph_worker, &hold, 0, WORK_BULK});
    T(blocker.epoch); while (!atomic_load(&hold.started)) sched_yield();
    T(editor_font_request(e, NULL, NULL) == 0); T(adopted(e) == 0); T(font_async_pending(e->fonts));
    T(editor_font_request(e, "DejaVu Sans Mono:style=Bold", NULL) == 0); T(adopted(e) == 0);
    T(font_async_pending(e->fonts));
    const uint8_t *pixels = editor_grid(e)->pages[0].pixels;
    T(editor_font_request(e, NULL, NULL) == 0); T(!e->font.handle.epoch);
    plat_event key = {.kind = PLAT_EV_KEY, .press = true, .keysym = XKB_KEY_a, .utf8_len = 1, .utf8 = {'a'}};
    if (edit_malloc_guard_active()) edit_malloc_guard_begin();
    T(editor_inject(e, &key) == 0);
    while (e->queue_count || e->dirty || e->backend->active) T(editor_step(e, 0) >= 0);
    size_t allocations = edit_malloc_guard_active() ? edit_malloc_guard_end() : 0;
    T(allocations == 0 && e->stats.submitted_sequence == e->stats.input_sequence);
    T(editor_grid(e)->pages[0].pixels == pixels && !e->font.handle.epoch);
    atomic_store(&hold.release, true); T(settle_glyphs(e) == 0);
    T(e->font.applied_generation == e->font.requested_generation);
    T(editor_length(e) == cfg.initial_len + 1u);
    /* Shutdown with a selection job outstanding joins before primary bytes free. */
    T(editor_font_request(e, "DejaVu Sans Mono:style=Bold", NULL) == 0);
    editor_close(e);
    printf("font_selection_test: DPI %.0f selected Unicode pixels, zoom/reset, glyph retirement, runnable input and outstanding selection shutdown passed\n", dpi);
    return 0;
}
int main(void)
{
    trace_init(); (void)trace_thread_register(); T(parsing() == 0); T(rounded_proportional() == 0);
    T(unsetenv("EDIT_FONT_FAMILY") == 0 && unsetenv("EDIT_FONT_FILE") == 0);
    render_backend b = {0}; T(render_null_backend(&b) == 0);
    guard g = {0};
    editor_config cfg = {.cols=32, .rows=8, .max_cols=64, .max_rows=16,
        .hook_ctx=&g, .on_ingress=ingress, .on_submit=submitted};
    editor *e = NULL; T(editor_open(&e, &cfg, &b) == 0); T(settle(e) == 0);
    const uint8_t *embedded = font_ascii_atlas_for_px(15)->pixels;
    T(e->grid.pages[0].pixels == embedded); T(type(e, &g) == 0);
    T(editor_font_request(e, "DejaVu Sans Mono:style=Bold", NULL) == 0);
    T(editor_font_request(e, "monospace", NULL) == EDITOR_MORE);
    T(prepared(e) == 0); T(e->font.atlas[e->font.preparing].selection.result == FONT_OK);
    T(e->grid.pages[0].pixels == embedded); /* mailbox delivery alone cannot swap */
    /* Hold a submitted typing frame through T5/T6. The pending font cannot
     * alter borrowed pixels or force input to wait for its adoption. */
    int (*old_present)(render_backend *, uint32_t) = b.ops.present;
    b.ops.present = hold_present;
    plat_event ev = {.kind=PLAT_EV_KEY, .press=true, .keysym=XKB_KEY_b, .utf8_len=1, .utf8={'b'}};
    T(editor_begin_frame(e) == 0); /* prepared completion during old-frame composition */
    T(editor_get_stats(e).font_pending);
    T(editor_inject(e, &ev) == 0);
    while (!b.active) T(editor_step(e, 0) >= 0);
    T(e->grid.pages[0].pixels == embedded && editor_get_stats(e).font_pending);
    T(editor_get_stats(e).submitted_sequence == editor_get_stats(e).input_sequence);
    T(g.allocations == 0 && !g.active);
    plat_event zoom = {.kind = PLAT_EV_KEY, .press = true, .keysym = XKB_KEY_equal, .mods = PLAT_MOD_CTRL};
    T(editor_inject(e, &zoom) == 0);
    zoom.keysym = XKB_KEY_0; T(editor_inject(e, &zoom) == 0);
    while (e->queue_count) T(editor_step(e, 0) >= 0);
    T(e->font.requested_px == e->font.default_px && editor_get_stats(e).font_pending);
    T(e->grid.pages[0].pixels == embedded);
    uint32_t frame = b.active_frame;
    T(render_backend_signal(&b, RENDER_EVENT_DEVICE_DONE, frame, trace_now_ns()) == 0);
    T(render_backend_signal(&b, RENDER_EVENT_PRESENT_COMPLETE, frame, trace_now_ns()) == 0);
    b.ops.present = old_present;
    T(adopted(e) == 0); T(e->grid.pages[0].pixels != embedded);
    T(e->stats.font_changes == 1 && e->stats.font_error == FONT_OK); /* one requested change */
    T(type(e, &g) == 0);
    T(e->stats.font_changes == 1); /* typing does not relayout for another swap */
    uint8_t text[sizeof "aba" - 1u]; T(editor_read(e, 0, text, sizeof text) == 0 && !memcmp(text, "aba", sizeof text));
    printf("font_selection_test: frame boundary, input sequences, worker identity, mallocs=%zu guard=%s\n", g.allocations,
           edit_malloc_guard_active() ? "active" : "ASan-inert");
    /* Failure must restore the embedded atlas, even after a successful custom face. */
    T(editor_font_request(e, "edit-5ih deliberately absent family", NULL) == 0);
    T(prepared(e) == 0 && e->font.atlas[e->font.preparing].selection.result == FONT_ERR_MISSING);
    T(adopted(e) == 0 && e->grid.pages[0].pixels == embedded);
    T(atomic_load(&e->font_logged)); T(type(e, &g) == 0);
    T(editor_font_request(e, "DejaVu Sans", NULL) == 0);
    T(prepared(e) == 0 && e->font.atlas[e->font.preparing].selection.result == FONT_ERR_INIT);
    T(adopted(e) == 0 && e->grid.pages[0].pixels == embedded);
    T(e->font.atlas[e->font.current].selection.files.base == NULL); /* failed file bytes retired */
    T(editor_font_request(e, NULL, "/missing/edit-5ih.ttf") == 0);
    T(adopted(e) == 0 && e->stats.font_error == FONT_ERR_INIT && e->grid.pages[0].pixels == embedded);
    char bad[] = "/tmp/edit-5ih-bad-XXXXXX"; int bad_fd = mkstemp(bad); T(bad_fd >= 0);
    const char bad_bytes[] = "bad font";
    T(write(bad_fd, bad_bytes, sizeof bad_bytes) == (ssize_t)sizeof bad_bytes && close(bad_fd) == 0);
    T(editor_font_request(e, NULL, bad) == 0);
    T(adopted(e) == 0 && e->stats.font_error == FONT_ERR_ARG && e->grid.pages[0].pixels == embedded);
    T(unlink(bad) == 0);
    /* FILE wins over a missing family, selecting face zero explicitly. */
    T(editor_font_request(e, "absent", "vendor/DejaVuSansMono.ttf#0") == 0);
    T(adopted(e) == 0 && e->stats.font_error == FONT_OK && e->grid.pages[0].pixels != embedded);
    T(e->grid.pages[0].pixels_len == font_ascii_atlas_for_px(15)->pixels_len);
    T(!memcmp(e->grid.pages[0].pixels, embedded, e->grid.pages[0].pixels_len)); /* worker raster matches embedded bake */
    T(type(e, &g) == 0);
    T(editor_font_request(e, NULL, "vendor/DejaVuSansMono.ttf#1") == 0);
    T(adopted(e) == 0 && e->stats.font_error == FONT_ERR_INIT && e->grid.pages[0].pixels == embedded);
    T(editor_font_request(e, NULL, NULL) == 0);
    T(adopted(e) == 0 && e->stats.font_error == FONT_OK && e->grid.pages[0].pixels == embedded);

    char ttc[] = "/tmp/edit-5ih-ttc-XXXXXX", selected[FONT_FALLBACK_PATH_MAX];
    T(ttc_fixture(ttc) == 0); T(snprintf(selected, sizeof selected, "%s#1", ttc) > 0);
    render_dims before = e->grid.dims;
    uint64_t changes = e->stats.font_changes;
    T(editor_font_request(e, NULL, selected) == 0); T(adopted(e) == 0);
    T(e->stats.font_error == FONT_OK && e->stats.font_changes == changes + 1u);
    T(e->font.atlas[e->font.current].selection.face.face_index == 1u);
    T(e->grid.dims.cell_w != before.cell_w);
    T(e->grid.dims.cols == before.cols * before.cell_w / e->grid.dims.cell_w);
    T(e->text_grid.dims.cell_w == e->grid.dims.cell_w && e->lay.wrap_capacity == e->max_rows + 1u); /* 3kk reserves max_rows + 1 wrap rows */
    T(type(e, &g) == 0); T(e->stats.font_changes == changes + 1u);
    T(unlink(ttc) == 0);
    puts("font_selection_test: TTC face selection, geometry recomputation and single adoption passed");
    const char *dump = getenv("FONT_SELECTION_TRACE");
    if (dump) {
        FILE *fp = fopen(dump, "wb"); T(fp);
        T(trace_dump(fp) == 0); T(fclose(fp) == 0);
        puts("font_selection_test: retained mid-typing trace written after input/submit guard closed");
    }
    editor_close(e);
    T(setenv("EDIT_FONT_FAMILY", "DejaVu Sans Mono:style=Bold", 1) == 0);
    T(render_null_backend(&b) == 0); T(editor_open(&e, &cfg, &b) == 0);
    T(adopted(e) == 0 && e->grid.pages[0].pixels != embedded);
    editor_close(e);
    T(setenv("EDIT_FONT_FAMILY", "absent family", 1) == 0);
    T(setenv("EDIT_FONT_FILE", "vendor/DejaVuSansMono.ttf#0", 1) == 0);
    T(render_null_backend(&b) == 0); T(editor_open(&e, &cfg, &b) == 0);
    T(adopted(e) == 0 && e->stats.font_error == FONT_OK && e->grid.pages[0].pixels != embedded);
    editor_close(e);
    T(unsetenv("EDIT_FONT_FAMILY") == 0 && unsetenv("EDIT_FONT_FILE") == 0);
    T(selected_zoom_unicode(96.0) == 0); T(selected_zoom_unicode(243.0) == 0);
    puts("font_selection_test: environment, installed family, failure fallback, proportional rejection, file precedence and restore passed");
    return 0;
}
