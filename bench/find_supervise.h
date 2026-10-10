/* Isolate fixtures before creating threads. A failed worker keeps all of its
 * arguments until process termination; the parent never joins that worker. */
#ifndef EDIT_FIND_SUPERVISE_H
#define EDIT_FIND_SUPERVISE_H
#include "harness.h"
#include <errno.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>
static int find_fixture_supervise(int (*run)(void *), void *arg,
                                  uint64_t budget, const char *label)
{
    fflush(NULL);
    pid_t child = fork();
    if (child < 0) { perror("fork"); return 2; }
    /* Successful fixtures have retired their workers and released storage.
     * Normal exit preserves registered cleanup/leak-check hooks; only timeout
     * paths terminate without attempting cleanup of live worker arguments. */
    if (!child) { int rc = run(arg); fflush(NULL); exit(rc); }
    uint64_t deadline = bench_now_ns() + budget;
    for (;;) {
        int status;
        pid_t result = waitpid(child, &status, WNOHANG);
        if (result == child) return WIFEXITED(status) ? WEXITSTATUS(status) : 2;
        if ((result < 0 && errno != EINTR) || bench_now_ns() >= deadline) {
            fprintf(stderr, "find fixture FAIL: %s deadline; terminating fixture (no shutdown join)\n", label);
            (void)kill(child, SIGKILL);
            while (waitpid(child, &status, 0) < 0 && errno == EINTR) {}
            return 2;
        }
        const struct timespec delay = {0, 1000000};
        (void)nanosleep(&delay, NULL);
    }
}
#endif
