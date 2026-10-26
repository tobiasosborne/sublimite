#ifndef EDIT_TEST_CORE_SYNC_H
#define EDIT_TEST_CORE_SYNC_H
#include "os/os.h"
#include <stdlib.h>
#if defined(_WIN32)
#include <malloc.h>
#else
#include <errno.h>
#include <unistd.h>
#endif
static inline void *core_aligned_alloc(size_t alignment, size_t size)
{
#if defined(_WIN32)
    return _aligned_malloc(size, alignment);
#else
    return aligned_alloc(alignment, size);
#endif
}
static inline void core_aligned_free(void *p)
{
#if defined(_WIN32)
    _aligned_free(p);
#else
    free(p);
#endif
}
/* A single completion token, backed by the same blocking pipe on Linux and
 * a kernel event on Windows. Neither path spins through a blocked job. */
typedef struct core_pipe {
#if defined(_WIN32)
    os_wake wake;
#else
    int fd[2];
#endif
} core_pipe;
static inline int core_pipe_init(core_pipe *p)
{
#if defined(_WIN32)
    return os_wake_init(&p->wake) == OS_OK ? 0 : -1;
#else
    return pipe(p->fd);
#endif
}
static inline int core_pipe_read(core_pipe *p)
{
#if defined(_WIN32)
    if (os_wake_wait(&p->wake, -1) != OS_OK) return -1;
    os_wake_drain(&p->wake); return 1;
#else
    unsigned char b; ssize_t n;
    do { n = read(p->fd[0], &b, 1); } while (n < 0 && errno == EINTR);
    return (int)n;
#endif
}
static inline int core_pipe_write(core_pipe *p, unsigned char byte)
{
#if defined(_WIN32)
    (void)byte; os_wake_signal(&p->wake); return 1;
#else
    return (int)write(p->fd[1], &byte, 1);
#endif
}
static inline int core_pipe_close(core_pipe *p)
{
#if defined(_WIN32)
    os_wake_close(&p->wake); return 0;
#else
    int a = close(p->fd[0]), b = close(p->fd[1]); return a || b ? -1 : 0;
#endif
}
typedef struct core_poll { const os_wake *wake; int events, revents; } core_poll;
#define CORE_READY 1
static inline int core_poll_wait(core_poll *p, int n, int ms)
{
    (void)n;
    os_err rc = os_wake_wait(p->wake, (int64_t)ms * 1000000);
    p->revents = rc == OS_OK ? CORE_READY : 0;
    return rc == OS_OK ? 1 : rc == OS_E_TIMEDOUT ? 0 : -1;
}
/* One-shot start barrier for the snapshot owner race. */
typedef struct core_barrier { os_mutex mu; os_cond cv; unsigned arrived, goal; } core_barrier;
static inline int core_barrier_init(core_barrier *b, unsigned goal)
{
    b->arrived = 0; b->goal = goal;
    if (os_mutex_init(&b->mu) != OS_OK) return -1;
    if (os_cond_init(&b->cv) != OS_OK) { os_mutex_destroy(&b->mu); return -1; }
    return 0;
}
static inline int core_barrier_wait(core_barrier *b)
{
    os_mutex_lock(&b->mu);
    if (++b->arrived == b->goal) os_cond_broadcast(&b->cv);
    while (b->arrived < b->goal) os_cond_wait(&b->cv, &b->mu);
    os_mutex_unlock(&b->mu); return 0;
}
static inline int core_barrier_destroy(core_barrier *b)
{ os_cond_destroy(&b->cv); os_mutex_destroy(&b->mu); return 0; }
static inline int core_cond_destroy(os_cond *c) { os_cond_destroy(c); return 0; }
static inline int core_mutex_destroy(os_mutex *m) { os_mutex_destroy(m); return 0; }
#endif
