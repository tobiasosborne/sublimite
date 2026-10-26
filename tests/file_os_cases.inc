/* Shared Linux/Windows migration regressions; no editor or native API. */
#include "os/os.h"
#include <stdatomic.h>

typedef struct file_os_messages { int seen; file_msg terminal; } file_os_messages;
static void file_os_receive(const work_msg *wm, void *ctx)
{
    file_os_messages *messages = ctx;
    file_msg m;
    if (!file_msg_decode(wm, &m) &&
        (m.kind == FILE_MSG_OPEN_READY || m.kind == FILE_MSG_OPEN_FAILED)) {
        messages->terminal = m; messages->seen = 1;
    }
}
static int file_os_terminal(work_pool *workers, file_msg *m)
{
    /* (E) Same allowance as the pre-existing file_test wait_msg:
     * 5000 attempts * 1 ms = 5 s. (P) 1 ms = 1000000 ns. */
    uint64_t until = os_now_ns() + UINT64_C(5000) * UINT64_C(1000000);
    file_os_messages messages = {0};
    do {
        (void)work_mailbox_drain(workers, file_os_receive, &messages);
        if (messages.seen) { *m = messages.terminal; return 1; }
        os_sleep_ns(1000000);
    } while (os_now_ns() < until);
    return 0;
}
typedef struct file_os_shrink { const char *path; int called; os_err result; } file_os_shrink;
static void file_os_before_map(void *ctx)
{
    file_os_shrink *hook = ctx;
    os_file source = OS_FILE_INIT;
    hook->called++;
    hook->result = os_file_open(&source, hook->path, OS_OPEN_RDWR);
    if (hook->result == OS_OK) hook->result = os_file_set_size(&source, 0);
    os_file_close(&source);
}
static void file_os_migration_cases(work_pool *workers, const char *root)
{
    file *f = NULL;
    file_msg m = {0};
    CHECK(file_open_begin(workers, root, NULL, &f) == FILE_OK);
    CHECK(file_os_terminal(workers, &m) && m.kind == FILE_MSG_OPEN_FAILED && m.status == FILE_ERR_NOTREG);
    file_close(f);
    puts("file_test: directory -> FILE_ERR_NOTREG: ok");

    char path[OS_PATH_MAX];
    CHECK(snprintf(path, sizeof path, "%s/shrink-before-map", root) > 0);
    os_file source = OS_FILE_INIT;
    CHECK(os_file_open(&source, path, OS_OPEN_RDWR | OS_OPEN_CREATE | OS_OPEN_EXCL) == OS_OK);
    /* (P) Two pages ensure a page beyond the shrunken EOF exists. */
    CHECK(os_file_set_size(&source, 2u * os_page_size()) == OS_OK);
    os_file_close(&source);
    file_os_shrink hook = {path, 0, OS_E_IO};
    file_open_opts opts = {1, 0};
    CHECK(file_test_open_begin(workers, path, &opts, file_os_before_map, &hook, &f) == FILE_OK);
    CHECK(file_os_terminal(workers, &m) && m.kind == FILE_MSG_OPEN_FAILED && m.status == FILE_ERR_CHANGED);
    file_close(f);
    CHECK(hook.called == 1 && hook.result == OS_OK);
    CHECK(os_unlink(path) == OS_OK);
    puts("file_test: shrink between identity validation and map -> FILE_ERR_CHANGED: ok");
}

/* Portable safety assertions, shared by the actual Linux and Windows file
 * implementations. POSIX signal delivery and xattrs remain extra Linux cases. */
