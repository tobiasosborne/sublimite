#include "support/edit_fixture.h"
#include "piece/piece_test.h"
#include "edit/edit_test.h"

#include <limits.h>
#include <string.h>

/* (P) Four journal pages hold the fixed small transaction fixtures and
 * every independently injectable prefix. The mapped streaming fixture
 * sizes its own image by bytes and records. */
static journal *small_journal(void)
{
    size_t bytes = 4 * JOURNAL_PAGE_BYTES;
    return ej_create(bytes, bytes / JOURNAL_HEADER_BYTES);
}

static edit_boundary boundary(edit_buffer *b, edit_phase phase)
{
    edit_boundary out;
    ECHECK(edit_read(b, &out) == phase);
    return out;
}

static void unchanged(edit_buffer *b, edit_boundary old, edit_phase phase)
{
    edit_boundary now = boundary(b, phase);
    ECHECK(now.snapshot == old.snapshot && now.content_id == old.content_id &&
           now.revision == old.revision);
    ECHECK(now.selection.active.cursor == old.selection.active.cursor &&
           now.selection.active.anchor == old.selection.active.anchor &&
           now.selection.mark_kind == old.selection.mark_kind &&
           now.selection.mark.cursor == old.selection.mark.cursor &&
           now.selection.mark.anchor == old.selection.mark.anchor);
    edit_change change = {.revision = UINT64_MAX};
    ECHECK(!edit_take_change(b, &change) && change.revision == UINT64_MAX);
}

static void quiescent(journal *j)
{
    edit_journal_info info = ej_info(j);
    ECHECK(info.open == 0 && info.requests == 0 && info.reserved_bytes == 0);
}

static void journal_text(journal *j, const uint8_t *seed, size_t seed_len,
                          const uint8_t *expected, size_t len)
{
    /* Include space for the starting text and intermediate test splices. */
    size_t cap = seed_len + len + JOURNAL_PAGE_BYTES;
    uint8_t *out = malloc(cap);
    ECHECK(out);
    ECHECK(ej_read(j, EDIT_TEST_OWNER, seed, seed_len, out, cap) == len);
    ECHECK(!len || memcmp(out, expected, len) == 0);
    free(out);
}

static const uint8_t seed[] = "ab";
static const uint8_t key[] = "XY";
static const edit_splice compound[] = {{0, 1, key, 1}, {2, 0, key + 1, 1}};

static edit_result apply_compound(edit_buffer *b)
{
    return ef_finish(b, edit_apply_plan(b, compound, sizeof compound / sizeof *compound,
                                        (edit_selection){3, 3}));
}

static void retry_compound(edit_fixture *f)
{
    ECHECK(apply_compound(f->edit) == EDIT_OK);
    edit_change change = ef_submit(f->edit);
    ECHECK(change.off == 0 && change.old_len == 2 && change.new_len == 3);
    ef_text(f->edit, (const uint8_t *)"XbY", 3);
    journal_text(f->j, seed, 2, (const uint8_t *)"XbY", 3);
    quiescent(f->j);
}

static void t_refusals(void)
{
    for (edit_journal_claim claim = EJ_COMMAND; claim < EJ_CLAIMS; claim++) {
        edit_fixture f;
        ef_open(&f, seed, 2, undo_default_cap_bytes(), small_journal());
        edit_boundary old = boundary(f.edit, EDIT_IDLE);
        ej_refuse_begin(f.j, claim, claim == EJ_SEQUENCES ? JOURNAL_LIMIT : JOURNAL_NOMEM);
        ECHECK(apply_compound(f.edit) == (claim == EJ_SEQUENCES ? EDIT_LIMIT : EDIT_NOMEM));
        unchanged(f.edit, old, EDIT_IDLE);
        ECHECK(piece_add_length(f.tree) == 0 && ej_info(f.j).frames == 0);
        quiescent(f.j);
        retry_compound(&f);
        ef_close(&f);
    }
    edit_fixture blocked;
    ef_open(&blocked, seed, 2, undo_default_cap_bytes(), small_journal());
    edit_boundary blocked_boundary = boundary(blocked.edit, EDIT_IDLE);
    ej_health(blocked.j, JOURNAL_RECOVERING);
    ECHECK(apply_compound(blocked.edit) == EDIT_WAIT);
    unchanged(blocked.edit, blocked_boundary, EDIT_IDLE);
    ej_health(blocked.j, JOURNAL_NAME_UNCERTAIN);
    ECHECK(apply_compound(blocked.edit) == EDIT_SUSPENDED);
    unchanged(blocked.edit, blocked_boundary, EDIT_IDLE);
    ej_health(blocked.j, JOURNAL_HEALTHY);
    retry_compound(&blocked);
    ef_close(&blocked);

    /* Each nonempty splice contributes one member per deletion and insert
     * in this fixture. Refuse each member, including after prior progress. */
    for (size_t n = 0; n < sizeof compound / sizeof *compound + 1; n++) {
        edit_fixture f;
        ef_open(&f, seed, 2, undo_default_cap_bytes(), small_journal());
        edit_boundary old = boundary(f.edit, EDIT_IDLE);
        if (n == 0) ej_refuse_member(f.j, JOURNAL_NOMEM);
        edit_result r = edit_apply_plan(f.edit, compound, sizeof compound / sizeof *compound,
                                        (edit_selection){3, 3});
        for (size_t i = 1; i < n; i++) {
            ECHECK(r == EDIT_MORE);
            r = edit_continue(f.edit, OS_DEADLINE_NEVER);
        }
        if (n) ej_refuse_member(f.j, JOURNAL_NOMEM);
        ECHECK(ef_finish(f.edit, r) == EDIT_NOMEM);
        unchanged(f.edit, old, EDIT_IDLE);
        ECHECK(piece_add_length(f.tree) == 0 && ej_info(f.j).committed == 0);
        journal_text(f.j, seed, 2, seed, 2);
        quiescent(f.j);
        retry_compound(&f);
        ef_close(&f);
    }
}

