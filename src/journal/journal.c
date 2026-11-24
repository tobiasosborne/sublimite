/* L2 journal runtime, live writer (L2-2b): open, the typing-path append,
 * the worker transport, the self-contained checkpoint and rotation.
 * Design, invariants and derivations: docs/decisions/L2-2b-journal-runtime.md.
 *
 * Ownership. Everything in struct journal belongs to the UI thread
 * except the job, which the UI fills before work_submit and reads back
 * after the worker's completion message (bound to this journal) or
 * after work_handle_finished; the worker writes only the job's outputs
 * and the file region a lease grants it, and never touches the job after
 * publishing. One descriptor serves both threads: positioned writes and
 * fdatasync may run concurrently, and it is closed only with no job. */
#include "journal/internal.h"

#include <string.h>

/* ---- storage ----
 * One base arena per journal: the struct, the UI's record buffer (also
 * the held record of an APPEND_GAP), the UI's payload buffer, and the
 * worker's staging and payload buffers. (P) Each record buffer holds one
 * record of at most JOURNAL_MAX_RECORD_BYTES (wire section 3); a payload
 * is at most that less the 32 B header. Alignment padding is at most
 * 63 B per allocation, five allocations. Pages are backed on first touch, so
 * the typing path touches only what one record needs. */
#define REC_CAP ((size_t)JOURNAL_MAX_RECORD_BYTES)
#define PAYLOAD_CAP ((size_t)JOURNAL_MAX_RECORD_BYTES - JOURNAL_HEADER_BYTES)
#define ARENA_ALIGN 64u

typedef enum group_phase { GROUP_OUTSIDE = 0, GROUP_EMPTY, GROUP_OPEN } group_phase;

typedef enum job_kind {
    JOB_SYNC = 0,    /* fdatasync through a barrier */
    JOB_DIRSYNC,     /* repeat a rotation's directory barrier */
    JOB_REPAIR,      /* lease: rewrite the held record */
    JOB_CHECKPOINT,  /* lease: one CHECKPOINT_BY_VALUE group */
    JOB_ROTATE       /* lease: a complete new image */
} job_kind;

typedef enum job_outcome {
    OUTCOME_DONE = 0,
    OUTCOME_FAILED,          /* changed nothing the UI relies on */
    OUTCOME_FAILED_DIRTY,    /* a lease left bytes it could not remove */
    OUTCOME_RENAMED_NO_DIRSYNC
} job_outcome;

typedef struct job {
    job_kind kind;
    /* inputs, fixed at submission */
    os_file fd;              /* the live image (shared descriptor) */
    os_dir *dir;
    const char *name;
    uint64_t start;          /* lease: append position at the grant */
    uint64_t sequence;       /* lease: next sequence at the grant */
    uint64_t barrier_end, barrier_sequence;   /* SYNC */
    const uint8_t *held;                      /* REPAIR */
    size_t held_len;
    piece_snapshot *text;                     /* CHECKPOINT */
    uint64_t buffer_id;
    journal_checkpoint descriptor;
    bool has_target;
    journal_target target;
    uint8_t target_path[JOURNAL_PATH_MAX_BYTES];
    const journal_rotation *rotation;         /* ROTATE */
    uint64_t generation_high_water, buffer_high_water;
    uint8_t *stage, *payload;
    /* outputs, written by the worker before it publishes */
    job_outcome outcome;
    os_err err;
    int native;
    uint64_t end, next_sequence, commit_sequence;
    os_file new_fd;
} job;

struct journal {
    base_arena arena;
    work_pool *pool;
    os_dir dir;
    char name[OS_NAME_MAX];
    os_file fd;
    journal_options opt;
    uint64_t end;              /* append position (file offset) */
    uint64_t next_seq;         /* 0: the sequence space is exhausted */
    uint64_t written_seq, durable_seq, durable_end;
    uint64_t barrier_end;      /* end at the last barrier submitted */
    uint64_t unsynced_since;   /* pump's first sight of unsynced bytes, 0: none */
    bool sync_requested;
    uint64_t gen_hw, buf_hw;
    group_phase group;
    journal_transport transport;
    journal_suspension suspension;
    uint8_t *rec, *payload;
    size_t held_len;           /* APPEND_GAP: the record in rec */
    uint64_t held_off, held_seq;
    uint64_t checkpoint_commit;
    os_err last_error;
    int last_native;
    job job;
    work_handle handle;
};

static void wr64(uint8_t *p, uint64_t v)
{
    for (unsigned i = 0; i < 8; i++) p[i] = (uint8_t)(v >> (8 * i));
}

static uint64_t stream_offset(uint64_t file_offset)
{
    ASSERT(file_offset >= JOURNAL_PREAMBLE_BYTES);
    return file_offset - JOURNAL_PREAMBLE_BYTES;
}

static void note_error(journal *j, os_err e, int native)
{
    j->last_error = e;
    j->last_native = native;
}

/* ---- the typing path ---- */

/* Admission of one record of len bytes. member: inside an open group (or
 * its COMMIT), admitted past the budget so the group can always close. */
static journal_err admit(journal *j, size_t len, bool member)
{
    switch (j->transport) {
    case JOURNAL_TRANSPORT_LEASE: return JOURNAL_ERR_BUSY;
    case JOURNAL_TRANSPORT_IDLE:
    case JOURNAL_TRANSPORT_SYNC:
    case JOURNAL_TRANSPORT_RETIRING: break;
    }
    switch (j->suspension) {
    case JOURNAL_HEALTHY: break;
    case JOURNAL_APPEND_GAP:
    case JOURNAL_DIRECTORY_PENDING: return JOURNAL_ERR_BUSY;
    case JOURNAL_WRITEBACK_LOST:
    case JOURNAL_NEEDS_ROTATION: return JOURNAL_ERR_SUSPENDED;
    }
    if (j->next_seq == 0 || (!member && j->end + len > j->opt.budget_bytes)) {
        j->suspension = JOURNAL_NEEDS_ROTATION;
        return JOURNAL_ERR_FULL;
    }
    return JOURNAL_OK;
}

