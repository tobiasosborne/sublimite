#ifndef EDIT_LAYOUT_WRAP_PRIVATE_H
#define EDIT_LAYOUT_WRAP_PRIVATE_H
#include "layout/layout.h"
/* An estimate cannot lose more lines than it knows about or exceed the
 * newline-per-byte ceiling. Arithmetic is checked before either operation. */
static inline uint64_t layout_estimated_lines(uint64_t lines, uint64_t bytes,
                                              uint64_t old_nl, uint64_t new_nl)
{
    uint64_t ceiling = bytes == UINT64_MAX ? UINT64_MAX : bytes + 1u;
    uint64_t remaining = old_nl < lines ? lines - old_nl : 1u;
    if (remaining > ceiling) remaining = ceiling;
    return new_nl > ceiling - remaining ? ceiling : remaining + new_nl;
}
void layout_wrap_geometry(layout *l);
void layout_wrap_begin(layout *l);
int layout_wrap_run(layout *l);
int layout_wrap_edit(layout *l, uint64_t off, uint64_t old_len, uint64_t new_len,
                     uint64_t old_nl, uint64_t new_nl);
int layout_wrap_relayout(layout *l, uint32_t first, uint32_t count);
void layout_refill(layout *l);
void layout_consume(layout *l, size_t n);
int layout_decode_cluster(layout *l, const uint8_t **bytes, size_t *len, int *width, size_t budget);
#endif