static void file_os_write(const char *path, const uint8_t *p, size_t n)
{
    os_file fd = OS_FILE_INIT; CHECK(os_file_open(&fd, path, OS_OPEN_RDWR | OS_OPEN_CREATE | OS_OPEN_TRUNC) == OS_OK);
    size_t off = 0;
    while (off < n) { size_t got = 0; CHECK(os_file_write(&fd, p + off, n - off, &got) == OS_OK && got); if (!got) break; off += got; }
    os_file_close(&fd);
}
static void file_os_equals(const char *path, const uint8_t *p, size_t n)
{
    os_file fd = OS_FILE_INIT; os_stat st; CHECK(os_file_open(&fd, path, OS_OPEN_READ) == OS_OK);
    CHECK(os_file_stat(&fd, &st) == OS_OK && st.size == n);
    uint8_t *bytes = malloc(n ? n : 1); size_t got = 0; CHECK(bytes);
    CHECK(os_file_pread(&fd, bytes, n, 0, &got) == OS_OK && got == n && !memcmp(bytes, p, n));
    free(bytes); os_file_close(&fd);
}
static void file_os_no_temps(const char *root)
{
    os_dir d = OS_DIR_INIT; os_dir_iter it; const char *name;
    CHECK(os_dir_open(&d, root) == OS_OK && os_dir_iter_open(&it, &d) == OS_OK);
    for (;;) { CHECK(os_dir_next(&it, &name) == OS_OK); if (!name) break; CHECK(!strstr(name, ".edit-") && strncmp(name, ".edit-", 6)); }
    os_dir_iter_close(&it); os_dir_close(&d);
}
typedef struct file_os_saved { int seen; file_msg message; } file_os_saved;
static void file_os_save_receive(const work_msg *wm, void *ctx)
{
    file_os_saved *saved = ctx; file_msg m;
    if (!file_msg_decode(wm, &m) && m.kind == FILE_MSG_SAVE_DONE) { saved->seen = 1; saved->message = m; }
}
static file_msg file_os_saved_wait(work_pool *workers, file *f)
{
    file_os_saved saved = {0};
    uint64_t end = os_now_ns() + UINT64_C(5000) * UINT64_C(1000000); /* existing wait_msg allowance (E) */
    do {
        (void)file_save_busy(f); (void)work_mailbox_drain(workers, file_os_save_receive, &saved);
        if (saved.seen && !file_save_busy(f)) return saved.message;
        os_sleep_ns(1000000);
    } while (os_now_ns() < end);
    CHECK(0); return saved.message;
}
static file *file_os_open(work_pool *workers, const char *path, uint64_t threshold, piece_tree **tree)
{
    file *f = NULL; file_open_opts opts = {threshold, 1}; file_msg m = {0};
    CHECK(file_open_begin(workers, path, &opts, &f) == FILE_OK);
    CHECK(file_os_terminal(workers, &m) && m.status == FILE_OK);
    piece_allocator a = piece_default_allocator(); *tree = piece_create(&a); CHECK(*tree && file_attach(f, *tree) == FILE_OK);
    return f;
}
static void file_os_repeated_save(work_pool *workers, const char *root)
{
    char path[OS_PATH_MAX]; CHECK(snprintf(path, sizeof path, "%s/shared-repeat", root) > 0);
    uint8_t old[16384]; memset(old, 'x', sizeof old); old[8192] = '\n'; /* existing retained-generation fixture */
    for (unsigned mapped = 0; mapped < 2; mapped++) {
        file_os_write(path, old, sizeof old); piece_tree *t = NULL;
        file *f = file_os_open(workers, path, mapped ? 1 : UINT64_MAX, &t);
        piece_snapshot *retained = piece_snapshot_take(t); CHECK(retained);
        for (unsigned save = 0; save < 2; save++) {
            CHECK(piece_insert(t, 0, (const uint8_t *)"!", 1) == PIECE_OK);
            CHECK(file_save_begin(f, t, 0, save) == FILE_OK);
            file_msg m = file_os_saved_wait(workers, f); CHECK(m.status == FILE_OK);
            CHECK(file_check(f, NULL) == 0 && !file_changed(f));
        }
        CHECK(file_watch_start(f) >= 0);
        edit_malloc_guard_begin();
        (void)file_save_busy(f); (void)file_open_ready(f); (void)file_prefix_ready(f); (void)file_changed(f);
        file_watch_dispatch();
        size_t allocations = edit_malloc_guard_end(); CHECK(!allocations);
        file_close(f); piece_destroy(t);
        uint8_t got[sizeof old]; CHECK(piece_snapshot_read(retained, 0, got, sizeof got) == PIECE_OK && !memcmp(got, old, sizeof old));
        piece_snapshot_release(retained); CHECK(os_unlink(path) == OS_OK); file_os_no_temps(root);
    }
    puts("file_test: shared COPY/MMAP repeated saves, own identity, retained original, noalloc status/dispatch: ok");
}
typedef struct file_os_fault { const uint8_t *address; int step, fired; } file_os_fault;
static void file_os_late_fault(void *ctx, int step)
{
    file_os_fault *fault = ctx;
    if (step == fault->step) { fault->fired++; CHECK(file_test_recover(fault->address) == 1); }
}
static void file_os_fault_save(work_pool *workers, const char *root)
{
    char path[OS_PATH_MAX]; CHECK(snprintf(path, sizeof path, "%s/shared-fault", root) > 0);
    size_t n = 4u * os_page_size(); /* distinct pages for two fault epochs and untouched prefix */
    uint8_t *old = malloc(n); CHECK(old); memset(old, 'x', n); file_os_write(path, old, n);
    piece_tree *t = NULL; file *f = file_os_open(workers, path, 1, &t);
    piece_snapshot *retained = piece_snapshot_take(t); const uint8_t *bytes = NULL; size_t len;
    CHECK(file_snapshot_bytes(retained, NULL, &bytes, &len) && len == n);
    /* The original backing survives the editor's first successful replace. */
    CHECK(file_save_begin(f, t, 0, 0) == FILE_OK);
    file_msg m = file_os_saved_wait(workers, f); CHECK(m.status == FILE_OK);
    file_os_fault fault = {bytes + 3u * os_page_size(), FILE_STEP_FSYNCED, 0};
    file_set_step_hook(f, file_os_late_fault, &fault);
    CHECK(file_save_begin(f, t, 0, 1) == FILE_OK);
    m = file_os_saved_wait(workers, f); CHECK(m.status == FILE_ERR_CHANGED && fault.fired == 1);
    file_os_equals(path, old, n); file_os_no_temps(root);
    CHECK(file_resolve_keep(f) == FILE_ERR_CHANGED && file_snapshot_faulted(retained));
    fault.address = bytes + 2u * os_page_size(); fault.fired = 0;
    CHECK(file_save_begin(f, t, FILE_SAVE_FORCE, 2) == FILE_OK);
    m = file_os_saved_wait(workers, f); CHECK(m.status == FILE_ERR_CHANGED && fault.fired == 1);
    file_os_equals(path, old, n); file_os_no_temps(root);
    file_close(f); piece_destroy(t); CHECK(file_snapshot_faulted(retained)); piece_snapshot_release(retained);
    free(old); CHECK(os_unlink(path) == OS_OK);
    puts("file_test: shared late fault, second FORCE fault, compromised Keep, retained backing, cleanup: ok");
}
typedef struct file_os_cancel { int cancelled, at; } file_os_cancel;
static void file_os_cancel_step(void *ctx, int step)
{ file_os_cancel *c = ctx; if (step == c->at) c->cancelled = 1; }
static int file_os_cancelled(void *ctx) { return ((file_os_cancel *)ctx)->cancelled; }
static void file_os_cleanup(work_pool *workers, const char *root)
{
    char path[OS_PATH_MAX]; CHECK(snprintf(path, sizeof path, "%s/shared-cancel", root) > 0);
    /* More than two save chunks makes MID_WRITE precede the final byte. */
    size_t n = FILE_PREFIX_MAX; uint8_t *bytes = malloc(n); CHECK(bytes); memset(bytes, 'N', n);
    file_os_write(path, (const uint8_t *)"old", 3);
    piece_allocator a = piece_default_allocator(); piece_tree *t = piece_create(&a);
    CHECK(piece_init_copy(t, bytes, n) == PIECE_OK); piece_snapshot *snap = piece_snapshot_take(t);
    for (int at = FILE_STEP_TEMP_CREATED; at <= FILE_STEP_FSYNCED; at++) {
        file_os_cancel c = {0, at}; file_save_args args = {.path = path, .snap = snap, .stop = file_os_cancelled,
            .stop_ctx = &c, .step = file_os_cancel_step, .step_ctx = &c};
        CHECK(file_save_write(&args) == FILE_ERR_CANCELLED);
        file_os_equals(path, (const uint8_t *)"old", 3); file_os_no_temps(root);
    }
    piece_snapshot_release(snap); piece_destroy(t); free(bytes);
    /* Close with acquisition results still reserved, then drain stale mail. */
    file *f = NULL; CHECK(file_open_begin(workers, path, NULL, &f) == FILE_OK); file_close(f);
    file_os_messages ignored = {0}; (void)work_mailbox_drain(workers, file_os_receive, &ignored);
    CHECK(file_open_begin(workers, path, NULL, &f) == FILE_OK); file_msg m;
    CHECK(file_os_terminal(workers, &m) && m.status == FILE_OK); file_close(f);
    CHECK(os_unlink(path) == OS_OK);
    puts("file_test: shared multichunk cancellation/cleanup, undrained acquisition close: ok");
}
typedef struct file_os_pause {
    _Atomic int entered, go;
    work_pool *workers;
    file *owner;
    work_slot *slot;
    uint32_t epoch;
} file_os_pause;
static void file_os_pause_map(void *ctx)
{
    file_os_pause *pause = ctx;
    atomic_store(&pause->entered, 1);
    while (!atomic_load(&pause->go)) os_yield();
}
static void file_os_pause_save(void *ctx, int step)
{ if (step == FILE_STEP_TEMP_CREATED) file_os_pause_map(ctx); }
static void file_os_release_cancel(void *ctx)
{
    file_os_pause *pause = ctx;
    while (atomic_load(&pause->slot->epoch) == pause->epoch) os_yield();
    atomic_store(&pause->go, 1);
}
static void file_os_cancel_inflight(work_pool *workers, const char *root)
{
    char path[OS_PATH_MAX]; CHECK(snprintf(path, sizeof path, "%s/shared-inflight", root) > 0);
    size_t n = 4u * os_page_size(); uint8_t *bytes = malloc(n); CHECK(bytes); memset(bytes, 'O', n);
    for (int saving = 0; saving < 2; saving++) {
        file_os_write(path, bytes, n);
        file_os_pause pause = {.workers = workers}; file *f = NULL; piece_tree *t = NULL;
        if (!saving) {
            file_open_opts opts = {1, 1};
            CHECK(file_test_open_begin(workers, path, &opts, file_os_pause_map, &pause, &f) == FILE_OK);
        } else {
            f = file_os_open(workers, path, UINT64_MAX, &t);
            file_set_step_hook(f, file_os_pause_save, &pause); CHECK(file_save_begin(f, t, 0, 2) == FILE_OK);
        }
        pause.owner = f;
        uint64_t end = os_now_ns() + UINT64_C(5000) * UINT64_C(1000000);
        while (!atomic_load(&pause.entered) && os_now_ns() < end) {
            file_os_messages ignored = {0}; (void)work_mailbox_drain(workers, file_os_receive, &ignored); os_yield();
        }
        CHECK(atomic_load(&pause.entered));
        for (size_t i = 0; i < WORK_MAX_JOBS; i++) if (workers->slots[i].job.arg == f && atomic_load(&workers->slots[i].busy)) {
            pause.slot = &workers->slots[i]; pause.epoch = atomic_load(&pause.slot->epoch); break;
        }
        CHECK(pause.slot);
        if (pause.slot) {
            os_thread release; CHECK(os_thread_start(&release, file_os_release_cancel, &pause) == OS_OK);
            file_close(f); CHECK(os_thread_join(&release) == OS_OK);
        } else { atomic_store(&pause.go, 1); file_close(f); }
        if (t) piece_destroy(t);
        file_os_messages ignored = {0}; (void)work_mailbox_drain(workers, file_os_receive, &ignored);
        file_os_equals(path, bytes, n); file_os_no_temps(root);
    }
    free(bytes); CHECK(os_unlink(path) == OS_OK);
    puts("file_test: shared in-flight acquisition/save cancellation and physical close: ok");
}
static void file_os_round2_cases(work_pool *workers, const char *root)
{
    os_stat st = {0}; st.kind = OS_KIND_FILE; st.id.volume = 1; st.id.index[0] = 2; st.id.index[1] = 3;
    st.mtime_ns = -1; st.ctime_ns = -2;
    file_id a, b; file_id_from_stat(&a, &st); b = a;
    CHECK(a.mtime_ns == UINT64_MAX && a.ctime_ns == UINT64_MAX - 1u);
    b.ino_hi++; CHECK(file_id_diff(&a, &b) & FILE_CHG_INODE); b = a; b.ino++; CHECK(file_id_diff(&a, &b) & FILE_CHG_INODE);
    file_os_repeated_save(workers, root); file_os_fault_save(workers, root); file_os_cleanup(workers, root); file_os_cancel_inflight(workers, root);
}