/* One os_file_pwrite_once of rec[0, len) at the append position (Q6). A
 * failed, short or empty write holds the record and its offset. */
static journal_err ui_write(journal *j, size_t len, uint64_t seq)
{
    size_t done = 0;
    os_err e = os_file_pwrite_once(&j->fd, j->rec, len, j->end, &done);
    if (e == OS_OK && done == len) {
        j->end += len;
        if (seq) j->written_seq = seq;
        return JOURNAL_OK;
    }
    note_error(j, e == OS_OK ? OS_E_IO : e, e == OS_OK ? 0 : os_native_error());
    j->held_len = len;
    j->held_off = j->end;
    j->held_seq = seq;
    j->suspension = JOURNAL_APPEND_GAP;
    return JOURNAL_ERR_IO;
}

static journal_err append_frame(journal *j, journal_record_type type, uint64_t buffer_id,
                                const uint8_t *payload, size_t plen)
{
    bool member = j->group != GROUP_OUTSIDE;
    journal_err a = admit(j, JOURNAL_HEADER_BYTES + plen, member);
    if (a != JOURNAL_OK) return a;
    size_t len = 0;
    journal_codec_err ce = journal_record_encode(j->rec, REC_CAP, &len, type, j->next_seq,
                                                 buffer_id, payload, plen,
                                                 stream_offset(j->end));
    ASSERT(ce == JOURNAL_CODEC_OK); /* payloads come from the codec's encoders */
    uint64_t seq = j->next_seq;
    j->next_seq = seq == UINT64_MAX ? 0 : seq + 1;
    journal_err e = ui_write(j, len, seq);
    if (j->group == GROUP_EMPTY && type != JOURNAL_REC_GROUP_BEGIN) j->group = GROUP_OPEN;
    return e;
}

journal_err journal_group_begin(journal *j)
{
    ASSERT(j != NULL && j->group == GROUP_OUTSIDE);
    journal_err e = append_frame(j, JOURNAL_REC_GROUP_BEGIN, 0, NULL, 0);
    if (e == JOURNAL_OK || e == JOURNAL_ERR_IO) j->group = GROUP_EMPTY;
    return e;
}

journal_err journal_group_commit(journal *j)
{
    ASSERT(j != NULL && j->group == GROUP_OPEN); /* groups are non-empty */
    journal_err e = append_frame(j, JOURNAL_REC_GROUP_COMMIT, 0, NULL, 0);
    if (e == JOURNAL_OK || e == JOURNAL_ERR_IO) j->group = GROUP_OUTSIDE;
    return e;
}

journal_err journal_insert_step(journal *j, uint64_t buffer_id, uint64_t dest,
                                const uint8_t *bytes, size_t len, size_t *progress)
{
    ASSERT(j != NULL && bytes != NULL && progress != NULL && len >= 1 && *progress < len);
    ASSERT(buffer_id != 0 && dest <= UINT64_MAX - (uint64_t)len);
    size_t n = len - *progress;
    if (n > JOURNAL_INSERT_SLICE_BYTES) n = JOURNAL_INSERT_SLICE_BYTES;
    /* INSERT payload (wire section 6): u64 destination, then the bytes. */
    wr64(j->payload, dest + *progress);
    memcpy(j->payload + 8, bytes + *progress, n);
    journal_err e = append_frame(j, JOURNAL_REC_INSERT, buffer_id, j->payload, 8 + n);
    if (e == JOURNAL_OK || e == JOURNAL_ERR_IO) *progress += n;
    if (e == JOURNAL_OK && *progress < len) return JOURNAL_MORE;
    return e;
}

journal_err journal_append_delete(journal *j, uint64_t buffer_id, uint64_t dest, uint64_t len)
{
    ASSERT(j != NULL);
    journal_delete d = {.destination = dest, .len = len};
    size_t n = 0;
    journal_codec_err ce = journal_delete_encode(j->payload, PAYLOAD_CAP, &n, &d);
    ASSERT(ce == JOURNAL_CODEC_OK); /* len >= 1, no overflow: caller's */
    return append_frame(j, JOURNAL_REC_DELETE, buffer_id, j->payload, n);
}

journal_err journal_append_restore(journal *j, uint64_t buffer_id, const journal_restore *r)
{
    ASSERT(j != NULL && r != NULL);
    size_t n = 0;
    journal_codec_err ce = journal_restore_encode(j->payload, PAYLOAD_CAP, &n, r);
    ASSERT(ce == JOURNAL_CODEC_OK);
    return append_frame(j, JOURNAL_REC_RESTORE_BASE_RANGE, buffer_id, j->payload, n);
}

journal_err journal_append(journal *j, journal_record_type type, uint64_t buffer_id,
                           const uint8_t *payload, size_t payload_len)
{
    ASSERT(j != NULL);
    ASSERT(type != JOURNAL_REC_PAD && type != JOURNAL_REC_GROUP_BEGIN &&
           type != JOURNAL_REC_GROUP_COMMIT && type != JOURNAL_REC_CHECKPOINT_BY_VALUE);
    ASSERT(payload_len <= PAYLOAD_CAP && (payload != NULL || payload_len == 0));
    return append_frame(j, type, buffer_id, payload, payload_len);
}

/* ---- allocators (section 5) ---- */
journal_err journal_new_generation_key(journal *j, uint64_t *key)
{
    ASSERT(j != NULL && key != NULL);
    if (j->gen_hw == UINT64_MAX) return JOURNAL_ERR_FULL;
    *key = ++j->gen_hw;
    return JOURNAL_OK;
}

journal_err journal_new_buffer_id(journal *j, uint64_t *id)
{
    ASSERT(j != NULL && id != NULL);
    if (j->buf_hw == UINT64_MAX) return JOURNAL_ERR_FULL;
    *id = ++j->buf_hw;
    return JOURNAL_OK;
}

