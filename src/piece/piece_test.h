/* Private synthesis observability. Sanitizer checks and explicit
 * -DPIECE_TESTING builds export these hooks; ordinary release builds do not. */
#ifndef PIECE_TEST_H
#define PIECE_TEST_H
#include "piece.h"
#if !defined(PIECE_TESTING)
#if defined(__SANITIZE_ADDRESS__)
#define PIECE_TESTING 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
#define PIECE_TESTING 1
#endif
#endif
#endif
#ifdef PIECE_TESTING
typedef struct piece_test_stats {
    uint64_t root_descents, cursor_hits, pathcopy_bytes, gap_deletes;
    size_t leaf_bytes, branch_bytes;
} piece_test_stats;
piece_test_stats piece_test_get_stats(const piece_tree *t);
void piece_test_reset_stats(piece_tree *t);
#endif
#endif
