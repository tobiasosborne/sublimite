#include "indent/indent.h"
#include "base/base.h"
#include <string.h>

#define CHECK(x) EDIT_ASSERT(x)
#define MAX_BYTES 8192u

static bool whitespace(uint8_t c) { return c==' ' || c=='\t'; }
static bool is_bracket(uint8_t c) { return c=='(' || c==')' || c=='[' || c==']' || c=='{' || c=='}'; }
static uint64_t match_oracle(const uint8_t *p, size_t n, size_t cursor, size_t lo, size_t hi)
{
    (void)n;
    size_t pos=cursor;
    if (!(pos>=lo && pos<hi && is_bracket(p[pos]))) {
        if (!(pos>0 && pos-1>=lo && pos-1<hi && is_bracket(p[pos-1]))) return INDENT_NONE;
        --pos;
    }
    uint8_t c=p[pos], open, close;
    if (c=='(' || c==')') { open='('; close=')'; }
    else if (c=='[' || c==']') { open='['; close=']'; }
    else { open='{'; close='}'; }
    size_t depth=1;
    if (c==open) {
        for (size_t i=pos+1;i<hi;++i) {
            if (p[i]==open) ++depth;
            if (p[i]==close && --depth==0) return i;
        }
    } else {
        for (size_t i=pos;i>lo;) {
            --i; if (p[i]==close) ++depth;
            if (p[i]==open && --depth==0) return i;
        }
    }
    return INDENT_NONE;
}
static void ws_oracle(const uint8_t *p, size_t n, size_t lo, size_t hi,
                      const indent_range *ranges, size_t count)
{
    size_t expected=0, start=lo;
    for (size_t i=lo;i<=hi;++i) {
        if (!((i<hi && p[i]=='\n') || (i==hi && hi==n))) continue;
        size_t end=i;
        if (i<hi && end>start && p[end-1]=='\r') --end;
        size_t begin=end; while (begin>start && whitespace(p[begin-1])) --begin;
        if (begin<end) {
            CHECK(expected<count); CHECK(ranges[expected].lo==begin && ranges[expected].hi==end); ++expected;
        }
        start=i+1;
    }
    CHECK(expected==count);
}
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (size<8) return 0;
    size_t n=size-8; if (n>MAX_BYTES) n=MAX_BYTES;
    uint8_t bytes[MAX_BYTES];
    static const uint8_t alphabet[]=" \t\r\n()[]{}x\"\\\xc3\xa9";
    for (size_t i=0;i<n;++i) bytes[i]=(data[0]&1u)?alphabet[data[i+8]%(sizeof alphabet-1)]:data[i+8];
    size_t cursor=((size_t)data[1]*256u+data[2])%(n+1);
    size_t lo=((size_t)data[3]*256u+data[4])%(n+1);
    size_t hi=((size_t)data[5]*256u+data[6])%(n+1);
    if (lo>hi) { size_t temp=lo; lo=hi; hi=temp; }
    uint8_t width=(uint8_t)(1u+data[7]%INDENT_MAX_WIDTH);
    piece_allocator a=piece_default_allocator(); piece_tree *t=piece_create(&a); CHECK(t);
    /* Vary chunk boundaries; reverse insert prevents append coalescing. */
    if (data[0]&2u) {
        size_t end=n;
        while (end) { size_t k=end>31?31:end; CHECK(piece_insert(t,0,bytes+end-k,k)==PIECE_OK); end-=k; }
    } else CHECK(piece_init_copy(t,bytes,n)==PIECE_OK);
    uint64_t match;
    CHECK(indent_bracket_match(t,cursor,lo,hi,&match)==INDENT_OK);
    CHECK(match==match_oracle(bytes,n,cursor,lo,hi));
    CHECK(match==INDENT_NONE || (match>=lo && match<hi));
    indent_range ranges[MAX_BYTES]; size_t count;
    CHECK(indent_trailing_ws_ranges(t,lo,hi,ranges,MAX_BYTES,&count)==INDENT_OK);
    ws_oracle(bytes,n,lo,hi,ranges,count);
    size_t cap=data[7]%16u, required;
    indent_code rc=indent_trailing_ws_ranges(t,lo,hi,ranges,cap,&required);
    CHECK(required==count && rc==(count>cap?INDENT_ERR_CAPACITY:INDENT_OK));
    uint8_t out[MAX_BYTES+2]; size_t length;
    rc=indent_on_enter(t,cursor,out,sizeof out,&length);
    bool split=cursor>0 && cursor<n && bytes[cursor-1]=='\r' && bytes[cursor]=='\n';
    CHECK(rc==(split?INDENT_ERR_RANGE:INDENT_OK));
    size_t start=cursor; while (start>0 && bytes[start-1]!='\n') --start;
    size_t end=cursor; while (end<n && bytes[end]!='\n') ++end;
    if (!split) {
        size_t lead=start; while (lead<cursor && whitespace(bytes[lead])) ++lead;
        bool crlf=end<n?(end>0 && bytes[end-1]=='\r'):(start>=2 && bytes[start-2]=='\r');
        size_t newline=crlf?2u:1u;
        CHECK(length==lead-start+newline);
        CHECK(out[newline-1]=='\n' && (!crlf || out[0]=='\r'));
        CHECK(memcmp(out+newline,bytes+start,lead-start)==0);
        memset(out,0xa5,sizeof out); size_t want=length;
        rc=indent_on_enter(t,cursor,out,want-1,&length);
        CHECK(rc==INDENT_ERR_CAPACITY && length==want && out[0]==0xa5);
    }
    indent_edit edit;
    rc=indent_on_close_brace(t,cursor,(indent_style){(data[7]&128u)!=0,width},&edit);
    CHECK(rc==(split?INDENT_ERR_RANGE:INDENT_OK));
    if (!split) {
        size_t content_end=end; if (end<n && end>start && bytes[end-1]=='\r') --content_end;
        bool white=true; for (size_t i=start;i<content_end;++i) if (!whitespace(bytes[i])) white=false;
        size_t column=0;
        for (size_t i=start;i<cursor;++i) column=bytes[i]=='\t'?column+width-column%width:column+1;
        size_t target=column?((column-1)/width)*width:0;
        size_t keep=start, visual=0;
        if (white) for (size_t i=start;i<cursor;++i) {
            visual=bytes[i]=='\t'?visual+width-visual%width:visual+1;
            if (visual<=target) keep=i+1;
        }
        CHECK(edit.lo==(white?keep:cursor) && edit.hi==cursor && edit.lo<=edit.hi);
        CHECK(edit.length==1 && edit.bytes[0]=='}');
    }
    indent_style direct, snapshot_style;
    CHECK(indent_detect_bytes(bytes,n,&direct)==INDENT_OK);
    piece_snapshot *s=piece_snapshot_take(t); CHECK(s);
    CHECK(indent_detect(s,&snapshot_style)==INDENT_OK);
    CHECK(direct.uses_tabs==snapshot_style.uses_tabs && direct.width==snapshot_style.width);
    CHECK(direct.width>=1 && direct.width<=INDENT_MAX_WIDTH);
    piece_snapshot_release(s); piece_destroy(t); return 0;
}
