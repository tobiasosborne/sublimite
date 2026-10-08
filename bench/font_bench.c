/* bench/font_bench.c - P2.3 / P2.3b: runtime rasterisation of one glyph at 30 px.
 * Gate (font_raster, e-acute): p50 <= 50 us (50000 ns). Exits non-zero on miss.
 * Tracked rows without gates: fallback discovery time, CJK U+4E2D raster via the
 * discovered fallback font (SKIP if none). Evidence tags: (M), battery state
 * stamped at run time from BAT0, "loaded" = other builds/agents running. */
#include "base/base.h"
#include "font/font.h"
#include "harness.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define N_OPS 20000u
#define GATE_P50 50000ull

static uint64_t samples_buf[N_OPS];

static const char *power_state(void)
{
    static char buf[32];
    FILE *fp = fopen("/sys/class/power_supply/BAT0/status", "r");
    if (!fp) return "AC?";
    if (!fgets(buf, sizeof buf, fp)) buf[0] = 0;
    fclose(fp);
    buf[strcspn(buf, "\n")] = 0;
    return strcmp(buf, "Discharging") == 0 ? "bat" : "AC";
}

static void print_row(const char *name, const bench_samples *s, uint64_t p50, uint64_t p99)
{
    printf("BENCH name=%s n=%zu p50=%llu p99=%llu gate=none dropped=%zu tag=(M)[%s] loaded\n",
           name, s->n, (unsigned long long)p50, (unsigned long long)p99, s->dropped,
           power_state());
}

int main(void)
{
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
    for (unsigned i = 0; i < N_OPS; i++) {
        edit_arena_mark_t mk = edit_arena_mark(&a);
        font_bitmap b;
        uint64_t t0 = bench_now_ns();
        int r = font_raster_glyph(&f, 0xE9u, &a, &b); /* e-acute: CJK is absent from DejaVu */
        (void)bench_add(&s, bench_now_ns() - t0);
        sink += b.w;
        (void)r;
        edit_arena_reset_to_mark(&a, mk);
    }
    uint64_t p50 = bench_p50(&s), p99 = bench_p99(&s);
    printf("BENCH name=font_raster n=%zu p50=%llu p99=%llu gate_p50=%llu dropped=%zu tag=(G)(M)[%s] loaded\n",
           s.n, (unsigned long long)p50, (unsigned long long)p99,
           (unsigned long long)GATE_P50, s.dropped, power_state());

    /* Tracked: fallback discovery (fontconfig on this thread, as a worker would). */
    static font_fallback fb;
    font_fallback_discover(&fb, "libfontconfig.so.1");
    printf("BENCH name=font_fallback_discover n=1 ns=%llu gate=none fontconfig=%d cjk=%s emoji=%s tag=(M)[%s] loaded\n",
           (unsigned long long)fb.elapsed_ns, fb.have_fontconfig,
           fb.cjk[0] ? fb.cjk : "none", fb.emoji[0] ? fb.emoji : "none", power_state());

    /* Tracked: real CJK glyph through the fallback font. */
    edit_arena fa;
    size_t cjk_len = 0;
    unsigned char *cjk = NULL;
    if (fb.cjk[0] && edit_arena_init(&fa, (size_t)128u << 20) == 0)
        cjk = font_load_file(fb.cjk, &fa, &cjk_len);
    font_t cf;
    if (cjk && font_init(&cf, cjk, cjk_len) == FONT_OK && font_set_px(&cf, 30) == FONT_OK) {
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
        print_row("font_raster_cjk_4E2D", &cs, bench_p50(&cs), bench_p99(&cs));
        if (bad) { fprintf(stderr, "font_bench: CJK raster failed\n"); return 1; }
    } else {
        printf("BENCH name=font_raster_cjk_4E2D SKIP (no fallback font found)\n");
    }
    edit_arena_free(&a);
    free(ttf);
    if (s.dropped || p50 > GATE_P50) return 1;
    return 0;
}
