#include "ipc/ipc.h"
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <time.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FAIL %s:%d: %s\n",__FILE__,__LINE__,#x); exit(1); } } while (0)
#define CLIENT_TIMEOUT_MS 30000
typedef struct capture { size_t calls; ipc_token token; bool stdin_seen; } capture;
static ipc_result opened(const ipc_request *r, ipc_token token, void *ctx)
{
    capture *c=ctx; c->calls++; c->token=token;
    CHECK(r->count==1 && strcmp(r->paths[0].path,"/tmp/example")==0);
    CHECK(r->paths[0].line==12 && r->paths[0].col==3);
    if(r->has_stdin) { CHECK(r->stdin_size==5 && memcmp(r->stdin_data,"a\0b\nc",5)==0); c->stdin_seen=true; }
    return IPC_OK;
}
static ipc_request request(bool wait, bool input)
{
    ipc_request r={0}; r.count=1; r.cwd="/tmp"; r.paths[0]=(ipc_path){"/tmp/example",12,3};
    r.wait=wait; r.has_stdin=input; r.stdin_data=(const uint8_t *)"a\0b\nc"; r.stdin_size=input?5u:0u; return r;
}
static void parser(const char *dir)
{
    char cwd[IPC_PATH_CAP]; CHECK(getcwd(cwd,sizeof cwd)!=NULL); CHECK(chdir(dir)==0);
    int f=open("a:1",O_CREAT|O_WRONLY,0600); CHECK(f>=0); close(f);
    CHECK(mkdir("real",0700)==0); CHECK(symlink("real","link")==0);
    const struct { const char *arg,*suffix; uint32_t line,col; ipc_result rc; } cases[]={
        {"file:12","/file",12,1,IPC_OK},{"file:12:3","/file",12,3,IPC_OK},
        {"a:1","/a:1",1,1,IPC_OK},{"a:b:12","/a:b",12,1,IPC_OK},
        {"file:0","",0,0,IPC_INVALID},{"file:4294967296","",0,0,IPC_INVALID},
        {"link/../new","/new",1,1,IPC_OK},{"link/missing","/real/missing",1,1,IPC_OK},
        {"x:abc","/x:abc",1,1,IPC_OK}
    };
    for(size_t i=0;i<sizeof cases/sizeof cases[0];i++) {
        char *av[]={"sublimite",(char *)cases[i].arg}; ipc_args a={0};
        CHECK(ipc_parse_args(2,av,&a)==cases[i].rc);
        if(cases[i].rc==IPC_OK) { char want[IPC_PATH_CAP]; CHECK(snprintf(want,sizeof want,"%s%s",dir,cases[i].suffix)>0);
            CHECK(a.request.count==1 && strcmp(a.request.paths[0].path,want)==0);
            CHECK(a.request.paths[0].line==cases[i].line && a.request.paths[0].col==cases[i].col); }
        ipc_args_fini(&a);
    }
    char *av[]={"sublimite","--wait","--new-instance","-","--","--odd"}; ipc_args a={0};
    CHECK(ipc_parse_args(6,av,&a)==IPC_OK && a.request.wait && a.request.new_instance && a.request.has_stdin);
    CHECK(a.request.count==1); int p[2]; CHECK(pipe(p)==0); CHECK(write(p[1],"a\0b\nc",5)==5); close(p[1]);
    CHECK(ipc_args_read_stdin(&a,p[0])==IPC_OK && a.request.stdin_size==5); close(p[0]); ipc_args_fini(&a);
    char *bad[]={"sublimite","--bad"}; CHECK(ipc_parse_args(2,bad,&a)==IPC_INVALID);
    char *dup[]={"sublimite","-","-"}; CHECK(ipc_parse_args(3,dup,&a)==IPC_INVALID);
    CHECK(chdir(cwd)==0);
}
static void wire_tests(void)
{
    uint8_t wire[8192]; size_t n=0; ipc_request r=request(true,true), out={0};
    CHECK(ipc_wire_encode(&r,wire,sizeof wire,&n)==IPC_OK);
    CHECK(ipc_wire_decode(wire,n,&out)==IPC_OK && out.wait && out.has_stdin && out.stdin_size==5);
    for(size_t i=0;i<n;i++) CHECK(ipc_wire_decode(wire,i,&out)==IPC_PROTOCOL);
    wire[n]=0; CHECK(ipc_wire_decode(wire,n+1,&out)==IPC_PROTOCOL);
    wire[0]^=1u; CHECK(ipc_wire_decode(wire,n,&out)==IPC_PROTOCOL);
}
static void roundtrip(const char *dir, bool wait, bool input)
{
    ipc_server s={0}; ipc_result init_rc=ipc_server_init(&s,dir);
    if(init_rc!=IPC_OK) fprintf(stderr,"init rc=%d errno=%d (%s)\n",(int)init_rc,errno,strerror(errno));
    CHECK(init_rc==IPC_OK);
    pid_t child=fork(); CHECK(child>=0);
    if(child==0) { ipc_request r=request(wait,input); ipc_result rc=ipc_client_send(dir,&r,CLIENT_TIMEOUT_MS);
        if(rc!=IPC_OK) fprintf(stderr,"roundtrip: ipc_result=%d expected=%d\n",(int)rc,(int)IPC_OK);
        _exit(rc==IPC_OK?0:2); }
    capture c={0};
    for(int i=0;i<20 && c.calls==0;i++) { struct pollfd p={ipc_server_fd(&s),POLLIN,0}; CHECK(poll(&p,1,CLIENT_TIMEOUT_MS)>0); CHECK(ipc_server_drain(&s,opened,&c)==IPC_OK); }
    CHECK(c.calls==1 && c.stdin_seen==input);
    if(wait) { int status=0; CHECK(waitpid(child,&status,WNOHANG)==0); CHECK(ipc_server_report_closed(&s,c.token)==IPC_OK); CHECK(ipc_server_drain(&s,opened,&c)==IPC_OK); }
    int status=0; CHECK(waitpid(child,&status,0)==child && WIFEXITED(status) && WEXITSTATUS(status)==0);
    ipc_server_fini(&s);
}

