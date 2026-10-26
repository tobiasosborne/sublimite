#if defined(_WIN32)
#include "savectl_fixture.h"
#if defined(EDIT_WIN_SAVECTL_REAL_DEPS)
#include <windows.h>
#include <stdatomic.h>
typedef struct save_pause { _Atomic bool entered, release; } save_pause;
static void before_rename(void *ctx, int step)
{
    save_pause *pause=ctx;
    if(step!=FILE_STEP_FSYNCED) return;
    atomic_store(&pause->entered,true);
    while(!atomic_load(&pause->release)) os_sleep_ns(UINT64_C(1000000));
}
static void contents(const char *path, const char *expected)
{
    os_file file=OS_FILE_INIT; CHECK(os_file_open(&file,path,OS_OPEN_READ)==OS_OK);
    uint8_t bytes[sizeof "saved"]; size_t n=0;
    CHECK(os_file_read(&file,bytes,sizeof bytes,&n)==OS_OK);
    CHECK(n==strlen(expected) && !memcmp(bytes,expected,n)); os_file_close(&file);
}
static void no_save_temporary(fixture *f)
{
    os_dir directory=OS_DIR_INIT; CHECK(os_dir_open(&directory,f->dir)==OS_OK);
    os_dir_iter it; CHECK(os_dir_iter_open(&it,&directory)==OS_OK); const char *name;
    os_err rc;
    while((rc=os_dir_next(&it,&name))==OS_OK && name) CHECK(!strcmp(name,"target"));
    CHECK(rc==OS_OK); os_dir_iter_close(&it); os_dir_close(&directory);
}
static void production_save(void)
{
    fixture f; init(&f,(const uint8_t *)"old",3);
    piece_allocator allocator=piece_default_allocator(); piece_tree *tree=piece_create(&allocator);
    CHECK(tree && piece_init_copy(tree,(const uint8_t *)"saved",5)==0);
    savectl_modified(f.s);
    WCHAR path[OS_PATH_MAX]; CHECK(MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,f.path,-1,path,OS_PATH_MAX));
    HANDLE blocked=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,NULL,OPEN_EXISTING,0,NULL);
    CHECK(blocked!=INVALID_HANDLE_VALUE);
    CHECK(savectl_save(f.s,tree,NULL,NULL,0)==SAVECTL_OK); wait_idle(f.s);
    savectl_model model=savectl_get_model(f.s);
    /* A sharing violation is an OS failure like any other: FILE_ERR_IO with the
     * native code. FILE_ERR_BUSY means a save job is already in flight (file.h). */
    CHECK(model.state==SAVECTL_FAILED && model.file_error==FILE_ERR_IO &&
          model.err_no==ERROR_SHARING_VIOLATION && model.modified);
    contents(f.path,"old"); no_save_temporary(&f); CHECK(CloseHandle(blocked));
    uint8_t retained[5]; CHECK(piece_read(tree,0,retained,sizeof retained)==0 && !memcmp(retained,"saved",5));
    puts("savectl production Windows sharing violation: IO/ERROR_SHARING_VIOLATION, old target and edits retained, no temporary: ok");

    os_file old=OS_FILE_INIT; CHECK(os_file_open(&old,f.path,OS_OPEN_READ)==OS_OK);
    os_map mapping={0}; CHECK(os_map_file(&mapping,&old,0,3)==OS_OK);
    save_pause pause={0}; CHECK(savectl_set_step_hook(f.s,before_rename,&pause)==SAVECTL_OK);
    CHECK(savectl_save(f.s,tree,NULL,NULL,0)==SAVECTL_OK);
    /* (G) Reuse the existing fixture worker deadline for the durability seam. */
    uint64_t end=os_now_ns()+UINT64_C(5000000000);
    while(!atomic_load(&pause.entered)) { CHECK(os_now_ns()<end); os_sleep_ns(UINT64_C(1000000)); }
    contents(f.path,"old"); CHECK(!memcmp(mapping.addr,"old",3));
    atomic_store(&pause.release,true); wait_idle(f.s);
    model=savectl_get_model(f.s);
    CHECK(model.state==SAVECTL_SAVED && !model.modified && model.file_error==FILE_OK);
    contents(f.path,"saved"); CHECK(!memcmp(mapping.addr,"old",3));
    file_id disk; CHECK(file_id_stat_path(f.path,&disk)==FILE_OK);
    CHECK(file_id_diff(&disk,savectl_target_identity(f.s))==FILE_CHG_NONE);
    no_save_temporary(&f); os_unmap(&mapping); os_file_close(&old);
    piece_destroy(tree); fini(&f);
    puts("savectl production Windows fsynced before rename, mapped old target, bytes/status/baseline: ok");
}
#endif
int main(int argc, char **argv)
{
    if(argc==2 && !strcmp(argv[1],"--require-production")) {
#if defined(EDIT_WIN_SAVECTL_REAL_DEPS)
        production_save(); return 0;
#else
        fputs("savectl production save required: file/journal Windows dependencies unavailable\n",stderr); return 1;
#endif
    }
    fixture f; init(&f,(const uint8_t *)"old",3);
    CHECK(savectl_get_model(f.s).state==SAVECTL_IDLE);
    CHECK(!savectl_get_model(f.s).modified);
    savectl_modified(f.s); CHECK(savectl_get_model(f.s).modified);
    savectl_content_identity(f.s,1,1,true); CHECK(!savectl_get_model(f.s).modified);
    savectl_content_identity(f.s,2,1,true); CHECK(savectl_get_model(f.s).modified);
    puts("savectl Windows native absolute path/lifecycle/content identity: ok");
    put(f.path,(const uint8_t *)"external",8);
    savectl_file_event(f.s); wait_idle(f.s);
    CHECK(savectl_get_model(f.s).banner && savectl_get_model(f.s).can_keep);
    CHECK(savectl_keep(f.s)==SAVECTL_OK); wait_idle(f.s);
    CHECK(!savectl_get_model(f.s).banner && savectl_get_model(f.s).modified);
    puts("savectl Windows check/keep native identity: ok");
    savectl_options bad={.pool=f.pool,.path="relative",.source_mode=FILE_MODE_COPY}; savectl *s=NULL;
    CHECK(savectl_create(&s,&bad,false)==SAVECTL_INVALID);
    bad.path=f.path; bad.reload_cache_dir="relative";
    CHECK(savectl_create(&s,&bad,false)==SAVECTL_INVALID);
    puts("savectl Windows invalid path/cache: ok");
    fini(&f);
#if defined(EDIT_WIN_SAVECTL_REAL_DEPS)
    production_save();
#else
    puts("savectl production save: SKIP W2.1 file and W2.2 journal not in WIN_MODULES; check-savectl-production requires them");
#endif
    puts("savectl_test: ok"); return 0;
}
#endif
