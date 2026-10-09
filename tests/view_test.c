#include "view/view.h"
#include "base/base.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

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
        CHECK(f->v.scanned<=VIEW_SCAN_BOUND); if(c.changed) { CHECK(!all.changed); all=c; }
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
    CHECK(f.v.state.approximate && f.v.scanned<=VIEW_SCAN_BOUND);
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
    CHECK(f.v.scanned<=VIEW_SCAN_BOUND && f.v.state.approximate);
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
int main(void) { scripts(); joins(); long_lines(); scroll_and_selection(); allocation_log(); puts("view_test: all passed"); return 0; }
