/* Win32 implementation of the portable platform contract. */
#if defined(_WIN32)
#include "private.h"
#include <windowsx.h>
#include <dwmapi.h>
#include <shellscalingapi.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include "keysyms.h"

#define NS_MS UINT64_C(1000000)
#define NS_S UINT64_C(1000000000)

static bool push(win32_plat *s, plat_event ev)
{
    if (s->tail-s->head == WIN32_EVENT_CAP) { s->dropped++; return false; }
    s->queue[s->tail++ % WIN32_EVENT_CAP] = ev;
    return true;
}
bool plat_poll_event(plat *p, plat_event *out)
{
    win32_plat *s=win32_private(p);
    if (!s || !out || s->head==s->tail) return false;
    *out=s->queue[s->head++ % WIN32_EVENT_CAP]; return true;
}
static void timer(HANDLE handle, uint32_t ms)
{
    if (!handle) return;
    (void)CancelWaitableTimer(handle);
    /* Cancel does not reset a signalled timer; setting then cancelling does. */
    LARGE_INTEGER due; due.QuadPart=-(LONGLONG)(ms ? ms : 1u)*10000;
    (void)SetWaitableTimer(handle,&due,0,NULL,NULL,FALSE);
    if (!ms) (void)CancelWaitableTimer(handle);
}
static void repeat_cancel(win32_plat *s)
{ s->repeat_key=0; s->repeat_next=0; timer(s->repeat_timer,0); }
void plat_set_repeat(plat *p, uint32_t delay_ms, uint32_t rate_hz)
{
    win32_plat *s=win32_private(p); if (!s) return;
    s->repeat_delay=delay_ms;
    s->repeat_rate=rate_hz>1000u ? 1000u : rate_hz; /* (G) X11_REPEAT_MAX_HZ. */
    if (!rate_hz) repeat_cancel(s);
    else if (s->repeat_key) {
        s->repeat_next=os_now_ns()+(uint64_t)delay_ms*NS_MS;
        timer(s->repeat_timer,delay_ms ? delay_ms : 1u);
    }
}
void plat_set_blink(plat *p, uint32_t ms)
{
    win32_plat *s=win32_private(p); if (!s) return;
    s->blink_ms=ms; timer(s->blink_timer,p->focused ? ms : 0);
}
static uint16_t mods(const BYTE keys[256])
{
    uint16_t m=0;
    if (keys[VK_SHIFT]&0x80) m|=PLAT_MOD_SHIFT;
    if (keys[VK_CONTROL]&0x80) m|=PLAT_MOD_CTRL;
    if (keys[VK_MENU]&0x80) m|=PLAT_MOD_ALT;
    if ((keys[VK_LWIN]|keys[VK_RWIN])&0x80) m|=PLAT_MOD_SUPER;
    if (keys[VK_CAPITAL]&1) m|=PLAT_MOD_CAPS;
    if (keys[VK_NUMLOCK]&1) m|=PLAT_MOD_NUM;
    return m;
}
static uint32_t scalar(const WCHAR *wide, int count)
{
    if (count==1 && (wide[0]<0xd800 || wide[0]>0xdfff)) return wide[0];
    if (count==2 && wide[0]>=0xd800 && wide[0]<=0xdbff && wide[1]>=0xdc00 && wide[1]<=0xdfff)
        return 0x10000u+((uint32_t)wide[0]-0xd800u)*0x400u+(uint32_t)wide[1]-0xdc00u;
    return 0;
}
static uint32_t keysym(uint32_t code)
{
    if (code<=0xff) return code;
    size_t lo=0, hi=sizeof win32_legacy_keysyms / sizeof win32_legacy_keysyms[0];
    while (lo<hi) {
        size_t mid=lo+(hi-lo)/2u;
        if (win32_legacy_keysyms[mid].scalar<code) lo=mid+1u; else hi=mid;
    }
    if (lo<sizeof win32_legacy_keysyms / sizeof win32_legacy_keysyms[0] && win32_legacy_keysyms[lo].scalar==code)
        return win32_legacy_keysyms[lo].keysym;
    return 0x01000000u|code;
}
static void text(plat_event *ev, uint32_t code)
{
    if (code<0x20 || (code>=0x7f && code<=0x9f) || code>0x10ffff ||
        (code>=0xd800 && code<=0xdfff)) return;
    if (ev->mods&(PLAT_MOD_CTRL|PLAT_MOD_ALT|PLAT_MOD_SUPER)) return;
    WCHAR wide[2]; int count=1;
    if (code>0xffff) {
        uint32_t rest=code-0x10000u; count=2;
        wide[0]=(WCHAR)(0xd800u+(rest>>10)); wide[1]=(WCHAR)(0xdc00u+(rest&0x3ffu));
    } else wide[0]=(WCHAR)code;
    int n=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,wide,count,(char *)ev->utf8,PLAT_UTF8_MAX,NULL,NULL);
    if (n>0) ev->utf8_len=(uint8_t)n;
}
static uint64_t stamp(void)
{
    /* GetMessageTime is the queued message's GetTickCount epoch. The unsigned
     * subtraction handles wrap; QPC remains the shared monotonic epoch. */
    uint64_t now=os_now_ns();
    uint32_t age=GetTickCount()-(uint32_t)GetMessageTime();
    uint64_t elapsed=(uint64_t)age*NS_MS;
    return elapsed<=now ? now-elapsed : now;
}
static bool modifier(UINT vk)
{ return (vk>=VK_SHIFT && vk<=VK_MENU) || (vk>=VK_LSHIFT && vk<=VK_RMENU) || vk==VK_LWIN || vk==VK_RWIN || vk==VK_CAPITAL || vk==VK_NUMLOCK || vk==VK_SCROLL; }
static UINT sided(UINT vk, LPARAM lp)
{
    bool ext=((uintptr_t)lp&(1u<<24))!=0;
    if (vk==VK_SHIFT) return MapVirtualKeyW((UINT)(((uintptr_t)lp>>16)&0xffu),MAPVK_VSC_TO_VK_EX);
    if (vk==VK_CONTROL) return ext ? VK_RCONTROL : VK_LCONTROL;
    if (vk==VK_MENU) return ext ? VK_RMENU : VK_LMENU;
    return vk;
}
static void clear_compose(win32_plat *s)
{
    BYTE clean[256]={0}; WCHAR buffer[8];
    /* A space flushes ToUnicodeEx's thread-local dead-key state. */
    (void)ToUnicodeEx(VK_SPACE,MapVirtualKeyW(VK_SPACE,MAPVK_VK_TO_VSC),clean,buffer,8,0,GetKeyboardLayout(0));
    s->composing=false; s->surrogate=0;
}
static bool translate(win32_plat *s, plat_event *ev, UINT vk, LPARAM lp, bool compose)
{
    UINT mapped=sided(vk,lp); bool press=ev->press;
    ev->keysym=mapped<256 ? win32_vk_keysyms[mapped] : 0;
    if (vk==VK_RETURN && ((uintptr_t)lp&(1u<<24))) ev->keysym=0xff8du; /* KP_Enter */
    if (vk==VK_TAB && (ev->mods&PLAT_MOD_SHIFT)) ev->keysym=0xfe20u; /* ISO_Left_Tab */
    bool keypad=vk>=VK_NUMPAD0 && vk<=VK_DIVIDE;
    if (!ev->keysym || keypad) {
        BYTE symbols[256]; memcpy(symbols,s->keys,sizeof symbols);
        WCHAR wide[8]={0}; UINT scan=(UINT)(((uintptr_t)lp>>16)&0xffu);
        HKL layout=GetKeyboardLayout(0);
        /* Probe without changing dead-key state (Windows 10 1607+). Ctrl+Alt
         * producing printable text is AltGr, not an application chord. */
        bool altgr=false;
        if ((ev->mods&(PLAT_MOD_CTRL|PLAT_MOD_ALT))==(PLAT_MOD_CTRL|PLAT_MOD_ALT)) {
            int probe=ToUnicodeEx(vk,scan,symbols,wide,8,4,layout);
            uint32_t cp=scalar(wide,probe);
            altgr=probe<0 || cp>=0x20;
        }
        if (altgr) ev->mods=(uint16_t)((ev->mods&~(PLAT_MOD_CTRL|PLAT_MOD_ALT))|PLAT_MOD_ALTGR);
        else {
            symbols[VK_CONTROL]=symbols[VK_LCONTROL]=symbols[VK_RCONTROL]=0;
            symbols[VK_MENU]=symbols[VK_LMENU]=symbols[VK_RMENU]=0;
        }
        int n=ToUnicodeEx(vk,scan,symbols,wide,8,compose ? 0u : 4u,layout);
        uint32_t cp=scalar(wide,n);
        if (compose && n<0) { s->composing=true; s->hidden[vk]=true; repeat_cancel(s); return false; }
        if (compose && s->composing) {
            s->composing=false;
            if (!cp) { s->hidden[vk]=true; repeat_cancel(s); return false; }
        }
        if (!keypad) ev->keysym=keysym(cp);
        if (press) text(ev,cp);
    }
    if (compose && s->composing && !modifier(vk) && ev->keysym) {
        clear_compose(s); repeat_cancel(s); s->hidden[vk]=true; return false;
    }
    return true;
}
static void key_event(win32_plat *s, UINT msg, WPARAM wp, LPARAM lp)
{
    UINT vk=(UINT)wp;
    if (vk>=256) return;
    bool press=msg==WM_KEYDOWN || msg==WM_SYSKEYDOWN;
    if (press && ((uintptr_t)lp&(UINT64_C(1)<<30))) return;
    (void)GetKeyboardState(s->keys);
    plat_event ev={.kind=PLAT_EV_KEY,.code=vk,.press=press,.mods=mods(s->keys),
        .time_ms=(uint32_t)GetMessageTime(),.t0_ns=stamp()};
    if (!press && s->repeat_key==vk) repeat_cancel(s);
    if (!press && s->hidden[vk]) { s->hidden[vk]=false; return; }
    bool composing=s->composing;
    if (!translate(s,&ev,vk,lp,press)) return;
    if (press && !composing && !modifier(vk) && s->repeat_rate) {
        s->repeat_key=vk; s->repeat_event=ev; s->repeat_lparam=lp;
        s->repeat_next=os_now_ns()+(uint64_t)s->repeat_delay*NS_MS;
        timer(s->repeat_timer,s->repeat_delay ? s->repeat_delay : 1u);
    }
    (void)push(s,ev);
}
static void unicode_event(win32_plat *s, uint32_t code, LPARAM lp)
{
    if ((uintptr_t)lp&(UINT64_C(1)<<30)) return;
    (void)GetKeyboardState(s->keys);
    plat_event ev={.kind=PLAT_EV_KEY,.press=true,.keysym=keysym(code),
        .mods=mods(s->keys),.time_ms=(uint32_t)GetMessageTime(),.t0_ns=stamp()};
    text(&ev,code);
    if (ev.utf8_len) (void)push(s,ev);
}
static void pointer(win32_plat *s, UINT msg, WPARAM wp, LPARAM lp)
{
    (void)GetKeyboardState(s->keys);
    plat_event ev={.mods=mods(s->keys),.x=GET_X_LPARAM(lp),.y=GET_Y_LPARAM(lp),
        .time_ms=(uint32_t)GetMessageTime(),.t0_ns=stamp()};
    if (msg==WM_MOUSEWHEEL || msg==WM_MOUSEHWHEEL) {
        POINT point={ev.x,ev.y}; (void)ScreenToClient(s->hwnd,&point); ev.x=point.x; ev.y=point.y;
        ev.kind=PLAT_EV_WHEEL; int32_t delta=GET_WHEEL_DELTA_WPARAM(wp);
        ev.smooth=delta%WHEEL_DELTA!=0;
        int32_t *rest=msg==WM_MOUSEWHEEL ? &s->wheel_y : &s->wheel_x;
        /* (P) contract units = delta * 256 / WHEEL_DELTA (120). Retain
         * the division remainder, so fractional notches never lose distance. */
        int32_t scaled=delta*PLAT_WHEEL_UNIT+*rest, units=scaled/WHEEL_DELTA;
        *rest=scaled%WHEEL_DELTA;
        if (msg==WM_MOUSEWHEEL) ev.dy=-units; else ev.dx=units;
    } else if (msg==WM_MOUSEMOVE) ev.kind=PLAT_EV_MOTION;
    else {
        ev.kind=PLAT_EV_BUTTON;
        ev.press=msg==WM_LBUTTONDOWN || msg==WM_MBUTTONDOWN || msg==WM_RBUTTONDOWN || msg==WM_XBUTTONDOWN;
        switch (msg) {
        case WM_LBUTTONDOWN: case WM_LBUTTONUP: ev.code=1; break;
        case WM_MBUTTONDOWN: case WM_MBUTTONUP: ev.code=2; break;
        case WM_RBUTTONDOWN: case WM_RBUTTONUP: ev.code=3; break;
        default: ev.code=GET_XBUTTON_WPARAM(wp)==XBUTTON1 ? 8u : 9u; break;
        }
        uint32_t bit=1u<<(ev.code-1u);
        if (ev.press) { s->buttons|=bit; if (GetCapture()!=s->hwnd) (void)SetCapture(s->hwnd); }
        else { s->buttons&=~bit; if (!s->buttons && GetCapture()==s->hwnd) (void)ReleaseCapture(); }
    }
    ev.buttons=s->buttons; (void)push(s,ev);
}
static void resize(win32_plat *s)
{
    RECT rect;
    if (!GetClientRect(s->hwnd,&rect)) return;
    s->owner->width=(uint32_t)(rect.right-rect.left); s->owner->height=(uint32_t)(rect.bottom-rect.top);
    (void)push(s,(plat_event){.kind=PLAT_EV_RESIZE,.w=s->owner->width,.h=s->owner->height});
}
static LRESULT CALLBACK procedure(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    win32_plat *s=(win32_plat *)GetWindowLongPtrW(hwnd,GWLP_USERDATA);
    if (msg==WM_NCCREATE) {
        s=((CREATESTRUCTW *)lp)->lpCreateParams; s->hwnd=hwnd;
        (void)SetWindowLongPtrW(hwnd,GWLP_USERDATA,(LONG_PTR)s);
    }
    if (!s) return DefWindowProcW(hwnd,msg,wp,lp);
    switch (msg) {
    case WM_KEYDOWN: case WM_SYSKEYDOWN: case WM_KEYUP: case WM_SYSKEYUP:
        key_event(s,msg,wp,lp); return 0;
    case WM_CHAR: case WM_SYSCHAR:
        if (wp>=0xd800 && wp<=0xdbff) { s->surrogate=(WCHAR)wp; return 0; }
        if (wp>=0xdc00 && wp<=0xdfff && s->surrogate) {
            uint32_t cp=0x10000u+((uint32_t)s->surrogate-0xd800u)*0x400u+(uint32_t)wp-0xdc00u;
            s->surrogate=0; unicode_event(s,cp,lp);
        } else { s->surrogate=0; unicode_event(s,(uint32_t)wp,lp); }
        return 0;
    case WM_UNICHAR:
        if (wp==UNICODE_NOCHAR) return TRUE;
        unicode_event(s,(uint32_t)wp,lp); return 0;
    case WM_DEADCHAR: case WM_SYSDEADCHAR: return 0; /* ToUnicodeEx owns composition. */
    case WM_INPUTLANGCHANGE:
        repeat_cancel(s); clear_compose(s); memset(s->hidden,0,sizeof s->hidden);
        (void)push(s,(plat_event){.kind=PLAT_EV_KEYMAP}); return 0;
    case WM_MOUSEMOVE: case WM_MOUSEWHEEL: case WM_MOUSEHWHEEL:
    case WM_LBUTTONDOWN: case WM_MBUTTONDOWN: case WM_RBUTTONDOWN: case WM_XBUTTONDOWN:
    case WM_LBUTTONUP: case WM_MBUTTONUP: case WM_RBUTTONUP: case WM_XBUTTONUP:
        pointer(s,msg,wp,lp); return msg==WM_XBUTTONDOWN || msg==WM_XBUTTONUP ? TRUE : 0;
    case WM_CAPTURECHANGED: s->buttons=0; return 0;
    case WM_SETFOCUS: case WM_KILLFOCUS:
        s->owner->focused=msg==WM_SETFOCUS; repeat_cancel(s); clear_compose(s);
        memset(s->hidden,0,sizeof s->hidden); plat_set_blink(s->owner,s->blink_ms);
        if (msg==WM_KILLFOCUS && GetCapture()==hwnd) (void)ReleaseCapture();
        (void)push(s,(plat_event){.kind=PLAT_EV_FOCUS,.focused=s->owner->focused}); return 0;
    case WM_CLOSE: (void)push(s,(plat_event){.kind=PLAT_EV_CLOSE}); return 0;
    case WM_SIZE: resize(s); return 0;
    case WM_DPICHANGED: {
        s->dpi=HIWORD(wp); RECT *r=(RECT *)lp;
        uint32_t old_w=s->owner->width,old_h=s->owner->height;
        if (r) (void)SetWindowPos(hwnd,NULL,r->left,r->top,r->right-r->left,r->bottom-r->top,SWP_NOZORDER|SWP_NOACTIVATE);
        /* WM_SIZE emits RESIZE for changed dimensions. A DPI-only change
         * still needs one RESIZE to invalidate the editor's geometry. */
        if (old_w==s->owner->width && old_h==s->owner->height) resize(s);
        return 0;
    }
    case WM_PAINT: {
        PAINTSTRUCT paint; (void)BeginPaint(hwnd,&paint); (void)EndPaint(hwnd,&paint);
        (void)push(s,(plat_event){.kind=PLAT_EV_EXPOSE,.w=s->owner->width,.h=s->owner->height}); return 0;
    }
    case WM_ERASEBKGND: return 1;
    case WM_DESTROYCLIPBOARD:
        if (s->clip_owned) {
            s->clip_owned=false;
            (void)push(s,(plat_event){.kind=PLAT_EV_CLIPBOARD,.code=2,.clip_which=PLAT_CLIP_CLIPBOARD});
        }
        return 0;
    case WM_NCDESTROY: (void)SetWindowLongPtrW(hwnd,GWLP_USERDATA,0); s->hwnd=NULL; break;
    default: break;
    }
    return DefWindowProcW(hwnd,msg,wp,lp);
}

