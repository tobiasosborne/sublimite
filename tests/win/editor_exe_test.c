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
#include "timeout_diag.h"
/* (G) Existing observation sleep exceeds the D3D 3 s observation window.
 * Native completion waits use the separately derived child guard. */
#define OBSERVE_NS UINT64_C(5000000000)
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
    search s = {pid,NULL}; uint64_t started=os_now_ns(), end = started+TEST_WIN_PROCESS_NS;
    do {
        EnumWindows(find_window,(LPARAM)&s);
        if(s.window) {
            fprintf(stderr,"editor_exe: ready pid=%lu elapsed_ms=%llu\n",pid,
                (unsigned long long)((os_now_ns()-started)/UINT64_C(1000000)));
            return s.window;
        }
        Sleep(1);
    } while(os_now_ns()<end);
    fprintf(stderr,"editor_exe: ready window timeout elapsed_ms=%llu pid=%lu\n",
        (unsigned long long)((os_now_ns()-started)/UINT64_C(1000000)),pid);
    test_timeout_process(pid);
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
    uint64_t started=os_now_ns(), end=started+TEST_WIN_PROCESS_NS;
    do {
        if(contents(path,expected)) {
            fprintf(stderr,"editor_exe: exact saved bytes elapsed_ms=%llu\n",
                (unsigned long long)((os_now_ns()-started)/UINT64_C(1000000)));
            return true;
        }
        Sleep(1);
    } while(os_now_ns()<end);
    os_file f=OS_FILE_INIT; char bytes[256]; size_t count=0;
    if(os_file_open(&f,path,OS_OPEN_READ)==OS_OK) {
        (void)os_file_read(&f,bytes,sizeof bytes,&count); os_file_close(&f);
        fprintf(stderr,"editor_exe: saved bytes [%.*s], expected [%s]\n",(int)count,bytes,expected);
    }
    return false;
}
static BOOL post_character(HWND window, WPARAM scalar)
{
    /* Posted text has no physical key state. Establish its intended modifiers
     * just as save() establishes Ctrl; never inherit state from native startup. */
    return PostMessageW(window,WM_APP+0x51,0,0) && PostMessageW(window,WM_CHAR,scalar,1);
}
static void text(HWND window)
{
    const WCHAR chars[]=L"typed \u00e4\u03bb\u4e2d\xd83d\xde42";
    for(size_t i=0;i<sizeof chars/sizeof chars[0]-1u;i++) post_character(window,chars[i]);
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
typedef struct crash_state { bool seen; uint64_t paint; uint8_t marker; } crash_state;
static int saw_crash_edit(void *ctx, const journal_record *record)
{
    crash_state *state=ctx;
    if(record->type==JOURNAL_INSERT && record->size==9 && record->data[8]==state->marker) state->seen=true;
    if(record->type==JOURNAL_PAINT) state->paint=record->buffer_id;
    return 0;
}
static bool journal_has_crash_edit(const char *profile, uint8_t marker)
{
    char directory[OS_PATH_MAX]; snprintf(directory,sizeof directory,"%s\\data\\sublimite",profile);
    os_dir dir=OS_DIR_INIT; os_dir_iter it; bool seen=false;
    if(os_dir_open(&dir,directory)!=OS_OK) return false;
    if(os_dir_iter_open(&it,&dir)==OS_OK) {
        const char *name;
        while(os_dir_next(&it,&name)==OS_OK && name) if(!strncmp(name,"session-",8)) {
            char path[OS_PATH_MAX]; snprintf(path,sizeof path,"%s\\%s",directory,name);
            journal_replay_result result; crash_state state={.marker=marker};
            (void)journal_scan_file(path,saw_crash_edit,&state,&result);
            if(state.seen && !state.paint) fprintf(stderr,"editor_exe: marker=%c records=%llu journal=%s\n",
                marker,(unsigned long long)result.records,path);
            seen=seen || (state.seen && !state.paint);
        }
        os_dir_iter_close(&it);
    }
    os_dir_close(&dir); return seen;
}
static int dump_record(void *ctx,const journal_record *record)
{
    (void)ctx;
    fprintf(stderr,"journal state seq=%llu type=%u buffer=%llu size=%zu",
        (unsigned long long)record->sequence,record->type,(unsigned long long)record->buffer_id,record->size);
    if(record->type==JOURNAL_INSERT && record->size>sizeof(uint64_t))
        fprintf(stderr," inserted=[%.*s]",(int)(record->size-sizeof(uint64_t)),record->data+sizeof(uint64_t));
    if(record->type==JOURNAL_DELETE && record->size==2u*sizeof(uint64_t)) {
        uint64_t offset=0,length=0; memcpy(&offset,record->data,sizeof offset); memcpy(&length,record->data+sizeof offset,sizeof length);
        fprintf(stderr," deleted_offset=%llu length=%llu",(unsigned long long)offset,(unsigned long long)length);
    }
    fputc('\n',stderr); return 0;
}
static void dump_journals(const char *path)
{
    os_dir dir=OS_DIR_INIT; os_dir_iter it;
    if(os_dir_open(&dir,path)!=OS_OK) return;
    if(os_dir_iter_open(&it,&dir)==OS_OK) {
        const char *name;
        while(os_dir_next(&it,&name)==OS_OK && name) {
            char child[OS_PATH_MAX]; snprintf(child,sizeof child,"%s\\%s",path,name);
            os_stat st;
            if(os_stat_path(child,OS_STAT_NOFOLLOW,&st)==OS_OK && st.kind==OS_KIND_DIR) dump_journals(child);
            else if(!strncmp(name,"session-",8)) {
                fprintf(stderr,"journal state file=%s\n",child); journal_replay_result replay;
                int rc=journal_scan_file(child,dump_record,NULL,&replay);
                fprintf(stderr,"journal state scan=%d records=%llu corrupt=%d\n",rc,(unsigned long long)replay.records,replay.corrupt);
            }
        }
        os_dir_iter_close(&it);
    }
    os_dir_close(&dir);
}
static bool wait_crash_edit(const char *root, uint8_t marker)
{
    uint64_t end=os_now_ns()+TEST_WIN_PROCESS_NS;
    do { if(journal_has_crash_edit(root,marker)) return true; Sleep(1); } while(os_now_ns()<end);
    return false;
}
static bool kill_committed_edit(os_proc *child,HWND window,const char *root,uint8_t marker)
{
    DWORD pid=0,tid=GetWindowThreadProcessId(window,&pid);
    if(!tid || pid!=(DWORD)child->id) return false;
    HANDLE thread=OpenThread(THREAD_SUSPEND_RESUME|THREAD_GET_CONTEXT,FALSE,tid);
    if(!thread) return false;
    uint64_t end=os_now_ns()+TEST_WIN_PROCESS_NS;
    bool killed=false;
    do {
        if(!journal_has_crash_edit(root,marker)) { Sleep(1); continue; }
        /* The scan and TerminateProcess are separate operations. PAINT can
         * arm between them; that death intentionally restores the tab closed.
         * Freeze only our child's UI, synchronize the suspension with context
         * capture, then recheck the exact edit/no-PAINT condition before kill.
         * Workers can finish I/O, but cannot begin a new UI paint boundary. */
        if(SuspendThread(thread)==(DWORD)-1) break;
        CONTEXT context={.ContextFlags=CONTEXT_CONTROL};
        bool stopped=GetThreadContext(thread,&context)!=0;
        bool safe=stopped && journal_has_crash_edit(root,marker);
        if(stopped && !safe) fprintf(stderr,"editor_exe: kill boundary raced PAINT marker=%c; resume/retry\n",marker);
        if(safe) killed=os_proc_kill(child)==OS_OK;
        if(!killed) (void)ResumeThread(thread);
        if(killed || !stopped) break;
    } while(os_now_ns()<end);
    CloseHandle(thread); return killed;
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
/* Native child owns the inherited pipe endpoints. Capture both usage paths
 * so a missing output call fails even when the process status is correct. */
static bool captured_usage(const char *exe, const WCHAR *option, DWORD expected)
{
    SECURITY_ATTRIBUTES sa={sizeof sa,NULL,TRUE}; HANDLE read_pipe=NULL,write_pipe=NULL;
    if(!CreatePipe(&read_pipe,&write_pipe,&sa,0)) return false;
    SetHandleInformation(read_pipe,HANDLE_FLAG_INHERIT,0);
    WCHAR wide[OS_PATH_MAX],command[OS_PATH_MAX+128];
    bool ok=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,exe,-1,wide,OS_PATH_MAX)>0;
    if(ok) ok=swprintf(command,sizeof command/sizeof command[0],L"\"%ls\" %ls",wide,option)>0;
    STARTUPINFOW si={.cb=sizeof si,.dwFlags=STARTF_USESTDHANDLES,.hStdInput=GetStdHandle(STD_INPUT_HANDLE),
        .hStdOutput=write_pipe,.hStdError=write_pipe}; PROCESS_INFORMATION pi={0};
    if(ok) ok=CreateProcessW(NULL,command,NULL,NULL,TRUE,0,NULL,NULL,&si,&pi)!=0;
    CloseHandle(write_pipe);
    char text[4096]={0}; DWORD bytes=0,code=0;
    if(ok) {
        ok=WaitForSingleObject(pi.hProcess,(DWORD)(TEST_WIN_PROCESS_NS/UINT64_C(1000000)))==WAIT_OBJECT_0;
        if(!ok) TerminateProcess(pi.hProcess,1);
        else ok=GetExitCodeProcess(pi.hProcess,&code) && code==expected &&
            ReadFile(read_pipe,text,sizeof text-1u,&bytes,NULL) && bytes>0 &&
            strstr(text,"usage: sublimite ") && strstr(text,"--wait") && strstr(text,"[path[:line[:col]]");
        CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
    }
    CloseHandle(read_pipe); return ok;
}
int main(int argc, char **argv)
{
    setvbuf(stdout,NULL,_IONBF,0);
    char temp[OS_PATH_MAX],root[OS_PATH_MAX]={0},path[OS_PATH_MAX],self[OS_PATH_MAX],exe[OS_PATH_MAX];
    os_proc child={0},other={0}; bool running=false; int result=1; os_file file=OS_FILE_INIT; size_t done;
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
    if(argc>=2 && !strcmp(argv[1],"--streams")) {
        const char *target=argc==3 ? argv[2] : exe;
        CHECK(captured_usage(target,L"--help",0) && captured_usage(target,L"--invalid-test-option",2));
        puts("editor_exe: captured help/error contents PASS"); result=0; goto fail;
    }
    const char *args[]={exe,path,NULL};
    CHECK(os_proc_spawn(&child,args)==OS_OK); running=true;
    HWND window=wait_window(child.id); CHECK(window);
    if(argc==2 && !strcmp(argv[1],"--input-state")) {
        (void)ShowWindow(window,SW_MINIMIZE); CHECK(IsIconic(window));
        CHECK(PostMessageW(window,WM_APP+0x51,PLAT_MOD_CTRL,0));
        text(window); save(window);
        CHECK(wait_contents(path,"typed äλ中🙂base\n"));
        CHECK(PostMessageW(window,WM_CLOSE,0,0)); os_proc_status input_status;
        CHECK(os_proc_wait(&child,(int64_t)TEST_WIN_PROCESS_NS,&input_status)==OS_OK); running=false;
        CHECK(input_status.how==OS_PROC_EXITED && input_status.code==0);
        puts("editor_exe: posted text resets inherited modifier state PASS"); result=0; goto fail;
    }
    char profile2[OS_PATH_MAX]; snprintf(profile2,sizeof profile2,"%s\\other-profile",root);
    CHECK(os_mkdir(profile2)==OS_OK && os_env_set("SUBLIMITE_PROFILE",profile2)==OS_OK);
    char other_path[OS_PATH_MAX]; snprintf(other_path,sizeof other_path,"%s\\other.txt",profile2);
    CHECK(os_file_open(&file,other_path,OS_OPEN_WRITE|OS_OPEN_CREATE|OS_OPEN_EXCL)==OS_OK);
    CHECK(os_file_write(&file,"other\n",6,&done)==OS_OK && done==6); os_file_close(&file);
    const char *other_args[]={exe,other_path,NULL};
    CHECK(os_proc_spawn(&other,other_args)==OS_OK);
    puts("editor_exe: other profile startup");
    HWND other_window=wait_window(other.id); CHECK(other_window);
    (void)ShowWindow(other_window,SW_MINIMIZE); CHECK(IsIconic(other_window));
    CHECK(post_character(other_window,'@')); CHECK(wait_crash_edit(profile2,'@'));
    CHECK(kill_committed_edit(&other,other_window,profile2,'@'));
    os_proc_status other_status; CHECK(os_proc_wait(&other,(int64_t)TEST_WIN_PROCESS_NS,&other_status)==OS_OK);
    CHECK(other_status.how==OS_PROC_ABNORMAL);
    CHECK(os_proc_spawn(&other,other_args)==OS_OK); other_window=wait_window(other.id); CHECK(other_window);
    (void)ShowWindow(other_window,SW_MINIMIZE); CHECK(IsIconic(other_window));
    CHECK(post_character(other_window,'$')); CHECK(wait_crash_edit(profile2,'$'));
    PostMessageW(other_window,WM_APP+0x51,PLAT_MOD_CTRL,0);
    PostMessageW(other_window,WM_KEYDOWN,'Z',1); PostMessageW(other_window,WM_KEYUP,'Z',1);
    PostMessageW(other_window,WM_APP+0x51,0,0); save(other_window);
    CHECK(wait_contents(other_path,"@other\n") && contents(path,"base\n"));
    CHECK(PostMessageW(other_window,WM_CLOSE,0,0));
    CHECK(os_proc_wait(&other,(int64_t)TEST_WIN_PROCESS_NS,&other_status)==OS_OK);
    CHECK(other_status.how==OS_PROC_EXITED && other_status.code==0);
    CHECK(os_env_set("SUBLIMITE_PROFILE",path)==OS_OK); /* file, invalid profile */
    CHECK(os_proc_spawn(&other,args)==OS_OK);
    CHECK(os_proc_wait(&other,(int64_t)TEST_WIN_PROCESS_NS,&other_status)==OS_OK);
    CHECK(other_status.how==OS_PROC_EXITED && other_status.code!=0);
    CHECK(os_env_set("SUBLIMITE_PROFILE",root)==OS_OK);
    puts("editor_exe: distinct profiles do not hand off; invalid profile fails before IPC PASS");
    /* (G) exceed the existing D3D observation window (3 s), using the
     * editor's existing 5 s settle watchdog. The process must keep editing. */
    Sleep((DWORD)(OBSERVE_NS/UINT64_C(1000000)));
    /* Negative-control state is deliberate: text() must clear it. */
    CHECK(PostMessageW(window,WM_APP+0x51,PLAT_MOD_CTRL,0));
    text(window); save(window);
    CHECK(wait_contents(path,"typed äλ中🙂base\n"));
    CHECK(PostMessageW(window,WM_CLOSE,0,0));
    os_proc_status status;
    CHECK(os_proc_wait(&child,(int64_t)TEST_WIN_PROCESS_NS,&status)==OS_OK); running=false;
    CHECK(status.how==OS_PROC_EXITED && status.code==0);
    puts("editor_exe: occluded argv file, posted Unicode, Ctrl+S, close exit=0 PASS");
    CHECK(captured_usage(exe,L"--help",0));
    CHECK(captured_usage(exe,L"--invalid-test-option",2));
    const char *help[]={exe,"--help",NULL};
    CHECK(os_proc_spawn(&child,help)==OS_OK); running=true;
    CHECK(os_proc_wait(&child,(int64_t)TEST_WIN_PROCESS_NS,&status)==OS_OK); running=false;
    CHECK(status.how==OS_PROC_EXITED && status.code==0);
    const char *bad[]={exe,"--invalid-test-option",NULL};
    CHECK(os_proc_spawn(&child,bad)==OS_OK); running=true;
    CHECK(os_proc_wait(&child,(int64_t)TEST_WIN_PROCESS_NS,&status)==OS_OK); running=false;
    CHECK(status.how==OS_PROC_EXITED && status.code==2);
    puts("editor_exe: GUI executable inherited usage/error streams and useful exit codes PASS");
    CHECK(os_proc_spawn(&child,args)==OS_OK); running=true;
    window=wait_window(child.id); CHECK(window);
    /* Private station is covered, never minimized: a parked undrawn first
     * frame must not classify this healthy recovered tab as a paint crash. */
    CHECK(!IsIconic(window));
    CHECK(post_character(window,'!'));
    CHECK(wait_crash_edit(root,'!'));
    CHECK(kill_committed_edit(&child,window,root,'!'));
    CHECK(os_proc_wait(&child,(int64_t)TEST_WIN_PROCESS_NS,&status)==OS_OK); running=false;
    CHECK(status.how==OS_PROC_ABNORMAL);
    CHECK(os_proc_spawn(&child,args)==OS_OK); running=true;
    window=wait_window(child.id); CHECK(window);
    /* A new marker cannot be found in the old source journal. Its protected
     * admission proves the restored tab is editable before undo/save. */
    (void)ShowWindow(window,SW_MINIMIZE); CHECK(IsIconic(window));
    CHECK(post_character(window,'?')); CHECK(wait_crash_edit(root,'?'));
    PostMessageW(window,WM_APP+0x51,PLAT_MOD_CTRL,0);
    PostMessageW(window,WM_KEYDOWN,'Z',1); PostMessageW(window,WM_KEYUP,'Z',1);
    PostMessageW(window,WM_APP+0x51,0,0); save(window);
    CHECK(wait_contents(path,"typed äλ中🙂!base\n"));
    CHECK(PostMessageW(window,WM_CLOSE,0,0));
    CHECK(os_proc_wait(&child,(int64_t)TEST_WIN_PROCESS_NS,&status)==OS_OK); running=false;
    CHECK(status.how==OS_PROC_EXITED && status.code==0);
    puts("editor_exe: TerminateProcess mid-edit -> same-profile relaunch -> byte-exact recovery PASS");
    const char *wait_args[]={exe,"--wait","--new-instance",path,NULL};
    CHECK(os_proc_spawn(&child,wait_args)==OS_OK); running=true;
    uint64_t end=os_now_ns()+TEST_WIN_PROCESS_NS; DWORD ui=0;
    do { ui=child_process(child.id); if(ui) break; Sleep(1); } while(os_now_ns()<end);
    CHECK(ui); window=wait_window(ui); CHECK(window);
    HANDLE ui_process=OpenProcess(SYNCHRONIZE,FALSE,ui); CHECK(ui_process);
    CHECK(PostMessageW(window,WM_CLOSE,0,0));
    CHECK(os_proc_wait(&child,(int64_t)TEST_WIN_PROCESS_NS,&status)==OS_OK); running=false;
    CHECK(status.how==OS_PROC_EXITED && status.code==0);
    CHECK(WaitForSingleObject(ui_process,(DWORD)(TEST_WIN_PROCESS_NS/UINT64_C(1000000)))==WAIT_OBJECT_0);
    CloseHandle(ui_process);
    puts("editor_exe: --wait launcher returns 0 after UI close; no leftover UI process PASS");
    result=0;
fail:
    if(result && *root) dump_journals(root);
    if(result && other.process) test_timeout_process((DWORD)other.id);
    if(result && running) test_timeout_process((DWORD)child.id);
    os_file_close(&file);
    if(running) { os_proc_status killed; (void)os_proc_kill(&child); (void)os_proc_wait(&child,(int64_t)TEST_WIN_PROCESS_NS,&killed); }
    if(*root) cleanup(root);
    return result;
}
