#include "edit/edit.h"

#include <string.h>

#include "base/base.h"
#include "journal/journal.h"

#ifdef EDIT_TEST_HOOKS
#include "edit_test.h"
#endif

/* (G) v1 UNDO_BURST_NS at the oracle pin, src/undo/undo.h:67. */
#define EDIT_BURST_NS UINT64_C(300000000)

typedef enum edit_operand { EDIT_ERASE, EDIT_LITERAL, EDIT_BASE_RANGE } edit_operand;

typedef struct edit_op {
    uint64_t off;
    piece_span source; /* The erase uses only length. */
    edit_operand kind;
} edit_op;

typedef struct edit_plan {
    uint64_t old_len, len, prefix, suffix;
    edit_selection_state selection;
    journal_member_claim wire;
    size_t count;
} edit_plan;

typedef struct edit_burst {
    /* A zero extent seals the burst. Every eligible command has bytes. */
    uint64_t off, extent, receipt;
    edit_kind kind;
} edit_burst;

typedef struct edit_bookmark {
    uint64_t id, position;
    bool reachable;
} edit_bookmark;

typedef struct edit_pending {
    edit_plan plan;
    undo_history before;
    edit_burst burst;
    base_arena storage;
    /* Inline DELETE + INSERT for a single replacement (P). */
    edit_op local[2];
    piece_snapshot *candidate;
    journal_command *command;
    size_t emitted;
    uint64_t skip;
    enum { EDIT_NEEDS_PAYLOAD, EDIT_FILLED_PAYLOAD } loan;
} edit_pending;

typedef enum edit_replay_step {
    EDIT_APPLY_GROUP, EDIT_SIZE_GROUP, EDIT_DESCRIBE_GROUP, EDIT_EMIT_GROUP
} edit_replay_step;

typedef struct edit_replaying {
    edit_pending pending;
    undo_replay_direction direction;
    edit_replay_step step;
    const undo_state *target;
    undo_splice_iter iter;
    undo_prepared_splice splice;
    size_t span, filled;
    uint64_t destination;
    uint64_t position;
    bool range_allowed;
} edit_replaying;

typedef struct edit_final {
    edit_boundary boundary;
    edit_change change;
    journal_request *request;
} edit_final;

struct edit_buffer {
    base_arena mem;
    piece_tree *tree;
    undo_log *undo;
    journal *journal;
    uint64_t owner, generation, thread, high_id;
    piece_validate_fn validate;
    void *ctx;
    edit_boundary published;
    edit_bookmark saved, cutoff;
    edit_burst burst;
    edit_phase phase;
    bool in_dispatch;
    union {
        edit_pending open;
        edit_replaying replay;
        edit_final final;
        edit_change ready;
    } state;
#ifdef EDIT_TEST_HOOKS
    size_t scratch_high;
#endif
};

static void edit_require(const edit_buffer *b)
{
    ASSERT(b && b->thread == os_thread_self() && !b->in_dispatch);
}

static bool edit_complete(const edit_buffer *b)
{
    switch (b->phase) {
    case EDIT_IDLE:
    case EDIT_READY:
        return true;
    case EDIT_OPEN:
    case EDIT_REPLAYING:
    case EDIT_FINAL_PENDING:
        return false;
    }
    ASSERT(false);
    return false;
}

static uint64_t edit_length(const edit_buffer *b)
{
    return piece_length(piece_snapshot_view(b->published.snapshot));
}

static uint64_t edit_position(undo_history h)
{
    ASSERT(h.undo_groups <= UINT64_MAX - h.evicted_groups);
    return h.evicted_groups + h.undo_groups;
}

static edit_result edit_undo_result(undo_err e)
{
    switch (e) {
    case UNDO_OK: return EDIT_OK;
    case UNDO_MORE: return EDIT_MORE;
    case UNDO_ERR_NOMEM: return EDIT_NOMEM;
    case UNDO_ERR_RANGE: return EDIT_RANGE;
    case UNDO_ERR_CONFLICT: return EDIT_CONFLICT;
    }
    ASSERT(false);
    return EDIT_RANGE;
}

static edit_result edit_piece_result(piece_err e)
{
    switch (e) {
    case PIECE_OK: return EDIT_OK;
    case PIECE_ERR_NOMEM: return EDIT_NOMEM;
    case PIECE_ERR_RANGE: return EDIT_RANGE;
    }
    ASSERT(false);
    return EDIT_RANGE;
}

