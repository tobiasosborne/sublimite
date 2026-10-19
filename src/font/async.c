/* edit-08z: UI requests, worker-only faces/cache, mailbox-only atlas adoption. */
#include "font/font.h"
#include "font/discovery.h"
#include "utf8/utf8.h"
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

/* (P) Inherited raster/GL descriptor ceiling, less baked printable ASCII.
 * These are storage ceilings, not invented latency or memory targets. */
#define ASYNC_ASCII ((size_t)('~' - ' ' + 1))
#define ASYNC_ENTRIES (FONT_RUNTIME_MAX_GLYPHS - ASYNC_ASCII)
#define ASYNC_SCRATCH ((size_t)FONT_ATLAS_PAGE_DIM * FONT_ATLAS_PAGE_DIM)
#define ASYNC_WIDTHS 2u /* (P) utf8's narrow/wide cluster images */
/* (P) ceil((requests + widths) / inherited three-quarter table occupancy). */
#define ASYNC_CACHE_ENTRIES (((ASYNC_ENTRIES + ASYNC_WIDTHS) * 4u + 2u) / 3u)
/* (P) Separate bits for prior-frame borrows and this frame's lookups. */
#define ASYNC_PRIOR_PIN 1u
#define ASYNC_CURRENT_PIN 2u
/* (P) Wide requests consume two narrow tiles; an odd-width row can waste
 * at most one trailing tile per request. Bound occupancy by that sum. */
#define ASYNC_TILES (ASYNC_ENTRIES * (ASYNC_WIDTHS + 1u))

typedef struct async_request {
    struct font_async *owner;
    uint64_t hash;
    size_t key, len, tile;
    size_t previous, following;
    uint32_t width, index;
    int result;
    bool inked; /* edit-2ghs: false once the settled image proved blank */
} async_request;
typedef struct async_face {
    font_t font;
    edit_arena arena;
    char path[FONT_FALLBACK_PATH_MAX];
    uint32_t index;
    int result;
} async_face;
typedef struct async_scalar { uint32_t cp, face; int result; } async_scalar;
typedef struct async_result { uint32_t request; int result; render_glyph glyph; } async_result;
_Static_assert(sizeof(async_result) <= WORK_MSG_DATA, "glyph result fits work mailbox");

struct font_async {
    work_pool *pool;
    work_handle active;
    work_handle retired;
    edit_arena ui, storage;
    font_family family;
    font_cache cache;
    font_fallback discovery;
    async_request *requests;
    async_face *faces;
    async_scalar *scalars;
    uint32_t *table;
    uint8_t *keys;
    uint8_t *retained;
    uint8_t *occupied;
    render_glyph *glyphs;
    render_atlas_page pages[FONT_ATLAS_MAX_PAGES + 1u];
    edit_arena pixels;
    size_t count, next, pending, page_count, tiles, per_page, tile_cursor;
    size_t oldest, newest, vacant;
    bool retaining;
    /* Exclusively worker-owned until shutdown. */
    size_t face_count, scalar_count;
    /* edit-6e2: faces[0..chain_count) are the resolved font.fallbacks chain,
     * in order; chain is the text they were resolved from. */
    size_t chain_count;
    uint32_t chain_skipped;
    char chain[FONT_CHAIN_TEXT_MAX];
    work_ctx *worker_ctx;
    uint32_t px;
    font_ascii_atlas ascii;
    font_cell max_cell;
    bool em;
    int init_result;
};

void font_async_limits(uint32_t *pages, size_t *glyphs, size_t *pixels, uint32_t px)
{
    const font_ascii_atlas *a = font_ascii_atlas_for_px(px);
    font_async_atlas_limits(pages, glyphs, pixels, a ? a->pixels_len : 0);
}

void font_async_atlas_limits(uint32_t *pages, size_t *glyphs, size_t *pixels, size_t ascii_bytes)
{
    *pages = FONT_ATLAS_MAX_PAGES + 1u;
    *glyphs = ASYNC_ASCII + ASYNC_ENTRIES;
    *pixels = ascii_bytes + FONT_ATLAS_MAX_PAGES * ASYNC_SCRATCH;
}

int font_async_open(font_async **out, uint32_t px, work_pool *pool)
{
    const font_ascii_atlas *a = font_ascii_atlas_for_px(px);
    if (!a) return FONT_ERR_ARG;
    return font_async_open_atlas(out, a, false, a->cell, pool);
}

