#ifndef EDIT_SAVECTL_WIN_FIXTURE_H
#define EDIT_SAVECTL_WIN_FIXTURE_H
#include "savectl/savectl.h"
#include "os/os.h"
#include "../core_sync.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"savectl_win:%d: %s\n",__LINE__,#x); exit(1); } } while(0)

/* Unit isolation until W2.1/W2.2 are in WIN_MODULES. Unavailable save/journal
 * operations fail the test if reached. Reload exercises production savectl
 * and real os.h files, mappings, work pool and anonymous NTFS scratch. */
#if !defined(EDIT_WIN_SAVECTL_REAL_DEPS)
#define file_id_diff savectl_test_id_diff
#define file_id_stat_path savectl_test_id_stat_path
#define file_default_copy_threshold savectl_test_copy_threshold
#define file_source_retain savectl_test_source_retain
#define file_source_release savectl_test_source_release
#define file_source_identity savectl_test_source_identity
#define file_source_mode savectl_test_source_mode
#define file_source_validate savectl_test_source_validate
#define file_source_adopt_retired savectl_test_source_adopt_retired
#define file_save_write savectl_test_save_write
#define journal_save_prepare savectl_test_prepare
#define journal_save_cancel savectl_test_cancel
#define journal_save_finish savectl_test_finish
#define journal_capture_base savectl_test_capture
#define journal_flush savectl_test_flush
#define journal_rotate savectl_test_rotate
static file_id savectl_test_identity(const os_stat *st)
{
    return (file_id){.dev=st->id.volume,.ino=st->id.index[0],.size=st->size,
        .mtime_ns=(uint64_t)st->mtime_ns,.ctime_ns=(uint64_t)st->ctime_ns,
        .mode=st->perm,.uid=st->uid,.gid=st->gid,.exists=1,.metadata_valid=1,
        .type=st->kind==OS_KIND_FILE?S_IFREG:S_IFDIR};
}
uint32_t savectl_test_id_diff(const file_id *a, const file_id *b)
{
    if(a->exists!=b->exists) return FILE_CHG_GONE;
    if(!a->exists) return FILE_CHG_NONE;
    return a->dev==b->dev && a->ino==b->ino && a->size==b->size &&
        a->mtime_ns==b->mtime_ns && a->ctime_ns==b->ctime_ns && a->mode==b->mode &&
        a->type==b->type ? FILE_CHG_NONE : FILE_CHG_METADATA;
}
int savectl_test_id_stat_path(const char *path, file_id *id)
{
    os_stat st; os_err rc=os_stat_path(path,OS_STAT_NOFOLLOW,&st);
    *id=(file_id){0};
    if(rc==OS_E_NOENT || rc==OS_E_NOTDIR) return FILE_OK;
    if(rc!=OS_OK) return FILE_ERR_IO;
    *id=savectl_test_identity(&st); return FILE_OK;
}
uint64_t savectl_test_copy_threshold(void) { return FILE_PREFIX_MAX; }
file_source *savectl_test_source_retain(file_source *s) { (void)s; CHECK(false); return NULL; }
void savectl_test_source_release(file_source *s) { CHECK(!s); }
const file_id *savectl_test_source_identity(const file_source *s) { (void)s; CHECK(false); return NULL; }
file_mode savectl_test_source_mode(const file_source *s) { (void)s; CHECK(false); return FILE_MODE_COPY; }
int savectl_test_source_validate(void *s) { (void)s; CHECK(false); return FILE_ERR_STATE; }
int savectl_test_source_adopt_retired(file_source *s, const file_id *id) { (void)s; (void)id; CHECK(false); return FILE_ERR_STATE; }
int savectl_test_save_write(file_save_args *a) { (void)a; CHECK(false); return FILE_ERR_STATE; }
int savectl_test_prepare(journal *j, uint64_t id, const journal_base *b, const journal_record *r, size_t n, journal_save *s)
{ (void)j; (void)id; (void)b; (void)r; (void)n; (void)s; CHECK(false); return JOURNAL_IO; }
int savectl_test_cancel(journal *j, journal_save *s, const journal_record *r, size_t n)
{ (void)j; (void)s; (void)r; (void)n; CHECK(false); return JOURNAL_IO; }
int savectl_test_finish(journal *j, journal_save *s, const journal_base *b, const journal_record *r, size_t n)
{ (void)j; (void)s; (void)b; (void)r; (void)n; CHECK(false); return JOURNAL_IO; }
int savectl_test_capture(const char *p, journal_base *b)
{ (void)p; (void)b; CHECK(false); return JOURNAL_IO; }
int savectl_test_flush(journal *j) { (void)j; CHECK(false); return JOURNAL_IO; }
int savectl_test_rotate(journal *j, const journal_record *r, size_t n)
{ (void)j; (void)r; (void)n; CHECK(false); return JOURNAL_IO; }
#include "../../src/savectl/savectl.c"
#endif

