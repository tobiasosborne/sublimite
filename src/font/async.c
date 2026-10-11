/* edit-08z: UI requests, worker-only faces/cache, mailbox-only atlas adoption. */
#include "font/font.h"
#include "font/discovery.h"
#include "utf8/utf8.h"
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

/* (G) Inherited raster/GL descriptor ceiling, less baked printable ASCII.
 * These are storage ceilings, not invented latency or memory targets. */
#define ASYNC_ASCII ((size_t)('~' - ' ' + 1))
#define ASYNC_ENTRIES (4096u - ASYNC_ASCII)
#define ASYNC_SCRATCH ((size_t)FONT_ATLAS_PAGE_DIM * FONT_ATLAS_PAGE_DIM)
#define ASYNC_WIDTHS 2u /* utf8's supported narrow/wide cluster images */
#define ASYNC_CACHE_ENTRIES (((ASYNC_ENTRIES + ASYNC_WIDTHS) * 4u + 2u) / 3u)

typedef struct async_request {
    struct font_async *owner;
    uint64_t hash;
    size_t key, len;
    uint32_t width, index;
    int result;
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
    render_glyph *glyphs;
    render_atlas_page pages[FONT_ATLAS_MAX_PAGES + 1u];
    render_glyph missing[ASYNC_WIDTHS]; /* completed replacement images, never pending '?' */
    size_t count, next, pending, key_used, page_count;
    /* Exclusively worker-owned until shutdown. */
    size_t face_count, scalar_count;
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
    /* Cache admission keeps a quarter of its hash table empty. Round up so
     * every UI request can be admitted; its private descriptors are copied
     * into the smaller renderer table only after mailbox completion. */
    size_t cache_entries = ASYNC_CACHE_ENTRIES;
    size_t negative = cache_entries < 64u ? cache_entries : 64u; /* inherited negative cache ceiling */
    size_t ui_sizes[] = {ASYNC_ENTRIES * sizeof(async_request), ASYNC_ENTRIES * sizeof(async_face),
        ASYNC_ENTRIES * sizeof(async_scalar), ASYNC_ENTRIES * sizeof(uint32_t), keys,
        (ASYNC_ASCII + ASYNC_ENTRIES) * sizeof(render_glyph)};
    size_t ui_bytes = 0;
    for (size_t i = 0; i < sizeof ui_sizes / sizeof ui_sizes[0]; i++)
        ui_bytes += ui_sizes[i] + _Alignof(max_align_t); /* one alignment allowance per allocation */
    size_t storage_sizes[] = {cache_entries * sizeof(font_cache_entry),
        (cache_entries + ASYNC_ASCII) * sizeof(render_glyph), keys + ASYNC_WIDTHS * sizeof "\xef\xbf\xbd",
        negative * sizeof(font_cache_entry), negative * 256u, /* inherited per-negative key bound */
        FONT_CLUSTER_MAX_BYTES, (size_t)max_cell.cell_w * 2u * max_cell.cell_h, ASYNC_SCRATCH};
    size_t storage_bytes = FONT_ATLAS_MAX_PAGES * (ASYNC_SCRATCH + _Alignof(max_align_t));
    for (size_t i = 0; i < sizeof storage_sizes / sizeof storage_sizes[0]; i++)
        storage_bytes += storage_sizes[i] + _Alignof(max_align_t);
    if (edit_arena_init(&owner->ui, ui_bytes) || edit_arena_init(&owner->storage, storage_bytes)) {
        font_async_close(owner); return FONT_ERR_NOMEM;
    }
    owner->requests = edit_arena_alloc(&owner->ui, ui_sizes[0], _Alignof(async_request));
    owner->faces = edit_arena_alloc(&owner->ui, ui_sizes[1], _Alignof(async_face));
    owner->scalars = edit_arena_alloc(&owner->ui, ui_sizes[2], _Alignof(async_scalar));
    owner->table = edit_arena_alloc(&owner->ui, ui_sizes[3], _Alignof(uint32_t));
    owner->keys = edit_arena_alloc(&owner->ui, ui_sizes[4], 1);
    owner->glyphs = edit_arena_alloc(&owner->ui, ui_sizes[5], _Alignof(render_glyph));
    if (!owner->requests || !owner->faces || !owner->scalars || !owner->table || !owner->keys || !owner->glyphs) {
        font_async_close(owner); return FONT_ERR_NOMEM;
    }
    memset(owner->faces, 0, ui_sizes[1]);
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
    for (size_t i = 0; i < owner->face_count; i++) edit_arena_free(&owner->faces[i].arena);
    memset(owner->faces, 0, owner->face_count * sizeof *owner->faces);
    owner->face_count = owner->scalar_count = 0;
    owner->count = owner->next = owner->pending = owner->key_used = 0;
    owner->retired = (work_handle){0};
    owner->px = a->px; owner->ascii = *a; owner->em = em; owner->init_result = FONT_MORE;
    memset(&owner->family, 0, sizeof owner->family);
    memset(&owner->cache, 0, sizeof owner->cache);
    memset(owner->missing, 0, sizeof owner->missing);
    memset(owner->pages, 0, sizeof owner->pages);
    edit_arena_reset(&owner->storage);
    font_fallback_reset(&owner->discovery);
    memset(owner->table, 0, ASYNC_ENTRIES * sizeof *owner->table);
    for (size_t i = 0; i < ASYNC_ASCII; i++)
        owner->glyphs[i] = (render_glyph){(uint32_t)i + ' ', 0, (uint32_t)i * a->cell.cell_w,
            0, a->cell.cell_w, a->cell.cell_h};
    owner->pages[0] = (render_atlas_page){a->pixels, a->pixels_len,
        ASYNC_ASCII * a->cell.cell_w, (uint32_t)ASYNC_ASCII * a->cell.cell_w, a->cell.cell_h};
    owner->page_count = 1;
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
    edit_arena_free(&owner->storage); edit_arena_free(&owner->ui); free(owner);
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
    /* Reuse already loaded faces before asking fontconfig for the script. */
    for (; face_at < owner->face_count; face_at++) {
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
                struct stat st;
                face->result = FONT_ERR_INIT;
                if (!stat(path, &st) && st.st_size > 0 && (uintmax_t)st.st_size <= SIZE_MAX &&
                    !edit_arena_init(&face->arena, (size_t)st.st_size)) {
                    size_t len; unsigned char *bytes = font_load_file(path, &face->arena, &len);
                    if (bytes) face->result = font_init_index(&face->font, bytes, len, index);
                    if (face->result == FONT_OK) face->result = owner->em ?
                        font_set_em_px(&face->font, owner->px) : font_set_px(&face->font, owner->px);
                }
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
                FONT_ATLAS_MAX_PAGES, ASYNC_CACHE_ENTRIES,
                ASYNC_ENTRIES * FONT_CLUSTER_MAX_BYTES + ASYNC_WIDTHS * sizeof "\xef\xbf\xbd", ASYNC_SCRATCH);
        owner->cache.resolve = resolve_scalar; owner->cache.resolve_ctx = owner;
        /* Reserve completed replacement artwork before the append-only cache
         * can fill. An unsupported face or exhausted cache ends in a missing
         * image, while '?' is reserved exclusively for a pending request. */
        if (result == FONT_OK) for (uint32_t width = 1; width <= ASYNC_WIDTHS; width++) {
            uint32_t replacement = RENDER_NO_SLOT; int rc;
            do { rc = font_cache_glyph(&owner->cache, (const uint8_t *)"\xef\xbf\xbd",
                                      sizeof "\xef\xbf\xbd" - 1u, width, &replacement); }
            while (rc == FONT_MORE && !work_should_stop(ctx));
            if (rc != FONT_OK || replacement == RENDER_NO_SLOT) { result = rc == FONT_OK ? FONT_ERR_INIT : rc; break; }
            owner->missing[width - 1u] = owner->cache.glyphs[replacement];
        }
        owner->init_result = result;
    }
    async_result output = {.request = request->index, .result = owner->init_result};
    uint32_t slot = RENDER_NO_SLOT;
    if (output.result == FONT_OK) do {
        output.result = font_cache_glyph(&owner->cache, owner->keys + request->key,
                                        request->len, request->width, &slot);
        if (work_should_stop(ctx)) return;
    } while (output.result == FONT_MORE);
    if (output.result == FONT_OK && slot != RENDER_NO_SLOT) output.glyph = owner->cache.glyphs[slot];
    else if (output.result != FONT_OK && owner->init_result == FONT_OK)
        output.glyph = owner->missing[request->width - 1u];
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
    if (!owner || owner->active.epoch || owner->next == owner->count) return;
    async_request *request = &owner->requests[owner->next];
    work_handle handle = work_submit(owner->pool,
        (work_job){glyph_job, request, request->index + 1u, WORK_BULK});
    if (handle.epoch) { owner->active = handle; owner->next++; }
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
    size_t at = (size_t)(hash % ASYNC_ENTRIES);
    for (size_t probe = 0; probe < FONT_CACHE_MAX_PROBES; probe++) {
        uint32_t entry = owner->table[at];
        if (entry) {
            async_request *request = &owner->requests[entry - 1u];
            if (request->hash == hash && request->len == len && request->width == width &&
                !memcmp(owner->keys + request->key, bytes, len)) {
                *slot = owner->glyphs[ASYNC_ASCII + request->index].w ? (uint32_t)ASYNC_ASCII + request->index : RENDER_NO_SLOT;
                /* Missing/error glyphs are a settled missing-image cell, not
                 * a retry/spin. Cache exhaustion is reported in the status. */
                return FONT_OK;
            }
        } else {
            if (owner->count == ASYNC_ENTRIES) {
                *slot = '#' - ' '; return FONT_OK; /* settled resource refusal, never a pending '?' */
            }
            async_request *request = &owner->requests[owner->count];
            *request = (async_request){owner, hash, owner->key_used, len, width, (uint32_t)owner->count, FONT_MORE};
            memcpy(owner->keys + owner->key_used, bytes, len); owner->key_used += len;
            *slot = (uint32_t)(ASYNC_ASCII + owner->count);
            owner->glyphs[*slot] = owner->glyphs['?' - ' '];
            owner->glyphs[*slot].glyph_index = *slot + 1u;
            owner->table[at] = (uint32_t)++owner->count; owner->pending++;
            font_async_pump(owner); return FONT_OK;
        }
        if (++at == ASYNC_ENTRIES) at = 0;
    }
    *slot = '#' - ' '; return FONT_OK; /* bounded probe refusal: completed missing marker */
}

bool font_async_pending(const font_async *owner) { return owner && owner->pending != 0; }

uint32_t font_async_event(font_async *owner, const work_msg *msg)
{
    if (!owner || !msg || msg->kind != FONT_ASYNC_MSG_KIND || !owner->active.epoch ||
        msg->slot_ != owner->active.slot || msg->epoch_ != owner->active.epoch) return RENDER_NO_SLOT;
    async_result output; memcpy(&output, msg->data, sizeof output);
    if (output.request >= owner->count || msg->generation != output.request + 1u ||
        output.request + 1u != owner->next || owner->requests[output.request].result != FONT_MORE) return RENDER_NO_SLOT;
    uint32_t slot = (uint32_t)ASYNC_ASCII + output.request;
    if (owner->init_result == FONT_OK) {
        /* Pages are immutable descriptors after cache initialisation; pixels
         * and glyphs append only. No worker reads the UI descriptor table. */
        memcpy(owner->pages, owner->cache.pages, sizeof owner->pages);
        owner->page_count = FONT_ATLAS_MAX_PAGES + 1u;
        output.glyph.glyph_index = slot + 1u;
        owner->glyphs[slot] = output.glyph;
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
