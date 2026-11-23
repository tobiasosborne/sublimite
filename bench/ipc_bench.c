#include "../tests/ipc_isolation.h"
#include "ipc/ipc.h"
#include "os/os.h"
#include <inttypes.h>
#include <poll.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

#define REQUIRE(x) do { if(!(x)) { fprintf(stderr,"ipc_bench: failed %s\n",#x); exit(2); } } while(0)
/* (E) Sample count inherited from the v1 ipc benchmark fixture. Each sample
 * is one forked client's handoff: child stamp -> server ACK -> child exit
 * observed by the parent, so it includes callback, ACK and reap. */
#define N_SAMPLES 200u

static int cmp_u64(const void *a, const void *b)
{
    uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
    return (x > y) - (x < y);
}
static ipc_result opened(const ipc_request *r, ipc_token token, void *ctx)
{
    (void)token;
    REQUIRE(r->count==1 && r->paths[0].line==12);
    size_t *calls=ctx; ++*calls; return IPC_OK;
}
int main(void)
{
    alarm(60); /* v1 fixture watchdog value, carried */
    const char *dir=test_ipc_isolate(); REQUIRE(dir!=NULL);
    ipc_server server={0}; REQUIRE(ipc_server_init(&server,dir)==IPC_OK);
    /* G10 margin (edit-457.21 §14): the receive pool is 16x64 KiB + 2x1 MiB = 3 MiB (+ peer table);
     * the old 33.5 MB arena (32 x 1 MiB) must not come back. (P): the pool byte
     * count is arithmetic over the header constants. A miss is a note, not a
     * failure (v1 edit-lwq). */
    size_t pool=(size_t)IPC_RX_SMALL_COUNT*IPC_RX_SMALL_SIZE+(size_t)IPC_RX_BIG_COUNT*IPC_MAX_WIRE;
    int mem_ok=pool==3u*1024u*1024u && server.arena.size>=pool && server.arena.size<=pool+256u*1024u;
    printf("ipc_bench memory TRACK (E): rx_pool=%zu arena=%zu bytes, no limit; %s\n",pool,server.arena.size,mem_ok?"as planned":"differs from plan");
    uint64_t samples[N_SAMPLES];
    size_t calls=0;
    for(size_t i=0;i<N_SAMPLES;i++) {
        int start[2]; REQUIRE(pipe(start)==0);
        pid_t child=fork(); REQUIRE(child>=0);
        if(child==0) {
            close(start[0]);
            ipc_request r={0}; r.cwd="/tmp"; r.count=1; r.paths[0]=(ipc_path){"/tmp/bench",12,3};
            uint64_t before=os_now_ns();
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
        samples[i]=os_now_ns()-before;
    }
    char power[32]="unknown", load[64]="unknown";
    FILE *f=fopen("/sys/class/power_supply/BAT0/status","r");
    if(f!=NULL) { if(fscanf(f,"%31s",power)!=1) strcpy(power,"unknown"); fclose(f); }
    f=fopen("/proc/loadavg","r");
    if(f!=NULL) { if(fscanf(f,"%63s",load)!=1) strcpy(load,"unknown"); fclose(f); }
    /* Nearest-rank percentiles of the sorted samples (v1 bench convention). */
    qsort(samples,N_SAMPLES,sizeof *samples,cmp_u64);
    printf("ipc_bench TRACK (M) status=%s load1=%s; no limit (no derivation); includes callback, ACK, client exit/reap\n",power,load);
    printf("BENCH name=ipc_handoff n=%u p50=%" PRIu64 " p99=%" PRIu64 " unit=ns\n",
           N_SAMPLES, samples[N_SAMPLES/2-1], samples[N_SAMPLES*99/100-1]);
    ipc_server_fini(&server);
    char lock[IPC_PATH_CAP]; REQUIRE(snprintf(lock,sizeof lock,"%s/" EDIT_APP_ID "-%lu.lock",dir,(unsigned long)getuid())>0);
    REQUIRE(unlink(lock)==0 && rmdir(dir)==0); return 0;
}
