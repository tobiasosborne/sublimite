/* Windows seeded campaign preserves the Linux journal_kill_test's seeded
 * independent byte oracle and [page_cache, issued] replay bounds. The test's
 * shared progress instrumentation is a small NTFS file instead of mmap.
 * (P) script/trial counts, seeds, model capacity and polling cadence are the
 * existing tests/journal_kill_test.c fixtures; no new performance budget. */
#define main journal_boundaries_main
#include "../journal_hot_exit_test.c"
#undef main

typedef struct kill_model { uint8_t bytes[256]; size_t len; uint64_t rng, edits; } kill_model;
typedef struct progress { uint64_t issued, cached, ack, error; } progress;
static uint64_t random_next(kill_model *m)
{ uint64_t x=m->rng; x^=x<<13; x^=x>>7; x^=x<<17; m->rng=x; return x; }
static int operation(kill_model *m, journal *j, const journal_record *record)
{
    uint64_t r=random_next(m); bool del=m->len && (m->len==sizeof m->bytes || (r&3u)==0);
    size_t off=(size_t)((r>>8)%(del?m->len:m->len+1u)); uint8_t ch=(uint8_t)(r>>32);
    if(record) {
        if(record->sequence!=m->edits+1u || record->buffer_id!=1 || record->type!=(uint32_t)(del?JOURNAL_DELETE:JOURNAL_INSERT) || get64(record->data)!=off) return 1;
        if(del) { if(record->size!=sizeof(uint64_t)*2u || get64(record->data+sizeof(uint64_t))!=1) return 1; }
        else if(record->size!=sizeof(uint64_t)+1u || record->data[sizeof(uint64_t)]!=ch) return 1;
    }
    if(j) { int rc=del?journal_delete(j,1,off,1):journal_insert(j,1,off,&ch,1); if(rc) return rc; }
    if(del) { memmove(m->bytes+off,m->bytes+off+1u,m->len-off-1u); m->len--; }
    else { memmove(m->bytes+off+1u,m->bytes+off,m->len-off); m->bytes[off]=ch; m->len++; }
    m->edits++; return 0;
}
static kill_model expected_prefix(uint64_t seed, uint64_t count)
{
    kill_model m={.rng=seed};
    for(uint64_t i=0;i<count;i++) {
        uint64_t x=m.rng; x^=x<<13; x^=x>>7; x^=x<<17; m.rng=x;
        bool remove=m.len!=0 && (m.len==sizeof m.bytes || x%4u==0);
        size_t at=(size_t)((x>>8)%(remove?m.len:m.len+1u));
        if(remove) { for(size_t k=at;k+1u<m.len;k++) m.bytes[k]=m.bytes[k+1u]; m.len--; }
        else { for(size_t k=m.len;k>at;k--) m.bytes[k]=m.bytes[k-1u]; m.bytes[at]=(uint8_t)(x>>32); m.len++; }
        m.edits++;
    }
    return m;
}
static int publish(os_file *f, size_t offset, uint64_t value)
{ size_t n; O(os_file_pwrite(f,&value,sizeof value,offset,&n)); T(n==sizeof value); return 0; }
static void receive(const work_msg *m, void *ctx) { (void)journal_receive(ctx,m); }
static int random_writer(const char *path, const char *shared, uint64_t seed, bool before_pump)
{
    os_file f=OS_FILE_INIT; O(os_file_open(&f,shared,OS_OPEN_RDWR)); work_pool pool;
    T(work_pool_init(&pool,1,0)==0); journal *j; journal_options opt={.batch_bytes=1048576,.max_file_bytes=16777216};
    T(journal_open(&j,path,&pool,&opt)==0); kill_model m={.rng=seed};
    const uint64_t tabs[]={1,19,271}; /* same discriminating metadata as Linux */
    T(journal_set_view(j,1,&(journal_view){79,13,4099,27})==0);
    T(journal_set_tabs(j,tabs,3,2)==0); T(journal_set_window(j,1373,911)==0);
    for(uint64_t i=0;i<100000;i++) {
        T(publish(&f,offsetof(progress,issued),i+1u)==0);
        if(operation(&m,j,NULL)) { (void)publish(&f,offsetof(progress,error),1); return 1; }
        T(publish(&f,offsetof(progress,cached),i+1u)==0);
        if(before_pump) park();
        if((i&255u)==255u) {
            work_mailbox_drain(&pool,receive,j); T(publish(&f,offsetof(progress,ack),(journal_get_stats(j).durable_sequence>=3?journal_get_stats(j).durable_sequence-3u:0))==0);
            int rc=journal_pump(j,os_now_ns(),false); T(rc==0 || rc==JOURNAL_BUSY);
        }
        if((i&4095u)==4095u) { T(journal_flush(j)==0); T(publish(&f,offsetof(progress,ack),(journal_get_stats(j).durable_sequence>=3?journal_get_stats(j).durable_sequence-3u:0))==0); }
    }
    T(journal_flush(j)==0); T(publish(&f,offsetof(progress,ack),(journal_get_stats(j).durable_sequence>=3?journal_get_stats(j).durable_sequence-3u:0))==0); park(); return 1;
}
typedef struct restored { kill_model script; piece_tree *tree; bool view,tabs,window; } restored;
static int replay_random(void *ctx, const journal_record *r)
{
    restored *s=ctx;
    if(r->type==JOURNAL_VIEW) {
        if(s->view || r->sequence!=1 || r->buffer_id!=1 || r->size!=32 ||
           get64(r->data)!=79 || get64(r->data+8)!=13 || get64(r->data+16)!=4099 || get64(r->data+24)!=27) return 1;
        s->view=true; return 0;
    }
    if(r->type==JOURNAL_TABS) {
        if(s->tabs || r->sequence!=2 || r->buffer_id || r->size!=40 || get64(r->data)!=3 || get64(r->data+8)!=2 ||
           get64(r->data+16)!=1 || get64(r->data+24)!=19 || get64(r->data+32)!=271) return 1;
        s->tabs=true; return 0;
    }
    if(r->type==JOURNAL_WINDOW) {
        journal_window window;
        if(s->window || r->sequence!=3 || r->buffer_id || journal_decode_window(r,&window) ||
           window.width!=1373 || window.height!=911 || window.flags!=JOURNAL_WINDOW_CHOSEN) return 1;
        s->window=true; return 0;
    }
    if(r->sequence<=3) return 1;
    journal_record edit=*r; edit.sequence-=3;
    if(operation(&s->script,NULL,&edit)) return 1;
    return journal_apply_piece(s->tree,r);
}
static int random_campaign(unsigned trials)
{
    uint64_t total_issued=0,total_cache=0,total_ack=0,total_replayed=0;
    char temp[OS_PATH_MAX], root[OS_PATH_MAX], self[OS_PATH_MAX]; O(os_temp_dir(temp,sizeof temp)); O(os_proc_self_path(self,sizeof self));
    int n=snprintf(root,sizeof root,"%s/sublimite-test-journal-random-%" PRIu64,temp,os_proc_id()); T(n>0 && (size_t)n<sizeof root); O(os_mkdir(root));
    char path[OS_PATH_MAX], shared[OS_PATH_MAX];
    n=snprintf(path,sizeof path,"%s/journal",root); T(n>0 && (size_t)n<sizeof path);
    n=snprintf(shared,sizeof shared,"%s/progress",root); T(n>0 && (size_t)n<sizeof shared);
    for(unsigned t=0;t<trials;t++) {
        uint64_t seed=UINT64_C(0x9e3779b97f4a7c15)^(uint64_t)(t+1u)*UINT64_C(0x100000001b3);
        kill_model target_model={.rng=seed^UINT64_C(0x123456789abcdef)};
        uint64_t target=t==0?1u:1u+random_next(&target_model)%100000u;
        os_file f=OS_FILE_INIT; O(os_file_open(&f,shared,OS_OPEN_RDWR|OS_OPEN_CREATE|OS_OPEN_EXCL));
        progress p={0}; size_t moved; O(os_file_write(&f,&p,sizeof p,&moved)); T(moved==sizeof p);
        char seed_arg[sizeof "18446744073709551615"], before[sizeof "1"];
        n=snprintf(seed_arg,sizeof seed_arg,"%" PRIu64,seed); T(n>0 && (size_t)n<sizeof seed_arg); strcpy(before,t==0?"1":"0");
        const char *args[]={self,"--random-writer",path,shared,seed_arg,before,NULL}; os_proc child; O(os_proc_spawn(&child,args));
        uint64_t end=os_now_ns()+UINT64_C(10000000000); bool reached=false;
        while(os_now_ns()<end) {
            O(os_file_pread(&f,&p,sizeof p,0,&moved)); T(moved==sizeof p);
            if(p.cached>=target) { reached=true; break; }
            os_proc_status st; os_err e=os_proc_wait(&child,0,&st);
            if(e==OS_OK) { fprintf(stderr,"journal_kill_test: random writer exited trial=%u code=%d\n",t,st.code); return 1; }
            T(e==OS_E_TIMEDOUT && !p.error); os_sleep_ns(50000);
        }
        O(os_proc_kill(&child)); os_proc_status st; O(os_proc_wait(&child,-1,&st)); T(reached && st.how==OS_PROC_ABNORMAL);
        O(os_file_pread(&f,&p,sizeof p,0,&moved)); T(moved==sizeof p && !p.error); os_file_close(&f);
        piece_allocator a=piece_default_allocator(); restored r={.script={.rng=seed},.tree=piece_create(&a)}; T(r.tree);
        journal_replay_result rr; T(journal_replay_file(path,replay_random,&r,&rr)==0);
        kill_model want=expected_prefix(seed,r.script.edits); uint8_t bytes[sizeof want.bytes];
        T(r.view && r.tabs && r.window && rr.records==r.script.edits+3u && rr.last_sequence==r.script.edits+3u && r.script.edits>=p.cached && r.script.edits>=p.ack && r.script.edits<=p.issued);
        T(piece_len(r.tree)==want.len && piece_read(r.tree,0,bytes,want.len)==0 && !memcmp(bytes,want.bytes,want.len));
        T(r.script.len==want.len && !memcmp(r.script.bytes,want.bytes,want.len)); piece_destroy(r.tree);
        total_issued+=p.issued; total_cache+=p.cached; total_ack+=p.ack; total_replayed+=r.script.edits;
        O(os_unlink(shared)); O(os_unlink(path));
    }
    O(os_rmdir(root));
    printf("journal_kill_test: ok trials=%u script_edits=100000 issued=%" PRIu64 " page_cache=%" PRIu64 " replayed=%" PRIu64 " acknowledged=%" PRIu64 " (page_cache<=replayed<=issued; piece_tree==independent_byte_model_at_replayed; trial0=kill_before_pump)\n",trials,total_issued,total_cache,total_replayed,total_ack); return 0;
}
int main(int argc, char **argv)
{
    if(argc==6 && !strcmp(argv[1],"--random-writer")) {
        char *end; uint64_t seed=strtoull(argv[4],&end,10); T(!*end);
        return random_writer(argv[2],argv[3],seed,!strcmp(argv[5],"1"));
    }
    if(argc==2 && !strcmp(argv[1],"--metadata-only")) return random_campaign(1);
    if(argc!=1) return journal_boundaries_main(argc,argv);
    T(journal_boundaries_main(argc,argv)==0); return random_campaign(50);
}
