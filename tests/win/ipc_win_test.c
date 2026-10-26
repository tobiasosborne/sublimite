#if defined(_WIN32)
#include "ipc/ipc.h"
#include "os/os.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <io.h>
#include <fcntl.h>
#include <windows.h>
#include <limits.h>
_CRTIMP _invalid_parameter_handler __cdecl _set_thread_local_invalid_parameter_handler(_invalid_parameter_handler handler);
static void __cdecl invalid_fatal(const wchar_t *expression, const wchar_t *function,
    const wchar_t *file_name, unsigned line, uintptr_t reserved)
{
    (void)expression; (void)function; (void)file_name; (void)line; (void)reserved;
    fputs("ipc_win invalid descriptor reached caller's fatal CRT handler\n",stderr); ExitProcess(1);
}

#define CHECK(x) do { if(!(x)) { fprintf(stderr,"ipc_win_test:%d: %s\n",__LINE__,#x); exit(1); } } while(0)
/* (G) Test bounds reuse the transport's existing request deadline; nanoseconds
 * per millisecond are a unit conversion (P). */
#define DEADLINE_NS ((uint64_t)IPC_REQUEST_DEADLINE_MS * UINT64_C(1000000))
typedef struct capture { size_t calls; ipc_token token; bool wait, reject, immediate; ipc_server *server; } capture;
typedef struct launcher { int fd; ipc_result result; os_wake done; } launcher;
static void launcher_thread(void *ctx)
{
    launcher *l=ctx; l->result=ipc_launcher_wait(l->fd); os_wake_signal(&l->done);
}
static void launcher_case(bool closed)
{
    ipc_server s={0}; CHECK(ipc_server_init_isolated(&s)==IPC_OK);
    int fds[2]; CHECK(ipc_launcher_pair(fds)==IPC_OK);
    ipc_token token=0; CHECK(ipc_server_adopt_wait(&s,fds[0],&token)==IPC_OK);
    CHECK(ipc_server_token_live(&s,token));
    launcher l={.fd=fds[1],.result=IPC_INVALID}; CHECK(os_wake_init(&l.done)==OS_OK);
    os_thread thread; CHECK(os_thread_start(&thread,launcher_thread,&l)==OS_OK);
    if(closed) CHECK(ipc_server_report_closed(&s,token)==IPC_OK);
    else ipc_server_fini(&s);
    CHECK(os_wake_wait(&l.done,(int64_t)DEADLINE_NS)==OS_OK);
    CHECK(os_thread_join(&thread)==OS_OK && l.result==(closed?IPC_OK:IPC_IO));
    if(closed) ipc_server_fini(&s);
    os_wake_close(&l.done);
    printf("ipc_win launcher %s: ok\n",closed?"adopt/close":"server death");
}
static ipc_result opened(const ipc_request *r, ipc_token token, void *ctx)
{
    capture *c=ctx;
    CHECK(r->count==1 && r->paths[0].line==12 && r->paths[0].col==3);
    CHECK(strstr(r->paths[0].path,"sublimite-ipc-new-file")!=NULL);
    CHECK(os_path_is_absolute(r->paths[0].path) && os_path_is_absolute(r->cwd));
    CHECK(r->has_stdin && r->stdin_size==5 && !memcmp(r->stdin_data,"a\0b\nc",5));
    CHECK(r->wait==c->wait); c->calls++; c->token=token;
    if(c->immediate) CHECK(ipc_server_report_closed(c->server,token)==IPC_OK);
    return c->reject ? IPC_REJECTED : IPC_OK;
}
static os_proc spawn(const char *role, const char *ns)
{
    char self[OS_PATH_MAX]; CHECK(os_proc_self_path(self,sizeof self)==OS_OK);
    const char *av[]={self,role,ns,NULL}; os_proc p;
    CHECK(os_proc_spawn(&p,av)==OS_OK); return p;
}
static void binary_stdin(void)
{
    char *av[]={"sublimite","-"}; ipc_args a={0};
    CHECK(ipc_parse_args(2,av,&a)==IPC_OK);
    int fds[2]; CHECK(_pipe(fds,IPC_RX_SMALL_SIZE,_O_BINARY)==0);
    const uint8_t data[]={'a','\r','\n',0x1a,'z'}; /* (P) CRLF and CRT text EOF. */
    CHECK(_write(fds[1],data,sizeof data)==sizeof data); CHECK(_close(fds[1])==0);
    CHECK(_setmode(fds[0],_O_TEXT)>=0);
    CHECK(ipc_args_read_stdin(&a,fds[0])==IPC_OK);
    CHECK(a.request.stdin_size==sizeof data && !memcmp(a.request.stdin_data,data,sizeof data));
    CHECK(_setmode(fds[0],_O_BINARY)==_O_TEXT); CHECK(_close(fds[0])==0);
    ipc_args_fini(&a); puts("ipc_win binary stdin/CRT mode restoration: ok");
}
static void reap(os_proc *p)
{
    os_proc_status st;
    CHECK(os_proc_wait(p,(int64_t)(DEADLINE_NS*2u),&st)==OS_OK);
    CHECK(st.how==OS_PROC_EXITED && st.code==0);
}
static void pump(ipc_server *s, capture *c)
{
    uint64_t end=os_now_ns()+DEADLINE_NS*2u;
    while(!c->calls && os_now_ns()<end) {
        (void)os_wake_wait(ipc_server_wake(s),(int64_t)(IPC_LOCK_TIMEOUT_MS*UINT64_C(1000000)));
        CHECK(ipc_server_drain(s,opened,c)==IPC_OK);
    }
    CHECK(c->calls==1);
}
static int child(const char *role)
{
    if(!strncmp(role,"--invalid-",10)) {
        _invalid_parameter_handler previous=_set_thread_local_invalid_parameter_handler(invalid_fatal);
        int fd=!strstr(role,"range")?-1:INT_MAX;
        if(strstr(role,"closed")) { fd=_dup(1); CHECK(fd>=0 && _close(fd)==0); }
        if(strstr(role,"stdin")) {
            char *av[]={"sublimite","-"}; ipc_args a={0}; CHECK(ipc_parse_args(2,av,&a)==IPC_OK);
            CHECK(ipc_args_read_stdin(&a,fd)==IPC_IO); ipc_args_fini(&a);
        } else if(strstr(role,"adopt")) {
            ipc_server s={0}; CHECK(ipc_server_init_isolated(&s)==IPC_OK); ipc_token token;
            CHECK(ipc_server_adopt_wait(&s,fd,&token)==IPC_INVALID); ipc_server_fini(&s);
        } else CHECK(ipc_launcher_wait(fd)==IPC_INVALID);
        CHECK(_set_thread_local_invalid_parameter_handler(previous)==invalid_fatal);
        printf("ipc_win %s normal error return/handler restored: ok\n",role); return 0;
    }
    if(!strcmp(role,"--refuse")) {
        ipc_server s={0}; CHECK(ipc_server_init(&s,NULL)==IPC_EXISTS); return 0;
    }
    char path[OS_PATH_MAX], input[OS_PATH_MAX]; CHECK(os_temp_dir(path,sizeof path)==OS_OK);
    CHECK(snprintf(input,sizeof input,"%s/sublimite-ipc-new-file:12:3",path)>0);
    bool wait=!strcmp(role,"--wait") || !strcmp(role,"--immediate");
    char *av[]={"sublimite",input,"-",wait?"--wait":"--",NULL}; ipc_args a={0};
    CHECK(ipc_parse_args(4,av,&a)==IPC_OK);
    a.request.stdin_data=(const uint8_t *)"a\0b\nc"; a.request.stdin_size=5;
    ipc_result want=!strcmp(role,"--absent")?IPC_IO:!strcmp(role,"--reject")?IPC_REJECTED:
        !strcmp(role,"--timeout")?IPC_TIMEOUT:IPC_OK;
    CHECK(ipc_client_send(NULL,&a.request,IPC_LOCK_TIMEOUT_MS)==want);
    ipc_args_fini(&a); return 0;
}
static void roundtrip(const char *ns, const char *role)
{
    ipc_server s={0}; CHECK(ipc_server_init(&s,NULL)==IPC_OK);
    capture c={.wait=!strcmp(role,"--wait") || !strcmp(role,"--immediate"),
        .reject=!strcmp(role,"--reject"),.immediate=!strcmp(role,"--immediate"),.server=&s};
    os_proc p=spawn(role,ns); pump(&s,&c);
    if(c.wait && !c.immediate) {
        CHECK(ipc_server_token_live(&s,c.token));
        os_proc_status st;
        /* (G) Wait beyond the client's ACK timeout by the existing eviction
         * interval. Once accepted, --wait has no deadline. */
        CHECK(os_proc_wait(&p,(int64_t)((IPC_LOCK_TIMEOUT_MS+IPC_EVICT_IDLE_MS)*UINT64_C(1000000)),&st)==OS_E_TIMEDOUT);
        CHECK(ipc_server_report_closed(&s,c.token)==IPC_OK);
        CHECK(!ipc_server_token_live(&s,c.token));
    }
    reap(&p); ipc_server_fini(&s);
    printf("ipc_win %s open/line/column/stdin: ok\n",role);
}
static int launcher_process(const char *self, const char *mode)
{
    char endpoint[IPC_PATH_CAP]; int fd=-1;
    CHECK(ipc_launcher_prepare(&fd,endpoint,sizeof endpoint)==IPC_OK);
    char done_name[IPC_PATH_CAP]; WCHAR wide[IPC_PATH_CAP];
    CHECK(snprintf(done_name,sizeof done_name,"Local\\sublimite-test-ui-%llu",(unsigned long long)os_proc_id())>0);
    CHECK(MultiByteToWideChar(CP_UTF8,0,done_name,-1,wide,IPC_PATH_CAP));
    HANDLE done=CreateEventW(NULL,TRUE,FALSE,wide); CHECK(done);
    const char *av[]={self,"--launcher-ui",mode,endpoint,done_name,NULL}; os_proc ui;
    CHECK(os_proc_spawn(&ui,av)==OS_OK);
    ipc_result result=ipc_launcher_wait(fd);
    CHECK(result==(!strcmp(mode,"death")?IPC_IO:IPC_OK));
    CHECK(SetEvent(done)); reap(&ui); CloseHandle(done); return 0;
}
static void launcher_process_cases(void)
{
    char self[OS_PATH_MAX]; CHECK(os_proc_self_path(self,sizeof self)==OS_OK);
    const char *modes[]={"primary","isolated","death"};
    for(size_t i=0;i<sizeof modes/sizeof modes[0];i++) {
        const char *av[]={self,"--launcher-process",modes[i],NULL}; os_proc launcher_proc;
        CHECK(os_proc_spawn(&launcher_proc,av)==OS_OK); reap(&launcher_proc);
        printf("ipc_win git-style launcher/UI %s over argv endpoint: ok\n",modes[i]);
    }
}
static void invalid_descriptors(const char *ns)
{
    const char *roles[]={"--invalid-stdin-negative","--invalid-stdin-closed","--invalid-stdin-range",
        "--invalid-adopt-negative","--invalid-adopt-closed","--invalid-adopt-range",
        "--invalid-wait-negative","--invalid-wait-closed","--invalid-wait-range"};
    for(size_t i=0;i<sizeof roles/sizeof roles[0];i++) { os_proc p=spawn(roles[i],ns); reap(&p); }
}
int main(int argc, char **argv)
{
    char ns[80];
    if(argc==5 && !strcmp(argv[1],"--launcher-ui")) {
        ipc_server ui={0};
        CHECK((!strcmp(argv[2],"primary")?ipc_server_init(&ui,"launcher-process"):ipc_server_init_isolated(&ui))==IPC_OK);
        ipc_token token=0; CHECK(ipc_server_connect_wait(&ui,argv[3],&token)==IPC_OK);
        CHECK(ipc_server_token_live(&ui,token));
        if(!strcmp(argv[2],"death")) { ipc_server_fini(&ui); return 0; }
        CHECK(ipc_server_report_closed(&ui,token)==IPC_OK);
        WCHAR wide[IPC_PATH_CAP];
        CHECK(MultiByteToWideChar(CP_UTF8,0,argv[4],-1,wide,IPC_PATH_CAP));
        HANDLE done=OpenEventW(SYNCHRONIZE,FALSE,wide); CHECK(done);
        CHECK(WaitForSingleObject(done,IPC_REQUEST_DEADLINE_MS)==WAIT_OBJECT_0); CloseHandle(done);
        ipc_server_fini(&ui); return 0;
    }
    if(argc==3 && !strcmp(argv[1],"--launcher-process")) {
        char self[OS_PATH_MAX]; CHECK(os_proc_self_path(self,sizeof self)==OS_OK);
        return launcher_process(self,argv[2]);
    }
    if(argc==2 && !strcmp(argv[1],"--launcher-cases")) { launcher_process_cases(); return 0; }
    if(argc==3) {
        CHECK(os_env_set("EDIT_IPC_NAMESPACE",argv[2])==OS_OK);
        return child(argv[1]);
    }
    CHECK(snprintf(ns,sizeof ns,"test-%llu",(unsigned long long)os_proc_id())>0);
    CHECK(os_env_set("EDIT_IPC_NAMESPACE",ns)==OS_OK);
    if(argc==2 && !strcmp(argv[1],"--invalid-cases")) { invalid_descriptors(ns); return 0; }
    invalid_descriptors(ns);
    binary_stdin();
    os_proc absent=spawn("--absent",ns); reap(&absent); puts("ipc_win no server: IPC_IO ok");
    ipc_server s={0}; CHECK(ipc_server_init(&s,NULL)==IPC_OK);
    ipc_server duplicate={0}; CHECK(ipc_server_init(&duplicate,NULL)==IPC_EXISTS);
    os_proc second=spawn("--refuse",ns); reap(&second);
    puts("ipc_win second server same/second process: IPC_EXISTS ok");
    ipc_server_fini(&s);
    roundtrip(ns,"--open"); roundtrip(ns,"--wait"); roundtrip(ns,"--immediate"); roundtrip(ns,"--reject");
    CHECK(ipc_server_init(&s,NULL)==IPC_OK);
    os_proc timeout=spawn("--timeout",ns); reap(&timeout);
    ipc_server_fini(&s); puts("ipc_win ACK deadline without drain/cancel cleanup: IPC_TIMEOUT ok");
    CHECK(ipc_server_init(&s,NULL)==IPC_OK); ipc_server_fini(&s);
    puts("ipc_win endpoint reuse after close: ok");
    launcher_case(true); launcher_case(false);
    launcher_process_cases();
    puts("ipc_win launcher integration: SKIP requires Windows main/platform (W2.4)");
    puts("ipc_win_test: ok"); return 0;
}
#endif