static int raw_connect(const char *dir)
{
    struct sockaddr_un a={0}; a.sun_family=AF_UNIX;
    CHECK(snprintf(a.sun_path,sizeof a.sun_path,"%s/sublimite-%lu.sock",dir,(unsigned long)getuid())>0);
    int fd=socket(AF_UNIX,SOCK_STREAM,0); CHECK(fd>=0);
    CHECK(connect(fd,(struct sockaddr *)&a,sizeof a)==0); return fd;
}
static void drain_ready(ipc_server *s, capture *c)
{
    struct pollfd p={ipc_server_fd(s),POLLIN,0}; CHECK(poll(&p,1,CLIENT_TIMEOUT_MS)>0);
    edit_malloc_guard_begin();
    CHECK(ipc_server_drain(s,opened,c)==IPC_OK);
    size_t allocations=edit_malloc_guard_end();
    if(edit_malloc_guard_active()) CHECK(allocations==0);
}
static void fragmented(const char *dir)
{
    ipc_server s={0}; CHECK(ipc_server_init(&s,dir)==IPC_OK); capture c={0};
    ipc_server other={0}; CHECK(ipc_server_init(&other,dir)==IPC_EXISTS);
    int fd=raw_connect(dir); uint8_t bytes[8192], response[8]; size_t n=0; ipc_request r=request(false,true);
    CHECK(ipc_wire_encode(&r,bytes,sizeof bytes,&n)==IPC_OK);
    CHECK(write(fd,bytes,7)==7); drain_ready(&s,&c); CHECK(c.calls==0);
    CHECK(write(fd,bytes+7,17)==17); drain_ready(&s,&c); CHECK(c.calls==0);
    CHECK(write(fd,bytes+24,n-24)==(ssize_t)(n-24)); drain_ready(&s,&c); CHECK(c.calls==1);
    CHECK(read(fd,response,8)==8 && memcmp(response,"EDIRA",5)==0 && response[5]==IPC_OK); close(fd);
    CHECK(ipc_server_report_closed(&s,c.token)==IPC_INVALID);
    fd=raw_connect(dir); bytes[0]^=1u; CHECK(write(fd,bytes,n)==(ssize_t)n); drain_ready(&s,&c);
    CHECK(read(fd,response,8)==8 && response[5]==IPC_PROTOCOL && c.calls==1); close(fd);
    fd=raw_connect(dir); CHECK(write(fd,bytes,5)==5); close(fd); drain_ready(&s,&c); CHECK(c.calls==1);
    ipc_server_fini(&s);
}

static void wait_tokens(const char *dir)
{
    ipc_server s={0}; CHECK(ipc_server_init(&s,dir)==IPC_OK); capture c={0};
    int fds[2]; ipc_token tokens[2]; uint8_t bytes[8192], response[8]; size_t n=0;
    ipc_request r=request(true,false); CHECK(ipc_wire_encode(&r,bytes,sizeof bytes,&n)==IPC_OK);
    for(size_t i=0;i<2;i++) {
        fds[i]=raw_connect(dir); CHECK(write(fds[i],bytes,n)==(ssize_t)n);
        drain_ready(&s,&c); tokens[i]=c.token;
        CHECK(read(fds[i],response,8)==8 && response[4]=='A' && response[5]==IPC_OK);
    }
    CHECK(tokens[0]!=tokens[1] && c.calls==2);
    struct pollfd p={fds[0],POLLIN,0}; CHECK(poll(&p,1,20)==0);
    CHECK(ipc_server_report_closed(&s,tokens[1])==IPC_OK);
    CHECK(read(fds[1],response,8)==8 && response[4]=='C' && response[5]==IPC_OK);
    CHECK(poll(&p,1,20)==0); close(fds[1]);
    CHECK(ipc_server_report_closed(&s,tokens[1])==IPC_INVALID);
    CHECK(ipc_server_report_closed(&s,tokens[0])==IPC_OK);
    CHECK(read(fds[0],response,8)==8 && response[4]=='C' && response[5]==IPC_OK); close(fds[0]);
    ipc_server_fini(&s);
}
static void endpoint_validation(const char *dir)
{
    char path[IPC_PATH_CAP]; CHECK(snprintf(path,sizeof path,"%s/sublimite-%lu.sock",dir,(unsigned long)getuid())>0);
    int fd=open(path,O_CREAT|O_WRONLY,0600); CHECK(fd>=0); close(fd);
    ipc_server s={0}; CHECK(ipc_server_init(&s,dir)==IPC_INVALID); struct stat st; CHECK(stat(path,&st)==0 && S_ISREG(st.st_mode)); CHECK(unlink(path)==0);
    CHECK(chmod(dir,0755)==0); CHECK(ipc_server_init(&s,dir)==IPC_INVALID); CHECK(chmod(dir,0700)==0);
    CHECK(unsetenv("XDG_RUNTIME_DIR")==0); CHECK(ipc_server_init(&s,NULL)==IPC_OK);
    const char *ns=getenv("EDIT_IPC_NAMESPACE"); CHECK(ns!=NULL && ns[0]!='\0');
    char saved[108], alternate[108], name[108];
    CHECK(snprintf(saved,sizeof saved,"%s",ns)>0);
    int n=snprintf(name,sizeof name,"sublimite-%lu-%s",(unsigned long)getuid(),saved);
    CHECK(n>0 && (size_t)n<sizeof name);
    struct sockaddr_un bound={0}; socklen_t len=sizeof bound;
    CHECK(getsockname(s.listener,(struct sockaddr *)&bound,&len)==0);
    CHECK(bound.sun_path[0]=='\0' && len==offsetof(struct sockaddr_un,sun_path)+1u+(size_t)n);
    CHECK(memcmp(bound.sun_path+1,name,(size_t)n)==0);
    ipc_server other={0}; CHECK(ipc_server_init(&other,NULL)==IPC_EXISTS);
    CHECK(snprintf(alternate,sizeof alternate,"%s-other",saved)>0);
    CHECK(setenv("EDIT_IPC_NAMESPACE",alternate,1)==0);
    roundtrip(NULL,false,true); /* Independent abstract server AND client. */
    char oversized[108]; memset(oversized,'x',sizeof oversized-1); oversized[sizeof oversized-1]='\0';
    ipc_request r=request(false,false);
    CHECK(setenv("EDIT_IPC_NAMESPACE",oversized,1)==0);
    CHECK(ipc_server_init(&other,NULL)==IPC_LIMIT && ipc_client_send(NULL,&r,CLIENT_TIMEOUT_MS)==IPC_LIMIT);
    CHECK(ipc_server_init(&other,dir)==IPC_OK); ipc_server_fini(&other); /* Hook is abstract-only. */
    CHECK(setenv("EDIT_IPC_NAMESPACE","",1)==0);
    CHECK(ipc_server_init(&other,NULL)==IPC_INVALID && ipc_client_send(NULL,&r,CLIENT_TIMEOUT_MS)==IPC_INVALID);
    CHECK(setenv("EDIT_IPC_NAMESPACE",saved,1)==0);
    CHECK(ipc_server_init(&other,NULL)==IPC_EXISTS); ipc_server_fini(&s);
    CHECK(setenv("XDG_RUNTIME_DIR",dir,1)==0);
}

