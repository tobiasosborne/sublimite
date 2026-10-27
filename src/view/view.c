#include "view/view.h"
#include "layout/layout.h"
#include "undo/undo.h"
#include <string.h>
#include "os/os.h"

static uint64_t now_ns(void)
{
    return os_now_ns();
}
static void slice_begin(view *v)
{
    v->scanned=0; v->deadline_ns=now_ns()+VIEW_SLICE_NS; v->deadline_check_work=256;
}
static bool exhausted(view *v)
{
    if(v->scanned+264u>VIEW_SCAN_BOUND) return true;
    if(v->scanned<v->deadline_check_work) return false;
    v->deadline_check_work=v->scanned+256u;
    return now_ns()>=v->deadline_ns;
}
static undo_state undo_position(const view_state *state)
{
    undo_state result;
    memcpy(result.bytes,&state->selection.cursor,sizeof(uint64_t));
    memcpy(result.bytes+sizeof(uint64_t),&state->selection.anchor,sizeof(uint64_t));
    return result;
}
static int mutation_begin(view *v)
{
    if(!v->undo || v->undo_group_open) return VIEW_OK;
    undo_state before=undo_position(&v->before_state);
    int rc=undo_group_begin(v->undo,&before);
    if(rc==UNDO_OK) v->undo_group_open=true;
    return rc;
}
static int mutation_end(view *v, int rc)
{
    if(v->undo_group_open) {
        undo_state after=undo_position(&v->state);
        int done=undo_group_end(v->undo,&after); v->undo_group_open=false;
        if(rc==VIEW_OK) rc=done;
    }
    return rc;
}
static int insert_bytes(view *v, uint64_t off, const uint8_t *text, size_t len)
{
    if(!len) return VIEW_OK;
    int rc=mutation_begin(v); if(rc!=VIEW_OK) return rc;
    if(!v->undo) return piece_insert(v->tree,off,text,len);
    undo_state position=undo_position(&v->before_state);
    return undo_insert(v->undo,off,text,len,now_ns(),&position,&position);
}
static int delete_bytes(view *v, uint64_t off, uint64_t len)
{
    int rc=mutation_begin(v); if(rc!=VIEW_OK) return rc;
    if(!v->undo) return piece_delete(v->tree,off,len,NULL);
    undo_kind kind=(v->key==VIEW_BACKSPACE || v->key==VIEW_WORD_BACKSPACE)?UNDO_BACKSPACE:UNDO_DELETE;
    undo_state position=undo_position(&v->before_state);
    return undo_delete(v->undo,off,len,kind,now_ns(),&position,&position);
}

/* Each scan begins at a certified boundary; no window edge is a boundary. */
enum { S_NEXT=1, S_PREV, S_CEIL, S_BYTE_COL, S_COL_BYTE, S_HOME,
       S_FLOOR,
       S_WORD_LEFT, S_WORD_RIGHT, S_WORD_SELECT };
enum { P_MOVE=1, P_VERTICAL_COL, P_VERTICAL_DEST, P_APPLY,
       P_FOLLOW, P_REPAIR, P_VERTICAL_BOUNDARY, P_NORMAL_CURSOR, P_NORMAL_ANCHOR, P_SETUP, P_FINISH, P_NORMAL_SETUP,
       P_VISUAL_COL, P_VISUAL_SETUP, P_BULK, P_BULK_PREPARE, P_BULK_COLUMN,
       P_REL_START, P_REL_LEFT_START, P_REL_COL, P_REL_ROWS, P_REL_DEST, P_REL_FOLLOW,
       P_SEEK, P_REL_META, P_REL_WRAP };
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
/* Query the bytes themselves in short reads. Cached entries are certified line
 * starts, never approximate counts; EOF answers to out-of-range line queries
 * are kept separately so they cannot masquerade as real line starts. */
enum { Q_BYTE=1, Q_LINE };
static void line_store(view *v, uint64_t byte, uint64_t line)
{
    unsigned i=v->line_next++%32u;
    v->lines[i].byte=byte; v->lines[i].line=line; v->lines[i].valid=true;
}
static void queries_invalidate(view *v, uint64_t off)
{
    v->query_kind=0;
    for(unsigned i=0;i<16;++i)
        if(v->queries[i].kind!=Q_BYTE || v->queries[i].target>off) v->queries[i].kind=0;
    for(unsigned i=0;i<32;++i)
        if(v->lines[i].byte>off) v->lines[i].valid=false;
}
static int line_query(view *v, unsigned kind, uint64_t target, uint64_t *result)
{
    uint64_t total=piece_len(v->tree);
    if(kind==Q_BYTE) target=min_u64(target,total);
    if(target==0) { *result=0; return VIEW_OK; }
    for(unsigned i=0;i<16;++i) if(v->queries[i].kind==kind && v->queries[i].target==target) {
        *result=v->queries[i].result; return VIEW_OK;
    }
    /* Nearby backward byte lookup subtracts exact newline bytes from a cached
     * prefix count. This keeps local movement from replaying a long line. */
    if(kind==Q_BYTE && !exhausted(v)) {
        for(unsigned i=0;i<16;++i) if(v->queries[i].kind==Q_BYTE && v->queries[i].target>target &&
            v->queries[i].target-target<=256u) {
            uint8_t tail[256]; size_t n=(size_t)(v->queries[i].target-target);
            int rc=piece_read(v->tree,target,tail,n); if(rc!=PIECE_OK) return rc;
            uint64_t line=v->queries[i].result; v->scanned+=n; v->line_scanned+=n;
            for(size_t j=0;j<n;++j) if(tail[j]=='\n') --line;
            unsigned j=v->query_next++%16u;
            v->queries[j].kind=kind; v->queries[j].target=target; v->queries[j].result=line;
            *result=line; return VIEW_OK;
        }
    }
    bool begin=v->query_kind!=kind || v->query_target!=target;
    if(begin) {
        v->query_kind=kind; v->query_target=target; v->query_pos=0; v->query_line=0;
        for(unsigned i=0;i<32;++i) if(v->lines[i].valid &&
            (kind==Q_BYTE?v->lines[i].byte<=target:v->lines[i].line<=target) &&
            v->lines[i].byte>=v->query_pos) {
            v->query_pos=v->lines[i].byte; v->query_line=v->lines[i].line;
        }
    }
    {
        for(unsigned i=0;i<16;++i) if(v->queries[i].kind==Q_BYTE &&
            (kind==Q_BYTE?v->queries[i].target<=target:v->queries[i].result<target) &&
            v->queries[i].target>v->query_pos) {
            v->query_pos=v->queries[i].target; v->query_line=v->queries[i].result;
        }
    }
    /* A checkpoint also certifies its logical line membership. It can skip a
     * known line's prefix, but cannot answer a line-start query inside that line.
     * External providers remain suppressed for the entire editing command. */
    if(begin && v->config.checkpoint && !v->edited &&
       (kind==Q_BYTE || v->query_line<target)) {
        uint64_t byte=v->query_pos, col=0, limit=kind==Q_BYTE?target:total;
        int ok=v->config.checkpoint(v->config.checkpoint_ctx,v->tree,v->query_line,limit,UINT64_MAX,&byte,&col);
        if(ok && byte>=v->query_pos && byte<=limit) v->query_pos=byte;
    }
    uint8_t bytes[256];
    while(v->query_pos<total && (kind==Q_BYTE?v->query_pos<target:v->query_line<target)) {
        if(exhausted(v)) return VIEW_MORE;
        uint64_t stop=kind==Q_BYTE?target:total;
        size_t n=(size_t)min_u64(sizeof bytes,stop-v->query_pos);
        int rc=piece_read(v->tree,v->query_pos,bytes,n); if(rc!=PIECE_OK) return rc;
        v->scanned+=n; v->line_scanned+=n;
        for(size_t i=0;i<n;++i) {
            ++v->query_pos;
            if(bytes[i]=='\n') {
                ++v->query_line; line_store(v,v->query_pos,v->query_line);
                if(kind==Q_LINE && v->query_line==target) break;
            }
        }
    }
    *result=kind==Q_BYTE?v->query_line:v->query_pos;
    if(kind==Q_LINE) {
        unsigned j=v->query_next++%16u;
        v->queries[j].kind=Q_BYTE; v->queries[j].target=v->query_pos; v->queries[j].result=v->query_line;
    }
    unsigned i=v->query_next++%16u;
    v->queries[i].kind=kind; v->queries[i].target=target; v->queries[i].result=*result;
    v->query_kind=0; return VIEW_OK;
}
static int line_at(view *v, uint64_t byte, uint64_t *line)
{ return line_query(v,Q_BYTE,byte,line); }
static int line_start(view *v, uint64_t line, uint64_t *byte)
{ return line_query(v,Q_LINE,line,byte); }
static int line_end(view *v, uint64_t line, uint64_t *result)
{
    uint64_t start, end; int rc=line_start(v,line,&start); if(rc!=VIEW_OK) return rc;
    rc=line_start(v,line+1,&end); if(rc!=VIEW_OK) return rc;
    if(end>start && byte_at(v,end-1)=='\n') {
        --end; if(end>start && byte_at(v,end-1)=='\r') --end;
    }
    *result=end; return VIEW_OK;
}
/* edit-ptn: the nearest boundary before x proven from the bytes alone: an
 * adjacent unit pair whose break holds for every left context
 * (utf8_break_certain: ASCII pairs, CJK, emoji, Hangul syllables, after LF).
 * Window (P): the 512 bytes before x (the pre-existing near_seed window)
 * plus up to 3 bytes after it, so a unit starting before x decodes whole.
 * Units are decoded from the window's first non-continuation byte: UTF-8
 * self-synchronizes, so the decomposition there equals the scanner's.
 * Returns 1 with *seed < x, 0 for none, VIEW_MORE when this call's charged
 * budget cannot hold the window (a fresh slice always can: 515 + 264 < 4096). */