int font_async_open_atlas(font_async **out, const font_ascii_atlas *a, bool em,
                          font_cell max_cell, work_pool *pool)
{
    if (!out || !pool || !a || !a->pixels || !a->px || !a->cell.cell_w || !a->cell.cell_h ||
        a->cell.cell_w > max_cell.cell_w || a->cell.cell_h > max_cell.cell_h ||
        max_cell.cell_w > FONT_ATLAS_PAGE_DIM / 2u || max_cell.cell_h > FONT_ATLAS_PAGE_DIM)
        return FONT_ERR_ARG;
    *out = NULL;
    font_async *owner = calloc(1, sizeof *owner);
    if (!owner) return FONT_ERR_NOMEM;
    owner->pool = pool; owner->max_cell = max_cell;
    font_fallback_reset(&owner->discovery);
    /* Worst-case exact UTF-8 keys, one per renderer-admissible request.
     * Virtual arena reservation; only requested keys are touched. */
    size_t keys = ASYNC_ENTRIES * FONT_CLUSTER_MAX_BYTES;
    /* (P) Worker composes a single image without cache admission. The UI
     * hash table reserves empty buckets at the inherited occupancy ratio. */
    size_t cache_entries = 1;
    size_t negative = cache_entries < 64u ? cache_entries : 64u; /* inherited negative cache ceiling */
    size_t ui_sizes[] = {ASYNC_ENTRIES * sizeof(async_request), ASYNC_ENTRIES * sizeof(async_face),
        ASYNC_ENTRIES * sizeof(async_scalar), ASYNC_CACHE_ENTRIES * sizeof(uint32_t), keys,
        (ASYNC_ASCII + ASYNC_ENTRIES) * sizeof(render_glyph), ASYNC_ENTRIES, ASYNC_TILES};
    size_t ui_bytes = 0;
    for (size_t i = 0; i < sizeof ui_sizes / sizeof ui_sizes[0]; i++)
        ui_bytes += ui_sizes[i] + _Alignof(max_align_t); /* one alignment allowance per allocation */
    size_t storage_sizes[] = {cache_entries * sizeof(font_cache_entry),
        (cache_entries + ASYNC_ASCII) * sizeof(render_glyph), FONT_CLUSTER_MAX_BYTES,
        negative * sizeof(font_cache_entry), negative * 256u, /* inherited per-negative key bound */
        FONT_CLUSTER_MAX_BYTES, (size_t)max_cell.cell_w * 2u * max_cell.cell_h, ASYNC_SCRATCH};
    /* (P) One private atlas page satisfies font_cache_init; published pixels
     * live in separate, pre-touched pages with reusable per-request tiles. */
    size_t storage_bytes = ASYNC_SCRATCH + _Alignof(max_align_t);
    for (size_t i = 0; i < sizeof storage_sizes / sizeof storage_sizes[0]; i++)
        storage_bytes += storage_sizes[i] + _Alignof(max_align_t);
    if (edit_arena_init(&owner->ui, ui_bytes) || edit_arena_init(&owner->storage, storage_bytes) ||
        edit_arena_init(&owner->pixels, FONT_ATLAS_MAX_PAGES * ASYNC_SCRATCH)) {
        font_async_close(owner); return FONT_ERR_NOMEM;
    }
    owner->requests = edit_arena_alloc(&owner->ui, ui_sizes[0], _Alignof(async_request));
    owner->faces = edit_arena_alloc(&owner->ui, ui_sizes[1], _Alignof(async_face));
    owner->scalars = edit_arena_alloc(&owner->ui, ui_sizes[2], _Alignof(async_scalar));
    owner->table = edit_arena_alloc(&owner->ui, ui_sizes[3], _Alignof(uint32_t));
    owner->keys = edit_arena_alloc(&owner->ui, ui_sizes[4], 1);
    owner->glyphs = edit_arena_alloc(&owner->ui, ui_sizes[5], _Alignof(render_glyph));
    owner->retained = edit_arena_alloc(&owner->ui, ui_sizes[6], 1);
    owner->occupied = edit_arena_alloc(&owner->ui, ui_sizes[7], 1);
    if (!owner->requests || !owner->faces || !owner->scalars || !owner->table || !owner->keys || !owner->glyphs || !owner->retained || !owner->occupied) {
        font_async_close(owner); return FONT_ERR_NOMEM;
    }
    memset(owner->faces, 0, ui_sizes[1]);
    memset(owner->pixels.base, 0, owner->pixels.size); /* INIT pre-touch */
    int rc = font_async_reset_atlas(owner, a, em);
    if (rc) { font_async_close(owner); return rc; }
    *out = owner; return FONT_OK;
}

