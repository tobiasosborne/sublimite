#include "ipc/ipc.h"
#include "ipc_isolation.h"
#include "ipc_peer_seam.h"
#include "assert_death.h"
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <dirent.h>
#include <sys/file.h>
#include <time.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>


#define CLIENT_TIMEOUT_MS 30000
typedef struct capture { size_t calls; ipc_token token; bool stdin_seen; } capture;
static ipc_result opened(const ipc_request *r, ipc_token token, void *ctx)
{
    capture *c=ctx; c->calls++; c->token=token;
    ASSERT(r->count==1 && strcmp(r->paths[0].path,"/tmp/example")==0);
    ASSERT(r->paths[0].line==12 && r->paths[0].col==3);
    if(r->has_stdin) { ASSERT(r->stdin_size==5 && memcmp(r->stdin_data,"a\0b\nc",5)==0); c->stdin_seen=true; }
    return IPC_OK;
}
static ipc_request request(bool wait, bool input)
{
    ipc_request r={0}; r.count=1; r.cwd="/tmp"; r.paths[0]=(ipc_path){"/tmp/example",12,3};
    r.wait=wait; r.has_stdin=input; r.stdin_data=(const uint8_t *)"a\0b\nc"; r.stdin_size=input?5u:0u; return r;
}
static void parser(const char *dir)
{
    char cwd[IPC_PATH_CAP]; ASSERT(getcwd(cwd,sizeof cwd)!=NULL); ASSERT(chdir(dir)==0);
    int f=open("a:1",O_CREAT|O_WRONLY,0600); ASSERT(f>=0); close(f);
    ASSERT(mkdir("real",0700)==0); ASSERT(symlink("real","link")==0);
    const struct { const char *arg,*suffix; uint32_t line,col; ipc_result rc; } cases[]={
        {"file:12","/file",12,1,IPC_OK},{"file:12:3","/file",12,3,IPC_OK},
        {"a:1","/a:1",1,1,IPC_OK},{"a:b:12","/a:b",12,1,IPC_OK},
        {"file:0","",0,0,IPC_INVALID},{"file:4294967296","",0,0,IPC_INVALID},
        {"link/../new","/new",1,1,IPC_OK},{"link/missing","/real/missing",1,1,IPC_OK},
        {"x:abc","/x:abc",1,1,IPC_OK}
    };
    for(size_t i=0;i<sizeof cases/sizeof cases[0];i++) {
        char *av[]={"sublimite",(char *)cases[i].arg}; ipc_args a={0};
        ASSERT(ipc_parse_args(2,av,&a)==cases[i].rc);
        if(cases[i].rc==IPC_OK) { char want[IPC_PATH_CAP]; ASSERT(snprintf(want,sizeof want,"%s%s",dir,cases[i].suffix)>0);
            ASSERT(a.request.count==1 && strcmp(a.request.paths[0].path,want)==0);
            ASSERT(a.request.paths[0].line==cases[i].line && a.request.paths[0].col==cases[i].col); }
        ipc_args_fini(&a);
    }
    char *av[]={"sublimite","--wait","--new-instance","-","--","--odd"}; ipc_args a={0};
    ASSERT(ipc_parse_args(6,av,&a)==IPC_OK && a.request.wait && a.request.new_instance && a.request.has_stdin);
    ASSERT(a.request.count==1); int p[2]; ASSERT(pipe(p)==0); ASSERT(write(p[1],"a\0b\nc",5)==5); close(p[1]);
    ASSERT(ipc_args_read_stdin(&a,p[0])==IPC_OK && a.request.stdin_size==5); close(p[0]); ipc_args_fini(&a);
    char *bad[]={"sublimite","--bad"}; ASSERT(ipc_parse_args(2,bad,&a)==IPC_INVALID);
    char *dup[]={"sublimite","-","-"}; ASSERT(ipc_parse_args(3,dup,&a)==IPC_INVALID);
    ASSERT(chdir(cwd)==0);
}
static void wire_tests(void)
{
    uint8_t wire[8192]; size_t n=0; ipc_request r=request(true,true), out={0};
    ASSERT(ipc_wire_encode(&r,wire,sizeof wire,&n)==IPC_OK);
    ASSERT(ipc_wire_decode(wire,n,&out)==IPC_OK && out.wait && out.has_stdin && out.stdin_size==5);
    for(size_t i=0;i<n;i++) ASSERT(ipc_wire_decode(wire,i,&out)==IPC_PROTOCOL);
    wire[n]=0; ASSERT(ipc_wire_decode(wire,n+1,&out)==IPC_PROTOCOL);
    wire[0]^=1u; ASSERT(ipc_wire_decode(wire,n,&out)==IPC_PROTOCOL);
}
static void roundtrip(const char *dir, bool wait, bool input)
{
    ipc_server s={0}; ipc_result init_rc=ipc_server_init(&s,dir);
    if(init_rc!=IPC_OK) fprintf(stderr,"init rc=%d errno=%d (%s)\n",(int)init_rc,errno,strerror(errno));
    ASSERT(init_rc==IPC_OK);
    pid_t child=fork(); ASSERT(child>=0);
    if(child==0) { ipc_request r=request(wait,input); ipc_result rc=ipc_client_send(dir,&r,CLIENT_TIMEOUT_MS);
        if(rc!=IPC_OK) fprintf(stderr,"roundtrip: ipc_result=%d expected=%d\n",(int)rc,(int)IPC_OK);
        _exit(rc==IPC_OK?0:2); }
    capture c={0};
    for(int i=0;i<20 && c.calls==0;i++) { struct pollfd p={ipc_server_fd(&s),POLLIN,0}; ASSERT(poll(&p,1,CLIENT_TIMEOUT_MS)>0); ASSERT(ipc_server_drain(&s,opened,&c)==IPC_OK); }
    ASSERT(c.calls==1 && c.stdin_seen==input);
    if(wait) { int status=0; ASSERT(waitpid(child,&status,WNOHANG)==0); ASSERT(ipc_server_report_closed(&s,c.token)==IPC_OK); ASSERT(ipc_server_drain(&s,opened,&c)==IPC_OK); }
    int status=0; ASSERT(waitpid(child,&status,0)==child && WIFEXITED(status) && WEXITSTATUS(status)==0);
    ipc_server_fini(&s);
}

/* Address a filesystem socket through its pinned directory, as production
 * does: an absolute build/ test path can exceed sun_path (108 bytes (P))
 * in a deep checkout. Keep *dir_fd open until bind/connect returns. */
static void fs_addr(struct sockaddr_un *a, const char *dir, const char *leaf, int *dir_fd)
{
    *dir_fd=open(dir,O_PATH|O_DIRECTORY|O_CLOEXEC); ASSERT(*dir_fd>=0);
    memset(a,0,sizeof *a); a->sun_family=AF_UNIX;
    ASSERT(snprintf(a->sun_path,sizeof a->sun_path,"/proc/self/fd/%d/%s",*dir_fd,leaf)>0);
}
static int raw_connect(const char *dir)
{
    char leaf[64]; ASSERT(snprintf(leaf,sizeof leaf,EDIT_APP_ID "-%lu.sock",(unsigned long)getuid())>0);
    struct sockaddr_un a; int dfd; fs_addr(&a,dir,leaf,&dfd);
    int fd=socket(AF_UNIX,SOCK_STREAM,0); ASSERT(fd>=0);
    ASSERT(connect(fd,(struct sockaddr *)&a,sizeof a)==0); close(dfd); return fd;
}
static void drain_ready(ipc_server *s, capture *c)
{
    struct pollfd p={ipc_server_fd(s),POLLIN,0}; ASSERT(poll(&p,1,CLIENT_TIMEOUT_MS)>0);
    base_malloc_guard_begin();
    ASSERT(ipc_server_drain(s,opened,c)==IPC_OK);
    size_t allocations=base_malloc_guard_end();
    if(base_malloc_guard_active()) ASSERT(allocations==0);
}
static void fragmented(const char *dir)
{
    ipc_server s={0}; ASSERT(ipc_server_init(&s,dir)==IPC_OK); capture c={0};
    ipc_server other={0}; ASSERT(ipc_server_init(&other,dir)==IPC_EXISTS);
    int fd=raw_connect(dir); uint8_t bytes[8192], response[8]; size_t n=0; ipc_request r=request(false,true);
    ASSERT(ipc_wire_encode(&r,bytes,sizeof bytes,&n)==IPC_OK);
    ASSERT(write(fd,bytes,7)==7); drain_ready(&s,&c); ASSERT(c.calls==0);
    ASSERT(write(fd,bytes+7,17)==17); drain_ready(&s,&c); ASSERT(c.calls==0);
    ASSERT(write(fd,bytes+24,n-24)==(ssize_t)(n-24)); drain_ready(&s,&c); ASSERT(c.calls==1);
    ASSERT(read(fd,response,8)==8 && memcmp(response,"EDIRA",5)==0 && response[5]==IPC_OK); close(fd);
    ASSERT(ipc_server_report_closed(&s,c.token)==IPC_INVALID);
    fd=raw_connect(dir); bytes[0]^=1u; ASSERT(write(fd,bytes,n)==(ssize_t)n); drain_ready(&s,&c);
    ASSERT(read(fd,response,8)==8 && response[5]==IPC_PROTOCOL && c.calls==1); close(fd);
    fd=raw_connect(dir); ASSERT(write(fd,bytes,5)==5); close(fd); drain_ready(&s,&c); ASSERT(c.calls==1);
    ipc_server_fini(&s);
}

