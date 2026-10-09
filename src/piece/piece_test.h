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
    size_t leaf_bytes, branch_bytes, snapshot_bytes;
    unsigned height;
} piece_test_stats;
piece_test_stats piece_test_get_stats(const piece_tree *t);
typedef struct piece_test_memory {
    size_t slabs[3], live[3], underfull_leaves, leaf_pieces;
    uint64_t deleted_original, fallback_add;
} piece_test_memory;
piece_test_memory piece_test_get_memory(const piece_tree *t);
void piece_test_reset_stats(piece_tree *t);
/* Quiescent snapshot only; fake owners do not invoke mapping hooks. Restore
 * the real count before releasing the test's owners. */
void piece_test_snapshot_set_owners(piece_snapshot *s, unsigned owners);
unsigned piece_test_snapshot_owners(const piece_snapshot *s);
/* Replace an empty tree with `len` repetitions of add byte zero (non-newline).
 * A small shared-node DAG represents the huge logical content exactly. */
int piece_test_repeat_byte(piece_tree *t, uint64_t len);
/* Fake physical add capacity for rejection-only probes; restore before any
 * successful mutation/read. Returns the previous logical add length. */
uint64_t piece_test_set_add_length(piece_tree *t, uint64_t len);
/* Identity and lengths used to verify refusal precedes COW, append and cursor
 * invalidation. No recursive walk of the enormous shared test fixture. */
typedef struct piece_test_state {
    const void *root, *cached_snapshot;
    uint64_t len, root_len, add_len, run_end, run_addend;
    int run_valid, cursor_valid;
} piece_test_state;
piece_test_state piece_test_get_state(const piece_tree *t);
#endif
#endif