bool font_async_idle(const font_async *owner)
{
    return !owner || (!owner->pending && !owner->active.epoch &&
        (!owner->retired.epoch || work_handle_finished(owner->pool, owner->retired)));
}

int font_async_reset_atlas(font_async *owner, const font_ascii_atlas *a, bool em)
{
    if (!owner || !a || !a->pixels || !a->px || !a->cell.cell_w || !a->cell.cell_h ||
        a->cell.cell_w > owner->max_cell.cell_w || a->cell.cell_h > owner->max_cell.cell_h ||
        !font_async_idle(owner)) return FONT_ERR_ARG;
    /* edit-6e2: resolved chain entries survive a size change; their faces
     * reload lazily at the new size. Discovery faces are dropped. */
    for (size_t i = 0; i < owner->face_count; i++) {
        async_face *face = &owner->faces[i];
        edit_arena_free(&face->arena);
        if (i < owner->chain_count) { memset(&face->font, 0, sizeof face->font); face->result = FONT_MORE; }
        else memset(face, 0, sizeof *face);
    }
    owner->face_count = owner->chain_count; owner->scalar_count = 0;
    owner->count = owner->next = owner->pending = 0;
    owner->oldest = owner->newest = owner->vacant = SIZE_MAX;
    owner->retaining = false;
    memset(owner->retained, 0, ASYNC_ENTRIES);
    /* (P) Rectangles occupy one narrow tile or two adjacent tiles in a row.
     * Both descriptor and physical pixel capacities remain bounded. */
    owner->per_page = (FONT_ATLAS_PAGE_DIM / a->cell.cell_w) *
                      (FONT_ATLAS_PAGE_DIM / a->cell.cell_h);
    owner->tiles = owner->per_page * FONT_ATLAS_MAX_PAGES;
    if (owner->tiles > ASYNC_TILES) owner->tiles = ASYNC_TILES;
    owner->tile_cursor = 0;
    memset(owner->occupied, 0, ASYNC_TILES);
    memset(owner->requests, 0, ASYNC_ENTRIES * sizeof *owner->requests);
    owner->retired = (work_handle){0};
    owner->px = a->px; owner->ascii = *a; owner->em = em; owner->init_result = FONT_MORE;
    memset(&owner->family, 0, sizeof owner->family);
    memset(&owner->cache, 0, sizeof owner->cache);
    memset(owner->pages, 0, sizeof owner->pages);
    edit_arena_reset(&owner->storage);
    font_fallback_reset(&owner->discovery);
    memset(owner->table, 0, ASYNC_CACHE_ENTRIES * sizeof *owner->table);
    for (size_t i = 0; i < ASYNC_ASCII; i++)
        owner->glyphs[i] = (render_glyph){(uint32_t)i + ' ', 0, (uint32_t)i * a->cell.cell_w,
            0, a->cell.cell_w, a->cell.cell_h};
    owner->pages[0] = (render_atlas_page){a->pixels, a->pixels_len,
        ASYNC_ASCII * a->cell.cell_w, (uint32_t)ASYNC_ASCII * a->cell.cell_w, a->cell.cell_h};
    for (size_t i = 0; i < FONT_ATLAS_MAX_PAGES; i++)
        owner->pages[i + 1u] = (render_atlas_page){owner->pixels.base + i * ASYNC_SCRATCH,
            ASYNC_SCRATCH, FONT_ATLAS_PAGE_DIM, FONT_ATLAS_PAGE_DIM, FONT_ATLAS_PAGE_DIM};
    owner->page_count = FONT_ATLAS_MAX_PAGES + 1u;
    return FONT_OK;
}

int font_async_set_primary(font_async *owner, const font_t *primary)
{
    if (!owner || !primary || !primary->data || !font_async_idle(owner) ||
        owner->init_result != FONT_MORE || primary->px != owner->px ||
        primary->cell.cell_w != owner->ascii.cell.cell_w ||
        primary->cell.cell_h != owner->ascii.cell.cell_h) return FONT_ERR_ARG;
    owner->family.faces[0] = *primary;
    owner->family.faces[0].atlas = &owner->ascii;
    return FONT_OK;
}

void font_async_close(font_async *owner)
{
    if (!owner) return;
    /* Caller has joined the pool and retired all backend pixel borrows. */
    if (owner->faces) for (size_t i = 0; i < owner->face_count; i++) edit_arena_free(&owner->faces[i].arena);
    edit_arena_free(&owner->pixels); edit_arena_free(&owner->storage); edit_arena_free(&owner->ui); free(owner);
}

