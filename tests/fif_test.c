#include "fif/fif.h"
#if defined(_WIN32)
#include "win/fif_fixture.h"
#else
#include "tree_fixture.h"
#include <sched.h>
#endif
#include <string.h>
#define CHECK(x) EDIT_ASSERT(x)
/* (E) Test capacities come from the actual witness tree and the core's bounded
 * offsets. Ordinals/line/column expectations below count bytes in its literals;
 * zero is the empty count/index, one the first line/column or one bulk lane. */
typedef struct capture {
    fif_job *job;
    fif_hit hits[FIF_BINARY_BYTES + 1u];
    size_t count, files, completions;
    fif_done done;
} capture;
static void receive(const work_msg *msg, void *arg)
{
    capture *got = arg;
    if (msg->kind == FIF_MSG_HIT) {
        fif_hit hit; memcpy(&hit, msg->data, sizeof hit);
        CHECK(got->count < sizeof got->hits / sizeof *got->hits);
        got->hits[got->count++] = hit;
        fif_result_view view;
        CHECK(fif_results(got->job, hit.index, &view) == FIF_OK);
        CHECK(view.path && view.context && view.hit.byte_offset == hit.byte_offset);
    } else if (msg->kind == FIF_MSG_FILE) got->files++;
    else if (msg->kind == FIF_MSG_DONE) {
        memcpy(&got->done, msg->data, sizeof got->done); got->completions++;
    } else CHECK(false);
}
static void settle(work_pool *pool, fif_job *job, capture *got)
{
    while (!fif_job_finished(job) || work_mailbox_pending(pool)) {
        (void)work_mailbox_drain(pool, receive, got); sched_yield();
    }
    CHECK(got->completions == 1);
}
static void run(work_pool *pool, fif_request *request, capture *got)
{
    fif_job job = {0}; CHECK(fif_job_init(&job, request) == FIF_OK);
    memset(got, 0, sizeof *got); got->job = &job;
    CHECK(fif_submit(pool, &job, 0).epoch);
    settle(pool, &job, got);
    CHECK(got->done.code == FIF_OK && !got->done.cancelled);
    CHECK(fif_job_dispose(&job) == FIF_OK);
}
static void filters(void)
{
    fif_filter f;
    CHECK(fif_filter_parse(&f, " *.c, nested, -nested/skip* ", "*.o") == FIF_OK);
    CHECK(fif_filter_matches(&f, "deep/x.c"));
    CHECK(fif_filter_matches(&f, "nested/b.txt"));
    CHECK(!fif_filter_matches(&f, "nested/skip.txt"));
    CHECK(!fif_filter_matches(&f, "nested/x.o"));
    CHECK(!fif_filter_matches(&f, "other.txt"));
    CHECK(fif_glob_match("**/*.c", "deep/more/a.c"));
    CHECK(fif_glob_match("**/*.c", "a.c"));
    CHECK(!fif_glob_match("*.c", "deep/a.c"));
    CHECK(fif_glob_match("a[0-9]?.c", "a7x.c"));
    CHECK(!fif_glob_match("a[!0-9].c", "a7.c"));
}
static void searches(work_pool *pool, tree_fixture *tree)
{
    fif_request r = {.root = tree->root, .query = (const uint8_t *)"cat",
                     .query_length = sizeof "cat" - 1u, .options = {.match_case = true}};
    capture got; run(pool, &r, &got);
    const uint64_t offsets[] = {6, 10, 23, 32, 0, 5, 12, 0}; /* (E) counted in fixture strings. */
    const uint64_t lines[] = {1, 2, 2, 3, 1, 2, 3, 1};
    const uint64_t columns[] = {7, 1, 14, 6, 1, 2, 1, 1};
    CHECK(got.count == sizeof offsets / sizeof *offsets);
    for (size_t i = 0; i < got.count; i++) {
        CHECK(got.hits[i].byte_offset == offsets[i]);
        CHECK(got.hits[i].line == lines[i] && got.hits[i].column == columns[i]);
    }
    CHECK(got.done.files == 3 && got.done.binary == 1); /* (E) three text fixture files, one binary. */
    CHECK(got.done.symlinks == 1); /* (E) the loop is the sole symlink. */
    r.options.match_case = false; run(pool, &r, &got);
    CHECK(got.count == sizeof offsets / sizeof *offsets + 1u); /* (E) extra CAT. */
    r.options.whole_word = true; run(pool, &r, &got);
    CHECK(got.count == 7); /* (E) nine folded hits minus catapult/scatter. */
    r.options.regex = true; r.query = (const uint8_t *)"c[a]t";
    r.query_length = sizeof "c[a]t" - 1u; run(pool, &r, &got); CHECK(got.count == 7);
    r.options.match_case = true; r.query = (const uint8_t *)"^cat";
    r.query_length = sizeof "^cat" - 1u; run(pool, &r, &got); CHECK(got.count == 3); /* (E) b.c twice, skip.txt once. */
    r.options.regex = false; r.options.whole_word = false;
    r.query = (const uint8_t *)"cat"; r.query_length = sizeof "cat" - 1u;
    r.include = "*.c, -nested/skip.txt"; run(pool, &r, &got); CHECK(got.count == 3);
    r.include = "nested"; r.exclude = "skip.txt"; run(pool, &r, &got); CHECK(got.count == 3);
    r.include = NULL; r.exclude = NULL; r.limits.max_hits = 1;
    run(pool, &r, &got); CHECK(got.count == 1 && got.done.truncated);
    r.limits.max_hits = 0; r.limits.context_columns = sizeof "Alpha" - 1u;
    fif_job job = {0}; CHECK(fif_job_init(&job, &r) == FIF_OK);
    got = (capture){.job = &job}; CHECK(fif_submit(pool, &job, 0).epoch); settle(pool, &job, &got);
    fif_result_view view; CHECK(fif_results(&job, 0, &view) == FIF_OK);
    CHECK(view.context_length == sizeof "Alpha" - 1u && view.context_truncated);
    CHECK(!memcmp(view.context, "Alpha", view.context_length));
    CHECK(!strcmp(view.path + strlen(tree->root), "/a.txt"));
    CHECK(fif_job_dispose(&job) == FIF_OK);
}
static void limits_and_errors(work_pool *pool, tree_fixture *tree)
{
    fif_request r = {.root = tree->root, .query = (const uint8_t *)"cat",
        .query_length = sizeof "cat" - 1u, .options = {.match_case = true}};
    capture got;
    /* (E) First rejected filter length = PATH_MAX, beyond its NUL-inclusive
     * storage. Failed init must release its arena and leave an empty job. */
    char invalid[PATH_MAX + 1u]; memset(invalid, 'x', sizeof invalid); invalid[PATH_MAX] = 0;
    r.include = invalid; fif_job rejected = {0};
    CHECK(fif_job_init(&rejected, &r) == FIF_ERR_FILTER && rejected.impl == NULL);
    r.include = NULL;
    r.limits.max_files = 1; run(pool, &r, &got);
    CHECK(got.done.files == 1 && got.done.truncated && got.count > 0);
    r.limits.max_files = 0; r.limits.max_entries = 1;
    run(pool, &r, &got); CHECK(got.count == 0 && got.done.truncated);
    r.limits.max_entries = 0; r.limits.max_file_bytes = sizeof "cat" - 1u;
    run(pool, &r, &got); CHECK(got.count == 1 && got.done.truncated);
    r.limits.max_file_bytes = 0;
    CHECK(tree_fixture_dir(tree, "nested/deeper") == 0);
    CHECK(tree_fixture_file(tree, "nested/deeper/deep", "cat", sizeof "cat" - 1u) == 0);
    r.limits.max_depth = 1; run(pool, &r, &got);
    CHECK(got.done.truncated && got.count == 8); /* (E) original eight hits, deeper file skipped. */
    char absolute[PATH_MAX]; CHECK(tree_fixture_path(tree, "a.txt", absolute) == 0);
    r.limits.max_depth = 0; r.include = absolute;
    run(pool, &r, &got); CHECK(got.count == 4); /* (E) four sensitive hits in a.txt. */
    r.include = NULL; r.query = (const uint8_t *)"["; r.query_length = sizeof "[" - 1u; r.options.regex = true;
    fif_job job = {0}; CHECK(fif_job_init(&job, &r) == FIF_OK);
    got = (capture){.job = &job}; CHECK(fif_submit(pool, &job, 0).epoch); settle(pool, &job, &got);
    CHECK(got.done.code == FIF_ERR_SEARCH && got.done.search_code == FIND_ERR_SYNTAX);
    CHECK(fif_job_dispose(&job) == FIF_OK);
    /* (E) The probe boundary is production FIF_BINARY_BYTES; append one NUL
     * beyond it to distinguish the requested initial-span binary heuristic. */
    uint8_t boundary[FIF_BINARY_BYTES + 1u]; memset(boundary, 'x', sizeof boundary);
    boundary[FIF_BINARY_BYTES] = 0;
    CHECK(tree_fixture_file(tree, "boundary", boundary, sizeof boundary) == 0);
    r.options.regex = false; r.query = (const uint8_t *)"x"; r.query_length = sizeof "x" - 1u;
    r.include = "boundary"; r.limits.max_hits = sizeof boundary;
    run(pool, &r, &got); CHECK(got.done.hits == FIF_BINARY_BYTES && got.done.binary == 0);
    boundary[FIF_BINARY_BYTES - 1u] = 0;
    CHECK(tree_fixture_file(tree, "boundary", boundary, sizeof boundary) == 0);
    run(pool, &r, &got); CHECK(got.done.hits == 0 && got.done.binary == 1);
}
/* A parked bulk lease makes queued cancellation independent of scheduling. */
typedef struct queued_gate { atomic_bool entered, release; } queued_gate;
static void park_bulk(work_ctx *context)
{
    queued_gate *gate = context->arg;
    atomic_store_explicit(&gate->entered, true, memory_order_release);
    while (!atomic_load_explicit(&gate->release, memory_order_acquire) && !work_should_stop(context)) sched_yield();
}
static void backpressure_cancel_and_guard(work_pool *pool, tree_fixture *tree)
{
    /* (E) One more hit than the entire mailbox proves retry; each hit is 'x'. */
    uint8_t text[WORK_MAILBOX_CAP + 1u]; memset(text, 'x', sizeof text);
    CHECK(tree_fixture_file(tree, "dense", text, sizeof text) == 0);
    fif_request r = {.root = tree->root, .query = (const uint8_t *)"x", .query_length = 1,
                     .include = "dense", .exclude = "nested", .options = {.match_case = true}};
    fif_job job = {0}; CHECK(fif_job_init(&job, &r) == FIF_OK);
    uint64_t initial_full = atomic_load(&pool->dropped_full);
    capture got = {.job = &job}; work_handle handle = fif_submit(pool, &job, 0); CHECK(handle.epoch);
    while (atomic_load(&pool->dropped_full) == initial_full) sched_yield();
    edit_malloc_guard_begin(); settle(pool, &job, &got);
    CHECK(edit_malloc_guard_end() == 0);
    CHECK(got.count == sizeof text && !got.done.truncated);
    CHECK(fif_job_dispose(&job) == FIF_OK);
    /* The gate holds the bulk lane so this cancellation removes a queued job
     * before any filesystem work. The companion must still deliver DONE. */
    r.include = NULL; r.exclude = NULL; r.query = (const uint8_t *)"cat"; r.query_length = sizeof "cat" - 1u;
    r.limits.max_hits = WORK_MAILBOX_CAP + 1u;
    CHECK(fif_job_init(&job, &r) == FIF_OK);
    queued_gate gate; atomic_init(&gate.entered, false); atomic_init(&gate.release, false);
    CHECK(work_submit(pool, (work_job){park_bulk, &gate, 0, WORK_BULK}).epoch);
    while (!atomic_load_explicit(&gate.entered, memory_order_acquire)) sched_yield();
    got = (capture){.job = &job}; handle = fif_submit(pool, &job, 0); CHECK(handle.epoch);
    work_cancel(pool, handle); atomic_store_explicit(&gate.release, true, memory_order_release);
    settle(pool, &job, &got);
    CHECK(got.done.cancelled && got.done.code == FIF_CANCELLED);
    CHECK(fif_job_dispose(&job) == FIF_OK);
    /* (E) Force a mid-walk stop by saturating hits in the lexically first file. */
    CHECK(tree_fixture_file(tree, "a.txt", text, sizeof text) == 0);
    r.query = (const uint8_t *)"x"; r.query_length = 1; r.limits.max_hits = 0;
    CHECK(fif_job_init(&job, &r) == FIF_OK); got = (capture){.job = &job};
    uint64_t full = atomic_load(&pool->dropped_full);
    handle = fif_submit(pool, &job, 0); CHECK(handle.epoch);
    while (atomic_load(&pool->dropped_full) == full) sched_yield();
    work_cancel(pool, handle); settle(pool, &job, &got);
    CHECK(got.done.cancelled && got.done.files == 1 && got.done.hits > 0);
    CHECK(fif_job_dispose(&job) == FIF_OK);
}
/* edit-n95 engine fixes. (E) Witness sizes come from the fixture literals and
 * the production context cap; zero/one keep their count/index meanings. */
