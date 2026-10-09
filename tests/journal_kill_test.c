#include "journal/journal.h"
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

/* Full script has 100000 edits. The kill target is uniform in [1,100000].
 * Shared progress is test instrumentation only; journal uses work mailboxes. */
typedef struct progress { _Atomic uint64_t issued, acknowledged; _Atomic int ready, error; } progress;
typedef struct kill_model { uint8_t data[256]; size_t len; uint64_t rng, edits; } kill_model;
static uint64_t random_next(kill_model *m)
{ uint64_t x=m->rng; x^=x<<13; x^=x>>7; x^=x<<17; m->rng=x; return x; }
static uint64_t read64(const uint8_t *p)
{ uint64_t v=0; for(unsigned i=0;i<8;i++) v|=(uint64_t)p[i]<<(8*i); return v; }
static uint64_t now_ns(void)
{ struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t); return (uint64_t)t.tv_sec*1000000000u+(uint64_t)t.tv_nsec; }
static void nap(void) { struct timespec t={0,50000}; nanosleep(&t,NULL); }
static int operation(kill_model *m, journal *j, const journal_record *record)
{
    uint64_t r=random_next(m); bool del=m->len && (m->len==sizeof m->data || (r&3u)==0);
    size_t off=(size_t)((r>>8)%(del?m->len:m->len+1)); uint8_t ch=(uint8_t)(r>>32);
    if(record) {
        if(record->sequence!=m->edits+1 || record->buffer_id!=1 || record->type!=(uint32_t)(del?JOURNAL_DELETE:JOURNAL_INSERT) || read64(record->data)!=off) return 1;
        if(del) { if(record->size!=16 || read64(record->data+8)!=1) return 1; }
        else if(record->size!=9 || record->data[8]!=ch) return 1;
    }
    if(j) { int rc=del?journal_delete(j,1,off,1):journal_insert(j,1,off,&ch,1); if(rc) return rc; }
    if(del) { memmove(m->data+off,m->data+off+1,m->len-off-1); m->len--; }
    else { memmove(m->data+off+1,m->data+off,m->len-off); m->data[off]=ch; m->len++; }
    m->edits++; return 0;
}
typedef struct recovery { kill_model script; piece_tree *tree; } recovery;
static int replay(void *ctx, const journal_record *r)
{
    recovery *c=ctx;
    if(operation(&c->script,NULL,r)) return 1;
    return journal_apply_piece(c->tree,r);
}
/* Independent byte oracle: generate the seeded edit choices, then rebuild the
 * byte array via explicit loops. It neither decodes records nor calls operation
 * or journal_apply_piece. */
