#ifndef EDIT_TEST_CORE_PROC_H
#define EDIT_TEST_CORE_PROC_H
#include "os/os.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* Re-exec selectors keep crashes and live worker storage in a child, as in
 * os_test. The caller handles the exit/signal verdict for its own case. */
static inline os_err core_child(const char *selector, int64_t budget, os_proc_status *status)
{
    char exe[OS_PATH_MAX];
    os_err rc = os_proc_self_path(exe, sizeof exe);
    if (rc != OS_OK) return rc;
    const char *args[] = {exe, selector, NULL};
    os_proc child;
    rc = os_proc_spawn(&child, args);
    if (rc != OS_OK) return rc;
    rc = os_proc_wait(&child, budget, status);
    if (rc == OS_E_TIMEDOUT) {
        (void)os_proc_kill(&child);
        (void)os_proc_wait(&child, -1, status);
    }
    return rc;
}
#endif
