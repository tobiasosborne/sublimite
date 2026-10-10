#include "savectl/savectl.h"
#include "trace/trace.h"
#include "base/base.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <time.h>
#include <string.h>
#include <stdatomic.h>
#include <stdarg.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <signal.h>
#include <dirent.h>
#include <errno.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); abort(); } } while (0)

static _Thread_local bool ui_no_io;
int stat(const char *path, struct stat *st) { CHECK(!ui_no_io); return (int)syscall(SYS_newfstatat,AT_FDCWD,path,st,0); }
int fstat(int fd, struct stat *st) { CHECK(!ui_no_io); return (int)syscall(SYS_fstat,fd,st); }
int fstatat(int fd, const char *path, struct stat *st, int flags)
{ CHECK(!ui_no_io); return (int)syscall(SYS_newfstatat,fd,path,st,flags); }
int open(const char *path, int flags, ...)
{
    CHECK(!ui_no_io); mode_t mode=0;
    if (flags & O_CREAT) { va_list args; va_start(args,flags); mode=(mode_t)va_arg(args,int); va_end(args); }
    return (int)syscall(SYS_openat,AT_FDCWD,path,flags,mode);
}
int openat(int fd, const char *path, int flags, ...)
{
    CHECK(!ui_no_io); mode_t mode=0;
    if (flags & O_CREAT) { va_list args; va_start(args,flags); mode=(mode_t)va_arg(args,int); va_end(args); }
    return (int)syscall(SYS_openat,fd,path,flags,mode);
}
int fsync(int fd) { CHECK(!ui_no_io); return (int)syscall(SYS_fsync,fd); }
ssize_t pread(int fd, void *bytes, size_t n, off_t off) { CHECK(!ui_no_io); return (ssize_t)syscall(SYS_pread64,fd,bytes,n,off); }
int close(int fd) { CHECK(!ui_no_io); return (int)syscall(SYS_close,fd); }
#define UI_NO_IO(x) do { ui_no_io=true; x; ui_no_io=false; } while (0)
static void basic(void)
{
    work_pool *pool = aligned_alloc(_Alignof(work_pool), sizeof *pool); CHECK(pool);
    CHECK((uintptr_t)pool % _Alignof(work_pool) == 0);
    CHECK(work_pool_init(pool, 1, 0) == 0);
    savectl_options o = {.pool=pool,.path="/tmp/savectl-uncreated",.source_mode=FILE_MODE_COPY,.reload_allocator=piece_default_allocator()};
    savectl *s = NULL;
    CHECK(savectl_create(&s, &o, false) == SAVECTL_OK);
    CHECK(savectl_get_model(s).state == SAVECTL_IDLE);
    savectl_modified(s);
    CHECK(savectl_get_model(s).modified);
    CHECK(savectl_destroy(s) == SAVECTL_OK);
    work_pool_shutdown(pool); free(pool);
}

typedef struct fixture {
    work_pool *pool;
    savectl *s;
    piece_tree *tree;
    char dir[80],path[120];
} fixture;
static void put(const char *path, const char *bytes)
{
    int fd=open(path,O_WRONLY|O_CREAT|O_TRUNC,0600); CHECK(fd>=0);
    size_t n=strlen(bytes); CHECK(write(fd,bytes,n)==(ssize_t)n); CHECK(close(fd)==0);
}
static void contents(const char *path, const char *expected)
{
    uint8_t bytes[128]={0}; int fd=open(path,O_RDONLY); CHECK(fd>=0);
    CHECK(read(fd,bytes,sizeof bytes)==(ssize_t)strlen(expected));
    CHECK(!memcmp(bytes,expected,strlen(expected))); CHECK(close(fd)==0);
}
static file_id base_id(const char *path)
{
    struct stat st; CHECK(stat(path,&st)==0);
    file_id id; file_id_from_stat(&id,&st); return id;
}
static void nap(void) { struct timespec t={0,100000}; (void)nanosleep(&t,NULL); }
static void wait_controller(fixture *f)
{
    for (unsigned i=0;i<100000u;++i) {
        UI_NO_IO(savectl_tick(f->s));
        if (!savectl_get_model(f->s).busy) return;
        nap();
    }
    CHECK(false);
}
static void init(fixture *f, const char *bytes)
{
    memset(f,0,sizeof *f); strcpy(f->dir,"/tmp/edit-savectl-test-XXXXXX"); CHECK(mkdtemp(f->dir));
    CHECK(snprintf(f->path,sizeof f->path,"%s/target",f->dir)>0);
    put(f->path,bytes);
    f->pool=aligned_alloc(_Alignof(work_pool),sizeof *f->pool); CHECK(f->pool); CHECK((uintptr_t)f->pool % _Alignof(work_pool) == 0); CHECK(work_pool_init(f->pool,1,0)==0);
    piece_allocator a=piece_default_allocator(); f->tree=piece_create(&a); CHECK(f->tree);
    CHECK(piece_init_copy(f->tree,(const uint8_t *)bytes,strlen(bytes))==0);
    savectl_options o={.pool=f->pool,.path=f->path,.baseline=base_id(f->path),.source_mode=FILE_MODE_COPY,.reload_allocator=piece_default_allocator()};
    CHECK(savectl_create(&f->s,&o,false)==0);
}
static void finish_fixture(fixture *f)
{
    wait_controller(f);
    while (savectl_destroy(f->s)==SAVECTL_BUSY) nap();
    work_pool_shutdown(f->pool); free(f->pool); piece_destroy(f->tree);
    CHECK(unlink(f->path)==0); CHECK(rmdir(f->dir)==0);
}
static void edit(fixture *f)
{
    CHECK(piece_insert(f->tree,piece_len(f->tree),(const uint8_t *)"!",1)==0);
    savectl_modified(f->s);
}
typedef struct pause_job { _Atomic bool entered,release; int step; } pause_job;
static void pause_wait(pause_job *p)
{
    for (unsigned i=0;i<100000u && !atomic_load(&p->entered);++i) nap();
    CHECK(atomic_load(&p->entered));
}
static void pause_hook(void *ctx, int step)
{
    pause_job *p=ctx; if (step!=p->step) return;
    atomic_store(&p->entered,true);
    while (!atomic_load(&p->release)) nap();
}
static void blocker(work_ctx *ctx) { pause_hook(ctx->arg,0); }

static _Atomic(pause_job *) publication_pause;
bool __wrap_work_publish(work_ctx *ctx, const work_msg *message)
{
    bool ok=work_publish(ctx,message);
    pause_job *p=atomic_load(&publication_pause);
    if (ok && p && message->kind==SAVECTL_MESSAGE) pause_hook(p,0);
    return ok;
}
static void notification_orders_completion(void)
{
    fixture f; init(&f,"old"); edit(&f); pause_job p={0};
    atomic_store(&publication_pause,&p);
    CHECK(savectl_save(f.s,f.tree,NULL,NULL,0)==0); pause_wait(&p);
    /* Publish has delivered the wake/message, but its function has not returned.
     * A consumer must already be able to see the completed immutable result. */
    savectl_tick(f.s);
    CHECK(savectl_get_model(f.s).state==SAVECTL_SAVED);
    atomic_store(&p.release,true); atomic_store(&publication_pause,NULL); finish_fixture(&f);
}
/* I/O symbols above interpose controller AND library calls in ordinary
 * release/sanitizer builds; direct Linux syscalls avoid recursion/dlsym.
 * Compile the production controller here only to intercept work publication
 * (work.o also supplies the pool, so link interposition would duplicate it).
 * The archive does not extract its duplicate savectl object. */
