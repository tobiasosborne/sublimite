/* libFuzzer: interpret input as an op script against tree and model. */
#include "piece/piece.h"
#include "../tests/piece_model.h"
#include <stdio.h>

#define NEED(k) do { if (i + (k) > size) goto done; } while (0)
#define FAIL() __builtin_trap()

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
    while (i < size) {
        uint8_t op = data[i++] % 6;
        if (op == 0 || op == 1) {            /* insert */
            NEED(3); uint64_t off = ((uint64_t)data[i] << 8 | data[i + 1]) % (m.n + 1); size_t l = data[i + 2] % 40; i += 3;
            NEED(l ? 1 : 0); uint8_t b[40];
            for (size_t j = 0; j < l; j++) b[j] = data[i + (j % (size - i))] & 0x0F ? data[i + (j % (size - i))] : '\n';
            if (piece_insert(t, off, b, l)) FAIL();
            pm_insert(&m, off, b, l);
        } else if (op == 2 || op == 3) {     /* delete */
            NEED(3); uint64_t off = m.n ? ((uint64_t)data[i] << 8 | data[i + 1]) % (m.n + 1) : 0;
            uint64_t l = data[i + 2]; i += 3; if (l > m.n - off) l = m.n - off;
            if (piece_delete(t, off, l)) FAIL();
            pm_delete(&m, off, l);
        } else if (op == 4) {                /* snapshot swap */
            if (snap) { piece_snapshot_release(snap); pm_free(&sm); }
            snap = piece_snapshot_take(t); if (!snap) FAIL();
            pm_init(&sm, m.d, m.n);
        } else {                             /* out-of-range must fail */
            uint8_t c; if (piece_read(t, m.n, &c, 1) == 0) FAIL();
            if (piece_delete(t, m.n, 1) == 0) FAIL();
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
    }
done:
    { piece_iter it; const uint8_t *p; size_t n, pos = 0; piece_iter_begin(&it, t, 0);
      while (piece_iter_next(&it, &p, &n)) { if (pos + n > m.n || memcmp(p, m.d + pos, n)) FAIL(); pos += n; }
      if (pos != m.n) FAIL(); }
    if (snap) { piece_snapshot_release(snap); pm_free(&sm); }
    piece_destroy(t); pm_free(&m);
    return 0;
}