journal_status journal_get_status(const journal *j)
{
    ASSERT(j != NULL);
    journal_status s = {
        .transport = j->transport,
        .suspension = j->suspension,
        .accepted_sequence = j->next_seq == 0 ? UINT64_MAX : j->next_seq - 1,
        .written_sequence = j->written_seq,
        .durable_sequence = j->durable_seq,
        .file_bytes = j->end,
        .durable_bytes = j->durable_end,
        .last_error = j->last_error,
        .last_native_error = j->last_native,
    };
    return s;
}

uint64_t journal_checkpoint_committed(const journal *j)
{
    ASSERT(j != NULL);
    return j->checkpoint_commit;
}

/* ---- worker side ---- */

/* Batches encoded records in the job's staging buffer and writes them
 * with restarting positioned writes (off the typing path). */
typedef struct stage_writer {
    os_file *fd;
    uint8_t *buf;
    size_t used;
    uint64_t file_off;  /* where buf[0] lands */
    uint64_t seq;       /* next sequence */
    work_ctx *ctx;
    os_err err;
} stage_writer;

static bool sw_flush(stage_writer *w)
{
    if (w->err != OS_OK) return false;
    if (w->used == 0) return true;
    w->err = journal_write_all(w->fd, w->buf, w->used, w->file_off);
    if (w->err != OS_OK) return false;
    w->file_off += w->used;
    w->used = 0;
    if (w->ctx && work_should_stop(w->ctx)) {
        w->err = OS_E_INTR;
        return false;
    }
    return true;
}

static bool sw_emit(stage_writer *w, journal_record_type type, uint64_t buffer_id,
                    const uint8_t *payload, size_t plen)
{
    if (w->err != OS_OK) return false;
    if (w->used + JOURNAL_HEADER_BYTES + plen > REC_CAP && !sw_flush(w)) return false;
    ASSERT(w->seq != 0); /* the lease's grant checked the sequence room */
    size_t len = 0;
    journal_codec_err ce = journal_record_encode(
        w->buf + w->used, REC_CAP - w->used, &len, type, w->seq, buffer_id, payload, plen,
        stream_offset(w->file_off + w->used));
    ASSERT(ce == JOURNAL_CODEC_OK);
    w->used += len;
    w->seq = w->seq == UINT64_MAX ? 0 : w->seq + 1;
    return true;
}

/* Sequential reads of a snapshot's text with a carry, so a run longer
 * than the bytes wanted is not lost between calls. */
typedef struct text_reader {
    piece_iter it;
    const uint8_t *p;
    size_t left;
} text_reader;

static void tr_seek(text_reader *r, const piece_view *v, uint64_t off, bool init)
{
    if (init) piece_iter_init(&r->it, v, off);
    else piece_iter_seek(&r->it, off);
    r->p = NULL;
    r->left = 0;
}

static void tr_read(text_reader *r, uint8_t *out, size_t n)
{
    while (n) {
        if (r->left == 0) {
            r->left = piece_iter_next(&r->it, &r->p);
            ASSERT(r->left != 0); /* the caller asks only within the text */
        }
        size_t k = n < r->left ? n : r->left;
        memcpy(out, r->p, k);
        out += k;
        r->p += k;
        r->left -= k;
        n -= k;
    }
}

/* INSERT slices of len bytes from r at destinations dest, dest + slice... */
static bool emit_bytes(stage_writer *w, job *jb, uint64_t buffer_id, uint64_t dest,
                       text_reader *r, uint64_t len)
{
    while (len) {
        size_t n = len < JOURNAL_INSERT_SLICE_BYTES ? (size_t)len : JOURNAL_INSERT_SLICE_BYTES;
        wr64(jb->payload, dest);
        tr_read(r, jb->payload + 8, n);
        if (!sw_emit(w, JOURNAL_REC_INSERT, buffer_id, jb->payload, 8 + n)) return false;
        dest += n;
        len -= n;
    }
    return true;
}

/* n is read through a pointer: the encoder that fills it is a sibling
 * argument, and C leaves the order of argument evaluation unspecified,
 * but every argument is evaluated before the body runs. */
static bool emit_payload(stage_writer *w, job *jb, journal_record_type type,
                         uint64_t buffer_id, journal_codec_err ce, const size_t *n)
{
    ASSERT(ce == JOURNAL_CODEC_OK); /* inputs are the caller's validated state */
    return sw_emit(w, type, buffer_id, jb->payload, *n);
}

/* A CHECKPOINT_BY_VALUE group of the whole text (wire sections 6, 7). */
static bool emit_checkpoint(stage_writer *w, job *jb, uint64_t buffer_id, piece_snapshot *text,
                            journal_checkpoint d, const journal_target *target)
{
    const piece_view *v = piece_snapshot_view(text);
    d.text_len = piece_length(v);
    if (d.covered_sequence == UINT64_MAX) d.covered_sequence = w->seq - 1; /* rotation */
    size_t n = 0;
    if (!sw_emit(w, JOURNAL_REC_GROUP_BEGIN, 0, NULL, 0)) return false;
    if (!emit_payload(w, jb, JOURNAL_REC_CHECKPOINT_BY_VALUE, buffer_id,
                      journal_checkpoint_encode(jb->payload, PAYLOAD_CAP, &n, &d), &n))
        return false;
    text_reader r;
    tr_seek(&r, v, 0, true);
    if (!emit_bytes(w, jb, buffer_id, 0, &r, d.text_len)) return false;
    if (target && !emit_payload(w, jb, JOURNAL_REC_TARGET, buffer_id,
                                journal_target_encode(jb->payload, PAYLOAD_CAP, &n, target), &n))
        return false;
    return sw_emit(w, JOURNAL_REC_GROUP_COMMIT, 0, NULL, 0);
}

static const journal_generation *find_generation(const journal_rotation *r, uint64_t key)
{
    for (size_t i = 0; i < r->generation_count; i++)
        if (r->generations[i].key == key) return &r->generations[i];
    return NULL;
}

