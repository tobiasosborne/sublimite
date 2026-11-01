/* Reusable Windows editor test fixture (edit-177r.14). Header-only, test code.
 *
 * Contract (frozen before consumers: hot exit, save/reload, commands, large
 * files, fonts). Every function returns 0 / true on success; failures print
 * "editor_fixture:<line> FAIL <expr>" to stderr, run efx_fail_hook (if set)
 * and return nonzero. Nothing here calls exit() or touches a display.
 *
 *  Profile    efx_profile_create / _adopt / _path / _destroy: a private
 *             %TEMP%\sublimite-test-<tag>-<hex> tree, exported as
 *             SUBLIMITE_PROFILE with SUBLIMITE_TEST_INPUT=1. _destroy removes
 *             only a tree whose final component starts "sublimite-test-".
 *  Posted     efx_post_char / _text / _ctrl_key post window messages only
 *  input      (no SendInput, no focus); each char first resets the posted
 *             modifier state (EFX_MOD_HOOK), as editor_exe_test does.
 *  In-process efx_settle (native barrier + idle), efx_wait_mutation,
 *             efx_type (post + wait for each mutation), efx_expect_bytes
 *             (exact oracle), efx_open_null / efx_wait_presented
 *             (headless null backend: NO window, so efx_type uses
 *             editor_inject; efx_wait_presented is a deterministic, SIMULATED
 *             completion: it signals T5/T6 synchronously, never display
 *             timing). efx_open_d3d11 gives a real hidden window for posted
 *             input; efx_wait_submitted is its CPU-submit completion, not a
 *             displayed present.
 *  Process    efx_child_run re-launches the running test exe with a mode
 *             (and optional profile root) and checks its exit code under a
 *             watchdog; efx_wait_window / efx_wait_contents drive a real
 *             sublimite.exe child by pid.
 *  Recovery   efx_journal_has_insert / efx_wait_journal_insert read the
 *             journal independently of the editor (journal_scan_file), the
 *             oracle that a mutation is durable before an abrupt kill.
 *
 * Timeouts: EFX_WATCH_NS is the existing editor settle watchdog
 * (tests/editor_test.c, (G) 5 s); a child run allows one EFX_WATCH_NS per
 * phase the caller names (startup, test, teardown), as editor_win_test did. */
#ifndef EDIT_TEST_EDITOR_FIXTURE_H
#define EDIT_TEST_EDITOR_FIXTURE_H
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include "editor/private.h"
#include "journal/journal.h"
#include "os/os.h"
#include "render/render.h"
#include "d3d11/d3d11.h"
#include "x11/plat.h"

#include "timeout_diag.h"
#define EFX_WATCH_NS UINT64_C(5000000000)
#define EFX_MOD_HOOK (WM_APP+0x51)  /* test-only posted modifier state */
/* Headless editors have no window to pump: a bounded wait on the wait set. */
#define EFX_HEADLESS_MS 20
#define EFX_BARRIER_MESSAGE (WM_APP+0x53)

/* Optional: called on every failure, e.g. to release a gate a test holds. */
static void (*efx_fail_hook)(void);
static inline int efx_failed(int line, const char *expr)
{
    fprintf(stderr,"editor_fixture:%d FAIL %s native=%lu\n",line,expr,GetLastError());
    if(efx_fail_hook) efx_fail_hook();
    return 1;
}
#define EFX_REQ(x) do { if(!(x)) return efx_failed(__LINE__,#x); } while(0)

/* ---- profile and scratch ------------------------------------------------ */
typedef struct efx_profile { char root[OS_PATH_MAX]; bool owner; } efx_profile;

