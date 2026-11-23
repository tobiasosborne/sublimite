/* Every kernel call goes through os (v2-0fv.13): local streams, the wait
 * set and its alarm, the pinned directory and the lock file. */
#include "ipc.h"
#include <string.h>

#define NS_PER_MS UINT64_C(1000000) /* (P) SI */

struct ipc_peer {
    os_stream stream;
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
};
struct budget { size_t bytes, callbacks, accepts; uint64_t now; };

/* (P) sun_path holds 108 bytes; an abstract name gives one to the leading
 * NUL. A leaf keeps v1's 64-byte buffer (docs/decisions/v2-0fv.6.md). */
#define ABSTRACT_CAP 107u
#define LEAF_CAP 64u
typedef struct endpoint {
    bool abstract;
    os_dir dir;                /* pinned, verified runtime directory; closed for abstract */
    char name[ABSTRACT_CAP];   /* leaf inside dir, or the abstract name */
} endpoint;
static const os_dir *ep_dir(const endpoint *ep) { return ep->abstract?NULL:&ep->dir; }

/* Bounded name building; over is sticky, like snprintf's truncation report. */
typedef struct text { char *p; size_t cap, len; bool over; } text;
static void put(text *t, const char *s)
{
    size_t n=strlen(s);
    if(t->over || n>=t->cap-t->len) { t->over=true; return; }
    memcpy(t->p+t->len,s,n+1); t->len+=n;
}
static void put_u64(text *t, uint64_t v)
{
    char d[21]; size_t i=sizeof d; d[--i]='\0'; /* (P) 2^64-1 has 20 digits */
    do { d[--i]=(char)('0'+v%10u); v/=10u; } while(v);
    put(t,d+i);
}

static uint64_t milliseconds(void)
{
    return os_now_ns()/NS_PER_MS;
}
/* Peer credentials: a peer must run as our uid, on every endpoint. Tests
 * model a foreign peer by wrapping os_stream_peer_uid in their own link
 * (tests/ipc_peer_seam.h); the shipped code has no override. */
static bool own_peer(const os_stream *s)
{
    uint32_t uid=0;
    return os_stream_peer_uid(s,&uid)==OS_OK && uid==os_user_id();
}
static void ep_close(endpoint *ep)
{
    os_dir_close(&ep->dir);
}
/* Open and verify the runtime directory once; every later operation goes
 * through this descriptor, so renaming or replacing the path afterwards
 * cannot redirect lock, bind, chmod or unlink (edit-457.21 review §2). */
