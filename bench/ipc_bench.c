#include "ipc/ipc.h"
#include "harness.h"
#include <poll.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

#define REQUIRE(x) do { if(!(x)) { fprintf(stderr,"ipc_bench: failed %s\n",#x); exit(2); } } while(0)
static ipc_result opened(const ipc_request *r, ipc_token token, void *ctx)
{
    (void)token;
    REQUIRE(r->count==1 && r->paths[0].line==12);
    size_t *calls=ctx; ++*calls; return IPC_OK;
}
int main(void)
{
    alarm(60);
    char dir[]="/tmp/edit-ipc-bench-XXXXXX"; REQUIRE(mkdtemp(dir)!=NULL);
    ipc_server server={0}; REQUIRE(ipc_server_init(&server,dir)==IPC_OK);
    /* G10 margin (edit-457.21 §14): the receive pool is 16x64 KiB + 2x1 MiB = 3 MiB (+ peer table);
     * the old 33.5 MB arena (32 x 1 MiB) must not come back. */
    size_t pool=(size_t)IPC_RX_SMALL_COUNT*IPC_RX_SMALL_SIZE+(size_t)IPC_RX_BIG_COUNT*IPC_MAX_WIRE;
    int mem_ok=pool==3u*1024u*1024u && server.arena.size>=pool && server.arena.size<=pool+256u*1024u;
    printf("ipc_bench memory (E/G): rx_pool=%zu arena=%zu bytes, gate arena<=%zu; %s\n",pool,server.arena.size,pool+256u*1024u,mem_ok?"ok":"MISS");
    REQUIRE(server.arena.size<4u*1024u*1024u);
    uint64_t samples[200]; bench_samples s; bench_samples_init(&s,samples,200);
    size_t calls=0;
    for(size_t i=0;i<200;i++) {
        int start[2]; REQUIRE(pipe(start)==0);
        pid_t child=fork(); REQUIRE(child>=0);
        if(child==0) {
            close(start[0]);
            ipc_request r={0}; r.cwd="/tmp"; r.count=1; r.paths[0]=(ipc_path){"/tmp/bench",12,3};
            uint64_t before=bench_now_ns();
            if(write(start[1],&before,sizeof before)!=(ssize_t)sizeof before) _exit(2);
            close(start[1]);
            _exit(ipc_client_send(dir,&r,2000)==IPC_OK?0:2);
        }
        close(start[1]); uint64_t before=0;
        REQUIRE(read(start[0],&before,sizeof before)==(ssize_t)sizeof before); close(start[0]);
        while(calls<=i) {
            struct pollfd p={ipc_server_fd(&server),POLLIN,0}; REQUIRE(poll(&p,1,2000)>0);
            REQUIRE(ipc_server_drain(&server,opened,&calls)==IPC_OK);
        }
        int status; REQUIRE(waitpid(child,&status,0)==child && WIFEXITED(status) && WEXITSTATUS(status)==0);
        REQUIRE(bench_add(&s,bench_now_ns()-before)==0);
    }
    char power[32], load[64]="unknown"; bench_battery_status(power,sizeof power);
    FILE *f=fopen("/proc/loadavg","r");
    if(f!=NULL) { if(fscanf(f,"%63s",load)!=1) strcpy(load,"unknown"); fclose(f); }
    printf("ipc_bench TRACK (M)%s load1=%s status=%s; gate_p99=10000000ns (G); includes callback, ACK, client exit/reap\n",bench__tag_from_power(power),load,power);
    int result=bench_report("ipc_handoff",&s,0,10000000);
    if(!mem_ok) result=1;
    ipc_server_fini(&server);
    char lock[IPC_PATH_CAP]; REQUIRE(snprintf(lock,sizeof lock,"%s/sublimite-%lu.lock",dir,(unsigned long)getuid())>0);
    REQUIRE(unlink(lock)==0 && rmdir(dir)==0); return result;
}
