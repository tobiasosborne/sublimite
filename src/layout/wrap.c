/* P4.1: allocation-free, streaming word wrap and visual-row queries. */
#include "layout/wrap.h"
#include <string.h>

static render_cell blank(const layout *l)
{
    return (render_cell){0,RENDER_NO_SLOT,l->cfg.fg,l->cfg.bg,0,0};
}
static void style(const layout *l,uint64_t byte,render_cell *c)
{
    if(byte==l->cursor) { c->fg=l->cfg.cursor_fg; c->bg=l->cfg.cursor_bg; c->attrs|=RENDER_ATTR_CURSOR; }
    else if(byte>=l->sel_lo && byte<l->sel_hi) { c->fg=l->cfg.sel_fg; c->bg=l->cfg.sel_bg; c->attrs|=RENDER_ATTR_SELECTION; }
}
static layout_wrap_row *descriptor(layout *l,uint32_t row)
{
    return &(l->wrap_planning?l->wrap_plan:l->wrap_rows)[row];
}
/* While idle, the last scratch descriptor's next stores the bottom row's
 * examined endpoint, including word lookahead discarded by a soft-break rewind.
 * Planning may reuse it; painting that bottom row publishes it again. */
static void remember_context(layout *l)
{
    if(!l->wrap_sink && !l->wrap_planning && l->row+1u==l->grid->dims.rows) {
        uint64_t *end=&l->wrap_plan[l->row].next;
        if(l->pos>*end) *end=l->pos;
    }
}
static void position(layout *l,uint64_t byte)
{
    if(byte<l->pos) remember_context(l);
    l->pos=byte;
    if(l->win_len && byte>=l->win_pos && byte-l->win_pos<=l->win_len) l->wi=(uint32_t)(byte-l->win_pos);
    else { l->win_len=l->wi=0; l->win_eof=false; }
}
static void reset_work(layout *l,uint32_t first,uint32_t end)
{
    l->row=first; l->row_end=end; l->phase=0; l->cluster_active=false;
    l->win_len=l->wi=0; l->win_eof=false;
}
int layout_wrap_init(layout *l,edit_arena *arena)
{
    if(!l || !l->grid || !arena || layout_busy(l) || l->wrap_rows) return LAYOUT_ERR_STATE;
    size_t n=l->grid->dims.rows;
    if(n>SIZE_MAX/(2u*sizeof(layout_wrap_row))) return LAYOUT_ERR_ARG;
    layout_wrap_row *p=edit_arena_alloc(arena,2u*n*sizeof *p,_Alignof(layout_wrap_row));
    if(!p) return LAYOUT_ERR_STATE;
    memset(p,0,2u*n*sizeof *p); l->wrap_rows=p; l->wrap_plan=p+n; l->wrap_capacity=l->grid->dims.rows;
    return LAYOUT_DONE;
}
void layout_set_cursor_visual(layout *l,uint64_t byte,bool trailing)
{
    layout_set_cursor(l,byte); l->wrap_cursor_end=trailing;
}
int layout_set_wrap(layout *l,bool enabled)
{
    if(!l || !l->grid || layout_busy(l) || (enabled && !l->wrap_rows)) return LAYOUT_ERR_STATE;
    l->wrap=enabled; l->wrap_planning=false; return LAYOUT_DONE;
}
void layout_wrap_geometry(layout *l)
{
    /* Columns change the physical cell stride. Clear the whole active extent
     * when painting the new geometry in either mode, including newly exposed
     * caller storage. Previous row_used extents no longer describe these rows. */
    if(l->gw+l->text_cols!=l->grid->dims.cols)
        for(uint32_t r=0;r<l->grid->dims.rows;r++) l->row_used[r]=l->grid->dims.cols;
}
void layout_wrap_begin(layout *l)
{
    l->hscroll=0; l->wrap_planning=false; l->wrap_fill=l->grid->dims.rows;
    memset(l->wrap_rows,0,(size_t)l->grid->dims.rows*sizeof *l->wrap_rows);
    l->wrap_rows[0]=(layout_wrap_row){.start=l->first_byte,.line_start=l->first_byte,.line=l->first_line};
}
int layout_begin_visual(layout *l,const piece_tree *t,layout_viewport vp,const layout_wrap_row *first)
{
    if(!l || !t || !first || !l->wrap || first->start>piece_len(t) || first->line_start>first->start) return LAYOUT_ERR_ARG;
    layout_wrap_row seed=*first; vp.first_byte=seed.line_start; vp.first_line=seed.line;
    int rc=layout_begin(l,t,vp); if(rc<0) return rc;
    l->row_byte[0]=seed.start; l->wrap_rows[0]=seed; l->approximate=seed.approximate;
    return LAYOUT_DONE;
}
static void gutter(layout *l,render_cell *cells,const layout_wrap_row *r)
{
    if(l->wrap_sink || l->wrap_planning) return;
    render_cell b={0,RENDER_NO_SLOT,l->cfg.gutter_fg,l->cfg.gutter_bg,0,0};
    for(uint32_t c=0;c<l->gw;c++) cells[c]=b;
    if(r->start==LAYOUT_VOID_ROW || r->continuation || l->gw<2) return;
    uint32_t c=l->gw-1; uint64_t number=r->line+1;
    do { if(!c) break; --c; uint32_t digit=(uint32_t)(number%10); number/=10;
        cells[c].glyph_index='0'+digit; cells[c].atlas_slot='0'+digit-0x20u;
    } while(number);
}
static void start_row(layout *l,render_cell *cells)
{
    layout_wrap_row *r=descriptor(l,l->row);
    position(l,r->start); l->col=r->column;
    l->vis=r->continuation?r->indent:0; l->wrap_written=l->vis; l->wrap_indent=r->continuation?r->indent:0;
    l->wrap_leading=!r->continuation;
    l->wrap_break_byte=LAYOUT_VOID_ROW; l->wrap_last=r->start;
    l->cluster_active=false; l->phase=1; l->wrap_cr=false; l->row_scanned=0;
    if(!l->wrap_sink && !l->wrap_planning && l->row+1u==l->grid->dims.rows)
        l->wrap_plan[l->row].next=r->start;
    gutter(l,cells,r);
    if(!l->wrap_sink && !l->wrap_planning) for(uint32_t c=0;c<l->vis;c++) cells[l->gw+c]=blank(l);
}
static void clear_row(layout *l,render_cell *cells,uint32_t written)
{
    if(l->wrap_sink || l->wrap_planning) return;
    uint32_t used=l->row_used[l->row];
    if(l->phase==1 && l->gw+l->wrap_written>used) used=l->gw+l->wrap_written;
    if(used>l->grid->dims.cols) used=l->grid->dims.cols;
    for(uint32_t c=written;c<used;c++) cells[c]=blank(l);
    l->row_used[l->row]=written;
}
static void next_seed(layout_wrap_row *dst,const layout_wrap_row *r)
{
    if(r->start==LAYOUT_VOID_ROW) { *dst=(layout_wrap_row){.start=LAYOUT_VOID_ROW,.end=LAYOUT_VOID_ROW,.next=LAYOUT_VOID_ROW}; return; }
    *dst=(layout_wrap_row){.start=r->next,.line_start=r->line_start,.line=r->line,
        .column=r->end_column,.indent=r->indent,.continuation=true};
    if(r->newline) { dst->line_start=r->next; dst->line++; dst->column=0; dst->indent=0; dst->continuation=false; }
    else if(r->next==LAYOUT_VOID_ROW) dst->continuation=false;
}
/* Planning writes only descriptors. Once the changed logical line's new row
 * count is known, preserve the suffix with one memmove, then paint that line. */
