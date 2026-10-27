#include "editor/private.h"
#include "x11/clip.h"
#include "trace/trace.h"
#include <string.h>
#include <time.h>

/* The bulk crossover is the bead's >= MiB requirement (G), expressed as
 * binary KiB * KiB. Mutation/copy slices reuse view's existing byte bounds. */
#define CLIP_BULK_BYTES (1024u * 1024u)
_Static_assert(VIEW_SCAN_BOUND < CLIP_BULK_BYTES, "view slices fit below the required bulk crossover");
#define CLIP_MSG UINT32_C(0x434c4950) /* ASCII CLIP mailbox namespace */
typedef struct clip_block { struct clip_block *next; size_t bin; } clip_block;
typedef struct clip_job {
    editor *e;
    editor_buffer *owner;
    work_handle handle;
    piece_snapshot *snapshot;
    uint64_t off, token;
    size_t len, capacity, at, written;
    const uint8_t *raw;
    uint8_t *text;
    x11_clip_buf *owned[2]; /* CLIPBOARD and PRIMARY, per plat.h */
    int status;
    bool primary, paste, received, crlf;
} clip_job;
struct editor_clip {
    edit_arena arena;
    pthread_mutex_t mutex;
    bool ready, waiting, cutting, hitting, cluster_live, primary_waiting;
    bool cut_line;
    uint64_t cut_off, cut_len;
    bool find_paste, find_replacement;
    uint64_t hit_byte, hit_column, hit_target, cluster_start, pointer_ns;
    uint8_t cluster_first;
    utf8_cseg segment;
    clip_block *free[EDITOR_PIECE_BINS];
    x11_clip_buf *owned[2];
    clip_job primary, command;
    piece_snapshot *desired;
    editor_buffer *desired_owner;
    uint64_t desired_off, token, selection_id, selection_revision;
    view_selection selection;
    size_t desired_len;
    uint8_t *text;
    size_t len, capacity, inserted;
    uint64_t off, deleting;
    int which;
};
static uint64_t lesser(uint64_t a, uint64_t b) { return a < b ? a : b; }
static uint64_t greater(uint64_t a, uint64_t b) { return a > b ? a : b; }
static void *clip_alloc(void *ctx, size_t bytes)
{
    editor_clip *c = ctx;
    size_t capacity = sizeof(clip_block), bin = 0;
    if (bytes > SIZE_MAX - sizeof(clip_block)) return NULL;
    while (capacity < bytes + sizeof(clip_block)) {
        if (capacity > SIZE_MAX / 2 || bin + 1 == EDITOR_PIECE_BINS) return NULL;
        capacity *= 2; bin++;
    }
    pthread_mutex_lock(&c->mutex);
    clip_block *block = c->free[bin];
    if (block) c->free[bin] = block->next;
    else block = edit_arena_alloc(&c->arena, capacity, _Alignof(clip_block));
    if (block) block->bin = bin;
    pthread_mutex_unlock(&c->mutex);
    return block ? block + 1 : NULL;
}
static void clip_free(void *ctx, void *ptr, size_t bytes)
{
    (void)bytes;
    if (!ptr) return;
    editor_clip *c = ctx;
    clip_block *block = (clip_block *)ptr - 1;
    pthread_mutex_lock(&c->mutex);
    block->next = c->free[block->bin]; c->free[block->bin] = block;
    pthread_mutex_unlock(&c->mutex);
}
int editor_clip_init(editor *e)
{
    editor_clip *c = edit_arena_alloc(&e->arena, sizeof *c, _Alignof(editor_clip));
    if (!c) return EDITOR_ERR_MEMORY;
    memset(c, 0, sizeof *c); e->clipboard = c;
    if (pthread_mutex_init(&c->mutex, NULL)) return EDITOR_ERR_MEMORY;
    c->ready = true;
    /* Reservation derives from the platform's existing live-byte admission
     * bound: owned transport + editor receive staging. (P) Each received LF
     * may expand to the CR and LF bytes of CRLF. Power-of-two recycling
     * needs at most double each request, including its block/blob headers. */
    return edit_arena_init(&c->arena, 2u * (X11_CLIP_OWN_BUDGET +
        X11_CLIP_DATA_MAX * (sizeof "\r\n" - 1u) +
        sizeof(clip_block) + sizeof *c)) ? EDITOR_ERR_MEMORY : 0;
}
static x11_clip_buf *new_owned(editor_clip *c, size_t len)
{ return x11_clip_buf_new_alloc(len, clip_alloc, clip_free, c); }
static int publish_owned(editor *e, int which, x11_clip_buf *b)
{
    if (!b) return EDITOR_ERR_MEMORY;
    if (e->has_platform) {
        if (e->cfg.on_io) e->cfg.on_io(e->cfg.hook_ctx, true);
        int rc = x11_clip_set_buf(&e->platform, which, b);
        if (e->cfg.on_io) e->cfg.on_io(e->cfg.hook_ctx, false);
        return rc ? EDITOR_ERR_CAPACITY : 0;
    }
    editor_clip *c = e->clipboard;
    x11_clip_buf_free(c->owned[which]); c->owned[which] = b;
    return 0;
}
static void normalize_part(clip_job *j, size_t end)
{
    while (j->at < end) {
        size_t at = j->at++;
        /* Preserve UTF-8, invalid bytes and lone CR. (P) Strip the CR of a
         * CRLF pair, then emit every LF in the captured destination style.
         * Lookahead uses the whole input, including across worker slices. */
        if (j->raw[at] == '\r' && at + 1 < j->len && j->raw[at + 1] == '\n') continue;
        if (j->crlf && j->raw[at] == '\n') j->text[j->written++] = '\r';
        j->text[j->written++] = j->raw[at];
    }
}
static void clipboard_worker(work_ctx *ctx)
{
    clip_job *j = ctx->arg;
    if (work_should_stop(ctx)) return;
    if (!j->paste && !j->owned[PLAT_CLIP_PRIMARY]) {
        j->owned[PLAT_CLIP_PRIMARY] = new_owned(j->e->clipboard, j->len);
        if (!j->primary) j->owned[PLAT_CLIP_CLIPBOARD] = new_owned(j->e->clipboard, j->len);
        if (!j->owned[PLAT_CLIP_PRIMARY] || (!j->primary && !j->owned[PLAT_CLIP_CLIPBOARD]))
            j->status = EDITOR_ERR_MEMORY;
    }
    size_t take = j->len - j->at;
    if (take > VIEW_SCAN_BOUND) take = VIEW_SCAN_BOUND;
    if (!j->status) {
        if (j->paste) normalize_part(j, j->at + take);
        else {
            uint8_t *p = x11_clip_buf_data(j->owned[PLAT_CLIP_PRIMARY]) + j->at;
            j->status = piece_snapshot_read(j->snapshot, j->off + j->at, p, take);
            if (!j->status && !j->primary)
                memcpy(x11_clip_buf_data(j->owned[PLAT_CLIP_CLIPBOARD]) + j->at, p, take);
            j->at += take;
        }
    }
    if (!j->status && j->at < j->len) { (void)work_continue(ctx); return; }
    work_msg msg = {.kind = CLIP_MSG};
    if (!work_publish(ctx, &msg)) (void)work_continue(ctx);
}
static void clipboard_result(const work_msg *msg, void *ctx)
{
    clip_job *j = ctx;
    if (msg->kind == CLIP_MSG) j->received = true;
}
static int submit_job(editor *e, clip_job *j)
{
    j->e = e;
    j->handle = work_submit(&e->pool, (work_job){clipboard_worker, j, 0, WORK_BULK});
    if (!j->handle.epoch) return EDITOR_ERR_CAPACITY;
    (void)work_mailbox_bind(&e->pool, j->handle, 0, clipboard_result, j);
    return 0;
}
static void clear_job(editor *e, clip_job *j)
{
    if (j->handle.epoch) (void)work_mailbox_bind(&e->pool, j->handle, 0, NULL, NULL);
    piece_snapshot_release(j->snapshot);
    x11_clip_buf_free(j->owned[PLAT_CLIP_CLIPBOARD]);
    x11_clip_buf_free(j->owned[PLAT_CLIP_PRIMARY]);
    if (j->text) clip_free(e->clipboard, j->text, j->capacity);
    memset(j, 0, sizeof *j);
}
void editor_clip_stop(editor *e)
{
    editor_clip *c = e->clipboard;
    if (!c) return;
    work_cancel(&e->pool, c->primary.handle); work_cancel(&e->pool, c->command.handle);
    /* Shutdown only: snapshots pin buffer arenas until physical completion. */
    while (!work_handle_finished(&e->pool, c->primary.handle) ||
           !work_handle_finished(&e->pool, c->command.handle)) {
        struct timespec pause = {0, 1000000}; /* same close-time poll as open.c */
        (void)nanosleep(&pause, NULL);
    }
    clear_job(e, &c->primary); clear_job(e, &c->command);
    piece_snapshot_release(c->desired); c->desired = NULL;
}
void editor_clip_close(editor *e)
{
    editor_clip *c = e->clipboard;
    if (!c) return;
    x11_clip_buf_free(c->owned[PLAT_CLIP_CLIPBOARD]); x11_clip_buf_free(c->owned[PLAT_CLIP_PRIMARY]);
    if (c->ready) pthread_mutex_destroy(&c->mutex);
    edit_arena_free(&c->arena); e->clipboard = NULL;
}
static int copy_small(editor *e, uint64_t off, size_t len, bool primary)
{
    editor_clip *c = e->clipboard;
    x11_clip_buf *p = new_owned(c, len), *b = primary ? NULL : new_owned(c, len);
    if (!p || (!primary && !b)) { x11_clip_buf_free(p); x11_clip_buf_free(b); return EDITOR_ERR_MEMORY; }
    int rc = piece_read(e->tree, off, x11_clip_buf_data(p), len);
    if (rc) { x11_clip_buf_free(p); x11_clip_buf_free(b); return rc; }
    if (b) memcpy(x11_clip_buf_data(b), x11_clip_buf_data(p), len);
    if (b) rc = publish_owned(e, PLAT_CLIP_CLIPBOARD, b);
    int pr = publish_owned(e, PLAT_CLIP_PRIMARY, p);
    return rc ? rc : pr;
}
void editor_clip_selection(editor *e)
{
    editor_clip *c = e->clipboard;
    if (!c || !e->buffer || e->buffer->source_stale || e->buffer->pending_open || editor_clip_busy(e)) return;
    view_selection s = e->v.state.selection;
    if (s.cursor == s.anchor) return; /* X convention: collapsing keeps PRIMARY. */
    if (c->selection_id == e->buffer->id && c->selection_revision == e->buffer->revision &&
        c->selection.cursor == s.cursor && c->selection.anchor == s.anchor) return;
    uint64_t off = lesser(s.cursor, s.anchor), length = greater(s.cursor, s.anchor) - off;
    if (length > X11_CLIP_DATA_MAX) { e->stats.rejected_commands++; return; }
    c->token++;
    piece_snapshot_release(c->desired); c->desired = NULL;
    int rc = 0;
    if (length <= VIEW_SCAN_BOUND) rc = copy_small(e, off, (size_t)length, true);
    else {
        c->desired = piece_snapshot_take(e->tree);
        if (!c->desired) rc = EDITOR_ERR_MEMORY;
        c->desired_owner = e->buffer;
        c->desired_off = off; c->desired_len = (size_t)length;
    }
    if (rc) { e->stats.rejected_commands++; return; }
    c->selection_id = e->buffer->id; c->selection_revision = e->buffer->revision; c->selection = s;
}
bool editor_clip_busy(const editor *e)
{
    const editor_clip *c = e->clipboard;
    return c && (c->waiting || c->command.handle.epoch || e->action == EDITOR_ACTION_CLIP);
}
bool editor_clip_pending(const editor *e)
{
    const editor_clip *c = e->clipboard;
    return c && (editor_clip_busy(e) || c->primary.handle.epoch || c->desired);
}
bool editor_clip_runnable(const editor *e)
{
    const editor_clip *c = e->clipboard;
    return c && ((c->desired && !c->primary.handle.epoch) ||
        (c->primary.handle.epoch && c->primary.received) ||
        (c->command.handle.epoch && c->command.received));
}
bool editor_clip_holds(const editor *e, const editor_buffer *b)
{
    const editor_clip *c = e->clipboard;
    return c && ((c->desired && c->desired_owner == b) ||
        (c->primary.snapshot && c->primary.owner == b) ||
        (c->command.snapshot && c->command.owner == b));
}
static int begin_command(editor *e, uint64_t t0_ns)
{
    e->old_selection = e->v.state.selection; e->old_cursor = e->old_selection.cursor;
    e->key_ns = trace_now_ns(); e->edit_time_ns = t0_ns ? t0_ns : e->key_ns;
    undo_break_burst(e->undo);
    editor_restart_blink(e, e->key_ns);
    uint64_t seq = ++e->stats.input_sequence;
    if (e->cfg.on_ingress) e->cfg.on_ingress(e->cfg.hook_ctx, seq, e->key_ns);
    int rc = editor_begin_frame(e); if (rc) return rc;
    if (!e->frame.first_sequence) { e->frame.first_sequence = seq; e->frame.ingress_ns = e->key_ns; }
    e->frame.last_sequence = seq;
    trace_record_at(e->edit_time_ns, TRACE_T0_INGRESS, e->grid.frame_id);
    trace_record_at(e->key_ns, TRACE_T1_DEQUEUE, e->grid.frame_id);
    return 0;
}
static int start_replace(editor *e, uint8_t *text, size_t len, size_t capacity)
{
    editor_clip *c = e->clipboard;
    uint64_t off = lesser(e->old_selection.cursor, e->old_selection.anchor);
    uint64_t old = greater(e->old_selection.cursor, e->old_selection.anchor) - off;
    if (c->cut_line && c->cutting) { off = c->cut_off; old = c->cut_len; }
    c->waiting = false;
    if (e->buffer->source_stale || e->buffer->pending_open || editor_read_only(e)) { /* edit-n95 */
        clip_free(c, text, capacity); e->stats.rejected_commands++; return 0;
    }
    if (!old && !len) { clip_free(c, text, capacity); return 0; }
    int rc = editor_replace_begin(e, len);
    if (rc) { clip_free(c, text, capacity); return rc; }
    c->text = text; c->capacity = capacity; c->len = len; c->inserted = 0;
    c->off = off; c->deleting = old; e->action = EDITOR_ACTION_CLIP;
    c->cut_line = false;
    return editor_clip_continue(e);
}
static int middle_continue(editor *e);
int editor_clip_continue(editor *e)
{
    if (e->clipboard->hitting) return middle_continue(e);
    editor_clip *c = e->clipboard;
    /* Every mutation is bounded by the view module's existing capture bound.
     * The surrounding editor loop checks input/time and pumps journal bytes. */
    if (e->journal && (e->op_count == EDITOR_STAGE_OPS ||
        EDITOR_STAGE_BYTES - e->stage_used < VIEW_MUTATION_BOUND)) return 0;
    if (c->deleting) {
        uint64_t take = lesser(c->deleting, VIEW_MUTATION_BOUND);
        int rc = editor_replace_delete(e, c->off, take); if (rc) return rc;
        c->deleting -= take;
    } else if (c->inserted < c->len) {
        size_t take = c->len - c->inserted;
        if (take > VIEW_MUTATION_BOUND) take = VIEW_MUTATION_BOUND;
        int rc = editor_replace_insert(e, c->off + c->inserted, c->text + c->inserted, take);
        if (rc) return rc;
        c->inserted += take;
    }
    if (c->deleting || c->inserted < c->len) return 0;
    uint64_t target = c->off + c->len;
    clip_free(c, c->text, c->capacity); c->text = NULL;
    e->action = EDITOR_ACTION_NONE;
    return editor_replace_end(e, target);
}
static int receive_text(editor *e, const uint8_t *raw, size_t len)
{
    editor_clip *c = e->clipboard;
    if (c->find_paste) {
        c->waiting=false; c->find_paste=false;
        return editor_find_paste(e,raw,len,c->find_replacement);
    }
    if (len > X11_CLIP_DATA_MAX || (len && !raw)) { c->waiting = false; e->stats.rejected_commands++; return 0; }
    if (!raw) { c->waiting = false; return 0; }
    file_eol ending = e->buffer->file ?
        file_prefix_info_of(e->buffer->file)->dominant : e->buffer->paste_eol;
    bool crlf = ending == FILE_EOL_CRLF;
    /* (P) Worst case: each LF becomes CRLF. Capture style/capacity on the
     * UI thread so the worker never reads the mutable destination tree. */
    size_t expansion = crlf ? sizeof "\r\n" - 1u : sizeof "\n" - 1u;
    if (len > SIZE_MAX / expansion) { c->waiting = false; e->stats.rejected_commands++; return 0; }
    size_t capacity = len * expansion;
    uint8_t *text = len ? clip_alloc(c, capacity) : NULL;
    if (len && !text) { c->waiting = false; e->stats.rejected_commands++; return 0; }
    clip_job *j = &c->command;
    *j = (clip_job){.paste = true, .raw = raw, .text = text, .len = len,
        .capacity = capacity, .crlf = crlf};
    if (len > VIEW_SCAN_BOUND) { /* includes all required bulk pastes */
        int rc = submit_job(e, j);
        if (!rc) return 0;
        clear_job(e, j); c->waiting = false; e->stats.rejected_commands++; return 0;
    }
    normalize_part(j, len);
    size_t normalized = j->written; j->text = NULL; memset(j, 0, sizeof *j);
    return start_replace(e, text, normalized, capacity);
}
static int request_paste(editor *e, int which, bool find_field)
{
    editor_clip *c = e->clipboard;
    c->find_paste=find_field;
    if (find_field) c->find_replacement=findui_get_state(&e->find).replacement_focus;
    c->which = which; c->waiting = true;
    if (which == PLAT_CLIP_PRIMARY && (c->desired || c->primary.handle.epoch)) {
        c->primary_waiting = true; return 0;
    }
    c->primary_waiting = false;
    if (e->has_platform) {
        if (e->cfg.on_io) e->cfg.on_io(e->cfg.hook_ctx, true);
        int rc = plat_clip_request(&e->platform, which);
        if (e->cfg.on_io) e->cfg.on_io(e->cfg.hook_ctx, false);
        if (rc) { c->waiting = false; e->stats.rejected_commands++; }
        return 0;
    }
    x11_clip_buf *b = c->owned[which];
    /* Null backend's lengths are captured on publication in x11-compatible
     * blobs; the pure buffer accessor is shared with the native transport. */
    return receive_text(e, b ? x11_clip_buf_data(b) : NULL, b ? x11_clip_buf_len(b) : 0);
}
int editor_clip_find_paste(editor *e)
{
    if (editor_clip_busy(e)) return 0;
    return request_paste(e,PLAT_CLIP_CLIPBOARD,true);
}
int editor_clip_find_copy(editor *e, const uint8_t *bytes, size_t length)
{
    /* Field capacity already bounds this copy. Reuse the clipboard arena's
     * recycler and transport ownership; no document snapshot or mutation. */
    editor_clip *c=e->clipboard;
    x11_clip_buf *b=new_owned(c,length);
    if (!b) return EDITOR_ERR_MEMORY;
    memcpy(x11_clip_buf_data(b),bytes,length);
    return publish_owned(e,PLAT_CLIP_CLIPBOARD,b);
}
int editor_clip_action(editor *e, keys_action action, const plat_event *ev)
{
    if (!tabs_count(&e->tabs) || editor_clip_busy(e)) return 0;
    int rc = begin_command(e, ev->t0_ns); if (rc) return rc;
    trace_input_key(e->edit_time_ns, TRACE_IN_KEY_DOWN, ev->keysym, ev->mods,
        ev->repeat, (const char *)ev->utf8, ev->utf8_len);
    if (action == KEYS_ACTION_PASTE) { e->clipboard->cut_line = false; return request_paste(e, PLAT_CLIP_CLIPBOARD, false); }
    uint64_t off = lesser(e->old_selection.cursor, e->old_selection.anchor);
    uint64_t length = greater(e->old_selection.cursor, e->old_selection.anchor) - off;
    bool whole_line = !length;
    if (whole_line) {
        view_edit_plan line;
        rc = view_delete_line(&e->v, NULL, 0, &line);
        if (rc == PIECE_ERR_NOMEM) {
            e->stats.unimplemented_actions++; e->unimplemented_frame = e->grid.frame_id; return 0;
        }
        if (rc) return rc;
        off = line.offset; length = line.old_len;
    }
    if (!length) return 0;
    if (length > X11_CLIP_DATA_MAX) { e->stats.rejected_commands++; return 0; }
    editor_clip *c = e->clipboard;
    c->cutting = action == KEYS_ACTION_CUT;
    c->cut_line = whole_line && c->cutting;
    c->cut_off = off; c->cut_len = length;
    c->token++; /* A prior selection worker cannot overwrite explicit copy. */
    piece_snapshot_release(c->desired); c->desired = NULL;
    if (length <= VIEW_SCAN_BOUND) {
        rc = copy_small(e, off, (size_t)length, false);
        if (rc) { e->stats.rejected_commands++; return 0; }
        return c->cutting ? start_replace(e, NULL, 0, 0) : 0;
    }
    clip_job *j = &c->command;
    *j = (clip_job){.owner = e->buffer, .snapshot = piece_snapshot_take(e->tree), .off = off, .len = (size_t)length};
    rc = j->snapshot ? submit_job(e, j) : EDITOR_ERR_MEMORY;
    if (rc) { clear_job(e, j); e->stats.rejected_commands++; }
    return 0;
}
static int finish_hit(editor *e, uint64_t byte)
{
    editor_clip *c = e->clipboard;
    c->hitting = false; e->action = EDITOR_ACTION_NONE;
    e->old_selection = e->v.state.selection; e->old_cursor = e->old_selection.cursor;
    e->v.state.selection.cursor = e->v.state.selection.anchor = byte;
    e->v.state.selection.preferred_col = VIEW_PREFERRED_UNSET;
    e->v.state.visual_end = false;
    undo_break_burst(e->undo);
    int rc = editor_refresh_cursor(e, e->old_cursor); if (rc) return rc;
    rc = begin_command(e, c->pointer_ns); if (rc) return rc;
    return request_paste(e, PLAT_CLIP_PRIMARY, false);
}
static int middle_continue(editor *e)
{
    editor_clip *c = e->clipboard;
    if (e->buffer->source_stale || e->buffer->pending_open) {
        c->hitting = false; e->action = EDITOR_ACTION_NONE; return 0;
    }
    uint64_t total = piece_len(e->tree);
    uint8_t bytes[VIEW_WINDOW]; size_t len = (size_t)lesser(total - c->hit_byte, sizeof bytes);
    if (!len) return finish_hit(e, c->hit_byte);
    int rc = piece_read(e->tree, c->hit_byte, bytes, len); if (rc) return rc;
    bool eof = c->hit_byte + len == total;
    size_t at = 0;
    while (at < len) {
        if (!c->cluster_live) {
            if (c->hit_column >= c->hit_target || bytes[at] == '\n' ||
                (bytes[at] == '\r' && at + 1 < len && bytes[at + 1] == '\n'))
                return finish_hit(e, c->hit_byte);
            if (bytes[at] == '\r' && at + 1 == len && c->hit_byte + 1 < total) {
                uint8_t next;
                rc = piece_read(e->tree, c->hit_byte + 1, &next, sizeof next);
                if (rc) return rc;
                if (next == '\n') return finish_hit(e, c->hit_byte);
            }
            utf8_cseg_init(&c->segment); c->cluster_live = true;
            c->cluster_start = c->hit_byte; c->cluster_first = bytes[at];
        }
        size_t used = 0; int width = 0;
        rc = utf8_cluster_step(&c->segment, bytes + at, len - at, len - at, eof, &used, &width);
        at += used; c->hit_byte += used;
        if (rc != UTF8_G_END) return 0; /* preserve decoding state across windows */
        uint64_t cells = c->cluster_first == '\t' ? e->lay.tab - c->hit_column % e->lay.tab : (uint64_t)width;
        if (cells > c->hit_target - c->hit_column) return finish_hit(e, c->cluster_start);
        c->hit_column += cells; c->cluster_live = false;
    }
    return 0;
}
static int middle_target(editor *e, const plat_event *ev, bool *accepted)
{
    *accepted = false;
    if (ev->x < 0 || ev->y < 0) return 0;
    uint32_t row = (uint32_t)ev->y / e->grid.dims.cell_h;
    uint32_t col = (uint32_t)ev->x / e->grid.dims.cell_w;
    if (row < e->tab_rows || row - e->tab_rows >= e->text_grid.dims.rows ||
        col >= e->text_grid.dims.cols) return 0;
    row -= e->tab_rows;
    uint64_t byte = e->row_byte[row];
    if (byte == LAYOUT_VOID_ROW || byte > piece_len(e->tree)) return 0;
    editor_clip *c = e->clipboard;
    uint64_t column = col > e->lay.gw ? col - e->lay.gw : 0;
    c->hit_byte = byte; c->hit_column = 0;
    if (e->v.state.wrap) {
        const layout_wrap_row *r = &e->lay.wrap_rows[row];
        c->hit_column = r->column;
        column = column > r->indent ? column - r->indent : 0;
        c->hit_target = r->column + column;
    } else c->hit_target = e->v.state.hscroll + column;
    c->hitting = true; c->cluster_live = false; e->action = EDITOR_ACTION_CLIP;
    *accepted = true; return 0;
}
int editor_clip_event(editor *e, const plat_event *ev)
{
    editor_clip *c = e->clipboard;
    if (ev->kind == PLAT_EV_BUTTON) {
        if (!ev->press || e->buffer->source_stale || e->buffer->pending_open || editor_clip_busy(e)) return 0;
        c->pointer_ns = ev->t0_ns;
        bool accepted; int rc = middle_target(e, ev, &accepted); if (rc || !accepted) return rc;
        if (c->hitting) return editor_clip_continue(e);
        rc = begin_command(e, ev->t0_ns); if (rc) return rc;
        return request_paste(e, PLAT_CLIP_PRIMARY, false);
    }
    if (e->buffer->source_stale || e->buffer->pending_open) { c->waiting = false; return 0; }
    if (!c->waiting || ev->clip_which != c->which || ev->code == 2) return 0;
    if (!ev->clip_ok || ev->code) { c->waiting = false; return 0; }
    size_t len = 0; const uint8_t *raw = plat_clip_data(&e->platform, &len);
    return receive_text(e, raw, len);
}
int editor_clip_poll(editor *e)
{
    editor_clip *c = e->clipboard;
    if (!c) return 0;
    clip_job *p = &c->primary, *j = &c->command;
    if (p->handle.epoch && p->received && work_handle_finished(&e->pool, p->handle)) {
        bool installed = false;
        if (!p->status && p->token == c->token && !p->owner->source_stale && !p->owner->retired) {
            x11_clip_buf *b = p->owned[PLAT_CLIP_PRIMARY]; p->owned[PLAT_CLIP_PRIMARY] = NULL;
            installed = publish_owned(e, PLAT_CLIP_PRIMARY, b) == 0;
            if (!installed) e->stats.rejected_commands++;
        }
        if (c->primary_waiting && p->token == c->token && !installed) {
            c->primary_waiting = false; c->waiting = false;
        }
        clear_job(e, p);
    }
    if (c->desired && !p->handle.epoch) {
        *p = (clip_job){.primary = true, .owner = c->desired_owner, .snapshot = c->desired, .off = c->desired_off,
            .len = c->desired_len, .token = c->token}; c->desired = NULL;
        if (submit_job(e, p)) {
            clear_job(e, p); e->stats.rejected_commands++;
            if (c->primary_waiting) { c->primary_waiting = false; c->waiting = false; }
        }
    }
    if (c->primary_waiting && !c->desired && !p->handle.epoch)
        return request_paste(e, PLAT_CLIP_PRIMARY, false);
    if (j->handle.epoch && j->received && work_handle_finished(&e->pool, j->handle)) {
        int rc = j->status;
        if (j->paste) {
            uint8_t *text = j->text; size_t len = j->written, capacity = j->capacity; j->text = NULL;
            clear_job(e, j);
            if (rc) { clip_free(c, text, capacity); c->waiting = false; return rc; }
            return start_replace(e, text, len, capacity);
        }
        if (!rc) {
            x11_clip_buf *b = j->owned[PLAT_CLIP_CLIPBOARD]; j->owned[PLAT_CLIP_CLIPBOARD] = NULL;
            rc = publish_owned(e, PLAT_CLIP_CLIPBOARD, b);
            b = j->owned[PLAT_CLIP_PRIMARY]; j->owned[PLAT_CLIP_PRIMARY] = NULL;
            int pr = publish_owned(e, PLAT_CLIP_PRIMARY, b); if (!rc) rc = pr;
        }
        clear_job(e, j);
        if (rc) { e->stats.rejected_commands++; return 0; }
        if (c->cutting) return start_replace(e, NULL, 0, 0);
    }
    editor_clip_selection(e);
    return 0;
}
