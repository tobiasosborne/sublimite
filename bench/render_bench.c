#include "render/render.h"
#include "trace/trace.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Geometry and sample count from the signed spec's v1 witnesses. W=10/H=20
 * is the same spec's font send-back sizing example. Three tiles distinguish
 * the one-tile scan from a many-tile scan with the smallest useful batch
 * including an interior queue entry. */
enum { COLS = 360, ORDINARY_ROWS = 120, MAX_ROWS = 300, SAMPLES = 10000,
       W = 10, H = 20, TILES = 3, SHEET = 2, TILE = RENDER_TILE_MAX_SPAN * W * H };
typedef struct bench_view {
    render_offer offer;
    render_strip run;
    render_cell *cells;
    render_tile_view runtime[TILES];
    uint8_t pixels[TILE];
} bench_view;

typedef enum bench_case { BENCH_ROW, BENCH_FULL, BENCH_RUNTIME, BENCH_ONE_TILE, BENCH_MANY_TILES } bench_case;

static render_stamp current(void *ctx) { return ((bench_view *)ctx)->offer.stamp; }
static render_rc run_view(void *ctx, size_t index, render_strip *out)
{
    if (index) return RENDER_INVALID;
    *out = ((bench_view *)ctx)->run;
    return RENDER_OK;
}
static render_rc row_view(void *ctx, uint32_t row, render_cell_span *out)
{
    if (row > MAX_ROWS) return RENDER_INVALID;
    *out = (render_cell_span){((bench_view *)ctx)->cells + (size_t)row * COLS, COLS};
    return RENDER_OK;
}
static render_rc region_view(void *ctx, render_region_view *out)
{
    (void)ctx;
    (void)out;
    return RENDER_INVALID;
}
static render_image_state identity(void *ctx, uint32_t slot, render_image_id *out)
{
    if (slot < SHEET || slot >= SHEET + TILES) return RENDER_IMAGE_VACANT;
    *out = ((bench_view *)ctx)->runtime[slot - SHEET].image;
    return out->version ? RENDER_IMAGE_INK : RENDER_IMAGE_VACANT;
}
static render_image_state directory(void *ctx, render_generation gen, uint32_t slot, render_image_id *out)
{
    return gen.owner == 1 && gen.generation == 1 ? identity(ctx, slot, out) : RENDER_IMAGE_VACANT;
}
static render_rc tile_view(void *ctx, render_image_id id, render_tile_view *out)
{
    if (id.slot < SHEET || id.slot >= SHEET + TILES) return RENDER_INVALID;
    *out = ((bench_view *)ctx)->runtime[id.slot - SHEET];
    return RENDER_OK;
}

static const char *power(void)
{
    FILE *f = fopen("/sys/class/power_supply/BAT0/status", "r");
    if (!f) return "unknown";
    char status[32];
    const char *stamp = "unknown";
    if (fgets(status, sizeof status, f)) {
        if (!strncmp(status, "Discharging", strlen("Discharging"))) stamp = "bat";
        else if (!strncmp(status, "Charging", strlen("Charging")) ||
                 !strncmp(status, "Full", strlen("Full")) ||
                 !strncmp(status, "Not charging", strlen("Not charging"))) stamp = "AC";
    }
    fclose(f);
    return stamp;
}

static void pair(const char *label, uint64_t *samples, size_t count)
{
    uint64_t p50 = trace_fmt_pct(samples, count, 50);
    uint64_t p99 = trace_fmt_pct(samples, count, 99);
    printf(" %s=%.3f/%.3f us", label, (double)p50 / 1000.0, (double)p99 / 1000.0);
}

static void finish(render *r)
{
    const render_notice pump = {.kind = RENDER_PUMP};
    render_rc rc;
    do rc = render_event(r, &pump); while (rc == RENDER_MORE);
    ASSERT(rc == RENDER_OK);
}

