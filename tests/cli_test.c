/* Compile the production entry point here so make check covers it with ASan. */
#include "editor/private.h"
#include "raster/raster.h"
static int test_backend_select(render_backend *backend, const char *name);
static int test_editor_open(editor **out, const editor_config *config, render_backend *backend);
static int test_editor_flush(editor *e);
#define editor_backend_select test_backend_select
#define editor_open test_editor_open
#define editor_flush test_editor_flush
#define main editor_cli_main
#include "../src/main.c"
#undef main
#undef editor_backend_select
#undef editor_open
#undef editor_flush
#include "trace/trace_fmt.h"
#include <dirent.h>
#include "ipc_isolation.h"
#include <signal.h>
#include <poll.h>
#include <sys/wait.h>
#include <sys/file.h>
#include <time.h>
#include <xcb/xcb.h>
/* (G, fixture) capture capacity = sizeof the typed marker + original text. */
typedef struct hot_snapshot {
    size_t count, active, len;
    view_state view;
    uint32_t width, height;
    uint8_t text[sizeof "xhello\n"];
    char journal_name[sizeof "session-XXXXXX"]; /* (P) journal_session_create's mkstemp template. */
} hot_snapshot;
/* edit-djg (P): observe the production main's explicit flush, after the UI
 * loop returns and before close. Pipe payload size follows journal_stats. */
static int test_editor_flush(editor *e)
{
    int rc = editor_flush(e);
    const char *descriptor = getenv("CLI_TEST_SIGNAL_FLUSH_FD");
    if (descriptor) {
        journal_stats stats = journal_get_stats(e->journal);
        if (write(atoi(descriptor), &stats, sizeof stats) != (ssize_t)sizeof stats) return EDITOR_ERR_IO;
    }
    return rc;
}
static int test_editor_open(editor **out, const editor_config *config, render_backend *backend)
{
    int rc = editor_open(out, config, backend);
    const char *descriptor = getenv("CLI_TEST_HOT_SNAPSHOT_FD");
    if (!rc && descriptor) {
        editor *e = *out;
        hot_snapshot s = {.count = editor_get_stats(e).tabs, .active = editor_get_stats(e).active_tab,
            .len = (size_t)editor_length(e), .view = editor_view(e),
            .width = e->font.window_width, .height = e->font.window_height};
        int fd = atoi(descriptor);
        if (s.len > sizeof s.text || editor_read(e, 0, s.text, s.len) ||
            write(fd, &s, sizeof s) != (ssize_t)sizeof s) rc = EDITOR_ERR_IO;
        close(fd);
    }
    return rc;
}
static int delayed_backend_init(render_backend *backend, const render_config *config)
{
    struct timespec remaining = {0, 250000000};
    while (nanosleep(&remaining, &remaining) && errno == EINTR) {}
    render_backend original = {0};
    int rc = render_cpu_backend(&original);
    if (rc) return rc;
    /* Restore implementation identity before production runtime diagnostics. */
    backend->ops.init = original.ops.init;
    rc = original.ops.init(backend, config);
    if (!rc) { (void)trace_thread_register(); trace_record(TRACE_T3_RENDER_DONE, 0); }
    return rc;
}
static int test_backend_select(render_backend *backend, const char *name)
{
    if (!getenv("CLI_TEST_DELAY_BACKEND")) return editor_backend_select(backend, name);
    int rc = render_cpu_backend(backend);
    if (!rc) backend->ops.init = delayed_backend_init;
    return rc;
}
#define T(c) do { if (!(c)) { fprintf(stderr, "cli_test:%d: FAIL %s\n", __LINE__, #c); goto cleanup; } } while (0)
static xcb_atom_t intern(xcb_connection_t *c, const char *name)
{
    xcb_intern_atom_reply_t *r = xcb_intern_atom_reply(c,
        xcb_intern_atom(c, 0, (uint16_t)strlen(name), name), NULL);
    xcb_atom_t a = r ? r->atom : XCB_ATOM_NONE; free(r); return a;
}
static xcb_window_t find_window(xcb_connection_t *c, xcb_window_t root, xcb_atom_t pid_atom, pid_t pid)
{
    xcb_query_tree_reply_t *tree = xcb_query_tree_reply(c, xcb_query_tree(c, root), NULL);
    if (!tree) return XCB_WINDOW_NONE;
    xcb_window_t found = XCB_WINDOW_NONE, *children = xcb_query_tree_children(tree);
    for (int i = 0; i < xcb_query_tree_children_length(tree); i++) {
        xcb_get_property_reply_t *r = xcb_get_property_reply(c,
            xcb_get_property(c, 0, children[i], pid_atom, XCB_ATOM_CARDINAL, 0, 1), NULL);
        if (r && r->format == 32 && xcb_get_property_value_length(r) == 4 &&
            *(uint32_t *)xcb_get_property_value(r) == (uint32_t)pid) found = children[i];
        free(r);
    }
    free(tree); return found;
}
/* EINTR must not strand the child, including when an assertion fails. */
static pid_t child_wait(pid_t child, int *status, int options)
{
    pid_t waited;
    do { waited = waitpid(child, status, options); } while (waited < 0 && errno == EINTR);
    return waited;
}
static void pause_poll(void)
{
    struct timespec pause = {0, 10000000}; (void)nanosleep(&pause, NULL);
}
/* (P) DISPLAY comes from the locked private Xvfb fixture, never EDIT_DISPLAY.
 * Refusal probes name the real display but the old launcher redirects them to
 * that private fixture. No probe may connect to the user's display. Statuses
 * are the CLI contract: EXIT_SUCCESS on close, EXIT_FAILURE on refusal; flags
 * and descriptor indexes use the POSIX boolean/pipe conventions. The watchdog
 * is the existing EDITOR_IDLE_NS, not an additional timing budget. */