static edit_result edit_journal_result(journal_err e)
{
    switch (e) {
    case JOURNAL_OK: return EDIT_OK;
    case JOURNAL_WAIT: return EDIT_WAIT;
    case JOURNAL_NOMEM: return EDIT_NOMEM;
    case JOURNAL_LIMIT: return EDIT_LIMIT;
    case JOURNAL_SOURCE: return EDIT_CONFLICT;
    case JOURNAL_DAMAGED:
    case JOURNAL_IO: return EDIT_SUSPENDED;
    case JOURNAL_SUPERSEDED: break; /* A command is never superseded. */
    }
    ASSERT(false);
    return EDIT_SUSPENDED;
}

static edit_result edit_admission(const edit_buffer *b)
{
    switch (journal_state(b->journal)) {
    case JOURNAL_HEALTHY: return EDIT_OK;
    case JOURNAL_RECOVERING:
    case JOURNAL_PARENT_PENDING: return EDIT_WAIT;
    case JOURNAL_GAP:
    case JOURNAL_WRITEBACK_LOST:
    case JOURNAL_NAME_UNCERTAIN: return EDIT_SUSPENDED;
    }
    ASSERT(false);
    return EDIT_SUSPENDED;
}

edit_result edit_create(edit_buffer **out, const edit_options *o)
{
    ASSERT(out && o && o->tree && o->journal && o->buffer_id &&
           o->history_cap_bytes && o->current_id);
    bool mapped = piece_view_backing(piece_tree_view(o->tree)) != NULL;
    ASSERT(mapped ? o->validate != NULL : o->original_generation == 0);
    *out = NULL;
    base_arena mem;
    if (base_arena_init(&mem, sizeof(edit_buffer)) != 0) return EDIT_NOMEM;
    edit_buffer *b = (edit_buffer *)mem.base;
    *b = (edit_buffer){
        .mem = mem, .tree = o->tree, .journal = o->journal, .owner = o->buffer_id,
        .generation = o->original_generation, .thread = os_thread_self(),
        .validate = o->validate, .ctx = o->ctx,
        .high_id = o->current_id > o->saved_id ? o->current_id : o->saved_id,
        .published.content_id = o->current_id,
        .saved = {o->saved_id, 0, o->saved_id == o->current_id},
        .phase = EDIT_IDLE
    };
    undo_err e = undo_create(&b->undo, b->tree, o->history_cap_bytes, b->validate, b->ctx);
    if (e != UNDO_OK) {
        base_arena_free(&mem);
        return edit_undo_result(e);
    }
    piece_err take = piece_snapshot_take(b->tree, &b->published.snapshot);
    if (take != PIECE_OK) {
        undo_destroy(b->undo);
        base_arena_free(&mem);
        return edit_piece_result(take);
    }
    *out = b;
    return EDIT_OK;
}

void edit_destroy(edit_buffer *b)
{
    edit_require(b);
    ASSERT(b->phase == EDIT_IDLE && !b->cutoff.id);
    b->in_dispatch = true;
    undo_destroy(b->undo);
    piece_snapshot_release(b->published.snapshot);
    piece_close(b->tree);
    base_arena mem = b->mem;
    base_arena_free(&mem);
}

edit_phase edit_read(const edit_buffer *b, edit_boundary *out)
{
    edit_require(b);
    ASSERT(out);
    *out = b->published;
    return b->phase;
}

static bool edit_selection_fits(edit_selection s, uint64_t len)
{
    return s.cursor <= len && s.anchor <= len;
}

static bool edit_same_selection(edit_selection a, edit_selection b)
{
    return a.cursor == b.cursor && a.anchor == b.anchor;
}

edit_result edit_select(edit_buffer *b, edit_selection_state s)
{
    edit_require(b);
    ASSERT(s.mark_kind == EDIT_MARK_ABSENT || s.mark_kind == EDIT_MARK_SET);
    if (b->phase != EDIT_IDLE) return EDIT_WAIT;
    uint64_t len = edit_length(b);
    if (!edit_selection_fits(s.active, len) ||
        (s.mark_kind == EDIT_MARK_SET && !edit_selection_fits(s.mark, len))) return EDIT_RANGE;
    edit_selection_state old = b->published.selection;
    if (!edit_same_selection(old.active, s.active) || s.mark_kind != old.mark_kind ||
        (s.mark_kind == EDIT_MARK_SET && !edit_same_selection(old.mark, s.mark)))
        b->burst.extent = 0;
    b->published.selection = s;
    return EDIT_OK;
}

