#include "config/private.h"
#include "base/base.h"
#include "file/file.h"
#include <string.h>
#include <stdlib.h>

struct config_runtime {
    work_pool pool;
    work_handle handle;
    edit_arena arena;
    char path[CONFIG_TEXT_CAP];
    uint8_t *source;
    config_settings current, staged;
    config_error error, staged_error;
    file_path_watch watch;
    uint64_t generation;
    uint32_t request;
    int watchfd;
    bool pool_ready, requested, received, staged_ready, directory_ready;
};
static int directory(config_runtime *r)
{
    char path[CONFIG_TEXT_CAP]; strcpy(path, r->path);
    char *end = strrchr(path, '/');
#if defined(_WIN32)
    char *back = strrchr(path, '\\');
    if (back && (!end || back > end)) end = back;
#endif
    if (!end || !end[1]) return CONFIG_ERR_ARG;
#if defined(_WIN32)
    if (path[1] == ':' && end == path + 2) {
        end[1] = 0;
        os_stat st;
        return os_stat_path(path, 0, &st) == OS_OK && st.kind == OS_KIND_DIR ? CONFIG_OK : CONFIG_ERR_IO;
    }
#endif
    *end = 0;
    char *start = path + 1;
#if defined(_WIN32)
    /* Do not mkdir the drive or UNC server/share prefix. */
    if (path[1] == ':') start = path + 3;
    else if ((path[0] == '/' || path[0] == '\\') && path[1] == path[0]) {
        start = path + 2;
        for (unsigned part = 0; part < 2u; part++) {
            while (*start && *start != '/' && *start != '\\') start++;
            if (*start) start++;
        }
    }
#endif
    for (char *at = start; ; at++) {
        if (*at && *at != '/'
#if defined(_WIN32)
            && *at != '\\'
#endif
        ) continue;
        char saved = *at; *at = 0;
        os_err rc = os_mkdir(path);
        if (rc && rc != OS_E_EXIST) return CONFIG_ERR_IO;
        os_stat st; if (os_stat_path(path, 0, &st) || st.kind != OS_KIND_DIR) return CONFIG_ERR_IO;
        *at = saved; if (!saved) return CONFIG_OK;
    }
}
static int read_settings(config_runtime *r, work_ctx *ctx)
{
    if (!r->directory_ready) {
        int rc = directory(r); if (rc) return rc;
        r->directory_ready = true;
    }
    os_file source = OS_FILE_INIT;
    os_err opened = os_file_open(&source, r->path, OS_OPEN_READ | OS_OPEN_NONBLOCK | OS_OPEN_NOFOLLOW);
    if (opened) {
        if (opened != OS_E_NOENT) return CONFIG_ERR_IO;
        config_defaults(&r->staged); return CONFIG_OK;
    }
    os_stat before, after; int rc = CONFIG_OK;
    if (os_file_stat(&source, &before) || before.kind != OS_KIND_FILE) { rc = CONFIG_ERR_IO; goto done; }
    if (before.size > CONFIG_SOURCE_CAP) { rc = CONFIG_ERR_LIMIT; goto done; }
    size_t length = (size_t)before.size, used = 0;
    while (used < length && !work_should_stop(ctx)) {
        size_t n = length - used; if (n > FILE_PREFIX_MAX) n = FILE_PREFIX_MAX;
        size_t got;
        if (os_file_pread(&source, r->source + used, n, used, &got)) { rc = CONFIG_ERR_IO; goto done; }
        if (!got) { rc = CONFIG_ERR_BUSY; goto done; }
        used += (size_t)got;
    }
    if (work_should_stop(ctx)) { rc = CONFIG_ERR_BUSY; goto done; }
    if (os_file_stat(&source, &after)) { rc = CONFIG_ERR_IO; goto done; }
    file_id first, last, entry; file_id_from_stat(&first, &before); file_id_from_stat(&last, &after);
    if (file_id_stat_path(r->path, &entry) != FILE_OK) { rc = CONFIG_ERR_IO; goto done; }
    if (file_id_diff(&first, &last) || file_id_diff(&last, &entry)) { rc = CONFIG_ERR_BUSY; goto done; }
    rc = config_parse_worker(r->source, length, &r->staged, &r->staged_error, ctx);
done:
    os_file_close(&source); return rc;
}
static void reload_job(work_ctx *ctx)
{
    config_runtime *r = ctx->arg;
    if (work_should_stop(ctx)) return;
    if (!r->staged_ready) {
        r->staged_error = (config_error){0};
        int rc = read_settings(r, ctx);
        if (rc && !r->staged_error.code) r->staged_error.code = rc;
        r->staged_ready = true;
    }
    /* (P) app message identifier spells CONF in hexadecimal ASCII. Payload
     * lives in the owner's reserved staging until this lease is acknowledged. */
    work_msg msg = {.kind = UINT32_C(0x434f4e46), .generation = ctx->generation};
    if (!work_publish(ctx, &msg)) (void)work_continue(ctx);
}
static void receive(const work_msg *msg, void *ctx)
{
    config_runtime *r = ctx;
    if (msg->generation != r->request || msg->slot_ != r->handle.slot || msg->epoch_ != r->handle.epoch) return;
    r->received = true; r->error = r->staged_error;
    if (r->error.code == CONFIG_ERR_BUSY) r->requested = true;
    if (!r->error.code && memcmp(&r->current, &r->staged, sizeof r->current)) {
        if (r->generation == UINT64_MAX) { r->error.code = CONFIG_ERR_LIMIT; return; }
        r->current = r->staged; r->generation++;
    }
    /* Accepted field fallback uses the existing diagnostic without blocking
     * publication of none or the other valid settings in this generation. */
    if (!r->error.code && r->staged.trailing_warning) r->error.code = CONFIG_ERR_VALUE;
}
static void request(config_runtime *r)
{
    if (!r->requested || (r->handle.epoch && (!r->received || !work_handle_finished(&r->pool, r->handle)))) return;
    if (r->request == UINT32_MAX) { r->error.code = CONFIG_ERR_LIMIT; return; }
    r->staged_ready = false; r->received = false;
    work_job job = {reload_job, r, r->request + 1u, WORK_BULK};
    work_handle handle = work_submit(&r->pool, job);
    if (!handle.epoch) return;
    r->handle = handle; r->request++; r->requested = false;
    (void)work_mailbox_bind(&r->pool, handle, r->request, receive, r);
}
int config_runtime_open(config_runtime **out, const char *path)
{
    if (!out) return CONFIG_ERR_ARG;
    *out = NULL;
    config_runtime *r = config_runtime_alloc(_Alignof(config_runtime), sizeof *r);
    if (!r) return CONFIG_ERR_IO;
    memset(r, 0, sizeof *r); r->watchfd = -1; r->watch.wd = -1;
    int rc = CONFIG_OK;
    if (path) {
        if (!os_path_is_absolute(path) || strlen(path) >= sizeof r->path) rc = CONFIG_ERR_ARG;
        else strcpy(r->path, path);
    } else rc = config_path(r->path, sizeof r->path);
    if (rc) goto fail;
    if (edit_arena_init(&r->arena, CONFIG_SOURCE_CAP)) { rc = CONFIG_ERR_IO; goto fail; }
    r->source = edit_arena_alloc(&r->arena, CONFIG_SOURCE_CAP, _Alignof(uint8_t));
    if (!r->source || work_pool_init(&r->pool, 1, 0)) { rc = CONFIG_ERR_IO; goto fail; }
    r->pool_ready = true; r->requested = true; request(r); *out = r; return CONFIG_OK;
fail:
    config_runtime_close(r); return rc;
}
void config_runtime_close(config_runtime *r)
{
    if (!r) return;
    file_watch_path_stop(&r->watch);
    if (r->pool_ready) work_pool_shutdown(&r->pool);
    edit_arena_free(&r->arena); config_runtime_free(r);
}
#if !defined(_WIN32)
int config_runtime_eventfd(const config_runtime *r) { return r ? work_pool_eventfd(&r->pool) : -1; }
#endif
int config_runtime_watchfd(const config_runtime *r) { return r ? r->watchfd : -1; }
bool config_runtime_tick(config_runtime *r)
{
    if (!r) return false;
    uint64_t before = r->generation;
    file_watch_dispatch();
    (void)work_mailbox_drain(&r->pool, receive, r);
    /* received is the mailbox barrier for worker directory setup. */
    if (r->received && r->directory_ready && (!r->watch.registered || r->watch.wd < 0)) {
        int fd = file_watch_path_start(&r->watch, r->path);
        if (fd >= 0) { r->watchfd = fd; r->requested = true; }
        else r->error.code = CONFIG_ERR_IO;
    }
    if (file_watch_path_take(&r->watch)) r->requested = true;
    request(r);
    return before != r->generation;
}
const config_settings *config_runtime_current(const config_runtime *r) { return r ? &r->current : NULL; }
uint64_t config_runtime_generation(const config_runtime *r) { return r ? r->generation : 0; }
config_error config_runtime_error(const config_runtime *r) { return r ? r->error : (config_error){CONFIG_ERR_ARG, 0}; }
bool config_runtime_busy(const config_runtime *r)
{
    return r && (r->requested || (r->handle.epoch && (!r->received || !work_handle_finished(&r->pool, r->handle))));
}
