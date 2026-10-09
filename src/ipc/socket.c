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
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

struct ipc_peer {
    int fd;
    ipc_token token;
    uint8_t *wire;
    size_t received, expected, sent, reply_size;
    uint8_t reply[16];
    bool opened, wait, closed;
};
static ipc_result address(const char *runtime, struct sockaddr_un *a, socklen_t *len)
{
    memset(a,0,sizeof *a); a->sun_family=AF_UNIX;
    if(runtime==NULL) runtime=getenv("XDG_RUNTIME_DIR");
    int n;
    if(runtime!=NULL) {
        struct stat st;
        if(runtime[0]!='/' || stat(runtime,&st)!=0 || !S_ISDIR(st.st_mode) || st.st_uid!=getuid() || (st.st_mode&077u)!=0) return IPC_INVALID;
        n=snprintf(a->sun_path,sizeof a->sun_path,"%s/edit-%lu.sock",runtime,(unsigned long)getuid());
        if(n<0 || (size_t)n>=sizeof a->sun_path) return IPC_LIMIT;
        *len=(socklen_t)(offsetof(struct sockaddr_un,sun_path)+(size_t)n+1u);
    } else {
        const char *ns=getenv("EDIT_IPC_NAMESPACE");
        if(ns!=NULL) {
            if(ns[0]=='\0') return IPC_INVALID;
            n=snprintf(a->sun_path+1,sizeof a->sun_path-1,"edit-%lu-%s",(unsigned long)getuid(),ns);
        } else n=snprintf(a->sun_path+1,sizeof a->sun_path-1,"edit-%lu",(unsigned long)getuid());
        if(n<0 || (size_t)n>=sizeof a->sun_path-1) return IPC_LIMIT;
        *len=(socklen_t)(offsetof(struct sockaddr_un,sun_path)+1u+(size_t)n);
    }
    return IPC_OK;
}
static bool own_peer(int fd)
{
    struct ucred cred; socklen_t n=sizeof cred;
    return getsockopt(fd,SOL_SOCKET,SO_PEERCRED,&cred,&n)==0 && cred.uid==getuid();
}
static uint64_t milliseconds(void)
{
    struct timespec ts; (void)clock_gettime(CLOCK_MONOTONIC,&ts);
    return (uint64_t)ts.tv_sec*1000u+(uint64_t)ts.tv_nsec/1000000u;
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
static ipc_result connect_socket(const struct sockaddr_un *a, socklen_t len, uint64_t deadline, int *out)
{
    *out=-1; int fd=socket(AF_UNIX,SOCK_STREAM|SOCK_NONBLOCK|SOCK_CLOEXEC,0);
    if(fd<0) return IPC_IO;
    if(connect(fd,(const struct sockaddr *)a,len)!=0) {
        if(errno!=EINPROGRESS) { int saved=errno; close(fd); errno=saved; return IPC_IO; }
        ipc_result rc=ready(fd,POLLOUT,deadline);
        if(rc!=IPC_OK) { close(fd); return rc; }
        int err=0; socklen_t n=sizeof err;
        if(getsockopt(fd,SOL_SOCKET,SO_ERROR,&err,&n)!=0 || err!=0) { if(err) errno=err; int saved=errno; close(fd); errno=saved; return IPC_IO; }
    }
    if(!own_peer(fd)) { close(fd); return IPC_REJECTED; }
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
ipc_result ipc_client_send(const char *runtime, const ipc_request *r, int timeout_ms)
{
    if(timeout_ms < -1) return IPC_INVALID;
    struct sockaddr_un a; socklen_t len; ipc_result rc=address(runtime,&a,&len);
    if(rc!=IPC_OK) return rc;
    edit_arena arena={0}; if(edit_arena_init(&arena,IPC_MAX_WIRE)!=0) return IPC_IO;
    uint8_t *wire=edit_arena_alloc(&arena,IPC_MAX_WIRE,1); size_t n=0;
    rc=ipc_wire_encode(r,wire,IPC_MAX_WIRE,&n);
    int fd=-1; uint64_t deadline=timeout_ms<0?UINT64_MAX:milliseconds()+(uint64_t)timeout_ms;
    if(rc==IPC_OK) rc=connect_socket(&a,len,deadline,&fd);
    if(rc==IPC_OK) rc=transfer(fd,wire,n,true,deadline);
    if(rc==IPC_OK) rc=receive_reply(fd,'A',deadline);
    if(rc==IPC_OK && r->wait) rc=receive_reply(fd,'C',UINT64_MAX);
    if(fd>=0) close(fd);
    edit_arena_free(&arena); return rc;
}
static void drop_peer(ipc_server *s, struct ipc_peer *p)
{
    if(p->fd>=0) { (void)epoll_ctl(s->fd,EPOLL_CTL_DEL,p->fd,NULL); close(p->fd); }
    uint8_t *wire=p->wire; memset(p,0,sizeof *p); p->wire=wire; p->fd=-1;
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
static void process_peer(ipc_server *s, struct ipc_peer *p, ipc_open_callback callback, void *ctx)
{
    if(p->opened) {
        uint8_t extra; ssize_t n=recv(p->fd,&extra,1,MSG_PEEK);
        if(n==0 || n>0 || (n<0 && errno!=EAGAIN && errno!=EWOULDBLOCK && errno!=EINTR)) { drop_peer(s,p); return; }
        flush_peer(s,p); return;
    }
    for(;;) {
        size_t target=p->expected?p->expected:24u;
        ssize_t n=recv(p->fd,p->wire+p->received,target-p->received,0);
        if(n>0) p->received+=(size_t)n;
        else if(n<0 && errno==EINTR) continue;
        else if(n<0 && (errno==EAGAIN || errno==EWOULDBLOCK)) return;
        else { drop_peer(s,p); return; }
        if(p->received<target) continue;
        if(!p->expected) {
            p->expected=(size_t)p->wire[8] | (size_t)p->wire[9]<<8 | (size_t)p->wire[10]<<16 | (size_t)p->wire[11]<<24;
            if(memcmp(p->wire,"EDIP",4)!=0 || p->expected<24 || p->expected>IPC_MAX_WIRE) {
                p->opened=true; reply(p->reply,'A',IPC_PROTOCOL); p->reply_size=8; flush_peer(s,p); return;
            }
            if(p->received<p->expected) continue;
        }
        ipc_request r; ipc_result rc=ipc_wire_decode(p->wire,p->received,&r);
        uint8_t extra;
        if(recv(p->fd,&extra,1,MSG_PEEK)>0) rc=IPC_PROTOCOL;
        p->opened=true;
        if(rc==IPC_OK) {
            p->wait=r.wait; rc=callback(&r,p->token,ctx);
            if(rc<IPC_OK || rc>IPC_REJECTED) rc=IPC_REJECTED;
        }
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
ipc_result ipc_server_drain(ipc_server *s, ipc_open_callback callback, void *ctx)
{
    if(s==NULL || s->peers==NULL || callback==NULL) return IPC_INVALID;
    struct epoll_event events[64];
    for(size_t batch=0;batch<2;batch++) {
        int n=epoll_wait(s->fd,events,64,0);
        if(n<0) { if(errno==EINTR) continue; return IPC_IO; }
        if(n==0) break;
        for(int k=0;k<n;k++) {
            if(events[k].data.u64==0) {
                for(size_t accepted=0;accepted<IPC_MAX_CLIENTS;accepted++) {
                    int fd=accept4(s->listener,NULL,NULL,SOCK_NONBLOCK|SOCK_CLOEXEC);
                    if(fd<0) { if(errno==EINTR) continue; if(errno==EAGAIN || errno==EWOULDBLOCK) break; return IPC_IO; }
                    struct ipc_peer *p=NULL;
                    for(size_t i=0;i<IPC_MAX_CLIENTS;i++) if(s->peers[i].fd<0) { p=&s->peers[i]; break; }
                    if(p==NULL || !own_peer(fd) || s->next_token==UINT64_MAX) { close(fd); continue; }
                    p->fd=fd; p->token=++s->next_token;
                    struct epoll_event ev={0}; ev.events=EPOLLIN|EPOLLRDHUP; ev.data.u64=p->token;
                    if(epoll_ctl(s->fd,EPOLL_CTL_ADD,fd,&ev)!=0) { drop_peer(s,p); continue; }
                    process_peer(s,p,callback,ctx);
                }
            } else {
                for(size_t i=0;i<IPC_MAX_CLIENTS;i++) {
                    struct ipc_peer *p=&s->peers[i];
                    if(p->fd>=0 && p->token==events[k].data.u64) { process_peer(s,p,callback,ctx); break; }
                }
            }
        }
    }
    return IPC_OK;
}
int ipc_server_fd(const ipc_server *s) { return s!=NULL?s->fd:-1; }
void ipc_server_fini(ipc_server *s)
{
    if(s==NULL) return;
    bool locked=s->lock_fd<0;
    if(s->lock_fd>=0) {
        for(;;) {
            if(flock(s->lock_fd,LOCK_EX)==0) { locked=true; break; }
            if(errno!=EINTR) break;
        }
    }
    if(s->peers!=NULL) for(size_t i=0;i<IPC_MAX_CLIENTS;i++) drop_peer(s,&s->peers[i]);
    if(s->listener>=0) close(s->listener);
    if(s->fd>=0) close(s->fd);
    if(s->owns_path && locked) {
        struct stat st;
        if(lstat(s->socket_path,&st)==0 && (uint64_t)st.st_dev==s->socket_device && (uint64_t)st.st_ino==s->socket_inode) (void)unlink(s->socket_path);
    }
    if(s->lock_fd>=0) close(s->lock_fd);
    edit_arena_free(&s->arena); memset(s,0,sizeof *s); s->fd=-1; s->listener=-1; s->lock_fd=-1;
}
ipc_result ipc_server_init(ipc_server *s, const char *runtime)
{
    if(s==NULL) return IPC_INVALID;
    memset(s,0,sizeof *s); s->fd=-1; s->listener=-1; s->lock_fd=-1;
    struct sockaddr_un a; socklen_t len; ipc_result rc=address(runtime,&a,&len);
    if(rc!=IPC_OK) return rc;
    int lock=-1;
    if(a.sun_path[0]) {
        char lockpath[108]; memcpy(lockpath,a.sun_path,strlen(a.sun_path)+1);
        memcpy(lockpath+strlen(lockpath)-4,"lock",4);
        lock=open(lockpath,O_CREAT|O_RDWR|O_CLOEXEC|O_NOFOLLOW,0600);
        if(lock<0) return IPC_IO;
        struct stat st;
        if(fstat(lock,&st)!=0 || !S_ISREG(st.st_mode) || st.st_uid!=getuid() || (st.st_mode&077u)!=0) { close(lock); return IPC_INVALID; }
        while(flock(lock,LOCK_EX)!=0) if(errno!=EINTR) { close(lock); return IPC_IO; }
        if(lstat(a.sun_path,&st)==0) {
            if(!S_ISSOCK(st.st_mode) || st.st_uid!=getuid()) { rc=IPC_INVALID; goto done; }
            int probe=-1; rc=connect_socket(&a,len,milliseconds()+1000u,&probe);
            if(rc==IPC_OK) { close(probe); rc=IPC_EXISTS; goto done; }
            if(rc!=IPC_IO || errno!=ECONNREFUSED) { rc=IPC_EXISTS; goto done; }
            if(unlink(a.sun_path)!=0) { rc=IPC_IO; goto done; }
        } else if(errno!=ENOENT) { rc=IPC_IO; goto done; }
    }
    s->listener=socket(AF_UNIX,SOCK_STREAM|SOCK_NONBLOCK|SOCK_CLOEXEC,0);
    if(s->listener<0) { rc=IPC_IO; goto done; }
    if(bind(s->listener,(struct sockaddr *)&a,len)!=0) { rc=errno==EADDRINUSE?IPC_EXISTS:IPC_IO; goto done; }
    if(a.sun_path[0]) {
        struct stat st;
        if(lstat(a.sun_path,&st)!=0) { rc=IPC_IO; goto done; }
        memcpy(s->socket_path,a.sun_path,strlen(a.sun_path)+1); s->owns_path=true;
        s->socket_device=(uint64_t)st.st_dev; s->socket_inode=(uint64_t)st.st_ino;
        if(chmod(a.sun_path,0600)!=0) { rc=IPC_IO; goto done; }
    }
    if(listen(s->listener,(int)IPC_MAX_CLIENTS)!=0) { rc=IPC_IO; goto done; }
    s->fd=epoll_create1(EPOLL_CLOEXEC);
    if(s->fd<0 || edit_arena_init(&s->arena,(size_t)IPC_MAX_CLIENTS*(IPC_MAX_WIRE+sizeof(struct ipc_peer))+4096u)!=0) { rc=IPC_IO; goto done; }
    s->peers=edit_arena_alloc(&s->arena,IPC_MAX_CLIENTS*sizeof *s->peers,_Alignof(struct ipc_peer));
    if(s->peers==NULL) { rc=IPC_IO; goto done; }
    memset(s->peers,0,IPC_MAX_CLIENTS*sizeof *s->peers);
    for(size_t i=0;i<IPC_MAX_CLIENTS;i++) s->peers[i].fd=-1;
    for(size_t i=0;i<IPC_MAX_CLIENTS;i++) {
        s->peers[i].wire=edit_arena_alloc(&s->arena,IPC_MAX_WIRE,1);
        if(s->peers[i].wire==NULL) { rc=IPC_IO; goto done; }
    }
    struct epoll_event ev={0}; ev.events=EPOLLIN; ev.data.u64=0;
    if(epoll_ctl(s->fd,EPOLL_CTL_ADD,s->listener,&ev)!=0) { rc=IPC_IO; goto done; }
    rc=IPC_OK;
done:
    if(rc!=IPC_OK) {
        ipc_server_fini(s);
        if(lock>=0) close(lock);
    } else if(lock>=0) {
        s->lock_fd=lock;
        (void)flock(lock,LOCK_UN);
    }
    return rc;
}
