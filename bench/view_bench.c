#include "view/view.h"
#include "base/base.h"
#include "work/work.h"
#include "../bench/harness.h"
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdlib.h>
#include <sched.h>

/* --track is the default. --gate is for a controlled quiet AC coordinator run.
 * Necessary module limits exclude layout/render; passing is not a G1/G3 verdict.
 * Slow commands are capped, reported incomplete, and fail controlled gates. */
typedef struct stamp { char power[32]; double load; } stamp;
typedef struct series {
    uint64_t commands[256], first[256], resumes[65536];
    bench_samples whole, initial, continuation;
    uint64_t max_slice;
    unsigned incomplete, setup_incomplete;
} series;
static void *alloc_piece(void *ctx, size_t n) { return edit_arena_alloc(ctx,n,16); }
static void free_piece(void *ctx, void *p, size_t n) { (void)ctx; (void)p; (void)n; }
static int prepare(view *v, bool replace, bool plain);
static int setup_checkpoint(void *ctx, const piece_tree *tree, uint64_t line,
    uint64_t byte_target, uint64_t col_target, uint64_t *byte, uint64_t *col)
{
    EDIT_ASSERT(line==0); ++*(unsigned *)ctx;
    uint64_t target=byte_target==UINT64_MAX?col_target:byte_target;
    if(target>piece_len(tree)) target=piece_len(tree);
    *byte=target; *col=target; return 1;
}
static stamp read_stamp(void)
{
    stamp s={"unknown",-1}; bench_battery_status(s.power,sizeof s.power);
    FILE *fp=fopen("/proc/loadavg","r");
    if(fp) { if(fscanf(fp,"%lf",&s.load)!=1) s.load=-1; fclose(fp); }
    return s;
}
static bool quiet_stamp(stamp s)
{
    return strcmp(bench__tag_from_power(s.power),"[AC]")==0 && s.load>=0 && s.load<=2;
}
static bool timing_miss(const bench_samples *s, uint64_t p50, uint64_t p99)
{
    return !s->n || s->dropped || (p50 && bench_p50(s)>p50) || (p99 && bench_p99(s)>p99);
}
static void series_init(series *s)
{
    memset(s,0,sizeof *s); bench_samples_init(&s->whole,s->commands,256);
    bench_samples_init(&s->initial,s->first,256); bench_samples_init(&s->continuation,s->resumes,65536);
}
static int row(const char *name, const bench_samples *s, uint64_t p50, uint64_t p99,
               bool gate, stamp power, bool incomplete)
{
    bool eligible=!gate || quiet_stamp(power), miss=timing_miss(s,p50,p99) || incomplete;
    printf("BENCH %s mode=%s n=%zu p50=%llu p99=%llu ns (M)%s load1=%.2f necessary_gate=%llu/%llu ns(G) verdict=%s incomplete=%d dropped=%zu\n",
        name,gate?"GATE":"TRACK",s->n,(unsigned long long)bench_p50(s),(unsigned long long)bench_p99(s),
        bench__tag_from_power(power.power),power.load,(unsigned long long)p50,(unsigned long long)p99,
        gate?(eligible?(miss?"FAIL":"PASS"):"INVALID"):"TRACK",incomplete?1:0,s->dropped);
    fflush(stdout); return !s->n || s->dropped || (gate && (!eligible || miss))?1:0;
}
/* Complete-source segmentation, never a prefix declared to be EOF. If setup
 * meets a huge cluster, retain the last certified stop instead of replaying it. */
static size_t initial_stop(const uint8_t *p, size_t n)
{
    size_t at=0;
    for(unsigned j=0;j<2 && at<n;++j) {
        size_t certified=at;
        utf8_gseg seg; utf8_gseg_init(&seg); int rc;
        do {
            if(at>=65536) return certified;
            size_t used=0, available=n-at; if(available>VIEW_WINDOW) available=VIEW_WINDOW;
            rc=utf8_grapheme_step(&seg,p+at,available,256,at+available==n,&used);
            at+=used;
        } while(rc!=UTF8_G_END);
    }
    return at;
}
/* A nearby certain ASCII boundary avoids replaying a whole ASCII document.
 * Otherwise use streaming segmentation from the document start. */
