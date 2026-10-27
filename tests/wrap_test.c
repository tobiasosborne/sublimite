#include "layout/layout.h"
#include "view/view.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FAIL wrap_test:%d %s\n",__LINE__,#x); exit(1); } } while (0)
#define ROWS 12u
#define COLS 40u
typedef struct fixture {
    layout l;
    render_grid g;
    render_cell cells[ROWS*COLS];
    render_glyph glyphs[95];
    render_atlas_page page;
    uint64_t bits[1], rb[ROWS]; uint32_t used[ROWS], frame;
    piece_tree *tree;
    edit_arena arena;
} fixture;
static void *piece_alloc(void *ctx,size_t n) { return edit_arena_alloc(ctx,n,16); }
static void piece_free(void *ctx,void *ptr,size_t n) { (void)ctx; (void)ptr; (void)n; }
static void init(fixture *f, const char *text, uint32_t cols, uint32_t rows, uint32_t slice)
{
    memset(f,0,sizeof *f);
    const font_ascii_atlas *a=font_ascii_atlas_for_px(15);
    CHECK(edit_arena_init(&f->arena,16u*1024u*1024u)==0);
    piece_allocator al={&f->arena,piece_alloc,piece_free}; f->tree=piece_create(&al); CHECK(f->tree);
    CHECK(piece_init_copy(f->tree,(const uint8_t *)text,strlen(text))==0);
    CHECK(render_grid_init(&f->g,(render_dims){cols,rows,a->cell.cell_w,a->cell.cell_h},f->cells,ROWS*COLS,f->bits,1)==0);
    layout_ascii_glyphs(a,f->glyphs);
    f->page=(render_atlas_page){a->pixels,a->pixels_len,(size_t)a->cell.cell_w*95,a->cell.cell_w*95,a->cell.cell_h};
    f->g.glyphs=f->glyphs; f->g.glyph_count=95; f->g.pages=&f->page; f->g.page_count=1;
    layout_config cfg={0}; cfg.fg=1; cfg.bg=2; cfg.cursor_bg=3; cfg.sel_bg=4; cfg.slice_clusters=slice;
    CHECK(layout_init(&f->l,&f->g,&cfg,f->rb,f->used)==0);
    CHECK(layout_wrap_init(&f->l,&f->arena)==0);
    CHECK(layout_set_wrap(&f->l,true)==0);
}
static void run(fixture *f)
{
    int rc; unsigned guard=0;
    do { uint64_t before=f->l.bytes_scanned; rc=layout_run(&f->l); CHECK(f->l.bytes_scanned-before<=LAYOUT_BYTE_BUDGET+4); CHECK(++guard<100000); } while(rc==LAYOUT_MORE);
    CHECK(rc==0); CHECK(render_grid_validate(&f->g)==0);
}
static void show(fixture *f)
{
    CHECK(render_frame_begin(&f->g,++f->frame)==0);
    CHECK(layout_begin(&f->l,f->tree,(layout_viewport){0,0,99,0})==0); run(f);
}
static void row(fixture *f,uint32_t r,const char *want)
{
    char text[COLS+1]; uint32_t cols=f->g.dims.cols;
    for(uint32_t c=0;c<cols;c++) { render_cell x=f->cells[r*cols+c]; text[c]=(x.attrs&RENDER_ATTR_WIDE_RIGHT)?'_':x.atlas_slot==RENDER_NO_SLOT?' ':(char)x.glyph_index; }
    text[cols]=0; while(cols && text[cols-1]==' ') text[--cols]=0;
    if(strcmp(text,want)) fprintf(stderr,"row %u '%s' expected '%s'\n",r,text,want);
    CHECK(strcmp(text,want)==0);
}
static void destroy(fixture *f) { piece_destroy(f->tree); edit_arena_free(&f->arena); }
/* Pass view stops unchanged: CRLF's stop belongs before CR, not before LF. */
static void cursor_stops(void)
{
    const char *texts[]={"a\nb","a\r\nb","a\rb","a","a\r\n"};
    const view_key keys[]={VIEW_RIGHT,VIEW_END,VIEW_DOC_END};
    for(unsigned wrapping=0;wrapping<2;wrapping++) {
        for(unsigned sliced=0;sliced<2;sliced++) {
            for(size_t i=0;i<sizeof texts/sizeof *texts;i++) {
                fixture f; init(&f,texts[i],8,3,sliced);
                CHECK(layout_set_wrap(&f.l,wrapping!=0)==0);
                CHECK(render_frame_begin(&f.g,++f.frame)==0);
                CHECK(layout_begin(&f.l,f.tree,(layout_viewport){0,0,0,0})==0); run(&f);
                for(size_t k=0;k<sizeof keys/sizeof *keys;k++) {
                    view v; view_config cfg={4,3,8,NULL,NULL}; view_init(&v,f.tree,&cfg);
                    CHECK(view_set_wrap(&v,wrapping!=0,&f.l)==VIEW_OK);
                    view_change ch; CHECK(view_command(&v,keys[k],false,NULL,0,&ch)==VIEW_OK);
                    uint64_t stop=keys[k]==VIEW_RIGHT?1:keys[k]==VIEW_END?(i==2?3:1):strlen(texts[i]);
                    CHECK(v.state.selection.cursor==stop);
                    layout_set_cursor(&f.l,v.state.selection.cursor);
                    CHECK(render_frame_begin(&f.g,++f.frame)==0);
                    /* Keep every stop in this fixed viewport: this test checks
                     * the byte-stop handoff independently of view scrolling. */
                    CHECK(layout_begin(&f.l,f.tree,(layout_viewport){0,0,0,0})==0);
                    run(&f);
                    uint32_t r=keys[k]==VIEW_DOC_END && (i==0 || i==1 || i==4)?1u:0u;
                    uint32_t c=keys[k]==VIEW_DOC_END?(i==2?3u:i==4?0u:1u):keys[k]==VIEW_END && i==2?3u:1u;
                    if(!(f.cells[r*8u+c].attrs&RENDER_ATTR_CURSOR))
                        fprintf(stderr,"cursor stop mode=%u slice=%u text=%zu key=%zu byte=%llu cell=%u,%u\n",
                                wrapping,sliced,i,k,(unsigned long long)stop,r,c);
                    CHECK(f.cells[r*8u+c].attrs&RENDER_ATTR_CURSOR);
                    CHECK(f.cells[r*8u+c].bg==3);
                    if(keys[k]==VIEW_RIGHT && i==2) CHECK(f.cells[c].attrs&RENDER_ATTR_INVERSE);
                    else CHECK(f.cells[r*8u+c].atlas_slot==RENDER_NO_SLOT);
                    unsigned cursors=0;
                    for(size_t j=0;j<24;j++) cursors+=(f.cells[j].attrs&RENDER_ATTR_CURSOR)!=0;
                    CHECK(cursors==1);
                }
                destroy(&f);
            }
        }
    }
    puts("wrap_test: view cursor stops LF/CRLF/lone CR/EOF passed (wrap off/on, sliced/one-shot)");
}
static void basic(void)
{
    fixture f; init(&f,"  alpha beta gamma\nabcdefghijk\n",10,8,0); show(&f);
    row(&f,0,"  alpha"); row(&f,1,"  beta"); row(&f,2,"  gamma"); row(&f,3,"abcdefghij"); row(&f,4,"k");
    CHECK(f.rb[1]==8 && f.rb[2]==13 && f.rb[3]==19);
    CHECK(layout_set_wrap(&f.l,false)==0); show(&f); row(&f,0,""); /* hscroll=99: existing clipped output */
    destroy(&f);
    init(&f,"abcd\xe4\xb8\xad" "e\xcc\x81x",5,4,1); show(&f);
    row(&f,0,"abcd"); row(&f,1,"?_?x"); CHECK(f.rb[1]==4);
    layout_set_cursor(&f.l,4); show(&f); CHECK(f.cells[5].attrs&RENDER_ATTR_CURSOR); destroy(&f);
}
static void edits(void)
{
    fixture f,fresh; const char *text="  alpha beta gamma\nsecond\nthird\nfourth\nfifth\nsixth";
    init(&f,text,10,10,2); init(&fresh,text,10,10,0); show(&f); show(&fresh);
    for(unsigned i=0;i<30;i++) {
        uint64_t off=8; bool insert=(i%2)==0;
        CHECK((insert?piece_insert(f.tree,off,(const uint8_t *)"xyz ",4):piece_delete(f.tree,off,4,NULL))==0);
        CHECK((insert?piece_insert(fresh.tree,off,(const uint8_t *)"xyz ",4):piece_delete(fresh.tree,off,4,NULL))==0);
        CHECK(render_frame_begin(&f.g,++f.frame)==0);
        CHECK(layout_edit(&f.l,off,insert?0:4,insert?4:0,0,0)>=0); run(&f); show(&fresh);
        if(memcmp(f.cells,fresh.cells,10u*10u*sizeof(render_cell))) {
            fprintf(stderr,"edit iteration=%u\n",i);
            for(unsigned k=0;k<10;k++) fprintf(stderr,"row=%u byte=%llu/%llu used=%u/%u firstglyph=%u/%u\n",k,(unsigned long long)f.rb[k],(unsigned long long)fresh.rb[k],f.used[k],fresh.used[k],f.cells[k*10].glyph_index,fresh.cells[k*10].glyph_index);
        }
        CHECK(memcmp(f.cells,fresh.cells,10u*10u*sizeof(render_cell))==0);
        CHECK(memcmp(f.rb,fresh.rb,10u*sizeof(uint64_t))==0);
        CHECK(f.l.bytes_scanned<90); /* affected line and newly exposed bottom, never replay the viewport */
    }
    edit_malloc_guard_begin();
    for(unsigned i=0;i<10000;i++) { CHECK(render_frame_begin(&f.g,++f.frame)==0); CHECK(layout_relayout_rows(&f.l,1,1)>=0); run(&f); }
    size_t n=edit_malloc_guard_end(); CHECK(n==0);
    printf("wrap_test: 10000 relayouts mallocs=%zu guard=%s\n",n,edit_malloc_guard_active()?"active":"ASan-inert");
    destroy(&f); destroy(&fresh);
}
static void navigation(void)
{
    fixture f; init(&f,"  alpha beta gamma\nabcdefghijk\n",10,8,0); show(&f);
    view v; view_config cfg={4,8,10,NULL,NULL}; view_init(&v,f.tree,&cfg);
    CHECK(view_set_wrap(&v,true,&f.l)==VIEW_OK);
    v.state.selection.cursor=v.state.selection.anchor=4;
    view_change ch;
    CHECK(view_command(&v,VIEW_DOWN,false,NULL,0,&ch)==0); CHECK(v.state.selection.cursor==10 && v.state.selection.preferred_col==4);
    CHECK(view_command(&v,VIEW_DOWN,true,NULL,0,&ch)==0); CHECK(v.state.selection.cursor==15 && v.state.selection.anchor==10);
    CHECK(view_command(&v,VIEW_HOME,false,NULL,0,&ch)==0); CHECK(v.state.selection.cursor==13);
    CHECK(view_command(&v,VIEW_END,false,NULL,0,&ch)==0); CHECK(v.state.selection.cursor==18);
    CHECK(view_command(&v,VIEW_UP,false,NULL,0,&ch)==0); CHECK(v.state.selection.cursor==13);
    CHECK(view_wrap_default(NULL) && view_wrap_default("") && view_wrap_default("note.md") && view_wrap_default("note.txt") && view_wrap_default("note.tex"));
    CHECK(!view_wrap_default("code.c") && !view_wrap_default("note.md.c"));
    const char *paths[]={NULL,"","note.md","note.txt","note.tex","code.c","note.md.c"};
    for(size_t i=0;i<sizeof paths/sizeof *paths;i++) {
        CHECK(view_wrap_file(&v,&f.l,paths[i])==VIEW_OK);
        CHECK(v.state.wrap==(i<5) && f.l.wrap==v.state.wrap);
    }
    destroy(&f);
}
static void hard_end(void)
{
    fixture f; init(&f,"abcdefghijk\r\nnext",5,6,0); show(&f);
    view v; view_config cfg={4,6,5,NULL,NULL}; view_init(&v,f.tree,&cfg);
    CHECK(view_set_wrap(&v,true,&f.l)==0); v.state.selection.cursor=v.state.selection.anchor=2;
    view_change ch;
    CHECK(view_command(&v,VIEW_END,false,NULL,0,&ch)==0); CHECK(v.state.selection.cursor==5);
    CHECK(view_command(&v,VIEW_HOME,false,NULL,0,&ch)==0); CHECK(v.state.selection.cursor==0);
    v.state.selection.cursor=v.state.selection.anchor=11;
    CHECK(view_command(&v,VIEW_END,false,NULL,0,&ch)==0); CHECK(v.state.selection.cursor==11);
    destroy(&f);
}
static void edges(void)
{
    fixture f; init(&f,"\talpha beta gamma\r\nx\n",9,9,3); show(&f);
    row(&f,0,"    alpha"); row(&f,1,"    beta"); row(&f,2,"    gamma"); row(&f,3,"x");
    CHECK(f.l.wrap_rows[2].end==17); /* CRLF has no extra display/cursor column */
    view v; view_config cfg={4,9,9,NULL,NULL}; view_init(&v,f.tree,&cfg); CHECK(view_set_wrap(&v,true,&f.l)==0);
    v.state.selection.cursor=v.state.selection.anchor=19; view_change ch;
    CHECK(view_command(&v,VIEW_UP,false,NULL,0,&ch)==0); CHECK(v.state.selection.cursor==12);
    CHECK(view_wrap_file(&v,&f.l,"code.c")==0 && !v.state.wrap && !f.l.wrap);
    CHECK(view_wrap_file(&v,&f.l,"note.md")==0 && v.state.wrap && f.l.wrap);
    destroy(&f);
    init(&f,"\xe4\xb8\xad" "z\n",1,5,1); show(&f);
    row(&f,0,"?"); row(&f,1,"z"); CHECK(layout_approximate(&f.l)); destroy(&f);
    init(&f,"abc def ghi\nsecond\nthird",8,8,1); f.l.cfg.gutter=true; show(&f);
    CHECK(layout_gutter_width(&f.l)==2); CHECK(f.cells[0].glyph_index=='1');
    CHECK(f.cells[8].atlas_slot==RENDER_NO_SLOT); /* continuation gutter is blank */
    CHECK(f.l.wrap_rows[1].line==0 && f.l.wrap_rows[3].line==1);
    destroy(&f);
}
static void newline_edits(void)
{
    fixture a,b; const char *text="  abc def ghi\nsecond\nthird\nfourth";
    init(&a,text,9,9,1); init(&b,text,9,9,0); show(&a); show(&b);
    CHECK(piece_insert(a.tree,6,(const uint8_t *)"\n",1)==0 && piece_insert(b.tree,6,(const uint8_t *)"\n",1)==0);
    CHECK(render_frame_begin(&a.g,++a.frame)==0 && layout_edit(&a.l,6,0,1,0,1)>=0); run(&a); show(&b);
    CHECK(memcmp(a.cells,b.cells,9u*9u*sizeof(render_cell))==0);
    CHECK(piece_delete(a.tree,6,1,NULL)==0 && piece_delete(b.tree,6,1,NULL)==0);
    CHECK(render_frame_begin(&a.g,++a.frame)==0 && layout_edit(&a.l,6,1,0,1,0)>=0); run(&a); show(&b);
    CHECK(memcmp(a.cells,b.cells,9u*9u*sizeof(render_cell))==0);
    /* Removing indentation must update every continuation. */
    CHECK(piece_delete(a.tree,0,2,NULL)==0 && piece_delete(b.tree,0,2,NULL)==0);
    CHECK(render_frame_begin(&a.g,++a.frame)==0 && layout_edit(&a.l,0,2,0,0,0)>=0); run(&a); show(&b);
    CHECK(memcmp(a.cells,b.cells,9u*9u*sizeof(render_cell))==0);
    destroy(&a); destroy(&b);
}
static void checkpoint_message(const work_msg *msg,void *ctx) { (void)layout_checkpoint_event(ctx,msg); }
static int large_column_seed(void *ctx,const piece_tree *tree,uint64_t line,
                             uint64_t byte_target,uint64_t col_target,uint64_t *byte,uint64_t *column)
{
    const layout_checkpoint_store *store=ctx;
    if(store->source!=tree || line!=0) return 0;
    size_t chosen=0;
    for(size_t i=0;i<store->count;i++)
        if(store->entries[i].byte<=byte_target && store->entries[i].column<=col_target) chosen=i;
    *byte=store->entries[chosen].byte; *column=store->entries[chosen].column;
    return 1;
}
static void large_hscroll(void)
{
    /* A compact, real tree with exact columns beyond UINT32_MAX: no huge file
     * or artificial runtime/checkpoint state is needed. The worker publishes
     * a checkpoint after these tabs, so UI painting decodes only the suffix. */
    char text[LAYOUT_CHECKPOINT_STRIDE+10u];
    memset(text,'\t',LAYOUT_CHECKPOINT_STRIDE);
    memcpy(text+LAYOUT_CHECKPOINT_STRIDE,"abcdefghi",10);
    const uint64_t base=(uint64_t)LAYOUT_CHECKPOINT_STRIDE*UINT32_MAX;
    for(unsigned sliced=0;sliced<2;sliced++) {
        fixture f; init(&f,text,8,3,sliced);
        CHECK(layout_set_wrap(&f.l,false)==0);
        f.l.cfg.tab_width=UINT32_MAX; f.l.tab=UINT32_MAX;
        layout_checkpoint_store store; CHECK(layout_checkpoint_init(&store,&f.arena,sizeof text)==0);
        CHECK(layout_set_checkpoints(&f.l,&store)==0);
        work_pool *pool=malloc(sizeof *pool); CHECK(pool && work_pool_init(pool,1,0)==0);
        piece_snapshot *snap=piece_snapshot_take(f.tree); CHECK(snap);
        CHECK(layout_checkpoint_request(&store,pool,snap,f.tree,0,UINT32_MAX)==0);
        piece_snapshot_release(snap);
        unsigned waits=0;
        while(store.pending) {
            (void)work_mailbox_drain(pool,checkpoint_message,&store);
            struct timespec ts={0,1000000}; if(store.pending) (void)nanosleep(&ts,NULL);
            CHECK(++waits<20000);
        }
        CHECK(store.complete && store.columns==base+9);
        view v; view_config cfg={UINT32_MAX,3,8,large_column_seed,&store}; view_init(&v,f.tree,&cfg);
        view_change ch; CHECK(view_command(&v,VIEW_DOC_END,false,NULL,0,&ch)==VIEW_OK);
        CHECK(v.state.selection.cursor==strlen(text) && v.state.hscroll==base+2);
        layout_viewport vp={v.state.first_byte,v.state.first_line,0,1};
        /* Compile against both header widths so the old representation fails
         * at runtime rather than -Wconversion rejecting the regression build. */
        vp.hscroll=_Generic(vp.hscroll,uint32_t:(uint32_t)v.state.hscroll,uint64_t:v.state.hscroll);
        CHECK(vp.hscroll==v.state.hscroll);
        layout_set_cursor(&f.l,v.state.selection.cursor);
        CHECK(render_frame_begin(&f.g,++f.frame)==0);
        CHECK(layout_begin(&f.l,f.tree,vp)==0);
        CHECK(f.l.hscroll==v.state.hscroll);
        run(&f); row(&f,0,"cdefghi");
        CHECK(f.cells[7].attrs&RENDER_ATTR_CURSOR);
        CHECK(!layout_approximate(&f.l) && f.l.bytes_scanned<LAYOUT_CHECKPOINT_STRIDE);
        CHECK(render_frame_begin(&f.g,++f.frame)==0);
        CHECK(layout_relayout_rows(&f.l,0,1)==LAYOUT_MORE); run(&f);
        CHECK(f.l.hscroll==v.state.hscroll && (f.cells[7].attrs&RENDER_ATTR_CURSOR));
        CHECK(layout_set_wrap(&f.l,true)==0);
        layout_wrap_row seed; bool approximate=false;
        CHECK(layout_visual_row(&f.l,f.tree,v.state.selection.cursor,0,&seed,&approximate)==0);
        CHECK(seed.column>=base && seed.end_column==base+9);
        CHECK(render_frame_begin(&f.g,++f.frame)==0);
        CHECK(layout_begin_visual(&f.l,f.tree,vp,&seed)==0); run(&f);
        CHECK(f.l.hscroll==0); /* Wrapping ignores horizontal scroll by contract. */
        CHECK(f.cells[seed.end_column-seed.column+seed.indent].attrs&RENDER_ATTR_CURSOR);
        work_pool_shutdown(pool); free(pool); destroy(&f);
    }
    puts("wrap_test: uint64 view hscroll/checkpoint/cursor passed (wrap off/on, sliced/one-shot)");
}
static void long_line(void)
{
    size_t n=2u*1024u*1024u; char *text=malloc(n+1); CHECK(text);
    for(size_t i=0;i<n;i++) text[i]="word "[i%5];
    text[n]=0;
    fixture f; init(&f,text,20,12,7); show(&f);
    CHECK(f.l.bytes_scanned<1000); /* No tail scan on a huge wrapped logical line. */
    layout_checkpoint_store store; CHECK(layout_checkpoint_init(&store,&f.arena,n)==0);
    CHECK(layout_set_checkpoints(&f.l,&store)==0);
    work_pool *pool=malloc(sizeof *pool); CHECK(pool && work_pool_init(pool,1,0)==0);
    piece_snapshot *snap=piece_snapshot_take(f.tree); CHECK(snap);
    CHECK(layout_checkpoint_request(&store,pool,snap,f.tree,0,4)==0); piece_snapshot_release(snap);
    while(store.pending) { (void)work_mailbox_drain(pool,checkpoint_message,&store); struct timespec ts={0,1000000}; if(store.pending) (void)nanosleep(&ts,NULL); }
    CHECK(store.complete);
    layout_wrap_row seed; bool approximate=false;
    CHECK(layout_visual_row(&f.l,f.tree,1000000,0,&seed,&approximate)==0);
    CHECK(seed.start>990000 && seed.start<=1000000 && seed.column==seed.start);
    CHECK(approximate); /* column seeds are exact; uncached word-wrap phase is estimated */
    CHECK(render_frame_begin(&f.g,++f.frame)==0);
    CHECK(layout_begin_visual(&f.l,f.tree,(layout_viewport){0,0,0,1},&seed)==0); run(&f);
    CHECK(f.l.bytes_scanned<1000 && f.l.bytes_read<=LAYOUT_WIN);
    uint64_t off=seed.start+2;
    CHECK(piece_insert(f.tree,off,(const uint8_t *)"xyz ",4)==0);
    CHECK(render_frame_begin(&f.g,++f.frame)==0 && layout_edit(&f.l,off,0,4,0,0)>=0); run(&f);
    CHECK(f.l.bytes_scanned<2000); /* Start at the cached visual boundary, not byte zero. */
    CHECK(piece_insert(f.tree,0,(const uint8_t *)"x",1)==0);
    CHECK(render_frame_begin(&f.g,++f.frame)==0 && layout_edit(&f.l,0,0,1,0,0)==LAYOUT_RESET);
    work_pool_shutdown(pool); free(pool); destroy(&f); free(text);
}
static void typing_allocations(void)
{
    fixture f; init(&f,"  alpha beta gamma delta\nnext",10,9,5); show(&f);
    view v; view_config cfg={4,9,10,NULL,NULL}; view_init(&v,f.tree,&cfg); CHECK(view_set_wrap(&v,true,&f.l)==0);
    edit_malloc_guard_begin();
    for(unsigned i=0;i<10000;i++) {
        v.state.selection.cursor=v.state.selection.anchor=10;
        view_change change; view_key key=i%2?VIEW_DELETE:VIEW_TYPE;
        int rc=view_command(&v,key,false,(const uint8_t *)"x",key==VIEW_TYPE?1u:0u,&change);
        while(rc==VIEW_MORE) rc=view_continue(&v,&change);
        CHECK(rc==0 && change.changed);
        CHECK(render_frame_begin(&f.g,++f.frame)==0);
        CHECK(layout_edit(&f.l,change.offset,change.old_len,change.new_len,0,0)>=0); run(&f);
    }
    size_t count=edit_malloc_guard_end(); CHECK(count==0);
    printf("wrap_test: 10000 typing edits mallocs=%zu guard=%s\n",count,edit_malloc_guard_active()?"active":"ASan-inert");
    destroy(&f);
}
static void query_edges(void)
{
    fixture f; init(&f,"abcdefghijk\r\nXYZ",5,2,0); show(&f);
    layout_wrap_row r; bool approx=false;
    CHECK(layout_visual_row(&f.l,f.tree,8,1,&r,&approx)==0 && r.start==10 && r.end==11 && !approx);
    CHECK(layout_visual_row(&f.l,f.tree,13,-1,&r,&approx)==0 && r.start==10 && r.end==11 && !approx);
    layout_set_cursor_visual(&f.l,5,true); show(&f);
    CHECK((f.cells[4].attrs&RENDER_ATTR_CURSOR) && !(f.cells[5].attrs&RENDER_ATTR_CURSOR));
    destroy(&f);
    init(&f,"abcdefghijk\nsecond",5,5,1); show(&f);
    piece_snapshot *s=piece_snapshot_take(f.tree); CHECK(s);
    render_cell saved[ROWS*COLS]; memcpy(saved,f.cells,sizeof saved);
    CHECK(render_frame_begin(&f.g,++f.frame)==0 && layout_begin_snapshot(&f.l,s,(layout_viewport){0,0,0,2})==0); run(&f);
    CHECK(memcmp(saved,f.cells,25u*sizeof(render_cell))==0);
    piece_snapshot_release(s); destroy(&f);
}
static void hidden_separator_cursor(void)
{
    fixture f; init(&f,"abcde   f",5,3,1); layout_set_cursor(&f.l,6); show(&f);
    CHECK(f.cells[4].attrs&RENDER_ATTR_CURSOR);
    view v; view_config cfg={4,3,5,NULL,NULL}; view_init(&v,f.tree,&cfg); CHECK(view_set_wrap(&v,true,&f.l)==0);
    v.state.selection.cursor=v.state.selection.anchor=2; view_change ch;
    CHECK(view_command(&v,VIEW_END,false,NULL,0,&ch)==0 && v.state.selection.cursor==8);
    CHECK(view_command(&v,VIEW_DOWN,false,NULL,0,&ch)==0 && v.state.selection.preferred_col==5);
    destroy(&f);
}
static void lookahead_edit(void)
{
    fixture a,b; const char *text="abcde\xe0\xb8\x9d" "tail";
    init(&a,text,5,1,0); init(&b,text,5,1,0); show(&a); show(&b);
    CHECK(a.l.wrap_rows[0].end==5);
    CHECK(piece_delete(a.tree,7,1,NULL)==0 && piece_insert(a.tree,7,(const uint8_t *)"\xb1",1)==0);
    CHECK(piece_delete(b.tree,7,1,NULL)==0 && piece_insert(b.tree,7,(const uint8_t *)"\xb1",1)==0);
    CHECK(render_frame_begin(&a.g,++a.frame)==0 && layout_edit(&a.l,7,1,1,0,0)>=0); run(&a); show(&b);
    CHECK(memcmp(a.cells,b.cells,5u*sizeof(render_cell))==0);
    destroy(&a); destroy(&b);
}
static void lookahead_word_edit(void)
{
    fixture a,b; const char *text="a abcdefghijklmnopqrstuvwxyz0123456789";
    init(&a,text,16,1,2); init(&b,text,16,1,0); show(&a); show(&b);
    CHECK(a.l.wrap_rows[0].end==2); /* rollback after decoding part of the word */
    CHECK(piece_insert(a.tree,10,(const uint8_t *)"\n",1)==0 && piece_insert(b.tree,10,(const uint8_t *)"\n",1)==0);
    CHECK(render_frame_begin(&a.g,++a.frame)==0 && layout_edit(&a.l,10,0,1,0,1)>=0); run(&a); show(&b);
    CHECK(memcmp(a.cells,b.cells,16u*sizeof(render_cell))==0);
    CHECK(a.l.wrap_rows[0].end==10);
    CHECK(piece_delete(a.tree,10,1,NULL)==0 && piece_delete(b.tree,10,1,NULL)==0);
    CHECK(render_frame_begin(&a.g,++a.frame)==0 && layout_edit(&a.l,10,1,0,1,0)>=0); run(&a); show(&b);
    CHECK(memcmp(a.cells,b.cells,16u*sizeof(render_cell))==0);
    /* An edit in the unused tail still performs no rendering. */
    CHECK(piece_insert(a.tree,strlen(text),(const uint8_t *)"tail",4)==0);
    CHECK(render_frame_begin(&a.g,++a.frame)==0 && layout_edit(&a.l,strlen(text),0,4,0,0)==LAYOUT_DONE);
    CHECK(a.bits[0]==0 && !a.g.full_frame);
    destroy(&a); destroy(&b);
}
static void wide_navigation(void)
{
    fixture f; init(&f,"abcd\xe4\xb8\xad" "e\xcc\x81x",5,4,2); show(&f);
    view v; view_config cfg={4,4,5,NULL,NULL}; view_init(&v,f.tree,&cfg); CHECK(view_set_wrap(&v,true,&f.l)==0);
    v.state.selection.cursor=v.state.selection.anchor=2; view_change ch;
    CHECK(view_command(&v,VIEW_DOWN,true,NULL,0,&ch)==0 && v.state.selection.cursor==7 && v.state.selection.anchor==2);
    CHECK(view_command(&v,VIEW_UP,false,NULL,0,&ch)==0 && v.state.selection.cursor==2);
    v.state.selection.cursor=v.state.selection.anchor=1; v.state.selection.preferred_col=VIEW_PREFERRED_UNSET;
    CHECK(view_command(&v,VIEW_DOWN,false,NULL,0,&ch)==0 && v.state.selection.cursor==4);
    CHECK(view_command(&v,VIEW_HOME,false,NULL,0,&ch)==0 && v.state.selection.cursor==4);
    CHECK(view_command(&v,VIEW_END,false,NULL,0,&ch)==0 && v.state.selection.cursor==11);
    destroy(&f);
}
static void whitespace_home(void)
{
    fixture f; init(&f,"         word",5,6,1); show(&f);
    view v; view_config cfg={4,6,5,NULL,NULL}; view_init(&v,f.tree,&cfg); CHECK(view_set_wrap(&v,true,&f.l)==0);
    v.state.selection.cursor=v.state.selection.anchor=1; view_change ch;
    CHECK(view_command(&v,VIEW_HOME,false,NULL,0,&ch)==0 && v.state.selection.cursor==5 && v.state.visual_end);
    layout_set_cursor_visual(&f.l,v.state.selection.cursor,v.state.visual_end); show(&f);
    CHECK((f.cells[4].attrs&RENDER_ATTR_CURSOR) && !(f.cells[7].attrs&RENDER_ATTR_CURSOR));
    CHECK(view_command(&v,VIEW_DOWN,true,NULL,0,&ch)==0 && v.state.selection.cursor==8 && v.state.selection.anchor==5);
    CHECK(view_command(&v,VIEW_UP,false,NULL,0,&ch)==0 && v.state.selection.cursor==5 && v.state.visual_end);
    CHECK(view_command(&v,VIEW_HOME,false,NULL,0,&ch)==0 && v.state.selection.cursor==0 && !v.state.visual_end);
    v.state.selection.cursor=v.state.selection.anchor=6;
    CHECK(view_command(&v,VIEW_HOME,false,NULL,0,&ch)==0 && v.state.selection.cursor==5 && !v.state.visual_end);
    layout_set_cursor_visual(&f.l,v.state.selection.cursor,v.state.visual_end); show(&f);
    CHECK(!(f.cells[4].attrs&RENDER_ATTR_CURSOR) && (f.cells[7].attrs&RENDER_ATTR_CURSOR));
    destroy(&f);
}
static void gutter_only_boundary(void)
{
    size_t n=70000; char *text=malloc(n+1); CHECK(text);
    for(size_t i=0;i<n;i++) text[i]=(char)(i%3==0?0xe4:i%3==1?0xb8:0xad);
    text[n]=0;
    fixture f; init(&f,text,1,2,0); f.l.cfg.gutter=true; show(&f);
    CHECK(f.l.text_cols==0 && layout_approximate(&f.l));
    CHECK(f.l.wrap_rows[0].end==f.l.wrap_rows[0].start); /* unknown end stays at a certified boundary */
    destroy(&f); free(text);
}
static void resize(void)
{
    const char *text="one two three four\n  alpha \xe4\xb8\xad" " beta gamma delta\nlast\n";
    const uint32_t widths[]={13,5,1,40,9};
    /* Poison inactive caller storage too: growing must initialize new cells.
     * Both wrap modes own repacking; callers only change the grid dimensions. */
    for(unsigned mode=0;mode<4;mode++) {
        fixture a,b; init(&a,text,9,8,3); a.l.cfg.gutter=(mode&2u)!=0;
        CHECK(layout_set_wrap(&a.l,(mode&1u)!=0)==0);
        for(size_t i=9u*8u;i<ROWS*COLS;i++) a.cells[i]=(render_cell){0,RENDER_NO_SLOT,99,100,0,0};
        show(&a);
        for(size_t i=0;i<sizeof widths/sizeof *widths;i++) {
            a.g.dims.cols=widths[i];
            bool wrapping=((mode+(unsigned)i)&1u)!=0;
            CHECK(layout_set_wrap(&a.l,wrapping)==0); show(&a);
            init(&b,text,widths[i],8,0); b.l.cfg.gutter=a.l.cfg.gutter;
            CHECK(layout_set_wrap(&b.l,wrapping)==0); show(&b);
            CHECK(memcmp(a.cells,b.cells,(size_t)widths[i]*8u*sizeof(render_cell))==0);
            CHECK(memcmp(a.rb,b.rb,8u*sizeof *a.rb)==0);
            CHECK(memcmp(a.used,b.used,8u*sizeof *a.used)==0);
            destroy(&b);
        }
        destroy(&a);
    }
}
/* edit-1vun: queries at or above a certified top (and upward from it) are
 * anchored by a backward LF walk from the top instead of piece line queries.
 * Every result must equal the exact result of a twin laid out from byte 0
 * (short lines: both exact), with zero piece line queries on the subject. A
 * previous line longer than LAYOUT_BYTE_BUDGET (P) keeps the piece query. */