typedef struct fixture { work_pool *pool; savectl *s; char dir[OS_PATH_MAX], path[OS_PATH_MAX]; } fixture;
static void wait_idle(savectl *s)
{
    /* (G) Reuse IPC's existing request deadline as a fixture hang bound. */
    uint64_t end=os_now_ns()+UINT64_C(5000000000);
    do {
        savectl_tick(s);
        if(!savectl_get_model(s).busy) return;
        os_sleep_ns(UINT64_C(1000000)); /* (G) Existing worker retry poll, 1 ms. */
    } while(os_now_ns()<end);
    CHECK(false);
}
static void put(const char *path, const uint8_t *bytes, size_t len)
{
    os_file f=OS_FILE_INIT;
    CHECK(os_file_open(&f,path,OS_OPEN_WRITE|OS_OPEN_CREATE|OS_OPEN_TRUNC)==OS_OK);
    size_t off=0;
    while(off<len) { size_t n; CHECK(os_file_write(&f,bytes+off,len-off,&n)==OS_OK && n); off+=n; }
    CHECK(os_file_sync(&f)==OS_OK); os_file_close(&f);
}
static void init(fixture *f, const uint8_t *bytes, size_t len)
{
    memset(f,0,sizeof *f); char temp[OS_PATH_MAX];
    CHECK(os_temp_dir(temp,sizeof temp)==OS_OK);
    CHECK(snprintf(f->dir,sizeof f->dir,"%s/sublimite-test-savectl-%llu",temp,(unsigned long long)os_proc_id())>0);
    CHECK(os_mkdir(f->dir)==OS_OK);
    CHECK(snprintf(f->path,sizeof f->path,"%s/target",f->dir)>0);
    put(f->path,bytes,len);
    f->pool=core_aligned_alloc(_Alignof(work_pool),sizeof *f->pool); CHECK(f->pool);
    CHECK(work_pool_init(f->pool,1,0)==0);
    file_id baseline; CHECK(file_id_stat_path(f->path,&baseline)==FILE_OK);
    savectl_options o={.pool=f->pool,.path=f->path,.baseline=baseline,.source_mode=FILE_MODE_COPY,
        .reload_allocator=piece_default_allocator(),.reload_copy_threshold=1};
    CHECK(savectl_create(&f->s,&o,false)==SAVECTL_OK);
}
static void fini(fixture *f)
{
    savectl_close_begin(f->s); wait_idle(f->s);
    uint64_t end=os_now_ns()+UINT64_C(5000000000);
    while(savectl_destroy(f->s)==SAVECTL_BUSY) { CHECK(os_now_ns()<end); savectl_tick(f->s); os_sleep_ns(UINT64_C(1000000)); }
    work_pool_shutdown(f->pool); core_aligned_free(f->pool);
    CHECK(os_unlink(f->path)==OS_OK); CHECK(os_rmdir(f->dir)==OS_OK);
}
#endif