plat_backend plat_backend_select(const char *display, const char *wayland_display)
{ (void)display; (void)wayland_display; return PLAT_BACKEND_WIN32; }
void *plat_native_win32(const plat *p)
{ win32_plat *s=win32_private(p); return s ? s->hwnd : NULL; }
plat_x11_native plat_native_x11(const plat *p)
{ (void)p; return (plat_x11_native){0}; }
bool plat_present_available(const plat *p)
{ return win32_private(p)!=NULL; }
int plat_set_present_events(plat *p, bool enabled)
{
    win32_plat *s=win32_private(p); if (!s) return PLAT_ERR_FAIL;
    AcquireSRWLockExclusive(&s->present_lock); s->present_enabled=enabled;
    if (!enabled) { s->present_head=s->present_tail; (void)ResetEvent(s->present_wake); }
    ReleaseSRWLockExclusive(&s->present_lock); return PLAT_OK;
}
int win32_plat_present_complete(plat *p, uint32_t serial, uint64_t ust, uint64_t msc)
{
    win32_plat *s=win32_private(p); if (!s) return PLAT_ERR_FAIL;
    AcquireSRWLockExclusive(&s->present_lock);
    int rc=PLAT_OK;
    if (s->present_enabled) {
        if (s->present_tail-s->present_head==WIN32_EVENT_CAP) rc=PLAT_ERR_FAIL;
        else {
            s->presents[s->present_tail++ % WIN32_EVENT_CAP]=(win32_present){serial,ust,msc};
            (void)SetEvent(s->present_wake);
        }
    }
    ReleaseSRWLockExclusive(&s->present_lock); return rc;
}
static WCHAR *wide_utf8(const void *bytes, size_t len)
{
    if (len>INT_MAX || (!bytes && len)) return NULL;
    int n=len ? MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,bytes,(int)len,NULL,0) : 0;
    if (len && !n) return NULL;
    WCHAR *wide=malloc(((size_t)n+1u)*sizeof *wide); if (!wide) return NULL;
    if (n && !MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,bytes,(int)len,wide,n)) { free(wide); return NULL; }
    wide[n]=0; return wide;
}
static bool awareness(void)
{
    return SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2) ||
        AreDpiAwarenessContextsEqual(GetThreadDpiAwarenessContext(),DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
}
int plat_init_backend(plat *p, const plat_config *cfg, plat_backend backend)
{
    if (!p || !cfg || backend!=PLAT_BACKEND_WIN32) return PLAT_ERR_FAIL;
    memset(p,0,sizeof *p); p->backend=backend;
    if (cfg->headless) return PLAT_ERR_NO_DISPLAY;
    if (!cfg->width || !cfg->height || cfg->width>INT_MAX || cfg->height>INT_MAX) return PLAT_ERR_FAIL;
    /* ERROR_ACCESS_DENIED means the host/manifest already fixed awareness.
     * Accept it only if it really is per-monitor v2. */
    if (!awareness()) return PLAT_ERR_FAIL;
    win32_plat *s=calloc(1,sizeof *s); if (!s) return PLAT_ERR_FAIL;
    p->backend_state=s; p->width=cfg->width; p->height=cfg->height; s->owner=p;
    s->work_wake=cfg->work_wake; s->instance=GetModuleHandleW(NULL);
    s->repeat_delay=400; s->repeat_rate=30; /* (G) P2.2 shared defaults. */
    s->present_enabled=true; InitializeSRWLock(&s->present_lock);
    s->blink_timer=CreateWaitableTimerW(NULL,FALSE,NULL);
    s->repeat_timer=CreateWaitableTimerW(NULL,FALSE,NULL);
    s->present_wake=CreateEventW(NULL,TRUE,FALSE,NULL);
    if (!s->blink_timer || !s->repeat_timer || !s->present_wake) goto fail;
    (void)swprintf(s->class_name,sizeof s->class_name / sizeof s->class_name[0],L"sublimite-%p",(void *)s);
    WNDCLASSEXW cls={0}; cls.cbSize=sizeof cls; cls.lpfnWndProc=procedure; cls.hInstance=s->instance;
    cls.hCursor=LoadCursorW(NULL,MAKEINTRESOURCEW(32513)); cls.lpszClassName=s->class_name;
    if (!RegisterClassExW(&cls)) goto fail;
    WCHAR *title=wide_utf8(cfg->title ? cfg->title : "sublimite",strlen(cfg->title ? cfg->title : "sublimite"));
    if (!title) goto fail;
    s->dpi=GetDpiForSystem();
    RECT rect={0,0,(LONG)cfg->width,(LONG)cfg->height};
    if (!AdjustWindowRectExForDpi(&rect,WS_OVERLAPPEDWINDOW,FALSE,0,s->dpi)) { free(title); goto fail; }
    HWND hwnd=CreateWindowExW(0,s->class_name,title,WS_OVERLAPPEDWINDOW,CW_USEDEFAULT,CW_USEDEFAULT,
        rect.right-rect.left,rect.bottom-rect.top,NULL,NULL,s->instance,s);
    free(title); if (!hwnd) goto fail;
    s->dpi=GetDpiForWindow(hwnd); return PLAT_OK;
fail:
    plat_shutdown(p); return PLAT_ERR_FAIL;
}
int plat_init(plat *p, const plat_config *cfg)
{ return plat_init_backend(p,cfg,PLAT_BACKEND_WIN32); }
void plat_map(plat *p)
{ win32_plat *s=win32_private(p); if (s) (void)ShowWindow(s->hwnd,SW_SHOWNORMAL); }
void plat_quit(plat *p)
{ if (p) { p->quit=true; win32_plat *s=win32_private(p); if (s) (void)PostMessageW(s->hwnd,WM_NULL,0,0); } }
void plat_shutdown(plat *p)
{
    win32_plat *s=win32_private(p); if (!s) return;
    clear_compose(s);
    if (s->hwnd) (void)DestroyWindow(s->hwnd);
    if (s->class_name[0]) (void)UnregisterClassW(s->class_name,s->instance);
    if (s->blink_timer) (void)CloseHandle(s->blink_timer);
    if (s->repeat_timer) (void)CloseHandle(s->repeat_timer);
    if (s->present_wake) (void)CloseHandle(s->present_wake);
    free(s->clip_data); free(s); memset(p,0,sizeof *p);
}

