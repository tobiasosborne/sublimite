/* P4.11: fixed-storage Unicode cluster atlas. No discovery/I/O on cache calls. */
#include "font/font.h"
#include "utf8/utf8.h"
#include <string.h>

int font_cluster_ignorable(uint32_t cp)
{
    return cp == 0x200cu || cp == 0x200du ||
           (cp >= 0xfe00u && cp <= 0xfe0fu) ||
           (cp >= 0xe0100u && cp <= 0xe01efu) ||
           (cp >= 0xe0020u && cp <= 0xe007fu);
}

int font_family_load(font_family *family, const font_t *primary,
                     const font_fallback *fb, edit_arena *arena)
{
    if (!family || !primary || !primary->atlas || !arena ||
        (fb && atomic_load_explicit(&fb->done, memory_order_acquire) != 1u))
        return FONT_ERR_ARG;
    font_t embedded = *primary;
    memset(family, 0, sizeof *family);
    family->faces[0] = embedded;
    family->count = 1;
    if (!fb) return FONT_OK;
    const char *paths[] = {fb->cjk, fb->emoji};
    uint32_t indices[] = {fb->cjk_index, fb->emoji_index};
    int result = FONT_OK;
    for (size_t i = 0; i < 2; i++) {
        if (!paths[i][0]) continue;
        edit_arena_mark_t mark = edit_arena_mark(arena);
        size_t len = 0;
        unsigned char *bytes = font_load_file(paths[i], arena, &len);
        font_t face;
        int rc = bytes ? font_init_index(&face, bytes, len, indices[i]) : FONT_ERR_NOMEM;
        if (rc == FONT_OK) rc = font_set_px(&face, embedded.px);
        if (rc == FONT_OK) family->faces[family->count++] = face;
        else { edit_arena_reset_to_mark(arena, mark); result = rc; }
    }
    return result;
}

int font_cache_init(font_cache *cache, font_family *family, edit_arena *arena,
                    uint32_t max_pages, size_t entries, size_t key_bytes,
                    size_t scratch_bytes)
{
    if (!cache || !family || !arena || family->count == 0 ||
        family->count > FONT_FAMILY_MAX_FACES || !family->faces[0].atlas ||
        !max_pages || max_pages > FONT_ATLAS_MAX_PAGES || !entries ||
        entries > UINT32_MAX - 95u || !key_bytes || !scratch_bytes ||
        entries > SIZE_MAX / sizeof(font_cache_entry) ||
        entries + 95u > SIZE_MAX / sizeof(render_glyph)) return FONT_ERR_ARG;
    font_cell cell = family->faces[0].cell;
    if (!cell.cell_w || !cell.cell_h || cell.cell_w > FONT_ATLAS_PAGE_DIM / 2u ||
        cell.cell_h > FONT_ATLAS_PAGE_DIM) return FONT_ERR_ARG;
    edit_arena_mark_t mark = edit_arena_mark(arena);
    memset(cache, 0, sizeof *cache);
    cache->family = family; cache->cell = cell;
    cache->capacity = entries; cache->key_capacity = key_bytes;
    cache->entries = edit_arena_alloc(arena, entries * sizeof *cache->entries, _Alignof(font_cache_entry));
    cache->glyphs = edit_arena_alloc(arena, (entries + 95u) * sizeof *cache->glyphs, _Alignof(render_glyph));
    cache->keys = edit_arena_alloc(arena, key_bytes, 1);
    cache->image = edit_arena_alloc(arena, (size_t)cell.cell_w * 2u * cell.cell_h, 1);
    cache->scratch.base = edit_arena_alloc(arena, scratch_bytes, 16);
    cache->scratch.size = scratch_bytes;
    if (!cache->entries || !cache->glyphs || !cache->keys || !cache->image || !cache->scratch.base) goto nomem;
    memset(cache->entries, 0, entries * sizeof *cache->entries);
    const font_ascii_atlas *a = family->faces[0].atlas;
    cache->pages[0] = (render_atlas_page){a->pixels, a->pixels_len,
        (size_t)cell.cell_w * 95u, cell.cell_w * 95u, cell.cell_h};
    for (uint32_t i = 0; i < 95u; i++)
        cache->glyphs[i] = (render_glyph){i + 0x20u, 0, i * cell.cell_w, 0, cell.cell_w, cell.cell_h};
    cache->glyph_count = 95u;
    for (uint32_t i = 0; i < max_pages; i++) {
        size_t bytes = (size_t)FONT_ATLAS_PAGE_DIM * FONT_ATLAS_PAGE_DIM;
        uint8_t *pixels = edit_arena_alloc(arena, bytes, 16);
        if (!pixels) goto nomem;
        /* Pre-touch at INIT; opening a new shelf never faults a fresh page. */
        memset(pixels, 0, bytes);
        cache->pages[i + 1u] = (render_atlas_page){pixels, bytes, FONT_ATLAS_PAGE_DIM,
                                               FONT_ATLAS_PAGE_DIM, FONT_ATLAS_PAGE_DIM};
    }
    font_atlas_init(&cache->atlas, max_pages);
    return FONT_OK;
nomem:
    edit_arena_reset_to_mark(arena, mark);
    memset(cache, 0, sizeof *cache);
    return FONT_ERR_NOMEM;
}

int font_cache_bind(font_cache *cache, render_grid *grid)
{
    if (!cache || !cache->family || !grid || grid->dims.cell_w != cache->cell.cell_w ||
        grid->dims.cell_h != cache->cell.cell_h) return FONT_ERR_ARG;
    cache->grid = grid;
    grid->pages = cache->pages; grid->page_count = cache->atlas.npages + 1u;
    grid->glyphs = cache->glyphs; grid->glyph_count = cache->glyph_count;
    return FONT_OK;
}