static void above_top_queries(void)
{
    const char *text="alpha beta gamma delta epsilon zeta eta theta\n\n  indented words here and more words\r\n"
        "x\nshort\n\tTabbed line with several words to wrap around the edge\n\n\nlast but one\nthe top line\nafter top\n";
    fixture ref, sub; init(&ref,text,16,4,0); init(&sub,text,16,4,0);
    uint64_t total=strlen(text), top=(uint64_t)(strstr(text,"the top line")-text), line=0;
    for(uint64_t i=0;i<top;i++) line+=text[i]=='\n';
    CHECK(render_frame_begin(&ref.g,++ref.frame)==0 && layout_begin(&ref.l,ref.tree,(layout_viewport){0,0,0,0})==0); run(&ref);
    CHECK(render_frame_begin(&sub.g,++sub.frame)==0 && layout_begin(&sub.l,sub.tree,(layout_viewport){top,line,0,0})==0); run(&sub);
    uint64_t probe=0; sub.l.line_query_probe=&probe;
    unsigned compared=0;
    for(uint64_t b=0;b<=top;b++) {
        if(b<total && text[b]=='\n' && b && text[b-1]=='\r') continue; /* CRLF: not a cluster boundary */
        layout_wrap_row self; bool sa=false;
        CHECK(layout_visual_row(&ref.l,ref.tree,b,0,&self,&sa)==LAYOUT_DONE);
        for(int d=self.start==b?-1:0;d<=1;d++) { /* upward from row starts, as view asks */
            layout_wrap_row want,got; bool wa=false,ga=false;
            CHECK(layout_visual_row(&ref.l,ref.tree,b,d,&want,&wa)==LAYOUT_DONE);
            CHECK(layout_visual_row(&sub.l,sub.tree,b,d,&got,&ga)==LAYOUT_DONE);
            if(got.start!=want.start || got.end!=want.end || got.next!=want.next || got.line_start!=want.line_start ||
               got.line!=want.line || got.column!=want.column || got.continuation!=want.continuation || ga!=wa)
                fprintf(stderr,"above_top b=%llu d=%d got start=%llu line=%llu want start=%llu line=%llu\n",(unsigned long long)b,d,
                    (unsigned long long)got.start,(unsigned long long)got.line,(unsigned long long)want.start,(unsigned long long)want.line);
            CHECK(got.start==want.start && got.end==want.end && got.next==want.next && got.line_start==want.line_start);
            CHECK(got.line==want.line && got.column==want.column && got.continuation==want.continuation && ga==wa);
            compared++;
        }
    }
    CHECK(compared>0 && probe==0);
    destroy(&ref); destroy(&sub);
    /* (P) 70000 > LAYOUT_BYTE_BUDGET = 65536: no LF within the walk. */
    static char long_text[70000+16];
    memset(long_text,'a',70000); memcpy(long_text+70000,"\nb\nc\n",6);
    fixture f; init(&f,long_text,16,4,0);
    CHECK(render_frame_begin(&f.g,++f.frame)==0 && layout_begin(&f.l,f.tree,(layout_viewport){70001,1,0,0})==0); run(&f);
    probe=0; f.l.line_query_probe=&probe;
    layout_wrap_row r; bool approx=false;
    CHECK(layout_visual_row(&f.l,f.tree,70001,-1,&r,&approx)==LAYOUT_DONE && r.line==0 && r.line_start==0 && probe>0);
    destroy(&f);
}
/* edit-cxho.26: a layout restarted while busy (a second key before the
 * frame) must not leave its cached rows reading as real rows at byte 0. */
