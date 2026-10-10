/* bench/font_bench.c - P2.3 / P2.3b: runtime rasterisation of one glyph at 30 px.
 * Raster regression budget: p50/p99 <= 50 us; cold/cached lookup slices
 * p50/p99 <= 0.5 ms. Timing misses fail even though shared-box runs are TRACK.
 * Discovery time is TRACK only. CJK is SKIP if unavailable. Pure --self-check
 * paths exercise failure predicates without running a timing campaign. */
#include "base/base.h"
#include "font/font.h"
#include "utf8/utf8.h"
#include "harness.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <poll.h>

#define N_OPS 20000u
#define GATE_P50 50000ull

static uint64_t samples_buf[N_OPS];

typedef struct unicode_setup {
    font_fallback *fb;
    font_t *primary;
    font_family family;
    edit_arena files;
    int result, ready;
} unicode_setup;
static void unicode_ready(const work_msg *msg, void *arg)
{
    unicode_setup *u = arg;
    if (font_fallback_event(u->fb, msg)) u->ready = 1;
}

/* Pure acceptance predicates: the self-check path does no measurements. */
static int raster_pass(int rc, const font_bitmap *b, int guard, size_t mallocs)
{
    if (rc != FONT_OK || !b || !b->pixels || !b->w || !b->h || !guard || mallocs) return 0;
    uint8_t ink = 0;
    for (size_t i = 0; i < (size_t)b->w * b->h; i++) ink |= b->pixels[i];
    return ink != 0;
}
static int timing_pass(uint64_t p50, uint64_t p99, unsigned row)
{
    uint64_t gate = row == 0 ? GATE_P50 : UINT64_C(500000);
    return p50 <= gate && p99 <= gate;
}
static const char *power_from_raw(const char *raw)
{
    return bench__tag_from_power(bench__power_from_status(raw));
}
static int self_check(const char *which)
{
    font_bitmap bitmap = {0};
    if (strcmp(which, "9") == 0) {
        if (raster_pass(FONT_ERR_NOMEM, &bitmap, 1, 0) || raster_pass(FONT_OK, &bitmap, 0, 0)) {
            puts("review §9 RED: failed/empty raster or inactive guard accepted"); return 1;
        }
        uint8_t pixel = 255; bitmap.pixels = &pixel; bitmap.w = bitmap.h = 1;
        if (!raster_pass(FONT_OK, &bitmap, 1, 0) || raster_pass(FONT_OK, &bitmap, 0, 0) || raster_pass(FONT_ERR_NOMEM, &bitmap, 1, 0)) return 1;
        puts("review §9 GREEN: raster failures, empty pixels and inactive guard rejected");
    } else if (strcmp(which, "10") == 0) {
        if (timing_pass(1, 1000000, 0) || timing_pass(1000000, 1000000, 1) || timing_pass(1000000, 1000000, 2)) {
            puts("review §10 RED: slow tail/cold slice/cached typing accepted"); return 1;
        }
        puts("review §10 GREEN: synthetic tail/cold/cached timing misses rejected");
    } else if (strcmp(which, "15") == 0) {
        if (strcmp(power_from_raw(""), "[unknown]") || strcmp(power_from_raw("unexpected"), "[unknown]") || strcmp(power_from_raw(NULL), "[unknown]")) {
            puts("review §15 RED: unknown power classified AC"); return 1;
        }
        if (strcmp(power_from_raw("Charging\n"), "[AC]") || strcmp(power_from_raw("Full"), "[AC]") ||
            strcmp(power_from_raw("Not charging"), "[AC]") || strcmp(power_from_raw("Discharging\n"), "[bat]")) return 1;
        puts("review §15 GREEN: unknown power stays unknown");
    } else return 1;
    return 0;
}

static int print_row(const char *name, const bench_samples *s, unsigned row)
{
    uint64_t gate = row == 0 ? GATE_P50 : UINT64_C(500000);
    int result = bench_report(name, s, gate, gate);
    if (!timing_pass(bench_p50(s), bench_p99(s), row)) result = 1;
    return result;
}

