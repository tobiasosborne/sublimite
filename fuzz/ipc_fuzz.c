/* ipc wire fuzzer (edit-457.21b, review P4-modules-1 §17 fuzz half).
 * Part 1: arbitrary bytes into the decoder (shape invariants).
 * Part 2: exact-result oracle. A request is built from the input, encoded, and
 *   one structural mutation is applied to the envelope. The model predicts the
 *   outcome exactly: either IPC_PROTOCOL (the right code) or IPC_OK with a
 *   request equal field-for-field to the expected one. Any other outcome traps.
 * Part 3: encoder contract (limits, determinism, new_instance).
 * Part 4: end to end through a real abstract-namespace server for ~1/32 of the
 *   inputs: the ACK code must equal the model, the callback must see exactly
 *   the expected request, and a peer whose uid is foreign must never reach the
 *   callback nor get an ACK (credential regression traps).
 * Part 5 (v2): NUL-split argv through ipc_parse_args + stdin + activation. */
#include "ipc/ipc.h"
#include "../tests/ipc_seam.h"
#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

typedef struct rd { const uint8_t *p; size_t n, i; } rd;
static uint8_t r8(rd *r) { return r->i<r->n ? r->p[r->i++] : 0; }
static uint32_t r32(rd *r) { uint32_t v=0; for(int k=0;k<4;k++) v|=(uint32_t)r8(r)<<(8*k); return v; }
static uint32_t get32(const uint8_t *p) { return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24; }
static void put32(uint8_t *p, uint32_t v) { for(int k=0;k<4;k++) p[k]=(uint8_t)(v>>(8*k)); }

#define MAXP 3u
#define STR_CAP 40u
typedef struct model {
    char cwd[STR_CAP], path[MAXP][STR_CAP];
    uint32_t line[MAXP], col[MAXP];
    size_t count, stdin_size;
    uint8_t stdin_buf[2048];
    bool wait, has_stdin;
} model;

static void gen_string(rd *r, char *out)
{
    static const char alpha[]="abcxyz019/.:-_ ";
    size_t n=r8(r)%20u; out[0]='/';
    for(size_t i=0;i<n;i++) out[1+i]=alpha[r8(r)%(sizeof alpha-1)];
    out[1+n]=0;
}
static void to_request(const model *m, ipc_request *q)
{
    memset(q,0,sizeof *q); q->cwd=m->cwd; q->count=m->count;
    for(size_t i=0;i<m->count;i++) q->paths[i]=(ipc_path){m->path[i],m->line[i],m->col[i]};
    q->wait=m->wait; q->has_stdin=m->has_stdin; q->stdin_size=m->stdin_size;
    q->stdin_data=m->has_stdin?m->stdin_buf:NULL;
}
/* Field-for-field equality between a decoded request and the model. */
static bool same(const ipc_request *g, const model *m)
{
    if(g->count!=m->count || g->cwd==NULL || strcmp(g->cwd,m->cwd)!=0) return false;
    for(size_t i=0;i<m->count;i++)
        if(g->paths[i].path==NULL || strcmp(g->paths[i].path,m->path[i])!=0 || g->paths[i].line!=m->line[i] || g->paths[i].col!=m->col[i]) return false;
    for(size_t i=m->count;i<IPC_MAX_PATHS;i++) if(g->paths[i].path!=NULL) return false;
    if(g->wait!=m->wait || g->has_stdin!=m->has_stdin || g->new_instance || g->stdin_size!=m->stdin_size) return false;
    if(m->has_stdin) return g->stdin_data!=NULL && (m->stdin_size==0 || memcmp(g->stdin_data,m->stdin_buf,m->stdin_size)==0);
    return g->stdin_data==NULL;
}
static void expect_ok(const uint8_t *w, size_t n, const model *m)
{
    ipc_request o; ipc_result rc=ipc_test_wire_decode(w,n,&o);
    ASSERT(rc==IPC_OK && same(&o,m));
}
static void expect_proto(const uint8_t *w, size_t n)
{
    ipc_request o; ipc_result rc=ipc_test_wire_decode(w,n,&o);
    ASSERT(rc==IPC_PROTOCOL);
    ASSERT(o.count==0 && o.cwd==NULL && o.stdin_data==NULL && o.stdin_size==0 && !o.wait && !o.has_stdin);
}