typedef struct callback_state { ipc_server *server; size_t calls; } callback_state;
static ipc_result rejected(const ipc_request *r, ipc_token token, void *ctx)
{
    (void)r; (void)token; callback_state *state=ctx; state->calls++; return IPC_REJECTED;
}
static ipc_result close_now(const ipc_request *r, ipc_token token, void *ctx)
{
    callback_state *state=ctx; state->calls++;
    CHECK(r->wait); CHECK(ipc_server_report_closed(state->server,token)==IPC_OK); return IPC_OK;
}
static void callback_results(const char *dir, bool reject)
{
    ipc_server s={0}; CHECK(ipc_server_init(&s,dir)==IPC_OK);
    callback_state state={&s,0};
    /* Accept a partial request before releasing the real client. A token is
     * assigned on accept, so it cannot prove the callback has sent an ACK. */
    int pending=raw_connect(dir), start[2]; CHECK(pipe(start)==0);
    CHECK(write(pending,"E",1)==1);
    pid_t child=fork(); CHECK(child>=0);
    if(child==0) { close(pending); close(start[1]); char b;
        CHECK(read(start[0],&b,1)==1); close(start[0]);
        ipc_request r=request(true,false); ipc_result rc=ipc_client_send(dir,&r,CLIENT_TIMEOUT_MS);
        ipc_result expected=reject?IPC_REJECTED:IPC_OK;
        if(rc!=expected) fprintf(stderr,"callback_results: reject=%d ipc_result=%d expected=%d (IPC_TIMEOUT=%d)\n",reject,(int)rc,(int)expected,(int)IPC_TIMEOUT);
        _exit(rc==expected?0:2); }
    close(start[0]);
    bool started=false;
    while(state.calls==0) {
        struct pollfd p={ipc_server_fd(&s),POLLIN,0}; CHECK(poll(&p,1,CLIENT_TIMEOUT_MS)>0);
        CHECK(ipc_server_drain(&s,reject?rejected:close_now,&state)==IPC_OK);
        if(!started) {
            CHECK(s.next_token==1 && state.calls==0);
            CHECK(write(start[1],"x",1)==1); close(start[1]); close(pending); started=true;
        }
    }
    CHECK(state.calls==1);
    int status; CHECK(waitpid(child,&status,0)==child && WIFEXITED(status) && WEXITSTATUS(status)==0);
    ipc_server_fini(&s);
}

static void stale(const char *dir)
{
    struct sockaddr_un a={0}; a.sun_family=AF_UNIX;
    CHECK(snprintf(a.sun_path,sizeof a.sun_path,"%s/sublimite-%lu.sock",dir,(unsigned long)getuid())>0);
    int fd=socket(AF_UNIX,SOCK_STREAM,0); CHECK(fd>=0); CHECK(bind(fd,(struct sockaddr *)&a,sizeof a)==0); close(fd);
    ipc_server s={0}; CHECK(ipc_server_init(&s,dir)==IPC_OK); ipc_server_fini(&s);
}
static void race(const char *dir)
{
    int start[2], result[2], finish[2]; CHECK(pipe(start)==0 && pipe(result)==0 && pipe(finish)==0);
    pid_t children[2];
    for(size_t i=0;i<2;i++) { children[i]=fork(); CHECK(children[i]>=0); if(children[i]==0) {
        close(start[1]); close(result[0]); close(finish[1]); char b; if(read(start[0],&b,1)!=1) _exit(3);
        ipc_server s={0}; ipc_result rc=ipc_server_init(&s,dir); b=rc==IPC_OK?'S':rc==IPC_EXISTS?'C':'E';
        if(write(result[1],&b,1)!=1) _exit(3);
        if(read(finish[0],&b,1)!=1) _exit(3);
        if(rc==IPC_OK) ipc_server_fini(&s);
        _exit(0);
    } }
    close(start[0]); close(result[1]); close(finish[0]); CHECK(write(start[1],"xx",2)==2);
    char b[2]; CHECK(read(result[0],b,1)==1 && read(result[0],b+1,1)==1);
    CHECK((b[0]=='S' && b[1]=='C') || (b[0]=='C' && b[1]=='S')); CHECK(write(finish[1],"xx",2)==2);
    for(size_t i=0;i<2;i++) { int status; CHECK(waitpid(children[i],&status,0)==children[i] && WEXITSTATUS(status)==0); }
    close(start[1]); close(result[0]); close(finish[1]);
}

/* ---- edit-457.21 review-fix tests -------------------------------------- */
static void rm_rf(const char *path)
{
    pid_t c=fork(); CHECK(c>=0);
    if(c==0) { execlp("rm","rm","-rf",path,(char *)NULL); _exit(127); }
    int st; CHECK(waitpid(c,&st,0)==c);
}
static void subdir(const char *dir, const char *name, char *out)
{
    CHECK(snprintf(out,IPC_PATH_CAP,"%s/%s",dir,name)>0); CHECK(mkdir(out,0700)==0);
}
static uint64_t now_ms(void)
{
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC,&ts);
    return (uint64_t)ts.tv_sec*1000u+(uint64_t)ts.tv_nsec/1000000u;
}
static void touch_file(const char *dir, const char *name, mode_t mode)
{
    char p[IPC_PATH_CAP]; CHECK(snprintf(p,sizeof p,"%s/%s",dir,name)>0);
    int f=open(p,O_CREAT|O_WRONLY,mode); CHECK(f>=0); close(f); CHECK(chmod(p,mode)==0);
}
static void sock_path(const char *dir, char *out)
{
    CHECK(snprintf(out,IPC_PATH_CAP,"%s/sublimite-%lu.sock",dir,(unsigned long)getuid())>0);
}
static void lock_path(const char *dir, char *out)
{
    CHECK(snprintf(out,IPC_PATH_CAP,"%s/sublimite-%lu.lock",dir,(unsigned long)getuid())>0);
}
static ipc_result count_only(const ipc_request *r, ipc_token token, void *ctx)
{
    (void)r; capture *c=ctx; c->calls++; c->token=token; return IPC_OK;
}
/* One poll+drain step. Returns the poll result (0 on timeout). */
static int pump(ipc_server *s, ipc_open_callback cb, void *ctx, int timeout_ms)
{
    struct pollfd p={ipc_server_fd(s),POLLIN,0}; int n=poll(&p,1,timeout_ms);
    CHECK(n>=0); if(n>0) CHECK(ipc_server_drain(s,cb,ctx)==IPC_OK); return n;
}
static pid_t spawn_client(const char *runtime, bool wait, ipc_result expected)
{
    pid_t child=fork(); CHECK(child>=0);
    if(child==0) { ipc_request r=request(wait,false); ipc_result rc=ipc_client_send(runtime,&r,CLIENT_TIMEOUT_MS);
        if(rc!=expected) fprintf(stderr,"client: ipc_result=%d expected=%d\n",(int)rc,(int)expected);
        _exit(rc==expected?0:2); }
    return child;
}
static void reap_ok(pid_t child)
{
    int status; CHECK(waitpid(child,&status,0)==child && WIFEXITED(status) && WEXITSTATUS(status)==0);
}
static void serve_one(ipc_server *s, capture *c)
{
    for(int i=0;i<40 && c->calls==0;i++) (void)pump(s,count_only,c,500);
    CHECK(c->calls==1);
}