static size_t base_calls, refuse_call;
os_err __real_os_vm_reserve(void **out, size_t len);
os_err __wrap_os_vm_reserve(void **out, size_t len);
os_err __wrap_os_vm_reserve(void **out, size_t len)
{
    if (refuse_call && ++base_calls == refuse_call) return OS_E_NOMEM;
    return __real_os_vm_reserve(out, len);
}

static void t_base(void)
{
    /* Walk the exact cold request sequence, including descriptor, history
     * and piece/ADD funding. Each cold attempt starts with the same owners.
     * The first index beyond that sequence must complete. */
    for (size_t fail = 1;; fail++) {
        edit_fixture f;
        ef_open(&f, seed, 2, undo_default_cap_bytes(), small_journal());
        edit_boundary old = boundary(f.edit, EDIT_IDLE);
        base_calls = 0;
        refuse_call = fail;
        edit_result r = apply_compound(f.edit);
        refuse_call = 0;
        if (r == EDIT_OK) {
            ef_submit(f.edit);
            ef_close(&f);
            ECHECK(fail > 1 && base_calls < fail);
            printf("edit_test: %zu cold base refusals\n", fail - 1);
            return;
        }
        ECHECK(r == EDIT_NOMEM && base_calls >= fail);
        unchanged(f.edit, old, EDIT_IDLE);
        ECHECK(piece_add_length(f.tree) == 0 && ej_info(f.j).frames == 0);
        quiescent(f.j);
        retry_compound(&f);
        ef_close(&f);
    }
}

typedef struct map_owner {
    os_file file;
    os_map map;
    os_fault_id fault;
    piece_backing backing;
    size_t owners;
    journal *j;
    size_t refuse_after_frames;
} map_owner;

static void map_acquire(void *ctx) { ((map_owner *)ctx)->owners++; }
static void map_release(void *ctx) { ((map_owner *)ctx)->owners--; }

static bool valid(void *ctx, const piece_backing *b)
{
    map_owner *m = ctx;
    ECHECK(b->ctx == ctx && m->owners == 1);
    return ej_info(m->j).frames < m->refuse_after_frames;
}

static void open_mapped(edit_fixture *f, map_owner *m, const uint8_t *bytes, size_t len,
                        uint64_t generation, journal *j)
{
    *m = (map_owner){.j = j, .refuse_after_frames = SIZE_MAX};
    os_dir dir;
    ECHECK(os_dir_open(&dir, "build") == OS_OK);
    ECHECK(os_file_open_anon(&m->file, &dir) == OS_OK);
    os_dir_close(&dir);
    size_t done;
    ECHECK(os_file_write(&m->file, bytes, len, &done) == OS_OK && done == len);
    ECHECK(os_map_file(&m->map, &m->file, 0, len) == OS_OK);
    ECHECK(os_fault_register(&m->map, &m->fault) == OS_OK);
    m->backing = (piece_backing){m->map.addr, len, m->fault, m, map_acquire, map_release};
    f->j = j;
    ECHECK(piece_reserve_create(&f->reserve) == PIECE_OK);
    ECHECK(piece_open_mapped(f->reserve, &m->backing, &f->tree) == PIECE_OK);
    edit_options o = {.tree = f->tree, .journal = j, .buffer_id = EDIT_TEST_OWNER,
                      .history_cap_bytes = undo_default_cap_bytes(), .validate = valid,
                      .ctx = m, .current_id = 1, .saved_id = 1, .original_generation = generation};
    ECHECK(edit_create(&f->edit, &o) == EDIT_OK);
}

