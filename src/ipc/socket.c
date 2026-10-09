#include "ipc.h"
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/timerfd.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#define TIMER_EVENT UINT64_MAX /* epoll tag of the request-deadline timerfd */

struct ipc_peer {
    int fd;
    ipc_token token;
    uint8_t *wire;      /* pooled receive slab, held only between header and callback */
    bool wire_big;
    uint8_t head[24];
    size_t received, expected, sent, reply_size;
    uint8_t reply[16];
    bool opened, wait, closed;
    uint64_t accepted_ms, active_ms;
};
/* Shared receive pool: IPC_RX_SMALL_COUNT slabs of IPC_RX_SMALL_SIZE and
 * IPC_RX_BIG_COUNT slabs of IPC_MAX_WIRE (edit-457.21 review §14). */
struct ipc_rx {
    uint8_t *small[IPC_RX_SMALL_COUNT], *big[IPC_RX_BIG_COUNT];
    size_t nsmall, nbig;
    uint64_t timer_ms; /* armed absolute deadline, 0 = disarmed */
};
struct budget { size_t bytes, callbacks, accepts; uint64_t now; };

typedef struct endpoint {
    struct sockaddr_un addr;
    socklen_t len;
    bool abstract;
    int dir_fd;      /* pinned, verified runtime directory (O_PATH); -1 for abstract */
    char leaf[64];   /* socket name inside dir_fd */
} endpoint;

static uint64_t milliseconds(void)
{
    struct timespec ts; (void)clock_gettime(CLOCK_MONOTONIC,&ts);
    return (uint64_t)ts.tv_sec*1000u+(uint64_t)ts.tv_nsec/1000000u;
}
/* Peer credentials. EDIT_IPC_TEST_PEER_UID is a test hook for the abstract
 * namespace only: it names the uid a peer must have, so tests can model a
 * foreign squatter without a second user. Filesystem endpoints ignore it. */
static bool own_peer(int fd, bool abstract)
{
    uid_t want=getuid();
    if(abstract) { const char *h=getenv("EDIT_IPC_TEST_PEER_UID"); if(h!=NULL && h[0]) want=(uid_t)strtoul(h,NULL,10); }
    struct ucred cred; socklen_t n=sizeof cred;
    return getsockopt(fd,SOL_SOCKET,SO_PEERCRED,&cred,&n)==0 && cred.uid==want;
}
static void ep_close(endpoint *ep)
{
    if(ep->dir_fd>=0) close(ep->dir_fd);
    ep->dir_fd=-1;
}
/* Open and verify the runtime directory once; every later operation goes
 * through this descriptor, so renaming or replacing the path afterwards
 * cannot redirect lock, bind, chmod or unlink (edit-457.21 review §2). */
static ipc_result resolve_dir(const char *runtime, endpoint *ep)
{
    if(runtime[0]!='/') return IPC_INVALID;
    int fd=open(runtime,O_PATH|O_DIRECTORY|O_CLOEXEC);
    if(fd<0) return IPC_INVALID;
    struct stat st;
    if(fstat(fd,&st)!=0 || !S_ISDIR(st.st_mode) || st.st_uid!=getuid() || (st.st_mode&077u)!=0) { close(fd); return IPC_INVALID; }
    int n=snprintf(ep->leaf,sizeof ep->leaf,"sublimite-%lu.sock",(unsigned long)getuid());
    if(n<0 || (size_t)n>=sizeof ep->leaf) { close(fd); return IPC_LIMIT; }
    n=snprintf(ep->addr.sun_path,sizeof ep->addr.sun_path,"/proc/self/fd/%d/%s",fd,ep->leaf);
    if(n<0 || (size_t)n>=sizeof ep->addr.sun_path) { close(fd); return IPC_LIMIT; }
    ep->len=(socklen_t)(offsetof(struct sockaddr_un,sun_path)+(size_t)n+1u);
    ep->dir_fd=fd; ep->abstract=false; return IPC_OK;
}
static ipc_result resolve(const char *runtime, endpoint *ep)
{
    memset(ep,0,sizeof *ep); ep->addr.sun_family=AF_UNIX; ep->dir_fd=-1;
    if(runtime==NULL) runtime=getenv("XDG_RUNTIME_DIR");
    if(runtime!=NULL) return resolve_dir(runtime,ep);
    struct sockaddr_un *a=&ep->addr; int n;
    const char *ns=getenv("EDIT_IPC_NAMESPACE");
    if(ns!=NULL) {
        if(ns[0]=='\0') return IPC_INVALID;
        n=snprintf(a->sun_path+1,sizeof a->sun_path-1,"sublimite-%lu-%s",(unsigned long)getuid(),ns);
    } else n=snprintf(a->sun_path+1,sizeof a->sun_path-1,"sublimite-%lu",(unsigned long)getuid());
    if(n<0 || (size_t)n>=sizeof a->sun_path-1) return IPC_LIMIT;
    ep->len=(socklen_t)(offsetof(struct sockaddr_un,sun_path)+1u+(size_t)n);
    ep->abstract=true; return IPC_OK;
}
/* Private filesystem endpoint used when another uid has squatted the abstract
 * name (review §13): EDIT_IPC_FALLBACK_DIR (test hook) or /run/user/<uid>. */
