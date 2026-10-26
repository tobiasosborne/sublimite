/* Off-path manual recovery. (P) structural constants are empty/one, field
 * widths are sizeof wire integers; POSIX owner read/write mode = 0600. */
#include "journal/private.h"
#include <inttypes.h>
#include <limits.h>
#include <string.h>

typedef struct replay {
    uint64_t id;
    size_t capacity, len;
    uint8_t *bytes;
    FILE *output;
    bool selected, automatic, ambiguous, base_seen;
    int error;
} replay;
static uint64_t offset(const uint8_t *p)
{
    uint64_t value=0;
    /* (P) little-endian uint64_t contains sizeof(uint64_t) octets. */
    for(size_t i=0;i<sizeof value;i++) value|=(uint64_t)p[i]<<(i*CHAR_BIT);
    return value;
}
static int plan(void *ctx, const journal_record *r)
{
    replay *s=ctx;
    if(r->type==JOURNAL_BASE) {
        if(s->automatic && !s->selected) { s->id=r->buffer_id; s->selected=true; }
        else if(s->automatic && s->id!=r->buffer_id) s->ambiguous=true;
        if(s->id==r->buffer_id) s->selected=true;
    }
    if(r->buffer_id==s->id && r->type==JOURNAL_INSERT) {
        size_t n=r->size-sizeof(uint64_t);
        if(n>SIZE_MAX-s->capacity) { s->error=JOURNAL_NOMEM; return s->error; }
        s->capacity+=n;
    }
    return 0;
}
static int apply_record(void *ctx, const journal_record *r)
{
    replay *s=ctx; if(r->buffer_id!=s->id) return 0;
    if(r->type==JOURNAL_BASE) {
        if(s->base_seen) { s->error=JOURNAL_INVALID; return s->error; }
        s->base_seen=true; journal_base base; char path[sizeof base.captured_path];
        if(journal_decode_base(r,&base,path,sizeof path)) { s->error=JOURNAL_INVALID; return s->error; }
        fprintf(s->output,"buffer=%" PRIu64 " recorded_base=%s identity=%s; using supplied file bytes\n",
            s->id,*path?path:"(untitled)",journal_check_base(&base)?"changed":"matches");
        return 0;
    }
    if(r->type!=JOURNAL_INSERT && r->type!=JOURNAL_DELETE) return 0;
    uint64_t off=offset(r->data);
    if(!s->base_seen || off>s->len) { s->error=JOURNAL_INVALID; return s->error; }
    size_t n;
    if(r->type==JOURNAL_INSERT) {
        n=r->size-sizeof(uint64_t);
        if(n>s->capacity-s->len) { s->error=JOURNAL_INVALID; return s->error; }
        memmove(s->bytes+(size_t)off+n,s->bytes+(size_t)off,s->len-(size_t)off);
        memcpy(s->bytes+(size_t)off,r->data+sizeof(uint64_t),n); s->len+=n;
    } else {
        uint64_t length=offset(r->data+sizeof(uint64_t));
        if(length>s->len-(size_t)off) { s->error=JOURNAL_INVALID; return s->error; }
        n=(size_t)length;
        memmove(s->bytes+(size_t)off,s->bytes+(size_t)off+n,s->len-(size_t)off-n); s->len-=n;
    }
    fprintf(s->output,"%s offset=%" PRIu64 " bytes=%zu\n",r->type==JOURNAL_INSERT?"INSERT":"DELETE",off,n);
    return 0;
}
static bool unchanged(const os_stat *a, const os_stat *b)
{ return journal_same_stat(a,b); }
static int transfer(os_file fd, uint8_t *bytes, size_t size, bool writing)
{
    size_t done=0;
    while(done<size) {
        size_t n=0;
        os_err e=writing?os_file_write(&fd,bytes+done,size-done,&n):os_file_read(&fd,bytes+done,size-done,&n);
        if(e!=OS_OK || !n) return JOURNAL_IO;
        done+=n;
    }
    return 0;
}
/* os_file_close has no result. The final publication close historically
 * reports errors, so transfer ownership to stdio and use its checked close.
 * This setup-only bridge may allocate; no bytes are buffered through it. */
