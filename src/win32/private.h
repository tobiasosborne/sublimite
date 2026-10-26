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
typedef struct win32_clip win32_clip;
typedef struct win32_plat {
    plat *owner;
    const plat_callbacks *callbacks;
    bool servicing;
    unsigned modal_depth, callback_depth;
    DWORD ui_thread;
    HWND hwnd;
    HINSTANCE instance;
    WCHAR class_name[64]; /* (P) prefix + pointer hex + terminating NUL fits. */
    HANDLE blink_timer, repeat_timer, present_wake;
    const os_wake *work_wake;
    uint32_t dpi, blink_ms, repeat_delay, repeat_rate, repeat_key, buttons;
    uint64_t repeat_next;
    LPARAM repeat_lparam;
    int32_t wheel_x, wheel_y, pointer_x, pointer_y;
    BYTE keys[256]; /* (P) Win32 virtual-key namespace size. */
    bool hidden[256], composing, present_enabled, layout_altgr, real_ctrl;
    WCHAR surrogate;
    UINT surrogate_scan, native_scan;
    bool native_text, suppress_char, native_repeat, alt_numeric, ime;
    bool commit_pending, commit_composed;
    unsigned commit_scalars;
    plat_event native_event, commit_event;
    plat_event repeat_event, queue[WIN32_EVENT_CAP];
    uint32_t head, tail, dropped;
    uint8_t *clip_data;
    size_t clip_len;
    win32_clip *clip;
    SRWLOCK present_lock;
    win32_present presents[WIN32_EVENT_CAP];
    uint32_t present_head, present_tail;
} win32_plat;
static inline win32_plat *win32_private(const plat *p)
{ return p && p->backend == PLAT_BACKEND_WIN32 ? p->backend_state : NULL; }

int win32_clip_init(win32_plat *s);
void win32_clip_stop(win32_plat *s);
HANDLE win32_clip_wake(const win32_plat *s);
int win32_clip_submit(win32_plat *s, bool set, int which, const void *bytes, size_t len);
bool win32_clip_poll(win32_plat *s, plat_event *ev);

/* All plat entries except native-handle reads and the T6 producer below
 * belong to the creating UI thread. Wrong-thread void entries are no-ops;
 * status entries fail. Shutdown is retried by the owner, never marshalled
 * synchronously (which could deadlock a caller being joined by the UI). */
/* W1.2 renderer -> platform handoff. Thread safe, allocation free. serial is
 * the frame id, ust is monotonic ns on os_now_ns's QPC clock, msc is DXGI's
 * displayed-frame counter. Called only for actual present statistics, never
 * submission. Returns FAIL on a full mailbox; caller retains/retries that
 * record. Stop/join the renderer before plat_shutdown. UI delivers T6 through
 * on_present_complete; no renderer thread calls application callbacks. */
int win32_plat_present_complete(plat *p, uint32_t serial, uint64_t ust, uint64_t msc);
#endif
#endif