#define work_publish(...) __wrap_work_publish(__VA_ARGS__)
#include "../src/savectl/savectl.c"
#undef work_publish
static void recreate(fixture *f, pause_job *p)
{
    CHECK(savectl_destroy(f->s)==0);
    savectl_options o={.pool=f->pool,.path=f->path,.baseline=base_id(f->path),
        .source_mode=FILE_MODE_COPY,.reload_allocator=piece_default_allocator(),.step=pause_hook,.step_ctx=p};
    CHECK(savectl_create(&f->s,&o,false)==0);
}
static void save_states(void)
{
    fixture f; init(&f,"old"); pause_job p={.step=FILE_STEP_FSYNCED}; recreate(&f,&p); edit(&f);
    UI_NO_IO(CHECK(savectl_save(f.s,f.tree,NULL,NULL,0)==0));
    CHECK(savectl_get_model(f.s).state==SAVECTL_SAVING);
    CHECK(!strcmp(savectl_get_model(f.s).status,"saving"));
    pause_wait(&p); edit(&f); CHECK(savectl_save(f.s,f.tree,NULL,NULL,0)==SAVECTL_BUSY);
    atomic_store(&p.release,true); wait_controller(&f);
    CHECK(savectl_get_model(f.s).state==SAVECTL_SAVED && savectl_get_model(f.s).modified);
    contents(f.path,"old!");
    UI_NO_IO(CHECK(savectl_save(f.s,f.tree,NULL,NULL,0)==0)); wait_controller(&f);
    CHECK(!savectl_get_model(f.s).modified); contents(f.path,"old!!");
    savectl_file_event(f.s); wait_controller(&f); CHECK(!savectl_get_model(f.s).banner);
    finish_fixture(&f); puts("savectl save states/snapshot isolation/self event: ok");
}
static void ack_queued(void)
{
    fixture f; init(&f,"old"); pause_job p={0};
    CHECK(work_submit(f.pool,(work_job){blocker,&p,0,WORK_BULK}).epoch); pause_wait(&p);
    edit(&f); CHECK(savectl_save(f.s,f.tree,NULL,NULL,0)==0);
    /* Returning while a deliberately blocked bulk worker cannot perform I/O
     * is stronger evidence than a flaky elapsed-time assertion. */
    CHECK(!atomic_load(&p.release) && savectl_get_model(f.s).busy);
    CHECK(savectl_get_model(f.s).state==SAVECTL_SAVING); contents(f.path,"old");
    CHECK(savectl_destroy(f.s)==SAVECTL_BUSY);
    atomic_store(&p.release,true); wait_controller(&f); finish_fixture(&f);
    puts("savectl ack never waits for worker/I/O: ok");
}
static void race(bool notified, bool restore_identity)
{
    fixture f; init(&f,"old"); pause_job p={.step=FILE_STEP_FSYNCED}; recreate(&f,&p); edit(&f);
    file_id original=base_id(f.path);
    struct stat st; CHECK(stat(f.path,&st)==0);
    UI_NO_IO(CHECK(savectl_save(f.s,f.tree,NULL,NULL,0)==0)); pause_wait(&p);
    if (restore_identity) {
        struct timespec times[2]={st.st_atim,{1100000000,0}};
        CHECK(utimensat(AT_FDCWD,f.path,times,0)==0);
        savectl_file_event(f.s);
        times[1]=st.st_mtim; CHECK(utimensat(AT_FDCWD,f.path,times,0)==0);
    } else put(f.path,"external");
    if (notified && !restore_identity) savectl_file_event(f.s);
    atomic_store(&p.release,true); wait_controller(&f);
    CHECK(savectl_get_model(f.s).modified && savectl_get_model(f.s).banner);
    CHECK(savectl_get_model(f.s).file_error==FILE_ERR_CHANGED || savectl_get_model(f.s).state==SAVECTL_EXTERNAL_MODIFIED);
    contents(f.path,restore_identity ? "old" : "external");
    if (restore_identity) CHECK(base_id(f.path).ino==original.ino);
    UI_NO_IO(CHECK(savectl_keep(f.s)==0)); wait_controller(&f);
    CHECK(!savectl_get_model(f.s).banner && savectl_get_model(f.s).modified);
    UI_NO_IO(CHECK(savectl_save(f.s,f.tree,NULL,NULL,0)==0)); wait_controller(&f); contents(f.path,"old!");
    finish_fixture(&f);
}
static void failed_io(void)
{
    fixture f; init(&f,"old"); edit(&f);
    char moved[120]; CHECK(snprintf(moved,sizeof moved,"%s-away",f.dir)>0);
    CHECK(rename(f.dir,moved)==0);
    UI_NO_IO(CHECK(savectl_save(f.s,f.tree,NULL,NULL,0)==0)); wait_controller(&f);
    CHECK(savectl_get_model(f.s).state==SAVECTL_FAILED && savectl_get_model(f.s).modified);
    CHECK(savectl_get_model(f.s).file_error==FILE_ERR_IO);
    CHECK(rename(moved,f.dir)==0); contents(f.path,"old"); finish_fixture(&f);
    puts("savectl failed save preserves old file and modified flag: ok");
}
static void reload_offsets(bool editing, bool late_edit)
{
    fixture f; init(&f,"long original text");
    savectl_set_view(f.s,(savectl_view){3,5,7,99});
    if (editing) edit(&f);
    put(f.path,"replacement bytes"); savectl_file_event(f.s); wait_controller(&f);
    if (editing) {
        CHECK(savectl_get_model(f.s).state==SAVECTL_EXTERNAL_MODIFIED);
        CHECK(savectl_get_model(f.s).can_reload && savectl_get_model(f.s).can_keep);
        CHECK(savectl_reload(f.s)==0); wait_controller(&f);
    }
    if (late_edit) edit(&f);
    piece_tree *replacement=NULL; savectl_view view={0};
    if (late_edit) {
        CHECK(savectl_take_reload(f.s,&replacement,&view)==SAVECTL_BUSY);
        CHECK(savectl_get_model(f.s).banner && savectl_get_model(f.s).modified);
        wait_controller(&f); finish_fixture(&f); return;
    }
    /* Latest view wins, even when the user scrolls during worker reload. */
    savectl_set_view(f.s,(savectl_view){4,500,8,101});
    UI_NO_IO(CHECK(savectl_take_reload(f.s,&replacement,&view)==0));
    CHECK(view.cursor==4 && view.anchor==17 && view.scroll_byte==8 && view.scroll_x==101);
    uint8_t bytes[17]; CHECK(piece_read(replacement,0,bytes,sizeof bytes)==0);
    CHECK(!memcmp(bytes,"replacement bytes",17)); CHECK(!savectl_get_model(f.s).modified);
    piece_destroy(f.tree); f.tree=replacement; finish_fixture(&f);
}
static void during_reload_edit(void)
{
    fixture f; init(&f,"old"); pause_job p={0};
    CHECK(work_submit(f.pool,(work_job){blocker,&p,0,WORK_BULK}).epoch); pause_wait(&p);
    CHECK(savectl_reload(f.s)==0); edit(&f);
    atomic_store(&p.release,true); wait_controller(&f);
    CHECK(savectl_get_model(f.s).state==SAVECTL_EXTERNAL_MODIFIED);
    finish_fixture(&f);
}
static void mapped_requires_guard(void)
{
    fixture f; init(&f,"old"); CHECK(savectl_destroy(f.s)==0);
    savectl_options o={.pool=f.pool,.path=f.path,.baseline=base_id(f.path),.source_mode=FILE_MODE_MMAP};
    CHECK(savectl_create(&f.s,&o,false)==0);
    CHECK(savectl_save(f.s,f.tree,NULL,NULL,0)==SAVECTL_SOURCE_GUARD);
    edit(&f); savectl_file_event(f.s); savectl_tick(f.s);
    CHECK(savectl_get_model(f.s).banner && !savectl_get_model(f.s).can_keep);
    CHECK(savectl_keep(f.s)==SAVECTL_SOURCE_GUARD); finish_fixture(&f);
}

