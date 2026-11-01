/* Win32 drop (edit-cxho.20) and pointer-leave (edit-osn3, Win32 part) contract.
 * Everything runs against a real window on the caller's (private) station:
 * WM_DROPFILES carries a hand-built HDROP, WM_MOUSELEAVE is sent directly.
 * A real drag from Explorer is a live check, not covered here. */
#if defined(_WIN32)
#define _WIN32_WINNT 0x0A00
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "x11/plat.h"
#include "os/os.h"
#include "base/base.h"
#include "dnd/dnd.h"

static int failures;
#define CHECK(x) do { if (!(x)) { printf("FAIL %s:%d: %s (winerr %lu)\n",__FILE__,__LINE__,#x,GetLastError()); failures++; } } while (0)

typedef struct fixture {
    plat p;
    os_wake wake;
    plat_event events[64];
    size_t count;
} fixture;
static void event(void *ud, const plat_event *e)
{ fixture *f=ud; if (f->count<sizeof f->events/sizeof f->events[0]) f->events[f->count++]=*e; else CHECK(false); }
static void work(void *ud) { os_wake_drain(&((fixture *)ud)->wake); }
static void blink(void *ud) { (void)ud; }
static void present(void *ud, uint32_t a, uint64_t b, uint64_t c) { (void)ud; (void)a; (void)b; (void)c; }
static void pump(fixture *f)
{ plat_callbacks cb={.ud=f,.on_event=event,.on_blink=blink,.on_work=work,.on_present_complete=present}; CHECK(plat_run_for(&f->p,&cb,0)==PLAT_OK); }
static HWND window(fixture *f) { return plat_native_win32(&f->p); }
static size_t kind(const fixture *f, plat_ev_kind k)
{ size_t n=0; for (size_t i=0;i<f->count;i++) if (f->events[i].kind==k) n++; return n; }

/* HDROP over a double-NUL wide list; entries are NUL-separated WCHAR strings. */
static HGLOBAL make_drop(const WCHAR *const *paths, size_t n, POINT pt)
{
    size_t chars=1; for (size_t i=0;i<n;i++) chars+=wcslen(paths[i])+1;
    HGLOBAL g=GlobalAlloc(GMEM_MOVEABLE|GMEM_ZEROINIT,sizeof(DROPFILES)+chars*sizeof(WCHAR));
    if (!g) return NULL;
    DROPFILES *d=GlobalLock(g); d->pFiles=sizeof *d; d->pt=pt; d->fWide=TRUE;
    WCHAR *at=(WCHAR *)((BYTE *)d+sizeof *d);
    for (size_t i=0;i<n;i++) { size_t l=wcslen(paths[i])+1; memcpy(at,paths[i],l*sizeof(WCHAR)); at+=l; }
    GlobalUnlock(g); return g;
}
/* Sends WM_DROPFILES (the handler owns and releases the HDROP via DragFinish). */
static void drop(fixture *f, const WCHAR *const *paths, size_t n)
{
    HGLOBAL g=make_drop(paths,n,(POINT){7,9}); CHECK(g!=NULL); if (!g) return;
    f->count=0; (void)SendMessageW(window(f),WM_DROPFILES,(WPARAM)g,0); pump(f);
}
static const char *data(fixture *f, size_t *len)
{ return (const char *)plat_drop_data(&f->p,len); }

static void accepts_files(fixture *f)
{
    CHECK((GetWindowLongW(window(f),GWL_EXSTYLE)&WS_EX_ACCEPTFILES)!=0);
}

static void drive_path(fixture *f)
{
    /* C:\Users\x y\<u-umlaut>.txt */
    const WCHAR *p[]={L"C:\\Users\\x y\\\u00fc.txt"};
    drop(f,p,1);
    CHECK(kind(f,PLAT_EV_DROP)==1);
    size_t len=0; const char *d=data(f,&len);
    const char want[]="file:///C:/Users/x%20y/%C3%BC.txt\r\n";
    CHECK(d && len==sizeof want-1 && !memcmp(d,want,len));
    edit_arena a; CHECK(edit_arena_init(&a,(size_t)IPC_MAX_PATHS*IPC_PATH_CAP)==0);
    ipc_request r; dnd_counts c;
    CHECK(dnd_parse_uri_list((const uint8_t *)d,len,"HOST",&a,&r,&c)==dnd_ok);
    CHECK(r.count==1 && c.skipped==0);
    CHECK(r.count==1 && !strcmp(r.paths[0].path,"C:\\Users\\x y\\\xc3\xbc.txt"));
    edit_arena_free(&a);
}

static void unc_path(fixture *f)
{
    const WCHAR *p[]={L"\\\\wsl.localhost\\Ubuntu\\home\\x"};
    drop(f,p,1);
    CHECK(kind(f,PLAT_EV_DROP)==1);
    size_t len=0; const char *d=data(f,&len);
    const char want[]="file://wsl.localhost/Ubuntu/home/x\r\n";
    CHECK(d && len==sizeof want-1 && !memcmp(d,want,len));
    edit_arena a; CHECK(edit_arena_init(&a,(size_t)IPC_MAX_PATHS*IPC_PATH_CAP)==0);
    ipc_request r; dnd_counts c;
    CHECK(dnd_parse_uri_list((const uint8_t *)d,len,"HOST",&a,&r,&c)==dnd_ok);
    CHECK(r.count==1 && !strcmp(r.paths[0].path,"\\\\wsl.localhost\\Ubuntu\\home\\x"));
    edit_arena_free(&a);
}