static ipc_result resolve_fallback(endpoint *ep)
{
    memset(ep,0,sizeof *ep); ep->addr.sun_family=AF_UNIX; ep->dir_fd=-1;
    char buf[64]; const char *d=getenv("EDIT_IPC_FALLBACK_DIR");
    if(d==NULL) { (void)snprintf(buf,sizeof buf,"/run/user/%lu",(unsigned long)getuid()); d=buf; }
    return resolve_dir(d,ep);
}
static ipc_result ready(int fd, short events, uint64_t deadline)
{
    for(;;) {
        int timeout=-1;
        if(deadline!=UINT64_MAX) {
            uint64_t now=milliseconds(); if(now>=deadline) return IPC_TIMEOUT;
            uint64_t left=deadline-now; timeout=left>2147483647u?2147483647:(int)left;
        }
        struct pollfd p={fd,events,0}; int n=poll(&p,1,timeout);
        if(n>0) return IPC_OK;
        if(n==0) return IPC_TIMEOUT;
        if(errno!=EINTR) return IPC_IO;
    }
}
static ipc_result connect_socket(const endpoint *ep, uint64_t deadline, int *out)
{
    *out=-1; int fd=socket(AF_UNIX,SOCK_STREAM|SOCK_NONBLOCK|SOCK_CLOEXEC,0);
    if(fd<0) return IPC_IO;
    if(connect(fd,(const struct sockaddr *)&ep->addr,ep->len)!=0) {
        if(errno!=EINPROGRESS) { int saved=errno; close(fd); errno=saved; return IPC_IO; }
        ipc_result rc=ready(fd,POLLOUT,deadline);
        if(rc!=IPC_OK) { close(fd); return rc; }
        int err=0; socklen_t n=sizeof err;
        if(getsockopt(fd,SOL_SOCKET,SO_ERROR,&err,&n)!=0 || err!=0) { if(err) errno=err; int saved=errno; close(fd); errno=saved; return IPC_IO; }
    }
    if(!own_peer(fd,ep->abstract)) { close(fd); return IPC_REJECTED; }
    *out=fd; return IPC_OK;
}
static ipc_result transfer(int fd, uint8_t *bytes, size_t n, bool sending, uint64_t deadline)
{
    size_t pos=0;
    while(pos<n) {
        ssize_t got=sending?send(fd,bytes+pos,n-pos,MSG_NOSIGNAL):recv(fd,bytes+pos,n-pos,0);
        if(got>0) { pos+=(size_t)got; continue; }
        if(got==0) return IPC_IO;
        if(errno==EINTR) continue;
        if(errno!=EAGAIN && errno!=EWOULDBLOCK) return IPC_IO;
        ipc_result rc=ready(fd,sending?POLLOUT:POLLIN,deadline); if(rc!=IPC_OK) return rc;
    }
    return IPC_OK;
}
static void reply(uint8_t *p, char kind, ipc_result rc)
{
    memcpy(p,"EDIR",4); p[4]=(uint8_t)kind; p[5]=(uint8_t)rc; p[6]=0; p[7]=0;
}
static ipc_result receive_reply(int fd, char kind, uint64_t deadline)
{
    uint8_t bytes[8]; ipc_result rc=transfer(fd,bytes,sizeof bytes,false,deadline);
    if(rc!=IPC_OK) return rc;
    if(memcmp(bytes,"EDIR",4)!=0 || bytes[4]!=(uint8_t)kind || bytes[5]>IPC_REJECTED || bytes[6] || bytes[7]) return IPC_PROTOCOL;
    return (ipc_result)bytes[5];
}
static ipc_result deliver(const endpoint *ep, uint8_t *wire, size_t n, bool wait, uint64_t deadline)
{
    int fd=-1; ipc_result rc=connect_socket(ep,deadline,&fd);
    if(rc==IPC_OK) rc=transfer(fd,wire,n,true,deadline);
    if(rc==IPC_OK) rc=receive_reply(fd,'A',deadline);
    if(rc==IPC_OK && wait) rc=receive_reply(fd,'C',UINT64_MAX);
    if(fd>=0) close(fd);
    return rc;
}
ipc_result ipc_client_send(const char *runtime, const ipc_request *r, int timeout_ms)
{
    if(timeout_ms < -1) return IPC_INVALID;
    endpoint ep; ipc_result rc=resolve(runtime,&ep);
    if(rc!=IPC_OK) return rc;
    edit_arena arena={0}; if(edit_arena_init(&arena,IPC_MAX_WIRE)!=0) { ep_close(&ep); return IPC_IO; }
    uint8_t *wire=edit_arena_alloc(&arena,IPC_MAX_WIRE,1); size_t n=0;
    rc=ipc_wire_encode(r,wire,IPC_MAX_WIRE,&n);
    uint64_t deadline=timeout_ms<0?UINT64_MAX:milliseconds()+(uint64_t)timeout_ms;
    if(rc==IPC_OK) rc=deliver(&ep,wire,n,r->wait,deadline);
    if(rc==IPC_REJECTED && ep.abstract) {
        /* Foreign uid on the abstract name; nothing was sent. Try the private path. */
        endpoint fb; if(resolve_fallback(&fb)==IPC_OK) {
            ipc_result r2=deliver(&fb,wire,n,r->wait,deadline); ep_close(&fb);
            if(r2!=IPC_IO) rc=r2;
        }
    }
    ep_close(&ep); edit_arena_free(&arena); return rc;
}