static void le64(uint8_t *p, uint64_t n) { for (unsigned i=0;i<8u;++i) p[i]=(uint8_t)(n>>(i*8u)); }
static void le32(uint8_t *p, uint32_t n) { for (unsigned i=0;i<4u;++i) p[i]=(uint8_t)(n>>(i*8u)); }
static size_t base_payload(uint8_t *p, const journal_base *b)
{
    le64(p,b->size); le64(p+8,b->mtime_ns); le64(p+16,b->inode); le64(p+24,b->device);
    le32(p+32,b->prefix_crc); le32(p+36,b->prefix_len);
    size_t n=strlen(b->path); memcpy(p+40,b->path,n+1); return 41+n;
}
static int restore(void *ctx, const journal_record *r)
{
    piece_tree *tree=ctx;
    if (r->type==JOURNAL_BASE) {
        journal_base b; char path[4097]; CHECK(journal_decode_base(r,&b,path,sizeof path)==0);
        uint8_t bytes[128]; CHECK(b.size<=sizeof bytes);
        if (*path) { int fd=open(path,O_RDONLY); CHECK(fd>=0); CHECK(read(fd,bytes,sizeof bytes)==(ssize_t)b.size); close(fd); }
        CHECK(piece_init_copy(tree,bytes,(size_t)b.size)==0);
    } else if (r->type==JOURNAL_INSERT || r->type==JOURNAL_DELETE) CHECK(journal_apply_piece(tree,r)==0);
    return 0;
}
static void journal_transaction(bool fail_prepare, bool fail_file)
{
    fixture f; init(&f,"old");
    char logpath[160]; CHECK(snprintf(logpath,sizeof logpath,"%s/journal",f.dir)>0);
    work_pool *jp=aligned_alloc(_Alignof(work_pool),sizeof *jp); CHECK(jp); CHECK((uintptr_t)jp % _Alignof(work_pool) == 0); CHECK(work_pool_init(jp,1,0)==0);
    journal *j=NULL; CHECK(journal_open(&j,logpath,jp,NULL)==0);
    journal_base previous; CHECK(journal_capture_base(f.path,&previous)==0);
    uint8_t bp[4137], insert[9]={0}; le64(insert,3); insert[8]='!';
    size_t bsize=base_payload(bp,&previous);
    journal_record cp[]={{JOURNAL_BASE,1,0,bp,bsize},{JOURNAL_INSERT,1,0,insert,sizeof insert}};
    CHECK(journal_set_base(j,1,&previous)==0); CHECK(journal_insert(j,1,3,(const uint8_t *)"!",1)==0);
    CHECK(savectl_destroy(f.s)==0);
    pause_job p={.step=FILE_STEP_FSYNCED};
    savectl_options o={.pool=f.pool,.path=f.path,.baseline=base_id(f.path),.source_mode=FILE_MODE_COPY,
        .journal=j,.journal_pool=jp,.buffer_id=1,.reload_allocator=piece_default_allocator(),
        .step=pause_hook,.step_ctx=&p};
    CHECK(savectl_create(&f.s,&o,false)==0); edit(&f);
    savectl_content_identity(f.s,100,99,true);
    if (fail_prepare) cp[0].buffer_id=2;
    CHECK(savectl_save(f.s,f.tree,&previous,cp,2)==0);
    CHECK(savectl_get_model(f.s).journal_leased);
    if (fail_prepare) {
        wait_controller(&f); CHECK(savectl_get_model(f.s).state==SAVECTL_FAILED);
        CHECK(savectl_get_model(f.s).journal_error==JOURNAL_INVALID);
        CHECK(savectl_get_model(f.s).modified); contents(f.path,"old");
    } else {
        for (unsigned i=0;i<100000u && !atomic_load(&p.entered);++i) {
            savectl_tick(f.s); nap();
        }
        CHECK(atomic_load(&p.entered));
        CHECK(!savectl_get_model(f.s).journal_leased);
        /* Appends may continue once prepare has returned ownership. */
        edit(&f); savectl_content_identity(f.s,101,99,true);
        CHECK(journal_insert(j,1,4,(const uint8_t *)"!",1)==0);
        if (fail_file) { put(f.path,"disk changed"); savectl_file_event(f.s); }
        atomic_store(&p.release,true); wait_controller(&f);
        if (fail_file) {
            CHECK(savectl_get_model(f.s).banner && savectl_get_model(f.s).modified);
            contents(f.path,"disk changed");
            const journal_save *token=savectl_save_token(f.s); CHECK(token && token->prepared);
            CHECK(access(token->previous_path,F_OK)==0); contents(token->previous_path,"old");
            /* Test teardown deliberately resolves retained recovery. Production
             * must checkpoint/recover before retiring this generation. */
            CHECK(unlink(token->previous_path)==0);
        } else {
            CHECK(savectl_get_model(f.s).needs_finish && savectl_get_model(f.s).modified);
            CHECK(!savectl_get_model(f.s).journal_leased); contents(f.path,"old!");
            char retained[4097]; strcpy(retained,savectl_save_token(f.s)->previous_path);
            CHECK(access(retained,F_OK)==0);
            const journal_base *saved_base=savectl_saved_base(f.s); CHECK(saved_base);
            uint8_t nextbase[4137], later[9]={0}; le64(later,4); later[8]='!';
            journal_record next[]={{JOURNAL_BASE,1,0,nextbase,base_payload(nextbase,saved_base)},
                {JOURNAL_INSERT,1,0,later,sizeof later}};
            journal_record invalid=next[0]; invalid.buffer_id=2;
            CHECK(savectl_finish(f.s,&invalid,1)==0); wait_controller(&f);
            CHECK(savectl_get_model(f.s).needs_finish && savectl_get_model(f.s).state==SAVECTL_FAILED);
            CHECK(access(retained,F_OK)==0 && savectl_get_model(f.s).modified);
            CHECK(savectl_finish(f.s,next,2)==0); wait_controller(&f);
            CHECK(savectl_get_model(f.s).state==SAVECTL_SAVED && savectl_get_model(f.s).modified);
            CHECK(!savectl_get_model(f.s).needs_finish && access(retained,F_OK)!=0);
            piece_allocator a=piece_default_allocator(); piece_tree *recovered=piece_create(&a); CHECK(recovered);
            journal_replay_result rr; CHECK(journal_replay_file(logpath,restore,recovered,&rr)==0);
            uint8_t text[5]; CHECK(piece_read(recovered,0,text,sizeof text)==0 && !memcmp(text,"old!!",5));
            piece_destroy(recovered);
        }
    }
    journal_close(j); work_pool_shutdown(jp); free(jp); CHECK(unlink(logpath)==0); finish_fixture(&f);
}
static int restore_other(void *ctx, const journal_record *record)
{ return record->buffer_id==2 ? restore(ctx,record) : 0; }
/* A crashed save must not strand another buffer's accepted appends. */
static void journal_handoff_crash(void)
{
    char directory[]="/tmp/edit-savectl-crash-XXXXXX"; CHECK(mkdtemp(directory));
    char path[160], logpath[160];
    CHECK(snprintf(path,sizeof path,"%s/target",directory)>0);
    CHECK(snprintf(logpath,sizeof logpath,"%s/journal",directory)>0);
    put(path,"old");
    pid_t child=fork(); CHECK(child>=0);
    if (!child) {
        work_pool *pool=aligned_alloc(_Alignof(work_pool),sizeof *pool);
        work_pool *jp=aligned_alloc(_Alignof(work_pool),sizeof *jp); CHECK(pool && jp);
        CHECK(work_pool_init(pool,1,0)==0 && work_pool_init(jp,1,0)==0);
        journal *j=NULL; CHECK(journal_open(&j,logpath,jp,NULL)==0);
        journal_base previous; CHECK(journal_capture_base(path,&previous)==0);
        CHECK(journal_set_base(j,1,&previous)==0);
        uint8_t bp[4137], empty[41]={0};
        journal_record cp[]={{JOURNAL_BASE,1,0,bp,base_payload(bp,&previous)},
            {JOURNAL_BASE,2,0,empty,sizeof empty}};
        pause_job pause={.step=FILE_STEP_FSYNCED};
        savectl_options options={.pool=pool,.path=path,.baseline=base_id(path),
            .source_mode=FILE_MODE_COPY,.journal=j,.journal_pool=jp,.buffer_id=1,
            .step=pause_hook,.step_ctx=&pause};
        savectl *controller=NULL; CHECK(savectl_create(&controller,&options,true)==0);
        piece_allocator allocator=piece_default_allocator(); piece_tree *tree=piece_create(&allocator);
        CHECK(tree && piece_init_copy(tree,(const uint8_t *)"saved",5)==0);
        CHECK(savectl_save(controller,tree,&previous,cp,2)==0);
        CHECK(savectl_get_model(controller).journal_leased);
        /* Published prepare returns ownership before the file-only stage. */
        for (unsigned i=0;i<100000u && !atomic_load(&pause.entered);++i) {
            UI_NO_IO(savectl_tick(controller)); nap();
        }
        CHECK(atomic_load(&pause.entered));
        CHECK(!savectl_get_model(controller).journal_leased);
        CHECK(savectl_get_model(controller).busy);
        CHECK(journal_insert(j,2,0,(const uint8_t *)"other-buffer",12)==0);
        CHECK(journal_set_view(j,2,&(journal_view){.cursor=12})==0);
        /* Process crash before rename/finish; no orderly journal flush. */
        CHECK(kill(getpid(),SIGKILL)==0); _exit(1);
    }
    int status=0; CHECK(waitpid(child,&status,0)==child);
    CHECK(WIFSIGNALED(status) && WTERMSIG(status)==SIGKILL);
    contents(path,"old");
    piece_allocator allocator=piece_default_allocator(); piece_tree *other=piece_create(&allocator); CHECK(other);
    journal_replay_result rr;
    /* Replay both buffers separately: buffer 2 starts from the empty BASE. */
    CHECK(journal_replay_file(logpath,restore_other,other,&rr)==0);
    uint8_t text[12]; CHECK(piece_len(other)==12);
    CHECK(piece_read(other,0,text,sizeof text)==0 && !memcmp(text,"other-buffer",12));
    piece_destroy(other);
    DIR *dir=opendir(directory); CHECK(dir); struct dirent *entry;
    while ((entry=readdir(dir))) if (strcmp(entry->d_name,".") && strcmp(entry->d_name,"..")) {
        char artifact[320]; CHECK(snprintf(artifact,sizeof artifact,"%s/%s",directory,entry->d_name)>0);
        CHECK(unlink(artifact)==0);
    }
    CHECK(closedir(dir)==0 && rmdir(directory)==0);
    puts("savectl prepare handoff/other-buffer crash replay: ok");
}
typedef struct finish_fault { _Atomic bool fail_data; } finish_fault;
static int finish_sync(void *ctx, int fd, bool directory)
{
    finish_fault *fault=ctx;
    if (!directory && atomic_exchange(&fault->fail_data,false)) { errno=EIO; return -1; }
    return directory ? fsync(fd) : fdatasync(fd);
}
static int restore_first(void *ctx, const journal_record *record)
{ return record->buffer_id==1 ? restore(ctx,record) : 0; }
static void finish_writeback_recovery(void)
{
    fixture f; init(&f,"old"); edit(&f);
    char logpath[160]; CHECK(snprintf(logpath,sizeof logpath,"%s/journal",f.dir)>0);
    work_pool *jp=aligned_alloc(_Alignof(work_pool),sizeof *jp); CHECK(jp);
    CHECK(work_pool_init(jp,1,0)==0); journal *j=NULL; CHECK(journal_open(&j,logpath,jp,NULL)==0);
    journal_base previous; CHECK(journal_capture_base(f.path,&previous)==0);
    CHECK(journal_set_base(j,1,&previous)==0);
    uint8_t bp[4137], first[9]={0}, empty[41]={0}; le64(first,3); first[8]='!';
    journal_record cp[]={{JOURNAL_BASE,1,0,bp,base_payload(bp,&previous)},
        {JOURNAL_INSERT,1,0,first,sizeof first},{JOURNAL_BASE,2,0,empty,sizeof empty}};
    CHECK(savectl_destroy(f.s)==0);
    savectl_options options={.pool=f.pool,.path=f.path,.baseline=base_id(f.path),
        .source_mode=FILE_MODE_COPY,.journal=j,.journal_pool=jp,.buffer_id=1};
    CHECK(savectl_create(&f.s,&options,true)==0);
    CHECK(savectl_save(f.s,f.tree,&previous,cp,3)==0); wait_controller(&f);
    CHECK(savectl_get_model(f.s).needs_finish);
    journal_save retained=*savectl_save_token(f.s);
    CHECK(retained.prepared && access(retained.previous_path,F_OK)==0);
    edit(&f); CHECK(journal_insert(j,2,0,(const uint8_t *)"other",5)==0);
    uint8_t nextbase[4137], later[9]={0}, other[13]={0}; le64(later,4); later[8]='!';
    memcpy(other+8,"other",5);
    journal_record next[]={{JOURNAL_BASE,1,0,nextbase,base_payload(nextbase,savectl_saved_base(f.s))},
        {JOURNAL_INSERT,1,0,later,sizeof later},{JOURNAL_BASE,2,0,empty,sizeof empty},
        {JOURNAL_INSERT,2,0,other,sizeof other}};
    finish_fault fault={.fail_data=true}; journal_io io={.ctx=&fault,.sync=finish_sync};
    CHECK(journal_set_io(j,&io)==0);
    CHECK(savectl_finish(f.s,next,4)==0); wait_controller(&f);
    CHECK(savectl_get_model(f.s).needs_finish && savectl_get_model(f.s).journal_error==JOURNAL_IO);
    CHECK(journal_retry(j)==JOURNAL_IO && journal_retry(j)==JOURNAL_IO);
    CHECK(savectl_finish(f.s,next,4)==0); wait_controller(&f);
    CHECK(savectl_get_model(f.s).needs_finish && savectl_get_model(f.s).journal_error==JOURNAL_IO);
    CHECK(!memcmp(&retained,savectl_save_token(f.s),sizeof retained));
    file_id old_log=base_id(logpath);
    int old_log_fd=open(logpath,O_RDONLY); CHECK(old_log_fd>=0);
    /* A failed fresh rotation also preserves the token for another attempt. */
    atomic_store(&fault.fail_data,true);
    CHECK(savectl_recover_finish(f.s,next,4)==0); wait_controller(&f);
    CHECK(savectl_get_model(f.s).needs_finish && savectl_get_model(f.s).journal_error==JOURNAL_IO);
    CHECK(!memcmp(&retained,savectl_save_token(f.s),sizeof retained));
    CHECK(access(retained.previous_path,F_OK)==0);
    CHECK(savectl_recover_finish(f.s,next,4)==0);
    CHECK(savectl_get_model(f.s).journal_leased); wait_controller(&f);
    CHECK(base_id(logpath).ino!=old_log.ino); CHECK(close(old_log_fd)==0);
    CHECK(!savectl_get_model(f.s).needs_finish && savectl_get_model(f.s).modified);
    CHECK(savectl_get_model(f.s).state==SAVECTL_SAVED);
    CHECK(!savectl_save_token(f.s)->prepared && access(retained.previous_path,F_OK)!=0);
    contents(f.path,"old!");
    piece_allocator allocator=piece_default_allocator();
    piece_tree *one=piece_create(&allocator), *two=piece_create(&allocator); CHECK(one && two);
    journal_replay_result rr; CHECK(journal_replay_file(logpath,restore_first,one,&rr)==0);
    CHECK(journal_replay_file(logpath,restore_other,two,&rr)==0);
    uint8_t text[5]; CHECK(piece_read(one,0,text,5)==0 && !memcmp(text,"old!!",5));
    CHECK(piece_read(two,0,text,5)==0 && !memcmp(text,"other",5));
    piece_destroy(one); piece_destroy(two); journal_close(j); work_pool_shutdown(jp); free(jp);
    CHECK(unlink(logpath)==0); finish_fixture(&f);
    puts("savectl finish writeback/fresh-inode retry/retained-token reconciliation: ok");
}
typedef struct session_request_test {
    journal_base previous;
    size_t buffers;
    journal_record *records;
    uint8_t base[4137], empty[41], insert[9];
    _Atomic unsigned preparations, releases;
    bool fail;
} session_request_test;
static int prepare_session(void *ctx, const piece_snapshot *snapshot, savectl_checkpoint *out)
{
    CHECK(!ui_no_io);
    session_request_test *session=ctx;
    (void)atomic_fetch_add(&session->preparations,1);
    if (session->fail) return JOURNAL_NOMEM;
    session->records=calloc(session->buffers*2,sizeof *session->records); CHECK(session->records);
    size_t size=base_payload(session->base,&session->previous);
    le64(session->insert,0); session->insert[8]='x';
    for (size_t i=0;i<session->buffers;++i) {
        session->records[i*2]=(journal_record){JOURNAL_BASE,i+1,0,
            i ? session->empty : session->base,i ? sizeof session->empty : size};
        if (i) session->records[i*2+1]=(journal_record){JOURNAL_INSERT,i+1,0,
            session->insert,sizeof session->insert};
        else {
            uint8_t byte=0; CHECK(piece_snapshot_read(snapshot,3,&byte,1)==0 && byte=='!');
            /* Selected buffer already has this one-byte immutable delta. */
            session->records[1]=(journal_record){JOURNAL_INSERT,1,0,
                (const uint8_t *)"\3\0\0\0\0\0\0\0!",9};
        }
    }
    *out=(savectl_checkpoint){session->previous,session->records,session->buffers*2};
    return JOURNAL_OK;
}
static void release_session(void *ctx)
{
    CHECK(!ui_no_io); session_request_test *session=ctx;
    free(session->records); session->records=NULL;
    (void)atomic_fetch_add(&session->releases,1);
}
static void bounded_request_status(bool failure)
{
    fixture f; init(&f,"old"); edit(&f);
    char logpath[160]; CHECK(snprintf(logpath,sizeof logpath,"%s/journal",f.dir)>0);
    work_pool *jp=aligned_alloc(_Alignof(work_pool),sizeof *jp); CHECK(jp && work_pool_init(jp,1,0)==0);
    journal *j=NULL; CHECK(journal_open(&j,logpath,jp,NULL)==0);
    session_request_test session={.buffers=2048,.fail=failure};
    CHECK(journal_capture_base(f.path,&session.previous)==0);
    CHECK(journal_set_base(j,1,&session.previous)==0);
    CHECK(savectl_destroy(f.s)==0);
    savectl_options options={.pool=f.pool,.path=f.path,.baseline=base_id(f.path),
        .source_mode=FILE_MODE_COPY,.journal=j,.journal_pool=jp,.buffer_id=1};
    CHECK(savectl_create(&f.s,&options,true)==0);
    pause_job backend={0}; CHECK(work_submit(f.pool,(work_job){blocker,&backend,0,WORK_BULK}).epoch);
    pause_wait(&backend);
    /* Snapshot/root publication is setup here, not hidden in ack timing. */
    savectl_request request={.snapshot=piece_snapshot_take(f.tree),.ctx=&session,
        .prepare=prepare_session,.release=release_session,.request_ns=100}; CHECK(request.snapshot);
    edit_malloc_guard_begin();
    UI_NO_IO(CHECK(savectl_save_request(f.s,&request)==0));
    savectl_model model=savectl_get_model(f.s);
    CHECK(edit_malloc_guard_end()==0);
    CHECK(model.busy && model.saving_frame_pending && !strcmp(model.status,"saving"));
    CHECK(model.request_id && model.request_ns==100 && !model.saving_submitted_ns);
    CHECK(!atomic_load(&session.preparations) && !atomic_load(&session.releases));
    /* A busy backend cannot yet submit; polling/ticking must not ack a frame. */
    UI_NO_IO(savectl_tick(f.s)); CHECK(savectl_get_model(f.s).saving_frame_pending);
    CHECK(savectl_status_frame_submitted(f.s,model.request_id+1,150)==SAVECTL_INVALID);
    CHECK(savectl_status_frame_submitted(f.s,model.request_id,99)==SAVECTL_INVALID);
    atomic_store(&backend.release,true); wait_controller(&f);
    CHECK(atomic_load(&session.preparations)==1 && atomic_load(&session.releases)==1);
    model=savectl_get_model(f.s);
    CHECK(model.saving_frame_pending && !strcmp(model.status,"saving"));
    CHECK(model.state==(failure ? SAVECTL_FAILED : SAVECTL_SAVING));
    UI_NO_IO(CHECK(savectl_status_frame_submitted(f.s,model.request_id,200)==0));
    CHECK(!savectl_get_model(f.s).saving_frame_pending && savectl_get_model(f.s).saving_submitted_ns==200);
    CHECK(savectl_status_frame_submitted(f.s,model.request_id,201)==SAVECTL_INVALID);
    if (failure) {
        CHECK(model.journal_error==JOURNAL_NOMEM); contents(f.path,"old");
    } else {
        CHECK(model.needs_finish); contents(f.path,"old!");
        /* Complete generated session exists on disk after releasing its storage. */
        piece_allocator allocator=piece_default_allocator(); piece_tree *other=piece_create(&allocator); CHECK(other);
        journal_replay_result rr; CHECK(journal_replay_file(logpath,restore_other,other,&rr)==0);
        uint8_t byte=0; CHECK(piece_read(other,0,&byte,1)==0 && byte=='x'); piece_destroy(other);
        char retained[4097]; strcpy(retained,savectl_save_token(f.s)->previous_path);
        /* Test teardown resolves recovery explicitly, rather than pretending a
         * partial one-buffer checkpoint is an application finish. */
        CHECK(unlink(retained)==0);
    }
    journal_close(j); work_pool_shutdown(jp); free(jp); CHECK(unlink(logpath)==0); finish_fixture(&f);
    puts("savectl bounded session request/worker preparation/submitted-status hook: ok");
}
static void flood(work_ctx *ctx)
{
    work_msg m={.kind=1};
    for (unsigned i=0;i<WORK_MAILBOX_CAP;++i) CHECK(work_publish(ctx,&m));
}
static void discard_foreign(const work_msg *message, void *ctx)
{ (void)message; (void)ctx; }
static void mailbox_loss(void)
{
    fixture f; init(&f,"old");
    work_handle h=work_submit(f.pool,(work_job){flood,NULL,0,WORK_BULK}); CHECK(h.epoch);
    while (atomic_load(&f.pool->slots[h.slot].busy)) nap();
    edit(&f); CHECK(savectl_save(f.s,f.tree,NULL,NULL,0)==0);
    for (unsigned i=0;i<100000u && !atomic_load(&f.pool->dropped_full);++i) nap();
    work_mailbox_drain(f.pool,discard_foreign,NULL); wait_controller(&f);
    CHECK(savectl_get_model(f.s).state==SAVECTL_SAVED);
    /* Result publication precedes notification and its diagnostic counters. */
    for (unsigned i=0;i<WORK_MAX_JOBS;++i)
        while (atomic_load(&f.pool->slots[i].busy)) nap();
    CHECK(atomic_load(&f.pool->dropped_full)>=1);
    contents(f.path,"old!"); finish_fixture(&f);
}

