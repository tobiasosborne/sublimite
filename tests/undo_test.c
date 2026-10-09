#include "undo/undo.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <time.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"undo_test:%d: FAIL %s\n",__LINE__,#x); exit(1); } } while (0)
static undo_state state(uint8_t v) { undo_state s; memset(&s,v,sizeof s); return s; }
static void same(piece_tree *t, const char *s) {
    size_t n = strlen(s); uint8_t b[256]; CHECK(n <= sizeof b);
    if(piece_len(t)!=n) { fprintf(stderr,"expected [%s] len=%zu actual_len=%llu\n",s,n,(unsigned long long)piece_len(t)); }
    CHECK(piece_len(t) == n); CHECK(piece_read(t,0,b,n)==0);
    CHECK(memcmp(b,s,n)==0);
}
static void start(undo_log *u, piece_tree **t, size_t cap) {
    piece_allocator a = piece_default_allocator(); *t = piece_create(&a); CHECK(*t);
    CHECK(undo_init(u,*t,cap)==0);
}
static void finish(undo_log *u) { piece_tree *t=u->tree; undo_destroy(u); piece_destroy(t); }
static void ins(undo_log *u, uint64_t off, const char *s, uint64_t tm, uint8_t b, uint8_t a) {
    undo_state sb=state(b),sa=state(a);
    CHECK(undo_insert(u,off,(const uint8_t *)s,strlen(s),tm,&sb,&sa)==0);
}
static void del(undo_log *u, uint64_t off, uint64_t n, undo_kind k, uint64_t tm) {
    undo_state sb=state(7),sa=state(8); CHECK(undo_delete(u,off,n,k,tm,&sb,&sa)==0);
}
static void grouping(void) {
    undo_log u; piece_tree *t; undo_change c; start(&u,&t,128);
    ins(&u,0,"a",0,1,2); ins(&u,1,"b",UNDO_BURST_NS,2,3);
    ins(&u,2,"c",2*UNDO_BURST_NS+1,3,4); CHECK(undo_get_stats(&u).undo_groups==2);
    CHECK(undo_undo(&u,1,&c)==0 && c.groups==1 && c.state.bytes[0]==3); same(t,"ab");
    CHECK(undo_undo(&u,1,&c)==0 && c.groups==1 && c.state.bytes[0]==1); same(t,"");
    CHECK(undo_redo(&u,2,&c)==0 && c.groups==2 && c.state.bytes[0]==4); same(t,"abc");
    del(&u,2,1,UNDO_BACKSPACE,700000000); del(&u,1,1,UNDO_BACKSPACE,700000001);
    CHECK(undo_undo(&u,1,&c)==0); same(t,"abc");
    ins(&u,3,"d",0,9,10); CHECK(undo_get_stats(&u).redo_groups==0);
    del(&u,0,1,UNDO_DELETE,1); del(&u,0,1,UNDO_DELETE,2);
    CHECK(undo_undo(&u,1,&c)==0); same(t,"abcd");
    del(&u,3,1,UNDO_BACKSPACE,3); del(&u,0,1,UNDO_DELETE,4);
    CHECK(undo_get_stats(&u).undo_groups==5);
    CHECK(undo_undo(&u,1,&c)==0); same(t,"abc");
    undo_break_burst(&u); ins(&u,3,"X",10,0,0); undo_break_burst(&u); ins(&u,4,"Y",11,0,0);
    CHECK(undo_undo(&u,1,&c)==0); same(t,"abcX"); finish(&u);
}
static void explicit_cap(void) {
    undo_log u; piece_tree *t; undo_change c; start(&u,&t,32);
    undo_state b=state(11),a=state(12);
    ins(&u,0,"old",0,0,1); CHECK(undo_group_begin(&u,&b)==0);
    CHECK(undo_group_begin(&u,&b)==UNDO_ERR_BUSY);
    del(&u,0,3,UNDO_DELETE,1); ins(&u,0,"new",2,0,1); ins(&u,3,"!",3,0,1);
    CHECK(undo_undo(&u,1,&c)==UNDO_ERR_BUSY);
    CHECK(undo_group_end(&u,&a)==0); CHECK(undo_get_stats(&u).undo_groups==2);
    CHECK(undo_undo(&u,1,&c)==0 && c.state.bytes[0]==11); same(t,"old");
    CHECK(undo_redo(&u,1,&c)==0 && c.state.bytes[0]==12); same(t,"new!");
    CHECK(undo_set_cap(&u,3)==0); CHECK(undo_get_stats(&u).undo_groups==1);
    CHECK(undo_undo(&u,1,&c)==0); same(t,"old"); CHECK(undo_undo(&u,1,&c)==0 && c.groups==0);
    CHECK(undo_set_cap(&u,1)==0 && undo_get_stats(&u).redo_groups==0); same(t,"old");
    CHECK(undo_group_begin(&u,&b)==0); ins(&u,3,"a",0,0,0); ins(&u,4,"b",1,0,0);
    CHECK(undo_group_end(&u,&a)==0); CHECK(undo_get_stats(&u).records==0); same(t,"oldab"); finish(&u);
}
static void references(void) {
    for (int mapped=0; mapped<2; mapped++) {
        undo_log u; piece_tree *t; undo_change c; start(&u,&t,128);
        uint8_t orig[]="0123456789";
        CHECK((mapped ? piece_init_mapped(t,orig,10,NULL) : piece_init_copy(t,orig,10))==0);
        ins(&u,5,"ABC",0,1,2); del(&u,3,8,UNDO_DELETE,1); same(t,"01289");
        memset(orig+3,'Z',5); /* only bytes absent from the tree change */
        CHECK(undo_undo(&u,1,&c)==0); same(t,"01234ABC56789");
        CHECK(undo_redo(&u,1,&c)==0); same(t,"01289");
        CHECK(undo_undo(&u,1,&c)==0); same(t,"01234ABC56789");
        piece_snapshot *s=piece_snapshot_take(t); CHECK(s);
        CHECK(undo_undo(&u,1,&c)==0); same(t,"0123456789");
        uint8_t b[13]; CHECK(piece_snapshot_read(s,0,b,13)==0 && memcmp(b,"01234ABC56789",13)==0);
        piece_snapshot_release(s); CHECK(undo_redo(&u,2,&c)==0); same(t,"01289"); finish(&u);
    }
}