/* 1: aliased input must never overrun the output buffer. */
static void t_wire_alias(const char *dir)
{
    (void)dir;
    struct { uint8_t wire[44]; uint8_t canary[32]; } g;
    memset(&g,0xA5,sizeof g); memset(g.wire,0,sizeof g.wire); memcpy(g.wire,"/a",3);
    ipc_request r={0}; r.cwd="/tmp"; r.count=1; r.paths[0]=(ipc_path){(const char *)g.wire,1,1};
    size_t n=99; ipc_result rc=ipc_wire_encode(&r,g.wire,sizeof g.wire,&n);
    for(size_t i=0;i<sizeof g.canary;i++) CHECK(g.canary[i]==0xA5);
    CHECK(rc==IPC_INVALID && n==0);
    /* cwd and stdin aliasing the output are rejected too */
    uint8_t buf[256]; memset(buf,0,sizeof buf); memcpy(buf,"/tmp",5);
    ipc_request c2={0}; c2.cwd=(const char *)buf; CHECK(ipc_wire_encode(&c2,buf,sizeof buf,&n)==IPC_INVALID);
    ipc_request c3=request(false,true); c3.stdin_data=buf+200; c3.stdin_size=5;
    CHECK(ipc_wire_encode(&c3,buf,sizeof buf,&n)==IPC_INVALID);
    /* decode then re-encode into the same storage: rejected; into other storage: identical */
    ipc_request src=request(true,true); uint8_t a[256], b[256], out[256]; size_t na=0, nb=0, no=0; ipc_request d={0};
    CHECK(ipc_wire_encode(&src,a,sizeof a,&na)==IPC_OK && ipc_wire_decode(a,na,&d)==IPC_OK);
    CHECK(ipc_wire_encode(&d,a,sizeof a,&nb)==IPC_INVALID);
    CHECK(ipc_wire_encode(&d,b,sizeof b,&nb)==IPC_OK && nb==na && memcmp(a,b,na)==0);
    memcpy(out,a,na); no=na; CHECK(ipc_wire_decode(out,no,&d)==IPC_OK);
    CHECK(ipc_wire_encode(&d,out+1,sizeof out-1,&nb)==IPC_INVALID); /* overlapping, shifted */
}

/* 17: table-driven wire validation with required results. */
static void t_wire_table(const char *dir)
{
    (void)dir;
    static uint8_t good[8192], mut[8192]; size_t n=0; ipc_request r=request(true,true), out;
    CHECK(ipc_wire_encode(&r,good,sizeof good,&n)==IPC_OK);
    static const uint8_t expect[]={'E','D','I','P',1,3,0,0, 0,0,0,0, 1,0,0,0, 5,0,0,0, 5,0,0,0,
        '/','t','m','p',0, 13,0,0,0, 12,0,0,0, 3,0,0,0, '/','t','m','p','/','e','x','a','m','p','l','e',0, 'a',0,'b','\n','c'};
    CHECK(n==sizeof expect); uint8_t exp2[sizeof expect]; memcpy(exp2,expect,n); exp2[8]=(uint8_t)n;
    CHECK(memcmp(good,exp2,n)==0);
    const struct { const char *name; size_t at; uint8_t val; } t[]={
        {"magic0",0,'X'},{"magic1",1,'X'},{"magic2",2,'X'},{"magic3",3,'X'},{"ver0",4,0},{"ver2",4,2},
        {"flag-unknown",5,7},{"flag-hi",5,0x80},{"stdin-flag-cleared",5,1},{"reserved6",6,1},{"reserved7",7,1},
        {"len-small",8,(uint8_t)(54+5-1)},{"len-big",8,(uint8_t)(54+5+1)},{"count129",12,129},{"count2",12,2},
        {"cwdn0",16,0},{"cwdn1",16,1},{"cwdn-big",16,200},{"cwd-relative",24,'t'},{"cwd-no-nul",28,'x'},
        {"inputn-small",20,4},{"inputn-big",20,6},{"inputn-no-flag-mismatch",23,1},
        {"path-n0",29,0},{"path-n1",29,1},{"path-n-big",29,200},{"path-n-short",29,12},{"line0",33,0},{"col0",37,0},
        {"path-relative",41,'x'},{"path-nul-inside",44,0},{"path-no-nul",53,'x'},{"path-len-hi",30,1}};
    for(size_t i=0;i<sizeof t/sizeof t[0];i++) {
        memcpy(mut,good,n); mut[t[i].at]=t[i].val; memset(&out,0xFF,sizeof out);
        ipc_result rc=ipc_wire_decode(mut,n,&out);
        if(rc!=IPC_PROTOCOL) fprintf(stderr,"table %s: rc=%d\n",t[i].name,(int)rc);
        CHECK(rc==IPC_PROTOCOL);
        CHECK(out.count==0 && out.cwd==NULL && out.stdin_data==NULL && out.stdin_size==0 && !out.wait && !out.has_stdin);
    }
    /* header-only frames: valid empty request, and truncated variants */
    uint8_t h[24+2]; memset(h,0,sizeof h); memcpy(h,"EDIP",4); h[4]=1; h[8]=26; h[16]=2; h[24]='/'; h[25]=0;
    memset(&out,0xFF,sizeof out); CHECK(ipc_wire_decode(h,26,&out)==IPC_OK && out.count==0 && !out.wait);
    CHECK(ipc_wire_decode(NULL,26,&out)==IPC_PROTOCOL && out.cwd==NULL);
    static uint8_t big[IPC_MAX_WIRE+1];
    CHECK(ipc_wire_decode(big,sizeof big,&out)==IPC_PROTOCOL);
    /* maximum frame round trips exactly; one more byte is IPC_LIMIT */
    static uint8_t maxw[IPC_MAX_WIRE]; static uint8_t payload[IPC_MAX_WIRE];
    ipc_request m=request(false,true); m.stdin_data=payload; m.stdin_size=IPC_MAX_WIRE-54u;
    size_t mn=0; CHECK(ipc_wire_encode(&m,maxw,sizeof maxw,&mn)==IPC_OK && mn==IPC_MAX_WIRE);
    CHECK(ipc_wire_decode(maxw,mn,&out)==IPC_OK && out.stdin_size==IPC_MAX_WIRE-54u);
    m.stdin_size++; CHECK(ipc_wire_encode(&m,maxw,sizeof maxw,&mn)==IPC_LIMIT && mn==0);
    uint8_t empty_in[1]; ipc_request e=request(false,false); e.stdin_data=empty_in; e.stdin_size=1;
    CHECK(ipc_wire_encode(&e,mut,sizeof mut,&mn)==IPC_INVALID);
}

