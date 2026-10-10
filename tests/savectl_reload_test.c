#include "savectl/savectl.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <time.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x); abort(); } } while (0)

typedef struct rewrite {
    int writer;
    struct stat before;
    uint8_t *bytes;
    size_t len;
    bool ran;
} rewrite;
static _Atomic(rewrite *) armed;

static void rewrite_file(rewrite *r)
{
    struct stat st;
    CHECK(pwrite(r->writer,r->bytes,r->len,0)==(ssize_t)r->len);
    struct timespec times[2]={r->before.st_atim,r->before.st_mtim};
    CHECK(futimens(r->writer,times)==0);
    CHECK(fstat(r->writer,&st)==0);
    CHECK(st.st_size==r->before.st_size);
    CHECK(st.st_mtim.tv_sec==r->before.st_mtim.tv_sec &&
          st.st_mtim.tv_nsec==r->before.st_mtim.tv_nsec);
    CHECK(st.st_ctim.tv_sec!=r->before.st_ctim.tv_sec ||
          st.st_ctim.tv_nsec!=r->before.st_ctim.tv_nsec);
    r->ran=true;
}

/* Executable-local syscall seam: change the actual file after copying the
 * first chunk, before the reload worker can request the second chunk. */
ssize_t pread(int fd, void *bytes, size_t n, off_t off)
{
    ssize_t got=(ssize_t)syscall(SYS_pread64,fd,bytes,n,off);
    rewrite *r=atomic_load(&armed);
    if (r && off==0 && got==(ssize_t)FILE_PREFIX_MAX) {
        struct stat st; CHECK(fstat(fd,&st)==0);
        CHECK(st.st_dev==r->before.st_dev && st.st_ino==r->before.st_ino);
        atomic_store(&armed,NULL);
        rewrite_file(r);
    }
    return got;
}

static file_id baseline(const struct stat *st)
{
    file_id id; file_id_from_stat(&id,st); return id;
}
static void wait_idle(savectl *s)
{
    for (unsigned i=0;i<100000u;++i) {
        savectl_tick(s);
        if (!savectl_get_model(s).busy) return;
        struct timespec delay={0,100000}; (void)nanosleep(&delay,NULL);
    }
    CHECK(false);
}
static void check_keep_identity(savectl *s, rewrite *r)
{
    for (unsigned i=0;i<2u;++i) {
        CHECK(fstat(r->writer,&r->before)==0);
        struct timespec delay={0,2000000}; CHECK(nanosleep(&delay,NULL)==0);
        memset(r->bytes,i ? 'D' : 'C',r->len); rewrite_file(r);
        savectl_file_event(s); wait_idle(s);
        CHECK(savectl_get_model(s).banner && savectl_get_model(s).modified);
        CHECK(savectl_keep(s)==0); wait_idle(s);
        CHECK(!savectl_get_model(s).banner && savectl_get_model(s).modified);
    }
    puts("savectl restored-mtime check/keep identity: ok");
}
int main(int argc, char **argv)
{
    char path[]="/tmp/edit-savectl-reload-XXXXXX";
    int fd=mkstemp(path); CHECK(fd>=0);
    size_t len=2u*FILE_PREFIX_MAX;
    uint8_t *bytes=malloc(len); CHECK(bytes); memset(bytes,'A',len);
    CHECK(write(fd,bytes,len)==(ssize_t)len);
    struct stat st; CHECK(fstat(fd,&st)==0);
    /* Ensure the subsequent write has a distinct observable ctime. */
    struct timespec delay={0,2000000}; CHECK(nanosleep(&delay,NULL)==0);
    work_pool *pool=aligned_alloc(_Alignof(work_pool),sizeof *pool); CHECK(pool);
    CHECK(work_pool_init(pool,1,0)==0);
    piece_allocator a=piece_default_allocator();
    piece_tree *old=piece_create(&a); CHECK(old);
    CHECK(piece_init_copy(old,(const uint8_t *)"old edits",9)==0);
    savectl_options options={.pool=pool,.path=path,.baseline=baseline(&st),
        .source_mode=FILE_MODE_COPY,.reload_allocator=a};
    savectl *s=NULL; CHECK(savectl_create(&s,&options,true)==0);
    memset(bytes,'B',len);
    rewrite r={.writer=fd,.before=st,.bytes=bytes,.len=len};
    if (argc==2 && !strcmp(argv[1],"--identity")) goto identity_check;
    atomic_store(&armed,&r);
    CHECK(savectl_reload(s)==0); wait_idle(s); CHECK(r.ran);
    piece_tree *candidate=old; savectl_view view={7,6,5,4};
    int rc=savectl_take_reload(s,&candidate,&view);
    savectl_model model=savectl_get_model(s);
    uint8_t first=0,last=0;
    CHECK(piece_read(candidate,0,&first,1)==0);
    CHECK(piece_read(candidate,piece_len(candidate)-1,&last,1)==0);
    printf("restored-mtime reload: install=%d file_error=%d first=%c last=%c modified=%d\n",
           rc,model.file_error,first,last,model.modified); fflush(stdout);
    CHECK(rc==SAVECTL_BUSY && model.file_error==FILE_ERR_CHANGED);
    CHECK(candidate==old && model.modified && model.banner);
    CHECK(view.cursor==7 && view.anchor==6 && view.scroll_byte==5 && view.scroll_x==4);
    uint8_t text[9]; CHECK(piece_read(old,0,text,sizeof text)==0 && !memcmp(text,"old edits",9));
identity_check:
    check_keep_identity(s,&r);
    savectl_close_begin(s); wait_idle(s);
    while (savectl_destroy(s)==SAVECTL_BUSY) { (void)nanosleep(&delay,NULL); }
    work_pool_shutdown(pool); free(pool); piece_destroy(old); free(bytes);
    CHECK(close(fd)==0); CHECK(unlink(path)==0);
    puts("savectl_reload_test: ok"); return 0;
}
