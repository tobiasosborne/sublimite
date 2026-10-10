#include "editor/editor.h"
#include "editor/runtime.h"
#include "journal/journal.h"
#include "trace/trace.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    ipc_args args = {0}; ipc_result parsed = ipc_parse_args(argc, argv, &args);
    if (parsed == IPC_OK && args.request.has_stdin) parsed = ipc_args_read_stdin(&args, STDIN_FILENO);
    if (parsed != IPC_OK) { fprintf(stderr, "usage: sublimite [--wait] [--new-instance] [--] [path[:line[:col]] ...] [-]\n"); ipc_args_fini(&args); return 2; }
    ipc_server server = {0}; bool owns_server = false;
    const char *display = getenv("DISPLAY");
    const char *selected = editor_runtime_display(display, getenv("EDIT_DISPLAY"), getenv("EDIT_ALLOW_REAL_DISPLAY"));
    if (selected != display && setenv("DISPLAY", selected, 1)) goto startup_fail;
    display = getenv("DISPLAY");
    if (!args.request.new_instance) {
        ipc_result role = ipc_server_init_display(&server, NULL, display);
        if (role == IPC_EXISTS) {
            ipc_result sent = ipc_client_send_display(NULL, display, &args.request, 2000);
            ipc_args_fini(&args);
            if (sent != IPC_OK) fprintf(stderr, "sublimite: handoff error %d\n", sent);
            return sent == IPC_OK ? 0 : 2;
        }
        if (role != IPC_OK) { fprintf(stderr, "sublimite: IPC startup error %d\n", role); ipc_args_fini(&args); return 2; }
        owns_server = true;
    }
    char journal_path[4097];
    if (journal_default_dir(journal_path, sizeof journal_path) != JOURNAL_OK) {
        fprintf(stderr, "sublimite: cannot resolve journal directory\n"); goto startup_fail;
    }
    if (editor_make_directory(journal_path, NULL, NULL)) { perror("sublimite: journal directory"); goto startup_fail; }
    const char suffix[] = "/session-XXXXXX";
    size_t n = strlen(journal_path);
    if (n > sizeof journal_path - sizeof suffix) goto startup_fail;
    memcpy(journal_path + n, suffix, sizeof suffix);
    ipc_token initial_token = 0;
    if (args.request.wait) {
        if (!owns_server) {
            ipc_result role = ipc_server_init_isolated(&server);
            if (role != IPC_OK) goto startup_fail;
            owns_server = true;
        }
        int channels[2];
        if (ipc_launcher_pair(channels) != IPC_OK) goto startup_fail;
        /* No workers, accepted peers or UI state exist at this fork. */
        pid_t ui = fork();
        if (ui < 0) { close(channels[0]); close(channels[1]); goto startup_fail; }
        if (ui > 0) {
            close(channels[0]);
            /* The UI inherits the endpoint. Release only the launcher's copy:
             * never unlink it or take its shared open-file-description lock. */
            server.owns_path = false;
            if (server.lock_fd >= 0) { close(server.lock_fd); server.lock_fd = -1; }
            ipc_server_fini(&server);
            ipc_args_fini(&args);
            ipc_result waited = ipc_launcher_wait(channels[1]);
            if (waited != IPC_OK) fprintf(stderr, "sublimite: wait error %d\n", waited);
            return waited == IPC_OK ? 0 : 1;
        }
        close(channels[1]);
        if (ipc_server_adopt_wait(&server, channels[0], &initial_token) != IPC_OK) {
            close(channels[0]); goto startup_fail;
        }
    }
    int fd = mkstemp(journal_path);
    if (fd < 0) { perror("sublimite: journal"); goto startup_fail; }
    close(fd);
    trace_init(); (void)trace_thread_register();
    render_backend backend = {0};
    /* The only application backend selection site; init may fall back once. */
    int rc = editor_backend_select(&backend, getenv("EDIT_BACKEND"));
    editor *e = NULL;
    editor_config config = {.journal_path = journal_path, .start_empty = true, .raster_fallback = true,
        .server = owns_server ? &server : NULL};
    if (!rc) rc = editor_open(&e, &config, &backend);
    if (!rc) rc = editor_open_request(e, &args.request, initial_token);
    ipc_args_fini(&args);
    if (!rc) rc = editor_run(e);
    if (e) {
        int flushed = editor_flush(e);
        if (!rc) rc = flushed;
        editor_close(e);
    }
    if (owns_server) ipc_server_fini(&server);
    /* editor_close joins workers: the trace rings are quiescent here. */
    const char *dump_path = getenv("EDIT_TRACE_DUMP");
    if (dump_path) {
        FILE *dump = fopen(dump_path, "wb");
        int dumped = dump ? trace_dump(dump) : -1;
        if (dump && fclose(dump)) dumped = -1;
        if (dumped) {
            fprintf(stderr, "sublimite: cannot write trace dump: %s\n", dump_path);
            if (!rc) rc = EDITOR_ERR_IO;
        }
    }
    if (rc) fprintf(stderr, "sublimite: error %d; journal: %s\n", rc, journal_path);
    else fprintf(stderr, "sublimite: unsaved session journal: %s\n", journal_path);
    return rc ? 1 : 0;
startup_fail:
    ipc_args_fini(&args);
    if (owns_server) ipc_server_fini(&server);
    return 1;
}