static inline bool efx_owned_name(const char *path)
{
    const char *base=strrchr(path,'\\'); base=base ? base+1 : path;
    return !strncmp(base,"sublimite-test-",sizeof "sublimite-test-"-1u);
}
static inline int efx_export(const efx_profile *p)
{
    EFX_REQ(os_env_set("SUBLIMITE_PROFILE",p->root)==OS_OK);
    EFX_REQ(os_env_set("SUBLIMITE_TEST_INPUT","1")==OS_OK);
    return 0;
}
/* Creates %TEMP%\sublimite-test-<tag>-<hex> and exports it. */
static inline int efx_profile_create(efx_profile *p, const char *tag)
{
    char temp[OS_PATH_MAX]; unsigned random;
    *p=(efx_profile){0};
    EFX_REQ(os_temp_dir(temp,sizeof temp)==OS_OK && os_random(&random,sizeof random)==OS_OK);
    EFX_REQ(snprintf(p->root,sizeof p->root,"%s\\sublimite-test-%s-%x",temp,tag,random)>0);
    EFX_REQ(os_mkdir(p->root)==OS_OK);
    p->owner=true;
    return efx_export(p);
}
/* A child adopts the parent's tree: exported, never removed by the child. */
static inline int efx_profile_adopt(efx_profile *p, const char *root)
{
    *p=(efx_profile){0};
    EFX_REQ(snprintf(p->root,sizeof p->root,"%s",root)>0 && efx_owned_name(p->root));
    return efx_export(p);
}
static inline int efx_profile_path(const efx_profile *p, const char *name, char *out, size_t cap)
{ EFX_REQ(snprintf(out,cap,"%s\\%s",p->root,name)>0); return 0; }
static inline int efx_write_file(const char *path, const void *bytes, size_t n)
{
    os_file f=OS_FILE_INIT; size_t wrote=0;
    EFX_REQ(os_file_open(&f,path,OS_OPEN_WRITE|OS_OPEN_CREATE)==OS_OK);
    os_err rc=os_file_write(&f,bytes,n,&wrote); os_file_close(&f);
    EFX_REQ(rc==OS_OK && wrote==n); return 0;
}
static inline void efx_tree_remove(const char *path)
{
    os_dir dir=OS_DIR_INIT; os_dir_iter it;
    if(os_dir_open(&dir,path)!=OS_OK) return;
    if(os_dir_iter_open(&it,&dir)==OS_OK) {
        const char *name;
        while(os_dir_next(&it,&name)==OS_OK && name) {
            char child[OS_PATH_MAX]; snprintf(child,sizeof child,"%s\\%s",path,name);
            os_stat st;
            if(os_stat_path(child,OS_STAT_NOFOLLOW,&st)==OS_OK && st.kind==OS_KIND_DIR) efx_tree_remove(child);
            else (void)os_unlink(child);
        }
        os_dir_iter_close(&it);
    }
    os_dir_close(&dir); (void)os_rmdir(path);
}
/* Removes only a tree named sublimite-test-*; anything else is left alone. */
static inline void efx_profile_destroy(efx_profile *p)
{
    if(p->owner && efx_owned_name(p->root)) efx_tree_remove(p->root);
    p->owner=false;
}

/* ---- posted input ------------------------------------------------------- */
static inline bool efx_post_char(HWND window, WPARAM scalar)
{
    /* Posted text has no physical key state: set the intended modifiers (none)
     * rather than inherit them from native startup. */
    return PostMessageW(window,EFX_MOD_HOOK,0,0) && PostMessageW(window,WM_CHAR,scalar,1);
}
static inline bool efx_post_text(HWND window, const WCHAR *text)
{
    for(;*text;text++) if(!efx_post_char(window,*text)) return false;
    return true;
}
/* Posted Ctrl+<vk> chord; posted messages do not update the thread's key state. */
static inline bool efx_post_ctrl_key(HWND window, WPARAM vk)
{
    return PostMessageW(window,EFX_MOD_HOOK,PLAT_MOD_CTRL,0) &&
        PostMessageW(window,WM_KEYDOWN,vk,1) && PostMessageW(window,WM_KEYUP,vk,1) &&
        PostMessageW(window,EFX_MOD_HOOK,0,0);
}

/* ---- in-process editor -------------------------------------------------- */
/* NULL on the headless (null) backend: it has no platform and no window. */
static inline HWND efx_window(editor *e) { return e->has_platform ? plat_native_win32(&e->platform) : NULL; }

/* Native barrier (every message posted earlier has been dispatched), then
 * step until no input, render, or font work is pending. */
