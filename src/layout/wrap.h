#ifndef EDIT_LAYOUT_WRAP_PRIVATE_H
#define EDIT_LAYOUT_WRAP_PRIVATE_H
#include "layout/layout.h"
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