static void c26_restart_rows(void)
{
    const char *text="one two three\nfour five six seven\neight\nnine ten\neleven twelve\nthirteen\n";
    fixture f,fresh; init(&f,text,10,6,0); show(&f);
    CHECK(piece_insert(f.tree,3,(const uint8_t *)"x",1)==0);
    CHECK(layout_edit(&f.l,3,0,1,0,0)==LAYOUT_MORE && layout_busy(&f.l));
    CHECK(piece_insert(f.tree,4,(const uint8_t *)"y",1)==0);
    CHECK(layout_edit(&f.l,4,0,1,0,0)==LAYOUT_MORE && layout_busy(&f.l) && f.l.row==0); /* restarted */
    for(uint32_t r=1;r<6;r++) {
        const layout_wrap_row *w=&f.l.wrap_rows[r];
        if(w->start!=LAYOUT_VOID_ROW || w->end!=LAYOUT_VOID_ROW || w->next!=LAYOUT_VOID_ROW || f.rb[r]!=LAYOUT_VOID_ROW)
            fprintf(stderr,"edit-cxho.26 restart: row %u start=%llu next=%llu row_byte=%llu, want empty\n",r,
                (unsigned long long)w->start,(unsigned long long)w->next,(unsigned long long)f.rb[r]);
        CHECK(w->start==LAYOUT_VOID_ROW && w->end==LAYOUT_VOID_ROW && w->next==LAYOUT_VOID_ROW && f.rb[r]==LAYOUT_VOID_ROW);
    }
    run(&f);
    init(&fresh,"onexy two three\nfour five six seven\neight\nnine ten\neleven twelve\nthirteen\n",10,6,0); show(&fresh);
    CHECK(memcmp(f.rb,fresh.rb,6u*sizeof f.rb[0])==0 && memcmp(f.cells,fresh.cells,60u*sizeof f.cells[0])==0);
    for(uint32_t r=0;r<6;r++) CHECK(f.l.wrap_rows[r].start==fresh.l.wrap_rows[r].start && f.l.wrap_rows[r].next==fresh.l.wrap_rows[r].next);
    destroy(&f); destroy(&fresh);
    puts("edit-cxho.26: a restarted layout's rows read empty, not row 0");
}
/* edit-cxho.26: an edit on the top's logical line before a continuation top
 * returns LAYOUT_RESET and leaves the layout idle with pre-edit rows (the
 * host re-lays out at its next turn); a row query meanwhile must not
 * answer from those rows. (P) 40 'a' fill four 10-column rows. */