static int near_seed(view *v, uint64_t x, uint64_t *seed)
{
    if(x==0) { *seed=0; return 1; }
    uint64_t total=piece_len(v->tree), from=x>512u?x-512u:0, to=min_u64(total,x+3u);
    size_t n=(size_t)(to-from);
    if(v->scanned+n+264u>VIEW_SCAN_BOUND) return VIEW_MORE;
    uint8_t buf[VIEW_WINDOW];
    if(piece_read(v->tree,from,buf,n)!=0) return 0;
    v->scanned+=n; v->seek_scanned+=n;
    size_t i=0, limit=(size_t)(x-from);
    while(i<limit && from && (buf[i]&0xC0u)==0x80u) ++i;
    bool found=false; utf8_step prev={0,0,0}; bool have=false;
    while(i<limit) {
        utf8_step u=utf8_decode(buf+i,n-i);
        if(have && utf8_break_certain(prev,u)) { *seed=from+i; found=true; }
        prev=u; have=true; i+=u.len;
    }
    return found?1:0;
}
static void seek_begin(view *v, unsigned mode, uint64_t from, uint64_t target, unsigned next)
{
    v->seek_mode=mode; v->seek_from=from; v->seek_target=target; v->seek_next=next;
    v->seek_walk=false; v->phase=P_SEEK;
}
/* Certify the start of a nearby ASCII class run, including whitespace to
 * its right. If Unicode or a window edge hides the start, use exact replay. */
static uint64_t word_seed(view *v, uint64_t cursor, uint64_t line_start)
{
    uint64_t from=max_u64(line_start,cursor>512u?cursor-512u:0);
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
    v->cluster_class=-1; v->win_len=0; utf8_cseg_init(&v->cluster); v->seeking=false;
}
/* edit-ptn probe: a boundary repair/seek scan from a seed counts into seek_scanned. */
static void seek_scan(view *v, unsigned mode, uint64_t start, uint64_t end, uint64_t target)
{
    scan_begin(v,mode,start,end,target,0); v->seeking=true;
}
static void seed_store(view *v, uint64_t byte, uint64_t col);
static int column_begin(view *v, unsigned mode, uint64_t line, uint64_t target)
{
    if(v->seed_tab!=v->config.tab_width) {
        for(unsigned i=0;i<16;++i) v->seeds[i].valid=false;
        v->seed_tab=v->config.tab_width;
    }
    uint64_t start, end=mode==S_BYTE_COL?target:piece_len(v->tree), col=0;
    int rc=line_start(v,line,&start); if(rc!=VIEW_OK) return rc;
    v->column_start=start;
    /* A column can have several boundaries around zero-width clusters. Seed
     * strictly before a cell target so vertical motion chooses the first. */
    if(v->config.checkpoint && !v->edited && (mode!=S_COL_BYTE || target>0)) {
        uint64_t seed=start, seed_col=0;
        int ok=v->config.checkpoint(v->config.checkpoint_ctx,v->tree,line,
            mode==S_BYTE_COL?(target>start?target-1:target):UINT64_MAX,
            mode==S_COL_BYTE?target-1:UINT64_MAX,&seed,&seed_col);
        if(ok && seed>=start && seed<=end &&
           (mode==S_BYTE_COL?seed<=target:seed_col<target)) { start=seed; col=seed_col; }
    }
    for(unsigned i=0;i<16;++i) {
        if(v->seeds[i].valid && v->seeds[i].start==v->column_start &&
           v->seeds[i].byte>=start && v->seeds[i].byte<=end &&
           (mode==S_BYTE_COL?v->seeds[i].byte<=target:
            (v->seeds[i].byte==v->column_start || v->seeds[i].col<target))) {
            start=v->seeds[i].byte; col=v->seeds[i].col;
        }
    }
    v->last_seed=start; seed_store(v,start,col);
    scan_begin(v,mode,start,end,target,col); return VIEW_OK;
}
static void seed_store(view *v, uint64_t byte, uint64_t col)
{
    unsigned i=v->seed_next++%16u;
    v->seeds[i].byte=byte; v->seeds[i].col=col;
    v->seeds[i].start=v->column_start; v->seeds[i].valid=true; v->last_seed=byte;
}
static void seeds_edit(view *v, uint64_t off)
{
    queries_invalidate(v,off);
    bool keep=off==0;
    if(off>0 && off<piece_len(v->tree)) {
        uint8_t a=byte_at(v,off-1), b=byte_at(v,off);
        keep=a<128 && b<128 && a!='\r';
    }
    for(unsigned i=0;i<16;++i)
        if(v->seeds[i].byte>off || (v->seeds[i].byte==off && !keep)) v->seeds[i].valid=false;
}
/* Streaming complete-cluster step. Charge consumed bytes plus four bytes of
 * lookahead per completion. Window copies/opaque piece queries are separate. */