/* BASE, then the text over its ORIGINAL (rotation BY_REFERENCE): kept
 * ORIGINAL spans in increasing order cost a DELETE of the gap before
 * them; ADD spans go by value; an ORIGINAL span out of order or repeated
 * (A14(c)) goes as RESTORE_BASE_RANGE, or by value when its generation
 * is VALUE_ONLY (A23 rule 3). The text so far is always text[0, dest)
 * followed by ORIGINAL[src, length). */
static bool emit_reference(stage_writer *w, job *jb, const journal_rotation_buffer *b)
{
    const journal_generation *g = NULL;
    uint64_t glen = 0;
    bool value_only = false;
    if (b->base_key != 0) {
        g = find_generation(jb->rotation, b->base_key);
        ASSERT(g != NULL); /* caller: BY_REFERENCE declares its generation */
        glen = g->identity.size;
        value_only = (g->flags & JOURNAL_GENERATION_VALUE_ONLY) != 0;
    }
    size_t n = 0;
    journal_base base = {.generation_key = b->base_key, .content_id = b->content_id};
    if (!emit_payload(w, jb, JOURNAL_REC_BASE, b->buffer_id,
                      journal_base_encode(jb->payload, PAYLOAD_CAP, &n, &base), &n))
        return false;
    const piece_view *v = piece_snapshot_view(b->text);
    uint64_t total = piece_length(v), pos = 0, dest = 0, src = 0;
    piece_iter it;
    piece_iter_init(&it, v, 0);
    text_reader r;
    tr_seek(&r, v, 0, true);
    piece_span sp;
    while (piece_iter_next_span(&it, total, &sp, NULL)) {
        bool add = (sp.src & PIECE_SPAN_ADD) != 0;
        uint64_t a = sp.src & ~PIECE_SPAN_ADD;
        if (!add && a >= src) {
            ASSERT(a + sp.len <= glen); /* the tree's ORIGINAL is the generation */
            if (a > src) {
                journal_delete d = {.destination = dest, .len = a - src};
                if (!emit_payload(w, jb, JOURNAL_REC_DELETE, b->buffer_id,
                                  journal_delete_encode(jb->payload, PAYLOAD_CAP, &n, &d), &n))
                    return false;
            }
            src = a + sp.len;
        } else if (!add && !value_only) {
            journal_restore rs = {.destination = dest, .generation_key = b->base_key,
                                  .source_offset = a, .len = sp.len};
            if (!emit_payload(w, jb, JOURNAL_REC_RESTORE_BASE_RANGE, b->buffer_id,
                              journal_restore_encode(jb->payload, PAYLOAD_CAP, &n, &rs), &n))
                return false;
        } else {
            tr_seek(&r, v, pos, false);
            if (!emit_bytes(w, jb, b->buffer_id, dest, &r, sp.len)) return false;
        }
        dest += sp.len;
        pos += sp.len;
    }
    if (src < glen) {
        journal_delete d = {.destination = dest, .len = glen - src};
        if (!emit_payload(w, jb, JOURNAL_REC_DELETE, b->buffer_id,
                          journal_delete_encode(jb->payload, PAYLOAD_CAP, &n, &d), &n))
            return false;
    }
    if (b->target && !emit_payload(w, jb, JOURNAL_REC_TARGET, b->buffer_id,
                                   journal_target_encode(jb->payload, PAYLOAD_CAP, &n, b->target), &n))
        return false;
    return true;
}

static void run_checkpoint(work_ctx *c, job *jb)
{
    stage_writer w = {.fd = &jb->fd, .buf = jb->stage, .file_off = jb->start,
                      .seq = jb->sequence, .ctx = c, .err = OS_OK};
    bool ok = emit_checkpoint(&w, jb, jb->buffer_id, jb->text, jb->descriptor,
                              jb->has_target ? &jb->target : NULL) && sw_flush(&w);
    if (ok) {
        jb->outcome = OUTCOME_DONE;
        jb->end = w.file_off;
        jb->next_sequence = w.seq;
        jb->commit_sequence = w.seq == 0 ? UINT64_MAX : w.seq - 1;
        return;
    }
    jb->err = w.err;
    jb->native = os_native_error();
    /* Nothing past start was acknowledged (the lease): remove the
     * partial group so no stale record can follow the next append. */
    jb->outcome = os_file_set_size(&jb->fd, jb->start) == OS_OK ? OUTCOME_FAILED
                                                                : OUTCOME_FAILED_DIRTY;
}