static undo_state edit_encode_selection(edit_selection s)
{
    /* P: two u64s fill undo's opaque selection slot. */
    _Static_assert(2 * sizeof(uint64_t) == UNDO_STATE_BYTES, "undo selection");
    undo_state out;
    memcpy(out.bytes, &s.cursor, sizeof s.cursor);
    memcpy(out.bytes + sizeof s.cursor, &s.anchor, sizeof s.anchor);
    return out;
}

static edit_selection edit_decode_selection(const undo_state *s)
{
    edit_selection out;
    memcpy(&out.cursor, s->bytes, sizeof out.cursor);
    memcpy(&out.anchor, s->bytes + sizeof out.cursor, sizeof out.anchor);
    return out;
}

static uint64_t edit_move_endpoint(uint64_t pos, uint64_t off, uint64_t remove, uint64_t add)
{
    if (pos < off) return pos;
    if (pos - off <= remove) return off + add;
    return pos - remove + add;
}

static edit_plan edit_new_plan(const edit_buffer *b)
{
    uint64_t len = edit_length(b);
    return (edit_plan){.old_len = len, .len = len, .prefix = len, .suffix = len,
                       .selection = b->published.selection};
}

static void edit_describe_change(edit_plan *p, uint64_t off, uint64_t remove, uint64_t add)
{
    if (off < p->prefix) p->prefix = off;
    uint64_t after_cut = p->len - off - remove;
    if (after_cut < p->suffix) p->suffix = after_cut;
    p->len = p->len - remove + add;
    if (p->selection.mark_kind == EDIT_MARK_SET) {
        edit_selection *m = &p->selection.mark;
        m->cursor = edit_move_endpoint(m->cursor, off, remove, add);
        m->anchor = edit_move_endpoint(m->anchor, off, remove, add);
    }
}

static bool edit_count_op(edit_plan *p, edit_op op)
{
    uint64_t count = 1, bytes = 0;
    size_t largest = 0;
    switch (op.kind) {
    case EDIT_ERASE:
        bytes = largest = JOURNAL_DELETE_BYTES;
        break;
    case EDIT_BASE_RANGE:
        bytes = largest = JOURNAL_RESTORE_BASE_RANGE_BYTES;
        break;
    case EDIT_LITERAL: {
        uint64_t len = op.source.len;
        size_t overhead = JOURNAL_HEADER_BYTES + sizeof(uint64_t);
        count = 1 + (len - 1) / JOURNAL_LITERAL_LIMIT;
        if (count > (UINT64_MAX - len) / overhead) return false;
        bytes = len + count * overhead;
        largest = len > JOURNAL_LITERAL_LIMIT ? JOURNAL_PAGE_BYTES : (size_t)bytes;
        break;
    }
    }
    /* Leave room for the two group markers in both ledgers. */
    if (p->count == SIZE_MAX || count > UINT64_MAX - 2 - p->wire.count ||
        bytes > UINT64_MAX - 2 * JOURNAL_MARKER_BYTES - p->wire.framed_bytes) return false;
    p->count++;
    p->wire.count += count;
    p->wire.framed_bytes += bytes;
    if (largest > p->wire.largest_frame) p->wire.largest_frame = largest;
    return true;
}

static edit_result edit_reserve_ops(edit_pending *p)
{
    if (p->plan.count <= sizeof p->local / sizeof *p->local) return EDIT_OK;
    if (p->plan.count > SIZE_MAX / sizeof(edit_op)) return EDIT_LIMIT;
    return base_arena_init(&p->storage, p->plan.count * sizeof(edit_op)) == 0 ? EDIT_OK : EDIT_NOMEM;
}

static edit_op *edit_ops(edit_pending *p)
{
    return p->storage.base ? (edit_op *)p->storage.base : p->local;
}

static edit_result edit_abort(edit_buffer *b, edit_pending *p, edit_result result)
{
    /* D1': journal drops its command, then all edit-owned provisional
     * pins leave before undo closes the checkpoint. */
    if (p->command) journal_command_abort(p->command);
    if (p->candidate) piece_snapshot_release(p->candidate);
    undo_abort(b->undo);
    if (p->storage.base) base_arena_free(&p->storage);
    b->phase = EDIT_IDLE;
    return result;
}

