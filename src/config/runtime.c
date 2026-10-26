#include "config/private.h"
#include "base/base.h"
#include "file/file.h"
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>

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
int config_path(char *out, size_t cap)
{
    if (!out || !cap) return CONFIG_ERR_ARG;
    const char *root = getenv("XDG_CONFIG_HOME"), *suffix = "/sublimite/config.toml";
    if (!root || root[0] != '/') { root = getenv("HOME"); suffix = "/.config/sublimite/config.toml"; }
    if (!root || root[0] != '/') return CONFIG_ERR_ARG;
    size_t n = strlen(root);
    while (n && root[n - 1u] == '/') n--;
    if (n >= cap || strlen(suffix) >= cap - n) return CONFIG_ERR_LIMIT;
    memcpy(out, root, n); strcpy(out + n, suffix); return CONFIG_OK;
}
static int directory(config_runtime *r)
{
    char path[CONFIG_TEXT_CAP]; strcpy(path, r->path);
    char *end = strrchr(path, '/'); if (!end || !end[1]) return CONFIG_ERR_ARG;
    *end = 0;
    for (char *at = path + 1; ; at++) {
        if (*at && *at != '/') continue;
        char saved = *at; *at = 0;
        /* (P) POSIX private directory permissions, owner rwx only. */
        if (mkdir(path, 0700) && errno != EEXIST) return CONFIG_ERR_IO;
        struct stat st; if (stat(path, &st) || !S_ISDIR(st.st_mode)) return CONFIG_ERR_IO;
        *at = saved; if (!saved) return CONFIG_OK;
    }
}
static int read_settings(config_runtime *r, work_ctx *ctx)
{
    if (!r->directory_ready) {
        int rc = directory(r); if (rc) return rc;
        r->directory_ready = true;
    }
    int fd = open(r->path, O_RDONLY | O_CLOEXEC | O_NONBLOCK | O_NOFOLLOW);
    if (fd < 0) {
        if (errno != ENOENT) return CONFIG_ERR_IO;
        config_defaults(&r->staged); return CONFIG_OK;
    }
    os_file source = os_file_from_fd(fd);
    os_stat before, after; int rc = CONFIG_OK;
    if (os_file_stat(&source, &before) || before.kind != OS_KIND_FILE) { rc = CONFIG_ERR_IO; goto done; }
    if (before.size > CONFIG_SOURCE_CAP) { rc = CONFIG_ERR_LIMIT; goto done; }
    size_t length = (size_t)before.size, used = 0;
    while (used < length && !work_should_stop(ctx)) {
        size_t n = length - used; if (n > FILE_PREFIX_MAX) n = FILE_PREFIX_MAX;
        ssize_t got = pread(fd, r->source + used, n, (off_t)used);
        if (got < 0) { if (errno == EINTR) continue; rc = CONFIG_ERR_IO; goto done; }
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
    close(fd); return rc;
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
    config_runtime *r = aligned_alloc(_Alignof(config_runtime), sizeof *r);
    if (!r) return CONFIG_ERR_IO;
    memset(r, 0, sizeof *r); r->watchfd = -1; r->watch.wd = -1;
    int rc = CONFIG_OK;
    if (path) {
        if (path[0] != '/' || strlen(path) >= sizeof r->path) rc = CONFIG_ERR_ARG;
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
    edit_arena_free(&r->arena); free(r);
}
int config_runtime_eventfd(const config_runtime *r) { return r ? work_pool_eventfd(&r->pool) : -1; }
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