static void run_rotation(work_ctx *c, job *jb)
{
    const journal_rotation *r = jb->rotation;
    char tmp[OS_NAME_MAX];
    os_file f = OS_FILE_INIT;
    jb->outcome = OUTCOME_FAILED;
    jb->new_fd = (os_file)OS_FILE_INIT;
    os_err e = journal_create_sibling(jb->dir, jb->name, "rot", tmp, &f);
    if (e != OS_OK) {
        jb->err = e;
        jb->native = os_native_error();
        return;
    }
    stage_writer w = {.fd = &f, .buf = jb->stage, .file_off = 0, .seq = 1, .ctx = c,
                      .err = OS_OK};
    journal_preamble_encode(w.buf, jb->generation_high_water, jb->buffer_high_water);
    w.used = JOURNAL_PREAMBLE_BYTES;
    size_t n = 0;
    bool ok = true;
    for (size_t i = 0; ok && i < r->generation_count; i++)
        ok = emit_payload(&w, jb, JOURNAL_REC_GENERATION, 0,
                          journal_generation_encode(jb->payload, PAYLOAD_CAP, &n,
                                                    &r->generations[i]), &n);
    for (size_t i = 0; ok && i < r->buffer_count; i++) {
        const journal_rotation_buffer *b = &r->buffers[i];
        switch (b->mode) {
        case JOURNAL_ROTATE_BY_VALUE: {
            journal_checkpoint d = {.covered_sequence = UINT64_MAX, .content_id = b->content_id,
                                    .saved_content_id = b->saved_content_id, .flags = b->flags};
            ok = emit_checkpoint(&w, jb, b->buffer_id, b->text, d, b->target);
            break;
        }
        case JOURNAL_ROTATE_BY_REFERENCE:
            ok = emit_reference(&w, jb, b);
            break;
        }
        if (ok && b->view)
            ok = emit_payload(&w, jb, JOURNAL_REC_VIEW, b->buffer_id,
                              journal_view_encode(jb->payload, PAYLOAD_CAP, &n, b->view), &n);
    }
    if (ok && r->tabs)
        ok = emit_payload(&w, jb, JOURNAL_REC_TABS, 0,
                          journal_tabs_encode(jb->payload, PAYLOAD_CAP, &n, r->tabs,
                                              r->tab_count, r->tab_active), &n);
    /* The image ends on a block, like every synced boundary (section 4). */
    size_t pad = journal_pad_length(stream_offset(w.file_off + w.used));
    if (ok && pad) {
        memset(jb->payload, 0, pad - JOURNAL_HEADER_BYTES);
        if (w.used + pad > REC_CAP) ok = sw_flush(&w);
        if (ok) {
            size_t len = 0;
            journal_codec_err ce = journal_record_encode(
                w.buf + w.used, REC_CAP - w.used, &len, JOURNAL_REC_PAD, 0, 0, jb->payload,
                pad - JOURNAL_HEADER_BYTES, stream_offset(w.file_off + w.used));
            ASSERT(ce == JOURNAL_CODEC_OK);
            w.used += len;
        }
    }
    ok = ok && sw_flush(&w);
    if (ok) {
        e = os_file_sync(&f);
        ok = e == OS_OK;
        if (!ok) w.err = e;
    }
    if (ok) {
        e = os_rename_at(jb->dir, tmp, jb->dir, jb->name, OS_RENAME_REPLACE);
        ok = e == OS_OK;
        if (!ok) w.err = e;
    }
    if (!ok) {
        jb->err = w.err;
        jb->native = os_native_error();
        (void)os_unlink_at(jb->dir, tmp);
        os_file_close(&f);
        return;
    }
    jb->new_fd = f;
    jb->end = w.file_off;
    jb->next_sequence = w.seq;
    e = os_dir_sync(jb->dir);
    if (e != OS_OK) {
        jb->err = e;
        jb->native = os_native_error();
        jb->outcome = OUTCOME_RENAMED_NO_DIRSYNC;
        return;
    }
    jb->outcome = OUTCOME_DONE;
}

static void job_run(work_ctx *c)
{
    job *jb = c->arg;
    jb->err = OS_OK;
    jb->native = 0;
    os_err e;
    switch (jb->kind) {
    case JOB_SYNC:
        e = os_file_sync_data(&jb->fd);
        jb->outcome = e == OS_OK ? OUTCOME_DONE : OUTCOME_FAILED;
        if (e != OS_OK) {
            jb->err = e;
            jb->native = os_native_error();
        }
        break;
    case JOB_DIRSYNC:
        e = os_dir_sync(jb->dir);
        jb->outcome = e == OS_OK ? OUTCOME_DONE : OUTCOME_FAILED;
        if (e != OS_OK) {
            jb->err = e;
            jb->native = os_native_error();
        }
        break;
    case JOB_REPAIR:
        e = journal_write_all(&jb->fd, jb->held, jb->held_len, jb->start);
        jb->outcome = e == OS_OK ? OUTCOME_DONE : OUTCOME_FAILED;
        if (e != OS_OK) {
            jb->err = e;
            jb->native = os_native_error();
        }
        break;
    case JOB_CHECKPOINT: run_checkpoint(c, jb); break;
    case JOB_ROTATE: run_rotation(c, jb); break;
    }
    /* The wake-up only: the UI reads the outputs above. A dropped
     * message is covered by work_handle_finished in journal_pump. */
    work_msg m = {.kind = JOURNAL_MSG_DONE, .generation = c->generation};
    (void)work_publish(c, &m);
}

/* ---- UI side of the transport ---- */

static void adopt(journal *j)
{
    job *jb = &j->job;
    switch (j->transport) {
    case JOURNAL_TRANSPORT_SYNC:
    case JOURNAL_TRANSPORT_LEASE: break;
    case JOURNAL_TRANSPORT_IDLE:
    case JOURNAL_TRANSPORT_RETIRING: return;
    }
    if (jb->outcome != OUTCOME_DONE) note_error(j, jb->err, jb->native);
    switch (jb->kind) {
    case JOB_SYNC:
        if (jb->outcome == OUTCOME_DONE) {
            if (jb->barrier_sequence > j->durable_seq) j->durable_seq = jb->barrier_sequence;
            if (jb->barrier_end > j->durable_end) j->durable_end = jb->barrier_end;
        } else {
            j->suspension = JOURNAL_WRITEBACK_LOST;
        }
        break;
    case JOB_DIRSYNC:
        if (jb->outcome == OUTCOME_DONE && j->suspension == JOURNAL_DIRECTORY_PENDING) {
            j->suspension = JOURNAL_HEALTHY;
            j->durable_seq = j->written_seq;
            j->durable_end = j->end;
        }
        break;
    case JOB_REPAIR:
        if (jb->outcome == OUTCOME_DONE && j->suspension == JOURNAL_APPEND_GAP) {
            j->end = j->held_off + j->held_len;
            if (j->held_seq) j->written_seq = j->held_seq;
            j->held_len = 0;
            j->suspension = JOURNAL_HEALTHY;
        }
        break;
    case JOB_CHECKPOINT:
        switch (jb->outcome) {
        case OUTCOME_DONE:
            j->end = jb->end;
            j->next_seq = jb->next_sequence;
            j->written_seq = jb->commit_sequence;
            j->checkpoint_commit = jb->commit_sequence;
            break;
        case OUTCOME_FAILED: break;
        case OUTCOME_FAILED_DIRTY:
        case OUTCOME_RENAMED_NO_DIRSYNC:
            j->suspension = JOURNAL_NEEDS_ROTATION;
            break;
        }
        break;
    case JOB_ROTATE:
        switch (jb->outcome) {
        case OUTCOME_FAILED:
        case OUTCOME_FAILED_DIRTY: break;
        case OUTCOME_DONE:
        case OUTCOME_RENAMED_NO_DIRSYNC:
            os_file_close(&j->fd);
            j->fd = jb->new_fd;
            j->end = jb->end;
            j->next_seq = jb->next_sequence;
            j->written_seq = jb->next_sequence - 1;
            j->barrier_end = jb->end;
            j->held_len = 0;
            j->group = GROUP_OUTSIDE;
            j->checkpoint_commit = 0;
            j->unsynced_since = 0;
            if (jb->outcome == OUTCOME_DONE) {
                j->suspension = JOURNAL_HEALTHY;
                j->durable_seq = j->written_seq;
                j->durable_end = j->end;
            } else {
                j->suspension = JOURNAL_DIRECTORY_PENDING;
                j->durable_seq = 0;
                j->durable_end = JOURNAL_PREAMBLE_BYTES;
            }
            break;
        }
        break;
    }
    if (jb->text) {
        piece_snapshot_release(jb->text);
        jb->text = NULL;
    }
    j->transport = JOURNAL_TRANSPORT_RETIRING;
}

