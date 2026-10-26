#if defined(_WIN32)
#include "win32/close.h"
#include <windows.h>
#include <wchar.h>
#include <string.h>

static void close_name(WCHAR *name, size_t capacity, bool complete)
{
    (void)swprintf(name, capacity, L"Local\\sublimite-close-%lu-%ls",
                  (unsigned long)GetCurrentProcessId(), complete ? L"done" : L"request");
}
static BOOL WINAPI console_control(DWORD control)
{
    if (control != CTRL_C_EVENT && control != CTRL_BREAK_EVENT && control != CTRL_CLOSE_EVENT &&
        control != CTRL_LOGOFF_EVENT && control != CTRL_SHUTDOWN_EVENT) return FALSE;
    /* (P) prefix + DWORD decimal pid (at most ten digits) + suffix + NUL
     * fits this stack buffer. No process-global callback context is needed. */
    WCHAR name[64]; close_name(name, sizeof name / sizeof name[0], false);
    HANDLE request = OpenEventW(EVENT_MODIFY_STATE, FALSE, name);
    close_name(name, sizeof name / sizeof name[0], true);
    HANDLE complete = OpenEventW(SYNCHRONIZE, FALSE, name);
    if (!request || !complete) {
        if (request) CloseHandle(request);
        if (complete) CloseHandle(complete);
        return FALSE;
    }
    BOOL signalled = SetEvent(request); CloseHandle(request);
    /* Windows may terminate the process as soon as a close/logoff handler
     * returns. Keep it alive while the UI takes its normal clean-close path. */
    if (signalled) (void)WaitForSingleObject(complete, INFINITE);
    CloseHandle(complete); return signalled;
}
static void close_bridge(void *ctx)
{
    win32_close *close_request = ctx;
    if (WaitForSingleObject(close_request->request, INFINITE) == WAIT_OBJECT_0 &&
        !atomic_load_explicit(&close_request->stopping, memory_order_acquire))
        os_wake_signal(&close_request->wake);
}
void win32_close_finish(win32_close *close_request)
{
    if (!close_request) return;
    if (close_request->registered) (void)SetConsoleCtrlHandler(console_control, FALSE);
    atomic_store_explicit(&close_request->stopping, true, memory_order_release);
    if (close_request->complete) (void)SetEvent(close_request->complete);
    if (close_request->request) (void)SetEvent(close_request->request);
    if (close_request->thread_ready) (void)os_thread_join(&close_request->bridge);
    if (close_request->request) CloseHandle(close_request->request);
    if (close_request->complete) CloseHandle(close_request->complete);
    if (close_request->wake_ready) os_wake_close(&close_request->wake);
    memset(close_request, 0, sizeof *close_request);
}
int win32_close_init(win32_close *close_request)
{
    if (!close_request) return -1;
    memset(close_request, 0, sizeof *close_request);
    atomic_init(&close_request->stopping, false);
    if (os_wake_init(&close_request->wake) != OS_OK) return -1;
    close_request->wake_ready = true;
    WCHAR name[64]; close_name(name, sizeof name / sizeof name[0], false);
    close_request->request = CreateEventW(NULL, TRUE, FALSE, name);
    if (!close_request->request || GetLastError() == ERROR_ALREADY_EXISTS) goto fail;
    close_name(name, sizeof name / sizeof name[0], true);
    close_request->complete = CreateEventW(NULL, TRUE, FALSE, name);
    if (!close_request->complete || GetLastError() == ERROR_ALREADY_EXISTS) goto fail;
    if (os_thread_start(&close_request->bridge, close_bridge, close_request) != OS_OK) goto fail;
    close_request->thread_ready = true;
    if (!SetConsoleCtrlHandler(console_control, TRUE)) goto fail;
    close_request->registered = true; return 0;
fail:
    win32_close_finish(close_request); return -1;
}
#endif