static int display_policy_child(const char *display, bool refuse, bool legacy_allow)
{
    int result = EXIT_FAILURE, status = 0;
    pid_t child = -1;
    xcb_connection_t *c = NULL;
    FILE *errors = NULL;
    char *line = NULL; size_t capacity = 0;
    const char *private_display = getenv("DISPLAY");
    const char *root = test_ipc_isolate(); T(root);
    T(private_display && !edit_display_is_real(private_display));
    c = xcb_connect(private_display, NULL); T(c && !xcb_connection_has_error(c));
    xcb_screen_t *screen = xcb_setup_roots_iterator(xcb_get_setup(c)).data; T(screen);
    xcb_atom_t pid_atom = intern(c, "_NET_WM_PID");
    errors = tmpfile(); T(errors);
    child = fork(); T(child >= 0);
    if (!child) {
        if (dup2(fileno(errors), STDERR_FILENO) < 0 ||
            setenv("XDG_DATA_HOME", root, 1) || setenv("XDG_CONFIG_HOME", root, 1) ||
            setenv("DISPLAY", display ? display : private_display, 1) ||
            setenv("EDIT_DISPLAY", private_display, 1) ||
            setenv("EDIT_BACKEND", "raster", 1) || unsetenv("WAYLAND_DISPLAY") ||
            unsetenv("WAYLAND_SOCKET") || unsetenv("EDIT_TRACE_DUMP") ||
            (refuse ? setenv("EDIT_REFUSE_REAL_DISPLAY", "1", 1) : unsetenv("EDIT_REFUSE_REAL_DISPLAY")) ||
            (legacy_allow ? setenv("EDIT_ALLOW_REAL_DISPLAY", "1", 1) : unsetenv("EDIT_ALLOW_REAL_DISPLAY")))
            _exit(EXIT_FAILURE);
        /* Poisoning the alternate demonstrates that DISPLAY itself suffices. */
        if (!refuse && setenv("EDIT_DISPLAY", "invalid-private-alternate", 1)) _exit(EXIT_FAILURE);
        execl("build/sublimite", "sublimite", "--new-instance", (char *)NULL);
        _exit(EXIT_FAILURE);
    }
    uint64_t deadline = trace_now_ns() + EDITOR_IDLE_NS;
    xcb_window_t window = XCB_WINDOW_NONE; pid_t waited = 0;
    while (!waited && !window && trace_now_ns() < deadline) {
        window = find_window(c, screen->root, pid_atom, child);
        waited = child_wait(child, &status, WNOHANG);
        if (!waited && !window) pause_poll();
    }
    if (window) {
        xcb_client_message_event_t close_event = {.response_type = XCB_CLIENT_MESSAGE, .format = 32,
            .window = window, .type = intern(c, "WM_PROTOCOLS"),
            .data.data32 = {intern(c, "WM_DELETE_WINDOW")}}; /* (P) X11 ClientMessage format is bits per item. */
        (void)xcb_send_event(c, 0, window, 0, (const char *)&close_event); (void)xcb_flush(c);
    }
    while (!waited && trace_now_ns() < deadline) {
        waited = child_wait(child, &status, WNOHANG);
        if (!waited) pause_poll();
    }
    T(waited == child); child = -1;
    bool guarded = false;
    rewind(errors);
    while (getline(&line, &capacity, errors) >= 0)
        if (strstr(line, "display guard: real display") && strstr(line, "refused")) guarded = true;
    if (refuse) {
        T(!window && guarded && WIFEXITED(status) && WEXITSTATUS(status) == EXIT_FAILURE);
        puts("cli_test: explicit real-display refusal exits with guard diagnostic");
    } else {
        T(window && !guarded && WIFEXITED(status) && WEXITSTATUS(status) == EXIT_SUCCESS);
        puts(legacy_allow ? "cli_test: legacy display opt-in remains compatible" :
            "cli_test: default production launch uses DISPLAY without an opt-in or alternate");
    }
    result = EXIT_SUCCESS;
cleanup:
    if (child > 0) { (void)kill(child, SIGKILL); (void)child_wait(child, NULL, 0); }
    if (errors) (void)fclose(errors);
    free(line);
    if (c) xcb_disconnect(c);
    return result;
}
static int display_policy(void)
{
    if (display_policy_child(NULL, false, false)) return EXIT_FAILURE;
    if (display_policy_child(NULL, false, true)) return EXIT_FAILURE;
    if (display_policy_child(":0", true, false)) return EXIT_FAILURE;
    return EXIT_SUCCESS;
}
static ipc_result decoy_open(const ipc_request *request, ipc_token token, void *ctx)
{
    (void)request; (void)token;
    unsigned *calls = ctx; ++*calls;
    return IPC_OK;
}
static int same_display_handoff(const char *source_path, ipc_server *decoy)
{
    pid_t child = fork(); if (child < 0) return 1;
    if (!child) {
        char *args[] = {"sublimite", (char *)source_path, NULL};
        _exit(editor_cli_main(2, args));
    }
    unsigned calls = 0; int status = 0; pid_t waited = 0;
    uint64_t deadline = trace_now_ns() + UINT64_C(30000000000);
    while (!waited && trace_now_ns() < deadline) {
        if (ipc_server_drain(decoy, decoy_open, &calls) != IPC_OK) break;
        waited = child_wait(child, &status, WNOHANG);
        if (!waited) pause_poll();
    }
    if (waited != child) { (void)kill(child, SIGKILL); (void)child_wait(child, NULL, 0); return 1; }
    if (!WIFEXITED(status) || WEXITSTATUS(status) || calls != 1) return 1;
    puts("cli_test: same-display launcher handoff preserved; child_exit=0 handoffs=1");
    return 0;
}
/* 0 = passed, 1 = failed. Display preflight is a required fixture. */
static int session(const char *source_path, const char *dump, bool fail_dump, ipc_server *decoy)
{
    int result = 1, status = 0;
    unsigned handed_off = 0;
    pid_t child = -1;
    xcb_connection_t *c = NULL;
    xcb_get_property_reply_t *title = NULL;
    FILE *f = NULL;
    trace_rec *records = NULL;
    T(setenv("EDIT_TRACE_DUMP", dump, 1) == 0);
    child = fork(); T(child >= 0);
    if (!child) {
        /* Deterministic unhealthy-child regression probe. */
        if (getenv("CLI_TEST_CHILD_EXIT")) _exit(23);
        if (getenv("CLI_TEST_CHILD_STOP")) (void)raise(SIGSTOP);
        char *args[] = {"sublimite", (char *)source_path, NULL};
        _exit(editor_cli_main(2, args));
    }
    c = xcb_connect(":99", NULL); T(c && !xcb_connection_has_error(c));
    xcb_screen_t *screen = xcb_setup_roots_iterator(xcb_get_setup(c)).data; T(screen != NULL);
    xcb_atom_t pid_atom = intern(c, "_NET_WM_PID");
    xcb_window_t window = XCB_WINDOW_NONE;
    double load1 = 0;
    f = fopen("/proc/loadavg", "r");
    if (f) { if (fscanf(f, "%lf", &load1) != 1 || !(load1 >= 0)) load1 = 0; (void)fclose(f); f = NULL; }
    /* At least 10 s; cap scaling at 50 s even on an overloaded host. */
    unsigned wait_seconds = 10u + 2u * (unsigned)(load1 < 20 ? load1 : 20);
    /* Test-only injection: never poll, even if the child creates a window. */
    uint64_t deadline = trace_now_ns() +
        (getenv("CLI_TEST_FORCE_WINDOW_TIMEOUT") ? 0 : (uint64_t)wait_seconds * UINT64_C(1000000000));
    while (!window && trace_now_ns() < deadline) {
        if (decoy) T(ipc_server_drain(decoy, decoy_open, &handed_off) == IPC_OK);
        pid_t waited = child_wait(child, &status, WNOHANG | WUNTRACED);
        if (waited == child && (WIFEXITED(status) || WIFSIGNALED(status))) child = -1;
        if (waited != 0) fprintf(stderr, "cli_test: editor exited or stopped before window (status=%d)\n", status);
        if (waited != 0) fprintf(stderr, "cli_test: decoy handoffs=%u window=%u child_exit=%d\n",
            handed_off, window, WIFEXITED(status) ? WEXITSTATUS(status) : -1);
        T(waited == 0);
        T(!xcb_connection_has_error(c));
        window = find_window(c, screen->root, pid_atom, child);
        if (!window) pause_poll();
    }
    if (!window) {
        pid_t waited = child_wait(child, &status, WNOHANG | WUNTRACED);
        if (waited == child && (WIFEXITED(status) || WIFSIGNALED(status))) child = -1;
        T(waited == 0 && kill(child, 0) == 0 && !xcb_connection_has_error(c));
        fprintf(stderr, "cli_test: FAIL: window not created within %u s (load1=%.2f)%s\n",
            getenv("CLI_TEST_FORCE_WINDOW_TIMEOUT") ? 0u : wait_seconds, load1,
            getenv("CLI_TEST_FORCE_WINDOW_TIMEOUT") ? " [forced test timeout]" : "");
        goto cleanup;
    }
    T(window != XCB_WINDOW_NONE);
    T(handed_off == 0);
    puts("cli_test: decoy bypassed; child window created; handoffs=0");
    title = xcb_get_property_reply(c,
        xcb_get_property(c, 0, window, intern(c, "_NET_WM_NAME"), XCB_GET_PROPERTY_TYPE_ANY, 0, 64), NULL);
    T(!getenv("CLI_TEST_FORCE_ASSERT_FAILURE"));
    T(title && title->type == intern(c, "UTF8_STRING"));
    T(xcb_get_property_value_length(title) == (int)strlen("sublimité"));
    T(memcmp(xcb_get_property_value(title), "sublimité", strlen("sublimité")) == 0); free(title); title = NULL;
    xcb_client_message_event_t close_event = {.response_type = XCB_CLIENT_MESSAGE, .format = 32,
        .window = window, .type = intern(c, "WM_PROTOCOLS"),
        .data.data32 = {intern(c, "WM_DELETE_WINDOW"), 0, 0, 0, 0}};
    (void)xcb_send_event(c, 0, window, 0, (const char *)&close_event); (void)xcb_flush(c);
    deadline = trace_now_ns() + (uint64_t)wait_seconds * UINT64_C(1000000000);
    pid_t waited;
    do { waited = child_wait(child, &status, WNOHANG); if (!waited) pause_poll(); }
    while (!waited && trace_now_ns() < deadline);
    T(waited == child);
    child = -1;
    T(WIFEXITED(status) && WEXITSTATUS(status) == (fail_dump ? 1 : 0));
    if (!fail_dump) {
        f = fopen(dump, "rb"); T(f != NULL); size_t count = 0;
        T(trace_fmt_load(f, &records, &count) == 0 && count > 0);
        int closed = fclose(f); f = NULL; T(closed == 0);
        unsigned ingress = 0, connected = 0, mapped = 0;
        uint64_t startup = 0, connection = 0, map = 0;
        for (size_t i = 0; i < count; i++) {
            if (records[i].frame_id != 0) continue;
            if (records[i].ev == TRACE_T0_INGRESS) { ingress++; startup = records[i].ns; }
            if (records[i].ev == TRACE_T1_DEQUEUE) { connected++; connection = records[i].ns; }
            if (records[i].ev == TRACE_T4_PRESENT_SUBMITTED) { mapped++; map = records[i].ns; }
        }
        T(ingress == 1 && connected == 1 && mapped == 1);
        T(startup && startup <= connection && connection <= map);
        if (getenv("CLI_TEST_DELAY_BACKEND")) {
            unsigned delayed = 0;
            for (size_t i = 0; i < count; i++)
                if (records[i].frame_id == 0 && records[i].ev == TRACE_T3_RENDER_DONE && records[i].ns >= map) delayed++;
            T(delayed == 1);
            fputs("cli_test: §36 delayed production backend initialization completed\n", stderr);
        }
        fputs("cli_test: §37 production startup trace endpoints passed\n", stderr);
    }
    result = 0;
cleanup:
    free(title); free(records);
    if (f) (void)fclose(f);
    if (c) xcb_disconnect(c);
    if (child > 0) { (void)kill(child, SIGKILL); (void)child_wait(child, NULL, 0); }
    return result;
}
/* Replay a live read-only byte image; journal_replay_file may repair its tail
 * and must never be used while the child is still appending. */