/* Worker-only: read and size one face file into its own arena. */
static int load_face(font_async *owner, async_face *face)
{
    struct stat st;
    face->result = FONT_ERR_INIT;
    if (!stat(face->path, &st) && st.st_size > 0 && (uintmax_t)st.st_size <= SIZE_MAX &&
        !edit_arena_init(&face->arena, (size_t)st.st_size)) {
        size_t len; unsigned char *bytes = font_load_file(face->path, &face->arena, &len);
        if (bytes) face->result = font_init_index(&face->font, bytes, len, face->index);
        if (face->result == FONT_OK) face->result = owner->em ?
            font_set_em_px(&face->font, owner->px) : font_set_px(&face->font, owner->px);
    }
    return face->result;
}

int font_async_set_fallbacks(font_async *owner, const char *chain, uint32_t *skipped, work_ctx *ctx)
{
    if (!owner || !chain || !skipped || !font_async_idle(owner) ||
        owner->face_count != owner->chain_count || strlen(chain) >= FONT_CHAIN_TEXT_MAX) return FONT_ERR_ARG;
    if (!strcmp(chain, owner->chain)) { *skipped = owner->chain_skipped; return FONT_OK; }
    for (size_t i = 0; i < owner->face_count; i++) {
        edit_arena_free(&owner->faces[i].arena); memset(&owner->faces[i], 0, sizeof owner->faces[i]);
    }
    owner->face_count = owner->chain_count = owner->scalar_count = 0;
    owner->chain[0] = 0; owner->chain_skipped = 0; /* a cancelled resolution retries next time */
    uint32_t missed = 0;
    for (const char *at = chain; *at;) {
        const char *end = strchr(at, '\n');
        size_t n = end ? (size_t)(end - at) : strlen(at);
        if (work_should_stop(ctx)) break; /* stop is sticky: handled below */
        char entry[FONT_CHAIN_TEXT_MAX], path[FONT_FALLBACK_PATH_MAX]; uint32_t index = 0;
        memcpy(entry, at, n); entry[n] = 0;
        /* (P) one face slot per entry: the chain shares the owner's face table. */
        if (!n || owner->chain_count == ASYNC_ENTRIES ||
            font_chain_resolve(entry, path, sizeof path, &index) != FONT_OK) missed++;
        else {
            async_face *face = &owner->faces[owner->chain_count++];
            memcpy(face->path, path, strlen(path) + 1u); face->index = index; face->result = FONT_MORE;
        }
        at += n; if (*at == '\n') at++;
    }
    if (work_should_stop(ctx)) { /* resolved faces hold no bytes yet */
        memset(owner->faces, 0, owner->chain_count * sizeof *owner->faces);
        owner->chain_count = 0; return FONT_MORE;
    }
    owner->face_count = owner->chain_count;
    memcpy(owner->chain, chain, strlen(chain) + 1u);
    owner->chain_skipped = *skipped = missed;
    return FONT_OK;
}

int font_async_scalar_source(const font_async *owner, uint32_t cp, char *path,
                             size_t cap, uint32_t *index)
{
    if (!owner || !path || !cap || !index || !font_async_idle(owner)) return FONT_ERR_ARG;
    for (size_t i = 0; i < owner->scalar_count; i++) if (owner->scalars[i].cp == cp) {
        if (owner->scalars[i].result != FONT_OK) return owner->scalars[i].result;
        const async_face *face = &owner->faces[owner->scalars[i].face];
        if (strlen(face->path) >= cap) return FONT_ERR_ARG;
        memcpy(path, face->path, strlen(face->path) + 1u); *index = face->index;
        return FONT_OK;
    }
    return FONT_ERR_MISSING;
}

