/* Headless DirectWrite discovery and R8 raster contract. */
#include "font/font.h"
#include "font/dwrite.h"
#include <windows.h>
#include <stdio.h>
#include <string.h>
#define T(c) do { if (!(c)) { fprintf(stderr, "font_dwrite_test:%d: %s\n", __LINE__, #c); return 1; } } while (0)
static void adopt(const work_msg *msg, void *arg) { (void)font_fallback_event(arg, msg); }
static void adopt_async(const work_msg *msg, void *arg) { (void)font_async_event(arg, msg); }
int main(void)
{
    font_fallback fb = {0};
    work_pool pool;
    T(work_pool_init(&pool, 1, 0) == 0);
    work_handle h = work_submit(&pool, (work_job){font_fallback_job, &fb, 1, WORK_BULK});
    T(h.epoch);
    /* (G) Existing discovery guard: 30 seconds, inherited from POSIX child. */
    uint64_t end = os_now_ns() + UINT64_C(30000000000);
    while (!atomic_load(&fb.done) && os_now_ns() < end) {
        (void)work_mailbox_drain(&pool, adopt, &fb); os_sleep_ns(UINT64_C(1000000));
    }
    T(atomic_load(&fb.done) && fb.worker != os_thread_self());
    T(fb.have_fontconfig && fb.cjk[0] && fb.emoji[0]);
    const char *paths[] = {fb.cjk, fb.emoji};
    uint32_t indices[] = {fb.cjk_index, fb.emoji_index}, cps[] = {0x4e2du, 0x1f600u};
    for (size_t i = 0; i < sizeof paths / sizeof *paths; i++) {
        edit_arena files, a, b;
        os_stat st; T(os_stat_path(paths[i], 0, &st) == OS_OK && st.size <= SIZE_MAX);
        T(edit_arena_init(&files, (size_t)st.size) == 0);
        T(edit_arena_init(&a, 1u << 20) == 0 && edit_arena_init(&b, 1u << 20) == 0);
        size_t len; unsigned char *bytes = font_load_file(paths[i], &files, &len); T(bytes);
        font_t f; T(font_init_index(&f, bytes, len, indices[i]) == FONT_OK);
        /* (G) Baked large size: same px=30 used by font_test raster comparisons. */
        T(font_set_px(&f, 30) == FONT_OK);
        font_bitmap dw, stb, injected, routed, byid;
        uint32_t gid = font_glyph_id(&f, cps[i]); T(gid);
        T(font_dwrite_raster(&f, gid, &a, &dw, 0) == FONT_OK);
        T(font_dwrite_raster(&f, gid, &b, &injected, 1) != FONT_OK);
        T(font_raster_glyph_stb(&f, cps[i], &b, &stb) == FONT_OK);
        T(SetEnvironmentVariableW(L"SUBLIMITE_TEST_NO_DWRITE", L"1"));
        T(font_raster_glyph(&f, cps[i], &b, &injected) == FONT_OK);
        T(injected.w == stb.w && injected.h == stb.h && memcmp(&injected.m, &stb.m, sizeof stb.m) == 0);
        T(memcmp(injected.pixels, stb.pixels, (size_t)stb.w * stb.h) == 0);
        T(SetEnvironmentVariableW(L"SUBLIMITE_TEST_NO_DWRITE", NULL));
        T(font_raster_glyph(&f, cps[i], &a, &routed) == FONT_OK);
        T(font_raster_glyph_id(&f, gid, &a, &byid) == FONT_OK);
        T(routed.w == dw.w && routed.h == dw.h && byid.w == dw.w && byid.h == dw.h);
        T(memcmp(routed.pixels, dw.pixels, (size_t)dw.w * dw.h) == 0);
        T(memcmp(byid.pixels, dw.pixels, (size_t)dw.w * dw.h) == 0);
        uint64_t ds = 0, ss = 0; int partial = 0;
        for (size_t j = 0; j < (size_t)dw.w * dw.h; j++) {
            ds += dw.pixels[j]; partial |= dw.pixels[j] > 0 && dw.pixels[j] < 255;
        }
        for (size_t j = 0; j < (size_t)stb.w * stb.h; j++) ss += stb.pixels[j];
        printf("font_dwrite_test: U+%04x path='%s' index=%u advance=%d/%d box=%d,%d,%u,%u/%d,%d,%u,%u coverage=%llu/%llu\n",
            cps[i], paths[i], indices[i], dw.m.advance, stb.m.advance,
            dw.m.bearing_x, dw.m.bearing_y, dw.w, dw.h,
            stb.m.bearing_x, stb.m.bearing_y, stb.w, stb.h,
            (unsigned long long)ds, (unsigned long long)ss);
        T(ds && ss && partial);
        /* Installed-font filter differences above are observations. The exact
         * analytic placement/pixel oracle lives in font_dwrite_r2_test. */
        T(dw.m.advance == stb.m.advance);
        edit_arena_free(&a); edit_arena_free(&b); edit_arena_free(&files);
    }
    /* Production worker path: discovery, file reads, scratch subarena,
     * rasterisation and mailbox adoption into the async atlas. */
    font_async *async = NULL;
    T(font_async_open(&async, 30, &pool) == FONT_OK);
    const font_ascii_atlas *ascii = font_ascii_atlas_for_px(30);
    render_grid grid; render_cell cells[2]; uint64_t dirty = 0;
    T(render_grid_init(&grid, (render_dims){2, 1, ascii->cell.cell_w, ascii->cell.cell_h},
                       cells, 2, &dirty, 1) == RENDER_OK);
    T(font_async_bind(async, &grid) == FONT_OK);
    const uint8_t *clusters[] = {(const uint8_t *)"中", (const uint8_t *)"😀"};
    size_t lengths[] = {sizeof "中" - 1u, sizeof "😀" - 1u};
    uint32_t slots[2];
    for (size_t i = 0; i < sizeof lengths / sizeof *lengths; i++)
        T(font_async_glyph(async, clusters[i], lengths[i], 2, &slots[i]) == FONT_OK);
    end = os_now_ns() + UINT64_C(30000000000); /* same inherited discovery guard */
    while (!font_async_idle(async) && os_now_ns() < end) {
        font_async_pump(async);
        (void)work_mailbox_drain(&pool, adopt_async, async); os_sleep_ns(UINT64_C(1000000));
    }
    T(font_async_idle(async) && !font_async_pending(async));
    for (size_t i = 0; i < sizeof paths / sizeof *paths; i++) {
        char path[FONT_FALLBACK_PATH_MAX]; uint32_t index;
        T(font_async_scalar_source(async, cps[i], path, sizeof path, &index) == FONT_OK);
        /* The representative Han probe is zh-CN; general scalar lookup uses
         * the user's locale and can legitimately choose a different face. */
        printf("font_dwrite_test: async U+%04x path='%s' index=%u\n", cps[i], path, index);
        T(path[0]);
        if (i) T(!strcmp(path, paths[i]) && index == indices[i]);
        T(font_async_inked(async, slots[i]) && grid.glyphs[slots[i]].page > 0);
    }
    work_pool_shutdown(&pool);
    font_async_close(async);
    puts("font_dwrite_test: discovery, grayscale R8, same-face metrics, routing, injected fallback and async atlas passed");
    return 0;
}
