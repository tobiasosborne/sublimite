#include "indent.h"
#include <string.h>

#define BACK_BLOCK 1024u

typedef struct byte_reader {
    piece_iter it;
    const uint8_t *p;
    size_t n, used;
    uint64_t pos, end;
} byte_reader;

static void reader_init(byte_reader *r, const piece_tree *t, uint64_t lo, uint64_t hi)
{
    memset(r,0,sizeof *r); r->pos=lo; r->end=hi;
    piece_iter_begin(&r->it,t,lo);
}
static bool reader_next(byte_reader *r, uint8_t *c)
{
    if (r->pos==r->end) return false;
    if (r->used==r->n) {
        if (!piece_iter_next(&r->it,&r->p,&r->n)) return false;
        r->used=0;
    }
    *c=r->p[r->used++]; ++r->pos; return true;
}
static bool ws(uint8_t c) { return c==' ' || c=='\t'; }
static uint8_t byte_at(const piece_tree *t, uint64_t off)
{
    uint8_t c=0; (void)piece_read(t,off,&c,1); return c;
}
static bool split_crlf(const piece_tree *t, uint64_t cursor)
{
    return cursor>0 && cursor<piece_len(t) && byte_at(t,cursor)=='\n' && byte_at(t,cursor-1)=='\r';
}
static uint64_t line_start(const piece_tree *t, uint64_t cursor)
{
    uint8_t block[BACK_BLOCK]; uint64_t end=cursor;
    while (end>0) {
        size_t n=end>BACK_BLOCK?BACK_BLOCK:(size_t)end;
        uint64_t lo=end-n; (void)piece_read(t,lo,block,n);
        for (size_t i=n;i>0;--i) if (block[i-1]=='\n') return lo+i;
        end=lo;
    }
    return 0;
}
/* No line index queries: mapped buffers must not be indexed as a side effect. */
static uint64_t line_end(const piece_tree *t, uint64_t cursor, bool *crlf, bool *terminated)
{
    byte_reader r; reader_init(&r,t,cursor,piece_len(t));
    uint8_t prev=cursor>0?byte_at(t,cursor-1):0, c;
    *crlf=false; *terminated=false;
    while (reader_next(&r,&c)) {
        if (c=='\n') {
            *terminated=true; *crlf=prev=='\r';
            return r.pos-1-(*crlf?1u:0u);
        }
        prev=c;
    }
    return piece_len(t);
}
indent_code indent_on_enter(const piece_tree *t, uint64_t cursor,
                            uint8_t *out, size_t cap, size_t *length)
{
    if (length) *length=0;
    if (!t || !length || (!out && cap)) return INDENT_ERR_ARGUMENT;
    if (cursor>piece_len(t) || split_crlf(t,cursor)) return INDENT_ERR_RANGE;
    uint64_t start=line_start(t,cursor), leading=0;
    byte_reader r; reader_init(&r,t,start,cursor); uint8_t c;
    while (reader_next(&r,&c) && ws(c)) ++leading;
    bool crlf, terminated; (void)line_end(t,cursor,&crlf,&terminated);
    if (!terminated && start>=2) crlf=byte_at(t,start-2)=='\r';
    size_t newline=crlf?2u:1u;
    if (leading>SIZE_MAX-newline) return INDENT_ERR_RANGE;
    *length=(size_t)leading+newline;
    if (cap<*length) return INDENT_ERR_CAPACITY;
    if (crlf) { out[0]='\r'; out[1]='\n'; } else out[0]='\n';
    if (leading) (void)piece_read(t,start,out+newline,(size_t)leading);
    return INDENT_OK;
}
indent_code indent_on_close_brace(const piece_tree *t, uint64_t cursor,
                                  indent_style style, indent_edit *edit)
{
    if (edit) memset(edit,0,sizeof *edit);
    if (!t || !edit || style.width==0 || style.width>INDENT_MAX_WIDTH) return INDENT_ERR_ARGUMENT;
    if (cursor>piece_len(t) || split_crlf(t,cursor)) return INDENT_ERR_RANGE;
    uint64_t start=line_start(t,cursor); bool crlf, terminated;
    uint64_t end=line_end(t,cursor,&crlf,&terminated);
    (void)crlf; (void)terminated;
    edit->lo=cursor; edit->hi=cursor; edit->bytes[0]='}'; edit->length=1;
    byte_reader r; reader_init(&r,t,start,end); uint8_t c;
    uint64_t stop=start, previous_stop=start;
    unsigned column=0; bool white=true;
    while (reader_next(&r,&c)) {
        if (!ws(c)) { white=false; break; }
        if (r.pos<=cursor) {
            if (c=='\t') column=style.width;
            else ++column;
            if (column==style.width) {
                previous_stop=stop; stop=r.pos; column=0;
            }
        }
    }
    if (white) edit->lo=column?stop:previous_stop;
    return INDENT_OK;
}
static bool bracket(uint8_t c, uint8_t *open, uint8_t *close)
{
    switch (c) {
    case '(': case ')': *open='('; *close=')'; return true;
    case '[': case ']': *open='['; *close=']'; return true;
    case '{': case '}': *open='{'; *close='}'; return true;
    default: return false;
    }
}
indent_code indent_bracket_match(const piece_tree *t, uint64_t cursor,
                                 uint64_t lo, uint64_t hi, uint64_t *match)
{
    if (match) *match=INDENT_NONE;
    if (!t || !match) return INDENT_ERR_ARGUMENT;
    if (cursor>piece_len(t) || lo>hi || hi>piece_len(t)) return INDENT_ERR_RANGE;
    uint8_t c=0, open=0, close=0; uint64_t source=cursor;
    bool found=false;
    if (source>=lo && source<hi) { c=byte_at(t,source); found=bracket(c,&open,&close); }
    if (!found && cursor>0 && cursor-1>=lo && cursor-1<hi) {
        source=cursor-1; c=byte_at(t,source); found=bracket(c,&open,&close);
    }
    if (!found) return INDENT_OK;
    uint64_t depth=1;
    if (c==open) {
        piece_iter it; piece_iter_begin(&it,t,source+1);
        uint64_t pos=source+1; const uint8_t *p; size_t n;
        while (pos<hi && piece_iter_next(&it,&p,&n)) {
            if ((uint64_t)n>hi-pos) n=(size_t)(hi-pos);
            for (size_t i=0;i<n;++i) {
                if (p[i]==open) ++depth;
                else if (p[i]==close && --depth==0) { *match=pos+i; return INDENT_OK; }
            }
            pos+=n;
        }
    } else {
        uint8_t block[BACK_BLOCK]; uint64_t end=source;
        while (end>lo) {
            size_t n=end-lo>BACK_BLOCK?BACK_BLOCK:(size_t)(end-lo);
            uint64_t start=end-n; (void)piece_read(t,start,block,n);
            for (size_t i=n;i>0;--i) {
                if (block[i-1]==close) ++depth;
                else if (block[i-1]==open && --depth==0) { *match=start+i-1; return INDENT_OK; }
            }
            end=start;
        }
    }
    return INDENT_OK;
}
static void add_range(indent_range *out, size_t cap, size_t *count, uint64_t lo, uint64_t hi)
{
    if (lo>=hi) return;
    if (*count<cap) out[*count]=(indent_range){lo,hi};
    ++*count;
}
indent_code indent_trailing_ws_ranges(const piece_tree *t, uint64_t lo,
                                     uint64_t hi, indent_range *out, size_t cap,
                                     size_t *count)
{
    if (count) *count=0;
    if (!t || !count || (!out && cap)) return INDENT_ERR_ARGUMENT;
    if (lo>hi || hi>piece_len(t)) return INDENT_ERR_RANGE;
    byte_reader r; reader_init(&r,t,lo,hi); uint8_t c;
    uint64_t run=INDENT_NONE; bool cr=false;
    while (reader_next(&r,&c)) {
        uint64_t pos=r.pos-1;
        if (c=='\n') {
            if (run!=INDENT_NONE) add_range(out,cap,count,run,pos-(cr?1u:0u));
            run=INDENT_NONE; cr=false; continue;
        }
        if (cr) { run=INDENT_NONE; cr=false; }
        if (ws(c)) { if (run==INDENT_NONE) run=pos; }
        else if (c=='\r') cr=true;
        else run=INDENT_NONE;
    }
    if (hi==piece_len(t) && run!=INDENT_NONE && !cr) add_range(out,cap,count,run,hi);
    return *count>cap?INDENT_ERR_CAPACITY:INDENT_OK;
}

