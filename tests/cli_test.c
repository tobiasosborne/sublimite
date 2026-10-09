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
#define T(c) do { if (!(c)) { fprintf(stderr, "cli_test:%d: FAIL %s\n", __LINE__, #c); return 1; } } while (0)
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
static int session(const char *file, const char *dump, bool fail_dump)
{
    T(setenv("EDIT_TRACE_DUMP", dump, 1) == 0);
    pid_t child = fork(); T(child >= 0);
    if (!child) {
        char *args[] = {"sublimite", (char *)file, NULL};
        _exit(editor_cli_main(2, args));
    }
    xcb_connection_t *c = xcb_connect(":99", NULL); T(!xcb_connection_has_error(c));
    xcb_screen_t *screen = xcb_setup_roots_iterator(xcb_get_setup(c)).data; T(screen != NULL);
    xcb_atom_t pid_atom = intern(c, "_NET_WM_PID");
    xcb_window_t window = XCB_WINDOW_NONE;
    uint64_t deadline = trace_now_ns() + UINT64_C(10000000000);
    while (!window && trace_now_ns() < deadline) {
        window = find_window(c, screen->root, pid_atom, child);
        struct timespec pause = {0, 10000000}; (void)nanosleep(&pause, NULL);
    }
    if (!window) { (void)kill(child, SIGTERM); (void)waitpid(child, NULL, 0); }
    T(window != XCB_WINDOW_NONE);
    xcb_get_property_reply_t *title = xcb_get_property_reply(c,
        xcb_get_property(c, 0, window, intern(c, "_NET_WM_NAME"), XCB_GET_PROPERTY_TYPE_ANY, 0, 64), NULL);
    T(title && title->type == intern(c, "UTF8_STRING"));
    T(xcb_get_property_value_length(title) == (int)strlen("sublimité"));
    T(memcmp(xcb_get_property_value(title), "sublimité", strlen("sublimité")) == 0); free(title);
    xcb_client_message_event_t close_event = {.response_type = XCB_CLIENT_MESSAGE, .format = 32,
        .window = window, .type = intern(c, "WM_PROTOCOLS"),
        .data.data32 = {intern(c, "WM_DELETE_WINDOW"), 0, 0, 0, 0}};
    (void)xcb_send_event(c, 0, window, 0, (const char *)&close_event); (void)xcb_flush(c);
    int status = 0; T(waitpid(child, &status, 0) == child);
    T(WIFEXITED(status) && WEXITSTATUS(status) == (fail_dump ? 1 : 0));
    xcb_disconnect(c);
    if (!fail_dump) {
        FILE *f = fopen(dump, "rb"); T(f != NULL); trace_rec *records = NULL; size_t count = 0;
        T(trace_fmt_load(f, &records, &count) == 0 && count > 0); free(records); T(fclose(f) == 0);
    }
    return 0;
}
int main(void)
{
    alarm(30);
    T(getenv("DISPLAY") && strcmp(getenv("DISPLAY"), ":99") == 0);
    char root[] = "/tmp/sublimite-cli-XXXXXX", file[4097], dump[4097], dir[4097];
    T(mkdtemp(root) != NULL); T(setenv("XDG_DATA_HOME", root, 1) == 0);
    T(snprintf(file, sizeof file, "%s/file.txt", root) > 0);
    T(snprintf(dump, sizeof dump, "%s/session.trace", root) > 0);
    FILE *f = fopen(file, "wb"); T(f && fputs("hello\n", f) >= 0 && fclose(f) == 0);
    T(session(file, dump, false) == 0); T(session(file, root, true) == 0);
    T(snprintf(dir, sizeof dir, "%s/sublimite", root) > 0);
    DIR *d = opendir(dir); T(d != NULL); struct dirent *entry; unsigned journals = 0;
    while ((entry = readdir(d))) {
        if (entry->d_name[0] == '.') continue;
        T(strncmp(entry->d_name, "session-", 8) == 0);
        char path[4097]; T(snprintf(path, sizeof path, "%s/%s", dir, entry->d_name) > 0);
        struct stat st; T(stat(path, &st) == 0 && st.st_size > 0 && (st.st_mode & 0777) == 0600);
        journals++; T(unlink(path) == 0);
    }
    T(journals == 2 && closedir(d) == 0 && rmdir(dir) == 0);
    T(unlink(file) == 0 && unlink(dump) == 0 && rmdir(root) == 0);
    puts("cli_test: file window identity, XDG session journals, quiescent trace dump and dump error passed");
    return 0;
}