static void wait_tokens(const char *dir)
{
    ipc_server s={0}; ASSERT(ipc_server_init(&s,dir)==IPC_OK); capture c={0};
    int fds[2]; ipc_token tokens[2]; uint8_t bytes[8192], response[8]; size_t n=0;
    ipc_request r=request(true,false); ASSERT(ipc_wire_encode(&r,bytes,sizeof bytes,&n)==IPC_OK);
    for(size_t i=0;i<2;i++) {
        fds[i]=raw_connect(dir); ASSERT(write(fds[i],bytes,n)==(ssize_t)n);
        drain_ready(&s,&c); tokens[i]=c.token;
        ASSERT(read(fds[i],response,8)==8 && response[4]=='A' && response[5]==IPC_OK);
    }
    ASSERT(tokens[0]!=tokens[1] && c.calls==2);
    struct pollfd p={fds[0],POLLIN,0}; ASSERT(poll(&p,1,20)==0);
    ASSERT(ipc_server_report_closed(&s,tokens[1])==IPC_OK);
    ASSERT(read(fds[1],response,8)==8 && response[4]=='C' && response[5]==IPC_OK);
    ASSERT(poll(&p,1,20)==0); close(fds[1]);
    ASSERT(ipc_server_report_closed(&s,tokens[1])==IPC_INVALID);
    ASSERT(ipc_server_report_closed(&s,tokens[0])==IPC_OK);
    ASSERT(read(fds[0],response,8)==8 && response[4]=='C' && response[5]==IPC_OK); close(fds[0]);
    ipc_server_fini(&s);
}
static void endpoint_validation(const char *dir)
{
    char path[IPC_PATH_CAP]; ASSERT(snprintf(path,sizeof path,"%s/" EDIT_APP_ID "-%lu.sock",dir,(unsigned long)getuid())>0);
    int fd=open(path,O_CREAT|O_WRONLY,0600); ASSERT(fd>=0); close(fd);
    ipc_server s={0}; ASSERT(ipc_server_init(&s,dir)==IPC_INVALID); struct stat st; ASSERT(stat(path,&st)==0 && S_ISREG(st.st_mode)); ASSERT(unlink(path)==0);
    ASSERT(chmod(dir,0755)==0); ASSERT(ipc_server_init(&s,dir)==IPC_INVALID); ASSERT(chmod(dir,0700)==0);
    ASSERT(unsetenv("XDG_RUNTIME_DIR")==0); ASSERT(ipc_server_init(&s,NULL)==IPC_OK);
    const char *ns=getenv("EDIT_IPC_NAMESPACE"); ASSERT(ns!=NULL && ns[0]!='\0');
    char saved[108], alternate[108], name[108];
    ASSERT(snprintf(saved,sizeof saved,"%s",ns)>0);
    int n=snprintf(name,sizeof name,EDIT_APP_ID "-%lu-%s",(unsigned long)getuid(),saved);
    ASSERT(n>0 && (size_t)n<sizeof name);
    struct sockaddr_un bound={0}; socklen_t len=sizeof bound;
    ASSERT(getsockname(os_listener_fd(&s.listener),(struct sockaddr *)&bound,&len)==0);
    ASSERT(bound.sun_path[0]=='\0' && len==offsetof(struct sockaddr_un,sun_path)+1u+(size_t)n);
    ASSERT(memcmp(bound.sun_path+1,name,(size_t)n)==0);
    ipc_server other={0}; ASSERT(ipc_server_init(&other,NULL)==IPC_EXISTS);
    ASSERT(snprintf(alternate,sizeof alternate,"%s-other",saved)>0);
    ASSERT(setenv("EDIT_IPC_NAMESPACE",alternate,1)==0);
    roundtrip(NULL,false,true); /* Independent abstract server AND client. */
    char oversized[108]; memset(oversized,'x',sizeof oversized-1); oversized[sizeof oversized-1]='\0';
    ipc_request r=request(false,false);
    ASSERT(setenv("EDIT_IPC_NAMESPACE",oversized,1)==0);
    ASSERT(ipc_server_init(&other,NULL)==IPC_LIMIT && ipc_client_send(NULL,&r,CLIENT_TIMEOUT_MS)==IPC_LIMIT);
    ASSERT(ipc_server_init(&other,dir)==IPC_OK); ipc_server_fini(&other); /* Hook is abstract-only. */
    ASSERT(setenv("EDIT_IPC_NAMESPACE","",1)==0);
    ASSERT(ipc_server_init(&other,NULL)==IPC_INVALID && ipc_client_send(NULL,&r,CLIENT_TIMEOUT_MS)==IPC_INVALID);
    ASSERT(setenv("EDIT_IPC_NAMESPACE",saved,1)==0);
    ASSERT(ipc_server_init(&other,NULL)==IPC_EXISTS); ipc_server_fini(&s);
    ASSERT(setenv("XDG_RUNTIME_DIR",dir,1)==0);
}

typedef struct callback_state { ipc_server *server; size_t calls; } callback_state;
static ipc_result rejected(const ipc_request *r, ipc_token token, void *ctx)
{
    (void)r; (void)token; callback_state *state=ctx; state->calls++; return IPC_REJECTED;
}
static ipc_result close_now(const ipc_request *r, ipc_token token, void *ctx)
{
    callback_state *state=ctx; state->calls++;
    ASSERT(r->wait); ASSERT(ipc_server_report_closed(state->server,token)==IPC_OK); return IPC_OK;
}
static void callback_results(const char *dir, bool reject)
{
    ipc_server s={0}; ASSERT(ipc_server_init(&s,dir)==IPC_OK);
    callback_state state={&s,0};
    /* Accept a partial request before releasing the real client. A token is
     * assigned on accept, so it cannot prove the callback has sent an ACK. */
    int pending=raw_connect(dir), start[2]; ASSERT(pipe(start)==0);
    ASSERT(write(pending,"E",1)==1);
    pid_t child=fork(); ASSERT(child>=0);
    if(child==0) { close(pending); close(start[1]); char b;
        ASSERT(read(start[0],&b,1)==1); close(start[0]);
        ipc_request r=request(true,false); ipc_result rc=ipc_client_send(dir,&r,CLIENT_TIMEOUT_MS);
        ipc_result expected=reject?IPC_REJECTED:IPC_OK;
        if(rc!=expected) fprintf(stderr,"callback_results: reject=%d ipc_result=%d expected=%d (IPC_TIMEOUT=%d)\n",reject,(int)rc,(int)expected,(int)IPC_TIMEOUT);
        _exit(rc==expected?0:2); }
    close(start[0]);
    bool started=false;
    while(state.calls==0) {
        struct pollfd p={ipc_server_fd(&s),POLLIN,0}; ASSERT(poll(&p,1,CLIENT_TIMEOUT_MS)>0);
        ASSERT(ipc_server_drain(&s,reject?rejected:close_now,&state)==IPC_OK);
        if(!started) {
            ASSERT(s.next_token==1 && state.calls==0);
            ASSERT(write(start[1],"x",1)==1); close(start[1]); close(pending); started=true;
        }
    }
    ASSERT(state.calls==1);
    int status; ASSERT(waitpid(child,&status,0)==child && WIFEXITED(status) && WEXITSTATUS(status)==0);
    ipc_server_fini(&s);
}

