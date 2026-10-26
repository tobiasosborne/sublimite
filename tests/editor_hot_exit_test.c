/* Hot-exit acceptance: trial count is the bead's 100 (G); script length is
 * that same count, kill point = seeded PRNG % script length. Geometry derives
 * from the status label plus tab/text/status rows. No latency is measured. */
#include "editor/private.h"
#include "trace/trace.h"
#include <dirent.h>
#include <errno.h>
#include <signal.h>
#include <sched.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include "../vendor/xkbcommon/xkbcommon-keysyms.h"
#define T(c) do { if (!(c)) { fprintf(stderr, "editor_hot_exit_test:%d: FAIL %s\n", __LINE__, #c); return 1; } } while (0)
#define HOT_LINE "abcdefghijklmnopqrstuvwxyz abcdefghijklmnopqrstuvwxyz abcdefghijklmnopqrstuvwxyz"
/* G: bead acceptance; three distinct tabs distinguish outgoing/active/survivor
 * order; text reservation = longest corpus literal + maximum typing script. */
enum { TRIALS = 100, LIVE_TABS = 3, TEXT_CAP = TRIALS + sizeof("third\nline\nlast: " HOT_LINE) };
typedef struct saved_tab { view_state state; size_t len; bool dirty; uint8_t text[TEXT_CAP]; } saved_tab;
typedef struct saved_session { size_t count, active; render_dims dims; uint32_t width, height; saved_tab tabs[LIVE_TABS]; } saved_session;
static int turn(editor *e)
{ int rc = editor_step(e, 0); return rc == EDITOR_OK || rc == EDITOR_MORE ? 0 : rc; }
static int settle(editor *e)
{
    uint64_t until = trace_now_ns() + EDITOR_IDLE_NS; /* G: existing idle policy watchdog */
    do { T(turn(e) == 0); T(trace_now_ns() < until); sched_yield(); }
    while (editor_get_stats(e).pending || editor_get_stats(e).render_active);
    return 0;
}
static int key(editor *e, uint32_t symbol, uint16_t mods, uint8_t byte)
{
    plat_event ev = {.kind = PLAT_EV_KEY, .press = true, .keysym = symbol, .mods = mods};
    if (byte) { ev.utf8[0] = byte; ev.utf8_len = 1; }
    T(editor_inject(e, &ev) == 0); return settle(e);
}
static int held_present(render_backend *backend, uint32_t frame)
{ (void)backend; (void)frame; return RENDER_ERR_BUSY; }
static int capture(editor *e, saved_session *s)
{
    s->count = tabs_count(&e->tabs); s->active = tabs_active_index(&e->tabs); s->dims = e->grid.dims;
    s->width = e->font.window_width; s->height = e->font.window_height;
    T(s->count <= LIVE_TABS);
    for (size_t i = 0; i < s->count; i++) {
        const tabs_tab *tab = editor_tab(e, i); saved_tab *out = &s->tabs[i];
        out->state = i == s->active ? editor_view(e) : tab->state;
        out->len = (size_t)piece_len(tab->buffer); out->dirty = tab->modified;
        T(out->len <= sizeof out->text); T(piece_read(tab->buffer, 0, out->text, out->len) == 0);
    }
    return 0;
}
static bool same_view(view_state a, view_state b)
{
    return a.selection.cursor == b.selection.cursor && a.selection.anchor == b.selection.anchor &&
        a.selection.preferred_col == b.selection.preferred_col && a.first_line == b.first_line &&
        a.first_byte == b.first_byte && a.visual_byte == b.visual_byte && a.hscroll == b.hscroll &&
        a.wrap == b.wrap && a.visual_end == b.visual_end && a.approximate == b.approximate;
}
static int equal(editor *e, const saved_session *want)
{
    saved_session got = {0}; T(capture(e, &got) == 0);
    T(got.count == want->count); T(got.active == want->active);
    T(got.dims.cols == want->dims.cols && got.dims.rows == want->dims.rows);
    T(got.width == want->width && got.height == want->height);
    T(e->backend->initialized);
    T(e->backend->config.dims.cols == want->dims.cols &&
        e->backend->config.dims.rows == want->dims.rows);
    for (size_t i = 0; i < want->count; i++) {
        T(got.tabs[i].len == want->tabs[i].len);
        T(!memcmp(got.tabs[i].text, want->tabs[i].text, want->tabs[i].len));
        T(got.tabs[i].dirty == want->tabs[i].dirty);
        if (!same_view(got.tabs[i].state, want->tabs[i].state)) {
            view_state a = got.tabs[i].state, b = want->tabs[i].state;
            fprintf(stderr, "tab=%zu got cursor/anchor/preferred/line/byte/visual/x=%llu/%llu/%llu/%llu/%llu/%llu/%llu flags=%d/%d/%d; want=%llu/%llu/%llu/%llu/%llu/%llu/%llu flags=%d/%d/%d\n", i,
                (unsigned long long)a.selection.cursor, (unsigned long long)a.selection.anchor,
                (unsigned long long)a.selection.preferred_col, (unsigned long long)a.first_line,
                (unsigned long long)a.first_byte, (unsigned long long)a.visual_byte, (unsigned long long)a.hscroll,
                a.wrap, a.visual_end, a.approximate,
                (unsigned long long)b.selection.cursor, (unsigned long long)b.selection.anchor,
                (unsigned long long)b.selection.preferred_col, (unsigned long long)b.first_line,
                (unsigned long long)b.first_byte, (unsigned long long)b.visual_byte, (unsigned long long)b.hscroll,
                b.wrap, b.visual_end, b.approximate);
            T(same_view(a, b));
        }
    }
    return 0;
}
static int child_script(const char *path, unsigned trial, unsigned kill_point, int fd)
{
    render_backend backend = {0}; T(render_null_backend(&backend) == 0);
    editor_config cfg = {.journal_path = path, .start_empty = true,
        .cols = (uint32_t)strlen("Source changed: R reload / K keep"), .rows = LIVE_TABS,
        .tab_capacity = LIVE_TABS, .closed_capacity = LIVE_TABS, .wrap_mode = trial % 2u ? -1 : 1};
    editor *e = NULL; T(editor_open(&e, &cfg, &backend) == 0);
    const char *corpus[] = {
        "first: " HOT_LINE, "second\nline: " HOT_LINE, "third\nline\nlast: " HOT_LINE};
    for (size_t i = 0; i < LIVE_TABS; i++) {
        uint64_t id; T(editor_add_buffer(e, NULL, (const uint8_t *)corpus[i], strlen(corpus[i]), &id) == 0);
        T(settle(e) == 0); T(editor_set_cursor(e, strlen(corpus[i])) == 0); T(settle(e) == 0);
        T(key(e, XKB_KEY_Left, PLAT_MOD_SHIFT, 0) == 0);
        T(key(e, 'x', 0, 'x') == 0);
        T(key(e, XKB_KEY_Up, PLAT_MOD_SHIFT, 0) == 0);
    }
    plat_event resize = {.kind = PLAT_EV_RESIZE,
        .w = (cfg.cols + LIVE_TABS) * e->grid.dims.cell_w + e->grid.dims.cell_w / 2u,
        .h = (cfg.rows + LIVE_TABS) * e->grid.dims.cell_h + e->grid.dims.cell_h / 2u}; /* half-cell edges */
    T(editor_inject(e, &resize) == 0); T(settle(e) == 0);
    for (unsigned step = 0; step <= kill_point; step++) {
        size_t index = step % LIVE_TABS;
        T(editor_select_tab(e, index) == 0); T(settle(e) == 0);
        T(key(e, 'x', 0, (uint8_t)('a' + step % ('z' - 'a' + 1))) == 0);
        T(key(e, XKB_KEY_Up, PLAT_MOD_SHIFT, 0) == 0);
    }
    /* Reorder through the editor's pointer path, using the composed strip's
     * own slot width rather than a guessed pointer coordinate. */
    T(editor_select_tab(e, LIVE_TABS - 1u) == 0); T(settle(e) == 0);
    uint64_t moved_id = editor_tab(e, LIVE_TABS - 1u)->id;
    size_t destination = e->strip.first_tab;
    plat_event down = {.kind = PLAT_EV_BUTTON, .press = true, .code = 1,
        .x = (int32_t)((e->strip.first_col + (LIVE_TABS - 1u - e->strip.first_tab) *
            e->strip.tab_cols) * e->grid.dims.cell_w)};
    T(editor_inject(e, &down) == 0); T(settle(e) == 0);
    plat_event drag = {.kind = PLAT_EV_MOTION, .x = 0};
    T(editor_inject(e, &drag) == 0); T(settle(e) == 0);
    T(destination != LIVE_TABS - 1u && editor_tab(e, destination)->id == moved_id);
    /* Crash immediately after successful setup APIs, before their next frame.
     * Variants cover cursor, active/order and close/reopen. */
    switch (trial % (LIVE_TABS + 1u)) {
    case 0: T(editor_set_cursor(e, 0) == 0); break;
    case 1: T(editor_select_tab(e, LIVE_TABS - 1u) == 0); break;
    case 2: T(editor_close_tab(e, tabs_active_index(&e->tabs)) == 0); break;
    default: T(editor_close_tab(e, tabs_active_index(&e->tabs)) == 0); T(editor_reopen_tab(e) == 0); break;
    }
    if (trial % (LIVE_TABS + 1u) == LIVE_TABS) {
        /* A prior Present is held while the next key mutates. Text and its
         * new selection must survive even though no containing frame submits. */
        backend.ops.present = held_present; backend.active = true; backend.presented = false;
        plat_event typed = {.kind = PLAT_EV_KEY, .press = true, .keysym = 'x', .utf8_len = 1, .utf8 = {'x'}};
        T(editor_inject(e, &typed) == 0);
        uint64_t until = trace_now_ns() + EDITOR_IDLE_NS;
        do { T(turn(e) == 0); T(trace_now_ns() < until); sched_yield(); }
        while (e->queue_count || view_busy(&e->v) || e->op_count ||
            e->action != EDITOR_ACTION_NONE || layout_busy(&e->lay) || e->full_pending || e->extra_rows);
        T(backend.active && !backend.presented);
    }
    saved_session s = {0}; T(capture(e, &s) == 0);
    T(write(fd, &s, sizeof s) == (ssize_t)sizeof s);
    /* Parent kills the editor at the published script boundary. No close,
     * flush, pump or additional frame can repair missing recovery metadata. */
    for (;;) pause();
}
static int kill_trials(void)
{
    for (unsigned trial = 0; trial < TRIALS; trial++) {
        /* Reproducible LCG constants from POSIX rand example; modulo maps to
         * the bead-derived script length. Each trial gets a new kill point. */
        uint32_t random = (trial + 1u) * UINT32_C(1103515245) + UINT32_C(12345);
        unsigned point = (random >> 16) % TRIALS;
        char root[] = "build/editor-hot-exit-XXXXXX"; T(mkdtemp(root));
        char path[IPC_PATH_CAP]; T(snprintf(path, sizeof path, "%s/journal", root) > 0);
        int pipes[2]; T(pipe(pipes) == 0); pid_t child = fork(); T(child >= 0);
        if (!child) { close(pipes[0]); _exit(child_script(path, trial, point, pipes[1])); }
        close(pipes[1]); saved_session want = {0}; size_t received = 0;
        while (received < sizeof want) {
            ssize_t n = read(pipes[0], (uint8_t *)&want + received, sizeof want - received);
            if (n < 0 && errno == EINTR) continue;
            if (n <= 0) break;
            received += (size_t)n;
        }
        close(pipes[0]); T(kill(child, SIGKILL) == 0);
        int status; T(waitpid(child, &status, 0) == child); T(WIFSIGNALED(status) && WTERMSIG(status) == SIGKILL);
        T(received == sizeof want);
        render_backend backend = {0}; T(render_null_backend(&backend) == 0);
        editor_config cfg = {.journal_path = path, .start_empty = true,
            .cols = (uint32_t)strlen("Source changed: R reload / K keep"), .rows = LIVE_TABS,
            .tab_capacity = LIVE_TABS, .closed_capacity = LIVE_TABS};
        editor *e = NULL; int opened = editor_open(&e, &cfg, &backend);
        if (opened) fprintf(stderr, "trial=%u kill_point=%u recovery open returned %d\n", trial, point, opened);
        T(opened == 0);
        /* Startup is complete on return, including geometry; the first frame
         * must not expose default dimensions or replace the restored checkpoint. */
        if (equal(e, &want)) { fprintf(stderr, "trial=%u kill_point=%u\n", trial, point); editor_close(e); return 1; }
        T(settle(e) == 0); T(equal(e, &want) == 0); editor_close(e);
        DIR *dir = opendir(root); T(dir); struct dirent *entry;
        while ((entry = readdir(dir))) if (strcmp(entry->d_name, ".") && strcmp(entry->d_name, "..")) T(unlinkat(dirfd(dir), entry->d_name, 0) == 0);
        T(closedir(dir) == 0 && rmdir(root) == 0);
    }
    printf("editor_hot_exit_test: ok trials=%u (G: bead acceptance), seeded random typing kill points; text/tabs/order/active/cursor/selection/scroll/window restored before first frame\n", TRIALS);
    return 0;
}
int main(void) { trace_init(); T(trace_thread_register() >= 0); return kill_trials(); }