static journal_record_type edit_record_type(edit_operand kind, bool grouped)
{
    switch (kind) {
    case EDIT_ERASE: return grouped ? JOURNAL_REC_COMMAND_DELETE : JOURNAL_REC_DELETE;
    case EDIT_LITERAL: return grouped ? JOURNAL_REC_COMMAND_INSERT : JOURNAL_REC_INSERT;
    case EDIT_BASE_RANGE:
        return grouped ? JOURNAL_REC_COMMAND_RESTORE_BASE_RANGE : JOURNAL_REC_RESTORE_BASE_RANGE;
    }
    ASSERT(false);
    return JOURNAL_REC_DELETE;
}

static edit_result edit_begin_journal(edit_buffer *b, edit_pending *p)
{
    if (!p->candidate) {
        piece_err take = piece_snapshot_take(b->tree, &p->candidate);
        if (take != PIECE_OK) return edit_piece_result(take);
    }
    journal_member_claim claim = p->plan.wire;
    journal_record_type final = JOURNAL_REC_COMMAND_COMMIT;
    size_t payload = 0;
    if (claim.count == 1) {
        payload = claim.largest_frame - JOURNAL_HEADER_BYTES;
        final = edit_record_type(edit_ops(p)[0].kind, false);
        claim = (journal_member_claim){0};
    }
    return edit_journal_result(journal_command_begin(b->journal, b->owner, claim, final,
                                                      payload, &p->command));
}

static size_t edit_payload_size(edit_op op, uint64_t skip)
{
    switch (op.kind) {
    case EDIT_ERASE: return JOURNAL_DELETE_BYTES - JOURNAL_HEADER_BYTES;
    case EDIT_BASE_RANGE: return JOURNAL_RESTORE_BASE_RANGE_BYTES - JOURNAL_HEADER_BYTES;
    case EDIT_LITERAL: {
        uint64_t left = op.source.len - skip;
        return sizeof op.off + (left > JOURNAL_LITERAL_LIMIT ? JOURNAL_LITERAL_LIMIT : (size_t)left);
    }
    }
    ASSERT(false);
    return 0;
}

static edit_result edit_fill(edit_buffer *b, edit_pending *p, edit_op op, uint8_t *window, size_t size)
{
#ifdef EDIT_TEST_HOOKS
    size_t framed = JOURNAL_HEADER_BYTES + size;
    if (b->scratch_high < framed) b->scratch_high = framed;
#endif
    size_t encoded = 0;
    journal_codec_err e = JOURNAL_CODEC_INVALID;
    switch (op.kind) {
    case EDIT_ERASE:
        e = journal_delete_encode(window, size, &encoded, &(journal_delete){op.off, op.source.len});
        break;
    case EDIT_BASE_RANGE:
        e = journal_restore_encode(window, size, &encoded,
              &(journal_restore){op.off, b->generation, op.source.src, op.source.len});
        break;
    case EDIT_LITERAL: {
        /* INSERT is a little-endian destination followed by bytes. Build
         * it in the loan so there is no second encoding/copy buffer. */
        uint64_t off = op.off + p->skip;
        for (size_t i = 0; i < sizeof off; i++, off >>= 8) window[i] = (uint8_t)off;
        for (size_t pos = sizeof off; pos < size;) {
            size_t got;
            piece_read_result read = piece_snapshot_read_span(p->candidate, op.source,
                p->skip + (pos - sizeof off), window + pos, size - pos, &got, b->validate, b->ctx);
            switch (read) {
            case PIECE_READ_RANGE: return EDIT_RANGE;
            case PIECE_READ_CONFLICT: return EDIT_CONFLICT;
            case PIECE_READ_OK: break;
            }
            ASSERT(got && got <= size - pos);
            pos += got;
        }
        return EDIT_OK;
    }
    }
    ASSERT(e == JOURNAL_CODEC_OK && encoded == size);
    return EDIT_OK;
}

static void edit_trim_bookmark(edit_bookmark *m, undo_history before, undo_history after)
{
    uint64_t start = after.evicted_groups;
    uint64_t end = edit_position(after) + after.redo_groups;
    uint64_t prior = edit_position(before);
    if (m->reachable && (m->position < start || m->position > end || m->position > prior ||
                         (m->position == prior && before.group_serial == after.group_serial)))
        m->reachable = false;
}

static uint64_t edit_replay_id(const edit_buffer *b, uint64_t position)
{
    if (b->saved.reachable && b->saved.position == position) return b->saved.id;
    if (b->cutoff.reachable && b->cutoff.position == position) return b->cutoff.id;
    return 0;
}