/* Coverage union preserves the base where mark bitmaps contain zeroes. */
static void composite(font_cache *cache, const font_bitmap *b, uint32_t width, int32_t pen)
{
    uint32_t stride = cache->cell.cell_w * width;
    for (uint32_t y = 0; y < b->h; y++) {
        int64_t cy = (int64_t)cache->cell.ascent + b->m.bearing_y + y;
        if (cy < 0 || cy >= cache->cell.cell_h) continue;
        for (uint32_t x = 0; x < b->w; x++) {
            int64_t cx = (int64_t)pen + b->m.bearing_x + x;
            if (cx < 0 || cx >= stride) continue;
            uint8_t *dst = &cache->image[(size_t)cy * stride + (size_t)cx];
            uint8_t src = b->pixels[(size_t)y * b->w + x];
            if (src > *dst) *dst = src;
        }
    }
}

static int compose_cluster(font_cache *cache, const uint8_t *bytes, size_t len, uint32_t width)
{
    memset(cache->image, 0, (size_t)cache->cell.cell_w * width * cache->cell.cell_h);
    int32_t advance = 0;
    for (size_t off = 0; off < len;) {
        utf8_step s = utf8_decode(bytes + off, len - off);
        if (!s.valid || s.cp < 0x20u || s.cp == 0x7fu) return FONT_ERR_ARG;
        off += s.len;
        if (font_cluster_ignorable(s.cp)) continue;
        font_t *face = NULL; font_metric metric;
        for (uint32_t i = 0; i < cache->family->count; i++) {
            int rc = font_glyph_metrics(&cache->family->faces[i], s.cp, &metric);
            if (rc == FONT_OK) { face = &cache->family->faces[i]; break; }
            if (rc != FONT_ERR_MISSING) return rc;
        }
        if (!face) return FONT_ERR_MISSING;
        font_bitmap b;
        edit_arena_reset(&cache->scratch);
        int rc = font_raster_glyph(face, s.cp, &cache->scratch, &b);
        if (rc != FONT_OK) return rc;
        int zero = utf8_cell_width(s.cp) == 0;
        /* Negative-bearing combining outlines are relative to the base's
         * advance; positive-bearing marks (including Mono) are cell-relative.
         * Multiple spacing components (ZWJ/Hangul) overlay at the origin. */
        int32_t pen = zero && b.m.bearing_x < 0 ? advance : 0;
        if (b.pixels) composite(cache, &b, width, pen);
        if (!zero) advance = b.m.advance;
    }
    edit_arena_reset(&cache->scratch);
    return FONT_OK;
}

int font_cache_glyph(void *ctx, const uint8_t *cluster, size_t len,
                     uint32_t width, uint32_t *slot)
{
    font_cache *cache = ctx;
    if (!cache || !cache->family || !cluster || !len || len > FONT_CLUSTER_MAX_BYTES ||
        !slot || width < 1u || width > 2u) return FONT_ERR_ARG;
    uint64_t hash = UINT64_C(14695981039346656037);
    for (size_t i = 0; i < len; i++) hash = (hash ^ cluster[i]) * UINT64_C(1099511628211);
    hash = (hash ^ width) * UINT64_C(1099511628211);
    size_t at = (size_t)(hash % cache->capacity);
    font_cache_entry *entry = NULL;
    for (size_t i = 0; i < cache->capacity; i++) {
        font_cache_entry *e = &cache->entries[at];
        if (!e->len) { entry = e; break; }
        if (e->hash == hash && e->len == len && e->width == width &&
            memcmp(cache->keys + e->key, cluster, len) == 0) {
            cache->hits++;
            if (e->result == FONT_OK) *slot = e->slot;
            return e->result;
        }
        if (++at == cache->capacity) at = 0;
    }
    cache->misses++;
    if (!entry || len > cache->key_capacity - cache->key_used) return FONT_ERR_NOMEM;
    int rc = compose_cluster(cache, cluster, len, width);
    edit_arena_reset(&cache->scratch);
    if (rc != FONT_OK && rc != FONT_ERR_MISSING) return rc;
    uint32_t result_slot = RENDER_NO_SLOT;
    if (rc == FONT_OK) {
        size_t bytes = (size_t)cache->cell.cell_w * width * cache->cell.cell_h;
        uint8_t ink = 0;
        for (size_t i = 0; i < bytes; i++) ink |= cache->image[i];
        if (ink) {
            uint32_t page, x, y, w = cache->cell.cell_w * width, h = cache->cell.cell_h;
            rc = font_atlas_alloc(&cache->atlas, w, h, &page, &x, &y);
            if (rc != FONT_OK) return rc;
            uint8_t *pixels = (uint8_t *)(void *)cache->pages[page + 1u].pixels;
            for (uint32_t row = 0; row < h; row++)
                memcpy(pixels + (size_t)(y + row) * FONT_ATLAS_PAGE_DIM + x,
                       cache->image + (size_t)row * w, w);
            result_slot = (uint32_t)cache->glyph_count++;
            cache->glyphs[result_slot] = (render_glyph){result_slot + 1u, page + 1u, x, y, w, h};
            if (cache->grid) {
                cache->grid->glyph_count = cache->glyph_count;
                cache->grid->page_count = cache->atlas.npages + 1u;
            }
        }
    }
    memcpy(cache->keys + cache->key_used, cluster, len);
    *entry = (font_cache_entry){hash, cache->key_used, (uint32_t)len, width, result_slot, rc};
    cache->key_used += len;
    if (rc == FONT_OK) *slot = result_slot;
    return rc;
}
