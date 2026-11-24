/* Differential piece state machine. The mode byte selects private or
 * mapped ORIGINAL, and simulated or actual faults at the end of a mapped
 * case. The remaining header sets the initial length and fill pattern.
 * ph_step consumes the edit operations, including allocator refusals. */
#include "../tests/piece_harness.h"

#include <stddef.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

static uint8_t scratch[PH_SCRATCH];
static uint8_t init[3 * PIECE_CHUNK];

static void adopt(ph_state *st, uint64_t chunk)
{
    const piece_backing *b = &st->map->b;
    uint32_t count;
    bool counted = piece_lines_count(b, chunk, &count, 1);
    PH_CHECK(counted && piece_lines_adopt(st->t, b->fault, chunk, &count, 1),
             "fuzz adopt chunk %llu", PH_U(chunk));
}

static void step_lines(ph_state *st, uint64_t state, uint64_t chunks)
{
    adopt(st, state % chunks);
    /* A resolve that replaces the root ends the snapshot cache; the
     * harness reads that from the live revision (ph_op_take). */
    bool more;
    /* Small budgets from zero through a packed root plus leaf (P).
     * The final sweep uses the public liveness threshold. */
    const size_t budget = (size_t)(state >> 16) % 3u;
    PH_CHECK(piece_lines_resolve(st->t, budget, &more) == PIECE_OK, "fuzz bounded resolve");
}

static void drain_lines(ph_state *st)
{
    const piece_view *v = piece_tree_view(st->t);
    /* (P) Each successful full-path budget finishes or advances by at
     * least one byte. An unchanged root and table bound this loop. */
    uint64_t remaining = piece_length(v) + 1;
    bool more = true;
    while (remaining-- && more) {
        piece_err err = piece_lines_resolve(st->t, PIECE_MAX_HEIGHT + 1u, &more);
        PH_CHECK(err == PIECE_OK, "fuzz complete resolve");
        if (err != PIECE_OK) return;
    }
    PH_CHECK(!more, "fuzz resolve made no progress");
}

static void check_tainted(const piece_view *v)
{
    uint64_t off = 7, before = 7, count = 7;
    PH_CHECK(!piece_lines_before(v, piece_length(v), &count) && count == 0, "fuzz tainted rank");
    PH_CHECK(!piece_lines_start(v, 1, &off, &before) && off == 0 && before == 0,
             "fuzz tainted select");
    PH_CHECK(piece_lines_start(v, 0, &off, &before) && off == 0 && before == 0,
             "fuzz line origin remains exact");
}

static void finish_mapped(ph_state *st, unsigned flags, uint64_t state, uint64_t chunks)
{
    /* The initial ORIGINAL has at most three chunks (the array's size).
     * Shuffle their count batches independently of document order. */
    uint64_t order[] = {0, 1, 2};
    for (uint64_t i = chunks; i > 1; i--) {
        uint64_t j = ph_rand(&state) % i;
        uint64_t saved = order[i - 1];
        order[i - 1] = order[j];
        order[j] = saved;
    }

    ph_op_end(st, true);
    ph_op_begin(st, false);
    for (uint64_t i = 0; i < chunks; i++) {
        adopt(st, order[i]);
        bool more;
        PH_CHECK(piece_lines_resolve(st->t, (size_t)(state % 3u), &more) == PIECE_OK,
                 "fuzz checkpoint resolve");
        state = ph_rand(&state);
    }
    const piece_view *v = piece_tree_view(st->t);
    drain_lines(st);
    ph_check_lines(v, pm_tree_view(st->m), state, 1, true, "fuzz resolved");
    ph_op_end(st, true);
    ph_check_lines(v, pm_tree_view(st->m), state, 1, false, "fuzz restored annotations");
    drain_lines(st);
    ph_check_lines(v, pm_tree_view(st->m), state, 1, true, "fuzz resolved after abort");

    const piece_backing *b = &st->map->b;
    uint32_t batch;
    bool counted = piece_lines_count(b, 0, &batch, 1);
    PH_CHECK(counted, "fuzz pristine count");
    if (flags & 4u) {
        ph_poll_set(PH_POLL_FAULTED);
        PH_CHECK(!piece_lines_adopt(st->t, b->fault, 0, &batch, 1), "fuzz simulated fault adoption");
        check_tainted(v);
        ph_poll_set(PH_POLL_REAL);
    }
    if (flags & 2u) {
        int result = ftruncate(st->map->f.fd, 0);
        PH_CHECK(result == 0, "fuzz truncate memfd");
        if (result == 0) {
            /* No mapped load between truncation and this call. Its
             * own scan must trigger the fault, then close the poll. */
            uint32_t discarded;
            PH_CHECK(!piece_lines_count(b, 0, &discarded, 1), "fuzz real fault count");
            PH_CHECK(!piece_lines_adopt(st->t, b->fault, 0, &batch, 1), "fuzz real fault adoption");
            check_tainted(v);
        }
    }
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    ph_in in = {data, size};
    unsigned flags = ph_u8(&in);
    size_t n = ((size_t)ph_u8(&in) << 8 | ph_u8(&in)) * 3u;
    if (n > sizeof init) n = sizeof init;
    uint64_t state = ph_u8(&in) + 1u;
    ph_fill(init, n, (uint32_t)state);
    ph_mapping map;
    ph_state st;
    ph_failures = 0;
    if (ph_open(&st, init, n, flags & 1u, &map)) {
        uint64_t chunks = (n + PIECE_CHUNK - 1) / PIECE_CHUNK;
        while (ph_failures == 0 && ph_step(&st, &in, scratch)) {
            state = ph_rand(&state);
            if (st.map) step_lines(&st, state, chunks);
        }
        ph_check_snaps(&st, "fuzz snapshots");
        if (ph_failures == 0) {
            if (st.map) finish_mapped(&st, flags, state, chunks);
            else ph_check_lines(piece_tree_view(st.t), pm_tree_view(st.m), state, 1, true,
                                "fuzz private lines");
        }
    }
    ph_close(&st);
    if (ph_failures) __builtin_trap();
    return 0;
}
