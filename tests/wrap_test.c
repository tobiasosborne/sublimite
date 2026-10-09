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
    CHECK(view_command(&v,VIEW_HOME,false,NULL,0,&ch)==0 && v.state.selection.cursor==0 && !v.state.visual_end);
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
    const char *text="one two three four\n  alpha beta gamma delta\nlast";
    fixture a,b; init(&a,text,9,8,3); show(&a);
    a.g.dims.cols=13; show(&a); /* caller capacity already covers the wider grid */
    init(&b,text,13,8,0); show(&b);
    CHECK(memcmp(a.cells,b.cells,13u*8u*sizeof(render_cell))==0); destroy(&b);
    a.g.dims.cols=5; show(&a); init(&b,text,5,8,0); show(&b);
    CHECK(memcmp(a.cells,b.cells,5u*8u*sizeof(render_cell))==0);
    destroy(&a); destroy(&b);
}
int main(void) { resize(); gutter_only_boundary(); whitespace_home(); wide_navigation(); lookahead_edit(); hidden_separator_cursor(); query_edges(); basic(); edits(); navigation(); hard_end(); edges(); newline_edits(); long_line(); typing_allocations(); puts("wrap_test: all passed"); return 0; }