/* ---- receive pool ----------------------------------------------------- */
static uint8_t *rx_acquire(struct ipc_rx *rx, size_t need, bool *big)
{
    if(need<=IPC_RX_SMALL_SIZE && rx->nsmall) { *big=false; return rx->small[--rx->nsmall]; }
    if(rx->nbig) { *big=true; return rx->big[--rx->nbig]; }
    return NULL;
}
static void rx_release(struct ipc_rx *rx, uint8_t *wire, bool big)
{
    if(wire==NULL) return;
    if(big) rx->big[rx->nbig++]=wire; else rx->small[rx->nsmall++]=wire;
}

static bool peer_waiting(const struct ipc_peer *p)
{
    return p->wait && p->opened && !p->closed && (p->reply_size==0 || p->reply[5]==IPC_OK);
}
static void drop_peer(ipc_server *s, struct ipc_peer *p)
{
    if(p->fd>=0) {
        if(peer_waiting(p)) s->wait_drops++;
        (void)epoll_ctl(s->fd,EPOLL_CTL_DEL,p->fd,NULL); close(p->fd);
    }
    rx_release(s->rx,p->wire,p->wire_big);
    memset(p,0,sizeof *p); p->fd=-1;
}
static void interest(ipc_server *s, struct ipc_peer *p)
{
    struct epoll_event ev={0}; ev.data.u64=p->token;
    ev.events=EPOLLIN|EPOLLRDHUP;
    if(p->sent<p->reply_size) ev.events|=EPOLLOUT;
    if(epoll_ctl(s->fd,EPOLL_CTL_MOD,p->fd,&ev)!=0) drop_peer(s,p);
}
static void flush_peer(ipc_server *s, struct ipc_peer *p)
{
    if(p->closed && p->reply_size==8 && p->reply[5]==IPC_OK) {
        reply(p->reply+8,'C',IPC_OK); p->reply_size=16;
    }
    while(p->sent<p->reply_size) {
        ssize_t n=send(p->fd,p->reply+p->sent,p->reply_size-p->sent,MSG_NOSIGNAL);
        if(n>0) { p->sent+=(size_t)n; continue; }
        if(n<0 && errno==EINTR) continue;
        if(n<0 && (errno==EAGAIN || errno==EWOULDBLOCK)) { interest(s,p); return; }
        drop_peer(s,p); return;
    }
    if(!p->wait || p->closed || p->reply[5]!=IPC_OK) drop_peer(s,p);
    else interest(s,p);
}
static void reject_peer(ipc_server *s, struct ipc_peer *p, ipc_result rc)
{
    p->opened=true; reply(p->reply,'A',rc); p->reply_size=8; flush_peer(s,p);
}
static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1]<<8 | (uint32_t)p[2]<<16 | (uint32_t)p[3]<<24;
}
static void process_peer(ipc_server *s, struct ipc_peer *p, ipc_open_callback callback, void *ctx, struct budget *b)
{
    if(p->opened) {
        uint8_t extra; ssize_t n=recv(p->fd,&extra,1,MSG_PEEK);
        if(n==0 || n>0 || (n<0 && errno!=EAGAIN && errno!=EWOULDBLOCK && errno!=EINTR)) { drop_peer(s,p); return; }
        flush_peer(s,p); return;
    }
    for(;;) {
        /* Work budget (review §10): unread bytes stay in the kernel and the
         * level-triggered epoll fd keeps the loop awake. A callback is only
         * ever reached through a recv, so it always has budget. */
        if(b->bytes==0 || b->callbacks==0) return;
        size_t target=p->expected?p->expected:24u;
        uint8_t *dst=p->wire!=NULL?p->wire:p->head;
        size_t want=target-p->received; if(want>b->bytes) want=b->bytes;
        ssize_t n=recv(p->fd,dst+p->received,want,0);
        if(n>0) { p->received+=(size_t)n; b->bytes-=(size_t)n; s->drain_bytes+=(uint64_t)n; p->active_ms=b->now; }
        else if(n<0 && errno==EINTR) continue;
        else if(n<0 && (errno==EAGAIN || errno==EWOULDBLOCK)) return;
        else { drop_peer(s,p); return; }
        if(p->received<target) continue;
        if(!p->expected) {
            p->expected=le32(p->head+8);
            if(memcmp(p->head,"EDIP",4)!=0 || p->expected<24 || p->expected>IPC_MAX_WIRE) { reject_peer(s,p,IPC_PROTOCOL); return; }
            p->wire=rx_acquire(s->rx,p->expected,&p->wire_big);
            if(p->wire==NULL) { reject_peer(s,p,IPC_BUSY); return; } /* bounded admission */
            memcpy(p->wire,p->head,24);
            if(p->received<p->expected) continue;
        }
        ipc_request r; ipc_result rc=ipc_wire_decode(p->wire,p->received,&r);
        uint8_t extra;
        if(recv(p->fd,&extra,1,MSG_PEEK)>0) rc=IPC_PROTOCOL;
        p->opened=true;
        if(rc==IPC_OK) {
            p->wait=r.wait; b->callbacks--; rc=callback(&r,p->token,ctx);
            if(rc<IPC_OK || rc>IPC_REJECTED) rc=IPC_REJECTED;
        }
        rx_release(s->rx,p->wire,p->wire_big); p->wire=NULL; /* storage is only needed until the callback returns */
        reply(p->reply,'A',rc); p->reply_size=8; flush_peer(s,p); return;
    }
}
ipc_result ipc_server_report_closed(ipc_server *s, ipc_token token)
{
    if(s==NULL || s->peers==NULL || !token) return IPC_INVALID;
    for(size_t i=0;i<IPC_MAX_CLIENTS;i++) {
        struct ipc_peer *p=&s->peers[i];
        if(p->fd>=0 && p->token==token && p->opened && p->wait) {
            p->closed=true;
            /* Callback can report immediate closure before its ACK is built. */
            if(p->reply_size) flush_peer(s,p);
            return IPC_OK;
        }
    }
    return IPC_INVALID;
}
bool ipc_server_token_live(const ipc_server *s, ipc_token token)
{
    if(s==NULL || s->peers==NULL || !token) return false;
    for(size_t i=0;i<IPC_MAX_CLIENTS;i++) {
        const struct ipc_peer *p=&s->peers[i];
        if(p->fd>=0 && p->token==token) return peer_waiting(p);
    }
    return false;
}
/* Request deadlines (review §11): a timerfd, armed only while an incomplete
 * request exists, wakes the loop; expiry closes the peer without a reply. */
