#include "view/view.h"
#include <string.h>

/* Each scan begins at a certified boundary; no window edge is a boundary. */
enum { S_NEXT=1, S_PREV, S_CEIL, S_BYTE_COL, S_COL_BYTE, S_HOME,
       S_WORD_LEFT, S_WORD_RIGHT, S_WORD_SELECT };
enum { P_MOVE=1, P_VERTICAL_COL, P_VERTICAL_DEST, P_APPLY,
       P_FOLLOW, P_REPAIR };
enum { C_WORD=0, C_SEPARATOR=1, C_SPACE=2, C_NEWLINE=3 };

static uint64_t min_u64(uint64_t a, uint64_t b) { return a<b?a:b; }
static uint64_t max_u64(uint64_t a, uint64_t b) { return a>b?a:b; }
static int byte_class(uint8_t b)
{
    if(b=='\r' || b=='\n') return C_NEWLINE;
    if(b==' ' || b=='\t' || b=='\v' || b=='\f') return C_SPACE;
    if(b<128 && strchr("./\\()\"'-:,.;<>~!@#$%^&*|+=[]{}`~?",(int)b)!=NULL && b!=0)
        return C_SEPARATOR;
    return C_WORD;
}
static int unit_class(utf8_step s)
{
    if(!s.valid) return C_WORD;
    uint32_t cp=s.cp;
    if(cp<128) return byte_class((uint8_t)cp);
    /* Unicode 15.1 White_Space, excluding ASCII handled above. */
    if(cp==0x85 || cp==0xa0 || cp==0x1680 || (cp>=0x2000 && cp<=0x200a) ||
       cp==0x2028 || cp==0x2029 || cp==0x202f || cp==0x205f || cp==0x3000) return C_SPACE;
    return C_WORD;
}
static uint8_t byte_at(view *v, uint64_t pos)
{
    uint8_t b=0;
    if(pos<piece_len(v->tree)) { (void)piece_read(v->tree,pos,&b,1); ++v->scanned; }
    return b;
}
static uint64_t line_end(view *v, uint64_t line)
{
    uint64_t end=piece_line_to_byte(v->tree,line+1);
    if(end>0 && byte_at(v,end-1)=='\n') {
        --end; if(end>0 && byte_at(v,end-1)=='\r') --end;
    }
    return end;
}
/* A pair of ASCII scalars (except CR/LF) proves a boundary before its
 * second byte even if a Prepend/Indic/RI/ZWJ context precedes the pair.
 * Otherwise replay from line start, resumably. */
static uint64_t near_seed(view *v, uint64_t target)
{
    uint64_t line=piece_byte_to_line(v->tree,target);
    uint64_t start=piece_line_to_byte(v->tree,line);
    if(target==start && target>0) {
        line=piece_byte_to_line(v->tree,target-1);
        start=piece_line_to_byte(v->tree,line);
    }
    if(target<=start) return start;
    uint64_t from=max_u64(start,target>VIEW_WINDOW?target-VIEW_WINDOW:0);
    size_t n=(size_t)(target-from);
    uint8_t buf[VIEW_WINDOW];
    if(n>=2 && v->scanned+n+264u<=VIEW_SCAN_BOUND && piece_read(v->tree,from,buf,n)==0) {
        v->scanned+=n;
        for(size_t i=n-1;i>0;--i) {
            if(buf[i]<128 && buf[i-1]<128 && buf[i]!='\n' && buf[i]!='\r' &&
               buf[i-1]!='\r') return from+i;
        }
    }
    return start;
}
/* Certify the start of a nearby ASCII class run, including whitespace to
 * its right. If Unicode or a window edge hides the start, use exact replay. */
