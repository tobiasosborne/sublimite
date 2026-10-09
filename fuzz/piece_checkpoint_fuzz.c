/* P1.3c: random checkpoint/edit scripts ending in commit/abort, with transient
 * and persistent NOMEM. Mandatory healthy begin makes the link-only stub fail
 * even on empty input. */
#include "piece/piece.h"
#include "../tests/piece_model.h"
#include <stdio.h>

#define CP_REQUIRE(c) do { if (!(c)) { fprintf(stderr, "piece_checkpoint_fuzz: %s:%d: %s\n", __FILE__, __LINE__, #c); __builtin_trap(); } } while (0)

typedef struct {
    piece_allocator backing;
    size_t live, attempts, remaining;
    int armed, persistent;
} cp_fuzz_allocator;
static void *cp_fuzz_alloc(void *ctx, size_t n) {
    cp_fuzz_allocator *f = ctx; f->attempts++;
    if (f->armed) {
        if (!f->remaining) { if (!f->persistent) f->armed = 0; return NULL; }
        f->remaining--;
    }
    void *p = f->backing.alloc(f->backing.ctx, n);
    if (p) f->live += n;
    return p;
}
static void cp_fuzz_free(void *ctx, void *p, size_t n) {
    cp_fuzz_allocator *f = ctx; CP_REQUIRE(n <= f->live);
    f->live -= n; f->backing.free(f->backing.ctx, p, n);
}
static void cp_fuzz_inject(cp_fuzz_allocator *f, uint8_t control) {
    f->armed = control != 0; f->persistent = (control & 128u) != 0;
    f->remaining = (size_t)(control & 7u);
}
typedef struct { const uint8_t *data; size_t size, pos; } cp_script;
static uint8_t cp_next(cp_script *s) { return s->pos < s->size ? s->data[s->pos++] : 0; }
static size_t cp_offset(cp_script *s, size_t n) {
    size_t off = (size_t)cp_next(s) << 8; off |= cp_next(s);
    return off % (n + 1);
}
static void cp_fuzz_same(const piece_tree *t, const pm_model *m, uint8_t sample) {
    CP_REQUIRE(piece_len(t) == m->n && piece_line_count(t) == pm_line_count(m));
    piece_iter it; const uint8_t *p; size_t n, pos = 0;
    piece_iter_begin(&it, t, 0);
    while (piece_iter_next(&it, &p, &n)) {
        CP_REQUIRE(n && n <= m->n - pos && !memcmp(p, m->d + pos, n)); pos += n;
    }
    CP_REQUIRE(pos == m->n);
    size_t off = (size_t)sample % (m->n + 1), len = m->n - off;
    uint8_t b[64]; if (len > sizeof b) len = sizeof b;
    CP_REQUIRE(piece_read(t, off, b, len) == PIECE_OK && (!len || !memcmp(b, m->d + off, len)));
    uint64_t line = (uint64_t)sample % (pm_line_count(m) + 1);
    CP_REQUIRE(piece_line_to_byte(t, line) == pm_line_to_byte(m, line));
    CP_REQUIRE(piece_byte_to_line(t, off) == pm_byte_to_line(m, off));
    CP_REQUIRE(piece_line_to_byte(t, UINT64_MAX) == m->n);
    CP_REQUIRE(piece_byte_to_line(t, UINT64_MAX) == pm_line_count(m) - 1);
}
typedef struct { piece_snapshot *s; pm_model model; } cp_fuzz_snapshot;
static void cp_fuzz_snapshot_same(const cp_fuzz_snapshot *snap, uint8_t sample) {
    if (!snap->s) return;
    const pm_model *m = &snap->model;
    CP_REQUIRE(piece_snapshot_len(snap->s) == m->n && piece_snapshot_line_count(snap->s) == pm_line_count(m));
    piece_iter it; const uint8_t *p; size_t n, pos = 0;
    piece_iter_begin_snapshot(&it, snap->s, 0);
    while (piece_iter_next(&it, &p, &n)) {
        CP_REQUIRE(n && n <= m->n - pos && !memcmp(p, m->d + pos, n)); pos += n;
    }
    CP_REQUIRE(pos == m->n);
    size_t off = (size_t)sample % (m->n + 1), len = m->n - off;
    uint8_t b[64]; if (len > sizeof b) len = sizeof b;
    CP_REQUIRE(piece_snapshot_read(snap->s, off, b, len) == PIECE_OK && (!len || !memcmp(b, m->d + off, len)));
    uint64_t line = (uint64_t)sample % (pm_line_count(m) + 1);
    CP_REQUIRE(piece_snapshot_line_to_byte(snap->s, line) == pm_line_to_byte(m, line));
    CP_REQUIRE(piece_snapshot_byte_to_line(snap->s, off) == pm_byte_to_line(m, off));
}
static void cp_fuzz_begin(piece_tree *t, pm_model *m, cp_fuzz_allocator *f,
                          uint8_t control, piece_checkpoint **c, pm_checkpoint **mc) {
    uint64_t pieces = piece_piece_count(t);
    cp_fuzz_inject(f, control);
    int rc = piece_checkpoint_begin(t, c);
    CP_REQUIRE(rc == PIECE_OK || (rc == PIECE_ERR_NOMEM && !*c));
    cp_fuzz_same(t, m, control); CP_REQUIRE(piece_piece_count(t) == pieces);
    f->armed = 0;
    /* Healthy retry prevents a permanent-NOMEM stub from passing vacuously. */
    if (rc == PIECE_ERR_NOMEM) rc = piece_checkpoint_begin(t, c);
    CP_REQUIRE(rc == PIECE_OK && *c);
    CP_REQUIRE(pm_checkpoint_begin(m, mc) == PIECE_OK && *mc);
}
static void cp_fuzz_end(piece_tree *t, pm_model *m, cp_fuzz_allocator *f,
                        piece_checkpoint **c, pm_checkpoint **mc, int commit,
                        uint64_t saved_pieces, uint8_t sample) {
    /* Both terminal operations must issue ZERO allocation requests. */
    f->armed = 1; f->persistent = 1; f->remaining = 0;
    size_t attempts = f->attempts;
    uint64_t pieces = piece_piece_count(t);
    if (commit) { piece_checkpoint_commit(*c); pm_checkpoint_commit(*mc); }
    else { piece_checkpoint_abort(*c); pm_checkpoint_abort(*mc); }
    *c = NULL; *mc = NULL;
    CP_REQUIRE(f->attempts == attempts);
    CP_REQUIRE(piece_piece_count(t) == (commit ? pieces : saved_pieces));
    /* Abort compares with the model's saved pre-begin bytes, not an inverse
     * edit script; commit compares with all successfully applied model edits. */
    cp_fuzz_same(t, m, sample); f->armed = 0;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    cp_script script = { data, size, 0 };
    uint8_t initial[64]; size_t init = (size_t)(cp_next(&script) & 63u);
    for (size_t j = 0; j < init; j++) initial[j] = cp_next(&script);
    cp_fuzz_allocator f = { .backing = piece_default_allocator() };
    piece_allocator a = { &f, cp_fuzz_alloc, cp_fuzz_free };
    piece_tree *t = piece_create(&a); CP_REQUIRE(t);
    CP_REQUIRE(piece_init_copy(t, initial, init) == PIECE_OK);
    pm_model m; pm_init(&m, initial, init);
    cp_fuzz_snapshot snaps[2] = {0};
    snaps[0].s = piece_snapshot_take(t); CP_REQUIRE(snaps[0].s); pm_init(&snaps[0].model, m.d, m.n);
    piece_checkpoint *c = NULL; pm_checkpoint *mc = NULL;
    uint64_t saved_pieces = piece_piece_count(t);
    cp_fuzz_begin(t, &m, &f, 0, &c, &mc);
    while (script.pos < script.size) {
        if (!c) {
            saved_pieces = piece_piece_count(t);
            cp_fuzz_begin(t, &m, &f, cp_next(&script), &c, &mc);
        }
        uint8_t op = cp_next(&script) & 7u, control = cp_next(&script);
        cp_fuzz_inject(&f, control);
        if (op <= 1) {
            size_t off = cp_offset(&script, m.n); uint8_t length = cp_next(&script);
            size_t len = (size_t)(length & 63u) + ((length & 128u) ? 65536u : 0u);
            if (len > 262144u - m.n) len = 262144u - m.n;
            uint8_t b[65599]; uint8_t seed = cp_next(&script);
            for (size_t j = 0; j < len; j++) b[j] = (uint8_t)(j % 13 == 0 ? '\n' : seed);
            int rc = piece_insert(t, off, b, len);
            CP_REQUIRE(rc == PIECE_OK || rc == PIECE_ERR_NOMEM);
            if (rc == PIECE_OK) pm_insert(&m, off, b, len);
        } else if (op == 2 || op == 3) {
            size_t off = cp_offset(&script, m.n), len = cp_offset(&script, m.n - off);
            piece_ref ref = {0}; uint8_t *saved = NULL;
            if (op == 3) { saved = malloc(len + 1); CP_REQUIRE(saved); if (len) memcpy(saved, m.d + off, len); }
            int rc = piece_delete(t, off, len, op == 3 ? &ref : NULL);
            CP_REQUIRE(rc == PIECE_OK || rc == PIECE_ERR_NOMEM);
            if (rc == PIECE_OK) {
                pm_delete(&m, off, len); cp_fuzz_same(t, &m, control);
                if (op == 3) {
                    CP_REQUIRE(ref.len == len && ref.nspans <= PIECE_REF_SPANS);
                    off = cp_offset(&script, m.n); cp_fuzz_inject(&f, cp_next(&script));
                    rc = piece_insert_ref(t, off, &ref);
                    CP_REQUIRE(rc == PIECE_OK || rc == PIECE_ERR_NOMEM);
                    if (rc == PIECE_OK) pm_insert(&m, off, saved, len);
                }
            }
            free(saved);
        } else if (op == 4) {
            size_t slot = (size_t)(cp_next(&script) & 1u);
            piece_snapshot *s = piece_snapshot_take(t);
            if (s) {
                if (snaps[slot].s) { piece_snapshot_release(snaps[slot].s); pm_free(&snaps[slot].model); }
                snaps[slot].s = s; pm_init(&snaps[slot].model, m.d, m.n);
            } else { CP_REQUIRE(control != 0); }
        } else if (op == 5 || op == 6) {
            cp_fuzz_end(t, &m, &f, &c, &mc, op == 5, saved_pieces, control);
        } else {
            piece_checkpoint *nested = c;
            CP_REQUIRE(piece_checkpoint_begin(t, &nested) == PIECE_ERR_RANGE && !nested);
            CP_REQUIRE(piece_checkpoint_begin(t, NULL) == PIECE_ERR_RANGE);
        }
        f.armed = 0; cp_fuzz_same(t, &m, control);
        for (size_t j = 0; j < 2; j++) cp_fuzz_snapshot_same(&snaps[j], control);
    }
    if (c) cp_fuzz_end(t, &m, &f, &c, &mc, 0, saved_pieces, 0);
    piece_destroy(t); pm_free(&m);
    for (size_t j = 0; j < 2; j++) {
        cp_fuzz_snapshot_same(&snaps[j], 0);
        if (snaps[j].s) { piece_snapshot_release(snaps[j].s); pm_free(&snaps[j].model); }
    }
    CP_REQUIRE(f.live == 0);
    return 0;
}