static void c26_reset_rows(void)
{
    const char *text="aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\nx\ny\nz\n";
    fixture f; init(&f,text,10,3,0); show(&f);
    layout_wrap_row top=f.l.wrap_rows[2]; CHECK(top.start==20 && top.continuation);
    CHECK(render_frame_begin(&f.g,++f.frame)==0 && layout_begin_visual(&f.l,f.tree,(layout_viewport){0,0,0,0},&top)==0); run(&f);
    CHECK(f.l.wrap_rows[0].start==20);
    CHECK(piece_delete(f.tree,0,5,NULL)==0);
    CHECK(layout_edit(&f.l,0,5,0,0,0)==LAYOUT_RESET && !layout_busy(&f.l));
    unsigned stale=0;
    for(uint64_t b=15;b<=piece_len(f.tree);b++) {
        layout_wrap_row got,want; bool ga=false,wa=false;
        CHECK(layout_visual_row(&f.l,f.tree,b,0,&got,&ga)==LAYOUT_DONE);
        CHECK(layout_visual_row_fresh(&f.l,f.tree,b,&want,&wa)==LAYOUT_DONE);
        if(got.start!=want.start || got.next!=want.next) {
            fprintf(stderr,"edit-cxho.26 reset: byte %llu cached row %llu..%llu, fresh %llu..%llu\n",(unsigned long long)b,
                (unsigned long long)got.start,(unsigned long long)got.next,(unsigned long long)want.start,(unsigned long long)want.next);
            stale++;
        }
    }
    CHECK(stale==0);
    destroy(&f);
    puts("edit-cxho.26: rows left by LAYOUT_RESET are not answered from");
}
/* edit-cxho.26: layout_wrap_follow_edit against the truth of a complete
 * fresh layout twice as tall, in every state a key can find the layout in:
 * idle, planning a line partway, painting partway, restarted (two edits
 * before a frame). Slices of 1-4 clusters stop it partway. */
