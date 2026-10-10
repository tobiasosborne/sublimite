/* Shared per-key injection/T4 accounting and structural verdict policy. */
#ifndef EDIT_EDITOR_ACCOUNTING_H
#define EDIT_EDITOR_ACCOUNTING_H
#include "editor/editor.h"
#include "base/base.h"
typedef struct key_sample {
    uint64_t injected, dequeued, submit, t4;
    uint32_t frame;
} key_sample;
typedef struct samples {
    editor_frame frame;
    uint64_t ingress, sequence;
    bool guard, suspended, measuring;
    size_t allocations;
    key_sample *keys;
    size_t count, completed;
    uint64_t first_sequence;
    bool invalid;
} samples;
static void ingress(void *ctx, uint64_t sequence, uint64_t ns)
{
    samples *s = ctx; s->ingress = ns; s->sequence = sequence;
    if (s->keys) {
        if (sequence < s->first_sequence || sequence - s->first_sequence >= s->count) s->invalid = true;
        else {
            key_sample *k = &s->keys[sequence - s->first_sequence];
            if (!k->injected || k->dequeued || ns < k->injected) s->invalid = true;
            k->dequeued = ns;
        }
    }
    if (s->measuring && !s->guard && !s->suspended) { edit_malloc_guard_begin(); s->guard = true; }
}
static void submitted(void *ctx, const editor_frame *f)
{
    samples *s = ctx;
    if (!f->last_sequence) return;
    if (s->guard) { s->allocations += edit_malloc_guard_end(); s->guard = false; }
    s->frame = *f;
    if (s->keys) {
        for (uint64_t seq = f->first_sequence; seq <= f->last_sequence; seq++) {
            if (seq < s->first_sequence) continue;
            if (seq - s->first_sequence >= s->count) { s->invalid = true; break; }
            key_sample *k = &s->keys[seq - s->first_sequence];
            if (k->submit || !k->dequeued || f->submit_ns < k->dequeued) s->invalid = true;
            k->submit = f->submit_ns; k->frame = f->id;
        }
    }
}
static void presented(void *ctx, const editor_frame *f)
{
    samples *s = ctx; if (f->last_sequence) s->frame = *f;
    if (s->keys && f->last_sequence) {
        for (uint64_t seq = f->first_sequence; seq <= f->last_sequence; seq++) {
            if (seq < s->first_sequence) continue;
            if (seq - s->first_sequence >= s->count) { s->invalid = true; break; }
            key_sample *k = &s->keys[seq - s->first_sequence];
            if (k->t4 || k->frame != f->id || f->present_ns < k->submit) s->invalid = true;
            else { k->t4 = f->present_ns; s->completed++; }
        }
    }
}
static void io_boundary(void *ctx, bool entering)
{
    samples *s = ctx;
    if (entering && s->guard) {
        s->allocations += edit_malloc_guard_end(); s->guard = false; s->suspended = true;
    } else if (!entering && s->suspended) {
        edit_malloc_guard_begin(); s->guard = true; s->suspended = false;
    }
}
static int row_result(int timing, bool structural, bool track)
{
    return structural ? 1 : track ? 0 : timing;
}
static bool typing_structure(const samples *measured, editor_stats before, editor_stats after, size_t count)
{
    return measured->invalid || measured->completed != count || measured->allocations != 0 ||
        measured->guard || measured->suspended || after.journal_error != 0 ||
        after.mutations - before.mutations != count || after.journal_records - before.journal_records != count;
}
#endif