static edit_result edit_adopt_final(edit_buffer *b)
{
    ASSERT(b->phase == EDIT_FINAL_PENDING);
    edit_final final = b->state.final;
    journal_result result = journal_request_result(final.request);
    switch (result.publication) {
    case JOURNAL_ABSENT_EFFECT:
    case JOURNAL_POSSIBLE:
        return EDIT_SUSPENDED;
    case JOURNAL_VISIBLE:
    case JOURNAL_DURABLE:
        journal_request_release(final.request);
        piece_snapshot_release(b->published.snapshot);
        b->published = final.boundary;
        b->state.ready = final.change;
        b->phase = EDIT_READY;
        return EDIT_OK;
    }
    ASSERT(false);
    return EDIT_SUSPENDED;
}

static edit_result edit_commit(edit_buffer *b, edit_pending *p)
{
    if (journal_state(b->journal) != JOURNAL_HEALTHY) return EDIT_SUSPENDED;
    bool replay = b->phase == EDIT_REPLAYING;
    uint64_t id = replay ? edit_replay_id(b, b->state.replay.position) : 0;
    if (!id && b->high_id == UINT64_MAX) return edit_abort(b, p, EDIT_LIMIT);

    undo_state after = edit_encode_selection(p->plan.selection.active);
    undo_err close = undo_commit(b->undo, replay ? b->state.replay.target : &after);
    if (close != UNDO_OK) return edit_abort(b, p, edit_undo_result(close));
    journal_request *request = journal_command_final(p->command);

    if (!replay) {
        undo_history history = undo_get_history(b->undo);
        edit_trim_bookmark(&b->saved, p->before, history);
        edit_trim_bookmark(&b->cutoff, p->before, history);
    }
    b->burst = p->burst;
    if (!id) id = ++b->high_id;
    uint64_t revision = b->published.revision + 1;
    uint64_t exterior = p->plan.prefix + p->plan.suffix;
    edit_final final = {
        .boundary = {p->candidate, p->plan.selection, id, revision},
        .change = {p->plan.prefix, p->plan.old_len - exterior, p->plan.len - exterior, revision},
        .request = request
    };
    if (p->storage.base) base_arena_free(&p->storage);
    b->state.final = final;
    b->phase = EDIT_FINAL_PENDING;
    return edit_adopt_final(b);
}

static edit_result edit_emit(edit_buffer *b, edit_pending *p)
{
    if (p->emitted == p->plan.count) return edit_commit(b, p);
    /* A turn advances at most one member (G, A28 fair service). The last
     * healthy member and RAM commit/final may complete in the same turn. */
    edit_op op = edit_ops(p)[p->emitted];
    bool grouped = p->plan.wire.count > 1;
    size_t size = edit_payload_size(op, p->skip);
    if (p->loan == EDIT_NEEDS_PAYLOAD) {
        uint8_t *payload = journal_command_payload(p->command);
        if (!payload) return EDIT_SUSPENDED;
        edit_result result = edit_fill(b, p, op, payload, size);
        if (result != EDIT_OK) return edit_abort(b, p, result);
        p->loan = EDIT_FILLED_PAYLOAD;
    }
    if (grouped) {
        journal_err e = journal_command_member(p->command, edit_record_type(op.kind, true), size);
        if (e == JOURNAL_WAIT)
            return journal_state(b->journal) == JOURNAL_HEALTHY ? EDIT_MORE : EDIT_SUSPENDED;
        if (e != JOURNAL_OK) return edit_abort(b, p, edit_journal_result(e));
    }
    if (op.kind == EDIT_LITERAL) p->skip += size - sizeof op.off;
    if (op.kind != EDIT_LITERAL || p->skip == op.source.len) {
        p->skip = 0;
        p->emitted++;
    }
    p->loan = EDIT_NEEDS_PAYLOAD;
    if (p->emitted == p->plan.count) return edit_commit(b, p);
    return journal_state(b->journal) == JOURNAL_HEALTHY ? EDIT_MORE : EDIT_SUSPENDED;
}

static bool edit_join(const edit_buffer *b, edit_burst next, undo_history before)
{
    edit_burst last = b->burst;
    if (!next.extent || !last.extent || next.kind != last.kind || before.redo_groups ||
        next.receipt < last.receipt || next.receipt - last.receipt > EDIT_BURST_NS) return false;
    switch (next.kind) {
    case EDIT_TYPE: return next.off == last.off + last.extent;
    case EDIT_DELETE_FORWARD: return next.off == last.off;
    case EDIT_BACKSPACE: return next.off + next.extent == last.off;
    }
    ASSERT(false);
    return false;
}