static void stale(const char *dir)
{
    char leaf[64]; ASSERT(snprintf(leaf,sizeof leaf,EDIT_APP_ID "-%lu.sock",(unsigned long)getuid())>0);
    struct sockaddr_un a; int dfd; fs_addr(&a,dir,leaf,&dfd);
    int fd=socket(AF_UNIX,SOCK_STREAM,0); ASSERT(fd>=0); ASSERT(bind(fd,(struct sockaddr *)&a,sizeof a)==0); close(dfd); close(fd);
    ipc_server s={0}; ASSERT(ipc_server_init(&s,dir)==IPC_OK); ipc_server_fini(&s);
}
static void race(const char *dir)
{
    int start[2], result[2], finish[2]; ASSERT(pipe(start)==0 && pipe(result)==0 && pipe(finish)==0);
    pid_t children[2];
    for(size_t i=0;i<2;i++) { children[i]=fork(); ASSERT(children[i]>=0); if(children[i]==0) {
        close(start[1]); close(result[0]); close(finish[1]); char b; if(read(start[0],&b,1)!=1) _exit(3);
        ipc_server s={0}; ipc_result rc=ipc_server_init(&s,dir); b=rc==IPC_OK?'S':rc==IPC_EXISTS?'C':'E';
        if(write(result[1],&b,1)!=1) _exit(3);
        if(read(finish[0],&b,1)!=1) _exit(3);
        if(rc==IPC_OK) ipc_server_fini(&s);
        _exit(0);
    } }
    close(start[0]); close(result[1]); close(finish[0]); ASSERT(write(start[1],"xx",2)==2);
    char b[2]; ASSERT(read(result[0],b,1)==1 && read(result[0],b+1,1)==1);
    ASSERT((b[0]=='S' && b[1]=='C') || (b[0]=='C' && b[1]=='S')); ASSERT(write(finish[1],"xx",2)==2);
    for(size_t i=0;i<2;i++) { int status; ASSERT(waitpid(children[i],&status,0)==children[i] && WEXITSTATUS(status)==0); }
    close(start[1]); close(result[0]); close(finish[1]);
}

/* ---- edit-457.21 review-fix tests -------------------------------------- */
static void rm_rf(const char *path)
{
    DIR *d=opendir(path); ASSERT(d!=NULL);
    test_ipc_remove_contents(d); ASSERT(closedir(d)==0); ASSERT(rmdir(path)==0);
}
static void subdir(const char *dir, const char *name, char *out)
{
    ASSERT(snprintf(out,IPC_PATH_CAP,"%s/%s",dir,name)>0); ASSERT(mkdir(out,0700)==0);
}
static uint64_t now_ms(void)
{
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC,&ts);
    return (uint64_t)ts.tv_sec*1000u+(uint64_t)ts.tv_nsec/1000000u;
}
static void touch_file(const char *dir, const char *name, mode_t mode)
{
    char p[IPC_PATH_CAP]; ASSERT(snprintf(p,sizeof p,"%s/%s",dir,name)>0);
    int f=open(p,O_CREAT|O_WRONLY,mode); ASSERT(f>=0); close(f); ASSERT(chmod(p,mode)==0);
}
static void sock_path(const char *dir, char *out)
{
    ASSERT(snprintf(out,IPC_PATH_CAP,"%s/" EDIT_APP_ID "-%lu.sock",dir,(unsigned long)getuid())>0);
}
static void lock_path(const char *dir, char *out)
{
    ASSERT(snprintf(out,IPC_PATH_CAP,"%s/" EDIT_APP_ID "-%lu.lock",dir,(unsigned long)getuid())>0);
}
static ipc_result count_only(const ipc_request *r, ipc_token token, void *ctx)
{
    (void)r; capture *c=ctx; c->calls++; c->token=token; return IPC_OK;
}
/* One poll+drain step. Returns the poll result (0 on timeout). */
static int pump(ipc_server *s, ipc_open_callback cb, void *ctx, int timeout_ms)
{
    struct pollfd p={ipc_server_fd(s),POLLIN,0}; int n=poll(&p,1,timeout_ms);
    ASSERT(n>=0); if(n>0) ASSERT(ipc_server_drain(s,cb,ctx)==IPC_OK); return n;
}
static pid_t spawn_client(const char *runtime, bool wait, ipc_result expected)
{
    pid_t child=fork(); ASSERT(child>=0);
    if(child==0) { ipc_request r=request(wait,false); ipc_result rc=ipc_client_send(runtime,&r,CLIENT_TIMEOUT_MS);
        if(rc!=expected) fprintf(stderr,"client: ipc_result=%d expected=%d\n",(int)rc,(int)expected);
        _exit(rc==expected?0:2); }
    return child;
}
static void reap_ok(pid_t child)
{
    int status; ASSERT(waitpid(child,&status,0)==child && WIFEXITED(status) && WEXITSTATUS(status)==0);
}
static void t_result_text(void)
{
    ASSERT(strcmp(ipc_result_string(IPC_REJECTED), "running instance rejected the open request") == 0);
    ASSERT(strcmp(ipc_result_string(IPC_LIMIT), "request or editor capacity limit exceeded") == 0);
    ASSERT(strcmp(ipc_result_string(IPC_BUSY), "running instance is busy") == 0);
    for (ipc_result rc = IPC_OK; rc <= IPC_REJECTED; rc++) ASSERT(*ipc_result_string(rc));
    puts("ipc_test: descriptive IPC result text passed");
}
static void serve_one(ipc_server *s, capture *c)
{
    for(int i=0;i<40 && c->calls==0;i++) (void)pump(s,count_only,c,500);
    ASSERT(c->calls==1);
}

/* 1: a decoded request re-encodes byte-identically into other storage.
 * Input aliasing the output is a caller bug (A15). */
static void t_wire_alias(const char *dir)
{
    (void)dir;
    ipc_request src=request(true,true); uint8_t a[256], b[256]; size_t na=0, nb=0; ipc_request d={0};
    ASSERT(ipc_wire_encode(&src,a,sizeof a,&na)==IPC_OK && ipc_wire_decode(a,na,&d)==IPC_OK);
    ASSERT(ipc_wire_encode(&d,b,sizeof b,&nb)==IPC_OK && nb==na && memcmp(a,b,na)==0);
}

/* 17: table-driven wire validation with required results. */
static void t_wire_table(const char *dir)
{
    (void)dir;
    static uint8_t good[8192], mut[8192]; size_t n=0; ipc_request r=request(true,true), out;
    ASSERT(ipc_wire_encode(&r,good,sizeof good,&n)==IPC_OK);
    static const uint8_t expect[]={'E','D','I','P',1,3,0,0, 0,0,0,0, 1,0,0,0, 5,0,0,0, 5,0,0,0,
        '/','t','m','p',0, 13,0,0,0, 12,0,0,0, 3,0,0,0, '/','t','m','p','/','e','x','a','m','p','l','e',0, 'a',0,'b','\n','c'};
    ASSERT(n==sizeof expect); uint8_t exp2[sizeof expect]; memcpy(exp2,expect,n); exp2[8]=(uint8_t)n;
    ASSERT(memcmp(good,exp2,n)==0);
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
        ASSERT(rc==IPC_PROTOCOL);
        ASSERT(out.count==0 && out.cwd==NULL && out.stdin_data==NULL && out.stdin_size==0 && !out.wait && !out.has_stdin);
    }
    /* header-only frames: valid empty request, and truncated variants */
    uint8_t h[24+2]; memset(h,0,sizeof h); memcpy(h,"EDIP",4); h[4]=1; h[8]=26; h[16]=2; h[24]='/'; h[25]=0;
    memset(&out,0xFF,sizeof out); ASSERT(ipc_wire_decode(h,26,&out)==IPC_OK && out.count==0 && !out.wait);
    static uint8_t big[IPC_MAX_WIRE+1];
    ASSERT(ipc_wire_decode(big,sizeof big,&out)==IPC_PROTOCOL);
    /* maximum frame round trips exactly; one more byte is IPC_LIMIT */
    static uint8_t maxw[IPC_MAX_WIRE]; static uint8_t payload[IPC_MAX_WIRE];
    ipc_request m=request(false,true); m.stdin_data=payload; m.stdin_size=IPC_MAX_WIRE-54u;
    size_t mn=0; ASSERT(ipc_wire_encode(&m,maxw,sizeof maxw,&mn)==IPC_OK && mn==IPC_MAX_WIRE);
    ASSERT(ipc_wire_decode(maxw,mn,&out)==IPC_OK && out.stdin_size==IPC_MAX_WIRE-54u);
    m.stdin_size++; ASSERT(ipc_wire_encode(&m,maxw,sizeof maxw,&mn)==IPC_LIMIT && mn==0);
}