static void fragmented_insert(void) {
    undo_log u; piece_tree *t; undo_change c; start(&u,&t,64);
    CHECK(piece_init_copy(t,(const uint8_t *)"0123456789",10)==0);
    ins(&u,5,"abcdefghij",0,1,2);
    for(size_t i=0;i<9;i++) ins(&u,0,"Y",i+1,3,4);
    del(&u,0,17,UNDO_DELETE,20); same(t,"defghij56789");
    CHECK(undo_undo(&u,11,&c)==0 && c.groups==11);
    same(t,"0123456789");
    /* The insert now consists of a copied prefix plus its original add suffix. */
    CHECK(undo_get_stats(&u).records==12 && c.records==12);
    CHECK(undo_redo(&u,10,&c)==0 && c.groups==10); same(t,"YYYYYYYYY01234abcdefghij56789");
    piece_snapshot *snap=piece_snapshot_take(t); CHECK(snap);
    CHECK(undo_redo(&u,1,&c)==0); same(t,"defghij56789");
    uint8_t b[29]; CHECK(piece_snapshot_read(snap,0,b,sizeof b)==0);
    CHECK(memcmp(b,"YYYYYYYYY01234abcdefghij56789",sizeof b)==0);
    piece_snapshot_release(snap); CHECK(undo_undo(&u,11,&c)==0); same(t,"0123456789");
    CHECK(undo_set_cap(&u,1)==0 && undo_get_stats(&u).records==0);finish(&u);
}
static void eight_span_capture(void) {
    undo_log u;piece_tree *t;start(&u,&t,64);undo_state s=state(3);undo_change c;
    const char *text="abcdefghabcdefghabcdefghabcdefghabcdefghabcdefghabcdefghabcdefgh";
    ins(&u,0,text,0,1,2);
    /* Flatten seven disjoint blocks with nine reverse-ordered noise spans,
     * then undo noise. Each block keeps its protected copied add location. */
    for(size_t block=0;block<7;block++) {
        uint64_t off=8*block;CHECK(undo_group_begin(&u,&s)==0);
        for(size_t j=0;j<9;j++) ins(&u,off,"X",j,0,0);
        CHECK(undo_group_end(&u,&s)==0);del(&u,off,17,UNDO_DELETE,20);
        CHECK(undo_undo(&u,2,&c)==0 && c.groups==2);same(t,text);
    }
    CHECK(undo_undo_slice(&u,1,1,UINT64_MAX,&c)==0 && c.operations==1 && c.records==8 && c.groups==1 && c.state.bytes[0]==1);
    same(t,"");CHECK(undo_get_stats(&u).records==18);
    CHECK(undo_redo(&u,1,&c)==0 && c.records==8 && c.state.bytes[0]==2);same(t,text);
    CHECK(undo_set_cap(&u,7)==0 && undo_get_stats(&u).records==0);
    same(t,text);finish(&u);
}

static void edge_cases(void) {
    undo_log u; piece_tree *t; undo_change c; start(&u,&t,8);undo_state s=state(0);
    CHECK(undo_set_cap(&u,0)==UNDO_ERR_RANGE && undo_set_cap(&u,9)==UNDO_ERR_RANGE);
    CHECK(undo_insert(&u,1,(const uint8_t *)"x",1,0,&s,&s)==UNDO_ERR_RANGE);
    CHECK(undo_insert(&u,0,NULL,1,0,&s,&s)==UNDO_ERR_RANGE);
    CHECK(undo_delete(&u,0,1,UNDO_DELETE,0,&s,&s)==UNDO_ERR_RANGE);
    CHECK(undo_delete(&u,0,0,UNDO_INSERT,0,&s,&s)==UNDO_ERR_RANGE);
    ins(&u,0,"a",10,0,0); ins(&u,1,"b",9,0,0); /* decreasing clock breaks burst */
    CHECK(undo_get_stats(&u).undo_groups==2); CHECK(undo_undo(&u,1,&c)==0);same(t,"a");
    CHECK(c.off==1 && c.len==1 && c.records==1 && c.groups==1);
    CHECK(undo_insert(&u,0,NULL,0,0,&s,&s)==0);
    CHECK(undo_delete(&u,0,0,UNDO_DELETE,0,&s,&s)==0);
    CHECK(undo_group_begin(&u,&s)==0 && undo_group_end(&u,&s)==0);
    CHECK(undo_get_stats(&u).redo_groups==1);CHECK(undo_redo(&u,0,&c)==0 && c.records==0);
    CHECK(undo_redo(&u,1,&c)==0);same(t,"ab");
    undo_clear(&u);CHECK(undo_get_stats(&u).live_bytes==0);same(t,"ab");
    CHECK(undo_set_cap(&u,1)==0);ins(&u,2,"c",0,0,0);ins(&u,3,"d",1,0,0);
    CHECK(undo_get_stats(&u).records==0);same(t,"abcd");
    ins(&u,4,"e",2,0,0);CHECK(undo_undo(&u,1,&c)==0);same(t,"abcd");finish(&u);
}