static inline int efx_settle(editor *e)
{
    uint64_t end=os_now_ns()+EFX_WATCH_NS;
    HWND window=efx_window(e);
    if(window) {
        uintptr_t token=(uintptr_t)GetPropW(window,L"SublimiteTestBarrier")+1u;
        EFX_REQ(PostMessageW(window,EFX_BARRIER_MESSAGE,token,0));
        while((uintptr_t)GetPropW(window,L"SublimiteTestBarrier")!=token) {
            EFX_REQ(editor_step(e,0)>=0); EFX_REQ(os_now_ns()<end); os_yield();
        }
    }
    do {
        int rc=editor_step(e,window ? 0 : EFX_HEADLESS_MS); EFX_REQ(rc>=0);
        editor_stats state=editor_get_stats(e);
        if(!state.pending && !state.render_active && !state.font_pending) return 0;
        os_yield();
    } while(os_now_ns()<end);
    editor_stats state=editor_get_stats(e);
    fprintf(stderr,"settle timeout pending=%d render_active=%d font_pending=%d\n",
        state.pending,state.render_active,state.font_pending);
    return efx_fail_hook ? (efx_fail_hook(),1) : 1;
}
/* Step until the editor has applied at least `target` mutations, then settle. */
static inline int efx_wait_mutation(editor *e, uint64_t target)
{
    uint64_t end=os_now_ns()+EFX_WATCH_NS;
    do {
        EFX_REQ(editor_step(e,0)>=0);
        if(editor_get_stats(e).mutations>=target) return efx_settle(e);
        os_yield();
    } while(os_now_ns()<end);
    fprintf(stderr,"mutation timeout target=%llu actual=%llu\n",
        (unsigned long long)target,(unsigned long long)editor_get_stats(e).mutations);
    return efx_fail_hook ? (efx_fail_hook(),1) : 1;
}
/* Types each ASCII char and waits for its mutation: a posted native WM_CHAR
 * when the editor has a window (D3D11 backend), else editor_inject of the
 * same key event (headless null backend, which has no window to post to). */
static inline int efx_type(editor *e, const char *ascii)
{
    HWND window=efx_window(e);
    for(;*ascii;ascii++) {
        uint64_t before=editor_get_stats(e).mutations;
        if(window) EFX_REQ(efx_post_char(window,(unsigned char)*ascii));
        else {
            plat_event key={.kind=PLAT_EV_KEY,.press=true,.keysym=(unsigned char)*ascii,
                .utf8_len=1,.utf8={(uint8_t)*ascii}};
            EFX_REQ(editor_inject(e,&key)==0);
        }
        EFX_REQ(efx_wait_mutation(e,before+1u)==0);
    }
    return 0;
}
/* Exact byte oracle over the whole document, independent of the view. */
static inline int efx_expect_bytes(const editor *e, const void *expected, size_t n)
{
    uint64_t length=editor_length(e);
    uint8_t *bytes=malloc(n ? n : 1u);
    int bad=length!=n || !bytes || (n && editor_read(e,0,bytes,n)!=0) || (n && memcmp(bytes,expected,n));
    if(bad) fprintf(stderr,"editor_fixture: bytes differ: length %llu, expected %zu\n",(unsigned long long)length,n);
    free(bytes);
    return bad ? efx_failed(__LINE__,"efx_expect_bytes") : 0;
}
/* Opens `cfg` on the headless null backend (no device, no display, NO window:
 * input goes through editor_inject, not posted messages). */
static inline int efx_open_null(editor **out, const editor_config *cfg, render_backend *backend)
{
    *backend=(render_backend){0};
    EFX_REQ(render_null_backend(backend)==RENDER_OK);
    EFX_REQ(editor_open(out,cfg,backend)==0);
    return 0;
}
/* SIMULATED completion: steps until every submitted frame has reached its
 * present (the null backend signals completion inside present). Real display
 * timing is neither produced nor claimed. */
