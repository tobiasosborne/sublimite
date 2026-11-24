/* check-layers L1: linked --whole-archive against lower layers only. */
#include "piece/piece.h"

int main(void)
{
    static const uint8_t text[] = "\n";
    piece_reserve *r;
    if (piece_reserve_create(&r) != PIECE_OK) return 1;
    piece_tree *t;
    if (piece_open_copy(r, text, sizeof text - 1, &t) != PIECE_OK) {
        piece_reserve_destroy(r);
        return 1;
    }
    const piece_view *v = piece_tree_view(t);
    uint64_t count;
    bool ok = piece_length(v) == sizeof text - 1 && piece_byte_at(v, 0) == '\n' &&
              piece_lines_before(v, sizeof text - 1, &count) && count == 1;
    piece_close(t);
    piece_reserve_destroy(r);
    return ok ? 0 : 1;
}
