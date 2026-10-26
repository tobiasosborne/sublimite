/* Real-window contract suite. (G) assertions are semantic, not timing budgets.
 * Fixture dimensions/delays below are (E) small desktop window and hang guards. */
#if defined(_WIN32)
#define _WIN32_WINNT 0x0A00
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "x11/plat.h"
#if defined(WIN32_PLAT_ROUND1)
#include "../../build/r2-round1/private.h"
#else
#include "win32/private.h"
#endif
#include "os/os.h"
#include "base/base.h"
#include "../../vendor/xkbcommon/xkbcommon-keysyms.h"

/* The source is included only in this test binary: later cases can supply
 * native API fixtures without adding indirection to the production object. */
static HANDLE sent_ready;
static plat *sent_platform;
static DWORD sent_thread;
static bool waited_over_event;
static DWORD test_wait(DWORD count, const HANDLE *handles, DWORD ms, DWORD mask, DWORD flags)
{
    win32_plat *s=win32_private(sent_platform);
    if(ms && GetCurrentThreadId()==sent_thread && s && s->head!=s->tail) waited_over_event=true;
    return MsgWaitForMultipleObjectsEx(count,handles,ms,mask,flags);
}
static BOOL test_peek(MSG *message, HWND hwnd, UINT low, UINT high, UINT remove)
{
    if(sent_ready && GetCurrentThreadId()==sent_thread) {
        HANDLE ready=sent_ready; sent_ready=NULL; (void)SetEvent(ready);
        /* Force the documented nonqueued dispatch at the empty-queue boundary. */
        while(!(HIWORD(GetQueueStatus(QS_SENDMESSAGE))&QS_SENDMESSAGE)) SwitchToThread();
        return PeekMessageW(message,hwnd,WM_APP+100u,WM_APP+100u,remove);
    }
    return PeekMessageW(message,hwnd,low,high,remove);
}
static unsigned multiscalar_fixture;
static int test_to_unicode(UINT vk, UINT scan, const BYTE *keys, WCHAR *out, int cap, UINT flags, HKL layout)
{
    if(multiscalar_fixture && vk=='A' && cap>=8) {
        if(multiscalar_fixture==1) { out[0]='a'; out[1]='b'; return 2; }
        for(unsigned i=0;i<4;i++) { out[2*i]=0xd83d; out[2*i+1]=0xde42; } return 8;
    }
    return ToUnicodeEx(vk,scan,keys,out,cap,flags,layout);
}
static BOOL test_translate_message(const MSG *m)
{
    if(multiscalar_fixture && m->message==WM_KEYDOWN && m->wParam=='A') {
        if(multiscalar_fixture==1) {
            (void)PostMessageW(m->hwnd,WM_CHAR,'a',m->lParam);
            (void)PostMessageW(m->hwnd,WM_CHAR,'b',m->lParam);
        } else for(unsigned i=0;i<4;i++) {
            (void)PostMessageW(m->hwnd,WM_CHAR,0xd83d,m->lParam);
            (void)PostMessageW(m->hwnd,WM_CHAR,0xde42,m->lParam);
        }
        return TRUE;
    }
    return TranslateMessage(m);
}
static DWORD clip_ui;
static volatile LONG clip_ui_reads, clip_open_failures;
static HANDLE test_clip_get(UINT format)
{
    if(clip_ui && GetCurrentThreadId()==clip_ui) (void)InterlockedIncrement(&clip_ui_reads);
    return GetClipboardData(format);
}
static BOOL test_clip_open(HWND hwnd)
{
    if(InterlockedCompareExchange(&clip_open_failures,0,0)>0) {
        (void)InterlockedDecrement(&clip_open_failures); SetLastError(ERROR_ACCESS_DENIED); return FALSE;
    }
    return OpenClipboard(hwnd);
}
#define GetClipboardData test_clip_get
#define OpenClipboard test_clip_open
static bool display_fixture;
static unsigned display_output;
static HMONITOR test_monitor(HWND hwnd, DWORD flags)
{
    if(display_fixture) return (HMONITOR)(uintptr_t)(display_output+1u);
    return MonitorFromWindow(hwnd,flags);
}
static BOOL test_monitor_info(HMONITOR monitor, LPMONITORINFO info)
{
    if(display_fixture) {
        MONITORINFOEXW *ex=(MONITORINFOEXW *)info;
        (void)swprintf(ex->szDevice,CCHDEVICENAME,L"DISPLAY%u",(unsigned)(uintptr_t)monitor);
        return TRUE;
    }
    return GetMonitorInfoW(monitor,info);
}
static LONG test_display_sizes(UINT32 flags, UINT32 *paths, UINT32 *modes)
{
    if(display_fixture) { (void)flags; *paths=2; *modes=0; return ERROR_SUCCESS; }
    return GetDisplayConfigBufferSizes(flags,paths,modes);
}
static LONG test_display_query(UINT32 flags, UINT32 *np, DISPLAYCONFIG_PATH_INFO *paths,
                               UINT32 *nm, DISPLAYCONFIG_MODE_INFO *modes, DISPLAYCONFIG_TOPOLOGY_ID *topology)
{
    if(display_fixture) {
        (void)flags; (void)nm; (void)modes; (void)topology; *np=2;
        memset(paths,0,2u*sizeof *paths);
        for(UINT32 i=0;i<2;i++) {
            paths[i].sourceInfo.id=i;
            paths[i].targetInfo.refreshRate=(DISPLAYCONFIG_RATIONAL){i ? 144u : 60u,1};
        }
        return ERROR_SUCCESS;
    }
    return QueryDisplayConfig(flags,np,paths,nm,modes,topology);
}
static LONG test_display_name(DISPLAYCONFIG_DEVICE_INFO_HEADER *header)
{
    if(display_fixture) {
        DISPLAYCONFIG_SOURCE_DEVICE_NAME *name=(DISPLAYCONFIG_SOURCE_DEVICE_NAME *)header;
        (void)swprintf(name->viewGdiDeviceName,CCHDEVICENAME,L"DISPLAY%u",header->id+1u); return ERROR_SUCCESS;
    }
    return DisplayConfigGetDeviceInfo(header);
}
#define MonitorFromWindow test_monitor
#define GetMonitorInfoW test_monitor_info
#define GetDisplayConfigBufferSizes test_display_sizes
#define QueryDisplayConfig test_display_query
#define DisplayConfigGetDeviceInfo test_display_name
#define ToUnicodeEx test_to_unicode
#define TranslateMessage test_translate_message
#define PeekMessageW test_peek
#define MsgWaitForMultipleObjectsEx test_wait
#if defined(WIN32_PLAT_ROUND1)
#include "../../build/r2-round1/win32_plat.c"
#else
#include "../../src/win32/win32_plat.c"
#include "../../src/win32/win32_clip_win.c"
#endif
#undef PeekMessageW
#undef MsgWaitForMultipleObjectsEx
#undef ToUnicodeEx
#undef TranslateMessage
#undef GetClipboardData
#undef OpenClipboard
#undef MonitorFromWindow
#undef GetMonitorInfoW
#undef GetDisplayConfigBufferSizes
#undef QueryDisplayConfig
#undef DisplayConfigGetDeviceInfo

