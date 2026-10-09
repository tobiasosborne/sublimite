#include "undo/undo.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
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
    CHECK(undo_undo(&u,1,&c)==0 && c.records==8 && c.groups==1 && c.state.bytes[0]==1);
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

typedef struct failing { piece_allocator backing; size_t calls, fail; } failing;
static void *fall(void *ctx,size_t n) { failing *f=ctx; f->calls++; if(f->fail && f->calls==f->fail) return NULL; return f->backing.alloc(f->backing.ctx,n); }
static void ffree(void *ctx,void *p,size_t n) { failing *f=ctx; f->backing.free(f->backing.ctx,p,n); }
static void failures(void) {
    size_t saw=0;
    for(size_t fail=1;fail<8;fail++) {
        failing f={piece_default_allocator(),0,0}; piece_allocator a={&f,fall,ffree};
        piece_tree *t=piece_create(&a); CHECK(t); undo_log u; CHECK(undo_init(&u,t,128)==0);
        CHECK(piece_init_copy(t,(const uint8_t *)"orig",4)==0);
        undo_state s=state(0); f.calls=0; f.fail=fail;
        int rc=undo_delete(&u,0,4,UNDO_DELETE,0,&s,&s);
        if(rc) { CHECK(rc==PIECE_ERR_NOMEM); same(t,"orig"); CHECK(undo_get_stats(&u).records==0); saw++; }
        f.fail=0; finish(&u);
    }
    CHECK(saw>0);
    failing f={piece_default_allocator(),0,0}; piece_allocator a={&f,fall,ffree};
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
    size_t failed=0,partial=0;
    for(size_t mode=0;mode<2;mode++) for(size_t k=1;k<=4;k++) {
        failing f={piece_default_allocator(),0,0};piece_allocator a={&f,fall,ffree};
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
            if(!c.records) {
                CHECK(piece_len(t)==bn && piece_read(t,0,after,bn)==0 && memcmp(before,after,bn)==0);
            } else {
                partial++;CHECK(c.len>0 && c.groups==0 && !c.has_state && u.partial==-1);
                CHECK(undo_insert(&u,0,(const uint8_t *)"q",1,0,&s,&s)==UNDO_ERR_BUSY);
                CHECK(undo_set_cap(&u,32)==UNDO_ERR_BUSY);
                undo_change rejected;CHECK(undo_redo(&u,1,&rejected)==UNDO_ERR_BUSY);
            }
            f.fail=0;CHECK(undo_undo(&u,1,&c)==0 && c.groups==1);
            same(t,"");CHECK(undo_redo(&u,1,&c)==0);
            CHECK(piece_len(t)==bn && piece_read(t,0,after,bn)==0 && memcmp(before,after,bn)==0);
        }
        f.fail=0;finish(&u);
    }
    CHECK(failed>0 && partial>0);
    printf("undo_test: replay allocator failures=%zu partial_groups=%zu (resumed)\n",failed,partial);
    free(after);free(before);free(text);
}

static void pool_exhaustion(void) {
    undo_log u;piece_tree *t;start(&u,&t,1);undo_state s=state(0);
    CHECK(undo_group_begin(&u,&s)==0);ins(&u,0,"a",0,0,0);ins(&u,1,"b",1,0,0);
    undo_stats before=undo_get_stats(&u);
    CHECK(undo_insert(&u,2,(const uint8_t *)"c",1,2,&s,&s)==UNDO_ERR_NOMEM);
    undo_stats after=undo_get_stats(&u);CHECK(after.records==before.records && after.live_bytes==before.live_bytes);
    same(t,"ab");CHECK(undo_group_end(&u,&s)==0 && undo_get_stats(&u).records==0);
    ins(&u,2,"c",3,0,0);undo_change c;CHECK(undo_undo(&u,1,&c)==0);same(t,"ab");finish(&u);
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
int main(void) { grouping(); explicit_cap(); references(); fragmented_insert(); eight_span_capture(); edge_cases(); failures(); replay_failures(); pool_exhaustion(); no_malloc(); puts("undo_test: all passed"); return 0; }
