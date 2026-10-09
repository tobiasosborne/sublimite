#include "view/view.h"
#include "base/base.h"
#include "layout/layout.h"
#include "undo/undo.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

#define CHECK(x) EDIT_ASSERT(x)
typedef struct fixture { edit_arena arena; piece_tree *tree; view v; } fixture;
static void *alloc_piece(void *ctx, size_t n) { return edit_arena_alloc(ctx,n,16); }
static void free_piece(void *ctx, void *p, size_t n) { (void)ctx; (void)p; (void)n; }
static void init(fixture *f, const uint8_t *text, size_t n, uint32_t rows, uint32_t cols)
{
    CHECK(edit_arena_init(&f->arena,64u*1024u*1024u)==0);
    piece_allocator a={&f->arena,alloc_piece,free_piece};
    f->tree=piece_create(&a); CHECK(f->tree);
    CHECK(piece_init_copy(f->tree,text,n)==0);
    view_config cfg={4,rows,cols,NULL,NULL}; view_init(&f->v,f->tree,&cfg);
}
static void destroy(fixture *f) { piece_destroy(f->tree); edit_arena_free(&f->arena); }
static view_change command(fixture *f, view_key key, bool shift, const uint8_t *p, size_t n)
{
    view_change all={0}, c; int rc=view_command(&f->v,key,shift,p,n,&c);
    if(c.changed) all=c;
    CHECK(f->v.scanned<=VIEW_SCAN_BOUND);
    unsigned calls=0;
    while(rc==VIEW_MORE) {
        CHECK(++calls<10000); rc=view_continue(&f->v,&c);
        CHECK(f->v.scanned<=VIEW_SCAN_BOUND); if(c.changed) {
            if(all.changed) {
                CHECK(c.offset==all.offset+all.new_len && c.new_len==0);
                all.old_len+=c.old_len;
            } else all=c;
        }
    }
    CHECK(rc==0); return all;
}
static void expect(fixture *f, const uint8_t *p, size_t n, uint64_t cursor, uint64_t anchor, uint64_t pref)
{
    uint8_t got[1024]; CHECK(n<=sizeof got); CHECK(piece_len(f->tree)==n);
    CHECK(piece_read(f->tree,0,got,n)==0); CHECK(memcmp(got,p,n)==0);
    if(f->v.state.selection.cursor!=cursor || f->v.state.selection.anchor!=anchor ||
       f->v.state.selection.preferred_col!=pref) {
        fprintf(stderr,"view state got %llu/%llu/%llu expected %llu/%llu/%llu\n",
            (unsigned long long)f->v.state.selection.cursor,(unsigned long long)f->v.state.selection.anchor,
            (unsigned long long)f->v.state.selection.preferred_col,(unsigned long long)cursor,
            (unsigned long long)anchor,(unsigned long long)pref);
        CHECK(false);
    }
}
/* KEY|typed escaped bytes|cursor|anchor|preferred (- = unset)|expected escaped text
 * One line is one command with a full assertion, not just a final snapshot. */