static int cluster_next(view *v, int *width)
{
    uint64_t total=piece_len(v->tree);
    for(;;) {
        if(exhausted(v)) return VIEW_MORE;
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
        size_t budget=(size_t)min_u64(256u,VIEW_SCAN_BOUND-v->scanned-256u);
        int rc=utf8_cluster_step(&v->cluster,v->win+at,available,budget,
            v->win_pos+v->win_len==total,&used,width);
        v->scan_pos+=used; v->scanned+=used; if(v->seeking) v->seek_scanned+=used;
        if(rc==UTF8_G_END) { v->scanned+=4; return VIEW_OK; }
        if(rc==UTF8_G_BUDGET) continue;
        /* MORE consumed all complete units; refill at the unconsumed tail. */
        v->win_len=0;
    }
}
static int scan_run(view *v)
{
    for(;;) {
        unsigned mode=v->scan_mode;
        if(v->scan_pos==v->cluster_start) {
            if(mode==S_PREV && v->target==0) { v->result=0; return VIEW_OK; }
            if((mode==S_BYTE_COL || mode==S_CEIL || mode==S_FLOOR) && v->scan_pos>=v->target) {
                v->result=v->scan_pos; v->column=v->scan_col; return 0;
            }
            if(mode==S_COL_BYTE && v->scan_col>=v->target) { v->result=v->scan_pos; return 0; }
            if(v->scan_pos>=v->end) {
                if(mode!=S_WORD_LEFT) v->result=v->scan_pos;
                v->column=v->scan_col; return 0;
            }
        }
        int width=0, rc=cluster_next(v,&width);
        if(rc!=0) return rc;
        uint64_t start=v->cluster_start, next=v->scan_pos;
        int cls=v->cluster_class;
        if(cls==C_NEWLINE && (mode==S_COL_BYTE || mode==S_WORD_SELECT)) {
            uint8_t first=byte_at(v,start);
            bool terminator=first=='\n' || (first=='\r' && byte_at(v,start+1)=='\n');
            if(terminator) { v->result=start; v->column=v->scan_col; return VIEW_OK; }
        }
        uint64_t cells=(uint64_t)width;
        if(cls==C_SPACE && byte_at(v,start)=='\t') cells=v->config.tab_width-v->scan_col%v->config.tab_width;
        if(mode==S_NEXT) { v->result=next; return 0; }
        if(mode==S_PREV) {
            v->result=start; if(next>=v->target) return 0;
        } else if(mode==S_CEIL) {
            if(next>=v->target) { v->result=next; return 0; }
        } else if(mode==S_FLOOR) {
            if(next>=v->target) { v->result=next==v->target?next:start; return 0; }
        } else if(mode==S_COL_BYTE) {
            if(cells>v->target-v->scan_col) { v->result=start; return 0; }
        } else if(mode==S_HOME) {
            if(cls!=C_SPACE) { v->result=start; return 0; }
        } else if(mode==S_WORD_RIGHT) {
            if(cls==C_NEWLINE && start==v->origin) { v->result=next; return VIEW_OK; }
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
        if((mode==S_BYTE_COL || mode==S_COL_BYTE) && next-v->last_seed>=256u) seed_store(v,next,v->scan_col);
        if(exhausted(v)) return VIEW_MORE;
    }
}
static void set_cursor(view *v, uint64_t target)
{
    v->state.selection.cursor=target;
    if(!v->shift) v->state.selection.anchor=target;
}
/* (P) reuse line_query's byte quantum: exhausted reserves this read plus
 * grapheme lookahead inside VIEW_SCAN_BOUND. No estimated ordinal is stored. */
static int relative_start(view *v, uint64_t *start)
{
    uint8_t bytes[256];
    while(v->relative_pos) {
        if(exhausted(v)) return VIEW_MORE;
        size_t n=(size_t)min_u64(sizeof bytes,v->relative_pos);
        uint64_t from=v->relative_pos-n;
        int rc=piece_read(v->tree,from,bytes,n); if(rc) return rc;
        v->scanned+=n; v->seek_scanned+=n;
        for(size_t i=n;i;i--) if(bytes[i-1u]=='\n') {
            *start=from+i; return VIEW_OK;
        }
        v->relative_pos=from;
    }
    *start=0; return VIEW_OK;
}
static int relative_rows(view *v)
{
    bool up=v->key==VIEW_UP || v->key==VIEW_PAGE_UP;
    uint64_t total=piece_len(v->tree);
    uint8_t bytes[256]; /* (P) same query quantum as relative_start */
    while(v->relative_rows) {
        if(up) {
            if(!v->relative_start) { v->relative_rows=0; break; }
            uint64_t start;
            int rc=relative_start(v,&start); if(rc) return rc;
            v->relative_start=start;
            v->relative_pos=start?start-1u:0; /* exclude this line's LF */
        } else {
            bool found=false;
            while(v->relative_pos<total && !found) {
                if(exhausted(v)) return VIEW_MORE;
                size_t n=(size_t)min_u64(sizeof bytes,total-v->relative_pos);
                int rc=piece_read(v->tree,v->relative_pos,bytes,n); if(rc) return rc;
                v->scanned+=n; v->seek_scanned+=n;
                for(size_t i=0;i<n;i++) if(bytes[i]=='\n') {
                    v->relative_start=v->relative_pos+i+1u;
                    v->relative_pos=v->relative_start; found=true; break;
                }
                if(!found) v->relative_pos+=n;
            }
            if(!found) { v->relative_rows=0; break; }
        }
        v->relative_boundary=false; --v->relative_rows;
    }
    if(v->relative_boundary) {
        v->result=up?0:total; v->phase=P_APPLY;
    } else {
        v->column_start=v->relative_start;
        scan_begin(v,S_COL_BYTE,v->relative_start,total,v->state.selection.preferred_col,0);
        v->phase=P_REL_DEST;
    }
    return VIEW_OK;
}
/* First LF at or after relative_pos: *lf is its position, or total. */
static int relative_next(view *v, uint64_t *lf, bool *found)
{
    uint64_t total=piece_len(v->tree);
    uint8_t bytes[256]; /* (P) same query quantum as relative_start */
    while(v->relative_pos<total) {
        if(exhausted(v)) return VIEW_MORE;
        size_t n=(size_t)min_u64(sizeof bytes,total-v->relative_pos);
        int rc=piece_read(v->tree,v->relative_pos,bytes,n); if(rc) return rc;
        v->scanned+=n; v->seek_scanned+=n;
        for(size_t i=0;i<n;i++) if(bytes[i]=='\n') { *lf=v->relative_pos+i; *found=true; return VIEW_OK; }
        v->relative_pos+=n;
    }
    *lf=total; *found=false; return VIEW_OK;
}
/* edit-ptn: line metadata for Home/End/word/select keys proven by LF walks
 * around the cursor (byte-relative), equal to line_at/line_start/line_end. */
static int relative_meta(view *v)
{
    uint64_t pos=0; bool found=false; int rc;
    for(;;) switch(v->meta_stage) {
    case 0: /* start of the cursor's line (lo's for SELECT_LINE) */
        rc=relative_start(v,&pos); if(rc) return rc;
        v->home_start=pos; v->meta_lo=pos;
        if(v->key==VIEW_SELECT_LINE) { v->relative_pos=v->hi; v->meta_stage=1; }
        else if(v->key==VIEW_END) { v->relative_pos=v->origin; v->meta_stage=3; }
        else { v->phase=P_SETUP; return VIEW_OK; }
        break;
    case 1: /* start of hi's line; hi at a line start ends the previous one */
        rc=relative_start(v,&pos); if(rc) return rc;
        if(v->hi>v->lo && v->hi==pos) { v->meta_hi=v->hi; v->meta_stage=4; }
        else { v->relative_pos=v->hi; v->meta_stage=2; }
        break;
    case 2: case 5: /* next line start after relative_pos */
        rc=relative_next(v,&pos,&found); if(rc) return rc;
        v->meta_hi=found?pos+1u:pos;
        if(v->meta_stage==2) v->meta_stage=4;
        else { v->phase=P_SETUP; return VIEW_OK; }
        break;
    case 3: /* END: the cursor line's end before LF/CRLF */
        rc=relative_next(v,&pos,&found); if(rc) return rc;
        if(found && pos>v->home_start && byte_at(v,pos-1u)=='\r') --pos;
        v->meta_end=pos; v->phase=P_SETUP; return VIEW_OK;
    case 4: /* whole lines already selected: extend by one more line */
        if(v->hi>v->lo && v->lo==v->meta_lo && v->hi==v->meta_hi) { v->relative_pos=v->meta_hi; v->meta_stage=5; }
        else { v->phase=P_SETUP; return VIEW_OK; }
        break;
    default: return VIEW_ERR_ARG;
    }
}
static int follow_line(view *v)
{
    uint64_t line, first; int rc=line_at(v,v->state.selection.cursor,&line); if(rc!=VIEW_OK) return rc;
    if(line<v->state.first_line) first=line;
    else if(line-v->state.first_line>=v->config.rows) first=line-v->config.rows+1;
    else first=v->state.first_line;
    uint64_t byte; rc=line_start(v,first,&byte); if(rc!=VIEW_OK) return rc;
    v->state.first_line=first; v->state.first_byte=byte; v->line=line; return VIEW_OK;
}
/* edit-wv7: wrapped placement with an inexact external line count. The
 * exact path (finish_move below) labels the new top with line_at/line_start,
 * a line query from the nearest certified anchor: byte 0 on an unindexed
 * lazy file, so Ctrl+End counted the whole file. Here `start` is the
 * cursor's line start, proven by walking back to LF (P_REL_WRAP). The row
 * query stays local: resident rows/seed when the cursor lies within layout's
 * own budget of its certified top (no piece line query, as resident_seed),
 * else anchored at `start` (layout_visual_row_anchored: no whole-source piece
 * line query). The top's line start is the chosen row's certified
 * line_start; its ordinal is unknown unless it is byte 0. The previous
 * first_line stays as a label, approximate is set and relative_top tells the
 * host to relabel (jf9: no estimate enters the view's exact caches). */
static int relative_wrap_place(view *v, uint64_t start)
{
    const layout *l=v->wrap_layout;
    uint64_t cursor=v->state.selection.cursor, total=piece_len(v->tree);
    uint64_t visual=v->state.visual_byte;
    v->state.hscroll=0;
    layout_wrap_row r; bool approx=false;
    bool resident=l->src==v->tree && l->total==total && l->first_line_valid &&
        cursor>=l->first_byte && cursor-l->first_byte<=LAYOUT_BYTE_BUDGET;
    uint64_t label=v->state.first_line==LAYOUT_LINE_UNKNOWN?0:v->state.first_line;
    int query=!resident?layout_visual_row_anchored(l,v->tree,cursor,start,label,&r,&approx):
        v->edited?layout_visual_row_fresh(l,v->tree,cursor,&r,&approx):
        layout_visual_row(l,v->tree,cursor,0,&r,&approx);
    if(query==LAYOUT_DONE) {
        uint64_t top_start=v->state.first_byte;
        if(cursor<v->state.visual_byte) { v->state.visual_byte=r.start; top_start=r.line_start; }
        uint32_t n=l->grid->dims.rows;
        uint64_t bottom=l->wrap_rows[n-1].next;
        if(bottom!=LAYOUT_VOID_ROW && cursor>=bottom && !(cursor==bottom && v->state.visual_end)) {
            layout_wrap_row top; bool top_approx=false;
            if(!v->edited && r.start==bottom &&
               layout_visual_row(l,v->tree,v->state.visual_byte,1,&top,&top_approx)==LAYOUT_DONE) {
                v->state.visual_byte=top.start; top_start=top.line_start; approx|=top_approx;
            } else { v->state.visual_byte=r.start; top_start=r.line_start; }
        }
        v->state.approximate|=approx;
        if(v->state.visual_byte==visual || top_start==v->state.first_byte) {
            /* Same logical top: keep its label and its estimate marker. */
            v->state.approximate|=v->before_state.approximate;
        } else if(top_start==0) {
            v->state.first_byte=0; v->state.first_line=0;
        } else {
            v->state.first_byte=top_start; v->state.approximate=true; v->relative_top=true;
        }
    }
    v->busy=false; return VIEW_OK;
}
/* edit-6090, edit-cxho.25: after a mutation the cached rows are stale from
 * the edited line on, so `cursor>=bottom` (the pre-edit end of the last row)
 * cannot tell a caret still on screen (a letter typed at the end of the
 * bottom row's line reaches its old end), one row below (a Return) or a few
 * rows below (a paste) from a far jump; each was followed directly, the
 * caret's row becoming the top row. layout_wrap_follow_edit decides from the
 * fresh rows: keep the top, or the minimal top with the caret on the bottom
 * row, or (farther) the caret's row. The caret's row honours its trailing
 * affinity at a soft row end. */
static void fresh_follow(view *v,uint64_t cursor,const layout_wrap_row *r,bool *approx)
{
    const layout *l=v->wrap_layout; layout_wrap_row c=*r, top; bool moved=false;
    if(v->state.visual_end && cursor && c.start==cursor && c.continuation &&
       layout_visual_row_fresh(l,v->tree,cursor-1u,&c,approx)!=LAYOUT_DONE) c=*r;
    if(c.start<v->state.visual_byte) { v->state.visual_byte=c.start; return; }
    /* An edit scrolls minimally while the caret is less than a screen below
     * (Return bursts, a paste); a motion over a rebuilding layout keeps the
     * one-row rule of the cached path and follows farther targets directly. */
    uint32_t n=l->grid->dims.rows, scroll=v->edited ? (n ? n-1u : 0u) : 1u;
    int rc=layout_wrap_follow_edit(l,v->tree,v->state.visual_byte,&c,scroll,&top,&moved,approx);
    if(rc!=LAYOUT_DONE) v->state.visual_byte=c.start; /* unproven: keep the caret's row visible */
    else if(moved) v->state.visual_byte=top.start;
}
static int finish_move(view *v)
{
    if(v->state.wrap && v->wrap_layout && v->relative) {
        /* edit-wv7: prove the cursor's line start first (P_REL_WRAP). */
        v->relative_pos=v->state.selection.cursor; v->phase=P_REL_WRAP;
        return VIEW_OK;
    }
    if(v->state.wrap && v->wrap_layout) {
        v->state.hscroll=0;
        uint64_t cursor=v->state.selection.cursor;
        layout_wrap_row r; bool approx=false;
        /* edit-cxho.25: a busy layout's rows are being rebuilt (a restart
         * zeroes them), so a motion keyed before the frame (key repeat after
         * an edit) is followed from the fresh rows as an edit is. */
        bool fresh=v->edited || layout_busy(v->wrap_layout);
        int query=fresh?layout_visual_row_fresh(v->wrap_layout,v->tree,cursor,&r,&approx):
            layout_visual_row(v->wrap_layout,v->tree,cursor,0,&r,&approx);
        if(query==LAYOUT_DONE) {
            uint32_t n=v->wrap_layout->grid->dims.rows;
            uint64_t bottom=v->wrap_layout->wrap_rows[n-1].next;
            /* edit-cxho.25: with trailing affinity at a soft row end the
             * caret shows on the row above its byte's row; follow that row
             * (Down onto such an end below the bottom row re-anchored the
             * top at the byte's row, the caret hidden above it). */
            layout_wrap_row c=r;
            if(!fresh && v->state.visual_end && cursor && r.start==cursor && r.continuation &&
               layout_visual_row(v->wrap_layout,v->tree,cursor-1u,0,&c,&approx)!=LAYOUT_DONE) c=r;
            if(fresh) fresh_follow(v,cursor,&r,&approx);
            else if(c.start<v->state.visual_byte) v->state.visual_byte=c.start;
            /* Cached viewport row membership avoids logical-line scrolling. */
            else if(bottom!=LAYOUT_VOID_ROW && c.start>=bottom) {
                /* One-row downward motion exposes one row and advances top
                 * by one visual row. Larger jumps follow the target directly. */
                layout_wrap_row top;
                if(c.start==bottom &&
                   layout_visual_row(v->wrap_layout,v->tree,v->state.visual_byte,1,&top,&approx)==LAYOUT_DONE)
                    v->state.visual_byte=top.start;
                else v->state.visual_byte=c.start;
            }
            uint64_t line, byte;
            int rc=line_at(v,v->state.visual_byte,&line); if(rc!=VIEW_OK) return rc;
            rc=line_start(v,line,&byte); if(rc!=VIEW_OK) return rc;
            v->state.first_line=line; v->state.first_byte=byte;
            v->state.approximate|=approx;
        }
        v->busy=false; return 0;
    }
    if(v->relative) {
        v->relative_pos=v->state.selection.cursor; v->phase=P_REL_FOLLOW;
        return VIEW_OK;
    }
    int rc=follow_line(v); if(rc!=VIEW_OK) return rc;
    rc=column_begin(v,S_BYTE_COL,v->line,v->state.selection.cursor); if(rc!=VIEW_OK) return rc;
    v->phase=P_FOLLOW;
    return 0;
}
static void fallback(view *v)
{
    bool wrap=v->state.wrap;
    v->state=(view_state){.selection={0,0,VIEW_PREFERRED_UNSET},.wrap=wrap};
}
static void bulk_begin(view *v, uint64_t lo, uint64_t old, size_t len)
{
    v->bulk_offset=lo+(uint64_t)len; v->bulk_remaining=old;
    v->bulk_target=v->bulk_offset; v->pending_len=len;
    v->bulk_insert=false; v->phase=P_BULK;
}
static int bulk_run(view *v, view_change *change)
{
    if(v->bulk_insert) {
        int rc=insert_bytes(v,v->bulk_offset-v->pending_len,v->pending_text,v->pending_len);
        if(rc!=VIEW_OK) return rc;
        *change=(view_change){v->bulk_offset-v->pending_len,0,v->pending_len,true};
        v->bulk_insert=false; v->edited=true; v->scanned+=v->pending_len;
        seeds_edit(v,change->offset);
    }
    uint64_t count=min_u64(v->bulk_remaining,VIEW_MUTATION_BOUND);
    count=min_u64(count,VIEW_SCAN_BOUND-v->scanned-896u);
    int rc=delete_bytes(v,v->bulk_offset,count); if(rc!=VIEW_OK) return rc;
    if(change->changed) change->old_len=count;
    else *change=(view_change){v->bulk_offset,count,0,true};
    v->scanned+=count; v->bulk_remaining-=count; v->edited=true;
    seeds_edit(v,change->offset); v->shift=false; fallback(v);
    if(v->bulk_remaining==0) {
        seek_begin(v,S_CEIL,v->bulk_target,v->bulk_target,P_REPAIR);
    }
    /* One bounded capture per call; no decoding or metadata replay on top. */
    return VIEW_MORE;
}
static int remove_range(view *v, uint64_t lo, uint64_t hi, view_change *change)
{
    if(hi==lo) { v->busy=false; return VIEW_OK; }
    if(hi-lo>VIEW_MUTATION_BOUND) { bulk_begin(v,lo,hi-lo,0); v->phase=P_BULK_PREPARE; return VIEW_MORE; }
    if(hi>lo) {
        int rc=delete_bytes(v,lo,hi-lo); if(rc!=0) return rc;
        *change=(view_change){lo,hi-lo,0,true};
        v->edited=true;
        seeds_edit(v,lo);
    }
    v->state.selection.preferred_col=VIEW_PREFERRED_UNSET;
    v->shift=false;
    /* Joining two clusters can destroy the old cursor boundary. */
    fallback(v);
    seek_begin(v,S_CEIL,lo,lo,P_REPAIR); return 0;
}
bool view_wrap_default(const char *path)
{
    if(!path || !*path) return true;
    const char *dot=strrchr(path,'.');
    return dot && (!strcmp(dot,".md") || !strcmp(dot,".txt") || !strcmp(dot,".tex"));
}
int view_set_wrap(view *v,bool enabled,struct layout *context)
{
    if(!v || v->busy || (enabled && (!context || !context->wrap))) return VIEW_ERR_ARG;
    v->state.wrap=enabled; v->wrap_layout=context;
    v->state.hscroll=0; v->state.visual_byte=v->state.first_byte; v->state.visual_end=false;
    v->state.selection.preferred_col=VIEW_PREFERRED_UNSET;
    return VIEW_OK;
}
int view_set_tab_width(view *v, uint32_t width)
{
    if (!v || !width) return VIEW_ERR_ARG;
    if (v->busy) return VIEW_ERR_BUSY;
    v->config.tab_width = width;
    for (size_t i = 0; i < sizeof v->seeds / sizeof v->seeds[0]; i++) v->seeds[i].valid = false;
    v->state.selection.preferred_col = VIEW_PREFERRED_UNSET;
    v->completed_state = v->state;
    return VIEW_OK;
}
int view_wrap_file(view *v,struct layout *context,const char *path)
{
    if(!v || !context || v->busy) return VIEW_ERR_ARG;
    bool enabled=view_wrap_default(path);
    if(layout_set_wrap(context,enabled)!=LAYOUT_DONE) return VIEW_ERR_ARG;
    return view_set_wrap(v,enabled,context);
}
static int visual_command(view *v,view_key key)
{
    layout_wrap_row current,dest; bool approximate=false;
    int rc=layout_visual_row(v->wrap_layout,v->tree,v->origin,0,&current,&approximate);
    if(rc<0) return VIEW_ERR_ARG;
    v->state.approximate|=approximate;
    /* Preferred-column discovery re-enters here after clearing the destination
     * affinity. Resolve the origin from the immutable command-start state. */
    if(v->before_state.visual_end && current.start==v->origin && v->origin) {
        layout_wrap_row previous;
        if(layout_visual_row(v->wrap_layout,v->tree,v->origin,-1,&previous,&approximate)==LAYOUT_DONE &&
           !previous.newline && previous.next==v->origin) current=previous;
        v->state.approximate|=approximate;
    }
    v->state.visual_end=false; v->wrap_target_soft=false;
    bool up=key==VIEW_UP || key==VIEW_PAGE_UP;
    v->home_start=current.start;
    if(key==VIEW_HOME) {
        v->phase=P_MOVE;
        /* Smart Home on the first row; continuation Home is its first cluster. */
        v->wrap_target_soft=!current.newline && current.next!=LAYOUT_VOID_ROW;
        scan_begin(v,S_HOME,current.start,current.end,0,current.column);
        if(current.continuation) { v->result=current.start; v->phase=P_APPLY; }
        return VIEW_OK;
    }
    if(key==VIEW_END) { v->result=current.end; v->state.visual_end=!current.newline && current.next!=LAYOUT_VOID_ROW; v->phase=P_APPLY; return VIEW_OK; }
    if(v->state.selection.preferred_col==VIEW_PREFERRED_UNSET) {
        scan_begin(v,S_BYTE_COL,current.start,v->origin,v->origin,current.column);
        v->visual_column=current.column; v->visual_indent=current.continuation?current.indent:0;
        v->phase=P_VISUAL_COL; return VIEW_OK;
    }
    dest=current;
    uint32_t count=(key==VIEW_PAGE_UP || key==VIEW_PAGE_DOWN)?v->config.rows:1;
    for(uint32_t i=0;i<count;i++) {
        layout_wrap_row next;
        rc=layout_visual_row(v->wrap_layout,v->tree,dest.start,up?-1:1,&next,&approximate);
        if(rc<0) return VIEW_ERR_ARG;
        v->state.approximate|=approximate;
        if(next.start==dest.start) { v->result=approximate?dest.start:up?0:piece_len(v->tree); v->phase=P_APPLY; return VIEW_OK; }
        dest=next;
    }
    uint64_t preferred=v->state.selection.preferred_col;
    uint32_t indent=dest.continuation?dest.indent:0;
    uint64_t target=dest.column+(preferred>indent?preferred-indent:0);
    uint64_t end=dest.end; v->wrap_target_soft=!dest.newline && dest.next!=LAYOUT_VOID_ROW;
    scan_begin(v,S_COL_BYTE,dest.start,end,target,dest.column);
    v->phase=P_VERTICAL_DEST;
    return VIEW_OK;
}
static bool deletion(view_key key) { return key>=VIEW_BACKSPACE && key<=VIEW_WORD_DELETE; }
static bool vertical(view_key key) { return key>=VIEW_UP && key<=VIEW_PAGE_DOWN; }
static void repair_begin(view *v, uint64_t target, bool before, unsigned next)
{
    seek_begin(v,before?S_FLOOR:S_CEIL,target,target,next);
}
static int setup_command(view *v, view_change *change)
{
    view_key key=v->key; bool shift=v->shift;
    uint64_t total=piece_len(v->tree), cursor=v->origin, anchor=v->before_state.selection.anchor;
    uint64_t start=0, end=total;
    bool metadata=vertical(key) || key==VIEW_HOME || key==VIEW_END || key==VIEW_SELECT_WORD ||
        key==VIEW_WORD_LEFT || key==VIEW_WORD_RIGHT || key==VIEW_WORD_BACKSPACE || key==VIEW_WORD_DELETE;
    if(metadata && v->relative) {
        start=v->home_start; if(key==VIEW_END) end=v->meta_end;
    } else if(metadata && !(v->state.wrap && v->wrap_layout && (vertical(key) || key==VIEW_HOME || key==VIEW_END))) {
        int rc=line_at(v,cursor,&v->line); if(rc!=VIEW_OK) return rc;
        rc=line_start(v,v->line,&start); if(rc!=VIEW_OK) return rc;
        bool need_end=key==VIEW_END;
        if(need_end) { rc=line_end(v,v->line,&end); if(rc!=VIEW_OK) return rc; }
        v->home_start=start;
    }
    if(!vertical(key)) v->state.selection.preferred_col=VIEW_PREFERRED_UNSET;
    if(v->state.wrap && v->wrap_layout && (vertical(key) || key==VIEW_HOME || key==VIEW_END)) {
        return visual_command(v,key);
    } else if(deletion(key) && cursor!=anchor) {
        int rc=remove_range(v,v->lo,v->hi,change); if(rc!=0) return rc;
    } else if((key==VIEW_LEFT || key==VIEW_RIGHT) && cursor!=anchor && !shift) {
        v->result=key==VIEW_LEFT?v->lo:v->hi; v->phase=P_APPLY;
    } else if(key==VIEW_LEFT || key==VIEW_BACKSPACE) {
        seek_begin(v,S_PREV,cursor,cursor,P_MOVE);
    } else if(key==VIEW_RIGHT || key==VIEW_DELETE) {
        scan_begin(v,S_NEXT,cursor,total,0,0);
    } else if(key==VIEW_WORD_LEFT || key==VIEW_WORD_BACKSPACE) {
        if(cursor==start && cursor>0) seek_begin(v,S_PREV,cursor-1,cursor,P_MOVE);
        else scan_begin(v,S_WORD_LEFT,word_seed(v,cursor,start),cursor,cursor,0);
    } else if(key==VIEW_WORD_RIGHT || key==VIEW_WORD_DELETE) {
        if(cursor>=end) scan_begin(v,S_NEXT,cursor,total,0,0);
        else scan_begin(v,S_WORD_RIGHT,cursor,end,0,0);
    } else if(key==VIEW_HOME) scan_begin(v,S_HOME,start,end,0,0);
    else if(key==VIEW_END || key==VIEW_DOC_HOME || key==VIEW_DOC_END) {
        v->result=key==VIEW_END?end:(key==VIEW_DOC_HOME?0:total); v->phase=P_APPLY;
    } else if(key==VIEW_SELECT_ALL) {
        v->state.selection.anchor=0; v->state.selection.cursor=total; v->phase=P_FINISH;
    } else if(key==VIEW_SELECT_LINE && v->relative) {
        v->state.selection.anchor=v->meta_lo; v->state.selection.cursor=v->meta_hi; v->phase=P_FINISH;
    } else if(key==VIEW_SELECT_LINE) {
        uint64_t a, b, lo, hi, bs;
        int rc=line_at(v,v->lo,&a); if(rc!=VIEW_OK) return rc;
        rc=line_at(v,v->hi,&b); if(rc!=VIEW_OK) return rc;
        rc=line_start(v,b,&bs); if(rc!=VIEW_OK) return rc;
        if(v->hi>v->lo && v->hi==bs) --b;
        rc=line_start(v,a,&lo); if(rc!=VIEW_OK) return rc;
        rc=line_start(v,b+1,&hi); if(rc!=VIEW_OK) return rc;
        if(v->hi>v->lo && v->lo==lo && v->hi==hi) { rc=line_start(v,b+2,&hi); if(rc!=VIEW_OK) return rc; }
        v->state.selection.anchor=lo; v->state.selection.cursor=hi; v->phase=P_FINISH;
    } else if(key==VIEW_SELECT_WORD) {
        uint8_t first=byte_at(v,cursor);
        bool eol=first=='\n' || (first=='\r' && byte_at(v,cursor+1)=='\n');
        if(cursor!=anchor || cursor>=end || eol) { v->result=cursor; v->phase=P_APPLY; v->shift=true; }
        else { v->lo=cursor; v->hi=cursor; scan_begin(v,S_WORD_SELECT,word_seed(v,cursor,start),end,cursor,0); }
    } else if(vertical(key)) {
        uint64_t delta=(key==VIEW_PAGE_UP || key==VIEW_PAGE_DOWN)?v->config.rows:1;
        bool up=key==VIEW_UP || key==VIEW_PAGE_UP;
        uint64_t dest=up?(v->line>delta?v->line-delta:0):v->line+delta, dest_byte;
        int rc=line_start(v,dest,&dest_byte); if(rc!=VIEW_OK) return rc;
        rc=line_at(v,dest_byte,&dest); if(rc!=VIEW_OK) return rc;
        if(key==VIEW_PAGE_UP || key==VIEW_PAGE_DOWN) {
            v->state.first_line=up?(v->before_state.first_line>delta?v->before_state.first_line-delta:0):
                min_u64(dest,v->before_state.first_line+delta);
        }
        bool boundary=(up && v->line==0) || (!up && dest==v->line);
        uint64_t preferred=v->state.selection.preferred_col;
        if(preferred==VIEW_PREFERRED_UNSET) {
            rc=column_begin(v,S_BYTE_COL,v->line,cursor); if(rc!=VIEW_OK) return rc; v->phase=P_VERTICAL_COL;
        } else { rc=column_begin(v,S_COL_BYTE,dest,preferred); if(rc!=VIEW_OK) return rc; v->phase=P_VERTICAL_DEST; }
        v->line=dest;
        if(boundary) {
            /* First/last line: keep preferred, move to document boundary. */
            if(preferred==VIEW_PREFERRED_UNSET) v->phase=P_VERTICAL_BOUNDARY;
            else { v->result=up?0:total; v->phase=P_APPLY; }
        }
    }
    if(v->phase==P_SETUP) v->phase=P_MOVE;
    return VIEW_OK;
}
static int advance(view *v, view_change *change)
{
    for(;;) {
        if(v->phase==P_REL_START || v->phase==P_REL_LEFT_START || v->phase==P_REL_FOLLOW) {
            uint64_t start;
            int rc=relative_start(v,&start); if(rc) return rc;
            v->column_start=start;
            if(v->phase==P_REL_FOLLOW) {
                scan_begin(v,S_BYTE_COL,start,v->state.selection.cursor,v->state.selection.cursor,0);
                v->phase=P_FOLLOW;
            } else if(vertical(v->key)) {
                v->relative_start=start; v->relative_boundary=true;
                if(v->state.selection.preferred_col==VIEW_PREFERRED_UNSET) {
                    scan_begin(v,S_BYTE_COL,start,v->origin,v->origin,0); v->phase=P_REL_COL;
                } else {
                    v->relative_pos=(v->key==VIEW_UP || v->key==VIEW_PAGE_UP)?(start?start-1u:0):start;
                    v->phase=P_REL_ROWS;
                }
            } else if(v->origin!=v->before_state.selection.anchor && !v->shift) {
                v->result=v->key==VIEW_LEFT?v->lo:v->hi; v->phase=P_APPLY;
            } else {
                if(v->phase==P_REL_START && v->key==VIEW_LEFT && v->origin==start && start) {
                    v->relative_pos=start-1u; v->phase=P_REL_LEFT_START; continue;
                }
                scan_begin(v,v->key==VIEW_LEFT?S_PREV:S_NEXT,
                    v->key==VIEW_LEFT?start:v->origin,piece_len(v->tree),v->origin,0);
                v->phase=P_MOVE;
            }
            continue;
        }
        if(v->phase==P_SEEK) {
            /* edit-ptn: never byte 0 unless it is the nearest proof. A target
             * at EOF/BOF is itself a boundary; else the local window; else
             * walk back to the previous LF (resumable, certified line start). */
            uint64_t total=piece_len(v->tree), seed=0;
            if(!v->seek_walk) {
                if(v->seek_mode!=S_PREV && (v->seek_target==0 || v->seek_target==total)) seed=v->seek_target;
                else {
                    int found=near_seed(v,v->seek_from,&seed);
                    if(found==VIEW_MORE) return VIEW_MORE;
                    if(!found) { v->seek_walk=true; v->relative_pos=v->seek_from>512u?v->seek_from-512u:0; }
                }
            }
            if(v->seek_walk) { int rc=relative_start(v,&seed); if(rc) return rc; }
            seek_scan(v,v->seek_mode,seed,total,v->seek_target); v->phase=v->seek_next;
            continue;
        }
        if(v->phase==P_REL_META) {
            int rc=relative_meta(v); if(rc) return rc;
            continue;
        }
        if(v->phase==P_REL_WRAP) {
            uint64_t start;
            int rc=relative_start(v,&start); if(rc) return rc;
            rc=relative_wrap_place(v,start); if(rc) return rc;
            if(!v->busy) return VIEW_OK;
            continue;
        }
        if(v->phase==P_REL_ROWS) {
            int rc=relative_rows(v); if(rc) return rc;
            continue;
        }
        if(v->phase==P_BULK_PREPARE) {
            uint64_t lo=v->bulk_offset-v->pending_len;
            int rc=line_at(v,lo,&v->line); if(rc!=VIEW_OK) return rc;
            rc=column_begin(v,S_BYTE_COL,v->line,lo); if(rc!=VIEW_OK) return rc;
            v->phase=P_BULK_COLUMN; continue;
        }
        if(v->phase==P_BULK) {
            uint64_t insertion=v->bulk_insert?v->pending_len:0;
            if(v->scanned+insertion+896u>=VIEW_SCAN_BOUND || exhausted(v)) return VIEW_MORE;
            return bulk_run(v,change);
        }
        if(v->phase==P_SETUP) {
            int rc=setup_command(v,change); if(rc!=VIEW_OK) return rc;
            if(!v->busy) return VIEW_OK;
            continue;
        }
        if(v->phase==P_FINISH) {
            int rc=finish_move(v); if(rc!=VIEW_OK) return rc;
            if(!v->busy) return VIEW_OK;
            continue;
        }
        if(v->phase==P_NORMAL_SETUP && v->relative) {
            /* edit-dz0: the host's first_line is a label, not a certified
             * ordinal (inexact external count). Prove only the top's line
             * start by walking back to LF; never count from byte 0. */
            uint64_t start;
            int rc=relative_start(v,&start); if(rc!=VIEW_OK) return rc;
            v->restore_state.first_byte=start; v->restore_state.approximate=true;
            repair_begin(v,v->restore_state.selection.cursor,v->cursor_before,P_NORMAL_CURSOR);
            continue;
        }
        if(v->phase==P_NORMAL_SETUP) {
            int rc=line_at(v,min_u64(v->restore_state.first_byte,piece_len(v->tree)),&v->restore_state.first_line);
            if(rc!=VIEW_OK) return rc;
            rc=line_start(v,v->restore_state.first_line,&v->restore_state.first_byte); if(rc!=VIEW_OK) return rc;
            repair_begin(v,v->restore_state.selection.cursor,v->cursor_before,P_NORMAL_CURSOR);
            continue;
        }
        if(v->phase==P_VISUAL_SETUP) {
            int rc=visual_command(v,v->key); if(rc!=VIEW_OK) return rc;
            continue;
        }

        if(v->phase==P_APPLY) {
            set_cursor(v,v->result); v->phase=P_FINISH; continue;
        }
        int rc=scan_run(v); if(rc!=0) return rc;
        if(v->phase==P_REL_COL) {
            v->state.selection.preferred_col=v->column;
            uint64_t start=v->relative_start;
            v->relative_pos=(v->key==VIEW_UP || v->key==VIEW_PAGE_UP)?(start?start-1u:0):start;
            v->phase=P_REL_ROWS;
        } else if(v->phase==P_REL_DEST) {
            v->phase=P_APPLY;
        } else if(v->phase==P_BULK_COLUMN) {
            seed_store(v,v->bulk_offset-v->pending_len,v->column); v->phase=P_BULK;
        } else if(v->phase==P_VISUAL_COL) {
            uint64_t col=v->column-v->visual_column+v->visual_indent;
            v->state.selection.preferred_col=min_u64(col,v->wrap_layout->text_cols);
            v->phase=P_VISUAL_SETUP;
        } else if(v->phase==P_NORMAL_CURSOR) {
            v->restore_state.selection.cursor=v->result;
            repair_begin(v,v->restore_state.selection.anchor,v->anchor_before,P_NORMAL_ANCHOR);
        } else if(v->phase==P_NORMAL_ANCHOR) {
            v->restore_state.selection.anchor=v->result;
            v->state=v->restore_state;
            v->phase=P_FINISH;
        } else if(v->phase==P_VERTICAL_BOUNDARY) {
            v->state.selection.preferred_col=v->column;
            v->result=(v->key==VIEW_UP || v->key==VIEW_PAGE_UP)?0:piece_len(v->tree);
            v->phase=P_APPLY;
        } else if(v->phase==P_VERTICAL_COL) {
            v->state.selection.preferred_col=v->column;
            rc=column_begin(v,S_COL_BYTE,v->line,v->state.selection.preferred_col);
            if(rc!=VIEW_OK) return rc;
            v->phase=P_VERTICAL_DEST;
        } else if(v->phase==P_VERTICAL_DEST) {
            if(v->state.wrap) v->state.visual_end=v->wrap_target_soft && v->result==v->end;
            v->phase=P_APPLY;
        } else if(v->phase==P_MOVE) {
            if(deletion(v->key)) {
                rc=remove_range(v,min_u64(v->origin,v->result),max_u64(v->origin,v->result),change);
                if(rc!=0) return rc;
                if(!v->busy) return VIEW_OK;
            } else if(v->key==VIEW_SELECT_WORD) {
                v->state.selection.anchor=v->lo; v->state.selection.cursor=v->hi;
                v->phase=P_FINISH; if(!v->busy) return 0;
            } else {
                if(v->key==VIEW_HOME && v->result==v->origin) v->result=v->home_start;
                if(v->state.wrap && v->key==VIEW_HOME) v->state.visual_end=v->wrap_target_soft && v->result==v->end;
                v->phase=P_APPLY;
            }
        } else if(v->phase==P_REPAIR) {
            v->state.selection.cursor=v->result; v->state.selection.anchor=v->result;
            v->phase=P_FINISH; if(!v->busy) return 0;
        } else if(v->phase==P_FOLLOW) {
            uint64_t col=v->column;
            seed_store(v,v->state.selection.cursor,col);
            if(col<v->state.hscroll) v->state.hscroll=col;
            else if(col-v->state.hscroll>=v->config.cols) v->state.hscroll=col-v->config.cols+1;
            v->busy=false; return 0;
        } else return VIEW_ERR_ARG;
    }
}
static int complete_call(view *v, int rc)
{
    if(rc==VIEW_MORE) return rc;
    v->busy=false;
    if(rc!=VIEW_OK) { if(v->edited) fallback(v); else v->state=v->before_state; }
    int done=mutation_end(v,rc);
    v->completed_state=v->state; v->completed_len=piece_len(v->tree);
    return done;
}
static int normalize(view *v, view_state saved, bool cursor_before, bool anchor_before, bool relative,
                     view_change *change)
{
    slice_begin(v); v->relative=relative; v->relative_top=false; v->before_state=v->state; v->restore_state=saved;
    uint64_t total=piece_len(v->tree);
    v->relative_pos=min_u64(saved.first_byte,total);
    v->restore_state.selection.cursor=min_u64(saved.selection.cursor,total);
    v->restore_state.selection.anchor=min_u64(saved.selection.anchor,total);
    v->restore_state.selection.preferred_col=VIEW_PREFERRED_UNSET;
    queries_invalidate(v,0);
    v->restore_state.approximate=false;
    v->cursor_before=cursor_before; v->anchor_before=anchor_before;
    for(unsigned i=0;i<16;++i) v->seeds[i].valid=false;
    v->busy=true; v->edited=true; v->shift=false; fallback(v);
    v->phase=P_NORMAL_SETUP;
    return complete_call(v,advance(v,change));
}
int view_restore(view *v, const view_state *state, view_change *change)
{
    if(change) *change=(view_change){0};
    if(!v || !v->tree || !state || !change) return VIEW_ERR_ARG;
    if(v->busy) return VIEW_ERR_BUSY;
    return normalize(v,*state,false,false,false,change);
}
int view_relative_restore(view *v, const view_state *state, view_change *change)
{
    if(change) *change=(view_change){0};
    /* edit-wv7: wrapped state is accepted when it matches the view's bound
     * wrap layout; its placement is relative_wrap_place. */
    if(!v || !v->tree || !state || !change || state->wrap!=v->state.wrap ||
       (state->wrap && !v->wrap_layout)) return VIEW_ERR_ARG;
    if(v->busy) return VIEW_ERR_BUSY;
    return normalize(v,*state,false,false,true,change);
}
static uint64_t rebase(uint64_t pos, view_change edit, bool before)
{
    if(pos<edit.offset) return pos;
    uint64_t end=edit.offset+edit.old_len;
    if(pos>end || (edit.old_len && pos==end)) return pos-edit.old_len+edit.new_len;
    return edit.offset+(before?0:edit.new_len);
}
int view_notify_edit(view *v, const view_change *edit, view_affinity cursor_affinity,
                     view_affinity anchor_affinity, view_change *change)
{
    view_change tuple=edit?*edit:(view_change){0};
    if(change) *change=(view_change){0};
    if(!v || !v->tree || !edit || !change || cursor_affinity>VIEW_AFFINITY_AFTER ||
       cursor_affinity<VIEW_AFFINITY_BEFORE || anchor_affinity>VIEW_AFFINITY_AFTER ||
       anchor_affinity<VIEW_AFFINITY_BEFORE) return VIEW_ERR_ARG;
    if(v->busy) return VIEW_ERR_BUSY;
    if(!tuple.changed) return view_restore(v,&v->state,change);
    uint64_t total=piece_len(v->tree);
    if(tuple.offset>total || tuple.new_len>total-tuple.offset ||
       tuple.old_len>UINT64_MAX-(total-tuple.new_len)) return VIEW_ERR_ARG;
    uint64_t old_total=total-tuple.new_len+tuple.old_len;
    if(v->state.selection.cursor>old_total || v->state.selection.anchor>old_total) return VIEW_ERR_ARG;
    view_state saved=v->state;
    saved.selection.cursor=rebase(saved.selection.cursor,tuple,cursor_affinity==VIEW_AFFINITY_BEFORE);
    saved.selection.anchor=rebase(saved.selection.anchor,tuple,anchor_affinity==VIEW_AFFINITY_BEFORE);
    saved.first_byte=rebase(min_u64(saved.first_byte,old_total),tuple,true);
    return normalize(v,saved,cursor_affinity==VIEW_AFFINITY_BEFORE,anchor_affinity==VIEW_AFFINITY_BEFORE,false,change);
}
void view_init(view *v, piece_tree *tree, const view_config *config)
{
    memset(v,0,sizeof *v); v->tree=tree;
    if(config) v->config=*config;
    if(!v->config.tab_width) v->config.tab_width=4;
    if(!v->config.rows) v->config.rows=24;
    if(!v->config.cols) v->config.cols=80;
    v->state.selection.preferred_col=VIEW_PREFERRED_UNSET;
    v->completed_state=v->state; v->completed_len=piece_len(tree);
}
bool view_busy(const view *v) { return v->busy; }
int view_set_undo(view *v, undo_log *log)
{
    if(!v || !v->tree || (log && log->tree!=v->tree)) return VIEW_ERR_ARG;
    if(v->busy || (v->undo && (v->undo->open || v->undo->partial)) ||
       (log && (log->open || log->partial))) return VIEW_ERR_BUSY;
    v->undo=log; return VIEW_OK;
}
void view_cancel(view *v)
{
    if(v->busy) { if(v->edited) fallback(v); else v->state=v->before_state; }
    v->busy=false; v->relative=false; v->relative_top=false;
    for(unsigned i=0;i<16;++i) v->seeds[i].valid=false;
    queries_invalidate(v,0);
    (void)mutation_end(v,VIEW_OK);
    v->completed_state=v->state; v->completed_len=piece_len(v->tree);
}
int view_continue(view *v, view_change *change)
{
    if(change) *change=(view_change){0};
    if(!v || !change || !v->tree) return VIEW_ERR_ARG;
    slice_begin(v);
    if(!v->busy) return VIEW_OK;
    return complete_call(v,advance(v,change));
}
int view_relative_command(view *v, view_key key, bool shift, view_change *change)
{
    if(change) *change=(view_change){0};
    bool meta=key==VIEW_HOME || key==VIEW_END || key==VIEW_WORD_LEFT || key==VIEW_WORD_RIGHT ||
        key==VIEW_SELECT_WORD || key==VIEW_SELECT_LINE;
    /* edit-wv7: wrapped buffers also accept document Home/End and Select
     * All. Visual rows come from layout as in view_command; only the
     * placement is relative. */
    bool wrapped=v && v->state.wrap;
    if(!v || !v->tree || !change || (wrapped && !v->wrap_layout) ||
       !(vertical(key) || key==VIEW_LEFT || key==VIEW_RIGHT || meta || key==VIEW_SELECT_ALL ||
         (wrapped && (key==VIEW_DOC_HOME || key==VIEW_DOC_END)))) return VIEW_ERR_ARG;
    if(v->busy) return VIEW_ERR_BUSY;
    uint64_t cursor=v->state.selection.cursor, anchor=v->state.selection.anchor;
    if(cursor>piece_len(v->tree) || anchor>piece_len(v->tree)) return VIEW_ERR_ARG;
    if(cursor==anchor && (((key==VIEW_LEFT || key==VIEW_WORD_LEFT) && cursor==0) ||
       ((key==VIEW_RIGHT || key==VIEW_WORD_RIGHT) && cursor==piece_len(v->tree)))) {
        v->scanned=0; v->relative=true; v->relative_top=false; return VIEW_OK;
    }
    slice_begin(v); v->before_state=v->state;
    v->busy=true; v->shift=shift; v->key=key; v->origin=cursor; v->edited=false;
    v->lo=min_u64(cursor,anchor); v->hi=max_u64(cursor,anchor);
    v->relative=true; v->relative_top=false; v->relative_pos=cursor; v->query_kind=0;
    if(wrapped) {
        /* view_command's wrapped command path; layout owns visual rows.
         * Word and select keys first prove their line metadata (P_REL_META);
         * wrapped Home/End use the visual row instead. */
        v->state.approximate=false;
        if(!(vertical(key) || key==VIEW_HOME || key==VIEW_END)) v->state.visual_end=false;
        v->phase=P_SETUP;
        if(meta && key!=VIEW_HOME && key!=VIEW_END) {
            v->meta_stage=0; v->relative_pos=key==VIEW_SELECT_LINE?v->lo:cursor; v->phase=P_REL_META;
        }
        return complete_call(v,advance(v,change));
    }
    /* (P) page displacement is exactly the configured visible row count;
     * an arrow advances its adjacent row. No estimated total limits motion. */
    v->relative_rows=(key==VIEW_PAGE_UP || key==VIEW_PAGE_DOWN)?v->config.rows:1u;
    v->state.visual_end=false;
    if(!vertical(key)) v->state.selection.preferred_col=VIEW_PREFERRED_UNSET;
    v->phase=P_REL_START;
    /* edit-1vun: unwrapped Select All too; its follow walks back from EOF
     * to the last LF (P_REL_FOLLOW) instead of line_at(EOF) from byte 0. */
    if(key==VIEW_SELECT_ALL) v->phase=P_SETUP;
    if(meta) {
        v->state.approximate=false; v->meta_stage=0;
        v->relative_pos=key==VIEW_SELECT_LINE?v->lo:cursor; v->phase=P_REL_META;
    }
    return complete_call(v,advance(v,change));
}
int view_command(view *v, view_key key, bool shift, const uint8_t *text, size_t len, view_change *change)
{
    if(change) *change=(view_change){0};
    if(!v || !v->tree || !change || key<VIEW_LEFT || key>VIEW_TYPE || (len && !text)) return VIEW_ERR_ARG;
    if(v->busy) return VIEW_ERR_BUSY;
    uint64_t total=piece_len(v->tree), cursor=v->state.selection.cursor, anchor=v->state.selection.anchor;
    if(cursor>total || anchor>total) return VIEW_ERR_ARG;
    v->scanned=0; v->relative=false; v->relative_top=false;
    /* Older editor callers use empty TYPE to repair a target they installed
     * after an external mutation. An unchanged completed state is a true no-op.
     * Explicit notify/restore remains required for arbitrary external edits. */
    if(key==VIEW_TYPE && len==0 && cursor==anchor &&
       (total!=v->completed_len || cursor!=v->completed_state.selection.cursor ||
        anchor!=v->completed_state.selection.anchor)) return view_restore(v,&v->state,change);
    if(cursor==anchor && ((key==VIEW_TYPE && len==0) ||
       (cursor==0 && (key==VIEW_LEFT || key==VIEW_BACKSPACE || key==VIEW_WORD_LEFT || key==VIEW_WORD_BACKSPACE)) ||
       (cursor==total && (key==VIEW_RIGHT || key==VIEW_DELETE || key==VIEW_WORD_RIGHT || key==VIEW_WORD_DELETE)))) return VIEW_OK;
    slice_begin(v); v->before_state=v->state;
    v->busy=true; v->shift=shift; v->key=key; v->origin=cursor; v->edited=false;
    v->lo=min_u64(cursor,anchor); v->hi=max_u64(cursor,anchor);
    v->state.approximate=false;
    if(!v->state.wrap || !(vertical(key) || key==VIEW_HOME || key==VIEW_END)) v->state.visual_end=false;
    if(key==VIEW_TYPE) {
        if(len>UINT64_MAX-total) return complete_call(v,PIECE_ERR_RANGE);
        uint64_t selection=v->hi-v->lo;
        if(selection>VIEW_MUTATION_BOUND) {
            bulk_begin(v,v->lo,selection,len);
            if(len<=VIEW_MUTATION_BOUND) {
                if(len) memcpy(v->pending_text,text,len);
                v->scanned=len; v->bulk_insert=len!=0; v->phase=P_BULK_PREPARE;
                return VIEW_MORE;
            }
            /* The legacy TYPE contract consumes arbitrary input synchronously.
             * Only selection capture is sliced when text exceeds scratch. */
            int rc=insert_bytes(v,v->lo,text,len); if(rc!=VIEW_OK) return complete_call(v,rc);
            *change=(view_change){v->lo,0,len,true}; v->edited=true;
            seeds_edit(v,v->lo); v->shift=false; fallback(v);
            return VIEW_MORE;
        }
        /* Insert first: insertion failure leaves the selection/text intact.
         * A later delete failure reports the insertion prefix, per piece API. */
        uint64_t lo=v->lo, old=v->hi-v->lo;
        int rc=insert_bytes(v,lo,text,len);
        if(rc!=0) return complete_call(v,rc);
        if(len) *change=(view_change){lo,0,len,true};
        if(len) v->edited=true;
        if(len) seeds_edit(v,lo);
        if(old) {
            rc=delete_bytes(v,lo+len,old);
            if(rc!=0) {
                fallback(v);
                return complete_call(v,rc);
            }
            *change=(view_change){lo,old,len,true};
            v->edited=true;
            seeds_edit(v,lo);
        }
        v->shift=false; fallback(v);
        seek_begin(v,S_CEIL,lo+len,lo+len,P_REPAIR);
    } else v->phase=P_SETUP;
    if(!v->busy) return VIEW_OK;
    return complete_call(v,advance(v,change));
}