static int finish_plan(layout *l)
{
    uint32_t rows=l->grid->dims.rows, first=l->wrap_first, old_after=l->wrap_after, after=l->row;
    uint32_t old_count=old_after-first, new_count=after-first;
    if(new_count>=old_count) {
        uint32_t delta=new_count-old_count, keep=rows-after;
        if(keep) {
            memmove(l->grid->cells+(size_t)after*l->grid->dims.cols,
                    l->grid->cells+(size_t)old_after*l->grid->dims.cols,
                    (size_t)keep*l->grid->dims.cols*sizeof(render_cell));
            memmove(l->wrap_rows+after,l->wrap_rows+old_after,(size_t)keep*sizeof *l->wrap_rows);
            memmove(l->row_byte+after,l->row_byte+old_after,(size_t)keep*sizeof *l->row_byte);
            memmove(l->row_used+after,l->row_used+old_after,(size_t)keep*sizeof *l->row_used);
        }
        if(delta && render_mark_rows(l->grid,first,rows-first)!=RENDER_OK) return LAYOUT_ERR_STATE;
    } else {
        uint32_t delta=old_count-new_count, keep=rows-old_after;
        if(keep) {
            memmove(l->grid->cells+(size_t)after*l->grid->dims.cols,
                    l->grid->cells+(size_t)old_after*l->grid->dims.cols,
                    (size_t)keep*l->grid->dims.cols*sizeof(render_cell));
            memmove(l->wrap_rows+after,l->wrap_rows+old_after,(size_t)keep*sizeof *l->wrap_rows);
            memmove(l->row_byte+after,l->row_byte+old_after,(size_t)keep*sizeof *l->row_byte);
            memmove(l->row_used+after,l->row_used+old_after,(size_t)keep*sizeof *l->row_used);
        }
        l->wrap_fill=rows-delta;
        /* Bottom cells have not moved; keep their old used extents to clear. */
        if(render_mark_rows(l->grid,first,rows-first)!=RENDER_OK) return LAYOUT_ERR_STATE;
    }
    memcpy(l->wrap_rows+first,l->wrap_plan+first,(size_t)new_count*sizeof *l->wrap_rows);
    for(uint32_t r=first;r<after;r++) l->row_byte[r]=l->wrap_rows[r].start;
    if(render_mark_rows(l->grid,first,new_count)!=RENDER_OK) return LAYOUT_ERR_STATE;
    l->wrap_planning=false; reset_work(l,first,after);
    return LAYOUT_DONE;
}
static int end_row(layout *l,render_cell *cells,bool newline,bool eof)
{
    remember_context(l);
    layout_wrap_row *r=descriptor(l,l->row);
    r->end=l->pos-(newline && l->wrap_cr?1u:0u); r->end_column=l->col; r->last=l->wrap_last;
    r->indent=l->wrap_indent; r->newline=newline; r->approximate=l->approximate;
    r->next=newline?l->pos+1:eof?LAYOUT_VOID_ROW:l->pos;
    uint32_t written=l->gw+l->vis;
    if(!l->wrap_sink && !l->wrap_planning && (newline || eof) && r->end==l->cursor && l->vis<l->text_cols) {
        cells[written]=blank(l); style(l,r->end,&cells[written]); written++;
    }
    if(!l->wrap_sink && !l->wrap_planning && !newline && !eof && l->wrap_cursor_end && r->end==l->cursor) {
        uint32_t c=l->vis<l->text_cols?l->vis:l->vis-1;
        if(c==l->vis) { cells[l->gw+c]=blank(l); written++; }
        if(c && (cells[l->gw+c].attrs&RENDER_ATTR_WIDE_RIGHT)) {
            style(l,r->end,&cells[l->gw+c-1]);
        }
        style(l,r->end,&cells[l->gw+c]);
    }
    clear_row(l,cells,written);
    layout_wrap_row seed; next_seed(&seed,r);
    ++l->row; l->phase=0;
    if(l->wrap_planning && (newline || eof || l->row==l->row_end)) return finish_plan(l);
    if(l->row<l->row_end) {
        *descriptor(l,l->row)=seed;
        if(!l->wrap_planning) l->row_byte[l->row]=seed.start;
    } else if(!l->wrap_sink && !l->wrap_planning && l->row==l->row_end) {
        /* Keep the cached unaffected next descriptor intact on partial redraw. */
        if(l->wrap_fill<l->grid->dims.rows) {
            uint32_t fill=l->wrap_fill; l->wrap_fill=l->grid->dims.rows;
            if(fill==l->row) l->wrap_rows[fill]=seed;
            else next_seed(&l->wrap_rows[fill],&l->wrap_rows[fill-1]);
            l->row_byte[fill]=l->wrap_rows[fill].start;
            reset_work(l,fill,l->grid->dims.rows);
        }
    }
    return LAYOUT_DONE;
}
/* Printable ASCII is the common typing/prose path. Emit a span per iteration,
 * keeping word/indent boundaries while paying window/budget checks once. The
 * last ASCII before a non-ASCII unit goes through grapheme segmentation. */