static void expire_peers(ipc_server *s, uint64_t now)
{
    for(size_t i=0;i<IPC_MAX_CLIENTS;i++) {
        struct ipc_peer *p=&s->peers[i];
        if(p->fd>=0 && !p->opened && now>=p->accepted_ms+s->request_deadline_ms) drop_peer(s,p);
    }
}
static void rearm_timer(ipc_server *s)
{
    uint64_t earliest=0;
    for(size_t i=0;i<IPC_MAX_CLIENTS;i++) {
        const struct ipc_peer *p=&s->peers[i];
        if(p->fd>=0 && !p->opened) { uint64_t d=p->accepted_ms+s->request_deadline_ms; if(earliest==0 || d<earliest) earliest=d; }
    }
    if(earliest==s->rx->timer_ms) return;
    struct itimerspec its; memset(&its,0,sizeof its);
    if(earliest) { its.it_value.tv_sec=(time_t)(earliest/1000u); its.it_value.tv_nsec=(long)(earliest%1000u)*1000000L; }
    if(earliest && its.it_value.tv_sec==0 && its.it_value.tv_nsec==0) its.it_value.tv_nsec=1;
    if(timerfd_settime(s->timer_fd,TFD_TIMER_ABSTIME,&its,NULL)==0) s->rx->timer_ms=earliest;
}
/* Free a slot for a newcomer by closing the longest-idle incomplete client. */
static struct ipc_peer *take_slot(ipc_server *s, uint64_t now)
{
    struct ipc_peer *victim=NULL;
    for(size_t i=0;i<IPC_MAX_CLIENTS;i++) {
        struct ipc_peer *p=&s->peers[i];
        if(p->fd<0) return p;
        if(!p->opened && now-p->active_ms>=s->evict_idle_ms && (victim==NULL || p->active_ms<victim->active_ms)) victim=p;
    }
    if(victim!=NULL) drop_peer(s,victim);
    return victim;
}
ipc_result ipc_server_drain(ipc_server *s, ipc_open_callback callback, void *ctx)
{
    if(s==NULL || s->peers==NULL || callback==NULL) return IPC_INVALID;
    struct budget b={IPC_DRAIN_BYTES,IPC_DRAIN_CALLBACKS,IPC_DRAIN_ACCEPTS,milliseconds()};
    s->drain_bytes=0;
    expire_peers(s,b.now);
    struct epoll_event events[64];
    for(size_t batch=0;batch<2;batch++) {
        int n=epoll_wait(s->fd,events,64,0);
        if(n<0) { if(errno==EINTR) continue; return IPC_IO; }
        if(n==0) break;
        for(int k=0;k<n;k++) {
            if(events[k].data.u64==TIMER_EVENT) {
                uint64_t ticks; (void)!read(s->timer_fd,&ticks,sizeof ticks);
                s->rx->timer_ms=0; expire_peers(s,milliseconds());
            } else if(events[k].data.u64==0) {
                while(b.accepts>0) {
                    int fd=accept4(s->listener,NULL,NULL,SOCK_NONBLOCK|SOCK_CLOEXEC);
                    if(fd<0) { if(errno==EINTR) continue; if(errno==EAGAIN || errno==EWOULDBLOCK) break; return IPC_IO; }
                    b.accepts--;
                    struct ipc_peer *p=take_slot(s,b.now);
                    if(p==NULL || !own_peer(fd,!s->owns_path) || s->next_token==UINT64_MAX) { close(fd); continue; }
                    p->fd=fd; p->token=++s->next_token; p->accepted_ms=p->active_ms=b.now;
                    struct epoll_event ev={0}; ev.events=EPOLLIN|EPOLLRDHUP; ev.data.u64=p->token;
                    if(epoll_ctl(s->fd,EPOLL_CTL_ADD,fd,&ev)!=0) { drop_peer(s,p); continue; }
                    process_peer(s,p,callback,ctx,&b);
                }
            } else {
                for(size_t i=0;i<IPC_MAX_CLIENTS;i++) {
                    struct ipc_peer *p=&s->peers[i];
                    if(p->fd>=0 && p->token==events[k].data.u64) { process_peer(s,p,callback,ctx,&b); break; }
                }
            }
        }
    }
    rearm_timer(s);
    return IPC_OK;
}
int ipc_server_fd(const ipc_server *s) { return s!=NULL?s->fd:-1; }