static void decode_file(const work_msg *message, void *ctx);
static void mailbox_terminal_protocol(void)
{
    fixture f; init(&f,"old");
    work_handle filled=work_submit(f.pool,(work_job){flood,NULL,0,WORK_BULK}); CHECK(filled.epoch);
    while (!work_handle_finished(f.pool,filled)) nap();
    edit(&f); CHECK(savectl_save(f.s,f.tree,NULL,NULL,0)==0);
    for (unsigned i=0;i<100000u && !atomic_load(&f.pool->dropped_full);++i) nap();
    CHECK(atomic_load(&f.pool->dropped_full));
    savectl_tick(f.s);
    CHECK(savectl_get_model(f.s).state==SAVECTL_SAVING && savectl_get_model(f.s).busy);
    /* Full result retry must yield the sole bulk lane to unrelated work. */
    pause_job p={0};
    CHECK(work_submit(f.pool,(work_job){blocker,&p,0,WORK_BULK}).epoch); pause_wait(&p);
    atomic_store(&p.release,true);
    /* Draining foreign traffic creates room for the guaranteed terminal lease. */
    for (unsigned i=0;i<100000u && savectl_get_model(f.s).busy;++i) {
        work_mailbox_drain(f.pool,decode_file,NULL); savectl_tick(f.s); nap();
    }
    CHECK(savectl_get_model(f.s).state==SAVECTL_SAVED && !savectl_get_model(f.s).modified);
    contents(f.path,"old!"); finish_fixture(&f);
    puts("savectl terminal completion requires mailbox and yields on saturation: ok");
}

