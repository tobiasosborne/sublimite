/* White-box interface of the real tree, compiled only into piece.c's
 * PIECE_TEST_HOOKS variant build/<cfg>/src/piece/piece_test_hooks.o, which
 * the Makefile links into piece_test, piece_shape_test and piece_fuzz only;
 * never into an archive or the shipped binary (CLAUDE.md, v2-0fv.16).
 * The hooks build also ASSERTs that every slot an operation draws fits the
 * claim it admitted (docs/decisions/v2-0fv.17.md, "Claim check"). */
#ifndef PIECE_TEST_H
#define PIECE_TEST_H

#include "piece/piece.h"

typedef struct piece_test_stats {
    /* Shape: branch levels, entries in the root, live view revision. */
    unsigned height, root_entries;
    uint64_t revision;
    /* Admission: the last claim in slots (a compound replace adds its
     * deletion claim), slots occupied by this core (nodes, headers, held
     * test slots), free slots this core could draw without base (its own
     * plus the reserve's whole extents), slots per extent, extents this core
     * owns, whole free extents in the reserve. */
    size_t slots_claimed, live_slots, available_slots, slots_per_extent, extents, free_extents;
    /* Physical sizes in native pages, rounded per base mapping. */
    size_t page, chunk_pages, dir_pages, core_pages, tree_pages, reserve_pages;
    /* ADD chunks and directory nodes this core holds; the reserve's spare
     * private boundary chunk (0 or 1) and spare directory nodes. */
    size_t add_chunks, add_dirs, spare_chunks, spare_dirs;
    /* Since piece_test_reset_reads: bytes read from the mapped ORIGINAL
     * (edits, describe with counts, queries, resolve; not the sanitizer-only
     * provenance recount), bytes read from private stores, query node
     * visits, resolve node visits (the last call). */
    size_t mapped_reads, private_reads, query_visits, resolve_visits;
    /* ADD directory levels walked or copied, process-wide (A7). */
    size_t dir_steps;
    /* Free slots in this core's own extents; retirement steps since
     * piece_test_reset_reads; extents waiting in the reserve's inbox;
     * owners of this core (the tree and one per live snapshot header). */
    size_t core_free_slots, retire_steps, inbox_extents;
    /* The last resolve call: nodes it entered plus the children whose
     * reference counts its branch copies updated (piece.h: at most
     * 17 x budget). */
    size_t resolve_touched;
    uint64_t core_owners;
} piece_test_stats;

/* Lowers the branch-level limit (<= PIECE_MAX_HEIGHT), process-wide. */
void piece_test_set_height_limit(unsigned levels);
void piece_test_reset_reads(piece_tree *t);
piece_test_stats piece_test_measure(const piece_tree *t);
/* Funds `slots` free slots for t's core exactly as admission would. */
bool piece_test_fund_slots(piece_tree *t, size_t slots);
/* Holds one funded slot outside any claim; release each on the same tree
 * before close. Used to leave an exact number of free slots. */
void piece_test_hold_slot(piece_tree *t);
void piece_test_release_slot(piece_tree *t);
/* Owners of a snapshot header. */
uint64_t piece_test_snapshot_owners(const piece_snapshot *s);
/* Adds delta to the owner count of s, or of t's core when s is NULL, as if
 * that many owners came or went (Astra 7b-F); the test restores it. */
void piece_test_shift_owners(piece_tree *t, piece_snapshot *s, int64_t delta);
/* The reference count of the live leaf holding offset off, and a setter that
 * stands in for references no test can build (2^32 of them). */
uint32_t piece_test_node_refs(const piece_tree *t, uint64_t off);
void piece_test_set_node_refs(piece_tree *t, uint64_t off, uint32_t refs);

#endif