static ipc_result lock_with_deadline(int fd)
{
    uint64_t end=milliseconds()+IPC_LOCK_TIMEOUT_MS;
    for(;;) {
        if(flock(fd,LOCK_EX|LOCK_NB)==0) return IPC_OK;
        if(errno==EINTR) continue;
        if(errno!=EWOULDBLOCK) return IPC_IO;
        if(milliseconds()>=end) return IPC_TIMEOUT;
        struct timespec ts={0,1000000L}; (void)nanosleep(&ts,NULL);
    }
}
static void reset_server(ipc_server *s)
{
    memset(s,0,sizeof *s);
    s->fd=-1; s->listener=-1; s->lock_fd=-1; s->dir_fd=-1; s->timer_fd=-1;
    s->request_deadline_ms=IPC_REQUEST_DEADLINE_MS; s->evict_idle_ms=IPC_EVICT_IDLE_MS;
}
/* locked: the caller holds (or never needed) the lifecycle lock, so the
 * socket may be unlinked if it is still ours. */
static void teardown(ipc_server *s, bool locked)
{
    if(s->peers!=NULL && s->rx!=NULL) for(size_t i=0;i<IPC_MAX_CLIENTS;i++) drop_peer(s,&s->peers[i]);
    if(s->listener>=0) close(s->listener);
    if(s->fd>=0) close(s->fd);
    if(s->timer_fd>=0) close(s->timer_fd);
    if(s->owns_path && locked && s->dir_fd>=0) {
        struct stat st;
        if(fstatat(s->dir_fd,s->socket_path,&st,AT_SYMLINK_NOFOLLOW)==0 && (uint64_t)st.st_dev==s->socket_device && (uint64_t)st.st_ino==s->socket_inode)
            (void)unlinkat(s->dir_fd,s->socket_path,0);
    }
    if(s->lock_fd>=0) close(s->lock_fd);
    if(s->dir_fd>=0) close(s->dir_fd);
    edit_arena_free(&s->arena); reset_server(s);
}
void ipc_server_fini(ipc_server *s)
{
    if(s==NULL) return;
    bool locked=s->lock_fd<0;
    /* A bounded wait: if the lock cannot be taken the socket is left for the
     * next startup's stale-socket recovery rather than unlinked blind. */
    if(s->lock_fd>=0) locked=lock_with_deadline(s->lock_fd)==IPC_OK;
    teardown(s,locked);
}
/* Epoll fd, deadline timer, peer table and the receive pool. */
static ipc_result setup_runtime(ipc_server *s)
{
    size_t size=(size_t)IPC_RX_SMALL_COUNT*IPC_RX_SMALL_SIZE+(size_t)IPC_RX_BIG_COUNT*IPC_MAX_WIRE
        +IPC_MAX_CLIENTS*sizeof(struct ipc_peer)+sizeof(struct ipc_rx)+4096u;
    s->fd=epoll_create1(EPOLL_CLOEXEC);
    s->timer_fd=timerfd_create(CLOCK_MONOTONIC,TFD_NONBLOCK|TFD_CLOEXEC);
    if(s->fd<0 || s->timer_fd<0 || edit_arena_init(&s->arena,size)!=0) return IPC_IO;
    s->peers=edit_arena_alloc(&s->arena,IPC_MAX_CLIENTS*sizeof *s->peers,_Alignof(struct ipc_peer));
    s->rx=edit_arena_alloc(&s->arena,sizeof *s->rx,_Alignof(struct ipc_rx));
    if(s->peers==NULL || s->rx==NULL) return IPC_IO;
    memset(s->peers,0,IPC_MAX_CLIENTS*sizeof *s->peers); memset(s->rx,0,sizeof *s->rx);
    for(size_t i=0;i<IPC_MAX_CLIENTS;i++) s->peers[i].fd=-1;
    for(size_t i=0;i<IPC_RX_SMALL_COUNT;i++) if((s->rx->small[i]=edit_arena_alloc(&s->arena,IPC_RX_SMALL_SIZE,16))==NULL) return IPC_IO;
    for(size_t i=0;i<IPC_RX_BIG_COUNT;i++) if((s->rx->big[i]=edit_arena_alloc(&s->arena,IPC_MAX_WIRE,16))==NULL) return IPC_IO;
    s->rx->nsmall=IPC_RX_SMALL_COUNT; s->rx->nbig=IPC_RX_BIG_COUNT;
    struct epoll_event ev={0}; ev.events=EPOLLIN; ev.data.u64=TIMER_EVENT;
    if(epoll_ctl(s->fd,EPOLL_CTL_ADD,s->timer_fd,&ev)!=0) return IPC_IO;
    if(s->listener>=0) { ev.data.u64=0; if(epoll_ctl(s->fd,EPOLL_CTL_ADD,s->listener,&ev)!=0) return IPC_IO; }
    return IPC_OK;
}
/* Bind the endpoint. *foreign is set when an abstract name is held by another
 * uid (or by something that never listens), so the caller can fall back. */
