#if defined(_WIN32)
#include "savectl_fixture.h"
static void reload_case(size_t len)
{
    uint8_t *bytes=malloc(len?len:1); CHECK(bytes); memset(bytes,'B',len);
    fixture f; init(&f,(const uint8_t *)"old",3);
    savectl_modified(f.s); put(f.path,bytes,len);
    CHECK(savectl_reload(f.s)==SAVECTL_OK); wait_idle(f.s);
    savectl_set_view(f.s,(savectl_view){UINT64_MAX,UINT64_MAX,UINT64_MAX,7});
    piece_tree *tree=NULL; savectl_view view={0}; int rc;
    do { rc=savectl_take_reload(f.s,&tree,&view); } while(rc==SAVECTL_BUSY);
    CHECK(rc==SAVECTL_OK && tree && piece_len(tree)==len);
    CHECK(view.cursor==len && view.anchor==len && view.scroll_byte==len && view.scroll_x==7);
    if(len) {
        uint8_t first=0,last=0; CHECK(piece_read(tree,0,&first,1)==0);
        CHECK(piece_read(tree,len-1,&last,1)==0 && first=='B' && last=='B');
    }
    CHECK(!savectl_get_model(f.s).modified);
    piece_destroy(tree); fini(&f); free(bytes);
}
int main(void)
{
    /* (G) More than one FILE_PREFIX_MAX copy chunk verifies short-loop offsets;
     * threshold=1 requests clone, exercising NTFS NOTSUP -> real copy. */
    reload_case(2u*FILE_PREFIX_MAX); puts("savectl Windows NTFS clone fallback/copy/map/install/clamp: ok");
    reload_case(0); puts("savectl Windows empty reload: ok");
    puts("savectl reload syscall-race suites: SKIP Linux syscall interposition; real-file integration awaits W2.1/W2.2");
    puts("savectl_reload_test: ok"); return 0;
}
#endif
