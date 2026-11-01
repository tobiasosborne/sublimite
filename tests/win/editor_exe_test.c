/* Real executable on the runner's private station. No SendInput/focus needed. */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "os/os.h"
#include "ipc/ipc.h"
#include "journal/journal.h"
#include <tlhelp32.h>
#include <wchar.h>
#include "x11/plat.h"
/* (G) existing editor settle watchdog, tests/editor_test.c. */
#define WATCH_NS UINT64_C(5000000000)
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"editor_exe:%d FAIL %s native=%lu\n",__LINE__,#x,GetLastError()); goto fail; } } while (0)
typedef struct search { DWORD pid; HWND window; } search;
static BOOL CALLBACK find_window(HWND window, LPARAM arg)
{
    search *s = (search *)arg; DWORD pid = 0;
    GetWindowThreadProcessId(window, &pid);
    if (pid == s->pid && !GetWindow(window,GW_OWNER) && GetPropW(window,L"SublimiteTestReady")) { s->window = window; return FALSE; }
    return TRUE;
}
static HWND wait_window(DWORD pid)
{
    search s = {pid,NULL}; uint64_t end = os_now_ns()+WATCH_NS;
    do { EnumWindows(find_window,(LPARAM)&s); if (s.window) return s.window; Sleep(1); } while (os_now_ns()<end);
    return NULL;
}
static bool contents(const char *path, const char *expected)
{
    os_file f = OS_FILE_INIT; char bytes[256]; size_t n=0;
    if (os_file_open(&f,path,OS_OPEN_READ)!=OS_OK) return false;
    os_err rc=os_file_read(&f,bytes,sizeof bytes,&n); os_file_close(&f);
    return rc==OS_OK && n==strlen(expected) && !memcmp(bytes,expected,n);
}
static bool wait_contents(const char *path, const char *expected)
{
    uint64_t end=os_now_ns()+WATCH_NS;
    do { if(contents(path,expected)) return true; Sleep(1); } while(os_now_ns()<end);
    os_file f=OS_FILE_INIT; char bytes[256]; size_t count=0;
    if(os_file_open(&f,path,OS_OPEN_READ)==OS_OK) {
        (void)os_file_read(&f,bytes,sizeof bytes,&count); os_file_close(&f);
        fprintf(stderr,"editor_exe: saved bytes [%.*s], expected [%s]\n",(int)count,bytes,expected);
    }
    return false;
}
static void text(HWND window)
{
    const WCHAR chars[]=L"typed \u00e4\u03bb\u4e2d\xd83d\xde42";
    for(size_t i=0;i<sizeof chars/sizeof chars[0]-1u;i++) PostMessageW(window,WM_CHAR,chars[i],1);
}
static void save(HWND window)
{
    /* Documented test-only modifier hook: posted messages do not update the
     * native thread keyboard state. Ordinary WM_KEYDOWN still owns the chord. */
    PostMessageW(window,WM_APP+0x51,PLAT_MOD_CTRL,0);
    PostMessageW(window,WM_KEYDOWN,'S',1);
    PostMessageW(window,WM_KEYUP,'S',1);
    PostMessageW(window,WM_APP+0x51,0,0);
}
typedef struct crash_state { bool seen; uint64_t paint; } crash_state;
static int saw_crash_edit(void *ctx, const journal_record *record)
{
    crash_state *state=ctx;
    if(record->type==JOURNAL_INSERT && record->size==9 && record->data[8]=='!') state->seen=true;
    if(record->type==JOURNAL_PAINT) state->paint=record->buffer_id;
    return 0;
}
static bool journal_has_crash_edit(const char *profile)
{
    char directory[OS_PATH_MAX]; snprintf(directory,sizeof directory,"%s\\data\\sublimite",profile);
    os_dir dir=OS_DIR_INIT; os_dir_iter it; bool seen=false;
    if(os_dir_open(&dir,directory)!=OS_OK) return false;
    if(os_dir_iter_open(&it,&dir)==OS_OK) {
        const char *name;
        while(os_dir_next(&it,&name)==OS_OK && name) if(!strncmp(name,"session-",8)) {
            char path[OS_PATH_MAX]; snprintf(path,sizeof path,"%s\\%s",directory,name);
            journal_replay_result result; crash_state state={0};
            (void)journal_scan_file(path,saw_crash_edit,&state,&result);
            seen=seen || (state.seen && !state.paint);
        }
        os_dir_iter_close(&it);
    }
    os_dir_close(&dir); return seen;
}
static bool wait_crash_edit(const char *root)
{
    uint64_t end=os_now_ns()+WATCH_NS;
    do { if(journal_has_crash_edit(root)) return true; Sleep(1); } while(os_now_ns()<end);
    return false;
}
static DWORD child_process(DWORD parent)
{
    HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);
    if(snapshot==INVALID_HANDLE_VALUE) return 0;
    PROCESSENTRY32W entry={.dwSize=sizeof entry}; DWORD found=0;
    if(Process32FirstW(snapshot,&entry)) do {
        if(entry.th32ParentProcessID==parent && !_wcsicmp(entry.szExeFile,L"sublimite.exe")) {
            found=entry.th32ProcessID; break;
        }
    } while(Process32NextW(snapshot,&entry));
    CloseHandle(snapshot); return found;
}
static void cleanup(const char *path)
{
    os_dir dir=OS_DIR_INIT; os_dir_iter it;
    if(os_dir_open(&dir,path)!=OS_OK) return;
    if(os_dir_iter_open(&it,&dir)==OS_OK) {
        const char *name;
        while(os_dir_next(&it,&name)==OS_OK && name) {
            char child[OS_PATH_MAX]; snprintf(child,sizeof child,"%s\\%s",path,name);
            os_stat st; if(os_stat_path(child,OS_STAT_NOFOLLOW,&st)==OS_OK && st.kind==OS_KIND_DIR) cleanup(child);
            else (void)os_unlink(child);
        }
        os_dir_iter_close(&it);
    }
    os_dir_close(&dir); (void)os_rmdir(path);
}
int main(void)
{
    char temp[OS_PATH_MAX],root[OS_PATH_MAX]={0},path[OS_PATH_MAX],self[OS_PATH_MAX],exe[OS_PATH_MAX];
    os_proc child={0}; bool running=false; int result=1; os_file file=OS_FILE_INIT; size_t done;
    CHECK(os_temp_dir(temp,sizeof temp)==OS_OK);
    unsigned random=0; CHECK(os_random(&random,sizeof random)==OS_OK);
    CHECK(snprintf(root,sizeof root,"%s\\sublimite-test-editor-%lu-%x",temp,GetCurrentProcessId(),random)>0);
    CHECK(os_mkdir(root)==OS_OK);
    CHECK(os_env_set("SUBLIMITE_PROFILE",root)==OS_OK);
    CHECK(os_env_set("SUBLIMITE_TEST_INPUT","1")==OS_OK);
    CHECK(snprintf(path,sizeof path,"%s\\file.txt",root)>0);
    CHECK(os_file_open(&file,path,OS_OPEN_WRITE|OS_OPEN_CREATE|OS_OPEN_EXCL)==OS_OK);
    CHECK(os_file_write(&file,"base\n",5,&done)==OS_OK && done==5); os_file_close(&file);
    CHECK(os_proc_self_path(self,sizeof self)==OS_OK);
    char *slash=strrchr(self,'\\'); CHECK(slash); *slash=0;
    CHECK(snprintf(exe,sizeof exe,"%s\\..\\sublimite.exe",self)>0);
    const char *args[]={exe,"--new-instance",path,NULL};
    CHECK(os_proc_spawn(&child,args)==OS_OK); running=true;
    HWND window=wait_window(child.id); CHECK(window);
    /* (G) exceed the existing D3D observation window (3 s), using the
     * editor's existing 5 s settle watchdog. The process must keep editing. */
    Sleep((DWORD)(WATCH_NS/UINT64_C(1000000)));
    text(window); save(window);
    CHECK(wait_contents(path,"typed äλ中🙂base\n"));
    CHECK(PostMessageW(window,WM_CLOSE,0,0));
    os_proc_status status;
    CHECK(os_proc_wait(&child,(int64_t)WATCH_NS,&status)==OS_OK); running=false;
    CHECK(status.how==OS_PROC_EXITED && status.code==0);
    puts("editor_exe: occluded argv file, posted Unicode, Ctrl+S, close exit=0 PASS");
    const char *help[]={exe,"--help",NULL};
    CHECK(os_proc_spawn(&child,help)==OS_OK); running=true;
    CHECK(os_proc_wait(&child,(int64_t)WATCH_NS,&status)==OS_OK); running=false;
    CHECK(status.how==OS_PROC_EXITED && status.code==0);
    const char *bad[]={exe,"--invalid-test-option",NULL};
    CHECK(os_proc_spawn(&child,bad)==OS_OK); running=true;
    CHECK(os_proc_wait(&child,(int64_t)WATCH_NS,&status)==OS_OK); running=false;
    CHECK(status.how==OS_PROC_EXITED && status.code==2);
    puts("editor_exe: GUI executable inherited usage/error streams and useful exit codes PASS");
    CHECK(os_proc_spawn(&child,args)==OS_OK); running=true;
    window=wait_window(child.id); CHECK(window);
    CHECK(PostMessageW(window,WM_CHAR,'!',1));
    CHECK(wait_crash_edit(root));
    CHECK(os_proc_kill(&child)==OS_OK);
    CHECK(os_proc_wait(&child,(int64_t)WATCH_NS,&status)==OS_OK); running=false;
    CHECK(status.how==OS_PROC_ABNORMAL);
    CHECK(os_proc_spawn(&child,args)==OS_OK); running=true;
    window=wait_window(child.id); CHECK(window); save(window);
    CHECK(wait_contents(path,"typed äλ中🙂!base\n"));
    CHECK(PostMessageW(window,WM_CLOSE,0,0));
    CHECK(os_proc_wait(&child,(int64_t)WATCH_NS,&status)==OS_OK); running=false;
    CHECK(status.how==OS_PROC_EXITED && status.code==0);
    puts("editor_exe: TerminateProcess mid-edit -> same-profile relaunch -> byte-exact recovery PASS");
    const char *wait_args[]={exe,"--wait","--new-instance",path,NULL};
    CHECK(os_proc_spawn(&child,wait_args)==OS_OK); running=true;
    uint64_t end=os_now_ns()+WATCH_NS; DWORD ui=0;
    do { ui=child_process(child.id); if(ui) break; Sleep(1); } while(os_now_ns()<end);
    CHECK(ui); window=wait_window(ui); CHECK(window);
    HANDLE ui_process=OpenProcess(SYNCHRONIZE,FALSE,ui); CHECK(ui_process);
    CHECK(PostMessageW(window,WM_CLOSE,0,0));
    CHECK(os_proc_wait(&child,(int64_t)WATCH_NS,&status)==OS_OK); running=false;
    CHECK(status.how==OS_PROC_EXITED && status.code==0);
    CHECK(WaitForSingleObject(ui_process,(DWORD)(WATCH_NS/UINT64_C(1000000)))==WAIT_OBJECT_0);
    CloseHandle(ui_process);
    puts("editor_exe: --wait launcher returns 0 after UI close; no leftover UI process PASS");
    result=0;
fail:
    os_file_close(&file);
    if(running) { os_proc_status killed; (void)os_proc_kill(&child); (void)os_proc_wait(&child,(int64_t)WATCH_NS,&killed); }
    if(*root) cleanup(root);
    return result;
}
