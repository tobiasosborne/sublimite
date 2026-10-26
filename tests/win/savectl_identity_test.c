#if defined(_WIN32)
#include "savectl/savectl.h"
#include "os/os.h"
#include <stdatomic.h>

/* A filesystem can supply two object IDs with equal low words. Inject only
 * the high word at each reload comparison boundary, leaving metadata equal. */
static _Atomic unsigned identity_boundary;
static _Thread_local unsigned descriptor_stats;
static os_err identity_file_stat(const os_file *file, os_stat *st)
{
    os_err rc=os_file_stat(file,st);
    if(rc==OS_OK && atomic_load(&identity_boundary)==2u && ++descriptor_stats==2u)
        st->id.index[1]^=1u;
    return rc;
}
static os_err identity_path_stat(const char *path, unsigned flags, os_stat *st)
{
    os_err rc=os_stat_path(path,flags,st);
    if(rc==OS_OK && atomic_load(&identity_boundary)==1u) st->id.index[1]^=1u;
    return rc;
}
#define os_file_stat identity_file_stat
#define os_stat_path identity_path_stat
#include "savectl_fixture.h"
#undef os_file_stat
#undef os_stat_path

int main(void)
{
    for(unsigned boundary=1;boundary<=2u;boundary++) {
        fixture f; init(&f,(const uint8_t *)"old",3);
        savectl_modified(f.s);
        atomic_store(&identity_boundary,boundary);
        CHECK(savectl_reload(f.s)==SAVECTL_OK); wait_idle(f.s);
        savectl_model model=savectl_get_model(f.s);
        atomic_store(&identity_boundary,0);
        printf("savectl high-half identity boundary %u: file_error=%d\n",boundary,model.file_error);
        CHECK(model.file_error==FILE_ERR_CHANGED && model.modified);
        fini(&f);
    }
    puts("savectl full Windows object identity guards reload: ok");
    return 0;
}
#endif