static inline int efx_wait_presented(editor *e)
{
    uint64_t end=os_now_ns()+EFX_WATCH_NS;
    do {
        EFX_REQ(editor_step(e,0)>=0);
        editor_stats s=editor_get_stats(e);
        if(s.submitted_sequence && s.presented_sequence>=s.submitted_sequence && !s.render_active && !s.pending)
            return 0;
        os_yield();
    } while(os_now_ns()<end);
    return efx_failed(__LINE__,"efx_wait_presented timeout");
}

/* Opens `cfg` on the production D3D11 backend: a real (hidden, on a private
 * station occluded) window, so posted input and the native barrier work. */
static inline int efx_open_d3d11(editor **out, const editor_config *cfg, render_backend *backend)
{
    *backend=(render_backend){0};
    EFX_REQ(render_d3d11_backend(backend)==RENDER_OK);
    EFX_REQ(editor_open(out,cfg,backend)==0);
    EFX_REQ(efx_window(*out)!=NULL);
    return 0;
}
/* Every input so far has been composed and its frame submitted to the CPU
 * surface (occluded: no T4/T6, so NOT a displayed present). Window-backed
 * editors cannot be driven to a real present on a private station; this is
 * the strongest completion available there and is labelled as such. */
static inline int efx_wait_submitted(editor *e)
{
    EFX_REQ(efx_settle(e)==0);
    EFX_REQ(editor_get_stats(e).submitted_sequence!=0);
    return 0;
}

/* ---- child processes ---------------------------------------------------- */
/* Re-launches this exe with `mode` [and quoted `root`]; passes if it exits
 * with `expected` within `phases` x TEST_WIN_PROCESS_NS (timeout_diag.h, its derivation) (else it is terminated). */
static inline int efx_child_run(const char *mode, const char *root, unsigned phases, DWORD expected)
{
    WCHAR self[OS_PATH_MAX],command[3u*OS_PATH_MAX];
    EFX_REQ(GetModuleFileNameW(NULL,self,OS_PATH_MAX)>0);
    if(root) {
        WCHAR wide[OS_PATH_MAX];
        EFX_REQ(MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,root,-1,wide,OS_PATH_MAX)>0);
        EFX_REQ(swprintf(command,sizeof command/sizeof command[0],L"\"%ls\" %hs \"%ls\"",self,mode,wide)>0);
    } else EFX_REQ(swprintf(command,sizeof command/sizeof command[0],L"\"%ls\" %hs",self,mode)>0);
    STARTUPINFOW startup={.cb=sizeof startup,.dwFlags=STARTF_USESTDHANDLES,
        .hStdInput=GetStdHandle(STD_INPUT_HANDLE),.hStdOutput=GetStdHandle(STD_OUTPUT_HANDLE),
        .hStdError=GetStdHandle(STD_ERROR_HANDLE)};
    PROCESS_INFORMATION process={0};
    EFX_REQ(CreateProcessW(NULL,command,NULL,NULL,TRUE,0,NULL,NULL,&startup,&process));
    DWORD result=WaitForSingleObject(process.hProcess,(DWORD)(phases*TEST_WIN_PROCESS_NS/UINT64_C(1000000)));
    if(result!=WAIT_OBJECT_0) TerminateProcess(process.hProcess,1);
    DWORD code=0; BOOL got=GetExitCodeProcess(process.hProcess,&code);
    CloseHandle(process.hThread); CloseHandle(process.hProcess);
    EFX_REQ(got);
    if(result!=WAIT_OBJECT_0 || code!=expected)
        fprintf(stderr,"editor_fixture: child %s result=%lu exit=%lu expected=%lu\n",mode,result,code,expected);
    EFX_REQ(result==WAIT_OBJECT_0 && code==expected);
    return 0;
}

/* The top-level test window of a real sublimite.exe child, once it has
 * announced SublimiteTestReady (set by main.c under SUBLIMITE_TEST_INPUT). */