static void on_message(const work_msg *m, void *ud)
{
    (void)m;
    adopt(ud);
}

static void on_leftover(const work_msg *m, void *ud)
{
    (void)m;
    (void)ud;
}

/* RETIRING -> IDLE once the worker has returned: the job struct and the
 * slot are free again. Takes a still-undrained message of its own first. */
static void retire(journal *j)
{
    if (j->transport == JOURNAL_TRANSPORT_SYNC || j->transport == JOURNAL_TRANSPORT_LEASE) {
        if (!work_handle_finished(j->pool, j->handle)) return;
        adopt(j);
    }
    if (j->transport != JOURNAL_TRANSPORT_RETIRING) return;
    if (!work_handle_finished(j->pool, j->handle)) return;
    (void)work_mailbox_receive(j->pool, j->handle, JOURNAL_JOB_GENERATION, on_leftover, NULL);
    (void)work_mailbox_bind(j->pool, j->handle, JOURNAL_JOB_GENERATION, NULL, NULL);
    j->transport = JOURNAL_TRANSPORT_IDLE;
}

static bool submit(journal *j, job_kind kind, journal_transport as)
{
    ASSERT(j->transport == JOURNAL_TRANSPORT_IDLE);
    j->job.kind = kind;
    j->job.fd = j->fd;
    j->job.dir = &j->dir;
    j->job.name = j->name;
    work_job wj = {.fn = job_run, .arg = &j->job, .generation = JOURNAL_JOB_GENERATION,
                   .cls = WORK_BULK};
    work_handle h = work_submit(j->pool, wj);
    if (h.epoch == 0) return false;
    j->handle = h;
    j->transport = as;
    int bound = work_mailbox_bind(j->pool, h, JOURNAL_JOB_GENERATION, on_message, j);
    ASSERT(bound == 0); /* a fresh handle of this pool with its generation */
    return true;
}

static journal_err write_pad(journal *j)
{
    size_t pad = journal_pad_length(stream_offset(j->end));
    if (pad == 0) return JOURNAL_OK;
    memset(j->payload, 0, pad - JOURNAL_HEADER_BYTES);
    size_t len = 0;
    journal_codec_err ce = journal_record_encode(j->rec, REC_CAP, &len, JOURNAL_REC_PAD, 0, 0,
                                                 j->payload, pad - JOURNAL_HEADER_BYTES,
                                                 stream_offset(j->end));
    ASSERT(ce == JOURNAL_CODEC_OK && len == pad);
    return ui_write(j, len, 0);
}

journal_err journal_pump(journal *j, uint64_t now_ns)
{
    ASSERT(j != NULL);
    retire(j);
    if (j->transport != JOURNAL_TRANSPORT_IDLE) return JOURNAL_OK;
    switch (j->suspension) {
    case JOURNAL_APPEND_GAP:
        j->job.held = j->rec;
        j->job.held_len = j->held_len;
        j->job.start = j->held_off;
        (void)submit(j, JOB_REPAIR, JOURNAL_TRANSPORT_LEASE);
        return JOURNAL_OK;
    case JOURNAL_DIRECTORY_PENDING:
        (void)submit(j, JOB_DIRSYNC, JOURNAL_TRANSPORT_SYNC);
        return JOURNAL_OK;
    case JOURNAL_WRITEBACK_LOST:
    case JOURNAL_NEEDS_ROTATION:
        return JOURNAL_OK;
    case JOURNAL_HEALTHY: break;
    }
    if (j->end == j->barrier_end) {
        j->sync_requested = false;
        j->unsynced_since = 0;
        return JOURNAL_OK;
    }
    if (j->unsynced_since == 0) j->unsynced_since = now_ns ? now_ns : 1;
    bool due = j->sync_requested || j->end - j->barrier_end >= j->opt.sync_bytes ||
               now_ns - j->unsynced_since >= j->opt.sync_interval_ns;
    if (!due) return JOURNAL_OK;
    journal_err e = write_pad(j);
    if (e != JOURNAL_OK) return e;
    j->job.barrier_end = j->end;
    j->job.barrier_sequence = j->written_seq;
    if (!submit(j, JOB_SYNC, JOURNAL_TRANSPORT_SYNC)) return JOURNAL_OK; /* retry next pump */
    j->barrier_end = j->end;
    j->sync_requested = false;
    j->unsynced_since = 0;
    return JOURNAL_OK;
}

void journal_request_sync(journal *j)
{
    ASSERT(j != NULL);
    j->sync_requested = true;
}

static journal_err lease_admit(journal *j)
{
    retire(j);
    if (j->transport != JOURNAL_TRANSPORT_IDLE || j->group != GROUP_OUTSIDE)
        return JOURNAL_ERR_BUSY;
    return JOURNAL_OK;
}