static void close_mapped(edit_fixture *f, map_owner *m)
{
    ef_close(f);
    ECHECK(m->owners == 0);
    os_fault_retire(m->fault);
    os_unmap(&m->map);
    os_file_close(&m->file);
}

static bool refuse_snapshot;
piece_err __real_piece_snapshot_take(piece_tree *tree, piece_snapshot **out);
piece_err __wrap_piece_snapshot_take(piece_tree *tree, piece_snapshot **out)
{
    if (refuse_snapshot) {
        refuse_snapshot = false;
        *out = NULL;
        return PIECE_ERR_NOMEM;
    }
    return __real_piece_snapshot_take(tree, out);
}

static void t_conflict(void)
{
    edit_fixture f;
    map_owner m;
    open_mapped(&f, &m, seed, 2, 0, small_journal());
    edit_boundary old = boundary(f.edit, EDIT_IDLE);
    /* BEGIN, DELETE and two INSERTs must all be accepted before refusal.
     * A release-before-abort mistake is an always-on piece assertion. */
    m.refuse_after_frames = 1 + sizeof compound / sizeof *compound + 1;
    ECHECK(apply_compound(f.edit) == EDIT_CONFLICT);
    unchanged(f.edit, old, EDIT_IDLE);
    edit_journal_info info = ej_info(f.j);
    ECHECK(info.frames == m.refuse_after_frames && info.aborts == 1 && info.finals == 0);
    ECHECK(piece_add_length(f.tree) == 0);
    journal_text(f.j, seed, 2, seed, 2);
    quiescent(f.j);
    m.refuse_after_frames = SIZE_MAX;
    retry_compound(&f);
    ECHECK(ef_finish(f.edit, edit_replay(f.edit, UNDO_REPLAY_UNDO, 0)) == EDIT_OK);
    ef_submit(f.edit);
    ef_text(f.edit, seed, 2);
    close_mapped(&f, &m);
}

static void delete_at(edit_buffer *b, uint64_t off, uint64_t len)
{
    ECHECK(ef_finish(b, edit_replace(b, EDIT_DELETE_FORWARD, (edit_splice){off, len, NULL, 0}, 0)) == EDIT_OK);
    ef_submit(b);
}

static void replay(edit_buffer *b, undo_replay_direction dir)
{
    ECHECK(ef_finish(b, edit_replay(b, dir, 0)) == EDIT_OK);
    ef_submit(b);
}

static void t_replay_refusal(void)
{
    /* A single retained span restored by value and the three descriptors
     * of a compound inverse cover both inline and external plans. */
    for (size_t grouped = 0; grouped <= 1; grouped++) {
        edit_fixture f;
        ef_open(&f, seed, 2, undo_default_cap_bytes(), small_journal());
        if (grouped) retry_compound(&f);
        else delete_at(f.edit, 0, 2);
        edit_boundary old = boundary(f.edit, EDIT_IDLE);
        uint64_t old_add = piece_add_length(f.tree);
        edit_journal_info prior = ej_info(f.j);
        edit_result r = edit_replay(f.edit, UNDO_REPLAY_UNDO, 0);
        ECHECK(r == EDIT_MORE);
        unchanged(f.edit, old, EDIT_REPLAYING);
        if (grouped) {
            /* A two-mutation group first yields before its last mutation.
             * Resume one step and stop before it sizes the descriptors. */
            ECHECK(edit_continue(f.edit, 0) == EDIT_MORE);
            ECHECK(piece_length(piece_tree_view(f.tree)) == 2);
        }
        refuse_snapshot = true;
        ECHECK(edit_continue(f.edit, OS_DEADLINE_NEVER) == EDIT_NOMEM);
        ECHECK(!refuse_snapshot);
        unchanged(f.edit, old, EDIT_IDLE);
        ECHECK(piece_add_length(f.tree) == old_add && ej_info(f.j).frames == prior.frames);
        quiescent(f.j);
        replay(f.edit, UNDO_REPLAY_UNDO);
        ef_text(f.edit, seed, 2);
        ef_close(&f);
    }
}

static edit_result maintain(edit_buffer *b, size_t reclaim, size_t resolve, size_t undo);

