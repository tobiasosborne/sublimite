#if defined(_WIN32)
#include "savectl_fixture.h"
int main(void)
{
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
    puts("savectl save/journal suites: SKIP W2.1 file and W2.2 journal not in this worktree's Windows modules; dependency calls fail if reached");
    puts("savectl_test: ok"); return 0;
}
#endif
