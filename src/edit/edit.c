#include "edit/edit.h"

#include <string.h>

#include "piece/piece.h"

size_t edit_smoke(void)
{
    piece_reserve *r;
    if (piece_reserve_create(&r) != PIECE_OK) return 0;
    piece_tree *t;
    const char *id = os_app_id();
    if (piece_open_copy(r, (const uint8_t *)id, strlen(id), &t) != PIECE_OK) {
        piece_reserve_destroy(r);
        return 0;
    }
    size_t length = (size_t)piece_length(piece_tree_view(t));
    piece_close(t);
    piece_reserve_destroy(r);
    return length;
}
