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
#include "../../src/ipc/ipc_win.c"
#undef edit_arena_alloc
#include <stdlib.h>
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"ipc_transport_test:%d: %s\n",__LINE__,#x); exit(1); } } while(0)
#define TEST_NS ((uint64_t)IPC_REQUEST_DEADLINE_MS*UINT64_C(1000000))
typedef struct capture { size_t calls, input_size; ipc_token token; bool wait; } capture;
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
        pipe=CreateFileW(ep.pipe,GENERIC_READ|GENERIC_WRITE,0,NULL,OPEN_EXISTING,FILE_FLAG_OVERLAPPED,NULL);
        if(pipe!=INVALID_HANDLE_VALUE) break;
        CHECK(GetLastError()==ERROR_PIPE_BUSY && milliseconds()<end);
        CHECK(WaitNamedPipeW(ep.pipe,timeout_left(end)));
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
    CHECK(os_proc_kill(&p)==OS_OK); os_proc_status status;
    CHECK(os_proc_wait(&p,(int64_t)TEST_NS,&status)==OS_OK && status.how==OS_PROC_ABNORMAL);
    CHECK(ipc_server_init(&s,NULL)==IPC_OK); ipc_server_fini(&s); CHECK(os_unlink(marker)==OS_OK);
    puts("ipc_win crashed server leaves no named-object residue: ok");
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
int main(int argc, char **argv)
{
    if(argc==4 && !strcmp(argv[1],"--hold")) {
        CHECK(os_env_set("EDIT_IPC_NAMESPACE",argv[2])==OS_OK);
        ipc_server s={0}; CHECK(ipc_server_init(&s,NULL)==IPC_OK);
        os_file f=OS_FILE_INIT; CHECK(os_file_open(&f,argv[3],OS_OPEN_WRITE|OS_OPEN_CREATE|OS_OPEN_EXCL)==OS_OK);
        os_file_close(&f); os_sleep_ns(TEST_NS*2u); ipc_server_fini(&s); return 0;
    }
    char ns[80], self[OS_PATH_MAX]; CHECK(os_proc_self_path(self,sizeof self)==OS_OK);
    CHECK(snprintf(ns,sizeof ns,"transport-%llu",(unsigned long long)os_proc_id())>0);
    CHECK(os_env_set("EDIT_IPC_NAMESPACE",ns)==OS_OK);
    allocation_failure();
    fragments_and_protocol(); deadlines_and_disconnect(); receive_pool_and_budget();
    crashed_server(self,ns);
    puts("ipc_transport_test: ok"); return 0;
}
#endif