static uint64_t c26_next(uint64_t *s) { *s^=*s<<13; *s^=*s>>7; *s^=*s<<17; return *s; }
static void c26_follow_truth(void)
{
    enum { N=5, TALL=10, W=12 };
    unsigned checked=0, wrong=0, states[4]={0}, far=0;
    const char *env=getenv("C26_SEEDS"); uint64_t seeds=env?strtoull(env,NULL,10):600u;
    for(uint64_t seed=1;seed<=seeds;seed++) {
        uint64_t s=seed*UINT64_C(0x9e3779b97f4a7c15)|1u;
        char text[512]; size_t len=0;
        while(len<300) {
            uint64_t r=c26_next(&s); size_t w=1+r%11;
            for(size_t i=0;i<w;i++) text[len++]=(char)('a'+(r>>8)%3);
            text[len++]=(r>>16)%6==0?'\n':' ';
        }
        text[len]=0;
        fixture f; init(&f,text,W,N,1+(uint32_t)(c26_next(&s)%4)); show(&f);
        unsigned edits=1+(unsigned)(c26_next(&s)%2); uint64_t caret=0;
        for(unsigned k=0;k<edits;k++) {
            uint64_t total=piece_len(f.tree), off=c26_next(&s)%(total<90?total:90);
            if(c26_next(&s)%3==0 && off+3<=total) {
                uint8_t gone[3]; uint64_t n=1+c26_next(&s)%3, nl=0;
                CHECK(piece_read(f.tree,off,gone,(size_t)n)==0);
                for(uint64_t i=0;i<n;i++) nl+=gone[i]=='\n';
                CHECK(piece_delete(f.tree,off,n,NULL)==0);
                CHECK(layout_edit(&f.l,off,n,0,nl,0)>=0); caret=off;
            } else {
                static const char pool[]="ab ab ab \n";
                uint8_t add[14]; uint64_t n=1+c26_next(&s)%14, nl=0;
                for(uint64_t i=0;i<n;i++) { add[i]=(uint8_t)pool[c26_next(&s)%(sizeof pool-1)]; nl+=add[i]=='\n'; }
                CHECK(piece_insert(f.tree,off,add,(size_t)n)==0);
                CHECK(layout_edit(&f.l,off,0,n,0,nl)>=0); caret=off+n;
            }
            unsigned slices=(unsigned)(c26_next(&s)%6);
            for(unsigned j=0;j<slices && layout_busy(&f.l);j++) CHECK(layout_run(&f.l)>=0);
        }
        if(c26_next(&s)%4==0) caret=c26_next(&s)%(piece_len(f.tree)+1);
        char now[512]; size_t total=(size_t)piece_len(f.tree); CHECK(total<sizeof now);
        CHECK(piece_read(f.tree,0,(uint8_t *)now,total)==0); now[total]=0;
        fixture t; init(&t,now,W,TALL,0); show(&t);
        layout_wrap_row c; bool a=false;
        CHECK(layout_visual_row_fresh(&f.l,f.tree,caret,&c,&a)==LAYOUT_DONE);
        uint32_t i=0; while(i<TALL && t.l.wrap_rows[i].start!=c.start) i++;
        states[!layout_busy(&f.l)?0:f.l.wrap_planning?(f.l.row>f.l.wrap_first?2:1):3]++; far+=i>=N;
        for(uint32_t scroll=1;scroll<=N-1u;scroll+=N-2u) {
            layout_wrap_row top; bool moved=false, ap=false;
            CHECK(layout_wrap_follow_edit(&f.l,f.tree,0,&c,scroll,&top,&moved,&ap)==LAYOUT_DONE);
            uint64_t want=i<N?0:i<=N-1u+scroll?t.l.wrap_rows[i-(N-1u)].start:c.start;
            bool ok=i<N?!moved:moved && top.start==want;
            if(!ok && wrong++<8)
                fprintf(stderr,"edit-cxho.26 follow seed %llu: busy %d planning %d row %u caret %llu row idx %u scroll %u: moved %d top %llu, want top %llu\n",
                    (unsigned long long)seed,layout_busy(&f.l),f.l.wrap_planning,f.l.row,(unsigned long long)caret,i,scroll,moved,
                    (unsigned long long)(moved?top.start:0),(unsigned long long)want);
            checked++;
        }
        destroy(&t); destroy(&f);
    }
    printf("edit-cxho.26: %u follow decisions checked against a fresh layout, %u wrong "
        "(idle %u, planning at its first row %u, planning partway %u, painting %u; caret below the screen %u)\n",
        checked,wrong,states[0],states[1],states[2],states[3],far);
    CHECK(wrong==0);
}
int main(int argc,char **argv)
{
    /* edit-cxho.26 selectors, each case alone. */
    if(argc==2 && strcmp(argv[1],"c26-restart")==0) { c26_restart_rows(); return 0; }
    if(argc==2 && strcmp(argv[1],"c26-reset")==0) { c26_reset_rows(); return 0; }
    if(argc==2 && strcmp(argv[1],"c26-follow")==0) { c26_follow_truth(); return 0; }
    c26_restart_rows(); c26_reset_rows(); c26_follow_truth(); above_top_queries(); large_hscroll(); cursor_stops(); lookahead_word_edit(); whitespace_home(); resize(); gutter_only_boundary(); wide_navigation(); lookahead_edit(); hidden_separator_cursor(); query_edges(); basic(); edits(); navigation(); hard_end(); edges(); newline_edits(); long_line(); typing_allocations(); puts("wrap_test: all passed"); return 0;
}
