#ifndef EDIT_TEST_WORK_HOOKS_H
#define EDIT_TEST_WORK_HOOKS_H
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
/* Inject at os.h rather than interposing a CRT pthread implementation. The
 * production work object is replaced only inside this suite's translation unit. */
static int inject_stage, init_stage, thread_attempts, bad_destroy, dead_locks;
static bool injecting, fault_fired, watch_dead;
static bool mutex_live, cond_live[2], foreground_cond_live, fail_eventfd;
static HANDLE last_eventfd;
static os_err test_mutex_init(os_mutex *m)
{
    if (injecting && ++init_stage == inject_stage) { fault_fired = true; return OS_E_NOMEM; }
    os_err rc = os_mutex_init(m);
    if (m == &pool.mu) mutex_live = rc == OS_OK;
    return rc;
}
static os_err test_cond_init(os_cond *c)
{
    if (injecting && ++init_stage == inject_stage) { fault_fired = true; return OS_E_NOMEM; }
    os_err rc = os_cond_init(c);
    for (size_t i = 0; i < 2; i++) if (c == &pool.cv[i]) cond_live[i] = rc == OS_OK;
    if (c == &pool.foreground_cv) foreground_cond_live = rc == OS_OK;
    return rc;
}
static os_err test_thread_start(os_thread *t, void (*fn)(void *), void *arg)
{
    if (injecting) {
        thread_attempts++;
        if (fault_fired || ++init_stage == inject_stage) { fault_fired = true; return OS_E_AGAIN; }
    }
    return os_thread_start(t, fn, arg);
}
static void test_mutex_destroy(os_mutex *m)
{
    if (m == &pool.mu) {
        if (!mutex_live) { bad_destroy++; return; }
        mutex_live = false;
    }
    os_mutex_destroy(m);
}
static void test_cond_destroy(os_cond *c)
{
    for (size_t i = 0; i < 2; i++) if (c == &pool.cv[i]) {
        if (!cond_live[i]) { bad_destroy++; return; }
        cond_live[i] = false;
    }
    if (c == &pool.foreground_cv) {
        if (!foreground_cond_live) { bad_destroy++; return; }
        foreground_cond_live = false;
    }
    os_cond_destroy(c);
}
static void test_mutex_lock(os_mutex *m)
{
    if (m == &pool.mu && watch_dead) { dead_locks++; return; }
    os_mutex_lock(m);
}
static void test_mutex_unlock(os_mutex *m)
{
    if (m == &pool.mu && watch_dead) return;
    os_mutex_unlock(m);
}
static os_err test_wake_init(os_wake *w)
{
    if (fail_eventfd) return OS_E_NOSPC;
    os_err rc = os_wake_init(w);
    if (injecting) last_eventfd = os_wake_handle(w);
    return rc;
}
static bool test_wake_closed(void)
{
    DWORD flags;
    return last_eventfd && !GetHandleInformation(last_eventfd, &flags) && GetLastError() == ERROR_INVALID_HANDLE;
}
static void resolve_sync(void) {}
#define os_mutex_init test_mutex_init
#define os_cond_init test_cond_init
#define os_thread_start test_thread_start
#define os_mutex_destroy test_mutex_destroy
#define os_cond_destroy test_cond_destroy
#define os_mutex_lock test_mutex_lock
#define os_mutex_unlock test_mutex_unlock
#define os_wake_init test_wake_init
#include "../../src/work/work.c"
#undef os_mutex_init
#undef os_cond_init
#undef os_thread_start
#undef os_mutex_destroy
#undef os_cond_destroy
#undef os_mutex_lock
#undef os_mutex_unlock
#undef os_wake_init
#endif