/* 3, 4: pathname semantics and literal-name precedence. */
static void t_parser_paths(const char *dir)
{
    char sub[IPC_PATH_CAP], saved[IPC_PATH_CAP]; subdir(dir,"parse",sub);
    CHECK(getcwd(saved,sizeof saved)!=NULL); CHECK(chdir(sub)==0);
    touch_file(sub,"leaf",0600); touch_file(sub,"victim",0600); touch_file(sub,"loop",0600);
    CHECK(mkdir("sd",0700)==0); CHECK(symlink("nowhere","dang:1")==0); CHECK(symlink("loop:1","loop:1")==0);
    CHECK(symlink("leaf","lnk")==0);
    const struct { const char *arg,*suffix; uint32_t line; bool ok; } c[]={
        {"leaf/../victim","",0,false},{"leaf/",NULL,0,false},{"leaf/.",NULL,0,false},{"leaf/..",NULL,0,false},
        {"leaf/missing",NULL,0,false},{"lnk/../victim",NULL,0,false},{"lnk/",NULL,0,false},
        {"sd/","/sd",1,true},{"sd/.","/sd",1,true},{"missing/../x","/x",1,true},{"missing/y","/missing/y",1,true},
        {"sd/../leaf","/leaf",1,true},{"dang:1","/dang:1",1,true},{"nonexist:3","/nonexist",3,true},
        {"loop:1",NULL,0,false},{"loop:1:2",NULL,0,true}};
    for(size_t i=0;i<sizeof c/sizeof c[0];i++) {
        char *av[]={"sublimite",(char *)c[i].arg}; ipc_args a={0}; ipc_result rc=ipc_parse_args(2,av,&a);
        if((rc==IPC_OK)!=c[i].ok) fprintf(stderr,"parse case %zu '%s': rc=%d path=%s\n",i,c[i].arg,(int)rc,rc==IPC_OK?a.request.paths[0].path:"-");
        if(strcmp(c[i].arg,"loop:1:2")==0) { if(rc==IPC_OK) ipc_args_fini(&a); continue; }
        CHECK((rc==IPC_OK)==c[i].ok);
        if(rc==IPC_OK) { char want[IPC_PATH_CAP]; CHECK(snprintf(want,sizeof want,"%s%s",sub,c[i].suffix)>0);
            CHECK(strcmp(a.request.paths[0].path,want)==0 && a.request.paths[0].line==c[i].line); ipc_args_fini(&a); }
    }
    CHECK(chdir(saved)==0); rm_rf(sub);
}

/* 2: lifecycle operations are pinned to the verified directory. */
static void t_runtime_swap(const char *dir)
{
    char rt[IPC_PATH_CAP], moved[IPC_PATH_CAP], sock[IPC_PATH_CAP], victim[IPC_PATH_CAP];
    subdir(dir,"rt",rt); CHECK(snprintf(moved,sizeof moved,"%s/rt-moved",dir)>0);
    CHECK(snprintf(victim,sizeof victim,"%s/victim",dir)>0);
    int vf=open(victim,O_CREAT|O_WRONLY,0644); CHECK(vf>=0); close(vf); CHECK(chmod(victim,0644)==0);
    ipc_server s={0}; CHECK(ipc_server_init(&s,rt)==IPC_OK);
    struct stat st; sock_path(rt,sock); CHECK(lstat(sock,&st)==0 && S_ISSOCK(st.st_mode) && (st.st_mode&0777)==0600);
    CHECK(rename(rt,moved)==0); CHECK(mkdir(rt,0700)==0);
    CHECK(symlink(victim,sock)==0); /* decoy: a symlink sitting where the socket used to be */
    ipc_server_fini(&s);
    char moved_sock[IPC_PATH_CAP]; sock_path(moved,moved_sock);
    CHECK(lstat(moved_sock,&st)!=0 && errno==ENOENT); /* removed through the pinned directory */
    CHECK(lstat(sock,&st)==0 && S_ISLNK(st.st_mode)); /* decoy untouched */
    CHECK(stat(victim,&st)==0 && (st.st_mode&0777)==0644);
    /* a symlink planted at the socket name is never chmod'ed or followed */
    CHECK(unlink(sock)==0); CHECK(symlink(victim,sock)==0);
    CHECK(ipc_server_init(&s,rt)==IPC_INVALID); CHECK(stat(victim,&st)==0 && (st.st_mode&0777)==0644);
    rm_rf(rt); rm_rf(moved); CHECK(unlink(victim)==0);
}

/* 10: bounded UI work per drain call. */
static void t_drain_budget(const char *dir)
{
    char rt[IPC_PATH_CAP]; subdir(dir,"budget",rt);
    ipc_server s={0}; CHECK(ipc_server_init(&s,rt)==IPC_OK); capture c={0};
    uint8_t frame[256]; size_t n=0; ipc_request r=request(false,false); CHECK(ipc_wire_encode(&r,frame,sizeof frame,&n)==IPC_OK);
    int fds[24];
    for(size_t i=0;i<24;i++) { fds[i]=raw_connect(rt); CHECK(write(fds[i],frame,n)==(ssize_t)n); }
    usleep(20000);
    CHECK(ipc_server_drain(&s,count_only,&c)==IPC_OK);
    if(c.calls>IPC_DRAIN_CALLBACKS) fprintf(stderr,"drain ran %zu callbacks in one call (budget %u)\n",c.calls,IPC_DRAIN_CALLBACKS);
    CHECK(c.calls>0 && c.calls<=IPC_DRAIN_CALLBACKS);
    for(int i=0;i<40 && c.calls<24;i++) (void)pump(&s,count_only,&c,200);
    CHECK(c.calls==24);
    for(size_t i=0;i<24;i++) close(fds[i]);
    for(int i=0;i<10;i++) (void)pump(&s,count_only,&c,20);
    /* byte budget: 8 near-complete 60 KB frames */
    static uint8_t body[60000]; memset(body,'x',sizeof body);
    ipc_request big=request(false,true); big.stdin_data=body; big.stdin_size=sizeof body;
    static uint8_t wire[IPC_RX_SMALL_SIZE]; CHECK(ipc_wire_encode(&big,wire,sizeof wire,&n)==IPC_OK);
    c.calls=0; int bf[8];
    for(size_t i=0;i<8;i++) { bf[i]=raw_connect(rt); CHECK(write(bf[i],wire,n-1)==(ssize_t)(n-1)); }
    usleep(20000);
    usleep(20000);
    CHECK(ipc_server_drain(&s,count_only,&c)==IPC_OK);
    if(s.drain_bytes==0 || s.drain_bytes>IPC_DRAIN_BYTES) fprintf(stderr,"drain_bytes=%llu budget=%u\n",(unsigned long long)s.drain_bytes,IPC_DRAIN_BYTES);
    CHECK(s.drain_bytes>0 && s.drain_bytes<=IPC_DRAIN_BYTES);
    for(size_t i=0;i<8;i++) CHECK(write(bf[i],wire+n-1,1)==1);
    for(int i=0;i<60 && c.calls<8;i++) { (void)pump(&s,count_only,&c,200); CHECK(s.drain_bytes<=IPC_DRAIN_BYTES); }
    CHECK(c.calls==8);
    for(size_t i=0;i<8;i++) close(bf[i]);
    ipc_server_fini(&s); rm_rf(rt);
}

