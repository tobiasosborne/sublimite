/* x11_xvfb.h - private X server for tests that take selection ownership (P2.2b).
 * make check usually runs with DISPLAY=:0, Tobias's real session; a test that owns CLIPBOARD
 * or talks to CLIPBOARD_MANAGER there would clobber his clipboard. So these tests never use the
 * inherited DISPLAY: they start Xvfb (-displayfd), point DISPLAY at it, and kill it at exit.
 * Returns 1 when a private server runs, 0 when Xvfb is missing (caller prints "skipped (no Xvfb)"). */
#ifndef X11_XVFB_H
#define X11_XVFB_H
#include <signal.h>
#include <sys/prctl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static pid_t xvfb_pid = -1;
static void xvfb_stop(void) {
    if (xvfb_pid > 0) { kill(xvfb_pid, SIGTERM); waitpid(xvfb_pid, NULL, 0); xvfb_pid = -1; }
}
static int xvfb_start(void) {
    /* An explicitly selected safe display is already owned by the caller. */
    const char *display = getenv("DISPLAY"), *requested = getenv("EDIT_DISPLAY");
    if (display && requested && !strcmp(display, ":99") && !strcmp(requested, ":99"))
        return 1;
    if (access("/usr/bin/Xvfb", X_OK) != 0) return 0;
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
        execl("/usr/bin/Xvfb", "Xvfb", "-displayfd", fdarg, "-screen", "0", "640x480x24", "-nolisten", "tcp", "-noreset", (char *)NULL);
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