static int resolve_scalar(void *ctx, uint32_t cp, font_t **out)
{
    font_async *owner = ctx;
    font_metric metric;
    if (font_glyph_metrics(&owner->family.faces[0], cp, &metric) == FONT_OK) {
        *out = &owner->family.faces[0]; return FONT_OK;
    }
    for (size_t i = 0; i < owner->scalar_count; i++) if (owner->scalars[i].cp == cp) {
        async_scalar scalar = owner->scalars[i];
        if (scalar.result == FONT_OK) *out = &owner->faces[scalar.face].font;
        return scalar.result;
    }
    int result = FONT_ERR_MISSING; size_t face_at = 0;
    /* edit-6e2: the configured chain, in order, before any fontconfig pick.
     * The per-codepoint scalar cache above makes this once per scalar. */
    for (; face_at < owner->chain_count; face_at++) {
        async_face *face = &owner->faces[face_at];
        if (face->result == FONT_MORE) (void)load_face(owner, face);
        if (face->result == FONT_OK && font_glyph_metrics(&face->font, cp, &metric) == FONT_OK) {
            result = FONT_OK; break;
        }
    }
    /* Reuse already loaded faces before asking fontconfig for the script. */
    if (result != FONT_OK) for (face_at = owner->chain_count; face_at < owner->face_count; face_at++) {
        async_face *face = &owner->faces[face_at];
        if (face->result == FONT_OK && font_glyph_metrics(&face->font, cp, &metric) == FONT_OK) {
            result = FONT_OK; break;
        }
    }
    if (result != FONT_OK) {
        char path[FONT_FALLBACK_PATH_MAX]; uint32_t index;
        if (!font_fallback_codepoint(&owner->discovery, owner->worker_ctx, cp, path, &index)) return FONT_ERR_INIT;
        if (path[0]) {
            for (face_at = 0; face_at < owner->face_count; face_at++)
                if (owner->faces[face_at].index == index && !strcmp(owner->faces[face_at].path, path)) break;
            if (face_at == owner->face_count) {
                if (owner->face_count == ASYNC_ENTRIES) return FONT_ERR_NOMEM;
                async_face *face = &owner->faces[owner->face_count++];
                memcpy(face->path, path, strlen(path) + 1u); face->index = index;
                (void)load_face(owner, face);
            }
            async_face *face = &owner->faces[face_at];
            result = face->result;
            if (result == FONT_OK) result = font_glyph_metrics(&face->font, cp, &metric);
        }
    }
    if (owner->scalar_count < ASYNC_ENTRIES)
        owner->scalars[owner->scalar_count++] = (async_scalar){cp, (uint32_t)face_at, result};
    if (result == FONT_OK) *out = &owner->faces[face_at].font;
    return result;
}

static void glyph_job(work_ctx *ctx)
{
    async_request *request = ctx->arg; font_async *owner = request->owner;
    if (work_should_stop(ctx)) return;
    owner->worker_ctx = ctx;
    if (owner->init_result == FONT_MORE) {
        size_t len; const unsigned char *bytes = font_embedded_data(&len);
        font_t *primary = &owner->family.faces[0];
        int result = primary->data ? FONT_OK : font_init(primary, bytes, len);
        if (result == FONT_OK) result = owner->em ?
            font_set_em_px(primary, owner->px) : font_set_px(primary, owner->px);
        if (result == FONT_OK && (primary->cell.cell_w != owner->ascii.cell.cell_w ||
                                 primary->cell.cell_h != owner->ascii.cell.cell_h)) result = FONT_ERR_ARG;
        if (result == FONT_OK) primary->atlas = &owner->ascii;
        owner->family.count = result == FONT_OK ? 1u : 0u;
        if (result == FONT_OK)
            result = font_cache_init(&owner->cache, &owner->family, &owner->storage,
                1, 1, FONT_CLUSTER_MAX_BYTES, ASYNC_SCRATCH); /* (P) private single-image scratch */
        owner->cache.resolve = resolve_scalar; owner->cache.resolve_ctx = owner;
        owner->init_result = result;
    }
    async_result output = {.request = request->index, .result = owner->init_result};
    if (output.result == FONT_OK) do {
        output.result = font_cache_render(&owner->cache, owner->keys + request->key,
                                         request->len, request->width);
        if (work_should_stop(ctx)) return;
    } while (output.result == FONT_MORE);
    int image_result = output.result;
    if (image_result != FONT_OK && owner->init_result == FONT_OK) {
        /* Unsupported glyphs retain replacement artwork. Capacity cannot
         * cause replacement: composition does not consume cache entries. */
        owner->cache.pending_len = 0;
        do {
            image_result = font_cache_render(&owner->cache, (const uint8_t *)"\xef\xbf\xbd",
                                             sizeof "\xef\xbf\xbd" - 1u, request->width);
            if (work_should_stop(ctx)) return;
        } while (image_result == FONT_MORE);
    }
    owner->cache.pending_len = 0;
    if (image_result == FONT_OK) {
        font_cell cell = owner->ascii.cell;
        uint32_t w = cell.cell_w * request->width, h = cell.cell_h;
        size_t tile = request->tile % owner->per_page;
        uint32_t columns = FONT_ATLAS_PAGE_DIM / cell.cell_w;
        uint32_t x = (uint32_t)(tile % columns) * cell.cell_w;
        uint32_t y = (uint32_t)(tile / columns) * h;
        uint32_t page = (uint32_t)(request->tile / owner->per_page) + 1u;
        uint8_t *pixels = (uint8_t *)(void *)owner->pages[page].pixels;
        uint8_t ink = 0;
        for (uint32_t row = 0; row < h; row++) {
            const uint8_t *source = owner->cache.image + (size_t)row * w;
            for (uint32_t col = 0; col < w; col++) ink |= source[col];
            memcpy(pixels + (size_t)(y + row) * FONT_ATLAS_PAGE_DIM + x, source, w);
        }
        if (ink) output.glyph = (render_glyph){0, page, x, y, w, h};
    }
    work_msg msg = {.kind = FONT_ASYNC_MSG_KIND, .generation = ctx->generation};
    memcpy(msg.data, &output, sizeof output);
    while (!work_should_stop(ctx)) {
        if (work_publish(ctx, &msg)) return; /* no owner writes after publication */
        struct timespec delay = {0, 1000000}; /* inherited mailbox retry cadence */
        (void)nanosleep(&delay, NULL);
    }
}

