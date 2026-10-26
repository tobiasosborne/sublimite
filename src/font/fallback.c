/* Shared fallback mailbox protocol and arena file loading. Platform discovery
 * lives in discovery_posix.c / discovery_win.c and runs only on a worker. */
#include "font/font.h"
#include "font/file.h"
#include "font/selection.h"
#include "font/discovery.h"
#include <stdio.h>
#include <string.h>

void font_fallback_reset(font_fallback *fb)
{
    atomic_store_explicit(&fb->done, 0u, memory_order_release);
    fb->cjk[0] = 0;
    fb->emoji[0] = 0;
    fb->cjk_index = 0;
    fb->emoji_index = 0;
    fb->have_fontconfig = 0;
    fb->elapsed_ns = 0;
    fb->staged_epoch = 0; fb->discovery_program = NULL;
    atomic_store(&fb->child_pid, 0u); atomic_store(&fb->cancellation_polls, 0u);
}

void font_fallback_job(work_ctx *c)
{
    if (work_should_stop(c)) return;
    font_fallback *fb = c->arg;
    font_fallback_result local;
    if (!font_fallback_isolated(fb, &local, c) || work_should_stop(c)) return;
    fb->staged = local;
    fb->staged_generation = c->generation;
    fb->staged_slot = (uint32_t)(c->slot - c->pool->slots); fb->staged_epoch = c->epoch;
    work_msg msg = {0}; msg.kind = FONT_FALLBACK_MSG_KIND; msg.generation = c->generation;
    uintptr_t identity = (uintptr_t)fb; memcpy(msg.data, &identity, sizeof identity);
    /* No reserved completion lane in work: wait for UI drain without losing
     * the terminal event. Cancellation suppresses both publication and adoption. */
    while (!work_should_stop(c)) {
        if (work_publish(c, &msg)) return;
        os_sleep_ns(UINT64_C(1000000));
    }
}

int font_fallback_event(font_fallback *fb, const work_msg *msg)
{
    if (!fb || !msg || msg->kind != FONT_FALLBACK_MSG_KIND) return 0;
    uintptr_t identity = 0; memcpy(&identity, msg->data, sizeof identity);
    if (identity != (uintptr_t)fb || atomic_load_explicit(&fb->done, memory_order_acquire) != 0u ||
        msg->generation != fb->staged_generation || msg->slot_ != fb->staged_slot ||
        msg->epoch_ != fb->staged_epoch || !fb->staged_epoch) return 0;
    memcpy(fb->cjk, fb->staged.cjk, sizeof fb->cjk);
    memcpy(fb->emoji, fb->staged.emoji, sizeof fb->emoji);
    fb->cjk_index = fb->staged.cjk_index; fb->emoji_index = fb->staged.emoji_index;
    fb->have_fontconfig = fb->staged.have_fontconfig;
    fb->elapsed_ns = fb->staged.elapsed_ns; fb->worker = fb->staged.worker;
    atomic_store_explicit(&fb->done, 1u, memory_order_release);
    return 1;
}

unsigned char *font_load_stream(FILE *fp, edit_arena *arena, size_t *len)
{
    if (len) *len = 0;
    if (!fp || !arena || !len) return NULL;
    edit_arena_mark_t mark = edit_arena_mark(arena);
    unsigned char *buf = NULL;
    long n = 0;
    if (fseek(fp, 0, SEEK_END) == 0) n = ftell(fp);
    if (n > 0 && fseek(fp, 0, SEEK_SET) == 0) {
        buf = edit_arena_alloc(arena, (size_t)n, 16);
        if (buf && fread(buf, 1, (size_t)n, fp) != (size_t)n) buf = NULL;
    }
    if (buf) *len = (size_t)n;
    else edit_arena_reset_to_mark(arena, mark);
    return buf;
}
unsigned char *font_load_file(const char *path, edit_arena *arena, size_t *len)
{
    if (len) *len = 0;
    if (!path || !arena || !len) return NULL;
    os_file file;
    if (os_file_open(&file, path, OS_OPEN_READ) != OS_OK) return NULL;
    os_stat st;
    edit_arena_mark_t mark = edit_arena_mark(arena);
    unsigned char *buf = NULL;
    if (os_file_stat(&file, &st) == OS_OK && st.kind == OS_KIND_FILE &&
        st.size > 0 && st.size <= SIZE_MAX) {
        buf = edit_arena_alloc(arena, (size_t)st.size, 16);
        size_t used = 0;
        while (buf && used < st.size) {
            size_t got = 0;
            if (os_file_read(&file, buf + used, (size_t)st.size - used, &got) != OS_OK || !got) buf = NULL;
            else used += got;
        }
        if (buf) *len = (size_t)st.size;
    }
    os_file_close(&file);
    if (!buf) edit_arena_reset_to_mark(arena, mark);
    return buf;
}

/* edit-6e2: one font.fallbacks entry to a file + face. Worker-only. */
int font_chain_resolve(const char *entry, char *path, size_t cap, uint32_t *index)
{
    if (!entry || !path || !cap || !index) return FONT_ERR_ARG;
    path[0] = 0; *index = 0;
    static const char file_prefix[] = "file:";
    if (!strncmp(entry, file_prefix, sizeof file_prefix - 1u)) {
        /* Same PATH[#INDEX] grammar as font.file; only the path is kept. */
        font_selection parsed = {0};
        const char *file = entry + sizeof file_prefix - 1u;
        if (!*file || font_selection_parse(&parsed, NULL, file) || !parsed.file[0] ||
            strlen(parsed.file) >= cap) return FONT_ERR_ARG;
        os_stat st;
        if (os_stat_path(parsed.file, 0, &st) != OS_OK || st.kind != OS_KIND_FILE || !st.size) return FONT_ERR_INIT;
        memcpy(path, parsed.file, strlen(parsed.file) + 1u); *index = parsed.index;
        return FONT_OK;
    }
    /* (P) pattern = entry + the colour exclusion suffix + NUL. */
    static const char mono[] = ":color=false";
    char pattern[FONT_FALLBACK_PATH_MAX + sizeof mono];
    size_t n = strlen(entry);
    if (!n || n >= FONT_FALLBACK_PATH_MAX) return FONT_ERR_ARG;
    memcpy(pattern, entry, n); memcpy(pattern + n, mono, sizeof mono);
    return font_selection_resolve(pattern, path, cap, index);
}
