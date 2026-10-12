/* (P) Exercise the actual application entry point with no compositor. Exit
 * status and diagnostic expectations derive from PLAT_ERR_NO_DISPLAY. */
#define main wayland_application_main
#include "../src/main.c"
#undef main
#include <assert.h>
#include <sys/wait.h>

static void startup_case(bool guarded)
{
    char state[] = "/tmp/edit-1zm-startup-XXXXXX";
    assert(mkdtemp(state));
    int channel[2]; /* (P) POSIX pipe read/write endpoints */
    assert(!pipe(channel));
    pid_t child = fork(); assert(child >= 0);
    if (!child) {
        close(channel[0]); assert(dup2(channel[1], STDERR_FILENO) >= 0); close(channel[1]);
        assert(!setenv("XDG_DATA_HOME", state, 1));
        assert(!setenv("DISPLAY", "", 1));
        assert(!unsetenv("WAYLAND_SOCKET"));
        assert(!setenv("WAYLAND_DISPLAY", "/nonexistent/edit-1zm-compositor", 1));
        if (guarded) {
            assert(!unsetenv("EDIT_ALLOW_REAL_DISPLAY"));
            assert(!unsetenv("EDIT_WAYLAND_DISPLAY"));
        } else assert(!setenv("EDIT_ALLOW_REAL_DISPLAY", "1", 1));
        assert(!setenv("EDIT_BACKEND", "gl", 1));
        char *args[] = {"sublimite", "--new-instance", NULL};
        _exit(wayland_application_main((int)(sizeof args / sizeof args[0] - 1), args));
    }
    close(channel[1]); FILE *stream = fdopen(channel[0], "r"); assert(stream);
    bool readable = false; char *line = NULL; size_t capacity = 0;
    while (getline(&line, &capacity, stream) >= 0)
        if (strstr(line, guarded ? "Wayland display guard refused compositor" : "cannot connect to Wayland compositor")) readable = true;
    free(line); assert(!fclose(stream));
    int status; assert(waitpid(child, &status, 0) == child);
    assert(WIFEXITED(status) && WEXITSTATUS(status) == EXIT_FAILURE);
    assert(readable);
    char directory[sizeof state + sizeof "/sublimite"]; /* root + module suffix */
    assert(snprintf(directory, sizeof directory, "%s/sublimite", state) > 0);
    if (!guarded) assert(!rmdir(directory));
    assert(!rmdir(state));
}
int main(void)
{
    startup_case(true);
    startup_case(false);
    puts("wayland_startup_test: application guard and missing compositor have readable diagnostics");
    return 0;
}
