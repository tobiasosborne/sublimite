/* bench/font_bench.c - P2.3: runtime rasterisation of one glyph at 30 px.
 * Gate: p50 <= 50 us (50000 ns). Exits non-zero on miss. */
#include "base/base.h"
#include "font/font.h"
#include "harness.h"
#include <stdio.h>
#include <stdlib.h>

#define N_OPS 20000u
#define GATE_P50 50000ull

static uint64_t samples_buf[N_OPS];

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
    printf("BENCH name=font_raster n=%zu p50=%llu p99=%llu gate_p50=%llu dropped=%zu\n",
           s.n, (unsigned long long)p50, (unsigned long long)p99,
           (unsigned long long)GATE_P50, s.dropped);
    edit_arena_free(&a);
    free(ttf);
    if (s.dropped || p50 > GATE_P50) return 1;
    return 0;
}
