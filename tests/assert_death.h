/* Death test for the tree's one ASSERT (A15, src/os/os.h): a violated
 * precondition runs in a forked child, which must die by SIGABRT after
 * writing exactly the fixed message "<file>:<line>: ASSERT failed: <expr>\n".
 * Fork while the suite is single-threaded: the child calls only the API
 * under test. */
#ifndef TEST_ASSERT_DEATH_H
#define TEST_ASSERT_DEATH_H
#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

/* file and expr are the ASSERT's __FILE__ and stringized argument. */
static bool assert_death(void (*violate)(void), const char *file, const char *expr)
{
    int p[2];
    if (pipe(p) != 0) return false;
    fflush(NULL);
    pid_t pid = fork();
    if (pid < 0) return false;
    if (pid == 0) {
        close(p[0]);
        if (dup2(p[1], STDERR_FILENO) != STDERR_FILENO) _exit(2);
        violate();
        _exit(0); /* Survived: the precondition is unchecked. */
    }
    close(p[1]);
    char text[512];
    size_t got = 0;
    for (;;) {
        ssize_t k = read(p[0], text + got, sizeof text - 1 - got);
        if (k < 0 && errno == EINTR) continue;
        if (k <= 0) break;
        got += (size_t)k;
        if (got == sizeof text - 1) break;
    }
    text[got] = '\0';
    close(p[0]);
    int status = 0;
    pid_t reaped;
    do reaped = waitpid(pid, &status, 0); while (reaped < 0 && errno == EINTR);

    bool died = reaped == pid && WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT;
    size_t fn = strlen(file), at = fn + 1;
    bool message = strncmp(text, file, fn) == 0 && text[fn] == ':';
    while (message && text[at] >= '0' && text[at] <= '9') at++;
    static const char tag[] = ": ASSERT failed: ";
    size_t en = strlen(expr);
    message = message && at > fn + 1 && strncmp(text + at, tag, sizeof tag - 1) == 0 &&
              strncmp(text + at + sizeof tag - 1, expr, en) == 0 &&
              strcmp(text + at + sizeof tag - 1 + en, "\n") == 0;
    printf("assert_death: %s (%s): %s child, stderr \"%.*s\"\n", file, expr,
           died ? "SIGABRT" : WIFEXITED(status) ? "exited" : "other",
           (int)(got && text[got - 1] == '\n' ? got - 1 : got), text);
    fflush(stdout);
    return died && message;
}
#endif
