#include "editor/private.h"
#include "trace/trace.h"
#include <xkbcommon/xkbcommon-keysyms.h>
#include <string.h>
#include <poll.h>

static void replace_begin(editor *e, bool all);
static void retain_source(void *ctx) { ((editor_buffer *)ctx)->find_leases++; }
static void release_source(void *ctx) { ((editor_buffer *)ctx)->find_leases--; }
static int preflight(void *ctx, size_t operations, uint64_t deleted, size_t replacement)
{
    editor *e=ctx;
    (void)deleted;
    if (e->buffer->pending_open || e->buffer->source_stale) return EDITOR_ERR_IO;
    if (replacement && operations>UINT64_MAX/replacement) return EDITOR_ERR_CAPACITY;
    uint64_t growth=(uint64_t)operations*replacement;
    if (e->buffer->index && lineidx_edit_check(e->buffer->index,0,0,growth)) return EDITOR_ERR_CAPACITY;
    /* Each replacement step pumps its journal before the next step. */
    if (e->journal && (replacement>EDITOR_STAGE_BYTES-e->stage_used ||
        e->op_count>EDITOR_STAGE_OPS-2u)) return EDITOR_ERR_CAPACITY;
    return 0;
}
int editor_find_init(editor *e)
{
    /* G: match/visible capacities reuse the layout cluster-cache bound.
     * E: exclusive arena = worker scratch+program maxima, outboxes for both
     * streams in both slots, and program-sized metadata/alignment allowance. */
    const size_t capacity=LAYOUT_CLUSTER_CACHE;
    size_t reserve=2u*(FIND_MAX_SCRATCH_BYTES+FIND_MAX_PROGRAM_BYTES+
        (2u*capacity+2u)*sizeof(work_msg)+FINDUI_QUERY_BYTES*4u)+
        4u*FIND_MAX_PROGRAM_BYTES;
    if (edit_arena_init(&e->find_arena,reserve)) return EDITOR_ERR_MEMORY;
    findui_config config={.arena=&e->find_arena,.workers=&e->pool,
        .match_capacity=capacity,.visible_capacity=capacity};
    if (findui_init(&e->find,&config)!=FINDUI_OK) return EDITOR_ERR_MEMORY;
    e->find_ranges=edit_arena_alloc(&e->find_arena,capacity*sizeof *e->find_ranges,_Alignof(findui_range));
    if (!e->find_ranges) return EDITOR_ERR_MEMORY;
    for (uint32_t i=0;i<LAYOUT_ASCII_GLYPHS;i++) e->find_ascii[i]=i;
    findui_mutation_host host={e,preflight,editor_find_delete,editor_find_insert};
    if (findui_set_mutation_host(&e->find,&host)!=FINDUI_OK) return EDITOR_ERR_ARG;
    return editor_fif_init(e);
}
void editor_find_close(editor *e)
{
    editor_fif_close(e); /* drains its jobs on the still-live pool */
    if (e->find.private_) {
        if (e->find_replace_active) {
            (void)findui_replace_cancel(&e->find); editor_find_replace_finish(e);
        }
        while (findui_dispose(&e->find)==FINDUI_MORE) {
            (void)work_mailbox_drain(&e->pool,editor_route_work,e);
            struct pollfd fd={work_pool_eventfd(&e->pool),POLLIN,0};
            (void)poll(&fd,1,1);
        }
    }
    edit_arena_free(&e->find_arena);
}
static int geometry(editor *e)
{
    return editor_text_geometry(e);
}
static bool visible_byte(const editor *e, uint64_t byte)
{
    uint32_t cols=e->text_grid.dims.cols;
    if (!e->lay.text_cols) return false;
    for (uint32_t r=0;r<e->v.config.rows;r++) { /* visible rows only */
        if (e->row_byte[r]==LAYOUT_VOID_ROW) continue;
        const editor_hit *first=&e->hits[(size_t)r*e->max_cols+e->lay.gw];
        const editor_hit *last=&e->hits[(size_t)r*e->max_cols+cols-1u];
        uint64_t lo=first->within && first->width>1u ? first->after : first->before;
        uint64_t hi=last->after; bool inclusive=last->before==last->after;
        const render_cell *cell=&e->text_grid.cells[(size_t)r*cols+cols-1u];
        if (last->width==2u && last->after-last->before>1u &&
            !(cell->attrs&(RENDER_ATTR_WIDE_LEFT|RENDER_ATTR_WIDE_RIGHT))) {
            hi=last->before; inclusive=false;
        }
        if (byte>=lo && (byte<hi || (inclusive && byte==hi))) return true;
    }
    return false;
}
static int select_result(editor *e, findui_range range)
{
    if (range.start==FIND_UNSET) return 0;
    uint64_t old=e->v.state.selection.cursor;
    e->old_selection=e->v.state.selection;
    e->find_scroll=false; e->wheel_pending=false;
    e->v.state.selection.cursor=range.end; e->v.state.selection.anchor=range.start;
    e->v.state.selection.preferred_col=VIEW_PREFERRED_UNSET;
    undo_break_burst(e->undo); editor_restart_blink(e,trace_now_ns());
    uint64_t last=range.end>range.start ? range.end-1u : range.start;
    if (visible_byte(e,range.start) && visible_byte(e,last)) return editor_refresh_cursor(e,old);
    /* Search results may be far beyond the resident viewport. Resolve the
     * byte through the worker-backed navigation bridge, never a whole-source
     * piece_byte_to_line/line_to_byte query on the input thread. Unwrapped,
     * that is 3kk's scroll path; editor_wheel_continue follows horizontally
     * after adoption. Wrapped, a private seek uses the same resident window. */
    if (!e->v.state.wrap) {
        int rc=editor_scroll_seek(e,range.start); if (rc) return rc;
        e->wheel_pending=true; e->find_scroll=true;
        return 0;
    }
    int rc=scroll_init(&e->wheel,(scroll_config){e->text_grid.dims.rows,1,0},
        (scroll_extent){piece_len(e->tree),e->buffer->lines,e->buffer->lg.lines_exact});
    if (rc) return rc;
    e->wheel.first_line=e->v.state.first_line; e->wheel.first_byte=e->v.state.first_byte;
    rc=scroll_seek_byte(&e->wheel,range.start); if (rc) return rc;
    e->wheel_resolver=(scroll_resolver){0}; e->wheel_pending=true; e->find_scroll=true;
    return 0;
}
int editor_find_service(editor *e)
{
    if (!e->find.private_ || !e->buffer) return 0;
    if (!findui_get_state(&e->find).open && !e->find_visible) return 0;
    (void)findui_service(&e->find);
    findui_state state=findui_get_state(&e->find);
    if (!e->find_replace_active && (e->find_id!=e->buffer->id ||
        e->find_source_generation!=e->buffer->source_generation ||
        e->find_buffer_revision!=e->buffer->revision)) {
        if (e->buffer->source_stale || e->buffer->pending_open) {
            (void)findui_set_source(&e->find,NULL,++e->find_revision);
            e->find_id=0; e->find_select_pending=false; e->find_replace_request=0; e->find_range_count=0;
            return 0;
        }
        piece_snapshot *snapshot=piece_snapshot_take(e->tree);
        if (!snapshot) return EDITOR_ERR_MEMORY;
        findui_source_lease lease={e->buffer,e->buffer->source_generation+1u,retain_source,release_source};
        findui_code code=findui_set_source_leased(&e->find,snapshot,++e->find_revision,&lease);
        piece_snapshot_release(snapshot);
        if (code==FINDUI_ERR_BUSY) return 0;
        if (code!=FINDUI_OK) return EDITOR_ERR_HISTORY;
        e->find_id=e->buffer->id; e->find_source_generation=e->buffer->source_generation;
        e->find_buffer_revision=e->buffer->revision; e->find_range_count=0;
        e->find_navigated=false; state=findui_get_state(&e->find);
    }
    if (e->find_replace_request && !state.searching && !state.complete) {
        e->find_replace_request=0; e->find_error=FINDUI_ERR_FIND;
    }
    if (e->find_replace_request && state.complete && !state.searching && !e->find_replace_active) {
        bool all=e->find_replace_request==2; e->find_replace_request=0;
        replace_begin(e,all); state=findui_get_state(&e->find);
    }
    if (e->find_select_pending && !state.searching) {
        e->find_select_pending=false;
        if (state.complete && state.match_count) {
            findui_range selected=state.selected;
            findui_code code=FINDUI_OK;
            if (e->find_direction && (e->find_navigated || e->find_direction<0))
                code=findui_next(&e->find,e->find_direction,&selected);
            if (code==FINDUI_MORE) {
                e->find_select_pending=true; e->find_navigated=false;
                /* (P) zero means the ordinal request already advanced;
                 * its completion only needs to select the resolved range. */
                e->find_direction=0;
            }
            else if (code==FINDUI_OK) {
                e->find_navigated=true;
                int rc=select_result(e,selected); if (rc) return rc;
            }
        }
    }
    if (!e->find_replace_active && !layout_busy(&e->lay) && !e->full_pending) {
        uint64_t start=e->v.state.wrap ? e->v.state.visual_byte : e->v.state.first_byte;
        uint64_t end=e->lay.pos; if (end<start) end=start;
        if (end>piece_len(e->tree)) end=piece_len(e->tree);
        if (start!=e->find_window_start || end!=e->find_window_end) {
            (void)findui_set_window(&e->find,start,end);
            e->find_window_start=start; e->find_window_end=end;
        }
        size_t count=0;
        if (findui_highlights(&e->find,start,end,e->find_ranges,LAYOUT_CLUSTER_CACHE,&count)==FINDUI_OK)
            e->find_range_count=count;
        else e->find_range_count=0;
    }
    return 0;
}
static void navigate(editor *e, int direction)
{
    e->find_direction=direction; e->find_select_pending=true;
    e->find_error=editor_find_service(e);
}
static void replace_begin(editor *e, bool all)
{
    if (editor_read_only(e)) { e->stats.rejected_commands++; e->find_replace_request=0; return; } /* edit-n95 */
    findui_state state=findui_get_state(&e->find);
    if (state.searching) { e->find_replace_request=all ? 2 : 1; return; }
    if (!state.complete) { e->find_error=FINDUI_ERR_FIND; e->find_replace_request=0; return; }
    undo_state before; memcpy(before.bytes,&e->v.state.selection.cursor,sizeof(uint64_t));
    memcpy(before.bytes+sizeof(uint64_t),&e->v.state.selection.anchor,sizeof(uint64_t));
    undo_break_burst(e->undo);
    e->edit_delta=(editor_delta){.off=piece_len(e->tree),.before=before,.after=before};
    e->find_replace_end=0;
    e->replay_before_len=piece_len(e->tree);
    findui_code code=all ? findui_replace_all(&e->find,e->undo,e->find_revision,
        e->buffer->history_cap*2u+8u,trace_now_ns(),&before,&before) :
        findui_replace_one(&e->find,e->undo,e->find_revision,
        e->buffer->history_cap*2u+8u,trace_now_ns(),&before,&before);
    e->find_error=(int)code;
    e->find_replace_active=findui_get_state(&e->find).replacing;
    if (e->find_replace_active) (void)editor_begin_frame(e);
}
int editor_find_replace_continue(editor *e)
{
    if (!e->find_replace_active) return 0;
    if (e->op_count) return 0;
    size_t replaced=0;
    findui_code code=findui_replace_step(&e->find,1,trace_now_ns()+EDITOR_SLICE_NS,&replaced);
    e->find_error=code==FINDUI_MORE ? 0 : (int)code;
    if (!findui_get_state(&e->find).replacing) editor_find_replace_finish(e);
    return 0;
}
static void field_select_all(editor *e)
{
    findui_state state=findui_get_state(&e->find);
    if (state.replacement_focus) {
        memcpy(e->find_edit,state.replacement,state.replacement_length);
        (void)findui_set_replacement(&e->find,e->find_edit,state.replacement_length,state.replacement_length);
    } else (void)findui_set_query_cursor(&e->find,state.query_length);
    e->find_field_all=true;
}
/* Field word motion uses the view's whitespace/word/separator policy on
 * grapheme boundaries. The query's fixed capacity bounds the scan. */
