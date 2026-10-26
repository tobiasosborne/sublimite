#if defined(_WIN32)
#include "ipc.h"
#include "crt_win.h"
#include <windows.h>
#include <sddl.h>
#include <io.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

/* One IOCP service owns every pipe operation. The loop only consumes complete
 * requests and queues replies; callbacks, including immediate report_closed,
 * remain on the loop thread. Pending buffers survive cancellation completion. */
#define PIPE_SLOTS (IPC_MAX_CLIENTS+1u) /* admitted peers plus one listener */
#define NAME_CAP 256u /* (P) Win32 named-pipe name limit, including prefix */
typedef struct rendezvous { volatile LONG ready; DWORD pid; WCHAR pipe[NAME_CAP]; } rendezvous;
typedef enum peer_state { FREE, LISTENING, READING, READY, OPENED, DROPPING } peer_state;
struct ipc_peer {
    HANDLE pipe;
    OVERLAPPED read, write;
    bool read_pending, write_pending, big, wait, closed, opened;
    int crt_fd;
    peer_state state;
    ipc_token token;
    uint64_t accepted_ms, active_ms;
    uint8_t head[24], extra, reply[16], *wire;
    size_t received, expected, credited, reply_size, sent;
    ipc_result result;
};
struct ipc_win {
    os_rmutex mutex;
    os_thread thread;
    os_wake wake;
    HANDLE port, arbitration, publication, initialized;
    rendezvous *published;
    ipc_result setup_result;
    WCHAR publication_name[NAME_CAP];
    PSECURITY_DESCRIPTOR security;
    WCHAR pipe_name[NAME_CAP];
    BYTE sid[SECURITY_MAX_SID_SIZE];
    bool stop, failed, endpoint;
    uint64_t wait_drops;
    uint32_t request_deadline_ms, evict_idle_ms;
    uint8_t *small[IPC_RX_SMALL_COUNT], *big[IPC_RX_BIG_COUNT];
    size_t nsmall, nbig;
};
typedef struct endpoint {
    WCHAR pipe[NAME_CAP], mutex[NAME_CAP], publication[NAME_CAP];
    BYTE sid[SECURITY_MAX_SID_SIZE];
    PSECURITY_DESCRIPTOR security;
} endpoint;
static uint64_t milliseconds(void) { return os_now_ns()/UINT64_C(1000000); }
static bool token_sid(HANDLE token, BYTE *sid)
{
    union { TOKEN_USER align; BYTE data[sizeof(TOKEN_USER)+SECURITY_MAX_SID_SIZE]; } user;
    DWORD n=0;
    return GetTokenInformation(token,TokenUser,&user,sizeof user,&n) &&
        CopySid(SECURITY_MAX_SID_SIZE,sid,((TOKEN_USER *)user.data)->User.Sid);
}
static bool current_sid(BYTE *sid)
{
    HANDLE token;
    if(!OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&token)) return false;
    bool ok=token_sid(token,sid); CloseHandle(token); return ok;
}
static ipc_result suffix(WCHAR *name, size_t *used, WCHAR tag, const char *text)
{
    if(!text) return IPC_OK;
    size_t n=strlen(text);
    if(!n) return IPC_INVALID;
    if(*used+2u>=NAME_CAP || n>(NAME_CAP-*used-3u)/2u) return IPC_LIMIT;
    name[(*used)++]='-'; name[(*used)++]=tag;
    const WCHAR *hex=L"0123456789abcdef";
    for(size_t i=0;i<n;i++) {
        unsigned byte=(unsigned char)text[i];
        name[(*used)++]=hex[byte>>4]; name[(*used)++]=hex[byte&15u];
    }
    name[*used]=0; return IPC_OK;
}
static ipc_result resolve(const char *runtime, const char *display, endpoint *ep)
{
    memset(ep,0,sizeof *ep);
    if(!current_sid(ep->sid)) return IPC_IO;
    WCHAR *sid=NULL;
    if(!ConvertSidToStringSidW(ep->sid,&sid)) return IPC_IO;
    WCHAR identity[NAME_CAP], acl[NAME_CAP];
    int n=swprintf(identity,NAME_CAP,L"sublimite-%ls",sid);
    int a=swprintf(acl,NAME_CAP,L"D:P(A;;GA;;;%ls)",sid); LocalFree(sid);
    if(n<0 || a<0) return IPC_LIMIT;
    size_t used=(size_t)n;
    char ns[IPC_PATH_CAP]; os_err e=os_env_get("EDIT_IPC_NAMESPACE",ns,sizeof ns);
    if(e!=OS_OK && e!=OS_E_NOENT) return IPC_LIMIT;
    ipc_result rc=suffix(identity,&used,'n',e==OS_OK?ns:NULL);
    if(rc==IPC_OK) rc=suffix(identity,&used,'r',runtime);
    if(rc==IPC_OK) rc=suffix(identity,&used,'d',display);
    /* Account for the longer of the two object prefixes before formatting. */
    if(rc!=IPC_OK) return rc;
    if(used+wcslen(L"\\\\.\\pipe\\")+1u>NAME_CAP) return IPC_LIMIT;
    if(swprintf(ep->pipe,NAME_CAP,L"\\\\.\\pipe\\%ls",identity)<0 ||
       swprintf(ep->mutex,NAME_CAP,L"Local\\%ls",identity)<0 ||
       swprintf(ep->publication,NAME_CAP,L"Local\\%ls-endpoint",identity)<0) return IPC_LIMIT;
    if(!ConvertStringSecurityDescriptorToSecurityDescriptorW(acl,SDDL_REVISION_1,&ep->security,NULL)) return IPC_IO;
    return IPC_OK;
}
static bool own_peer(HANDLE pipe, const BYTE *sid, bool client)
{
    ULONG pid=0;
    BOOL got=client?GetNamedPipeServerProcessId(pipe,&pid):GetNamedPipeClientProcessId(pipe,&pid);
    if(!got) return false;
    HANDLE process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);
    if(!process) return false;
    HANDLE token=NULL; BYTE other[SECURITY_MAX_SID_SIZE]; bool ok=false;
    if(OpenProcessToken(process,TOKEN_QUERY,&token)) {
        ok=token_sid(token,other) && EqualSid((void *)sid,other);
        CloseHandle(token);
    }
    CloseHandle(process); return ok;
}
static DWORD timeout_left(uint64_t deadline)
{
    if(deadline==UINT64_MAX) return INFINITE;
    uint64_t now=milliseconds(), left=deadline>now?deadline-now:0;
    return left>=INFINITE ? INFINITE-1u : (DWORD)left;
}
/* The mutex is owned for the service thread's lifetime. Only that owner may
 * publish a random generation's pipe. Old client-held instances therefore
 * never occupy a replacement listener's name. Readiness is published last. */
