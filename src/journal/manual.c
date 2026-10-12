/* Off-path manual recovery. (P) structural constants are empty/one, field
 * widths are sizeof wire integers; POSIX owner read/write mode = 0600. */
#include "journal/journal.h"
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

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
static bool unchanged(const struct stat *a, const struct stat *b)
{
    return a->st_ino==b->st_ino && a->st_dev==b->st_dev && a->st_size==b->st_size &&
        a->st_mtim.tv_sec==b->st_mtim.tv_sec && a->st_mtim.tv_nsec==b->st_mtim.tv_nsec &&
        a->st_ctim.tv_sec==b->st_ctim.tv_sec && a->st_ctim.tv_nsec==b->st_ctim.tv_nsec;
}
static int transfer(int fd, uint8_t *bytes, size_t size, bool writing)
{
    size_t done=0;
    while(done<size) {
        ssize_t n=writing?write(fd,bytes+done,size-done):read(fd,bytes+done,size-done);
        if(n<0 && errno==EINTR) continue;
        if(n<=0) return JOURNAL_IO;
        done+=(size_t)n;
    }
    return 0;
}
int journal_manual_replay(const char *source, const char *target, uint64_t id, bool apply, FILE *output)
{
    if(!source || !target || !output) return JOURNAL_INVALID;
    int fd=open(target,O_RDONLY|O_CLOEXEC|O_NOFOLLOW|O_NONBLOCK); if(fd<0) return JOURNAL_IO;
    struct stat original,journal_identity,now;
    int rc=0;
    if(fstat(fd,&original) || !S_ISREG(original.st_mode) || original.st_size<0 ||
       (uint64_t)original.st_size>SIZE_MAX || lstat(source,&journal_identity) || !S_ISREG(journal_identity.st_mode) ||
       (original.st_ino==journal_identity.st_ino && original.st_dev==journal_identity.st_dev)) rc=JOURNAL_INVALID;
    replay state={.id=id,.automatic=id==0,.output=output}; journal_replay_result result;
    if(!rc) { state.capacity=(size_t)original.st_size; rc=journal_scan_file(source,plan,&state,&result); }
    if(!rc && (result.corrupt || !state.selected || state.ambiguous)) rc=JOURNAL_INVALID;
    edit_arena arena={0};
    if(!rc && edit_arena_init(&arena,state.capacity?state.capacity:1)) rc=JOURNAL_NOMEM;
    if(!rc) {
        state.bytes=edit_arena_alloc(&arena,state.capacity?state.capacity:1,_Alignof(uint8_t));
        state.len=(size_t)original.st_size; rc=transfer(fd,state.bytes,state.len,false);
    }
    close(fd);
    if(!rc) rc=journal_scan_file(source,apply_record,&state,&result);
    if(state.error) rc=state.error;
    if(!rc && (result.corrupt || !state.base_seen || lstat(source,&now) || !unchanged(&journal_identity,&now))) rc=JOURNAL_INVALID;
    if(!rc && (lstat(target,&now) || !unchanged(&original,&now))) rc=JOURNAL_BASE_CHANGED;
    if(!rc) fprintf(output,"%s %s: resulting_bytes=%zu\n",apply?"APPLY":"WOULD REPLACE",target,state.len);
    char temporary[sizeof ((journal_base *)0)->captured_path]="";
    int dfd=-1; bool created=false;
    if(!rc && apply) {
        int n=snprintf(temporary,sizeof temporary,"%s.replay-XXXXXX",target);
        if(n<0 || (size_t)n>=sizeof temporary) rc=JOURNAL_INVALID;
        if(!rc) {
            fd=mkstemp(temporary);
            if(fd<0) rc=JOURNAL_IO;
            else {
                created=true;
                if(fchmod(fd,original.st_mode & (S_IRWXU|S_IRWXG|S_IRWXO))) rc=JOURNAL_IO;
                if(!rc) rc=transfer(fd,state.bytes,state.len,true);
                if(!rc && fsync(fd)) rc=JOURNAL_IO;
                if(close(fd) && !rc) rc=JOURNAL_IO;
            }
        }
        char parent[sizeof temporary]; strcpy(parent,target); char *slash=strrchr(parent,'/');
        if(slash==parent) slash[1]=0; else if(slash) *slash=0; else strcpy(parent,".");
        if(!rc) { dfd=open(parent,O_RDONLY|O_DIRECTORY|O_CLOEXEC); if(dfd<0) rc=JOURNAL_IO; }
        if(!rc && (lstat(target,&now) || !unchanged(&original,&now))) rc=JOURNAL_BASE_CHANGED;
        if(!rc && (lstat(source,&now) || !unchanged(&journal_identity,&now))) rc=JOURNAL_INVALID;
        if(!rc && rename(temporary,target)) rc=JOURNAL_IO;
        else if(!rc) { created=false; if(fsync(dfd)) rc=JOURNAL_IO; }
    }
    if(created && unlink(temporary) && !rc) rc=JOURNAL_IO;
    if(dfd>=0) close(dfd);
    if(arena.base) edit_arena_free(&arena);
    return rc;
}