static uint64_t hot_u64(const uint8_t *p)
{ uint64_t v = 0; for (unsigned i = 0; i < 8; i++) v |= (uint64_t)p[i] << (8u * i); return v; }
static int hot_record(void *ctx, const journal_record *r)
{
    hot_snapshot *s = ctx; const uint8_t *p = r->data;
    if (r->type == JOURNAL_BASE) {
        journal_base base; char path[IPC_PATH_CAP];
        if (journal_decode_base(r, &base, path, sizeof path) || base.size > sizeof s->text) return 1;
        s->len = (size_t)base.size;
        if (s->len) {
            int fd = open(path, O_RDONLY); if (fd < 0) return 1;
            ssize_t n = read(fd, s->text, s->len); close(fd);
            if (n != (ssize_t)s->len) return 1;
        }
    } else if (r->type == JOURNAL_INSERT || r->type == JOURNAL_DELETE) {
        uint64_t off = hot_u64(p); if (off > s->len) return 1;
        if (r->type == JOURNAL_INSERT) {
            size_t n = r->size - sizeof(uint64_t); if (n > sizeof s->text - s->len) return 1;
            memmove(s->text + off + n, s->text + off, s->len - (size_t)off);
            memcpy(s->text + off, p + sizeof(uint64_t), n); s->len += n;
        } else {
            uint64_t n = hot_u64(p + sizeof(uint64_t)); if (n > s->len - off) return 1;
            memmove(s->text + off, s->text + off + n, s->len - (size_t)(off + n)); s->len -= (size_t)n;
        }
    } else if (r->type == JOURNAL_VIEW) {
        journal_view_state v; if (journal_decode_view_state(r, &v)) return 1;
        s->view = (view_state){.selection = {v.position.cursor, v.position.anchor, v.preferred_col},
            .first_byte = v.position.scroll_byte, .first_line = v.first_line,
            .hscroll = v.position.scroll_x, .visual_byte = v.visual_byte,
            .wrap = (v.flags & JOURNAL_VIEW_WRAP) != 0,
            .approximate = (v.flags & JOURNAL_VIEW_APPROXIMATE) != 0,
            .visual_end = (v.flags & JOURNAL_VIEW_VISUAL_END) != 0};
    } else if (r->type == JOURNAL_TABS) {
        s->count = (size_t)hot_u64(p); s->active = (size_t)hot_u64(p + sizeof(uint64_t));
    } else if (r->type == JOURNAL_WINDOW) {
        uint64_t dims = hot_u64(p); s->width = (uint32_t)dims; s->height = (uint32_t)(dims >> 32);
    }
    return 0;
}
static int hot_image(const char *directory, hot_snapshot *s)
{
    DIR *dir = opendir(directory); if (!dir) return 1;
    struct dirent *entry; int result = 1;
    while ((entry = readdir(dir))) {
        if (strncmp(entry->d_name, "session-", strlen("session-")) || strchr(entry->d_name, '.')) continue;
        int fd = openat(dirfd(dir), entry->d_name, O_RDONLY); if (fd < 0) continue;
        struct stat st;
        if (!fstat(fd, &st) && st.st_size > 0 && (uint64_t)st.st_size <= SIZE_MAX) {
            size_t len = (size_t)st.st_size; uint8_t *bytes = malloc(len);
            if (bytes) {
                ssize_t n = read(fd, bytes, len); journal_replay_result rr;
                *s = (hot_snapshot){0};
                if (n == (ssize_t)len && !journal_replay_bytes(bytes, len, hot_record, s, &rr)) {
                    int named = snprintf(s->journal_name, sizeof s->journal_name, "%s", entry->d_name);
                    result = named > 0 && (size_t)named < sizeof s->journal_name ? 0 : 1;
                }
                free(bytes);
            }
        }
        close(fd); if (!result) break;
    }
    closedir(dir); return result;
}
static int hot_exit_cli(const char *source_path, const char *directory, int stop_signal)
{
    int result = 1, status = 0, pipes[2] = {-1, -1}; pid_t child = -1;
    int flush_pipe[2] = {-1, -1}, lease = -1;
    char journal_path[IPC_PATH_CAP] = "";
    xcb_connection_t *c = NULL; xcb_get_geometry_reply_t *geometry = NULL;
    xcb_get_keyboard_mapping_reply_t *mapping = NULL;
    T(unsetenv("EDIT_TRACE_DUMP") == 0);
    c = xcb_connect(":99", NULL); T(c && !xcb_connection_has_error(c));
    xcb_screen_t *screen = xcb_setup_roots_iterator(xcb_get_setup(c)).data; T(screen);
    xcb_atom_t pid_atom = intern(c, "_NET_WM_PID");
    /* edit-djg (P): a pipe has a read end and a write end; negative fd means
     * absent. Reuse the existing idle watchdog and native typing fixture. */
    if (stop_signal != SIGKILL) T(pipe(flush_pipe) == 0);
    child = fork(); T(child >= 0);
    if (!child) {
        if (stop_signal != SIGKILL) {
            close(flush_pipe[0]);
            char descriptor[sizeof(int) * 3u + 1u]; /* (P) decimal digits <= bytes * 3 plus NUL. */
            if (snprintf(descriptor, sizeof descriptor, "%d", flush_pipe[1]) <= 0 ||
                setenv("CLI_TEST_SIGNAL_FLUSH_FD", descriptor, 1)) _exit(1);
        }
        char *args[] = {"sublimite", (char *)source_path, NULL}; _exit(editor_cli_main(2, args));
    }
    if (flush_pipe[1] >= 0) { close(flush_pipe[1]); flush_pipe[1] = -1; }
    uint64_t until = trace_now_ns() + EDITOR_IDLE_NS; /* G: existing editor idle watchdog */
    xcb_window_t window = XCB_WINDOW_NONE;
    while (!window && trace_now_ns() < until) { window = find_window(c, screen->root, pid_atom, child); if (!window) pause_poll(); }
    T(window);
    geometry = xcb_get_geometry_reply(c, xcb_get_geometry(c, window), NULL); T(geometry);
    /* (G, fixture) shrink to half the existing window plus its rounding pixel. */
    uint32_t width = geometry->width / 2u + 1u, height = geometry->height / 2u + 1u;
    free(geometry); geometry = NULL;
    const uint32_t sizes[] = {width, height};
    (void)xcb_configure_window(c, window, XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT, sizes);
    const xcb_setup_t *setup = xcb_get_setup(c);
    uint8_t codes = (uint8_t)(setup->max_keycode - setup->min_keycode + 1u);
    mapping = xcb_get_keyboard_mapping_reply(c, xcb_get_keyboard_mapping(c, setup->min_keycode, codes), NULL); T(mapping);
    xcb_keysym_t *symbols = xcb_get_keyboard_mapping_keysyms(mapping); uint8_t code = 0;
    for (unsigned i = 0; i < codes; i++)
        if (symbols[i * mapping->keysyms_per_keycode] == 'x') { code = (uint8_t)(setup->min_keycode + i); break; }
    T(code); free(mapping); mapping = NULL;
    xcb_key_press_event_t typed = {.response_type = XCB_KEY_PRESS, .detail = code,
        .root = screen->root, .event = window, .same_screen = 1};
    (void)xcb_send_event(c, 0, window, XCB_EVENT_MASK_KEY_PRESS, (const char *)&typed);
    typed.response_type = XCB_KEY_RELEASE;
    (void)xcb_send_event(c, 0, window, XCB_EVENT_MASK_KEY_RELEASE, (const char *)&typed); (void)xcb_flush(c);
    hot_snapshot before = {0}; bool ready = false;
    until = trace_now_ns() + EDITOR_IDLE_NS;
    while (!ready && trace_now_ns() < until) {
        ready = !hot_image(directory, &before) && before.len == strlen("xhello\n") &&
            !memcmp(before.text, "xhello\n", before.len) && before.view.selection.cursor == strlen("x") &&
            before.view.selection.anchor == strlen("x") && before.width == width && before.height == height;
        if (!ready) pause_poll();
    }
    T(ready);
    if (stop_signal != SIGKILL) {
        char lock[IPC_PATH_CAP];
        T(snprintf(lock, sizeof lock, "%s/.%s.lock", directory, before.journal_name) > 0);
        T(snprintf(journal_path, sizeof journal_path, "%s/%s", directory, before.journal_name) > 0);
        lease = open(lock, O_RDWR | O_CLOEXEC); T(lease >= 0);
        T(flock(lease, LOCK_EX | LOCK_NB) < 0 && errno == EWOULDBLOCK);
    }
    T(kill(child, stop_signal) == 0);
    until = trace_now_ns() + EDITOR_IDLE_NS;
    pid_t stopped;
    do { stopped = child_wait(child, &status, WNOHANG); if (!stopped) pause_poll(); }
    while (!stopped && trace_now_ns() < until);
    T(stopped == child); child = -1;
    if (stop_signal == SIGKILL) T(WIFSIGNALED(status) && WTERMSIG(status) == SIGKILL);
    else {
        journal_stats flushed = {0};
        bool observed = read(flush_pipe[0], &flushed, sizeof flushed) == (ssize_t)sizeof flushed;
        close(flush_pipe[0]); flush_pipe[0] = -1;
        bool released = flock(lease, LOCK_EX | LOCK_NB) == 0;
        close(lease); lease = -1;
        /* (P) POSIX shell signal status = 128 + signal number; success = zero. */
        printf("edit-djg: %s exit=%d clean_flush=%s lease_released=%s accepted=%llu written=%llu durable=%llu\n",
            stop_signal == SIGTERM ? "SIGTERM" : "SIGINT",
            WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status),
            observed ? "yes" : "no", released ? "yes" : "no",
            (unsigned long long)flushed.accepted_sequence, (unsigned long long)flushed.written_sequence,
            (unsigned long long)flushed.durable_sequence);
        T(WIFEXITED(status) && WEXITSTATUS(status) == 0);
        T(observed && released && !flushed.error && !flushed.pending_bytes &&
            flushed.accepted_sequence == flushed.written_sequence && flushed.written_sequence == flushed.durable_sequence);
        hot_snapshot replay = {0}; journal_replay_result rr; struct stat st;
        T(stat(journal_path, &st) == 0);
        T(journal_replay_file(journal_path, hot_record, &replay, &rr) == JOURNAL_OK);
        T(!rr.corrupt && rr.valid_bytes == (uint64_t)st.st_size && replay.len == before.len &&
            !memcmp(replay.text, before.text, before.len));
    }
    T(pipe(pipes) == 0); child = fork(); T(child >= 0);
    if (!child) {
        close(pipes[0]); char descriptor[sizeof(int) * 3u + 1u]; /* decimal digits <= bytes * 3 plus NUL */
        if (snprintf(descriptor, sizeof descriptor, "%d", pipes[1]) <= 0 || setenv("CLI_TEST_HOT_SNAPSHOT_FD", descriptor, 1)) _exit(1);
        char *args[] = {"sublimite", (char *)source_path, NULL}; _exit(editor_cli_main(2, args));
    }
    close(pipes[1]); pipes[1] = -1;
    struct pollfd snapshot_ready = {.fd = pipes[0], .events = POLLIN};
    until = trace_now_ns() + EDITOR_IDLE_NS;
    ready = false;
    while (!ready && trace_now_ns() < until) {
        int polled = poll(&snapshot_ready, 1, 0);
        T(polled >= 0 || errno == EINTR);
        if (polled > 0) { ready = (snapshot_ready.revents & POLLIN) != 0; break; }
        pause_poll();
    }
    T(ready);
    hot_snapshot after = {0}; T(read(pipes[0], &after, sizeof after) == (ssize_t)sizeof after);
    close(pipes[0]); pipes[0] = -1;
    T(after.count == before.count && after.active == before.active && after.len == before.len);
    T(!memcmp(after.text, before.text, before.len));
    T(after.view.selection.cursor == before.view.selection.cursor && after.view.selection.anchor == before.view.selection.anchor);
    T(after.view.first_byte == before.view.first_byte && after.view.visual_byte == before.view.visual_byte && after.view.hscroll == before.view.hscroll);
    T(after.width == before.width && after.height == before.height);
    window = XCB_WINDOW_NONE; until = trace_now_ns() + EDITOR_IDLE_NS;
    while (!window && trace_now_ns() < until) { window = find_window(c, screen->root, pid_atom, child); if (!window) pause_poll(); }
    T(window); geometry = xcb_get_geometry_reply(c, xcb_get_geometry(c, window), NULL); T(geometry);
    T(geometry->width == width && geometry->height == height); free(geometry); geometry = NULL;
    xcb_client_message_event_t close_event = {.response_type = XCB_CLIENT_MESSAGE, .format = 32,
        .window = window, .type = intern(c, "WM_PROTOCOLS"), .data.data32 = {intern(c, "WM_DELETE_WINDOW")}};
    (void)xcb_send_event(c, 0, window, 0, (const char *)&close_event); (void)xcb_flush(c);
    until = trace_now_ns() + EDITOR_IDLE_NS; pid_t waited;
    do { waited = child_wait(child, &status, WNOHANG); if (!waited) pause_poll(); } while (!waited && trace_now_ns() < until);
    T(waited == child); child = -1; T(WIFEXITED(status) && WEXITSTATUS(status) == 0);
    puts("cli_test: termination after native typing; production relaunch restores marker/cursor/selection/scroll and actual X window geometry (G: bead display trial)");
    result = 0;
