/* P1.7f: process-kill VISIBILITY, late cancellation, pooled save allocation.
 * These tests cannot certify power-loss durability; see P1.7f's file_io proposal.
 * Kept separate from P1.7c's file_test.c edits. */
#include "file/file.h"
#include "base/base.h"
#include "trace/trace.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

/* Compile the bench's deterministic adversarial checks into make check.
 * The renamed entry point is never used for measurements here. */
#define main file_bench_test_driver
#include "../bench/file_bench.c"
#undef main

typedef struct file_test_context {
    char dir[128], path[160];
    int failures;
} file_test_context;
typedef struct file_cancel_context { int step, stopped, seen; } file_cancel_context;
typedef struct file_job_context {
    _Atomic int started, release, done;
    file *f;
    file_msg completion;
    int seen;
} file_job_context;
#define CHECK(test, cond) do { if (!(cond)) { \
    fprintf(stderr, "file_kill_test:%d: FAIL %s\n", __LINE__, #cond); (test)->failures++; \
} } while (0)

static void file_test_pause(void)
{
    struct timespec ts = {0, 100000}; (void)nanosleep(&ts, NULL);
}
static int file_test_write(const char *path, const uint8_t *bytes, size_t len)
{
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0640);
    if (fd < 0) return 1;
    size_t done = 0; int rc = 0;
    while (done < len) {
        ssize_t n = write(fd, bytes + done, len - done);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) { rc = 1; break; } done += (size_t)n;
    }
    if (fsync(fd) != 0 || close(fd) != 0) rc = 1;
    return rc;
}
static int file_test_bytes(const char *path, const uint8_t *want, size_t len)
{
    int fd = open(path, O_RDONLY); struct stat st;
    if (fd < 0) return 0;
    int good = fstat(fd, &st) == 0 && st.st_size == (off_t)len;
    uint8_t got[4096];
    for (size_t off = 0; off < len && good;) {
        size_t part = len - off < sizeof got ? len - off : sizeof got;
        ssize_t n = pread(fd, got, part, (off_t)off);
        if (n < 0 && errno == EINTR) continue;
        good = n == (ssize_t)part && memcmp(got, want + off, part) == 0; off += part;
    }
    close(fd); return good;
}
static int file_test_temps(file_test_context *test, int remove_them)
{
    DIR *d = opendir(test->dir); if (!d) return -1;
    int count = 0; struct dirent *entry;
    while ((entry = readdir(d))) if (strstr(entry->d_name, ".edit-")) {
        count++;
        if (remove_them) {
            char path[512]; snprintf(path, sizeof path, "%s/%s", test->dir, entry->d_name);
            CHECK(test, unlink(path) == 0);
        }
    }
    closedir(d); return count;
}
static void file_test_kill(void *ctx, int step)
{
    if (step == *(int *)ctx) (void)kill(getpid(), SIGKILL);
}
static void file_test_cancel_step(void *ctx, int step)
{
    file_cancel_context *c = ctx;
    if (step == c->step) { c->stopped = 1; c->seen = 1; }
}
static int file_test_stop(void *ctx) { return ((file_cancel_context *)ctx)->stopped; }
static void file_test_visibility(file_test_context *test)
{
    uint8_t old[65536], next[65536]; memset(old, 'o', sizeof old); memset(next, 'n', sizeof next);
    piece_allocator alloc = piece_default_allocator(); piece_tree *tree = piece_create(&alloc);
    CHECK(test, tree && piece_init_copy(tree, next, sizeof next) == PIECE_OK);
    if (!tree) return;
    piece_snapshot *s = piece_snapshot_take(tree); CHECK(test, s != NULL);
    if (!s) { piece_destroy(tree); return; }
    for (int step = FILE_STEP_TEMP_CREATED; step <= FILE_STEP_DIR_SYNCED; step++) {
        CHECK(test, file_test_write(test->path, old, sizeof old) == 0);
        pid_t pid = fork(); CHECK(test, pid >= 0);
        if (pid < 0) break;
        if (pid == 0) {
            file_save_args a = {.path = test->path, .snap = s, .step = file_test_kill, .step_ctx = &step};
            (void)file_save_write(&a); _exit(90);
        }
        int ws = 0; CHECK(test, waitpid(pid, &ws, 0) == pid);
        CHECK(test, WIFSIGNALED(ws) && WTERMSIG(ws) == SIGKILL);
        CHECK(test, file_test_bytes(test->path, step < FILE_STEP_RENAMED ? old : next, sizeof old));
        CHECK(test, file_test_temps(test, 0) == (step < FILE_STEP_RENAMED ? 1 : 0));
        (void)file_test_temps(test, 1);
    }
    /* Cancel AFTER temp creation, mid-write, and after data sync: every path
     * must preserve the target, remove the temp and report cancellation. */
    for (int step = FILE_STEP_TEMP_CREATED; step <= FILE_STEP_FSYNCED; step++) {
        CHECK(test, file_test_write(test->path, old, sizeof old) == 0);
        file_cancel_context ctx = {.step = step};
        file_save_args a = {.path = test->path, .snap = s, .step = file_test_cancel_step,
            .step_ctx = &ctx, .stop = file_test_stop, .stop_ctx = &ctx};
        CHECK(test, file_save_write(&a) == FILE_ERR_CANCELLED && ctx.seen);
        CHECK(test, file_test_bytes(test->path, old, sizeof old));
        CHECK(test, file_test_temps(test, 0) == 0);
        (void)file_test_temps(test, 1);
    }
    piece_snapshot_release(s); piece_destroy(tree);
}
static void *file_test_alloc(void *ctx, size_t n)
{
    return edit_arena_alloc(ctx, n, 16);
}
static void file_test_free(void *ctx, void *p, size_t n)
{
    /* Arena storage lives through pool shutdown; releases on workers are safe. */
    (void)ctx; (void)p; (void)n;
}
static void file_test_blocker(work_ctx *ctx)
{
    file_job_context *job = ctx->arg; atomic_store(&job->started, 1);
    while (!atomic_load(&job->release) && !work_should_stop(ctx)) file_test_pause();
    atomic_store(&job->done, 1);
}
static void file_test_collect(const work_msg *wm, void *ctx)
{
    file_job_context *job = ctx; file_msg msg;
    if (file_msg_decode(wm, &msg) == FILE_OK && msg.f == job->f && msg.kind == FILE_MSG_SAVE_DONE) {
        job->seen++; job->completion = msg;
    }
}
static void file_test_allocation(file_test_context *test)
{
    work_pool pool; edit_arena arena;
    CHECK(test, edit_arena_init(&arena, 16u << 20) == 0);
    CHECK(test, work_pool_init(&pool, 1, 0) == 0);
    uint8_t bytes[4096]; memset(bytes, 'a', sizeof bytes);
    CHECK(test, file_test_write(test->path, bytes, sizeof bytes) == 0);
    file *f = NULL; CHECK(test, file_open_begin(&pool, test->path, NULL, &f) == FILE_OK);
    piece_allocator alloc = {&arena, file_test_alloc, file_test_free};
    piece_tree *tree = piece_create(&alloc); CHECK(test, tree && f && file_attach(f, tree) == FILE_OK);
    if (!tree || !f) { if (tree) piece_destroy(tree); if (f) file_close(f); work_pool_shutdown(&pool); edit_arena_free(&arena); return; }
    /* Warm snapshot infrastructure, then force a FRESH snapshot for each ack. */
    piece_snapshot *warm = piece_snapshot_take(tree); CHECK(test, warm != NULL);
    if (warm) piece_snapshot_release(warm);
    for (uint32_t generation = 1; generation <= 8; generation++) {
        uint8_t byte = (uint8_t)('A' + generation);
        CHECK(test, piece_delete(tree, 0, 1, NULL) == 0 && piece_insert(tree, 0, &byte, 1) == 0);
        file_job_context job = {.f = f};
        work_handle h = work_submit(&pool, (work_job){file_test_blocker, &job, 0, WORK_BULK});
        CHECK(test, h.epoch != 0);
        while (!atomic_load(&job.started)) file_test_pause();
        edit_malloc_guard_begin();
        int rc = file_save_begin(f, tree, 0, generation);
        size_t allocations = edit_malloc_guard_end();
        CHECK(test, rc == FILE_OK);
        if (edit_malloc_guard_active()) CHECK(test, allocations == 0);
        atomic_store(&job.release, 1);
        for (unsigned i = 0; i < 100000 && (!job.seen || file_save_busy(f)); i++) {
            (void)work_mailbox_drain(&pool, file_test_collect, &job); file_test_pause();
        }
        CHECK(test, atomic_load(&job.done) && job.seen == 1 && job.completion.status == FILE_OK &&
                    job.completion.generation == generation && job.completion.size == sizeof bytes);
        bytes[0] = byte; CHECK(test, file_test_bytes(test->path, bytes, sizeof bytes));
        CHECK(test, file_test_temps(test, 0) == 0);
    }
    printf("file_save_alloc: %s pooled fresh ack guard=%s\n", test->failures ? "FAIL" : "PASS", edit_malloc_guard_active() ? "active" : "ASan-inert (release required)");
    piece_destroy(tree); file_close(f); work_pool_shutdown(&pool); edit_arena_free(&arena);
}
static void file_test_viewport(file_test_context *test)
{
    work_pool pool;
    CHECK(test, work_pool_init(&pool, 1, 4) == 0);
    view *v = calloc(1, sizeof *v);
    int rc = !v || view_init(v, &pool);
    CHECK(test, rc == 0);
    if (!rc) {
        CHECK(test, v->grid.dims.cell_w == 18 && v->grid.dims.cell_h == 36);
        CHECK(test, layout_prefix(v, (const uint8_t *)"visible\n", 8) == 0);
        for (size_t i = 0; i < 7; i++) CHECK(test, v->cells[i].glyph_index == (uint32_t)"visible"[i]);
        CHECK(test, submit_view(v, &pool) == 0);
        CHECK(test, finish_frame(v, &pool) == 0);
        render_stats stats;
        CHECK(test, render_backend_stats(&v->backend, &stats) == 0 &&
                    stats.submitted_frames == 1 && stats.presented_frames == 1);
        render_cell preserved[COLS]; memcpy(preserved, v->cells, sizeof preserved);
        CHECK(test, saving_shown(v, &pool) == 0 && finish_frame(v, &pool) == 0);
        CHECK(test, memcmp(preserved, v->cells, sizeof preserved) == 0);
        CHECK(test, !v->grid.full_frame && v->dirty == (UINT64_C(1) << (ROWS - 1u)));
        for (size_t i = 0; i < 6; i++)
            CHECK(test, v->cells[(ROWS - 1u) * COLS + i].glyph_index == (uint32_t)"saving"[i]);
        CHECK(test, render_backend_stats(&v->backend, &stats) == 0 && stats.presented_frames == 2);
    }
    if (v) {
        render_backend_shutdown(&v->backend); free(v->state);
        if (v->platform.conn) plat_shutdown(&v->platform);
        free(v);
    }
    work_pool_shutdown(&pool);
}
static int file_test_queued_open_child(file_test_context *test)
{
    trace_init();
    work_pool pool;
    if (work_pool_init(&pool, 1, 4) != 0) return 1;
    view *v = calloc(1, sizeof *v);
    if (!v || view_init(v, &pool)) return 1;
    /* Exercise a skipped frame ID, as used by the untimed reference grid. */
    v->serial = 1;
    const unsigned conditions[] = {0, 1, 3};
    size_t repeats = getenv("FILE_KILL_STRESS") ? 100u : 3u;
    for (size_t i = 0; i < 3u * repeats; i++) {
        busy_jobs busy;
        if (getenv("FT_V") && i % repeats == 0) fprintf(stderr, "file queued-open probe: busy%u start\n", conditions[i / repeats]);
        if (start_busy(&pool, &busy, conditions[i / repeats])) return 1;
        file *f = NULL;
        file_open_opts opts = {.copy_threshold = 1, .generation = (uint32_t)i + 1u};
        if (getenv("FT_V") && i % repeats == 0) fprintf(stderr, "file queued-open probe: open\n");
        if (file_open_begin(&pool, test->path, &opts, &f) != FILE_OK) return 1;
        size_t n = 0; const uint8_t *bytes = file_prefix(f, &n);
        if (layout_prefix(v, bytes, n)) return 1;
        if (getenv("FT_V") && i % repeats == 0) fprintf(stderr, "file queued-open probe: submit\n");
        if (submit_view(v, &pool)) return 1;
        if (getenv("FT_V") && i % repeats == 0) fprintf(stderr, "file queued-open probe: release bulk\n");
        stop_busy(&pool, &busy);
        if (finish_frame(v, &pool)) return 1;
        if (getenv("FT_V") && i % repeats == 0) fprintf(stderr, "file queued-open probe: close\n");
        file_close(f);
        collector c = {0}; (void)work_mailbox_drain(&pool, collect, &c);
    }
    render_backend_shutdown(&v->backend); free(v->state); plat_shutdown(&v->platform); free(v);
    work_pool_shutdown(&pool);
    return 0;
}
static int file_test_queued_save_child(file_test_context *test)
{
    trace_init();
    work_pool pool;
    if (work_pool_init(&pool, 1, 4) != 0) return 1;
    view *v = calloc(1, sizeof *v);
    if (!v || view_init(v, &pool)) return 1;
    file *f = NULL;
    if (file_open_begin(&pool, test->path, NULL, &f) != FILE_OK || !file_open_ready(f)) return 1;
    piece_allocator alloc = piece_default_allocator(); piece_tree *tree = piece_create(&alloc);
    if (!tree || file_attach(f, tree) != FILE_OK) return 1;
    for (uint64_t i = 0; i < 4096; i++)
        if (piece_insert(tree, (i * 997u) % piece_len(tree), (const uint8_t *)"xy", 2) != 0) return 1;
    const unsigned conditions[] = {0, 1, 3};
    uint32_t repeats = getenv("FILE_KILL_STRESS") ? 100u : 3u;
    for (uint32_t i = 0; i < 3u * repeats; i++) {
        if (getenv("FT_V")) fprintf(stderr, "file queued-save probe: sample%u busy%u start\n", i, conditions[i / repeats]);
        if (prepare_fresh(tree, i + 1u)) return 1;
        busy_jobs busy; if (start_busy(&pool, &busy, conditions[i / repeats])) return 1;
        collector c = {.f = f, .kind = FILE_MSG_SAVE_DONE, .generation = i + 1u, .backend = &v->backend};
        v->pending = &c;
        if (getenv("FT_V")) fprintf(stderr, "file queued-save probe: ack\n");
        if (file_save_begin(f, tree, 0, i + 1u) != FILE_OK) return 1;
        if (getenv("FT_V")) fprintf(stderr, "file queued-save probe: status\n");
        if (saving_shown(v, &pool)) return 1;
        if (getenv("FT_V")) fprintf(stderr, "file queued-save probe: release bulk\n");
        stop_busy(&pool, &busy);
        if (getenv("FT_V")) fprintf(stderr, "file queued-save probe: completion\n");
        if (await_message(&pool, &c) || !completion_correct(&c.msg, f, i + 1u, piece_len(tree))) return 1;
        piece_snapshot *snapshot = piece_snapshot_take(tree);
        if (!snapshot || output_correct(test->path, snapshot)) return 1;
        piece_snapshot_release(snapshot);
        if (finish_frame(v, &pool)) return 1;
        v->pending = NULL;
    }
    piece_destroy(tree); file_close(f);
    render_backend_shutdown(&v->backend); free(v->state); plat_shutdown(&v->platform); free(v);
    work_pool_shutdown(&pool);
    return 0;
}
static void file_test_queued(file_test_context *test, int saving)
{
    pid_t pid = fork(); CHECK(test, pid >= 0);
    if (pid < 0) return;
    if (pid == 0) { alarm(getenv("FILE_KILL_STRESS") ? 300u : 30u); _exit(saving ? file_test_queued_save_child(test) : file_test_queued_open_child(test)); }
    int status = 0; CHECK(test, waitpid(pid, &status, 0) == pid);
    CHECK(test, WIFEXITED(status) && WEXITSTATUS(status) == 0);
}
int main(void)
{
    file_test_context test = {0}; strcpy(test.dir, "/tmp/edit-file-kill-XXXXXX");
    if (!mkdtemp(test.dir)) return 2;
    snprintf(test.path, sizeof test.path, "%s/target", test.dir);
    trace_init();
    CHECK(&test, self_check(26) == 0);
    CHECK(&test, self_check(27) == 0);
    CHECK(&test, self_check(28) == 0);
    file_test_visibility(&test);
    file_test_allocation(&test);
    file_test_viewport(&test);
    file_test_queued(&test, 0);
    file_test_queued(&test, 1);
    CHECK(&test, unlink(test.path) == 0 && rmdir(test.dir) == 0);
    printf("file_kill_test: %s visibility/cancellation; power-loss barriers unverified\n", test.failures ? "FAIL" : "PASS");
    return test.failures ? 1 : 0;
}
