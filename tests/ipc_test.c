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
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FAIL %s:%d: %s\n",__FILE__,__LINE__,#x); exit(1); } } while (0)
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
        char *av[]={"edit",(char *)cases[i].arg}; ipc_args a={0};
        CHECK(ipc_parse_args(2,av,&a)==cases[i].rc);
        if(cases[i].rc==IPC_OK) { char want[IPC_PATH_CAP]; CHECK(snprintf(want,sizeof want,"%s%s",dir,cases[i].suffix)>0);
            CHECK(a.request.count==1 && strcmp(a.request.paths[0].path,want)==0);
            CHECK(a.request.paths[0].line==cases[i].line && a.request.paths[0].col==cases[i].col); }
        ipc_args_fini(&a);
    }
    char *av[]={"edit","--wait","--new-instance","-","--","--odd"}; ipc_args a={0};
    CHECK(ipc_parse_args(6,av,&a)==IPC_OK && a.request.wait && a.request.new_instance && a.request.has_stdin);
    CHECK(a.request.count==1); int p[2]; CHECK(pipe(p)==0); CHECK(write(p[1],"a\0b\nc",5)==5); close(p[1]);
    CHECK(ipc_args_read_stdin(&a,p[0])==IPC_OK && a.request.stdin_size==5); close(p[0]); ipc_args_fini(&a);
    char *bad[]={"edit","--bad"}; CHECK(ipc_parse_args(2,bad,&a)==IPC_INVALID);
    char *dup[]={"edit","-","-"}; CHECK(ipc_parse_args(3,dup,&a)==IPC_INVALID);
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
    if(child==0) { ipc_request r=request(wait,input); _exit(ipc_client_send(dir,&r,2000)==IPC_OK?0:2); }
    capture c={0};
    for(int i=0;i<20 && c.calls==0;i++) { struct pollfd p={ipc_server_fd(&s),POLLIN,0}; CHECK(poll(&p,1,1000)>0); CHECK(ipc_server_drain(&s,opened,&c)==IPC_OK); }
    CHECK(c.calls==1 && c.stdin_seen==input);
    if(wait) { int status=0; CHECK(waitpid(child,&status,WNOHANG)==0); CHECK(ipc_server_report_closed(&s,c.token)==IPC_OK); CHECK(ipc_server_drain(&s,opened,&c)==IPC_OK); }
    int status=0; CHECK(waitpid(child,&status,0)==child && WIFEXITED(status) && WEXITSTATUS(status)==0);
    ipc_server_fini(&s);
}

static int raw_connect(const char *dir)
{
    struct sockaddr_un a={0}; a.sun_family=AF_UNIX;
    CHECK(snprintf(a.sun_path,sizeof a.sun_path,"%s/edit-%lu.sock",dir,(unsigned long)getuid())>0);
    int fd=socket(AF_UNIX,SOCK_STREAM,0); CHECK(fd>=0);
    CHECK(connect(fd,(struct sockaddr *)&a,sizeof a)==0); return fd;
}
static void drain_ready(ipc_server *s, capture *c)
{
    struct pollfd p={ipc_server_fd(s),POLLIN,0}; CHECK(poll(&p,1,1000)>0);
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
    char path[IPC_PATH_CAP]; CHECK(snprintf(path,sizeof path,"%s/edit-%lu.sock",dir,(unsigned long)getuid())>0);
    int fd=open(path,O_CREAT|O_WRONLY,0600); CHECK(fd>=0); close(fd);
    ipc_server s={0}; CHECK(ipc_server_init(&s,dir)==IPC_INVALID); struct stat st; CHECK(stat(path,&st)==0 && S_ISREG(st.st_mode)); CHECK(unlink(path)==0);
    CHECK(chmod(dir,0755)==0); CHECK(ipc_server_init(&s,dir)==IPC_INVALID); CHECK(chmod(dir,0700)==0);
    CHECK(unsetenv("XDG_RUNTIME_DIR")==0); CHECK(ipc_server_init(&s,NULL)==IPC_OK);
    ipc_server other={0}; CHECK(ipc_server_init(&other,NULL)==IPC_EXISTS); ipc_server_fini(&s);
    CHECK(setenv("XDG_RUNTIME_DIR",dir,1)==0);
}

static ipc_result rejected(const ipc_request *r, ipc_token token, void *ctx)
{
    (void)r; (void)token; (void)ctx; return IPC_REJECTED;
}
static ipc_result close_now(const ipc_request *r, ipc_token token, void *ctx)
{
    CHECK(r->wait); CHECK(ipc_server_report_closed(ctx,token)==IPC_OK); return IPC_OK;
}
static void callback_results(const char *dir, bool reject)
{
    ipc_server s={0}; CHECK(ipc_server_init(&s,dir)==IPC_OK);
    pid_t child=fork(); CHECK(child>=0);
    if(child==0) { ipc_request r=request(true,false); ipc_result rc=ipc_client_send(dir,&r,2000); _exit(rc==(reject?IPC_REJECTED:IPC_OK)?0:2); }
    for(int i=0;i<3;i++) {
        struct pollfd p={ipc_server_fd(&s),POLLIN,0}; CHECK(poll(&p,1,1000)>0);
        CHECK(ipc_server_drain(&s,reject?rejected:close_now,&s)==IPC_OK);
        int status; pid_t done=waitpid(child,&status,WNOHANG);
        if(done==child) { CHECK(WIFEXITED(status) && WEXITSTATUS(status)==0); ipc_server_fini(&s); return; }
        /* Callback has sent ACK; child can finish before another fd event. */
        if(s.next_token>0) { CHECK(waitpid(child,&status,0)==child && WIFEXITED(status) && WEXITSTATUS(status)==0); ipc_server_fini(&s); return; }
    }
    CHECK(false);
}

static void stale(const char *dir)
{
    struct sockaddr_un a={0}; a.sun_family=AF_UNIX;
    CHECK(snprintf(a.sun_path,sizeof a.sun_path,"%s/edit-%lu.sock",dir,(unsigned long)getuid())>0);
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
int main(void)
{
    alarm(30); char tmp[]="/tmp/edit-ipc-test-XXXXXX"; char *dir=mkdtemp(tmp); CHECK(dir!=NULL); CHECK(setenv("XDG_RUNTIME_DIR",dir,1)==0);
    parser(dir); wire_tests(); roundtrip(dir,false,false); roundtrip(dir,true,false); roundtrip(dir,false,true); stale(dir); race(dir); fragmented(dir); wait_tokens(dir); endpoint_validation(dir); callback_results(dir,true); callback_results(dir,false);
    char p[IPC_PATH_CAP]; const char *names[]={"a:1","link","real"};
    for(size_t i=0;i<3;i++) { CHECK(snprintf(p,sizeof p,"%s/%s",dir,names[i])>0); if(i==2) CHECK(rmdir(p)==0); else CHECK(unlink(p)==0); }
    CHECK(snprintf(p,sizeof p,"%s/edit-%lu.lock",dir,(unsigned long)getuid())>0); CHECK(unlink(p)==0); CHECK(rmdir(dir)==0);
    puts("ipc_test: parser, wire, roundtrip, --wait, stdin, stale, race, fragments, tokens, endpoints ok"); return 0;
}
