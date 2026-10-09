/* Reference model for piece tests: flat buffer, naive, trivially correct. */
#ifndef PIECE_MODEL_H
#define PIECE_MODEL_H
#include "piece/piece.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct pm_checkpoint pm_checkpoint;
typedef struct { uint8_t *d; size_t n, cap; pm_checkpoint *checkpoint; } pm_model;
struct pm_checkpoint { pm_model *owner; uint8_t *d; size_t n, cap; };

static inline void pm_init(pm_model *m, const uint8_t *d, size_t n) {
    m->cap = n + 16; m->n = n; m->d = (uint8_t *)malloc(m->cap); m->checkpoint = NULL;
    if (n) memcpy(m->d, d, n);
}
static inline void pm_free(pm_model *m) {
    if (m->checkpoint) { free(m->checkpoint->d); free(m->checkpoint); }
    free(m->d); *m = (pm_model){0};
}
/* P1.3c oracle: save a flat copy at begin; abort transfers it back. Kernel
 * allocator failure injection never applies to this independent model. */
static inline int pm_checkpoint_begin(pm_model *m, pm_checkpoint **out) {
    if (out) *out = NULL;
    if (!m || !out || m->checkpoint) return PIECE_ERR_RANGE;
    pm_checkpoint *c = malloc(sizeof *c);
    if (!c) return PIECE_ERR_NOMEM;
    c->d = malloc(m->cap);
    if (!c->d) { free(c); return PIECE_ERR_NOMEM; }
    if (m->n) memcpy(c->d, m->d, m->n);
    c->owner = m; c->n = m->n; c->cap = m->cap;
    m->checkpoint = c; *out = c;
    return PIECE_OK;
}
static inline void pm_checkpoint_commit(pm_checkpoint *c) {
    c->owner->checkpoint = NULL; free(c->d); free(c);
}
static inline void pm_checkpoint_abort(pm_checkpoint *c) {
    pm_model *m = c->owner;
    free(m->d); m->d = c->d; m->n = c->n; m->cap = c->cap;
    m->checkpoint = NULL; free(c);
}
static inline void pm_insert(pm_model *m, size_t off, const uint8_t *d, size_t n) {
    if (m->n + n > m->cap) { m->cap = (m->n + n) * 2; m->d = (uint8_t *)realloc(m->d, m->cap); }
    memmove(m->d + off + n, m->d + off, m->n - off);
    if (n) memcpy(m->d + off, d, n);
    m->n += n;
}
static inline void pm_delete(pm_model *m, size_t off, size_t n) {
    memmove(m->d + off, m->d + off + n, m->n - off - n);
    m->n -= n;
}
static inline uint64_t pm_line_count(const pm_model *m) {
    uint64_t c = 1;
    for (size_t i = 0; i < m->n; i++) if (m->d[i] == '\n') c++;
    return c;
}
static inline uint64_t pm_line_to_byte(const pm_model *m, uint64_t line) {
    if (line == 0) return 0;
    uint64_t c = 0;
    for (size_t i = 0; i < m->n; i++)
        if (m->d[i] == '\n' && ++c == line) return i + 1;
    return m->n;
}
static inline uint64_t pm_byte_to_line(const pm_model *m, uint64_t off) {
    uint64_t c = 0;
    if (off > m->n) off = m->n;
    for (size_t i = 0; i < off; i++) if (m->d[i] == '\n') c++;
    return c;
}
#endif
