/* libFuzzer: interpret input as an op script against tree and model. */
#include "piece/piece.h"
#include "piece/piece_test.h"
#include "../tests/piece_model.h"
#include <pthread.h>
#include <stdio.h>

#define NEED(k) do { if (i + (k) > size) goto done; } while (0)
#define FAIL() __builtin_trap()
static void *retire_snapshot(void *ctx) {
    piece_snapshot_release(ctx); return NULL;
}

static uint64_t wide_decode(const uint8_t *p) {
    uint64_t v = 0;
    for (unsigned k = 0; k < 8; k++) v |= (uint64_t)p[k] << (8 * k);
    return v;
}
/* Raw uint64_t inputs plus deliberately frequent EOF/high-bit positions. */
static uint64_t wide_position(uint64_t raw, uint64_t total) {
    switch (raw & 7) {
    case 0: return total;
    case 1: return UINT64_MAX;
    case 2: return UINT64_MAX - (raw >> 3 & 255);
    case 3: return (UINT64_C(1) << 32) + (raw >> 3 & 65535);
    case 4: return (UINT64_C(1) << 63) + (raw >> 3 & 65535);
    default: return raw;
    }
}
static void wide_probe(piece_tree *t, piece_snapshot *s, const pm_model *m,
                       const pm_model *sm, uint64_t off, size_t n) {
    uint8_t b[256];
    int valid = off <= m->n && n <= m->n - off;
    int rc = piece_read(t, off, b, n);
    if (rc != (valid ? PIECE_OK : PIECE_ERR_RANGE)) FAIL();
    if (valid && n && memcmp(b, m->d + off, n)) FAIL();
    if (piece_byte_to_line(t, off) != pm_byte_to_line(m, off) ||
        piece_line_to_byte(t, off) != pm_line_to_byte(m, off)) FAIL();
    piece_iter it; const uint8_t *p; size_t span;
    piece_iter_begin(&it, t, off);
    int next = piece_iter_next(&it, &p, &span);
    if (next != (off < m->n)) FAIL();
    if (next && (!span || span > m->n - off || memcmp(p, m->d + off, span))) FAIL();
    if (s) {
        valid = off <= sm->n && n <= sm->n - off;
        rc = piece_snapshot_read(s, off, b, n);
        if (rc != (valid ? PIECE_OK : PIECE_ERR_RANGE)) FAIL();
        if (valid && n && memcmp(b, sm->d + off, n)) FAIL();
        if (piece_snapshot_byte_to_line(s, off) != pm_byte_to_line(sm, off) ||
            piece_snapshot_line_to_byte(s, off) != pm_line_to_byte(sm, off)) FAIL();
        piece_iter_begin_snapshot(&it, s, off);
        next = piece_iter_next(&it, &p, &span);
        if (next != (off < sm->n)) FAIL();
        if (next && (!span || span > sm->n - off || memcmp(p, sm->d + off, span))) FAIL();
    }
}
#ifdef PIECE_TESTING
static void growth_probe(uint64_t raw, uint8_t amount) {
    piece_allocator a = piece_default_allocator(); piece_tree *t = piece_create(&a);
    if (!t) FAIL();
    if (piece_insert(t, 0, (const uint8_t *)"x", 1) || piece_delete(t, 0, 1, NULL)) FAIL();
    uint64_t total = UINT64_MAX - (raw & 7);
    if (piece_test_repeat_byte(t, total)) FAIL();
    /* Prime an exclusive cursor at a high offset, or exercise snapshot COW. */
    if ((raw & 8) && total < UINT64_MAX) {
        if (piece_insert(t, total, (const uint8_t *)"x", 1)) FAIL();
        total++;
    }
    piece_snapshot *s = NULL;
    if (raw & 16) { s = piece_snapshot_take(t); if (!s) FAIL(); }
    uint64_t snapshot_len = total;
    uint8_t b[8]; memset(b, 'x', sizeof b);
    size_t n = 1 + (amount & 7u);
    piece_test_state before = piece_test_get_state(t);
    int rc = piece_insert(t, raw & 32 ? 0 : total, b, n);
    if (rc != (n <= UINT64_MAX - total ? PIECE_OK : PIECE_ERR_RANGE)) FAIL();
    if (rc == PIECE_OK) total += n;
    else {
        piece_test_state after = piece_test_get_state(t);
        if (before.root != after.root || before.add_len != after.add_len ||
            before.cached_snapshot != after.cached_snapshot ||
            before.cursor_valid != after.cursor_valid) FAIL();
    }
    piece_ref ref = {0}; ref.nspans = 1 + (amount >> 3 & 7u); ref.len = ref.nspans;
    for (unsigned k = 0; k < ref.nspans; k++) ref.span[k].len = 1;
    before = piece_test_get_state(t);
    rc = piece_insert_ref(t, total, &ref);
    if (rc != (ref.len <= UINT64_MAX - total ? PIECE_OK : PIECE_ERR_RANGE)) FAIL();
    if (rc == PIECE_OK) total += ref.len;
    else {
        piece_test_state after = piece_test_get_state(t);
        if (before.root != after.root || before.add_len != after.add_len ||
            before.cached_snapshot != after.cached_snapshot) FAIL();
    }
    if (piece_len(t) != total || piece_test_get_state(t).root_len != total) FAIL();
    if (piece_read(t, total - 1, b, 1) || b[0] != 'x' || piece_read(t, total, NULL, 0)) FAIL();
    if (piece_delete(t, total, 1, NULL) != PIECE_ERR_RANGE) FAIL();
    /* Cursor deletion must form its pointer using a piece-relative offset. */
    if (raw & 8) {
        if (piece_delete(t, total - 1, 1, &ref) || piece_insert_ref(t, total - 1, &ref)) FAIL();
    }
    if (s) {
        if (piece_snapshot_len(s) != snapshot_len || piece_snapshot_read(s, snapshot_len - 1, b, 1)) FAIL();
        piece_snapshot_release(s);
    }
    piece_destroy(t);
}
#endif

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    piece_allocator a = piece_default_allocator();
    piece_tree *t = piece_create(&a);
    pm_model m; size_t i = 0;
    size_t init = size ? data[0] % 64 : 0; i = 1;
    uint8_t ib[64]; for (size_t j = 0; j < init; j++) ib[j] = (j % 5 == 4) ? '\n' : (uint8_t)('a' + j);
    if (piece_init_copy(t, ib, init)) FAIL();
    pm_init(&m, ib, init);
    piece_snapshot *snap = NULL; pm_model sm = {0};
    piece_ref saved[4] = {0}; uint8_t saved_bytes[4][256];
    size_t saved_len[4] = {0}; unsigned save_slot = 0;
    while (i < size) {
        uint8_t op = data[i++] % 12;
        if (op == 0 || op == 1) {            /* insert */
            NEED(3); uint64_t off = ((uint64_t)data[i] << 8 | data[i + 1]) % (m.n + 1); size_t l = data[i + 2] % 40; i += 3;
            NEED(l ? 1 : 0); uint8_t b[40];
            for (size_t j = 0; j < l; j++) b[j] = data[i + (j % (size - i))] & 0x0F ? data[i + (j % (size - i))] : '\n';
            if (piece_insert(t, off, b, l)) FAIL();
            pm_insert(&m, off, b, l);
        } else if (op == 2 || op == 3) {     /* delete */
            NEED(3); uint64_t off = m.n ? ((uint64_t)data[i] << 8 | data[i + 1]) % (m.n + 1) : 0;
            uint64_t l = data[i + 2]; i += 3; if (l > m.n - off) l = m.n - off;
            if (piece_delete(t, off, l, NULL)) FAIL();
            pm_delete(&m, off, l);
        } else if (op == 4) {                /* snapshot swap */
            if (snap) { piece_snapshot_release(snap); pm_free(&sm); }
            snap = piece_snapshot_take(t); if (!snap) FAIL();
            pm_init(&sm, m.d, m.n);
        } else if (op == 6) {                /* delete then reinsert by ref */
            NEED(5); uint64_t off = m.n ? ((uint64_t)data[i] << 8 | data[i + 1]) % (m.n + 1) : 0;
            uint64_t l = data[i + 2]; uint64_t off2; i += 3; if (l > m.n - off) l = m.n - off;
            uint8_t sv[256]; if (l && piece_read(t, off, sv, (size_t)l)) FAIL();
            piece_ref r; if (piece_delete(t, off, l, &r) || r.len != l) FAIL();
            saved[save_slot] = r; saved_len[save_slot] = (size_t)l;
            if (l) memcpy(saved_bytes[save_slot], sv, (size_t)l);
            save_slot = (save_slot + 1) % 4;
            pm_delete(&m, off, l);
            off2 = ((uint64_t)data[i] << 8 | data[i + 1]) % (m.n + 1); i += 2;
            if (piece_insert_ref(t, off2, &r)) FAIL();
            pm_insert(&m, off2, sv, (size_t)l);
        } else if (op == 7) {                 /* full-width query and edit positions */
            NEED(9); uint64_t off = wide_position(wide_decode(data + i), m.n);
            size_t n = data[i + 8]; i += 9;
            wide_probe(t, snap, &m, &sm, off, n);
            if (off > m.n) {
                if (piece_insert(t, off, (const uint8_t *)"x", 1) != PIECE_ERR_RANGE ||
                    piece_delete(t, off, 0, NULL) != PIECE_ERR_RANGE ||
                    piece_insert_ref(t, off, &saved[0]) != PIECE_ERR_RANGE) FAIL();
            } else {
                if (piece_insert(t, off, NULL, 0) || piece_delete(t, off, 0, NULL)) FAIL();
            }
        } else if (op == 8) {                 /* retain deleted references for later reuse */
            NEED(9); uint64_t off = wide_decode(data + i) % (m.n + 1);
            size_t n = data[i + 8]; i += 9; if (n > m.n - off) n = m.n - off;
            if (piece_read(t, off, saved_bytes[save_slot], n) ||
                piece_delete(t, off, n, &saved[save_slot])) FAIL();
            saved_len[save_slot] = n; save_slot = (save_slot + 1) % 4;
            pm_delete(&m, off, n);
        } else if (op == 9) {                 /* logical growth at UINT64_MAX */
            NEED(9); uint64_t raw = wide_decode(data + i); uint8_t n = data[i + 8]; i += 9;
#ifdef PIECE_TESTING
            growth_probe(raw, n);
#else
            (void)raw; (void)n;
#endif
        } else if (op == 10) {                /* repeated reference replay grows the model */
            NEED(10); uint64_t raw = wide_decode(data + i);
            unsigned slot = data[i + 8] % 4, reps = 1 + data[i + 9] % 16u; i += 10;
            for (unsigned k = 0; k < reps && m.n + saved_len[slot] <= 65536; k++) {
                uint64_t off = raw % (m.n + 1);
                if (piece_insert_ref(t, off, &saved[slot])) FAIL();
                pm_insert(&m, off, saved_bytes[slot], saved_len[slot]);
            }
        } else if (op == 11) {                /* worker retirement / bounded owner drain */
            NEED(1); size_t budget = data[i++] % 65u;
            piece_snapshot *retire = piece_snapshot_take(t); if (!retire) FAIL();
            if (piece_insert(t, 0, (const uint8_t *)"x", 1)) FAIL();
            pm_insert(&m, 0, (const uint8_t *)"x", 1);
            pthread_t worker;
            if (pthread_create(&worker, NULL, retire_snapshot, retire)) FAIL();
            (void)piece_reclaim(t, budget);
            if (pthread_join(worker, NULL)) FAIL();
            (void)piece_reclaim(t, budget);
        } else {                             /* out-of-range must fail */
            uint8_t c; if (piece_read(t, m.n, &c, 1) == 0) FAIL();
            if (piece_delete(t, m.n, 1, NULL) == 0) FAIL();
        }
        if (piece_len(t) != m.n || piece_line_count(t) != pm_line_count(&m)) FAIL();
        if (m.n) {
            uint8_t *b = malloc(m.n); if (piece_read(t, 0, b, m.n) || memcmp(b, m.d, m.n)) FAIL(); free(b);
            uint64_t l = data[i - 1] % (pm_line_count(&m) + 1);
            if (piece_line_to_byte(t, l) != pm_line_to_byte(&m, l)) FAIL();
            if (piece_byte_to_line(t, l * 3) != pm_byte_to_line(&m, l * 3)) FAIL();
        }
        if (snap) {
            if (piece_snapshot_len(snap) != sm.n) FAIL();
            uint8_t *b = malloc(sm.n + 1); if (piece_snapshot_read(snap, 0, b, sm.n) || (sm.n && memcmp(b, sm.d, sm.n))) FAIL(); free(b);
        }
        /* EOF is valid for empty reads/no-op edits and is always iter end. */
        wide_probe(t, snap, &m, &sm, m.n, 0);
    }
done:
    { piece_iter it; const uint8_t *p; size_t n, pos = 0; piece_iter_begin(&it, t, 0);
      while (piece_iter_next(&it, &p, &n)) { if (pos + n > m.n || memcmp(p, m.d + pos, n)) FAIL(); pos += n; }
      if (pos != m.n) FAIL(); }
    if (snap) { piece_snapshot_release(snap); pm_free(&sm); }
    piece_destroy(t); pm_free(&m);
    return 0;
}
