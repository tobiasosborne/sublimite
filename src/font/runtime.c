/* Worker preparation and mailbox-only handoff for an editor font owner. */
#include "font/font.h"
#include <string.h>
#include <time.h>

int font_runtime_limits(const font_runtime_config *config, uint32_t *pages,
                        size_t *glyphs, size_t *pixel_bytes)
{
    if (!config || !pages || !glyphs || !pixel_bytes || !config->primary_path ||
        !config->max_pages || config->max_pages > FONT_ATLAS_MAX_PAGES ||
        !config->entries || config->entries > 4096u - 95u ||
        !config->key_bytes || !config->scratch_bytes) return FONT_ERR_ARG;
    const font_ascii_atlas *ascii = font_ascii_atlas_for_px(config->px);
    if (!ascii) return FONT_ERR_ARG;
    *pages = config->max_pages + 1u;
    *glyphs = config->entries + 95u;
    *pixel_bytes = ascii->pixels_len + (size_t)config->max_pages *
                   FONT_ATLAS_PAGE_DIM * FONT_ATLAS_PAGE_DIM;
    return FONT_OK;
}

int font_runtime_init(font_runtime *owner, const font_runtime_config *config,
                      edit_arena *files, edit_arena *storage)
{
    uint32_t pages; size_t glyphs, pixels;
    if (!owner || !files || !storage || files == storage || !files->base || !storage->base ||
        font_runtime_limits(config, &pages, &glyphs, &pixels) != FONT_OK ||
        strlen(config->primary_path) >= sizeof owner->primary_path ||
        (config->fallback && atomic_load_explicit(&config->fallback->done, memory_order_acquire) != 1u))
        return FONT_ERR_ARG;
    memset(owner, 0, sizeof *owner);
    owner->config = *config;
    memcpy(owner->primary_path, config->primary_path, strlen(config->primary_path) + 1u);
    owner->config.primary_path = owner->primary_path;
    owner->files = files; owner->storage = storage;
    owner->result = owner->staged_result = FONT_MORE;
    return FONT_OK;
}

void font_runtime_prepare_job(work_ctx *ctx)
{
    if (work_should_stop(ctx)) return;
    font_runtime *owner = ctx->arg;
    edit_arena_mark_t files_mark = edit_arena_mark(owner->files);
    edit_arena_mark_t storage_mark = edit_arena_mark(owner->storage);
    owner->worker = pthread_self();
    size_t len = 0;
    unsigned char *bytes = font_load_file(owner->primary_path, owner->files, &len);
    font_t primary;
    int result = bytes ? font_init(&primary, bytes, len) : FONT_ERR_INIT;
    if (result == FONT_OK) result = font_set_px(&primary, owner->config.px);
    if (result == FONT_OK) {
        owner->fallback_error = font_family_load(&owner->family, &primary,
                                                 owner->config.fallback, owner->files);
        /* Unsupported fallback files preserve the usable embedded primary. */
        if (!owner->family.count) result = owner->fallback_error;
    }
    if (result == FONT_OK && !work_should_stop(ctx))
        result = font_cache_init(&owner->cache, &owner->family, owner->storage,
                                 owner->config.max_pages, owner->config.entries,
                                 owner->config.key_bytes, owner->config.scratch_bytes);
    if (result != FONT_OK || work_should_stop(ctx)) {
        edit_arena_reset_to_mark(owner->files, files_mark);
        edit_arena_reset_to_mark(owner->storage, storage_mark);
        memset(&owner->family, 0, sizeof owner->family);
        memset(&owner->cache, 0, sizeof owner->cache);
    }
    if (work_should_stop(ctx)) return;
    owner->staged_result = result;
    owner->staged_generation = ctx->generation;
    owner->staged_slot = (uint32_t)(ctx->slot - ctx->pool->slots);
    owner->staged_epoch = ctx->epoch;
    work_msg message = {0};
    message.kind = FONT_RUNTIME_MSG_KIND; message.generation = ctx->generation;
    uintptr_t identity = (uintptr_t)owner;
    memcpy(message.data, &identity, sizeof identity);
    while (!work_should_stop(ctx)) {
        if (work_publish(ctx, &message)) return;
        struct timespec delay = {0, 1000000}; (void)nanosleep(&delay, NULL);
    }
}

int font_runtime_event(font_runtime *owner, const work_msg *msg)
{
    if (!owner || !msg || msg->kind != FONT_RUNTIME_MSG_KIND) return 0;
    uintptr_t identity = 0; memcpy(&identity, msg->data, sizeof identity);
    if (identity != (uintptr_t)owner || owner->adopted || !owner->staged_epoch ||
        msg->generation != owner->staged_generation || msg->slot_ != owner->staged_slot ||
        msg->epoch_ != owner->staged_epoch) return 0;
    owner->result = owner->staged_result; owner->adopted = 1;
    return 1;
}

int font_runtime_bind(font_runtime *owner, render_grid *grid)
{
    if (!owner || !grid) return FONT_ERR_ARG;
    if (!owner->adopted) return FONT_MORE;
    if (owner->result != FONT_OK) return owner->result;
    return font_cache_bind(&owner->cache, grid);
}

int font_runtime_glyph(void *ctx, const uint8_t *bytes, size_t len,
                       uint32_t width, uint32_t *slot)
{
    font_runtime *owner = ctx;
    if (!owner) return FONT_ERR_ARG;
    if (!owner->adopted) return FONT_MORE;
    if (owner->result != FONT_OK) return owner->result;
    return font_cache_glyph(&owner->cache, bytes, len, width, slot);
}