static int field_class(const uint8_t *bytes, size_t length)
{
    utf8_step scalar=utf8_decode(bytes,length); uint32_t cp=scalar.cp;
    if (!scalar.valid) return 0;
    if (cp==' ' || cp=='\t' || cp=='\r' || cp=='\n' || cp=='\v' || cp=='\f' ||
        cp==0x85 || cp==0xa0 || cp==0x1680 || (cp>=0x2000 && cp<=0x200a) ||
        cp==0x2028 || cp==0x2029 || cp==0x202f || cp==0x205f || cp==0x3000) return 2;
    if (cp && cp<128 && strchr("./\\()\"'-:,.;<>~!@#$%^&*|+=[]{}`~?",(int)cp)) return 1;
    return 0;
}
static size_t field_word_left(const uint8_t *bytes, size_t cursor)
{
    size_t at=0, run=0, result=0; int kind=-1;
    while (at<cursor) {
        size_t step=utf8_grapheme_next(bytes+at,cursor-at);
        int next=field_class(bytes+at,step);
        if (next!=kind) { run=at; kind=next; }
        if (next!=2) result=run;
        at+=step;
    }
    return result;
}
static size_t field_word_right(const uint8_t *bytes, size_t length, size_t cursor)
{
    size_t at=cursor; int kind=-1;
    while (at<length) {
        size_t step=utf8_grapheme_next(bytes+at,length-at);
        int next=field_class(bytes+at,step);
        if (kind<0 && next!=2) kind=next;
        else if (kind>=0 && next!=kind) break;
        at+=step;
    }
    return at;
}
static void field_edit(editor *e, findui_state state, size_t at, size_t erase,
                       const uint8_t *insert, size_t add)
{
    size_t length=state.replacement_focus ? state.replacement_length : state.query_length;
    if (add>sizeof e->find_edit-(length-erase)) { e->stats.rejected_commands++; return; }
    if (state.replacement_focus) {
        memcpy(e->find_edit,state.replacement,at);
        if (add) memcpy(e->find_edit+at,insert,add);
        memcpy(e->find_edit+at+add,state.replacement+at+erase,length-at-erase);
        e->find_error=(int)findui_set_replacement(&e->find,e->find_edit,length-erase+add,at+add);
    } else if (erase || add) {
        e->find_error=(int)findui_edit_query(&e->find,at,erase,insert,add); e->find_navigated=false;
    } else e->find_error=(int)findui_set_query_cursor(&e->find,at);
}
int editor_find_paste(editor *e, const uint8_t *bytes, size_t length, bool replacement)
{
    if (!e->find_visible || e->find_replace_active || !bytes) return 0;
    /* Field paste stays bounded by its setup-reserved query capacity; native
     * replies retain their field destination and never enter document undo.
     * Normalize CRLF like document paste, without clipboard heap staging. */
    if (length>FINDUI_QUERY_BYTES) { e->stats.rejected_commands++; return 0; }
    uint8_t normalized[FINDUI_QUERY_BYTES]; size_t count=0;
    for (size_t i=0;i<length;i++) {
        if (bytes[i]=='\r' && i+1u<length && bytes[i+1u]=='\n') continue;
        normalized[count++]=bytes[i];
    }
    if (e->fif.mode && e->fif.where_focus) return editor_fif_paste(e,normalized,count); /* edit-n95 */
    findui_state state=findui_get_state(&e->find);
    if (state.replacement_focus!=replacement) return 0;
    size_t cursor=replacement ? state.replacement_cursor : state.query_cursor;
    size_t old=replacement ? state.replacement_length : state.query_length;
    size_t erase=e->find_field_all ? old : 0;
    field_edit(e,state,e->find_field_all ? 0 : cursor,erase,normalized,count);
    e->find_field_all=false;
    return editor_begin_frame(e);
}
bool editor_find_action(editor *e, const plat_event *ev, const keys_binding *binding)
{
    if (!e->find.private_ || !ev->press || !binding) return false;
    /* Focus keeps its physical field-editing keys even when their global
     * actions are rebound. Other strokes dispatch the configured find action. */
    bool ctrl=(ev->mods&PLAT_MOD_CTRL)!=0;
    bool enter=ev->keysym==XKB_KEY_Return || ev->keysym==XKB_KEY_KP_Enter;
    if (e->find_visible && !(ev->mods&PLAT_MOD_SUPER) &&
        (!(ev->mods&PLAT_MOD_ALT) || (ctrl && enter)) &&
        (ev->keysym==XKB_KEY_Escape || ev->keysym==XKB_KEY_Return || ev->keysym==XKB_KEY_KP_Enter ||
         ev->keysym==XKB_KEY_Tab || ev->keysym==XKB_KEY_Left || ev->keysym==XKB_KEY_Right ||
         ev->keysym==XKB_KEY_Home || ev->keysym==XKB_KEY_End || ev->keysym==XKB_KEY_BackSpace ||
         ev->keysym==XKB_KEY_Delete ||
         (ctrl && (ev->keysym=='a' || ev->keysym=='A' || ev->keysym=='v' || ev->keysym=='V'))))
        return false;
    switch (binding->action) {
    case KEYS_ACTION_FIND_IN_FILES: /* edit-n95: Where field mode; cancels a running search */
        (void)editor_find_service(e);
        e->find_visible=true;
        (void)findui_show(&e->find,true,false);
        editor_fif_show(e);
        field_select_all(e); keys_reset(&e->keys);
        e->find_error=geometry(e); return true;
    case KEYS_ACTION_NEXT_RESULT: case KEYS_ACTION_PREVIOUS_RESULT: {
        int rc=editor_fif_step(e,binding->action==KEYS_ACTION_NEXT_RESULT ? 1 : -1);
        if (rc && !e->error) e->error=rc;
        keys_reset(&e->keys); return true;
    }
    case KEYS_ACTION_FIND: case KEYS_ACTION_REPLACE:
        (void)editor_find_service(e);
        e->fif.mode=false;
        e->find_visible=true;
        (void)findui_show(&e->find,true,binding->action==KEYS_ACTION_REPLACE);
        field_select_all(e); keys_reset(&e->keys);
        e->find_error=geometry(e); return true;
    case KEYS_ACTION_FIND_NEXT: case KEYS_ACTION_FIND_PREVIOUS:
        if (!findui_get_state(&e->find).open) (void)findui_show(&e->find,true,false);
        /* (P) findui_next's direction contract is negative/positive one. */
        navigate(e,binding->action==KEYS_ACTION_FIND_PREVIOUS ? -1 : 1); return true;
    case KEYS_ACTION_REPLACE_NEXT:
        if (e->find_visible && findui_get_state(&e->find).replace_mode && !e->find_replace_active)
            replace_begin(e,false);
        return true;
    case KEYS_ACTION_TOGGLE_REGEX: case KEYS_ACTION_TOGGLE_CASE_SENSITIVE:
    case KEYS_ACTION_TOGGLE_WHOLE_WORD: {
        if (!e->find_visible || e->find_replace_active) return true;
        findui_options options=findui_get_state(&e->find).options;
        if (binding->action==KEYS_ACTION_TOGGLE_REGEX) options.regex=!options.regex;
        else if (binding->action==KEYS_ACTION_TOGGLE_CASE_SENSITIVE) options.match_case=!options.match_case;
        else options.whole_word=!options.whole_word;
        (void)findui_set_options(&e->find,options); e->find_navigated=false;
        (void)editor_begin_frame(e); return true;
    }
    default: return false;
    }
}
bool editor_find_key(editor *e, const plat_event *ev)
{
    if (!e->find.private_ || !ev->press) return false;
    if (!e->find_visible && !e->find_replace_active) return false;
    /* A consumed panel chord must not seed the global keymap's next stroke. */
    keys_reset(&e->keys);
    bool ctrl=(ev->mods&PLAT_MOD_CTRL)!=0;
    bool shift=(ev->mods&PLAT_MOD_SHIFT)!=0;
    findui_state state=findui_get_state(&e->find);
    if (ev->keysym==XKB_KEY_Escape) {
        if (e->find_replace_active) { (void)findui_replace_cancel(&e->find); editor_find_replace_finish(e); }
        e->find_visible=false; e->find_select_pending=false; e->find_replace_request=0;
        e->fif.mode=false;
        e->find_error=geometry(e); return true;
    }
    if (e->find_replace_active) return true;
    bool alt=(ev->mods&PLAT_MOD_ALT)!=0, super=(ev->mods&PLAT_MOD_SUPER)!=0;
    bool enter=ev->keysym==XKB_KEY_Return || ev->keysym==XKB_KEY_KP_Enter;
    /* Focus owns editing chords, including Ctrl(+Alt)+Enter below. */
    if (super || (ctrl && alt && !enter)) return true;
    if (ctrl && (ev->keysym=='a' || ev->keysym=='A')) {
        if (e->fif.mode && e->fif.where_focus) (void)editor_fif_key(e,ev); /* edit-n95 */
        else field_select_all(e);
        (void)editor_begin_frame(e); return true;
    }
    if (ctrl && (ev->keysym=='v' || ev->keysym=='V')) {
        e->find_error=editor_clip_find_paste(e); return true;
    }
    /* Ctrl+X (and unsupported global chords) is a consumed no-op. */
    if (ctrl && !enter && ev->keysym!=XKB_KEY_Left && ev->keysym!=XKB_KEY_Right &&
        ev->keysym!=XKB_KEY_BackSpace) return true;
    if (alt && !ctrl) return true;
    if (e->fif.mode && (ev->keysym==XKB_KEY_Return || ev->keysym==XKB_KEY_KP_Enter)) {
        int rc=editor_fif_start(e); /* edit-n95: refusals are counted, not errors */
        if (rc && !e->error) e->error=rc;
    } else if (e->fif.mode && ev->keysym==XKB_KEY_Tab) {
        e->fif.where_focus=!e->fif.where_focus;
        if (e->fif.where_focus) { e->fif.where_all=true; e->fif.where_cursor=e->fif.where_length; e->find_field_all=false; }
        else field_select_all(e);
    } else if (e->fif.mode && e->fif.where_focus) {
        (void)editor_fif_key(e,ev);
    } else if (ev->keysym==XKB_KEY_Return || ev->keysym==XKB_KEY_KP_Enter) {
        if (ctrl && state.replace_mode) replace_begin(e,(ev->mods&PLAT_MOD_ALT)!=0);
        else {
            /* Return advances from the query's initial preview as well as
             * subsequent selections. Keep keymap/hidden navigation unchanged.
             * (P) direction magnitude follows findui_next's one-step contract. */
            e->find_navigated=true;
            navigate(e,shift ? -1 : 1);
        }
    } else if (ev->keysym==XKB_KEY_Tab && state.replace_mode) {
        (void)findui_focus_replacement(&e->find,!state.replacement_focus);
        field_select_all(e);
    } else {
        const uint8_t *bytes=state.replacement_focus ? state.replacement : state.query;
        size_t length=state.replacement_focus ? state.replacement_length : state.query_length;
        size_t cursor=state.replacement_focus ? state.replacement_cursor : state.query_cursor;
        size_t at=cursor, erase=0, add=0;
        if (e->find_field_all && (ev->keysym==XKB_KEY_BackSpace || ev->keysym==XKB_KEY_Delete ||
            (ev->utf8_len && !(ev->mods&(PLAT_MOD_CTRL|PLAT_MOD_SUPER))))) {
            at=0; erase=length;
            if (ev->keysym!=XKB_KEY_BackSpace && ev->keysym!=XKB_KEY_Delete) add=ev->utf8_len;
            e->find_field_all=false;
        } else if (ev->keysym==XKB_KEY_Left) {
            at=e->find_field_all ? 0 : ctrl ? field_word_left(bytes,cursor) : cursor ? utf8_grapheme_prev(bytes,cursor) : 0;
            e->find_field_all=false;
        } else if (ev->keysym==XKB_KEY_Right) {
            at=e->find_field_all ? length : ctrl ? field_word_right(bytes,length,cursor) :
                cursor<length ? cursor+utf8_grapheme_next(bytes+cursor,length-cursor) : length;
            e->find_field_all=false;
        } else if (ev->keysym==XKB_KEY_Home) { at=0; e->find_field_all=false; }
        else if (ev->keysym==XKB_KEY_End) { at=length; e->find_field_all=false; }
        else if (ev->keysym==XKB_KEY_BackSpace && cursor) {
            at=ctrl ? field_word_left(bytes,cursor) : utf8_grapheme_prev(bytes,cursor); erase=cursor-at;
        } else if (ev->keysym==XKB_KEY_Delete && cursor<length) erase=utf8_grapheme_next(bytes+cursor,length-cursor);
        else if (ev->utf8_len && !(ev->mods&(PLAT_MOD_CTRL|PLAT_MOD_SUPER))) add=ev->utf8_len;
        field_edit(e,state,at,erase,ev->utf8,add);
    }
    (void)editor_begin_frame(e); return true;
}
int editor_find_paint(editor *e)
{
    if (!e->panel_rows) return 0;
    findui_style style={0xd8dee9,0x171b22,0x8d98a9,0x20242c,0xe5e9f0,e->find_ascii,LAYOUT_ASCII_GLYPHS};
    if (findui_render(&e->find,&e->grid,e->grid.dims.rows-e->panel_rows,e->panel_rows,&style)!=FINDUI_OK)
        return EDITOR_ERR_ARG;
    editor_fif_paint(e,e->grid.dims.rows-e->panel_rows,e->panel_rows); /* edit-n95 */
    if (e->find_field_all && !(e->fif.mode && e->fif.where_focus)) {
        findui_state state=findui_get_state(&e->find);
        uint32_t row=e->grid.dims.rows-e->panel_rows;
        uint32_t column=(uint32_t)(sizeof "Find: "-1u);
        if (state.replacement_focus && e->panel_rows>1u) { row++; column=(uint32_t)(sizeof "Replace: "-1u); }
        for (uint32_t c=column;c<e->grid.dims.cols;c++) {
            render_cell *cell=&e->grid.cells[(size_t)row*e->grid.dims.cols+c];
            if ((cell->attrs&RENDER_ATTR_CURSOR) || cell->fg==style.accent) break;
            cell->fg=e->layout_cfg.sel_fg; cell->bg=e->layout_cfg.sel_bg;
        }
    }
    return 0;
}