static edit_result edit_forward(edit_buffer *b, const edit_splice *s, size_t n,
                                  edit_selection after, edit_burst burst)
{
    edit_plan plan = edit_new_plan(b);
    uint64_t add_room = PIECE_SPAN_ADD - 1 - piece_add_length(b->tree);
    for (size_t i = 0; i < n; i++) {
        edit_splice op = s[i];
        ASSERT(op.bytes || !op.length);
        if (op.off > plan.len || op.remove > plan.len - op.off ||
            op.length > PIECE_MAX_LENGTH - (plan.len - op.remove) ||
            op.length > add_room) return EDIT_RANGE;
        if (!op.remove && !op.length) continue;
        add_room -= op.length;
        edit_describe_change(&plan, op.off, op.remove, op.length);
        if ((op.remove && !edit_count_op(&plan, (edit_op){op.off, {0, op.remove}, EDIT_ERASE})) ||
            (op.length && !edit_count_op(&plan, (edit_op){op.off, {0, op.length}, EDIT_LITERAL})))
            return EDIT_LIMIT;
    }
    if (!edit_selection_fits(after, plan.len)) return EDIT_RANGE;
    if (!plan.count) {
        if (!edit_same_selection(after, b->published.selection.active)) b->burst.extent = 0;
        b->published.selection.active = after;
        return EDIT_OK;
    }
    undo_history before = undo_get_history(b->undo);
    if (b->published.revision == UINT64_MAX || b->high_id == UINT64_MAX ||
        before.group_serial == UINT64_MAX || edit_position(before) == UINT64_MAX) return EDIT_LIMIT;
    edit_result ready = edit_admission(b);
    if (ready != EDIT_OK) return ready;

    plan.selection.active = after;
    b->state.open = (edit_pending){.plan = plan, .before = before, .burst = burst};
    b->phase = EDIT_OPEN;
    edit_pending *p = &b->state.open;
    edit_result reserve = edit_reserve_ops(p);
    if (reserve != EDIT_OK) return edit_abort(b, p, reserve);
    undo_state start = edit_encode_selection(b->published.selection.active);
    undo_err e = undo_begin(b->undo, &start, edit_join(b, burst, before));
    if (e != UNDO_OK) return edit_abort(b, p, edit_undo_result(e));
    edit_op *out = edit_ops(p);
    for (size_t i = 0; i < n; i++) {
        edit_splice op = s[i];
        if (!op.remove && !op.length) continue;
        /* An ORIGINAL or ADD inverse may append a short literal before
         * undo appends the input. Only the final input bytes name it. */
        if (op.length >= PIECE_SPAN_ADD - piece_add_length(b->tree))
            return edit_abort(b, p, EDIT_RANGE);
        e = undo_replace(b->undo, op.off, op.remove, op.bytes, op.length);
        if (e != UNDO_OK) return edit_abort(b, p, edit_undo_result(e));
        if (op.remove) *out++ = (edit_op){op.off, {0, op.remove}, EDIT_ERASE};
        if (op.length) {
            uint64_t src = piece_add_length(b->tree) - op.length;
            *out++ = (edit_op){op.off, {PIECE_SPAN_ADD | src, op.length}, EDIT_LITERAL};
        }
    }
    ASSERT((size_t)(out - edit_ops(p)) == plan.count);
    edit_result result = edit_begin_journal(b, p);
    if (result != EDIT_OK) return edit_abort(b, p, result);
    return edit_emit(b, p);
}

edit_result edit_apply_plan(edit_buffer *b, const edit_splice *s, size_t n, edit_selection after)
{
    edit_require(b);
    ASSERT(s || !n);
    if (b->phase != EDIT_IDLE) return EDIT_WAIT;
    b->in_dispatch = true;
    edit_result result = edit_forward(b, s, n, after, (edit_burst){0});
    if (result == EDIT_OK && b->phase == EDIT_IDLE) b->burst.extent = 0;
    b->in_dispatch = false;
    return result;
}