static ipc_result display_suffix(const char *display, char *out, size_t cap)
{
    out[0] = '\0';
    if (display == NULL) return IPC_OK; /* legacy/general IPC callers */
    size_t n = strlen(display);
    if (n == 0) return IPC_INVALID;
    if (cap < 3u || n > (cap - 3u) / 2u) return IPC_LIMIT;
    out[0] = '-'; out[1] = 'd';
    const char *hex = "0123456789abcdef";
    for (size_t i = 0; i < n; i++) {
        unsigned char byte = (unsigned char)display[i];
        out[2u + 2u*i] = hex[byte >> 4]; out[3u + 2u*i] = hex[byte & 15u];
    }
    out[2u + 2u*n] = '\0'; return IPC_OK;
}
static ipc_result resolve_dir(const char *runtime, const char *display, endpoint *ep)
{
    if(runtime[0]!='/') return IPC_INVALID;
    os_dir d=OS_DIR_INIT; os_stat st;
    if(os_dir_open(&d,runtime)!=OS_OK) return IPC_INVALID;
    if(os_stat_at(&d,".",0,&st)!=OS_OK || st.kind!=OS_KIND_DIR || st.uid!=os_user_id() || (st.perm&077u)!=0) { os_dir_close(&d); return IPC_INVALID; }
    char suffix[64]; ipc_result rc=display_suffix(display,suffix,sizeof suffix);
    if(rc!=IPC_OK) { os_dir_close(&d); return rc; }
    text t={ep->name,LEAF_CAP,0,false};
    put(&t,EDIT_APP_ID "-"); put_u64(&t,os_user_id()); put(&t,suffix); put(&t,".sock");
    if(t.over) { os_dir_close(&d); return IPC_LIMIT; }
    ep->dir=d; ep->abstract=false; return IPC_OK;
}
static void ep_reset(endpoint *ep)
{
    memset(ep,0,sizeof *ep); ep->dir=(os_dir)OS_DIR_INIT;
}
static ipc_result resolve(const char *runtime, const char *display, endpoint *ep)
{
    ep_reset(ep);
    char xdg[OS_PATH_MAX];
    if(runtime==NULL) {
        os_err e=os_env_get("XDG_RUNTIME_DIR",xdg,sizeof xdg);
        if(e==OS_OK) runtime=xdg;
        else if(e!=OS_E_NOENT) return IPC_INVALID;
    }
    if(runtime!=NULL) return resolve_dir(runtime,display,ep);
    char suffix[64]; ipc_result rc=display_suffix(display,suffix,sizeof suffix);
    if(rc!=IPC_OK) return rc;
    char ns[ABSTRACT_CAP+1u]; os_err e=os_env_get("EDIT_IPC_NAMESPACE",ns,sizeof ns);
    text t={ep->name,ABSTRACT_CAP,0,false};
    put(&t,EDIT_APP_ID "-"); put_u64(&t,os_user_id());
    if(e==OS_OK) {
        if(ns[0]=='\0') return IPC_INVALID;
        put(&t,"-"); put(&t,ns); put(&t,suffix);
    } else if(e==OS_E_RANGE) return IPC_LIMIT;
    else if(e==OS_E_NOENT) put(&t,suffix);
    else return IPC_INVALID;
    if(t.over) return IPC_LIMIT;
    ep->abstract=true; return IPC_OK;
}
/* Private filesystem endpoint used when another uid has squatted the abstract
 * name (review §13): EDIT_IPC_FALLBACK_DIR (test hook) or /run/user/<uid>. */
static ipc_result resolve_fallback(const char *display, endpoint *ep)
{
    ep_reset(ep);
    char buf[OS_PATH_MAX]; os_err e=os_env_get("EDIT_IPC_FALLBACK_DIR",buf,sizeof buf);
    if(e==OS_OK) return resolve_dir(buf,display,ep);
    if(e!=OS_E_NOENT) return IPC_INVALID;
    text t={buf,sizeof buf,0,false}; put(&t,"/run/user/"); put_u64(&t,os_user_id());
    return resolve_dir(buf,display,ep);
}
/* One client connection: the stream and a private wait set for its
 * deadlines. Startup and launcher only, never the UI loop. */
