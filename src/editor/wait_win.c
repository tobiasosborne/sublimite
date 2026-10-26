#if defined(_WIN32)
#include "editor/wait.h"
#include "file/file.h"
#include <windows.h>
#include <string.h>

_Static_assert(EDITOR_WAIT_CAP + 1u + 4u == MAXIMUM_WAIT_OBJECTS, "platform wait bound");
os_err editor_wait_init(editor_wait_set *set)
{
    if (!set) return OS_E_INVAL;
    memset(set, 0, sizeof *set); return OS_OK;
}
void editor_wait_close(editor_wait_set *set) { if (set) set->count = 0; }
os_err editor_wait_add_handle(editor_wait_set *set, void *handle, uint32_t tag)
{
    if (!set || !handle || handle == INVALID_HANDLE_VALUE || !tag) return OS_E_INVAL;
    for (size_t i = 0; i < set->count; i++) {
        if (set->tags[i] == tag && set->sources[i] == handle) return OS_OK;
        if (set->tags[i] == tag || set->sources[i] == handle) return OS_E_EXIST;
    }
    if (set->count == EDITOR_WAIT_CAP) return OS_E_NOSPC;
    set->sources[set->count] = handle; set->tags[set->count++] = tag;
    return OS_OK;
}
os_err editor_wait_file(editor_wait_set *set, int token)
{ return token < 0 ? OS_E_INVAL : editor_wait_add_handle(set, file_watch_handle(), EDITOR_WAIT_FILE); }
os_err editor_wait_add(editor_wait_set *set, const os_wake *wake, uint32_t tag)
{ return wake ? editor_wait_add_handle(set, os_wake_handle(wake), tag) : OS_E_INVAL; }
os_err editor_wait_remove(editor_wait_set *set, uint32_t tag)
{
    if (!set) return OS_E_INVAL;
    for (size_t i = 0; i < set->count; i++) if (set->tags[i] == tag) {
        set->count--; set->sources[i] = set->sources[set->count]; set->tags[i] = set->tags[set->count];
        break;
    }
    return OS_OK;
}
size_t editor_wait_handles(void *ctx, void **handles, size_t capacity)
{
    const editor_wait_set *set = ctx;
    if (!set || set->count > capacity || (!handles && set->count)) return SIZE_MAX;
    if (set->count) memcpy(handles, set->sources, set->count * sizeof *handles);
    return set->count;
}
os_err editor_wait_ready(editor_wait_set *set, int timeout_ms,
                         uint32_t *tags, size_t capacity, size_t *count)
{
    if (!set || !tags || !capacity || !count) return OS_E_INVAL;
    *count = 0;
    if (timeout_ms && set->count) {
        DWORD result = WaitForMultipleObjects((DWORD)set->count, set->sources, FALSE,
                                              timeout_ms < 0 ? INFINITE : (DWORD)timeout_ms);
        if (result == WAIT_FAILED) return OS_E_IO;
        if (result == WAIT_TIMEOUT) return OS_OK;
    } else if (timeout_ms && !set->count) {
        Sleep(timeout_ms < 0 ? INFINITE : (DWORD)timeout_ms);
    }
    for (size_t i = 0; i < set->count && *count < capacity; i++) {
        DWORD result = WaitForSingleObject(set->sources[i], 0);
        if (result == WAIT_FAILED) return OS_E_IO;
        if (result == WAIT_OBJECT_0) tags[(*count)++] = set->tags[i];
    }
    return OS_OK;
}
#endif
