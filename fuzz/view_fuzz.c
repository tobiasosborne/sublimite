#include "view/view.h"
#include "base/base.h"
#include <string.h>

#define CAP 8192u
static void *arena_alloc(void *ctx, size_t n) { return edit_arena_alloc(ctx,n,16); }
static void arena_free(void *ctx, void *p, size_t n) { (void)ctx; (void)p; (void)n; }
static int cls(const uint8_t *p, size_t n, size_t pos)
{
    uint8_t b=p[pos]; utf8_step unit=utf8_decode(p+pos,n-pos);
    if(unit.valid && unit.cp>=128) {
        const uint32_t ws[]={0x85,0xa0,0x1680,0x2000,0x2001,0x2002,0x2003,0x2004,0x2005,0x2006,0x2007,0x2008,0x2009,0x200a,0x2028,0x2029,0x202f,0x205f,0x3000};
        for(size_t i=0;i<sizeof ws/sizeof ws[0];++i) if(unit.cp==ws[i]) return 2;
    }
    if(b=='\n'||b=='\r') return 3;
    if(b==' '||b=='\t'||b=='\v'||b=='\f') return 2;
    const uint8_t separators[]="./\\()\"'-:,.;<>~!@#$%^&*|+=[]{}`~?";
    for(size_t i=0;i<sizeof separators-1;++i) if(b==separators[i]) return 1;
    return 0;
}
static size_t next(const uint8_t *p, size_t n, size_t at)
{ return at==n?n:at+utf8_grapheme_next(p+at,n-at); }
static size_t prev(const uint8_t *p, size_t at)
{ return utf8_grapheme_prev(p,at); }
static bool boundary(const uint8_t *p, size_t n, uint64_t at)
{
    if(at>n) return false;
    size_t pos=0; while(pos<(size_t)at) pos=next(p,n,pos); return pos==at;
}
static size_t word_right(const uint8_t *p, size_t n, size_t at)
{
    if(at==n) return n;
    if(cls(p,n,at)==3) return next(p,n,at);
    while(at<n && cls(p,n,at)==2) at=next(p,n,at);
    if(at==n || cls(p,n,at)==3) return at;
    int c=cls(p,n,at); while(at<n && cls(p,n,at)==c) at=next(p,n,at);
    return at;
}
static size_t word_left(const uint8_t *p, size_t n, size_t at)
{
    if(at==0) return 0;
    size_t q=prev(p,at);
    if(cls(p,n,q)==3) return q;
    while(at>0 && cls(p,n,prev(p,at))==2) at=prev(p,at);
    if(at==0 || cls(p,n,prev(p,at))==3) return at;
    int c=cls(p,n,prev(p,at));
    while(at>0 && cls(p,n,prev(p,at))==c) at=prev(p,at);
    return at;
}
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if(size<2) return 0;
    size_t initial=(size_t)data[0]*4u; if(initial>(size-1)/2u) initial=(size-1)/2u;
    uint8_t model[CAP], got[CAP]; memcpy(model,data+1,initial); size_t n=initial;
    edit_arena arena; EDIT_ASSERT(edit_arena_init(&arena,16u*1024u*1024u)==0);
    piece_allocator a={&arena,arena_alloc,arena_free}; piece_tree *tree=piece_create(&a); EDIT_ASSERT(tree);
    EDIT_ASSERT(piece_init_copy(tree,model,n)==0);
    view v; view_config cfg={1u+(uint32_t)(data[1]%8u),1u+(uint32_t)(data[0]%20u),
        1u+(uint32_t)(data[1]%40u),NULL,NULL}; view_init(&v,tree,&cfg);
    size_t pos=1+initial, steps=0;
    while(pos<size && steps++<256) {
        uint8_t code=data[pos++]; view_key key=(view_key)(code%20u); bool shift=(code&128u)!=0;
        size_t cursor=(size_t)v.state.selection.cursor, anchor=(size_t)v.state.selection.anchor;
        size_t lo=cursor<anchor?cursor:anchor, hi=cursor>anchor?cursor:anchor;
        const uint8_t *typed=data+pos; size_t count=0;
        if(key==VIEW_TYPE && pos<size) { count=(size_t)(data[pos++]%16u); if(count>size-pos) count=size-pos; typed=data+pos; pos+=count; }
        bool edit=key>=VIEW_BACKSPACE;
        if(edit && lo==hi && key!=VIEW_TYPE) {
            if(key==VIEW_BACKSPACE) lo=prev(model,cursor);
            if(key==VIEW_DELETE) hi=next(model,n,cursor);
            if(key==VIEW_WORD_BACKSPACE) lo=word_left(model,n,cursor);
            if(key==VIEW_WORD_DELETE) hi=word_right(model,n,cursor);
        }
        if(edit) {
            EDIT_ASSERT(n-(hi-lo)+count<CAP);
            memmove(model+lo+count,model+hi,n-hi); if(count) memcpy(model+lo,typed,count);
            n=n-(hi-lo)+count;
        }
        view_change change; int rc=view_command(&v,key,shift,typed,count,&change);
        uint64_t expected_old=(uint64_t)(hi-lo);
        if(edit && (count || expected_old)) {
            if(!(change.changed && change.offset==lo && change.old_len==expected_old && change.new_len==count))
                fprintf(stderr,"view_fuzz edit key=%d cursor=%zu anchor=%zu expected=%zu/%llu/%zu got=%llu/%llu/%llu rc=%d\n",
                    (int)key,cursor,anchor,lo,(unsigned long long)expected_old,count,
                    (unsigned long long)change.offset,(unsigned long long)change.old_len,
                    (unsigned long long)change.new_len,rc);
            EDIT_ASSERT(change.changed && change.offset==lo && change.old_len==expected_old && change.new_len==count);
        } else EDIT_ASSERT(!change.changed);
        unsigned resumes=0;
        for(;;) {
            EDIT_ASSERT(v.scanned<=VIEW_SCAN_BOUND);
            EDIT_ASSERT(piece_len(tree)==n && piece_read(tree,0,got,n)==0 && memcmp(got,model,n)==0);
            EDIT_ASSERT(boundary(model,n,v.state.selection.cursor));
            EDIT_ASSERT(boundary(model,n,v.state.selection.anchor));
            if(rc!=VIEW_MORE) break;
            EDIT_ASSERT(++resumes<1000); rc=view_continue(&v,&change); EDIT_ASSERT(!change.changed);
        }
        EDIT_ASSERT(rc==VIEW_OK);
    }
    piece_destroy(tree); edit_arena_free(&arena); return 0;
}