typedef struct conn { os_stream s; os_waitset ws; } conn;
static void conn_close(conn *c)
{
    os_waitset_close(&c->ws); os_stream_close(&c->s);
}
static ipc_result conn_watch(conn *c)
{
    if(os_waitset_init(&c->ws)!=OS_OK) return IPC_IO;
    return os_waitset_add(&c->ws,os_stream_fd(&c->s),OS_WAIT_IN,0)==OS_OK?IPC_OK:IPC_IO;
}
static ipc_result ready(conn *c, unsigned interest, uint64_t deadline)
{
    os_wait_event ev; size_t n=0;
    if(os_waitset_modify(&c->ws,os_stream_fd(&c->s),interest,0)!=OS_OK) return IPC_IO;
    if(os_waitset_wait(&c->ws,deadline,&ev,1,&n)!=OS_OK) return IPC_IO;
    return n?IPC_OK:IPC_TIMEOUT;
}
/* *why keeps the os outcome: OS_E_REFUSED marks a stale filesystem socket. */
static ipc_result connect_socket(const os_dir *dir, const char *name, os_stream *out, os_err *why)
{
    *why=os_connect_local(out,dir,name);
    if(*why!=OS_OK) return IPC_IO;
    if(!own_peer(out)) { os_stream_close(out); return IPC_REJECTED; }
    return IPC_OK;
}
static ipc_result transfer(conn *c, uint8_t *bytes, size_t n, bool sending, uint64_t deadline)
{
    size_t pos=0;
    while(pos<n) {
        size_t got=0;
        os_err e=sending?os_stream_write(&c->s,bytes+pos,n-pos,&got):os_stream_read(&c->s,bytes+pos,n-pos,&got);
        if(e==OS_OK && got>0) { pos+=got; continue; }
        if(e!=OS_E_AGAIN) return IPC_IO; /* end of stream or a failed transfer */
        ipc_result rc=ready(c,sending?OS_WAIT_OUT:OS_WAIT_IN,deadline); if(rc!=IPC_OK) return rc;
    }
    return IPC_OK;
}
static void reply(uint8_t *p, char kind, ipc_result rc)
{
    memcpy(p,"EDIR",4); p[4]=(uint8_t)kind; p[5]=(uint8_t)rc; p[6]=0; p[7]=0;
}
static ipc_result receive_reply(conn *c, char kind, uint64_t deadline)
{
    uint8_t bytes[8]; ipc_result rc=transfer(c,bytes,sizeof bytes,false,deadline);
    if(rc!=IPC_OK) return rc;
    if(memcmp(bytes,"EDIR",4)!=0 || bytes[4]!=(uint8_t)kind || bytes[5]>IPC_REJECTED || bytes[6] || bytes[7]) return IPC_PROTOCOL;
    return (ipc_result)bytes[5];
}
static ipc_result deliver(const endpoint *ep, uint8_t *wire, size_t n, bool wait, uint64_t deadline)
{
    conn c={OS_STREAM_INIT,OS_WAITSET_INIT}; os_err why;
    ipc_result rc=connect_socket(ep_dir(ep),ep->name,&c.s,&why);
    if(rc==IPC_OK) rc=conn_watch(&c);
    if(rc==IPC_OK) rc=transfer(&c,wire,n,true,deadline);
    if(rc==IPC_OK) rc=receive_reply(&c,'A',deadline);
    if(rc==IPC_OK && wait) rc=receive_reply(&c,'C',OS_DEADLINE_NEVER);
    conn_close(&c);
    return rc;
}
ipc_result ipc_client_send_display(const char *runtime, const char *display, const ipc_request *r, int timeout_ms)
{
    ASSERT(r != NULL && timeout_ms >= -1);
    endpoint ep; ipc_result rc=resolve(runtime,display,&ep);
    if(rc!=IPC_OK) return rc;
    base_arena arena={0}; if(base_arena_init(&arena,IPC_MAX_WIRE)!=0) { ep_close(&ep); return IPC_IO; }
    uint8_t *wire=base_arena_alloc(&arena,IPC_MAX_WIRE,1); size_t n=0;
    rc=ipc_wire_encode(r,wire,IPC_MAX_WIRE,&n);
    uint64_t deadline=timeout_ms<0?OS_DEADLINE_NEVER:os_now_ns()+(uint64_t)timeout_ms*NS_PER_MS;
    if(rc==IPC_OK) rc=deliver(&ep,wire,n,r->wait,deadline);
    if(rc==IPC_PROTOCOL && (r->activation_token!=NULL || r->startup_id!=NULL)) {
        /* edit-b6ai.8: a server of an earlier release refuses the activation
         * block. Resend in the previous format: the files still open. */
        ipc_request plain=*r; plain.activation_token=plain.startup_id=NULL;
        if(ipc_wire_encode(&plain,wire,IPC_MAX_WIRE,&n)==IPC_OK) rc=deliver(&ep,wire,n,r->wait,deadline);
    }
    if(rc==IPC_REJECTED && ep.abstract) {
        /* Foreign uid on the abstract name; nothing was sent. Try the private path. */
        endpoint fb; if(resolve_fallback(display,&fb)==IPC_OK) {
            ipc_result r2=deliver(&fb,wire,n,r->wait,deadline); ep_close(&fb);
            if(r2!=IPC_IO) rc=r2;
        }
    }
    ep_close(&ep); base_arena_free(&arena); return rc;
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
static bool peer_live(const struct ipc_peer *p) { return os_stream_fd(&p->stream)>=0; }
static void peer_reset(struct ipc_peer *p)
{
    memset(p,0,sizeof *p); p->stream=(os_stream)OS_STREAM_INIT;
}
static void drop_peer(ipc_server *s, struct ipc_peer *p)
{
    if(peer_live(p)) {
        if(peer_waiting(p)) s->wait_drops++;
        os_waitset_remove(&s->set,os_stream_fd(&p->stream)); os_stream_close(&p->stream);
    }
    rx_release(s->rx,p->wire,p->wire_big);
    peer_reset(p);
}
static void interest(ipc_server *s, struct ipc_peer *p)
{
    unsigned want=OS_WAIT_IN|(p->sent<p->reply_size?OS_WAIT_OUT:0u);
    if(os_waitset_modify(&s->set,os_stream_fd(&p->stream),want,p->token)!=OS_OK) drop_peer(s,p);
}
static void flush_peer(ipc_server *s, struct ipc_peer *p)
{
    if(p->closed && p->reply_size==8 && p->reply[5]==IPC_OK) {
        reply(p->reply+8,'C',IPC_OK); p->reply_size=16;
    }
    while(p->sent<p->reply_size) {
        size_t n=0; os_err e=os_stream_write(&p->stream,p->reply+p->sent,p->reply_size-p->sent,&n);
        if(e==OS_OK && n>0) { p->sent+=n; continue; }
        if(e==OS_E_AGAIN) { interest(s,p); return; }
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
        /* Only "nothing queued" keeps an answered peer: end of stream, extra
         * bytes and errors all drop it. */
        uint8_t extra; size_t n=0;
        if(os_stream_peek(&p->stream,&extra,1,&n)!=OS_E_AGAIN) { drop_peer(s,p); return; }
        flush_peer(s,p); return;
    }
    for(;;) {
        /* Work budget (review §10): unread bytes stay in the kernel and the
         * level-triggered wait set keeps the loop awake. A callback is only
         * ever reached through a recv, so it always has budget. */
        if(b->bytes==0 || b->callbacks==0) return;
        size_t target=p->expected?p->expected:24u;
        uint8_t *dst=p->wire!=NULL?p->wire:p->head;
        size_t want=target-p->received; if(want>b->bytes) want=b->bytes;
        size_t n=0; os_err e=os_stream_read(&p->stream,dst+p->received,want,&n);
        if(e==OS_OK && n>0) { p->received+=n; b->bytes-=n; s->drain_bytes+=(uint64_t)n; p->active_ms=b->now; }
        else if(e==OS_E_AGAIN) return;
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
        uint8_t extra; size_t more=0;
        if(os_stream_peek(&p->stream,&extra,1,&more)==OS_OK && more>0) rc=IPC_PROTOCOL;
        p->opened=true;
        if(rc==IPC_OK) {
            p->wait=r.wait; b->callbacks--; rc=callback(&r,p->token,ctx);
            if(rc<IPC_OK || rc>IPC_REJECTED) rc=IPC_REJECTED;
        }
        rx_release(s->rx,p->wire,p->wire_big); p->wire=NULL; /* storage is only needed until the callback returns */
        reply(p->reply,'A',rc); p->reply_size=8; flush_peer(s,p); return;
    }
}
/* Server entries take an initialized server; tokens start at 1. */
ipc_result ipc_server_report_closed(ipc_server *s, ipc_token token)
{
    ASSERT(s != NULL && s->peers != NULL && token != 0);
    for(size_t i=0;i<IPC_MAX_CLIENTS;i++) {
        struct ipc_peer *p=&s->peers[i];
        if(peer_live(p) && p->token==token && p->opened && p->wait) {
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
    ASSERT(s != NULL && s->peers != NULL && token != 0);
    for(size_t i=0;i<IPC_MAX_CLIENTS;i++) {
        const struct ipc_peer *p=&s->peers[i];
        if(peer_live(p) && p->token==token) return peer_waiting(p);
    }
    return false;
}
/* Request deadlines (review §11): the wait set's alarm, armed only while an
 * incomplete request exists, wakes the loop; expiry closes the peer without a
 * reply. */
static void expire_peers(ipc_server *s, uint64_t now)
{
    for(size_t i=0;i<IPC_MAX_CLIENTS;i++) {
        struct ipc_peer *p=&s->peers[i];
        if(peer_live(p) && !p->opened && now>=p->accepted_ms+s->request_deadline_ms) drop_peer(s,p);
    }
}
static void rearm_timer(ipc_server *s)
{
    uint64_t earliest=OS_DEADLINE_NEVER;
    for(size_t i=0;i<IPC_MAX_CLIENTS;i++) {
        const struct ipc_peer *p=&s->peers[i];
        if(peer_live(p) && !p->opened) { uint64_t d=(p->accepted_ms+s->request_deadline_ms)*NS_PER_MS; if(d<earliest) earliest=d; }
    }
    /* A failure keeps the previous alarm; the next drain retries. */
    (void)os_waitset_alarm(&s->set,earliest);
}
/* Free a slot for a newcomer by closing the longest-idle incomplete client. */
static struct ipc_peer *take_slot(ipc_server *s, uint64_t now)
{
    struct ipc_peer *victim=NULL;
    for(size_t i=0;i<IPC_MAX_CLIENTS;i++) {
        struct ipc_peer *p=&s->peers[i];
        if(!peer_live(p)) return p;
        if(!p->opened && now-p->active_ms>=s->evict_idle_ms && (victim==NULL || p->active_ms<victim->active_ms)) victim=p;
    }
    if(victim!=NULL) drop_peer(s,victim);
    return victim;
}
ipc_result ipc_server_drain(ipc_server *s, ipc_open_callback callback, void *ctx)
{
    ASSERT(s != NULL && s->peers != NULL && callback != NULL);
    struct budget b={IPC_DRAIN_BYTES,IPC_DRAIN_CALLBACKS,IPC_DRAIN_ACCEPTS,milliseconds()};
    s->drain_bytes=0;
    expire_peers(s,b.now);
    os_wait_event events[64];
    for(size_t batch=0;batch<2;batch++) {
        size_t n=0; /* deadline 0: drain polls; the loop's root set is where the UI thread blocks */
        if(os_waitset_wait(&s->set,0,events,64,&n)!=OS_OK) return IPC_IO;
        if(n==0) break;
        for(size_t k=0;k<n;k++) {
            if(events[k].tag==OS_WAIT_ALARM) {
                expire_peers(s,milliseconds()); /* the wait set consumed and disarmed the alarm */
            } else if(events[k].tag==0) {
                while(b.accepts>0) {
                    os_stream c=OS_STREAM_INIT; os_err e=os_listener_accept(&s->listener,&c);
                    if(e==OS_E_AGAIN) break;
                    if(e!=OS_OK) return IPC_IO;
                    b.accepts--;
                    struct ipc_peer *p=take_slot(s,b.now);
                    if(p==NULL || !own_peer(&c) || s->next_token==UINT64_MAX) { os_stream_close(&c); continue; }
                    p->stream=c; p->token=++s->next_token; p->accepted_ms=p->active_ms=b.now;
                    if(os_waitset_add(&s->set,os_stream_fd(&p->stream),OS_WAIT_IN,p->token)!=OS_OK) { drop_peer(s,p); continue; }
                    process_peer(s,p,callback,ctx,&b);
                }
            } else {
                for(size_t i=0;i<IPC_MAX_CLIENTS;i++) {
                    struct ipc_peer *p=&s->peers[i];
                    if(peer_live(p) && p->token==events[k].tag) { process_peer(s,p,callback,ctx,&b); break; }
                }
            }
        }
    }
    rearm_timer(s);
    return IPC_OK;
}
int ipc_server_fd(const ipc_server *s)
{
    ASSERT(s != NULL);
    return os_waitset_fd(&s->set);
}

static ipc_result lock_with_deadline(os_file *lock)
{
    uint64_t end=milliseconds()+IPC_LOCK_TIMEOUT_MS;
    for(;;) {
        os_err e=os_file_try_lock(lock,OS_LOCK_EXCLUSIVE);
        if(e==OS_OK) return IPC_OK;
        if(e!=OS_E_BUSY) return IPC_IO;
        if(milliseconds()>=end) return IPC_TIMEOUT;
        os_sleep_ns(NS_PER_MS);
    }
}
static void reset_server(ipc_server *s)
{
    memset(s,0,sizeof *s);
    s->set=(os_waitset)OS_WAITSET_INIT; s->listener=(os_listener)OS_LISTENER_INIT;
    s->lock=(os_file)OS_FILE_INIT; s->dir=(os_dir)OS_DIR_INIT;
    s->request_deadline_ms=IPC_REQUEST_DEADLINE_MS; s->evict_idle_ms=IPC_EVICT_IDLE_MS;
}
/* locked: the caller holds (or never needed) the lifecycle lock, so the
 * socket may be unlinked if it is still ours. */
static void teardown(ipc_server *s, bool locked)
{
    if(s->peers!=NULL && s->rx!=NULL) for(size_t i=0;i<IPC_MAX_CLIENTS;i++) drop_peer(s,&s->peers[i]);
    os_listener_close(&s->listener);
    os_waitset_close(&s->set);
    if(s->owns_path && locked && os_dir_is_open(&s->dir)) {
        os_stat st;
        if(os_stat_at(&s->dir,s->socket_path,OS_STAT_NOFOLLOW,&st)==OS_OK && os_identity_equal(&st.id,&s->socket_id))
            (void)os_unlink_at(&s->dir,s->socket_path);
    }
    os_file_close(&s->lock);
    os_dir_close(&s->dir);
    base_arena_free(&s->arena); reset_server(s);
}
void ipc_server_fini(ipc_server *s)
{
    ASSERT(s != NULL);
    bool locked=!os_file_is_open(&s->lock);
    /* A bounded wait: if the lock cannot be taken the socket is left for the
     * next startup's stale-socket recovery rather than unlinked blind. */
    if(!locked) locked=lock_with_deadline(&s->lock)==IPC_OK;
    teardown(s,locked);
}
/* Wait set (its alarm is the request deadline), peer table and receive pool. */
static ipc_result setup_runtime(ipc_server *s)
{
    size_t size=(size_t)IPC_RX_SMALL_COUNT*IPC_RX_SMALL_SIZE+(size_t)IPC_RX_BIG_COUNT*IPC_MAX_WIRE
        +IPC_MAX_CLIENTS*sizeof(struct ipc_peer)+sizeof(struct ipc_rx)+4096u;
    if(os_waitset_init(&s->set)!=OS_OK || base_arena_init(&s->arena,size)!=0) return IPC_IO;
    s->peers=base_arena_alloc(&s->arena,IPC_MAX_CLIENTS*sizeof *s->peers,_Alignof(struct ipc_peer));
    s->rx=base_arena_alloc(&s->arena,sizeof *s->rx,_Alignof(struct ipc_rx));
    if(s->peers==NULL || s->rx==NULL) return IPC_IO;
    memset(s->peers,0,IPC_MAX_CLIENTS*sizeof *s->peers); memset(s->rx,0,sizeof *s->rx);
    for(size_t i=0;i<IPC_MAX_CLIENTS;i++) peer_reset(&s->peers[i]);
    for(size_t i=0;i<IPC_RX_SMALL_COUNT;i++) if((s->rx->small[i]=base_arena_alloc(&s->arena,IPC_RX_SMALL_SIZE,16))==NULL) return IPC_IO;
    for(size_t i=0;i<IPC_RX_BIG_COUNT;i++) if((s->rx->big[i]=base_arena_alloc(&s->arena,IPC_MAX_WIRE,16))==NULL) return IPC_IO;
    s->rx->nsmall=IPC_RX_SMALL_COUNT; s->rx->nbig=IPC_RX_BIG_COUNT;
    int l=os_listener_fd(&s->listener); /* tag 0 is the listener; peer tokens start at 1 */
    if(l>=0 && os_waitset_add(&s->set,l,OS_WAIT_IN,0)!=OS_OK) return IPC_IO;
    return IPC_OK;
}
/* Bind the endpoint. *foreign is set when an abstract name is held by another
 * uid (or by something that never listens), so the caller can fall back. */
static ipc_result start_endpoint(ipc_server *s, endpoint *ep, bool *foreign)
{
    ipc_result rc; os_file lock=OS_FILE_INIT; *foreign=false;
    s->dir=ep->dir; ep->dir=(os_dir)OS_DIR_INIT; /* the server owns the pinned directory from here on */
    const os_dir *dir=ep->abstract?NULL:&s->dir;
    os_stat st; os_err e, why;
    if(!ep->abstract) {
        char lockleaf[LEAF_CAP]; size_t ln=strlen(ep->name);
        memcpy(lockleaf,ep->name,ln+1); memcpy(lockleaf+ln-4,"lock",4);
        if(os_file_open_at(&lock,&s->dir,lockleaf,OS_OPEN_RDWR|OS_OPEN_CREATE|OS_OPEN_NOFOLLOW)!=OS_OK) { rc=IPC_IO; goto done; }
        if(os_file_stat(&lock,&st)!=OS_OK || st.kind!=OS_KIND_FILE || st.uid!=os_user_id() || (st.perm&077u)!=0) { rc=IPC_INVALID; goto done; }
        rc=lock_with_deadline(&lock);
        if(rc!=IPC_OK) goto done;
        e=os_stat_at(&s->dir,ep->name,OS_STAT_NOFOLLOW,&st);
        if(e==OS_OK) {
            if(st.kind!=OS_KIND_SOCKET || st.uid!=os_user_id()) { rc=IPC_INVALID; goto done; }
            os_stream probe=OS_STREAM_INIT; rc=connect_socket(dir,ep->name,&probe,&why);
            if(rc==IPC_OK) { os_stream_close(&probe); rc=IPC_EXISTS; goto done; }
            if(rc!=IPC_IO || why!=OS_E_REFUSED) { rc=IPC_EXISTS; goto done; }
            if(os_unlink_at(&s->dir,ep->name)!=OS_OK) { rc=IPC_IO; goto done; }
        } else if(e!=OS_E_NOENT) { rc=IPC_IO; goto done; }
    }
    /* os binds, makes a directory name owner-only through a pinned reference
     * and listens; the inode it reports is the only one teardown unlinks. */
    e=os_listen_local(&s->listener,dir,ep->name,IPC_MAX_CLIENTS,&st);
    if(e!=OS_OK) {
        if(e!=OS_E_EXIST) { rc=IPC_IO; goto done; }
        rc=IPC_EXISTS;
        if(ep->abstract) for(int attempt=0;attempt<3;attempt++) {
            os_stream probe=OS_STREAM_INIT; ipc_result pr=connect_socket(NULL,ep->name,&probe,&why);
            if(pr==IPC_OK) { os_stream_close(&probe); break; }                          /* ours: a live incumbent */
            if(pr==IPC_REJECTED) { *foreign=true; break; }                               /* another uid */
            if(why!=OS_E_REFUSED) break;
            if(attempt==2) *foreign=true; else { os_sleep_ns(10u*NS_PER_MS); }
        }
        goto done;
    }
    if(!ep->abstract) {
        memcpy(s->socket_path,ep->name,strlen(ep->name)+1); s->owns_path=true; s->socket_id=st.id;
    }
    rc=setup_runtime(s);
done:
    if(rc!=IPC_OK) {
        teardown(s,true);
        os_file_close(&lock);
    } else if(os_file_is_open(&lock)) {
        s->lock=lock;
        os_file_unlock(&s->lock);
    }
    return rc;
}
ipc_result ipc_server_init_display(ipc_server *s, const char *runtime, const char *display)
{
    ASSERT(s != NULL);
    reset_server(s);
    endpoint ep; ipc_result rc=resolve(runtime,display,&ep);
    if(rc!=IPC_OK) return rc;
    bool foreign=false; bool abstract=ep.abstract;
    rc=start_endpoint(s,&ep,&foreign);
    ep_close(&ep);
    if(rc!=IPC_OK && abstract && foreign) {
        reset_server(s);
        if(resolve_fallback(display,&ep)!=IPC_OK) return IPC_EXISTS; /* no private directory: report the squat */
        rc=start_endpoint(s,&ep,&foreign); ep_close(&ep);
    }
    return rc;
}
ipc_result ipc_server_init_isolated(ipc_server *s)
{
    ASSERT(s != NULL);
    reset_server(s);
    ipc_result rc=setup_runtime(s);
    if(rc!=IPC_OK) teardown(s,true);
    return rc;
}
ipc_result ipc_launcher_pair(int fds[2])
{
    ASSERT(fds != NULL);
    os_stream a=OS_STREAM_INIT, b=OS_STREAM_INIT;
    if(os_stream_pair(&a,&b)!=OS_OK) return IPC_IO;
    fds[0]=os_stream_fd(&a); fds[1]=os_stream_fd(&b); /* ownership passes to the caller */
    return IPC_OK;
}
ipc_result ipc_server_adopt_wait(ipc_server *s, int fd, ipc_token *token)
{
    ASSERT(s != NULL && s->peers != NULL && fd >= 0 && token != NULL);
    /* (P) 2^64 issued tokens: exhaustion stays a return code (A15). */
    if(s->next_token==UINT64_MAX) return IPC_LIMIT;
    struct ipc_peer *p=NULL;
    for(size_t i=0;i<IPC_MAX_CLIENTS;i++) if(!peer_live(&s->peers[i])) { p=&s->peers[i]; break; }
    if(p==NULL) return IPC_LIMIT;
    /* On failure the handle is forgotten, not closed: fd stays the caller's. */
    if(os_stream_adopt(&p->stream,fd)!=OS_OK) { peer_reset(p); return IPC_IO; }
    p->token=++s->next_token; p->opened=true; p->wait=true;
    p->accepted_ms=p->active_ms=milliseconds();
    reply(p->reply,'A',IPC_OK); p->reply_size=8; p->sent=8; /* the launcher expects only the closed reply */
    if(os_waitset_add(&s->set,fd,OS_WAIT_IN,p->token)!=OS_OK) { peer_reset(p); return IPC_IO; }
    *token=p->token; return IPC_OK;
}
ipc_result ipc_launcher_wait(int fd)
{
    ASSERT(fd >= 0);
    conn c={OS_STREAM_INIT,OS_WAITSET_INIT}; ipc_result rc=IPC_IO;
    if(os_stream_adopt(&c.s,fd)==OS_OK && conn_watch(&c)==IPC_OK) rc=receive_reply(&c,'C',OS_DEADLINE_NEVER);
    conn_close(&c); return rc;
}

ipc_result ipc_server_init(ipc_server *s, const char *runtime)
{
    return ipc_server_init_display(s, runtime, NULL);
}
ipc_result ipc_client_send(const char *runtime, const ipc_request *r, int timeout_ms)
{
    return ipc_client_send_display(runtime, NULL, r, timeout_ms);
}