static void count_original(edit_fixture *f, map_owner *m, size_t chunks)
{
    uint32_t *counts = malloc(chunks * sizeof *counts);
    ECHECK(counts);
    ECHECK(piece_lines_count(&m->backing, 0, counts, chunks));
    ECHECK(piece_lines_adopt(f->tree, m->fault, 0, counts, chunks));
    free(counts);
    /* One visit per open node suffices on the packed fixture, bounded by
     * the sum of its leaf/chunk count and all its ancestors. */
    ECHECK(maintain(f->edit, 0, chunks * (PIECE_MAX_HEIGHT + 1), 0) == EDIT_OK);
}

static void check_lines(edit_fixture *f, size_t len, size_t newlines, size_t first_line)
{
    edit_boundary published = boundary(f->edit, EDIT_IDLE);
    const piece_view *view = piece_snapshot_view(published.snapshot);
    uint64_t count, off, before;
    piece_test_reset_reads(f->tree);
    ECHECK(piece_lines_before(view, len, &count) && count == newlines);
    ECHECK(piece_lines_start(view, 1, &off, &before) && off == first_line && before == 1);
    piece_test_stats stats = piece_test_measure(f->tree);
    /* Two queries, each descending at most one root-to-leaf path. */
    ECHECK(stats.query_visits <= 2 * (stats.height + 1));
}

static void t_counted(void)
{
    /* A19's one-past-512 fixture. Delete 513 chunks from the midpoint
     * of the first of 514 chunks to the midpoint of the last. One LF at
     * each end of every chunk forces the counted cuts to scan. */
    enum { removed_chunks = 513, chunks = removed_chunks + 1 };
    const size_t len = (size_t)chunks * PIECE_CHUNK;
    uint8_t *bytes = calloc(len, 1);
    ECHECK(bytes);
    for (size_t i = 0; i < len; i += PIECE_CHUNK)
        bytes[i] = bytes[i + PIECE_CHUNK - 1] = '\n';
    edit_fixture f;
    map_owner m;
    const uint64_t generation = 7; /* a declared key distinct from owner */
    open_mapped(&f, &m, bytes, len, generation, small_journal());
    ej_original(f.j, generation, bytes, len);
    count_original(&f, &m, chunks);
    check_lines(&f, len, 2 * chunks, 1);
    piece_test_reset_reads(f.tree);
    const size_t removed = (size_t)removed_chunks * PIECE_CHUNK;
    delete_at(f.edit, PIECE_CHUNK / 2, removed);
    size_t delete_reads = piece_test_measure(f.tree).mapped_reads;
    undo_stats inverse = edit_test_undo_stats(f.edit);
    /* The declared one-span inverse fits the inline slot for its sole record. */
    ECHECK(inverse.records == 1 && inverse.span_payload_bytes == 0);
    ECHECK(piece_add_length(f.tree) == 0);
    check_lines(&f, len - removed, 2, 1);
    piece_test_reset_reads(f.tree);
    replay(f.edit, UNDO_REPLAY_UNDO);
    size_t reads = delete_reads + piece_test_measure(f.tree).mapped_reads;
    ECHECK(reads <= 4 * (PIECE_CHUNK / 2));
    ECHECK(piece_add_length(f.tree) == 0 && ej_info(f.j).ranges == 1);
    check_lines(&f, len, 2 * chunks, 1);
    ef_text(f.edit, bytes, len);
    /* Literal is preferable for a one-byte, one-span inverse. */
    delete_at(f.edit, 0, 1);
    ECHECK(piece_add_length(f.tree) == 1);
    replay(f.edit, UNDO_REPLAY_UNDO);
    ECHECK(piece_add_length(f.tree) == 1 && ej_info(f.j).ranges == 1 && ej_info(f.j).inserts == 1);
    ef_text(f.edit, bytes, len);
    printf("edit_test: A13 one-span mapped reads=%zu, A19 removed_chunks=%d\n", reads, removed_chunks);
    close_mapped(&f, &m);
    free(bytes);
}