journal_err journal_checkpoint_start(journal *j, const journal_checkpoint_request *req)
{
    ASSERT(j != NULL && req != NULL && req->text != NULL && req->buffer_id != 0);
    journal_err e = lease_admit(j);
    if (e != JOURNAL_OK) return e;
    switch (j->suspension) {
    case JOURNAL_HEALTHY: break;
    case JOURNAL_APPEND_GAP:
    case JOURNAL_DIRECTORY_PENDING: return JOURNAL_ERR_BUSY;
    case JOURNAL_WRITEBACK_LOST:
    case JOURNAL_NEEDS_ROTATION: return JOURNAL_ERR_SUSPENDED;
    }
    ASSERT(j->next_seq != 0 && req->covered_sequence < j->next_seq);
    /* (P) records: BEGIN, descriptor, one INSERT per slice, TARGET,
     * COMMIT; the group must fit the sequence space. */
    uint64_t len = piece_length(piece_snapshot_view(req->text));
    uint64_t records = 4u + (len + JOURNAL_INSERT_SLICE_BYTES - 1u) / JOURNAL_INSERT_SLICE_BYTES;
    if (UINT64_MAX - j->next_seq < records) {
        j->suspension = JOURNAL_NEEDS_ROTATION;
        return JOURNAL_ERR_FULL;
    }
    job *jb = &j->job;
    jb->text = piece_snapshot_retain(req->text);
    jb->buffer_id = req->buffer_id;
    jb->descriptor = (journal_checkpoint){.covered_sequence = req->covered_sequence,
                                          .content_id = req->content_id,
                                          .saved_content_id = req->saved_content_id,
                                          .flags = req->flags};
    jb->has_target = req->target != NULL;
    if (req->target) {
        ASSERT(req->target->path_len <= JOURNAL_PATH_MAX_BYTES);
        jb->target = *req->target;
        if (req->target->path_len) memcpy(jb->target_path, req->target->path, req->target->path_len);
        jb->target.path = jb->target_path;
    }
    jb->start = j->end;
    jb->sequence = j->next_seq;
    if (!submit(j, JOB_CHECKPOINT, JOURNAL_TRANSPORT_LEASE)) {
        piece_snapshot_release(jb->text);
        jb->text = NULL;
        return JOURNAL_ERR_BUSY;
    }
    return JOURNAL_OK;
}

journal_err journal_rotate_start(journal *j, const journal_rotation *r)
{
    ASSERT(j != NULL && r != NULL);
    journal_err e = lease_admit(j);
    if (e != JOURNAL_OK) return e;
    job *jb = &j->job;
    jb->rotation = r;
    jb->generation_high_water = j->gen_hw;
    jb->buffer_high_water = j->buf_hw;
    jb->text = NULL;
    if (!submit(j, JOB_ROTATE, JOURNAL_TRANSPORT_LEASE)) return JOURNAL_ERR_BUSY;
    return JOURNAL_OK;
}

/* ---- sources ---- */
os_err journal_capture(os_file *fd, journal_generation *g)
{
    ASSERT(fd != NULL && g != NULL);
    os_stat before, after;
    os_err e = os_file_stat(fd, &before);
    if (e != OS_OK) return e;
    if (before.kind != OS_KIND_FILE) return OS_E_INVAL;
    uint8_t prefix[JOURNAL_PREFIX_BYTES];
    size_t want = before.size < JOURNAL_PREFIX_BYTES ? (size_t)before.size : JOURNAL_PREFIX_BYTES;
    size_t got = 0;
    e = journal_read_all(fd, prefix, want, 0, &got);
    if (e != OS_OK) return e;
    if (got != want) return OS_E_BUSY;
    e = os_file_stat(fd, &after);
    if (e != OS_OK) return e;
    journal_wire_identity a = journal_identity_of(&before), b = journal_identity_of(&after);
    if (!journal_identity_same(&a, &b)) return OS_E_BUSY;
    g->identity = a;
    g->prefix_len = (uint32_t)want;
    g->prefix_crc32c = journal_crc32c(prefix, want);
    return OS_OK;
}

/* ---- open and close ---- */

static journal_err alloc_journal(journal **out, const os_dir *dir, const char *name,
                                 work_pool *pool, const journal_options *opt)
{
    ASSERT(out != NULL && dir != NULL && name != NULL && pool != NULL && opt != NULL);
    ASSERT(strlen(name) >= 1 && strlen(name) <= JOURNAL_NAME_MAX && strchr(name, '/') == NULL);
    ASSERT(opt->budget_bytes > JOURNAL_PREAMBLE_BYTES && opt->sync_bytes > 0);
    /* (P) see "storage" above: the struct plus four buffers, five
     * allocations of at most ARENA_ALIGN - 1 B padding each. */
    size_t size = sizeof(journal) + 2u * REC_CAP + 2u * PAYLOAD_CAP + 5u * ARENA_ALIGN;
    base_arena a;
    if (base_arena_init(&a, size) != 0) return JOURNAL_ERR_NOMEM;
    journal *j = base_arena_alloc(&a, sizeof *j, ARENA_ALIGN);
    ASSERT(j != NULL); /* sized above */
    memset(j, 0, sizeof *j);
    j->rec = base_arena_alloc(&a, REC_CAP, ARENA_ALIGN);
    j->payload = base_arena_alloc(&a, PAYLOAD_CAP, ARENA_ALIGN);
    j->job.stage = base_arena_alloc(&a, REC_CAP, ARENA_ALIGN);
    j->job.payload = base_arena_alloc(&a, PAYLOAD_CAP, ARENA_ALIGN);
    ASSERT(j->rec && j->payload && j->job.stage && j->job.payload); /* sized above */
    j->arena = a;
    j->pool = pool;
    j->opt = *opt;
    j->fd = (os_file)OS_FILE_INIT;
    j->dir = (os_dir)OS_DIR_INIT;
    j->job.new_fd = (os_file)OS_FILE_INIT;
    memcpy(j->name, name, strlen(name) + 1); /* length ASSERTed above */
    if (os_dir_open_at(&j->dir, dir, ".") != OS_OK) {
        base_arena_free(&a);
        return JOURNAL_ERR_IO;
    }
    *out = j;
    return JOURNAL_OK;
}