/* 3, 4: pathname semantics and literal-name precedence. */
static void t_parser_paths(const char *dir)
{
    char sub[IPC_PATH_CAP], saved[IPC_PATH_CAP]; subdir(dir,"parse",sub);
    ASSERT(getcwd(saved,sizeof saved)!=NULL); ASSERT(chdir(sub)==0);
    touch_file(sub,"leaf",0600); touch_file(sub,"victim",0600); touch_file(sub,"loop",0600);
    ASSERT(mkdir("sd",0700)==0); ASSERT(symlink("nowhere","dang:1")==0); ASSERT(symlink("loop:1","loop:1")==0);
    ASSERT(symlink("leaf","lnk")==0);
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
        ASSERT((rc==IPC_OK)==c[i].ok);
        if(rc==IPC_OK) { char want[IPC_PATH_CAP]; ASSERT(snprintf(want,sizeof want,"%s%s",sub,c[i].suffix)>0);
            ASSERT(strcmp(a.request.paths[0].path,want)==0 && a.request.paths[0].line==c[i].line); ipc_args_fini(&a); }
    }
    ASSERT(chdir(saved)==0); rm_rf(sub);
}

/* 2: lifecycle operations are pinned to the verified directory. */
static void t_runtime_swap(const char *dir)
{
    char rt[IPC_PATH_CAP], moved[IPC_PATH_CAP], sock[IPC_PATH_CAP], victim[IPC_PATH_CAP];
    subdir(dir,"rt",rt); ASSERT(snprintf(moved,sizeof moved,"%s/rt-moved",dir)>0);
    ASSERT(snprintf(victim,sizeof victim,"%s/victim",dir)>0);
    int vf=open(victim,O_CREAT|O_WRONLY,0644); ASSERT(vf>=0); close(vf); ASSERT(chmod(victim,0644)==0);
    ipc_server s={0}; ASSERT(ipc_server_init(&s,rt)==IPC_OK);
    struct stat st; sock_path(rt,sock); ASSERT(lstat(sock,&st)==0 && S_ISSOCK(st.st_mode) && (st.st_mode&0777)==0600);
    ASSERT(rename(rt,moved)==0); ASSERT(mkdir(rt,0700)==0);
    ASSERT(symlink(victim,sock)==0); /* decoy: a symlink sitting where the socket used to be */
    ipc_server_fini(&s);
    char moved_sock[IPC_PATH_CAP]; sock_path(moved,moved_sock);
    ASSERT(lstat(moved_sock,&st)!=0 && errno==ENOENT); /* removed through the pinned directory */
    ASSERT(lstat(sock,&st)==0 && S_ISLNK(st.st_mode)); /* decoy untouched */
    ASSERT(stat(victim,&st)==0 && (st.st_mode&0777)==0644);
    /* a symlink planted at the socket name is never chmod'ed or followed */
    ASSERT(unlink(sock)==0); ASSERT(symlink(victim,sock)==0);
    ASSERT(ipc_server_init(&s,rt)==IPC_INVALID); ASSERT(stat(victim,&st)==0 && (st.st_mode&0777)==0644);
    rm_rf(rt); rm_rf(moved); ASSERT(unlink(victim)==0);
}

/* 10: bounded UI work per drain call. */
static void t_drain_budget(const char *dir)
{
    char rt[IPC_PATH_CAP]; subdir(dir,"budget",rt);
    ipc_server s={0}; ASSERT(ipc_server_init(&s,rt)==IPC_OK); capture c={0};
    uint8_t frame[256]; size_t n=0; ipc_request r=request(false,false); ASSERT(ipc_wire_encode(&r,frame,sizeof frame,&n)==IPC_OK);
    int fds[24];
    for(size_t i=0;i<24;i++) { fds[i]=raw_connect(rt); ASSERT(write(fds[i],frame,n)==(ssize_t)n); }
    usleep(20000);
    ASSERT(ipc_server_drain(&s,count_only,&c)==IPC_OK);
    if(c.calls>IPC_DRAIN_CALLBACKS) fprintf(stderr,"drain ran %zu callbacks in one call (budget %u)\n",c.calls,IPC_DRAIN_CALLBACKS);
    ASSERT(c.calls>0 && c.calls<=IPC_DRAIN_CALLBACKS);
    for(int i=0;i<40 && c.calls<24;i++) (void)pump(&s,count_only,&c,200);
    ASSERT(c.calls==24);
    for(size_t i=0;i<24;i++) close(fds[i]);
    for(int i=0;i<10;i++) (void)pump(&s,count_only,&c,20);
    /* byte budget: 8 near-complete 60 KB frames */
    static uint8_t body[60000]; memset(body,'x',sizeof body);
    ipc_request big=request(false,true); big.stdin_data=body; big.stdin_size=sizeof body;
    static uint8_t wire[IPC_RX_SMALL_SIZE]; ASSERT(ipc_wire_encode(&big,wire,sizeof wire,&n)==IPC_OK);
    c.calls=0; int bf[8];
    for(size_t i=0;i<8;i++) { bf[i]=raw_connect(rt); ASSERT(write(bf[i],wire,n-1)==(ssize_t)(n-1)); }
    usleep(20000);
    usleep(20000);
    ASSERT(ipc_server_drain(&s,count_only,&c)==IPC_OK);
    if(s.drain_bytes==0 || s.drain_bytes>IPC_DRAIN_BYTES) fprintf(stderr,"drain_bytes=%llu budget=%u\n",(unsigned long long)s.drain_bytes,IPC_DRAIN_BYTES);
    ASSERT(s.drain_bytes>0 && s.drain_bytes<=IPC_DRAIN_BYTES);
    for(size_t i=0;i<8;i++) ASSERT(write(bf[i],wire+n-1,1)==1);
    for(int i=0;i<60 && c.calls<8;i++) { (void)pump(&s,count_only,&c,200); ASSERT(s.drain_bytes<=IPC_DRAIN_BYTES); }
    ASSERT(c.calls==8);
    for(size_t i=0;i<8;i++) close(bf[i]);
    ipc_server_fini(&s); rm_rf(rt);
}

/* 11: incomplete clients cannot hold every slot forever. */
static void silent_fill(ipc_server *s, const char *rt, int *fds)
{
    capture c={0};
    for(size_t i=0;i<IPC_MAX_CLIENTS;i++) fds[i]=raw_connect(rt);
    for(int i=0;i<20 && s->next_token<IPC_MAX_CLIENTS;i++) (void)pump(s,count_only,&c,50);
    ASSERT(s->next_token==IPC_MAX_CLIENTS);
}
static void t_slot_exhaust(const char *dir)
{
    char rt[IPC_PATH_CAP]; subdir(dir,"slots",rt); int fds[IPC_MAX_CLIENTS]; capture c={0};
    /* A: absolute pre-ACK deadline; the timer wakes the epoll fd by itself */
    ipc_server s={0}; ASSERT(ipc_server_init(&s,rt)==IPC_OK); s.request_deadline_ms=200; s.evict_idle_ms=600000;
    silent_fill(&s,rt,fds);
    int woke=pump(&s,count_only,&c,3000);
    if(woke==0) fprintf(stderr,"deadline timer never woke the loop\n");
    ASSERT(woke>0);
    for(size_t i=0;i<IPC_MAX_CLIENTS;i++) { /* later arrivals expire on their own timer ticks */
        char b; ssize_t got=recv(fds[i],&b,1,MSG_DONTWAIT);
        for(int k=0;k<20 && got!=0;k++) { (void)pump(&s,count_only,&c,300); got=recv(fds[i],&b,1,MSG_DONTWAIT); }
        ASSERT(got==0); close(fds[i]); }
    pid_t ch=spawn_client(rt,false,IPC_OK); serve_one(&s,&c); reap_ok(ch);
    ipc_server_fini(&s);
    /* B: pressure eviction of the longest-idle incomplete client */
    ASSERT(ipc_server_init(&s,rt)==IPC_OK); s.request_deadline_ms=600000; s.evict_idle_ms=100; c.calls=0;
    silent_fill(&s,rt,fds); usleep(150000);
    ch=spawn_client(rt,false,IPC_OK); serve_one(&s,&c); reap_ok(ch);
    for(size_t i=0;i<IPC_MAX_CLIENTS;i++) close(fds[i]);
    ipc_server_fini(&s); rm_rf(rt);
}