static HANDLE published_connect(const endpoint *ep)
{
    HANDLE mapping=OpenFileMappingW(FILE_MAP_READ,FALSE,ep->publication);
    if(!mapping) return INVALID_HANDLE_VALUE;
    const rendezvous *view=MapViewOfFile(mapping,FILE_MAP_READ,0,0,sizeof *view);
    HANDLE pipe=INVALID_HANDLE_VALUE;
    if(view) {
        LONG ready=view->ready; MemoryBarrier();
        if(ready) {
            WCHAR name[NAME_CAP]; memcpy(name,view->pipe,sizeof name);
            DWORD pid=view->pid; name[NAME_CAP-1u]=0;
            MemoryBarrier();
            if(ready==view->ready) {
                pipe=CreateFileW(name,GENERIC_READ|GENERIC_WRITE,0,NULL,OPEN_EXISTING,
                    FILE_FLAG_OVERLAPPED|SECURITY_SQOS_PRESENT|SECURITY_IDENTIFICATION,NULL);
                ULONG server=0;
                if(pipe!=INVALID_HANDLE_VALUE &&
                   (!GetNamedPipeServerProcessId(pipe,&server) || server!=pid || !own_peer(pipe,ep->sid,true))) {
                    CloseHandle(pipe); pipe=INVALID_HANDLE_VALUE;
                }
            }
        }
        UnmapViewOfFile(view);
    }
    CloseHandle(mapping); return pipe;
}
/* All client transfers are overlapped, including the unbounded close wait.
 * Cancellation is always reaped before stack OVERLAPPED/buffers expire. */
static ipc_result transfer(HANDLE pipe, uint8_t *bytes, size_t n, bool sending, uint64_t deadline)
{
    HANDLE event=CreateEventW(NULL,TRUE,FALSE,NULL);
    if(!event) return IPC_IO;
    ipc_result rc=IPC_OK; size_t pos=0;
    while(pos<n) {
        OVERLAPPED ov={0}; ov.hEvent=event; ResetEvent(event);
        DWORD done=0, want=(DWORD)(n-pos);
        BOOL ok=sending?WriteFile(pipe,bytes+pos,want,&done,&ov):ReadFile(pipe,bytes+pos,want,&done,&ov);
        if(!ok && GetLastError()==ERROR_IO_PENDING) {
            DWORD waited=WaitForSingleObject(event,timeout_left(deadline));
            if(waited!=WAIT_OBJECT_0) {
                (void)CancelIoEx(pipe,&ov); (void)GetOverlappedResult(pipe,&ov,&done,TRUE);
                rc=waited==WAIT_TIMEOUT?IPC_TIMEOUT:IPC_IO; break;
            }
            ok=GetOverlappedResult(pipe,&ov,&done,FALSE);
        }
        if(!ok || !done) { rc=IPC_IO; break; }
        pos+=(size_t)done;
    }
    CloseHandle(event); return rc;
}
static void reply(uint8_t *p, char kind, ipc_result rc)
{
    memcpy(p,"EDIR",4); p[4]=(uint8_t)kind; p[5]=(uint8_t)rc; p[6]=0; p[7]=0;
}
static ipc_result receive_reply(HANDLE pipe, char kind, uint64_t deadline)
{
    uint8_t bytes[8]; ipc_result rc=transfer(pipe,bytes,sizeof bytes,false,deadline);
    if(rc!=IPC_OK) return rc;
    if(memcmp(bytes,"EDIR",4) || bytes[4]!=(uint8_t)kind || bytes[5]>IPC_REJECTED || bytes[6] || bytes[7]) return IPC_PROTOCOL;
    return (ipc_result)bytes[5];
}
ipc_result ipc_client_send_display(const char *runtime, const char *display, const ipc_request *r, int timeout_ms)
{
    if(timeout_ms < -1) return IPC_INVALID;
    endpoint ep; ipc_result rc=resolve(runtime,display,&ep);
    if(rc!=IPC_OK) return rc;
    edit_arena arena={0}; HANDLE pipe=INVALID_HANDLE_VALUE;
    if(edit_arena_init(&arena,IPC_MAX_WIRE)!=0) { LocalFree(ep.security); return IPC_IO; }
    uint8_t *wire=edit_arena_alloc(&arena,IPC_MAX_WIRE,1); size_t n=0;
    rc=ipc_wire_encode(r,wire,IPC_MAX_WIRE,&n);
    uint64_t deadline=timeout_ms<0?UINT64_MAX:milliseconds()+(uint64_t)timeout_ms;
    while(rc==IPC_OK) {
        pipe=published_connect(&ep);
        if(pipe!=INVALID_HANDLE_VALUE) break;
        HANDLE owner=OpenMutexW(SYNCHRONIZE,FALSE,ep.mutex);
        if(!owner) { rc=IPC_IO; break; }
        CloseHandle(owner);
        DWORD left=timeout_left(deadline);
        if(!left) { rc=IPC_TIMEOUT; break; }
        /* WaitNamedPipe returns immediately while the first name is absent.
         * Reuse the OS process-wait poll interval, under the same deadline. */
        os_sleep_ns(UINT64_C(1000000));
    }
    if(rc==IPC_OK && !own_peer(pipe,ep.sid,true)) rc=IPC_REJECTED;
    if(rc==IPC_OK) rc=transfer(pipe,wire,n,true,deadline);
    if(rc==IPC_OK) rc=receive_reply(pipe,'A',deadline);
    if(rc==IPC_OK && r->wait) rc=receive_reply(pipe,'C',UINT64_MAX);
    if(pipe!=INVALID_HANDLE_VALUE) CloseHandle(pipe);
    LocalFree(ep.security); edit_arena_free(&arena); return rc;
}
ipc_result ipc_client_send(const char *runtime, const ipc_request *r, int timeout_ms)
{ return ipc_client_send_display(runtime,NULL,r,timeout_ms); }