static uint64_t word_seed(view *v, uint64_t cursor, uint64_t line_start)
{
    uint64_t from=max_u64(line_start,cursor>VIEW_WINDOW?cursor-VIEW_WINDOW:0);
    size_t n=(size_t)(cursor-from);
    if(!n || v->scanned+n+256u>VIEW_SCAN_BOUND) return line_start;
    uint8_t buf[VIEW_WINDOW];
    if(piece_read(v->tree,from,buf,n)!=0) return line_start;
    v->scanned+=n;
    size_t p=n;
    while(p && buf[p-1]<128 && byte_class(buf[p-1])==C_SPACE) --p;
    if(p==0) return line_start;
    if(buf[p-1]>=128 || byte_class(buf[p-1])==C_NEWLINE) return line_start;
    int kind=byte_class(buf[p-1]);
    while(p && buf[p-1]<128 && byte_class(buf[p-1])==kind) --p;
    if(p==0) return line_start;
    /* Certify both sides: a Prepend can attach to an ASCII separator and
     * give that cluster a word class. Merely proving the right boundary
     * would then miss the true start of the word run. */
    bool left_start=(p==1 && from==line_start) ||
        (p>1 && buf[p-2]<128 && buf[p-2]!='\r');
    if(left_start && buf[p-1]<128 && buf[p-1]!='\r') return from+p;
    return line_start;
}
static void scan_begin(view *v, unsigned mode, uint64_t start, uint64_t end, uint64_t target, uint64_t col)
{
    v->scan_mode=mode; v->scan_pos=start; v->cluster_start=start;
    v->scan_col=col; v->end=end; v->target=target; v->word_found=false;
    v->result=start; v->column=col; v->run_start=start; v->run_class=-1;
    v->cluster_class=-1; v->win_len=0; utf8_cseg_init(&v->cluster);
}
static void column_begin(view *v, unsigned mode, uint64_t line, uint64_t target)
{
    uint64_t start=piece_line_to_byte(v->tree,line), col=0;
    uint64_t end=line_end(v,line);
    if(v->config.checkpoint && !v->edited) {
        uint64_t seed=start, seed_col=0;
        int ok=v->config.checkpoint(v->config.checkpoint_ctx,v->tree,line,
            mode==S_BYTE_COL?target:UINT64_MAX,mode==S_COL_BYTE?target:UINT64_MAX,&seed,&seed_col);
        if(ok && seed>=start && seed<=end &&
           (mode==S_BYTE_COL?seed<=target:seed_col<=target)) { start=seed; col=seed_col; }
    }
    scan_begin(v,mode,start,end,target,col);
}
/* Streaming complete-cluster step. Charge consumed bytes plus four bytes of
 * lookahead per completion. Window copies/opaque piece queries are separate. */