void font_async_pump(font_async *owner)
{
    if (!owner || owner->active.epoch || !owner->pending) return;
    for (size_t i = 0; i < owner->count; i++) {
        size_t at = (owner->next + i) % owner->count;
        async_request *request = &owner->requests[at];
        if (request->result != FONT_MORE) continue;
        work_handle handle = work_submit(owner->pool,
            (work_job){glyph_job, request, request->index + 1u, WORK_BULK});
        if (handle.epoch) { owner->active = handle; owner->next = at; }
        return;
    }
}

int font_async_bind(font_async *owner, render_grid *grid)
{
    if (!owner || !grid) return FONT_ERR_ARG;
    const font_ascii_atlas *a = &owner->ascii;
    if (grid->dims.cell_w != a->cell.cell_w || grid->dims.cell_h != a->cell.cell_h) return FONT_ERR_ARG;
    grid->glyphs = owner->glyphs; grid->glyph_count = ASYNC_ASCII + owner->count;
    grid->pages = owner->pages; grid->page_count = owner->page_count;
    return FONT_OK;
}

/* edit-dzk1: the bound table is this owner's; slot 0 is the baked ASCII space,
 * a full cell with no ink, so a replaced descriptor draws nothing. */
size_t font_async_repair(font_async *owner, render_grid *grid)
{
    if (!owner || !grid) return 0;
    bool owned = owner->glyphs && grid->glyphs == owner->glyphs;
    return render_grid_repair(grid, owned ? owner->glyphs : NULL, owned ? owner->glyphs[0] : (render_glyph){0});
}

void font_async_frame(font_async *owner, const render_grid *grid,
                      const render_grid *text, bool active)
{
    if (!owner) return;
    owner->retaining = true;
    /* (P) No slots exist beyond count, so ASCII-only frames need no pin
     * work and clearing scales with admitted descriptors, not capacity. */
    if (!owner->count) return;
    if (!active) memset(owner->retained, 0, owner->count);
    else for (size_t i = 0; i < owner->count; i++)
        if (owner->retained[i]) owner->retained[i] = ASYNC_PRIOR_PIN;
    const render_grid *grids[] = {grid, text};
    for (size_t i = 0; i < sizeof grids / sizeof grids[0]; i++) {
        const render_grid *g = grids[i];
        if (!g) continue;
        for (size_t cell = 0; cell < (size_t)g->dims.cols * g->dims.rows; cell++) {
            uint32_t slot = g->cells[cell].atlas_slot;
            if (slot >= ASYNC_ASCII && slot < ASYNC_ASCII + owner->count)
                owner->retained[slot - ASYNC_ASCII] = ASYNC_PRIOR_PIN;
        }
    }
}

void font_async_reclaim(font_async *owner)
{
    if (!owner) return;
    /* Full replacement after backend retirement may reuse outgoing cells,
     * while every lookup already made for the incoming frame stays pinned. */
    for (size_t i = 0; i < owner->count; i++) owner->retained[i] &= ASYNC_CURRENT_PIN;
}

/* edit-2ghs: render.h validates every bound descriptor and forbids
 * zero-sized rectangles. Blank (inkless) and vacant slots therefore carry
 * the baked space rectangle (page 0, slot 0: a full cell with no ink) under
 * their own identity; cells never reference them (lookups answer NO_SLOT). */