/* 11: incomplete clients cannot hold every slot forever. */
static void silent_fill(ipc_server *s, const char *rt, int *fds)
{
    capture c={0};
    for(size_t i=0;i<IPC_MAX_CLIENTS;i++) fds[i]=raw_connect(rt);
    for(int i=0;i<20 && s->next_token<IPC_MAX_CLIENTS;i++) (void)pump(s,count_only,&c,50);
    CHECK(s->next_token==IPC_MAX_CLIENTS);
}
static void t_slot_exhaust(const char *dir)
{
    char rt[IPC_PATH_CAP]; subdir(dir,"slots",rt); int fds[IPC_MAX_CLIENTS]; capture c={0};
    /* A: absolute pre-ACK deadline; the timer wakes the epoll fd by itself */
    ipc_server s={0}; CHECK(ipc_server_init(&s,rt)==IPC_OK); s.request_deadline_ms=200; s.evict_idle_ms=600000;
    silent_fill(&s,rt,fds);
    int woke=pump(&s,count_only,&c,3000);
    if(woke==0) fprintf(stderr,"deadline timer never woke the loop\n");
    CHECK(woke>0);
    for(size_t i=0;i<IPC_MAX_CLIENTS;i++) { /* later arrivals expire on their own timer ticks */
        char b; ssize_t got=recv(fds[i],&b,1,MSG_DONTWAIT);
        for(int k=0;k<20 && got!=0;k++) { (void)pump(&s,count_only,&c,300); got=recv(fds[i],&b,1,MSG_DONTWAIT); }
        CHECK(got==0); close(fds[i]); }
    pid_t ch=spawn_client(rt,false,IPC_OK); serve_one(&s,&c); reap_ok(ch);
    ipc_server_fini(&s);
    /* B: pressure eviction of the longest-idle incomplete client */
    CHECK(ipc_server_init(&s,rt)==IPC_OK); s.request_deadline_ms=600000; s.evict_idle_ms=100; c.calls=0;
    silent_fill(&s,rt,fds); usleep(150000);
    ch=spawn_client(rt,false,IPC_OK); serve_one(&s,&c); reap_ok(ch);
    for(size_t i=0;i<IPC_MAX_CLIENTS;i++) close(fds[i]);
    ipc_server_fini(&s); rm_rf(rt);
}

/* 12: lifecycle lock waits are bounded. */
static void t_lock_timeout(const char *dir)
{
    char rt[IPC_PATH_CAP], lp[IPC_PATH_CAP], sp[IPC_PATH_CAP]; subdir(dir,"lock",rt); lock_path(rt,lp); sock_path(rt,sp);
    ipc_server s={0}; CHECK(ipc_server_init(&s,rt)==IPC_OK); ipc_server_fini(&s);
    alarm(15);
    int hold=open(lp,O_RDWR|O_CLOEXEC); CHECK(hold>=0); CHECK(flock(hold,LOCK_EX)==0);
    uint64_t t0=now_ms(); ipc_result rc=ipc_server_init(&s,rt); uint64_t dt=now_ms()-t0;
    if(rc!=IPC_TIMEOUT) fprintf(stderr,"init under held lock: rc=%d after %llu ms\n",(int)rc,(unsigned long long)dt);
    CHECK(rc==IPC_TIMEOUT && dt<IPC_LOCK_TIMEOUT_MS+1500u);
    CHECK(flock(hold,LOCK_UN)==0);
    CHECK(ipc_server_init(&s,rt)==IPC_OK);
    CHECK(flock(hold,LOCK_EX)==0);
    t0=now_ms(); ipc_server_fini(&s); dt=now_ms()-t0; CHECK(dt<IPC_LOCK_TIMEOUT_MS+1500u);
    struct stat st; CHECK(lstat(sp,&st)==0 && S_ISSOCK(st.st_mode)); /* ownership preserved, not unlinked blind */
    CHECK(flock(hold,LOCK_UN)==0); close(hold);
    CHECK(ipc_server_init(&s,rt)==IPC_OK); ipc_server_fini(&s); /* stale socket recovered */
    CHECK(lstat(sp,&st)!=0);
    alarm(120); rm_rf(rt);
}

/* 13 + 17: foreign credentials, squatting, fallback endpoint. */
static void t_foreign_cred(const char *dir)
{
    char rt[IPC_PATH_CAP], uidbuf[32]; subdir(dir,"cred",rt); CHECK(unsetenv("XDG_RUNTIME_DIR")==0);
    char ns[128]; CHECK(snprintf(ns,sizeof ns,"%s-cred",getenv("EDIT_IPC_NAMESPACE"))>0);
    CHECK(setenv("EDIT_IPC_NAMESPACE",ns,1)==0); CHECK(setenv("EDIT_IPC_FALLBACK_DIR",rt,1)==0);
    CHECK(snprintf(uidbuf,sizeof uidbuf,"%lu",(unsigned long)getuid()+1ul)>0);
    ipc_request r=request(false,false); capture c={0};
    /* client refuses a server whose credentials are foreign */
    ipc_server s={0}; CHECK(ipc_server_init(&s,NULL)==IPC_OK);
    CHECK(setenv("EDIT_IPC_TEST_PEER_UID",uidbuf,1)==0);
    ipc_result rc=ipc_client_send(NULL,&r,1000);
    if(rc!=IPC_REJECTED) fprintf(stderr,"client foreign-server rc=%d\n",(int)rc);
    CHECK(rc==IPC_REJECTED);
    /* server refuses a foreign client: connection dropped, callback never runs */
    pid_t ch=fork(); CHECK(ch>=0);
    if(ch==0) { unsetenv("EDIT_IPC_TEST_PEER_UID"); ipc_result x=ipc_client_send(NULL,&r,5000); _exit(x==IPC_IO?0:2); }
    for(int i=0;i<10;i++) { int st; if(waitpid(ch,&st,WNOHANG)==ch) { CHECK(WIFEXITED(st) && WEXITSTATUS(st)==0); ch=-1; break; } (void)pump(&s,count_only,&c,200); }
    CHECK(ch==-1 && c.calls==0);
    ipc_server_fini(&s);
    /* another uid squats the abstract name: we fall back to the private path */
    struct sockaddr_un a={0}; a.sun_family=AF_UNIX;
    int n=snprintf(a.sun_path+1,sizeof a.sun_path-1,"sublimite-%lu-%s",(unsigned long)getuid(),ns); CHECK(n>0);
    int squat=socket(AF_UNIX,SOCK_STREAM,0); CHECK(squat>=0);
    CHECK(bind(squat,(struct sockaddr *)&a,(socklen_t)(offsetof(struct sockaddr_un,sun_path)+1u+(size_t)n))==0 && listen(squat,4)==0);
    rc=ipc_server_init(&s,NULL);
    if(rc!=IPC_OK) fprintf(stderr,"init with squatted abstract name: rc=%d\n",(int)rc);
    CHECK(rc==IPC_OK);
    char sp[IPC_PATH_CAP]; sock_path(rt,sp); struct stat st; CHECK(lstat(sp,&st)==0 && S_ISSOCK(st.st_mode));
    ipc_server other={0}; CHECK(ipc_server_init(&other,NULL)==IPC_EXISTS);
    ch=spawn_client(NULL,false,IPC_OK); serve_one(&s,&c); reap_ok(ch);
    ipc_server_fini(&s); close(squat);
    CHECK(unsetenv("EDIT_IPC_TEST_PEER_UID")==0); CHECK(unsetenv("EDIT_IPC_FALLBACK_DIR")==0);
    CHECK(setenv("EDIT_IPC_NAMESPACE",ns+0,1)==0);
    char saved[128]; CHECK(snprintf(saved,sizeof saved,"%s",ns)>0); saved[strlen(ns)-5]='\0';
    CHECK(setenv("EDIT_IPC_NAMESPACE",saved,1)==0); CHECK(setenv("XDG_RUNTIME_DIR",dir,1)==0);
    rm_rf(rt);
}