typedef struct failing { piece_allocator backing; size_t calls, fail; int persistent; } failing;
static void *fall(void *ctx,size_t n) { failing *f=ctx; f->calls++; if(f->fail && (f->persistent?f->calls>=f->fail:f->calls==f->fail)) return NULL; return f->backing.alloc(f->backing.ctx,n); }
static void ffree(void *ctx,void *p,size_t n) { failing *f=ctx; f->backing.free(f->backing.ctx,p,n); }
static void failures(void) {
    size_t saw=0;
    for(size_t fail=1;fail<8;fail++) {
        failing f={piece_default_allocator(),0,0,0}; piece_allocator a={&f,fall,ffree};
        piece_tree *t=piece_create(&a); CHECK(t); undo_log u; CHECK(undo_init(&u,t,128)==0);
        CHECK(piece_init_copy(t,(const uint8_t *)"orig",4)==0);
        undo_state s=state(0); f.calls=0; f.fail=fail;
        int rc=undo_delete(&u,0,4,UNDO_DELETE,0,&s,&s);
        if(rc) { CHECK(rc==PIECE_ERR_NOMEM); same(t,"orig"); CHECK(undo_get_stats(&u).records==0); saw++; }
        f.fail=0; finish(&u);
    }
    CHECK(saw>0);
    failing f={piece_default_allocator(),0,0,0}; piece_allocator a={&f,fall,ffree};
    piece_tree *t=piece_create(&a); undo_log u; CHECK(t && undo_init(&u,t,128)==0);
    ins(&u,0,"x",0,1,2); undo_change c; CHECK(undo_undo(&u,1,&c)==0);
    f.calls=0; f.fail=1; uint8_t big[70000]; memset(big,'z',sizeof big); undo_state s=state(0);
    CHECK(undo_insert(&u,0,big,sizeof big,0,&s,&s)==PIECE_ERR_NOMEM);
    same(t,""); CHECK(undo_get_stats(&u).redo_groups==1); f.fail=0;
    CHECK(undo_redo(&u,1,&c)==0); same(t,"x"); finish(&u);
}

static void replay_failures(void) {
    enum { BIG=1024*1024 };
    uint8_t *text=malloc(BIG),*before=malloc(BIG+1),*after=malloc(BIG+1);
    CHECK(text && before && after);
    for(size_t i=0;i<BIG;i++) text[i]=(uint8_t)('a'+i%26);
    size_t failed=0;
    for(size_t mode=0;mode<2;mode++) for(size_t k=1;k<=32;k++) {
        failing f={piece_default_allocator(),0,0,0};piece_allocator a={&f,fall,ffree};
        piece_tree *t=piece_create(&a);CHECK(t);undo_log u;CHECK(undo_init(&u,t,64)==0);
        undo_state s=state(5);
        CHECK(undo_group_begin(&u,&s)==0);
        CHECK(undo_insert(&u,0,text,BIG,0,&s,&s)==0);
        if(mode) ins(&u,BIG,"Z",1,1,2);
        CHECK(undo_group_end(&u,&s)==0);
        CHECK(undo_group_begin(&u,&s)==0);
        for(size_t i=0;i<9;i++) ins(&u,2*i+1,"X",i,0,0);
        CHECK(undo_group_end(&u,&s)==0);
        del(&u,0,BIG+9,UNDO_DELETE,10);
        undo_change c;CHECK(undo_undo(&u,2,&c)==0 && c.groups==2);
        size_t bn=(size_t)piece_len(t);
        CHECK(bn==BIG+mode && piece_read(t,0,before,bn)==0 && memcmp(before,text,BIG)==0);
        f.calls=0;f.fail=k;
        int rc=undo_undo(&u,1,&c);
        if(rc) {
            CHECK(rc==PIECE_ERR_NOMEM);failed++;
            CHECK(c.records==0 && c.operations==0 && c.len==0 && c.groups==0 && !c.has_state && !u.partial);
            CHECK(piece_len(t)==bn && piece_read(t,0,after,bn)==0 && memcmp(before,after,bn)==0);
            CHECK(undo_insert(&u,0,NULL,0,0,&s,&s)==UNDO_OK);
            undo_change rejected;CHECK(undo_redo(&u,0,&rejected)==UNDO_OK);
            size_t pn=(size_t)piece_len(t);CHECK(piece_read(t,0,after,pn)==0);
            f.calls=0;f.fail=1;f.persistent=1;
            for(size_t retry=0;retry<3;retry++) {
                CHECK(undo_undo(&u,1,&c)==UNDO_ERR_NOMEM && c.records==0 && c.groups==0 && !c.has_state);
                CHECK(piece_len(t)==pn && piece_read(t,0,before,pn)==0 && memcmp(before,after,pn)==0);
                CHECK(!u.partial && !undo_replay_snapshot(&u));
            }
            f.fail=0;f.persistent=0;CHECK(undo_undo(&u,1,&c)==0 && c.groups==1);
            same(t,"");CHECK(undo_redo(&u,1,&c)==0);
            CHECK(piece_len(t)==bn && piece_read(t,0,after,bn)==0 && memcmp(text,after,BIG)==0 && (!mode || after[BIG]=='Z'));
        }
        f.fail=0;finish(&u);
    }
    CHECK(failed>0);
    printf("undo_test: replay allocator failures=%zu (aborted, retried)\n",failed);
    free(after);free(before);free(text);
}

