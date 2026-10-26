#ifndef EDIT_WIN32_PRIVATE_H
#define EDIT_WIN32_PRIVATE_H
#if defined(_WIN32)
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#include <windows.h>
#include "x11/plat.h"
#include "os/os.h"

/* (G) same fixed queue capacity as P2.2; no allocation during input. */
#define WIN32_EVENT_CAP 256u
typedef struct win32_present {
    uint32_t serial;
    uint64_t ust, msc;
} win32_present;
typedef struct win32_plat {
    plat *owner;
    HWND hwnd;
    HINSTANCE instance;
    WCHAR class_name[64]; /* (P) prefix + pointer hex + terminating NUL fits. */
    HANDLE blink_timer, repeat_timer, present_wake;
    const os_wake *work_wake;
    uint32_t dpi, blink_ms, repeat_delay, repeat_rate, repeat_key, buttons;
    uint64_t repeat_next;
    LPARAM repeat_lparam;
    int32_t wheel_x, wheel_y;
    BYTE keys[256]; /* (P) Win32 virtual-key namespace size. */
    bool hidden[256], composing, present_enabled;
    WCHAR surrogate;
    plat_event repeat_event, queue[WIN32_EVENT_CAP];
    uint32_t head, tail, dropped;
    uint8_t *clip_data;
    size_t clip_len;
    bool clip_owned;
    SRWLOCK present_lock;
    win32_present presents[WIN32_EVENT_CAP];
    uint32_t present_head, present_tail;
} win32_plat;
static inline win32_plat *win32_private(const plat *p)
{ return p && p->backend == PLAT_BACKEND_WIN32 ? p->backend_state : NULL; }

/* W1.2 renderer -> platform handoff. Thread safe, allocation free. serial is
 * the frame id, ust is monotonic ns on os_now_ns's QPC clock, msc is DXGI's
 * displayed-frame counter. Called only for actual present statistics, never
 * submission. Returns FAIL on a full mailbox; caller retains/retries that
 * record. Stop/join the renderer before plat_shutdown. UI delivers T6 through
 * on_present_complete; no renderer thread calls application callbacks. */
int win32_plat_present_complete(plat *p, uint32_t serial, uint64_t ust, uint64_t msc);
#endif
#endif
