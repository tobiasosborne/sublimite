#include "view/view.h"
#include "base/base.h"
#include "../bench/harness.h"
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdlib.h>

static void *alloc_piece(void *ctx, size_t n) { return edit_arena_alloc(ctx,n,16); }
static void free_piece(void *ctx, void *p, size_t n) { (void)ctx; (void)p; (void)n; }
static int execute(view *v, view_key key)
{
    view_change c; int rc=view_command(v,key,false,(const uint8_t *)"x",key==VIEW_TYPE?1u:0u,&c);
    while(rc==VIEW_MORE) { EDIT_ASSERT(v->scanned<=VIEW_SCAN_BOUND); rc=view_continue(v,&c); }
    EDIT_ASSERT(v->scanned<=VIEW_SCAN_BOUND); return rc;
}
static int corpus(const char *path, unsigned samples)
{
    int fd=open(path,O_RDONLY); if(fd<0) { perror(path); return 1; }
    struct stat st; if(fstat(fd,&st)!=0 || st.st_size<=0) { close(fd); return 1; }
    size_t n=(size_t)st.st_size;
    uint8_t *map=mmap(NULL,n,PROT_READ,MAP_PRIVATE,fd,0); close(fd);
    if(map==MAP_FAILED) { perror("mmap"); return 1; }
    const char *names[]={"char","word","vertical","page","home_end","document","selection","edit"};
    const view_key groups[][5]={{VIEW_LEFT,VIEW_RIGHT}, {VIEW_WORD_LEFT,VIEW_WORD_RIGHT},
        {VIEW_UP,VIEW_DOWN}, {VIEW_PAGE_UP,VIEW_PAGE_DOWN}, {VIEW_HOME,VIEW_END},
        {VIEW_DOC_HOME,VIEW_DOC_END}, {VIEW_SELECT_ALL,VIEW_SELECT_LINE,VIEW_SELECT_WORD},
        {VIEW_TYPE,VIEW_BACKSPACE,VIEW_DELETE,VIEW_WORD_BACKSPACE,VIEW_WORD_DELETE}};
    const unsigned counts[]={2,2,2,2,2,2,3,5};
    uint64_t times[256]; size_t mallocs=0; int fail=0;
    printf("CORPUS %s bytes=%zu samples/class=%u (M)%s loaded-box INDICATIVE\n",path,n,samples,bench_evidence_tag()); fflush(stdout);
    for(unsigned g=0;g<8;++g) {
        bench_samples s; bench_samples_init(&s,times,256);
        for(unsigned i=0;i<samples;++i) {
            edit_arena arena; EDIT_ASSERT(edit_arena_init(&arena,16u*1024u*1024u)==0);
            piece_allocator a={&arena,alloc_piece,free_piece}; piece_tree *t=piece_create(&a); EDIT_ASSERT(t);
            EDIT_ASSERT(piece_init_mapped(t,map,n,NULL)==0);
            view v; view_config cfg={4,24,80,NULL,NULL}; view_init(&v,t,&cfg);
            /* Two real clusters into the first line: both directions do work.
             * No view setup command is hidden inside the measured interval. */
            size_t prefix=n<256?n:256, initial=0;
            for(unsigned j=0;j<2 && initial<prefix;++j) initial+=utf8_grapheme_next(map+initial,prefix-initial);
            v.state.selection.cursor=initial; v.state.selection.anchor=initial;
            edit_malloc_guard_begin();
            uint64_t begin=bench_now_ns(); int rc=execute(&v,groups[g][i%counts[g]]);
            uint64_t elapsed=bench_now_ns()-begin; mallocs+=edit_malloc_guard_end();
            EDIT_ASSERT(rc==0); (void)bench_add(&s,elapsed);
            piece_destroy(t); edit_arena_free(&arena);
        }
        char label[256]; const char *base=strrchr(path,'/'); base=base?base+1:path;
        (void)snprintf(label,sizeof label,"view_%.160s_%s_TRACK_M",base,names[g]);
        fail|=bench_report(label,&s,0,0);
        double us=(double)bench_p99(&s)/1000.0;
        printf("TRACK class=%s p99=%.3f us target<=50 us(E) G1-share=%.3f%%/%.3f%% of 1.0/2.0 ms(G)\n",
            names[g],us,us/10.0,us/20.0); fflush(stdout);
    }
    printf("ALLOCATIONS (M)%s mallocs=%zu gate=0(G) guard=%s pass=%d\n",bench_evidence_tag(),mallocs,
        edit_malloc_guard_active()?"active":"inactive",mallocs==0?1:0);
    if(mallocs || !edit_malloc_guard_active()) fail=1;
    munmap(map,n); return fail;
}
int main(int argc, char **argv)
{
    char status[32]; bench_battery_status(status,sizeof status);
    printf("POWER status=%s %s; measurements (M), concurrent workers: INDICATIVE ONLY\n",status,bench_evidence_tag());
    unsigned samples=31; const char *root="/tmp/edit-corpus";
    if(argc>1) root=argv[1];
    if(argc>2) samples=(unsigned)strtoul(argv[2],NULL,10);
    if(samples<5 || samples>256) return 2;
    const char *files[]={"log_1g.txt","oneline_1g.txt","unicode.txt"}; int fail=0;
    for(unsigned i=0;i<3;++i) { char path[1024]; (void)snprintf(path,sizeof path,"%s/%s",root,files[i]); fail|=corpus(path,samples); }
    return fail;
}