/* 12: lifecycle lock waits are bounded. */
static void t_lock_timeout(const char *dir)
{
    char rt[IPC_PATH_CAP], lp[IPC_PATH_CAP], sp[IPC_PATH_CAP]; subdir(dir,"lock",rt); lock_path(rt,lp); sock_path(rt,sp);
    ipc_server s={0}; ASSERT(ipc_server_init(&s,rt)==IPC_OK); ipc_server_fini(&s);
    alarm(15);
    int hold=open(lp,O_RDWR|O_CLOEXEC); ASSERT(hold>=0); ASSERT(flock(hold,LOCK_EX)==0);
    uint64_t t0=now_ms(); ipc_result rc=ipc_server_init(&s,rt); uint64_t dt=now_ms()-t0;
    if(rc!=IPC_TIMEOUT) fprintf(stderr,"init under held lock: rc=%d after %llu ms\n",(int)rc,(unsigned long long)dt);
    ASSERT(rc==IPC_TIMEOUT && dt<IPC_LOCK_TIMEOUT_MS+1500u);
    ASSERT(flock(hold,LOCK_UN)==0);
    ASSERT(ipc_server_init(&s,rt)==IPC_OK);
    ASSERT(flock(hold,LOCK_EX)==0);
    t0=now_ms(); ipc_server_fini(&s); dt=now_ms()-t0; ASSERT(dt<IPC_LOCK_TIMEOUT_MS+1500u);
    struct stat st; ASSERT(lstat(sp,&st)==0 && S_ISSOCK(st.st_mode)); /* ownership preserved, not unlinked blind */
    ASSERT(flock(hold,LOCK_UN)==0); close(hold);
    ASSERT(ipc_server_init(&s,rt)==IPC_OK); ipc_server_fini(&s); /* stale socket recovered */
    ASSERT(lstat(sp,&st)!=0);
    alarm(120); rm_rf(rt);
}

/* 13 + 17: foreign credentials, squatting, fallback endpoint. */
static void t_foreign_cred(const char *dir)
{
    char rt[IPC_PATH_CAP]; subdir(dir,"cred",rt); ASSERT(unsetenv("XDG_RUNTIME_DIR")==0);
    char ns[128]; ASSERT(snprintf(ns,sizeof ns,"%s-cred",getenv("EDIT_IPC_NAMESPACE"))>0);
    ASSERT(setenv("EDIT_IPC_NAMESPACE",ns,1)==0); ASSERT(setenv("EDIT_IPC_FALLBACK_DIR",rt,1)==0);
    ipc_request r=request(false,false); capture c={0};
    /* client refuses a server whose credentials are foreign */
    ipc_server s={0}; ASSERT(ipc_server_init(&s,NULL)==IPC_OK);
    ipc_seam_foreign_abstract=true;
    ipc_result rc=ipc_client_send(NULL,&r,1000);
    if(rc!=IPC_REJECTED) fprintf(stderr,"client foreign-server rc=%d\n",(int)rc);
    ASSERT(rc==IPC_REJECTED);
    /* server refuses a foreign client: connection dropped, callback never runs */
    pid_t ch=fork(); ASSERT(ch>=0);
    if(ch==0) { ipc_seam_foreign_abstract=false; ipc_result x=ipc_client_send(NULL,&r,5000); _exit(x==IPC_IO?0:2); }
    for(int i=0;i<10;i++) { int st; if(waitpid(ch,&st,WNOHANG)==ch) { ASSERT(WIFEXITED(st) && WEXITSTATUS(st)==0); ch=-1; break; } (void)pump(&s,count_only,&c,200); }
    ASSERT(ch==-1 && c.calls==0);
    ipc_server_fini(&s);
    /* another uid squats the abstract name: we fall back to the private path */
    struct sockaddr_un a={0}; a.sun_family=AF_UNIX;
    int n=snprintf(a.sun_path+1,sizeof a.sun_path-1,EDIT_APP_ID "-%lu-%s",(unsigned long)getuid(),ns); ASSERT(n>0);
    int squat=socket(AF_UNIX,SOCK_STREAM,0); ASSERT(squat>=0);
    ASSERT(bind(squat,(struct sockaddr *)&a,(socklen_t)(offsetof(struct sockaddr_un,sun_path)+1u+(size_t)n))==0 && listen(squat,4)==0);
    rc=ipc_server_init(&s,NULL);
    if(rc!=IPC_OK) fprintf(stderr,"init with squatted abstract name: rc=%d\n",(int)rc);
    ASSERT(rc==IPC_OK);
    char sp[IPC_PATH_CAP]; sock_path(rt,sp); struct stat st; ASSERT(lstat(sp,&st)==0 && S_ISSOCK(st.st_mode));
    ipc_server other={0}; ASSERT(ipc_server_init(&other,NULL)==IPC_EXISTS);
    ch=spawn_client(NULL,false,IPC_OK); serve_one(&s,&c); reap_ok(ch);
    ipc_server_fini(&s); close(squat);
    ipc_seam_foreign_abstract=false; ASSERT(unsetenv("EDIT_IPC_FALLBACK_DIR")==0);
    ASSERT(setenv("EDIT_IPC_NAMESPACE",ns+0,1)==0);
    char saved[128]; ASSERT(snprintf(saved,sizeof saved,"%s",ns)>0); saved[strlen(ns)-5]='\0';
    ASSERT(setenv("EDIT_IPC_NAMESPACE",saved,1)==0); ASSERT(setenv("XDG_RUNTIME_DIR",dir,1)==0);
    rm_rf(rt);
}

/* 14: receive storage is a small shared pool, released after the callback. */
static void t_memory(const char *dir)
{
    char rt[IPC_PATH_CAP]; subdir(dir,"mem",rt);
    ipc_server s={0}; ASSERT(ipc_server_init(&s,rt)==IPC_OK); capture c={0};
    if(s.arena.size>4u*1024u*1024u) fprintf(stderr,"ipc arena is %zu bytes\n",s.arena.size);
    ASSERT(s.arena.size<=4u*1024u*1024u);
    uint8_t hdr[24]; memset(hdr,0,sizeof hdr); memcpy(hdr,"EDIP",4); hdr[4]=1; hdr[10]=0x10; /* total = 1 MiB */
    int big[IPC_RX_BIG_COUNT+1];
    for(size_t i=0;i<=IPC_RX_BIG_COUNT;i++) { big[i]=raw_connect(rt); ASSERT(write(big[i],hdr,sizeof hdr)==(ssize_t)sizeof hdr); }
    for(int i=0;i<12;i++) (void)pump(&s,count_only,&c,50);
    uint8_t reply[8]; ASSERT(recv(big[IPC_RX_BIG_COUNT],reply,8,0)==8 && reply[4]=='A' && reply[5]==IPC_BUSY); /* bounded admission */
    for(size_t i=0;i<IPC_RX_BIG_COUNT;i++) ASSERT(recv(big[i],reply,8,MSG_DONTWAIT)<0 && errno==EAGAIN);
    pid_t ch=spawn_client(rt,false,IPC_OK); serve_one(&s,&c); reap_ok(ch); /* small work still flows */
    for(size_t i=0;i<=IPC_RX_BIG_COUNT;i++) close(big[i]);
    for(int i=0;i<10;i++) (void)pump(&s,count_only,&c,20);
    c.calls=0;
    for(int k=0;k<60;k++) { ch=spawn_client(rt,false,IPC_OK); size_t want=c.calls+1; for(int i=0;i<40 && c.calls<want;i++) (void)pump(&s,count_only,&c,500); ASSERT(c.calls==want); reap_ok(ch); }
    ipc_server_fini(&s); rm_rf(rt);
}

