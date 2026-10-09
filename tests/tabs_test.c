#include "tabs/tabs.h"
#include <string.h>
#define CHECK(x) EDIT_ASSERT(x)

/* Independent oracle: packed display array and separate MRU ID vector,
 * never slot indices or links. Shared by the deterministic driver and fuzz. */
#define MODEL_CAP 8u
#define MODEL_CLOSED 3u
typedef struct model_item {
    uint64_t id;
    piece_tree *buffer;
    undo_log *undo;
    view_state state;
    size_t position;
    bool modified;
    char title[32];
} model_item;
typedef struct tabs_model {
    model_item open[MODEL_CAP], closed[MODEL_CLOSED];
    uint64_t mru[MODEL_CAP], active, next;
    size_t n, nc;
    bool held;
    view_state live;
} tabs_model;
static size_t model_index(const tabs_model *m, uint64_t id)
{
    for(size_t i=0;i<m->n;++i) if(m->open[i].id==id) return i;
    return SIZE_MAX;
}
static void model_save(tabs_model *m)
{
    if(m->n) m->open[model_index(m,m->active)].state=m->live;
}
static void model_promote(tabs_model *m)
{
    if(!m->n) return;
    size_t i=0; while(m->mru[i]!=m->active) ++i;
    for(;i>0;--i) m->mru[i]=m->mru[i-1];
    m->mru[0]=m->active;
}
static void model_release(tabs_model *m)
{ if(m->held) { model_promote(m); m->held=false; } }
static void model_load(tabs_model *m, uint64_t id)
{
    model_save(m); m->active=id; m->live=m->open[model_index(m,id)].state;
}
static void model_compare(const tabs_set *s, const tabs_model *m, const view_state *live)
{
    CHECK(tabs_count(s)==m->n && tabs_closed_count(s)==m->nc && s->cycling==m->held);
    CHECK(memcmp(live,&m->live,sizeof *live)==0);
    CHECK(tabs_active_index(s)==model_index(m,m->active));
    for(size_t i=0;i<m->n;++i) {
        const tabs_tab *t=tabs_at(s,i); const model_item *e=&m->open[i];
        CHECK(t && t->id==e->id && t->buffer==e->buffer && t->undo==e->undo);
        CHECK(t->modified==e->modified && strcmp(t->title,e->title)==0 && strcmp(t->path,"/small")==0);
        CHECK(memcmp(&t->state,&e->state,sizeof e->state)==0);
    }
    uint32_t slot=s->mru_head, prev=TABS_NONE;
    for(size_t i=0;i<m->n;++i) {
        CHECK(slot!=TABS_NONE && s->slots[slot].id==m->mru[i]);
        CHECK(s->slots[slot].mru_prev==prev); prev=slot; slot=s->slots[slot].mru_next;
    }
    CHECK(slot==TABS_NONE && s->mru_tail==prev);
    for(size_t i=0;i<m->nc;++i) {
        const tabs_tab *t=tabs_closed_at(s,i); const model_item *e=&m->closed[m->nc-1-i];
        CHECK(t->id==e->id && t->buffer==e->buffer && t->undo==e->undo && t->modified==e->modified);
        CHECK(strcmp(t->title,e->title)==0 && memcmp(&t->state,&e->state,sizeof e->state)==0);
    }
    CHECK(tabs_at(s,m->n)==NULL && tabs_closed_at(s,m->nc)==NULL);
    CHECK(s->count+s->closed_count+s->free_count==s->capacity+s->closed_capacity);
}
static void model_op(tabs_set *s, tabs_model *m, view_state *live,
                     piece_tree *buffer, undo_log *undo, uint8_t op, uint8_t a, uint8_t b)
{
    size_t index=m->n?(size_t)a%m->n:0;
    int rc;
    switch(op%10u) {
    case 0: {
        char title[32]; (void)snprintf(title,sizeof title,"file-%u",(unsigned)a);
        tabs_desc d={.buffer=buffer,.undo=undo,.title=title,.path="/small",
            .title_len=strlen(title),.path_len=6,.modified=(b&1u)!=0};
        d.state.selection.cursor=a; d.state.hscroll=b;
        uint64_t id=UINT64_MAX; rc=tabs_open(s,&d,live,&id);
        if(m->n==MODEL_CAP) { CHECK(rc==TABS_ERR_CAPACITY && id==UINT64_MAX); break; }
        CHECK(rc==0); model_release(m); model_save(m);
        model_item e={.id=m->next++,.buffer=buffer,.undo=undo,.state=d.state,.modified=d.modified};
        memcpy(e.title,title,strlen(title)+1); m->open[m->n]=e; m->mru[m->n]=e.id; ++m->n;
        m->active=e.id; m->live=e.state; model_promote(m); CHECK(id==e.id); break;
    }
    case 1: {
        tabs_tab evicted; memset(&evicted,0x5a,sizeof evicted);
        rc=tabs_close(s,index,live,&evicted);
        if(!m->n) { CHECK(rc==TABS_ERR_RANGE); break; }
        CHECK(rc==0); model_release(m); model_save(m);
        model_item e=m->open[index]; e.position=index;
        uint64_t expected=0;
        if(m->nc==MODEL_CLOSED) { expected=m->closed[0].id;
            memmove(m->closed,m->closed+1,(MODEL_CLOSED-1)*sizeof e); --m->nc; }
        m->closed[m->nc++]=e; CHECK(evicted.id==expected);
        size_t rank=0; while(m->mru[rank]!=e.id) ++rank;
        memmove(m->mru+rank,m->mru+rank+1,(m->n-rank-1)*sizeof m->mru[0]);
        memmove(m->open+index,m->open+index+1,(m->n-index-1)*sizeof e); --m->n;
        if(m->active==e.id) {
            if(m->n) { m->active=m->mru[0]; m->live=m->open[model_index(m,m->active)].state; }
            else { m->active=0; memset(&m->live,0,sizeof m->live); m->live.selection.preferred_col=VIEW_PREFERRED_UNSET; }
        }
        break;
    }
    case 2:
        rc=tabs_select(s,index,live);
        if(!m->n) { CHECK(rc==TABS_ERR_RANGE); break; }
        CHECK(rc==0); model_release(m); model_load(m,m->open[index].id); model_promote(m); break;
    case 3: {
        bool reverse=(b&1u)!=0; rc=tabs_mru_step(s,reverse,live);
        if(!m->n) { CHECK(rc==TABS_ERR_EMPTY); break; }
        CHECK(rc==0); m->held=true; size_t rank=0; while(m->mru[rank]!=m->active) ++rank;
        rank=reverse?(rank+m->n-1)%m->n:(rank+1)%m->n;
        model_load(m,m->mru[rank]); break;
    }
    case 4: tabs_mru_release(s); model_release(m); break;
    case 5: {
        size_t to=m->n?(size_t)b%m->n:0; rc=tabs_reorder(s,index,to);
        if(!m->n) { CHECK(rc==TABS_ERR_RANGE); break; }
        CHECK(rc==0); model_release(m); model_item e=m->open[index];
        if(index<to) for(size_t i=index;i<to;++i) m->open[i]=m->open[i+1];
        else for(size_t i=index;i>to;--i) m->open[i]=m->open[i-1];
        m->open[to]=e; break;
    }
    case 6: {
        uint64_t id=UINT64_MAX; rc=tabs_reopen(s,live,&id);
        if(!m->nc) { CHECK(rc==TABS_ERR_EMPTY); break; }
        if(m->n==MODEL_CAP) { CHECK(rc==TABS_ERR_CAPACITY); break; }
        CHECK(rc==0); model_release(m); model_save(m); model_item e=m->closed[--m->nc];
        size_t at=e.position<m->n?e.position:m->n;
        memmove(m->open+at+1,m->open+at,(m->n-at)*sizeof e); m->open[at]=e;
        m->mru[m->n]=e.id; ++m->n; m->active=e.id; m->live=e.state; model_promote(m); CHECK(id==e.id); break;
    }
    case 7:
        live->selection.cursor=a; live->selection.anchor=b; live->first_line=(uint64_t)a*b;
        live->first_byte=b; live->hscroll=a; live->approximate=(b&1u)!=0; m->live=*live; break;
    case 8:
        rc=tabs_set_modified(s,index,(b&1u)!=0);
        if(!m->n) CHECK(rc==TABS_ERR_RANGE);
        else { CHECK(rc==0); m->open[index].modified=(b&1u)!=0; } break;
    default: {
        char title[32]; (void)snprintf(title,sizeof title,"renamed-%u",(unsigned)b);
        rc=tabs_rename(s,index,title,strlen(title),"/small",6);
        if(!m->n) CHECK(rc==TABS_ERR_RANGE);
        else { CHECK(rc==0); memcpy(m->open[index].title,title,strlen(title)+1); } break;
    }
    }
    model_compare(s,m,live);
}
#ifndef TABS_MODEL_ONLY
static void sequences(void)
{
    tabs_set s; CHECK(tabs_init(&s,MODEL_CAP,MODEL_CLOSED)==0);
    tabs_model m={.next=1}; view_state live={0};
    piece_allocator allocator=piece_default_allocator(); piece_tree *tree=piece_create(&allocator); CHECK(tree);
    CHECK(piece_init_copy(tree,(const uint8_t *)"small",5)==0);
    undo_log undo; CHECK(undo_init(&undo,tree,8)==0);
    for(uint8_t i=0;i<8;++i) model_op(&s,&m,&live,tree,&undo,0,i,i);
    const uint8_t script[][3]={{2,2,0},{7,9,7},{3,0,0},{3,0,0},{3,0,1},{4,0,0},
        {5,7,0},{1,0,0},{1,1,0},{1,2,0},{1,3,0},{6,0,0},{6,0,0},{6,0,0},{6,0,0}};
    for(size_t i=0;i<sizeof script/sizeof script[0];++i)
        model_op(&s,&m,&live,tree,&undo,script[i][0],script[i][1],script[i][2]);
    uint32_t rng=0x12345678u;
    for(unsigned i=0;i<20000;++i) {
        rng=rng*1664525u+1013904223u;
        model_op(&s,&m,&live,tree,&undo,(uint8_t)(rng>>24),(uint8_t)(rng>>8),(uint8_t)rng);
    }
    tabs_fini(&s); undo_destroy(&undo); piece_destroy(tree);
    puts("tabs_test: sequences vs independent model passed");
}

