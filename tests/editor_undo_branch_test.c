/* edit-37a: saved-state identity across undo branches, without a display.
 * (P) Zero denotes empty initialization, success, or the first byte/tab;
 * sizeof(string) - 1 excludes its C terminator. Fixture buffers derive from
 * the longest expected text. (E) Waits reuse EDITOR_IDLE_NS's existing watchdog,
 * without introducing a duration or making a latency claim. */
#include "editor/private.h"
#include "trace/trace.h"
#include <fcntl.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define T(c) do { if (!(c)) { fprintf(stderr, "editor_undo_branch_test:%d: FAIL %s\n", __LINE__, #c); return EXIT_FAILURE; } } while (0)

static int settle(editor *e)
{
    uint64_t deadline = trace_now_ns() + EDITOR_IDLE_NS;
    do {
        int rc = editor_step(e, 0);
        T(rc == EDITOR_OK || rc == EDITOR_MORE);
        T(trace_now_ns() < deadline);
        sched_yield();
    } while (editor_get_stats(e).pending || editor_get_stats(e).render_active || editor_save_active(e));
    return EXIT_SUCCESS;
}

static int press(editor *e, uint32_t symbol, uint16_t mods, const char *text)
{
    plat_event event = {.kind = PLAT_EV_KEY, .press = true, .keysym = symbol,
        .mods = mods, .t0_ns = trace_now_ns()};
    if (text) { event.utf8_len = (uint8_t)strlen(text); memcpy(event.utf8, text, event.utf8_len); }
    T(editor_inject(e, &event) == EDITOR_OK);
    return settle(e);
}

static int expect(editor *e, const char *text)
{
    uint8_t bytes[sizeof "xybase"];
    size_t length = strlen(text);
    T(length <= sizeof bytes);
    T(editor_length(e) == length);
    T(editor_read(e, 0, bytes, length) == EDITOR_OK);
    T(!memcmp(bytes, text, length));
    return EXIT_SUCCESS;
}

static int branch(bool discard_saved)
{
    char path[] = "build/edit-37a-source-XXXXXX";
    int fd = mkstemp(path); T(fd >= 0);
    T(write(fd, "base", strlen("base")) == (ssize_t)strlen("base"));
    T(close(fd) == 0);
    render_backend backend = {0}; T(render_null_backend(&backend) == EDITOR_OK);
    editor_config config = {.path = path,
        .cols = (uint32_t)strlen("Source changed: R reload / K keep"),
        .rows = 3}; /* (P) tab row + text row + source-status row */
    editor *e = NULL; T(editor_open(&e, &config, &backend) == EDITOR_OK);
    T(settle(e) == EXIT_SUCCESS);
    T(press(e, 'x', 0, "x") == EXIT_SUCCESS);
    T(press(e, 's', PLAT_MOD_CTRL, NULL) == EXIT_SUCCESS);
    T(!editor_tab(e, 0)->modified);
    T(expect(e, "xbase") == EXIT_SUCCESS);
    editor_buffer *owner = e->buffer;

    if (!discard_saved) T(press(e, 'y', 0, "y") == EXIT_SUCCESS);
    T(press(e, 'z', PLAT_MOD_CTRL, NULL) == EXIT_SUCCESS);
    T(press(e, 'y', 0, "y") == EXIT_SUCCESS);
    if (discard_saved) {
        bool dirty_before_replacement = editor_tab(e, 0)->modified;
        T(expect(e, "ybase") == EXIT_SUCCESS);
        char replacement[] = "build/edit-37a-replacement-XXXXXX";
        fd = mkstemp(replacement); T(fd >= 0);
        T(write(fd, "disk", strlen("disk")) == (ssize_t)strlen("disk"));
        T(close(fd) == 0); T(rename(replacement, path) == 0);
        T(file_check(owner->file, NULL));
        T(editor_check_sources(e) == EDITOR_OK); T(settle(e) == EXIT_SUCCESS);
        fprintf(stderr, "discarded saved branch: dirty_before=%s dirty_after=%s owner_preserved=%s source_stale=%s\n",
            dirty_before_replacement ? "true" : "false", editor_tab(e, 0)->modified ? "true" : "false",
            e->buffer == owner ? "true" : "false", e->buffer->source_stale ? "true" : "false");
        T(dirty_before_replacement);
        T(editor_tab(e, 0)->modified && e->buffer == owner && owner->source_stale);
        T(expect(e, "ybase") == EXIT_SUCCESS);
    } else {
        /* A saved state at the branch point remains reachable and clean. */
        T(editor_tab(e, 0)->modified && !owner->saved_lost);
        T(press(e, 'z', PLAT_MOD_CTRL, NULL) == EXIT_SUCCESS);
        T(!editor_tab(e, 0)->modified); T(expect(e, "xbase") == EXIT_SUCCESS);
        T(press(e, 'y', PLAT_MOD_CTRL, NULL) == EXIT_SUCCESS);
        T(editor_tab(e, 0)->modified); T(expect(e, "xybase") == EXIT_SUCCESS);
    }
    editor_close(e); T(unlink(path) == 0);
    puts(discard_saved ? "editor_undo_branch_test: discarded saved branch stays dirty and preserves unsaved text on external replacement passed" :
        "editor_undo_branch_test: retained saved branch point stays clean on undo and dirty on redo passed");
    return EXIT_SUCCESS;
}

int main(void)
{
    trace_init(); T(trace_thread_register() >= 0);
    T(branch(true) == EXIT_SUCCESS);
    T(branch(false) == EXIT_SUCCESS);
    return EXIT_SUCCESS;
}
