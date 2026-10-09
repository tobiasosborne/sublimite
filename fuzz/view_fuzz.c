#include "view/view.h"
#include "base/base.h"
#include <string.h>
#include <stdlib.h>

#define CAP 300000u
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
static size_t prev(const uint8_t *stops, size_t at)
{
    if(at) { --at; while(at && !stops[at]) --at; }
    return at;
}
static void boundaries(const uint8_t *p, size_t n, uint8_t *stops)
{
    memset(stops,0,n+1); stops[0]=1;
    for(size_t pos=0;pos<n;) { pos=next(p,n,pos); stops[pos]=1; }
}
static bool motion_matches(const view *v, const view_state *expected, const uint8_t *p, size_t n)
{
    (void)p;
    return v->state.selection.cursor<=n && v->state.selection.anchor<=n &&
        v->state.selection.cursor==expected->selection.cursor && v->state.selection.anchor==expected->selection.anchor &&
        v->state.selection.preferred_col==expected->selection.preferred_col &&
        v->state.first_line==expected->first_line && v->state.first_byte==expected->first_byte &&
        v->state.hscroll==expected->hscroll && v->state.approximate==expected->approximate;
}
int LLVMFuzzerInitialize(int *argc, char ***argv)
{
    for(int i=1;i<*argc;++i) if(strcmp((*argv)[i],"--oracle-self-check")==0) {
        const uint8_t p[]="a\n"; view v={0}; v.state.selection=(view_selection){1,1,VIEW_PREFERRED_UNSET};
        view_state expected={{2,2,VIEW_PREFERRED_UNSET},1,2,0,false,false,false,0};
        if(motion_matches(&v,&expected,p,2)) { puts("review_13 RED: wrong movement accepted by oracle"); exit(1); }
        puts("review_13 GREEN: wrong movement rejected by independent oracle"); exit(0);
    }
    return 0;
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
static size_t word_left(const uint8_t *p, size_t n, size_t at, const uint8_t *stops)
{
    if(at==0) return 0;
    size_t q=prev(stops,at);
    if(cls(p,n,q)==3) return q;
    while(at>0 && cls(p,n,prev(stops,at))==2) at=prev(stops,at);
    if(at==0 || cls(p,n,prev(stops,at))==3) return at;
    int c=cls(p,n,prev(stops,at));
    while(at>0 && cls(p,n,prev(stops,at))==c) at=prev(stops,at);
    return at;
}
static size_t line_of(const uint8_t *p, size_t at)
{ size_t line=0; for(size_t i=0;i<at;++i) if(p[i]=='\n') ++line; return line; }
static size_t line_start(const uint8_t *p, size_t n, size_t line)
{
    if(!line) return 0;
    for(size_t i=0;i<n;++i) if(p[i]=='\n' && --line==0) return i+1;
    return n;
}
static size_t content_end(const uint8_t *p, size_t n, size_t line)
{
    size_t start=line_start(p,n,line), end=line_start(p,n,line+1);
    if(end>start && p[end-1]=='\n') { --end; if(end>start && p[end-1]=='\r') --end; }
    return end;
}
static uint64_t cells_at(const uint8_t *p, size_t n, size_t pos, uint64_t col, uint32_t tab, size_t *len)
{
    int width=0; *len=utf8_cluster(p+pos,n-pos,&width);
    return p[pos]=='\t'?tab-col%tab:(uint64_t)width;
}
static uint64_t byte_col(const uint8_t *p, size_t n, size_t at, uint32_t tab)
{
    size_t pos=line_start(p,n,line_of(p,at)); uint64_t col=0;
    while(pos<at) { size_t len; col+=cells_at(p,n,pos,col,tab,&len); pos+=len; }
    return col;
}
static size_t col_byte(const uint8_t *p, size_t n, size_t line, uint64_t target, uint32_t tab)
{
    size_t pos=line_start(p,n,line), end=content_end(p,n,line); uint64_t col=0;
    while(pos<end && col<target) {
        size_t len; uint64_t width=cells_at(p,n,pos,col,tab,&len);
        if(width>target-col) break;
        pos+=len; col+=width;
    }
    return pos;
}
static void model_follow(view_state *s, const uint8_t *p, size_t n, const view_config *cfg)
{
    uint64_t line=line_of(p,(size_t)s->selection.cursor);
    if(line<s->first_line) s->first_line=line;
    else if(line-s->first_line>=cfg->rows) s->first_line=line-cfg->rows+1;
    s->first_byte=line_start(p,n,(size_t)s->first_line);
    uint64_t col=byte_col(p,n,(size_t)s->selection.cursor,cfg->tab_width);
    if(col<s->hscroll) s->hscroll=col;
    else if(col-s->hscroll>=cfg->cols) s->hscroll=col-cfg->cols+1;
    s->approximate=false;
}
static bool model_noop(view_state s, view_key key, size_t n, size_t count)
{
    if(s.selection.cursor!=s.selection.anchor) return false;
    size_t at=(size_t)s.selection.cursor;
    return (key==VIEW_TYPE && !count) ||
        (at==0 && (key==VIEW_LEFT || key==VIEW_BACKSPACE || key==VIEW_WORD_LEFT || key==VIEW_WORD_BACKSPACE)) ||
        (at==n && (key==VIEW_RIGHT || key==VIEW_DELETE || key==VIEW_WORD_RIGHT || key==VIEW_WORD_DELETE));
}
static void model_move(view_state *s, view_key key, bool shift, const uint8_t *p, size_t n,
                       const view_config *cfg, const uint8_t *stops)
{
    size_t cursor=(size_t)s->selection.cursor, anchor=(size_t)s->selection.anchor, target=cursor;
    size_t lo=cursor<anchor?cursor:anchor, hi=cursor>anchor?cursor:anchor;
    size_t line=line_of(p,cursor), start=line_start(p,n,line), end=content_end(p,n,line);
    if(key>=VIEW_UP && key<=VIEW_PAGE_DOWN) {
        if(s->selection.preferred_col==VIEW_PREFERRED_UNSET) s->selection.preferred_col=byte_col(p,n,cursor,cfg->tab_width);
        size_t lines=line_of(p,n)+1, delta=(key==VIEW_PAGE_UP || key==VIEW_PAGE_DOWN)?cfg->rows:1;
        bool up=key==VIEW_UP || key==VIEW_PAGE_UP;
        size_t dest=up?(line>delta?line-delta:0):(line+delta<lines?line+delta:lines-1);
        if(key==VIEW_PAGE_UP || key==VIEW_PAGE_DOWN) {
            if(up) s->first_line=s->first_line>delta?s->first_line-delta:0;
            else s->first_line=s->first_line+delta<lines?s->first_line+delta:lines-1;
        }
        if(up && !line) target=0;
        else if(!up && line==lines-1) target=n;
        else target=col_byte(p,n,dest,s->selection.preferred_col,cfg->tab_width);
    } else {
        s->selection.preferred_col=VIEW_PREFERRED_UNSET;
        switch(key) {
        case VIEW_LEFT: target=cursor!=anchor && !shift?lo:prev(stops,cursor); break;
        case VIEW_RIGHT: target=cursor!=anchor && !shift?hi:next(p,n,cursor); break;
        case VIEW_WORD_LEFT: target=word_left(p,n,cursor,stops); break;
        case VIEW_WORD_RIGHT: target=word_right(p,n,cursor); break;
        case VIEW_HOME:
            target=start; while(target<end && cls(p,n,target)==2) target=next(p,n,target);
            if(target==cursor) target=start;
            break;
        case VIEW_END: target=end; break;
        case VIEW_DOC_HOME: target=0; break;
        case VIEW_DOC_END: target=n; break;
        case VIEW_SELECT_ALL: s->selection.anchor=0; s->selection.cursor=n; model_follow(s,p,n,cfg); return;
        case VIEW_SELECT_LINE: {
            size_t a=line_of(p,lo), b=line_of(p,hi);
            if(hi>lo && hi==line_start(p,n,b)) --b;
            size_t left=line_start(p,n,a), right=line_start(p,n,b+1);
            if(hi>lo && lo==left && hi==right) right=line_start(p,n,b+2);
            s->selection.anchor=left; s->selection.cursor=right; model_follow(s,p,n,cfg); return;
        }
        case VIEW_SELECT_WORD:
            if(cursor==anchor && cursor<end) {
                size_t begin=start, q=start; int kind=-1;
                while(q<=cursor) { int c=cls(p,n,q); if(c!=kind) { begin=q; kind=c; } q=next(p,n,q); }
                while(q<end && cls(p,n,q)==kind) q=next(p,n,q);
                s->selection.anchor=begin; s->selection.cursor=q;
            }
            model_follow(s,p,n,cfg); return;
        default: EDIT_ASSERT(false);
        }
    }
    s->selection.cursor=target; if(!shift) s->selection.anchor=target;
    model_follow(s,p,n,cfg);
}
static int structured_checkpoint(void *ctx, const piece_tree *tree, uint64_t line,
    uint64_t byte_target, uint64_t col_target, uint64_t *byte, uint64_t *col)
{
    (void)tree; uint64_t end=*(size_t *)ctx-2;
    uint64_t target=byte_target==UINT64_MAX?col_target:byte_target;
    if(line==0) { if(target>end) target=end; *byte=target; *col=target; }
    else { *byte=end+1; *col=0; }
    return 1;
}
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if(size<2) return 0;
    bool structured=data[0]==255;
    size_t initial=(size_t)data[0]*4u; if(initial>(size-1)/2u) initial=(size-1)/2u;
    uint8_t model[CAP], got[CAP], stops[CAP+1u]; size_t n=initial;
    if(structured) {
        n=131075; memset(model,'x',n); model[0]='a';
        if(!(data[1]&1u)) for(size_t i=1;i<n-2;i+=2) { model[i]=0xcc; model[i+1]=0x81; }
        model[n-2]='\n'; model[n-1]='b';
    } else memcpy(model,data+1,initial);
    boundaries(model,n,stops);
    edit_arena arena; EDIT_ASSERT(edit_arena_init(&arena,16u*1024u*1024u)==0);
    piece_allocator a={&arena,arena_alloc,arena_free}; piece_tree *tree=piece_create(&a); EDIT_ASSERT(tree);
    EDIT_ASSERT(piece_init_copy(tree,model,n)==0);
    view v; view_config cfg={1u+(uint32_t)(data[1]%8u),1u+(uint32_t)(data[0]%20u),
        1u+(uint32_t)(data[1]%40u),NULL,NULL}; view_init(&v,tree,&cfg);
    size_t checkpoint_len=n;
    if(structured && (data[1]&1u)) { v.config.checkpoint=structured_checkpoint; v.config.checkpoint_ctx=&checkpoint_len; }
    view_state expected={{0,0,VIEW_PREFERRED_UNSET},0,0,0,false,false,false,0};
    size_t pos=structured?2:1+initial, steps=0;
    while(pos<size && steps++<(structured?32u:256u)) {
        uint8_t code=data[pos++]; view_key key=(view_key)(code%20u); bool shift=(code&128u)!=0;
        view_state before=expected;
        size_t cursor=(size_t)expected.selection.cursor, anchor=(size_t)expected.selection.anchor;
        size_t lo=cursor<anchor?cursor:anchor, hi=cursor>anchor?cursor:anchor;
        const uint8_t *typed=data+pos; size_t count=0;
        if(key==VIEW_TYPE && pos<size) { count=(size_t)(data[pos++]%16u); if(count>size-pos) count=size-pos; typed=data+pos; pos+=count; }
        bool edit=key>=VIEW_BACKSPACE, noop=model_noop(expected,key,n,count);
        if(edit && lo==hi && key!=VIEW_TYPE) {
            if(key==VIEW_BACKSPACE) lo=prev(stops,cursor);
            if(key==VIEW_DELETE) hi=next(model,n,cursor);
            if(key==VIEW_WORD_BACKSPACE) lo=word_left(model,n,cursor,stops);
            if(key==VIEW_WORD_DELETE) hi=word_right(model,n,cursor);
        }
        if(!edit && !noop) model_move(&expected,key,shift,model,n,&cfg,stops);
        view_change change; int rc=view_command(&v,key,shift,typed,count,&change);
        uint64_t expected_old=edit?(uint64_t)(hi-lo):0;
        bool expected_edit=edit && (count || expected_old), committed=false, cancelled=false;
        uint64_t removed=0; size_t inserted=0;
        unsigned resumes=0;
        for(;;) {
            if(change.changed) {
                EDIT_ASSERT(expected_edit && change.old_len<=expected_old-removed);
                EDIT_ASSERT(change.offset==lo+inserted && change.new_len==(committed?0u:count));
                size_t off=(size_t)change.offset, old=(size_t)change.old_len, added=(size_t)change.new_len;
                EDIT_ASSERT(off<=n && old<=n-off && n-old+added<CAP);
                memmove(model+off+added,model+off+old,n-off-old);
                if(added) memcpy(model+off,typed,added);
                n=n-old+added; removed+=old; inserted+=added;
                boundaries(model,n,stops); committed=true; v.config.checkpoint=NULL;
                size_t repaired=0; while(repaired<lo+count) repaired=next(model,n,repaired);
                expected=(view_state){{repaired,repaired,VIEW_PREFERRED_UNSET},0,0,0,false,false,false,0};
                model_follow(&expected,model,n,&cfg);
            }
            EDIT_ASSERT(v.scanned<=VIEW_SCAN_BOUND);
            EDIT_ASSERT(piece_len(tree)==n && piece_read(tree,0,got,n)==0 && memcmp(got,model,n)==0);
            EDIT_ASSERT(v.state.selection.cursor<=n && stops[v.state.selection.cursor]);
            EDIT_ASSERT(v.state.selection.anchor<=n && stops[v.state.selection.anchor]);
            if(rc!=VIEW_MORE) break;
            if(structured && (code&64u) && resumes==0) {
                view_cancel(&v);
                expected=committed?(view_state){{0,0,VIEW_PREFERRED_UNSET},0,0,0,false,false,false,0}:before;
                cancelled=true; rc=VIEW_OK; break;
            }
            EDIT_ASSERT(++resumes<10000); rc=view_continue(&v,&change);
        }
        EDIT_ASSERT(rc==VIEW_OK);
        EDIT_ASSERT(cancelled || (committed==expected_edit && removed==expected_old && inserted==count));
        if(!motion_matches(&v,&expected,model,n)) {
            fprintf(stderr,"view_fuzz motion key=%d expected=%llu/%llu/%llu viewport=%llu/%llu/%llu actual=%llu/%llu/%llu viewport=%llu/%llu/%llu\n",
                (int)key,(unsigned long long)expected.selection.cursor,(unsigned long long)expected.selection.anchor,
                (unsigned long long)expected.selection.preferred_col,(unsigned long long)expected.first_line,
                (unsigned long long)expected.first_byte,(unsigned long long)expected.hscroll,
                (unsigned long long)v.state.selection.cursor,(unsigned long long)v.state.selection.anchor,
                (unsigned long long)v.state.selection.preferred_col,(unsigned long long)v.state.first_line,
                (unsigned long long)v.state.first_byte,(unsigned long long)v.state.hscroll);
        }
        EDIT_ASSERT(motion_matches(&v,&expected,model,n));
    }
    piece_destroy(tree); edit_arena_free(&arena); return 0;
}
