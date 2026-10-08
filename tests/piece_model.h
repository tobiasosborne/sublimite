/* Reference model for piece tests: flat buffer, naive, trivially correct. */
#ifndef PIECE_MODEL_H
#define PIECE_MODEL_H
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct { uint8_t *d; size_t n, cap; } pm_model;

static inline void pm_init(pm_model *m, const uint8_t *d, size_t n) {
    m->cap = n + 16; m->n = n; m->d = (uint8_t *)malloc(m->cap);
    if (n) memcpy(m->d, d, n);
}
static inline void pm_free(pm_model *m) { free(m->d); m->d = NULL; }
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