static void pool_exhaustion(void) {
    undo_log u;piece_tree *t;start(&u,&t,1);undo_state s=state(0);
    CHECK(undo_group_begin(&u,&s)==0);for(size_t i=0;i<9;i++) ins(&u,i,"a",i,0,0);
    undo_stats before=undo_get_stats(&u);
    CHECK(undo_insert(&u,9,(const uint8_t *)"c",1,9,&s,&s)==UNDO_ERR_NOMEM);
    undo_stats after=undo_get_stats(&u);CHECK(after.records==before.records && after.live_bytes==before.live_bytes);
    same(t,"aaaaaaaaa");CHECK(undo_group_end(&u,&s)==0 && undo_get_stats(&u).records==0);
    ins(&u,9,"c",10,0,0);undo_change c;CHECK(undo_undo(&u,1,&c)==0);same(t,"aaaaaaaaa");finish(&u);
}

static void *arena_alloc(void *ctx,size_t n) { return edit_arena_alloc(ctx,n,16); }
static void arena_free(void *ctx,void *p,size_t n) { (void)ctx;(void)p;(void)n; }
static void no_malloc(void) {
    edit_arena arena; CHECK(edit_arena_init(&arena,16*1024*1024)==0);
    piece_allocator a={&arena,arena_alloc,arena_free}; piece_tree *t=piece_create(&a); CHECK(t);
    undo_log u; CHECK(undo_init(&u,t,10016)==0); undo_state s=state(0);
    CHECK(undo_insert(&u,0,(const uint8_t *)"x",1,0,&s,&s)==0);
    edit_malloc_guard_begin();
    for(size_t i=1;i<=10000;i++) CHECK(undo_insert(&u,i,(const uint8_t *)"x",1,(uint64_t)i,&s,&s)==0);
    size_t calls=edit_malloc_guard_end(); CHECK(!edit_malloc_guard_active() || calls==0);
    undo_stats st=undo_get_stats(&u); CHECK(st.records==10001 && st.live_bytes==64*st.records);
    printf("undo_test: 10000 keys mallocs=%zu guard=%s bytes/record=%zu\n",calls,edit_malloc_guard_active()?"active":"ASan-inert",st.live_bytes/st.records);
    undo_change c; CHECK(undo_undo(&u,1,&c)==0 && c.groups==1); same(t,"");
    CHECK(undo_redo(&u,1,&c)==0 && piece_len(t)==10001); finish(&u); edit_arena_free(&arena);
}
static void original_tests(void) { grouping(); explicit_cap(); references(); fragmented_insert(); eight_span_capture(); edge_cases(); failures(); replay_failures(); pool_exhaustion(); no_malloc(); puts("undo_test: all passed"); }

