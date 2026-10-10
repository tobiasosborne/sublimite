/* A fixture owns every borrowed argument until process termination. The outer
 * supervisor covers setup/exit; the monotonic timer covers individual waits,
 * including library joins that do not expose a timed API. No timer is global. */
#ifndef EDIT_SAVECTL_DEADLINE_H
#define EDIT_SAVECTL_DEADLINE_H
#include "harness.h"
#include <errno.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>
#include <signal.h>
static void savectl_phase_expired(int signal_number)
{
    (void)signal_number;
    const char message[] = "savectl fixture FAIL: monotonic phase deadline; terminating without joins\n";
    ssize_t ignored = write(STDERR_FILENO, message, sizeof message - 1u); (void)ignored;
    _exit(2);
}
static int savectl_deadline_init(timer_t *timer)
{
    struct sigaction action = {.sa_handler = savectl_phase_expired};
    if (sigemptyset(&action.sa_mask) || sigaction(SIGALRM, &action, NULL)) return -1;
    struct sigevent event = {.sigev_notify = SIGEV_SIGNAL, .sigev_signo = SIGALRM};
    return timer_create(CLOCK_MONOTONIC, &event, timer);
}
static int savectl_deadline_arm(timer_t timer, uint64_t ns)
{
    struct itimerspec value = {.it_value = {(time_t)(ns / UINT64_C(1000000000)),
                                          (long)(ns % UINT64_C(1000000000))}};
    return timer_settime(timer, 0, &value, NULL);
}
static int savectl_fixture_supervise(int (*run)(void *), void *arg, uint64_t budget, const char *label)
{
    fflush(NULL); pid_t child=fork(); if (child<0) return 2;
    if (!child) { int rc=run(arg); fflush(NULL); exit(rc); }
    uint64_t deadline=bench_now_ns()+budget; bool terminating=false;
    for (;;) {
        int status=0; pid_t got=waitpid(child,&status,WNOHANG);
        if (got==child) {
            if (WIFEXITED(status)) return WEXITSTATUS(status);
            fprintf(stderr,"savectl fixture FAIL: %s signal=%d\n",label,WIFSIGNALED(status) ? WTERMSIG(status) : 0);
            return 2;
        }
        if (got<0 && errno!=EINTR) return 2;
        if (bench_now_ns()>=deadline) {
            if (terminating) { fprintf(stderr,"savectl fixture FAIL: %s termination not yet reaped\n",label); return 2; }
            fprintf(stderr,"savectl fixture FAIL: %s deadline; terminating fixture (no shutdown join)\n",label);
            (void)kill(child,SIGKILL); terminating=true;
            deadline=bench_now_ns()+UINT64_C(1000000000);
        }
        (void)nanosleep(&(struct timespec){0,1000000},NULL);
    }
}
#endif
