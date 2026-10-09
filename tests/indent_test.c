#include "indent/indent.h"
#include "base/base.h"
#include <string.h>
#include <stdio.h>
#include <sys/mman.h>

#define CHECK(x) EDIT_ASSERT(x)
static piece_tree *tree_bytes(const uint8_t *p, size_t n, bool fragmented)
{
    piece_allocator a=piece_default_allocator();
    piece_tree *t=piece_create(&a); CHECK(t);
    if (fragmented) {
        for (size_t i=n;i>0;--i) CHECK(piece_insert(t,0,p+i-1,1)==PIECE_OK);
    } else CHECK(piece_init_copy(t,p,n)==PIECE_OK);
    return t;
}
static piece_tree *tree_text(const char *p) { return tree_bytes((const uint8_t *)p,strlen(p),false); }
static void enter_case(const char *text, uint64_t cursor, const char *want)
{
    for (unsigned fragment=0;fragment<2;++fragment) {
        piece_tree *t=tree_bytes((const uint8_t *)text,strlen(text),fragment!=0);
        uint8_t out[128]; size_t n=99;
        CHECK(indent_on_enter(t,cursor,out,sizeof out,&n)==INDENT_OK);
        CHECK(n==strlen(want) && memcmp(out,want,n)==0);
        memset(out,0xa5,sizeof out);
        CHECK(indent_on_enter(t,cursor,out,n-1,&n)==INDENT_ERR_CAPACITY);
        CHECK(n==strlen(want) && out[0]==0xa5);
        piece_destroy(t);
    }
}
static void test_enter(void)
{
    enter_case("",0,"\n"); enter_case("  a",3,"\n  ");
    enter_case("\t  a\n",4,"\n\t  "); enter_case("\t  a\n",5,"\n");
    enter_case("    abc",2,"\n  "); enter_case("  {",3,"\n  ");
    enter_case("  x\r\n\ty",7,"\r\n\t"); enter_case("  x\r\n",3,"\r\n  ");
    enter_case("  x\r\n",5,"\r\n"); enter_case("\tfoo\r\n",2,"\r\n\t");
    piece_tree *t=tree_text("a\r\n"); uint8_t out[8]; size_t n;
    CHECK(indent_on_enter(t,2,out,sizeof out,&n)==INDENT_ERR_RANGE && n==0);
    CHECK(indent_on_enter(t,4,out,sizeof out,&n)==INDENT_ERR_RANGE);
    CHECK(indent_on_enter(t,0,NULL,1,&n)==INDENT_ERR_ARGUMENT);
    CHECK(indent_on_enter(NULL,0,out,sizeof out,&n)==INDENT_ERR_ARGUMENT);
    piece_destroy(t);
}
static void close_case(const char *text, uint64_t cursor, uint8_t width,
                       uint64_t lo, const char *replacement, const char *after)
{
    piece_tree *t=tree_text(text); indent_edit e;
    CHECK(indent_on_close_brace(t,cursor,(indent_style){false,width},&e)==INDENT_OK);
    CHECK(e.lo==lo && e.hi==cursor && e.length==strlen(replacement));
    CHECK(memcmp(e.bytes,replacement,e.length)==0);
    CHECK(piece_delete(t,e.lo,e.hi-e.lo,NULL)==PIECE_OK);
    CHECK(piece_insert(t,e.lo,e.bytes,e.length)==PIECE_OK);
    uint8_t out[128]; CHECK(piece_len(t)==strlen(after));
    CHECK(piece_read(t,0,out,strlen(after))==PIECE_OK);
    CHECK(memcmp(out,after,strlen(after))==0); piece_destroy(t);
}
static void test_close(void)
{
    close_case("",0,4,0,"}","}"); close_case("        ",8,4,4,"}","    }");
    close_case("      \r\n",6,4,4,"}","    }\r\n");
    close_case("\t\t",2,4,1,"}","\t}");
    close_case(" \t  ",4,4,2,"}"," \t}");
    close_case(" \t\t",3,4,2,"}"," \t}");
    close_case("  \t ",4,4,3,"}","  \t}");
    close_case("\t  ",1,4,0,"}","}  ");
    close_case("  x",2,4,2,"}","  }x");
    close_case("\n   ",4,2,3,"}","\n  }");
    /* Tab columns use the explicit caller width. */
    close_case("  \t",3,3,0,"}","}");
    piece_tree *t=tree_text("\r\n"); indent_edit e;
    CHECK(indent_on_close_brace(t,1,(indent_style){true,4},&e)==INDENT_ERR_RANGE);
    CHECK(indent_on_close_brace(t,0,(indent_style){false,0},&e)==INDENT_ERR_ARGUMENT);
    CHECK(indent_on_close_brace(t,3,(indent_style){false,4},&e)==INDENT_ERR_RANGE);
    piece_destroy(t);
}
static void match_case(piece_tree *t, uint64_t cursor, uint64_t lo, uint64_t hi, uint64_t want)
{
    uint64_t m=0; CHECK(indent_bracket_match(t,cursor,lo,hi,&m)==INDENT_OK && m==want);
}
static void test_brackets(void)
{
    const uint8_t text[]={0xc3,0xa9,'{','(','[',']',')','}','x'};
    for (unsigned f=0;f<2;++f) {
        piece_tree *t=tree_bytes(text,sizeof text,f!=0);
        match_case(t,2,2,8,7); match_case(t,8,2,8,2);
        match_case(t,3,2,8,6); match_case(t,6,2,8,3);
        match_case(t,4,4,6,5); match_case(t,6,4,6,4);
        match_case(t,2,2,7,INDENT_NONE); match_case(t,7,3,8,INDENT_NONE);
        match_case(t,0,0,9,INDENT_NONE); match_case(t,9,0,9,INDENT_NONE);
        match_case(t,2,2,2,INDENT_NONE);
        uint64_t m; CHECK(indent_bracket_match(t,10,0,9,&m)==INDENT_ERR_RANGE);
        CHECK(indent_bracket_match(t,2,8,2,&m)==INDENT_ERR_RANGE); piece_destroy(t);
    }
    piece_tree *t=tree_text("\"{\"}\r\n"); match_case(t,1,0,piece_len(t),3); piece_destroy(t);
    t=tree_text(""); match_case(t,0,0,0,INDENT_NONE); piece_destroy(t);
}
static void test_whitespace(void)
{
    const char *text="x \t\r\n  \nq\r \nx  ";
    for (unsigned f=0;f<2;++f) {
        piece_tree *t=tree_bytes((const uint8_t *)text,strlen(text),f!=0);
        indent_range r[8]; size_t n;
        CHECK(indent_trailing_ws_ranges(t,0,piece_len(t),r,8,&n)==INDENT_OK && n==4);
        CHECK(r[0].lo==1 && r[0].hi==3); CHECK(r[1].lo==5 && r[1].hi==7);
        CHECK(r[2].lo==10 && r[2].hi==11); CHECK(r[3].lo==13 && r[3].hi==15);
        CHECK(indent_trailing_ws_ranges(t,2,5,r,8,&n)==INDENT_OK && n==1 && r[0].lo==2 && r[0].hi==3);
        CHECK(indent_trailing_ws_ranges(t,0,4,r,8,&n)==INDENT_OK && n==0);
        CHECK(indent_trailing_ws_ranges(t,0,piece_len(t),r,1,&n)==INDENT_ERR_CAPACITY && n==4);
        CHECK(indent_trailing_ws_ranges(t,0,piece_len(t),NULL,0,&n)==INDENT_ERR_CAPACITY && n==4);
        CHECK(indent_trailing_ws_ranges(t,8,3,r,8,&n)==INDENT_ERR_RANGE); piece_destroy(t);
    }
    piece_tree *t=tree_text(" \r"); indent_range r; size_t n;
    CHECK(indent_trailing_ws_ranges(t,0,2,&r,1,&n)==INDENT_OK && n==0); piece_destroy(t);
    t=tree_text(""); CHECK(indent_trailing_ws_ranges(t,0,0,&r,1,&n)==INDENT_OK && n==0); piece_destroy(t);
}
static void detect_case(const char *text, bool tabs, uint8_t width)
{
    indent_style style; size_t len=strlen(text);
    CHECK(indent_detect_bytes((const uint8_t *)text,len,&style)==INDENT_OK);
    CHECK(style.uses_tabs==tabs && style.width==width);
    piece_tree *t=tree_bytes((const uint8_t *)text,len,true);
    piece_snapshot *s=piece_snapshot_take(t); CHECK(s); piece_destroy(t);
    CHECK(indent_detect(s,&style)==INDENT_OK && style.uses_tabs==tabs && style.width==width);
    piece_snapshot_release(s);
}
static void test_detect(void)
{
    detect_case("",false,4); detect_case("x\n \t \r\n",false,4);
    detect_case("  a\r\n    b\r\n",false,2); detect_case("    a\n        b",false,4);
    detect_case("\ta\n\t b\n  c",true,2); detect_case("\ta\n  b",false,2);
    detect_case("   a\n      b",false,3); detect_case(" a",false,1);
    /* Inaccessible suffix proves bounded reads, including the final cut line. */
    size_t n=INDENT_PREFIX_BYTES; uint8_t *p=mmap(NULL,n+4096,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    CHECK(p!=MAP_FAILED); memset(p,'x',n); memcpy(p,"  a\n    b\n",10);
    CHECK(mprotect(p+n,4096,PROT_NONE)==0);
    indent_style style; CHECK(indent_detect_bytes(p,n+4096,&style)==INDENT_OK && style.width==2);
    piece_allocator a=piece_default_allocator(); piece_tree *t=piece_create(&a); CHECK(t);
    CHECK(piece_init_mapped(t,p,n+4096,NULL)==PIECE_OK); piece_snapshot *s=piece_snapshot_take(t); CHECK(s);
    CHECK(indent_detect(s,&style)==INDENT_OK && style.width==2);
    piece_snapshot_release(s); piece_destroy(t); CHECK(munmap(p,n+4096)==0);
}
static void test_long_lines_and_window_bound(void)
{
    uint8_t text[4096], output[4096]; memset(text,' ',sizeof text);
    text[0]='\n'; text[2049]='{'; text[4095]='}';
    piece_tree *t=tree_bytes(text,sizeof text,false); size_t n;
    CHECK(indent_on_enter(t,2050,output,sizeof output,&n)==INDENT_OK && n==2049);
    CHECK(output[0]=='\n' && output[2048]==' ');
    match_case(t,4096,2049,4096,2049);
    match_case(t,2049,2049,4095,INDENT_NONE);
    match_case(t,4096,2050,4096,INDENT_NONE);
    piece_destroy(t);
    /* Only the middle page can be read; viewport routines must not inspect
     * either inaccessible adjacent line/file region. */
    size_t page=4096;
    uint8_t *p=mmap(NULL,page*3,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    CHECK(p!=MAP_FAILED); memset(p+page,'x',page);
    memcpy(p+page,"{ \t\r\n}",6);
    CHECK(mprotect(p,page,PROT_NONE)==0 && mprotect(p+page*2,page,PROT_NONE)==0);
    piece_allocator a=piece_default_allocator(); t=piece_create(&a); CHECK(t);
    CHECK(piece_init_mapped(t,p,page*3,NULL)==PIECE_OK);
    match_case(t,page,page,page*2,page+5);
    match_case(t,page+6,page,page*2,page);
    indent_range ranges[2];
    CHECK(indent_trailing_ws_ranges(t,page,page*2,ranges,2,&n)==INDENT_OK && n==1);
    CHECK(ranges[0].lo==page+1 && ranges[0].hi==page+3);
    piece_destroy(t); CHECK(munmap(p,page*3)==0);
}
static void test_no_malloc(void)
{
    piece_tree *t=tree_text("    {\n        \n    }\n");
    uint8_t out[64]; size_t n; uint64_t match; indent_edit e;
    edit_malloc_guard_begin();
    for (unsigned i=0;i<10000;++i) {
        CHECK(indent_on_enter(t,5,out,sizeof out,&n)==INDENT_OK);
        CHECK(indent_on_close_brace(t,14,(indent_style){false,4},&e)==INDENT_OK);
        CHECK(indent_bracket_match(t,4,0,piece_len(t),&match)==INDENT_OK && match==19);
    }
    size_t allocations=edit_malloc_guard_end(); CHECK(allocations==0);
    printf("indent typing malloc guard: %s, allocations=%zu\n",edit_malloc_guard_active()?"active":"ASan inactive",allocations);
    piece_destroy(t);
}
int main(void)
{
    test_enter(); test_close(); test_brackets(); test_whitespace(); test_detect(); test_long_lines_and_window_bound(); test_no_malloc();
    puts("indent_test: all cases passed"); return 0;
}