static uint32_t ascii_span(layout *l,render_cell *cells,const uint8_t *p,size_t avail,uint32_t limit)
{
    size_t n=0, bound=limit<avail?limit:avail;
    const uint64_t high=UINT64_C(0x8080808080808080);
    while(n+8u<=bound) {
        uint64_t word; memcpy(&word,p+n,sizeof word);
        uint64_t del=word^UINT64_C(0x7f7f7f7f7f7f7f7f);
        /* A borrow may conservatively flag the following byte too; the
         * scalar tail below determines the exact printable prefix. */
        if((word&high) || ((word-UINT64_C(0x2020202020202020))&~word&high) ||
           ((del-UINT64_C(0x0101010101010101))&~del&high)) break;
        n+=8;
    }
    while(n<bound && p[n]>=0x20 && p[n]<0x7f) n++;
    if(n && (n<avail?p[n]>=0x80:!l->win_eof)) n--;
    if(!n) return 0;
    uint64_t at=l->pos, col=l->col;
    size_t leading=0;
    if(l->wrap_leading) {
        while(leading<n && p[leading]==' ') leading++;
        if(leading) {
            uint32_t cap=l->text_cols>2?l->text_cols/2u:0;
            uint64_t lead=col+leading; l->wrap_indent=lead<cap?(uint32_t)lead:cap;
        }
        if(leading<n) l->wrap_leading=false;
    }
    if(!l->wrap_leading) {
        const uint8_t *last=memrchr(p,' ',n);
        if(last && (size_t)(last-p)>=leading) {
            uint32_t after=(uint32_t)(last-p)+1u;
            l->wrap_break_byte=at+after; l->wrap_break_col=col+after;
            l->wrap_break_vis=l->vis+after; l->wrap_break_last=at+after-1u;
        }
    }
    if(!l->wrap_sink && !l->wrap_planning) {
        render_cell *dst=cells+l->gw+l->vis;
        uint32_t fg=l->cfg.fg, bg=l->cfg.bg;
        if(!l->marks) {
            const uint32_t colors[2]={fg,bg};
            for(size_t i=0;i<n;i++) {
                uint32_t b=p[i], glyph=b==' '?0:b, slot=b==' '?RENDER_NO_SLOT:b-0x20u;
                const uint32_t identity[2]={glyph,slot};
                memcpy(&dst[i],identity,sizeof identity);
                memcpy((uint8_t *)&dst[i]+offsetof(render_cell,fg),colors,sizeof colors);
                dst[i].attrs=0; dst[i].reserved=0;
            }
        } else {
            const layout_wrap_row *r=&l->wrap_rows[l->row];
            for(size_t i=0;i<n;i++) {
                uint32_t b=p[i];
                render_cell c={b==' '?0:b,b==' '?RENDER_NO_SLOT:b-0x20u,fg,bg,0,0};
                if(!(l->wrap_cursor_end && r->continuation && at+i==r->start && at+i==l->cursor)) style(l,at+i,&c);
                dst[i]=c;
            }
        }
    }
    l->wrap_last=at+n-1u;
    l->vis+=(uint32_t)n; l->col+=n;
    if(l->vis>l->wrap_written) l->wrap_written=l->vis;
    layout_consume(l,n);
    return (uint32_t)n;
}