static void t_stream(void)
{
    /* Cross a source chunk boundary away from a wire slice boundary. */
    size_t len = PIECE_CHUNK + JOURNAL_LITERAL_LIMIT;
    size_t slices = (len + JOURNAL_LITERAL_LIMIT - 1) / JOURNAL_LITERAL_LIMIT;
    uint8_t *bytes = malloc(len);
    ECHECK(bytes);
    for (size_t i = 0; i < len; i++) bytes[i] = (uint8_t)i;
    for (uint64_t key_id = 0; key_id <= 1; key_id++) {
        edit_fixture f;
        map_owner m;
        size_t overhead = slices * (JOURNAL_HEADER_BYTES + sizeof(uint64_t)) +
                          2 * JOURNAL_MARKER_BYTES + JOURNAL_DELETE_BYTES;
        open_mapped(&f, &m, bytes, len, key_id, ej_create(len + overhead, slices + 3));
        ej_original(f.j, 1, bytes, len);
        if (key_id) ej_embargo(f.j);
        delete_at(f.edit, 0, len);
        edit_boundary prior = boundary(f.edit, EDIT_IDLE);
        edit_result r = edit_replay(f.edit, UNDO_REPLAY_UNDO, 0);
        ECHECK(r == EDIT_MORE);
        unchanged(f.edit, prior, EDIT_REPLAYING);
        ECHECK(maintain(f.edit, 0, 0, 0) == EDIT_WAIT);
        ECHECK(ef_finish(f.edit, r) == EDIT_OK);
        ef_submit(f.edit);
        edit_journal_info info = ej_info(f.j);
        ECHECK(info.ranges == 0 && info.inserts == slices && info.frames == slices + 3);
        ECHECK(info.bytes == len + overhead && info.largest == JOURNAL_PAGE_BYTES);
        ECHECK(edit_test_scratch_high(f.edit) == JOURNAL_PAGE_BYTES);
        ECHECK(info.range_checks == (size_t)key_id);
        ef_text(f.edit, bytes, len);
        journal_text(f.j, bytes, len, bytes, len);
        close_mapped(&f, &m);
    }
    free(bytes);
}

static void t_stream_conflict_and_repair(void)
{
    /* One fixture fits a singleton; the other needs a two-member group.
     * No file or mapping changes. Only the live validator changes its answer. */
    uint8_t bytes[JOURNAL_LITERAL_LIMIT + 1];
    memset(bytes, 'R', sizeof bytes);
    for (size_t group = 0; group <= 1; group++) {
        edit_fixture f;
        map_owner m;
        size_t len = group ? sizeof bytes : sizeof(piece_span);
        open_mapped(&f, &m, bytes, len, 0, small_journal());
        delete_at(f.edit, 0, len);
        edit_boundary old = boundary(f.edit, EDIT_IDLE);
        if (group) {
            /* Count the completed delete, BEGIN and the first literal. */
            m.refuse_after_frames = 3;
            ECHECK(ef_finish(f.edit, edit_replay(f.edit, UNDO_REPLAY_UNDO, 0)) == EDIT_CONFLICT);
            unchanged(f.edit, old, EDIT_IDLE);
            quiescent(f.j);
            ECHECK(ej_info(f.j).frames == 3 && ej_info(f.j).committed == 1);
            journal_text(f.j, bytes, len, NULL, 0);
            m.refuse_after_frames = SIZE_MAX;
        }
        ej_short(f.j, group ? JOURNAL_REC_COMMAND_COMMIT : JOURNAL_REC_INSERT);
        ECHECK(ef_finish(f.edit, edit_replay(f.edit, UNDO_REPLAY_UNDO, 0)) == EDIT_SUSPENDED);
        unchanged(f.edit, old, EDIT_FINAL_PENDING);
        m.refuse_after_frames = 0;
        size_t finals = ej_info(f.j).finals;
        piece_test_reset_reads(f.tree);
        ECHECK(journal_reconcile(f.j) == JOURNAL_OK);
        ECHECK(edit_continue(f.edit, 0) == EDIT_OK);
        ECHECK(piece_test_measure(f.tree).mapped_reads == 0 && ej_info(f.j).finals == finals);
        ef_submit(f.edit);
        journal_text(f.j, bytes, len, bytes, len);
        quiescent(f.j);
        close_mapped(&f, &m);
    }
}

static size_t maintenance_step;
static bool maintenance_window, refuse_topup;
bool __real_piece_reclaim(piece_tree *t, size_t n);
piece_err __real_piece_lines_resolve(piece_tree *t, size_t n, bool *more);
piece_err __real_piece_replenish(piece_tree *t);
size_t __real_undo_maintain(undo_log *u, size_t n);

bool __wrap_piece_reclaim(piece_tree *t, size_t n)
{
    ECHECK(maintenance_window && maintenance_step++ == 0);
    return __real_piece_reclaim(t, n);
}

piece_err __wrap_piece_lines_resolve(piece_tree *t, size_t n, bool *more)
{
    ECHECK(maintenance_window && maintenance_step++ == 1);
    return __real_piece_lines_resolve(t, n, more);
}

piece_err __wrap_piece_replenish(piece_tree *t)
{
    ECHECK(maintenance_window && maintenance_step++ == 2);
    return refuse_topup ? PIECE_ERR_NOMEM : __real_piece_replenish(t);
}

size_t __wrap_undo_maintain(undo_log *u, size_t n)
{
    ECHECK(maintenance_window && maintenance_step++ == 3);
    return __real_undo_maintain(u, n);
}