/* 15: wait associations are retired when the client disconnects. */
static void t_wait_disconnect(const char *dir)
{
    char rt[IPC_PATH_CAP]; subdir(dir,"waitdrop",rt);
    ipc_server s={0}; ASSERT(ipc_server_init(&s,rt)==IPC_OK); capture c={0};
    uint8_t bytes[256], reply[8]; size_t n=0; ipc_request r=request(true,false); ASSERT(ipc_wire_encode(&r,bytes,sizeof bytes,&n)==IPC_OK);
    for(int k=0;k<100;k++) {
        int fd=raw_connect(rt); ASSERT(write(fd,bytes,n)==(ssize_t)n);
        size_t want=c.calls+1; for(int i=0;i<40 && c.calls<want;i++) (void)pump(&s,count_only,&c,200);
        ASSERT(c.calls==want); ASSERT(recv(fd,reply,8,0)==8 && reply[4]=='A');
        ipc_token t=c.token; ASSERT(ipc_server_token_live(&s,t));
        close(fd);
        for(int i=0;i<40 && ipc_server_token_live(&s,t);i++) (void)pump(&s,count_only,&c,50);
        if(ipc_server_token_live(&s,t)) fprintf(stderr,"token %llu still live after disconnect (cycle %d)\n",(unsigned long long)t,k);
        ASSERT(!ipc_server_token_live(&s,t));
        ASSERT(s.wait_drops==(uint64_t)k+1u);
        ASSERT(ipc_server_report_closed(&s,t)==IPC_INVALID);
    }
    ASSERT(!ipc_server_token_live(&s,12345678));
    /* a wait client that got its closed reply is retired without counting as a drop */
    int fd=raw_connect(rt); ASSERT(write(fd,bytes,n)==(ssize_t)n); size_t want=c.calls+1;
    for(int i=0;i<40 && c.calls<want;i++) (void)pump(&s,count_only,&c,200);
    ASSERT(ipc_server_report_closed(&s,c.token)==IPC_OK); ASSERT(recv(fd,reply,8,0)==8); ASSERT(recv(fd,reply,8,0)==8 && reply[4]=='C'); close(fd);
    for(int i=0;i<5;i++) (void)pump(&s,count_only,&c,20);
    ASSERT(s.wait_drops==100u);
    ipc_server_fini(&s); rm_rf(rt);
}

/* 16: --wait for primary and isolated launches. */
static pid_t launcher_child(int fds[2], ipc_result expected)
{
    ASSERT(ipc_launcher_pair(fds)==IPC_OK);
    pid_t ch=fork(); ASSERT(ch>=0);
    if(ch==0) { close(fds[0]); ipc_result x=ipc_launcher_wait(fds[1]);
        if(x!=expected) fprintf(stderr,"launcher: rc=%d expected=%d\n",(int)x,(int)expected);
        _exit(x==expected?0:2); }
    close(fds[1]); return ch;
}
static void t_launcher(const char *dir)
{
    char rt[IPC_PATH_CAP]; subdir(dir,"launch",rt);
    for(int isolated=0;isolated<2;isolated++) {
        ipc_server s={0}; ipc_result rc=isolated?ipc_server_init_isolated(&s):ipc_server_init(&s,rt); ASSERT(rc==IPC_OK);
        if(isolated) { char sp[IPC_PATH_CAP]; struct stat st; sock_path(rt,sp); ASSERT(os_listener_fd(&s.listener)<0 && !s.owns_path && lstat(sp,&st)!=0); }
        int fds[2]; pid_t ch=launcher_child(fds,IPC_OK); ipc_token t=0;
        ASSERT(ipc_server_adopt_wait(&s,fds[0],&t)==IPC_OK && t!=0 && ipc_server_token_live(&s,t));
        usleep(50000); int st; ASSERT(waitpid(ch,&st,WNOHANG)==0); /* launcher blocks until completion */
        ASSERT(ipc_server_report_closed(&s,t)==IPC_OK); reap_ok(ch);
        capture c={0}; for(int i=0;i<5;i++) (void)pump(&s,count_only,&c,20);
        ASSERT(!ipc_server_token_live(&s,t) && s.wait_drops==0);
        /* the UI process goes away first: the launcher must fail, not report success */
        ch=launcher_child(fds,IPC_IO); ASSERT(ipc_server_adopt_wait(&s,fds[0],&t)==IPC_OK);
        ipc_server_fini(&s); reap_ok(ch);
    }
    rm_rf(rt);
}
/* Display routing is exact for filesystem and abstract endpoints. */
static void t_display(const char *dir)
{
    for (unsigned abstract = 0; abstract < 2; abstract++) {
        if (abstract) ASSERT(unsetenv("XDG_RUNTIME_DIR") == 0);
        const char *runtime = abstract ? NULL : dir;
        ipc_server first = {0}, second = {0}, duplicate = {0};
        ASSERT(ipc_server_init_display(&first, runtime, ":99") == IPC_OK);
        ASSERT(ipc_server_init_display(&duplicate, runtime, ":99") == IPC_EXISTS);
        ASSERT(ipc_server_init_display(&second, runtime, ":98") == IPC_OK);
        capture seen = {0}, wrong = {0};
        pid_t child = fork(); ASSERT(child >= 0);
        if (!child) {
            ipc_request r = request(false, false);
            _exit(ipc_client_send_display(runtime, ":99", &r, CLIENT_TIMEOUT_MS) == IPC_OK ? 0 : 2);
        }
        serve_one(&first, &seen); reap_ok(child);
        ASSERT(ipc_server_drain(&second, count_only, &wrong) == IPC_OK && wrong.calls == 0);
        ASSERT(ipc_server_init_display(&duplicate, runtime, "") == IPC_INVALID);
        char long_display[128]; memset(long_display, 'x', sizeof long_display - 1u);
        long_display[sizeof long_display - 1u] = '\0';
        ASSERT(ipc_server_init_display(&duplicate, runtime, long_display) == IPC_LIMIT);
        ipc_request r = request(false, false);
        ASSERT(ipc_client_send_display(runtime, long_display, &r, CLIENT_TIMEOUT_MS) == IPC_LIMIT);
        ipc_server_fini(&first); ipc_server_fini(&second);
        ASSERT(setenv("XDG_RUNTIME_DIR", dir, 1) == 0);
    }
    puts("ipc_test: exact display routing, same-display handoff, distinct primaries and bounds passed");
}
/* ---- edit-b6ai.8: activation fields (XDG_ACTIVATION_TOKEN, DESKTOP_STARTUP_ID) ---- */
#define ACT_TOKEN "5f0a3c-token_X.1"
#define ACT_STARTUP "host;4242;sublimite;_TIME987654"
/* The decoder of every build before this bead, as a model: header rules only.
 * It must accept a request without activation and reject one with it, which
 * is why the client retries without the fields on IPC_PROTOCOL. */
static bool legacy_accepts(const uint8_t *wire, size_t size)
{
    return size>=24 && memcmp(wire,"EDIP",4)==0 && wire[4]==1 && wire[5]<=3 && wire[6]==0 && wire[7]==0;
}
static ipc_result act_seen(const ipc_request *r, ipc_token token, void *ctx)
{
    (void)token; ipc_request *out=ctx;
    ASSERT(r->count==1 && r->activation_token!=NULL && r->startup_id!=NULL);
    ASSERT(strcmp(r->activation_token,ACT_TOKEN)==0 && strcmp(r->startup_id,ACT_STARTUP)==0);
    out->count++; return IPC_OK;
}
/* A server of the previous release on the real socket name: header rules of
 * legacy_accepts, replies like ipc_posix. Reports "verdict flagbyte" per
 * connection through fd. */
