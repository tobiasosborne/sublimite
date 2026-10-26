/* Clipboard I/O can synchronously invoke a foreign delayed-rendering owner.
 * This worker owns its helper HWND and all native clipboard handles. */
#if defined(_WIN32)
#include "private.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

typedef struct win32_clip_job {
    struct win32_clip_job *next;
    uint8_t *bytes;
    size_t len;
    int which;
    bool set, ok;
} win32_clip_job;
struct win32_clip {
    SRWLOCK lock;
    volatile LONG refs;
    HANDLE thread, ready, stop, tasks, completed;
    HWND hwnd;
    WCHAR class_name[64]; /* (P) prefix + pointer hex + NUL fits. */
    win32_clip_job *first, *last, *done, *done_last;
    unsigned used, lost;
    bool started, owned, publishing, busy, stopping;
};
static void clip_job_free(win32_clip_job *j)
{ if (j) { free(j->bytes); free(j); } }
static void clip_unref(win32_clip *c)
{
    if (InterlockedDecrement(&c->refs)) return;
    while (c->first) { win32_clip_job *j=c->first; c->first=j->next; clip_job_free(j); }
    while (c->done) { win32_clip_job *j=c->done; c->done=j->next; clip_job_free(j); }
    if (c->ready) (void)CloseHandle(c->ready);
    if (c->stop) (void)CloseHandle(c->stop);
    if (c->tasks) (void)CloseHandle(c->tasks);
    if (c->completed) (void)CloseHandle(c->completed);
    free(c);
}
static LRESULT CALLBACK clip_procedure(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    win32_clip *c=(win32_clip *)GetWindowLongPtrW(hwnd,GWLP_USERDATA);
    if (msg==WM_NCCREATE) {
        c=((CREATESTRUCTW *)lp)->lpCreateParams;
        (void)SetWindowLongPtrW(hwnd,GWLP_USERDATA,(LONG_PTR)c);
    }
    if (c && msg==WM_DESTROYCLIPBOARD && c->owned && !c->publishing) {
        c->owned=false;
        AcquireSRWLockExclusive(&c->lock); c->lost++;
        (void)SetEvent(c->completed); ReleaseSRWLockExclusive(&c->lock);
    }
    return DefWindowProcW(hwnd,msg,wp,lp);
}
static bool clip_open(win32_clip *c)
{
    /* (E) retry envelope follows the user's native double-click tolerance;
     * (P) backoff starts at the system's minimum UI timer period and doubles.
     * No retry or foreign clipboard work runs on the application thread. */
    ULONGLONG end=GetTickCount64()+GetDoubleClickTime(); DWORD delay=USER_TIMER_MINIMUM;
    for (;;) {
        if (WaitForSingleObject(c->stop,0)==WAIT_OBJECT_0) return false;
        if (OpenClipboard(c->hwnd)) return true;
        ULONGLONG now=GetTickCount64(); if (now>=end) return false;
        DWORD left=(DWORD)(end-now), wait=delay<left ? delay : left;
        if (WaitForSingleObject(c->stop,wait)==WAIT_OBJECT_0) return false;
        if (delay<=MAXDWORD/2u) delay*=2u;
    }
}
static bool clip_write(win32_clip *c, const win32_clip_job *j)
{
    int units=j->len ? MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,(const char *)j->bytes,(int)j->len,NULL,0) : 0;
    if (j->len && !units) return false;
    /* (P) each input UTF-16 unit needs at most CR+LF, plus terminating NUL. */
    size_t capacity=2u*(size_t)units+1u;
    if (capacity>SIZE_MAX/sizeof(WCHAR)) return false;
    WCHAR *source=malloc(((size_t)units+1u)*sizeof *source);
    HGLOBAL mem=GlobalAlloc(GMEM_MOVEABLE,capacity*sizeof(WCHAR));
    WCHAR *dest=mem ? GlobalLock(mem) : NULL;
    if (!source || !dest) {
        free(source); if (dest) (void)GlobalUnlock(mem); if (mem) (void)GlobalFree(mem); return false;
    }
    bool ok=!units || MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,(const char *)j->bytes,(int)j->len,source,units)==units;
    size_t at=0;
    if (ok) for (int i=0;i<units;i++) {
        if (source[i]=='\n' && (!i || source[i-1]!='\r')) dest[at++]='\r';
        dest[at++]=source[i];
    }
    dest[at]=0; free(source); (void)GlobalUnlock(mem);
    if (!ok || !clip_open(c)) { (void)GlobalFree(mem); return false; }
    c->publishing=true;
    ok=EmptyClipboard()!=0;
    c->publishing=false;
    if (ok) { c->owned=false; ok=SetClipboardData(CF_UNICODETEXT,mem)!=NULL; }
    if (ok) c->owned=true;
    (void)CloseClipboard();
    if (!ok) (void)GlobalFree(mem);
    return ok;
}
static bool clip_read(win32_clip *c, win32_clip_job *j)
{
    if (j->which==PLAT_CLIP_PRIMARY || !clip_open(c)) return false;
    HGLOBAL mem=GetClipboardData(CF_UNICODETEXT);
    const WCHAR *source=mem ? GlobalLock(mem) : NULL;
    bool ok=false;
    if (source) {
        SIZE_T capacity=GlobalSize(mem)/sizeof *source, units=0;
        while (units<capacity && source[units]) units++;
        if (units<capacity && units<=INT_MAX) {
            int bytes=units ? WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,source,(int)units,NULL,0,NULL,NULL) : 0;
            if (!units || bytes>0) {
                j->bytes=malloc((size_t)bytes+1u);
                if (j->bytes) {
                    ok=!bytes || WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,source,(int)units,(char *)j->bytes,bytes,NULL,NULL)==bytes;
                    /* The editor's internal text convention is LF. Preserve
                     * isolated CR; normalise only native CRLF pairs. */
                    if (ok) for (size_t i=0;i<(size_t)bytes;i++) {
                        if (j->bytes[i]=='\r' && i+1u<(size_t)bytes && j->bytes[i+1u]=='\n') continue;
                        j->bytes[j->len++]=j->bytes[i];
                    }
                    j->bytes[j->len]=0;
                }
            }
        }
        (void)GlobalUnlock(mem);
    }
    (void)CloseClipboard(); return ok;
}
static DWORD WINAPI clip_thread(void *arg)
{
    win32_clip *c=arg; HINSTANCE instance=GetModuleHandleW(NULL);
    (void)swprintf(c->class_name,sizeof c->class_name/sizeof c->class_name[0],L"sublimite-clip-%p",(void *)c);
    WNDCLASSEXW cls={0}; cls.cbSize=sizeof cls; cls.lpfnWndProc=clip_procedure;
    cls.hInstance=instance; cls.lpszClassName=c->class_name;
    if (RegisterClassExW(&cls)) c->hwnd=CreateWindowExW(0,c->class_name,L"",0,0,0,0,0,HWND_MESSAGE,NULL,instance,c);
    c->started=c->hwnd!=NULL; (void)SetEvent(c->ready);
    while (c->started && WaitForSingleObject(c->stop,0)!=WAIT_OBJECT_0) {
        MSG msg;
        while (PeekMessageW(&msg,NULL,0,0,PM_REMOVE)) (void)DispatchMessageW(&msg);
        AcquireSRWLockExclusive(&c->lock);
        if (c->stopping) { ReleaseSRWLockExclusive(&c->lock); break; }
        win32_clip_job *j=c->first;
        c->busy=j!=NULL;
        if (j) { c->first=j->next; if (!c->first) c->last=NULL; j->next=NULL; }
        if (!c->first) (void)ResetEvent(c->tasks);
        ReleaseSRWLockExclusive(&c->lock);
        if (!j) {
            HANDLE wakes[]={c->stop,c->tasks};
            (void)MsgWaitForMultipleObjectsEx(2,wakes,INFINITE,QS_ALLINPUT,MWMO_INPUTAVAILABLE); continue;
        }
        j->ok=j->set ? clip_write(c,j) : clip_read(c,j);
        AcquireSRWLockExclusive(&c->lock);
        c->busy=false;
        if (j->set && j->ok) { c->used--; clip_job_free(j); }
        else {
            if (c->done_last) c->done_last->next=j; else c->done=j;
            c->done_last=j; (void)SetEvent(c->completed);
        }
        ReleaseSRWLockExclusive(&c->lock);
    }
    if (c->hwnd) (void)DestroyWindow(c->hwnd);
    (void)UnregisterClassW(c->class_name,instance); clip_unref(c); return 0;
}
int win32_clip_init(win32_plat *s)
{
    win32_clip *c=calloc(1,sizeof *c); if (!c) return PLAT_ERR_FAIL;
    c->refs=1; InitializeSRWLock(&c->lock);
    c->ready=CreateEventW(NULL,TRUE,FALSE,NULL); c->stop=CreateEventW(NULL,TRUE,FALSE,NULL);
    c->tasks=CreateEventW(NULL,TRUE,FALSE,NULL); c->completed=CreateEventW(NULL,TRUE,FALSE,NULL);
    if (!c->ready || !c->stop || !c->tasks || !c->completed) { clip_unref(c); return PLAT_ERR_FAIL; }
    (void)InterlockedIncrement(&c->refs);
    c->thread=CreateThread(NULL,0,clip_thread,c,0,NULL);
    if (!c->thread) { clip_unref(c); clip_unref(c); return PLAT_ERR_FAIL; }
    (void)WaitForSingleObject(c->ready,INFINITE);
    if (!c->started) { (void)WaitForSingleObject(c->thread,INFINITE); (void)CloseHandle(c->thread); clip_unref(c); return PLAT_ERR_FAIL; }
    s->clip=c; return PLAT_OK;
}
void win32_clip_stop(win32_plat *s)
{
    win32_clip *c=s->clip; if (!c) return;
    s->clip=NULL;
    AcquireSRWLockExclusive(&c->lock); c->stopping=true;
    bool busy=c->busy;
    (void)SetEvent(c->stop); ReleaseSRWLockExclusive(&c->lock);
    /* Idle teardown cannot enter foreign work after stopping was set. */
    if (!busy) (void)WaitForSingleObject(c->thread,INFINITE);
    (void)CloseHandle(c->thread);
    /* A delayed foreign renderer cannot be interrupted safely. Detach with no
     * platform pointer: the worker releases its own resources when it returns. */
    clip_unref(c);
}
HANDLE win32_clip_wake(const win32_plat *s)
{ return s->clip ? s->clip->completed : NULL; }
int win32_clip_submit(win32_plat *s, bool set, int which, const void *bytes, size_t len)
{
    win32_clip *c=s->clip; if (!c) return PLAT_ERR_FAIL;
    win32_clip_job *j=calloc(1,sizeof *j); if (!j) return PLAT_ERR_FAIL;
    j->which=which; j->set=set;
    if (set && len) {
        j->bytes=malloc(len); if (!j->bytes) { free(j); return PLAT_ERR_FAIL; }
        memcpy(j->bytes,bytes,len); j->len=len;
    }
    AcquireSRWLockExclusive(&c->lock);
    if (c->used==WIN32_EVENT_CAP) { ReleaseSRWLockExclusive(&c->lock); clip_job_free(j); return PLAT_ERR_FAIL; }
    c->used++;
    if (c->last) c->last->next=j; else c->first=j;
    c->last=j; (void)SetEvent(c->tasks); ReleaseSRWLockExclusive(&c->lock); return PLAT_OK;
}
bool win32_clip_poll(win32_plat *s, plat_event *ev)
{
    win32_clip *c=s->clip; if (!c) return false;
    AcquireSRWLockExclusive(&c->lock);
    win32_clip_job *j=c->done;
    bool lost=false;
    if (j) { c->done=j->next; if (!c->done) c->done_last=NULL; c->used--; }
    else if (c->lost) { c->lost--; lost=true; }
    if (!c->done && !c->lost) (void)ResetEvent(c->completed);
    ReleaseSRWLockExclusive(&c->lock);
    if (!j && !lost) return false;
    *ev=(plat_event){.kind=PLAT_EV_CLIPBOARD,.clip_which=PLAT_CLIP_CLIPBOARD,.code=2};
    if (j) {
        ev->clip_which=(uint8_t)j->which; ev->clip_ok=j->ok; ev->code=j->set ? 2u : (j->ok ? 0u : 1u);
        if (!j->set) {
            free(s->clip_data); s->clip_data=j->ok ? j->bytes : NULL; s->clip_len=j->ok ? j->len : 0;
            if (j->ok) j->bytes=NULL;
        }
        clip_job_free(j);
    }
    return true;
}
#endif