static edit_result maintain(edit_buffer *b, size_t reclaim, size_t resolve, size_t undo)
{
    maintenance_step = 0;
    maintenance_window = true;
    edit_result r = edit_maintain(b, reclaim, resolve, undo);
    maintenance_window = false;
    ECHECK(maintenance_step == (r == EDIT_WAIT ? 0u : 4u));
    return r;
}

static void t_maintenance(void)
{
    edit_fixture f;
    ef_open(&f, seed, 2, undo_default_cap_bytes(), small_journal());
    edit_result r = edit_apply_plan(f.edit, compound, sizeof compound / sizeof *compound,
                                    (edit_selection){3, 3});
    ECHECK(r == EDIT_MORE && maintain(f.edit, 1, 1, 1) == EDIT_WAIT);
    ECHECK(ef_finish(f.edit, r) == EDIT_OK);
    ef_submit(f.edit);
    /* Reclaim one job, resolve one visit, retire one record. */
    refuse_topup = true;
    ECHECK(maintain(f.edit, 1, 1, 1) == EDIT_NOMEM);
    refuse_topup = false;
    delete_at(f.edit, 0, 1);
    ECHECK(maintain(f.edit, 1, 1, 1) == EDIT_OK);
    ef_close(&f);
}

static void t_trace(void)
{
    /* MUST-FIX 2: equal to a span pair, then below it to force inverse
     * literal captures. The reused ADD offset must name the retry bytes. */
    uint8_t lower[sizeof(piece_span)], upper[sizeof(piece_span)];
    memset(lower, 'a', sizeof lower);
    memset(upper, 'B', sizeof upper);
    for (size_t len = sizeof upper - 1; len <= sizeof upper; len++) {
        edit_fixture f;
        ef_open(&f, lower, sizeof lower, undo_default_cap_bytes(), small_journal());
        edit_splice plan[] = {{sizeof lower, 0, upper, len}, {sizeof lower, len, NULL, 0}};
        ej_refuse_member(f.j, JOURNAL_NOMEM);
        ECHECK(ef_finish(f.edit, edit_apply_plan(f.edit, plan, 2, (edit_selection){0})) == EDIT_NOMEM);
        ECHECK(piece_add_length(f.tree) == 0);
        ECHECK(edit_replay(f.edit, UNDO_REPLAY_UNDO, 0) == EDIT_OK);
        edit_change none;
        ECHECK(!edit_take_change(f.edit, &none));

        plan[0].bytes = lower; /* same ADD offset, different contents */
        ECHECK(ef_finish(f.edit, edit_apply_plan(f.edit, plan, 1,
                                                (edit_selection){0})) == EDIT_OK);
        ef_submit(f.edit);
        delete_at(f.edit, sizeof lower, len);
        uint8_t expected[2 * sizeof lower];
        memset(expected, 'a', sizeof expected);
        replay(f.edit, UNDO_REPLAY_UNDO);
        ef_text(f.edit, expected, sizeof lower + len);
        journal_text(f.j, lower, sizeof lower, expected, sizeof lower + len);
        replay(f.edit, UNDO_REPLAY_REDO);
        ef_text(f.edit, lower, sizeof lower);
        ef_close(&f);
    }
}

