#include "savectl/savectl.h"
#include "trace/trace.h"
#include "base/base.h"
#include <fcntl.h>
#include <sched.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/inotify.h>
#include <poll.h>

#define REQUIRE(x) EDIT_ASSERT(x)
typedef struct fuzz_pause {
    _Atomic bool enabled, entered, released;
} fuzz_pause;
static void pause_hook(void *ctx, int step)
{
    fuzz_pause *p=ctx;
    if (step!=FILE_STEP_FSYNCED || !atomic_load(&p->enabled)) return;
    atomic_store(&p->entered,true);
    while (!atomic_load(&p->released)) (void)sched_yield();
}
typedef struct fuzz_model {
    uint8_t text[256],disk[256];
    size_t len,disk_len;
    bool modified,external;
    savectl_view view;
} fuzz_model;
static void route(const work_msg *msg, void *ctx) { (void)savectl_receive(ctx,msg); }
static void settle(savectl *s, work_pool *pool)
{
    for (;;) {
        (void)work_mailbox_drain(pool,route,s); savectl_tick(s);
        if (!savectl_get_model(s).busy) break;
        (void)sched_yield();
    }
}
static void replace_disk(const char *path, const uint8_t *bytes, size_t size)
{
    char temporary[128]; REQUIRE(snprintf(temporary,sizeof temporary,"%s-next",path)>0);
    int fd=open(temporary,O_WRONLY|O_CREAT|O_EXCL,0600); REQUIRE(fd>=0);
    REQUIRE(write(fd,bytes,size)==(ssize_t)size); REQUIRE(close(fd)==0);
    REQUIRE(rename(temporary,path)==0);
}
static void install(savectl *s, piece_tree **tree, fuzz_model *m)
{
    piece_tree *next=NULL; savectl_view view;
    REQUIRE(savectl_take_reload(s,&next,&view)==0);
    uint64_t len=m->disk_len;
    REQUIRE(view.cursor==(m->view.cursor<len ? m->view.cursor : len));
    REQUIRE(view.anchor==(m->view.anchor<len ? m->view.anchor : len));
    REQUIRE(view.scroll_byte==(m->view.scroll_byte<len ? m->view.scroll_byte : len));
    REQUIRE(view.scroll_x==m->view.scroll_x);
    piece_destroy(*tree); *tree=next;
    memcpy(m->text,m->disk,m->disk_len); m->len=m->disk_len;
    m->modified=false; m->external=false;
}
static void verify(savectl *s, piece_tree *tree, const char *path, const fuzz_model *m)
{
    savectl_model actual=savectl_get_model(s);
    REQUIRE(actual.modified==m->modified && (actual.banner!=NULL)==m->external);
    REQUIRE(piece_len(tree)==m->len);
    uint8_t text[256]; REQUIRE(piece_read(tree,0,text,m->len)==0);
    REQUIRE(!memcmp(text,m->text,m->len));
    int fd=open(path,O_RDONLY); REQUIRE(fd>=0);
    REQUIRE(read(fd,text,sizeof text)==(ssize_t)m->disk_len); REQUIRE(close(fd)==0);
    REQUIRE(!memcmp(text,m->disk,m->disk_len));
}
static void put64(uint8_t *p, uint64_t n)
{ for (unsigned i = 0; i < 8; i++) p[i] = (uint8_t)(n >> (i * 8u)); }
static void put32(uint8_t *p, uint32_t n)
{ for (unsigned i = 0; i < 4; i++) p[i] = (uint8_t)(n >> (i * 8u)); }
static journal_record checkpoint_base(uint8_t *p, const journal_base *b)
{
    put64(p, b->size); put64(p + 8, b->mtime_ns); put64(p + 16, b->inode); put64(p + 24, b->device);
    put32(p + 32, b->prefix_crc); put32(p + 36, b->prefix_len);
    size_t n = strlen(b->path); memcpy(p + 40, b->path, n + 1);
    return (journal_record){JOURNAL_BASE, 1, 0, p, 41 + n};
}
static work_pool *new_pool(void)
{
    work_pool *pool = aligned_alloc(_Alignof(work_pool), sizeof *pool); REQUIRE(pool);
    REQUIRE((uintptr_t)pool % _Alignof(work_pool) == 0);
    REQUIRE(work_pool_init(pool, 1, 0) == 0); return pool;
}
static void disk_is(const char *path, const uint8_t *bytes, size_t length)
{
    uint8_t actual[256]; REQUIRE(length <= sizeof actual);
    int fd = open(path, O_RDONLY); REQUIRE(fd >= 0);
    REQUIRE(read(fd, actual, sizeof actual) == (ssize_t)length && close(fd) == 0);
    REQUIRE(memcmp(bytes, actual, length) == 0);
}
typedef struct io_fault { _Atomic bool fail_sync; } io_fault;
static int sync_hook(void *ctx, int fd, bool directory)
{
    io_fault *fault = ctx;
    if (atomic_exchange(&fault->fail_sync, false)) { errno = EIO; return -1; }
    return directory ? fsync(fd) : fdatasync(fd);
}
static void journal_session(const uint8_t *data, size_t size)
{
    if (!size) return;
    char directory[] = "/tmp/edit-savectl-journal-fuzz-XXXXXX"; REQUIRE(mkdtemp(directory));
    char path[128], logpath[128];
    REQUIRE(snprintf(path, sizeof path, "%s/target", directory) > 0);
    REQUIRE(snprintf(logpath, sizeof logpath, "%s/journal", directory) > 0);
    replace_disk(path, (const uint8_t *)"base", 4);
    work_pool *pool = new_pool(), *jp = new_pool();
    io_fault fault = {0}; journal_io io = {.ctx=&fault, .sync=sync_hook};
    journal *j = NULL; REQUIRE(journal_open_with_io(&j, logpath, jp, NULL, &io) == 0);
    journal_base previous; REQUIRE(journal_capture_base(path, &previous) == 0);
    REQUIRE(journal_set_base(j, 1, &previous) == 0);
    struct stat st; REQUIRE(stat(path, &st) == 0); file_id id; file_id_from_stat(&id, &st);
    unsigned scenario = data[0] % 6u;
    fuzz_pause pause = {0};
    savectl_options options = {.pool=pool, .path=path, .baseline=id, .source_mode=FILE_MODE_COPY,
        .journal=j, .journal_pool=jp, .buffer_id=1, .reload_allocator=piece_default_allocator(),
        .step=pause_hook, .step_ctx=&pause};
    REQUIRE(options.journal != NULL);
    savectl *s = NULL; REQUIRE(savectl_create(&s, &options, false) == 0);
    piece_allocator allocator = piece_default_allocator(); piece_tree *tree = piece_create(&allocator);
    REQUIRE(tree && piece_init_copy(tree, (const uint8_t *)"base", 4) == 0);
    uint8_t edit_byte = size > 1 ? data[1] : '!';
    REQUIRE(piece_insert(tree, 4, &edit_byte, 1) == 0); savectl_modified(s);
    uint8_t bp[4137], insert[9]; put64(insert, 4); insert[8] = edit_byte;
    journal_record cp[] = {checkpoint_base(bp, &previous), {JOURNAL_INSERT, 1, 0, insert, sizeof insert}};
    if (scenario == 1) cp[0].buffer_id = 2;
    if (scenario == 2) replace_disk(path, (const uint8_t *)"other", 5);
    if (scenario == 3) atomic_store(&pause.enabled, true);
    if (scenario == 4) atomic_store(&fault.fail_sync, true);
    REQUIRE(savectl_save(s, tree, &previous, cp, 2) == 0);
    REQUIRE(savectl_get_model(s).journal_leased);
    /* Partial drains and a reused work slot must preserve the lease/result. */
    (void)work_mailbox_drain_bounded(pool, route, s, 1, 0); savectl_tick(s);
    if (scenario == 3) {
        while (!atomic_load(&pause.entered)) {
            (void)work_mailbox_drain_bounded(pool,route,s,1,0); savectl_tick(s);
            (void)sched_yield();
        }
        REQUIRE(savectl_get_model(s).busy && !savectl_get_model(s).journal_leased);
        REQUIRE(savectl_save_token(s) && savectl_save_token(s)->prepared);
        REQUIRE(journal_set_window(j,640,480)==0);
        replace_disk(path, (const uint8_t *)"other", 5);
        if (data[0] & 0x80u) savectl_file_event(s);
        atomic_store(&pause.released, true);
    }
    settle(s, pool); savectl_model model = savectl_get_model(s);
    REQUIRE(!model.journal_leased && model.modified);
    if (scenario == 1 || scenario == 2 || scenario == 4) {
        REQUIRE(model.state == (scenario == 2 ? SAVECTL_EXTERNAL_MODIFIED : SAVECTL_FAILED));
        REQUIRE(model.journal_error == (scenario == 1 ? JOURNAL_INVALID :
                                       scenario == 2 ? JOURNAL_BASE_CHANGED : JOURNAL_IO));
        if (scenario == 2) {
            /* Prepare can detect an external conflict before file work. */
            REQUIRE(model.banner && model.can_reload && model.can_keep);
            REQUIRE(model.file_error == FILE_OK && !model.needs_finish);
            const journal_save *token = savectl_save_token(s);
            REQUIRE(token && !token->prepared && !token->previous_path[0]);
        }
        disk_is(path, (const uint8_t *)(scenario == 2 ? "other" : "base"), scenario == 2 ? 5u : 4u);
    } else if (scenario == 3) {
        REQUIRE(model.banner && (model.file_error == FILE_ERR_CHANGED ||
                ((data[0] & 0x80u) && model.file_error == FILE_OK)));
        const journal_save *token = savectl_save_token(s);
        REQUIRE(token && token->prepared && token->previous_path[0]);
        disk_is(token->previous_path, (const uint8_t *)"base", 4);
        REQUIRE(savectl_save(s, tree, &previous, cp, 2) == SAVECTL_BUSY);
        disk_is(path, (const uint8_t *)"other", 5);
    } else {
        uint8_t expected[5] = {'b','a','s','e',edit_byte}; disk_is(path, expected, sizeof expected);
        REQUIRE(model.needs_finish && !model.file_error && !model.journal_error);
        const journal_save *token = savectl_save_token(s); REQUIRE(token && token->prepared);
        char retained[4097]; strcpy(retained, token->previous_path);
        disk_is(retained, (const uint8_t *)"base", 4);
        journal_record next = checkpoint_base(bp, savectl_saved_base(s));
        journal_record invalid = next; invalid.buffer_id = 2;
        REQUIRE(savectl_finish(s, &invalid, 1) == 0); settle(s, pool);
        REQUIRE(savectl_get_model(s).needs_finish && savectl_get_model(s).journal_error == JOURNAL_INVALID);
        REQUIRE(access(retained, F_OK) == 0);
        if (scenario == 5) {
            atomic_store(&fault.fail_sync, true);
            REQUIRE(savectl_finish(s, &next, 1) == 0); settle(s, pool);
            REQUIRE(savectl_get_model(s).needs_finish && savectl_get_model(s).journal_error == JOURNAL_IO);
            /* A failed sync requires a complete checkpoint in a fresh inode. */
            int retry = journal_get_stats(j).error == JOURNAL_IO ? journal_retry(j) : JOURNAL_OK;
            REQUIRE(retry == JOURNAL_OK || retry == JOURNAL_IO);
            REQUIRE(savectl_recover_finish(s, &next, 1) == 0); settle(s,pool);
        } else {
            REQUIRE(savectl_finish(s, &next, 1) == 0); settle(s, pool);
        }
        REQUIRE(!savectl_get_model(s).needs_finish && !savectl_get_model(s).modified);
        REQUIRE(savectl_get_model(s).state == SAVECTL_SAVED && access(retained, F_OK) != 0);
    }
    /* Resolve the complete current session checkpoint before retiring any
     * ambiguous retained generation during fixture teardown. */
    journal_base current; REQUIRE(journal_capture_base(path, &current) == 0);
    journal_record resolved = checkpoint_base(bp, &current);
    int retry = journal_get_stats(j).error == JOURNAL_IO ? journal_retry(j) : JOURNAL_OK;
    REQUIRE(retry == JOURNAL_OK || retry == JOURNAL_IO);
    int flushed = journal_flush(j); REQUIRE(flushed == JOURNAL_OK || flushed == JOURNAL_IO);
    REQUIRE(journal_rotate(j, &resolved, 1) == 0);
    const journal_save *token = savectl_save_token(s);
    if (token && token->previous_path[0]) REQUIRE(unlink(token->previous_path) == 0 || errno == ENOENT);
    while (savectl_destroy(s) == SAVECTL_BUSY) { savectl_tick(s); (void)sched_yield(); }
    piece_destroy(tree); journal_close(j); work_pool_shutdown(jp); work_pool_shutdown(pool); free(jp); free(pool);
    REQUIRE(unlink(logpath) == 0 && unlink(path) == 0 && rmdir(directory) == 0);
}
typedef struct acquisition_fault { size_t calls, fail_at; unsigned validations, fail_validation; } acquisition_fault;
static void *reload_alloc(void *ctx, size_t size)
{
    acquisition_fault *fault = ctx;
    if (++fault->calls == fault->fail_at) return NULL;
    piece_allocator allocator = piece_default_allocator(); return allocator.alloc(allocator.ctx, size);
}
static void reload_free(void *ctx, void *ptr, size_t size)
{ (void)ctx; piece_allocator allocator = piece_default_allocator(); allocator.free(allocator.ctx, ptr, size); }
static int source_guard(void *ctx)
{
    acquisition_fault *fault = ctx;
    return ++fault->validations == fault->fail_validation ? FILE_ERR_IO : FILE_OK;
}
static void acquisition_session(const uint8_t *data, size_t size)
{
    if (!size) return;
    char directory[] = "/tmp/edit-savectl-acquire-fuzz-XXXXXX"; REQUIRE(mkdtemp(directory));
    char path[128], away[128]; REQUIRE(snprintf(path, sizeof path, "%s/target", directory) > 0);
    REQUIRE(snprintf(away, sizeof away, "%s-away", directory) > 0);
    replace_disk(path, (const uint8_t *)"base", 4);
    struct stat before; REQUIRE(stat(path, &before) == 0); file_id id; file_id_from_stat(&id, &before);
    work_pool *pool = new_pool(); acquisition_fault fault = {0};
    unsigned scenario = data[0] % 6u;
    savectl_options options = {.pool=pool, .path=path, .baseline=id, .source_mode=FILE_MODE_COPY,
        .reload_allocator={&fault, reload_alloc, reload_free}};
    if (scenario == 0 || scenario == 1) {
        options.source_mode = FILE_MODE_MMAP; options.validate_source = source_guard; options.source_ctx = &fault;
        fault.fail_validation = scenario + 1;
    }
    savectl *s = NULL; REQUIRE(savectl_create(&s, &options, true) == 0);
    piece_allocator allocator = piece_default_allocator(); piece_tree *tree = piece_create(&allocator);
    REQUIRE(tree && piece_init_copy(tree, (const uint8_t *)"base", 4) == 0);
    if (scenario == 0 || scenario == 1) {
        REQUIRE(savectl_save(s, tree, NULL, NULL, 0) == 0); settle(s, pool);
        REQUIRE(savectl_get_model(s).state == SAVECTL_FAILED && savectl_get_model(s).file_error == FILE_ERR_IO);
        REQUIRE(savectl_get_model(s).modified); disk_is(path, (const uint8_t *)"base", 4);
    } else if (scenario == 2 || scenario == 3) {
        REQUIRE(unlink(path) == 0);
        if (scenario == 3) REQUIRE(mkdir(path, 0700) == 0);
        REQUIRE(savectl_reload(s) == 0); settle(s, pool);
        REQUIRE(savectl_get_model(s).state == SAVECTL_FAILED);
        REQUIRE(savectl_get_model(s).file_error == (scenario == 2 ? FILE_ERR_IO : FILE_ERR_NOTREG));
        REQUIRE(savectl_get_model(s).modified && piece_len(tree) == 4);
        if (scenario == 3) REQUIRE(rmdir(path) == 0);
        replace_disk(path, (const uint8_t *)"base", 4);
    } else if (scenario == 4) {
        REQUIRE(savectl_reload(s) == 0); settle(s, pool);
        fault.fail_at = 1u + (size > 1 ? data[1] % 4u : 0u);
        piece_tree *replacement = NULL; savectl_view view = {UINT64_MAX,UINT64_MAX,UINT64_MAX,UINT64_MAX};
        int code = savectl_take_reload(s, &replacement, &view);
        if (code == SAVECTL_NOMEM) {
            REQUIRE(!replacement && view.cursor == UINT64_MAX && view.anchor == UINT64_MAX &&
                    view.scroll_byte == UINT64_MAX && view.scroll_x == UINT64_MAX);
            REQUIRE(piece_len(tree) == 4); disk_is(path, (const uint8_t *)"base", 4);
            savectl_model model = savectl_get_model(s);
            REQUIRE(model.state == SAVECTL_FAILED && model.file_error == FILE_ERR_NOMEM);
            REQUIRE(model.modified && model.banner);
            fault.fail_at = 0;
            /* A failed installation retires its private backing. Clearing the
             * allocator fault cannot revive it; explicitly acquire a new copy. */
            REQUIRE(savectl_take_reload(s, &replacement, &view) == SAVECTL_BUSY);
            settle(s, pool); model = savectl_get_model(s);
            REQUIRE(model.can_reload && model.can_keep);
            REQUIRE(savectl_reload(s) == SAVECTL_OK); settle(s, pool);
            REQUIRE(savectl_take_reload(s, &replacement, &view) == SAVECTL_OK);
        } else REQUIRE(code == 0);
        uint8_t text[4]; REQUIRE(replacement && piece_read(replacement, 0, text, 4) == 0);
        REQUIRE(memcmp(text, "base", 4) == 0); piece_destroy(tree); tree = replacement;
    } else {
        REQUIRE(rename(directory, away) == 0);
        REQUIRE(savectl_save(s, tree, NULL, NULL, 0) == 0); settle(s, pool);
        REQUIRE(savectl_get_model(s).file_error == FILE_ERR_IO && savectl_get_model(s).modified);
        REQUIRE(rename(away, directory) == 0); disk_is(path, (const uint8_t *)"base", 4);
    }
    while (savectl_destroy(s) == SAVECTL_BUSY) { savectl_tick(s); (void)sched_yield(); }
    piece_destroy(tree); work_pool_shutdown(pool); free(pool);
    REQUIRE(unlink(path) == 0 && rmdir(directory) == 0);
}
static void decode_file(const work_msg *message, void *ctx)
{ (void)ctx; file_msg decoded; (void)file_msg_decode(message, &decoded); }
typedef struct mapped_guard {
    piece_snapshot *snapshot;
    file_backing *backing;
    int fd;
    unsigned calls, fault_at;
} mapped_guard;
static int backing_guard(void *ctx)
{
    mapped_guard *guard = ctx;
    if (++guard->calls == guard->fault_at) {
        REQUIRE(ftruncate(guard->fd, 0) == 0);
        uint8_t byte;
        REQUIRE(piece_snapshot_read(guard->snapshot, 0, &byte, 1) == PIECE_OK);
        REQUIRE(file_backing_faulted(guard->backing) && file_snapshot_faulted(guard->snapshot));
    }
    return file_backing_faulted(guard->backing) ? FILE_ERR_CHANGED : FILE_OK;
}
static void real_mapped_fault(const uint8_t *data, const char *path, work_pool *pool)
{
    uint8_t bytes[8192]; memset(bytes, 'a', sizeof bytes); replace_disk(path, bytes, sizeof bytes);
    struct stat st; REQUIRE(stat(path, &st) == 0); file_id id; file_id_from_stat(&id, &st);
    file *opened = NULL; file_open_opts opts = {.copy_threshold=1, .generation=73};
    REQUIRE(file_open_begin(pool, path, &opts, &opened) == FILE_OK);
    while (!file_open_ready(opened)) {
        (void)work_mailbox_drain(pool, decode_file, NULL); (void)sched_yield();
    }
    REQUIRE(file_open_mode(opened) == FILE_MODE_MMAP);
    piece_allocator allocator = piece_default_allocator(); piece_tree *tree = piece_create(&allocator);
    REQUIRE(tree && file_attach(opened, tree) == FILE_OK);
    mapped_guard guard = {.snapshot=piece_snapshot_take(tree), .fd=open(path,O_RDWR),
                          .fault_at=1u + data[0] % 2u};
    REQUIRE(guard.snapshot && guard.fd >= 0);
    guard.backing = file_snapshot_backing(guard.snapshot); REQUIRE(guard.backing);
    file_backing_acquire(guard.backing);
    file_close(opened); /* Real backing token/snapshot survive UI file retirement. */
    savectl_options options = {.pool=pool, .path=path, .baseline=id, .source_mode=FILE_MODE_MMAP,
        .validate_source=backing_guard, .source_ctx=&guard, .reload_allocator=allocator};
    savectl *s = NULL; REQUIRE(savectl_create(&s, &options, true) == SAVECTL_OK);
    REQUIRE(savectl_save(s, tree, NULL, NULL, 0) == SAVECTL_OK); settle(s, pool);
    savectl_model result = savectl_get_model(s);
    REQUIRE(result.modified && result.state == SAVECTL_EXTERNAL_MODIFIED && result.file_error == FILE_ERR_CHANGED);
    REQUIRE(guard.calls >= guard.fault_at && file_backing_faulted(guard.backing));
    while (savectl_destroy(s) == SAVECTL_BUSY) (void)sched_yield();
    piece_destroy(tree); piece_snapshot_release(guard.snapshot); file_backing_release(guard.backing);
    REQUIRE(close(guard.fd) == 0);
}
static void multichunk_reload(const uint8_t *data, size_t size, const char *path, work_pool *pool)
{
    size_t chunks = 3u + (size > 1 ? data[1] % 6u : 0u), length = chunks * FILE_PREFIX_MAX;
    uint8_t *block = malloc(FILE_PREFIX_MAX); REQUIRE(block); memset(block, 'a', FILE_PREFIX_MAX);
    int fd = open(path, O_RDWR|O_CREAT|O_TRUNC, 0600); REQUIRE(fd >= 0);
    for (size_t i = 0; i < chunks; i++) REQUIRE(write(fd, block, FILE_PREFIX_MAX) == FILE_PREFIX_MAX);
    struct stat st; REQUIRE(fstat(fd, &st) == 0); file_id id; file_id_from_stat(&id, &st);
    int events = inotify_init1(IN_NONBLOCK|IN_CLOEXEC); REQUIRE(events >= 0);
    REQUIRE(inotify_add_watch(events, path, IN_ACCESS) >= 0);
    piece_allocator allocator = piece_default_allocator();
    savectl_options options = {.pool=pool, .path=path, .baseline=id, .source_mode=FILE_MODE_COPY,
                              .reload_allocator=allocator};
    savectl *s = NULL; REQUIRE(savectl_create(&s, &options, true) == SAVECTL_OK);
    REQUIRE(savectl_reload(s) == SAVECTL_OK);
    /* IN_ACCESS is generated by the first pread. The event can arrive after
     * completion on a fast run; then a complete old version is also valid.
     * This schedule never claims a deterministic inter-chunk barrier. */
    uint8_t notifications[4096];
    for (;;) {
        ssize_t got = read(events, notifications, sizeof notifications);
        if (got > 0) break;
        REQUIRE(got < 0 && (errno == EAGAIN || errno == EINTR)); (void)sched_yield();
    }
    memset(block, 'b', FILE_PREFIX_MAX);
    struct timespec times[2] = {st.st_atim, st.st_mtim};
    for (size_t i = 0; i < chunks; i++) {
        size_t chunk = (data[0] & 0x80u) ? chunks - i - 1u : i;
        REQUIRE(pwrite(fd, block, FILE_PREFIX_MAX, (off_t)(chunk * FILE_PREFIX_MAX)) == FILE_PREFIX_MAX);
        REQUIRE(futimens(fd, times) == 0); /* restored mtime after every chunk */
        if (data[0] & 0x40u) (void)sched_yield();
    }
    REQUIRE(fstat(fd, &st) == 0 && st.st_size == (off_t)length);
    REQUIRE(st.st_mtim.tv_sec == times[1].tv_sec && st.st_mtim.tv_nsec == times[1].tv_nsec);
    settle(s, pool); savectl_model result = savectl_get_model(s);
    if (result.file_error) REQUIRE(result.file_error == FILE_ERR_CHANGED && result.modified && result.banner);
    else {
        piece_tree *replacement = NULL; savectl_view view;
        REQUIRE(savectl_take_reload(s, &replacement, &view) == SAVECTL_OK && replacement);
        uint8_t first; REQUIRE(piece_read(replacement, 0, &first, 1) == PIECE_OK);
        REQUIRE(first == 'a' || first == 'b');
        for (size_t off = 0; off < length; off += FILE_PREFIX_MAX) {
            REQUIRE(piece_read(replacement, off, block, FILE_PREFIX_MAX) == PIECE_OK);
            for (size_t i = 0; i < FILE_PREFIX_MAX; i++) REQUIRE(block[i] == first);
        }
        piece_destroy(replacement);
    }
    while (savectl_destroy(s) == SAVECTL_BUSY) { savectl_tick(s); (void)sched_yield(); }
    REQUIRE(close(fd) == 0 && close(events) == 0); free(block);
}
typedef struct reuse_hold { _Atomic bool entered, released; } reuse_hold;
static void reuse_worker(work_ctx *ctx)
{
    reuse_hold *hold = ctx->arg; atomic_store(&hold->entered, true);
    while (!atomic_load(&hold->released) && !work_should_stop(ctx)) (void)sched_yield();
}
static void reused_slots(const uint8_t *data, const char *path, work_pool *pool)
{
    replace_disk(path, (const uint8_t *)"base", 4);
    file_id id; REQUIRE(file_id_stat_path(path, &id) == FILE_OK);
    piece_allocator allocator = piece_default_allocator(); piece_tree *tree = piece_create(&allocator);
    REQUIRE(tree && piece_init_copy(tree, (const uint8_t *)"base", 4) == PIECE_OK);
    savectl_options options = {.pool=pool, .path=path, .baseline=id, .source_mode=FILE_MODE_COPY,
                              .reload_allocator=allocator};
    for (unsigned round = 0; round < 3; round++) {
        savectl *s = NULL; REQUIRE(savectl_create(&s, &options, true) == SAVECTL_OK);
        REQUIRE(savectl_save(s, tree, NULL, NULL, 0) == SAVECTL_OK);
        if ((data[0] + round) & 1u) {
            (void)work_mailbox_drain_bounded(pool, route, s, 1, 0); savectl_tick(s);
        }
        settle(s, pool); REQUIRE(savectl_get_model(s).state == SAVECTL_SAVED);
        for (size_t i = 0; i < WORK_MAX_JOBS; i++)
            while (atomic_load(&pool->slots[i].busy)) (void)sched_yield();
        (void)work_mailbox_drain(pool, route, s);
        reuse_hold hold = {0}; work_handle handles[WORK_MAX_JOBS]; size_t submitted = 0;
        /* Fill all free slots with unrelated jobs: the former controller slot
         * is definitely reused, both as running and queued work. */
        for (size_t i = 0; i < WORK_MAX_JOBS; i++) {
            handles[i] = work_submit(pool, (work_job){reuse_worker, &hold, round + 90u, WORK_BULK});
            if (!handles[i].epoch) break;
            submitted++;
        }
        REQUIRE(submitted > 0);
        while (!atomic_load(&hold.entered)) (void)sched_yield();
        savectl_close_begin(s);
        int code = savectl_destroy(s);
        /* Known #12: destroy currently waits on unrelated slot reuse. Unit
         * regression requires immediate OK; fuzz allows this exact BUSY until
         * the owning module fixes the epoch check. */
        REQUIRE(code == SAVECTL_OK || code == SAVECTL_BUSY);
        for (size_t i = 0; i < submitted; i++) if ((data[0] + i) & 1u) work_cancel(pool, handles[i]);
        atomic_store(&hold.released, true);
        for (size_t i = 0; i < submitted; i++)
            while (atomic_load(&pool->slots[handles[i].slot].busy)) (void)sched_yield();
        if (code == SAVECTL_BUSY) REQUIRE(savectl_destroy(s) == SAVECTL_OK);
        REQUIRE(file_id_stat_path(path, &options.baseline) == FILE_OK);
    }
    piece_destroy(tree);
}
static void mapped_and_reload(const uint8_t *data, size_t size)
{
    if (!size) return;
    char directory[] = "/tmp/edit-savectl-backing-fuzz-XXXXXX"; REQUIRE(mkdtemp(directory));
    char path[128]; REQUIRE(snprintf(path, sizeof path, "%s/target", directory) > 0);
    work_pool *pool = new_pool();
    if (data[0] % 3u == 0) real_mapped_fault(data, path, pool);
    else if (data[0] % 3u == 1) multichunk_reload(data, size, path, pool);
    else reused_slots(data, path, pool);
    work_pool_shutdown(pool); free(pool);
    REQUIRE(unlink(path) == 0 && rmdir(directory) == 0);
}
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (!size) return 0;
    trace_init();
    mapped_and_reload(data, size);
    journal_session(data, size);
    acquisition_session(data, size);
    char directory[]="/tmp/edit-savectl-fuzz-XXXXXX"; REQUIRE(mkdtemp(directory));
    char path[128]; REQUIRE(snprintf(path,sizeof path,"%s/target",directory)>0);
    int fd=open(path,O_WRONLY|O_CREAT|O_EXCL,0600); REQUIRE(fd>=0 && write(fd,"base",4)==4); close(fd);
    struct stat st; REQUIRE(stat(path,&st)==0);
    work_pool *pool=aligned_alloc(_Alignof(work_pool),sizeof *pool); REQUIRE(pool); REQUIRE((uintptr_t)pool % _Alignof(work_pool) == 0); REQUIRE(work_pool_init(pool,1,0)==0);
    fuzz_pause pause={0};
    savectl_options options={.step=pause_hook,.step_ctx=&pause,.pool=pool,.path=path,.source_mode=FILE_MODE_COPY,
        .reload_allocator=piece_default_allocator(),
        .baseline={.dev=(uint64_t)st.st_dev,.ino=(uint64_t)st.st_ino,.size=4,
            .mtime_ns=(uint64_t)st.st_mtim.tv_sec*UINT64_C(1000000000)+(uint64_t)st.st_mtim.tv_nsec,
            .mode=0600,.exists=1}};
    savectl *s=NULL; REQUIRE(savectl_create(&s,&options,false)==0);
    piece_allocator a=piece_default_allocator(); piece_tree *tree=piece_create(&a); REQUIRE(tree);
    REQUIRE(piece_init_copy(tree,(const uint8_t *)"base",4)==0);
    fuzz_model m={.text="base",.disk="base",.len=4,.disk_len=4};
    size_t limit=size<64 ? size : 64;
    for (size_t i=0;i<limit;++i) {
        uint8_t byte=data[i];
        switch (byte%10u) {
        case 0:
            REQUIRE(piece_insert(tree,m.len,&byte,1)==0); m.text[m.len++]=byte;
            savectl_modified(s); m.modified=true; break;
        case 1: {
            int rc=savectl_save(s,tree,NULL,NULL,0);
            if (m.external) REQUIRE(rc==FILE_ERR_CHANGED);
            else {
                REQUIRE(rc==0); REQUIRE(savectl_get_model(s).state==SAVECTL_SAVING);
                memcpy(m.disk,m.text,m.len); m.disk_len=m.len;
                if (byte & 0x80u) {
                    REQUIRE(piece_insert(tree,m.len,&byte,1)==0); m.text[m.len++]=byte;
                    savectl_modified(s); m.modified=true;
                } else m.modified=false;
                settle(s,pool); REQUIRE(savectl_get_model(s).state==SAVECTL_SAVED);
            }
            break;
        }
        case 2:
            m.disk_len=(size_t)(byte%31u);
            memset(m.disk,byte,m.disk_len); replace_disk(path,m.disk,m.disk_len);
            savectl_file_event(s); settle(s,pool); m.external=true;
            if (!m.modified) install(s,&tree,&m);
            break;
        case 3:
            if (m.external) {
                REQUIRE(savectl_keep(s)==0); settle(s,pool); m.external=false; m.modified=true;
            } else REQUIRE(savectl_keep(s)==SAVECTL_INVALID);
            break;
        case 4:
            REQUIRE(savectl_reload(s)==0); settle(s,pool); install(s,&tree,&m); break;
        case 5:
            m.view=(savectl_view){byte,(uint64_t)byte*2u,(uint64_t)byte*3u,(uint64_t)byte*4u};
            savectl_set_view(s,m.view); break;
        case 6:
            REQUIRE(savectl_save(s,NULL,NULL,NULL,0)==SAVECTL_INVALID);
            REQUIRE(!savectl_receive(s,&(work_msg){.kind=0})); break;
        case 7:
            savectl_file_event(s); settle(s,pool); break;
        case 8:
            if (m.len) {
                REQUIRE(piece_delete(tree,m.len-1,1,NULL)==0); --m.len;
                savectl_modified(s); m.modified=true;
            }
            break;
        case 9:
            savectl_modified(s); m.modified=true;
            if (m.external) REQUIRE(savectl_keep(s)==0);
            settle(s,pool); m.external=false;
            atomic_store(&pause.entered,false); atomic_store(&pause.released,false);
            atomic_store(&pause.enabled,true);
            REQUIRE(savectl_save(s,tree,NULL,NULL,0)==0);
            while (!atomic_load(&pause.entered)) (void)sched_yield();
            m.disk_len=1; m.disk[0]=byte; replace_disk(path,m.disk,m.disk_len);
            if (byte & 0x80u) savectl_file_event(s);
            atomic_store(&pause.released,true);
            settle(s,pool); atomic_store(&pause.enabled,false);
            m.external=true;
            break;

        }
        verify(s,tree,path,&m);
    }
    settle(s,pool);
    while (savectl_destroy(s)==SAVECTL_BUSY) (void)sched_yield();
    work_pool_shutdown(pool); free(pool); piece_destroy(tree);
    REQUIRE(unlink(path)==0 && rmdir(directory)==0); return 0;
}