static int cluster_next(view *v, int *width)
{
    uint64_t total=piece_len(v->tree);
    for(;;) {
        if(v->scanned+264u>VIEW_SCAN_BOUND) return VIEW_MORE;
        uint64_t pos=v->scan_pos;
        if(v->win_len==0 || pos<v->win_pos || pos>=v->win_pos+v->win_len ||
           (v->win_pos+v->win_len<total && v->win_pos+v->win_len-pos<4)) {
            v->win_pos=pos; v->win_len=(size_t)min_u64(total-pos,sizeof v->win);
            if(v->win_len && piece_read(v->tree,pos,v->win,v->win_len)!=0) return PIECE_ERR_RANGE;
        }
        size_t at=(size_t)(pos-v->win_pos), available=v->win_len-at, used=0;
        if(v->cluster_class<0) {
            v->cluster_class=available?unit_class(utf8_decode(v->win+at,available)):C_NEWLINE;
            v->scanned+=4;
        }
        size_t budget=(size_t)(VIEW_SCAN_BOUND-v->scanned-256u);
        int rc=utf8_cluster_step(&v->cluster,v->win+at,available,budget,
            v->win_pos+v->win_len==total,&used,width);
        v->scan_pos+=used; v->scanned+=used;
        if(rc==UTF8_G_END) { v->scanned+=4; return VIEW_OK; }
        if(rc==UTF8_G_BUDGET) return VIEW_MORE;
        /* MORE consumed all complete units; refill at the unconsumed tail. */
        v->win_len=0;
    }
}
static int scan_run(view *v)
{
    for(;;) {
        unsigned mode=v->scan_mode;
        if(v->scan_pos==v->cluster_start) {
            if((mode==S_BYTE_COL || mode==S_CEIL) && v->scan_pos>=v->target) {
                v->result=v->scan_pos; v->column=v->scan_col; return 0;
            }
            if(mode==S_COL_BYTE && v->scan_col>=v->target) { v->result=v->scan_pos; return 0; }
            if(v->scan_pos>=v->end) {
                if(mode!=S_WORD_LEFT) v->result=v->scan_pos;
                v->column=v->scan_col; return 0;
            }
        }
        int width=0, rc=cluster_next(v,&width);
        if(rc!=0) {
            if(rc==VIEW_MORE && (mode==S_BYTE_COL || mode==S_COL_BYTE || mode==S_HOME)) {
                v->state.approximate=true; v->result=v->cluster_start;
                v->column=v->scan_col; return 0;
            }
            return rc;
        }
        uint64_t start=v->cluster_start, next=v->scan_pos;
        int cls=v->cluster_class;
        uint64_t cells=(uint64_t)width;
        if(cls==C_SPACE && byte_at(v,start)=='\t') cells=v->config.tab_width-v->scan_col%v->config.tab_width;
        if(mode==S_NEXT) { v->result=next; return 0; }
        if(mode==S_PREV) {
            v->result=start; if(next>=v->target) return 0;
        } else if(mode==S_CEIL) {
            if(next>=v->target) { v->result=next; return 0; }
        } else if(mode==S_COL_BYTE) {
            if(cells>v->target-v->scan_col) { v->result=start; return 0; }
        } else if(mode==S_HOME) {
            if(cls!=C_SPACE) { v->result=start; return 0; }
        } else if(mode==S_WORD_RIGHT) {
            if(cls==C_NEWLINE && start!=v->origin) { v->result=start; return 0; }
            if(v->run_class<0 && cls!=C_SPACE) v->run_class=cls;
            else if(v->run_class>=0 && cls!=v->run_class) { v->result=start; return 0; }
        } else if(mode==S_WORD_LEFT) {
            if(cls!=v->run_class) { v->run_start=start; v->run_class=cls; }
            if(cls!=C_SPACE) v->result=cls==C_NEWLINE?(next<v->target?next:start):v->run_start;
            if(next>=v->target) return 0;
        } else if(mode==S_WORD_SELECT) {
            if(!v->word_found) {
                if(cls!=v->run_class) { v->run_start=start; v->run_class=cls; }
                if(next>v->target) { v->lo=v->run_start; v->hi=next; v->word_found=true; }
            } else if(cls==v->run_class) v->hi=next;
            else { v->result=start; return 0; }
        }
        v->scan_col+=cells; v->cluster_start=next; v->cluster_class=-1;
        if(v->scanned+264u>VIEW_SCAN_BOUND) {
            if(mode==S_BYTE_COL || mode==S_COL_BYTE || mode==S_HOME) {
                v->state.approximate=true; v->result=next; v->column=v->scan_col; return 0;
            }
            return VIEW_MORE;
        }
    }
}
static void set_cursor(view *v, uint64_t target)
{
    v->state.selection.cursor=target;
    if(!v->shift) v->state.selection.anchor=target;
}
static void follow_line(view *v)
{
    uint64_t line=piece_byte_to_line(v->tree,v->state.selection.cursor);
    if(line<v->state.first_line) v->state.first_line=line;
    else if(line-v->state.first_line>=v->config.rows) v->state.first_line=line-v->config.rows+1;
    v->state.first_byte=piece_line_to_byte(v->tree,v->state.first_line);
}
static int finish_move(view *v)
{
    follow_line(v);
    v->line=piece_byte_to_line(v->tree,v->state.selection.cursor);
    column_begin(v,S_BYTE_COL,v->line,v->state.selection.cursor); v->phase=P_FOLLOW;
    return 0;
}
static int remove_range(view *v, uint64_t lo, uint64_t hi, view_change *change)
{
    if(hi>lo) {
        int rc=piece_delete(v->tree,lo,hi-lo,NULL); if(rc!=0) return rc;
        *change=(view_change){lo,hi-lo,0,true};
        v->edited=true;
    }
    v->state.selection.preferred_col=VIEW_PREFERRED_UNSET;
    v->shift=false;
    /* Joining two clusters can destroy the old cursor boundary. */
    v->state.selection.cursor=0; v->state.selection.anchor=0;
    scan_begin(v,S_CEIL,near_seed(v,lo),piece_len(v->tree),lo,0);
    v->phase=P_REPAIR; return 0;
}
static bool deletion(view_key key) { return key>=VIEW_BACKSPACE && key<=VIEW_WORD_DELETE; }
static bool vertical(view_key key) { return key>=VIEW_UP && key<=VIEW_PAGE_DOWN; }
static int advance(view *v, view_change *change)
{
    for(;;) {
        if(v->phase==P_APPLY) {
            set_cursor(v,v->result); (void)finish_move(v); continue;
        }
        int rc=scan_run(v); if(rc!=0) return rc;
        if(v->phase==P_VERTICAL_COL) {
            v->state.selection.preferred_col=v->column;
            column_begin(v,S_COL_BYTE,v->line,v->state.selection.preferred_col);
            v->phase=P_VERTICAL_DEST;
        } else if(v->phase==P_VERTICAL_DEST) {
            v->phase=P_APPLY;
        } else if(v->phase==P_MOVE) {
            if(deletion(v->key)) {
                rc=remove_range(v,min_u64(v->origin,v->result),max_u64(v->origin,v->result),change);
                if(rc!=0) return rc;
            } else if(v->key==VIEW_SELECT_WORD) {
                v->state.selection.anchor=v->lo; v->state.selection.cursor=v->hi;
                (void)finish_move(v);
            } else {
                if(v->key==VIEW_HOME && v->result==v->origin) v->result=piece_line_to_byte(v->tree,v->line);
                v->phase=P_APPLY;
            }
        } else if(v->phase==P_REPAIR) {
            v->state.selection.cursor=v->result; v->state.selection.anchor=v->result;
            (void)finish_move(v);
        } else if(v->phase==P_FOLLOW) {
            uint64_t col=v->column;
            if(col<v->state.hscroll) v->state.hscroll=col;
            else if(col-v->state.hscroll>=v->config.cols) v->state.hscroll=col-v->config.cols+1;
            v->busy=false; return 0;
        } else return VIEW_ERR_ARG;
    }
}
void view_init(view *v, piece_tree *tree, const view_config *config)
{
    memset(v,0,sizeof *v); v->tree=tree;
    if(config) v->config=*config;
    if(!v->config.tab_width) v->config.tab_width=4;
    if(!v->config.rows) v->config.rows=24;
    if(!v->config.cols) v->config.cols=80;
    v->state.selection.preferred_col=VIEW_PREFERRED_UNSET;
}
bool view_busy(const view *v) { return v->busy; }
void view_cancel(view *v) { v->busy=false; }
int view_continue(view *v, view_change *change)
{
    if(!v || !change || !v->tree) return VIEW_ERR_ARG;
    *change=(view_change){0}; v->scanned=0;
    if(!v->busy) return VIEW_OK;
    int rc=advance(v,change); if(rc!=VIEW_MORE) v->busy=false; return rc;
}
int view_command(view *v, view_key key, bool shift, const uint8_t *text, size_t len, view_change *change)
{
    if(!v || !v->tree || !change || key<VIEW_LEFT || key>VIEW_TYPE || (len && !text)) return VIEW_ERR_ARG;
    *change=(view_change){0};
    if(v->busy) return VIEW_ERR_BUSY;
    uint64_t total=piece_len(v->tree), cursor=v->state.selection.cursor, anchor=v->state.selection.anchor;
    if(cursor>total || anchor>total) return VIEW_ERR_ARG;
    v->scanned=0; v->busy=true; v->shift=shift; v->key=key; v->origin=cursor; v->edited=false;
    v->lo=min_u64(cursor,anchor); v->hi=max_u64(cursor,anchor);
    v->state.approximate=false;
    v->line=piece_byte_to_line(v->tree,cursor);
    uint64_t start=piece_line_to_byte(v->tree,v->line), end=line_end(v,v->line);
    v->phase=P_MOVE;
    if(!vertical(key)) v->state.selection.preferred_col=VIEW_PREFERRED_UNSET;
    if(key==VIEW_TYPE) {
        /* Insert first: insertion failure leaves the selection/text intact.
         * A later delete failure reports the insertion prefix, per piece API. */
        uint64_t lo=v->lo, old=v->hi-v->lo;
        int rc=piece_insert(v->tree,lo,text,len);
        if(rc!=0) { v->busy=false; return rc; }
        if(len) *change=(view_change){lo,0,len,true};
        if(len) v->edited=true;
        if(old) {
            rc=piece_delete(v->tree,lo+len,old,NULL);
            if(rc!=0) {
                v->state.selection.cursor=0; v->state.selection.anchor=0;
                v->busy=false; return rc;
            }
            *change=(view_change){lo,old,len,true};
            v->edited=true;
        }
        v->shift=false; v->state.selection.cursor=0; v->state.selection.anchor=0;
        scan_begin(v,S_CEIL,near_seed(v,lo+len),piece_len(v->tree),lo+len,0); v->phase=P_REPAIR;
    } else if(deletion(key) && cursor!=anchor) {
        int rc=remove_range(v,v->lo,v->hi,change); if(rc!=0) { v->busy=false; return rc; }
    } else if((key==VIEW_LEFT || key==VIEW_RIGHT) && cursor!=anchor && !shift) {
        v->result=key==VIEW_LEFT?v->lo:v->hi; v->phase=P_APPLY;
    } else if(key==VIEW_LEFT || key==VIEW_BACKSPACE) {
        scan_begin(v,S_PREV,near_seed(v,cursor),total,cursor,0);
    } else if(key==VIEW_RIGHT || key==VIEW_DELETE) {
        scan_begin(v,S_NEXT,cursor,total,0,0);
    } else if(key==VIEW_WORD_LEFT || key==VIEW_WORD_BACKSPACE) {
        if(cursor==start && cursor>0) scan_begin(v,S_PREV,near_seed(v,cursor-1),total,cursor,0);
        else scan_begin(v,S_WORD_LEFT,word_seed(v,cursor,start),cursor,cursor,0);
    } else if(key==VIEW_WORD_RIGHT || key==VIEW_WORD_DELETE) {
        if(cursor>=end) scan_begin(v,S_NEXT,cursor,total,0,0);
        else scan_begin(v,S_WORD_RIGHT,cursor,end,0,0);
    } else if(key==VIEW_HOME) scan_begin(v,S_HOME,start,end,0,0);
    else if(key==VIEW_END || key==VIEW_DOC_HOME || key==VIEW_DOC_END) {
        v->result=key==VIEW_END?end:(key==VIEW_DOC_HOME?0:total); v->phase=P_APPLY;
    } else if(key==VIEW_SELECT_ALL) {
        v->state.selection.anchor=0; v->state.selection.cursor=total; (void)finish_move(v);
    } else if(key==VIEW_SELECT_LINE) {
        uint64_t a=piece_byte_to_line(v->tree,v->lo), b=piece_byte_to_line(v->tree,v->hi);
        if(v->hi>v->lo && v->hi==piece_line_to_byte(v->tree,b)) --b;
        uint64_t lo=piece_line_to_byte(v->tree,a), hi=piece_line_to_byte(v->tree,b+1);
        if(v->hi>v->lo && v->lo==lo && v->hi==hi) hi=piece_line_to_byte(v->tree,b+2);
        v->state.selection.anchor=lo; v->state.selection.cursor=hi; (void)finish_move(v);
    } else if(key==VIEW_SELECT_WORD) {
        if(cursor!=anchor || cursor>=end) { v->result=cursor; v->phase=P_APPLY; v->shift=true; }
        else { v->lo=cursor; v->hi=cursor; scan_begin(v,S_WORD_SELECT,word_seed(v,cursor,start),end,cursor,0); }
    } else if(vertical(key)) {
        uint64_t lines=piece_line_count(v->tree), delta=(key==VIEW_PAGE_UP || key==VIEW_PAGE_DOWN)?v->config.rows:1;
        bool up=key==VIEW_UP || key==VIEW_PAGE_UP;
        uint64_t dest=up?(v->line>delta?v->line-delta:0):min_u64(lines-1,v->line+delta);
        if(key==VIEW_PAGE_UP || key==VIEW_PAGE_DOWN) {
            v->state.first_line=up?(v->state.first_line>delta?v->state.first_line-delta:0):
                min_u64(lines-1,v->state.first_line+delta);
        }
        uint64_t preferred=v->state.selection.preferred_col;
        if(preferred==VIEW_PREFERRED_UNSET) {
            column_begin(v,S_BYTE_COL,v->line,cursor); v->phase=P_VERTICAL_COL;
        } else { column_begin(v,S_COL_BYTE,dest,preferred); v->phase=P_VERTICAL_DEST; }
        v->line=dest;
        if((up && piece_byte_to_line(v->tree,cursor)==0) || (!up && piece_byte_to_line(v->tree,cursor)==lines-1)) {
            /* First/last line: keep preferred, move to document boundary. */
            if(preferred==VIEW_PREFERRED_UNSET) {
                int rc=scan_run(v); if(rc!=0) { v->busy=false; return rc; }
                v->state.selection.preferred_col=v->column;
            }
            v->result=up?0:total; v->phase=P_APPLY;
        }
    }
    int rc=advance(v,change); if(rc!=VIEW_MORE) v->busy=false; return rc;
}