int plat_clip_set(plat *p, int which, const void *utf8, size_t len)
{
    win32_plat *s=win32_private(p); if (!s) return PLAT_ERR_FAIL;
    if (which==PLAT_CLIP_PRIMARY) return PLAT_OK;
    if (which!=PLAT_CLIP_CLIPBOARD) return PLAT_ERR_FAIL;
    WCHAR *wide=wide_utf8(utf8,len); if (!wide) return PLAT_ERR_FAIL;
    size_t bytes=(wcslen(wide)+1u)*sizeof *wide;
    HGLOBAL memory=GlobalAlloc(GMEM_MOVEABLE,bytes);
    void *dest=memory ? GlobalLock(memory) : NULL;
    if (!dest) { if (memory) (void)GlobalFree(memory); free(wide); return PLAT_ERR_FAIL; }
    memcpy(dest,wide,bytes); (void)GlobalUnlock(memory); free(wide);
    if (!OpenClipboard(s->hwnd)) { (void)GlobalFree(memory); return PLAT_ERR_FAIL; }
    s->clip_owned=false;
    bool ok=EmptyClipboard() && SetClipboardData(CF_UNICODETEXT,memory)!=NULL;
    (void)CloseClipboard();
    if (!ok) { (void)GlobalFree(memory); return PLAT_ERR_FAIL; }
    s->clip_owned=true; return PLAT_OK;
}
int plat_clip_request(plat *p, int which)
{
    win32_plat *s=win32_private(p); if (!s || (which!=PLAT_CLIP_PRIMARY && which!=PLAT_CLIP_CLIPBOARD)) return PLAT_ERR_FAIL;
    if (s->tail-s->head==WIN32_EVENT_CAP) return PLAT_ERR_FAIL;
    bool ok=false; uint8_t *bytes=NULL; int n=0;
    if (which==PLAT_CLIP_CLIPBOARD && OpenClipboard(s->hwnd)) {
        HANDLE memory=GetClipboardData(CF_UNICODETEXT);
        const WCHAR *wide=memory ? GlobalLock(memory) : NULL;
        if (wide) {
            SIZE_T capacity=GlobalSize(memory)/sizeof *wide, len=0;
            while (len<capacity && wide[len]) len++;
            if (len<capacity && len<=INT_MAX) {
                n=len ? WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,wide,(int)len,NULL,0,NULL,NULL) : 0;
                if (!len || n>0) {
                    bytes=malloc((size_t)n+1u);
                    if (bytes) {
                        ok=!n || WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,wide,(int)len,(char *)bytes,n,NULL,NULL)==n;
                        bytes[n]=0;
                    }
                }
            }
            (void)GlobalUnlock(memory);
        }
        (void)CloseClipboard();
    }
    free(s->clip_data); s->clip_data=bytes; s->clip_len=ok ? (size_t)n : 0;
    (void)push(s,(plat_event){.kind=PLAT_EV_CLIPBOARD,.clip_ok=ok,.clip_which=(uint8_t)which,.code=ok ? 0u : 1u});
    return PLAT_OK;
}
const uint8_t *plat_clip_data(const plat *p, size_t *len)
{
    win32_plat *s=win32_private(p); if (len) *len=s ? s->clip_len : 0;
    return s ? s->clip_data : NULL;
}