static uint8_t *rx_acquire(struct ipc_win *w, size_t need, bool *big)
{
    if(need<=IPC_RX_SMALL_SIZE && w->nsmall) { *big=false; return w->small[--w->nsmall]; }
    if(w->nbig) { *big=true; return w->big[--w->nbig]; }
    return NULL;
}
static void rx_release(struct ipc_win *w, struct ipc_peer *p)
{
    if(!p->wire) return;
    if(p->big) w->big[w->nbig++]=p->wire; else w->small[w->nsmall++]=p->wire;
    p->wire=NULL;
}
static bool peer_waiting(const struct ipc_peer *p)
{
    return p->state!=FREE && p->state!=DROPPING && p->opened && p->wait && !p->closed && p->result==IPC_OK;
}
static void reset_peer(ipc_server *s, struct ipc_peer *p)
{
    rx_release(s->win,p); memset(p,0,sizeof *p); p->pipe=INVALID_HANDLE_VALUE; p->crt_fd=-1;
}
static void drop_peer(ipc_server *s, struct ipc_peer *p)
{
    if(p->state==FREE || p->state==DROPPING) return;
    if(peer_waiting(p)) { s->win->wait_drops++; os_wake_signal(&s->win->wake); }
    p->state=DROPPING;
    (void)CancelIoEx(p->pipe,NULL);
    if(p->crt_fd>=0) (void)_close(p->crt_fd); else CloseHandle(p->pipe);
    p->pipe=INVALID_HANDLE_VALUE;
    if(!p->read_pending && !p->write_pending) reset_peer(s,p);
}
static void start_read(ipc_server *s, struct ipc_peer *p)
{
    if(p->read_pending || p->state==DROPPING || p->state==FREE) return;
    uint8_t *dst=&p->extra; size_t n=1;
    if(p->state==READING) {
        dst=p->wire?p->wire:p->head;
        n=(p->expected?p->expected:sizeof p->head)-p->received; dst+=p->received;
    }
    memset(&p->read,0,sizeof p->read); p->read_pending=true;
    if(!ReadFile(p->pipe,dst,(DWORD)n,NULL,&p->read) && GetLastError()!=ERROR_IO_PENDING) {
        p->read_pending=false; drop_peer(s,p);
    }
}
static void flush_peer(ipc_server *s, struct ipc_peer *p)
{
    if(p->state!=OPENED || p->write_pending || !p->reply_size) return;
    if(p->sent==p->reply_size) {
        if(!p->wait || p->closed || p->result!=IPC_OK) {
            if(p->closed && p->wait && p->result==IPC_OK && p->reply_size==8) {
                reply(p->reply+8,'C',IPC_OK); p->reply_size=16;
            } else { drop_peer(s,p); return; }
        } else return;
    }
    memset(&p->write,0,sizeof p->write); p->write_pending=true;
    if(!WriteFile(p->pipe,p->reply+p->sent,(DWORD)(p->reply_size-p->sent),NULL,&p->write) && GetLastError()!=ERROR_IO_PENDING) {
        p->write_pending=false; drop_peer(s,p);
    }
}
static void reject_peer(ipc_server *s, struct ipc_peer *p, ipc_result rc)
{
    p->opened=true; p->state=OPENED; p->result=rc;
    reply(p->reply,'A',rc); p->reply_size=8; rx_release(s->win,p); flush_peer(s,p);
}
static bool create_listener(ipc_server *s, struct ipc_peer *p, bool first)
{
    struct ipc_win *w=s->win;
    SECURITY_ATTRIBUTES sa={sizeof sa,w->security,FALSE};
    p->pipe=CreateNamedPipeW(w->pipe_name,PIPE_ACCESS_DUPLEX|FILE_FLAG_OVERLAPPED|(first?FILE_FLAG_FIRST_PIPE_INSTANCE:0),
        PIPE_TYPE_BYTE|PIPE_READMODE_BYTE|PIPE_WAIT|PIPE_REJECT_REMOTE_CLIENTS,
        PIPE_UNLIMITED_INSTANCES,IPC_RX_SMALL_SIZE,IPC_RX_SMALL_SIZE,IPC_REQUEST_DEADLINE_MS,&sa);
    if(p->pipe==INVALID_HANDLE_VALUE) return false;
    if(!CreateIoCompletionPort(p->pipe,w->port,(ULONG_PTR)p,0)) { CloseHandle(p->pipe); p->pipe=INVALID_HANDLE_VALUE; return false; }
    p->state=LISTENING; p->read_pending=true;
    if(!ConnectNamedPipe(p->pipe,&p->read)) {
        DWORD e=GetLastError();
        if(e==ERROR_PIPE_CONNECTED) {
            if(PostQueuedCompletionStatus(w->port,0,(ULONG_PTR)p,&p->read)) return true;
        } else if(e==ERROR_IO_PENDING) return true;
        p->read_pending=false; drop_peer(s,p); return false;
    }
    return true;
}
static void ensure_listener(ipc_server *s)
{
    if(!s->win->endpoint || s->win->stop) return;
    struct ipc_peer *slot=NULL;
    for(size_t i=0;i<PIPE_SLOTS;i++) {
        struct ipc_peer *p=&s->peers[i];
        if(p->state==LISTENING) return;
        if(p->state==FREE) slot=p;
    }
    if(slot && !create_listener(s,slot,false)) { s->win->failed=true; os_wake_signal(&s->win->wake); }
}
static uint32_t le32(const uint8_t *p)
{ return (uint32_t)p[0] | (uint32_t)p[1]<<8 | (uint32_t)p[2]<<16 | (uint32_t)p[3]<<24; }
static void read_complete(ipc_server *s, struct ipc_peer *p, DWORD bytes)
{
    if(p->state==LISTENING) {
        if(!own_peer(p->pipe,s->win->sid,false) || s->next_token==UINT64_MAX) { drop_peer(s,p); return; }
        p->state=READING; p->token=++s->next_token; p->accepted_ms=p->active_ms=milliseconds();
        size_t active=0; struct ipc_peer *victim=NULL;
        for(size_t i=0;i<PIPE_SLOTS;i++) {
            struct ipc_peer *other=&s->peers[i];
            if(other->state!=FREE && other->state!=DROPPING && other->state!=LISTENING) active++;
            if(other!=p && other->state==READING && p->active_ms-other->active_ms>=s->win->evict_idle_ms &&
               (!victim || other->active_ms<victim->active_ms)) victim=other;
        }
        if(active>IPC_MAX_CLIENTS) {
            if(victim) drop_peer(s,victim); else { reject_peer(s,p,IPC_BUSY); return; }
        }
        start_read(s,p); return;
    }
    if(p->state!=READING || !bytes) { drop_peer(s,p); return; }
    p->received+=(size_t)bytes; p->active_ms=milliseconds();
    if(!p->expected && p->received==sizeof p->head) {
        p->expected=le32(p->head+8);
        if(memcmp(p->head,"EDIP",4) || p->expected<24 || p->expected>IPC_MAX_WIRE) { reject_peer(s,p,IPC_PROTOCOL); return; }
        p->wire=rx_acquire(s->win,p->expected,&p->big);
        if(!p->wire) { reject_peer(s,p,IPC_BUSY); return; }
        memcpy(p->wire,p->head,sizeof p->head);
    }
    if(p->expected && p->received==p->expected) {
        DWORD available=0;
        if(!PeekNamedPipe(p->pipe,NULL,0,NULL,&available,NULL)) { drop_peer(s,p); return; }
        if(available) { reject_peer(s,p,IPC_PROTOCOL); return; }
        p->state=READY; os_wake_signal(&s->win->wake);
        return;
    }
    start_read(s,p);
}
static ipc_result bind_endpoint(ipc_server *s)
{
    struct ipc_win *w=s->win;
    uint64_t deadline=milliseconds()+IPC_LOCK_TIMEOUT_MS;
    for(;;) {
        DWORD acquired=WaitForSingleObject(w->arbitration,0);
        if(acquired==WAIT_OBJECT_0 || acquired==WAIT_ABANDONED) break;
        if(acquired!=WAIT_TIMEOUT) return IPC_IO;
        endpoint ep={0};
        memcpy(ep.publication,w->publication_name,sizeof ep.publication);
        memcpy(ep.sid,w->sid,sizeof ep.sid);
        HANDLE incumbent=published_connect(&ep);
        if(incumbent!=INVALID_HANDLE_VALUE) { CloseHandle(incumbent); return IPC_EXISTS; }
        if(!timeout_left(deadline)) return IPC_TIMEOUT;
        os_sleep_ns(UINT64_C(1000000));
    }
    SECURITY_ATTRIBUTES sa={sizeof sa,w->security,FALSE};
    w->publication=CreateFileMappingW(INVALID_HANDLE_VALUE,&sa,PAGE_READWRITE,0,sizeof(rendezvous),w->publication_name);
    if(!w->publication) goto fail;
    w->published=MapViewOfFile(w->publication,FILE_MAP_WRITE|FILE_MAP_READ,0,0,sizeof(rendezvous));
    if(!w->published) goto fail;
    InterlockedExchange(&w->published->ready,0);
    uint64_t nonce[2];
    if(os_random(nonce,sizeof nonce)!=OS_OK) goto fail;
    size_t used=wcslen(w->pipe_name);
    /* (P) Two 64-bit words need 32 hex digits, plus separator and NUL. */
    if(used+34u>NAME_CAP || swprintf(w->pipe_name+used,NAME_CAP-used,L"-%016llx%016llx",
        (unsigned long long)nonce[0],(unsigned long long)nonce[1])<0) goto fail;
    if(!create_listener(s,&s->peers[0],true)) goto fail;
    memcpy(w->published->pipe,w->pipe_name,sizeof w->pipe_name);
    w->published->pid=GetCurrentProcessId();
    InterlockedExchange(&w->published->ready,(LONG)((nonce[0]&INT32_MAX)|1u));
    return IPC_OK;
fail:
    ReleaseMutex(w->arbitration); return IPC_IO;
}
static void service(void *arg)
{
    ipc_server *s=arg; struct ipc_win *w=s->win;
    w->setup_result=w->endpoint?bind_endpoint(s):IPC_OK;
    SetEvent(w->initialized);
    if(w->setup_result!=IPC_OK) return;
    for(;;) {
        os_rmutex_lock(&w->mutex);
        uint64_t now=milliseconds(), earliest=UINT64_MAX; bool pending=false;
        for(size_t i=0;i<PIPE_SLOTS;i++) {
            struct ipc_peer *p=&s->peers[i];
            if(w->stop) drop_peer(s,p);
            else {
                if(p->state==READING) {
                    uint64_t end=p->accepted_ms+w->request_deadline_ms;
                    if(now>=end) drop_peer(s,p); else if(end<earliest) earliest=end;
                }
                flush_peer(s,p);
                if(p->state==OPENED) start_read(s,p);
            }
            if(p->state!=FREE) pending=true;
        }
        ensure_listener(s);
        bool quit=w->stop && !pending;
        os_rmutex_unlock(&w->mutex);
        if(quit) {
            if(w->endpoint) { InterlockedExchange(&w->published->ready,0); ReleaseMutex(w->arbitration); }
            return;
        }
        DWORD bytes=0; ULONG_PTR key=0; OVERLAPPED *ov=NULL;
        BOOL ok=GetQueuedCompletionStatus(w->port,&bytes,&key,&ov,timeout_left(earliest));
        if(!ov) {
            if(!ok && GetLastError()!=WAIT_TIMEOUT) {
                os_rmutex_lock(&w->mutex); w->failed=true; w->stop=true; os_wake_signal(&w->wake); os_rmutex_unlock(&w->mutex);
            }
            continue;
        }
        struct ipc_peer *p=(struct ipc_peer *)key;
        os_rmutex_lock(&w->mutex);
        bool writing=ov==&p->write;
        if(writing) p->write_pending=false; else p->read_pending=false;
        if(p->state==DROPPING) {
            if(!p->read_pending && !p->write_pending) reset_peer(s,p);
        } else if(!ok) drop_peer(s,p);
        else if(writing) {
            if(!bytes) drop_peer(s,p); else { p->sent+=(size_t)bytes; flush_peer(s,p); }
        } else read_complete(s,p,bytes);
        os_rmutex_unlock(&w->mutex);
    }
}
static void reset_server(ipc_server *s)
{
    memset(s,0,sizeof *s); s->fd=s->listener=s->lock_fd=s->dir_fd=s->timer_fd=-1;
    s->request_deadline_ms=IPC_REQUEST_DEADLINE_MS; s->evict_idle_ms=IPC_EVICT_IDLE_MS;
}
static ipc_result setup(ipc_server *s, endpoint *ep)
{
    reset_server(s);
    size_t size=sizeof(struct ipc_win)+PIPE_SLOTS*sizeof(struct ipc_peer)+
        (size_t)IPC_RX_SMALL_COUNT*IPC_RX_SMALL_SIZE+(size_t)IPC_RX_BIG_COUNT*IPC_MAX_WIRE+OS_PATH_MAX;
    if(edit_arena_init(&s->arena,size)!=0) return IPC_IO;
    struct ipc_win *w=edit_arena_alloc(&s->arena,sizeof *w,_Alignof(struct ipc_win));
    if(!w) { edit_arena_free(&s->arena); reset_server(s); return IPC_IO; }
    s->win=w; memset(w,0,sizeof *w);
    w->request_deadline_ms=s->request_deadline_ms; w->evict_idle_ms=s->evict_idle_ms;
    s->peers=edit_arena_alloc(&s->arena,PIPE_SLOTS*sizeof *s->peers,_Alignof(struct ipc_peer));
    if(!s->peers) { edit_arena_free(&s->arena); reset_server(s); return IPC_IO; }
    memset(s->peers,0,PIPE_SLOTS*sizeof *s->peers);
    for(size_t i=0;i<PIPE_SLOTS;i++) reset_peer(s,&s->peers[i]);
    bool mutex=false, wake=false; ipc_result rc=IPC_IO;
    for(size_t i=0;i<IPC_RX_SMALL_COUNT;i++) {
        w->small[w->nsmall]=edit_arena_alloc(&s->arena,IPC_RX_SMALL_SIZE,1);
        if(!w->small[w->nsmall]) goto fail;
        w->nsmall++;
    }
    for(size_t i=0;i<IPC_RX_BIG_COUNT;i++) {
        w->big[w->nbig]=edit_arena_alloc(&s->arena,IPC_MAX_WIRE,1);
        if(!w->big[w->nbig]) goto fail;
        w->nbig++;
    }
    if(os_rmutex_init(&w->mutex)!=OS_OK) goto fail;
    mutex=true;
    if(os_wake_init(&w->wake)!=OS_OK) goto fail;
    wake=true;
    w->port=CreateIoCompletionPort(INVALID_HANDLE_VALUE,NULL,0,1);
    if(!w->port) goto fail;
    if(ep) {
        w->security=ep->security; ep->security=NULL; w->endpoint=true;
        memcpy(w->sid,ep->sid,sizeof w->sid); memcpy(w->pipe_name,ep->pipe,sizeof w->pipe_name);
        memcpy(w->publication_name,ep->publication,sizeof w->publication_name);
        SECURITY_ATTRIBUTES sa={sizeof sa,w->security,FALSE};
        w->arbitration=CreateMutexW(&sa,FALSE,ep->mutex);
        if(!w->arbitration) goto fail;
    }
    w->initialized=CreateEventW(NULL,TRUE,FALSE,NULL);
    if(!w->initialized) goto fail;
    if(os_thread_start(&w->thread,service,s)!=OS_OK) goto fail;
    WaitForSingleObject(w->initialized,INFINITE);
    rc=w->setup_result;
    if(rc!=IPC_OK) { (void)os_thread_join(&w->thread); goto fail; }
    CloseHandle(w->initialized); w->initialized=NULL;
    return IPC_OK;
fail:
    /* A listener can already have a queued connect. Closing the port after the
     * handle cancels it; no thread or buffer user survives this setup failure. */
    for(size_t i=0;i<PIPE_SLOTS;i++) if(s->peers[i].pipe!=INVALID_HANDLE_VALUE) {
        struct ipc_peer *p=&s->peers[i];
        (void)CancelIoEx(p->pipe,NULL);
        if(p->read_pending) { DWORD done; (void)GetOverlappedResult(p->pipe,&p->read,&done,TRUE); }
        CloseHandle(p->pipe);
    }
    if(w->port) CloseHandle(w->port);
    if(w->initialized) CloseHandle(w->initialized);
    if(w->published) UnmapViewOfFile(w->published);
    if(w->publication) CloseHandle(w->publication);
    if(w->arbitration) CloseHandle(w->arbitration);
    if(w->security) LocalFree(w->security);
    if(wake) os_wake_close(&w->wake);
    if(mutex) os_rmutex_destroy(&w->mutex);
    edit_arena_free(&s->arena); reset_server(s); return rc;
}
ipc_result ipc_server_init_display(ipc_server *s, const char *runtime, const char *display)
{
    if(!s) return IPC_INVALID;
    reset_server(s); endpoint ep; ipc_result rc=resolve(runtime,display,&ep);
    if(rc==IPC_OK) { rc=setup(s,&ep); if(ep.security) LocalFree(ep.security); }
    return rc;
}
ipc_result ipc_server_init(ipc_server *s, const char *runtime)
{ return ipc_server_init_display(s,runtime,NULL); }
ipc_result ipc_server_init_isolated(ipc_server *s)
{ return s?setup(s,NULL):IPC_INVALID; }
int ipc_server_fd(const ipc_server *s) { (void)s; return -1; }
const os_wake *ipc_server_wake(const ipc_server *s) { return s && s->win?&s->win->wake:NULL; }
ipc_result ipc_server_drain(ipc_server *s, ipc_open_callback callback, void *ctx)
{
    if(!s || !s->win || !callback) return IPC_INVALID;
    struct ipc_win *w=s->win; os_wake_drain(&w->wake); os_rmutex_lock(&w->mutex);
    w->request_deadline_ms=s->request_deadline_ms; w->evict_idle_ms=s->evict_idle_ms;
    size_t bytes=IPC_DRAIN_BYTES, calls=IPC_DRAIN_CALLBACKS; s->drain_bytes=0;
    for(size_t i=0;i<PIPE_SLOTS;i++) {
        struct ipc_peer *p=&s->peers[i];
        if(p->state!=READY) continue;
        size_t credit=p->expected-p->credited; if(credit>bytes) credit=bytes;
        p->credited+=credit; bytes-=credit; s->drain_bytes+=credit;
        if(p->credited!=p->expected || !calls) { os_wake_signal(&w->wake); continue; }
        DWORD available=0;
        if(!PeekNamedPipe(p->pipe,NULL,0,NULL,&available,NULL)) { drop_peer(s,p); continue; }
        if(available) { reject_peer(s,p,IPC_PROTOCOL); continue; }
        ipc_request r; ipc_result rc=ipc_wire_decode(p->wire,p->expected,&r);
        p->state=OPENED; p->opened=true;
        if(rc==IPC_OK) {
            p->wait=r.wait; calls--; rc=callback(&r,p->token,ctx);
            if(rc<IPC_OK || rc>IPC_REJECTED) rc=IPC_REJECTED;
        }
        p->result=rc; rx_release(w,p); reply(p->reply,'A',rc); p->reply_size=8;
    }
    s->wait_drops=w->wait_drops;
    ipc_result rc=w->failed?IPC_IO:IPC_OK;
    (void)PostQueuedCompletionStatus(w->port,0,0,NULL); os_rmutex_unlock(&w->mutex); return rc;
}
ipc_result ipc_server_report_closed(ipc_server *s, ipc_token token)
{
    if(!s || !s->win || !token) return IPC_INVALID;
    struct ipc_win *w=s->win; os_rmutex_lock(&w->mutex); ipc_result rc=IPC_INVALID;
    for(size_t i=0;i<PIPE_SLOTS;i++) {
        struct ipc_peer *p=&s->peers[i];
        if(p->state==OPENED && p->token==token && p->wait) { p->closed=true; rc=IPC_OK; break; }
    }
    if(rc==IPC_OK) (void)PostQueuedCompletionStatus(w->port,0,0,NULL);
    os_rmutex_unlock(&w->mutex); return rc;
}
bool ipc_server_token_live(const ipc_server *s, ipc_token token)
{
    if(!s || !s->win || !token) return false;
    os_rmutex_lock(&s->win->mutex); bool live=false;
    for(size_t i=0;i<PIPE_SLOTS;i++) if(s->peers[i].token==token) { live=peer_waiting(&s->peers[i]); break; }
    os_rmutex_unlock(&s->win->mutex); return live;
}
void ipc_server_fini(ipc_server *s)
{
    if(!s || !s->win) return;
    struct ipc_win *w=s->win; os_rmutex_lock(&w->mutex); w->stop=true;
    (void)PostQueuedCompletionStatus(w->port,0,0,NULL); os_rmutex_unlock(&w->mutex);
    (void)os_thread_join(&w->thread); CloseHandle(w->port);
    if(w->published) UnmapViewOfFile(w->published);
    if(w->publication) CloseHandle(w->publication);
    if(w->arbitration) CloseHandle(w->arbitration);
    if(w->security) LocalFree(w->security);
    os_wake_close(&w->wake); os_rmutex_destroy(&w->mutex); edit_arena_free(&s->arena); reset_server(s);
}
/* The launcher's CRT descriptors own overlapped pipe handles. W2.4 replaces
 * main's fork choreography with self-spawn; these keep the channel contract. */