static void t_fence(void)
{
    for (size_t grouped = 0; grouped <= 1; grouped++) {
        edit_fixture f;
        ef_open(&f, seed, 2, undo_default_cap_bytes(), small_journal());
        edit_boundary old = boundary(f.edit, EDIT_IDLE);
        ej_short(f.j, grouped ? JOURNAL_REC_COMMAND_COMMIT : JOURNAL_REC_INSERT);
        edit_result r = grouped ? apply_compound(f.edit) :
            edit_replace(f.edit, EDIT_TYPE, (edit_splice){0, 0, key, 1}, 0);
        ECHECK(ef_finish(f.edit, r) == EDIT_SUSPENDED);
        unchanged(f.edit, old, EDIT_FINAL_PENDING);
        ECHECK(maintain(f.edit, 0, 0, 0) == EDIT_WAIT);
        ECHECK(edit_continue(f.edit, 0) == EDIT_SUSPENDED);
        journal_text(f.j, seed, 2, seed, 2);
        ECHECK(ej_info(f.j).finals == 1 && ej_info(f.j).requests == 1);
        ECHECK(journal_reconcile(f.j) == JOURNAL_OK);
        ECHECK(edit_continue(f.edit, 0) == EDIT_OK);
        edit_change change = ef_submit(f.edit);
        ECHECK(change.revision == 1 && ej_info(f.j).finals == 1);
        quiescent(f.j);
        replay(f.edit, UNDO_REPLAY_UNDO);
        ef_text(f.edit, seed, 2);
        ef_close(&f);
    }

    edit_fixture f;
    ef_open(&f, seed, 2, undo_default_cap_bytes(), small_journal());
    edit_boundary old = boundary(f.edit, EDIT_IDLE);
    ej_short(f.j, JOURNAL_REC_COMMAND_DELETE);
    edit_result r = edit_apply_plan(f.edit, compound, 2, (edit_selection){3, 3});
    ECHECK(r == EDIT_MORE || r == EDIT_SUSPENDED);
    ECHECK(ef_finish(f.edit, r) == EDIT_SUSPENDED);
    ECHECK(ej_info(f.j).frames == 1 && ej_info(f.j).finals == 0);
    unchanged(f.edit, old, EDIT_OPEN);
    ECHECK(journal_reconcile(f.j) == JOURNAL_OK);
    /* A peer finishes while the first owner still holds its group. */
    journal_command *peer;
    ECHECK(journal_command_begin(f.j, EDIT_TEST_PEER, (journal_member_claim){0},
                                 JOURNAL_REC_INSERT, sizeof(uint64_t) + 1, &peer) == JOURNAL_OK);
    uint8_t *p = journal_command_payload(peer);
    memset(p, 0, sizeof(uint64_t));
    p[sizeof(uint64_t)] = 'p';
    journal_request *req = journal_command_final(peer);
    ECHECK(journal_request_result(req).publication == JOURNAL_VISIBLE);
    journal_request_release(req);
    ej_refuse_member(f.j, JOURNAL_WAIT);
    ECHECK(ef_finish(f.edit, edit_continue(f.edit, OS_DEADLINE_NEVER)) == EDIT_OK);
    ef_submit(f.edit);
    ECHECK(ej_info(f.j).deletes == 1 && ej_info(f.j).inserts == 3);
    journal_text(f.j, seed, 2, (const uint8_t *)"XbY", 3);
    uint8_t peer_text;
    ECHECK(ej_read(f.j, EDIT_TEST_PEER, NULL, 0, &peer_text, 1) == 1 && peer_text == 'p');
    ef_close(&f);
}

static uint64_t type(edit_buffer *b, uint64_t off, uint64_t time)
{
    ECHECK(edit_replace(b, EDIT_TYPE, (edit_splice){off, 0, key, 1}, time) == EDIT_OK);
    ef_submit(b);
    return boundary(b, EDIT_IDLE).content_id;
}

static void t_bookmarks(void)
{
    edit_fixture f;
    ef_open(&f, NULL, 0, undo_default_cap_bytes(), small_journal());
    edit_save_boundary(f.edit, EDIT_SAVE_FORGOTTEN);
    edit_save_boundary(f.edit, EDIT_SAVE_FORGOTTEN);
    uint64_t cutoff = type(f.edit, 0, 0);
    edit_save_boundary(f.edit, EDIT_SAVE_CAPTURED);
    /* Simulate an ambiguous settlement: a key commits and FORGOTTEN
     * invalidates saved in this dispatch while the independent cutoff lives. */
    uint64_t later = type(f.edit, 1, 0);
    ECHECK(later != cutoff);
    edit_save_boundary(f.edit, EDIT_SAVE_FORGOTTEN);
    edit_save_boundary(f.edit, EDIT_SAVE_INSTALLED); /* verified durable install */
    replay(f.edit, UNDO_REPLAY_UNDO);
    ECHECK(boundary(f.edit, EDIT_IDLE).content_id == cutoff);
    edit_save_boundary(f.edit, EDIT_SAVE_CAPTURED);
    edit_save_boundary(f.edit, EDIT_SAVE_RELEASED); /* definitive non-install */
    replay(f.edit, UNDO_REPLAY_REDO);
    replay(f.edit, UNDO_REPLAY_UNDO);
    ECHECK(boundary(f.edit, EDIT_IDLE).content_id == cutoff);
    edit_save_boundary(f.edit, EDIT_SAVE_FORGOTTEN);
    replay(f.edit, UNDO_REPLAY_REDO);
    replay(f.edit, UNDO_REPLAY_UNDO);
    ECHECK(boundary(f.edit, EDIT_IDLE).content_id != cutoff);
    ef_close(&f);

    ef_open(&f, NULL, 0, undo_default_cap_bytes(), small_journal());
    const uint64_t burst_ns = UINT64_C(300000000); /* v1 undo.h:67 */
    type(f.edit, 0, 0);
    type(f.edit, 1, burst_ns);
    type(f.edit, 2, burst_ns - 1); /* backwards stamp splits */
    replay(f.edit, UNDO_REPLAY_UNDO);
    ef_text(f.edit, (const uint8_t *)"XX", 2);
    replay(f.edit, UNDO_REPLAY_UNDO);
    ECHECK(boundary(f.edit, EDIT_IDLE).content_id == 1);
    ef_text(f.edit, NULL, 0);
    ef_close(&f);
}