typedef struct fixture {
    plat p;
    os_wake wake;
    plat_event events[512]; /* (G) twice the contract ring, detects burst loss. */
    size_t count;
    unsigned work, blink, presents;
    bool quit_first;
    HANDLE close_seen;
    DWORD ui_thread;
    uint32_t serial;
    uint64_t ust, msc;
    unsigned clip_results;
    uint8_t clip_bytes[4][32];
    size_t clip_sizes[4];
    bool request_in_clip;
    HANDLE release_on_work;
} fixture;
static HANDLE modal_entered, modal_serviced;
static WNDPROC modal_original;
static bool in_native_modal;
static fixture *modal_fixture;
static int failures;
#define CHECK(x) do { if (!(x)) { fprintf(stdout,"FAIL %s:%d: %s (winerr %lu)\n",__FILE__,__LINE__,#x,GetLastError()); failures++; } } while (0)
static void event(void *ud, const plat_event *e)
{
    fixture *f = ud;
    if (f->count < sizeof f->events / sizeof f->events[0]) f->events[f->count++] = *e;
    else CHECK(false);
    if (e->kind==PLAT_EV_CLOSE && f->close_seen) (void)SetEvent(f->close_seen);
    if (in_native_modal && modal_fixture==f && e->kind==PLAT_EV_CLOSE && f->work && f->presents)
        (void)SetEvent(modal_serviced);
    if (e->kind==PLAT_EV_CLIPBOARD && e->code!=2) {
        size_t len=0; const uint8_t *bytes=plat_clip_data(&f->p,&len);
        unsigned slot=f->clip_results++;
        if(slot<4) {
            f->clip_sizes[slot]=len;
            if(bytes && len<=sizeof f->clip_bytes[slot]) memcpy(f->clip_bytes[slot],bytes,len);
        }
        if(f->request_in_clip && slot==0) {
            CHECK(plat_clip_request(&f->p,PLAT_CLIP_PRIMARY)==PLAT_OK);
            size_t after=0; const uint8_t *again=plat_clip_data(&f->p,&after);
            CHECK(after==len && again==bytes && (!len || !memcmp(again,bytes,len)));
        }
    }
    if (f->quit_first) plat_quit(&f->p);
}
static void work(void *ud) { fixture *f=ud; os_wake_drain(&f->wake); f->work++; if(f->release_on_work) (void)SetEvent(f->release_on_work); }
static void blink(void *ud) { ((fixture *)ud)->blink++; }
static void present(void *ud, uint32_t serial, uint64_t ust, uint64_t msc)
{ fixture *f=ud; CHECK(GetCurrentThreadId()==f->ui_thread); f->presents++; f->serial=serial; f->ust=ust; f->msc=msc; }
static int pump(fixture *f, int ms)
{
    plat_callbacks cb = {f,event,blink,work,present};
    return plat_run_for(&f->p,&cb,ms);
}
static HWND window(fixture *f) { return plat_native_win32(&f->p); }
static void verdict(const char *name, int before)
{ printf("win32_plat: %s: %s\n",name,failures==before?"PASS":"FAIL"); }
static size_t kind(fixture *f, plat_ev_kind k)
{ size_t n=0; for(size_t i=0;i<f->count;i++) if(f->events[i].kind==k)n++; return n; }
static plat_event *last(fixture *f, plat_ev_kind k)
{ for(size_t i=f->count;i>0;i--) if(f->events[i-1].kind==k) return &f->events[i-1]; return NULL; }
static void post(fixture *f, UINT msg, WPARAM wp, LPARAM lp)
{ CHECK(PostMessageW(window(f),msg,wp,lp)); CHECK(pump(f,0)==PLAT_OK); }
static void state(BYTE shift, BYTE ctrl, BYTE alt)
{
    BYTE keys[256]={0}; keys[VK_SHIFT]=shift; keys[VK_CONTROL]=ctrl; keys[VK_MENU]=alt;
    keys[VK_RMENU]=alt; CHECK(SetKeyboardState(keys));
}
static void key(fixture *f, UINT vk, bool down, bool native_repeat)
{
    UINT scan=MapVirtualKeyExW(vk,MAPVK_VK_TO_VSC,GetKeyboardLayout(0));
    LPARAM bits=(LPARAM)(1u | (scan<<16));
    if(native_repeat) bits |= (LPARAM)(UINT64_C(1)<<30);
    if(!down) bits |= (LPARAM)(UINT64_C(3)<<30);
    post(f,down?WM_KEYDOWN:WM_KEYUP,vk,bits);
}
static void keyboard(fixture *f)
{
    int before=failures; HKL original=GetKeyboardLayout(0);
    HKL us=LoadKeyboardLayoutW(L"00000409",KLF_NOTELLSHELL);
    CHECK(us!=NULL); CHECK(ActivateKeyboardLayout(us,0)!=NULL);
    post(f,WM_INPUTLANGCHANGE,0,(LPARAM)us);
    state(0,0,0); f->count=0; key(f,'A',true,false);
    plat_event *e=last(f,PLAT_EV_KEY);
    CHECK(e && e->press && e->keysym==XKB_KEY_a && e->utf8_len==1 && e->utf8[0]=='a');
    size_t n=kind(f,PLAT_EV_KEY); key(f,'A',true,true); CHECK(kind(f,PLAT_EV_KEY)==n);
    key(f,'A',false,false); e=last(f,PLAT_EV_KEY); CHECK(e && !e->press && !e->utf8_len);
    state(0x80,0x80,0); key(f,'A',true,false); e=last(f,PLAT_EV_KEY);
    CHECK(e && e->keysym==XKB_KEY_A && e->mods==(PLAT_MOD_SHIFT|PLAT_MOD_CTRL) && !e->utf8_len);
    key(f,'A',false,false); state(0,0,0); key(f,VK_RETURN,true,false);
    e=last(f,PLAT_EV_KEY); CHECK(e && e->keysym==XKB_KEY_Return && !e->utf8_len);
    key(f,VK_RETURN,false,false);
    /* A generated WM_CHAR must not duplicate the preceding translated key. */
    n=kind(f,PLAT_EV_KEY); post(f,WM_CHAR,'\r',1); CHECK(kind(f,PLAT_EV_KEY)==n);
    post(f,WM_UNICHAR,0x1f642,1); e=last(f,PLAT_EV_KEY);
    CHECK(e && e->keysym==0x0101f642u && e->utf8_len==4);
    CHECK(SendMessageW(window(f),WM_UNICHAR,UNICODE_NOCHAR,0)==TRUE);
    /* WM_CHAR accepts UTF-16 input injected without a preceding key message. */
    post(f,WM_CHAR,0xd83d,1); post(f,WM_CHAR,0xde42,1); e=last(f,PLAT_EV_KEY);
    CHECK(e && e->keysym==0x0101f642u && e->utf8_len==4);
    BYTE numkeys[256]={0}; numkeys[VK_NUMLOCK]=1; CHECK(SetKeyboardState(numkeys));
    key(f,VK_NUMPAD1,true,false); e=last(f,PLAT_EV_KEY);
    CHECK(e && e->keysym==XKB_KEY_KP_1 && e->utf8_len==1 && e->utf8[0]=='1');
    key(f,VK_NUMPAD1,false,false); state(0,0,0);
    verdict("ASCII/modifiers/native-repeat suppression/Unicode/keypad",before);

    before=failures; HKL layouts[256]; int total=GetKeyboardLayoutList(256,layouts); HKL german=NULL;
    printf("win32_plat: installed layouts:");
    for(int i=0;i<total;i++) { printf(" %p",(void *)layouts[i]); if(LOWORD((ULONG_PTR)layouts[i])==0x0407) german=layouts[i]; }
    putchar('\n');
    if(!german) printf("win32_plat: German AltGr/dead keys: SKIP (German layout not installed)\n");
    else {
        CHECK(ActivateKeyboardLayout(german,0)!=NULL); post(f,WM_INPUTLANGCHANGE,0,(LPARAM)german);
        state(0,0x80,0x80); key(f,'Q',true,false); e=last(f,PLAT_EV_KEY);
        CHECK(e && e->keysym==XKB_KEY_at && e->utf8_len==1 && e->utf8[0]=='@');
        CHECK(e && (e->mods&PLAT_MOD_ALTGR) && !(e->mods&(PLAT_MOD_CTRL|PLAT_MOD_ALT)));
        key(f,'Q',false,false); state(0,0,0);
        /* German acute is the OEM_PLUS key, followed by E -> U+00E9. */
        SHORT acute=VkKeyScanExW((WCHAR)0x00b4,german); CHECK(acute!=-1);
        plat_set_repeat(&f->p,10,100);
        state((acute&0x100)?0x80:0,0,0); n=kind(f,PLAT_EV_KEY);
        key(f,(UINT)(acute&0xff),true,false); CHECK(kind(f,PLAT_EV_KEY)==n);
        key(f,(UINT)(acute&0xff),false,false); state(0,0,0); key(f,'E',true,false);
        e=last(f,PLAT_EV_KEY); CHECK(e && e->keysym==XKB_KEY_eacute && e->utf8_len==2 && e->utf8[0]==0xc3 && e->utf8[1]==0xa9);
        n=kind(f,PLAT_EV_KEY); CHECK(pump(f,25)==PLAT_OK); CHECK(kind(f,PLAT_EV_KEY)==n);
        key(f,'E',false,false); plat_set_repeat(&f->p,0,0);
        /* An invalid acute+Q composition consumes both press and release. */
        state((acute&0x100)?0x80:0,0,0); n=kind(f,PLAT_EV_KEY);
        key(f,(UINT)(acute&0xff),true,false); key(f,(UINT)(acute&0xff),false,false);
        state(0,0,0); key(f,'Q',true,false); key(f,'Q',false,false);
        CHECK(kind(f,PLAT_EV_KEY)==n);
        verdict("German AltGr/dead keys",before);
    }
    state(0,0,0); CHECK(ActivateKeyboardLayout(original,0)!=NULL);
    post(f,WM_INPUTLANGCHANGE,0,(LPARAM)original);
}
static void pointer_test(fixture *f)
{
    int before=failures; f->count=0;
    post(f,WM_MOUSEMOVE,0,MAKELPARAM(7,9)); plat_event *e=last(f,PLAT_EV_MOTION);
    CHECK(e && e->x==7 && e->y==9);
    post(f,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(7,9)); e=last(f,PLAT_EV_BUTTON);
    CHECK(e && e->code==1 && e->press && e->buttons==1 && GetCapture()==window(f));
    post(f,WM_RBUTTONDOWN,MK_LBUTTON|MK_RBUTTON,MAKELPARAM(7,9));
    post(f,WM_LBUTTONUP,MK_RBUTTON,0); CHECK(GetCapture()==window(f));
    post(f,WM_RBUTTONUP,0,0); CHECK(GetCapture()!=window(f));
    post(f,WM_XBUTTONDOWN,MAKEWPARAM(0,XBUTTON1),0); e=last(f,PLAT_EV_BUTTON); CHECK(e && e->code==8);
    post(f,WM_XBUTTONUP,MAKEWPARAM(0,XBUTTON1),0);
    post(f,WM_MOUSEWHEEL,MAKEWPARAM(0,WHEEL_DELTA),0); e=last(f,PLAT_EV_WHEEL);
    CHECK(e && e->dy==-PLAT_WHEEL_UNIT && !e->smooth);
    int sum=0;
    /* (P) three 40/120-notch deltas sum to one contract notch. */
    for(int i=0;i<3;i++) { post(f,WM_MOUSEWHEEL,MAKEWPARAM(0,40),0); e=last(f,PLAT_EV_WHEEL); CHECK(e && e->smooth); if(e)sum+=e->dy; }
    CHECK(sum==-PLAT_WHEEL_UNIT);
    post(f,WM_MOUSEHWHEEL,MAKEWPARAM(0,WHEEL_DELTA),0); e=last(f,PLAT_EV_WHEEL); CHECK(e && e->dx==PLAT_WHEEL_UNIT && !e->dy);
    verdict("pointer/capture/notches/fractional wheel",before);
}
static void geometry_clipboard(fixture *f)
{
    int before=failures; f->count=0;
    CHECK(SetWindowPos(window(f),NULL,0,0,360,240,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE));
    CHECK(pump(f,0)==PLAT_OK); RECT client; CHECK(GetClientRect(window(f),&client));
    CHECK(f->p.width==(uint32_t)client.right && f->p.height==(uint32_t)client.bottom);
    CHECK(kind(f,PLAT_EV_RESIZE)>0);
    RECT suggested; CHECK(GetWindowRect(window(f),&suggested)); suggested.right+=10;
    size_t n=kind(f,PLAT_EV_RESIZE);
    /* Win32 rejects posting pointer-bearing system messages (1159). */
    (void)SendMessageW(window(f),WM_DPICHANGED,MAKEWPARAM(144,144),(LPARAM)&suggested); CHECK(pump(f,0)==PLAT_OK);
    CHECK(kind(f,PLAT_EV_RESIZE)>n && win32_private(&f->p)->dpi==144);
    CHECK(InvalidateRect(window(f),NULL,FALSE)); CHECK(pump(f,0)==PLAT_OK); CHECK(kind(f,PLAT_EV_EXPOSE)>0);
    plat_display_info info; CHECK(plat_display_size(PLAT_BACKEND_WIN32,&info)==PLAT_OK);
    CHECK(info.dpi>0 && info.work_width>0 && info.work_height>0);
    printf("win32_plat: display dpi=%.1f work=%ux%u interval_ns=%llu (M)\n",info.dpi,info.work_width,info.work_height,(unsigned long long)plat_display_interval_ns(&f->p));
    verdict("resize/DPI/expose/display",before);
    before=failures; const char text[]="sublimit\xc3\xa9 \xe6\x96\x87 \xf0\x9f\x99\x82";
    CHECK(plat_clip_set(&f->p,PLAT_CLIP_CLIPBOARD,text,sizeof text-1)==PLAT_OK);
    CHECK(plat_clip_request(&f->p,PLAT_CLIP_CLIPBOARD)==PLAT_OK); CHECK(pump(f,100)==PLAT_OK);
    size_t len=0; const uint8_t *bytes=plat_clip_data(&f->p,&len);
    plat_event *e=last(f,PLAT_EV_CLIPBOARD);
    CHECK(e && e->clip_ok && len==sizeof text-1 && bytes && !memcmp(bytes,text,len));
    CHECK(plat_clip_set(&f->p,PLAT_CLIP_PRIMARY,"ignored",7)==PLAT_OK);
    CHECK(plat_clip_request(&f->p,PLAT_CLIP_PRIMARY)==PLAT_OK); CHECK(pump(f,100)==PLAT_OK);
    e=last(f,PLAT_EV_CLIPBOARD); CHECK(e && e->clip_which==PLAT_CLIP_PRIMARY && !e->clip_ok);
    CHECK(plat_clip_set(&f->p,PLAT_CLIP_CLIPBOARD,"\xff",1)==PLAT_ERR_FAIL);
    verdict("Unicode clipboard/PRIMARY no-op",before);
}
static void allocation(fixture *f)
{
    int before=failures; f->count=0; state(0,0,0);
    bool active=edit_malloc_guard_active(); edit_malloc_guard_begin();
    /* (G) exercise translation, queue delivery and pointer input together. */
    for(unsigned i=0;i<32;i++) {
        key(f,'A',true,false); key(f,'A',false,false);
        post(f,WM_MOUSEMOVE,0,MAKELPARAM(7,9));
    }
    size_t calls=edit_malloc_guard_end(); CHECK(calls==0);
    printf("win32_plat: typing allocation guard: %s (%zu calls)\n",active?"PASS":"SKIP (sanitizer owns allocator)",calls);
    verdict("input/queue allocation",before);
}
static void present_thread(void *arg)
{ fixture *f=arg; CHECK(win32_plat_present_complete(&f->p,17,23,29)==PLAT_OK); }
static void wake_thread(void *arg) { fixture *f=arg; os_sleep_ns(UINT64_C(20000000)); os_wake_signal(&f->wake); }
static void loop(fixture *f)
{
    int before=failures;
    uint64_t start=os_now_ns(); CHECK(pump(f,20)==PLAT_OK);
    CHECK(os_now_ns()>=start+UINT64_C(20000000));
    os_thread thread; CHECK(os_thread_start(&thread,wake_thread,f)==OS_OK);
    CHECK(pump(f,80)==PLAT_OK); CHECK(os_thread_join(&thread)==OS_OK); CHECK(f->work==1);
    post(f,WM_SETFOCUS,0,0); plat_set_blink(&f->p,10); CHECK(pump(f,35)==PLAT_OK); CHECK(f->blink>0);
    post(f,WM_KILLFOCUS,0,0); unsigned n=f->blink; CHECK(pump(f,25)==PLAT_OK); CHECK(f->blink==n);
    post(f,WM_SETFOCUS,0,0); plat_set_blink(&f->p,0);
    plat_set_repeat(&f->p,10,100); f->count=0; state(0,0,0); key(f,'A',true,false); CHECK(pump(f,45)==PLAT_OK);
    size_t repeated=0; for(size_t i=0;i<f->count;i++) if(f->events[i].repeat) { repeated++; CHECK(f->events[i].press && f->events[i].keysym==XKB_KEY_a); }
    CHECK(repeated>0);
    /* Repeat translates with current modifiers, as XKB does. */
    state(0x80,0,0); key(f,VK_SHIFT,true,false); f->count=0;
    CHECK(pump(f,25)==PLAT_OK); plat_event *shifted=last(f,PLAT_EV_KEY);
    CHECK(shifted && shifted->repeat && shifted->keysym==XKB_KEY_A && shifted->utf8_len==1 && shifted->utf8[0]=='A');
    key(f,VK_SHIFT,false,false); state(0,0,0); key(f,'A',false,false); size_t count=f->count; CHECK(pump(f,25)==PLAT_OK); CHECK(f->count==count);
    key(f,'B',true,false); post(f,WM_KILLFOCUS,0,0); count=f->count; CHECK(pump(f,25)==PLAT_OK); CHECK(f->count==count);
    plat_set_repeat(&f->p,0,0);
    CHECK(os_thread_start(&thread,present_thread,f)==OS_OK);
    CHECK(pump(f,20)==PLAT_OK); CHECK(os_thread_join(&thread)==OS_OK); CHECK(pump(f,0)==PLAT_OK);
    CHECK(f->presents==1 && f->serial==17 && f->ust==23 && f->msc==29);
    CHECK(plat_set_present_events(&f->p,false)==PLAT_OK);
    CHECK(win32_plat_present_complete(&f->p,18,24,30)==PLAT_OK); CHECK(pump(f,0)==PLAT_OK); CHECK(f->presents==1);
    verdict("bounded loop/work/blink/owned repeat/T6",before);
    before=failures; f->count=0; f->quit_first=true;
    CHECK(PostMessageW(window(f),WM_CLOSE,0,0)); CHECK(PostMessageW(window(f),WM_MOUSEMOVE,0,0));
    CHECK(pump(f,-1)==PLAT_OK); CHECK(f->count==1 && f->events[0].kind==PLAT_EV_CLOSE);
    verdict("close/order/quit callback",before);
}
static void wrong_thread(void *arg)
{
    fixture *f=arg;
    CHECK(pump(f,0)==PLAT_ERR_FAIL);
    plat_set_repeat(&f->p,7,7);
    plat_set_blink(&f->p,7);
    plat_shutdown(&f->p);
}
static void affinity_test(fixture *f)
{
    int before=failures; HWND hwnd=window(f); win32_plat *saved=win32_private(&f->p);
    WCHAR class_name[64]; memcpy(class_name,saved->class_name,sizeof class_name);
    os_thread thread; CHECK(os_thread_start(&thread,wrong_thread,f)==OS_OK);
    CHECK(os_thread_join(&thread)==OS_OK);
    CHECK(win32_private(&f->p)==saved);
    CHECK(IsWindow(hwnd) && GetWindowLongPtrW(hwnd,GWLP_USERDATA)==(LONG_PTR)saved);
    if(win32_private(&f->p)==saved) CHECK(saved->repeat_rate==0 && saved->blink_ms==0);
    else { /* Keep the red fixture from dispatching through freed userdata. */
        (void)SetWindowLongPtrW(hwnd,GWLP_USERDATA,0); (void)DestroyWindow(hwnd);
        (void)UnregisterClassW(class_name,GetModuleHandleW(NULL));
    }
    verdict("r2 HWND owner thread rejects run/configuration/shutdown",before);
}
typedef struct foreign_clip {
    HANDLE ready, rendering, release, stop, process;
    bool rescued;
} foreign_clip;
static LRESULT CALLBACK foreign_procedure(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    foreign_clip *c=(foreign_clip *)GetWindowLongPtrW(hwnd,GWLP_USERDATA);
    if(msg==WM_NCCREATE) {
        c=((CREATESTRUCTW *)lp)->lpCreateParams;
        (void)SetWindowLongPtrW(hwnd,GWLP_USERDATA,(LONG_PTR)c);
    }
    if(c && msg==WM_RENDERFORMAT && wp==CF_UNICODETEXT) {
        (void)SetEvent(c->rendering); (void)WaitForSingleObject(c->release,INFINITE);
        /* (G) same 1 MiB large-transfer fixture as the X11 clipboard suite;
         * (P) one UTF-16 ASCII unit per byte plus the terminating unit. */
        size_t len=1024u*1024u; HGLOBAL mem=GlobalAlloc(GMEM_MOVEABLE,(len+1u)*sizeof(WCHAR));
        WCHAR *wide=mem ? GlobalLock(mem) : NULL;
        if(wide) { for(size_t i=0;i<len;i++) wide[i]='a'; wide[len]=0; (void)GlobalUnlock(mem); (void)SetClipboardData(CF_UNICODETEXT,mem); }
        return 0;
    }
    return DefWindowProcW(hwnd,msg,wp,lp);
}
static int foreign_main(char **argv)
{
    foreign_clip c={0};
    c.ready=(HANDLE)(uintptr_t)strtoull(argv[2],NULL,10); c.rendering=(HANDLE)(uintptr_t)strtoull(argv[3],NULL,10);
    c.release=(HANDLE)(uintptr_t)strtoull(argv[4],NULL,10); c.stop=(HANDLE)(uintptr_t)strtoull(argv[5],NULL,10);
    WNDCLASSEXW cls={0}; cls.cbSize=sizeof cls; cls.lpfnWndProc=foreign_procedure;
    cls.hInstance=GetModuleHandleW(NULL); cls.lpszClassName=L"sublimite-test-foreign-clip";
    if(!RegisterClassExW(&cls)) return 2;
    HWND hwnd=CreateWindowExW(0,cls.lpszClassName,L"",0,0,0,0,0,HWND_MESSAGE,NULL,cls.hInstance,&c);
    if(!hwnd || !OpenClipboard(hwnd)) return 2;
    if(!strcmp(argv[6],"hold")) {
        (void)SetEvent(c.ready); (void)WaitForSingleObject(c.release,INFINITE); (void)CloseClipboard();
    } else {
        (void)EmptyClipboard(); (void)SetClipboardData(CF_UNICODETEXT,NULL); (void)CloseClipboard(); (void)SetEvent(c.ready);
        while(WaitForSingleObject(c.stop,0)!=WAIT_OBJECT_0) {
            MSG m; while(PeekMessageW(&m,NULL,0,0,PM_REMOVE)) (void)DispatchMessageW(&m);
            (void)MsgWaitForMultipleObjectsEx(1,&c.stop,INFINITE,QS_ALLINPUT,MWMO_INPUTAVAILABLE);
        }
    }
    (void)DestroyWindow(hwnd); (void)UnregisterClassW(cls.lpszClassName,cls.hInstance); return 0;
}
static void foreign_start(foreign_clip *c, bool hold)
{
    SECURITY_ATTRIBUTES sa={sizeof sa,NULL,TRUE};
    c->ready=CreateEventW(&sa,TRUE,FALSE,NULL); c->rendering=CreateEventW(&sa,TRUE,FALSE,NULL);
    c->release=CreateEventW(&sa,TRUE,FALSE,NULL); c->stop=CreateEventW(&sa,TRUE,FALSE,NULL);
    WCHAR exe[32768], command[32768]; CHECK(GetModuleFileNameW(NULL,exe,32768)>0);
    int count=swprintf(command,32768,L"\"%ls\" clip-owner %llu %llu %llu %llu %ls",exe,
        (unsigned long long)(uintptr_t)c->ready,(unsigned long long)(uintptr_t)c->rendering,
        (unsigned long long)(uintptr_t)c->release,(unsigned long long)(uintptr_t)c->stop,hold ? L"hold" : L"delay");
    CHECK(count>0); STARTUPINFOW start={0}; start.cb=sizeof start; PROCESS_INFORMATION info={0};
    CHECK(CreateProcessW(exe,command,NULL,NULL,TRUE,0,NULL,NULL,&start,&info));
    c->process=info.hProcess; if(info.hThread) (void)CloseHandle(info.hThread);
    CHECK(WaitForSingleObject(c->ready,3000)==WAIT_OBJECT_0); /* (E) child startup hang guard. */
}
static void foreign_stop(foreign_clip *c)
{
    (void)SetEvent(c->release); (void)SetEvent(c->stop);
    CHECK(WaitForSingleObject(c->process,3000)==WAIT_OBJECT_0); DWORD code=1;
    CHECK(GetExitCodeProcess(c->process,&code) && code==0);
    (void)CloseHandle(c->process); (void)CloseHandle(c->ready); (void)CloseHandle(c->rendering);
    (void)CloseHandle(c->release); (void)CloseHandle(c->stop);
}
static void foreign_guard(void *arg)
{
    foreign_clip *c=arg;
    c->rescued=WaitForSingleObject(c->release,1000)!=WAIT_OBJECT_0; /* (E) rescue, no latency gate. */
    if(c->rescued) (void)SetEvent(c->release);
}
static void clipboard_foreign_test(fixture *f)
{
    int before=failures; foreign_clip c={0}; foreign_start(&c,false); os_thread guard;
    CHECK(os_thread_start(&guard,foreign_guard,&c)==OS_OK); clip_ui=GetCurrentThreadId(); clip_ui_reads=0;
    f->release_on_work=c.release;
    CHECK(plat_clip_request(&f->p,PLAT_CLIP_CLIPBOARD)==PLAT_OK);
    os_wake_signal(&f->wake); CHECK(pump(f,250)==PLAT_OK);
    CHECK(os_thread_join(&guard)==OS_OK); CHECK(!c.rescued && clip_ui_reads==0);
    CHECK(f->work==1 && f->clip_results==1 && f->clip_sizes[0]==1024u*1024u);
    clip_ui=0; f->release_on_work=NULL; foreign_stop(&c);
    verdict("r2 foreign delayed rendering and large payload leave UI responsive",before);
}
static void clipboard_foreign_contention_test(fixture *f)
{
    int before=failures; foreign_clip c={0}; foreign_start(&c,true);
    CHECK(plat_clip_set(&f->p,PLAT_CLIP_CLIPBOARD,"foreign",7)==PLAT_OK);
    CHECK(plat_clip_request(&f->p,PLAT_CLIP_CLIPBOARD)==PLAT_OK);
    (void)SetEvent(c.release); foreign_stop(&c); CHECK(pump(f,150)==PLAT_OK);
    CHECK(f->clip_results==1 && f->clip_sizes[0]==7 && !memcmp(f->clip_bytes[0],"foreign",7));
    /* A native holder that outlasts the bounded retry policy yields exactly
     * one failed completion; shutting down another pending request is safe. */
    f->clip_results=0; foreign_clip held={0}; foreign_start(&held,true);
    CHECK(plat_clip_request(&f->p,PLAT_CLIP_CLIPBOARD)==PLAT_OK);
    CHECK(pump(f,(int)GetDoubleClickTime()+200)==PLAT_OK);
    CHECK(f->clip_results==1); plat_event *e=last(f,PLAT_EV_CLIPBOARD); CHECK(e && e->code==1 && !e->clip_ok);
    CHECK(plat_clip_set(&f->p,PLAT_CLIP_CLIPBOARD,"pending",7)==PLAT_OK);
    plat_shutdown(&f->p); foreign_stop(&held);
    verdict("r2 foreign clipboard contention, expiry and pending shutdown",before);
}
static void display_test(fixture *f)
{
    int before=failures; display_fixture=true; display_output=0;
    /* (G) fake outputs deliberately have different rationals; expected ns
     * follow (P) SI ns/second divided by each fixture's frames/second. */
    CHECK(plat_display_interval_ns(&f->p)==UINT64_C(1000000000)/60u);
    display_output=1; CHECK(plat_display_interval_ns(&f->p)==UINT64_C(1000000000)/144u);
    display_fixture=false;
    printf("win32_plat: live output interval_ns=%llu (M)\n",(unsigned long long)plat_display_interval_ns(&f->p));
    verdict("r2 refresh timing follows HWND output association",before);
}
static void capture_test(fixture *f)
{
    int before=failures; f->count=0;
    HWND thief=CreateWindowExW(WS_EX_NOACTIVATE,L"STATIC",L"capture fixture",WS_POPUP,0,0,1,1,NULL,NULL,GetModuleHandleW(NULL),NULL);
    CHECK(thief!=NULL); post(f,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(7,9));
    (void)SetCapture(thief); CHECK(pump(f,0)==PLAT_OK);
    plat_event *e=last(f,PLAT_EV_BUTTON); CHECK(e && !e->press && e->code==1 && !e->buttons);
    post(f,WM_MOUSEMOVE,0,MAKELPARAM(8,10)); e=last(f,PLAT_EV_MOTION); CHECK(e && !e->buttons);
    (void)ReleaseCapture(); (void)DestroyWindow(thief);
    f->count=0; post(f,WM_LBUTTONDOWN,MK_LBUTTON,0); post(f,WM_LBUTTONUP,0,0);
    CHECK(kind(f,PLAT_EV_BUTTON)==2);
    f->count=0; post(f,WM_RBUTTONDOWN,MK_RBUTTON,0); post(f,WM_CANCELMODE,0,0);
    e=last(f,PLAT_EV_BUTTON); CHECK(e && !e->press && e->code==3 && !e->buttons);
    verdict("r2 capture theft/cancel ends drag once",before);
}
static void clipboard_async_test(fixture *f)
{
    int before=failures; clip_ui=GetCurrentThreadId(); clip_ui_reads=0;
    CHECK(plat_clip_set(&f->p,PLAT_CLIP_CLIPBOARD,"async",5)==PLAT_OK);
    CHECK(plat_clip_request(&f->p,PLAT_CLIP_CLIPBOARD)==PLAT_OK); CHECK(pump(f,100)==PLAT_OK);
    CHECK(clip_ui_reads==0); CHECK(f->clip_results==1 && f->clip_sizes[0]==5);
    clip_ui=0; verdict("r2 clipboard foreign read stays off UI thread",before);
}
static void clipboard_contention_test(fixture *f)
{
    int before=failures; clip_open_failures=3;
    CHECK(plat_clip_set(&f->p,PLAT_CLIP_CLIPBOARD,"retry",5)==PLAT_OK);
    CHECK(plat_clip_request(&f->p,PLAT_CLIP_CLIPBOARD)==PLAT_OK); CHECK(pump(f,700)==PLAT_OK);
    CHECK(f->clip_results==1 && f->clip_sizes[0]==5 && !memcmp(f->clip_bytes[0],"retry",5));
    clip_open_failures=0; verdict("r2 clipboard contention retries copy and paste",before);
}
static void clipboard_results_test(fixture *f)
{
    int before=failures;
    CHECK(plat_clip_set(&f->p,PLAT_CLIP_CLIPBOARD,"first",5)==PLAT_OK);
    CHECK(plat_clip_request(&f->p,PLAT_CLIP_CLIPBOARD)==PLAT_OK);
    CHECK(plat_clip_request(&f->p,PLAT_CLIP_PRIMARY)==PLAT_OK); f->request_in_clip=true;
    CHECK(pump(f,150)==PLAT_OK);
    CHECK(f->clip_results==3 && f->clip_sizes[0]==5 && !memcmp(f->clip_bytes[0],"first",5));
    CHECK(f->clip_sizes[1]==0 && f->clip_sizes[2]==0); f->request_in_clip=false;
    verdict("r2 clipboard completion owns its data and callback lifetime",before);
}
static void clipboard_newlines_test(fixture *f)
{
    int before=failures;
    const char input[]="a\nb\r\nc\n\xf0\x9f\x99\x82";
    CHECK(plat_clip_set(&f->p,PLAT_CLIP_CLIPBOARD,input,sizeof input-1)==PLAT_OK);
    CHECK(pump(f,100)==PLAT_OK); CHECK(OpenClipboard(window(f)));
    HGLOBAL mem=GetClipboardData(CF_UNICODETEXT); const WCHAR *wide=mem ? GlobalLock(mem) : NULL;
    CHECK(wide && !wcscmp(wide,L"a\r\nb\r\nc\r\n\xd83d\xde42"));
    if(wide) (void)GlobalUnlock(mem); (void)CloseClipboard();
    CHECK(plat_clip_request(&f->p,PLAT_CLIP_CLIPBOARD)==PLAT_OK); CHECK(pump(f,100)==PLAT_OK);
    size_t len=0; const uint8_t *bytes=plat_clip_data(&f->p,&len);
    const char expected[]="a\nb\nc\n\xf0\x9f\x99\x82";
    CHECK(bytes && len==sizeof expected-1 && !memcmp(bytes,expected,len));
    verdict("r2 CF_UNICODETEXT CRLF export and LF import",before);
}
static void clipboard_nul_test(fixture *f)
{
    int before=failures;
    CHECK(plat_clip_set(&f->p,PLAT_CLIP_CLIPBOARD,"keep",4)==PLAT_OK);
    CHECK(plat_clip_set(&f->p,PLAT_CLIP_CLIPBOARD,"a\0b",3)==PLAT_ERR_FAIL);
    CHECK(plat_clip_request(&f->p,PLAT_CLIP_CLIPBOARD)==PLAT_OK); CHECK(pump(f,100)==PLAT_OK);
    size_t len=0; const uint8_t *bytes=plat_clip_data(&f->p,&len);
    CHECK(bytes && len==4 && !memcmp(bytes,"keep",4));
    verdict("r2 embedded NUL rejected without replacing clipboard",before);
}
static void sided_state(BYTE lc, BYTE rc, BYTE la, BYTE ra)
{
    BYTE keys[256]={0}; keys[VK_LCONTROL]=lc; keys[VK_RCONTROL]=rc;
    keys[VK_LMENU]=la; keys[VK_RMENU]=ra;
    keys[VK_CONTROL]=(BYTE)(lc|rc); keys[VK_MENU]=(BYTE)(la|ra); CHECK(SetKeyboardState(keys));
}
static void altgr_test(fixture *f)
{
    int before=failures; HKL saved=GetKeyboardLayout(0);
    HKL de=LoadKeyboardLayoutW(L"00000407",KLF_NOTELLSHELL); CHECK(de!=NULL);
    CHECK(ActivateKeyboardLayout(de,0)!=NULL); post(f,WM_INPUTLANGCHANGE,0,(LPARAM)de);
    sided_state(0x80,0,0,0x80); f->count=0;
    key(f,'P',true,false); plat_event *e=last(f,PLAT_EV_KEY);
    CHECK(e && (e->mods&PLAT_MOD_ALTGR) && !(e->mods&(PLAT_MOD_CTRL|PLAT_MOD_ALT)));
    key(f,'P',false,false); key(f,VK_LEFT,true,false); e=last(f,PLAT_EV_KEY);
    CHECK(e && (e->mods&PLAT_MOD_ALTGR) && !(e->mods&(PLAT_MOD_CTRL|PLAT_MOD_ALT)));
    post(f,WM_MOUSEMOVE,0,0); e=last(f,PLAT_EV_MOTION); CHECK(e && (e->mods&PLAT_MOD_ALTGR));
    sided_state(0x80,0,0x80,0); key(f,'Q',true,false); e=last(f,PLAT_EV_KEY);
    CHECK(e && e->mods==(PLAT_MOD_CTRL|PLAT_MOD_ALT) && !e->utf8_len);
    sided_state(0x80,0x80,0,0x80); key(f,'Q',true,false); e=last(f,PLAT_EV_KEY);
    CHECK(e && (e->mods&(PLAT_MOD_ALTGR|PLAT_MOD_CTRL))==(PLAT_MOD_ALTGR|PLAT_MOD_CTRL) && !e->utf8_len);
    sided_state(0x80,0,0,0); key(f,VK_CONTROL,true,false);
    sided_state(0x80,0,0,0x80); key(f,'Q',true,false); e=last(f,PLAT_EV_KEY);
    CHECK(e && (e->mods&(PLAT_MOD_ALTGR|PLAT_MOD_CTRL))==(PLAT_MOD_ALTGR|PLAT_MOD_CTRL) && !e->utf8_len);
    sided_state(0,0,0,0x80); key(f,VK_CONTROL,false,false);
    HKL us=LoadKeyboardLayoutW(L"00000409",KLF_NOTELLSHELL); CHECK(ActivateKeyboardLayout(us,0)!=NULL);
    post(f,WM_INPUTLANGCHANGE,0,(LPARAM)us); sided_state(0,0,0,0x80); key(f,'P',true,false);
    e=last(f,PLAT_EV_KEY); CHECK(e && e->mods==PLAT_MOD_ALT && !e->utf8_len);
    state(0,0,0); CHECK(ActivateKeyboardLayout(saved,0)!=NULL); post(f,WM_INPUTLANGCHANGE,0,(LPARAM)saved);
    verdict("r2 AltGr modifier state, navigation/pointer, explicit Ctrl+Alt and real Ctrl",before);
}
static void ownership_test(fixture *f)
{
    int before=failures; state(0,0,0); f->count=0;
    UINT scan=MapVirtualKeyW('A',MAPVK_VK_TO_VSC); LPARAM lp=(LPARAM)(1u|(scan<<16));
    /* An external/native modal dispatcher legitimately calls TranslateMessage. */
    MSG m={.hwnd=window(f),.message=WM_KEYDOWN,.wParam='A',.lParam=lp};
    CHECK(TranslateMessage(&m)); (void)DispatchMessageW(&m); CHECK(pump(f,0)==PLAT_OK);
    size_t letters=0; for(size_t i=0;i<f->count;i++) letters+=f->events[i].utf8_len;
    CHECK(letters==1); key(f,'A',false,false);
    f->count=0; post(f,WM_CHAR,'z',(LPARAM)((UINT64_C(1)<<30)|3u));
    letters=0; for(size_t i=0;i<f->count;i++) letters+=f->events[i].utf8_len;
    CHECK(letters==3);
    post(f,WM_UNICHAR,'x',(LPARAM)((UINT64_C(1)<<30)|1u));
    plat_event *e=last(f,PLAT_EV_KEY); CHECK(e && e->utf8_len && e->utf8[0]=='x');
    verdict("r2 single text owner and standalone repeated commits",before);
}
static void packet_test(fixture *f)
{
    int before=failures; f->count=0; state(0,0,0);
    if(GetForegroundWindow()==window(f)) {
        const WORD units[]={0x03bb,0xd83d,0xde42}; INPUT input[6]={0};
        for(unsigned i=0;i<3;i++) {
            input[2*i].type=INPUT_KEYBOARD; input[2*i].ki.wScan=units[i]; input[2*i].ki.dwFlags=KEYEVENTF_UNICODE;
            input[2*i+1]=input[2*i]; input[2*i+1].ki.dwFlags|=KEYEVENTF_KEYUP;
        }
        CHECK(SendInput(6,input,sizeof input[0])==6); CHECK(pump(f,30)==PLAT_OK);
        uint8_t bytes[8]={0}; size_t len=0;
        for(size_t i=0;i<f->count;i++) if(len+f->events[i].utf8_len<=sizeof bytes) {
            memcpy(bytes+len,f->events[i].utf8,f->events[i].utf8_len); len+=f->events[i].utf8_len;
        }
        CHECK(len==6 && !memcmp(bytes,"\xce\xbb\xf0\x9f\x99\x82",6));
    } else printf("win32_plat: KEYEVENTF_UNICODE: SKIP (not foreground)\n");
    key(f,VK_PACKET,false,false); f->count=0;
    post(f,WM_KEYDOWN,VK_PROCESSKEY,1); CHECK(kind(f,PLAT_EV_KEY)==0);
    verdict("r2 VK_PACKET native translation and IME key isolation",before);
}
static void ime_test(fixture *f)
{
    int before=failures; f->count=0; state(0,0,0);
    post(f,WM_IME_STARTCOMPOSITION,0,0); key(f,VK_PROCESSKEY,true,false);
    CHECK(kind(f,PLAT_EV_KEY)==0);
    /* The Unicode default IME window procedure forwards committed WM_IME_CHAR
     * as WM_CHAR. Preedit keys never enter the editor's chord dispatcher. */
    post(f,WM_IME_CHAR,0x6587,1); post(f,WM_IME_ENDCOMPOSITION,0,0);
    plat_event *e=last(f,PLAT_EV_KEY); CHECK(e && e->utf8_len==3 && !memcmp(e->utf8,"\xe6\x96\x87",3));
    size_t n=kind(f,PLAT_EV_KEY); post(f,WM_IME_STARTCOMPOSITION,0,0);
    post(f,WM_KEYDOWN,VK_PROCESSKEY,1); post(f,WM_IME_ENDCOMPOSITION,0,0);
    CHECK(kind(f,PLAT_EV_KEY)==n);
    printf("win32_plat: real IME engine compose/commit/cancel: SKIP (installed layouts are UK/US/German)\n");
    verdict("r2 native IME commit/default handling and preedit/cancel isolation",before);
}
static void multiscalar_test(fixture *f)
{
    int before=failures; f->count=0; state(0,0,0); multiscalar_fixture=1;
    key(f,'A',true,false); multiscalar_fixture=false;
    uint8_t bytes[PLAT_UTF8_MAX]={0}; size_t len=0;
    for(size_t i=0;i<f->count;i++) if(len+f->events[i].utf8_len<=sizeof bytes) {
        memcpy(bytes+len,f->events[i].utf8,f->events[i].utf8_len); len+=f->events[i].utf8_len;
    }
    CHECK(len==2 && !memcmp(bytes,"ab",2)); key(f,'A',false,false);
    f->count=0; multiscalar_fixture=2; plat_set_repeat(&f->p,10,100); key(f,'A',true,false);
    size_t initial=0; for(size_t i=0;i<f->count;i++) initial+=f->events[i].utf8_len;
    CHECK(initial==16); /* (P) four supplementary scalars * four UTF-8 bytes. */
    CHECK(pump(f,25)==PLAT_OK); size_t repeated=0;
    for(size_t i=0;i<f->count;i++) if(f->events[i].repeat) repeated+=f->events[i].utf8_len;
    CHECK(repeated>=16 && repeated%16==0);
    key(f,'A',false,false); plat_set_repeat(&f->p,0,0); multiscalar_fixture=0;
    verdict("r2 multiscalar keyboard output including long commits/repeat",before);
}
static void altcode_test(fixture *f)
{
    int before=failures; HKL saved=GetKeyboardLayout(0);
    HKL us=LoadKeyboardLayoutW(L"00000409",KLF_NOTELLSHELL); CHECK(ActivateKeyboardLayout(us,0)!=NULL);
    post(f,WM_INPUTLANGCHANGE,0,(LPARAM)us); f->count=0;
    sided_state(0,0,0x80,0); post(f,WM_SYSKEYDOWN,VK_MENU,(LPARAM)(1u|(0x38u<<16)));
    const UINT digits[]={VK_NUMPAD0,VK_NUMPAD6,VK_NUMPAD5};
    for(size_t i=0;i<sizeof digits/sizeof digits[0];i++) {
        UINT scan=MapVirtualKeyW(digits[i],MAPVK_VK_TO_VSC);
        post(f,WM_SYSKEYDOWN,digits[i],(LPARAM)(1u|(scan<<16)|(1u<<29)));
        post(f,WM_SYSKEYUP,digits[i],(LPARAM)((UINT64_C(3)<<30)|(scan<<16)|(1u<<29)|1u));
    }
    state(0,0,0); post(f,WM_SYSKEYUP,VK_MENU,(LPARAM)((UINT64_C(3)<<30)|(0x38u<<16)|1u));
    size_t letters=0; for(size_t i=0;i<f->count;i++) letters+=f->events[i].utf8_len;
    plat_event *e=last(f,PLAT_EV_KEY); CHECK(letters==1 && e && e->utf8_len==1 && e->utf8[0]=='A');
    CHECK(ActivateKeyboardLayout(saved,0)!=NULL); post(f,WM_INPUTLANGCHANGE,0,(LPARAM)saved);
    verdict("r2 native Alt+numpad commit",before);
}
static void system_test(fixture *f)
{
    int before=failures; f->count=0; sided_state(0,0,0x80,0);
    post(f,WM_SYSKEYDOWN,VK_F4,(LPARAM)(1u|(MapVirtualKeyW(VK_F4,MAPVK_VK_TO_VSC)<<16)|(1u<<29)));
    CHECK(kind(f,PLAT_EV_CLOSE)==1 && IsWindow(window(f)));
    state(0,0,0); verdict("r2 Alt+F4 reaches native CLOSE",before);
}
static void surrogate_test(fixture *f)
{
    int before=failures; state(0,0,0); f->count=0;
    post(f,WM_CHAR,0xd83d,1); post(f,WM_UNICHAR,'X',1); post(f,WM_CHAR,0xde42,1);
    CHECK(kind(f,PLAT_EV_KEY)==1); plat_event *e=last(f,PLAT_EV_KEY); CHECK(e && e->utf8[0]=='X');
    post(f,WM_CHAR,0xde42,1); CHECK(kind(f,PLAT_EV_KEY)==1);
    post(f,WM_CHAR,0xd83d,1); post(f,WM_KILLFOCUS,0,0); post(f,WM_CHAR,0xde42,1);
    CHECK(kind(f,PLAT_EV_KEY)==1);
    post(f,WM_UNICHAR,0x110000,1); CHECK(kind(f,PLAT_EV_KEY)==1);
    verdict("r2 UTF-16 commit isolation and invalid scalars",before);
}
static LRESULT CALLBACK modal_observer(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if(msg==WM_ENTERSIZEMOVE || msg==WM_ENTERMENULOOP) { in_native_modal=true; (void)SetEvent(modal_entered); }
    if(msg==WM_EXITSIZEMOVE || msg==WM_EXITMENULOOP) in_native_modal=false;
    return CallWindowProcW(modal_original,hwnd,msg,wp,lp);
}
static void modal_producer(void *arg)
{
    fixture *f=arg;
    if(WaitForSingleObject(modal_entered,1000)!=WAIT_OBJECT_0) {
        (void)PostMessageW(window(f),WM_KEYDOWN,VK_ESCAPE,1); return;
    }
    os_wake_signal(&f->wake);
    (void)win32_plat_present_complete(&f->p,31,37,41);
    /* (G) exceed the contract ring by one complete ring plus one event. */
    for(unsigned i=0;i<WIN32_EVENT_CAP+1u;i++)
        (void)SendMessageW(window(f),WM_SIZE,0,MAKELPARAM(320,180));
    (void)SendMessageW(window(f),WM_CLOSE,0,0);
    (void)WaitForSingleObject(modal_serviced,500); /* (E) rescue guard. */
    (void)SendMessageW(window(f),WM_CANCELMODE,0,0);
    (void)PostMessageW(window(f),WM_KEYDOWN,VK_ESCAPE,1);
    (void)PostMessageW(window(f),WM_LBUTTONUP,0,0);
}
static void modal_test_common(fixture *f, WPARAM command)
{
    int before=failures; f->count=0; f->work=0; f->presents=0;
    modal_entered=CreateEventW(NULL,TRUE,FALSE,NULL);
    modal_serviced=CreateEventW(NULL,TRUE,FALSE,NULL); modal_fixture=f;
    modal_original=(WNDPROC)SetWindowLongPtrW(window(f),GWLP_WNDPROC,(LONG_PTR)modal_observer);
    os_thread thread; CHECK(os_thread_start(&thread,modal_producer,f)==OS_OK);
    if(command) CHECK(PostMessageW(window(f),WM_SYSCOMMAND,command,0));
    else {
        sided_state(0,0,0x80,0);
        CHECK(PostMessageW(window(f),WM_SYSKEYDOWN,VK_SPACE,(LPARAM)(1u|(0x39u<<16)|(1u<<29))));
    }
    CHECK(pump(f,900)==PLAT_OK); CHECK(os_thread_join(&thread)==OS_OK);
    CHECK(WaitForSingleObject(modal_entered,0)==WAIT_OBJECT_0);
    CHECK(WaitForSingleObject(modal_serviced,0)==WAIT_OBJECT_0);
    CHECK(f->work==1 && f->presents==1 && kind(f,PLAT_EV_CLOSE)==1);
    CHECK(win32_private(&f->p)->dropped==0);
    RECT r; CHECK(GetClientRect(window(f),&r));
    plat_event *e=last(f,PLAT_EV_RESIZE);
    CHECK(e && e->w==(uint32_t)r.right && e->h==(uint32_t)r.bottom);
    (void)SetWindowLongPtrW(window(f),GWLP_WNDPROC,(LONG_PTR)modal_original);
    (void)CloseHandle(modal_entered); (void)CloseHandle(modal_serviced); modal_fixture=NULL;
    verdict("r2 native sizing loop services work/T6/close and final geometry",before);
}
static void modal_test(fixture *f) { modal_test_common(f,SC_SIZE|WMSZ_BOTTOMRIGHT); }
static void move_test(fixture *f) { modal_test_common(f,SC_MOVE); }
static void menu_test(fixture *f) { modal_test_common(f,0); state(0,0,0); }
typedef struct sent_case { fixture *f; HANDLE ready; bool rescued; } sent_case;
static void sent_close(void *arg)
{
    sent_case *c=arg;
    (void)WaitForSingleObject(c->ready,INFINITE);
    (void)SendMessageW(window(c->f),WM_CLOSE,0,0);
    /* (E) hang guard, not a latency gate. No other message wakes the UI. */
    c->rescued=WaitForSingleObject(c->f->close_seen,500)!=WAIT_OBJECT_0;
    if(c->rescued) (void)PostMessageW(window(c->f),WM_NULL,0,0);
}
static void sent_test(fixture *f)
{
    int before=failures; f->count=0;
    f->close_seen=CreateEventW(NULL,TRUE,FALSE,NULL); CHECK(f->close_seen!=NULL);
    sent_case c={f,CreateEventW(NULL,TRUE,FALSE,NULL),false}; os_thread thread;
    sent_ready=c.ready; sent_platform=&f->p; sent_thread=GetCurrentThreadId(); waited_over_event=false;
    CHECK(os_thread_start(&thread,sent_close,&c)==OS_OK);
    CHECK(pump(f,700)==PLAT_OK); CHECK(os_thread_join(&thread)==OS_OK);
    CHECK(!waited_over_event); CHECK(!c.rescued); CHECK(kind(f,PLAT_EV_CLOSE)==1); sent_platform=NULL; (void)CloseHandle(c.ready);
    (void)CloseHandle(f->close_seen); f->close_seen=NULL;
    verdict("r2 sent-message wake without unrelated input",before);
}
typedef struct r2_case { const char *name; void (*run)(fixture *); } r2_case;
static const r2_case r2_cases[]={
    {"sent",sent_test},{"modal",modal_test},{"move",move_test},{"menu",menu_test},{"affinity",affinity_test},
    {"altgr",altgr_test},{"ownership",ownership_test},{"packet",packet_test},{"ime",ime_test},
    {"multiscalar",multiscalar_test},{"altcode",altcode_test},{"system",system_test},{"surrogate",surrogate_test},
    {"clip-async",clipboard_async_test},{"clip-contention",clipboard_contention_test},{"clip-results",clipboard_results_test},
    {"clip-newlines",clipboard_newlines_test},{"clip-nul",clipboard_nul_test},
    {"clip-foreign",clipboard_foreign_test},{"clip-foreign-contention",clipboard_foreign_contention_test},
    {"capture",capture_test},{"display",display_test}
};
static void r2_all(void)
{
    for(size_t i=0;i<sizeof r2_cases/sizeof r2_cases[0];i++) {
        fixture f={0}; f.ui_thread=GetCurrentThreadId();
        CHECK(os_wake_init(&f.wake)==OS_OK);
        plat_config cfg={"sublimite Win32 regression",320,180,false,&f.wake,0};
        if(plat_init(&f.p,&cfg)!=PLAT_OK) { CHECK(false); os_wake_close(&f.wake); continue; }
        /* Foreground-only packet injection is the explicit native-input case.
         * All other regression fixtures show without activating. */
        if(!strcmp(r2_cases[i].name,"packet") || !strcmp(r2_cases[i].name,"menu") ||
           !strcmp(r2_cases[i].name,"system")) plat_map(&f.p);
        else (void)ShowWindow(window(&f),SW_SHOWNOACTIVATE);
        CHECK(pump(&f,20)==PLAT_OK); plat_set_repeat(&f.p,0,0); f.count=0;
        printf("win32_plat: case %s\n",r2_cases[i].name); r2_cases[i].run(&f);
        plat_shutdown(&f.p); os_wake_close(&f.wake);
    }
}
int main(int argc, char **argv)
{
    if(argc==7 && !strcmp(argv[1],"clip-owner")) return foreign_main(argv);
    (void)test_translate_message; (void)test_display_sizes; (void)test_display_query; (void)test_display_name;
    setvbuf(stdout,NULL,_IONBF,0); fixture f={0}; f.ui_thread=GetCurrentThreadId();
    CHECK(plat_backend_select(":99","wayland-0")==PLAT_BACKEND_WIN32);
    plat_config cfg={"sublimite Win32 contract",320,180,true,NULL,0};
    CHECK(plat_init(&f.p,&cfg)==PLAT_ERR_NO_DISPLAY);
    CHECK(os_wake_init(&f.wake)==OS_OK); cfg.headless=false; cfg.work_wake=&f.wake;
    if(plat_init(&f.p,&cfg)!=PLAT_OK) { CHECK(false); return 1; }
    plat_map(&f.p); CHECK(pump(&f,20)==PLAT_OK); plat_set_repeat(&f.p,0,0);
    CHECK(window(&f)!=NULL && f.p.backend==PLAT_BACKEND_WIN32);
    if(argc==2) {
        bool found=false;
        for(size_t i=0;i<sizeof r2_cases/sizeof r2_cases[0];i++) if(!strcmp(argv[1],r2_cases[i].name)) {
            r2_cases[i].run(&f); found=true; break;
        }
        CHECK(found); plat_shutdown(&f.p); os_wake_close(&f.wake);
        printf("win32_plat_test: %s (%d failures)\n",failures?"FAIL":"PASS",failures);
        return failures?1:0;
    }
    /* SendInput is allowed only while this window owns foreground focus. */
    if(GetForegroundWindow()==window(&f)) {
        INPUT input[2]={{0},{0}}; input[0].type=INPUT_KEYBOARD; input[0].ki.wVk=VK_F12;
        input[1]=input[0]; input[1].ki.dwFlags=KEYEVENTF_KEYUP; f.count=0;
        CHECK(SendInput(2,input,sizeof input[0])==2); CHECK(pump(&f,30)==PLAT_OK);
        plat_event *e=last(&f,PLAT_EV_KEY); CHECK(e && e->keysym==XKB_KEY_F12);
        printf("win32_plat: SendInput: PASS\n");
    } else printf("win32_plat: SendInput: SKIP (window not foreground; no focus theft)\n");
    keyboard(&f); pointer_test(&f); geometry_clipboard(&f); allocation(&f); loop(&f); plat_shutdown(&f.p);
    int before=failures; DWORD handles_before=0,handles_after=0;
    CHECK(GetProcessHandleCount(GetCurrentProcess(),&handles_before));
    /* (G) repeated lifecycle must not grow the process handle count. Windows
     * may retire unrelated lazily created handles during this interval. */
    for(unsigned i=0;i<4;i++) {
        CHECK(plat_init(&f.p,&cfg)==PLAT_OK); plat_shutdown(&f.p); plat_shutdown(&f.p);
    }
    CHECK(GetProcessHandleCount(GetCurrentProcess(),&handles_after));
    CHECK(handles_after<=handles_before); CHECK(plat_native_win32(&f.p)==NULL);
    printf("win32_plat: handles before=%lu after=%lu (M)\n",handles_before,handles_after);
    verdict("shutdown handle balance",before); os_wake_close(&f.wake);
    r2_all();
    printf("win32_plat_test: %s (%d failures)\n",failures?"FAIL":"PASS",failures);
    return failures?1:0;
}
#endif