ipc_result ipc_launcher_pair(int fds[2])
{
    if(!fds) return IPC_INVALID;
    fds[0]=fds[1]=-1;
    uint64_t nonce;
    if(os_random(&nonce,sizeof nonce)!=OS_OK) return IPC_IO;
    WCHAR name[NAME_CAP];
    if(swprintf(name,NAME_CAP,L"\\\\.\\pipe\\sublimite-launch-%llu-%llu",
        (unsigned long long)os_proc_id(),(unsigned long long)nonce)<0) return IPC_IO;
    endpoint ep; ipc_result rc=resolve(NULL,NULL,&ep);
    if(rc!=IPC_OK) return rc;
    SECURITY_ATTRIBUTES sa={sizeof sa,ep.security,FALSE};
    HANDLE server=CreateNamedPipeW(name,PIPE_ACCESS_DUPLEX|FILE_FLAG_OVERLAPPED|FILE_FLAG_FIRST_PIPE_INSTANCE,
        PIPE_TYPE_BYTE|PIPE_READMODE_BYTE|PIPE_WAIT|PIPE_REJECT_REMOTE_CLIENTS,1,IPC_RX_SMALL_SIZE,IPC_RX_SMALL_SIZE,0,&sa);
    LocalFree(ep.security);
    if(server==INVALID_HANDLE_VALUE) return IPC_IO;
    HANDLE client=CreateFileW(name,GENERIC_READ|GENERIC_WRITE,0,NULL,OPEN_EXISTING,
        FILE_FLAG_OVERLAPPED|SECURITY_SQOS_PRESENT|SECURITY_IDENTIFICATION,NULL);
    if(client==INVALID_HANDLE_VALUE) { CloseHandle(server); return IPC_IO; }
    /* The client connected before ConnectNamedPipe; no pending operation. */
    OVERLAPPED ov={0}; ov.hEvent=CreateEventW(NULL,TRUE,FALSE,NULL);
    if(!ov.hEvent) { CloseHandle(server); CloseHandle(client); return IPC_IO; }
    BOOL connected=ConnectNamedPipe(server,&ov); DWORD error=GetLastError(); CloseHandle(ov.hEvent);
    if(!connected && error!=ERROR_PIPE_CONNECTED) { CloseHandle(server); CloseHandle(client); return IPC_IO; }
    fds[0]=_open_osfhandle((intptr_t)server,_O_BINARY|_O_RDWR|_O_NOINHERIT);
    fds[1]=_open_osfhandle((intptr_t)client,_O_BINARY|_O_RDWR|_O_NOINHERIT);
    if(fds[0]<0 || fds[1]<0) {
        if(fds[0]>=0) (void)_close(fds[0]); else CloseHandle(server);
        if(fds[1]>=0) (void)_close(fds[1]); else CloseHandle(client);
        fds[0]=fds[1]=-1; return IPC_IO;
    }
    return IPC_OK;
}
ipc_result ipc_launcher_prepare(int *fd, char *name, size_t capacity)
{
    if(!fd || !name || !capacity) return IPC_INVALID;
    *fd=-1; name[0]=0;
    uint64_t nonce[2];
    if(os_random(nonce,sizeof nonce)!=OS_OK) return IPC_IO;
    char endpoint_name[NAME_CAP];
    int n=snprintf(endpoint_name,sizeof endpoint_name,"\\\\.\\pipe\\sublimite-launch-%016llx%016llx",
        (unsigned long long)nonce[0],(unsigned long long)nonce[1]);
    if(n<0 || (size_t)n>=capacity) return IPC_LIMIT;
    endpoint ep; ipc_result rc=resolve(NULL,NULL,&ep);
    if(rc!=IPC_OK) return rc;
    WCHAR wide[NAME_CAP];
    if(!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,endpoint_name,-1,wide,NAME_CAP)) {
        LocalFree(ep.security); return IPC_INVALID;
    }
    SECURITY_ATTRIBUTES sa={sizeof sa,ep.security,FALSE};
    HANDLE pipe=CreateNamedPipeW(wide,PIPE_ACCESS_DUPLEX|FILE_FLAG_OVERLAPPED|FILE_FLAG_FIRST_PIPE_INSTANCE,
        PIPE_TYPE_BYTE|PIPE_READMODE_BYTE|PIPE_WAIT|PIPE_REJECT_REMOTE_CLIENTS,1,IPC_RX_SMALL_SIZE,IPC_RX_SMALL_SIZE,0,&sa);
    LocalFree(ep.security);
    if(pipe==INVALID_HANDLE_VALUE) return IPC_IO;
    *fd=_open_osfhandle((intptr_t)pipe,_O_BINARY|_O_RDWR|_O_NOINHERIT);
    if(*fd<0) { CloseHandle(pipe); return IPC_IO; }
    memcpy(name,endpoint_name,(size_t)n+1u); return IPC_OK;
}
ipc_result ipc_server_connect_wait(ipc_server *s, const char *name, ipc_token *token)
{
    if(!s || !s->win || !name || !token || strncmp(name,"\\\\.\\pipe\\sublimite-launch-",strlen("\\\\.\\pipe\\sublimite-launch-"))) return IPC_INVALID;
    WCHAR wide[NAME_CAP]; BYTE sid[SECURITY_MAX_SID_SIZE];
    if(!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,name,-1,wide,NAME_CAP) || !current_sid(sid)) return IPC_INVALID;
    HANDLE pipe=CreateFileW(wide,GENERIC_READ|GENERIC_WRITE,0,NULL,OPEN_EXISTING,
        FILE_FLAG_OVERLAPPED|SECURITY_SQOS_PRESENT|SECURITY_IDENTIFICATION,NULL);
    if(pipe==INVALID_HANDLE_VALUE) return IPC_IO;
    if(!own_peer(pipe,sid,true)) { CloseHandle(pipe); return IPC_REJECTED; }
    int fd=_open_osfhandle((intptr_t)pipe,_O_BINARY|_O_RDWR|_O_NOINHERIT);
    if(fd<0) { CloseHandle(pipe); return IPC_IO; }
    ipc_result rc=ipc_server_adopt_wait(s,fd,token);
    if(rc!=IPC_OK) (void)_close(fd);
    return rc;
}
ipc_result ipc_server_adopt_wait(ipc_server *s, int fd, ipc_token *token)
{
    if(!s || !s->win || fd<0 || !token) return IPC_INVALID;
    HANDLE pipe=(HANDLE)ipc_crt_handle(fd);
    if(pipe==INVALID_HANDLE_VALUE) return IPC_INVALID;
    struct ipc_win *w=s->win; os_rmutex_lock(&w->mutex); ipc_result rc=IPC_LIMIT;
    size_t active=0; struct ipc_peer *slot=NULL;
    for(size_t i=0;i<PIPE_SLOTS;i++) {
        struct ipc_peer *p=&s->peers[i];
        if(p->state==FREE) slot=p;
        else if(p->state!=LISTENING && p->state!=DROPPING) active++;
    }
    if(s->next_token==UINT64_MAX) rc=IPC_INVALID;
    else if(slot && active<IPC_MAX_CLIENTS) {
        if(!CreateIoCompletionPort(pipe,w->port,(ULONG_PTR)slot,0)) rc=IPC_IO;
        else {
            slot->pipe=pipe; slot->crt_fd=fd; slot->token=++s->next_token;
            slot->state=OPENED; slot->opened=true; slot->wait=true; slot->reply_size=slot->sent=8;
            reply(slot->reply,'A',IPC_OK); *token=slot->token; rc=IPC_OK;
            (void)PostQueuedCompletionStatus(w->port,0,0,NULL);
        }
    }
    os_rmutex_unlock(&w->mutex); return rc;
}
ipc_result ipc_launcher_wait(int fd)
{
    if(fd<0) return IPC_INVALID;
    HANDLE pipe=(HANDLE)ipc_crt_handle(fd);
    if(pipe==INVALID_HANDLE_VALUE) return IPC_INVALID;
    DWORD flags=0;
    if(!GetNamedPipeInfo(pipe,&flags,NULL,NULL,NULL)) { (void)_close(fd); return IPC_INVALID; }
    if(flags&PIPE_SERVER_END) {
        OVERLAPPED ov={0}; ov.hEvent=CreateEventW(NULL,TRUE,FALSE,NULL);
        if(!ov.hEvent) { (void)_close(fd); return IPC_IO; }
        BOOL connected=ConnectNamedPipe(pipe,&ov); DWORD error=GetLastError(), done=0;
        if(!connected && error==ERROR_IO_PENDING) {
            DWORD waited=WaitForSingleObject(ov.hEvent,IPC_REQUEST_DEADLINE_MS);
            if(waited==WAIT_OBJECT_0) connected=GetOverlappedResult(pipe,&ov,&done,FALSE);
            else { (void)CancelIoEx(pipe,&ov); (void)GetOverlappedResult(pipe,&ov,&done,TRUE); }
        } else if(!connected && error==ERROR_PIPE_CONNECTED) connected=TRUE;
        CloseHandle(ov.hEvent);
        BYTE sid[SECURITY_MAX_SID_SIZE];
        if(!connected || !current_sid(sid) || !own_peer(pipe,sid,false)) { (void)_close(fd); return IPC_IO; }
    }
    ipc_result rc=receive_reply(pipe,'C',UINT64_MAX); (void)_close(fd); return rc;
}
#endif
