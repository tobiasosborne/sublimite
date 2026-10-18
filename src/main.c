#include "editor/editor.h"
#include "editor/runtime.h"
#include "wayland/plat.h"
#include "journal/journal.h"
#include "trace/trace.h"
#include <errno.h>
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/signalfd.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    const char *usage = "usage: sublimite [--wait] [--new-instance] [--font-family PATTERN] [--font-file PATH[#INDEX]] [--font-fallbacks A,B] [--] [path[:line[:col]] ...] [-]";
    if (argc == 2 && !strcmp(argv[1], "--help")) { /* (P) executable plus help option. */
        puts(usage); return EXIT_SUCCESS;
    }
    uint64_t startup_ns = trace_now_ns();
    trace_init(); (void)trace_thread_register();
    trace_record_at(startup_ns, TRACE_T0_INGRESS, 0);
    trace_record_at(startup_ns, TRACE_STARTUP_PROCESS_START, 0);
    ipc_args args = {0}; ipc_result parsed = ipc_parse_args(argc, argv, &args);
    if (parsed == IPC_OK && args.request.has_stdin) parsed = ipc_args_read_stdin(&args, STDIN_FILENO);
    if (parsed != IPC_OK) { fprintf(stderr, "%s\n", usage); ipc_args_fini(&args); return 2; }
    ipc_server server = {0}; bool owns_server = false;
    const char *display = getenv("DISPLAY");
    const char *wayland_display = getenv("WAYLAND_DISPLAY");
    plat_backend platform_backend = plat_backend_select(display, wayland_display);
    /* Legacy EDIT_ALLOW_REAL_DISPLAY is accepted as a no-op. Refusal wins. */
    const char *refuse = getenv("EDIT_REFUSE_REAL_DISPLAY");
    const char *selected = editor_runtime_display(display, getenv("EDIT_DISPLAY"), refuse);
    if (platform_backend == PLAT_BACKEND_X11 && selected != display) {
        if (display && *display) {
            fprintf(stderr, "sublimite: display guard: real display %s refused\n", display);
            goto startup_fail;
        }
        if (setenv("DISPLAY", selected, 1)) goto startup_fail;
    }
    if (platform_backend == PLAT_BACKEND_WAYLAND) {
        const char *safe = wayland_display_select(wayland_display, getenv("EDIT_WAYLAND_DISPLAY"), refuse ? NULL : "");
        if (refuse) {
            /* A descriptor overrides the socket name: refusal clears it.
             * Private alternate names use XDG_RUNTIME_DIR. */
            if (unsetenv("WAYLAND_SOCKET")) goto startup_fail;
            if (!safe) { fprintf(stderr, "sublimite: Wayland display guard refused compositor; set EDIT_WAYLAND_DISPLAY for a private compositor or EDIT_ALLOW_REAL_DISPLAY to opt in\n"); goto startup_fail; }
        }
        if (safe != wayland_display && safe && setenv("WAYLAND_DISPLAY", safe, 1)) goto startup_fail;
        wayland_display = getenv("WAYLAND_DISPLAY");
    }
    display = platform_backend == PLAT_BACKEND_WAYLAND ? wayland_display : getenv("DISPLAY");
    if (!args.request.new_instance) {
        ipc_result role = ipc_server_init_display(&server, NULL, display);
        if (role == IPC_EXISTS) {
            ipc_result sent = ipc_client_send_display(NULL, display, &args.request, 2000);
            ipc_args_fini(&args);
            if (sent != IPC_OK) fprintf(stderr, "sublimite: handoff failed: %s\n", ipc_result_string(sent));
            return sent == IPC_OK ? 0 : 2;
        }
        if (role != IPC_OK) { fprintf(stderr, "sublimite: IPC startup failed: %s\n", ipc_result_string(role)); ipc_args_fini(&args); return 2; }
        owns_server = true;
    }
    char journal_path[4097];
    if (journal_default_dir(journal_path, sizeof journal_path) != JOURNAL_OK) {
        fprintf(stderr, "sublimite: cannot resolve journal directory\n"); goto startup_fail;
    }
    if (editor_make_directory(journal_path, NULL, NULL)) { perror("sublimite: journal directory"); goto startup_fail; }
    char journal_dir[sizeof journal_path];
    memcpy(journal_dir, journal_path, sizeof journal_dir);
    int session_lease = -1;
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
            if (waited != IPC_OK) fprintf(stderr, "sublimite: wait failed: %s\n", ipc_result_string(waited));
            return waited == IPC_OK ? 0 : 1;
        }
        close(channels[1]);
        if (ipc_server_adopt_wait(&server, channels[0], &initial_token) != IPC_OK) {
            close(channels[0]); goto startup_fail;
        }
    }
    /* edit-cxo: the lease is held before the session name is visible, so a
     * concurrently starting instance's housekeeping cannot retire it. */
    if (journal_session_create(journal_dir, journal_path, sizeof journal_path, &session_lease) != JOURNAL_OK) {
        fprintf(stderr, "sublimite: cannot create session journal in %s\n", journal_dir); goto startup_fail;
    }
    struct stat placeholder;
    if (lstat(journal_path, &placeholder)) {
        (void)unlink(journal_path); (void)journal_session_release(journal_path, session_lease);
        close(session_lease); goto startup_fail;
    }
    render_backend backend = {0};
    /* edit-djg: block before any editor/backend workers start; they inherit
     * the mask. The UI receives termination through its existing poll set,
     * with no handler or process global and no change to SIGBUS. Keep the
     * mask through process exit so repeat signals cannot interrupt teardown.
     * (P) Negative fd is absent; zero is syscall/API success. */
    sigset_t close_signals;
    int close_signal_fd = -1, rc = 0;
    if (sigemptyset(&close_signals) || sigaddset(&close_signals, SIGTERM) ||
        sigaddset(&close_signals, SIGINT) || pthread_sigmask(SIG_BLOCK, &close_signals, NULL)) rc = EDITOR_ERR_IO;
    if (!rc) {
        close_signal_fd = signalfd(-1, &close_signals, SFD_NONBLOCK | SFD_CLOEXEC);
        if (close_signal_fd < 0) rc = EDITOR_ERR_IO;
    }
    /* The only application backend selection site; init may fall back once. */
    if (!rc) rc = editor_backend_select(&backend, getenv("EDIT_BACKEND"));
    const char *platform_error = NULL;
    editor *e = NULL;
    editor_config config = {.journal_path = journal_path, .session_lease = &session_lease, .start_empty = true, .raster_fallback = true, .load_preferences = true,
        .font_family = args.font_family, .font_file = args.font_file, .font_fallbacks = args.font_fallbacks,
        /* (G): edit-jmn's Sublime-comparable target. Tab records and buffer
         * pointers are reserved during startup, never on the typing path. */
        .tab_capacity = 1000u, .server = owns_server ? &server : NULL, .platform_backend = platform_backend,
        .close_signal_fd = close_signal_fd >= 0 ? &close_signal_fd : NULL};
    editor_display_size display_size;
    /* A supplied Wayland fd is an existing protocol connection, not a socket
     * name from which another connection can be opened. Preserve it for
     * platform init rather than consume it in the preliminary output query. */
    bool native_size = platform_backend == PLAT_BACKEND_X11 ||
        (!(backend.info.capabilities & RENDER_CAP_HEADLESS) && !getenv("WAYLAND_SOCKET"));
    if (!rc && native_size) {
        if (platform_backend == PLAT_BACKEND_X11) rc = editor_runtime_display_size(&display_size);
        else {
            wayland_display_info output;
            rc = wayland_display_size(&output);
            if (rc) platform_error = wayland_error_string(rc);
            if (!rc) display_size = (editor_display_size){output.dpi, output.width, output.height};
        }
    }
    if (!rc && native_size) {
        config.font_dpi = display_size.dpi;
        if (display_size.work_width && display_size.work_height) {
            config.max_window_width = display_size.work_width;
            config.max_window_height = display_size.work_height;
            editor_runtime_window(display_size.work_width, display_size.work_height,
                                  &config.window_width, &config.window_height);
            config.window_size_default = true;
        }
        const char *point_size = getenv("EDIT_FONT_SIZE_PT");
        if (point_size) {
            char *tail = NULL; double points = strtod(point_size, &tail);
            if (tail == point_size || *tail || !isfinite(points) || points <= 0) rc = EDITOR_ERR_ARG;
            else config.font_points = points;
        }
    }
    if (!rc) rc = editor_open(&e, &config, &backend);
    if (!rc) rc = editor_open_request(e, &args.request, initial_token);
    ipc_args_fini(&args);
    if (!rc) rc = editor_run(e);
    if (e) {
        int flushed = editor_flush_settle(e); /* edit-ivv: bounded JOURNAL_BUSY retry */
        if (!rc) rc = flushed;
        editor_close(e);
    }
    if (close_signal_fd >= 0) close(close_signal_fd);
    if (owns_server) ipc_server_fini(&server);
    /* Backend/platform setup can fail before editor_open owns the placeholder.
     * Retire only this launch's still-empty inode, never a recovery checkpoint. */
    struct stat remaining;
    if (!lstat(journal_path, &remaining) && S_ISREG(remaining.st_mode) && !remaining.st_size &&
        remaining.st_dev == placeholder.st_dev && remaining.st_ino == placeholder.st_ino)
        (void)unlink(journal_path);
    (void)journal_session_release(journal_path, session_lease); /* no-op if the journal remains */
    close(session_lease); /* after the placeholder check: still ours throughout */
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
    if (rc && platform_error) fprintf(stderr, "sublimite: %s; journal: %s\n", platform_error, journal_path);
    else if (rc) fprintf(stderr, "sublimite: error %d; journal: %s\n", rc, journal_path);
    else fprintf(stderr, "sublimite: unsaved session journal: %s\n", journal_path);
    return rc ? 1 : 0;
startup_fail:
    ipc_args_fini(&args);
    if (owns_server) ipc_server_fini(&server);
    return 1;
}