cleanup:
    free(geometry); free(mapping);
    if (pipes[0] >= 0) close(pipes[0]);
    if (pipes[1] >= 0) close(pipes[1]);
    if (flush_pipe[0] >= 0) close(flush_pipe[0]);
    if (flush_pipe[1] >= 0) close(flush_pipe[1]);
    if (lease >= 0) close(lease);
    if (child > 0) { (void)kill(child, SIGKILL); (void)child_wait(child, NULL, 0); }
    if (c) xcb_disconnect(c);
    return result;
}
static int run_cli_suite(int stop_signal)
{
    int result = 1;
    bool root_created = false, decoy_live = false;
    ipc_server decoy = {0};
    FILE *f = NULL;
    DIR *d = NULL;
    char root[] = "/tmp/sublimite-cli-XXXXXX", source_path[4097] = "", dump[4097] = "", dir[4097] = "";
    T(getenv("DISPLAY") && strcmp(getenv("DISPLAY"), ":99") == 0);
    T(mkdtemp(root) != NULL); root_created = true; T(setenv("XDG_DATA_HOME", root, 1) == 0);
    T(snprintf(source_path, sizeof source_path, "%s/file.txt", root) > 0);
    T(snprintf(dump, sizeof dump, "%s/session.trace", root) > 0);
    T(snprintf(dir, sizeof dir, "%s/sublimite", root) > 0);
    f = fopen(source_path, "wb"); T(f && fputs("hello\n", f) >= 0);
    int closed = fclose(f); f = NULL; T(closed == 0);
    T(setenv("XDG_RUNTIME_DIR", root, 1) == 0);
    const char *decoy_display = getenv("CLI_TEST_DECOY_DISPLAY");
    T(setenv("DISPLAY", decoy_display ? decoy_display : ":99", 1) == 0);
    T(ipc_server_init_display(&decoy, NULL, getenv("DISPLAY")) == IPC_OK); decoy_live = true;
    T(setenv("DISPLAY", ":99", 1) == 0);
    fprintf(stderr, "cli_test: decoy display=%s launcher=:99 shared runtime regression\n",
        getenv("CLI_TEST_DECOY_DISPLAY") ? getenv("CLI_TEST_DECOY_DISPLAY") : ":99");
    /* The mismatch case deliberately keeps the shared runtime: production
     * must reject cross-display routing independently of test isolation. */
    if (!decoy_display) {
        T(same_display_handoff(source_path, &decoy) == 0);
        T(test_ipc_isolate() != NULL);
    }
    int ran = session(source_path, dump, false, &decoy);
    T(ran == 0);
    ipc_server_fini(&decoy); decoy_live = false;
    /* Always cover different displays in make check, on a shared runtime.
     * This decoy owns only an IPC socket; it never opens a window on :98. */
    T(ipc_server_init_display(&decoy, NULL, ":98") == IPC_OK); decoy_live = true;
    fputs("cli_test: different-display decoy, shared runtime regression\n", stderr);
    ran = session(source_path, dump, false, &decoy);
    T(ran == 0);
    ipc_server_fini(&decoy); decoy_live = false;
    ran = session(source_path, root, true, NULL);
    T(ran == 0);
    if (!getenv("CLI_TEST_DELAY_BACKEND")) T(hot_exit_cli(source_path, dir, stop_signal) == 0);
    d = opendir(dir); T(d != NULL); struct dirent *entry; unsigned journals = 0;
    while ((entry = readdir(d))) {
        if (!strncmp(entry->d_name, ".session-", 9)) {
            T(unlinkat(dirfd(d), entry->d_name, 0) == 0);
            continue;
        }
        if (entry->d_name[0] == '.') continue;
        T(strncmp(entry->d_name, "session-", 8) == 0);
        char path[4097]; T(snprintf(path, sizeof path, "%s/%s", dir, entry->d_name) > 0);
        struct stat st; T(stat(path, &st) == 0 && st.st_size > 0 && (st.st_mode & 0777) == 0600);
        journals++; T(unlink(path) == 0);
    }
    /* Each later launch restores and durably replaces the previous session;
     * normal exits and the killed/restarted launch leave one owned journal. */
    T(journals == 1); closed = closedir(d); d = NULL; T(closed == 0 && rmdir(dir) == 0);
    char decoy_lock[4097]; (void)snprintf(decoy_lock, sizeof decoy_lock, "%s/sublimite-%lu.lock", root, (unsigned long)getuid());
    (void)unlink(decoy_lock);
    d = opendir(root); T(d != NULL);
    struct dirent *endpoint;
    while ((endpoint = readdir(d))) {
        if (!strncmp(endpoint->d_name, "sublimite-", 10)) T(unlinkat(dirfd(d), endpoint->d_name, 0) == 0);
    }
    T(closedir(d) == 0); d = NULL;
    T(unlink(source_path) == 0 && unlink(dump) == 0 && rmdir(root) == 0);
    puts("cli_test: file window identity, XDG session journals, quiescent trace dump and dump error passed");
    result = 0;
cleanup:
    if (decoy_live) ipc_server_fini(&decoy);
    char lock[4097]; (void)snprintf(lock, sizeof lock, "%s/sublimite-%lu.lock", root, (unsigned long)getuid());
    (void)unlink(lock);
    if (f) (void)fclose(f);
    if (d) (void)closedir(d);
    if (root_created) {
        d = opendir(root);
        if (d) {
            struct dirent *remaining_endpoint;
            while ((remaining_endpoint = readdir(d)))
                if (!strncmp(remaining_endpoint->d_name, "sublimite-", 10)) (void)unlinkat(dirfd(d), remaining_endpoint->d_name, 0);
            (void)closedir(d); d = NULL;
        }
        /* A failed session may have left partial journals. */
        d = opendir(dir);
        if (d) {
            struct dirent *leftover;
            while ((leftover = readdir(d))) {
                if (!strcmp(leftover->d_name, ".") || !strcmp(leftover->d_name, "..")) continue;
                (void)unlinkat(dirfd(d), leftover->d_name, 0);
            }
            (void)closedir(d);
        }
        (void)rmdir(dir); (void)unlink(source_path); (void)unlink(dump); (void)rmdir(root);
    }
    return result;
}