static int close_written(os_file *out)
{
    FILE *stream=NULL;
    if(os_file_to_stdio(out,"r+b",&stream)!=OS_OK) { os_file_close(out); return JOURNAL_IO; }
    return fclose(stream)==0?0:JOURNAL_IO;
}
int journal_manual_replay(const char *source, const char *target, uint64_t id, bool apply, FILE *output)
{
    if(!source || !target || !output) return JOURNAL_INVALID;
    os_file fd=OS_FILE_INIT;
    os_err opened=os_file_open(&fd,target,OS_OPEN_READ|OS_OPEN_NOFOLLOW|OS_OPEN_NONBLOCK);
    if(opened!=OS_OK) return opened==OS_E_ISDIR?JOURNAL_INVALID:JOURNAL_IO;
    os_stat original,journal_identity,now;
    int rc=0;
    if(os_file_stat(&fd,&original) || original.kind!=OS_KIND_FILE ||
       (uint64_t)original.size>SIZE_MAX || os_stat_path(source,OS_STAT_NOFOLLOW,&journal_identity) || journal_identity.kind!=OS_KIND_FILE ||
       (journal_same_id(&original,&journal_identity))) rc=JOURNAL_INVALID;
    replay state={.id=id,.automatic=id==0,.output=output}; journal_replay_result result;
    if(!rc) { state.capacity=(size_t)original.size; rc=journal_scan_file(source,plan,&state,&result); }
    if(!rc && (result.corrupt || !state.selected || state.ambiguous)) rc=JOURNAL_INVALID;
    edit_arena arena={0};
    if(!rc && edit_arena_init(&arena,state.capacity?state.capacity:1)) rc=JOURNAL_NOMEM;
    if(!rc) {
        state.bytes=edit_arena_alloc(&arena,state.capacity?state.capacity:1,_Alignof(uint8_t));
        state.len=(size_t)original.size; rc=transfer(fd,state.bytes,state.len,false);
    }
    os_file_close(&fd);
    if(!rc) rc=journal_scan_file(source,apply_record,&state,&result);
    if(state.error) rc=state.error;
    if(!rc && (result.corrupt || !state.base_seen || os_stat_path(source,OS_STAT_NOFOLLOW,&now) || !unchanged(&journal_identity,&now))) rc=JOURNAL_INVALID;
    if(!rc && (os_stat_path(target,OS_STAT_NOFOLLOW,&now) || !unchanged(&original,&now))) rc=JOURNAL_BASE_CHANGED;
    if(!rc) fprintf(output,"%s %s: resulting_bytes=%zu\n",apply?"APPLY":"WOULD REPLACE",target,state.len);
    char temporary[sizeof ((journal_base *)0)->captured_path]="";
    os_dir dfd=OS_DIR_INIT; bool created=false;
    if(!rc && apply) {
        int n=snprintf(temporary,sizeof temporary,"%s.replay-XXXXXX",target);
        if(n<0 || (size_t)n>=sizeof temporary) rc=JOURNAL_INVALID;
        if(!rc) {
            opened=journal_temp(&fd,temporary,0);
            if(opened!=OS_OK) rc=JOURNAL_IO;
            else {
                created=true;
                if(os_file_set_mode(&fd,original.perm & 0777u)) rc=JOURNAL_IO;
                if(!rc) rc=transfer(fd,state.bytes,state.len,true);
                if(!rc && os_file_sync(&fd)) rc=JOURNAL_IO;
                if(rc) os_file_close(&fd);
                else rc=close_written(&fd);
            }
        }
        char parent[sizeof temporary]; strcpy(parent,target); journal_slashes(parent); char *slash=strrchr(parent,'/');
        journal_parent(parent,slash);
        if(!rc && os_dir_open(&dfd,parent)!=OS_OK) rc=JOURNAL_IO;
        if(!rc && (os_stat_path(target,OS_STAT_NOFOLLOW,&now) || !unchanged(&original,&now))) rc=JOURNAL_BASE_CHANGED;
        if(!rc && (os_stat_path(source,OS_STAT_NOFOLLOW,&now) || !unchanged(&journal_identity,&now))) rc=JOURNAL_INVALID;
        if(!rc && os_rename(temporary,target,OS_RENAME_REPLACE)) rc=JOURNAL_IO;
        else if(!rc) { created=false; if(os_dir_sync(&dfd)) rc=JOURNAL_IO; }
    }
    if(created && os_unlink(temporary) && !rc) rc=JOURNAL_IO;
    os_dir_close(&dfd);
    if(arena.base) edit_arena_free(&arena);
    return rc;
}
