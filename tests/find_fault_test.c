#include "find/find.h"

#include <stdio.h>
#include <string.h>

#define CHECK(expr) do { if (!(expr)) { \
    fprintf(stderr, "find_fault_test:%d: %s\n", __LINE__, #expr); return 1; \
} } while (0)

/* (P) Two short mapped runs with an ADD separator. The wrapped iterator
 * supplies the second run only after zeroing its backing tail, simulating
 * recovery immediately before the first load in that run. The whole fixture
 * fits inside one polling interval, including the final short candidate. */
static uint8_t backing_bytes[] = "pristine|suffix";
static const size_t split = sizeof "pristine|"-1;
static const os_fault_id fault = {.gen = 1}; /* (P) one synthetic live id */
static bool recovered;
static size_t polls, before_recovery;
static unsigned owners;

os_err __real_os_fault_poll(os_fault_id id, uint64_t *epoch);
os_err __wrap_os_fault_poll(os_fault_id id, uint64_t *epoch)
{
    if (id.gen != fault.gen || id.slot != fault.slot) return __real_os_fault_poll(id, epoch);
    polls++;
    if (!recovered) before_recovery++;
    *epoch = recovered;
    return OS_OK;
}

size_t __real_piece_iter_next(piece_iter *it, const uint8_t **bytes);
size_t __wrap_piece_iter_next(piece_iter *it, const uint8_t **bytes)
{
    size_t n = __real_piece_iter_next(it, bytes);
    if (n && *bytes == backing_bytes + split) {
        memset(backing_bytes + split, 0, sizeof backing_bytes - 1 - split);
        recovered = true;
    }
    return n;
}

static void acquire(void *ctx) { (*(unsigned *)ctx)++; }
static void release(void *ctx) { (*(unsigned *)ctx)--; }

static bool record(void *ctx, uint64_t ordinal, find_capture range)
{
    (void)ordinal;
    (void)range;
    (*(size_t *)ctx)++;
    return true;
}

static void restore(void)
{
    memcpy(backing_bytes + split, "suffix", sizeof "suffix");
    recovered = false;
    polls = before_recovery = 0;
}

int main(void)
{
    piece_reserve *reserve;
    piece_tree *tree;
    piece_snapshot *snapshot;
    piece_backing backing = {backing_bytes, sizeof backing_bytes - 1, fault,
                             &owners, acquire, release};
    CHECK(piece_reserve_create(&reserve) == PIECE_OK);
    CHECK(piece_open_mapped(reserve, &backing, &tree) == PIECE_OK);
    CHECK(piece_replace(tree, split, 0, (const uint8_t *)"!", 1) == PIECE_OK);
    CHECK(piece_snapshot_take(tree, &snapshot) == PIECE_OK);
    piece_close(tree);
    find_source source = {.view = piece_snapshot_view(snapshot)};
    /* (P) One zero byte would match every byte of the recovered suffix. */
    const uint8_t zero = 0;
    const find_literal_options literal = {0};
    size_t emitted = 0;
    find_visit visit = {.prefix_capacity = FIND_MAX_OFFSETS, .wanted = FIND_UNSET,
                        .emit = record, .user = &emitted};
    find_code code = find_literal_visit(&source, &zero, sizeof zero, literal, NULL, &visit);
    printf("mapped mid-find: recovered=%d pristine_polls=%zu polls=%zu code=%d emitted=%zu\n",
           recovered, before_recovery, polls, (int)code, emitted);
    CHECK(recovered && before_recovery && polls > before_recovery);
    CHECK(code == FIND_CANCELLED && emitted == 0 && visit.total == 0 && visit.visible == 0);

    restore();
    find_result result;
    CHECK(find_literal(&source, &zero, sizeof zero, literal, NULL, &result) == FIND_CANCELLED);
    CHECK(recovered && !result.total && !result.stored);
    restore();
    find_match match;
    CHECK(find_literal_next(&source, &zero, sizeof zero, literal, 0, NULL, &match) == FIND_CANCELLED);
    CHECK(recovered && !match.matched && match.whole.start == FIND_UNSET);

    _Alignas(max_align_t) uint8_t program[FIND_MAX_PROGRAM_BYTES];
    _Alignas(max_align_t) uint8_t memory[FIND_MAX_SCRATCH_BYTES];
    find_regex *regex;
    CHECK(find_regex_compile(program, sizeof program, &zero, sizeof zero, &regex, NULL) == FIND_OK);
    restore();
    CHECK(find_regex_visit(&source, regex, memory, sizeof memory, NULL, &visit) == FIND_CANCELLED);
    CHECK(recovered && !emitted && !visit.total && !visit.visible);
    restore();
    CHECK(find_regex_stream_begin(&source, regex, memory, sizeof memory, false) == FIND_OK);
    bool done;
    CHECK(find_regex_stream_step(memory, NULL, &visit, &done) == FIND_CANCELLED);
    CHECK(recovered && !done && !emitted && !visit.total && !visit.visible);

    /* (P) Once tainted, even a search which reads no input cannot succeed. */
    CHECK(find_literal(&source, NULL, 0, literal, NULL, &result) == FIND_CANCELLED);
    piece_snapshot_release(snapshot);
    piece_reserve_destroy(reserve);
    CHECK(owners == 0);
    puts("find_fault_test: recovered zero suffix never reached a result or sink");
    return 0;
}
