#include "tabs.h"
#include "utf8/utf8.h"
#include <string.h>
#include <unistd.h>

static bool ready(const tabs_set *s) { return s && s->arena.base; }
static bool names_valid(const char *title, size_t tn, const char *path, size_t pn)
{
    return tn<TABS_TITLE_BYTES && pn<TABS_PATH_BYTES && (title || tn==0) && (path || pn==0)
        && (!tn || !memchr(title,0,tn)) && (!pn || !memchr(path,0,pn));
}
static void names_copy(tabs_tab *t, const char *title, size_t tn, const char *path, size_t pn)
{
    if(tn) memmove(t->title,title,tn);
    t->title[tn]=0; t->title_len=tn;
    memset(t->title_widths,0,sizeof t->title_widths);
    t->title_clusters=0; t->title_cells=0;
    for(size_t pos=0;pos<tn;) {
        const uint8_t *text=(const uint8_t *)t->title+pos;
        int width; size_t len=utf8_cluster(text,tn-pos,&width);
        utf8_step unit=utf8_decode(text,tn-pos);
        bool replace=!unit.valid || unit.cp<32 || (unit.cp>=127 && unit.cp<160);
        uint8_t flags=replace?3u:(uint8_t)width;
        size_t cluster=t->title_clusters++;
        t->title_spans[cluster]=(uint8_t)len;
        t->title_widths[cluster/4u]|=(uint8_t)(flags<<((cluster%4u)*2u));
        t->title_cells=(uint16_t)(t->title_cells+(replace?1u:(unsigned)width));
        pos+=len;
    }
    if(pn) memmove(t->path,path,pn);
    t->path[pn]=0; t->path_len=pn;
}
int tabs_init(tabs_set *s, size_t capacity, size_t closed_capacity)
{
    if(!s || !capacity || capacity>=TABS_NONE || closed_capacity>=TABS_NONE ||
       closed_capacity>(size_t)TABS_NONE-1-capacity) return TABS_ERR_ARG;
    size_t total=capacity+closed_capacity;
    /* tabs_tab is aligned more strictly than uint32_t; all arrays follow it. */
    size_t per=sizeof(tabs_tab)+2*sizeof(uint32_t);
    if(total>SIZE_MAX/per) return TABS_ERR_CAPACITY;
    size_t bytes=total*per;
    long page=sysconf(_SC_PAGESIZE);
    if(page<=0) return TABS_ERR_INIT;
    size_t pg=(size_t)page;
    if(bytes>SIZE_MAX-(pg-1)) return TABS_ERR_CAPACITY;
    bytes=((bytes+pg-1)/pg)*pg;
    tabs_set value={.capacity=capacity,.closed_capacity=closed_capacity,
        .active=TABS_NONE,.mru_head=TABS_NONE,.mru_tail=TABS_NONE,.next_id=1};
    if(edit_arena_init(&value.arena,bytes)!=0) return TABS_ERR_INIT;
    value.slots=edit_arena_alloc(&value.arena,total*sizeof(tabs_tab),_Alignof(tabs_tab));
    value.order=edit_arena_alloc(&value.arena,capacity*sizeof(uint32_t),_Alignof(uint32_t));
    value.closed=closed_capacity?edit_arena_alloc(&value.arena,closed_capacity*sizeof(uint32_t),_Alignof(uint32_t)):NULL;
    value.free_slots=edit_arena_alloc(&value.arena,total*sizeof(uint32_t),_Alignof(uint32_t));
    if(!value.slots || !value.order || !value.free_slots || (closed_capacity && !value.closed)) {
        edit_arena_free(&value.arena); return TABS_ERR_INIT;
    }
    memset(value.slots,0,total*sizeof(tabs_tab));
    for(size_t i=0;i<total;++i) value.free_slots[i]=(uint32_t)(total-1-i);
    value.free_count=total; *s=value; return TABS_OK;
}
void tabs_fini(tabs_set *s)
{ if(s) { edit_arena_free(&s->arena); memset(s,0,sizeof *s); } }
size_t tabs_owned_bytes(const tabs_set *s) { return ready(s)?sizeof *s+s->arena.size:0; }
size_t tabs_count(const tabs_set *s) { return ready(s)?s->count:0; }
size_t tabs_closed_count(const tabs_set *s) { return ready(s)?s->closed_count:0; }
const tabs_tab *tabs_at(const tabs_set *s, size_t index)
{ return ready(s) && index<s->count?&s->slots[s->order[index]]:NULL; }
const tabs_tab *tabs_closed_at(const tabs_set *s, size_t depth)
{ return ready(s) && depth<s->closed_count?&s->slots[s->closed[s->closed_count-1-depth]]:NULL; }
const tabs_tab *tabs_active(const tabs_set *s)
{ return ready(s) && s->active!=TABS_NONE?&s->slots[s->active]:NULL; }
size_t tabs_active_index(const tabs_set *s)
{
    if(ready(s)) for(size_t i=0;i<s->count;++i) if(s->order[i]==s->active) return i;
    return SIZE_MAX;
}
static void unlink_mru(tabs_set *s, uint32_t slot)
{
    tabs_tab *t=&s->slots[slot];
    if(t->mru_prev!=TABS_NONE) s->slots[t->mru_prev].mru_next=t->mru_next;
    else s->mru_head=t->mru_next;
    if(t->mru_next!=TABS_NONE) s->slots[t->mru_next].mru_prev=t->mru_prev;
    else s->mru_tail=t->mru_prev;
    t->mru_prev=TABS_NONE; t->mru_next=TABS_NONE;
}
static void prepend_mru(tabs_set *s, uint32_t slot)
{
    tabs_tab *t=&s->slots[slot]; t->mru_prev=TABS_NONE; t->mru_next=s->mru_head;
    if(s->mru_head!=TABS_NONE) s->slots[s->mru_head].mru_prev=slot;
    else s->mru_tail=slot;
    s->mru_head=slot;
}
static void promote(tabs_set *s)
{ if(s->active!=TABS_NONE && s->active!=s->mru_head) { unlink_mru(s,s->active); prepend_mru(s,s->active); } }
void tabs_mru_release(tabs_set *s)
{ if(ready(s) && s->cycling) { promote(s); s->cycling=false; } }
static void save(tabs_set *s, const view_state *live)
{ if(s->active!=TABS_NONE) s->slots[s->active].state=*live; }
static void swap(tabs_set *s, uint32_t slot, view_state *live)
{ save(s,live); s->active=slot; *live=s->slots[slot].state; }
int tabs_open(tabs_set *s, const tabs_desc *d, view_state *live, uint64_t *id)
{
    if(!ready(s) || !d || !live || !id || !d->buffer || !d->undo ||
       !names_valid(d->title,d->title_len,d->path,d->path_len)) return TABS_ERR_ARG;
    if(s->count==s->capacity || s->next_id==UINT64_MAX) return TABS_ERR_CAPACITY;
    tabs_mru_release(s); save(s,live);
    uint32_t slot=s->free_slots[--s->free_count]; tabs_tab *t=&s->slots[slot];
    memset(t,0,sizeof *t); t->buffer=d->buffer; t->undo=d->undo; t->state=d->state;
    t->id=s->next_id++; t->modified=d->modified;
    names_copy(t,d->title,d->title_len,d->path,d->path_len);
    s->order[s->count++]=slot; prepend_mru(s,slot); s->active=slot; *live=t->state; *id=t->id;
    return TABS_OK;
}
int tabs_select(tabs_set *s, size_t index, view_state *live)
{
    if(!ready(s) || !live) return TABS_ERR_ARG;
    if(index>=s->count) return TABS_ERR_RANGE;
    tabs_mru_release(s); swap(s,s->order[index],live); promote(s); return TABS_OK;
}
int tabs_mru_step(tabs_set *s, bool reverse, view_state *live)
{
    if(!ready(s) || !live) return TABS_ERR_ARG;
    if(!s->count) return TABS_ERR_EMPTY;
    uint32_t slot=reverse?s->slots[s->active].mru_prev:s->slots[s->active].mru_next;
    if(slot==TABS_NONE) slot=reverse?s->mru_tail:s->mru_head;
    s->cycling=true; swap(s,slot,live); return TABS_OK;
}
int tabs_close(tabs_set *s, size_t index, view_state *live, tabs_tab *evicted)
{
    if(!ready(s) || !live || !evicted) return TABS_ERR_ARG;
    if(index>=s->count) return TABS_ERR_RANGE;
    tabs_mru_release(s); save(s,live); uint32_t slot=s->order[index];
    tabs_tab *t=&s->slots[slot]; t->closed_index=index; unlink_mru(s,slot);
    memmove(s->order+index,s->order+index+1,(s->count-index-1)*sizeof *s->order); --s->count;
    if(s->active==slot) {
        s->active=s->mru_head;
        if(s->count) *live=s->slots[s->active].state;
        else { memset(live,0,sizeof *live); live->selection.preferred_col=VIEW_PREFERRED_UNSET; }
    }
    memset(evicted,0,sizeof *evicted);
    if(!s->closed_capacity) { *evicted=*t; s->free_slots[s->free_count++]=slot; }
    else {
        if(s->closed_count==s->closed_capacity) {
            uint32_t old=s->closed[0]; *evicted=s->slots[old];
            s->free_slots[s->free_count++]=old;
            memmove(s->closed,s->closed+1,(s->closed_count-1)*sizeof *s->closed); --s->closed_count;
        }
        s->closed[s->closed_count++]=slot;
    }
    return TABS_OK;
}
int tabs_reopen(tabs_set *s, view_state *live, uint64_t *id)
{
    if(!ready(s) || !live || !id) return TABS_ERR_ARG;
    if(!s->closed_count) return TABS_ERR_EMPTY;
    if(s->count==s->capacity) return TABS_ERR_CAPACITY;
    tabs_mru_release(s); save(s,live);
    uint32_t slot=s->closed[--s->closed_count]; tabs_tab *t=&s->slots[slot];
    size_t index=t->closed_index<s->count?t->closed_index:s->count;
    memmove(s->order+index+1,s->order+index,(s->count-index)*sizeof *s->order);
    s->order[index]=slot; ++s->count; prepend_mru(s,slot); s->active=slot; *live=t->state; *id=t->id;
    return TABS_OK;
}
int tabs_reorder(tabs_set *s, size_t from, size_t to)
{
    if(!ready(s)) return TABS_ERR_ARG;
    if(from>=s->count || to>=s->count) return TABS_ERR_RANGE;
    tabs_mru_release(s); uint32_t slot=s->order[from];
    if(from<to) memmove(s->order+from,s->order+from+1,(to-from)*sizeof *s->order);
    else if(from>to) memmove(s->order+to+1,s->order+to,(from-to)*sizeof *s->order);
    s->order[to]=slot; return TABS_OK;
}
int tabs_set_modified(tabs_set *s, size_t index, bool modified)
{
    if(!ready(s)) return TABS_ERR_ARG;
    if(index>=s->count) return TABS_ERR_RANGE;
    s->slots[s->order[index]].modified=modified; return TABS_OK;
}
int tabs_rename(tabs_set *s, size_t index, const char *title, size_t tn, const char *path, size_t pn)
{
    if(!ready(s) || !names_valid(title,tn,path,pn)) return TABS_ERR_ARG;
    if(index>=s->count) return TABS_ERR_RANGE;
    /* Both inputs may refer to the existing record, including crossed names. */
    char title_copy[TABS_TITLE_BYTES], path_copy[TABS_PATH_BYTES];
    if(tn) memcpy(title_copy,title,tn);
    if(pn) memcpy(path_copy,path,pn);
    names_copy(&s->slots[s->order[index]],title_copy,tn,path_copy,pn); return TABS_OK;
}

