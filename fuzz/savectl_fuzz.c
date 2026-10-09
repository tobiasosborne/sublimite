#include "savectl/savectl.h"
#include "trace/trace.h"
#include "base/base.h"
#include <fcntl.h>
#include <sched.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>

#define REQUIRE(x) EDIT_ASSERT(x)
typedef struct fuzz_pause {
    _Atomic bool enabled, entered, released;
} fuzz_pause;
static void pause_hook(void *ctx, int step)
{
    fuzz_pause *p=ctx;
    if (step!=FILE_STEP_FSYNCED || !atomic_load(&p->enabled)) return;
    atomic_store(&p->entered,true);
    while (!atomic_load(&p->released)) (void)sched_yield();
}
typedef struct fuzz_model {
    uint8_t text[256],disk[256];
    size_t len,disk_len;
    bool modified,external;
    savectl_view view;
} fuzz_model;
static void route(const work_msg *msg, void *ctx) { (void)savectl_receive(ctx,msg); }
static void settle(savectl *s, work_pool *pool)
{
    for (;;) {
        (void)work_mailbox_drain(pool,route,s); savectl_tick(s);
        if (!savectl_get_model(s).busy) break;
        (void)sched_yield();
    }
}
static void replace_disk(const char *path, const uint8_t *bytes, size_t size)
{
    char temporary[128]; REQUIRE(snprintf(temporary,sizeof temporary,"%s-next",path)>0);
    int fd=open(temporary,O_WRONLY|O_CREAT|O_EXCL,0600); REQUIRE(fd>=0);
    REQUIRE(write(fd,bytes,size)==(ssize_t)size); REQUIRE(close(fd)==0);
    REQUIRE(rename(temporary,path)==0);
}
static void install(savectl *s, piece_tree **tree, fuzz_model *m)
{
    piece_tree *next=NULL; savectl_view view;
    REQUIRE(savectl_take_reload(s,&next,&view)==0);
    uint64_t len=m->disk_len;
    REQUIRE(view.cursor==(m->view.cursor<len ? m->view.cursor : len));
    REQUIRE(view.anchor==(m->view.anchor<len ? m->view.anchor : len));
    REQUIRE(view.scroll_byte==(m->view.scroll_byte<len ? m->view.scroll_byte : len));
    REQUIRE(view.scroll_x==m->view.scroll_x);
    piece_destroy(*tree); *tree=next;
    memcpy(m->text,m->disk,m->disk_len); m->len=m->disk_len;
    m->modified=false; m->external=false;
}
static void verify(savectl *s, piece_tree *tree, const char *path, const fuzz_model *m)
{
    savectl_model actual=savectl_get_model(s);
    REQUIRE(actual.modified==m->modified && (actual.banner!=NULL)==m->external);
    REQUIRE(piece_len(tree)==m->len);
    uint8_t text[256]; REQUIRE(piece_read(tree,0,text,m->len)==0);
    REQUIRE(!memcmp(text,m->text,m->len));
    int fd=open(path,O_RDONLY); REQUIRE(fd>=0);
    REQUIRE(read(fd,text,sizeof text)==(ssize_t)m->disk_len); REQUIRE(close(fd)==0);
    REQUIRE(!memcmp(text,m->disk,m->disk_len));
}
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (!size) return 0;
    trace_init();
    char directory[]="/tmp/edit-savectl-fuzz-XXXXXX"; REQUIRE(mkdtemp(directory));
    char path[128]; REQUIRE(snprintf(path,sizeof path,"%s/target",directory)>0);
    int fd=open(path,O_WRONLY|O_CREAT|O_EXCL,0600); REQUIRE(fd>=0 && write(fd,"base",4)==4); close(fd);
    struct stat st; REQUIRE(stat(path,&st)==0);
    work_pool *pool=calloc(1,sizeof *pool); REQUIRE(pool && work_pool_init(pool,1,0)==0);
    fuzz_pause pause={0};
    savectl_options options={.step=pause_hook,.step_ctx=&pause,.pool=pool,.path=path,.source_mode=FILE_MODE_COPY,
        .reload_allocator=piece_default_allocator(),
        .baseline={.dev=(uint64_t)st.st_dev,.ino=(uint64_t)st.st_ino,.size=4,
            .mtime_ns=(uint64_t)st.st_mtim.tv_sec*UINT64_C(1000000000)+(uint64_t)st.st_mtim.tv_nsec,
            .mode=0600,.exists=1}};
    savectl *s=NULL; REQUIRE(savectl_create(&s,&options,false)==0);
    piece_allocator a=piece_default_allocator(); piece_tree *tree=piece_create(&a); REQUIRE(tree);
    REQUIRE(piece_init_copy(tree,(const uint8_t *)"base",4)==0);
    fuzz_model m={.text="base",.disk="base",.len=4,.disk_len=4};
    size_t limit=size<64 ? size : 64;
    for (size_t i=0;i<limit;++i) {
        uint8_t byte=data[i];
        switch (byte%10u) {
        case 0:
            REQUIRE(piece_insert(tree,m.len,&byte,1)==0); m.text[m.len++]=byte;
            savectl_modified(s); m.modified=true; break;
        case 1: {
            int rc=savectl_save(s,tree,NULL,NULL,0);
            if (m.external) REQUIRE(rc==FILE_ERR_CHANGED);
            else {
                REQUIRE(rc==0); REQUIRE(savectl_get_model(s).state==SAVECTL_SAVING);
                memcpy(m.disk,m.text,m.len); m.disk_len=m.len;
                if (byte & 0x80u) {
                    REQUIRE(piece_insert(tree,m.len,&byte,1)==0); m.text[m.len++]=byte;
                    savectl_modified(s); m.modified=true;
                } else m.modified=false;
                settle(s,pool); REQUIRE(savectl_get_model(s).state==SAVECTL_SAVED);
            }
            break;
        }
        case 2:
            m.disk_len=(size_t)(byte%31u);
            memset(m.disk,byte,m.disk_len); replace_disk(path,m.disk,m.disk_len);
            savectl_file_event(s); settle(s,pool); m.external=true;
            if (!m.modified) install(s,&tree,&m);
            break;
        case 3:
            if (m.external) {
                REQUIRE(savectl_keep(s)==0); settle(s,pool); m.external=false; m.modified=true;
            } else REQUIRE(savectl_keep(s)==SAVECTL_INVALID);
            break;
        case 4:
            REQUIRE(savectl_reload(s)==0); settle(s,pool); install(s,&tree,&m); break;
        case 5:
            m.view=(savectl_view){byte,(uint64_t)byte*2u,(uint64_t)byte*3u,(uint64_t)byte*4u};
            savectl_set_view(s,m.view); break;
        case 6:
            REQUIRE(savectl_save(s,NULL,NULL,NULL,0)==SAVECTL_INVALID);
            REQUIRE(!savectl_receive(s,&(work_msg){.kind=0})); break;
        case 7:
            savectl_file_event(s); settle(s,pool); break;
        case 8:
            if (m.len) {
                REQUIRE(piece_delete(tree,m.len-1,1,NULL)==0); --m.len;
                savectl_modified(s); m.modified=true;
            }
            break;
        case 9:
            savectl_modified(s); m.modified=true;
            if (m.external) REQUIRE(savectl_keep(s)==0);
            settle(s,pool); m.external=false;
            atomic_store(&pause.entered,false); atomic_store(&pause.released,false);
            atomic_store(&pause.enabled,true);
            REQUIRE(savectl_save(s,tree,NULL,NULL,0)==0);
            while (!atomic_load(&pause.entered)) (void)sched_yield();
            m.disk_len=1; m.disk[0]=byte; replace_disk(path,m.disk,m.disk_len);
            if (byte & 0x80u) savectl_file_event(s);
            atomic_store(&pause.released,true);
            settle(s,pool); atomic_store(&pause.enabled,false);
            m.external=true;
            break;

        }
        verify(s,tree,path,&m);
    }
    settle(s,pool);
    while (savectl_destroy(s)==SAVECTL_BUSY) (void)sched_yield();
    work_pool_shutdown(pool); free(pool); piece_destroy(tree);
    REQUIRE(unlink(path)==0 && rmdir(directory)==0); return 0;
}
