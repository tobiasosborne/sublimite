#include "ipc.h"
#include <string.h>

/* Input ranges may not overlap the output buffer: writing the header would
 * rewrite the strings being measured and copied (edit-457.21 review §1). */
static bool overlaps(const void *a, size_t an, const uint8_t *wire, size_t capacity)
{
    uintptr_t x=(uintptr_t)a, w=(uintptr_t)wire;
    return an!=0 && capacity!=0 && x<w+capacity && w<x+an;
}

static uint32_t get32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1]<<8 | (uint32_t)p[2]<<16 | (uint32_t)p[3]<<24;
}
static void put32(uint8_t *p, uint32_t n)
{
    for(size_t i=0;i<4;i++) p[i]=(uint8_t)(n>>(i*8u));
}
static bool string_ok(const uint8_t *p, size_t n)
{
    return n>=2 && n<=IPC_PATH_CAP && p[0]=='/' && p[n-1]==0 && memchr(p,0,n-1)==NULL;
}
ipc_result ipc_wire_decode(const uint8_t *wire, size_t size, ipc_request *out)
{
    if(out==NULL) return IPC_INVALID;
    memset(out,0,sizeof *out);
    if(wire==NULL || size<24 || size>IPC_MAX_WIRE) return IPC_PROTOCOL;
    if(memcmp(wire,"EDIP",4)!=0 || wire[4]!=1 || wire[5]>3 || wire[6]!=0 || wire[7]!=0 || get32(wire+8)!=size) return IPC_PROTOCOL;
    uint32_t count=get32(wire+12), cwdn=get32(wire+16), inputn=get32(wire+20);
    if(count>IPC_MAX_PATHS || cwdn>size-24) return IPC_PROTOCOL;
    ipc_request r={0}; r.wait=(wire[5]&1u)!=0; r.has_stdin=(wire[5]&2u)!=0;
    if(!r.has_stdin && inputn!=0) return IPC_PROTOCOL;
    size_t pos=24;
    if(!string_ok(wire+pos,cwdn)) return IPC_PROTOCOL;
    r.cwd=(const char *)(wire+pos); pos+=cwdn;
    for(size_t i=0;i<count;i++) {
        if(size-pos<12) return IPC_PROTOCOL;
        uint32_t n=get32(wire+pos), line=get32(wire+pos+4), col=get32(wire+pos+8); pos+=12;
        if(n>size-pos || !line || !col || !string_ok(wire+pos,n)) return IPC_PROTOCOL;
        r.paths[i]=(ipc_path){(const char *)(wire+pos),line,col}; pos+=n;
    }
    if(inputn!=size-pos) return IPC_PROTOCOL;
    r.count=count; r.stdin_size=inputn; r.stdin_data=r.has_stdin?wire+pos:NULL; *out=r;
    return IPC_OK;
}
ipc_result ipc_wire_encode(const ipc_request *r, uint8_t *wire, size_t capacity, size_t *size)
{
    if(size!=NULL) *size=0;
    if(r==NULL || wire==NULL || size==NULL || r->cwd==NULL || r->count>IPC_MAX_PATHS || (!r->has_stdin && r->stdin_size) || (r->stdin_size && r->stdin_data==NULL)) return IPC_INVALID;
    size_t cwdn=strnlen(r->cwd,IPC_PATH_CAP)+1u, plen[IPC_MAX_PATHS];
    if(cwdn>IPC_PATH_CAP || !string_ok((const uint8_t *)r->cwd,cwdn) || overlaps(r->cwd,cwdn,wire,capacity)) return IPC_INVALID;
    size_t total=24u+cwdn;
    for(size_t i=0;i<r->count;i++) {
        const ipc_path *p=&r->paths[i]; if(p->path==NULL || !p->line || !p->col) return IPC_INVALID;
        size_t n=strnlen(p->path,IPC_PATH_CAP)+1u;
        if(n>IPC_PATH_CAP || !string_ok((const uint8_t *)p->path,n) || overlaps(p->path,n,wire,capacity)) return IPC_INVALID;
        plen[i]=n; total+=12u+n;
    }
    if(r->stdin_size<=IPC_MAX_WIRE && overlaps(r->stdin_data,r->stdin_size,wire,capacity)) return IPC_INVALID;
    if(r->stdin_size>IPC_MAX_WIRE || total>IPC_MAX_WIRE-r->stdin_size) return IPC_LIMIT;
    total+=r->stdin_size;
    if(total>capacity) return IPC_LIMIT;
    memcpy(wire,"EDIP",4); wire[4]=1; wire[5]=(uint8_t)((r->wait?1u:0u)|(r->has_stdin?2u:0u)); wire[6]=0; wire[7]=0;
    put32(wire+8,(uint32_t)total); put32(wire+12,(uint32_t)r->count); put32(wire+16,(uint32_t)cwdn); put32(wire+20,(uint32_t)r->stdin_size);
    size_t pos=24; memcpy(wire+pos,r->cwd,cwdn); pos+=cwdn;
    for(size_t i=0;i<r->count;i++) {
        size_t n=plen[i]; put32(wire+pos,(uint32_t)n); put32(wire+pos+4,r->paths[i].line); put32(wire+pos+8,r->paths[i].col); pos+=12;
        memcpy(wire+pos,r->paths[i].path,n); pos+=n;
    }
    if(r->stdin_size) memcpy(wire+pos,r->stdin_data,r->stdin_size);
    *size=total; return IPC_OK;
}