static int source_io_failure(void *ctx)
{
    unsigned *calls=ctx; ++*calls;
    return *calls==1 ? FILE_OK : FILE_ERR_IO;
}
static void guard_error(void)
{
    fixture f; init(&f,"old"); CHECK(savectl_destroy(f.s)==0);
    unsigned calls=0;
    savectl_options options={.pool=f.pool,.path=f.path,.baseline=base_id(f.path),
        .source_mode=FILE_MODE_MMAP,.validate_source=source_io_failure,.source_ctx=&calls};
    CHECK(savectl_create(&f.s,&options,false)==0); edit(&f);
    UI_NO_IO(CHECK(savectl_save(f.s,f.tree,NULL,NULL,0)==0)); wait_controller(&f);
    CHECK(savectl_get_model(f.s).file_error==FILE_ERR_IO);
    CHECK(savectl_get_model(f.s).modified); contents(f.path,"old"); finish_fixture(&f);
}

static void pool_refusal(void)
{
    fixture f; init(&f,"old"); pause_job p={0};
    CHECK(work_submit(f.pool,(work_job){blocker,&p,0,WORK_BULK}).epoch); pause_wait(&p);
    for (unsigned i=1;i<WORK_MAX_JOBS;++i) CHECK(work_submit(f.pool,(work_job){blocker,&p,0,WORK_BULK}).epoch);
    edit(&f);
    UI_NO_IO(CHECK(savectl_save(f.s,f.tree,NULL,NULL,0)==SAVECTL_POOL));
    CHECK(savectl_get_model(f.s).modified && savectl_get_model(f.s).state==SAVECTL_IDLE);
    atomic_store(&p.release,true);
    finish_fixture(&f);
}
static void typing_notifications(void)
{
    fixture f; init(&f,"old");
    edit_malloc_guard_begin();
    for (unsigned i=0;i<10000u;++i) {
        UI_NO_IO(savectl_modified(f.s));
        UI_NO_IO(savectl_set_view(f.s,(savectl_view){i,i,i,i}));
        UI_NO_IO(savectl_tick(f.s));
        UI_NO_IO(CHECK(savectl_get_model(f.s).modified));
    }
    CHECK(edit_malloc_guard_end()==0); finish_fixture(&f);
}

static void *owner_alloc(void *ctx, size_t size)
{
    pthread_t *owner=ctx; CHECK(pthread_equal(pthread_self(),*owner));
    piece_allocator a=piece_default_allocator(); return a.alloc(a.ctx,size);
}
static void owner_free(void *ctx, void *p, size_t size)
{
    (void)ctx; piece_allocator a=piece_default_allocator(); a.free(a.ctx,p,size);
}
static void reload_ui_tree_owner(void)
{
    fixture f; init(&f,"old"); CHECK(savectl_destroy(f.s)==0);
    pthread_t owner=pthread_self();
    savectl_options options={.pool=f.pool,.path=f.path,.baseline=base_id(f.path),.source_mode=FILE_MODE_COPY,
        .reload_allocator={.ctx=&owner,.alloc=owner_alloc,.free=owner_free}};
    CHECK(savectl_create(&f.s,&options,false)==0);
    CHECK(savectl_reload(f.s)==0); wait_controller(&f);
    piece_tree *next=NULL; savectl_view view;
    UI_NO_IO(CHECK(savectl_take_reload(f.s,&next,&view)==0));
    piece_destroy(f.tree); f.tree=next; finish_fixture(&f);
}

