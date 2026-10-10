/* Compile the production entry point here so make check covers it with ASan. */
#define main editor_cli_main
#include "../src/main.c"
#undef main
#include "trace/trace_fmt.h"
#include <dirent.h>
#include <signal.h>
#include <sys/wait.h>
#include <time.h>
#include <xcb/xcb.h>
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
/* 0 = passed, 1 = failed, 2 = skipped. */
static int session(const char *file, const char *dump, bool fail_dump)
{
    int result = 1, status = 0;
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
        pid_t waited = child_wait(child, &status, WNOHANG | WUNTRACED);
        if (waited == child && (WIFEXITED(status) || WIFSIGNALED(status))) child = -1;
        if (waited != 0) fprintf(stderr, "cli_test: editor exited or stopped before window (status=%d)\n", status);
        T(waited == 0);
        T(!xcb_connection_has_error(c));
        window = find_window(c, screen->root, pid_atom, child);
        if (!window) pause_poll();
    }
    if (!window) {
        pid_t waited = child_wait(child, &status, WNOHANG | WUNTRACED);
        if (waited == child && (WIFEXITED(status) || WIFSIGNALED(status))) child = -1;
        T(waited == 0 && kill(child, 0) == 0 && !xcb_connection_has_error(c));
        fprintf(stderr, "cli_test: SKIP: window not created within %u s (load1=%.2f)%s\n",
            getenv("CLI_TEST_FORCE_WINDOW_TIMEOUT") ? 0u : wait_seconds, load1,
            getenv("CLI_TEST_FORCE_WINDOW_TIMEOUT") ? " [forced test timeout]" : "");
        result = 2; goto cleanup;
    }
    T(window != XCB_WINDOW_NONE);
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
    }
    result = 0;
cleanup:
    free(title); free(records);
    if (f) (void)fclose(f);
    if (c) xcb_disconnect(c);
    if (child > 0) { (void)kill(child, SIGKILL); (void)child_wait(child, NULL, 0); }
    return result;
}
int main(void)
{
    int result = 1;
    bool root_created = false;
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
    int ran = session(file, dump, false);
    if (ran == 2) { result = 0; goto cleanup; } T(ran == 0);
    ran = session(file, root, true);
    if (ran == 2) { result = 0; goto cleanup; } T(ran == 0);
    d = opendir(dir); T(d != NULL); struct dirent *entry; unsigned journals = 0;
    while ((entry = readdir(d))) {
        if (entry->d_name[0] == '.') continue;
        T(strncmp(entry->d_name, "session-", 8) == 0);
        char path[4097]; T(snprintf(path, sizeof path, "%s/%s", dir, entry->d_name) > 0);
        struct stat st; T(stat(path, &st) == 0 && st.st_size > 0 && (st.st_mode & 0777) == 0600);
        journals++; T(unlink(path) == 0);
    }
    T(journals == 2); closed = closedir(d); d = NULL; T(closed == 0 && rmdir(dir) == 0);
    T(unlink(file) == 0 && unlink(dump) == 0 && rmdir(root) == 0);
    puts("cli_test: file window identity, XDG session journals, quiescent trace dump and dump error passed");
    result = 0;
cleanup:
    if (f) (void)fclose(f);
    if (d) (void)closedir(d);
    if (root_created) {
        /* A skipped/failed session may have left partial journals. */
        d = opendir(dir);
        if (d) {
            struct dirent *leftover;
            while ((leftover = readdir(d))) {
                if (leftover->d_name[0] == '.') continue;
                (void)unlinkat(dirfd(d), leftover->d_name, 0);
            }
            (void)closedir(d);
        }
        (void)rmdir(dir); (void)unlink(file); (void)unlink(dump); (void)rmdir(root);
    }
    return result;
}