edit_result edit_replace(edit_buffer *b, edit_kind kind, edit_splice s, uint64_t receipt)
{
    edit_require(b);
    ASSERT(s.bytes || !s.length);
    bool ordinary = false;
    switch (kind) {
    case EDIT_TYPE: ordinary = !s.remove; break;
    case EDIT_DELETE_FORWARD:
    case EDIT_BACKSPACE: ordinary = !s.length; break;
    }
    ASSERT(kind == EDIT_TYPE || kind == EDIT_DELETE_FORWARD || kind == EDIT_BACKSPACE);
    if (b->phase != EDIT_IDLE) return EDIT_WAIT;
    if (s.length > UINT64_MAX - s.off) return EDIT_RANGE;
    const edit_selection current = b->published.selection.active;
    edit_burst burst = {.off = s.off, .kind = kind, .receipt = receipt};
    if (ordinary && current.cursor == current.anchor)
        burst.extent = s.remove ? s.remove : s.length;
    uint64_t caret = s.off + s.length;
    b->in_dispatch = true;
    edit_result result = edit_forward(b, &s, 1, (edit_selection){caret, caret}, burst);
    b->in_dispatch = false;
    return result;
}

/* Enumerate a prepared group twice: exact sizing, then owned descriptors.
 * A step describes one operation, so a large inverse with many spans
 * cannot postpone a deadline check. All cursors die with the checkpoint. */
static bool edit_next_replay_op(edit_replaying *r, edit_op *out)
{
    if (r->span == r->splice.span_count) {
        if (!undo_splice_iter_next(&r->iter, &r->splice)) return false;
        r->span = 0;
        r->destination = r->splice.off;
        if (r->splice.removed) {
            *out = (edit_op){r->destination, {0, r->splice.removed}, EDIT_ERASE};
            return true;
        }
    }
    ASSERT(r->span < r->splice.span_count);
    piece_span source = undo_splice_span(&r->splice, r->span++);
    edit_operand kind = r->range_allowed && !(source.src & PIECE_SPAN_ADD) ?
                        EDIT_BASE_RANGE : EDIT_LITERAL;
    *out = (edit_op){r->destination, source, kind};
    r->destination += source.len;
    return true;
}

static bool edit_due(uint64_t deadline)
{
    return deadline != OS_DEADLINE_NEVER && os_now_ns() >= deadline;
}

static void edit_start_walk(edit_buffer *b, edit_replaying *r, edit_replay_step step)
{
    r->step = step;
    r->splice = (undo_prepared_splice){0};
    r->span = 0;
    undo_splice_iter_init(&r->iter, b->undo);
}

static edit_result edit_replay_progress(edit_buffer *b, uint64_t deadline)
{
    edit_replaying *r = &b->state.replay;
    edit_pending *p = &r->pending;
    switch (r->step) {
    case EDIT_APPLY_GROUP: {
        undo_err e = undo_replay_prepare(b->undo, r->direction, deadline, &r->target);
        if (e == UNDO_MORE) return EDIT_MORE;
        if (e != UNDO_OK) return edit_abort(b, p, edit_undo_result(e));
        ASSERT(r->target);
        p->plan.selection.active = edit_decode_selection(r->target);
        r->range_allowed = b->generation && journal_range_allowed(b->journal, b->generation);
        edit_start_walk(b, r, EDIT_SIZE_GROUP);
        if (edit_due(deadline)) return EDIT_MORE;
    }
    /* fall through */
    case EDIT_SIZE_GROUP: {
        edit_op op;
        while (edit_next_replay_op(r, &op)) {
            if (!edit_count_op(&p->plan, op)) return edit_abort(b, p, EDIT_LIMIT);
            /* Delete and then insert each run at its growing end. This
             * composes to inclusive rebasing through the whole splice. */
            uint64_t removed = op.kind == EDIT_ERASE ? op.source.len : 0;
            uint64_t added = op.kind == EDIT_ERASE ? 0 : op.source.len;
            edit_describe_change(&p->plan, op.off, removed, added);
            if (edit_due(deadline)) return EDIT_MORE;
        }
        edit_result reserve = edit_reserve_ops(p);
        if (reserve != EDIT_OK) return edit_abort(b, p, reserve);
        edit_start_walk(b, r, EDIT_DESCRIBE_GROUP);
    }
    /* fall through */
    case EDIT_DESCRIBE_GROUP: {
        edit_op op;
        while (edit_next_replay_op(r, &op)) {
            ASSERT(r->filled < p->plan.count);
            edit_ops(p)[r->filled++] = op;
            if (edit_due(deadline)) return EDIT_MORE;
        }
        ASSERT(r->filled == p->plan.count && r->filled > 0);
        r->step = EDIT_EMIT_GROUP;
        break;
    }
    case EDIT_EMIT_GROUP:
        break;
    }
    if (!p->command) {
        edit_result ready = edit_admission(b);
        if (ready != EDIT_OK) return ready == EDIT_WAIT ? EDIT_MORE : ready;
        edit_result begin = edit_begin_journal(b, p);
        if (begin == EDIT_WAIT) return EDIT_MORE;
        if (begin != EDIT_OK) return edit_abort(b, p, begin);
    }
    return edit_emit(b, p);
}