/* 14: receive storage is a small shared pool, released after the callback. */
static void t_memory(const char *dir)
{
    char rt[IPC_PATH_CAP]; subdir(dir,"mem",rt);
    ipc_server s={0}; CHECK(ipc_server_init(&s,rt)==IPC_OK); capture c={0};
    if(s.arena.size>4u*1024u*1024u) fprintf(stderr,"ipc arena is %zu bytes\n",s.arena.size);
    CHECK(s.arena.size<=4u*1024u*1024u);
    uint8_t hdr[24]; memset(hdr,0,sizeof hdr); memcpy(hdr,"EDIP",4); hdr[4]=1; hdr[10]=0x10; /* total = 1 MiB */
    int big[IPC_RX_BIG_COUNT+1];
    for(size_t i=0;i<=IPC_RX_BIG_COUNT;i++) { big[i]=raw_connect(rt); CHECK(write(big[i],hdr,sizeof hdr)==(ssize_t)sizeof hdr); }
    for(int i=0;i<12;i++) (void)pump(&s,count_only,&c,50);
    uint8_t reply[8]; CHECK(recv(big[IPC_RX_BIG_COUNT],reply,8,0)==8 && reply[4]=='A' && reply[5]==IPC_BUSY); /* bounded admission */
    for(size_t i=0;i<IPC_RX_BIG_COUNT;i++) CHECK(recv(big[i],reply,8,MSG_DONTWAIT)<0 && errno==EAGAIN);
    pid_t ch=spawn_client(rt,false,IPC_OK); serve_one(&s,&c); reap_ok(ch); /* small work still flows */
    for(size_t i=0;i<=IPC_RX_BIG_COUNT;i++) close(big[i]);
    for(int i=0;i<10;i++) (void)pump(&s,count_only,&c,20);
    c.calls=0;
    for(int k=0;k<60;k++) { ch=spawn_client(rt,false,IPC_OK); size_t want=c.calls+1; for(int i=0;i<40 && c.calls<want;i++) (void)pump(&s,count_only,&c,500); CHECK(c.calls==want); reap_ok(ch); }
    ipc_server_fini(&s); rm_rf(rt);
}

/* 15: wait associations are retired when the client disconnects. */
static void t_wait_disconnect(const char *dir)
{
    char rt[IPC_PATH_CAP]; subdir(dir,"waitdrop",rt);
    ipc_server s={0}; CHECK(ipc_server_init(&s,rt)==IPC_OK); capture c={0};
    uint8_t bytes[256], reply[8]; size_t n=0; ipc_request r=request(true,false); CHECK(ipc_wire_encode(&r,bytes,sizeof bytes,&n)==IPC_OK);
    for(int k=0;k<100;k++) {
        int fd=raw_connect(rt); CHECK(write(fd,bytes,n)==(ssize_t)n);
        size_t want=c.calls+1; for(int i=0;i<40 && c.calls<want;i++) (void)pump(&s,count_only,&c,200);
        CHECK(c.calls==want); CHECK(recv(fd,reply,8,0)==8 && reply[4]=='A');
        ipc_token t=c.token; CHECK(ipc_server_token_live(&s,t));
        close(fd);
        for(int i=0;i<40 && ipc_server_token_live(&s,t);i++) (void)pump(&s,count_only,&c,50);
        if(ipc_server_token_live(&s,t)) fprintf(stderr,"token %llu still live after disconnect (cycle %d)\n",(unsigned long long)t,k);
        CHECK(!ipc_server_token_live(&s,t));
        CHECK(s.wait_drops==(uint64_t)k+1u);
        CHECK(ipc_server_report_closed(&s,t)==IPC_INVALID);
    }
    CHECK(!ipc_server_token_live(&s,0) && !ipc_server_token_live(&s,12345678));
    /* a wait client that got its closed reply is retired without counting as a drop */
    int fd=raw_connect(rt); CHECK(write(fd,bytes,n)==(ssize_t)n); size_t want=c.calls+1;
    for(int i=0;i<40 && c.calls<want;i++) (void)pump(&s,count_only,&c,200);
    CHECK(ipc_server_report_closed(&s,c.token)==IPC_OK); CHECK(recv(fd,reply,8,0)==8); CHECK(recv(fd,reply,8,0)==8 && reply[4]=='C'); close(fd);
    for(int i=0;i<5;i++) (void)pump(&s,count_only,&c,20);
    CHECK(s.wait_drops==100u);
    ipc_server_fini(&s); rm_rf(rt);
}