static void logical_close(void)
{
    fixture f; init(&f,"old"); pause_job p={.step=FILE_STEP_FSYNCED}; recreate(&f,&p); edit(&f);
    UI_NO_IO(CHECK(savectl_save(f.s,f.tree,NULL,NULL,0)==0)); pause_wait(&p);
    UI_NO_IO(savectl_close_begin(f.s));
    CHECK(savectl_destroy(f.s)==SAVECTL_BUSY); CHECK(savectl_reload(f.s)==SAVECTL_BUSY);
    atomic_store(&p.release,true); wait_controller(&f); contents(f.path,"old"); finish_fixture(&f);
}

static void edit_after_reload_publication(void)
{
    fixture f; init(&f,"old"); put(f.path,"changed"); savectl_file_event(f.s); wait_controller(&f);
    edit(&f); wait_controller(&f);
    CHECK(savectl_get_model(f.s).can_keep);
    CHECK(savectl_keep(f.s)==0); wait_controller(&f);
    CHECK(savectl_get_model(f.s).modified && !savectl_get_model(f.s).banner);
    finish_fixture(&f);
}
static void creation_permissions(uint32_t mode, bool valid, uint32_t expected)
{
    fixture f; init(&f, "new"); CHECK(savectl_destroy(f.s) == 0); CHECK(unlink(f.path) == 0);
    savectl_options options = {.pool=f.pool, .path=f.path, .source_mode=FILE_MODE_COPY,
        .reload_allocator=piece_default_allocator(), .create_mode=mode, .create_mode_valid=valid};
    CHECK(savectl_create(&f.s, &options, true) == 0);
    mode_t previous_mask = umask(0777);
    UI_NO_IO(CHECK(savectl_save(f.s, f.tree, NULL, NULL, 0) == 0)); wait_controller(&f);
    (void)umask(previous_mask);
    CHECK(savectl_get_model(f.s).state == SAVECTL_SAVED && !savectl_get_model(f.s).modified);
    CHECK(base_id(f.path).mode == expected);
    finish_fixture(&f);
}
static void decode_file(const work_msg *message, void *ctx)
{ (void)ctx; file_msg decoded; (void)file_msg_decode(message,&decoded); }
static void mapped_file_source(unsigned scenario)
{
    fixture f; init(&f,"old"); CHECK(savectl_destroy(f.s)==0);
    int writer=open(f.path,O_RDWR); CHECK(writer>=0);
    const off_t size=scenario ? 8*1024*1024 : 256*1024*1024; CHECK(ftruncate(writer,size)==0);
    struct stat initial; CHECK(fstat(writer,&initial)==0);
    file *opened=NULL; file_open_opts open_options={.copy_threshold=1};
    CHECK(file_open_begin(f.pool,f.path,&open_options,&opened)==FILE_OK);
    for (unsigned i=0;i<100000u && !file_open_ready(opened);++i) {
        work_mailbox_drain(f.pool,decode_file,NULL); nap();
    }
    CHECK(file_open_ready(opened) && file_open_mode(opened)==FILE_MODE_MMAP);
    piece_destroy(f.tree); piece_allocator allocator=piece_default_allocator();
    f.tree=piece_create(&allocator); CHECK(f.tree && file_attach(opened,f.tree)==FILE_OK);
    file_source *source=NULL; CHECK(file_source_acquire(opened,&source)==FILE_OK);
    CHECK(file_source_identity(source)->metadata_valid);
    CHECK(file_source_identity(source)->ino==(uint64_t)initial.st_ino);
    savectl_options o={.pool=f.pool,.path=f.path,.source_mode=FILE_MODE_MMAP,
        .source=source,.reload_allocator=piece_default_allocator()};
    CHECK(savectl_create(&f.s,&o,true)==0);
    file_close(opened); file_source_release(source); /* controller owns its lease */
    if (scenario==1) {
        CHECK(unlink(f.path)==0); put(f.path,"replacement");
        savectl_file_event(f.s); wait_controller(&f);
        CHECK(savectl_get_model(f.s).banner);
        CHECK(savectl_keep(f.s)==0); wait_controller(&f);
        CHECK(savectl_get_model(f.s).file_error==FILE_ERR_CHANGED);
    } else {
        if (scenario==2) {
            CHECK(pwrite(writer,"X",1,0)==1);
            struct timespec times[2]={initial.st_atim,initial.st_mtim};
            CHECK(futimens(writer,times)==0);
        } else if (scenario==3) {
            CHECK(ftruncate(writer,4096)==0);
            uint8_t byte; CHECK(piece_read(f.tree,(uint64_t)size-1,&byte,1)==0);
            CHECK(ftruncate(writer,size)==0);
            struct timespec times[2]={initial.st_atim,initial.st_mtim};
            CHECK(futimens(writer,times)==0);
        }
        CHECK(savectl_save(f.s,f.tree,NULL,NULL,0)==0); wait_controller(&f);
        if (scenario) CHECK(savectl_get_model(f.s).file_error==FILE_ERR_CHANGED && savectl_get_model(f.s).modified);
        else CHECK(savectl_get_model(f.s).state==SAVECTL_SAVED && !savectl_get_model(f.s).modified);
    }
    CHECK(close(writer)==0); finish_fixture(&f);
}

typedef struct failing_storage { edit_arena arena; size_t calls, fail_at; } failing_storage;
static void *fail_allocate(void *ctx, size_t size)
{
    failing_storage *storage=ctx;
    if (++storage->calls==storage->fail_at) return NULL;
    return edit_arena_alloc(&storage->arena,size,16);
}
static void fail_free(void *ctx, void *ptr, size_t size)
{ (void)ctx; (void)ptr; (void)size; }
static size_t reload_mark(void *ctx)
{ failing_storage *storage=ctx; return edit_arena_mark(&storage->arena); }
static void reload_reset(void *ctx, size_t mark)
{ failing_storage *storage=ctx; edit_arena_reset_to_mark(&storage->arena,mark); }
static void reload_allocation_recovery(void)
{
    bool reached_success=false;
    for (size_t fail_at=1;fail_at<64u;++fail_at) {
        fixture f; init(&f,"old"); CHECK(savectl_destroy(f.s)==0);
        failing_storage storage={.fail_at=fail_at};
        CHECK(edit_arena_init(&storage.arena,256u*1024u)==0);
        piece_allocator allocator={&storage,fail_allocate,fail_free};
        savectl_options o={.pool=f.pool,.path=f.path,.baseline=base_id(f.path),
            .source_mode=FILE_MODE_COPY,.reload_allocator=allocator,
            .reload_mark=reload_mark,.reload_reset=reload_reset};
        CHECK(savectl_create(&f.s,&o,true)==0);
        int fd=open(f.path,O_WRONLY|O_TRUNC); CHECK(fd>=0);
        CHECK(ftruncate(fd,16*1024*1024)==0 && close(fd)==0);
        CHECK(savectl_reload(f.s)==0); wait_controller(&f);
        piece_tree *replacement=NULL; savectl_view view={3,2,1,4};
        int rc;
        do { rc=savectl_take_reload(f.s,&replacement,&view); } while (rc==SAVECTL_BUSY);
        if (rc==SAVECTL_OK) {
            CHECK(storage.calls<fail_at); piece_destroy(replacement); reached_success=true;
        } else {
            CHECK(rc==SAVECTL_NOMEM && !replacement);
            savectl_model m=savectl_get_model(f.s);
            CHECK(m.state==SAVECTL_FAILED && m.file_error==FILE_ERR_NOMEM);
            CHECK(m.modified && m.banner);
            CHECK(view.cursor==3 && view.anchor==2 && view.scroll_byte==1 && view.scroll_x==4);
            wait_controller(&f);
            CHECK(storage.arena.used==0);
            m=savectl_get_model(f.s); CHECK(m.can_reload && m.can_keep);
            /* Retry the same tab after freeing the exclusive reservation. */
            storage.fail_at=0; storage.calls=0;
            CHECK(savectl_reload(f.s)==0); wait_controller(&f);
            do { rc=savectl_take_reload(f.s,&replacement,&view); } while (rc==SAVECTL_BUSY);
            CHECK(rc==SAVECTL_OK && replacement);
            piece_destroy(replacement);
        }
        finish_fixture(&f); edit_arena_free(&storage.arena);
        if (reached_success) break;
    }
    CHECK(reached_success);
    puts("savectl every reload construction allocation: rollback/retry: ok");
}