static void reserved_chars(fixture *f)
{
    const WCHAR *p[]={L"D:\\a#b?c%d [e]+f.txt"};
    drop(f,p,1);
    size_t len=0; const char *d=data(f,&len);
    CHECK(kind(f,PLAT_EV_DROP)==1 && d);
    edit_arena a; CHECK(edit_arena_init(&a,(size_t)IPC_MAX_PATHS*IPC_PATH_CAP)==0);
    ipc_request r; dnd_counts c;
    CHECK(dnd_parse_uri_list((const uint8_t *)d,len,NULL,&a,&r,&c)==dnd_ok);
    CHECK(r.count==1 && c.skipped==0 && !strcmp(r.paths[0].path,"D:\\a#b?c%d [e]+f.txt"));
    edit_arena_free(&a);
}

static void many_files(fixture *f)
{
    enum { N=IPC_MAX_PATHS+5 };
    WCHAR (*names)[32]=calloc(N,sizeof *names); const WCHAR **p=calloc(N,sizeof *p);
    CHECK(names && p); if (!names || !p) { free(names); free(p); return; }
    for (size_t i=0;i<N;i++) { swprintf(names[i],32,L"C:\\d\\f%zu.txt",i); p[i]=names[i]; }
    drop(f,p,N);
    CHECK(kind(f,PLAT_EV_DROP)==1);
    size_t len=0; const char *d=data(f,&len); size_t lines=0;
    for (size_t i=0;d && i<len;i++) if (d[i]=='\n') lines++;
    CHECK(lines==N);
    edit_arena a; CHECK(edit_arena_init(&a,(size_t)IPC_MAX_PATHS*IPC_PATH_CAP)==0);
    ipc_request r; dnd_counts c;
    CHECK(dnd_parse_uri_list((const uint8_t *)d,len,NULL,&a,&r,&c)==dnd_ok);
    CHECK(r.count==IPC_MAX_PATHS && c.overflow==5);
    CHECK(r.count==IPC_MAX_PATHS && !strcmp(r.paths[IPC_MAX_PATHS-1].path,"C:\\d\\f127.txt"));
    edit_arena_free(&a); free(names); free(p);
}

static void long_path(fixture *f)
{
    /* Past MAX_PATH (260): DragQueryFileW's length probe must size the buffer. */
    WCHAR path[400]; wcscpy(path,L"C:\\"); size_t l=3;
    while (l+9<sizeof path/sizeof path[0]-8) { wcscpy(path+l,L"abcdefgh\\"); l+=9; }
    wcscpy(path+l,L"z.txt");
    const WCHAR *p[]={path}; drop(f,p,1);
    size_t len=0; const char *d=data(f,&len);
    CHECK(kind(f,PLAT_EV_DROP)==1 && d);
    edit_arena a; CHECK(edit_arena_init(&a,(size_t)IPC_MAX_PATHS*IPC_PATH_CAP)==0);
    ipc_request r; dnd_counts c;
    CHECK(dnd_parse_uri_list((const uint8_t *)d,len,NULL,&a,&r,&c)==dnd_ok);
    char want[1024]; int n=WideCharToMultiByte(CP_UTF8,0,path,-1,want,sizeof want,NULL,NULL);
    CHECK(n>0 && r.count==1 && !strcmp(r.paths[0].path,want));
    edit_arena_free(&a);
}

static void failed_conversion(fixture *f)
{
    /* Establish known data, then a drop whose only entry cannot become UTF-8
     * (NTFS names may hold a lone surrogate). No event; old data intact. */
    const WCHAR *ok[]={L"C:\\keep.txt"}; drop(f,ok,1);
    CHECK(kind(f,PLAT_EV_DROP)==1);
    size_t before=0; const char *old=data(f,&before);
    char saved[128]; CHECK(old && before<sizeof saved); if (!old || before>=sizeof saved) return;
    memcpy(saved,old,before);
    const WCHAR bad[]={'C',':','\\','a',0xd800,'.','t','x','t',0};
    const WCHAR *b[]={bad}; drop(f,b,1);
    CHECK(kind(f,PLAT_EV_DROP)==0);
    size_t now=0; const char *d=data(f,&now);
    CHECK(d && now==before && !memcmp(d,saved,now));
    /* A bad entry among good ones is skipped; the good ones still arrive. */
    const WCHAR *mix[]={L"C:\\one.txt",bad,L"relative\\two.txt",L"C:\\three.txt"};
    drop(f,mix,4);
    CHECK(kind(f,PLAT_EV_DROP)==1);
    d=data(f,&now);
    const char want[]="file:///C:/one.txt\r\nfile:///C:/three.txt\r\n";
    CHECK(d && now==sizeof want-1 && !memcmp(d,want,now));
    /* Empty HDROP: no event. */
    drop(f,NULL,0); CHECK(kind(f,PLAT_EV_DROP)==0);
}