static render_cell blank(uint32_t fg, uint32_t bg)
{
    render_cell c={.atlas_slot=RENDER_NO_SLOT,.fg=fg,.bg=bg}; return c;
}
static void glyph_cell(render_grid *g, render_cell *cells, const tabs_strip *strip,
                       const uint8_t *p, size_t len, int width, uint32_t fg, uint32_t bg)
{
    render_cell c=blank(fg,bg); uint32_t identity=0, slot=RENDER_NO_SLOT;
    if(strip->glyph && strip->glyph(strip->glyph_ctx,p,len,width,&identity,&slot) &&
       g->glyphs && slot<g->glyph_count && g->glyphs[slot].glyph_index==identity) {
        c.glyph_index=identity; c.atlas_slot=slot;
    }
    if(width==2) { c.attrs=RENDER_ATTR_WIDE_LEFT; cells[1]=blank(fg,bg);
        cells[1].attrs=RENDER_ATTR_WIDE_RIGHT; }
    cells[0]=c;
}
static void strip_tab(render_grid *g, render_cell *cells, uint32_t n,
                      const tabs_strip *strip, const tabs_tab *t, bool active)
{
    uint32_t fg=active?strip->active_fg:strip->fg, bg=active?strip->active_bg:strip->bg;
    for(uint32_t i=0;i<n;++i) cells[i]=blank(fg,bg);
    uint32_t end=n;
    if(n>=2) { --end; glyph_cell(g,cells+end,strip,(const uint8_t *)"|",1,1,fg,bg); }
    if(t->modified && end) { --end; glyph_cell(g,cells+end,strip,(const uint8_t *)"*",1,1,fg,bg); }
    uint32_t at=n>=4?1u:0u;
    if(at>=end) return;
    uint32_t room=end-at;
    bool truncate=t->title_cells>room;
    uint32_t limit=truncate?end-1:end;
    size_t pos=0;
    for(size_t cluster=0;cluster<t->title_clusters && at<limit;++cluster) {
        size_t len=t->title_spans[cluster];
        uint8_t flags=(uint8_t)((t->title_widths[cluster/4u]>>((cluster%4u)*2u))&3u);
        int width=flags==3?1:(int)flags;
        const uint8_t *draw=flags==3?(const uint8_t *)"\xef\xbf\xbd":(const uint8_t *)t->title+pos;
        size_t draw_len=flags==3?3:len;
        pos+=len;
        if(!width) continue;
        if((uint32_t)width>limit-at) break;
        glyph_cell(g,cells+at,strip,draw,draw_len,width,fg,bg); at+=(uint32_t)width;
    }
    if(truncate) glyph_cell(g,cells+limit,strip,(const uint8_t *)"\xe2\x80\xa6",3,1,fg,bg);
}
int tabs_strip_render(const tabs_set *s, render_grid *g, const tabs_strip *strip)
{
    if(!ready(s) || !g || !strip || !strip->tab_cols ||
       ((strip->fg|strip->bg|strip->active_fg|strip->active_bg)&UINT32_C(0xff000000))) return TABS_ERR_ARG;
    if(strip->row>=g->dims.rows || strip->first_col>g->dims.cols ||
       strip->col_count>g->dims.cols-strip->first_col || strip->first_tab>s->count) return TABS_ERR_RANGE;
    /* Validate storage/frame through the frozen API before dereferencing cells.
     * Zero-width fills don't touch cells or damage. */
    int rc=render_mark_rows(g,strip->row,0);
    if(rc!=RENDER_OK) return rc;
    if(!strip->col_count) return TABS_OK;
    render_cell *cells=g->cells+(size_t)strip->row*g->dims.cols+strip->first_col;
    /* A slice cannot leave an old wide half in the untouched neighbouring cell.
     * Row-edge flags need no test: both halves then lie inside the filled range. */
    if((strip->first_col && (cells[0].attrs&RENDER_ATTR_WIDE_RIGHT)) ||
       (strip->first_col+strip->col_count<g->dims.cols &&
        (cells[strip->col_count-1].attrs&RENDER_ATTR_WIDE_LEFT))) return TABS_ERR_RANGE;
    rc=render_mark_rows(g,strip->row,1);
    if(rc!=RENDER_OK) return rc;
    for(uint32_t i=0;i<strip->col_count;++i) cells[i]=blank(strip->fg,strip->bg);
    uint32_t col=0; size_t index=strip->first_tab;
    while(col<strip->col_count && index<s->count) {
        uint32_t n=strip->tab_cols<strip->col_count-col?strip->tab_cols:strip->col_count-col;
        strip_tab(g,cells+col,n,strip,&s->slots[s->order[index]],s->order[index]==s->active);
        col+=n; ++index;
    }
    return TABS_OK;
}
size_t tabs_strip_hit(const tabs_set *s, const tabs_strip *strip, uint32_t col)
{
    if(!ready(s) || !strip || !strip->tab_cols || col<strip->first_col ||
       col-strip->first_col>=strip->col_count || strip->first_tab>=s->count) return SIZE_MAX;
    size_t delta=(size_t)(col-strip->first_col)/strip->tab_cols;
    return delta<s->count-strip->first_tab?strip->first_tab+delta:SIZE_MAX;
}
