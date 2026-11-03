#include "config/private.h"
#include "base/base.h"
#include "file/file.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/* edit-b6ai.2: what the worker found at the palette path. */
enum { PALETTE_NONE, PALETTE_OK, PALETTE_ABSENT, PALETTE_BAD };

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
    /* edit-b6ai.2 system palette. palette_dir is the directory Omarchy
     * deletes and renames in; palette_name its sibling <dir>.name, written
     * last. Both are members of the shared parent watch. */
    char palette_path[CONFIG_TEXT_CAP], palette_dir[CONFIG_TEXT_CAP], palette_name[CONFIG_TEXT_CAP];
    file_path_watch palette_watch[2];
    config_palette palette, staged_palette;
    config_error staged_palette_error;
    int palette_error, staged_palette_status;
    bool palette_watch_tried, palette_system;
    bool palette_noticed; /* worker-owned: one notice per malformed episode */
    /* edit-b6ai.3: fonts.conf is never read here (Fontconfig re-reads it on
     * the font worker); only its changes are noticed, on the UI thread.
     * fonts_watch[1] is the file's parent; fonts_watch[0] is the directory's
     * own entry in its parent, kept only while that directory is missing. */
    char fontconf_path[CONFIG_TEXT_CAP], fontconf_dir[CONFIG_TEXT_CAP];
    file_path_watch fonts_watch[2];
    uint64_t fonts_generation;
    bool fonts_watch_tried;
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
/* Worker: read the palette into staged_palette. The settings source has
 * been parsed by now, so its buffer is reused. Read through one descriptor:
 * Omarchy replaces the directory, never the file in place, so an open file
 * is complete (theme-system.md 3.1 step 7). */