static void opens_real_file(fixture *f)
{
    WCHAR dir[MAX_PATH]; DWORD n=GetTempPathW(MAX_PATH,dir); CHECK(n>0 && n<MAX_PATH);
    WCHAR path[MAX_PATH+64];
    swprintf(path,sizeof path/sizeof path[0],L"%lssublimite-test-drop %lu \u00fc.txt",dir,GetCurrentProcessId());
    HANDLE h=CreateFileW(path,GENERIC_WRITE,0,NULL,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);
    CHECK(h!=INVALID_HANDLE_VALUE); if (h==INVALID_HANDLE_VALUE) return;
    DWORD w; (void)WriteFile(h,"hi",2,&w,NULL); (void)CloseHandle(h);
    const WCHAR *p[]={path}; drop(f,p,1);
    size_t len=0; const char *d=data(f,&len);
    CHECK(kind(f,PLAT_EV_DROP)==1 && d);
    edit_arena a; CHECK(edit_arena_init(&a,(size_t)IPC_MAX_PATHS*IPC_PATH_CAP)==0);
    ipc_request r; dnd_counts c;
    CHECK(dnd_parse_uri_list((const uint8_t *)d,len,NULL,&a,&r,&c)==dnd_ok && r.count==1);
    if (r.count==1) {
        /* What editor.c's stat() sees (UTF-8 active code page) and the wide open. */
        struct stat st; CHECK(stat(r.paths[0].path,&st)==0 && st.st_size==2);
        WCHAR wide[MAX_PATH+64];
        CHECK(MultiByteToWideChar(CP_UTF8,0,r.paths[0].path,-1,wide,MAX_PATH+64)>0);
        CHECK(!wcscmp(wide,path));
        HANDLE o=CreateFileW(wide,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,0,NULL);
        CHECK(o!=INVALID_HANDLE_VALUE); if (o!=INVALID_HANDLE_VALUE) (void)CloseHandle(o);
    }
    edit_arena_free(&a); CHECK(DeleteFileW(path));
}

/* After a move the leave request is either still armed or already answered: with
 * no real pointer over this invisible window the system posts WM_MOUSELEAVE at once
 * (and clears the request). Either way a LEAVE event must reach the queue. */
static void move_then_leave(fixture *f, int x)
{
    HWND w=window(f); f->count=0;
    (void)SendMessageW(w,WM_MOUSEMOVE,0,MAKELPARAM(x,13));
    TRACKMOUSEEVENT q={sizeof q,TME_QUERY,w,0}; CHECK(TrackMouseEvent(&q));
    bool armed=q.hwndTrack==w && (q.dwFlags&TME_LEAVE);
    pump(f);
    CHECK(kind(f,PLAT_EV_MOTION)==1);
    CHECK(armed || kind(f,PLAT_EV_LEAVE)==1);
}
static void pointer_leave(fixture *f)
{
    HWND w=window(f);
    move_then_leave(f,11);
    /* An explicit WM_MOUSELEAVE yields exactly one LEAVE event at the last pointer. */
    f->count=0; (void)SendMessageW(w,WM_MOUSELEAVE,0,0); pump(f);
    CHECK(kind(f,PLAT_EV_LEAVE)==1 && f->events[0].x==11 && f->events[0].y==13);
    /* The one-shot request is re-armed by the next move. */
    move_then_leave(f,12);
    move_then_leave(f,13);
}

int main(void)
{
    fixture f; memset(&f,0,sizeof f);
    plat_config cfg={0}; cfg.title="sublimite Win32 drop"; cfg.width=320; cfg.height=180;
    CHECK(os_wake_init(&f.wake)==OS_OK); cfg.work_wake=&f.wake;
    if (plat_init(&f.p,&cfg)!=PLAT_OK) { CHECK(false); return 1; }
    plat_map(&f.p); pump(&f);
    CHECK(window(&f)!=NULL);
    CHECK(plat_drop_data(&f.p,NULL)==NULL);
    size_t len=99; CHECK(plat_drop_data(&f.p,&len)==NULL && len==0);
    struct { const char *name; void (*run)(fixture *); } cases[]={
        {"accepts files",accepts_files},{"drive path",drive_path},{"unc path",unc_path},
        {"reserved chars",reserved_chars},{"many files",many_files},{"long path",long_path},
        {"failed conversion",failed_conversion},{"opens real file",opens_real_file},
        {"pointer leave",pointer_leave}};
    for (size_t i=0;i<sizeof cases/sizeof cases[0];i++) {
        int before=failures; cases[i].run(&f);
        printf("win32_drop: %s: %s\n",cases[i].name,failures==before?"PASS":"FAIL");
    }
    plat_shutdown(&f.p); os_wake_close(&f.wake);
    printf("win32_drop_test: %s (%d failures)\n",failures?"FAIL":"PASS",failures);
    return failures?1:0;
}
#else
int main(void) { return 0; }
#endif