static render_glyph blank_descriptor(const font_async *owner, size_t slot)
{
    render_glyph glyph = owner->glyphs[0];
    glyph.glyph_index = (uint32_t)slot + 1u;
    return glyph;
}

bool font_async_inked(const font_async *owner, uint32_t slot)
{
    if (!owner || slot < ASYNC_ASCII || slot >= ASYNC_ASCII + owner->count) return false;
    return owner->requests[slot - ASYNC_ASCII].inked;
}

static void unlink_request(font_async *owner, size_t index)
{
    async_request *request = &owner->requests[index];
    if (request->previous == SIZE_MAX) owner->oldest = request->following;
    else owner->requests[request->previous].following = request->following;
    if (request->following == SIZE_MAX) owner->newest = request->previous;
    else owner->requests[request->following].previous = request->previous;
}

static void append_request(font_async *owner, size_t index)
{
    async_request *request = &owner->requests[index];
    request->previous = owner->newest; request->following = SIZE_MAX;
    if (owner->newest == SIZE_MAX) owner->oldest = index;
    else owner->requests[owner->newest].following = index;
    owner->newest = index;
}

static void touch(font_async *owner, size_t index)
{
    unlink_request(owner, index); append_request(owner, index);
    if (owner->retaining) owner->retained[index] |= ASYNC_CURRENT_PIN;
}

static void remove_key(font_async *owner, const async_request *request)
{
    size_t hole = (size_t)(request->hash % ASYNC_CACHE_ENTRIES);
    while (owner->table[hole] != request->index + 1u)
        hole = (hole + 1u) % ASYNC_CACHE_ENTRIES;
    size_t scan = (hole + 1u) % ASYNC_CACHE_ENTRIES;
    while (owner->table[scan]) {
        size_t home = (size_t)(owner->requests[owner->table[scan] - 1u].hash % ASYNC_CACHE_ENTRIES);
        /* (P) Modular probe distances: shift only keys whose search crosses
         * the hole, including clusters wrapping around the table end. */
        if ((scan + ASYNC_CACHE_ENTRIES - home) % ASYNC_CACHE_ENTRIES >=
            (scan + ASYNC_CACHE_ENTRIES - hole) % ASYNC_CACHE_ENTRIES) {
            owner->table[hole] = owner->table[scan]; hole = scan;
        }
        scan = (scan + 1u) % ASYNC_CACHE_ENTRIES;
    }
    owner->table[hole] = 0;
}

static size_t free_tile(const font_async *owner, uint32_t width)
{
    size_t columns = FONT_ATLAS_PAGE_DIM / owner->ascii.cell.cell_w;
    for (size_t i = 0; i < owner->tiles; i++) {
        size_t tile = (owner->tile_cursor + i) % owner->tiles;
        if (tile + width > owner->tiles || tile % columns + width > columns) continue;
        bool free = true;
        for (uint32_t part = 0; part < width; part++) free &= !owner->occupied[tile + part];
        if (free) return tile;
    }
    return SIZE_MAX;
}

static size_t free_request(const font_async *owner)
{
    if (owner->vacant != SIZE_MAX) return owner->vacant;
    return owner->count < ASYNC_ENTRIES ? owner->count : SIZE_MAX;
}