static bool valid_stop(const piece_tree *tree, uint64_t at)
{
    uint64_t total=piece_len(tree); if(at>total) return false;
    if(at==0 || at==total) return true;
    uint8_t buf[VIEW_WINDOW+4u]; uint64_t start=at>512?at-512:0;
    size_t count=(size_t)(at-start+4); if(total-start<count) count=(size_t)(total-start);
    EDIT_ASSERT(piece_read(tree,start,buf,count)==PIECE_OK);
    bool certain=start==0;
    for(size_t i=1;i<=(size_t)(at-start);++i)
        if(buf[i]<128 && buf[i-1]<128 && buf[i]!='\r' && buf[i]!='\n' && buf[i-1]!='\r') {
            start+=i; certain=true; break;
        }
    if(!certain) start=0;
    if(certain && start==at) return true;
    uint64_t pos=start; utf8_gseg seg; utf8_gseg_init(&seg);
    while(pos<=at) {
        size_t available=(size_t)(total-pos<sizeof buf?total-pos:sizeof buf), used=0;
        EDIT_ASSERT(piece_read(tree,pos,buf,available)==PIECE_OK);
        int rc=utf8_grapheme_step(&seg,buf,available,256,pos+available==total,&used); pos+=used;
        if(rc==UTF8_G_END) { if(pos==at) return true; if(pos>at) return false; }
    }
    return false;
}
static int start_self_check(void)
{
    uint8_t p[402]; p[0]='a';
    for(size_t i=1;i<401;i+=2) { p[i]=0xcc; p[i+1]=0x81; } p[401]='b';
    if(initial_stop(p,sizeof p)!=sizeof p) { puts("review_16 RED: benchmark start splits complete source cluster"); return 1; }
    edit_arena arena; EDIT_ASSERT(edit_arena_init(&arena,1024u*1024u)==0);
    piece_allocator a={&arena,alloc_piece,free_piece}; piece_tree *tree=piece_create(&a); EDIT_ASSERT(tree);
    EDIT_ASSERT(piece_init_mapped(tree,p,sizeof p,NULL)==PIECE_OK);
    EDIT_ASSERT(valid_stop(tree,401) && valid_stop(tree,402) && !valid_stop(tree,256));
    piece_destroy(tree); edit_arena_free(&arena);
    uint8_t longp[131074]; longp[0]='a';
    for(size_t i=1;i<131073;i+=2) { longp[i]=0xcc; longp[i+1]=0x81; } longp[131073]='b';
    EDIT_ASSERT(initial_stop(longp,sizeof longp)==0);
    puts("review_16 GREEN: benchmark start uses complete clusters"); return 0;
}
static uint64_t replacement_anchor(uint64_t n) { return n>1048576?n-1048576:0; }
static int gate_self_check(void)
{
    if(quiet_stamp((stamp){"Not charging",3.0}) || quiet_stamp((stamp){"Discharging",0.0})) {
        puts("review_12 RED: unsuitable row stamp permits timing verdict"); return 1;
    }
    EDIT_ASSERT(quiet_stamp((stamp){"Full",1.0}));
    uint8_t text[20000]; memset(text,'x',sizeof text);
    edit_arena arena; EDIT_ASSERT(edit_arena_init(&arena,1024u*1024u)==0);
    piece_allocator a={&arena,alloc_piece,free_piece}; piece_tree *tree=piece_create(&a); EDIT_ASSERT(tree);
    EDIT_ASSERT(piece_init_copy(tree,text,sizeof text)==PIECE_OK);
    unsigned calls=0; view v; view_config cfg={4,24,80,setup_checkpoint,&calls}; view_init(&v,tree,&cfg);
    int rc=prepare(&v,false,true); bool ready=rc==VIEW_OK && calls>0 && v.scanned<256;
    view_cancel(&v); piece_destroy(tree); edit_arena_free(&arena);
    if(!ready) { puts("review_12 RED: checkpoint-backed setup replays full columns"); return 1; }
    if(replacement_anchor(1048576)==1048576 || replacement_anchor(100)==100) {
        puts("review_12 RED: replacement fixture has an empty selection"); return 1;
    }
    /* Synthetic samples: no wall-clock measurement or benchmark run. */
    uint64_t delay=UINT64_C(1000000000); bench_samples s; bench_samples_init(&s,&delay,1); (void)bench_add(&s,delay);
    if(!timing_miss(&s,1000000,2000000) || !timing_miss(&s,5000000,5555555) || !timing_miss(&s,500000,500000)) {
        puts("review_12 RED: delayed command accepted by timing gate"); return 1;
    }
    delay=1000;
    if(timing_miss(&s,1000000,2000000)) return 1;
    puts("review_12 GREEN: delayed command rejected by G1/G3/slice checks"); return 0;
}
static bool plain_line(const uint8_t *map, size_t n)
{
    for(size_t pos=0;pos<n;) {
        size_t len=n-pos; if(len>65536) len=65536;
        if(utf8_ascii_run(map+pos,len)!=len || memchr(map+pos,'\n',len) ||
           memchr(map+pos,'\r',len) || memchr(map+pos,'\t',len)) return false;
        pos+=len;
    }
    return true;
}
static int plain_checkpoint(void *ctx, const piece_tree *tree, uint64_t line,
    uint64_t byte_target, uint64_t col_target, uint64_t *byte, uint64_t *col)
{
    (void)ctx; EDIT_ASSERT(line==0);
    uint64_t target=byte_target==UINT64_MAX?col_target:byte_target;
    if(target>piece_len(tree)) target=piece_len(tree);
    *byte=target; *col=target; return 1;
}
static int prepare(view *v, bool replace, bool plain)
{
    view_change change;
    if(plain) return view_command(v,VIEW_DOC_END,false,NULL,0,&change);
    uint64_t n=piece_len(v->tree);
    view_state saved={{n,replace?replacement_anchor(n):n,VIEW_PREFERRED_UNSET},0,0,0,false,false,false,0};
    return view_restore(v,&saved,&change);
}
static int execute(view *v, view_key key, series *s)
{
    view_change c; uint64_t begin=bench_now_ns(), call=begin;
    int rc=view_command(v,key,false,(const uint8_t *)"x",key==VIEW_TYPE?1u:0u,&c);
    uint64_t elapsed=bench_now_ns()-call; (void)bench_add(&s->initial,elapsed);
    if(elapsed>s->max_slice) s->max_slice=elapsed;
    unsigned calls=0;
    while(rc==VIEW_MORE) {
        EDIT_ASSERT(v->scanned<=VIEW_SCAN_BOUND);
        if(++calls>1024 || bench_now_ns()-begin>UINT64_C(100000000)) {
            ++s->incomplete; (void)bench_add(&s->whole,bench_now_ns()-begin);
            view_cancel(v); return VIEW_MORE;
        }
        call=bench_now_ns(); rc=view_continue(v,&c); elapsed=bench_now_ns()-call;
        (void)bench_add(&s->continuation,elapsed); if(elapsed>s->max_slice) s->max_slice=elapsed;
    }
    EDIT_ASSERT(v->scanned<=VIEW_SCAN_BOUND); (void)bench_add(&s->whole,bench_now_ns()-begin); return rc;
}
typedef struct bulk_fixture { piece_snapshot *snapshot; _Atomic bool active; _Atomic uint64_t hash; } bulk_fixture;
static void bulk_job(work_ctx *ctx)
{
    bulk_fixture *f=ctx->arg; uint8_t bytes[4096]; uint64_t pos=0, hash=0, n=piece_snapshot_len(f->snapshot);
    atomic_store(&f->active,true);
    while(!work_should_stop(ctx)) {
        size_t count=(size_t)(n-pos<sizeof bytes?n-pos:sizeof bytes);
        EDIT_ASSERT(piece_snapshot_read(f->snapshot,pos,bytes,count)==PIECE_OK);
        for(size_t i=0;i<count;++i) hash=hash*33u+bytes[i];
        atomic_store(&f->hash,hash); pos+=count; if(pos==n) pos=0;
    }
}
static int corpus(const char *path, unsigned samples, bool gate)
{
    int fd=open(path,O_RDONLY); if(fd<0) { perror(path); return 1; }
    struct stat st; if(fstat(fd,&st)!=0 || st.st_size<=0) { close(fd); return 1; }
    size_t n=(size_t)st.st_size;
    uint8_t *map=mmap(NULL,n,PROT_READ,MAP_PRIVATE,fd,0); close(fd);
    if(map==MAP_FAILED) { perror("mmap"); return 1; }
    bool plain=plain_line(map,n); size_t initial=initial_stop(map,n);
    const char *names[]={"char","word","vertical","page","home_end","document","selection","edit",
        "deep_type","selection_1MiB_replace","unindexed_type"};
    const view_key groups[][5]={{VIEW_LEFT,VIEW_RIGHT},{VIEW_WORD_LEFT,VIEW_WORD_RIGHT},
        {VIEW_UP,VIEW_DOWN},{VIEW_PAGE_UP,VIEW_PAGE_DOWN},{VIEW_HOME,VIEW_END},
        {VIEW_DOC_HOME,VIEW_DOC_END},{VIEW_SELECT_ALL,VIEW_SELECT_LINE,VIEW_SELECT_WORD},
        {VIEW_TYPE,VIEW_BACKSPACE,VIEW_DELETE,VIEW_WORD_BACKSPACE,VIEW_WORD_DELETE},
        {VIEW_TYPE},{VIEW_TYPE},{VIEW_TYPE}};
    const unsigned counts[]={2,2,2,2,2,2,3,5,1,1,1};
    int fail=0; size_t mallocs=0;
    edit_arena bulk_arena; EDIT_ASSERT(edit_arena_init(&bulk_arena,16u*1024u*1024u)==0);
    piece_allocator ba={&bulk_arena,alloc_piece,free_piece}; piece_tree *bt=piece_create(&ba); EDIT_ASSERT(bt);
    EDIT_ASSERT(piece_init_mapped(bt,map,n,NULL)==PIECE_OK);
    bulk_fixture bulk; bulk.snapshot=piece_snapshot_take(bt); EDIT_ASSERT(bulk.snapshot);
    atomic_init(&bulk.active,false); atomic_init(&bulk.hash,0);
    work_pool pool; EDIT_ASSERT(work_pool_init(&pool,1,0)==0);
    for(unsigned jobs=1;jobs<=3;jobs+=2) {
        work_handle handles[3]; atomic_store(&bulk.active,false);
        for(unsigned j=0;j<jobs;++j) {
            handles[j]=work_submit(&pool,(work_job){bulk_job,&bulk,j+1,WORK_BULK}); EDIT_ASSERT(handles[j].epoch);
        }
        while(!atomic_load(&bulk.active)) sched_yield();
        for(unsigned g=0;g<sizeof counts/sizeof counts[0];++g) {
            series s; series_init(&s); stamp power=read_stamp();
            printf("FIXTURE %s bytes=%zu(M)%s load1=%.2f bulk=%s source=%s initial=certified\n",
                path,n,bench__tag_from_power(power.power),power.load,jobs==1?"one-active":"three-submitted",plain?"plain-indexed":"mapped");
            for(unsigned i=0;i<samples;++i) {
                edit_arena arena; EDIT_ASSERT(edit_arena_init(&arena,16u*1024u*1024u)==0);
                piece_allocator a={&arena,alloc_piece,free_piece}; piece_tree *t=piece_create(&a); EDIT_ASSERT(t);
                EDIT_ASSERT(piece_init_mapped(t,map,n,NULL)==PIECE_OK);
                view v; view_config cfg={4,24,80,plain && g!=10?plain_checkpoint:NULL,NULL}; view_init(&v,t,&cfg);
                if(g==8 || g==9) {
                    view_change c; int rc=prepare(&v,g==9,plain);
                    uint64_t setup_begin=bench_now_ns(); unsigned setup_calls=0;
                    while(rc==VIEW_MORE && ++setup_calls<=1024 && bench_now_ns()-setup_begin<=UINT64_C(100000000))
                        rc=view_continue(&v,&c);
                    if(rc==VIEW_MORE) {
                        ++s.setup_incomplete; view_cancel(&v);
                        piece_destroy(t); edit_arena_free(&arena); continue;
                    }
                    EDIT_ASSERT(rc==VIEW_OK); /* setup/index completion, not hidden in the interval */
                    if(g==9 && plain) v.state.selection.anchor=replacement_anchor(n);
                } else if(g!=10) { v.state.selection.cursor=initial; v.state.selection.anchor=initial; }
                EDIT_ASSERT(valid_stop(t,v.state.selection.cursor) && valid_stop(t,v.state.selection.anchor));
                edit_malloc_guard_begin(); int rc=execute(&v,groups[g][i%counts[g]],&s);
                mallocs+=edit_malloc_guard_end(); EDIT_ASSERT(rc==VIEW_OK || rc==VIEW_MORE);
                EDIT_ASSERT(valid_stop(t,v.state.selection.cursor) && valid_stop(t,v.state.selection.anchor));
                piece_destroy(t); edit_arena_free(&arena);
            }
            if(s.setup_incomplete) {
                printf("SETUP_LIMIT %s skipped=%u(M)%s load1=%.2f mode=%s verdict=%s setup_cap=100000000 ns(G)/1024 calls(G)\n",
                    names[g],s.setup_incomplete,bench__tag_from_power(power.power),power.load,gate?"GATE":"TRACK",gate?"FAIL":"TRACK");
                if(gate) fail=1;
            }
            if(s.whole.n==0 && s.setup_incomplete) continue;
            char label[256]; const char *base=strrchr(path,'/'); base=base?base+1:path;
            (void)snprintf(label,sizeof label,"view_%.100s_%s_bulk%u_command",base,names[g],jobs);
            uint64_t p50=g>=7?1000000:5000000, p99=g>=7?2000000:5555555;
            fail|=row(label,&s.whole,p50,p99,gate,power,s.incomplete!=0 || s.setup_incomplete!=0);
            (void)snprintf(label,sizeof label,"view_%.100s_%s_bulk%u_initial",base,names[g],jobs);
            fail|=row(label,&s.initial,500000,500000,gate,power,false);
            if(s.continuation.n) {
                (void)snprintf(label,sizeof label,"view_%.100s_%s_bulk%u_continue",base,names[g],jobs);
                fail|=row(label,&s.continuation,500000,500000,gate,power,false);
            }
            printf("SLICE_MAX %s %llu ns(M)%s load1=%.2f hard_limit=500000 ns(G) verdict=%s incomplete_commands=%u\n",
                names[g],(unsigned long long)s.max_slice,bench__tag_from_power(power.power),power.load,
                gate?(quiet_stamp(power)?(s.max_slice>500000?"FAIL":"PASS"):"INVALID"):"TRACK",s.incomplete);
            if(gate && s.max_slice>500000) fail=1;
        }
        for(unsigned j=0;j<jobs;++j) work_cancel(&pool,handles[j]);
        /* Join before resetting fixture activity for the next controlled mode. */
        work_pool_shutdown(&pool);
        if(jobs==1) EDIT_ASSERT(work_pool_init(&pool,1,0)==0);
    }
    stamp power=read_stamp();
    printf("ALLOCATIONS %s mallocs=%zu(M)%s load1=%.2f gate=0(G) guard=%s pass=%d\n",path,mallocs,
        bench__tag_from_power(power.power),power.load,edit_malloc_guard_active()?"active":"inactive",mallocs==0?1:0);
    if(mallocs || !edit_malloc_guard_active()) fail=1;
    piece_snapshot_release(bulk.snapshot); piece_destroy(bt); edit_arena_free(&bulk_arena);
    munmap(map,n); return fail;
}
int main(int argc, char **argv)
{
    if(argc==2 && strcmp(argv[1],"--self-check-start")==0) return start_self_check();
    if(argc==2 && strcmp(argv[1],"--self-check-gates")==0) return gate_self_check();
    unsigned samples=31; const char *root="/tmp/edit-corpus"; bool gate=false; int arg=1;
    if(argc>arg && (strcmp(argv[arg],"--track")==0 || strcmp(argv[arg],"--gate")==0)) { gate=strcmp(argv[arg],"--gate")==0; ++arg; }
    if(argc>arg) root=argv[arg++];
    if(argc>arg) samples=(unsigned)strtoul(argv[arg++],NULL,10);
    if(samples<5 || samples>256 || arg<argc) return 2;
    stamp power=read_stamp();
    printf("POWER %s %s load1=%.2f mode=%s; module-only necessary limits, 50 us remains (E)\n",
        power.power,bench__tag_from_power(power.power),power.load,gate?"GATE":"TRACK"); fflush(stdout);
    if(gate && !quiet_stamp(power)) {
        puts("GATE refused: requires quiet AC box (load1 <= 2(G)); use --track"); return 2;
    }
    const char *files[]={"log_1g.txt","oneline_1g.txt","unicode.txt"}; int fail=0;
    for(unsigned i=0;i<3;++i) { char path[1024]; (void)snprintf(path,sizeof path,"%s/%s",root,files[i]); fail|=corpus(path,samples,gate); }
    return fail;
}