typedef struct efx_search { DWORD pid; HWND window; } efx_search;
static inline BOOL CALLBACK efx_find_window(HWND window, LPARAM arg)
{
    efx_search *s=(efx_search *)arg; DWORD pid=0;
    GetWindowThreadProcessId(window,&pid);
    if(pid==s->pid && !GetWindow(window,GW_OWNER) && GetPropW(window,L"SublimiteTestReady")) {
        s->window=window; return FALSE;
    }
    return TRUE;
}
static inline HWND efx_wait_window(DWORD pid)
{
    efx_search s={pid,NULL}; uint64_t end=os_now_ns()+TEST_WIN_PROCESS_NS;
    do { EnumWindows(efx_find_window,(LPARAM)&s); if(s.window) return s.window; Sleep(1); }
    while(os_now_ns()<end);
    return NULL;
}
static inline bool efx_contents(const char *path, const char *expected)
{
    os_file f=OS_FILE_INIT; char bytes[256]; size_t n=0;
    if(os_file_open(&f,path,OS_OPEN_READ)!=OS_OK) return false;
    os_err rc=os_file_read(&f,bytes,sizeof bytes,&n); os_file_close(&f);
    return rc==OS_OK && n==strlen(expected) && !memcmp(bytes,expected,n);
}
static inline bool efx_wait_contents(const char *path, const char *expected)
{
    uint64_t end=os_now_ns()+TEST_WIN_PROCESS_NS;
    do { if(efx_contents(path,expected)) return true; Sleep(1); } while(os_now_ns()<end);
    os_file f=OS_FILE_INIT; char bytes[256]; size_t count=0;
    if(os_file_open(&f,path,OS_OPEN_READ)==OS_OK) {
        (void)os_file_read(&f,bytes,sizeof bytes,&count); os_file_close(&f);
        fprintf(stderr,"editor_fixture: saved bytes [%.*s], expected [%s]\n",(int)count,bytes,expected);
    }
    return false;
}

/* ---- journal oracle ----------------------------------------------------- */
typedef struct efx_scan { bool seen; uint64_t paint; uint8_t marker; } efx_scan;
static inline int efx_scan_record(void *ctx, const journal_record *record)
{
    efx_scan *state=ctx;
    /* An insert record: 8-byte offset then the single marker byte. */
    if(record->type==JOURNAL_INSERT && record->size==9 && record->data[8]==state->marker) state->seen=true;
    if(record->type==JOURNAL_PAINT) state->paint=record->buffer_id;
    return 0;
}
/* True when the one-byte insert `marker` is durable in the journal file at
 * `path` and no first-paint guard was open after it (not a paint crash). */
static inline bool efx_journal_has_insert(const char *path, uint8_t marker)
{
    journal_replay_result result; efx_scan state={.marker=marker};
    (void)journal_scan_file(path,efx_scan_record,&state,&result);
    return state.seen && !state.paint;
}
/* Same over every session-* journal of a real sublimite.exe profile. */
static inline bool efx_profile_has_insert(const char *profile, uint8_t marker)
{
    char directory[OS_PATH_MAX]; snprintf(directory,sizeof directory,"%s\\data\\sublimite",profile);
    os_dir dir=OS_DIR_INIT; os_dir_iter it; bool seen=false;
    if(os_dir_open(&dir,directory)!=OS_OK) return false;
    if(os_dir_iter_open(&it,&dir)==OS_OK) {
        const char *name;
        while(os_dir_next(&it,&name)==OS_OK && name) if(!strncmp(name,"session-",8)) {
            char path[OS_PATH_MAX]; snprintf(path,sizeof path,"%s\\%s",directory,name);
            seen=seen || efx_journal_has_insert(path,marker);
        }
        os_dir_iter_close(&it);
    }
    os_dir_close(&dir); return seen;
}
static inline bool efx_wait_journal_insert(const char *path, uint8_t marker)
{
    uint64_t end=os_now_ns()+TEST_WIN_PROCESS_NS;
    do { if(efx_journal_has_insert(path,marker)) return true; Sleep(1); } while(os_now_ns()<end);
    return false;
}
static inline bool efx_wait_profile_insert(const char *profile, uint8_t marker)
{
    uint64_t end=os_now_ns()+TEST_WIN_PROCESS_NS;
    do { if(efx_profile_has_insert(profile,marker)) return true; Sleep(1); } while(os_now_ns()<end);
    return false;
}
#endif