int font_async_glyph(void *ctx, const uint8_t *bytes, size_t len, uint32_t width, uint32_t *slot)
{
    font_async *owner = ctx;
    if (!owner || !bytes || !len || len > FONT_CLUSTER_MAX_BYTES || !slot || width < 1u || width > 2u) return FONT_ERR_ARG;
    for (size_t off = 0; off < len;) {
        utf8_step step = utf8_decode(bytes + off, len - off);
        if (!step.valid || step.cp < ' ' || step.cp == 0x7fu) return FONT_ERR_ARG;
        off += step.len;
    }
    if (len == 1 && bytes[0] >= ' ' && bytes[0] <= '~' && width == 1u) {
        *slot = bytes[0] == ' ' ? RENDER_NO_SLOT : (uint32_t)(bytes[0] - ' '); return FONT_OK;
    }
    uint64_t hash = UINT64_C(14695981039346656037); /* FNV-1a offset basis */
    for (size_t i = 0; i < len; i++) hash = (hash ^ bytes[i]) * UINT64_C(1099511628211); /* FNV-1a prime */
    hash = (hash ^ width) * UINT64_C(1099511628211);
    size_t at = (size_t)(hash % ASYNC_CACHE_ENTRIES);
    for (size_t probe = 0; probe < ASYNC_CACHE_ENTRIES; probe++) {
        uint32_t entry = owner->table[at];
        if (!entry) break;
        async_request *request = &owner->requests[entry - 1u];
        if (request->hash == hash && request->len == len && request->width == width &&
            !memcmp(owner->keys + request->key, bytes, len)) {
            touch(owner, request->index);
            *slot = request->inked ? (uint32_t)ASYNC_ASCII + request->index : RENDER_NO_SLOT;
            return FONT_OK;
        }
        at = (at + 1u) % ASYNC_CACHE_ENTRIES;
    }
    size_t index = free_request(owner), tile = free_tile(owner, width);
    while (index == SIZE_MAX || tile == SIZE_MAX) {
        size_t victim = owner->oldest;
        while (victim != SIZE_MAX && (owner->retained[victim] || owner->requests[victim].result == FONT_MORE))
            victim = owner->requests[victim].following;
        if (victim == SIZE_MAX) return FONT_MORE; /* retry after frame/job retirement */
        async_request *old = &owner->requests[victim];
        remove_key(owner, old); unlink_request(owner, victim);
        memset(owner->occupied + old->tile, 0, old->width);
        old->len = 0; old->result = FONT_ERR_ARG; old->inked = false;
        old->following = owner->vacant; owner->vacant = victim;
        owner->glyphs[ASYNC_ASCII + victim] = blank_descriptor(owner, ASYNC_ASCII + victim);
        index = free_request(owner); tile = free_tile(owner, width);
    }
    /* Deletion can shift buckets: find the current insertion hole. */
    at = (size_t)(hash % ASYNC_CACHE_ENTRIES);
    while (owner->table[at]) at = (at + 1u) % ASYNC_CACHE_ENTRIES;
    if (index == owner->count) owner->count++;
    else owner->vacant = owner->requests[index].following;
    async_request *request = &owner->requests[index];
    *request = (async_request){.owner = owner, .hash = hash,
        .key = index * FONT_CLUSTER_MAX_BYTES, .len = len, .tile = tile, .width = width,
        .index = (uint32_t)index, .result = FONT_MORE, .inked = true,
        .previous = SIZE_MAX, .following = SIZE_MAX};
    memcpy(owner->keys + request->key, bytes, len);
    memset(owner->occupied + tile, 1, width); /* (P) one occupancy byte per narrow tile */
    owner->tile_cursor = (tile + width) % owner->tiles;
    *slot = (uint32_t)(ASYNC_ASCII + index);
    owner->glyphs[*slot] = owner->glyphs['?' - ' '];
    owner->glyphs[*slot].glyph_index = *slot + 1u;
    owner->table[at] = (uint32_t)index + 1u;
    append_request(owner, index);
    owner->retained[index] = owner->retaining ? ASYNC_CURRENT_PIN : 0;
    owner->pending++;
    font_async_pump(owner);
    return FONT_OK;
}

bool font_async_pending(const font_async *owner) { return owner && owner->pending != 0; }

uint32_t font_async_event(font_async *owner, const work_msg *msg)
{
    if (!owner || !msg || msg->kind != FONT_ASYNC_MSG_KIND || !owner->active.epoch ||
        msg->slot_ != owner->active.slot || msg->epoch_ != owner->active.epoch) return RENDER_NO_SLOT;
    async_result output; memcpy(&output, msg->data, sizeof output);
    if (output.request >= owner->count || msg->generation != output.request + 1u ||
        output.request != owner->next || owner->requests[output.request].result != FONT_MORE) return RENDER_NO_SLOT;
    uint32_t slot = (uint32_t)ASYNC_ASCII + output.request;
    if (owner->init_result == FONT_OK) {
        /* The worker writes only this unretained request's tile. Descriptors
         * change on authenticated adoption; retained frame pixels are intact. */
        output.glyph.glyph_index = slot + 1u;
        bool inked = output.glyph.w != 0;
        owner->glyphs[slot] = inked ? output.glyph : blank_descriptor(owner, slot);
        owner->requests[output.request].inked = inked;
    } else {
        /* Worker preparation failure is settled too. Use the baked missing
         * marker, preserving startup/ASCII even if runtime storage is refused. */
        owner->glyphs[slot] = owner->glyphs['#' - ' '];
        owner->glyphs[slot].glyph_index = slot + 1u;
    }
    owner->requests[output.request].result = output.result;
    owner->pending--; owner->retired = owner->active; owner->active = (work_handle){0};
    font_async_pump(owner);
    return slot;
}