/* edit-97i: real production launcher hand-off to a null-backend editor.
 * (P) fixture tab capacity = Unicode source + peer; argv counts derive from
 * arrays, coordinates use IPC's origin, wait client count derives from pair. */
static int duplicate_cli_handoff(void)
{
    int result = 1, status = 0; editor *e = NULL; ipc_server server = {0}; bool live = false;
    pid_t child = -1, waiters[] = {-1, -1}; /* (P) pair of independent wait clients. */
    const char *root = test_ipc_isolate(); T(root);
    char path[IPC_PATH_CAP], alias[IPC_PATH_CAP];
    T(snprintf(path, sizeof path, "%s/é文.txt", root) > 0);
    T(snprintf(alias, sizeof alias, "%s/alias.txt", root) > 0);
    FILE *f = fopen(path, "w"); T(f); T(fputs("hello\n", f) >= 0); T(fclose(f) == 0);
    T(symlink(path, alias) == 0);
    T(setenv("DISPLAY", ":99", 1) == 0); /* (P) repository's designated test display identity; no X connection. */
    T(unsetenv("WAYLAND_DISPLAY") == 0);
    T(ipc_server_init_display(&server, NULL, getenv("DISPLAY")) == IPC_OK); live = true;
    render_backend backend = {0}; T(render_null_backend(&backend) == 0);
    editor_config cfg = {.path = path, .server = &server, .tab_capacity = sizeof "ab" - 1u};
    T(editor_open(&e, &cfg, &backend) == 0);
    uint64_t original = tabs_active(&e->tabs)->id, peer;
    T(editor_add_buffer(e, NULL, (const uint8_t *)"peer", sizeof "peer" - 1u, &peer) == 0);
    size_t tabs = tabs_count(&e->tabs);
    child = fork(); T(child >= 0);
    if (!child) {
        char *args[] = {"sublimite", path, alias};
        _exit(editor_cli_main((int)(sizeof args / sizeof *args), args));
    }
    uint64_t deadline = trace_now_ns() + EDITOR_IDLE_NS; /* (P) reuse the existing editor idle watchdog. */
    pid_t waited = 0;
    while (!waited && trace_now_ns() < deadline) { T(editor_step(e, 0) >= 0); waited = child_wait(child, &status, WNOHANG); }
    T(waited == child); child = -1; T(WIFEXITED(status) && WEXITSTATUS(status) == 0);
    T(tabs_count(&e->tabs) == tabs && tabs_active(&e->tabs)->id == original);
    for (size_t at = 0; at < sizeof waiters / sizeof *waiters; at++) {
        waiters[at] = fork(); T(waiters[at] >= 0);
        if (!waiters[at]) {
            char *args[] = {"sublimite", "--wait", path, alias};
            _exit(editor_cli_main((int)(sizeof args / sizeof *args), args));
        }
        size_t accepted = 0; deadline = trace_now_ns() + EDITOR_IDLE_NS;
        while (accepted <= at && trace_now_ns() < deadline) {
            T(editor_step(e, 0) >= 0); accepted = 0;
            for (size_t i = 0; i < IPC_MAX_CLIENTS; i++) accepted += e->waits[i].token != 0;
        }
        T(accepted == at + 1u); /* (P) current client plus previously admitted clients. */
        T(child_wait(waiters[at], &status, WNOHANG) == 0);
        T(tabs_count(&e->tabs) == tabs && tabs_active(&e->tabs)->id == original);
    }
    T(editor_close_tab(e, tabs_active_index(&e->tabs)) == 0);
    for (size_t at = 0; at < sizeof waiters / sizeof *waiters; at++) {
        deadline = trace_now_ns() + EDITOR_IDLE_NS; waited = 0;
        while (!waited && trace_now_ns() < deadline) { T(editor_step(e, 0) >= 0); waited = child_wait(waiters[at], &status, WNOHANG); }
        T(waited == waiters[at]); waiters[at] = -1; T(WIFEXITED(status) && WEXITSTATUS(status) == 0);
    }
    puts("edit-97i: production CLI Unicode/alias hand-off reuses tab; independent --wait clients finish on its close");
    result = 0;
cleanup:
    if (child > 0) { (void)kill(child, SIGKILL); (void)child_wait(child, NULL, 0); }
    for (size_t at = 0; at < sizeof waiters / sizeof *waiters; at++)
        if (waiters[at] > 0) { (void)kill(waiters[at], SIGKILL); (void)child_wait(waiters[at], NULL, 0); }
    editor_close(e); if (live) ipc_server_fini(&server);
    return result;
}

