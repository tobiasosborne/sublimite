#include "savectl/savectl.h"
#include "trace/trace.h"
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
static void race(bool notification, bool restore_identity)
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
    if (notification && !restore_identity) savectl_file_event(f.s);
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
        int fd=open(path,O_RDONLY); CHECK(fd>=0); CHECK(read(fd,bytes,sizeof bytes)==(ssize_t)b.size); close(fd);
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
    if (fail_prepare) cp[0].buffer_id=2;
    CHECK(savectl_save(f.s,f.tree,&previous,cp,2)==0);
    CHECK(savectl_get_model(f.s).journal_leased);
    if (fail_prepare) {
        wait_controller(&f); CHECK(savectl_get_model(f.s).state==SAVECTL_FAILED);
        CHECK(savectl_get_model(f.s).journal_error==JOURNAL_INVALID);
        CHECK(savectl_get_model(f.s).modified); contents(f.path,"old");
    } else {
        pause_wait(&p);
        /* Caller defers journal records during the exclusive worker lease. */
        edit(&f);
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
static void flood(work_ctx *ctx)
{
    work_msg m={.kind=1};
    for (unsigned i=0;i<WORK_MAILBOX_CAP;++i) CHECK(work_publish(ctx,&m));
}
static void mailbox_loss(void)
{
    fixture f; init(&f,"old");
    work_handle h=work_submit(f.pool,(work_job){flood,NULL,0,WORK_BULK}); CHECK(h.epoch);
    while (atomic_load(&f.pool->slots[h.slot].busy)) nap();
    edit(&f); CHECK(savectl_save(f.s,f.tree,NULL,NULL,0)==0); wait_controller(&f);
    CHECK(savectl_get_model(f.s).state==SAVECTL_SAVED);
    /* Result publication precedes notification and its diagnostic counters. */
    for (unsigned i=0;i<WORK_MAX_JOBS;++i)
        while (atomic_load(&f.pool->slots[i].busy)) nap();
    CHECK(atomic_load(&f.pool->dropped_full)==1);
    contents(f.path,"old!"); finish_fixture(&f);
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
int main(void)
{
    trace_init();
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
    edit_after_reload_publication(); logical_close(); reload_ui_tree_owner(); guard_error(); pool_refusal(); typing_notifications(); mailbox_loss(); puts("savectl full-mailbox completion fallback: ok");
    puts("savectl_test: ok"); return 0;
}