static ipc_result start_endpoint(ipc_server *s, endpoint *ep, bool *foreign)
{
    ipc_result rc; int lock=-1; *foreign=false;
    s->dir_fd=ep->dir_fd; ep->dir_fd=-1; /* the server owns the pinned directory from here on */
    struct stat st;
    if(!ep->abstract) {
        char lockleaf[64]; size_t ln=strlen(ep->leaf);
        memcpy(lockleaf,ep->leaf,ln+1); memcpy(lockleaf+ln-4,"lock",4);
        lock=openat(s->dir_fd,lockleaf,O_CREAT|O_RDWR|O_CLOEXEC|O_NOFOLLOW,0600);
        if(lock<0) { rc=IPC_IO; goto done; }
        if(fstat(lock,&st)!=0 || !S_ISREG(st.st_mode) || st.st_uid!=getuid() || (st.st_mode&077u)!=0) { rc=IPC_INVALID; goto done; }
        rc=lock_with_deadline(lock);
        if(rc!=IPC_OK) goto done;
        if(fstatat(s->dir_fd,ep->leaf,&st,AT_SYMLINK_NOFOLLOW)==0) {
            if(!S_ISSOCK(st.st_mode) || st.st_uid!=getuid()) { rc=IPC_INVALID; goto done; }
            int probe=-1; rc=connect_socket(ep,milliseconds()+1000u,&probe);
            if(rc==IPC_OK) { close(probe); rc=IPC_EXISTS; goto done; }
            if(rc!=IPC_IO || errno!=ECONNREFUSED) { rc=IPC_EXISTS; goto done; }
            if(unlinkat(s->dir_fd,ep->leaf,0)!=0) { rc=IPC_IO; goto done; }
        } else if(errno!=ENOENT) { rc=IPC_IO; goto done; }
    }
    s->listener=socket(AF_UNIX,SOCK_STREAM|SOCK_NONBLOCK|SOCK_CLOEXEC,0);
    if(s->listener<0) { rc=IPC_IO; goto done; }
    if(bind(s->listener,(struct sockaddr *)&ep->addr,ep->len)!=0) {
        if(errno!=EADDRINUSE) { rc=IPC_IO; goto done; }
        rc=IPC_EXISTS;
        if(ep->abstract) for(int attempt=0;attempt<3;attempt++) {
            int probe=-1; ipc_result pr=connect_socket(ep,milliseconds()+1000u,&probe);
            if(pr==IPC_OK) { close(probe); break; }                                  /* ours: a live incumbent */
            if(pr==IPC_REJECTED) { *foreign=true; break; }                           /* another uid */
            if(pr!=IPC_IO || errno!=ECONNREFUSED) break;
            if(attempt==2) *foreign=true; else { struct timespec ts={0,10000000L}; (void)nanosleep(&ts,NULL); }
        }
        goto done;
    }
    if(!ep->abstract) {
        if(fstatat(s->dir_fd,ep->leaf,&st,AT_SYMLINK_NOFOLLOW)!=0 || !S_ISSOCK(st.st_mode) || st.st_uid!=getuid()) { rc=IPC_IO; goto done; }
        memcpy(s->socket_path,ep->leaf,strlen(ep->leaf)+1); s->owns_path=true;
        s->socket_device=(uint64_t)st.st_dev; s->socket_inode=(uint64_t)st.st_ino;
        /* chmod the exact inode we verified, never whatever the name now points at. */
        int pin=openat(s->dir_fd,ep->leaf,O_PATH|O_NOFOLLOW|O_CLOEXEC);
        struct stat ps; char proc[64];
        if(pin<0) { rc=IPC_IO; goto done; }
        (void)snprintf(proc,sizeof proc,"/proc/self/fd/%d",pin);
        bool same=fstat(pin,&ps)==0 && (uint64_t)ps.st_dev==s->socket_device && (uint64_t)ps.st_ino==s->socket_inode;
        int cr=same?chmod(proc,0600):-1; close(pin);
        if(cr!=0) { rc=IPC_IO; goto done; }
    }
    if(listen(s->listener,(int)IPC_MAX_CLIENTS)!=0) { rc=IPC_IO; goto done; }
    rc=setup_runtime(s);
done:
    if(rc!=IPC_OK) {
        teardown(s,true);
        if(lock>=0) close(lock);
    } else if(lock>=0) {
        s->lock_fd=lock;
        (void)flock(lock,LOCK_UN);
    }
    return rc;
}
ipc_result ipc_server_init(ipc_server *s, const char *runtime)
{
    if(s==NULL) return IPC_INVALID;
    reset_server(s);
    endpoint ep; ipc_result rc=resolve(runtime,&ep);
    if(rc!=IPC_OK) return rc;
    bool foreign=false; bool abstract=ep.abstract;
    rc=start_endpoint(s,&ep,&foreign);
    ep_close(&ep);
    if(rc!=IPC_OK && abstract && foreign) {
        reset_server(s);
        if(resolve_fallback(&ep)!=IPC_OK) return IPC_EXISTS; /* no private directory: report the squat */
        rc=start_endpoint(s,&ep,&foreign); ep_close(&ep);
    }
    return rc;
}
ipc_result ipc_server_init_isolated(ipc_server *s)
{
    if(s==NULL) return IPC_INVALID;
    reset_server(s);
    ipc_result rc=setup_runtime(s);
    if(rc!=IPC_OK) teardown(s,true);
    return rc;
}
ipc_result ipc_launcher_pair(int fds[2])
{
    if(fds==NULL) return IPC_INVALID;
    return socketpair(AF_UNIX,SOCK_STREAM|SOCK_CLOEXEC,0,fds)==0?IPC_OK:IPC_IO;
}
ipc_result ipc_server_adopt_wait(ipc_server *s, int fd, ipc_token *token)
{
    if(s==NULL || s->peers==NULL || fd<0 || token==NULL || s->next_token==UINT64_MAX) return IPC_INVALID;
    struct ipc_peer *p=NULL;
    for(size_t i=0;i<IPC_MAX_CLIENTS;i++) if(s->peers[i].fd<0) { p=&s->peers[i]; break; }
    if(p==NULL) return IPC_LIMIT;
    int fl=fcntl(fd,F_GETFL); if(fl<0 || fcntl(fd,F_SETFL,fl|O_NONBLOCK)!=0) return IPC_IO;
    p->fd=fd; p->token=++s->next_token; p->opened=true; p->wait=true;
    p->accepted_ms=p->active_ms=milliseconds();
    reply(p->reply,'A',IPC_OK); p->reply_size=8; p->sent=8; /* the launcher expects only the closed reply */
    struct epoll_event ev={0}; ev.events=EPOLLIN|EPOLLRDHUP; ev.data.u64=p->token;
    if(epoll_ctl(s->fd,EPOLL_CTL_ADD,fd,&ev)!=0) { memset(p,0,sizeof *p); p->fd=-1; return IPC_IO; }
    *token=p->token; return IPC_OK;
}
ipc_result ipc_launcher_wait(int fd)
{
    if(fd<0) return IPC_INVALID;
    ipc_result rc=receive_reply(fd,'C',UINT64_MAX);
    close(fd); return rc;
}