static void t_selection_and_branch(void)
{
    edit_fixture f;
    ef_open(&f, seed, 2, undo_default_cap_bytes(), small_journal());
    edit_selection_state selection = {.active = {2, 0}, .mark = {0, 2}, .mark_kind = EDIT_MARK_SET};
    ECHECK(edit_select(f.edit, selection) == EDIT_OK);
    retry_compound(&f);
    edit_boundary published = boundary(f.edit, EDIT_IDLE);
    ECHECK(published.selection.mark.cursor == 1 && published.selection.mark.anchor == 3);
    uint64_t saved_branch = published.content_id;
    edit_save_boundary(f.edit, EDIT_SAVE_CAPTURED);
    edit_save_boundary(f.edit, EDIT_SAVE_INSTALLED);
    replay(f.edit, UNDO_REPLAY_UNDO);
    published = boundary(f.edit, EDIT_IDLE);
    ECHECK(published.selection.active.cursor == 2 && published.selection.active.anchor == 0);
    ECHECK(published.selection.mark.cursor == 1 && published.selection.mark.anchor == 2);
    uint64_t branch = type(f.edit, 0, 0);
    replay(f.edit, UNDO_REPLAY_UNDO);
    replay(f.edit, UNDO_REPLAY_REDO);
    uint64_t redo_id = boundary(f.edit, EDIT_IDLE).content_id;
    ECHECK(redo_id != branch && redo_id != saved_branch);
    ef_text(f.edit, (const uint8_t *)"Xab", 3);
    ef_close(&f);

    ef_open(&f, NULL, 0, undo_default_cap_bytes(), small_journal());
    type(f.edit, 0, 0);
    /* Even an empty explicit plan seals a preceding ordinary burst. */
    ECHECK(edit_apply_plan(f.edit, NULL, 0, (edit_selection){1, 1}) == EDIT_OK);
    type(f.edit, 1, 0);
    replay(f.edit, UNDO_REPLAY_UNDO);
    ef_text(f.edit, key, 1);
    ef_close(&f);
}

static void t_eviction(void)
{
    edit_fixture f;
    /* One one-byte typing group, from the signed 32,768-group default. */
    size_t one = undo_default_cap_bytes() / 32768u;
    ef_open(&f, NULL, 0, one, small_journal());
    uint64_t cutoff = type(f.edit, 0, 0);
    edit_save_boundary(f.edit, EDIT_SAVE_CAPTURED);
    type(f.edit, 1, 0);
    const uint64_t separate = UINT64_C(300000001); /* one ns past v1's window */
    type(f.edit, 2, separate);
    edit_save_boundary(f.edit, EDIT_SAVE_INSTALLED);
    replay(f.edit, UNDO_REPLAY_UNDO);
    ef_text(f.edit, (const uint8_t *)"XX", 2);
    ECHECK(boundary(f.edit, EDIT_IDLE).content_id != cutoff);
    edit_boundary old = boundary(f.edit, EDIT_IDLE);
    ECHECK(edit_replay(f.edit, UNDO_REPLAY_UNDO, 0) == EDIT_OK);
    unchanged(f.edit, old, EDIT_IDLE);
    ef_close(&f);
}

int main(int argc, char **argv)
{
    const struct { const char *name; void (*run)(void); } cases[] = {
        {"refusals", t_refusals}, {"base", t_base}, {"conflict", t_conflict},
        {"A13-A19", t_counted}, {"E3", t_stream}, {"maintenance", t_maintenance},
        {"replay-refusal", t_replay_refusal}, {"stream-conflict", t_stream_conflict_and_repair},
        {"selection-branch", t_selection_and_branch}, {"eviction", t_eviction},
        {"MUST-FIX-2", t_trace}, {"fence", t_fence}, {"bookmarks-burst", t_bookmarks}
    };
    ECHECK(argc <= 2);
    size_t ran = 0;
    for (size_t i = 0; i < sizeof cases / sizeof *cases; i++) {
        if (argc > 1 && strcmp(argv[1], cases[i].name) != 0) continue;
        cases[i].run();
        printf("edit_test: %s passed\n", cases[i].name);
        ran++;
    }
    ECHECK(ran > 0);
    return EXIT_SUCCESS;
}