edit_result edit_replay(edit_buffer *b, undo_replay_direction direction, uint64_t deadline)
{
    edit_require(b);
    ASSERT(direction == UNDO_REPLAY_UNDO || direction == UNDO_REPLAY_REDO);
    if (b->phase != EDIT_IDLE) return EDIT_WAIT;
    undo_history before = undo_get_history(b->undo);
    bool reverse = direction == UNDO_REPLAY_UNDO;
    if ((reverse ? before.undo_groups : before.redo_groups) == 0) {
        b->burst.extent = 0;
        return EDIT_OK;
    }
    uint64_t from = edit_position(before);
    uint64_t position = reverse ? from - 1 : from + 1;
    if (b->published.revision == UINT64_MAX ||
        (b->high_id == UINT64_MAX && !edit_replay_id(b, position))) return EDIT_LIMIT;
    edit_result ready = edit_admission(b);
    if (ready != EDIT_OK) return ready;
    b->state.replay = (edit_replaying){
        .pending = {.plan = edit_new_plan(b), .before = before},
        .direction = direction, .step = EDIT_APPLY_GROUP, .position = position
    };
    b->phase = EDIT_REPLAYING;
    b->in_dispatch = true;
    edit_result result = edit_replay_progress(b, deadline);
    b->in_dispatch = false;
    return result;
}

edit_result edit_continue(edit_buffer *b, uint64_t deadline)
{
    edit_require(b);
    b->in_dispatch = true;
    edit_result result = EDIT_OK;
    switch (b->phase) {
    case EDIT_IDLE:
    case EDIT_READY:
        break;
    case EDIT_OPEN:
        result = edit_emit(b, &b->state.open);
        break;
    case EDIT_REPLAYING:
        result = edit_replay_progress(b, deadline);
        break;
    case EDIT_FINAL_PENDING:
        result = edit_adopt_final(b);
        break;
    }
    b->in_dispatch = false;
    return result;
}

bool edit_take_change(edit_buffer *b, edit_change *out)
{
    edit_require(b);
    ASSERT(out);
    if (b->phase != EDIT_READY) return false;
    *out = b->state.ready;
    b->phase = EDIT_IDLE;
    return true;
}

void edit_save_boundary(edit_buffer *b, edit_save_event event)
{
    edit_require(b);
    switch (event) {
    case EDIT_SAVE_CAPTURED:
        ASSERT(edit_complete(b) && !b->cutoff.id);
        b->burst.extent = 0;
        b->cutoff = (edit_bookmark){b->published.content_id,
                                   edit_position(undo_get_history(b->undo)), true};
        return;
    case EDIT_SAVE_INSTALLED:
        ASSERT(b->cutoff.id);
        b->saved = b->cutoff;
        b->cutoff = (edit_bookmark){0};
        return;
    case EDIT_SAVE_RELEASED:
        ASSERT(b->cutoff.id);
        b->cutoff = (edit_bookmark){0};
        return;
    case EDIT_SAVE_FORGOTTEN:
        b->saved = (edit_bookmark){0};
        return;
    }
    ASSERT(false);
}

edit_result edit_maintain(edit_buffer *b, size_t reclaim, size_t resolve, size_t undo)
{
    edit_require(b);
    if (!edit_complete(b)) return EDIT_WAIT;
    b->in_dispatch = true;
    (void)piece_reclaim(b->tree, reclaim);
    bool more;
    piece_err geometry = piece_lines_resolve(b->tree, resolve, &more);
    piece_snapshot *snapshot;
    piece_err take = piece_snapshot_take(b->tree, &snapshot);
    if (take == PIECE_OK) {
        piece_snapshot_release(b->published.snapshot);
        b->published.snapshot = snapshot;
    }
    piece_err topup = piece_replenish(b->tree);
    (void)undo_maintain(b->undo, undo);
    b->in_dispatch = false;
    return geometry == PIECE_OK && take == PIECE_OK && topup == PIECE_OK ? EDIT_OK : EDIT_NOMEM;
}

#ifdef EDIT_TEST_HOOKS
size_t edit_test_scratch_high(const edit_buffer *b)
{
    edit_require(b);
    return b->scratch_high;
}

undo_stats edit_test_undo_stats(const edit_buffer *b)
{
    edit_require(b);
    return undo_get_stats(b->undo);
}
#endif