static void legacy_server(int listener, int report)
{
    for(int i=0;i<2;i++) {
        int fd=accept(listener,NULL,NULL); if(fd<0) _exit(3);
        uint8_t head[24]; size_t got=0;
        while(got<24) { ssize_t k=read(fd,head+got,24-got); if(k<=0) _exit(4); got+=(size_t)k; }
        size_t total=(size_t)head[8]|(size_t)head[9]<<8|(size_t)head[10]<<16|(size_t)head[11]<<24;
        uint8_t rest[4096]; if(total<24 || total-24>sizeof rest) _exit(5);
        got=0; while(got<total-24) { ssize_t k=read(fd,rest+got,total-24-got); if(k<=0) _exit(6); got+=(size_t)k; }
        bool ok=legacy_accepts(head,total);
        uint8_t verdict[2]={ok?0:1,head[5]}; if(write(report,verdict,2)!=2) _exit(7);
        uint8_t reply[8]={'E','D','I','R','A',ok?(uint8_t)IPC_OK:(uint8_t)IPC_PROTOCOL,0,0};
        if(write(fd,reply,8)!=8) _exit(8);
        close(fd);
    }
    _exit(0);
}
static void t_activation(const char *dir)
{
    uint8_t wire[8192]; size_t n=0; ipc_request r=request(false,true), out={0};
    /* Absent fields: byte-identical to the previous format. */
    ASSERT(ipc_wire_encode(&r,wire,sizeof wire,&n)==IPC_OK && legacy_accepts(wire,n) && !(wire[5]&4u));
    ASSERT(ipc_wire_decode(wire,n,&out)==IPC_OK && out.activation_token==NULL && out.startup_id==NULL);
    /* Both, token only, startup id only: exact round trip. */
    const struct { const char *token,*startup; } cases[]={{ACT_TOKEN,ACT_STARTUP},{ACT_TOKEN,NULL},{NULL,ACT_STARTUP},{"",""}};
    for(size_t i=0;i<sizeof cases/sizeof cases[0];i++) {
        r.activation_token=cases[i].token; r.startup_id=cases[i].startup;
        bool any=(cases[i].token&&cases[i].token[0])||(cases[i].startup&&cases[i].startup[0]);
        ASSERT(ipc_wire_encode(&r,wire,sizeof wire,&n)==IPC_OK);
        ASSERT(((wire[5]&4u)!=0)==any && legacy_accepts(wire,n)==!any);
        ipc_request d={0}; ASSERT(ipc_wire_decode(wire,n,&d)==IPC_OK);
        ASSERT(d.has_stdin && d.stdin_size==5 && memcmp(d.stdin_data,"a\0b\nc",5)==0 && d.count==1);
        ASSERT(any ? (cases[i].token&&cases[i].token[0] ? d.activation_token&&strcmp(d.activation_token,cases[i].token)==0 : d.activation_token==NULL) : d.activation_token==NULL);
        ASSERT(any ? (cases[i].startup&&cases[i].startup[0] ? d.startup_id&&strcmp(d.startup_id,cases[i].startup)==0 : d.startup_id==NULL) : d.startup_id==NULL);
        for(size_t k=0;k<n;k++) ASSERT(ipc_wire_decode(wire,k,&d)==IPC_PROTOCOL); /* every truncation */
    }
    /* The decoder refuses malformed blocks. Unusable strings never reach the
     * encoder: ipc_args_set_activation drops them (below). */
    char big[IPC_ACTIVATION_CAP+8]; memset(big,'a',sizeof big); big[sizeof big-1]=0;
    r.activation_token=ACT_TOKEN; r.startup_id=ACT_STARTUP; ASSERT(ipc_wire_encode(&r,wire,sizeof wire,&n)==IPC_OK);
    size_t blk=(size_t)wire[6]|(size_t)wire[7]<<8; ASSERT(blk==4u+sizeof ACT_TOKEN+sizeof ACT_STARTUP && blk<n);
    uint8_t m[8192]; ipc_request d;
    memcpy(m,wire,n); m[5]&=(uint8_t)~4u; ASSERT(ipc_wire_decode(m,n,&d)==IPC_PROTOCOL);   /* block size without the flag */
    memcpy(m,wire,n); m[6]=m[7]=0; ASSERT(ipc_wire_decode(m,n,&d)==IPC_PROTOCOL);           /* flag without a block */
    memcpy(m,wire,n); m[6]=3; m[7]=0; ASSERT(ipc_wire_decode(m,n,&d)==IPC_PROTOCOL);        /* block shorter than its header */
    memcpy(m,wire,n); m[7]=0xff; ASSERT(ipc_wire_decode(m,n,&d)==IPC_PROTOCOL);             /* block larger than the request */
    memcpy(m,wire,n); m[5]|=8u; ASSERT(ipc_wire_decode(m,n,&d)==IPC_PROTOCOL);              /* unknown flag */
    memcpy(m,wire,n); m[n-blk]++; ASSERT(ipc_wire_decode(m,n,&d)==IPC_PROTOCOL);            /* token length disagrees with block */
    memcpy(m,wire,n); m[n-1]='x'; ASSERT(ipc_wire_decode(m,n,&d)==IPC_PROTOCOL);            /* no trailing NUL */
    memcpy(m,wire,n); m[n-blk+4+2]=0; ASSERT(ipc_wire_decode(m,n,&d)==IPC_PROTOCOL);        /* NUL inside the token */
    memcpy(m,wire,n); m[n-blk+4]=' '; ASSERT(ipc_wire_decode(m,n,&d)==IPC_PROTOCOL);        /* space inside the token */
    /* Launcher helper: copies, drops unusable values instead of failing the handoff. */
    char *av[]={"sublimite","/tmp/x"}; ipc_args a={0}; ASSERT(ipc_parse_args(2,av,&a)==IPC_OK);
    ASSERT(a.request.activation_token==NULL && a.request.startup_id==NULL);
    char token[]=ACT_TOKEN, startup[]=ACT_STARTUP;
    ASSERT(ipc_args_set_activation(&a,token,startup)==IPC_OK);
    token[0]='Z'; startup[0]='Z'; /* the request owns copies */
    ASSERT(strcmp(a.request.activation_token,ACT_TOKEN)==0 && strcmp(a.request.startup_id,ACT_STARTUP)==0);
    ASSERT(ipc_args_set_activation(&a,"bad token",big)==IPC_OK && a.request.activation_token==NULL && a.request.startup_id==NULL);
    ASSERT(ipc_args_set_activation(&a,NULL,"")==IPC_OK && a.request.activation_token==NULL && a.request.startup_id==NULL);
    ipc_args_fini(&a);
    /* New launcher, new server: the callback sees both fields. */
    {
        ipc_server s={0}; ASSERT(ipc_server_init(&s,dir)==IPC_OK);
        pid_t child=fork(); ASSERT(child>=0);
        if(child==0) { ipc_request q=request(false,false); q.activation_token=ACT_TOKEN; q.startup_id=ACT_STARTUP;
            _exit(ipc_client_send(dir,&q,CLIENT_TIMEOUT_MS)==IPC_OK?0:2); }
        ipc_request seen={0};
        for(int i=0;i<40 && seen.count==0;i++) (void)pump(&s,act_seen,&seen,500);
        ASSERT(seen.count==1); reap_ok(child); ipc_server_fini(&s);
    }
    /* New launcher, previous-release server: rejected once, then resent
     * without the fields and accepted. Old launcher, new server is the
     * no-flag format above. */
    {
        char legacy[IPC_PATH_CAP]; subdir(dir,"legacy",legacy);
        char path[IPC_PATH_CAP]; sock_path(legacy,path);
        int listener=socket(AF_UNIX,SOCK_STREAM|SOCK_CLOEXEC,0); ASSERT(listener>=0);
        char leaf[64]; ASSERT(snprintf(leaf,sizeof leaf,EDIT_APP_ID "-%lu.sock",(unsigned long)getuid())>0);
        struct sockaddr_un addr; int dfd; fs_addr(&addr,legacy,leaf,&dfd);
        ASSERT(bind(listener,(struct sockaddr *)&addr,sizeof addr)==0 && listen(listener,4)==0); close(dfd);
        int rp[2]; ASSERT(pipe(rp)==0);
        pid_t server=fork(); ASSERT(server>=0);
        if(server==0) { close(rp[0]); legacy_server(listener,rp[1]); }
        close(listener); close(rp[1]);
        ipc_request q=request(false,false); q.activation_token=ACT_TOKEN; q.startup_id=ACT_STARTUP;
        ASSERT(ipc_client_send(legacy,&q,CLIENT_TIMEOUT_MS)==IPC_OK);
        uint8_t v[4]; ASSERT(read(rp[0],v,4)==4);
        ASSERT(v[0]==1 && (v[1]&4u)); /* first attempt carried activation and was refused */
        ASSERT(v[2]==0 && !(v[3]&4u)); /* the resend is the previous format and was accepted */
        close(rp[0]); int st; ASSERT(waitpid(server,&st,0)==server && WIFEXITED(st) && WEXITSTATUS(st)==0);
        ASSERT(unlink(path)==0); ASSERT(rmdir(legacy)==0);
    }
}

/* v2 namespace: endpoint names carry EDIT_APP_ID, and a live socket bound
 * at the v1 leaf/name in the same scope is never probed, adopted or removed.
 * The decoys are this test's own listeners; nothing real is contacted. */