static kill_model expected_prefix(uint64_t seed, uint64_t count)
{
    kill_model m={.rng=seed};
    for(uint64_t i=0;i<count;i++) {
        uint64_t x=m.rng; x^=x<<13; x^=x>>7; x^=x<<17; m.rng=x;
        bool remove=m.len!=0 && (m.len==256 || x%4==0);
        size_t at=(size_t)((x>>8)%(remove?m.len:m.len+1));
        if(remove) { for(size_t k=at;k+1<m.len;k++) m.data[k]=m.data[k+1]; m.len--; }
        else { for(size_t k=m.len;k>at;k--) m.data[k]=m.data[k-1]; m.data[at]=(uint8_t)(x>>32); m.len++; }
        m.edits++;
    }
    return m;
}
static void receive(const work_msg *m, void *ctx) { (void)journal_receive(ctx,m); }
static void child_run(const char *path, progress *p, uint64_t seed)
{
    work_pool pool; journal *j=NULL;
    journal_options opt={.batch_bytes=1048576,.max_file_bytes=16777216};
    if(work_pool_init(&pool,1,0) || journal_open(&j,path,&pool,&opt)) { atomic_store(&p->error,1); _exit(2); }
    kill_model model={.rng=seed}; atomic_store_explicit(&p->ready,1,memory_order_release);
    for(uint64_t i=0;i<100000;i++) {
        if(operation(&model,j,NULL)) { atomic_store(&p->error,2); _exit(3); }
        atomic_store_explicit(&p->issued,i+1,memory_order_release);
        if((i&255u)==255u) {
            work_mailbox_drain(&pool,receive,j);
            atomic_store_explicit(&p->acknowledged,journal_get_stats(j).durable_sequence,memory_order_release);
            int rc=journal_pump(j,now_ns(),false); if(rc && rc!=JOURNAL_BUSY) { atomic_store(&p->error,3); _exit(4); }
        }
        /* Bound pending volume below capacity despite slow worker. This is
         * script throttling, separate from the never-blocking append API. */
        if((i&4095u)==4095u) {
            if(journal_flush(j)) { atomic_store(&p->error,4); _exit(5); }
            atomic_store_explicit(&p->acknowledged,journal_get_stats(j).durable_sequence,memory_order_release);
        }
    }
    if(journal_flush(j)) atomic_store(&p->error,5);
    atomic_store(&p->acknowledged,journal_get_stats(j).durable_sequence);
    /* Stay available to be killed even when parent scheduling is slow. */
    for(;;) nap();
}
int main(int argc, char **argv)
{
    unsigned trials=50;
    if(argc==2 && sscanf(argv[1],"--trials=%u",&trials)!=1) return 2;
    if(!trials || trials>10000) return 2;
    progress *p=mmap(NULL,sizeof *p,PROT_READ|PROT_WRITE,MAP_SHARED|MAP_ANONYMOUS,-1,0);
    if(p==MAP_FAILED) return 2;
    uint64_t total_replayed=0, total_issued=0, total_ack=0;
    for(unsigned t=0;t<trials;t++) {
        atomic_init(&p->issued,0); atomic_init(&p->acknowledged,0); atomic_init(&p->ready,0); atomic_init(&p->error,0);
        uint64_t seed=0x9e3779b97f4a7c15ull^(uint64_t)(t+1)*0x100000001b3ull;
        kill_model target_model={.rng=seed^0x123456789abcdefull};
        uint64_t target=1+random_next(&target_model)%100000;
        char path[]="/tmp/journal-kill-XXXXXX"; int fd=mkstemp(path); if(fd<0) return 2; close(fd);
        pid_t child=fork(); if(child<0) return 2;
        if(child==0) child_run(path,p,seed);
        uint64_t start=now_ns(); bool timed_out=false;
        while(!atomic_load_explicit(&p->ready,memory_order_acquire) || atomic_load_explicit(&p->issued,memory_order_acquire)<target) {
            if(atomic_load(&p->error) || now_ns()-start>10000000000ull) { timed_out=true; break; }
            nap();
        }
        if(kill(child,SIGKILL)) return 2;
        int status=0; while(waitpid(child,&status,0)<0) if(errno!=EINTR) return 2;
        uint64_t issued=atomic_load(&p->issued), ack=atomic_load(&p->acknowledged);
        piece_allocator a=piece_default_allocator();
        recovery restored={.script={.rng=seed},.tree=piece_create(&a)}; if(!restored.tree) return 2;
        journal_replay_result rr; int rc=journal_replay_file(path,replay,&restored,&rr);
        kill_model actual=restored.script, expected=expected_prefix(seed,rr.last_sequence);
        uint8_t tree_bytes[256];
        bool tree_equal=piece_len(restored.tree)==expected.len &&
            piece_read(restored.tree,0,tree_bytes,expected.len)==0 && !memcmp(tree_bytes,expected.data,expected.len);
        piece_destroy(restored.tree);
        if(timed_out || atomic_load(&p->error) || !WIFSIGNALED(status) || WTERMSIG(status)!=SIGKILL || rc || !tree_equal || rr.last_sequence!=actual.edits || actual.edits>issued || actual.edits<ack || actual.len!=expected.len || memcmp(actual.data,expected.data,actual.len)) {
            fprintf(stderr,"journal_kill_test: FAIL trial=%u target=%llu issued=%llu ack=%llu replay=%llu rc=%d error=%d\n",t,(unsigned long long)target,(unsigned long long)issued,(unsigned long long)ack,(unsigned long long)actual.edits,rc,atomic_load(&p->error)); unlink(path); return 1;
        }
        total_issued+=issued; total_replayed+=actual.edits; total_ack+=ack; unlink(path);
    }
    munmap(p,sizeof *p);
    printf("journal_kill_test: ok trials=%u script_edits=100000 issued=%llu replayed=%llu acknowledged=%llu (ack<=replayed<=issued; piece_tree==independent_byte_model_at_replayed)\n",trials,(unsigned long long)total_issued,(unsigned long long)total_replayed,(unsigned long long)total_ack);
    return 0;
}