static int read_palette(config_runtime *r, work_ctx *ctx)
{
    r->staged_palette_error = (config_error){0};
    if (!r->palette_path[0]) return PALETTE_NONE;
    os_file source = OS_FILE_INIT;
    os_err opened = os_file_open(&source, r->palette_path, OS_OPEN_READ | OS_OPEN_NONBLOCK);
    if (opened == OS_E_NOENT || opened == OS_E_NOTDIR) return PALETTE_ABSENT;
    int rc = CONFIG_OK; size_t used = 0;
    if (opened) rc = CONFIG_ERR_IO;
    else {
        os_stat st;
        if (os_file_stat(&source, &st) || st.kind != OS_KIND_FILE) rc = CONFIG_ERR_IO;
        else if (st.size > CONFIG_SOURCE_CAP) rc = CONFIG_ERR_LIMIT;
        size_t length = rc ? 0 : (size_t)st.size;
        while (!rc && used < length && !work_should_stop(ctx)) {
            size_t n = length - used, got; if (n > FILE_PREFIX_MAX) n = FILE_PREFIX_MAX;
            if (os_file_pread(&source, r->source + used, n, used, &got)) rc = CONFIG_ERR_IO;
            else if (!got) break; /* truncated under us: parse what was read */
            else used += got;
        }
        os_file_close(&source);
    }
    if (work_should_stop(ctx)) rc = CONFIG_ERR_BUSY;
    char marker[CONFIG_TEXT_CAP + sizeof "/light.mode"]; os_stat st;
    bool light = snprintf(marker, sizeof marker, "%s/light.mode", r->palette_dir) > 0 &&
                 os_stat_path(marker, 0, &st) == OS_OK;
    if (!rc) rc = config_palette_parse(r->source, used, light, CONFIG_DARK, &r->staged_palette, &r->staged_palette_error);
    else { config_palette_builtin(CONFIG_DARK, &r->staged_palette); r->staged_palette_error.code = rc; }
    if (!rc) { r->palette_noticed = false; return PALETTE_OK; }
    if (!r->palette_noticed && rc != CONFIG_ERR_BUSY) {
        r->palette_noticed = true;
        fprintf(stderr, "sublimite: %s: invalid system palette (error %d at byte %zu); using the built-in %s theme\n",
                r->palette_path, rc, r->staged_palette_error.offset,
                r->staged_palette.mode == CONFIG_LIGHT ? "light" : "dark");
    }
    return PALETTE_BAD;
}
static void reload_job(work_ctx *ctx)
{
    config_runtime *r = ctx->arg;
    if (work_should_stop(ctx)) return;
    if (!r->staged_ready) {
        r->staged_error = (config_error){0};
        int rc = read_settings(r, ctx);
        if (rc && !r->staged_error.code) r->staged_error.code = rc;
        r->staged_palette_status = read_palette(r, ctx);
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
    bool settings = !r->error.code && memcmp(&r->current, &r->staged, sizeof r->current);
    const config_settings *s = settings ? &r->staged : &r->current;
    /* edit-b6ai.2: the effective palette for the settings in force. */
    config_palette next; bool system = false; int palette_error = CONFIG_OK;
    config_palette_builtin(s->theme, &next);
    if (!s->theme_pinned) switch (r->staged_palette_status) {
    case PALETTE_OK: next = r->staged_palette; system = true; break;
    case PALETTE_BAD:
        if (r->staged_palette_error.code == CONFIG_ERR_BUSY) { next = r->palette; system = r->palette_system; r->requested = true; }
        else { next = r->staged_palette; palette_error = r->staged_palette_error.code; }
        break;
    case PALETTE_ABSENT: /* the theme directory is briefly absent mid-switch */
        if (r->palette_system) { next = r->palette; system = true; }
        break;
    default: break;
    }
    /* edit-b6ai.3: the system font follows the same switch as the palette. */
    bool palette = memcmp(&next, &r->palette, sizeof next) != 0 || system != r->palette_system;
    r->palette_error = palette_error;
    if (settings || palette) {
        if (r->generation == UINT64_MAX) { r->error.code = CONFIG_ERR_LIMIT; return; }
        if (settings) r->current = r->staged;
        r->palette = next; r->generation++;
    }
    r->palette_system = system;
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
    return config_runtime_open_palette(out, path, NULL);
}
int config_runtime_open_palette(config_runtime **out, const char *path, const char *palette)
{
    return config_runtime_open_follow(out, path, palette, NULL);
}
int config_runtime_open_follow(config_runtime **out, const char *path, const char *palette, const char *fontconf)
{
    if (!out) return CONFIG_ERR_ARG;
    *out = NULL;
    config_runtime *r = config_runtime_alloc(_Alignof(config_runtime), sizeof *r);
    if (!r) return CONFIG_ERR_IO;
    memset(r, 0, sizeof *r); r->watchfd = -1; r->watch.wd = -1;
    r->palette_watch[0].wd = r->palette_watch[1].wd = -1;
    r->fonts_watch[0].wd = r->fonts_watch[1].wd = -1;
    config_palette_builtin(CONFIG_DARK, &r->palette); /* config_defaults' theme */
    int rc = CONFIG_OK;
    if (path) {
        if (!os_path_is_absolute(path) || strlen(path) >= sizeof r->path) rc = CONFIG_ERR_ARG;
        else strcpy(r->path, path);
    } else rc = config_path(r->path, sizeof r->path);
    if (!rc && palette) {
        const char *slash = strrchr(palette, '/');
        size_t dir = slash ? (size_t)(slash - palette) : 0;
        if (!os_path_is_absolute(palette) || !dir || strlen(palette) >= sizeof r->palette_path ||
            dir + sizeof ".name" > sizeof r->palette_name) rc = CONFIG_ERR_ARG;
        else {
            strcpy(r->palette_path, palette);
            memcpy(r->palette_dir, palette, dir); r->palette_dir[dir] = 0;
            memcpy(r->palette_name, palette, dir); strcpy(r->palette_name + dir, ".name");
        }
    }
    if (!rc && fontconf) {
        /* fonts.conf lives in <dir>; <dir> itself must have a parent entry. */
        const char *slash = strrchr(fontconf, '/');
        size_t dir = slash ? (size_t)(slash - fontconf) : 0;
        const char *up = NULL; /* the directory's own parent separator */
        for (size_t i = dir; i-- > 0;) if (fontconf[i] == '/') { up = fontconf + i; break; }
        if (!os_path_is_absolute(fontconf) || !dir || !up || up == fontconf || strlen(fontconf) >= sizeof r->fontconf_path) rc = CONFIG_ERR_ARG;
        else {
            strcpy(r->fontconf_path, fontconf);
            memcpy(r->fontconf_dir, fontconf, dir); r->fontconf_dir[dir] = 0;
        }
    }
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
    file_watch_path_stop(&r->palette_watch[0]); file_watch_path_stop(&r->palette_watch[1]);
    file_watch_path_stop(&r->fonts_watch[0]); file_watch_path_stop(&r->fonts_watch[1]);
    if (r->pool_ready) work_pool_shutdown(&r->pool);
    edit_arena_free(&r->arena); config_runtime_free(r);
}
const os_wake *config_runtime_wake(const config_runtime *r) { return r ? work_pool_wake(&r->pool) : NULL; }
#if !defined(_WIN32)
int config_runtime_eventfd(const config_runtime *r) { return r ? work_pool_eventfd(&r->pool) : -1; }
#endif
int config_runtime_watchfd(const config_runtime *r) { return r ? r->watchfd : -1; }
/* edit-b6ai.3: keep the narrowest watch that can see fonts.conf appear or
 * change. While the fontconfig directory exists, only it is watched (events
 * for its entries). While it is missing, its entry in the parent is watched
 * (a busy directory such as ~/.config), and dropped again as soon as the
 * directory can be watched. Any notice, and every (re)start of the file
 * watch, bumps fonts_generation: the file may have appeared in between. */
static void fonts_sync(config_runtime *r, bool first)
{
    file_path_watch *conf = &r->fonts_watch[1], *entry = &r->fonts_watch[0];
    if (conf->registered && conf->wd >= 0) return;
    int fd = file_watch_path_start(conf, r->fontconf_path);
    if (fd >= 0) {
        file_watch_path_stop(entry);
        if (!first) r->fonts_generation++;
    } else if (!entry->registered || entry->wd < 0) fd = file_watch_path_start(entry, r->fontconf_dir);
    if (fd >= 0 && r->watchfd < 0) r->watchfd = fd;
}
static void fonts_tick(config_runtime *r)
{
    if (!r->fontconf_path[0] || !r->palette_path[0] || !r->received) return;
    bool first = !r->fonts_watch_tried;
    r->fonts_watch_tried = true;
    bool noticed = file_watch_path_take(&r->fonts_watch[0]);
    noticed = file_watch_path_take(&r->fonts_watch[1]) || noticed;
    if (first || noticed) fonts_sync(r, first);
    if (noticed) r->fonts_generation++;
}
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
    /* edit-b6ai.2: palette watches start once after the first read; a later
     * start only follows a notification (the parent went away), so a box
     * without Omarchy state never retries: no idle work. */
    if (r->received && r->palette_path[0] && !r->palette_watch_tried) {
        r->palette_watch_tried = true;
        int a = file_watch_path_start(&r->palette_watch[0], r->palette_dir);
        int b = file_watch_path_start(&r->palette_watch[1], r->palette_name);
        if (a >= 0 || b >= 0) { if (r->watchfd < 0) r->watchfd = a >= 0 ? a : b; r->requested = true; }
    }
    for (size_t i = 0; i < 2u; i++) if (file_watch_path_take(&r->palette_watch[i])) {
        r->requested = true;
        if (r->palette_watch[i].wd < 0)
            (void)file_watch_path_start(&r->palette_watch[i], i ? r->palette_name : r->palette_dir);
    }
    fonts_tick(r);
    request(r);
    return before != r->generation;
}
const config_settings *config_runtime_current(const config_runtime *r) { return r ? &r->current : NULL; }
bool config_runtime_system_font(const config_runtime *r)
{
    return r && r->fontconf_path[0] && r->palette_system && !r->current.theme_pinned;
}
uint64_t config_runtime_fonts_generation(const config_runtime *r) { return r ? r->fonts_generation : 0; }
const config_palette *config_runtime_palette(const config_runtime *r) { return r ? &r->palette : NULL; }
int config_runtime_palette_error(const config_runtime *r) { return r ? r->palette_error : CONFIG_ERR_ARG; }
uint64_t config_runtime_generation(const config_runtime *r) { return r ? r->generation : 0; }
config_error config_runtime_error(const config_runtime *r) { return r ? r->error : (config_error){CONFIG_ERR_ARG, 0}; }
bool config_runtime_busy(const config_runtime *r)
{
    return r && (r->requested || (r->handle.epoch && (!r->received || !work_handle_finished(&r->pool, r->handle))));
}
