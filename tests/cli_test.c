/* Compile the production entry point here so make check covers it with ASan. */
#include "editor/editor.h"
#include "raster/raster.h"
static int test_backend_select(render_backend *backend, const char *name);
#define editor_backend_select test_backend_select
#define main editor_cli_main
#include "../src/main.c"
#undef main
#undef editor_backend_select
#include "trace/trace_fmt.h"
#include <dirent.h>
#include "ipc_isolation.h"
#include <signal.h>
#include <sys/wait.h>
#include <time.h>
#include <xcb/xcb.h>
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
static ipc_result decoy_open(const ipc_request *request, ipc_token token, void *ctx)
{
    (void)request; (void)token;
    unsigned *calls = ctx; ++*calls;
    return IPC_OK;
}
static int same_display_handoff(const char *file, ipc_server *decoy)
{
    pid_t child = fork(); if (child < 0) return 1;
    if (!child) {
        char *args[] = {"sublimite", (char *)file, NULL};
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
static int session(const char *file, const char *dump, bool fail_dump, ipc_server *decoy)
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
        char *args[] = {"sublimite", (char *)file, NULL};
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
static int run_cli_suite(void)
{
    int result = 1;
    bool root_created = false, decoy_live = false;
    ipc_server decoy = {0};
    FILE *f = NULL;
    DIR *d = NULL;
    char root[] = "/tmp/sublimite-cli-XXXXXX", file[4097] = "", dump[4097] = "", dir[4097] = "";
    T(getenv("DISPLAY") && strcmp(getenv("DISPLAY"), ":99") == 0);
    T(mkdtemp(root) != NULL); root_created = true; T(setenv("XDG_DATA_HOME", root, 1) == 0);
    T(snprintf(file, sizeof file, "%s/file.txt", root) > 0);
    T(snprintf(dump, sizeof dump, "%s/session.trace", root) > 0);
    T(snprintf(dir, sizeof dir, "%s/sublimite", root) > 0);
    f = fopen(file, "wb"); T(f && fputs("hello\n", f) >= 0);
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
        T(same_display_handoff(file, &decoy) == 0);
        T(test_ipc_isolate() != NULL);
    }
    int ran = session(file, dump, false, &decoy);
    T(ran == 0);
    ipc_server_fini(&decoy); decoy_live = false;
    /* Always cover different displays in make check, on a shared runtime.
     * This decoy owns only an IPC socket; it never opens a window on :98. */
    T(ipc_server_init_display(&decoy, NULL, ":98") == IPC_OK); decoy_live = true;
    fputs("cli_test: different-display decoy, shared runtime regression\n", stderr);
    ran = session(file, dump, false, &decoy);
    T(ran == 0);
    ipc_server_fini(&decoy); decoy_live = false;
    ran = session(file, root, true, NULL);
    T(ran == 0);
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
     * the three launches must leave one journal, not duplicate recovery state. */
    T(journals == 1); closed = closedir(d); d = NULL; T(closed == 0 && rmdir(dir) == 0);
    char decoy_lock[4097]; (void)snprintf(decoy_lock, sizeof decoy_lock, "%s/sublimite-%lu.lock", root, (unsigned long)getuid());
    (void)unlink(decoy_lock);
    d = opendir(root); T(d != NULL);
    struct dirent *endpoint;
    while ((endpoint = readdir(d))) {
        if (!strncmp(endpoint->d_name, "sublimite-", 10)) T(unlinkat(dirfd(d), endpoint->d_name, 0) == 0);
    }
    T(closedir(d) == 0); d = NULL;
    T(unlink(file) == 0 && unlink(dump) == 0 && rmdir(root) == 0);
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
        (void)rmdir(dir); (void)unlink(file); (void)unlink(dump); (void)rmdir(root);
    }
    return result;
}

int main(void)
{
    if (getenv("CLI_TEST_FORCE_WINDOW_TIMEOUT")) return run_cli_suite();
    /* A live implementation that misses its window deadline must fail. */
    pid_t control = fork();
    if (control < 0) return 1;
    if (!control) {
        if (setenv("CLI_TEST_FORCE_WINDOW_TIMEOUT", "1", 1)) _exit(3);
        _exit(run_cli_suite());
    }
    int status = 0;
    if (child_wait(control, &status, 0) != control || !WIFEXITED(status) || WEXITSTATUS(status) != 1) {
        fputs("cli_test: FAIL §36 forced startup timeout succeeded/skipped\n", stderr);
        return 1;
    }
    fputs("cli_test: §36 forced startup timeout correctly failed\n", stderr);
    int normal = run_cli_suite();
    if (normal) return normal;
    if (setenv("CLI_TEST_DELAY_BACKEND", "1", 1)) return 1;
    int delayed = run_cli_suite();
    (void)unsetenv("CLI_TEST_DELAY_BACKEND");
    return delayed;
}