int main(int argc, char **argv)
{
    if (argc == 3 && strcmp(argv[1], "--self-check") == 0) return self_check(argv[2]);
    FILE *fp = fopen("vendor/DejaVuSansMono.ttf", "rb");
    if (!fp) { fprintf(stderr, "font_bench: no vendor/DejaVuSansMono.ttf\n"); return 1; }
    fseek(fp, 0, SEEK_END);
    long n = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    unsigned char *ttf = malloc((size_t)n); /* bench setup, untimed */
    if (!ttf || fread(ttf, 1, (size_t)n, fp) != (size_t)n) return 1;
    fclose(fp);

    font_t f;
    EDIT_ASSERT(font_init(&f, ttf, (size_t)n) == FONT_OK);
    EDIT_ASSERT(font_set_px(&f, 30) == FONT_OK);
    edit_arena a;
    EDIT_ASSERT(edit_arena_init(&a, 1u << 20) == 0);

    bench_samples s;
    bench_samples_init(&s, samples_buf, N_OPS);
    volatile uint32_t sink = 0;
    int failed = 0, raster_failures = 0, raster_valid = 0;
    {   /* warm up, then law-2 gate: zero libc mallocs across the whole timed loop */
        edit_arena_mark_t wm = edit_arena_mark(&a);
        font_bitmap wb;
        int rc = font_raster_glyph(&f, 0xE9u, &a, &wb);
        raster_valid = raster_pass(rc, &wb, edit_malloc_guard_active(), 0);
        edit_arena_reset_to_mark(&a, wm);
    }
    if (edit_malloc_guard_active()) edit_malloc_guard_begin();
    for (unsigned i = 0; i < N_OPS; i++) {
        edit_arena_mark_t mk = edit_arena_mark(&a);
        font_bitmap b;
        uint64_t t0 = bench_now_ns();
        int r = font_raster_glyph(&f, 0xE9u, &a, &b); /* e-acute: CJK is absent from DejaVu */
        (void)bench_add(&s, bench_now_ns() - t0);
        sink += b.w;
        raster_failures += !raster_pass(r, &b, edit_malloc_guard_active(), 0);
        edit_arena_reset_to_mark(&a, mk);
    }
    size_t mallocs = 0;
    if (edit_malloc_guard_active()) mallocs = edit_malloc_guard_end();
    uint64_t p50 = bench_p50(&s), p99 = bench_p99(&s);
    printf("BENCH name=font_raster_mallocs n=%u value=%zu gate=0 guard=%d tag=(G)(M)%s TRACK\n",
           N_OPS, mallocs, edit_malloc_guard_active() ? 1 : 0, bench_evidence_tag());
    failed |= print_row("font_raster_TRACK", &s, 0);
    printf("BENCH name=font_raster_correctness failures=%d nonempty=%d guard=%d pass=%d\n",
           raster_failures, raster_valid, edit_malloc_guard_active(), raster_failures == 0 && raster_valid);
    failed |= raster_failures != 0 || !raster_valid || mallocs != 0;


    /* Tracked: real worker discovery and font-file loading; never timed as typing. */
    static font_fallback fb;
    unicode_setup u = {0}; u.fb = &fb; u.primary = &f;
    EDIT_ASSERT(edit_arena_init(&u.files, 64u << 20) == 0);
    work_pool *pool = aligned_alloc(_Alignof(work_pool), sizeof *pool);
    EDIT_ASSERT(pool && work_pool_init(pool, 1, 0) == 0);
    EDIT_ASSERT(work_submit(pool, (work_job){font_fallback_job, u.fb, 1, WORK_BULK}).epoch != 0);
    struct pollfd fd = {work_pool_eventfd(pool), POLLIN, 0};
    while (!u.ready) {
        EDIT_ASSERT(poll(&fd, 1, 10000) > 0);
        (void)work_mailbox_drain(pool, unicode_ready, &u);
    }
    work_pool_shutdown(pool); free(pool);
    u.result = font_family_load(&u.family, u.primary, u.fb, &u.files);
    EDIT_ASSERT(u.result == FONT_OK);
    printf("BENCH name=font_fallback_discover n=1 ns=%llu gate=none fontconfig=%d cjk=%s emoji=%s tag=(M)%s TRACK\n",
           (unsigned long long)fb.elapsed_ns, fb.have_fontconfig,
           fb.cjk[0] ? fb.cjk : "none", fb.emoji[0] ? fb.emoji : "none", bench_evidence_tag());

    /* Tracked: real CJK glyph through the fallback font. */
    edit_arena fa = {0};
    size_t cjk_len = 0;
    unsigned char *cjk = NULL;
    if (fb.cjk[0] && edit_arena_init(&fa, (size_t)128u << 20) == 0)
        cjk = font_load_file(fb.cjk, &fa, &cjk_len);
    font_t cf;
    if (cjk && font_init_index(&cf, cjk, cjk_len, fb.cjk_index) == FONT_OK && font_set_px(&cf, 30) == FONT_OK) {
        bench_samples cs;
        bench_samples_init(&cs, samples_buf, N_OPS);
        int bad = 0;
        for (unsigned i = 0; i < N_OPS; i++) {
            edit_arena_mark_t mk = edit_arena_mark(&a);
            font_bitmap b;
            uint64_t t0 = bench_now_ns();
            int r = font_raster_glyph(&cf, 0x4E2Du, &a, &b);
            (void)bench_add(&cs, bench_now_ns() - t0);
            bad |= r != FONT_OK || b.w == 0;
            sink += b.w;
            edit_arena_reset_to_mark(&a, mk);
        }
        failed |= print_row("font_raster_cjk_4E2D_TRACK", &cs, 0);
        if (bad) { fprintf(stderr, "font_bench: CJK raster failed\n"); return 1; }
    } else {
        printf("BENCH name=font_raster_cjk_4E2D SKIP (no fallback font found)\n");
    }
    edit_arena_free(&fa);
    edit_arena storage;
    EDIT_ASSERT(edit_arena_init(&storage, 8u << 20) == 0);
    font_cache cache;
    EDIT_ASSERT(font_cache_init(&cache, &u.family, &storage, 2, 1024, 65536, 1u << 20) == FONT_OK);
    bench_samples pressure; bench_samples_init(&pressure, samples_buf, N_OPS);
    for (uint32_t cp = 0xf0000; cp < 0xf2000; cp++) {
        uint8_t bytes[4]; size_t len = utf8_encode(cp, bytes); uint32_t slot;
        uint64_t t0 = bench_now_ns(); int rc = font_cache_glyph(&cache, bytes, len, 1, &slot);
        (void)bench_add(&pressure, bench_now_ns() - t0);
        failed |= rc != FONT_ERR_MISSING;
    }
    failed |= print_row("font_cache_negative_pressure_TRACK", &pressure, 1);
    bench_samples varied; bench_samples_init(&varied, samples_buf, N_OPS);
    for (uint32_t i = 0; i < 256; i++) {
        uint8_t bytes[32]; size_t len = 0; bytes[len++] = 'e';
        for (uint32_t j = 0; j < 4; j++) len += utf8_encode((i >> (2u * j)) & 1u ? 0x301u : 0x308u, bytes + len);
        len += utf8_encode(0xe0100u + i, bytes + len); /* distinct invisible selectors */
        uint32_t slot; int rc;
        do { uint64_t t0 = bench_now_ns(); rc = font_cache_glyph(&cache, bytes, len, 1, &slot);
            (void)bench_add(&varied, bench_now_ns() - t0);
        } while (rc == FONT_MORE);
        failed |= rc != FONT_OK;
    }
    failed |= print_row("font_cluster_varied_cold_TRACK", &varied, 1);
    uint8_t long_cluster[FONT_CLUSTER_MAX_BYTES]; long_cluster[0] = 'e';
    for (size_t i = 1; i + 1 < sizeof long_cluster; i += 2) { long_cluster[i] = 0xcc; long_cluster[i + 1] = 0x81; }
    bench_samples slices; bench_samples_init(&slices, samples_buf, N_OPS);
    for (size_t len = sizeof long_cluster - 3u; len <= sizeof long_cluster - 1u; len += 2) {
        uint32_t slot; int rc;
        do { uint64_t t0 = bench_now_ns(); rc = font_cache_glyph(&cache, long_cluster, len, 1, &slot);
            (void)bench_add(&slices, bench_now_ns() - t0);
        } while (rc == FONT_MORE);
        failed |= rc != FONT_OK;
    }
    failed |= print_row("font_cluster_long_extension_slice_TRACK", &slices, 1);
    const uint8_t *clusters[] = {(const uint8_t *)"e\xcc\x81", (const uint8_t *)"\xe4\xb8\xad",
                                (const uint8_t *)"\xf0\x9f\x98\x80"};
    const size_t lengths[] = {3, 3, 4}; const uint32_t widths[] = {1, 2, 2};
    bench_samples cold; bench_samples_init(&cold, samples_buf, N_OPS);
    for (size_t i = 0; i < 3; i++) {
        uint32_t slot; int rc;
        do {
            uint64_t t0 = bench_now_ns();
            rc = font_cache_glyph(&cache, clusters[i], lengths[i], widths[i], &slot);
            (void)bench_add(&cold, bench_now_ns() - t0);
        } while (rc == FONT_MORE);
        failed |= rc != FONT_OK && rc != FONT_ERR_MISSING;
    }
    failed |= print_row("font_cluster_cold_TRACK", &cold, 1);
    bench_samples warm; bench_samples_init(&warm, samples_buf, N_OPS);
    size_t storage_used = storage.used;
    if (edit_malloc_guard_active()) edit_malloc_guard_begin();
    for (size_t i = 0; i < N_OPS; i++) {
        uint32_t slot = 0; size_t k = i % 3;
        uint64_t t0 = bench_now_ns();
        int rc = font_cache_glyph(&cache, clusters[k], lengths[k], widths[k], &slot);
        (void)bench_add(&warm, bench_now_ns() - t0);
        failed |= rc != FONT_OK && rc != FONT_ERR_MISSING;
        sink += slot;
    }
    size_t cache_mallocs = edit_malloc_guard_active() ? edit_malloc_guard_end() : 0;
    failed |= cache_mallocs != 0 || storage.used != storage_used;
    failed |= print_row("font_cluster_cached_TRACK", &warm, 2);
    printf("BENCH name=font_cluster_mallocs n=%u value=%zu gate=0 guard=%d tag=(G)(M)%s TRACK\n",
           N_OPS, cache_mallocs, edit_malloc_guard_active() ? 1 : 0, bench_evidence_tag());
    edit_arena_free(&storage); edit_arena_free(&u.files);
    edit_arena_free(&a);
    free(ttf);
    if (s.dropped || !timing_pass(p50, p99, 0) || !edit_malloc_guard_active() || cache_mallocs) failed = 1;
    return failed;
}
