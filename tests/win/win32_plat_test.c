/* Real-window contract suite. (G) assertions are semantic, not timing budgets.
 * Fixture dimensions/delays below are (E) small desktop window and hang guards. */
#if defined(_WIN32)
#define _WIN32_WINNT 0x0A00
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "x11/plat.h"
#include "win32/private.h"
#include "os/os.h"
#include "base/base.h"
#include "../../vendor/xkbcommon/xkbcommon-keysyms.h"

typedef struct fixture {
    plat p;
    os_wake wake;
    plat_event events[512]; /* (G) twice the contract ring, detects burst loss. */
    size_t count;
    unsigned work, blink, presents;
    bool quit_first;
    DWORD ui_thread;
    uint32_t serial;
    uint64_t ust, msc;
} fixture;
static int failures;
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FAIL %s:%d: %s (winerr %lu)\n",__FILE__,__LINE__,#x,GetLastError()); failures++; } } while (0)
static void event(void *ud, const plat_event *e)
{
    fixture *f = ud;
    if (f->count < sizeof f->events / sizeof f->events[0]) f->events[f->count++] = *e;
    else CHECK(false);
    if (f->quit_first) plat_quit(&f->p);
}
static void work(void *ud) { fixture *f=ud; os_wake_drain(&f->wake); f->work++; }
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
static void pointer(fixture *f)
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
    CHECK(plat_clip_request(&f->p,PLAT_CLIP_CLIPBOARD)==PLAT_OK); CHECK(pump(f,0)==PLAT_OK);
    size_t len=0; const uint8_t *bytes=plat_clip_data(&f->p,&len);
    plat_event *e=last(f,PLAT_EV_CLIPBOARD);
    CHECK(e && e->clip_ok && len==sizeof text-1 && bytes && !memcmp(bytes,text,len));
    CHECK(plat_clip_set(&f->p,PLAT_CLIP_PRIMARY,"ignored",7)==PLAT_OK);
    CHECK(plat_clip_request(&f->p,PLAT_CLIP_PRIMARY)==PLAT_OK); CHECK(pump(f,0)==PLAT_OK);
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
int main(void)
{
    setvbuf(stdout,NULL,_IONBF,0); fixture f={0}; f.ui_thread=GetCurrentThreadId();
    CHECK(plat_backend_select(":99","wayland-0")==PLAT_BACKEND_WIN32);
    plat_config cfg={"sublimite Win32 contract",320,180,true,NULL,0};
    CHECK(plat_init(&f.p,&cfg)==PLAT_ERR_NO_DISPLAY);
    CHECK(os_wake_init(&f.wake)==OS_OK); cfg.headless=false; cfg.work_wake=&f.wake;
    if(plat_init(&f.p,&cfg)!=PLAT_OK) { CHECK(false); return 1; }
    plat_map(&f.p); CHECK(pump(&f,20)==PLAT_OK); plat_set_repeat(&f.p,0,0);
    CHECK(window(&f)!=NULL && f.p.backend==PLAT_BACKEND_WIN32);
    /* SendInput is allowed only while this window owns foreground focus. */
    if(GetForegroundWindow()==window(&f)) {
        INPUT input[2]={{0},{0}}; input[0].type=INPUT_KEYBOARD; input[0].ki.wVk=VK_F12;
        input[1]=input[0]; input[1].ki.dwFlags=KEYEVENTF_KEYUP; f.count=0;
        CHECK(SendInput(2,input,sizeof input[0])==2); CHECK(pump(&f,30)==PLAT_OK);
        plat_event *e=last(&f,PLAT_EV_KEY); CHECK(e && e->keysym==XKB_KEY_F12);
        printf("win32_plat: SendInput: PASS\n");
    } else printf("win32_plat: SendInput: SKIP (window not foreground; no focus theft)\n");
    keyboard(&f); pointer(&f); geometry_clipboard(&f); allocation(&f); loop(&f); plat_shutdown(&f.p);
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
    printf("win32_plat_test: %s (%d failures)\n",failures?"FAIL":"PASS",failures);
    return failures?1:0;
}
#endif