int layout_wrap_run(layout *l)
{
    uint32_t units=0, budget=l->cfg.slice_clusters?l->cfg.slice_clusters:UINT32_MAX;
    uint64_t initial=l->wrap_sink?0:l->bytes_scanned;
    while(l->row<l->row_end) {
        if(units>=budget || l->bytes_scanned-initial>=LAYOUT_BYTE_BUDGET) return LAYOUT_MORE;
        render_cell *cells=l->wrap_sink?NULL:l->grid->cells+(size_t)l->row*l->grid->dims.cols;
        layout_wrap_row *r=descriptor(l,l->row);
        if(l->phase==0) {
            if(r->start==LAYOUT_VOID_ROW) {
                r->end=r->next=LAYOUT_VOID_ROW;
                gutter(l,cells,r); clear_row(l,cells,l->gw);
                ++l->row;
                if(l->row<l->row_end) { descriptor(l,l->row)->start=LAYOUT_VOID_ROW; if(!l->wrap_planning) l->row_byte[l->row]=LAYOUT_VOID_ROW; }
                continue;
            }
            start_row(l,cells);
        }
        if(l->wi+128u>l->win_len && !l->win_eof) layout_refill(l);
        if(l->wi>=l->win_len && !l->cluster_active) {
            int rc=end_row(l,cells,false,true); if(rc<0) return rc; continue;
        }
        const uint8_t *p=l->win+l->wi; size_t avail=l->win_len-l->wi;
        uint8_t b=avail?p[0]:0; uint64_t at=l->cluster_active?l->cluster_at:l->pos;
        if(!l->cluster_active && b=='\n') { int rc=end_row(l,cells,true,false); if(rc<0) return rc; continue; }
        if(!l->cluster_active && b=='\r' && avail>=2 && p[1]=='\n') { l->wrap_cr=true; layout_consume(l,1); continue; }
        if(!l->text_cols) {
            /* Gutter-only windows cannot display text. Bound tail scanning. */
            if(l->bytes_scanned-initial+1u>=LAYOUT_BYTE_BUDGET) return LAYOUT_MORE;
            if(l->row_scanned>=LAYOUT_LONG_LINE) {
                /* A byte-budget cutoff is not a grapheme boundary. With no
                 * text cells, keep the unresolved endpoint at the line seed. */
                l->approximate=true; position(l,r->start); l->col=r->column;
                int rc=end_row(l,cells,false,true); if(rc<0) return rc; continue;
            }
            layout_consume(l,1); units++; continue;
        }
        if(l->vis>=l->text_cols && !l->cluster_active && !l->wrap_leading &&
           (b=='\t' || (b==' ' && (avail>=2?p[1]<0x80:l->win_eof)))) {
            if(!l->wrap_sink && !l->wrap_planning && l->pos==l->cursor) {
                uint32_t c=l->gw+l->vis-1u;
                if(c>l->gw && (cells[c].attrs&RENDER_ATTR_WIDE_RIGHT)) style(l,l->pos,&cells[c-1u]);
                style(l,l->pos,&cells[c]);
            }
            uint32_t spaces=b=='\t'?l->tab-(uint32_t)(l->col%l->tab):1u;
            l->wrap_last=l->pos; l->col+=spaces; layout_consume(l,1); units++;
            l->wrap_break_byte=l->pos; l->wrap_break_col=l->col;
            l->wrap_break_vis=l->vis; l->wrap_break_last=l->wrap_last;
            continue;
        }
        if(l->vis>=l->text_cols && !l->cluster_active) {
            if(l->wrap_break_byte!=LAYOUT_VOID_ROW && l->wrap_break_byte>r->start) {
                position(l,l->wrap_break_byte); l->col=l->wrap_break_col; l->vis=l->wrap_break_vis; l->wrap_last=l->wrap_break_last;
            }
            int rc=end_row(l,cells,false,false); if(rc<0) return rc; continue;
        }
        if(!l->cluster_active && b>=0x20 && b<0x7f) {
            uint32_t limit=l->text_cols-l->vis;
            if(limit>budget-units) limit=budget-units;
            uint64_t left=LAYOUT_BYTE_BUDGET-(l->bytes_scanned-initial);
            if(left<limit) limit=(uint32_t)left;
            uint32_t n=ascii_span(l,cells,p,avail,limit);
            if(n) { units+=n; continue; }
        }
        int width=1; size_t len=1; uint16_t attrs=0; bool space=false;
        if(!l->cluster_active && b=='\t') {
            width=(int)(l->tab-(uint32_t)(l->col%l->tab)); space=true; layout_consume(l,1);
        } else if(!l->cluster_active && b>=0x20 && b<0x7f && (avail>=2?p[1]<0x80:l->win_eof)) {
            space=b==' '; layout_consume(l,1);
        } else if(!l->cluster_active && (b<0x20 || b==0x7f || !utf8_decode(p,avail).valid)) {
            attrs=RENDER_ATTR_INVERSE; layout_consume(l,1);
        } else {
            uint64_t remaining=LAYOUT_BYTE_BUDGET-(l->bytes_scanned-initial);
            size_t step=remaining<UTF8_GRAPHEME_BUDGET?(size_t)remaining:UTF8_GRAPHEME_BUDGET;
            int rc=layout_decode_cluster(l,&p,&len,&width,step);
            if(rc!=UTF8_G_END) return LAYOUT_MORE;
            b=p[0]; space=len==1 && b==' ';
        }
        units++;
        if(!width) continue;
        uint32_t w=(uint32_t)width;
        uint32_t indent=r->continuation?r->indent:0;
        if(w>l->text_cols-l->vis && l->vis>indent) {
            /* Put the entire cluster/word on the next row. Replaying a bounded
             * decoded cluster avoids retaining pointers across source refills. */
            uint64_t back=at; uint64_t col=l->col; uint32_t vis=l->vis;
            if(l->wrap_break_byte!=LAYOUT_VOID_ROW && l->wrap_break_byte>r->start) {
                back=l->wrap_break_byte; col=l->wrap_break_col; vis=l->wrap_break_vis; l->wrap_last=l->wrap_break_last;
            }
            position(l,back); l->col=col; l->vis=vis;
            int rc=end_row(l,cells,false,false); if(rc<0) return rc; continue;
        }
        l->wrap_last=at;
        if(l->wrap_leading) {
            if(space) { uint64_t lead=l->col+w; uint32_t max_indent=l->text_cols>2?l->text_cols/2u:0;
                l->wrap_indent=lead<max_indent?(uint32_t)lead:max_indent;
            } else l->wrap_leading=false;
        }
        uint32_t draw=w;
        if(draw>l->text_cols-l->vis) { draw=l->text_cols-l->vis; if(b!='\t') l->approximate=true; }
        if(!l->wrap_sink && !l->wrap_planning) {
            uint32_t slot=RENDER_NO_SLOT,glyph=0;
            if(b!='\t') {
                slot='?'-0x20u; glyph='?';
                if(len==1 && b>=0x20 && b<0x7f) { slot=b==' '?RENDER_NO_SLOT:b-0x20u; glyph=b==' '?0:b; }
                else if(w>draw) { slot='?'-0x20u; glyph='?'; }
                else if(!attrs) {
                    uint32_t candidate;
                    if(len>LAYOUT_WIN) l->approximate=true;
                    else if(l->cfg.glyph && l->cfg.glyph(l->cfg.glyph_ctx,p,len,w,&candidate)==0 &&
                            (candidate==RENDER_NO_SLOT || candidate<l->grid->glyph_count)) {
                        slot=candidate; glyph=slot==RENDER_NO_SLOT?0:l->grid->glyphs[slot].glyph_index;
                    }
                }
            }
            render_cell *dst=cells+l->gw+l->vis;
            if(b=='\t') {
                for(uint32_t c=0;c<draw;c++) {
                    dst[c]=blank(l);
                    if(l->marks && !(c && at==l->cursor)) style(l,at,&dst[c]);
                }
            } else {
                render_cell cell={glyph,slot,l->cfg.fg,l->cfg.bg,attrs,0};
                if(l->marks && !(l->wrap_cursor_end && r->continuation && at==r->start && at==l->cursor)) style(l,at,&cell);
                if(w==2 && draw==2) {
                    cell.attrs|=RENDER_ATTR_WIDE_LEFT; dst[0]=cell;
                    cell.glyph_index=0; cell.atlas_slot=RENDER_NO_SLOT;
                    cell.attrs=(uint16_t)((cell.attrs & (uint16_t)~RENDER_ATTR_WIDE_LEFT)|RENDER_ATTR_WIDE_RIGHT); dst[1]=cell;
                } else dst[0]=cell;
            }
        }
        l->vis+=draw; l->col+=w; if(l->vis>l->wrap_written) l->wrap_written=l->vis;
        if(space && !l->wrap_leading) {
            l->wrap_break_byte=l->pos; l->wrap_break_col=l->col; l->wrap_break_vis=l->vis; l->wrap_break_last=at;
        }
    }
    return LAYOUT_DONE;
}
int layout_wrap_relayout(layout *l,uint32_t first,uint32_t count)
{
    uint32_t rows=l->grid->dims.rows;
    if(first>rows || count>rows-first) return LAYOUT_ERR_ARG;
    if(render_mark_rows(l->grid,first,count)!=RENDER_OK) return LAYOUT_ERR_STATE;
    l->wrap_planning=false; l->wrap_fill=rows;
    l->bytes_scanned=l->bytes_read=l->cache_hits=l->cache_misses=0;
    reset_work(l,first,first+count); return count?LAYOUT_MORE:LAYOUT_DONE;
}
static void shift_bytes(layout *l,uint32_t from,uint64_t off,uint64_t old_len,uint64_t new_len)
{
    uint32_t rows=l->grid->dims.rows;
    uint64_t *context=&l->wrap_plan[rows-1u].next;
    if(from<rows && *context>=off+old_len) *context=*context-old_len+new_len;
    for(uint32_t i=from;i<rows;i++) {
        layout_wrap_row *r=&l->wrap_rows[i];
        if(r->start==LAYOUT_VOID_ROW) continue;
        uint64_t *fields[]={&r->start,&r->end,&r->next,&r->line_start,&r->last};
        for(size_t k=0;k<sizeof fields/sizeof fields[0];k++) if(*fields[k]!=LAYOUT_VOID_ROW && *fields[k]>=off+old_len) *fields[k]=*fields[k]-old_len+new_len;
        l->row_byte[i]=r->start;
    }
}
static uint32_t width_digits(uint64_t n) { uint32_t d=1; while(n>=10) { n/=10; d++; } return d; }
int layout_wrap_edit(layout *l,uint64_t off,uint64_t old_len,uint64_t new_len,uint64_t old_nl,uint64_t new_nl)
{
    bool busy=layout_busy(l); uint32_t rows=l->grid->dims.rows;
    layout_checkpoint_invalidate(l->checkpoints,off,old_len,new_len,old_nl,new_nl);
    l->total=piece_len((const piece_tree *)l->src);
    l->line_count=l->line_count-old_nl+new_nl;
    if(off<l->first_byte) {
        if(off+old_len>l->first_byte || (off+old_len==l->first_byte && old_nl)) return LAYOUT_RESET;
        l->first_byte=l->first_byte-old_len+new_len; l->first_line=l->first_line-old_nl+new_nl;
        shift_bytes(l,0,off,old_len,new_len);
        if(!busy && old_nl==new_nl) return LAYOUT_DONE;
        for(uint32_t i=0;i<rows;i++) l->wrap_rows[i].line=l->wrap_rows[i].line-old_nl+new_nl;
    }
    uint32_t gw=l->cfg.gutter?width_digits(l->line_count)+1:0; if(gw>l->grid->dims.cols) gw=l->grid->dims.cols;
    if(busy || gw!=l->gw) {
        layout_viewport vp={l->first_byte,l->first_line,0,l->line_count};
        return layout_begin(l,(const piece_tree *)l->src,vp)<0?LAYOUT_ERR_STATE:LAYOUT_MORE;
    }
    uint32_t found=0;
    while(found+1<rows && l->row_byte[found+1]<=off) found++;
    if(l->row_byte[found]==LAYOUT_VOID_ROW) return LAYOUT_DONE;
    layout_wrap_row *hit=&l->wrap_rows[found];
    /* A soft break can rewind past an examined word. Its bytes still affect
     * this row's partition; the next scalar can also change its final cluster. */
    uint64_t context=hit->end;
    if(found+1u==rows && l->wrap_plan[found].next>context) context=l->wrap_plan[found].next;
    if(off>context && off-context>4u) return LAYOUT_DONE;
    uint32_t first=found; while(first && l->wrap_rows[first-1].line_start==hit->line_start) first--;
    if(l->wrap_rows[first].continuation && off<l->wrap_rows[first].start) return LAYOUT_RESET;
    uint32_t after=found+1; while(after<rows && l->row_byte[after]!=LAYOUT_VOID_ROW && l->wrap_rows[after].line_start==hit->line_start) after++;
    shift_bytes(l,after,off,old_len,new_len);
    l->bytes_scanned=l->bytes_read=l->cache_hits=l->cache_misses=0; l->wrap_fill=rows;
    if(old_nl || new_nl) {
        l->wrap_planning=false;
        reset_work(l,first,rows);
        return render_mark_rows(l->grid,first,rows-first)==RENDER_OK?LAYOUT_MORE:LAYOUT_ERR_STATE;
    }
    l->wrap_first=first; l->wrap_after=after; l->wrap_planning=true;
    l->wrap_plan[first]=l->wrap_rows[first]; reset_work(l,first,rows);
    return LAYOUT_MORE;
}