/* P1.5d regressions, written before implementation. */
static void review_2(void) {
    undo_log u; piece_tree *t; undo_change c; start(&u,&t,12);
    CHECK(piece_init_copy(t,(const uint8_t *)"0123456789",10)==0);
    undo_state s=state(0); CHECK(undo_group_begin(&u,&s)==0);
    ins(&u,10,"Z",0,0,0); ins(&u,5,"abcdefghij",1,0,0);
    CHECK(undo_group_end(&u,&s)==0);
    for(size_t i=0;i<9;i++) ins(&u,0,"Y",i+2,0,0);
    del(&u,0,17,UNDO_DELETE,20);
    CHECK(undo_undo(&u,10,&c)==0 && c.groups==10);
    CHECK(undo_undo(&u,1,&c)==0 && c.groups==1); same(t,"0123456789");
    finish(&u);
    start(&u,&t,1); CHECK(undo_group_begin(&u,&s)==0);
    for(size_t i=0;i<9;i++) ins(&u,i,"a",i,0,0);
    CHECK(undo_insert(&u,9,(const uint8_t *)"b",1,9,&s,&s)==UNDO_ERR_NOMEM);
    finish(&u); puts("review 2: expansion and one-slot admission passed");
}
static size_t resident(const edit_pool *p) {
    size_t pages=p->map_bytes/4096; unsigned char *v=malloc(pages); CHECK(v);
    CHECK(mincore(p->base,p->map_bytes,v)==0); size_t n=0;
    for(size_t i=0;i<pages;i++) if(v[i]&1u) n+=4096;
    free(v); return n;
}
static void review_3(void) {
    undo_log u;piece_tree *t;start(&u,&t,100000);
    for(size_t i=0;i<100000;i++) ins(&u,i,"a",i,0,0);
    undo_clear(&u); CHECK(undo_set_cap(&u,1)==0);
    size_t n=resident(&u.pool); printf("review 3: cleared resident pool=%zu allowance=4096\n",n);
    CHECK(n<=4096 && undo_get_stats(&u).committed_bytes==0);
    /* Reclaim an old half while relocating the live suffix, including both
     * boundary records. Every remaining slot occupies the packed prefix. */
    CHECK(undo_set_cap(&u,100000)==0);undo_break_burst(&u);
    for(size_t i=0;i<50000;i++) ins(&u,100000+i,"b",i,1,2);
    undo_break_burst(&u);
    for(size_t i=0;i<50000;i++) ins(&u,150000+i,"c",i,3,4);
    CHECK(undo_set_cap(&u,50000)==0);
    while(undo_maintain(&u,16)) {}
    undo_stats packed=undo_get_stats(&u);
    CHECK(packed.records==50000 && packed.retired_records==0 && packed.live_bytes==50000*64);
    CHECK(packed.committed_bytes-packed.live_bytes<u.page_bytes);
    CHECK(resident(&u.pool)<=packed.committed_bytes);
    undo_change c;CHECK(undo_undo(&u,1,&c)==0 && c.groups==1 && c.state.bytes[0]==3 && piece_len(t)==150000);
    CHECK(undo_redo(&u,1,&c)==0 && c.groups==1 && c.state.bytes[0]==4 && piece_len(t)==200000);
    undo_clear(&u);CHECK(resident(&u.pool)==0);finish(&u);
}
static void review_4(void) {
    undo_log u;piece_tree *t;undo_change c;start(&u,&t,100000);
    for(size_t i=0;i<100000;i++) ins(&u,i,"a",i,0,0);
    CHECK(undo_undo(&u,1,&c)==0);size_t before=u.pool.live;
    ins(&u,0,"b",100001,0,0);
    size_t reclaimed=before+1-u.pool.live;
    printf("review 4: redo records reclaimed by key=%zu bound=16\n",reclaimed);
    CHECK(reclaimed<=16); CHECK(undo_get_stats(&u).redo_groups==0);
    for(size_t i=1;i<1024;i++) {
        before=u.pool.live;ins(&u,i,"b",100001+i,5,6);
        CHECK(before+1-u.pool.live<=16);
    }
    while(undo_maintain(&u,16)) {}
    CHECK(undo_get_stats(&u).records==1024 && undo_get_stats(&u).retired_records==0);
    CHECK(undo_undo(&u,1,&c)==0 && c.groups==1 && c.state.bytes[0]==0);same(t,"");
    CHECK(undo_redo(&u,1,&c)==0 && c.groups==1 && c.state.bytes[0]==6 && piece_len(t)==1024);finish(&u);
    start(&u,&t,100000);
    for(size_t i=0;i<100000;i++) ins(&u,i,"a",i,0,0);
    before=u.pool.live;ins(&u,100000,"b",100001,0,0);
    CHECK(before+1-u.pool.live<=16);CHECK(undo_get_stats(&u).records==0);finish(&u);
}
static void review_5(void) {
    undo_log u;piece_tree *t;undo_change c;start(&u,&t,100000);
    for(size_t i=0;i<100000;i++) ins(&u,i,"a",i,1,2);
    int rc=undo_undo_slice(&u,1,1,UINT64_MAX,&c);
    printf("review 5: first slice rc=%d records=%zu groups=%zu\n",rc,c.records,c.groups);
    CHECK(rc==UNDO_MORE && c.records==1 && c.groups==0 && !c.has_state);
    const piece_snapshot *view=undo_replay_snapshot(&u);CHECK(view && piece_snapshot_len(view)==100000);
    uint8_t b[32];CHECK(piece_snapshot_read(view,0,b,sizeof b)==0);
    for(size_t i=0;i<sizeof b;i++) CHECK(b[i]=='a');
    CHECK(undo_redo_slice(&u,1,1,UINT64_MAX,&c)==UNDO_ERR_BUSY);
    CHECK(undo_undo_slice(&u,1,32,0,&c)==UNDO_MORE && c.records==0 && c.operations==0 && c.groups==0);
    CHECK(undo_replay_snapshot(&u)==view);
    size_t records=1,groups=0;
    do {rc=undo_undo_slice(&u,1,32,UINT64_MAX,&c);CHECK(c.records<=32);records+=c.records;groups+=c.groups;} while(rc==UNDO_MORE);
    CHECK(rc==0 && records==100000 && groups==1 && c.state.bytes[0]==1);
    CHECK(!undo_replay_snapshot(&u));same(t,"");finish(&u);
}
static void review_1(void) {
    /* Default regression: the piece checkpoint must restore both tree and log. */
    /* Review mode=1: NOMEM after removing Z must leave the entire group intact. */
    enum { BIG=1024*1024 };uint8_t *text=malloc(BIG),*b=malloc(BIG+1);CHECK(text&&b);memset(text,'a',BIG);
    size_t failed=0;
    for(size_t k=1;k<=20;k++) {
        failing f={piece_default_allocator(),0,0,0};piece_allocator a={&f,fall,ffree};piece_tree *t=piece_create(&a);CHECK(t);
        undo_log u;CHECK(undo_init(&u,t,64)==0);undo_state st=state(5);undo_change c;
        CHECK(undo_group_begin(&u,&st)==0);CHECK(undo_insert(&u,0,text,BIG,0,&st,&st)==0);ins(&u,BIG,"Z",1,0,0);CHECK(undo_group_end(&u,&st)==0);
        CHECK(undo_group_begin(&u,&st)==0);for(size_t i=0;i<9;i++) ins(&u,2*i+1,"X",i,0,0);CHECK(undo_group_end(&u,&st)==0);del(&u,0,BIG+9,UNDO_DELETE,10);
        CHECK(undo_undo(&u,2,&c)==0);f.calls=0;f.fail=k;int rc=undo_undo(&u,1,&c);
        if(rc==UNDO_ERR_NOMEM) {
            failed++;printf("review 1: allocation=%zu successful_prefix=%zu tree_len=%llu original=%d\n",k,c.records,(unsigned long long)piece_len(t),BIG+1);
            CHECK(c.records==0 && c.groups==0 && !c.has_state && u.partial==0);
            CHECK(piece_len(t)==BIG+1 && piece_read(t,0,b,BIG+1)==0 && memcmp(b,text,BIG)==0 && b[BIG]=='Z');
        } else CHECK(rc==0 && c.groups==1 && piece_len(t)==0);
        f.fail=0;finish(&u);
    }
    CHECK(failed>0);free(b);free(text);
}
static void review_8(void) {
    /* Redo a large mixed group under every allocation failure and persistent retries. */
    enum { BIG=1024*1024 };uint8_t *text=malloc(BIG);CHECK(text);memset(text,'a',BIG);size_t failures_seen=0;
    for(size_t k=1;k<=40;k++) {
        failing f={piece_default_allocator(),0,0,0};piece_allocator a={&f,fall,ffree};piece_tree *t=piece_create(&a);CHECK(t);undo_log u;CHECK(undo_init(&u,t,64)==0);
        undo_state st=state(9);undo_change c;CHECK(undo_group_begin(&u,&st)==0);ins(&u,0,"Z",0,0,0);CHECK(undo_insert(&u,1,text,BIG,1,&st,&st)==0);CHECK(undo_group_end(&u,&st)==0);
        CHECK(undo_undo(&u,1,&c)==0);f.calls=0;f.fail=k;int rc=undo_redo(&u,1,&c);
        if(rc==UNDO_ERR_NOMEM) {
            failures_seen++;CHECK(!u.partial && !undo_replay_snapshot(&u));
            CHECK(c.records==0 && c.operations==0 && c.len==0 && c.groups==0 && !c.has_state);
            size_t pn=(size_t)piece_len(t);CHECK(pn==0);uint8_t prior[1];CHECK(piece_read(t,0,prior,pn)==0);
            f.calls=0;f.fail=1;f.persistent=1;
            for(size_t retry=0;retry<3;retry++) {
                CHECK(undo_redo(&u,1,&c)==UNDO_ERR_NOMEM && c.records==0 && c.groups==0 && !c.has_state && c.len==0);
                uint8_t current[1];CHECK(piece_len(t)==pn && piece_read(t,0,current,pn)==0 && memcmp(prior,current,pn)==0);
            }
            f.fail=0;f.persistent=0;CHECK(undo_redo(&u,1,&c)==0 && c.groups==1 && c.state.bytes[0]==9);
            CHECK(c.off==pn && c.len==BIG+1-pn);
        }
        f.fail=0;finish(&u);
    }
    CHECK(failures_seen>0);free(text);printf("review 8: redo failure positions=%zu passed\n",failures_seen);
}
static void batch_failure_reporting(void) {
    enum { BIG=1024*1024 };uint8_t *text=malloc(BIG),*bytes=malloc(BIG+2);CHECK(text && bytes);memset(text,'a',BIG);
    size_t after_group=0;
    for(size_t k=1;k<=32;k++) {
        failing f={piece_default_allocator(),0,0,0};piece_allocator pa={&f,fall,ffree};piece_tree *t=piece_create(&pa);CHECK(t);
        undo_log u;CHECK(undo_init(&u,t,64)==0);undo_change c;undo_state bs=state(7),as=state(9);
        ins(&u,0,"Q",0,2,3);CHECK(undo_group_begin(&u,&bs)==0);ins(&u,1,"Z",1,7,8);
        CHECK(undo_insert(&u,2,text,BIG,2,&bs,&as)==0 && undo_group_end(&u,&as)==0);
        CHECK(undo_undo(&u,2,&c)==0 && c.groups==2);same(t,"");
        f.calls=0;f.fail=k;int rc=undo_redo(&u,2,&c);
        if(rc==UNDO_ERR_NOMEM) {
            CHECK(c.groups<=1 && c.has_state==(c.groups!=0));
            size_t done=c.groups,pn=(size_t)piece_len(t);CHECK(pn==done && !u.partial && !undo_replay_snapshot(&u));
            CHECK(c.records==done && c.operations==done);
            if(done) { after_group++;CHECK(c.state.bytes[0]==3 && c.off==0 && c.len==1);same(t,"Q"); }
            f.fail=0;CHECK(undo_redo(&u,2-done,&c)==0 && c.groups==2-done && c.state.bytes[0]==9);
            CHECK(c.off==pn && c.len==BIG+2-pn);
        } else CHECK(rc==0 && c.groups==2 && c.state.bytes[0]==9 && c.off==0 && c.len==BIG+2);
        CHECK(piece_len(t)==BIG+2 && piece_read(t,0,bytes,BIG+2)==0 && bytes[0]=='Q' && bytes[1]=='Z' && memcmp(bytes+2,text,BIG)==0);
        CHECK(!undo_replay_snapshot(&u));f.fail=0;finish(&u);
    }
    CHECK(after_group>0);free(bytes);free(text);puts("review 8: completed-group state and dirty range across batch failures passed");
}
static void undo_batch_failure_reporting(void) {
    enum { BIG=1024*1024 };uint8_t *text=malloc(BIG),*bytes=malloc(BIG+2);CHECK(text && bytes);memset(text,'a',BIG);
    size_t after_group=0;
    for(size_t k=1;k<=40;k++) {
        failing f={piece_default_allocator(),0,0,0};piece_allocator pa={&f,fall,ffree};piece_tree *t=piece_create(&pa);CHECK(t);
        undo_log u;CHECK(undo_init(&u,t,64)==0);undo_change c;undo_state bs=state(7),as=state(9);
        CHECK(undo_group_begin(&u,&bs)==0);CHECK(undo_insert(&u,0,text,BIG,0,&bs,&as)==0);
        ins(&u,BIG,"Z",1,7,8);CHECK(undo_group_end(&u,&as)==0);
        /* Give the first insertion >8 discontinuous add spans on first undo. */
        CHECK(undo_group_begin(&u,&bs)==0);for(size_t i=0;i<9;i++) ins(&u,2*i+1,"X",i,0,0);
        CHECK(undo_group_end(&u,&as)==0);del(&u,0,BIG+9,UNDO_DELETE,10);
        CHECK(undo_undo(&u,2,&c)==0);ins(&u,BIG+1,"Q",11,2,3);
        f.calls=0;f.fail=k;f.persistent=1;int rc=undo_undo(&u,2,&c);
        if(rc==UNDO_ERR_NOMEM) {
            CHECK(c.groups<=1 && c.has_state==(c.groups!=0) && !u.partial && !undo_replay_snapshot(&u));
            size_t done=c.groups;CHECK(c.records==done && c.operations==done && piece_len(t)==BIG+2-done);
            if(done) { after_group++;CHECK(c.state.bytes[0]==2 && c.off==BIG+1 && c.len==1); }
            CHECK(piece_read(t,0,bytes,BIG+2-done)==0 && memcmp(bytes,text,BIG)==0 && bytes[BIG]=='Z' && (done || bytes[BIG+1]=='Q'));
            f.fail=0;CHECK(undo_undo(&u,2-done,&c)==0 && c.groups==2-done && c.state.bytes[0]==7 && c.off==0 && c.len==BIG+2-done);
        } else CHECK(rc==0 && c.groups==2 && c.state.bytes[0]==7 && c.off==0 && c.len==BIG+2);
        same(t,"");f.fail=0;CHECK(undo_redo(&u,2,&c)==0 && c.groups==2 && c.state.bytes[0]==3);
        CHECK(piece_len(t)==BIG+2 && piece_read(t,0,bytes,BIG+2)==0 && memcmp(bytes,text,BIG)==0 && bytes[BIG]=='Z' && bytes[BIG+1]=='Q');
        finish(&u);
    }
    CHECK(after_group>0);free(bytes);free(text);puts("review 1: completed undo groups survive a later group abort");
}
enum { ATOMIC_BIG=1024*1024 };
static void atomic_fixture(undo_log *u,piece_tree **tree,failing *f,const uint8_t *text,int reverse) {
    piece_allocator a={f,fall,ffree};*tree=piece_create(&a);CHECK(*tree);
    CHECK(undo_init(u,*tree,64)==0);undo_state bs=state(11),as=state(12);undo_change c;
    if(reverse) {
        CHECK(undo_group_begin(u,&bs)==0);
        CHECK(undo_insert(u,0,text,ATOMIC_BIG,0,&bs,&as)==0);
        ins(u,ATOMIC_BIG,"abcdefghij",1,0,0);ins(u,ATOMIC_BIG+10,"Z",2,0,0);
        CHECK(undo_group_end(u,&as)==0);
        CHECK(undo_group_begin(u,&bs)==0);
        for(size_t i=0;i<9;i++) ins(u,2*i+1,"X",i,0,0);
        CHECK(undo_group_end(u,&as)==0);del(u,0,ATOMIC_BIG+14,UNDO_DELETE,10);
        CHECK(undo_undo(u,2,&c)==0 && c.groups==2);
        /* The middle insertion now crosses the copied prefix and old add tail.
         * Its first capture expands before the large first insertion fails. */
    } else {
        CHECK(piece_init_copy(*tree,(const uint8_t *)"orig\n",5)==0);
        CHECK(undo_group_begin(u,&bs)==0);del(u,1,2,UNDO_DELETE,0);
        ins(u,1,"Z",1,0,0);CHECK(undo_insert(u,2,text,ATOMIC_BIG,2,&bs,&as)==0);
        del(u,0,1,UNDO_DELETE,3);CHECK(undo_group_end(u,&as)==0);
        CHECK(undo_undo(u,1,&c)==0 && c.groups==1);same(*tree,"orig\n");
    }
}
typedef struct saved_log {
    uint8_t *records,*bytes;
    size_t fresh,count,applied,len;
    uint32_t head,tail,cursor;
    uint64_t pieces,lines;
    undo_stats stats;
} saved_log;
static saved_log save_log(const undo_log *u) {
    saved_log s={0};s.fresh=u->pool.fresh;s.count=u->count;s.applied=u->applied_count;
    s.head=u->head;s.tail=u->tail;s.cursor=u->cursor;s.len=(size_t)piece_len(u->tree);
    s.pieces=piece_piece_count(u->tree);s.lines=piece_line_count(u->tree);s.stats=undo_get_stats(u);
    s.records=malloc(s.fresh*64);s.bytes=malloc(s.len+1);CHECK(s.records && s.bytes);
    memcpy(s.records,u->pool.base,s.fresh*64);CHECK(piece_read(u->tree,0,s.bytes,s.len)==0);return s;
}
static void check_saved(const undo_log *u,const saved_log *s) {
    CHECK(!u->partial && !undo_replay_snapshot(u));
    CHECK(u->head==s->head && u->tail==s->tail && u->cursor==s->cursor);
    CHECK(u->count==s->count && u->applied_count==s->applied && u->pool.fresh==s->fresh);
    CHECK(memcmp(u->pool.base,s->records,s->fresh*64)==0);
    CHECK(piece_len(u->tree)==s->len && piece_piece_count(u->tree)==s->pieces && piece_line_count(u->tree)==s->lines);
    uint8_t *b=malloc(s->len+1);CHECK(b);CHECK(piece_read(u->tree,0,b,s->len)==0 && memcmp(b,s->bytes,s->len)==0);free(b);
    undo_stats st=undo_get_stats(u);CHECK(st.records==s->stats.records && st.undo_groups==s->stats.undo_groups && st.redo_groups==s->stats.redo_groups);
    CHECK(st.live_bytes==s->stats.live_bytes && st.retired_records==s->stats.retired_records);
}
static void free_saved(saved_log *s) { free(s->records);free(s->bytes); }
static int atomic_replay(undo_log *u,int reverse,size_t budget,undo_change *c) {
    return reverse?undo_undo_slice(u,1,budget,UINT64_MAX,c):undo_redo_slice(u,1,budget,UINT64_MAX,c);
}
static void atomic_sweep(void) {
    uint8_t *text=malloc(ATOMIC_BIG);CHECK(text);
    for(size_t i=0;i<ATOMIC_BIG;i++) text[i]=(uint8_t)(i%251);
    size_t failures_seen=0,after_slice=0;
    for(int reverse=0;reverse<2;reverse++) for(int sliced=0;sliced<2;sliced++) {
        size_t allocations[4]={0},ops=0;
        failing healthy={piece_default_allocator(),0,0,0};undo_log probe;piece_tree *pt;
        atomic_fixture(&probe,&pt,&healthy,text,reverse);
        int rc;undo_change c;
        do {
            healthy.calls=0;rc=atomic_replay(&probe,reverse,sliced?1:SIZE_MAX,&c);
            CHECK(ops<4 && (rc==UNDO_MORE || rc==UNDO_OK));allocations[ops++]=healthy.calls;
        } while(rc==UNDO_MORE);
        finish(&probe);
        for(size_t op=0;op<ops;op++) for(int persistent=0;persistent<2;persistent++)
            for(size_t k=1;k<=allocations[op]+1;k++) {
                failing f={piece_default_allocator(),0,0,0};undo_log u;piece_tree *t;
                atomic_fixture(&u,&t,&f,text,reverse);saved_log saved=save_log(&u);
                for(size_t prefix=0;prefix<op;prefix++) CHECK(atomic_replay(&u,reverse,1,&c)==UNDO_MORE);
                f.calls=0;f.fail=k;f.persistent=persistent;
                rc=atomic_replay(&u,reverse,sliced?1:SIZE_MAX,&c);
                if(k<=allocations[op]) {
                    CHECK(rc==UNDO_ERR_NOMEM && c.records==0 && c.operations==0 && c.groups==0 && !c.has_state && c.len==0);
                    failures_seen++;if(op) after_slice++;check_saved(&u,&saved);
                    f.calls=0;f.fail=1;f.persistent=1;
                    for(size_t retry=0;retry<3;retry++) {
                        CHECK(atomic_replay(&u,reverse,1,&c)==UNDO_ERR_NOMEM && c.records==0 && c.groups==0);
                        check_saved(&u,&saved);
                    }
                    f.fail=0;CHECK(atomic_replay(&u,reverse,SIZE_MAX,&c)==UNDO_OK && c.groups==1);
                    CHECK(c.state.bytes[0]==(reverse?11:12));
                    /* A second round uses only committed refs; transaction-only
                     * capture refs must have been discarded before the retry. */
                    CHECK(atomic_replay(&u,!reverse,SIZE_MAX,&c)==UNDO_OK && c.groups==1);
                    CHECK(atomic_replay(&u,reverse,SIZE_MAX,&c)==UNDO_OK && c.groups==1);
                } else CHECK(rc==UNDO_OK || rc==UNDO_MORE);
                f.fail=0;free_saved(&saved);finish(&u);
            }
    }
    CHECK(failures_seen && after_slice);free(text);
    printf("review 1: exhaustive allocator sweep failures=%zu after_slice=%zu; bytes, queries, records and cursors restored\n",failures_seen,after_slice);
}
static void sliced_lifecycle(void) {
    for(int destroy=0;destroy<2;destroy++) for(int reverse=0;reverse<2;reverse++) {
        undo_log u;piece_tree *t;start(&u,&t,32);undo_state s=state(4);undo_change c;
        CHECK(undo_group_begin(&u,&s)==0);ins(&u,0,"a",0,1,2);ins(&u,1,"b",1,2,3);CHECK(undo_group_end(&u,&s)==0);
        if(!reverse) CHECK(undo_undo(&u,1,&c)==0);
        const char *pre=reverse?"ab":"";
        CHECK(atomic_replay(&u,reverse,1,&c)==UNDO_MORE);
        piece_snapshot *view=piece_snapshot_retain((piece_snapshot *)undo_replay_snapshot(&u));CHECK(view);
        CHECK(undo_insert(&u,0,(const uint8_t *)"x",1,0,&s,&s)==UNDO_ERR_BUSY);
        CHECK(undo_set_cap(&u,1)==UNDO_ERR_BUSY && undo_group_begin(&u,&s)==UNDO_ERR_BUSY);
        /* Maintenance must not relocate the handles saved in replay scratch. */
        CHECK(undo_maintain(&u,16)==0);
        if(destroy) undo_destroy(&u);else undo_clear(&u);
        same(t,pre);uint8_t b[2];CHECK(piece_snapshot_read(view,0,b,strlen(pre))==0 && memcmp(b,pre,strlen(pre))==0);
        piece_snapshot_release(view);if(!destroy) undo_destroy(&u);piece_destroy(t);
    }
    /* A successful same-length out-of-band tree edit between slices is a
     * contract violation, including when the replacement bytes are identical. */
    puts("review 5: expecting an assertion in the out-of-band mutation child");
    fflush(NULL);pid_t child=fork();CHECK(child>=0);
    if(child==0) {
        undo_log u;piece_tree *t;start(&u,&t,32);undo_change c;
        ins(&u,0,"a",0,0,0);ins(&u,1,"b",1,0,0);
        CHECK(undo_undo_slice(&u,1,1,UINT64_MAX,&c)==UNDO_MORE);
        CHECK(piece_delete(t,0,1,NULL)==0 && piece_insert(t,0,(const uint8_t *)"a",1)==0);
        (void)undo_undo_slice(&u,1,1,UINT64_MAX,&c);_exit(0);
    }
    int status;CHECK(waitpid(child,&status,0)==child);
    CHECK(WIFSIGNALED(status) && WTERMSIG(status)==SIGABRT);
    puts("review 5: slice clear/destroy abort and out-of-band mutation assertion passed");
}
int main(int argc,char **argv) {
    if(argc==2) {
        switch(atoi(argv[1])) {case 1:review_1();atomic_sweep();undo_batch_failure_reporting();break;case 2:review_2();break;case 3:review_3();break;case 4:review_4();break;case 5:review_5();sliced_lifecycle();break;case 8:review_8();batch_failure_reporting();break;default:return 2;}
        return 0;
    }
    review_1();atomic_sweep();undo_batch_failure_reporting();original_tests();review_2();review_3();review_4();review_5();sliced_lifecycle();review_8();batch_failure_reporting();puts("undo_test: P1.5e all passed");return 0;
}