static void exercise(uint32_t rows, bench_case kind, uint64_t idle_ns)
{
    size_t count = idle_ns ? 1u : SAMPLES;
    double load = 0;
    int have_load = getloadavg(&load, 1);
    const char *supply = power();
    size_t cells = (size_t)COLS * (rows + 1u);
    base_arena arena;
    /* Two retained/packet cell copies, null's sample records, fixture and
     * memcpy output. Page slack covers atlas, slots, rows and setup. */
    size_t bytes = cells * (4u * sizeof(render_cell) + sizeof(render_sample)) +
        4u * count * sizeof(uint64_t) + 4u * os_page_size();
    ASSERT(base_arena_init(&arena, bytes) == 0);
    bench_view b = {0};
    b.cells = base_arena_alloc(&arena, cells * sizeof *b.cells, _Alignof(render_cell));
    render_cell *copy = base_arena_alloc(&arena, cells * sizeof *copy, _Alignof(render_cell));
    uint64_t *samples = base_arena_alloc(&arena, 4u * count * sizeof *samples, _Alignof(uint64_t));
    ASSERT(b.cells && copy && samples);
    memset(copy, 0, cells * sizeof *copy);
    bool runtime = kind == BENCH_RUNTIME;
    bool full = kind == BENCH_FULL || runtime;
    size_t tiles = kind == BENCH_ONE_TILE ? 1u : kind == BENCH_MANY_TILES ? TILES : 0u;
    for (size_t i = 0; i < cells; ++i) b.cells[i] = (render_cell){1, runtime ? SHEET : 0, RENDER_RGB_MASK, 0, 0, 0};
    render_tile_view markers[SHEET];
    render_sheet sheet = {.tiles = markers, .count = SHEET};
    for (uint32_t i = 0; i < SHEET; ++i) {
        markers[i] = (render_tile_view){.image = {i, 1}, .page = b.pixels, .length = TILE,
            .stride = RENDER_TILE_MAX_SPAN * W, .page_width = RENDER_TILE_MAX_SPAN * W,
            .page_height = H, .span = i + 1u, .rect = {0, 0, (i + 1u) * W, H}};
        sheet.marker[i] = markers[i].image;
    }
    for (uint32_t i = 0; i < TILES; ++i) {
        b.runtime[i] = markers[1];
        b.runtime[i].image = (render_image_id){SHEET + i, 1};
        if (runtime) {
            b.runtime[i].span = 1;
            b.runtime[i].rect.width = W;
        }
    }
    render_binding binding = {.surface = 1, .geometry = 1, .font = {1, 1},
        .client_width = COLS * W, .client_height = rows * H, .cols = COLS, .rows = rows,
        .cell_width = W, .cell_height = H, .target_stride = COLS * W * sizeof(uint32_t),
        .sheet = {0, SHEET}, .runtime = {SHEET, TILES}, .overscan = 1};
    render_setup setup = {.driver = render_driver_null(), .recovery_slots = TILES,
        .directory_ctx = &b, .directory = directory,
        .limits = {.client_width = binding.client_width, .client_height = binding.client_height,
            .cols = COLS, .rows = rows, .cell_width = W, .cell_height = H, .cells = cells,
            .target_bytes = binding.target_stride * binding.client_height,
            .sheet_slots = SHEET, .runtime_slots = TILES, .atlas_bytes = (SHEET + TILES) * TILE,
            .tile_descriptors = TILES, .tile_bytes = TILES * TILE}};
    render_source src = {&b, current, run_view, row_view, region_view, identity, tile_view};
    render *r;
    ASSERT(render_open(&arena, &setup, &r) == RENDER_OK);
    ASSERT(render_bind(r, &binding, &sheet, NULL) == RENDER_OK);
    b.offer = (render_offer){
        .stamp = {.surface = 1, .geometry = 1, .font = {1, 1}, .buffer = 1,
            .layout_owner = 1, .layout_serial = 1, .paint = 1, .composition = 1},
        .mode = RENDER_FULL, .cols = COLS, .rows = rows, .run_count = 1,
        .overscan_dirty = true, .region = RENDER_REMOVE,
        .scroll = {.clip = {0, 0, COLS * W, rows * H}, .source_rows = rows, .overscan_row = rows}
    };
    b.run = (render_strip){0, rows};
    if (runtime) {
        render_image_changed(r, binding.font, SHEET);
        b.offer.tile_descriptors = 1;
        b.offer.tile_bytes = TILE;
    }
    render_frame_id id;
    ASSERT(render_submit(r, &b.offer, &src, &id, NULL) == RENDER_OK);
    finish(r);
    b.offer.mode = full ? RENDER_FULL : RENDER_PATCH;
    b.offer.run_count = tiles ? 0u : 1u;
    b.offer.overscan_dirty = full;
    b.offer.region = full ? RENDER_REMOVE : RENDER_KEEP;
    b.offer.tile_descriptors = tiles;
    b.offer.tile_bytes = tiles * TILE;
    if (!full) b.run.row_count = 1;
    size_t payload = (tiles ? 0u : full ? cells : COLS) * sizeof(render_cell);
    render_counters stats;
    for (size_t i = 0; i < count; ++i) {
        b.offer.stamp.base = id;
        ++b.offer.stamp.composition;
        for (uint32_t k = 0; k < tiles; ++k) {
            ++b.runtime[k].image.version;
            render_image_changed(r, binding.font, b.runtime[k].image.slot);
        }
        if (idle_ns) os_sleep_ns(idle_ns);
        uint64_t t0 = os_now_ns();
        ASSERT(render_submit(r, &b.offer, &src, &id, NULL) == RENDER_OK);
        uint64_t t1 = os_now_ns();
        finish(r);
        uint64_t t2 = os_now_ns();
        render_stats(r, &stats);
        if (payload) {
            uint64_t t3 = os_now_ns();
            memcpy(copy, b.cells, payload);
            __asm__ volatile("" : : "r"(copy) : "memory");
            samples[3u * count + i] = os_now_ns() - t3;
        }
        samples[i] = t1 - t0;
        samples[count + i] = t2 - t0;
        samples[2u * count + i] = stats.draw.scan_ns;
    }
    const char *name = full ? (runtime ? "full-runtime" : "full-sheet") :
        (tiles ? "tile-only" : "one-row");
    printf("%ux%u+overscan %s tiles=%zu cells=%zu scan=%zu copies=%zu B n=%zu idle_ns=%llu [%s]",
           COLS, rows, name, tiles, stats.cells, stats.draw.scan_cells, payload + stats.tile_bytes,
           count, (unsigned long long)idle_ns, supply);
    if (have_load == 1) printf(" load1=%.2f", load);
    pair("submit", samples, count);
    pair("submit+null-retire", samples + count, count);
    pair("scan", samples + 2u * count, count);
    if (payload) pair("memcpy", samples + 3u * count, count);
    putchar('\n');
    ASSERT(render_close(r) == RENDER_OK);
    base_arena_free(&arena);
}

int main(int argc, char **argv)
{
    uint64_t idle_ns = 0;
    if (argc == 2) {
        char *end;
        unsigned long long ms = strtoull(argv[1], &end, 10);
        if (!*argv[1] || *end || ms > UINT64_MAX / 1000000u) return 2;
        idle_ns = ms * 1000000u; /* SI conversion from caller-supplied ms */
    } else if (argc != 1) return 2;
    puts("p50/p99 (M); signed UI slice 500 us (G); whole operation uses synthetic null");
    const uint32_t fixtures[] = {ORDINARY_ROWS, MAX_ROWS};
    for (size_t i = 0; i < sizeof fixtures / sizeof fixtures[0]; ++i) {
        exercise(fixtures[i], BENCH_ROW, idle_ns);
        exercise(fixtures[i], BENCH_FULL, idle_ns);
        exercise(fixtures[i], BENCH_RUNTIME, idle_ns);
        exercise(fixtures[i], BENCH_ONE_TILE, idle_ns);
        exercise(fixtures[i], BENCH_MANY_TILES, idle_ns);
    }
    return 0;
}
