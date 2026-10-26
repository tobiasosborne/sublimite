#if !defined(_WIN32)
#include "work/work.h"

/* Compatibility for Linux event loops until their W2 migration. */
int work_pool_eventfd(const work_pool *p)
{
    return p->active ? os_wake_fd(&p->wake) : -1;
}
#endif
