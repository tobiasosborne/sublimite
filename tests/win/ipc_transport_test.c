#if defined(_WIN32)
/* Exercise the platform's actual overlapped transport with raw peers. As in
 * os_win_test, including the implementation exposes only test-local helpers;
 * the production object has no shims or test hooks. */
#include "ipc/ipc.h"
static _Thread_local size_t fail_allocation, allocation;
static void *ipc_test_arena_alloc(edit_arena *arena, size_t n, size_t align)
{
    if(fail_allocation && ++allocation==fail_allocation) return NULL;
    return edit_arena_alloc(arena,n,align);
}
#define edit_arena_alloc ipc_test_arena_alloc
#include <windows.h>
static bool inspect_security, inspect_sqos;
static void inspect_pipe(HANDLE pipe, LPCWSTR name);
static HANDLE test_open_pipe(LPCWSTR name, DWORD access, DWORD share, LPSECURITY_ATTRIBUTES sa,
    DWORD disposition, DWORD flags, HANDLE template_file)
{
    if(inspect_sqos && !wcsncmp(name,L"\\\\.\\pipe\\",9) &&
       (flags&(SECURITY_SQOS_PRESENT|SECURITY_IDENTIFICATION|SECURITY_IMPERSONATION))!=(SECURITY_SQOS_PRESENT|SECURITY_IDENTIFICATION)) {
        fprintf(stderr,"ipc_win client SQOS flags missing: 0x%lx\n",flags); ExitProcess(1);
    }
    return CreateFileW(name,access,share,sa,disposition,flags,template_file);
}
static HANDLE startup_enter, startup_release;
static HANDLE test_create_pipe(LPCWSTR name, DWORD access, DWORD mode, DWORD instances,
    DWORD outsize, DWORD insize, DWORD timeout, LPSECURITY_ATTRIBUTES security)
{
    if(startup_enter) {
        HANDLE enter=startup_enter; startup_enter=NULL;
        if(!SetEvent(enter) || WaitForSingleObject(startup_release,INFINITE)!=WAIT_OBJECT_0)
            ExitProcess(1);
    }
    HANDLE pipe=CreateNamedPipeW(name,access,mode,instances,outsize,insize,timeout,security);
    if(inspect_security && pipe!=INVALID_HANDLE_VALUE) inspect_pipe(pipe,name);
    return pipe;
}
#define CreateNamedPipeW test_create_pipe
#define CreateFileW test_open_pipe
#include "../../src/ipc/ipc_win.c"
#undef edit_arena_alloc
#undef CreateNamedPipeW
#undef CreateFileW
#include <stdlib.h>
#include <aclapi.h>
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"ipc_transport_test:%d: %s\n",__LINE__,#x); exit(1); } } while(0)
#define TEST_NS ((uint64_t)IPC_REQUEST_DEADLINE_MS*UINT64_C(1000000))
typedef struct capture { size_t calls, input_size; ipc_token token; bool wait; } capture;
static void inspect_pipe(HANDLE pipe, LPCWSTR name)
{
    PSECURITY_DESCRIPTOR sd=NULL; PACL acl=NULL; PSID owner=NULL;
    CHECK(GetSecurityInfo(pipe,SE_KERNEL_OBJECT,DACL_SECURITY_INFORMATION|OWNER_SECURITY_INFORMATION,
        &owner,NULL,&acl,NULL,&sd)==ERROR_SUCCESS);
    SECURITY_DESCRIPTOR_CONTROL control; DWORD revision;
    CHECK(GetSecurityDescriptorControl(sd,&control,&revision) && (control&SE_DACL_PRESENT));
    CHECK(acl && acl->AceCount==1);
    ACCESS_ALLOWED_ACE *ace=NULL; CHECK(GetAce(acl,0,(void **)&ace));
    BYTE sid[SECURITY_MAX_SID_SIZE]; CHECK(current_sid(sid));
    CHECK(ace->Header.AceType==ACCESS_ALLOWED_ACE_TYPE && EqualSid(&ace->SidStart,sid));
    LocalFree(sd);
    HANDLE token, restricted; CHECK(OpenProcessToken(GetCurrentProcess(),TOKEN_DUPLICATE|TOKEN_QUERY|TOKEN_IMPERSONATE,&token));
    PSID foreign=NULL; CHECK(ConvertStringSidToSidW(L"S-1-5-21-1-2-3-999",&foreign));
    SID_AND_ATTRIBUTES restriction={foreign,0};
    CHECK(CreateRestrictedToken(token,DISABLE_MAX_PRIVILEGE,0,NULL,0,NULL,1,&restriction,&restricted));
    const DWORD access[]={GENERIC_READ,GENERIC_READ|GENERIC_WRITE};
    for(size_t i=0;i<sizeof access/sizeof access[0];i++) {
        CHECK(ImpersonateLoggedOnUser(restricted));
        HANDLE denied=CreateFileW(name,access[i],0,NULL,OPEN_EXISTING,FILE_FLAG_OVERLAPPED,NULL);
        DWORD error=GetLastError(); CHECK(RevertToSelf());
        CHECK(denied==INVALID_HANDLE_VALUE && error==ERROR_ACCESS_DENIED);
    }
    LocalFree(foreign); CloseHandle(restricted); CloseHandle(token);
}
static ipc_result opened(const ipc_request *r, ipc_token token, void *ctx)
{
    capture *c=ctx; CHECK(r->stdin_size==c->input_size && r->wait==c->wait);
    c->calls++; c->token=token; return IPC_OK;
}
static HANDLE raw_connect(void)
{
    endpoint ep; CHECK(resolve(NULL,NULL,&ep)==IPC_OK);
    HANDLE pipe;
    uint64_t end=milliseconds()+IPC_REQUEST_DEADLINE_MS;
    do {
        pipe=published_connect(&ep);
        if(pipe!=INVALID_HANDLE_VALUE) break;
        CHECK(milliseconds()<end);
        os_sleep_ns(UINT64_C(1000000));
    } while(true);
    LocalFree(ep.security); CHECK(pipe!=INVALID_HANDLE_VALUE); return pipe;
}
static void send_bytes(HANDLE pipe, uint8_t *bytes, size_t n)
{ CHECK(transfer(pipe,bytes,n,true,milliseconds()+IPC_REQUEST_DEADLINE_MS)==IPC_OK); }
static void pump(ipc_server *s, capture *c, size_t calls)
{
    uint64_t end=os_now_ns()+TEST_NS;
    while(c->calls<calls && os_now_ns()<end) {
        CHECK(os_wake_wait(ipc_server_wake(s),(int64_t)TEST_NS)==OS_OK);
        CHECK(ipc_server_drain(s,opened,c)==IPC_OK && s->drain_bytes<=IPC_DRAIN_BYTES);
    }
    CHECK(c->calls==calls);
}
static uint8_t *encoded(size_t input_size, bool wait, size_t *size)
{
    uint8_t *input=calloc(input_size?input_size:1,1), *wire=malloc(IPC_MAX_WIRE); CHECK(input && wire);
    ipc_request r={.cwd="C:/",.wait=wait,.has_stdin=true,.stdin_data=input,.stdin_size=input_size};
    CHECK(ipc_wire_encode(&r,wire,IPC_MAX_WIRE,size)==IPC_OK); free(input); return wire;
}
static void fragments_and_protocol(void)
{
    ipc_server s={0}; CHECK(ipc_server_init(&s,NULL)==IPC_OK);
    size_t n; uint8_t *wire=encoded(0,false,&n); HANDLE p=raw_connect();
    for(size_t i=0;i<n;i++) send_bytes(p,wire+i,1);
    capture c={0}; pump(&s,&c,1);
    CHECK(receive_reply(p,'A',milliseconds()+IPC_REQUEST_DEADLINE_MS)==IPC_OK); CloseHandle(p);
    p=raw_connect(); wire[0]='X'; send_bytes(p,wire,24);
    CHECK(receive_reply(p,'A',milliseconds()+IPC_REQUEST_DEADLINE_MS)==IPC_PROTOCOL); CloseHandle(p);
    ipc_server_fini(&s); free(wire);
    puts("ipc_win byte-fragmented request/malformed header: ok");
}
static void deadlines_and_disconnect(void)
{
    ipc_server s={0}; CHECK(ipc_server_init(&s,NULL)==IPC_OK);
    s.request_deadline_ms=IPC_EVICT_IDLE_MS;
    capture c={0}; CHECK(ipc_server_drain(&s,opened,&c)==IPC_OK);
    HANDLE p=raw_connect(); uint8_t byte='E'; send_bytes(p,&byte,1);
    CHECK(transfer(p,&byte,1,false,milliseconds()+IPC_REQUEST_DEADLINE_MS)==IPC_IO); CloseHandle(p);
    puts("ipc_win incomplete request deadline/cancel: ok");
    size_t n; uint8_t *wire=encoded(0,true,&n); p=raw_connect(); send_bytes(p,wire,n);
    c.wait=true; pump(&s,&c,1);
    CHECK(receive_reply(p,'A',milliseconds()+IPC_REQUEST_DEADLINE_MS)==IPC_OK);
    CHECK(ipc_server_token_live(&s,c.token)); CloseHandle(p);
    uint64_t end=os_now_ns()+TEST_NS;
    while(s.wait_drops==0 && os_now_ns()<end) {
        (void)os_wake_wait(ipc_server_wake(&s),(int64_t)TEST_NS);
        CHECK(ipc_server_drain(&s,opened,&c)==IPC_OK);
    }
    CHECK(s.wait_drops==1 && !ipc_server_token_live(&s,c.token));
    CHECK(ipc_server_report_closed(&s,c.token)==IPC_INVALID);
    ipc_server_fini(&s); free(wire);
    puts("ipc_win wait disconnect/token retirement: ok");
}
static void receive_pool_and_budget(void)
{
    ipc_server s={0}; CHECK(ipc_server_init(&s,NULL)==IPC_OK);
    size_t n; uint8_t *wire=encoded(IPC_RX_SMALL_SIZE,false,&n);
    HANDLE pipes[IPC_RX_BIG_COUNT];
    for(size_t i=0;i<IPC_RX_BIG_COUNT;i++) { pipes[i]=raw_connect(); send_bytes(pipes[i],wire,n); }
    /* Big slabs are held until callback consumption. A further valid header
     * must get BUSY without allocating another slab or blocking the loop. */
    HANDLE extra=raw_connect(); send_bytes(extra,wire,24);
    CHECK(receive_reply(extra,'A',milliseconds()+IPC_REQUEST_DEADLINE_MS)==IPC_BUSY); CloseHandle(extra);
    capture c={.input_size=IPC_RX_SMALL_SIZE}; pump(&s,&c,IPC_RX_BIG_COUNT);
    for(size_t i=0;i<IPC_RX_BIG_COUNT;i++) {
        CHECK(receive_reply(pipes[i],'A',milliseconds()+IPC_REQUEST_DEADLINE_MS)==IPC_OK); CloseHandle(pipes[i]);
    }
    ipc_server_fini(&s); free(wire);
    puts("ipc_win bounded receive pool admission: IPC_BUSY ok");
    CHECK(ipc_server_init(&s,NULL)==IPC_OK);
    wire=encoded(IPC_DRAIN_BYTES,false,&n); extra=raw_connect(); send_bytes(extra,wire,n);
    c=(capture){.input_size=IPC_DRAIN_BYTES};
    CHECK(os_wake_wait(ipc_server_wake(&s),(int64_t)TEST_NS)==OS_OK);
    CHECK(ipc_server_drain(&s,opened,&c)==IPC_OK && c.calls==0 && s.drain_bytes==IPC_DRAIN_BYTES);
    CHECK(os_wake_wait(ipc_server_wake(&s),0)==OS_OK); pump(&s,&c,1);
    CHECK(receive_reply(extra,'A',milliseconds()+IPC_REQUEST_DEADLINE_MS)==IPC_OK); CloseHandle(extra);
    ipc_server_fini(&s); free(wire);
    puts("ipc_win bounded drain/resignal: ok");
}
static void crashed_server(const char *self, const char *ns)
{
    char tmp[OS_PATH_MAX], marker[OS_PATH_MAX]; CHECK(os_temp_dir(tmp,sizeof tmp)==OS_OK);
    CHECK(snprintf(marker,sizeof marker,"%s/sublimite-test-ipc-%s",tmp,ns)>0);
    const char *av[]={self,"--hold",ns,marker,NULL}; os_proc p;
    CHECK(os_proc_spawn(&p,av)==OS_OK);
    uint64_t end=os_now_ns()+TEST_NS; os_stat st;
    while(os_stat_path(marker,0,&st)!=OS_OK) { CHECK(os_now_ns()<end); os_sleep_ns(UINT64_C(1000000)); }
    ipc_server s={0}; CHECK(ipc_server_init(&s,NULL)==IPC_EXISTS);
    /* Keep an old client endpoint alive across server death and replacement. */
    HANDLE orphan=raw_connect();
    CHECK(os_proc_kill(&p)==OS_OK); os_proc_status status;
    CHECK(os_proc_wait(&p,(int64_t)TEST_NS,&status)==OS_OK && status.how==OS_PROC_ABNORMAL);
    CHECK(ipc_server_init(&s,NULL)==IPC_OK);
    HANDLE fresh=raw_connect(); size_t n; uint8_t *wire=encoded(0,false,&n);
    send_bytes(fresh,wire,n); capture c={0}; pump(&s,&c,1);
    CHECK(receive_reply(fresh,'A',milliseconds()+IPC_REQUEST_DEADLINE_MS)==IPC_OK);
    CloseHandle(fresh); free(wire); CloseHandle(orphan);
    ipc_server_fini(&s); CHECK(os_unlink(marker)==OS_OK);
    puts("ipc_win crash/rebind/handoff with surviving old client: ok");
}
static void startup_race(const char *self, const char *ns, bool crash)
{
    WCHAR enter_name[NAME_CAP], release_name[NAME_CAP], done_name[NAME_CAP];
    CHECK(swprintf(enter_name,NAME_CAP,L"Local\\sublimite-test-start-enter-%hs",ns)>0);
    CHECK(swprintf(release_name,NAME_CAP,L"Local\\sublimite-test-start-release-%hs",ns)>0);
    CHECK(swprintf(done_name,NAME_CAP,L"Local\\sublimite-test-start-done-%hs",ns)>0);
    HANDLE done=CreateEventW(NULL,TRUE,FALSE,done_name);
    HANDLE enter=CreateEventW(NULL,TRUE,FALSE,enter_name), release=CreateEventW(NULL,TRUE,FALSE,release_name);
    CHECK(enter && release);
    const char *owner_av[]={self,"--startup-owner",ns,NULL}; os_proc owner;
    CHECK(os_proc_spawn(&owner,owner_av)==OS_OK);
    CHECK(WaitForSingleObject(enter,IPC_REQUEST_DEADLINE_MS)==WAIT_OBJECT_0);
    const char *other_av[]={self,crash?"--startup-recover":"--startup-other",ns,NULL}; os_proc other;
    CHECK(os_proc_spawn(&other,other_av)==OS_OK);
    os_proc_status status;
    /* (G) Existing idle eviction interval proves the contender cannot publish
     * EXISTS during the deterministic pre-listener barrier. */
    os_err waiting=os_proc_wait(&other,(int64_t)IPC_EVICT_IDLE_MS*INT64_C(1000000),&status);
    if(waiting!=OS_E_TIMEDOUT) { (void)os_proc_kill(&owner); (void)os_proc_wait(&owner,(int64_t)TEST_NS,&status); }
    CHECK(waiting==OS_E_TIMEDOUT);
    if(crash) CHECK(os_proc_kill(&owner)==OS_OK); else CHECK(SetEvent(release));
    CHECK(os_proc_wait(&other,(int64_t)TEST_NS,&status)==OS_OK && status.how==OS_PROC_EXITED && status.code==0);
    CHECK(SetEvent(done));
    CHECK(os_proc_wait(&owner,(int64_t)TEST_NS,&status)==OS_OK);
    CHECK(crash?status.how==OS_PROC_ABNORMAL:status.how==OS_PROC_EXITED && status.code==0);
    CloseHandle(enter); CloseHandle(release); CloseHandle(done);
    printf("ipc_win startup %s: ok\n",crash?"abandoned initializer takeover":"publication/handoff race");
}
static void allocation_failure(void)
{
    /* (P) Setup allocates service + peer table + each receive slab. */
    for(size_t i=1;i<=2u+IPC_RX_SMALL_COUNT+IPC_RX_BIG_COUNT;i++) {
        fail_allocation=i; allocation=0;
        ipc_server s={0}; CHECK(ipc_server_init_isolated(&s)==IPC_IO);
        CHECK(!s.win && !s.peers); fail_allocation=0;
    }
    puts("ipc_win setup allocation failures return IPC_IO/clean up: ok");
}
static void security_cases(bool acl)
{
    inspect_security=acl; inspect_sqos=!acl;
    ipc_server s={0}; CHECK(ipc_server_init(&s,NULL)==IPC_OK);
    endpoint ep; CHECK(resolve(NULL,NULL,&ep)==IPC_OK);
    HANDLE p=published_connect(&ep); CHECK(p!=INVALID_HANDLE_VALUE); CloseHandle(p); LocalFree(ep.security);
    ipc_server_fini(&s);
    int fds[2]; CHECK(ipc_launcher_pair(fds)==IPC_OK); CHECK(_close(fds[0])==0 && _close(fds[1])==0);
    char name[IPC_PATH_CAP]; int fd;
    CHECK(ipc_launcher_prepare(&fd,name,sizeof name)==IPC_OK);
    CHECK(ipc_server_init_isolated(&s)==IPC_OK); ipc_token token;
    CHECK(ipc_server_connect_wait(&s,name,&token)==IPC_OK);
    ipc_server_fini(&s); CHECK(_close(fd)==0);
    inspect_security=inspect_sqos=false;
    puts(acl?"ipc_win primary/launcher protected user ACL; restricted-token read/duplex denied: ok":
        "ipc_win all client opens request identification SQOS: ok");
}
static void trailing_requests(void)
{
    for(unsigned second=0;second<2u;second++) {
        ipc_server s={0}; CHECK(ipc_server_init(&s,NULL)==IPC_OK);
        size_t n; uint8_t *wire=encoded(0,false,&n);
        size_t extra=second?n:1u; if(second) memcpy(wire+n,wire,n); else wire[n]='X';
        HANDLE p=raw_connect(); send_bytes(p,wire,n+extra);
        /* Wait until service has consumed the payload, then hold its mutex
         * while UI drain runs. Pending trailer completion cannot win it. */
        bool consumed=false; uint64_t end=milliseconds()+IPC_REQUEST_DEADLINE_MS;
        while(!consumed && milliseconds()<end) {
            os_rmutex_lock(&s.win->mutex);
            for(size_t i=0;i<PIPE_SLOTS;i++) if(s.peers[i].received==n) consumed=true;
            DWORD available=0;
            if(PeekNamedPipe(p,NULL,0,NULL,&available,NULL) && available) consumed=true;
            if(!consumed) { os_rmutex_unlock(&s.win->mutex); os_yield(); }
        }
        CHECK(consumed); capture c={0}; CHECK(ipc_server_drain(&s,opened,&c)==IPC_OK);
        CHECK(c.calls==0); os_rmutex_unlock(&s.win->mutex);
        CHECK(receive_reply(p,'A',milliseconds()+IPC_REQUEST_DEADLINE_MS)==IPC_PROTOCOL);
        CloseHandle(p); free(wire); ipc_server_fini(&s);
    }
    puts("ipc_win buffered garbage/second frame rejected before callback: ok");
}
int main(int argc, char **argv)
{
    if(argc==3 && !strncmp(argv[1],"--startup-",10)) {
        CHECK(os_env_set("EDIT_IPC_NAMESPACE",argv[2])==OS_OK);
        bool owner=!strcmp(argv[1],"--startup-owner"), recover=!strcmp(argv[1],"--startup-recover");
        if(owner) {
            WCHAR name[NAME_CAP];
            CHECK(swprintf(name,NAME_CAP,L"Local\\sublimite-test-start-enter-%hs",argv[2])>0);
            startup_enter=OpenEventW(EVENT_MODIFY_STATE,FALSE,name);
            CHECK(swprintf(name,NAME_CAP,L"Local\\sublimite-test-start-release-%hs",argv[2])>0);
            startup_release=OpenEventW(SYNCHRONIZE,FALSE,name); CHECK(startup_enter && startup_release);
        }
        ipc_server s={0}; ipc_result started=ipc_server_init(&s,NULL);
        if(started!=(owner||recover?IPC_OK:IPC_EXISTS)) fprintf(stderr,"startup role %s: result %d, native %lu\n",argv[1],started,GetLastError());
        CHECK(started==(owner||recover?IPC_OK:IPC_EXISTS));
        if(owner) {
            capture c={0}; pump(&s,&c,1); WCHAR name[NAME_CAP];
            CHECK(swprintf(name,NAME_CAP,L"Local\\sublimite-test-start-done-%hs",argv[2])>0);
            HANDLE done=OpenEventW(SYNCHRONIZE,FALSE,name); CHECK(done);
            CHECK(WaitForSingleObject(done,IPC_REQUEST_DEADLINE_MS)==WAIT_OBJECT_0);
            CloseHandle(done); ipc_server_fini(&s);
        }
        else if(recover) ipc_server_fini(&s);
        else { ipc_request r={.cwd="C:/"}; CHECK(ipc_client_send(NULL,&r,IPC_LOCK_TIMEOUT_MS)==IPC_OK); }
        return 0;
    }
    if(argc==4 && !strcmp(argv[1],"--hold")) {
        CHECK(os_env_set("EDIT_IPC_NAMESPACE",argv[2])==OS_OK);
        ipc_server s={0}; CHECK(ipc_server_init(&s,NULL)==IPC_OK);
        os_file f=OS_FILE_INIT; CHECK(os_file_open(&f,argv[3],OS_OPEN_WRITE|OS_OPEN_CREATE|OS_OPEN_EXCL)==OS_OK);
        os_file_close(&f); os_sleep_ns(TEST_NS*2u); ipc_server_fini(&s); return 0;
    }
    char ns[80], self[OS_PATH_MAX]; CHECK(os_proc_self_path(self,sizeof self)==OS_OK);
    CHECK(snprintf(ns,sizeof ns,"transport-%llu",(unsigned long long)os_proc_id())>0);
    CHECK(os_env_set("EDIT_IPC_NAMESPACE",ns)==OS_OK);
    if(argc==2 && !strcmp(argv[1],"--security")) { security_cases(true); return 0; }
    if(argc==2 && !strcmp(argv[1],"--sqos")) { security_cases(false); return 0; }
    if(argc==2 && !strcmp(argv[1],"--trailers")) { trailing_requests(); return 0; }
    if(argc==2 && !strcmp(argv[1],"--startup")) { startup_race(self,ns,false); startup_race(self,ns,true); return 0; }
    if(argc==2 && !strcmp(argv[1],"--orphan")) { crashed_server(self,ns); return 0; }
    startup_race(self,ns,false); startup_race(self,ns,true);
    security_cases(true); security_cases(false); trailing_requests();
    allocation_failure();
    fragments_and_protocol(); deadlines_and_disconnect(); receive_pool_and_budget();
    crashed_server(self,ns);
    puts("ipc_transport_test: ok"); return 0;
}
#endif