typedef struct allocation_count { size_t calls, live; } allocation_count;
static void *count_allocate(void *ctx, size_t size)
{
    allocation_count *count=ctx; ++count->calls;
    void *p=malloc(size); if (p) count->live+=size; return p;
}
static void count_free(void *ctx, void *p, size_t size)
{
    allocation_count *count=ctx; CHECK(count->live>=size); count->live-=size; free(p);
}
static uint64_t test_ns(void)
{
    struct timespec now; CHECK(clock_gettime(CLOCK_MONOTONIC,&now)==0);
    return (uint64_t)now.tv_sec*UINT64_C(1000000000)+(uint64_t)now.tv_nsec;
}
static void reload_construction_slices(void)
{
    fixture f; init(&f,"old"); CHECK(savectl_destroy(f.s)==0);
    allocation_count count={0};
    piece_allocator allocator={&count,count_allocate,count_free};
    savectl_options o={.pool=f.pool,.path=f.path,.baseline=base_id(f.path),
        .source_mode=FILE_MODE_COPY,.reload_allocator=allocator};
    CHECK(savectl_create(&f.s,&o,false)==0);
    int fd=open(f.path,O_WRONLY|O_TRUNC); CHECK(fd>=0);
    const off_t size=64*1024*1024; CHECK(ftruncate(fd,size)==0 && close(fd)==0);
    CHECK(savectl_reload(f.s)==0); wait_controller(&f);
    piece_tree *replacement=NULL; savectl_view view={3,2,1,4};
    uint64_t start=test_ns(); int rc=savectl_take_reload(f.s,&replacement,&view);
    uint64_t maximum=test_ns()-start;
    printf("reload first UI slice=%llu ns (M)[AC], allocations=%zu (M)[AC]\n",
        (unsigned long long)maximum,count.calls); fflush(stdout);
    CHECK(rc==SAVECTL_BUSY && !replacement);
    CHECK(view.cursor==3 && view.anchor==2 && view.scroll_byte==1 && view.scroll_x==4);
    CHECK(count.calls<=32u);
    size_t slices=1;
    do {
        size_t before=count.calls; start=test_ns();
        rc=savectl_take_reload(f.s,&replacement,&view);
        uint64_t elapsed=test_ns()-start; if (elapsed>maximum) maximum=elapsed;
        CHECK(count.calls-before<=32u); ++slices;
    } while (rc==SAVECTL_BUSY && slices<100u);
    CHECK(rc==SAVECTL_OK && replacement && piece_len(replacement)==(uint64_t)size);
    printf("reload UI slices=%zu, max=%llu ns (M)[AC]\n",slices,(unsigned long long)maximum);
    piece_destroy(f.tree); f.tree=replacement; finish_fixture(&f);
    CHECK(count.live==0);
    puts("savectl reload UI construction yields bounded slices: ok");
}

static uint64_t anonymous_bytes(void)
{
    FILE *status=fopen("/proc/self/smaps_rollup","r"); CHECK(status);
    char line[256]; unsigned long long kb=0; bool found=false;
    while (fgets(line,sizeof line,status)) {
        if (sscanf(line,"Anonymous: %llu kB",&kb)==1) { found=true; break; }
    }
    CHECK(fclose(status)==0 && found); return (uint64_t)kb*1024u;
}
static void reload_memory_bound(void)
{
    fixture f; init(&f,"old"); CHECK(savectl_destroy(f.s)==0);
    savectl_options o={.pool=f.pool,.path=f.path,.baseline=base_id(f.path),
        .source_mode=FILE_MODE_COPY,.reload_allocator=piece_default_allocator(),
        .reload_copy_threshold=FILE_PREFIX_MAX};
    CHECK(savectl_create(&f.s,&o,false)==0);
    piece_snapshot *old=piece_snapshot_take(f.tree); CHECK(old);
    const off_t size=16*1024*1024;
    int writer=open(f.path,O_WRONLY|O_TRUNC); CHECK(writer>=0);
    CHECK(ftruncate(writer,size)==0);
    CHECK(pwrite(writer,"Z",1,size-1)==1 && close(writer)==0);
    uint64_t before=anonymous_bytes();
    savectl_file_event(f.s); wait_controller(&f);
    CHECK(savectl_get_model(f.s).state==SAVECTL_RELOADING);
    uint64_t after=anonymous_bytes();
    printf("reload anonymous ownership delta=%llu bytes (M)[AC], bound=%u bytes (G)\n",
        (unsigned long long)(after>before ? after-before : 0),2u*FILE_PREFIX_MAX);
    fflush(stdout);
    CHECK(after<=before+2u*FILE_PREFIX_MAX);
    piece_tree *replacement=NULL; savectl_view view;
    int rc; do { rc=savectl_take_reload(f.s,&replacement,&view); } while (rc==SAVECTL_BUSY);
    CHECK(rc==SAVECTL_OK);
    uint8_t byte=0; CHECK(piece_read(replacement,(uint64_t)size-1,&byte,1)==0 && byte=='Z');
    /* A live old snapshot and the new backing may overlap; destroying their
     * trees must preserve each independent backing until its last owner. */
    piece_destroy(f.tree); f.tree=replacement;
    CHECK(piece_snapshot_read(old,0,&byte,1)==0 && byte=='o');
    piece_snapshot *newer=piece_snapshot_take(replacement); CHECK(newer);
    finish_fixture(&f);
    CHECK(piece_snapshot_read(newer,(uint64_t)size-1,&byte,1)==0 && byte=='Z');
    piece_snapshot_release(newer); piece_snapshot_release(old);
    puts("savectl reload mapping budget/overlapping snapshots: ok");
}

static void ignore_message(const work_msg *message, void *ctx)
{ (void)message; (void)ctx; }
static void destroy_reused_slot(void)
{
    fixture f; init(&f,"old");
    savectl_file_event(f.s); wait_controller(&f);
    /* Release message reservations too, then force the first free slot to be
     * used by a completely unrelated physical worker lease. */
    for (unsigned i=0;i<WORK_MAX_JOBS;++i)
        while (atomic_load(&f.pool->slots[i].busy)) nap();
    work_mailbox_drain(f.pool,ignore_message,NULL);
    pause_job p={0};
    work_handle unrelated=work_submit(f.pool,(work_job){blocker,&p,0,WORK_BULK});
    CHECK(unrelated.epoch && unrelated.slot==0); pause_wait(&p);
    CHECK(savectl_destroy(f.s)==SAVECTL_OK); f.s=NULL;
    CHECK(!work_handle_finished(f.pool,unrelated));
    atomic_store(&p.release,true); work_pool_shutdown(f.pool); free(f.pool);
    piece_destroy(f.tree); CHECK(unlink(f.path)==0 && rmdir(f.dir)==0);
    puts("savectl destroy ignores unrelated reused work slot: ok");
}

static void undo_clean_identity(void)
{
    fixture f; init(&f,"base");
    savectl_content_identity(f.s,10,10,true);
    CHECK(!savectl_get_model(f.s).modified);
    CHECK(savectl_save(f.s,f.tree,NULL,NULL,0)==0); wait_controller(&f);
    CHECK(piece_insert(f.tree,4,(const uint8_t *)"!",1)==0);
    savectl_content_identity(f.s,11,10,true);
    CHECK(savectl_get_model(f.s).modified);
    CHECK(piece_delete(f.tree,4,1,NULL)==0);
    savectl_content_identity(f.s,10,10,true);
    CHECK(!savectl_get_model(f.s).modified);
    /* Branch identities cannot alias a discarded redo state. Unknown saved
     * history remains dirty even if a numeric cursor happens to be equal. */
    savectl_content_identity(f.s,12,10,true); CHECK(savectl_get_model(f.s).modified);
    savectl_content_identity(f.s,10,10,false); CHECK(savectl_get_model(f.s).modified);
    pause_job p={.step=FILE_STEP_FSYNCED}; recreate(&f,&p);
    savectl_content_identity(f.s,20,10,true);
    CHECK(savectl_save(f.s,f.tree,NULL,NULL,0)==0); pause_wait(&p);
    savectl_content_identity(f.s,21,10,true);
    savectl_content_identity(f.s,20,10,true);
    atomic_store(&p.release,true); wait_controller(&f);
    CHECK(!savectl_get_model(f.s).modified);
    /* Returned-to-clean content gets silent reload on an external change. */
    put(f.path,"external"); savectl_file_event(f.s); wait_controller(&f);
    CHECK(savectl_get_model(f.s).state==SAVECTL_RELOADING);
    piece_tree *replacement=NULL; savectl_view view;
    CHECK(savectl_take_reload(f.s,&replacement,&view)==0);
    piece_destroy(f.tree); f.tree=replacement;
    finish_fixture(&f); puts("savectl undo clean identity/branch/eviction/save cutoff: ok");
}