typedef struct detect_state {
    size_t tabs, spaces, leading, divisor;
    bool in_leading, has_tab, has_content;
} detect_state;
static size_t gcd(size_t a, size_t b)
{
    while (b) { size_t rem=a%b; a=b; b=rem; } return a;
}
static void detect_line(detect_state *d)
{
    if (d->has_content && d->leading) {
        if (d->has_tab) ++d->tabs;
        else { ++d->spaces; d->divisor=gcd(d->divisor,d->leading); }
    }
    d->leading=0; d->has_tab=false; d->has_content=false; d->in_leading=true;
}
static void detect_feed(detect_state *d, const uint8_t *p, size_t n)
{
    for (size_t i=0;i<n;++i) {
        uint8_t c=p[i];
        if (c=='\n') detect_line(d);
        else if (d->in_leading && ws(c)) { ++d->leading; if (c=='\t') d->has_tab=true; }
        else {
            d->in_leading=false;
            if (c!='\r') d->has_content=true;
        }
    }
}
static void detect_finish(detect_state *d, bool eof, indent_style *style)
{
    if (eof) detect_line(d);
    style->uses_tabs=d->tabs>d->spaces;
    style->width=(uint8_t)((d->divisor==0 || d->divisor>INDENT_MAX_WIDTH)?4u:d->divisor);
}
indent_code indent_detect_bytes(const uint8_t *p, size_t length, indent_style *style)
{
    if (style) *style=(indent_style){false,0};
    if (!style || (!p && length)) return INDENT_ERR_ARGUMENT;
    size_t n=length>INDENT_PREFIX_BYTES?INDENT_PREFIX_BYTES:length;
    detect_state d={0}; d.in_leading=true;
    detect_feed(&d,p,n); detect_finish(&d,n==length,style); return INDENT_OK;
}
indent_code indent_detect(const piece_snapshot *s, indent_style *style)
{
    if (style) *style=(indent_style){false,0};
    if (!s || !style) return INDENT_ERR_ARGUMENT;
    uint64_t length=piece_snapshot_len(s), end=length>INDENT_PREFIX_BYTES?INDENT_PREFIX_BYTES:length, pos=0;
    piece_iter it; piece_iter_begin_snapshot(&it,s,0);
    detect_state d={0}; d.in_leading=true; const uint8_t *p; size_t n;
    while (pos<end && piece_iter_next(&it,&p,&n)) {
        if ((uint64_t)n>end-pos) n=(size_t)(end-pos);
        detect_feed(&d,p,n); pos+=n;
    }
    detect_finish(&d,end==length,style); return INDENT_OK;
}
