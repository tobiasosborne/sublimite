#include "journal/journal.h"
#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

/* Full script has 100000 edits. Trial zero kills before any pump; the remaining
 * kill targets are uniform in [1,100000].
 * Shared progress is test instrumentation only; journal uses work mailboxes. */
typedef struct progress { _Atomic uint64_t issued, page_cache, acknowledged; _Atomic int ready, error; } progress;
typedef struct kill_model { uint8_t data[256]; size_t len; uint64_t rng, edits; } kill_model;
static uint64_t random_next(kill_model *m)
{ uint64_t x=m->rng; x^=x<<13; x^=x>>7; x^=x<<17; m->rng=x; return x; }
static uint64_t read64(const uint8_t *p)
{ uint64_t v=0; for(unsigned i=0;i<8;i++) v|=(uint64_t)p[i]<<(8*i); return v; }
/* (P) little-endian uint32_t fields contain sizeof(uint32_t) octets;
 * CHAR_BIT bits per octet. Zero initializes the accumulator. */
static uint32_t read32(const uint8_t *p)
{ uint32_t v=0; for(size_t i=0;i<sizeof v;i++) v|=(uint32_t)p[i]<<(CHAR_BIT*i); return v; }
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
typedef struct recovery { kill_model script; piece_tree *tree; bool view, tabs, window; } recovery;
static int replay(void *ctx, const journal_record *r)
{
    recovery *c=ctx;
    if(r->type==JOURNAL_VIEW) {
        if(c->view || r->sequence!=1 || r->buffer_id!=1 || r->size!=32 ||
           read64(r->data)!=79 || read64(r->data+8)!=13 || read64(r->data+16)!=4099 || read64(r->data+24)!=27) return 1;
        c->view=true; return 0;
    }
    if(r->type==JOURNAL_TABS) {
        if(c->tabs || r->sequence!=2 || r->size!=40 || read64(r->data)!=3 || read64(r->data+8)!=2 ||
           read64(r->data+16)!=1 || read64(r->data+24)!=19 || read64(r->data+32)!=271) return 1;
        c->tabs=true; return 0;
    }
    if(r->type==JOURNAL_WINDOW) {
        /* (P) WINDOW follows VIEW and TABS; session records use buffer id
         * zero. Payload sizes derive from the legacy/v2 wire schemas. The
         * campaign dimensions are existing (E) discriminating fixtures. */
        if(c->window || r->sequence!=3 || r->buffer_id!=0 ||
           (r->size!=JOURNAL_WINDOW_LEGACY_BYTES && r->size!=JOURNAL_WINDOW_BYTES) ||
           read32(r->data)!=1373 || read32(r->data+sizeof(uint32_t))!=911 ||
           (r->size==JOURNAL_WINDOW_BYTES && read32(r->data+JOURNAL_WINDOW_LEGACY_BYTES)!=JOURNAL_WINDOW_CHOSEN)) return 1;
        c->window=true; return 0;
    }
    if(r->sequence<=3) return 1;
    journal_record edit=*r; edit.sequence-=3;
    if(operation(&c->script,NULL,&edit)) return 1;
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
static uint64_t durable_edits(const journal *j)
{ uint64_t seq=journal_get_stats(j).durable_sequence; return seq>=3?seq-3:0; }
static void child_run(const char *path, progress *p, uint64_t seed, bool before_pump)
{
    work_pool pool; journal *j=NULL;
    journal_options opt={.batch_bytes=1048576,.max_file_bytes=16777216};
    if(work_pool_init(&pool,1,0) || journal_open(&j,path,&pool,&opt)) { atomic_store(&p->error,1); _exit(2); }
    const uint64_t tabs[]={1,19,271};
    if(journal_set_view(j,1,&(journal_view){79,13,4099,27}) ||
       journal_set_tabs(j,tabs,3,2) || journal_set_window(j,1373,911)) {
        atomic_store(&p->error,6); _exit(2);
    }
    kill_model model={.rng=seed}; atomic_store_explicit(&p->ready,1,memory_order_release);
    for(uint64_t i=0;i<100000;i++) {
        /* issued is an upper bound even if SIGKILL interrupts append between
         * its write and publication below. Successful return is the page-cache
         * commit point required by P1.9g, independently of durable ack/pump. */
        atomic_store_explicit(&p->issued,i+1,memory_order_release);
        if(operation(&model,j,NULL)) { atomic_store(&p->error,2); _exit(3); }
        atomic_store_explicit(&p->page_cache,i+1,memory_order_release);
        if(before_pump) for(;;) nap();
        if((i&255u)==255u) {
            work_mailbox_drain(&pool,receive,j);
            atomic_store_explicit(&p->acknowledged,durable_edits(j),memory_order_release);
            int rc=journal_pump(j,now_ns(),false); if(rc && rc!=JOURNAL_BUSY) { atomic_store(&p->error,3); _exit(4); }
        }
        /* Bound pending volume below capacity despite slow worker. This is
         * script throttling, separate from the never-blocking append API. */
        if((i&4095u)==4095u) {
            if(journal_flush(j)) { atomic_store(&p->error,4); _exit(5); }
            atomic_store_explicit(&p->acknowledged,durable_edits(j),memory_order_release);
        }
    }
    if(journal_flush(j)) atomic_store(&p->error,5);
    atomic_store(&p->acknowledged,durable_edits(j));
    /* Stay available to be killed even when parent scheduling is slow. */
    for(;;) nap();
}
/* (P) Exercise the legacy WINDOW payload directly through this test's record
 * visitor. The fixture reuses the campaign dimensions; each wire byte comes
 * from width32 followed by height32, little-endian. */
#define T(c) do { if(!(c)) { fprintf(stderr,"journal_kill_test:%d FAIL %s\n",__LINE__,#c); return 1; } } while(0)
static int legacy_window_case(void)
{
    uint8_t bytes[JOURNAL_WINDOW_LEGACY_BYTES];
    uint64_t packed=1373ull+(911ull<<(sizeof(uint32_t)*CHAR_BIT)); /* (E) reused campaign dimensions; (P) width32 + height32 */
    for(size_t i=0;i<sizeof bytes;i++) bytes[i]=(uint8_t)(packed>>(CHAR_BIT*i));
    journal_record r={.type=JOURNAL_WINDOW,.sequence=3,.data=bytes,.size=sizeof bytes};
    /* (P) sequence follows the existing VIEW and TABS metadata records. */
    recovery c={0}; T(replay(&c,&r)==0 && c.window);
    puts("journal_kill_test: legacy WINDOW parser passed"); return 0;
}
/* (P) IDs, boolean/return sentinels and empty payloads below follow the
 * journal/POSIX APIs. Buffer id one names the fixture's sole buffer;
 * buffer id zero names session/group records.
 * (P) These boundaries come from the replacement action's wire records:
 * BEGIN, DELETE, INSERT, COMMIT. No random target or new timing budget. */
typedef enum group_boundary { AFTER_BEGIN, AFTER_DELETE, BEFORE_COMMIT, AFTER_COMMIT } group_boundary;
typedef struct group_hook { group_boundary at; _Atomic uint32_t reached; } group_hook;
static ssize_t group_append(void *ctx, int fd, const uint8_t *bytes, size_t size, uint64_t off)
{
    group_hook *h=ctx;
    /* (P) type32 follows magic32 and length32 in the wire header. */
    uint32_t type=read32(bytes+sizeof(uint32_t)*2u);
    if(h->at==BEFORE_COMMIT && type==JOURNAL_GROUP_COMMIT) {
        atomic_store(&h->reached,type); (void)kill(getpid(),SIGKILL);
        return -1; /* (P) POSIX write error if the kill unexpectedly returns. */
    }
    ssize_t written=pwrite(fd,bytes,size,(off_t)off);
    if(written==(ssize_t)size &&
       ((h->at==AFTER_BEGIN && type==JOURNAL_GROUP_BEGIN) ||
        (h->at==AFTER_DELETE && type==JOURNAL_DELETE) ||
        (h->at==AFTER_COMMIT && type==JOURNAL_GROUP_COMMIT))) {
        atomic_store(&h->reached,type); (void)kill(getpid(),SIGKILL);
    }
    return written;
}
static int grouped_child(const char *path, group_hook *h)
{
    work_pool pool; journal *j;
    T(work_pool_init(&pool,1,0)==0); /* (P) one journal worker */
    T(journal_open(&j,path,&pool,NULL)==0);
    T(journal_insert(j,1,0,(const uint8_t *)"before",strlen("before"))==0);
    T(journal_flush(j)==0); /* baseline is complete before arming crash seam */
    journal_io io={.ctx=h,.append_write=group_append}; T(journal_set_io(j,&io)==0);
    T(journal_append(j,JOURNAL_GROUP_BEGIN,0,NULL,0)==0);
    T(journal_delete(j,1,0,strlen("before"))==0);
    T(journal_flush(j)==0); /* PAD and batch transition inside the group */
    T(journal_insert(j,1,0,(const uint8_t *)"after",strlen("after"))==0);
    T(journal_append(j,JOURNAL_GROUP_COMMIT,0,NULL,0)==0);
    return 1; /* (P) every requested boundary must kill, never exit normally */
}
static int apply_group(void *ctx, const journal_record *r)
{ return journal_apply_piece(ctx,r); } /* markers must not reach the visitor */
static int group_kill_cases(void)
{
    group_hook *h=mmap(NULL,sizeof *h,PROT_READ|PROT_WRITE,MAP_SHARED|MAP_ANONYMOUS,-1,0);
    T(h!=MAP_FAILED);
    const uint32_t reached[]={JOURNAL_GROUP_BEGIN,JOURNAL_DELETE,JOURNAL_GROUP_COMMIT,JOURNAL_GROUP_COMMIT};
    /* (P) expected record counts derive from the baseline INSERT plus each
     * action record. PADs do not count; markers count but are not delivered. */
    const uint32_t committed[]={JOURNAL_INSERT,JOURNAL_GROUP_BEGIN,JOURNAL_DELETE,JOURNAL_INSERT,JOURNAL_GROUP_COMMIT};
    for(group_boundary at=AFTER_BEGIN;at<=AFTER_COMMIT;at++) {
        h->at=at; atomic_init(&h->reached,0);
        char path[]="build/journal-group-kill-XXXXXX"; int fd=mkstemp(path); T(fd>=0); T(close(fd)==0);
        pid_t child=fork(); T(child>=0); if(child==0) _exit(grouped_child(path,h));
        int status;
        while(waitpid(child,&status,0)<0) T(errno==EINTR);
        T(WIFSIGNALED(status) && WTERMSIG(status)==SIGKILL); T(atomic_load(&h->reached)==reached[at]);
        piece_allocator a=piece_default_allocator(); piece_tree *tree=piece_create(&a); T(tree);
        journal_replay_result rr; T(journal_replay_file(path,apply_group,tree,&rr)==0);
        const char *want=at==AFTER_COMMIT ? "after" : "before";
        uint8_t bytes[sizeof "before"]; T(piece_len(tree)==strlen(want));
        T(piece_read(tree,0,bytes,strlen(want))==0 && !memcmp(bytes,want,strlen(want)));
        uint64_t records=at==AFTER_COMMIT ? sizeof committed/sizeof committed[0] : 1u;
        /* (P) uncommitted replay keeps only the baseline INSERT. */
        T(rr.records==records && rr.last_sequence==records);
        T(rr.corrupt==(at!=AFTER_COMMIT));
        piece_destroy(tree); T(unlink(path)==0);
    }
    T(munmap(h,sizeof *h)==0);
    puts("journal_kill_test: BEGIN/DELETE/pre-COMMIT/post-COMMIT kills with PAD passed"); return 0;
}
int main(int argc, char **argv)
{
    T(legacy_window_case()==0); T(group_kill_cases()==0);
    unsigned trials=50;
    if(argc==2 && sscanf(argv[1],"--trials=%u",&trials)!=1) return 2;
    if(!trials || trials>10000) return 2;
    progress *p=mmap(NULL,sizeof *p,PROT_READ|PROT_WRITE,MAP_SHARED|MAP_ANONYMOUS,-1,0);
    if(p==MAP_FAILED) return 2;
    uint64_t total_replayed=0, total_issued=0, total_cache=0, total_ack=0;
    for(unsigned t=0;t<trials;t++) {
        atomic_init(&p->issued,0); atomic_init(&p->page_cache,0); atomic_init(&p->acknowledged,0); atomic_init(&p->ready,0); atomic_init(&p->error,0);
        uint64_t seed=0x9e3779b97f4a7c15ull^(uint64_t)(t+1)*0x100000001b3ull;
        kill_model target_model={.rng=seed^0x123456789abcdefull};
        uint64_t target=t==0?1:1+random_next(&target_model)%100000;
        char path[]="/tmp/journal-kill-XXXXXX"; int fd=mkstemp(path); if(fd<0) return 2; close(fd);
        pid_t child=fork(); if(child<0) return 2;
        if(child==0) child_run(path,p,seed,t==0);
        uint64_t start=now_ns(); bool timed_out=false;
        while(!atomic_load_explicit(&p->ready,memory_order_acquire) || atomic_load_explicit(&p->page_cache,memory_order_acquire)<target) {
            if(atomic_load(&p->error) || now_ns()-start>10000000000ull) { timed_out=true; break; }
            nap();
        }
        if(kill(child,SIGKILL)) return 2;
        int status=0; while(waitpid(child,&status,0)<0) if(errno!=EINTR) return 2;
        uint64_t issued=atomic_load(&p->issued), cached=atomic_load(&p->page_cache), ack=atomic_load(&p->acknowledged);
        piece_allocator a=piece_default_allocator();
        recovery restored={.script={.rng=seed},.tree=piece_create(&a)}; if(!restored.tree) return 2;
        journal_replay_result rr; int rc=journal_replay_file(path,replay,&restored,&rr);
        uint64_t replayed_edits=rr.last_sequence>=3?rr.last_sequence-3:0;
        kill_model actual=restored.script, expected=expected_prefix(seed,replayed_edits);
        uint8_t tree_bytes[256];
        bool tree_equal=piece_len(restored.tree)==expected.len &&
            piece_read(restored.tree,0,tree_bytes,expected.len)==0 && !memcmp(tree_bytes,expected.data,expected.len);
        piece_destroy(restored.tree);
        if(timed_out || atomic_load(&p->error) || !WIFSIGNALED(status) || WTERMSIG(status)!=SIGKILL || rc ||
           !restored.view || !restored.tabs || !restored.window || !tree_equal || rr.last_sequence!=actual.edits+3 ||
           rr.records!=actual.edits+3 || actual.edits>issued || actual.edits<cached || actual.edits<ack || actual.len!=expected.len || memcmp(actual.data,expected.data,actual.len)) {
            fprintf(stderr,"journal_kill_test: FAIL trial=%u target=%llu issued=%llu page_cache=%llu ack=%llu replay=%llu rc=%d error=%d\n",t,(unsigned long long)target,(unsigned long long)issued,(unsigned long long)cached,(unsigned long long)ack,(unsigned long long)actual.edits,rc,atomic_load(&p->error)); unlink(path); return 1;
        }
        total_issued+=issued; total_cache+=cached; total_replayed+=actual.edits; total_ack+=ack; unlink(path);
    }
    munmap(p,sizeof *p);
    printf("journal_kill_test: ok trials=%u script_edits=100000 issued=%llu page_cache=%llu replayed=%llu acknowledged=%llu (page_cache<=replayed<=issued; piece_tree==independent_byte_model_at_replayed; session_values_verified=1; trial0=kill_before_pump)\n",trials,(unsigned long long)total_issued,(unsigned long long)total_cache,(unsigned long long)total_replayed,(unsigned long long)total_ack);
    return 0;
}