static void journal_base_conflict(bool notified)
{
    fixture f; init(&f,"old");
    char logpath[160]; CHECK(snprintf(logpath,sizeof logpath,"%s/journal",f.dir)>0);
    work_pool *jp=aligned_alloc(_Alignof(work_pool),sizeof *jp); CHECK(jp); CHECK((uintptr_t)jp % _Alignof(work_pool) == 0); CHECK(work_pool_init(jp,1,0)==0);
    journal *j=NULL; CHECK(journal_open(&j,logpath,jp,NULL)==0);
    journal_base previous; CHECK(journal_capture_base(f.path,&previous)==0);
    uint8_t bp[4137], insert[9]={0}; le64(insert,3); insert[8]='!';
    journal_record cp[]={{JOURNAL_BASE,1,0,bp,base_payload(bp,&previous)},
        {JOURNAL_INSERT,1,0,insert,sizeof insert}};
    CHECK(journal_set_base(j,1,&previous)==0);
    CHECK(savectl_destroy(f.s)==0);
    savectl_options o={.pool=f.pool,.path=f.path,.baseline=base_id(f.path),
        .source_mode=FILE_MODE_COPY,.reload_allocator=piece_default_allocator(),
        .journal=j,.journal_pool=jp,.buffer_id=1};
    CHECK(savectl_create(&f.s,&o,false)==0); edit(&f);
    put(f.path,"external");
    if (notified) savectl_file_event(f.s);
    CHECK(savectl_save(f.s,f.tree,&previous,cp,2)==0); wait_controller(&f);
    savectl_model m=savectl_get_model(f.s);
    CHECK(m.banner && m.modified && m.can_keep && m.can_reload);
    CHECK(m.state==SAVECTL_EXTERNAL_MODIFIED);
    CHECK(m.journal_error==JOURNAL_BASE_CHANGED && m.file_error==FILE_OK);
    const journal_save *token=savectl_save_token(f.s);
    CHECK(token && !token->prepared && !token->previous_path[0]);
    contents(f.path,"external");
    CHECK(savectl_keep(f.s)==0); wait_controller(&f);
    CHECK(!savectl_get_model(f.s).banner && savectl_get_model(f.s).modified);
    journal_close(j); work_pool_shutdown(jp); free(jp);
    CHECK(unlink(logpath)==0); finish_fixture(&f);
    puts("savectl journal BASE_CHANGED exposes external recovery: ok");
}

static void reload_fifo(void)
{
    fixture f; init(&f,"old"); pause_job p={0};
    work_handle held=work_submit(f.pool,(work_job){blocker,&p,0,WORK_BULK});
    CHECK(held.epoch); pause_wait(&p);
    CHECK(savectl_reload(f.s)==0);
    CHECK(unlink(f.path)==0 && mkfifo(f.path,0600)==0);
    atomic_store(&p.release,true);
    /* Bounded progress assertion: never open a writer to unblock acquisition. */
    for (unsigned i=0;i<2000u && savectl_get_model(f.s).busy;++i) {
        savectl_tick(f.s); nap();
    }
    CHECK(!savectl_get_model(f.s).busy);
    CHECK(savectl_get_model(f.s).file_error==FILE_ERR_NOTREG);
    piece_tree *replacement=NULL; savectl_view view;
    CHECK(savectl_take_reload(f.s,&replacement,&view)==SAVECTL_BUSY && !replacement);
    CHECK(piece_len(f.tree)==3);
    finish_fixture(&f);
    puts("savectl reload regular-to-FIFO rejects without writer: ok");
}

static void reuse_receive(const work_msg *message, void *ctx)
{ (void)savectl_receive(ctx, message); }
typedef struct reuse_probe { _Atomic bool entered, release; } reuse_probe;
static void reuse_probe_worker(work_ctx *ctx)
{
    reuse_probe *probe = ctx->arg; atomic_store(&probe->entered, true);
    while (!atomic_load(&probe->release)) (void)sched_yield();
}
/* Regression for #12, fixed on main: completed controller work slots
 * may belong to unrelated running/queued epochs at close. */
static void known_unrelated_slot_reuse(void)
{
    fixture f; init(&f, "base"); edit(&f);
    CHECK(savectl_save(f.s, f.tree, NULL, NULL, 0) == SAVECTL_OK); wait_controller(&f);
    for (size_t i = 0; i < WORK_MAX_JOBS; i++)
        while (atomic_load(&f.pool->slots[i].busy)) (void)sched_yield();
    (void)work_mailbox_drain(f.pool, reuse_receive, f.s);
    reuse_probe probe = {0}; work_handle handles[WORK_MAX_JOBS]; size_t n = 0;
    for (; n < WORK_MAX_JOBS; n++) {
        handles[n] = work_submit(f.pool, (work_job){reuse_probe_worker, &probe, 90, WORK_BULK});
        if (!handles[n].epoch) break;
    }
    CHECK(n > 0); while (!atomic_load(&probe.entered)) (void)sched_yield();
    savectl_close_begin(f.s);
    int code = savectl_destroy(f.s);
    printf("KNOWN unrelated slot reuse: destroy=%d expected=0 queued=%zu\n", code, n); fflush(stdout);
    CHECK(code == SAVECTL_OK);
    f.s = NULL; atomic_store(&probe.release, true);
    for (size_t i = 0; i < n; i++) while (atomic_load(&f.pool->slots[handles[i].slot].busy)) (void)sched_yield();
    work_pool_shutdown(f.pool); free(f.pool); piece_destroy(f.tree);
    CHECK(unlink(f.path) == 0 && rmdir(f.dir) == 0);
}
int main(int argc, char **argv)
{
    trace_init();
    if (argc>1 && !strcmp(argv[1],"--bounded-request")) { bounded_request_status(false); bounded_request_status(true); return 0; }
    if (argc>1 && !strcmp(argv[1],"--finish-recovery")) { finish_writeback_recovery(); return 0; }
    if (argc>1 && !strcmp(argv[1],"--journal-handoff")) { journal_handoff_crash(); return 0; }
    if (argc>1 && !strcmp(argv[1],"--fifo")) { reload_fifo(); return 0; }
    if (argc>1 && !strcmp(argv[1],"--journal-conflict")) { journal_base_conflict(false); journal_base_conflict(true); return 0; }
    if (argc>1 && !strcmp(argv[1],"--undo-clean")) { undo_clean_identity(); return 0; }
    if (argc>1 && !strcmp(argv[1],"--reused-slot")) { destroy_reused_slot(); return 0; }
    if (argc>1 && !strcmp(argv[1],"--reload-memory")) { reload_memory_bound(); return 0; }
    if (argc>1 && !strcmp(argv[1],"--reload-slices")) { reload_construction_slices(); return 0; }
    if (argc>1 && !strcmp(argv[1],"--reload-failure")) { reload_allocation_recovery(); return 0; }
    if (argc>1 && !strcmp(argv[1],"--mailbox-protocol")) { mailbox_terminal_protocol(); return 0; }
    if (argc>1 && !strcmp(argv[1],"--file-source")) { for (unsigned i=0;i<4u;++i) mapped_file_source(i); puts("savectl retained actual mapped identity/guard: ok"); return 0; }
    journal_handoff_crash(); finish_writeback_recovery(); bounded_request_status(false); bounded_request_status(true);
    mailbox_terminal_protocol();
    for (unsigned i=0;i<4u;++i) mapped_file_source(i);
    reload_allocation_recovery(); reload_construction_slices(); reload_memory_bound(); destroy_reused_slot(); undo_clean_identity();
    reload_fifo(); journal_base_conflict(false); journal_base_conflict(true);
    if (getenv("EDIT_YQU_KNOWN_FAILURES")) known_unrelated_slot_reuse();
    notification_orders_completion();
    puts("savectl normal-build I/O and held-publication guard: ok");
    creation_permissions(0, true, 0);
    creation_permissions(0, false, 0644);
    creation_permissions(0777, false, 0644);
    creation_permissions(0571, true, 0571);
    puts("savectl creation permissions/default/umask: ok");
    basic(); ack_queued(); save_states(); failed_io();
    race(true,false); race(false,false); race(true,true);
    puts("savectl §2.14 save/external-change races (notified, unnotified, restored identity): ok");
    reload_offsets(false,false); reload_offsets(true,false); reload_offsets(false,true);
    during_reload_edit(); mapped_requires_guard();
    puts("savectl external banner/silent reload/latest offsets/edit protection: ok");
    journal_transaction(false,false); journal_transaction(true,false); journal_transaction(false,true);
    puts("savectl journal prepare/finish/replay/failures/retained generation: ok");
    edit_after_reload_publication(); logical_close(); reload_ui_tree_owner(); guard_error(); pool_refusal(); typing_notifications(); mailbox_loss(); puts("savectl full-mailbox terminal publication retry: ok");
    puts("savectl_test: ok"); return 0;
}