static void t_namespace(const char *dir)
{
    char rt[IPC_PATH_CAP]; subdir(dir,"namespace",rt);
    char decoy[IPC_PATH_CAP];
    ASSERT(snprintf(decoy,sizeof decoy,"%s/sublimite-%lu.sock",rt,(unsigned long)getuid())>0);
    char leaf[64]; ASSERT(snprintf(leaf,sizeof leaf,"sublimite-%lu.sock",(unsigned long)getuid())>0);
    struct sockaddr_un a; int dir_fd; fs_addr(&a,rt,leaf,&dir_fd);
    int dfd=socket(AF_UNIX,SOCK_STREAM,0); ASSERT(dfd>=0);
    ASSERT(bind(dfd,(struct sockaddr *)&a,sizeof a)==0 && listen(dfd,1)==0); close(dir_fd);
    struct stat before; ASSERT(lstat(decoy,&before)==0);
    ipc_server s={0}; ipc_result rc=ipc_server_init(&s,rt);
    if(rc!=IPC_OK) fprintf(stderr,"namespace: init rc=%d beside v1 decoy leaf\n",(int)rc);
    ASSERT(rc==IPC_OK);
    char want[128]; ASSERT(snprintf(want,sizeof want,EDIT_APP_ID "-%lu.sock",(unsigned long)getuid())>0);
    if(strcmp(s.socket_path,want)!=0) fprintf(stderr,"namespace: leaf %s, want %s\n",s.socket_path,want);
    ASSERT(strcmp(s.socket_path,want)==0);
    char lp[IPC_PATH_CAP]; ASSERT(snprintf(lp,sizeof lp,"%s/" EDIT_APP_ID "-%lu.lock",rt,(unsigned long)getuid())>0);
    struct stat st; ASSERT(lstat(lp,&st)==0 && S_ISREG(st.st_mode));
    ipc_server_fini(&s);
    ASSERT(lstat(decoy,&st)==0 && st.st_ino==before.st_ino && S_ISSOCK(st.st_mode));
    close(dfd); ASSERT(unlink(decoy)==0);
    /* Abstract: a decoy at the v1-style namespaced name receives nothing. */
    ASSERT(unsetenv("XDG_RUNTIME_DIR")==0);
    const char *ns=getenv("EDIT_IPC_NAMESPACE"); ASSERT(ns!=NULL && ns[0]!=0);
    struct sockaddr_un va={0}; va.sun_family=AF_UNIX;
    int vn=snprintf(va.sun_path+1,sizeof va.sun_path-1,"sublimite-%lu-%s",(unsigned long)getuid(),ns);
    ASSERT(vn>0 && (size_t)vn<sizeof va.sun_path-1);
    int vfd=socket(AF_UNIX,SOCK_STREAM|SOCK_NONBLOCK,0); ASSERT(vfd>=0);
    ASSERT(bind(vfd,(struct sockaddr *)&va,(socklen_t)(offsetof(struct sockaddr_un,sun_path)+1u+(size_t)vn))==0 && listen(vfd,1)==0);
    ASSERT(ipc_server_init(&s,NULL)==IPC_OK);
    struct sockaddr_un bound={0}; socklen_t bl=sizeof bound;
    ASSERT(getsockname(os_listener_fd(&s.listener),(struct sockaddr *)&bound,&bl)==0);
    char awant[128]; int an=snprintf(awant,sizeof awant,EDIT_APP_ID "-%lu-%s",(unsigned long)getuid(),ns);
    ASSERT(an>0 && bound.sun_path[0]==0 && memcmp(bound.sun_path+1,awant,(size_t)an)==0);
    capture c={0}; pid_t ch=spawn_client(NULL,false,IPC_OK); serve_one(&s,&c); reap_ok(ch);
    int acc=accept(vfd,NULL,NULL); ASSERT(acc<0 && (errno==EAGAIN || errno==EWOULDBLOCK));
    ipc_server_fini(&s); close(vfd);
    ASSERT(setenv("XDG_RUNTIME_DIR",dir,1)==0);
    rm_rf(rt);
    puts("ipc_test: endpoint names carry the v2 app id; v1 decoys untouched");
}

/* An encoder request comes from ipc_parse_args or ipc_wire_decode, which
 * produce only absolute paths: a relative cwd is a caller bug (A15). */
static void encode_relative_cwd(void)
{
    uint8_t wire[64]; size_t n=0; ipc_request r={0}; r.cwd="relative";
    (void)ipc_wire_encode(&r,wire,sizeof wire,&n);
}
static void t_precondition_death(void)
{
    ASSERT(assert_death(encode_relative_cwd,"src/ipc/wire.c",
                        "cwdn <= IPC_PATH_CAP && string_ok((const uint8_t *)r->cwd, cwdn)"));
    puts("ipc_test: a forged request dies by ASSERT");
}

static int run_suite(const char *only, int ns_fd)
{
    alarm(120); const char *dir=test_ipc_isolate(); ASSERT(dir!=NULL);
    /* mkdtemp makes this namespace unique across processes and worktrees.
     * Forked clients must inherit it rather than choose their own endpoint. */
    const char *ns=getenv("EDIT_IPC_NAMESPACE"); ASSERT(ns!=NULL && ns[0]!=0);
    if(ns_fd>=0) { size_t l=strlen(ns); ASSERT(write(ns_fd,ns,l+1u)==(ssize_t)(l+1u)); close(ns_fd); } /* --parallel: report the namespace */
#define T(name,call) do { if(only==NULL || strcmp(only,name)==0) { call; } } while(0)
    T("death",t_precondition_death()); T("parser",parser(dir)); T("wire",wire_tests()); T("roundtrip",roundtrip(dir,false,false)); T("roundtrip",roundtrip(dir,true,false)); T("roundtrip",roundtrip(dir,false,true));
    T("result_text",t_result_text());
    T("stale",stale(dir)); T("race",race(dir)); T("fragmented",fragmented(dir)); T("tokens",wait_tokens(dir)); T("callbacks",callback_results(dir,true)); T("callbacks",callback_results(dir,false)); T("endpoints",endpoint_validation(dir));
    T("wire_alias",t_wire_alias(dir)); T("wire_table",t_wire_table(dir)); T("parser_paths",t_parser_paths(dir)); T("runtime_swap",t_runtime_swap(dir));
    T("drain_budget",t_drain_budget(dir)); T("slot_exhaust",t_slot_exhaust(dir)); T("lock_timeout",t_lock_timeout(dir)); T("foreign_cred",t_foreign_cred(dir));
    T("display",t_display(dir)); T("memory",t_memory(dir)); T("wait_disconnect",t_wait_disconnect(dir)); T("launcher",t_launcher(dir)); T("activation",t_activation(dir)); T("namespace",t_namespace(dir));
#undef T
    if(only!=NULL) { rm_rf(dir); return 0; }
    char p[IPC_PATH_CAP]; const char *names[]={"a:1","link","real"};
    for(size_t i=0;i<3;i++) { ASSERT(snprintf(p,sizeof p,"%s/%s",dir,names[i])>0); if(i==2) ASSERT(rmdir(p)==0); else ASSERT(unlink(p)==0); }
    const char *display_names[] = {"-d3a3939", "-d3a3938", ""};
    for (size_t i=0;i<3;i++) {
        ASSERT(snprintf(p,sizeof p,"%s/" EDIT_APP_ID "-%lu%s.lock",dir,(unsigned long)getuid(),display_names[i])>0);
        ASSERT(unlink(p)==0);
    }
    ASSERT(rmdir(dir)==0);
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
            int p[2]; ASSERT(pipe(p)==0); children[i]=fork(); ASSERT(children[i]>=0);
            if(children[i]==0) { close(p[0]); exit(run_suite(NULL,p[1])); }
            close(p[1]); rd[i]=p[0];
        }
        for(size_t i=0;i<n;i++) {
            memset(ns[i],0,sizeof ns[i]); size_t got=0; ssize_t k;
            while(got<sizeof ns[i]-1u && (k=read(rd[i],ns[i]+got,1))==1) { got++; if(ns[i][got-1]==0) break; }
            close(rd[i]); ASSERT(got>1u);
            for(size_t j=0;j<i;j++) ASSERT(strcmp(ns[i],ns[j])!=0);
            int status; ASSERT(waitpid(children[i],&status,0)==children[i]);
            if(!WIFEXITED(status) || WEXITSTATUS(status)!=0) failures++;
        }
    }
    printf("ipc_test parallel: %zu rounds x %zu processes = %zu runs, distinct namespaces, %zu failures\n",rounds,n,rounds*n,failures);
    return failures==0?0:1;
}
int main(int argc, char **argv)
{
    /* Line-buffered: forked clients must not duplicate buffered parent output. */
    setvbuf(stdout,NULL,_IOLBF,0);
    if(argc==1) return run_suite(NULL,-1);
    if(strcmp(argv[1],"--parallel")!=0) return run_suite(argv[1],-1); /* run one named test */
    size_t n=argc>2?(size_t)strtoul(argv[2],NULL,10):4u, rounds=argc>3?(size_t)strtoul(argv[3],NULL,10):1u;
    ASSERT(n>=2u && n<=PAR_MAX && rounds>=1u);
    return parallel(n,rounds);
}