static size_t unescape(const char *s, uint8_t *out)
{
    size_t n=0;
    while(*s) {
        if(*s=='\\') { ++s; if(*s=='n') out[n++]='\n'; else if(*s=='r') out[n++]='\r';
            else if(*s=='t') out[n++]='\t'; else if(*s=='x') { char hex[3]={s[1],s[2],0}; out[n++]=(uint8_t)strtoul(hex,NULL,16); s+=2; }
            else out[n++]=(uint8_t)*s;
        } else out[n++]=(uint8_t)*s;
        ++s;
    }
    return n;
}
static view_key key_name(const char *s)
{
    const char *names[]={"Left","Right","Ctrl+Left","Ctrl+Right","Up","Down","PageUp","PageDown",
        "Home","End","Ctrl+Home","Ctrl+End","Ctrl+A","Ctrl+L","Ctrl+D","Backspace","Delete",
        "Ctrl+Backspace","Ctrl+Delete","Type"};
    for(unsigned i=0;i<sizeof names/sizeof names[0];++i) if(strcmp(names[i],s)==0) return (view_key)i;
    CHECK(false); return VIEW_LEFT;
}
static void script(const char *initial, const char *log, uint32_t rows, uint32_t cols)
{
    uint8_t bytes[1024]; size_t n=unescape(initial,bytes); fixture f; init(&f,bytes,n,rows,cols);
    const char *next=log; unsigned step=0;
    while(*next) {
        char line[2048]; const char *e=strchr(next,'\n'); CHECK(e); size_t k=(size_t)(e-next);
        CHECK(k<sizeof line); memcpy(line,next,k); line[k]=0; next=e+1;
        char *parts[6]; parts[0]=line;
        for(unsigned j=1;j<6;++j) { char *bar=strchr(parts[j-1],'|'); CHECK(bar); *bar=0; parts[j]=bar+1; }
        bool shift=strncmp(parts[0],"Shift+",6)==0; const char *name=parts[0]+(shift?6:0);
        n=unescape(parts[1],bytes); (void)command(&f,key_name(name),shift,bytes,n);
        n=unescape(parts[5],bytes);
        ++step;
        expect(&f,bytes,n,strtoull(parts[2],NULL,10),strtoull(parts[3],NULL,10),
            strcmp(parts[4],"-")==0?VIEW_PREFERRED_UNSET:strtoull(parts[4],NULL,10));
    }
    CHECK(step>0);
    destroy(&f);
}
static void scripts(void)
{
    script("", "Left||0|0|-|\nRight||0|0|-|\nUp||0|0|0|\nDown||0|0|0|\n"
        "Home||0|0|-|\nEnd||0|0|-|\nBackspace||0|0|-|\nDelete||0|0|-|\n"
        "Ctrl+D||0|0|-|\nCtrl+L||0|0|-|\nCtrl+A||0|0|-|\nType|abc|3|3|-|abc\n"
        "Shift+Left||2|3|-|abc\nType|X|3|3|-|abX\nCtrl+Home||0|0|-|abX\n"
        "Delete||0|0|-|bX\nCtrl+End||2|2|-|bX\nBackspace||1|1|-|b\n",3,5);
    script("ab cd..ef\\nxy", "Ctrl+Right||2|2|-|ab cd..ef\\nxy\n"
        "Ctrl+Right||5|5|-|ab cd..ef\\nxy\nCtrl+Right||7|7|-|ab cd..ef\\nxy\n"
        "Ctrl+Right||9|9|-|ab cd..ef\\nxy\nCtrl+Right||10|10|-|ab cd..ef\\nxy\n"
        "Ctrl+Right||12|12|-|ab cd..ef\\nxy\nCtrl+Left||10|10|-|ab cd..ef\\nxy\n"
        "Ctrl+Left||9|9|-|ab cd..ef\\nxy\nCtrl+Left||7|7|-|ab cd..ef\\nxy\n"
        "Ctrl+Left||5|5|-|ab cd..ef\\nxy\nCtrl+Left||3|3|-|ab cd..ef\\nxy\n"
        "Ctrl+D||5|3|-|ab cd..ef\\nxy\nRight||5|5|-|ab cd..ef\\nxy\n"
        "Shift+Ctrl+Left||3|5|-|ab cd..ef\\nxy\nLeft||3|3|-|ab cd..ef\\nxy\n"
        "Ctrl+Delete||3|3|-|ab ..ef\\nxy\nCtrl+Backspace||0|0|-|..ef\\nxy\n"
        "Ctrl+L||5|0|-|..ef\\nxy\nCtrl+L||7|0|-|..ef\\nxy\n"
        "Type|q|1|1|-|q\n",3,10);
    script("  abc\\nq\\n  z", "Home||2|2|-|  abc\\nq\\n  z\nHome||0|0|-|  abc\\nq\\n  z\n"
        "End||5|5|-|  abc\\nq\\n  z\nDown||7|7|5|  abc\\nq\\n  z\n"
        "Down||11|11|5|  abc\\nq\\n  z\nDown||11|11|5|  abc\\nq\\n  z\n"
        "Up||7|7|5|  abc\\nq\\n  z\nShift+Up||5|7|5|  abc\\nq\\n  z\n"
        "Shift+Home||2|7|-|  abc\\nq\\n  z\nShift+Ctrl+End||11|7|-|  abc\\nq\\n  z\n"
        "Shift+Ctrl+Home||0|7|-|  abc\\nq\\n  z\nType|X|1|1|-|X\\n  z\n",2,4);
    script("a\\nb\\nc\\nd\\ne\\nf\\ng", "PageDown||4|4|0|a\\nb\\nc\\nd\\ne\\nf\\ng\n"
        "Shift+PageDown||8|4|0|a\\nb\\nc\\nd\\ne\\nf\\ng\n"
        "Shift+PageUp||4|4|0|a\\nb\\nc\\nd\\ne\\nf\\ng\n"
        "PageUp||0|0|0|a\\nb\\nc\\nd\\ne\\nf\\ng\nUp||0|0|0|a\\nb\\nc\\nd\\ne\\nf\\ng\n",2,8);
    script("a\\r\\nb", "Right||1|1|-|a\\r\\nb\nRight||3|3|-|a\\r\\nb\n"
        "Left||1|1|-|a\\r\\nb\nEnd||1|1|-|a\\r\\nb\nDelete||1|1|-|ab\n"
        "Type|\\r\\n|3|3|-|a\\r\\nb\nBackspace||1|1|-|ab\n",3,8);
    script("abc\\nxy", "Shift+Right||1|0|-|abc\\nxy\nShift+Ctrl+Right||3|0|-|abc\\nxy\n"
        "Shift+End||3|0|-|abc\\nxy\nShift+Down||6|0|3|abc\\nxy\n"
        "Shift+Up||3|0|3|abc\\nxy\nShift+Home||0|0|-|abc\\nxy\n"
        "Shift+Ctrl+End||6|0|-|abc\\nxy\nShift+Ctrl+Home||0|0|-|abc\\nxy\n",3,8);
    script("12345\\n\\t中a\\n1234567", "End||5|5|-|12345\\n\\t中a\\n1234567\n"
        "Down||7|7|5|12345\\n\\t中a\\n1234567\nDown||17|17|5|12345\\n\\t中a\\n1234567\n"
        "Up||7|7|5|12345\\n\\t中a\\n1234567\nLeft||6|6|-|12345\\n\\t中a\\n1234567\n"
        "Ctrl+D||7|6|-|12345\\n\\t中a\\n1234567\n",3,8);
    script("\\r\\r\\r\\t", "Ctrl+End||4|4|-|\\r\\r\\r\\t\nCtrl+Backspace||3|3|-|\\r\\r\\r\n"
        "Ctrl+Backspace||2|2|-|\\r\\r\nCtrl+Home||0|0|-|\\r\\r\nCtrl+Delete||0|0|-|\\r\n",3,8);
    script("a b　c", "Ctrl+Right||1|1|-|a b　c\nCtrl+Right||4|4|-|a b　c\n"
        "Ctrl+Right||8|8|-|a b　c\nCtrl+Left||7|7|-|a b　c\n"
        "Ctrl+Left||3|3|-|a b　c\nCtrl+Left||0|0|-|a b　c\n"
        "Ctrl+Delete||0|0|-| b　c\nCtrl+Delete||0|0|-|　c\n",3,8);
    script(" 　q", "Home||5|5|-| 　q\nHome||0|0|-| 　q\n",3,8);
    script("\\xd8\\x80/xy", "Ctrl+End||5|5|-|\\xd8\\x80/xy\n"
        "Ctrl+Left||0|0|-|\\xd8\\x80/xy\nCtrl+End||5|5|-|\\xd8\\x80/xy\n"
        "Ctrl+Backspace||0|0|-|\n",3,8);
    script("\\t中é👩‍💻\\xff\\n123456789", "Right||1|1|-|\\t中é👩‍💻\\xff\\n123456789\n"
        "Right||4|4|-|\\t中é👩‍💻\\xff\\n123456789\nRight||7|7|-|\\t中é👩‍💻\\xff\\n123456789\n"
        "Right||18|18|-|\\t中é👩‍💻\\xff\\n123456789\nRight||19|19|-|\\t中é👩‍💻\\xff\\n123456789\n"
        "Down||29|29|10|\\t中é👩‍💻\\xff\\n123456789\nUp||19|19|10|\\t中é👩‍💻\\xff\\n123456789\n"
        "Left||18|18|-|\\t中é👩‍💻\\xff\\n123456789\nLeft||7|7|-|\\t中é👩‍💻\\xff\\n123456789\n"
        "Left||4|4|-|\\t中é👩‍💻\\xff\\n123456789\nLeft||1|1|-|\\t中é👩‍💻\\xff\\n123456789\n",3,5);
}
static void joins(void)
{
    fixture f; const uint8_t p[]={'a','\n',0xcc,0x81,'b'}; init(&f,p,sizeof p,3,10);
    (void)command(&f,VIEW_RIGHT,false,NULL,0);
    view_change c=command(&f,VIEW_DELETE,false,NULL,0);
    CHECK(c.changed && c.offset==1 && c.old_len==1 && c.new_len==0);
    const uint8_t q[]={'a',0xcc,0x81,'b'}; expect(&f,q,sizeof q,3,3,VIEW_PREFERRED_UNSET);
    (void)command(&f,VIEW_DOC_HOME,false,NULL,0);
    const uint8_t mark[]={0xcc,0x81}; c=command(&f,VIEW_TYPE,false,mark,sizeof mark);
    CHECK(c.offset==0 && c.new_len==2);
    const uint8_t r[]={0xcc,0x81,'a',0xcc,0x81,'b'}; expect(&f,r,sizeof r,2,2,VIEW_PREFERRED_UNSET);
    destroy(&f);
}
static void allocation_log(void)
{
    fixture f; init(&f,(const uint8_t *)"a\nb\nc",5,2,8);
    edit_malloc_guard_begin();
    for(unsigned i=0;i<10000;++i) {
        const view_key keys[]={VIEW_DOC_HOME,VIEW_TYPE,VIEW_RIGHT,VIEW_WORD_RIGHT,VIEW_DOWN,
            VIEW_UP,VIEW_PAGE_DOWN,VIEW_PAGE_UP,VIEW_HOME,VIEW_END,VIEW_SELECT_WORD,VIEW_SELECT_LINE,
            VIEW_SELECT_ALL,VIEW_DELETE,VIEW_TYPE,VIEW_BACKSPACE,VIEW_WORD_DELETE,VIEW_WORD_BACKSPACE,
            VIEW_DOC_END,VIEW_LEFT};
        view_key k=keys[i%(sizeof keys/sizeof keys[0])];
        (void)command(&f,k,(i%7)==0,(const uint8_t *)"x",k==VIEW_TYPE?1u:0u);
    }
    size_t count=edit_malloc_guard_end(); CHECK(count==0);
    printf("view_test: 10000 keys mallocs=%zu guard=%s\n",count,edit_malloc_guard_active()?"active":"ASan-inert");
    destroy(&f);
}
static int ascii_checkpoint(void *ctx, const piece_tree *tree, uint64_t line,
    uint64_t byte_target, uint64_t col_target, uint64_t *byte, uint64_t *col)
{
    unsigned *calls=ctx; ++*calls; CHECK(line==0);
    uint64_t target=byte_target==UINT64_MAX?col_target:byte_target;
    target=target<piece_len(tree)?target:piece_len(tree);
    *byte=target; *col=target; return 1;
}
static void long_lines(void)
{
    size_t n=200000; uint8_t *text=malloc(n); CHECK(text); memset(text,'x',n);
    fixture f; init(&f,text,n,4,20);
    (void)command(&f,VIEW_DOC_END,false,NULL,0);
    CHECK(!f.v.state.approximate && f.v.scanned<=VIEW_SCAN_BOUND);
    CHECK(f.v.state.selection.cursor==n);
    (void)command(&f,VIEW_LEFT,false,NULL,0); CHECK(f.v.state.selection.cursor==n-1);
    /* An ordinary word near the end of a huge line should not replay its prefix. */
    destroy(&f); memcpy(text+n-7,"foo bar",7); init(&f,text,n,4,20);
    (void)command(&f,VIEW_DOC_END,false,NULL,0);
    view_change word_change;
    CHECK(view_command(&f.v,VIEW_WORD_LEFT,false,NULL,0,&word_change)==VIEW_OK);
    CHECK(f.v.state.selection.cursor==n-3);
    (void)command(&f,VIEW_DOC_END,false,NULL,0);
    unsigned calls=0; f.v.config.checkpoint=ascii_checkpoint; f.v.config.checkpoint_ctx=&calls;
    (void)command(&f,VIEW_DOC_END,false,NULL,0);
    CHECK(!f.v.state.approximate && calls>0 && f.v.state.hscroll==n-19);
    (void)command(&f,VIEW_UP,false,NULL,0); CHECK(f.v.state.selection.cursor==0);
    CHECK(f.v.state.selection.preferred_col==n);
    (void)command(&f,VIEW_DOWN,false,NULL,0); CHECK(f.v.state.selection.cursor==n);
    f.v.config.checkpoint=NULL;
    /* Edits must not consult now-stale checkpoints in the same call. */
    f.v.config.checkpoint=ascii_checkpoint; unsigned before=calls;
    (void)command(&f,VIEW_TYPE,false,(const uint8_t *)"a",1); CHECK(calls==before);
    destroy(&f);
    text[0]='a'; for(size_t i=1;i<n-1;i+=2) { text[i]=0xcc; text[i+1]=0x81; }
    text[n-1]='b'; init(&f,text,n,4,20);
    view_change c; int rc=view_command(&f.v,VIEW_RIGHT,false,NULL,0,&c);
    CHECK(rc==VIEW_MORE && view_busy(&f.v)); CHECK(f.v.state.selection.cursor==0);
    CHECK(view_command(&f.v,VIEW_END,false,NULL,0,&c)==VIEW_ERR_BUSY);
    unsigned resumes=0;
    while(rc==VIEW_MORE) { rc=view_continue(&f.v,&c); CHECK(f.v.scanned<=VIEW_SCAN_BOUND); ++resumes; }
    CHECK(rc==0 && resumes>0 && f.v.state.selection.cursor==n-1);
    (void)command(&f,VIEW_LEFT,false,NULL,0); CHECK(f.v.state.selection.cursor==0);
    (void)command(&f,VIEW_DOC_END,false,NULL,0); CHECK(f.v.state.selection.cursor==n);
    (void)command(&f,VIEW_LEFT,false,NULL,0); CHECK(f.v.state.selection.cursor==n-1);
    (void)command(&f,VIEW_BACKSPACE,false,NULL,0); CHECK(piece_len(f.tree)==1 && f.v.state.selection.cursor==0);
    destroy(&f); free(text);
    /* A long cluster starting exactly where scalar lookahead exhausts the
     * fallback budget must yield, never wrap the remaining size_t budget. */
    n=27255; text=malloc(n); CHECK(text); memset(text,'x',7253); text[7253]='a';
    for(size_t i=7254;i<27254;i+=2) { text[i]=0xcc; text[i+1]=0x81; }
    text[27254]='b'; init(&f,text,n,4,20);
    (void)command(&f,VIEW_DOC_END,false,NULL,0);
    CHECK(f.v.scanned<=VIEW_SCAN_BOUND && !f.v.state.approximate);
    destroy(&f); free(text);
}
static void scroll_and_selection(void)
{
    fixture f; init(&f,(const uint8_t *)"abcdef\nabcdef\nabcdef\nabcdef\nabcdef",34,2,3);
    (void)command(&f,VIEW_RIGHT,false,NULL,0); (void)command(&f,VIEW_RIGHT,false,NULL,0);
    (void)command(&f,VIEW_DOWN,false,NULL,0);
    CHECK(f.v.state.first_line==0 && f.v.state.first_byte==0);
    (void)command(&f,VIEW_PAGE_DOWN,false,NULL,0);
    CHECK(f.v.state.first_line==2 && f.v.state.first_byte==14 && f.v.state.selection.cursor==23);
    (void)command(&f,VIEW_END,false,NULL,0); CHECK(f.v.state.hscroll==4);
    (void)command(&f,VIEW_HOME,false,NULL,0); CHECK(f.v.state.hscroll==0);
    (void)command(&f,VIEW_DOC_END,true,NULL,0);
    CHECK(f.v.state.selection.anchor==21 && f.v.state.selection.cursor==34);
    (void)command(&f,VIEW_LEFT,false,NULL,0); CHECK(f.v.state.selection.cursor==21);
    (void)command(&f,VIEW_DOC_HOME,true,NULL,0); (void)command(&f,VIEW_RIGHT,false,NULL,0);
    CHECK(f.v.state.selection.cursor==21 && f.v.state.selection.anchor==21);
    destroy(&f);
}
static void rendered_cursor(fixture *f, bool ascii)
{
    render_grid grid; render_cell cells[20]; uint64_t dirty=0, row_byte=0; uint32_t used=0;
    CHECK(render_grid_init(&grid,(render_dims){20,1,1,1},cells,20,&dirty,1)==RENDER_OK);
    CHECK(render_frame_begin(&grid,1)==RENDER_OK);
    layout l; layout_config cfg={0}; cfg.slice_clusters=256;
    CHECK(layout_init(&l,&grid,&cfg,&row_byte,&used)==LAYOUT_DONE);
    layout_checkpoint entries[64]; layout_checkpoint_store store={0};
    if(ascii) {
        uint64_t n=piece_len(f->tree); size_t count=0;
        for(uint64_t p=0;p<n;p+=LAYOUT_CHECKPOINT_STRIDE) entries[count++]=(layout_checkpoint){p,p};
        entries[count++]=(layout_checkpoint){n,n}; CHECK(count<=64);
        store.entries=entries; store.capacity=64; store.count=count; store.source=f->tree;
        store.end=n; store.columns=n; store.tab=4; store.complete=true;
        CHECK(layout_set_checkpoints(&l,&store)==LAYOUT_DONE);
    }
    CHECK(f->v.state.hscroll<=UINT32_MAX);
    layout_set_cursor(&l,f->v.state.selection.cursor);
    layout_viewport vp={f->v.state.first_byte,f->v.state.first_line,(uint32_t)f->v.state.hscroll,0};
    CHECK(layout_begin(&l,f->tree,vp)==LAYOUT_DONE);
    int rc; do { rc=layout_run(&l); } while(rc==LAYOUT_MORE); CHECK(rc==LAYOUT_DONE);
    unsigned cursors=0; for(size_t i=0;i<20;++i) if(cells[i].attrs&RENDER_ATTR_CURSOR) ++cursors;
    CHECK(cursors==1);
}
static void review_2(void)
{
    size_t n=200000; uint8_t *p=malloc(n); CHECK(p); memset(p,'x',n);
    fixture f; init(&f,p,n,1,20); unsigned calls=0;
    f.v.config.checkpoint=ascii_checkpoint; f.v.config.checkpoint_ctx=&calls;
    (void)command(&f,VIEW_DOC_END,false,NULL,0); CHECK(f.v.state.hscroll==n-19);
    unsigned before=calls; (void)command(&f,VIEW_TYPE,false,(const uint8_t *)"a",1);
    CHECK(calls==before);
    CHECK(f.v.state.hscroll<=n+1 && n+1-f.v.state.hscroll<20);
    rendered_cursor(&f,true);
    f.v.config.checkpoint=NULL; (void)command(&f,VIEW_DOC_HOME,false,NULL,0);
    (void)command(&f,VIEW_DOC_END,false,NULL,0);
    CHECK(f.v.state.hscroll<=n+1 && n+1-f.v.state.hscroll<20); rendered_cursor(&f,true);
    (void)command(&f,VIEW_LEFT,false,NULL,0);
    CHECK(n-f.v.state.hscroll<20); rendered_cursor(&f,true);
    destroy(&f); free(p);
    init(&f,(const uint8_t *)"\tab",3,1,2); (void)command(&f,VIEW_RIGHT,false,NULL,0);
    f.v.config.tab_width=8; (void)command(&f,VIEW_END,false,NULL,0);
    CHECK(f.v.state.hscroll==9); destroy(&f);
    /* Keep the original cancel-before-external-edit/manual-state obligation
     * compatible too: cancellation must discard private cached columns. */
    init(&f,(const uint8_t *)"ab",2,1,2); (void)command(&f,VIEW_RIGHT,false,NULL,0);
    view_cancel(&f.v); const uint8_t wide[]={0xe4,0xb8,0xad};
    CHECK(piece_insert(f.tree,0,wide,sizeof wide)==PIECE_OK);
    f.v.state.selection=(view_selection){4,4,VIEW_PREFERRED_UNSET};
    (void)command(&f,VIEW_END,false,NULL,0); CHECK(f.v.state.hscroll==3); destroy(&f);
    const uint8_t zero_prefix[]={0xcc,0x81,'\n','x'};
    init(&f,zero_prefix,sizeof zero_prefix,2,8);
    (void)command(&f,VIEW_RIGHT,false,NULL,0); (void)command(&f,VIEW_DOWN,false,NULL,0);
    (void)command(&f,VIEW_UP,false,NULL,0); CHECK(f.v.state.selection.cursor==0); destroy(&f);
    const uint8_t zero_after_control[]={0xff,0xcc,0x81,'\n','a','b'};
    init(&f,zero_after_control,sizeof zero_after_control,2,8);
    (void)command(&f,VIEW_END,false,NULL,0); (void)command(&f,VIEW_DOWN,false,NULL,0);
    (void)command(&f,VIEW_UP,false,NULL,0); CHECK(f.v.state.selection.cursor==1); destroy(&f);
    puts("review_2: exact long-line follow and rendered cursor passed");
}
static void review_9(void)
{
    size_t n=200001; uint8_t *p=malloc(n); CHECK(p); p[0]='a';
    for(size_t i=1;i<n;i+=2) { p[i]=0xcc; p[i+1]=0x81; }
    fixture f; init(&f,p,n,1,20); view_change c;
    int rc=view_command(&f.v,VIEW_RIGHT,false,NULL,0,&c); CHECK(rc==VIEW_MORE);
    CHECK(f.v.scanned<=4096);
    c=(view_change){1,2,3,true};
    CHECK(view_command(&f.v,VIEW_TYPE,false,NULL,1,&c)==VIEW_ERR_ARG && !c.changed);
    while(rc==VIEW_MORE) { rc=view_continue(&f.v,&c); CHECK(f.v.scanned<=4096 && !c.changed); }
    CHECK(rc==VIEW_OK && f.v.state.selection.cursor==n);
    destroy(&f); free(p); puts("review_9: conservative resumable scan budget passed");
}
static void review_1(void)
{
    const char *texts[]={"a\n","a\r\n","a\n\n","a\r\n\r\n","\n",""};
    for(size_t i=0;i<sizeof texts/sizeof texts[0];++i) {
        fixture f; size_t n=strlen(texts[i]); init(&f,(const uint8_t *)texts[i],n,1,8);
        (void)command(&f,VIEW_DOC_END,false,NULL,0);
        (void)command(&f,VIEW_END,false,NULL,0);
        CHECK(f.v.state.selection.cursor==n && f.v.state.selection.anchor==n);
        (void)command(&f,VIEW_END,true,NULL,0);
        CHECK(f.v.state.selection.cursor==n && f.v.state.selection.anchor==n);
        (void)command(&f,VIEW_DOC_HOME,false,NULL,0); (void)command(&f,VIEW_DOC_END,true,NULL,0);
        (void)command(&f,VIEW_END,true,NULL,0);
        CHECK(f.v.state.selection.cursor==n && f.v.state.selection.anchor==0);
        destroy(&f);
    }
    puts("review_1: trailing empty End/Shift+End passed");
}
static void review_3(void)
{
    size_t n=200003; uint8_t *p=malloc(n); CHECK(p); p[0]='a';
    for(size_t i=1;i<200001;i+=2) { p[i]=0xcc; p[i+1]=0x81; }
    p[200001]='\n'; p[200002]='b';
    fixture f; init(&f,p,n,1,8);
    (void)command(&f,VIEW_END,false,NULL,0);
    (void)command(&f,VIEW_DOC_END,true,NULL,0);
    CHECK(f.v.state.first_byte==200002);
    view_change c; CHECK(view_command(&f.v,VIEW_DELETE,false,NULL,0,&c)==VIEW_MORE);
    CHECK(c.changed && c.offset==200001 && c.old_len==2);
    view_cancel(&f.v);
    CHECK(!view_busy(&f.v) && f.v.state.selection.cursor==0 && f.v.state.selection.anchor==0);
    CHECK(f.v.state.first_byte==0 && f.v.state.first_line==0 && f.v.state.hscroll==0);
    rendered_cursor(&f,false);
    (void)command(&f,VIEW_RIGHT,false,NULL,0); CHECK(f.v.state.selection.cursor==200001);
    destroy(&f); free(p); puts("review_3: cancelled edit repair viewport passed");
}
static void review_4(void)
{
    size_t n=200001; uint8_t *p=malloc(n); CHECK(p); p[0]='a';
    for(size_t i=1;i<n;i+=2) { p[i]=0xcc; p[i+1]=0x81; }
    fixture f; init(&f,p,n,1,8); view_change c;
    CHECK(view_command(&f.v,VIEW_LEFT,false,NULL,0,&c)==VIEW_OK);
    CHECK(!c.changed && f.v.state.selection.cursor==0);
    CHECK(view_command(&f.v,VIEW_BACKSPACE,false,NULL,0,&c)==VIEW_OK);
    CHECK(!c.changed && f.v.state.selection.cursor==0);
    (void)command(&f,VIEW_DOC_END,false,NULL,0);
    view_state saved=f.v.state;
    CHECK(view_command(&f.v,VIEW_DELETE,false,NULL,0,&c)==VIEW_OK);
    CHECK(!c.changed && !view_busy(&f.v)); view_cancel(&f.v);
    CHECK(f.v.state.selection.cursor==saved.selection.cursor && f.v.state.hscroll==saved.hscroll);
    CHECK(view_command(&f.v,VIEW_TYPE,false,NULL,0,&c)==VIEW_OK);
    CHECK(!c.changed && f.v.state.selection.cursor==n);
    (void)command(&f,VIEW_DOC_HOME,true,NULL,0);
    CHECK(f.v.state.selection.cursor==0 && f.v.state.selection.anchor==n);
    CHECK(view_command(&f.v,VIEW_LEFT,true,NULL,0,&c)==VIEW_OK);
    CHECK(!c.changed && f.v.state.selection.cursor==0 && f.v.state.selection.anchor==n);
    destroy(&f); free(p);
    /* Existing editor callers use empty TYPE after external mutation and a
     * manually installed repair target. Preserve that established interface. */
    init(&f,NULL,0,1,8); CHECK(piece_insert(f.tree,0,(const uint8_t *)"a\n",2)==PIECE_OK);
    f.v.state.selection=(view_selection){2,2,VIEW_PREFERRED_UNSET};
    view_change repaired=command(&f,VIEW_TYPE,false,NULL,0);
    CHECK(!repaired.changed && f.v.state.first_byte==2 && f.v.state.first_line==1);
    CHECK(piece_delete(f.tree,0,2,NULL)==PIECE_OK); f.v.state.selection=(view_selection){0,0,VIEW_PREFERRED_UNSET};
    repaired=command(&f,VIEW_TYPE,false,NULL,0);
    CHECK(!repaired.changed && f.v.state.first_byte==0 && f.v.state.first_line==0);
    destroy(&f); puts("review_4: long-cluster boundary no-ops and legacy external repair passed");
}
static void review_11(void)
{
    fixture f; init(&f,(const uint8_t *)"abc",3,1,8);
    view_change c=command(&f,VIEW_TYPE,false,(const uint8_t *)"x",1); CHECK(c.changed);
    CHECK(view_command(&f.v,VIEW_TYPE,false,NULL,1,&c)==VIEW_ERR_ARG);
    CHECK(!c.changed && c.offset==0 && c.old_len==0 && c.new_len==0);
    c=(view_change){1,2,3,true}; CHECK(view_continue(NULL,&c)==VIEW_ERR_ARG); CHECK(!c.changed);
    c=(view_change){1,2,3,true}; CHECK(view_command(NULL,VIEW_LEFT,false,NULL,0,&c)==VIEW_ERR_ARG); CHECK(!c.changed);
    c=(view_change){1,2,3,true}; CHECK(view_command(&f.v,(view_key)99,false,NULL,0,&c)==VIEW_ERR_ARG); CHECK(!c.changed);
    f.v.busy=true; c=(view_change){1,2,3,true};
    CHECK(view_command(&f.v,VIEW_LEFT,false,NULL,0,&c)==VIEW_ERR_BUSY); CHECK(!c.changed);
    view_cancel(&f.v); c=(view_change){1,2,3,true};
    CHECK(view_continue(&f.v,&c)==VIEW_OK && !c.changed);
    destroy(&f); puts("review_11: reused error change output passed");
}
static void review_10(void)
{
    fixture f; init(&f,(const uint8_t *)"abc",3,1,8);
    undo_log u; CHECK(undo_init(&u,f.tree,64)==UNDO_OK);
    CHECK(view_set_undo(&f.v,&u)==VIEW_OK);
    (void)command(&f,VIEW_RIGHT,false,NULL,0);
    (void)command(&f,VIEW_DELETE,false,NULL,0);
    expect(&f,(const uint8_t *)"ac",2,1,1,VIEW_PREFERRED_UNSET);
    undo_change c; CHECK(undo_undo(&u,1,&c)==UNDO_OK);
    CHECK(c.groups==1 && c.has_state);
    uint8_t p[8]; CHECK(piece_len(f.tree)==3 && piece_read(f.tree,0,p,3)==PIECE_OK && memcmp(p,"abc",3)==0);
    CHECK(undo_redo(&u,1,&c)==UNDO_OK && c.groups==1);
    /* A new edit must invalidate redo without deleting the wrong next byte. */
    CHECK(undo_undo(&u,1,&c)==UNDO_OK && c.groups==1);
    f.v.state.selection=(view_selection){1,1,VIEW_PREFERRED_UNSET};
    (void)command(&f,VIEW_TYPE,false,(const uint8_t *)"x",1);
    CHECK(undo_get_stats(&u).redo_groups==0);
    (void)command(&f,VIEW_SELECT_ALL,false,NULL,0);
    (void)command(&f,VIEW_TYPE,false,(const uint8_t *)"z",1);
    CHECK(undo_undo(&u,1,&c)==UNDO_OK && c.groups==1 && c.operations>=2);
    CHECK(piece_len(f.tree)==4 && piece_read(f.tree,0,p,4)==PIECE_OK && memcmp(p,"axbc",4)==0);
    CHECK(undo_redo(&u,1,&c)==UNDO_OK && c.groups==1);
    CHECK(piece_len(f.tree)==1 && piece_read(f.tree,0,p,1)==PIECE_OK && p[0]=='z');
    undo_destroy(&u); destroy(&f); puts("review_10: undo delete/type/replace/group/redo integration passed");
}
static void review_14(void)
{
    fixture f; init(&f,(const uint8_t *)"ab",2,1,8);
    (void)command(&f,VIEW_RIGHT,false,NULL,0);
    const uint8_t wide[]={0xe4,0xb8,0xad}; CHECK(piece_insert(f.tree,0,wide,3)==PIECE_OK);
    view_change edit={0,0,3,true}, out;
    int rc=view_notify_edit(&f.v,&edit,VIEW_AFFINITY_AFTER,VIEW_AFFINITY_AFTER,&out);
    while(rc==VIEW_MORE) { rc=view_continue(&f.v,&out); } CHECK(rc==VIEW_OK && !out.changed);
    CHECK(f.v.state.selection.cursor==4 && f.v.state.selection.anchor==4);
    destroy(&f); init(&f,(const uint8_t *)"abcdef",6,1,8);
    f.v.state.selection=(view_selection){5,2,7}; CHECK(piece_delete(f.tree,1,3,NULL)==PIECE_OK);
    edit=(view_change){1,3,0,true};
    rc=view_notify_edit(&f.v,&edit,VIEW_AFFINITY_AFTER,VIEW_AFFINITY_BEFORE,&out);
    while(rc==VIEW_MORE) { rc=view_continue(&f.v,&out); } CHECK(rc==VIEW_OK);
    CHECK(f.v.state.selection.cursor==2 && f.v.state.selection.anchor==1);
    CHECK(f.v.state.selection.preferred_col==VIEW_PREFERRED_UNSET);
    destroy(&f); const uint8_t joined[]={'a','\n',0xcc,0x81,'b'}; init(&f,joined,sizeof joined,1,8);
    f.v.state.selection=(view_selection){2,1,7}; CHECK(piece_delete(f.tree,1,1,NULL)==PIECE_OK);
    edit=(view_change){1,1,0,true};
    rc=view_notify_edit(&f.v,&edit,VIEW_AFFINITY_AFTER,VIEW_AFFINITY_BEFORE,&out);
    while(rc==VIEW_MORE) { rc=view_continue(&f.v,&out); } CHECK(rc==VIEW_OK);
    CHECK(f.v.state.selection.cursor==3 && f.v.state.selection.anchor==0);
    view_state saved={{1,UINT64_MAX,7},99,99,UINT64_MAX,true,false,false,0};
    rc=view_restore(&f.v,&saved,&out);
    while(rc==VIEW_MORE) { rc=view_continue(&f.v,&out); } CHECK(rc==VIEW_OK);
    CHECK(f.v.state.selection.cursor==3 && f.v.state.selection.anchor==4);
    CHECK(f.v.state.first_byte==0 && f.v.state.first_line==0 && f.v.state.hscroll<=3);
    rendered_cursor(&f,false);
    destroy(&f);
    size_t n=200004; uint8_t *p=malloc(n); CHECK(p); p[0]='a'; p[1]='\n';
    for(size_t i=2;i<n-2;i+=2) { p[i]=0xcc; p[i+1]=0x81; } p[n-2]='b'; p[n-1]='c';
    init(&f,p,n,1,8); f.v.state.selection=(view_selection){2,1,7};
    CHECK(piece_delete(f.tree,1,1,NULL)==PIECE_OK); edit=(view_change){1,1,0,true};
    rc=view_notify_edit(&f.v,&edit,VIEW_AFFINITY_AFTER,VIEW_AFFINITY_BEFORE,&out);
    CHECK(rc==VIEW_MORE && view_busy(&f.v) && !out.changed);
    out=(view_change){1,2,3,true}; CHECK(view_restore(&f.v,&saved,&out)==VIEW_ERR_BUSY && !out.changed);
    view_cancel(&f.v); CHECK(f.v.state.first_byte==0 && f.v.state.hscroll==0 && f.v.state.selection.cursor==0);
    rc=view_restore(&f.v,&(view_state){{123,456,7},99,UINT64_MAX,UINT64_MAX,true,false,false,0},&out);
    CHECK(rc==VIEW_MORE); while(rc==VIEW_MORE) { rc=view_continue(&f.v,&out); CHECK(!out.changed && f.v.scanned<=VIEW_SCAN_BOUND); }
    CHECK(rc==VIEW_OK && f.v.state.selection.cursor==n-3 && f.v.state.selection.anchor==n-3);
    CHECK(f.v.state.first_line==0 && f.v.state.first_byte==0 && f.v.state.hscroll<=1);
    edit=(view_change){UINT64_MAX,1,1,true}; out=(view_change){1,2,3,true};
    CHECK(view_notify_edit(&f.v,&edit,VIEW_AFFINITY_AFTER,VIEW_AFFINITY_AFTER,&out)==VIEW_ERR_ARG && !out.changed);
    destroy(&f); free(p); puts("review_14: external rebase/affinity/cluster/restore passed");
}
typedef struct fault_fixture { fixture f; bool fail_all, fail_after_insert; uint64_t original_len; unsigned failures; } fault_fixture;
static void *fault_alloc(void *ctx, size_t n)
{
    fault_fixture *q=ctx;
    if(q->fail_all || (q->fail_after_insert && q->f.tree && piece_len(q->f.tree)>q->original_len)) {
        ++q->failures; return NULL;
    }
    return edit_arena_alloc(&q->f.arena,n,16);
}
static void fault_init(fault_fixture *q, const uint8_t *p, size_t n)
{
    memset(q,0,sizeof *q); CHECK(edit_arena_init(&q->f.arena,16u*1024u*1024u)==0);
    piece_allocator a={q,fault_alloc,free_piece}; q->f.tree=piece_create(&a); CHECK(q->f.tree);
    CHECK(piece_init_mapped(q->f.tree,p,n,NULL)==PIECE_OK); q->original_len=n;
    view_config cfg={4,1,8,NULL,NULL}; view_init(&q->f.v,q->f.tree,&cfg);
}
static void review_15(void)
{
    size_t n=262144; uint8_t *p=malloc(n); CHECK(p); memset(p,'a',n); p[n-2]='\n'; p[n-1]='b';
    for(unsigned use_undo=0;use_undo<2;++use_undo) {
    fault_fixture q; fault_init(&q,p,n); fixture *f=&q.f; view_change c; undo_log u;
    if(use_undo) { CHECK(undo_init(&u,f->tree,256)==UNDO_OK); CHECK(view_set_undo(&f->v,&u)==VIEW_OK); }
    f->v.state.selection=(view_selection){n,n,7}; f->v.state.first_byte=n-1; f->v.state.first_line=1;
    q.fail_all=true;
    CHECK(view_command(&f->v,VIEW_TYPE,false,(const uint8_t *)"x",1,&c)==PIECE_ERR_NOMEM);
    CHECK(q.failures>0 && !c.changed && !view_busy(&f->v) && piece_len(f->tree)==n);
    CHECK(f->v.state.selection.cursor==n && f->v.state.selection.anchor==n && f->v.state.selection.preferred_col==7);
    q.fail_all=false; (void)command(f,VIEW_SELECT_ALL,false,NULL,0);
    q.fail_all=true; int rc=view_command(&f->v,VIEW_DELETE,false,NULL,0,&c);
    CHECK(rc==VIEW_MORE && !c.changed);
    CHECK(view_continue(&f->v,&c)==PIECE_ERR_NOMEM);
    CHECK(!c.changed && piece_len(f->tree)==n && f->v.state.selection.cursor==n && f->v.state.selection.anchor==0);
    q.fail_all=false; q.fail_after_insert=true;
    uint8_t inserted[65536]; memset(inserted,'x',sizeof inserted);
    CHECK(view_command(&f->v,VIEW_TYPE,false,inserted,sizeof inserted,&c)==VIEW_MORE);
    CHECK(c.changed && c.offset==0 && c.old_len==0 && c.new_len==sizeof inserted);
    CHECK(view_continue(&f->v,&c)==PIECE_ERR_NOMEM && !c.changed);
    CHECK(piece_len(f->tree)==n+sizeof inserted);
    CHECK(!view_busy(&f->v) && f->v.state.selection.cursor==0 && f->v.state.selection.anchor==0);
    CHECK(f->v.state.first_byte==0 && f->v.state.first_line==0 && f->v.state.hscroll==0);
    uint8_t b; CHECK(piece_read(f->tree,0,&b,1)==PIECE_OK && b=='x'); rendered_cursor(f,false);
    uint8_t chunk[4096];
    for(size_t off=0;off<n;off+=sizeof chunk) {
        size_t len=n-off<sizeof chunk?n-off:sizeof chunk;
        CHECK(piece_read(f->tree,off+sizeof inserted,chunk,len)==PIECE_OK && memcmp(chunk,p+off,len)==0);
    }
    if(use_undo) {
        CHECK(!u.open && undo_get_stats(&u).undo_groups==1);
        q.fail_after_insert=false; undo_change uc; CHECK(undo_undo(&u,1,&uc)==UNDO_OK && uc.groups==1);
        CHECK(piece_len(f->tree)==n); view_state restored={{n,0,7},1,n-1,0,false,false,false,0};
        rc=view_restore(&f->v,&restored,&c); while(rc==VIEW_MORE) rc=view_continue(&f->v,&c);
        CHECK(rc==VIEW_OK);
        (void)command(f,VIEW_TYPE,false,(const uint8_t *)"x",1);
        CHECK(piece_len(f->tree)==1 && undo_get_stats(&u).redo_groups==0);
        undo_destroy(&u);
    } else {
    q.fail_after_insert=false;
    f->v.state.selection=(view_selection){sizeof inserted,0,VIEW_PREFERRED_UNSET};
    (void)command(f,VIEW_DELETE,false,NULL,0); CHECK(piece_len(f->tree)==n);
    }
    destroy(f);
    }
    free(p); puts("review_15: insert/delete/prefix allocation failures and retry passed");
}
/* Protected mappings turn accidental unbounded reads into deterministic reds. */
static void query_tail_signal(int sig)
{
    (void)sig;
    const char msg[]="review_7 RED: unindexed Down read protected tail before yielding\n";
    ssize_t written=write(STDERR_FILENO,msg,sizeof msg-1); (void)written; _exit(99);
}
static void proposal_7(void)
{
    size_t n=2u*1024u*1024u; uint8_t *p=mmap(NULL,n,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    CHECK(p!=MAP_FAILED); memset(p,'x',n); p[n-1]='\n'; fault_fixture q; fault_init(&q,p,n);
    CHECK(mprotect(p+n/2,n/2,PROT_NONE)==0);
    pid_t child=fork(); CHECK(child>=0);
    if(child==0) {
        struct sigaction sa={0}; sa.sa_handler=query_tail_signal; sigemptyset(&sa.sa_mask);
        CHECK(sigaction(SIGSEGV,&sa,NULL)==0);
        view_change c;
        CHECK(view_command(&q.f.v,VIEW_TYPE,false,(const uint8_t *)"a",1,&c)==VIEW_OK && c.changed);
        int rc=view_command(&q.f.v,VIEW_DOWN,false,NULL,0,&c);
        CHECK(rc==VIEW_MORE && !c.changed);
        for(unsigned i=0;i<16;++i) {
            rc=view_continue(&q.f.v,&c);
            CHECK(rc==VIEW_MORE && !c.changed && q.f.v.scanned<=VIEW_SCAN_BOUND);
        }
        view_cancel(&q.f.v); CHECK(q.f.v.state.selection.cursor==1);
        _exit(0);
    }
    int status; CHECK(waitpid(child,&status,0)==child);
    CHECK(mprotect(p+n/2,n/2,PROT_READ|PROT_WRITE)==0);
    CHECK(WIFEXITED(status) && WEXITSTATUS(status)==0);
    (void)command(&q.f,VIEW_DOWN,false,NULL,0);
    CHECK(q.f.v.state.selection.cursor==n && q.f.v.state.first_line==1);
    (void)command(&q.f,VIEW_PAGE_UP,false,NULL,0); CHECK(q.f.v.state.selection.cursor==0);
    destroy(&q.f); CHECK(munmap(p,n)==0);
    puts("review_7: protected-tail queries and exact resumable line motion passed");
}
static void prefix_signal(int sig)
{
    (void)sig; const char msg[]="review_8 RED: post-capture follow reread protected prefix\n";
    ssize_t written=write(STDERR_FILENO,msg,sizeof msg-1); (void)written; _exit(99);
}
static void proposal_8(void)
{
    size_t n=1048576; uint8_t *p=mmap(NULL,n,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    CHECK(p!=MAP_FAILED); memset(p,'x',n);
    for(unsigned use_undo=0;use_undo<2;++use_undo) {
        fault_fixture q; fault_init(&q,p,n); undo_log u;
        if(use_undo) { CHECK(undo_init(&u,q.f.tree,n/VIEW_MUTATION_BOUND+8)==UNDO_OK); CHECK(view_set_undo(&q.f.v,&u)==VIEW_OK); }
        q.f.v.state.selection=(view_selection){n,0,VIEW_PREFERRED_UNSET};
        view_change c; uint8_t typed='a';
        edit_malloc_guard_begin();
        int rc=view_command(&q.f.v,VIEW_TYPE,false,&typed,1,&c); size_t allocations=edit_malloc_guard_end();
        CHECK(rc==VIEW_MORE && !c.changed && piece_len(q.f.tree)==n && allocations==0);
        typed='z'; view_cancel(&q.f.v);
        CHECK(q.f.v.state.selection.cursor==n && q.f.v.state.selection.anchor==0 && piece_len(q.f.tree)==n);
        typed='a'; rc=view_command(&q.f.v,VIEW_TYPE,false,&typed,1,&c); typed='z';
        CHECK(rc==VIEW_MORE && !c.changed); CHECK(mprotect(p+n/2,n/2,PROT_NONE)==0);
        rc=view_continue(&q.f.v,&c);
        CHECK(rc==VIEW_MORE && c.changed && c.offset==0 && c.old_len==VIEW_MUTATION_BOUND && c.new_len==1);
        CHECK(q.f.v.scanned<=VIEW_SCAN_BOUND && piece_len(q.f.tree)==n-VIEW_MUTATION_BOUND+1);
        CHECK(mprotect(p+n/2,n/2,PROT_READ|PROT_WRITE)==0);
        view_cancel(&q.f.v); CHECK(q.f.v.state.selection.cursor==0 && q.f.v.state.first_byte==0);
        if(use_undo) {
            undo_change uc; CHECK(!u.open && undo_undo(&u,1,&uc)==UNDO_OK && uc.groups==1);
            CHECK(piece_len(q.f.tree)==n); CHECK(undo_redo(&u,1,&uc)==UNDO_OK && uc.groups==1);
        }
        if(use_undo) undo_destroy(&u);
        destroy(&q.f);
        /* Complete a fresh replacement, with per-call tuples and one group. */
        fault_init(&q,p,n);
        if(use_undo) { CHECK(undo_init(&u,q.f.tree,n/VIEW_MUTATION_BOUND+8)==UNDO_OK); CHECK(view_set_undo(&q.f.v,&u)==VIEW_OK); }
        q.f.v.state.selection=(view_selection){n,0,VIEW_PREFERRED_UNSET};
        typed='a'; rc=view_command(&q.f.v,VIEW_TYPE,false,&typed,1,&c); typed='z';
        uint64_t removed=0; unsigned commits=0;
        while(rc==VIEW_MORE) {
            edit_malloc_guard_begin(); rc=view_continue(&q.f.v,&c); allocations=edit_malloc_guard_end();
            CHECK(q.f.v.scanned<=VIEW_SCAN_BOUND && allocations==0);
            if(c.changed) {
                CHECK(c.old_len<=VIEW_MUTATION_BOUND && c.offset==(commits?1u:0u) && c.new_len==(commits?0u:1u));
                removed+=c.old_len; ++commits;
            }
        }
        CHECK(rc==VIEW_OK && removed==n && piece_len(q.f.tree)==1);
        uint8_t got; CHECK(piece_read(q.f.tree,0,&got,1)==PIECE_OK && got=='a');
        CHECK(q.f.v.state.selection.cursor==1 && q.f.v.state.selection.anchor==1);
        if(use_undo) {
            undo_change uc; CHECK(!u.open && undo_get_stats(&u).undo_groups==1);
            CHECK(undo_undo(&u,1,&uc)==UNDO_OK && uc.groups==1 && piece_len(q.f.tree)==n);
            CHECK(undo_redo(&u,1,&uc)==UNDO_OK && uc.groups==1 && piece_len(q.f.tree)==1);
            undo_destroy(&u);
        }
        destroy(&q.f);
    }
    CHECK(munmap(p,n)==0);
    /* Maximum deferred TYPE input shares a slice with a smaller first
     * capture; both the byte-work bound and synchronous input ownership hold. */
    p=mmap(NULL,n,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    CHECK(p!=MAP_FAILED); memset(p,'x',n); fault_fixture maximum; fault_init(&maximum,p,n);
    maximum.f.v.state.selection=(view_selection){n,0,VIEW_PREFERRED_UNSET};
    uint8_t payload[VIEW_MUTATION_BOUND]; memset(payload,'z',sizeof payload); view_change chunk;
    int maximum_rc=view_command(&maximum.f.v,VIEW_TYPE,false,payload,sizeof payload,&chunk);
    CHECK(maximum_rc==VIEW_MORE && !chunk.changed); memset(payload,'y',sizeof payload);
    while(maximum_rc==VIEW_MORE) {
        maximum_rc=view_continue(&maximum.f.v,&chunk); CHECK(maximum.f.v.scanned<=VIEW_SCAN_BOUND);
        if(chunk.changed) CHECK(chunk.old_len<=VIEW_MUTATION_BOUND);
    }
    CHECK(maximum_rc==VIEW_OK && piece_len(maximum.f.tree)==sizeof payload);
    CHECK(piece_read(maximum.f.tree,0,payload,sizeof payload)==PIECE_OK);
    for(size_t i=0;i<sizeof payload;++i) CHECK(payload[i]=='z');
    destroy(&maximum.f); CHECK(munmap(p,n)==0);
    /* Retain exact prefix metadata before a remote replacement. Following
     * must not reread a protected earlier prefix after capture completes. */
    n=2u*1024u*1024u; p=mmap(NULL,n,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    CHECK(p!=MAP_FAILED); memset(p,'x',n); fault_fixture deep; fault_init(&deep,p,n);
    unsigned checkpoints=0; deep.f.v.config.checkpoint=ascii_checkpoint; deep.f.v.config.checkpoint_ctx=&checkpoints;
    (void)command(&deep.f,VIEW_DOC_END,false,NULL,0);
    size_t old=16384; deep.f.v.state.selection.anchor=n-old;
    struct sigaction previous_signal, handler={0}; handler.sa_handler=prefix_signal; sigemptyset(&handler.sa_mask);
    CHECK(sigaction(SIGSEGV,&handler,&previous_signal)==0);
    CHECK(mprotect(p,n/2,PROT_NONE)==0); view_change c;
    int rc=view_command(&deep.f.v,VIEW_TYPE,false,(const uint8_t *)"a",1,&c);
    CHECK(rc==VIEW_MORE && !c.changed); unsigned calls=0; uint64_t removed=0;
    while(rc==VIEW_MORE) {
        CHECK(++calls<32); rc=view_continue(&deep.f.v,&c); CHECK(deep.f.v.scanned<=VIEW_SCAN_BOUND);
        if(c.changed) removed+=c.old_len;
    }
    CHECK(rc==VIEW_OK && removed==old && deep.f.v.state.selection.cursor==n-old+1);
    CHECK(mprotect(p,n/2,PROT_READ|PROT_WRITE)==0); CHECK(sigaction(SIGSEGV,&previous_signal,NULL)==0);
    destroy(&deep.f); CHECK(munmap(p,n)==0);
    puts("review_8: bounded replacement/progress/input ownership/cancel/undo passed");
}
static void wrap_selection(void)
{
    const uint8_t text[]="         word"; fixture f; init(&f,text,sizeof text-1,6,5);
    render_grid grid; render_cell cells[30]; uint64_t dirty=0, rows[6]; uint32_t used[6];
    CHECK(render_grid_init(&grid,(render_dims){5,6,1,1},cells,30,&dirty,1)==RENDER_OK);
    CHECK(render_frame_begin(&grid,1)==RENDER_OK); layout l; layout_config cfg={0}; cfg.slice_clusters=256;
    CHECK(layout_init(&l,&grid,&cfg,rows,used)==LAYOUT_DONE);
    CHECK(layout_wrap_init(&l,&f.arena)==LAYOUT_DONE && layout_set_wrap(&l,true)==LAYOUT_DONE);
    CHECK(layout_begin(&l,f.tree,(layout_viewport){0})==LAYOUT_DONE);
    int rc; do { rc=layout_run(&l); } while(rc==LAYOUT_MORE); CHECK(rc==LAYOUT_DONE);
    CHECK(view_set_wrap(&f.v,true,&l)==VIEW_OK);
    f.v.state.selection=(view_selection){1,1,VIEW_PREFERRED_UNSET};
    CHECK(!command(&f,VIEW_HOME,false,NULL,0).changed);
    CHECK(f.v.state.selection.cursor==5 && f.v.state.selection.anchor==5 && f.v.state.visual_end);
    CHECK(!command(&f,VIEW_DOWN,true,NULL,0).changed);
    CHECK(f.v.state.selection.cursor==8 && f.v.state.selection.anchor==5);
    CHECK(f.v.state.selection.preferred_col==5 && !f.v.state.visual_end);
    CHECK(!command(&f,VIEW_UP,true,NULL,0).changed);
    expect(&f,text,sizeof text-1,5,5,5); CHECK(f.v.state.visual_end);
    CHECK(!command(&f,VIEW_DOWN,true,NULL,0).changed);
    expect(&f,text,sizeof text-1,8,5,5); CHECK(!f.v.state.visual_end);
    /* Recompute the preferred column at a trailing endpoint with an existing
     * backward selection; both directions must keep its remote anchor. */
    f.v.state.selection=(view_selection){6,10,VIEW_PREFERRED_UNSET}; f.v.state.visual_end=false;
    CHECK(!command(&f,VIEW_END,true,NULL,0).changed);
    expect(&f,text,sizeof text-1,9,10,VIEW_PREFERRED_UNSET); CHECK(f.v.state.visual_end);
    CHECK(!command(&f,VIEW_UP,true,NULL,0).changed);
    expect(&f,text,sizeof text-1,5,10,5); CHECK(f.v.state.visual_end);
    CHECK(!command(&f,VIEW_DOWN,true,NULL,0).changed);
    expect(&f,text,sizeof text-1,8,10,5); CHECK(!f.v.state.visual_end);
    destroy(&f); puts("wrap_selection: anchored Shift+Up/Down preserves soft-row affinity and preferred column");
}
static void wrap_port(void)
{
    size_t cluster=20001, n=cluster+20; uint8_t *p=malloc(n); CHECK(p); p[0]='a';
    for(size_t i=1;i<cluster;i+=2) { p[i]=0xcc; p[i+1]=0x81; }
    memset(p+cluster,'b',n-cluster); fixture f; init(&f,p,n,3,5);
    render_grid grid; render_cell cells[15]; uint64_t dirty=0, rows[3]; uint32_t used[3];
    CHECK(render_grid_init(&grid,(render_dims){5,3,1,1},cells,15,&dirty,1)==RENDER_OK);
    CHECK(render_frame_begin(&grid,1)==RENDER_OK); layout l; layout_config cfg={0}; cfg.slice_clusters=256;
    CHECK(layout_init(&l,&grid,&cfg,rows,used)==LAYOUT_DONE);
    CHECK(layout_wrap_init(&l,&f.arena)==LAYOUT_DONE && layout_set_wrap(&l,true)==LAYOUT_DONE);
    CHECK(layout_begin(&l,f.tree,(layout_viewport){0})==LAYOUT_DONE);
    int rc; do { rc=layout_run(&l); } while(rc==LAYOUT_MORE); CHECK(rc==LAYOUT_DONE);
    CHECK(view_set_wrap(&f.v,true,&l)==VIEW_OK);
    f.v.state.selection=(view_selection){cluster,cluster,VIEW_PREFERRED_UNSET};
    layout_wrap_row dest=l.wrap_rows[1]; uint64_t expected=dest.start+(dest.indent<1?1u-dest.indent:0u);
    view_change c; rc=view_command(&f.v,VIEW_DOWN,true,NULL,0,&c);
    CHECK(rc==VIEW_MORE && view_busy(&f.v) && !c.changed);
    uint64_t previous=f.v.scan_pos; rc=view_continue(&f.v,&c);
    CHECK(rc==VIEW_MORE && f.v.scan_pos>previous && view_busy(&f.v));
    unsigned calls=0; while(rc==VIEW_MORE) { CHECK(++calls<10000); rc=view_continue(&f.v,&c); CHECK(!c.changed && f.v.scanned<=VIEW_SCAN_BOUND); }
    CHECK(rc==VIEW_OK && f.v.state.selection.cursor==expected && f.v.state.selection.anchor==cluster);
    CHECK(f.v.state.selection.preferred_col==1 && f.v.state.wrap);
    /* Column discovery at a trailing endpoint must retain its origin row even
     * when the first cluster spans multiple view continuations. */
    CHECK(l.wrap_rows[0].end==cluster+4 && l.wrap_rows[1].end==cluster+9);
    f.v.state.selection=(view_selection){cluster+4,cluster,VIEW_PREFERRED_UNSET}; f.v.state.visual_end=true;
    view_state before=f.v.state;
    rc=view_command(&f.v,VIEW_DOWN,true,NULL,0,&c); CHECK(rc==VIEW_MORE && !c.changed);
    view_cancel(&f.v);
    CHECK(f.v.state.selection.cursor==before.selection.cursor && f.v.state.selection.anchor==before.selection.anchor);
    CHECK(f.v.state.selection.preferred_col==VIEW_PREFERRED_UNSET && f.v.state.visual_end && !view_busy(&f.v));
    rc=view_command(&f.v,VIEW_DOWN,true,NULL,0,&c); CHECK(rc==VIEW_MORE && !c.changed);
    calls=0; while(rc==VIEW_MORE) { CHECK(++calls<10000); rc=view_continue(&f.v,&c); CHECK(!c.changed && f.v.scanned<=VIEW_SCAN_BOUND); }
    CHECK(rc==VIEW_OK && f.v.state.selection.cursor==cluster+9 && f.v.state.selection.anchor==cluster);
    CHECK(f.v.state.selection.preferred_col==5 && f.v.state.visual_end);
    CHECK(!command(&f,VIEW_UP,true,NULL,0).changed);
    CHECK(f.v.state.selection.cursor==cluster+4 && f.v.state.selection.anchor==cluster);
    CHECK(f.v.state.selection.preferred_col==5 && f.v.state.visual_end);
    f.v.state.selection=(view_selection){cluster+1,cluster,VIEW_PREFERRED_UNSET};
    rc=view_command(&f.v,VIEW_DELETE,false,NULL,0,&c); CHECK(rc==VIEW_MORE && c.changed);
    view_cancel(&f.v);
    CHECK(f.v.state.wrap && f.v.state.visual_byte==0 && !f.v.state.visual_end && f.v.state.first_byte==0);
    CHECK(f.v.state.selection.cursor==0 && f.v.state.selection.anchor==0);
    destroy(&f); free(p); puts("wrap_port: long visual-column continuation, anchored soft-end motion and cancellation passed");
}
int main(int argc, char **argv)
{
    if(argc==2 && strcmp(argv[1],"wrap-port")==0) { wrap_port(); return 0; }
    if(argc==2 && strcmp(argv[1],"wrap-selection")==0) { wrap_selection(); return 0; }
    if(argc==2 && strcmp(argv[1],"proposal-7")==0) { proposal_7(); return 0; }
    if(argc==2 && strcmp(argv[1],"proposal-8")==0) { proposal_8(); return 0; }
    const struct { const char *name; void (*run)(void); } reviews[]={
        {"1",review_1},{"2",review_2},{"3",review_3},{"4",review_4},{"7",proposal_7},{"8",proposal_8},{"9",review_9},{"10",review_10},{"11",review_11},
        {"14",review_14},{"15",review_15}
    };
    for(size_t i=0;i<sizeof reviews/sizeof reviews[0];++i)
        if(argc==1 || strcmp(argv[1],reviews[i].name)==0) reviews[i].run();
    if(argc==1) { scripts(); joins(); long_lines(); scroll_and_selection(); allocation_log(); wrap_selection(); wrap_port(); }
    puts("view_test: all passed"); return 0;
}
