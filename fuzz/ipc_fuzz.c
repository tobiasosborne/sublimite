#include "ipc/ipc.h"
#include <string.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    ipc_request out={0}; ipc_result rc=ipc_wire_decode(data,size,&out);
    EDIT_ASSERT(rc==IPC_OK || rc==IPC_PROTOCOL);
    if(rc==IPC_OK) {
        EDIT_ASSERT(out.count<=IPC_MAX_PATHS && out.cwd!=NULL);
        for(size_t i=0;i<out.count;i++) EDIT_ASSERT(out.paths[i].path[0]=='/' && out.paths[i].line && out.paths[i].col);
    } else EDIT_ASSERT(out.count==0 && out.cwd==NULL && out.stdin_data==NULL);
    /* A valid envelope on every iteration reaches deep parsing even without
     * an external seed corpus; arbitrary raw input still tests all headers. */
    uint8_t wire[8192]; ipc_request r={0}; size_t n=0;
    r.cwd="/tmp"; r.count=1; r.paths[0]=(ipc_path){"/tmp/fuzz:1",1,1};
    r.wait=size>0 && (data[0]&1u)!=0; r.has_stdin=true;
    r.stdin_data=data; r.stdin_size=size<4096?size:4096;
    EDIT_ASSERT(ipc_wire_encode(&r,wire,sizeof wire,&n)==IPC_OK);
    EDIT_ASSERT(ipc_wire_decode(wire,n,&out)==IPC_OK && out.stdin_size==r.stdin_size);
    if(size>0) {
        size_t truncated=(size_t)data[0]%n;
        EDIT_ASSERT(ipc_wire_decode(wire,truncated,&out)==IPC_PROTOCOL);
        /* Mutate structural bytes, not merely the opaque stdin payload. */
        size_t at=size>1?(size_t)data[1]%(n-r.stdin_size):0;
        wire[at]^=data[0];
        rc=ipc_wire_decode(wire,n,&out);
        EDIT_ASSERT(rc==IPC_OK || rc==IPC_PROTOCOL);
    }
    return 0;
}
