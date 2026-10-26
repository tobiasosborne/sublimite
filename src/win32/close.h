#ifndef EDIT_WIN32_CLOSE_H
#define EDIT_WIN32_CLOSE_H
#if defined(_WIN32)
#include "os/os.h"
#include <stdatomic.h>
/* Main owns this service until editor teardown. Named native events bridge
 * the context-free console callback to a caller-owned wake without globals. */
typedef struct win32_close {
    os_wake wake;
    os_thread bridge;
    void *request, *complete;
    _Atomic bool stopping;
    bool wake_ready, thread_ready, registered;
} win32_close;
int win32_close_init(win32_close *close_request);
void win32_close_finish(win32_close *close_request);
#endif
#endif