static void free_journal(journal *j)
{
    os_file_close(&j->fd);
    os_dir_close(&j->dir);
    base_arena a = j->arena; /* the struct lives inside it */
    base_arena_free(&a);
}

/* A fresh image: preamble written and synced under a temporary name,
 * renamed into place without replacing anything, directory synced. */
static journal_err create_image(journal *j)
{
    char tmp[OS_NAME_MAX];
    os_file f = OS_FILE_INIT;
    os_err e = journal_create_sibling(&j->dir, j->name, "new", tmp, &f);
    if (e != OS_OK) {
        note_error(j, e, os_native_error());
        return JOURNAL_ERR_IO;
    }
    uint8_t pre[JOURNAL_PREAMBLE_BYTES];
    journal_preamble_encode(pre, j->gen_hw, j->buf_hw);
    e = journal_write_all(&f, pre, sizeof pre, 0);
    if (e == OS_OK) e = os_file_sync(&f);
    if (e == OS_OK) e = os_rename_at(&j->dir, tmp, &j->dir, j->name, OS_RENAME_NOREPLACE);
    if (e != OS_OK) {
        (void)os_unlink_at(&j->dir, tmp);
        os_file_close(&f);
        note_error(j, e, os_native_error());
        return e == OS_E_EXIST ? JOURNAL_ERR_EXISTS : JOURNAL_ERR_IO;
    }
    e = os_dir_sync(&j->dir);
    if (e != OS_OK) {
        /* Named, but not durably: the old binding (none) may return. */
        os_file_close(&f);
        note_error(j, e, os_native_error());
        return JOURNAL_ERR_IO;
    }
    j->fd = f;
    j->end = JOURNAL_PREAMBLE_BYTES;
    j->next_seq = 1;
    return JOURNAL_OK;
}

/* Continue a recovered image at its valid end: same inode and size as
 * recovery left, then PAD to a block and fsync, so the first append
 * starts on a block and the prefix is durable. */
static journal_err continue_image(journal *j, const journal_recovery *rec)
{
    os_err e = os_file_open_at(&j->fd, &j->dir, j->name, OS_OPEN_RDWR | OS_OPEN_NOFOLLOW);
    if (e != OS_OK) {
        note_error(j, e, os_native_error());
        return JOURNAL_ERR_IO;
    }
    os_stat st;
    e = os_file_stat(&j->fd, &st);
    if (e != OS_OK || st.kind != OS_KIND_FILE || !os_identity_equal(&st.id, &rec->image) ||
        st.size != rec->valid_bytes) {
        note_error(j, e == OS_OK ? OS_E_BUSY : e, e == OS_OK ? 0 : os_native_error());
        return JOURNAL_ERR_IO;
    }
    j->end = rec->valid_bytes;
    j->next_seq = rec->last_sequence == UINT64_MAX ? 0 : rec->last_sequence + 1;
    j->written_seq = rec->last_sequence;
    size_t pad = journal_pad_length(stream_offset(j->end));
    if (pad) {
        memset(j->payload, 0, pad - JOURNAL_HEADER_BYTES);
        size_t len = 0;
        journal_codec_err ce = journal_record_encode(j->rec, REC_CAP, &len, JOURNAL_REC_PAD, 0,
                                                     0, j->payload, pad - JOURNAL_HEADER_BYTES,
                                                     stream_offset(j->end));
        ASSERT(ce == JOURNAL_CODEC_OK);
        e = journal_write_all(&j->fd, j->rec, len, j->end);
        if (e != OS_OK) {
            /* Remove what the PAD left: nothing may follow a torn record. */
            (void)os_file_set_size(&j->fd, j->end);
            note_error(j, e, os_native_error());
            return JOURNAL_ERR_IO;
        }
        j->end += len;
    }
    e = os_file_sync_data(&j->fd);
    if (e != OS_OK) {
        note_error(j, e, os_native_error());
        return JOURNAL_ERR_IO;
    }
    if (j->next_seq == 0) j->suspension = JOURNAL_NEEDS_ROTATION;
    return JOURNAL_OK;
}

journal_err journal_open(journal **out, const os_dir *dir, const char *name, work_pool *pool,
                         const journal_recovery *rec, const journal_options *opt)
{
    ASSERT(out != NULL);
    *out = NULL;
    journal *j = NULL;
    journal_err e = alloc_journal(&j, dir, name, pool, opt);
    if (e != JOURNAL_OK) return e;
    journal_image_verdict v = rec ? rec->verdict : JOURNAL_IMAGE_ABSENT;
    if (rec) {
        j->gen_hw = rec->generation_high_water;
        j->buf_hw = rec->buffer_high_water;
    }
    switch (v) {
    case JOURNAL_IMAGE_ABSENT:
    case JOURNAL_IMAGE_REFUSED: e = create_image(j); break;
    case JOURNAL_IMAGE_CLEAN:
    case JOURNAL_IMAGE_REPAIRED:
    case JOURNAL_IMAGE_SET_ASIDE: e = continue_image(j, rec); break;
    case JOURNAL_IMAGE_DAMAGED: e = JOURNAL_ERR_REFUSED; break;
    }
    if (e != JOURNAL_OK) {
        free_journal(j);
        return e;
    }
    j->durable_seq = j->written_seq;
    j->durable_end = j->end;
    j->barrier_end = j->end;
    *out = j;
    return JOURNAL_OK;
}

journal_err journal_close(journal *j)
{
    ASSERT(j != NULL);
    retire(j);
    if (j->transport != JOURNAL_TRANSPORT_IDLE) return JOURNAL_ERR_BUSY;
    free_journal(j);
    return JOURNAL_OK;
}