int plat_display_size(plat_backend backend, plat_display_info *info)
{
    if (backend!=PLAT_BACKEND_WIN32 || !info || !awareness()) return PLAT_ERR_FAIL;
    /* No plat argument in this INIT API: use this thread's window if it has
     * one, otherwise the primary monitor, just as Linux uses its primary. */
    HWND hwnd=GetActiveWindow(); HMONITOR monitor=MonitorFromWindow(hwnd,MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO mi={0}; mi.cbSize=sizeof mi;
    if (!monitor || !GetMonitorInfoW(monitor,&mi)) return PLAT_ERR_NO_DISPLAY;
    UINT x=0,y=0;
    if (FAILED(GetDpiForMonitor(monitor,MDT_EFFECTIVE_DPI,&x,&y)) || !x) return PLAT_ERR_FAIL;
    *info=(plat_display_info){(double)x,(uint32_t)(mi.rcWork.right-mi.rcWork.left),(uint32_t)(mi.rcWork.bottom-mi.rcWork.top)};
    return PLAT_OK;
}
uint64_t plat_display_interval_ns(const plat *p)
{
    if (!win32_private(p)) return 0;
    DWM_TIMING_INFO timing={0}; timing.cbSize=sizeof timing;
    /* (P) rational frames/second inverted, times SI ns/second. */
    if (SUCCEEDED(DwmGetCompositionTimingInfo(NULL,&timing)) && timing.rateRefresh.uiNumerator && timing.rateRefresh.uiDenominator)
        return NS_S*timing.rateRefresh.uiDenominator/timing.rateRefresh.uiNumerator;
    return 0;
}
static bool drain(win32_plat *s, const plat_callbacks *cb, uint64_t end)
{
    /* (G) same bounded message slice as X11; callbacks always precede the
     * next raw message, so focus/close cannot overtake earlier input. */
    for (unsigned i=0;i<64 && !s->owner->quit;i++) {
        plat_event ev;
        if (plat_poll_event(s->owner,&ev)) {
            if (cb->on_event) cb->on_event(cb->ud,&ev);
        } else {
            MSG message;
            if (!PeekMessageW(&message,NULL,0,0,PM_REMOVE)) return false;
            if (message.message==WM_QUIT) { plat_quit(s->owner); return false; }
            /* ToUnicodeEx translates key messages exactly once. Calling
             * TranslateMessage here would mutate dead-key state again and
             * queue duplicate WM_CHAR text. Unicode injection is separate. */
            (void)DispatchMessageW(&message);
        }
        if (end && os_now_ns()>=end) return true;
    }
    return true;
}
static void present_drain(win32_plat *s, const plat_callbacks *cb)
{
    for (unsigned i=0;i<WIN32_EVENT_CAP && !s->owner->quit;i++) {
        AcquireSRWLockExclusive(&s->present_lock);
        if (s->present_head==s->present_tail) {
            (void)ResetEvent(s->present_wake); ReleaseSRWLockExclusive(&s->present_lock); return;
        }
        win32_present record=s->presents[s->present_head++ % WIN32_EVENT_CAP];
        ReleaseSRWLockExclusive(&s->present_lock);
        if (cb->on_present_complete) cb->on_present_complete(cb->ud,record.serial,record.ust,record.msc);
    }
}
static void repeat_poll(win32_plat *s, const plat_callbacks *cb)
{
    if (!s->repeat_key || !s->repeat_rate) return;
    uint64_t now=os_now_ns();
    uint64_t step=NS_S/s->repeat_rate; if (!step) step=1;
    if (now<s->repeat_next) return;
    if ((now-s->repeat_next)/step>2u) s->repeat_next=now; /* (G) P2.2 catch-up cap. */
    for (unsigned n=0;n<3 && !s->owner->quit && s->repeat_key && now>=s->repeat_next;n++) {
        plat_event ev=s->repeat_event; ev.repeat=true; ev.t0_ns=now;
        (void)GetKeyboardState(s->keys); ev.mods=mods(s->keys); ev.utf8_len=0;
        (void)translate(s,&ev,s->repeat_key,s->repeat_lparam,false);
        ev.time_ms=GetTickCount(); s->repeat_next+=step;
        if (cb->on_event) cb->on_event(cb->ud,&ev);
    }
    if (s->repeat_key) {
        uint64_t left=s->repeat_next>now ? s->repeat_next-now : 1;
        LARGE_INTEGER due; due.QuadPart=-(LONGLONG)((left+99u)/100u); /* (P) waitable timer: 100 ns units. */
        (void)SetWaitableTimer(s->repeat_timer,&due,0,NULL,NULL,FALSE);
    }
}
int plat_run_for(plat *p, const plat_callbacks *cb, int timeout_ms)
{
    win32_plat *s=win32_private(p); if (!s || !cb) return PLAT_ERR_FAIL;
    uint64_t end=timeout_ms>0 ? os_now_ns()+(uint64_t)timeout_ms*NS_MS : 0;
    bool first=true;
    while (!p->quit) {
        bool buffered=drain(s,cb,end);
        if (p->quit) break;
        present_drain(s,cb);
        if (p->quit) break;
        if (timeout_ms==0 && !first) break;
        if (timeout_ms>0 && os_now_ns()>=end) break;
        if (!buffered) repeat_poll(s,cb);
        if (p->quit) break;
        HANDLE handles[4]; DWORD count=0,work_index=MAXDWORD;
        handles[count++]=s->blink_timer; handles[count++]=s->repeat_timer; handles[count++]=s->present_wake;
        if (s->work_wake && os_wake_handle(s->work_wake) && cb->on_work) {
            work_index=count; handles[count++]=os_wake_handle(s->work_wake);
        }
        DWORD wait=INFINITE;
        if (timeout_ms==0 || buffered) wait=0;
        else if (timeout_ms>0) {
            uint64_t now=os_now_ns();
            wait=now>=end ? 0 : (DWORD)((end-now+NS_MS-1u)/NS_MS);
        }
        DWORD result=MsgWaitForMultipleObjectsEx(count,handles,wait,QS_ALLINPUT,MWMO_INPUTAVAILABLE);
        first=false;
        if (result==WAIT_FAILED) return PLAT_ERR_FAIL;
        if (result==WAIT_TIMEOUT) { if (timeout_ms==0) break; continue; }
        p->iterations++;
        /* Input is drained on the next turn before repeat is generated. */
        if (result==WAIT_OBJECT_0 && p->focused && s->blink_ms) {
            timer(s->blink_timer,s->blink_ms);
            if (cb->on_blink) cb->on_blink(cb->ud);
        }
        if (!p->quit && work_index!=MAXDWORD && WaitForSingleObject(handles[work_index],0)==WAIT_OBJECT_0 && cb->on_work)
            cb->on_work(cb->ud); /* Borrowed wake: callback drains before mailbox reads. */
    }
    return PLAT_OK;
}
int plat_run(plat *p, const plat_callbacks *cb) { return plat_run_for(p,cb,-1); }
#endif
