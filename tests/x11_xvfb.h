/* x11_xvfb.h - private X server for tests that take selection ownership (P2.2b).
 * make check usually runs with DISPLAY=:0, Tobias's real session; a test that owns CLIPBOARD
 * or talks to CLIPBOARD_MANAGER there would clobber his clipboard. So these tests never use the
 * inherited DISPLAY: they start Xvfb (-displayfd), point DISPLAY at it, and kill it at exit.
 * A read-only WSLg socket directory needs an explicit, free abstract-socket display.
 * Returns 1 when a private server runs, 0 when Xvfb is missing (caller prints "skipped (no Xvfb)"). */
#ifndef X11_XVFB_H
#define X11_XVFB_H
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <signal.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static pid_t xvfb_pid = -1;
static void xvfb_stop(void) {
    if (xvfb_pid > 0) { kill(xvfb_pid, SIGTERM); waitpid(xvfb_pid, NULL, 0); xvfb_pid = -1; }
}
static int xvfb_explicit_display(char *display, size_t capacity) {
    /* Probe by binding, not connecting: no requests reach an existing server.
     * Xvfb's own lock/bind still arbitrates a race after the probe is closed.
     * (P) Search positive uint16_t display numbers; zero is the real desktop. */
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) return 0;
    for (unsigned number = 1; number <= UINT16_MAX; number++) {
        struct sockaddr_un address = {.sun_family = AF_UNIX};
        int n = snprintf(address.sun_path + 1, sizeof address.sun_path - 1,
                         "/tmp/.X11-unix/X%u", number);
        socklen_t length = (socklen_t)(offsetof(struct sockaddr_un, sun_path) + 1u + (size_t)n);
        if (bind(fd, (struct sockaddr *)&address, length) == 0) {
            close(fd);
            snprintf(display, capacity, ":%u", number);
            return 1;
        }
        if (errno != EADDRINUSE) break;
    }
    close(fd);
    return 0;
}
/* /usr/bin/Xvfb first (distribution package); otherwise the first Xvfb on PATH
 * (a user-space install, e.g. monolith's ~/.local/bin/Xvfb, edit-b6ai.1). */
static char xvfb_path[4096];
static int xvfb_find(void) {
    if (access("/usr/bin/Xvfb", X_OK) == 0) { snprintf(xvfb_path, sizeof xvfb_path, "/usr/bin/Xvfb"); return 1; }
    const char *path = getenv("PATH");
    while (path && *path) {
        const char *end = strchr(path, ':');
        size_t len = end ? (size_t)(end - path) : strlen(path);
        if (len > 0 && (size_t)snprintf(xvfb_path, sizeof xvfb_path, "%.*s/Xvfb", (int)len, path) < sizeof xvfb_path &&
            access(xvfb_path, X_OK) == 0) return 1;
        path = end ? end + 1 : NULL;
    }
    return 0;
}
static int xvfb_start(void) {
    /* An explicitly selected safe display is already owned by the caller. */
    const char *display = getenv("DISPLAY"), *requested = getenv("EDIT_DISPLAY");
    if (display && requested && !strcmp(display, ":99") && !strcmp(requested, ":99"))
        return 1;
    if (!xvfb_find()) return 0;
    char explicit_display[32] = "";
    if (access("/tmp/.X11-unix", W_OK) != 0 &&
        !xvfb_explicit_display(explicit_display, sizeof explicit_display)) return 0;
    int fds[2];
    if (pipe(fds)) return 0;
    char fdarg[16];
    snprintf(fdarg, sizeof fdarg, "%d", fds[1]);
    pid_t pid = fork();
    if (pid < 0) { close(fds[0]); close(fds[1]); return 0; }
    if (pid == 0) {
        close(fds[0]);
        prctl(PR_SET_PDEATHSIG, SIGTERM);
        unsetenv("DISPLAY");
        /* Explicit selection permits a partial listener set (abstract only);
         * automatic -displayfd selection requires the pathname socket too. */
        if (*explicit_display)
            execl(xvfb_path, "Xvfb", explicit_display, "-displayfd", fdarg, "-screen", "0", "640x480x24", "-nolisten", "tcp", "-noreset", (char *)NULL);
        else
            execl(xvfb_path, "Xvfb", "-displayfd", fdarg, "-screen", "0", "640x480x24", "-nolisten", "tcp", "-noreset", (char *)NULL);
        _exit(127);
    }
    close(fds[1]);
    char buf[32]; size_t n = 0;
    while (n < sizeof buf - 1) {
        ssize_t r = read(fds[0], buf + n, 1);
        if (r <= 0) break;
        if (buf[n] == '\n') break;
        n++;
    }
    close(fds[0]);
    buf[n] = 0;
    if (n == 0) { kill(pid, SIGTERM); waitpid(pid, NULL, 0); return 0; }
    char disp[48];
    snprintf(disp, sizeof disp, ":%s", buf);
    setenv("DISPLAY", disp, 1);
    xvfb_pid = pid;
    atexit(xvfb_stop);
    return 1;
}
#endif