int main(int argc, char **argv)
{
    /* edit-djg (P): executable plus the named focused-test option. */
    if (argc == (int)(sizeof "ab" - 1u) && !strcmp(argv[1], "--sigterm")) return run_cli_suite(SIGTERM);
    if (argc == (int)(sizeof "ab" - 1u) && !strcmp(argv[1], "--sigint")) return run_cli_suite(SIGINT);
    if (argc == (int)(sizeof "ab" - 1u) && !strcmp(argv[1], "--display-policy")) return display_policy();
    if (argc == (int)(sizeof "ab" - 1u) && !strcmp(argv[1], "--duplicate-path")) return duplicate_cli_handoff();
    if (display_policy()) return EXIT_FAILURE;
    if (duplicate_cli_handoff()) return 1;
    if (getenv("CLI_TEST_FORCE_WINDOW_TIMEOUT")) return run_cli_suite(SIGKILL);
    /* A live implementation that misses its window deadline must fail. */
    pid_t control = fork();
    if (control < 0) return 1;
    if (!control) {
        if (setenv("CLI_TEST_FORCE_WINDOW_TIMEOUT", "1", 1)) _exit(3);
        _exit(run_cli_suite(SIGKILL));
    }
    int status = 0;
    if (child_wait(control, &status, 0) != control || !WIFEXITED(status) || WEXITSTATUS(status) != 1) {
        fputs("cli_test: FAIL §36 forced startup timeout succeeded/skipped\n", stderr);
        return 1;
    }
    fputs("cli_test: §36 forced startup timeout correctly failed\n", stderr);
    int normal = run_cli_suite(SIGKILL);
    if (normal) return normal;
    if (run_cli_suite(SIGTERM) || run_cli_suite(SIGINT)) return 1;
    if (setenv("CLI_TEST_DELAY_BACKEND", "1", 1)) return 1;
    int delayed = run_cli_suite(SIGKILL);
    (void)unsetenv("CLI_TEST_DELAY_BACKEND");
    return delayed;
}