static void n95_fixes(work_pool *pool, tree_fixture *tree)
{
    fif_request r = {.root = tree->root, .query = (const uint8_t *)"cat",
                     .query_length = sizeof "cat" - 1u, .options = {.match_case = true}};
    capture got;
    /* 1. A symlinked project root is followed; entries below keep the skip rule. */
    char link[PATH_MAX]; strcpy(link, tree->root); strcat(link, "-link");
#if defined(_WIN32)
    CHECK(dep_junction(tree->root, link) == 0);
#else
    CHECK(symlink(tree->root, link) == 0);
#endif
    r.root = link; run(pool, &r, &got);
    CHECK(got.count == 8 && got.done.symlinks == 1); /* (E) the eight sensitive hits, loop skipped. */
#if defined(_WIN32)
    CHECK(dep_remove(link) == 0); r.root = tree->root;
    /* Native absolute filters and a trailing project separator select the
     * same a.txt witness as the slash spelling below. */
    char native_root[PATH_MAX], native_file[PATH_MAX];
    strcpy(native_root, tree->root); strcat(native_root, "\\");
    CHECK(tree_fixture_path(tree, "a.txt", native_file) == 0);
    for (char *at = native_file; *at; at++) if (*at == '/') *at = '\\';
    r.root = native_root; r.include = native_file; run(pool, &r, &got);
    CHECK(got.count == 4); /* (E) four sensitive hits in a.txt. */
    r.root = tree->root; r.include = NULL;
#else
    CHECK(unlink(link) == 0); r.root = tree->root;
#endif
    /* 2. An unreadable subdirectory counts an error but keeps the walk OK. */
#if !defined(_WIN32)
    if (geteuid() != 0) {
        CHECK(tree_fixture_dir(tree, "locked") == 0);
        CHECK(tree_fixture_file(tree, "locked/x", "cat", sizeof "cat" - 1u) == 0);
        char locked[PATH_MAX]; CHECK(tree_fixture_path(tree, "locked", locked) == 0);
        CHECK(chmod(locked, 0) == 0);
        run(pool, &r, &got); /* run() requires code OK */
        CHECK(got.count == 8 && got.done.errors >= 1);
        CHECK(chmod(locked, S_IRWXU) == 0); CHECK(tree_fixture_remove(locked) == 0);
    }
#else
    puts("fif_test: SKIP POSIX chmod unreadable directory (Windows uses ACLs)");
#endif
    /* A missing root is still a whole-job I/O failure. */
    char missing[PATH_MAX]; CHECK(tree_fixture_path(tree, "no-such-dir", missing) == 0);
    r.root = missing; fif_job job = {0}; CHECK(fif_job_init(&job, &r) == FIF_OK);
    got = (capture){.job = &job}; CHECK(fif_submit(pool, &job, 0).epoch); settle(pool, &job, &got);
    CHECK(got.done.code == FIF_ERR_IO && got.count == 0);
    CHECK(fif_job_dispose(&job) == FIF_OK); r.root = tree->root;
    /* 3. A context cut by the column cap ends on a UTF-8 boundary: "cat",
     * filler up to one byte before the cap, then a two-byte scalar. */
    static uint8_t line[FIF_DEFAULT_CONTEXT + 16u];
    size_t n = 0; memcpy(line, "cat", 3); n = 3;
    while (n < FIF_DEFAULT_CONTEXT - 1u) line[n++] = 'x';
    line[n++] = 0xc3; line[n++] = 0xa9; memcpy(line + n, "tail", 4); n += 4;
    CHECK(tree_fixture_file(tree, "wide.txt", line, n) == 0);
    r.include = "wide.txt"; CHECK(fif_job_init(&job, &r) == FIF_OK);
    got = (capture){.job = &job}; CHECK(fif_submit(pool, &job, 0).epoch); settle(pool, &job, &got);
    fif_result_view view; CHECK(got.count == 1 && fif_results(&job, 0, &view) == FIF_OK);
    CHECK(view.context_truncated && view.context_length == FIF_DEFAULT_CONTEXT - 1u);
    CHECK(view.context[view.context_length - 1u] == 'x');
    CHECK(fif_job_dispose(&job) == FIF_OK);
    char wide[PATH_MAX]; CHECK(tree_fixture_path(tree, "wide.txt", wide) == 0); CHECK(os_unlink(wide) == OS_OK);
    r.include = NULL;
}
int main(void)
{
    filters(); tree_fixture tree; CHECK(tree_fixture_init(&tree) == 0);
    edit_arena arena; CHECK(edit_arena_init(&arena, sizeof(work_pool) + _Alignof(work_pool)) == 0);
    work_pool *pool = edit_arena_alloc(&arena, sizeof *pool, _Alignof(work_pool)); CHECK(pool);
    CHECK(work_pool_init(pool, 1, 0) == 0);
    n95_fixes(pool, &tree); searches(pool, &tree); limits_and_errors(pool, &tree); backpressure_cancel_and_guard(pool, &tree);
    work_pool_shutdown(pool); edit_arena_free(&arena); CHECK(tree_fixture_remove(tree.root) == 0);
    puts("fif_test: filters, search, walker, cancellation, truncation, retry and allocation guard passed");
    return 0;
}