/* 16: --wait for primary and isolated launches. */
static pid_t launcher_child(int fds[2], ipc_result expected)
{
    CHECK(ipc_launcher_pair(fds)==IPC_OK);
    pid_t ch=fork(); CHECK(ch>=0);
    if(ch==0) { close(fds[0]); ipc_result x=ipc_launcher_wait(fds[1]);
        if(x!=expected) fprintf(stderr,"launcher: rc=%d expected=%d\n",(int)x,(int)expected);
        _exit(x==expected?0:2); }
    close(fds[1]); return ch;
}
static void t_launcher(const char *dir)
{
    char rt[IPC_PATH_CAP]; subdir(dir,"launch",rt);
    for(int isolated=0;isolated<2;isolated++) {
        ipc_server s={0}; ipc_result rc=isolated?ipc_server_init_isolated(&s):ipc_server_init(&s,rt); CHECK(rc==IPC_OK);
        if(isolated) { char sp[IPC_PATH_CAP]; struct stat st; sock_path(rt,sp); CHECK(s.listener<0 && !s.owns_path && lstat(sp,&st)!=0); }
        int fds[2]; pid_t ch=launcher_child(fds,IPC_OK); ipc_token t=0;
        CHECK(ipc_server_adopt_wait(&s,fds[0],&t)==IPC_OK && t!=0 && ipc_server_token_live(&s,t));
        usleep(50000); int st; CHECK(waitpid(ch,&st,WNOHANG)==0); /* launcher blocks until completion */
        CHECK(ipc_server_report_closed(&s,t)==IPC_OK); reap_ok(ch);
        capture c={0}; for(int i=0;i<5;i++) (void)pump(&s,count_only,&c,20);
        CHECK(!ipc_server_token_live(&s,t) && s.wait_drops==0);
        /* the UI process goes away first: the launcher must fail, not report success */
        ch=launcher_child(fds,IPC_IO); CHECK(ipc_server_adopt_wait(&s,fds[0],&t)==IPC_OK);
        ipc_server_fini(&s); reap_ok(ch);
        /* malformed adoption */
        CHECK(isolated?ipc_server_init_isolated(&s)==IPC_OK:ipc_server_init(&s,rt)==IPC_OK);
        CHECK(ipc_server_adopt_wait(&s,-1,&t)==IPC_INVALID && ipc_server_adopt_wait(&s,0,NULL)==IPC_INVALID);
        ipc_server_fini(&s);
    }
    rm_rf(rt);
}
static int run_suite(const char *only, int ns_fd)
{
    alarm(120); char tmp[]="/tmp/edit-ipc-test-XXXXXX"; char *dir=mkdtemp(tmp); CHECK(dir!=NULL); CHECK(setenv("XDG_RUNTIME_DIR",dir,1)==0);
    /* mkdtemp makes this namespace unique across processes and worktrees.
     * Forked clients must inherit it rather than choose their own endpoint. */
    CHECK(setenv("EDIT_IPC_NAMESPACE",dir+5,1)==0);
    if(ns_fd>=0) { size_t l=strlen(dir+5); CHECK(write(ns_fd,dir+5,l+1u)==(ssize_t)(l+1u)); close(ns_fd); } /* --parallel: report the namespace */
#define T(name,call) do { if(only==NULL || strcmp(only,name)==0) { call; } } while(0)
    T("parser",parser(dir)); T("wire",wire_tests()); T("roundtrip",roundtrip(dir,false,false)); T("roundtrip",roundtrip(dir,true,false)); T("roundtrip",roundtrip(dir,false,true));
    T("stale",stale(dir)); T("race",race(dir)); T("fragmented",fragmented(dir)); T("tokens",wait_tokens(dir)); T("callbacks",callback_results(dir,true)); T("callbacks",callback_results(dir,false)); T("endpoints",endpoint_validation(dir));
    T("wire_alias",t_wire_alias(dir)); T("wire_table",t_wire_table(dir)); T("parser_paths",t_parser_paths(dir)); T("runtime_swap",t_runtime_swap(dir));
    T("drain_budget",t_drain_budget(dir)); T("slot_exhaust",t_slot_exhaust(dir)); T("lock_timeout",t_lock_timeout(dir)); T("foreign_cred",t_foreign_cred(dir));
    T("memory",t_memory(dir)); T("wait_disconnect",t_wait_disconnect(dir)); T("launcher",t_launcher(dir));
#undef T
    if(only!=NULL) { rm_rf(dir); return 0; }
    char p[IPC_PATH_CAP]; const char *names[]={"a:1","link","real"};
    for(size_t i=0;i<3;i++) { CHECK(snprintf(p,sizeof p,"%s/%s",dir,names[i])>0); if(i==2) CHECK(rmdir(p)==0); else CHECK(unlink(p)==0); }
    CHECK(snprintf(p,sizeof p,"%s/sublimite-%lu.lock",dir,(unsigned long)getuid())>0); CHECK(unlink(p)==0); CHECK(rmdir(dir)==0);
    puts("ipc_test: parser, wire, roundtrip, --wait, stdin, stale, race, fragments, tokens, endpoints, review-fix suites ok"); return 0;
}

#define PAR_MAX 16u
/* --parallel [N [ROUNDS]]: N independent suites at once, each with its own
 * EDIT_IPC_NAMESPACE (reported over a pipe and checked pairwise distinct). A
 * shared endpoint, a leaked default name or a cross-talking client fails here
 * (edit-457.17 isolation regression). Defaults 4 x 1: part of nothing in
 * `make check`, which stays at one serial suite. */
static int parallel(size_t n, size_t rounds)
{
    size_t failures=0;
    for(size_t round=0;round<rounds;round++) {
        pid_t children[PAR_MAX]; int rd[PAR_MAX]; char ns[PAR_MAX][64];
        for(size_t i=0;i<n;i++) {
            int p[2]; CHECK(pipe(p)==0); children[i]=fork(); CHECK(children[i]>=0);
            if(children[i]==0) { close(p[0]); exit(run_suite(NULL,p[1])); }
            close(p[1]); rd[i]=p[0];
        }
        for(size_t i=0;i<n;i++) {
            memset(ns[i],0,sizeof ns[i]); size_t got=0; ssize_t k;
            while(got<sizeof ns[i]-1u && (k=read(rd[i],ns[i]+got,1))==1) { got++; if(ns[i][got-1]==0) break; }
            close(rd[i]); CHECK(got>1u);
            for(size_t j=0;j<i;j++) CHECK(strcmp(ns[i],ns[j])!=0);
            int status; CHECK(waitpid(children[i],&status,0)==children[i]);
            if(!WIFEXITED(status) || WEXITSTATUS(status)!=0) failures++;
        }
    }
    printf("ipc_test parallel: %zu rounds x %zu processes = %zu runs, distinct namespaces, %zu failures\n",rounds,n,rounds*n,failures);
    return failures==0?0:1;
}
int main(int argc, char **argv)
{
    if(argc==1) return run_suite(NULL,-1);
    if(strcmp(argv[1],"--parallel")!=0) return run_suite(argv[1],-1); /* run one named test */
    size_t n=argc>2?(size_t)strtoul(argv[2],NULL,10):4u, rounds=argc>3?(size_t)strtoul(argv[3],NULL,10):1u;
    CHECK(n>=2u && n<=PAR_MAX && rounds>=1u);
    return parallel(n,rounds);
}