/* ---- end to end server (lazy; process lifetime) ---- */
typedef struct sink { const model *want; int calls; } sink;
static ipc_result on_open(const ipc_request *r, ipc_token t, void *ctx)
{
    (void)t; sink *s=ctx; ASSERT(s->want!=NULL && same(r,s->want)); s->calls++; return IPC_OK;
}
static ipc_server g_srv; static int g_srv_state; /* 0 untried, 1 up, -1 unavailable */
static int e2e_connect(void)
{
    struct sockaddr_un a; memset(&a,0,sizeof a); a.sun_family=AF_UNIX;
    int n=snprintf(a.sun_path+1,sizeof a.sun_path-1,EDIT_APP_ID "-%lu-%s",(unsigned long)getuid(),ipc_seam_namespace);
    int fd=socket(AF_UNIX,SOCK_STREAM|SOCK_CLOEXEC,0); ASSERT(fd>=0);
    ASSERT(connect(fd,(struct sockaddr *)&a,(socklen_t)(offsetof(struct sockaddr_un,sun_path)+1u+(size_t)n))==0);
    return fd;
}
static void pump(sink *s, int ms)
{
    struct pollfd p={ipc_server_fd(&g_srv),POLLIN,0};
    if(poll(&p,1,ms)>0) ASSERT(ipc_server_drain(&g_srv,on_open,s)==IPC_OK);
}
static void e2e(const uint8_t *w, size_t n, const model *m, ipc_result expect, bool foreign)
{
    if(g_srv_state==0) {
        snprintf(ipc_seam_namespace,sizeof ipc_seam_namespace,"fuzz-%ld",(long)getpid());
        unsetenv("XDG_RUNTIME_DIR");
        g_srv_state=ipc_server_init_display(&g_srv,NULL,NULL)==IPC_OK?1:-1;
    }
    if(g_srv_state<0) return;
    ipc_seam_foreign_abstract=foreign;
    sink s={m,0}; int fd=e2e_connect();
    ASSERT(send(fd,w,n,MSG_NOSIGNAL)==(ssize_t)n);
    uint8_t reply[8]; ssize_t got=-2;
    for(int i=0;i<60 && got==-2;i++) {
        pump(&s,50);
        ssize_t k=recv(fd,reply,sizeof reply,MSG_DONTWAIT);
        if(k>0 || k==0 || (k<0 && errno!=EAGAIN && errno!=EWOULDBLOCK)) got=k;
    }
    if(foreign) {
        ipc_seam_foreign_abstract=false;
        /* credential regression: a foreign peer must never reach the callback or see an ACK */
        ASSERT(s.calls==0 && got!=8 && got!=-2);
    } else {
        ASSERT(got==8 && memcmp(reply,"EDIR",4)==0 && reply[4]=='A' && reply[5]==(uint8_t)expect);
        ASSERT(s.calls==(expect==IPC_OK?1:0));
    }
    close(fd);
    for(int i=0;i<3;i++) pump(&s,0); /* retire the slot (wait clients too) */
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    /* Part 1: raw bytes. */
    ipc_request out={0}; ipc_result rc=ipc_test_wire_decode(data,size,&out);
    ASSERT(rc==IPC_OK || rc==IPC_PROTOCOL);
    if(rc==IPC_OK) {
        ASSERT(out.count<=IPC_MAX_PATHS && out.cwd!=NULL && out.cwd[0]=='/');
        for(size_t i=0;i<out.count;i++) ASSERT(out.paths[i].path[0]=='/' && out.paths[i].line && out.paths[i].col);
        ASSERT(!out.new_instance);
    } else ASSERT(out.count==0 && out.cwd==NULL && out.stdin_data==NULL && out.stdin_size==0);

    /* Part 2: build the model. Control bytes come first, stdin payload last. */
    rd r={data,size,0};
    uint8_t mk=r8(&r), sel=r8(&r); uint32_t arg=r32(&r), arg2=r32(&r);
    model m; memset(&m,0,sizeof m);
    gen_string(&r,m.cwd); m.count=mk%(MAXP+1u); m.wait=(mk&0x10u)!=0; m.has_stdin=(mk&0x20u)!=0;
    for(size_t i=0;i<m.count;i++) {
        gen_string(&r,m.path[i]); m.line[i]=(mk&0x40u)?r32(&r)|1u:1u+r8(&r); m.col[i]=(mk&0x80u)?r32(&r)|1u:1u+r8(&r);
    }
    if(m.has_stdin) {
        size_t rest=size-r.i; m.stdin_size=rest<sizeof m.stdin_buf?rest:sizeof m.stdin_buf;
        memcpy(m.stdin_buf,data+r.i,m.stdin_size);
    }
    ipc_request q; to_request(&m,&q); q.new_instance=(sel&0x80u)!=0;
    uint8_t wire[4096], base[4096]; size_t n=0;
    ASSERT(ipc_test_wire_encode(&q,base,sizeof base,&n)==IPC_OK);
    ASSERT(n>=24 && get32(base+8)==n && memcmp(base,"EDIP",4)==0);
    expect_ok(base,n,&m);
    /* Offsets of the structural fields, derived from the model. */
    size_t cwdn=strlen(m.cwd)+1u, pos=24u+cwdn, hdr[MAXP], str[MAXP], len[MAXP];
    for(size_t i=0;i<m.count;i++) { len[i]=strlen(m.path[i])+1u; hdr[i]=pos; str[i]=pos+12u; pos+=12u+len[i]; }
    ASSERT(n==pos+m.stdin_size && get32(base+12)==m.count && get32(base+16)==cwdn && get32(base+20)==m.stdin_size);

    memcpy(wire,base,n); size_t wn=n; model e=m; bool want_ok=true; bool ambiguous=false;
    switch((sel&0x3fu)%17u) {
    case 0: break;
    case 1: wn=(size_t)arg%n; want_ok=false; break;                                   /* truncation */
    case 2: { size_t k=1u+arg%8u; for(size_t i=0;i<k;i++) wire[n+i]=(uint8_t)arg2;
              wn=n+k; want_ok=false; if(arg2&1u) put32(wire+8,(uint32_t)wn); break; }  /* trailing bytes, header consistent or not */
    case 3: wire[arg%4u]^=(uint8_t)(1u+arg2%255u); want_ok=false; break;               /* magic */
    case 4: wire[4]=(uint8_t)(arg%255u>=1u?arg%255u+1u:2u); want_ok=false; break;      /* version != 1 */
    case 5: wire[5]=(uint8_t)(4u+arg%252u); want_ok=false; break;                       /* unknown flag bits */
    case 6: wire[6+arg%2u]=(uint8_t)(1u+arg2%255u); want_ok=false; break;               /* reserved */
    case 7: { uint32_t t=arg; if(t==n) t++; put32(wire+8,t); want_ok=false; break; }    /* total field */
    case 8: { uint32_t c=arg; put32(wire+12,c);
              if(c==m.count) break;
              if(c>IPC_MAX_PATHS || c<m.count || m.stdin_size==0) want_ok=false; else ambiguous=true;
              break; }                                                                  /* count field */
    case 9: { uint32_t c=arg; if(c==cwdn) c++; put32(wire+16,c); want_ok=false; break; } /* cwd length */
    case 10: { uint32_t c=arg; if(c==m.stdin_size) c++; put32(wire+20,c); want_ok=false; break; } /* input length */
    case 11: if(m.count) { size_t i=arg2%m.count; uint32_t c=arg; if(c==len[i]) c++; put32(wire+hdr[i],c); want_ok=false; } break;
    case 12: if(m.count) { size_t i=arg2%m.count; bool col=(arg&1u)!=0; uint32_t v=(arg>>1)&1u?0u:(arg2|1u);
                 put32(wire+hdr[i]+(col?8:4),v);
                 if(v==0) want_ok=false; else if(col) e.col[i]=v; else e.line[i]=v; } break; /* coordinates */
    case 13: { bool in_cwd=m.count==0 || (arg&1u); uint8_t b=(uint8_t)(arg2%256u); if(b=='/') b=0;
               size_t at=in_cwd?24u:str[(arg>>1)%m.count]; wire[at]=b; want_ok=false; break; } /* leading '/' */
    case 14: { bool in_cwd=m.count==0 || (arg&1u); size_t i=in_cwd?0:(arg>>1)%m.count;
               size_t base_at=in_cwd?24u:str[i], sl=in_cwd?cwdn:len[i];
               if(arg2&1u || sl<3u) wire[base_at+sl-1u]=(uint8_t)(1u+arg2%255u);        /* NUL terminator replaced */
               else wire[base_at+1u+(arg2>>1)%(sl-2u)]=0;                               /* interior NUL */
               want_ok=false; break; }
    case 15: { uint32_t which=arg%3u;
               if(which==0) { wire[5]^=1u; e.wait=!m.wait; }
               else if(m.stdin_size==0) { wire[5]^=2u; e.has_stdin=!m.has_stdin; }       /* empty stdin may toggle */
               else if(m.has_stdin) { wire[5]&=(uint8_t)~2u; want_ok=false; }            /* payload without flag */
               break; }
    default: if(m.stdin_size) { size_t i=arg%m.stdin_size; uint8_t b=(uint8_t)(arg2); wire[pos+i]=b; e.stdin_buf[i]=b; } break; /* payload byte */
    }
    if(ambiguous) { ipc_request o; rc=ipc_test_wire_decode(wire,wn,&o); ASSERT(rc==IPC_OK||rc==IPC_PROTOCOL); want_ok=(rc==IPC_OK); if(want_ok) {/* a lucky parse: nothing exact to assert */} }
    else if(want_ok) expect_ok(wire,wn,&e);
    else expect_proto(wire,wn);

    /* Part 3: encoder contract. */
    uint8_t again[4096]; size_t n2=0;
    ASSERT(ipc_test_wire_encode(&q,again,sizeof again,&n2)==IPC_OK && n2==n && memcmp(again,base,n)==0); /* deterministic, new_instance not serialized */
    size_t sink_n=7; ASSERT(ipc_test_wire_encode(&q,again,(size_t)arg%n,&sink_n)==IPC_LIMIT && sink_n==0);
    /* Requests the parser and decoder cannot produce, and input aliasing the
     * output, are caller bugs that die by ASSERT (A15): not encoder outcomes. */

    /* Part 4: end to end, deterministic 1/32 sample, only envelopes the server will answer. */
    if((mk&0x0cu)==0x0cu && ((arg2>>24)&7u)==3u && !ambiguous && wn>=24 && wn<=sizeof wire && (memcmp(wire,"EDIP",4)!=0 || get32(wire+8)==wn)) {
        ipc_request o; ipc_result d=ipc_test_wire_decode(wire,wn,&o);
        ASSERT(d==IPC_OK || d==IPC_PROTOCOL);
        bool foreign=(sel&0x40u)!=0 && (arg&3u)==0;
        e2e(wire,wn,&e,d,foreign);
    }

    /* Part 5: argv parsing (args.c). The input splits on NUL bytes into a
     * bounded argv. Every outcome is a valid ipc_result; a successful parse
     * satisfies the parser invariants, stdin round-trips exactly, and
     * activation capture fails only on arena exhaustion (IPC_IO). Paths
     * resolve read-only against the process cwd; nothing is created. */
    {
        char *argv[10]; static char storage[10][64]; int argc=1;
        argv[0]=(char *)"fuzz";
        size_t at=0;
        while(at<size && argc<10) {
            size_t alen=0;
            while(at+alen<size && data[at+alen] && alen<63) alen++;
            memcpy(storage[argc],data+at,alen); storage[argc][alen]=0;
            argv[argc]=storage[argc]; argc++; at+=alen+1;
        }
        ipc_args a; ipc_result prc=ipc_parse_args(argc,argv,&a);
        ASSERT(prc>=IPC_OK && prc<=IPC_REJECTED);
        if(prc==IPC_OK) {
            ASSERT(a.request.count<=IPC_MAX_PATHS && a.request.cwd && a.request.cwd[0]=='/');
            for(size_t i=0;i<a.request.count;i++)
                ASSERT(a.request.paths[i].path[0]=='/' && a.request.paths[i].line && a.request.paths[i].col);
            if(a.request.has_stdin) {
                int p[2]; ASSERT(pipe(p)==0);
                size_t payload=size<128?size:128;
                ASSERT(write(p[1],data,payload)==(ssize_t)payload); close(p[1]);
                ipc_result src2=ipc_args_read_stdin(&a,p[0]); close(p[0]);
                ASSERT(src2==IPC_OK && a.request.stdin_size==payload &&
                            (payload==0 || memcmp(a.request.stdin_data,data,payload)==0));
            }
            const char *tok=argc>1?storage[1]:NULL;
            ipc_result arc=ipc_args_set_activation(&a,tok,NULL);
            ASSERT(arc==IPC_OK || arc==IPC_IO);
            if(a.request.activation_token) ASSERT(strcmp(a.request.activation_token,tok)==0);
            ASSERT(a.request.startup_id==NULL);
        }
        ipc_args_fini(&a);
    }
    return 0;
}