typedef struct glyph_fixture {
    render_glyph glyphs[12];
    bool combining, zwj;
} glyph_fixture;
static bool test_glyph(void *ctx, const uint8_t *text, size_t len, int width,
                       uint32_t *identity, uint32_t *slot)
{
    glyph_fixture *f=ctx; utf8_step unit=utf8_decode(text,len);
    if(unit.cp=='e' && len==3) f->combining=true;
    if(unit.cp==0x1f469 && len==11) f->zwj=true;
    for(uint32_t i=0;i<12;++i) if(f->glyphs[i].glyph_index==unit.cp) {
        CHECK((int)f->glyphs[i].w==width); *identity=unit.cp; *slot=i; return true;
    }
    return false;
}
static void rendering(void)
{
    tabs_set s; CHECK(tabs_init(&s,4,1)==0);
    piece_allocator allocator=piece_default_allocator(); piece_tree *tree=piece_create(&allocator); CHECK(tree);
    undo_log undo; CHECK(undo_init(&undo,tree,8)==0); view_state live={0}; uint64_t id;
    const char *titles[]={"abc","中中中","e\xcc\x81\xf0\x9f\x91\xa9\xe2\x80\x8d\xf0\x9f\x92\xbbx"};
    for(size_t i=0;i<3;++i) {
        tabs_desc d={.buffer=tree,.undo=&undo,.title=titles[i],.title_len=strlen(titles[i]),.modified=i==1};
        CHECK(tabs_open(&s,&d,&live,&id)==0);
    }
    glyph_fixture fixture={0};
    const uint32_t cps[12]={'a','b','c','x','|','*',0x2026,0xfffd,0x4e2d,'e',0x1f469,' '};
    for(size_t i=0;i<12;++i) fixture.glyphs[i]=(render_glyph){.glyph_index=cps[i],.page=0,
        .x=(uint32_t)(i*2),.w=i==8 || i==10?2u:1u,.h=1};
    uint8_t pixels[24]={0}; render_atlas_page page={pixels,sizeof pixels,24,24,1};
    render_cell cells[96];
    render_cell untouched={.atlas_slot=RENDER_NO_SLOT,.fg=7,.bg=8};
    for(size_t i=0;i<96;++i) cells[i]=untouched;
    render_grid g; uint64_t dirty[1]; CHECK(render_grid_init(&g,(render_dims){32,3,1,1},cells,96,dirty,1)==0);
    g.glyphs=fixture.glyphs; g.glyph_count=12; g.pages=&page; g.page_count=1;
    CHECK(render_frame_begin(&g,1)==0);
    tabs_strip strip={.row=1,.first_col=2,.col_count=24,.tab_cols=8,.fg=1,.bg=2,
        .active_fg=3,.active_bg=4,.glyph=test_glyph,.glyph_ctx=&fixture};
    CHECK(tabs_strip_render(&s,&g,&strip)==0 && render_grid_validate(&g)==0);
    CHECK(dirty[0]==2 && !g.full_frame && fixture.combining && fixture.zwj);
    for(size_t i=0;i<96;++i) if(i<34 || i>=58) CHECK(memcmp(&cells[i],&untouched,sizeof untouched)==0);
    CHECK(cells[35].glyph_index=='a' && cells[35].bg==2);
    CHECK(cells[43].glyph_index==0x4e2d && cells[43].attrs==RENDER_ATTR_WIDE_LEFT);
    CHECK(cells[44].attrs==RENDER_ATTR_WIDE_RIGHT && cells[44].atlas_slot==RENDER_NO_SLOT);
    CHECK(cells[47].glyph_index==0x2026 && cells[48].glyph_index=='*' && cells[49].glyph_index=='|');
    CHECK(cells[51].glyph_index=='e' && cells[51].fg==3 && cells[51].bg==4);
    CHECK(tabs_strip_hit(&s,&strip,1)==SIZE_MAX && tabs_strip_hit(&s,&strip,2)==0);
    CHECK(tabs_strip_hit(&s,&strip,10)==1 && tabs_strip_hit(&s,&strip,25)==2);
    CHECK(tabs_strip_hit(&s,&strip,26)==SIZE_MAX);
    /* Every tiny width, title clipping and active-window offset remains a
     * frozen-contract-valid grid. Clear between cases to avoid prior halves. */
    for(uint32_t width=1;width<=12;++width) {
        for(size_t i=0;i<96;++i) cells[i]=untouched;
        CHECK(render_frame_begin(&g,width+1)==0);
        strip.tab_cols=width; strip.col_count=width; strip.first_tab=1;
        CHECK(tabs_strip_render(&s,&g,&strip)==0 && render_grid_validate(&g)==0);
    }
    /* Missing resident glyphs safely become backgrounds. Invalid bytes and
     * controls become replacement glyphs, never raw control identities. */
    const char invalid[]={'a',(char)0xff,'\n','b'};
    CHECK(tabs_rename(&s,0,invalid,sizeof invalid,NULL,0)==0);
    for(size_t i=0;i<96;++i) cells[i]=untouched;
    CHECK(render_frame_begin(&g,20)==0); strip.first_tab=0; strip.tab_cols=8; strip.col_count=8;
    CHECK(tabs_strip_render(&s,&g,&strip)==0 && cells[36].glyph_index==0xfffd && cells[37].glyph_index==0xfffd);
    strip.glyph=NULL; CHECK(tabs_strip_render(&s,&g,&strip)==0 && render_grid_validate(&g)==0);
    for(size_t i=34;i<42;++i) CHECK(cells[i].atlas_slot==RENDER_NO_SLOT);
    CHECK(render_frame_begin(&g,21)==0); render_cell saved=cells[34];
    strip.bg=UINT32_MAX; CHECK(tabs_strip_render(&s,&g,&strip)==TABS_ERR_ARG && dirty[0]==0);
    CHECK(memcmp(&saved,&cells[34],sizeof saved)==0); strip.bg=2;
    strip.col_count=31; CHECK(tabs_strip_render(&s,&g,&strip)==TABS_ERR_RANGE && dirty[0]==0);
    /* Refuse to orphan an existing glyph in the untouched neighbour. */
    strip.col_count=1; cells[34].attrs=RENDER_ATTR_WIDE_RIGHT;
    CHECK(tabs_strip_render(&s,&g,&strip)==TABS_ERR_RANGE && dirty[0]==0);
    tabs_fini(&s); undo_destroy(&undo); piece_destroy(tree);
    puts("tabs_test: UTF-8 strip, clipping, damage and hit testing passed");
}
static void errors_and_eviction(void)
{
    tabs_set s={0}; CHECK(tabs_init(NULL,1,1)==TABS_ERR_ARG);
    CHECK(tabs_init(&s,0,1)==TABS_ERR_ARG && s.arena.base==NULL);
    CHECK(tabs_init(&s,SIZE_MAX,1)==TABS_ERR_ARG);
    CHECK(tabs_init(&s,1,0)==0); view_state live={0};
    piece_allocator allocator=piece_default_allocator(); piece_tree *tree=piece_create(&allocator); CHECK(tree);
    undo_log undo; CHECK(undo_init(&undo,tree,8)==0);
    tabs_desc d={.buffer=tree,.undo=&undo,.title="a",.title_len=1}; uint64_t id=7;
    CHECK(tabs_open(&s,&d,&live,&id)==0); const tabs_tab *stable=tabs_at(&s,0);
    CHECK(tabs_mru_step(&s,false,&live)==0);
    CHECK(tabs_select(&s,1,&live)==TABS_ERR_RANGE && s.cycling);
    CHECK(tabs_open(&s,&d,&live,&id)==TABS_ERR_CAPACITY && s.cycling);
    CHECK(tabs_reorder(&s,0,1)==TABS_ERR_RANGE && s.cycling && tabs_at(&s,0)==stable);
    CHECK(tabs_rename(&s,0,"bad\0title",9,NULL,0)==TABS_ERR_ARG);
    CHECK(tabs_rename(&s,0,NULL,1,NULL,0)==TABS_ERR_ARG);
    CHECK(tabs_rename(&s,0,"a",TABS_TITLE_BYTES,NULL,0)==TABS_ERR_ARG);
    CHECK(tabs_open(&s,NULL,&live,&id)==TABS_ERR_ARG);
    CHECK(tabs_rename(&s,0,"title",5,"path",4)==0);
    const tabs_tab *names=tabs_at(&s,0);
    CHECK(tabs_rename(&s,0,names->path,names->path_len,names->title,names->title_len)==0);
    CHECK(strcmp(names->title,"path")==0 && strcmp(names->path,"title")==0);
    CHECK(tabs_close(&s,0,&live,NULL)==TABS_ERR_ARG && s.cycling);
    tabs_tab evicted; CHECK(tabs_close(&s,0,&live,&evicted)==0);
    CHECK(evicted.id==id && evicted.buffer==tree && evicted.undo==&undo);
    CHECK(tabs_closed_count(&s)==0 && tabs_active(&s)==NULL && tabs_active_index(&s)==SIZE_MAX);
    CHECK(tabs_reopen(&s,&live,&id)==TABS_ERR_EMPTY);
    CHECK(tabs_mru_step(&s,false,&live)==TABS_ERR_EMPTY);
    CHECK(live.selection.preferred_col==VIEW_PREFERRED_UNSET);
    tabs_fini(&s); tabs_fini(&s); CHECK(tabs_owned_bytes(&s)==0);
    /* Reopen at full capacity must preserve the entire closed stack. */
    CHECK(tabs_init(&s,1,1)==0); CHECK(tabs_open(&s,&d,&live,&id)==0);
    CHECK(tabs_close(&s,0,&live,&evicted)==0 && evicted.id==0);
    uint64_t old=tabs_closed_at(&s,0)->id; CHECK(tabs_open(&s,&d,&live,&id)==0);
    CHECK(tabs_reopen(&s,&live,&id)==TABS_ERR_CAPACITY && tabs_closed_at(&s,0)->id==old);
    CHECK(tabs_close(&s,0,&live,&evicted)==0 && evicted.id==old);
    tabs_fini(&s); undo_destroy(&undo); piece_destroy(tree);
    puts("tabs_test: errors, bounded eviction and full-capacity reopen passed");
}
static void resource_retention(void)
{
    tabs_set s; CHECK(tabs_init(&s,2,1)==0); view_state live={0};
    piece_allocator allocator=piece_default_allocator(); piece_tree *trees[2]; undo_log logs[2];
    uint64_t first_id=0, id;
    for(size_t i=0;i<2;++i) {
        trees[i]=piece_create(&allocator); CHECK(trees[i]);
        CHECK(piece_init_copy(trees[i],(const uint8_t *)"small",5)==0);
        CHECK(undo_init(&logs[i],trees[i],8)==0);
        tabs_desc d={.buffer=trees[i],.undo=&logs[i],.title="small",.title_len=5};
        CHECK(tabs_open(&s,&d,&live,&id)==0);
        if(i==0) {
            first_id=id;
            undo_state before={0}, after={0}; after.bytes[0]=6;
            CHECK(undo_insert(&logs[0],5,(const uint8_t *)"!",1,1,&before,&after)==0);
            live.selection.cursor=6; live.selection.anchor=2; live.hscroll=17;
            CHECK(tabs_set_modified(&s,0,true)==0);
        }
    }
    tabs_tab evicted; CHECK(tabs_close(&s,0,&live,&evicted)==0 && evicted.id==0);
    CHECK(tabs_reopen(&s,&live,&id)==0 && id==first_id);
    const tabs_tab *active=tabs_active(&s);
    CHECK(active->buffer==trees[0] && active->undo==&logs[0] && active->modified);
    CHECK(live.selection.cursor==6 && live.selection.anchor==2 && live.hscroll==17);
    CHECK(piece_len(active->buffer)==6); undo_change change;
    CHECK(undo_undo(active->undo,1,&change)==0 && change.groups==1 && piece_len(active->buffer)==5);
    uint8_t text[5]; CHECK(piece_read(active->buffer,0,text,5)==0 && memcmp(text,"small",5)==0);
    tabs_fini(&s);
    for(size_t i=0;i<2;++i) { undo_destroy(&logs[i]); piece_destroy(trees[i]); }
    puts("tabs_test: closed buffer, undo history and view retention passed");
}
static void allocations(void)
{
    tabs_set s; CHECK(tabs_init(&s,100,16)==0);
    piece_allocator allocator=piece_default_allocator(); piece_tree *trees[100]; undo_log logs[100];
    view_state live={0}; uint64_t id;
    for(size_t i=0;i<100;++i) {
        trees[i]=piece_create(&allocator); CHECK(trees[i]);
        CHECK(piece_init_copy(trees[i],(const uint8_t *)"small\n",6)==0);
        CHECK(undo_init(&logs[i],trees[i],8)==0);
        tabs_desc d={.buffer=trees[i],.undo=&logs[i],.title="small",.title_len=5};
        CHECK(tabs_open(&s,&d,&live,&id)==0);
    }
    render_cell cells[240]; uint64_t dirty[1]; render_grid g;
    CHECK(render_grid_init(&g,(render_dims){240,1,1,1},cells,240,dirty,1)==0);
    for(size_t i=0;i<240;++i) cells[i]=(render_cell){.atlas_slot=RENDER_NO_SLOT};
    tabs_strip strip={.col_count=240,.tab_cols=24,.fg=1,.bg=2,.active_fg=3,.active_bg=4};
    CHECK(render_frame_begin(&g,1)==0);
    edit_malloc_guard_begin();
    for(uint32_t i=0;i<10000;++i) {
        size_t index=i%100u; live.selection.cursor=i; live.hscroll=i*2u;
        CHECK(tabs_select(&s,index,&live)==0);
        strip.first_tab=(index/10u)*10u;
        CHECK(tabs_strip_render(&s,&g,&strip)==0);
        CHECK(tabs_mru_step(&s,(i&1u)!=0,&live)==0); tabs_mru_release(&s);
    }
    size_t count=edit_malloc_guard_end(); CHECK(count==0);
    CHECK(tabs_owned_bytes(&s)<=1000000u);
    printf("tabs_test: 10000 switches + strip + MRU mallocs=%zu guard=%s\n",count,
        edit_malloc_guard_active()?"active":"ASan-inert");
    /* Also prove mutations use the pre-reserved storage after init. */
    edit_malloc_guard_begin();
    for(unsigned i=0;i<1000;++i) {
        tabs_tab evicted; CHECK(tabs_close(&s,i%100u,&live,&evicted)==0 && evicted.id==0);
        CHECK(tabs_reopen(&s,&live,&id)==0); CHECK(tabs_reorder(&s,0,99)==0);
    }
    count=edit_malloc_guard_end(); CHECK(count==0);
    tabs_fini(&s);
    for(size_t i=0;i<100;++i) { undo_destroy(&logs[i]); piece_destroy(trees[i]); }
}
int main(void)
{
    sequences(); rendering(); errors_and_eviction(); resource_retention(); allocations();
    puts("tabs_test: all passed"); return 0;
}

#endif