static bool contains(const layout_wrap_row *r,uint64_t byte)
{
    return r->start!=LAYOUT_VOID_ROW && byte>=r->start && (byte<=r->end || (r->newline && byte<r->next)) &&
        (r->newline || r->next==LAYOUT_VOID_ROW || byte<r->next);
}
/* A column checkpoint certifies a grapheme boundary, not a word-wrap phase.
 * A deep uncached query uses it as a local visual-row seed and flags the phase
 * estimate. Cached visual descriptors (including scroll seeds) are exact. */
static int scan_row(const layout *context,const piece_tree *t,uint64_t byte,int direction,
                    layout_wrap_row *out,bool *approximate,bool indexed,const layout_wrap_row *visual_seed)
{
    layout q; memset(&q,0,offsetof(layout,win));
    render_grid grid={0}; grid.dims=context->grid->dims; grid.dims.rows=1;
    layout_wrap_row row[1], plan[1]; uint64_t rb[1]; uint32_t ru[1]={0};
    q.grid=&grid; q.cfg=context->cfg; q.cfg.slice_clusters=0; q.src=t; q.total=piece_len(t);
    q.tab=context->tab; q.text_cols=context->text_cols; q.gw=context->gw;
    q.wrap_rows=row; q.wrap_plan=plan; q.row_byte=rb; q.row_used=ru; q.wrap=true; q.wrap_sink=true;
    q.cursor=UINT64_MAX;
    uint64_t line=visual_seed?visual_seed->line:piece_byte_to_line(t,byte);
    uint64_t start=visual_seed?visual_seed->start:piece_line_to_byte(t,line), col=visual_seed?visual_seed->column:0;
    if(direction<0 && byte==start && line) { --line; start=piece_line_to_byte(t,line); byte=start==byte?byte:piece_line_to_byte(t,line+1)-1; direction=0; }
    uint64_t logical=visual_seed?visual_seed->line_start:start; bool seeded=false;
    const layout_checkpoint_store *s=indexed?context->checkpoints:NULL;
    uint64_t lookbehind=(uint64_t)q.text_cols*8u+LAYOUT_CHECKPOINT_STRIDE;
    uint64_t target=byte>lookbehind?byte-lookbehind:0;
    if(!visual_seed && s && s->source==t && s->start==start && s->tab==q.tab && s->count && target>start) {
        size_t lo=0,hi=s->count;
        while(lo+1<hi) { size_t mid=lo+(hi-lo)/2; if(s->entries[mid].byte<=target) lo=mid; else hi=mid; }
        start=s->entries[lo].byte; col=s->entries[lo].column; seeded=start!=logical;
    }
    if(!visual_seed && byte-start>LAYOUT_BYTE_BUDGET/2u) {
        /* Unindexed deep ASCII text still gets a bounded certified boundary.
         * An arbitrary UTF-8 window edge is never treated as a cluster start. */
        uint8_t near[LAYOUT_CHECKPOINT_STRIDE];
        uint64_t from=target>logical?target:logical;
        size_t n=(size_t)(q.total-from<sizeof near?q.total-from:sizeof near);
        if(n>=2 && piece_read(t,from,near,n)==PIECE_OK) {
            for(size_t i=1;i<n;i++) if(near[i]<128 && near[i-1]<128 && near[i]!='\n' && near[i]!='\r' && near[i-1]!='\r') {
                start=from+i; col=start-logical; seeded=true; break;
            }
        }
    }
    uint32_t indent=0;
    if(seeded) {
        *approximate=true;
        if(indexed) for(uint32_t i=0;i<context->grid->dims.rows;i++) if(context->wrap_rows[i].line_start==logical) { indent=context->wrap_rows[i].indent; break; }
        if(!indexed) {
            uint8_t prefix[LAYOUT_CHECKPOINT_STRIDE];
            size_t n=(size_t)(q.total-logical<sizeof prefix?q.total-logical:sizeof prefix);
            if(n && piece_read(t,logical,prefix,n)==PIECE_OK) {
                uint32_t cap=q.text_cols>2?q.text_cols/2u:0;
                for(size_t i=0;i<n && indent<cap;i++) {
                    if(prefix[i]==' ') indent++;
                    else if(prefix[i]=='\t') { uint32_t add=q.tab-indent%q.tab; indent=add>cap-indent?cap:indent+add; }
                    else break;
                }
            }
        }
    }
    row[0]=(layout_wrap_row){.start=start,.line_start=logical,.line=line,.column=col,.indent=indent,.continuation=seeded};
    if(visual_seed) { row[0]=*visual_seed; q.approximate=visual_seed->approximate; }
    layout_wrap_row previous=row[0]; bool have_previous=false, found=false;
    for(;;) {
        rb[0]=row[0].start; reset_work(&q,0,1);
        uint64_t before=q.bytes_scanned;
        int rc=layout_wrap_run(&q);
        if(rc<0) return rc;
        if(rc==LAYOUT_MORE || q.bytes_scanned>=LAYOUT_BYTE_BUDGET) {
            *approximate=true; *out=have_previous?previous:row[0];
            if(!have_previous) { out->end=out->start; out->next=out->start; out->end_column=out->column; }
            return LAYOUT_DONE;
        }
        if(found) { *out=row[0]; return LAYOUT_DONE; }
        if(contains(&row[0],byte)) {
            if(direction<0 && have_previous) { *out=previous; return LAYOUT_DONE; }
            if(direction<=0 || row[0].next==LAYOUT_VOID_ROW) { *out=row[0]; return LAYOUT_DONE; }
            found=true;
        }
        if(row[0].next==LAYOUT_VOID_ROW || q.bytes_scanned==before) { *out=row[0]; return LAYOUT_DONE; }
        previous=row[0]; have_previous=true; next_seed(&row[0],&previous);
    }
}
int layout_visual_row(const layout *l,const piece_tree *t,uint64_t byte,int direction,
                      layout_wrap_row *out,bool *approximate)
{
    if(!l || !l->grid || !l->wrap || !t || !out || !approximate || byte>piece_len(t) || direction< -1 || direction>1) return LAYOUT_ERR_ARG;
    *approximate=false;
    if(!layout_busy(l) && l->src==t && l->total==piece_len(t)) {
        uint32_t rows=l->grid->dims.rows;
        for(uint32_t i=0;i<rows;i++) if(contains(&l->wrap_rows[i],byte)) {
            const layout_wrap_row *r=NULL;
            if(direction==0) r=&l->wrap_rows[i];
            else if(direction<0) { if(i) r=&l->wrap_rows[i-1]; else if(l->wrap_rows[i].start==0) r=&l->wrap_rows[i]; }
            else if(i+1<rows && l->row_byte[i+1]!=LAYOUT_VOID_ROW) r=&l->wrap_rows[i+1];
            else if(l->wrap_rows[i].next==LAYOUT_VOID_ROW) r=&l->wrap_rows[i];
            else {
                layout_wrap_row seed; next_seed(&seed,&l->wrap_rows[i]);
                *approximate=l->wrap_rows[i].approximate;
                return scan_row(l,t,seed.start,0,out,approximate,true,&seed);
            }
            if(r) { *out=*r; *approximate=r->approximate; return LAYOUT_DONE; }
            break;
        }
    }
    int rc=scan_row(l,t,byte,direction,out,approximate,true,NULL);
    if(rc==LAYOUT_DONE) out->approximate|=*approximate;
    return rc;
}

int layout_visual_row_fresh(const layout *l,const piece_tree *t,uint64_t byte,
                            layout_wrap_row *out,bool *approximate)
{
    if(!l || !l->grid || !l->wrap || !t || !out || !approximate || byte>piece_len(t)) return LAYOUT_ERR_ARG;
    *approximate=false;
    int rc=scan_row(l,t,byte,0,out,approximate,false,NULL);
    if(rc==LAYOUT_DONE) out->approximate|=*approximate;
    return rc;
}
