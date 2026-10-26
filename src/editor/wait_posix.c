#if !defined(_WIN32)
#include "editor/wait.h"
#include <errno.h>
#include <string.h>
#include <sys/epoll.h>
#include <unistd.h>

os_err editor_wait_init(editor_wait_set *set)
{
    if (!set) return OS_E_INVAL;
    memset(set, 0, sizeof *set);
    set->readiness.fd = epoll_create1(EPOLL_CLOEXEC);
    return set->readiness.fd < 0 ? OS_E_IO : OS_OK;
}
void editor_wait_close(editor_wait_set *set)
{
    if (!set) return;
    if (set->readiness.fd >= 0) (void)close(set->readiness.fd);
    set->readiness.fd = -1; set->count = 0;
}
const os_wake *editor_wait_wake(const editor_wait_set *set)
{ return set ? &set->readiness : NULL; }
os_err editor_wait_add_fd(editor_wait_set *set, int fd, uint32_t tag)
{
    if (!set || fd < 0 || !tag) return OS_E_INVAL;
    for (size_t i = 0; i < set->count; i++) {
        if (set->tags[i] == tag && set->sources[i] == fd) return OS_OK;
        if (set->tags[i] == tag || set->sources[i] == fd) return OS_E_EXIST;
    }
    if (set->count == EDITOR_WAIT_CAP) return OS_E_NOSPC;
    struct epoll_event event = {.events = EPOLLIN, .data.u32 = tag};
    if (epoll_ctl(set->readiness.fd, EPOLL_CTL_ADD, fd, &event)) return OS_E_IO;
    set->sources[set->count] = fd; set->tags[set->count++] = tag;
    return OS_OK;
}
os_err editor_wait_file(editor_wait_set *set, int token)
{ return editor_wait_add_fd(set, token, EDITOR_WAIT_FILE); }
os_err editor_wait_add(editor_wait_set *set, const os_wake *wake, uint32_t tag)
{ return wake ? editor_wait_add_fd(set, os_wake_fd(wake), tag) : OS_E_INVAL; }
os_err editor_wait_remove(editor_wait_set *set, uint32_t tag)
{
    if (!set) return OS_E_INVAL;
    for (size_t i = 0; i < set->count; i++) if (set->tags[i] == tag) {
        if (epoll_ctl(set->readiness.fd, EPOLL_CTL_DEL, set->sources[i], NULL)) return OS_E_IO;
        set->count--; set->sources[i] = set->sources[set->count]; set->tags[i] = set->tags[set->count];
        return OS_OK;
    }
    return OS_OK;
}
os_err editor_wait_ready(editor_wait_set *set, int timeout_ms,
                         uint32_t *tags, size_t capacity, size_t *count)
{
    if (!set || !tags || !capacity || !count) return OS_E_INVAL;
    *count = 0;
    if (capacity > EDITOR_WAIT_CAP) capacity = EDITOR_WAIT_CAP;
    struct epoll_event events[EDITOR_WAIT_CAP];
    int n = epoll_wait(set->readiness.fd, events, (int)capacity, timeout_ms);
    if (n < 0) return errno == EINTR ? OS_OK : OS_E_IO;
    for (int i = 0; i < n; i++) tags[(*count)++] = events[i].data.u32;
    return OS_OK;
}
#endif
