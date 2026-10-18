/* (P) Fixture length = FILE_PREFIX_MAX + the tail's non-NUL bytes, forcing
 * COPY acquisition beyond the preview. Geometry = status width and tab/text/
 * status rows. Zero/one literals denote empty/success, Boolean states, first
 * tab, unit increments and string terminators; 0600 is POSIX owner read/write.
 * (E) Watchdogs reuse EDITOR_IDLE_NS, the existing editor loop policy. */
#include "editor/private.h"
#include "trace/trace.h"
#include <dirent.h>
#include <fcntl.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <sys/wait.h>
#include <unistd.h>
#include <xkbcommon/xkbcommon-keysyms.h>

#define T(c) do { if (!(c)) { fprintf(stderr, "editor_checkpoint_test:%d: FAIL %s\n", __LINE__, #c); return 1; } } while (0)

typedef struct held_bulk { atomic_bool entered; } held_bulk;
static void hold_bulk(work_ctx *ctx)
{
    held_bulk *held = ctx->arg;
    uint64_t limit = trace_now_ns() + EDITOR_IDLE_NS;
    atomic_store(&held->entered, true);
    while (!work_should_stop(ctx) && trace_now_ns() < limit) sched_yield();
}
static int settle_until(editor *e, uint64_t limit)
{
    do {
        int rc = editor_step(e, 0);
        if (rc != EDITOR_OK && rc != EDITOR_MORE)
            fprintf(stderr, "editor_checkpoint_test: editor_step rc=%d journal_error=%d operation_error=%d\n",
                rc, e->stats.journal_error, e->buffer->operation_error);
        T(rc == EDITOR_OK || rc == EDITOR_MORE);
        T(trace_now_ns() < limit); sched_yield();
    } while (editor_get_stats(e).pending || editor_get_stats(e).render_active || editor_save_active(e));
    return 0;
}
static int settle(editor *e) { return settle_until(e, trace_now_ns() + EDITOR_IDLE_NS); }
static int cleanup(const char *root)
{
    DIR *directory = opendir(root); T(directory); struct dirent *entry;
    while ((entry = readdir(directory))) if (strcmp(entry->d_name, ".") && strcmp(entry->d_name, ".."))
        T(unlinkat(dirfd(directory), entry->d_name, 0) == 0);
    T(closedir(directory) == 0 && rmdir(root) == 0);
    return 0;
}
/* edit-dr9 (P): one buffer/worker, literal text, and schema path capacity.
 * Inject the existing FULL checkpoint-fallback condition after a durable
 * baseline. A directory inventory must defer close without latching fail().
 * (E) Retry watchdog is the existing EDITOR_IDLE_NS, as in settle(). */
static int busy_close_checkpoint(void)
{
    char root[]="build/editor-checkpoint-busy-XXXXXX"; T(mkdtemp(root));
    char log[IPC_PATH_CAP]; editor *e=NULL; int guard=-1, result=0;
#define C(x) do { if(!(x)) { fprintf(stderr,"editor_checkpoint_test:%d FAIL %s\n",__LINE__,#x); result=1; goto done; } } while(0)
    C(snprintf(log,sizeof log,"%s/session",root)>0);
    render_backend backend={0}; C(render_null_backend(&backend)==0);
    editor_config config={.journal_path=log,.initial=(const uint8_t *)"checkpoint",.initial_len=sizeof "checkpoint"-1u};
    C(editor_open(&e,&config,&backend)==0); C(settle(e)==0 && editor_flush(e)==0);
    journal *original=e->journal; struct stat before,after; C(stat(log,&before)==0);
    guard=open(root,O_RDONLY|O_DIRECTORY|O_CLOEXEC); C(guard>=0 && flock(guard,LOCK_EX|LOCK_NB)==0);
    e->stats.journal_error=JOURNAL_FULL;
    plat_event close_event={.kind=PLAT_EV_CLOSE}; C(editor_inject(e,&close_event)==0);
    int rc=editor_step(e,0); C(rc==EDITOR_MORE || rc==EDITOR_OK);
    C(e->quit && !e->error && e->journal==original && e->stats.journal_error==JOURNAL_FULL);
    C(stat(log,&after)==0 && after.st_ino==before.st_ino && after.st_size==before.st_size);
    uint8_t text[sizeof "checkpoint"-1u];
    C(editor_read(e,0,text,sizeof text)==0 && !memcmp(text,"checkpoint",sizeof text));
    rc=editor_step(e,0); C(rc==EDITOR_MORE || rc==EDITOR_OK); C(!e->error);
    C(close(guard)==0); guard=-1;
    uint64_t limit=trace_now_ns()+EDITOR_IDLE_NS;
    do { rc=editor_step(e,0); C(trace_now_ns()<limit); sched_yield(); } while(rc==EDITOR_OK || rc==EDITOR_MORE);
    C(rc==EDITOR_CLOSED && !e->error && !e->stats.journal_error && e->journal==original);
    C(stat(log,&after)==0 && after.st_ino!=before.st_ino);
done:
    if(guard>=0) close(guard);
    if(e) editor_close(e);
    if(cleanup(root)) result=1;
#undef C
    if(!result) puts("editor_checkpoint_test: directory inventory defers close checkpoint; current text and open journal survive until retry");
    return result;
}
typedef struct checkpoint_shape { uint64_t id; bool base, target, view, replacement; } checkpoint_shape;
static int inspect(void *ctx, const journal_record *r)
{
    checkpoint_shape *shape = ctx;
    if (r->buffer_id != shape->id) return 0;
    if (r->type == JOURNAL_BASE) shape->base = true;
    if (r->type == JOURNAL_TARGET) shape->target = true;
    if (r->type == JOURNAL_VIEW) shape->view = true;
    if (r->type == JOURNAL_DELETE || r->type == JOURNAL_INSERT) shape->replacement = true;
    return 0;
}
static int crash_checkpoint(const char *source, const char *log, size_t len, bool active)
{
    render_backend backend = {0}; T(render_null_backend(&backend) == 0);
    /* Match editor_large_test's null-backend acquisition lane: prefix work
     * needs a raster worker while the full COPY remains held on bulk. */
    backend.info.capabilities |= RENDER_CAP_RASTER_POOL;
    editor_config config = {.journal_path = log, .initial = (const uint8_t *)"other tab",
        .initial_len = sizeof "other tab" - 1, .copy_threshold = len + 1,
        .cols = (uint32_t)strlen("Source changed: R reload / K keep"), .rows = 3};
    editor *e = NULL; T(editor_open(&e, &config, &backend) == 0); T(settle(e) == 0);
    held_bulk held = {0};
    work_handle handle = work_submit(&e->pool, (work_job){hold_bulk, &held, 0, WORK_BULK}); T(handle.epoch);
    uint64_t limit = trace_now_ns() + EDITOR_IDLE_NS;
    while (!atomic_load(&held.entered)) { T(trace_now_ns() < limit); sched_yield(); }
    uint64_t id; T(editor_add_buffer(e, source, NULL, 0, &id) == 0);
    editor_buffer *pending = e->buffer;
    T(pending->pending_open && file_open_mode(pending->file) == FILE_MODE_COPY);
    T(!file_open_ready(pending->file) && piece_len(pending->tree) == 0);
    T(piece_len(pending->preview) == FILE_PREFIX_MAX && pending->base.size == len);
    if (!active) T(editor_select_tab(e, 0) == 0);
    /* Rotate while another tab's full COPY is queued behind the held bulk job.
     * Abrupt exit deliberately skips editor_close and adoption. */
    T(journal_flush(e->journal) == 0);
    T(editor_checkpoint(e) == 0); T(pending->pending_open);
    _exit(0);
}
static int pending_checkpoint(bool active)
{
    char root[] = "build/editor-checkpoint-XXXXXX"; T(mkdtemp(root));
    char source[IPC_PATH_CAP], log[IPC_PATH_CAP];
    T(snprintf(source, sizeof source, "%s/source", root) > 0);
    T(snprintf(log, sizeof log, "%s/session", root) > 0);
    const char tail[] = "\npending tail\n";
    size_t len = FILE_PREFIX_MAX + sizeof tail - 1;
    uint8_t *expected = malloc(len), *actual = malloc(len + 1); T(expected && actual);
    memset(expected, 'x', FILE_PREFIX_MAX); memcpy(expected + FILE_PREFIX_MAX, tail, sizeof tail - 1);
    int fd = open(source, O_WRONLY | O_CREAT | O_EXCL, 0600); T(fd >= 0);
    T(write(fd, expected, len) == (ssize_t)len); T(close(fd) == 0);
    pid_t child = fork(); T(child >= 0);
    if (!child) _exit(crash_checkpoint(source, log, len, active));
    int status; T(waitpid(child, &status, 0) == child);
    T(WIFEXITED(status) && WEXITSTATUS(status) == 0);
    /* (P) Pending ID is the next ID after the first tab's ID. */
    checkpoint_shape shape = {.id = 1 + 1}; journal_replay_result replay;
    T(journal_replay_file(log, inspect, &shape, &replay) == 0 && !replay.corrupt);
    render_backend backend = {0}; T(render_null_backend(&backend) == 0);
    editor_config config = {.journal_path = log, .start_empty = true,
        .cols = (uint32_t)strlen("Source changed: R reload / K keep"), .rows = 3};
    editor *e = NULL; T(editor_open(&e, &config, &backend) == 0); T(settle(e) == 0);
    T(tabs_count(&e->tabs) == shape.id);
    bool order = !strcmp(editor_tab(e, 0)->title, "untitled");
    bool active_preserved = tabs_active_index(&e->tabs) == (active ? 1u : 0u);
    T(editor_select_tab(e, 1) == 0); T(settle(e) == 0);
    uint64_t recovered_len = editor_length(e);
    bool intact = recovered_len == len && editor_read(e, 0, actual, len) == 0 && !memcmp(actual, expected, len);
    bool clean = !editor_tab(e, 1)->modified;
    /* Saving recovered contents must preserve the complete on-disk document. */
    plat_event save = {.kind = PLAT_EV_KEY, .press = true, .keysym = 's', .mods = PLAT_MOD_CTRL};
    T(editor_inject(e, &save) == 0); T(settle(e) == 0); T(!e->buffer->save_error);
    fd = open(source, O_RDONLY); T(fd >= 0);
    ssize_t disk_len = read(fd, actual, len + 1); T(close(fd) == 0);
    bool disk_intact = disk_len == (ssize_t)len && !memcmp(actual, expected, len);
    editor_close(e); free(actual); free(expected);
    T(cleanup(root) == 0);
    printf("editor_checkpoint_test: %s pending COPY recovered=%llu expected=%zu clean=%s disk_intact=%s\n",
        active ? "active" : "inactive", (unsigned long long)recovered_len, len,
        clean ? "yes" : "no", disk_intact ? "yes" : "no");
    T(fflush(stdout) == 0);
    T(intact); T(clean); T(disk_intact); T(active_preserved); T(order);
    T(shape.base && shape.target && shape.view && !shape.replacement);
    return 0;
}
static int adopted_empty_checkpoint(void)
{
    char root[] = "build/editor-checkpoint-empty-XXXXXX"; T(mkdtemp(root));
    char source[IPC_PATH_CAP], log[IPC_PATH_CAP];
    T(snprintf(source, sizeof source, "%s/source", root) > 0);
    T(snprintf(log, sizeof log, "%s/session", root) > 0);
    const char marker[] = "delete me";
    int fd = open(source, O_WRONLY | O_CREAT | O_EXCL, 0600); T(fd >= 0);
    T(write(fd, marker, sizeof marker - 1) == (ssize_t)(sizeof marker - 1)); T(close(fd) == 0);
    pid_t child = fork(); T(child >= 0);
    if (!child) {
        render_backend backend = {0}; T(render_null_backend(&backend) == 0);
        editor_config config = {.path = source, .journal_path = log,
            .cols = (uint32_t)strlen("Source changed: R reload / K keep"), .rows = 3};
        editor *e = NULL; T(editor_open(&e, &config, &backend) == 0); T(settle(e) == 0);
        T(!e->buffer->pending_open);
        plat_event select = {.kind = PLAT_EV_KEY, .press = true, .keysym = 'a', .mods = PLAT_MOD_CTRL};
        T(editor_inject(e, &select) == 0); T(settle(e) == 0);
        plat_event remove = {.kind = PLAT_EV_KEY, .press = true, .keysym = XKB_KEY_BackSpace};
        T(editor_inject(e, &remove) == 0); T(settle(e) == 0);
        T(editor_length(e) == 0 && editor_tab(e, 0)->modified);
        T(journal_flush(e->journal) == 0); T(editor_checkpoint(e) == 0); _exit(0);
    }
    int status; T(waitpid(child, &status, 0) == child);
    T(WIFEXITED(status) && WEXITSTATUS(status) == 0);
    checkpoint_shape shape = {.id = 1}; journal_replay_result replay;
    T(journal_replay_file(log, inspect, &shape, &replay) == 0 && !replay.corrupt);
    render_backend backend = {0}; T(render_null_backend(&backend) == 0);
    editor_config config = {.journal_path = log, .start_empty = true,
        .cols = (uint32_t)strlen("Source changed: R reload / K keep"), .rows = 3};
    editor *e = NULL; T(editor_open(&e, &config, &backend) == 0); T(settle(e) == 0);
    bool empty_modified = editor_length(e) == 0 && editor_tab(e, 0)->modified;
    editor_close(e); T(cleanup(root) == 0);
    T(empty_modified); T(shape.base && shape.target && shape.view && shape.replacement);
    puts("editor_checkpoint_test: adopted deletion to empty survives checkpoint/crash/recovery");
    return 0;
}
/* ---- edit-28u: checkpoints reference BASE instead of copying it ----------
 * (P) Fixture sizes: GIB = 2^30, the brief's 1 GiB sparse file (ftruncate:
 * no data blocks except the marker pages written below). SMALL = 2 *
 * FILE_PREFIX_MAX + a 7-byte tail, a COPY file above FILE_PREFIX_MAX so its
 * modified checkpoints take the referencing form (editor_checkpoint_references).
 * Typed literals are the edits themselves; offsets are derived from them. */
#define GIB (UINT64_C(1) << 30)
#define MID (GIB / 2u)
#define SMALL ((size_t)FILE_PREFIX_MAX * 2u + 7u)
static void stamp(char power[32], char load[32])
{
    strcpy(power, "unknown"); strcpy(load, "unknown");
    FILE *f = fopen("/sys/class/power_supply/BAT0/status", "r");
    if (f) { if (fscanf(f, "%31s", power) != 1) strcpy(power, "unknown"); fclose(f); }
    f = fopen("/proc/loadavg", "r");
    if (f) { if (fscanf(f, "%31s", load) != 1) strcpy(load, "unknown"); fclose(f); }
}
static void remove_tree(const char *root)
{
    DIR *d = opendir(root); struct dirent *entry;
    if (d) {
        while ((entry = readdir(d))) if (strcmp(entry->d_name, ".") && strcmp(entry->d_name, "..")) {
            char sub[IPC_PATH_CAP];
            if (snprintf(sub, sizeof sub, "%s/%s", root, entry->d_name) > 0) {
                struct stat st;
                if (!lstat(sub, &st) && S_ISDIR(st.st_mode)) remove_tree(sub);
                else (void)unlink(sub);
            }
        }
        closedir(d);
    }
    (void)rmdir(root);
}
typedef struct shape28 { uint64_t id, deletes, deleted, inserts, inserted, bases, first_delete_off, first_delete_len; } shape28;
static int shape_record(void *ctx, const journal_record *r)
{
    shape28 *s = ctx;
    if (r->buffer_id != s->id) return 0;
    uint64_t off = 0, len = 0;
    for (unsigned i = 0; r->size >= 16 && i < 8; i++) {
        off |= (uint64_t)r->data[i] << (8u * i); len |= (uint64_t)r->data[8 + i] << (8u * i);
    }
    if (r->type == JOURNAL_BASE) s->bases++;
    if (r->type == JOURNAL_DELETE) {
        if (!s->deletes) { s->first_delete_off = off; s->first_delete_len = len; }
        s->deletes++; s->deleted += len;
    }
    if (r->type == JOURNAL_INSERT) { s->inserts++; s->inserted += r->size - 8; }
    return 0;
}
static int shape_of(const char *log, uint64_t id, shape28 *out)
{
    *out = (shape28){.id = id}; journal_replay_result result;
    T(journal_scan_file(log, shape_record, out, &result) == 0 && !result.corrupt);
    return 0;
}
/* (P) wire sizes of one named buffer's metadata checkpoint, each record
 * JOURNAL_HEADER + payload: BASE v2 = 40 + path + NUL + ctime 8; TARGET =
 * path + NUL; VIEW = JOURNAL_VIEW_STATE_BYTES; TABS = 16 + 8 per tab; WINDOW =
 * JOURNAL_WINDOW_BYTES. The file seals with a PAD to a JOURNAL_PAGE multiple. */
static uint64_t metadata_bound_with(const char *path, size_t tabs, uint64_t extra)
{
    uint64_t n = strlen(path) + 1, bytes = extra;
    bytes += JOURNAL_HEADER + 40 + n + 8; bytes += JOURNAL_HEADER + n;
    bytes += JOURNAL_HEADER + JOURNAL_VIEW_STATE_BYTES; bytes += JOURNAL_HEADER + 16 + 8 * tabs;
    bytes += JOURNAL_HEADER + JOURNAL_WINDOW_BYTES;
    return (bytes + JOURNAL_HEADER + JOURNAL_PAGE - 1) / JOURNAL_PAGE * JOURNAL_PAGE;
}
static uint64_t metadata_bound(const char *path, size_t tabs) { return metadata_bound_with(path, tabs, 0); }
/* edit-f3i (P) text record sizes: INSERT = JOURNAL_HEADER + 8 (offset) +
 * bytes; DELETE = JOURNAL_HEADER + 16 (offset, length). */
#define INSERT_RECORD(n) ((uint64_t)JOURNAL_HEADER + 8u + (uint64_t)(n))
#define DELETE_RECORD ((uint64_t)JOURNAL_HEADER + 16u)
static int wait_adopted(editor *e)
{
    uint64_t limit = trace_now_ns() + EDITOR_IDLE_NS;
    /* Edits start after adoption and the background line index: typing
     * while a GB-scale index build is in flight leaves settle waiting on it. */
    while (e->buffer->pending_open || !editor_index_complete(e)) {
        int rc = editor_step(e, 0); T(rc == EDITOR_OK || rc == EDITOR_MORE);
        T(trace_now_ns() < limit); sched_yield();
    }
    return settle(e);
}
static int type_text(editor *e, const char *text)
{
    for (const char *c = text; *c; c++) {
        plat_event key = {.kind = PLAT_EV_KEY, .press = true, .keysym = (uint8_t)*c, .utf8_len = 1, .t0_ns = trace_now_ns()};
        key.utf8[0] = (uint8_t)*c;
        T(editor_inject(e, &key) == 0); T(settle(e) == 0);
    }
    return 0;
}
typedef struct timed_checkpoint { uint64_t ns, size; size_t allocations; bool counted; } timed_checkpoint;
static int checkpoint_once(editor *e, const char *log, timed_checkpoint *out)
{
    T(journal_flush(e->journal) == 0);
    out->counted = edit_malloc_guard_active();
    uint64_t t0 = trace_now_ns();
    if (out->counted) edit_malloc_guard_begin();
    int rc = editor_checkpoint(e);
    if (out->counted) out->allocations = edit_malloc_guard_end();
    out->ns = trace_now_ns() - t0;
    T(rc == 0);
    struct stat st; T(stat(log, &st) == 0); out->size = (uint64_t)st.st_size;
    return 0;
}
/* Marker bytes at the start, around the middle and at the end make sampled
 * replay offsets distinguishable from the sparse zeros elsewhere. */
static const char head_mark[] = "HEAD\n", mid_mark[] = "MID\n", tail_mark[] = "\nTAIL";
static int gib_fixture(const char *source)
{
    int fd = open(source, O_CREAT | O_EXCL | O_WRONLY, 0600); T(fd >= 0);
    bool ok = ftruncate(fd, (off_t)GIB) == 0 &&
        pwrite(fd, head_mark, sizeof head_mark - 1, 0) == (ssize_t)(sizeof head_mark - 1) &&
        pwrite(fd, mid_mark, sizeof mid_mark - 1, (off_t)(MID - (sizeof mid_mark - 1))) == (ssize_t)(sizeof mid_mark - 1) &&
        pwrite(fd, tail_mark, sizeof tail_mark - 1, (off_t)(GIB - (sizeof tail_mark - 1))) == (ssize_t)(sizeof tail_mark - 1);
    T(close(fd) == 0); T(ok); return 0;
}
typedef struct piece_replay { piece_tree *tree; uint64_t id; int error; } piece_replay;
static int replay_piece(void *ctx, const journal_record *r)
{
    piece_replay *p = ctx;
    if (r->buffer_id != p->id || (r->type != JOURNAL_INSERT && r->type != JOURNAL_DELETE)) return 0;
    int rc = journal_apply_piece(p->tree, r); if (rc) p->error = rc;
    return rc;
}
/* (a) unchanged 1 GiB file, two checkpoints; (b) ten bytes typed at the
 * middle, one checkpoint, replay of that journal onto the base file. */
static int gib_body(const char *root)
{
    char source[IPC_PATH_CAP], log[IPC_PATH_CAP], power[32], load[32];
    T(snprintf(source, sizeof source, "%s/big", root) > 0);
    T(snprintf(log, sizeof log, "%s/session", root) > 0);
    T(gib_fixture(source) == 0);
    render_backend backend = {0}; T(render_null_backend(&backend) == 0);
    editor_config config = {.path = source, .journal_path = log, .cols = 32, .rows = 3};
    editor *e = NULL; T(editor_open(&e, &config, &backend) == 0);
    int rc = wait_adopted(e);
    char canonical[IPC_PATH_CAP]; if (!realpath(source, canonical)) { editor_close(e); return 1; }
    uint64_t bound = metadata_bound(canonical, 1); /* BASE and TARGET carry the canonical path */
    timed_checkpoint first = {0}, second = {0};
    if (!rc) rc = checkpoint_once(e, log, &first);
    stamp(power, load);
    printf("editor_checkpoint_test: (a) 1 GiB unchanged checkpoint 1: journal=%llu B bound=%llu B (P) time=%.3f ms (M) allocations=%s%zu power=%s load1=%s\n",
        (unsigned long long)first.size, (unsigned long long)bound, (double)first.ns / 1e6,
        first.counted ? "" : "uncounted:", first.allocations, power, load);
    if (!rc && first.size > bound) { fprintf(stderr, "editor_checkpoint_test: (a) journal %llu B > %llu B\n", (unsigned long long)first.size, (unsigned long long)bound); rc = 1; }
    if (!rc) rc = checkpoint_once(e, log, &second);
    stamp(power, load);
    if (!rc) printf("editor_checkpoint_test: (a) 1 GiB unchanged checkpoint 2: journal=%llu B growth=%lld B time=%.3f ms (M) allocations=%s%zu power=%s load1=%s\n",
        (unsigned long long)second.size, (long long)second.size - (long long)first.size, (double)second.ns / 1e6,
        second.counted ? "" : "uncounted:", second.allocations, power, load);
    shape28 shape = {0};
    if (!rc) rc = shape_of(log, e->buffer->id, &shape);
    if (!rc && (second.size > bound || shape.deletes || shape.inserts || shape.bases != 1)) {
        fprintf(stderr, "editor_checkpoint_test: (a) size=%llu deletes=%llu inserts=%llu\n", (unsigned long long)second.size,
            (unsigned long long)shape.deletes, (unsigned long long)shape.inserts);
        rc = 1;
    }
    /* (b) Ten bytes at the middle (a line start: mid_mark ends in '\n'). */
    static const char typed[] = "0123456789";
    if (!rc) rc = editor_set_cursor(e, MID) ? 1 : settle(e);
    if (!rc) rc = type_text(e, typed);
    if (!rc && editor_length(e) != GIB + sizeof typed - 1) rc = 1;
    timed_checkpoint third = {0};
    uint64_t pieces = rc ? 0 : piece_piece_count(e->buffer->tree);
    if (!rc) rc = checkpoint_once(e, log, &third);
    if (!rc) rc = shape_of(log, e->buffer->id, &shape);
    stamp(power, load);
    printf("editor_checkpoint_test: (b) 1 GiB + 10 B checkpoint: journal=%llu B bound=%llu B (P) pieces=%llu deletes=%llu inserts=%llu inserted=%llu B time=%.3f ms (M) allocations=%s%zu power=%s load1=%s\n",
        (unsigned long long)third.size, (unsigned long long)bound, (unsigned long long)pieces,
        (unsigned long long)shape.deletes, (unsigned long long)shape.inserts, (unsigned long long)shape.inserted,
        (double)third.ns / 1e6, third.counted ? "" : "uncounted:", third.allocations, power, load);
    /* (P) ten typed bytes coalesce into one add piece (piece.h APPEND
     * COALESCING); the original's two halves stay contiguous: no DELETE. */
    if (!rc && (third.size > bound || shape.deletes || shape.inserted != sizeof typed - 1)) rc = 1;
    uint64_t id = e->buffer->id;
    editor_close(e);
    if (rc) return rc;
    /* Replay the journal onto the base file: the edit at the middle, base
     * bytes everywhere else. */
    int fd = open(source, O_RDONLY); T(fd >= 0);
    void *map = mmap(NULL, (size_t)GIB, PROT_READ, MAP_PRIVATE, fd, 0); T(close(fd) == 0); T(map != MAP_FAILED);
    piece_allocator allocator = piece_default_allocator();
    piece_replay replay = {piece_create(&allocator), id, 0}; journal_replay_result result;
    bool ok = replay.tree && piece_init_mapped(replay.tree, map, (size_t)GIB, NULL) == 0 &&
        journal_replay_file(log, replay_piece, &replay, &result) == 0 && !result.corrupt && !replay.error &&
        piece_len(replay.tree) == GIB + sizeof typed - 1;
    uint8_t got[16];
    if (ok) ok = piece_read(replay.tree, 0, got, sizeof head_mark - 1) == 0 && !memcmp(got, head_mark, sizeof head_mark - 1);
    if (ok) ok = piece_read(replay.tree, MID - (sizeof mid_mark - 1), got, sizeof mid_mark - 1) == 0 && !memcmp(got, mid_mark, sizeof mid_mark - 1);
    if (ok) ok = piece_read(replay.tree, MID, got, sizeof typed - 1) == 0 && !memcmp(got, typed, sizeof typed - 1);
    if (ok) ok = piece_read(replay.tree, MID + sizeof typed - 1, got, 1) == 0 && got[0] == 0;
    if (ok) ok = piece_read(replay.tree, GIB / 4u, got, 1) == 0 && got[0] == 0;
    if (ok) ok = piece_read(replay.tree, GIB + sizeof typed - 1 - (sizeof tail_mark - 1), got, sizeof tail_mark - 1) == 0 &&
        !memcmp(got, tail_mark, sizeof tail_mark - 1);
    if (replay.tree) piece_destroy(replay.tree);
    munmap(map, (size_t)GIB);
    printf("editor_checkpoint_test: (b) replay onto base: %s\n", ok ? "edit at middle, base bytes at head/quarter/after edit/tail" : "MISMATCH");
    T(ok);
    return 0;
}
static int gib_checkpoints(void)
{
    char root[] = "build/editor-28u-gib-XXXXXX"; T(mkdtemp(root));
    uint64_t t0 = trace_now_ns();
    int rc = gib_body(root);
    remove_tree(root); /* every path, success or failure */
    char power[32], load[32]; stamp(power, load);
    printf("editor_checkpoint_test: (a)+(b) wall=%.3f ms (M) power=%s load1=%s\n", (double)(trace_now_ns() - t0) / 1e6, power, load);
    return rc;
}
static int small_fixture(const char *source, uint8_t *bytes)
{
    for (size_t i = 0; i < SMALL; i++) bytes[i] = i % 64u == 63u ? '\n' : (uint8_t)('a' + i % 26u);
    int fd = open(source, O_CREAT | O_EXCL | O_WRONLY, 0600); T(fd >= 0);
    bool ok = write(fd, bytes, SMALL) == (ssize_t)SMALL; T(close(fd) == 0); T(ok); return 0;
}
/* Edits: "EF" at the end, three forward deletes at P2, "AB" at P1. */
#define P1 ((size_t)100u)
#define P2 ((size_t)FILE_PREFIX_MAX + 5u)
static size_t small_expected(const uint8_t *base, uint8_t *out)
{
    size_t n = 0;
    memcpy(out, base, P1); n += P1; memcpy(out + n, "AB", 2); n += 2;
    memcpy(out + n, base + P1, P2 - P1); n += P2 - P1;
    memcpy(out + n, base + P2 + 3, SMALL - P2 - 3); n += SMALL - P2 - 3;
    memcpy(out + n, "EF", 2); n += 2; return n;
}
/* edit-fi2: case (c) pins edit-btj's set-aside fallback, which applies when
 * no retained copy of BASE was referenced yet; the child disables retention. */
static bool child_no_retain;
static int small_child(const char *source, const char *log, const int *lease)
{
    render_backend backend = {0}; T(render_null_backend(&backend) == 0);
    editor_config config = {.path = source, .journal_path = log, .session_lease = lease, .cols = 32, .rows = 3};
    editor *e = NULL; T(editor_open(&e, &config, &backend) == 0); T(wait_adopted(e) == 0);
    if (child_no_retain) e->retain_limit = 0;
    T(editor_set_cursor(e, SMALL) == 0); T(type_text(e, "EF") == 0);
    T(editor_set_cursor(e, P2) == 0);
    for (int i = 0; i < 3; i++) {
        plat_event del = {.kind = PLAT_EV_KEY, .press = true, .keysym = XKB_KEY_Delete, .t0_ns = trace_now_ns()};
        T(editor_inject(e, &del) == 0); T(settle(e) == 0);
    }
    T(editor_set_cursor(e, P1) == 0); T(type_text(e, "AB") == 0);
    T(editor_length(e) == SMALL + 1u);
    T(journal_flush(e->journal) == 0); T(editor_checkpoint(e) == 0);
    _exit(0); /* crash: no editor_close */
}
static int read_all(editor *e, uint8_t *out, size_t cap, size_t *len)
{
    *len = (size_t)editor_length(e); T(*len <= cap);
    T(editor_read(e, 0, out, *len) == 0); return 0;
}
/* Edited 2 MiB file: referencing checkpoint, crash, editor recovery. */
static int small_recovery_body(const char *root, uint8_t *base, uint8_t *want, uint8_t *got)
{
    char source[IPC_PATH_CAP], log[IPC_PATH_CAP];
    T(snprintf(source, sizeof source, "%s/source", root) > 0);
    T(snprintf(log, sizeof log, "%s/session", root) > 0);
    T(small_fixture(source, base) == 0);
    size_t want_len = small_expected(base, want);
    pid_t child = fork(); T(child >= 0);
    if (!child) _exit(small_child(source, log, NULL));
    int status; T(waitpid(child, &status, 0) == child); T(WIFEXITED(status) && WEXITSTATUS(status) == 0);
    shape28 shape; T(shape_of(log, 1, &shape) == 0);
    struct stat st; T(stat(log, &st) == 0);
    printf("editor_checkpoint_test: (b2) 2 MiB edited checkpoint: journal=%llu B deletes=%llu deleted=%llu inserts=%llu inserted=%llu B\n",
        (unsigned long long)st.st_size, (unsigned long long)shape.deletes, (unsigned long long)shape.deleted,
        (unsigned long long)shape.inserts, (unsigned long long)shape.inserted);
    /* (P) pieces: original [0,P1), "AB", original [P1,P2), original
     * [P2+3,end), "EF": one gap of 3 deleted bytes, 2 + 2 inserted bytes. */
    T(shape.deletes == 1 && shape.deleted == 3 && shape.inserted == 4);
    render_backend backend = {0}; T(render_null_backend(&backend) == 0);
    editor_config config = {.journal_path = log, .start_empty = true, .cols = 32, .rows = 3};
    editor *e = NULL; T(editor_open(&e, &config, &backend) == 0);
    /* edit-f3i (c): editor_open's startup install wrote its checkpoint;
     * later turns append metadata records after it. */
    struct stat st_after; T(stat(log, &st_after) == 0);
    int rc = settle(e);
    size_t got_len = 0;
    if (!rc) rc = tabs_count(&e->tabs) == 1 ? read_all(e, got, SMALL + 8u, &got_len) : 1;
    bool modified = !rc && editor_tab(e, 0)->modified;
    /* edit-f3i (c): the startup install's checkpoint (rotated into the same
     * journal) is differences-only too, the same records as the crashed one. */
    uint64_t installed_id = rc ? 0 : editor_tab(e, 0)->id;
    editor_close(e);
    T(rc == 0); T(got_len == want_len && !memcmp(got, want, want_len)); T(modified);
    puts("editor_checkpoint_test: (b2) referencing checkpoint of an edited COPY file recovers the edited text");
    shape28 after; T(shape_of(log, installed_id, &after) == 0);
    char canonical[IPC_PATH_CAP]; T(realpath(source, canonical));
    /* (P) checkpoint: metadata + one DELETE + two 2-byte INSERTs; then the
     * install's first metadata turn appends one VIEW, TABS (one tab) and
     * WINDOW record (the checkpoint does not seed the metadata caches). */
    uint64_t appended = (JOURNAL_HEADER + JOURNAL_VIEW_STATE_BYTES) + (JOURNAL_HEADER + 16u + 8u) + (JOURNAL_HEADER + JOURNAL_WINDOW_BYTES);
    uint64_t bound = metadata_bound_with(canonical, 1, DELETE_RECORD + 2u * INSERT_RECORD(2)) + appended;
    printf("editor_checkpoint_test: f3i (c) recovered 2 MiB buffer, install checkpoint: journal before=%llu B after=%llu B bound=%llu B (P) deletes=%llu deleted=%llu inserts=%llu inserted=%llu B\n",
        (unsigned long long)st.st_size, (unsigned long long)st_after.st_size, (unsigned long long)bound,
        (unsigned long long)after.deletes, (unsigned long long)after.deleted,
        (unsigned long long)after.inserts, (unsigned long long)after.inserted);
    T(after.bases == 1 && after.deletes == 1 && after.deleted == 3 && after.inserted == 4 && (uint64_t)st_after.st_size <= bound);
    return 0;
}
/* (c) The source changes after the referencing checkpoint: replay refuses
 * the journal and discovery applies the edit-btj set-aside policy. */
static int changed_base_body(const char *root, uint8_t *base, uint8_t *got)
{
    char dir[IPC_PATH_CAP], source[IPC_PATH_CAP], orphan[IPC_PATH_CAP];
    T(setenv("XDG_DATA_HOME", root, 1) == 0);
    T(journal_default_dir(dir, sizeof dir) == JOURNAL_OK);
    T(!strncmp(dir, root, strlen(root))); /* never the real profile */
    T(editor_make_directory(dir, NULL, NULL) == 0);
    T(snprintf(source, sizeof source, "%s/source", root) > 0);
    T(small_fixture(source, base) == 0);
    /* The orphan is written through an explicitly named journal and then
     * renamed into the profile as a session-* name: a crashed session left
     * exactly that file. (A lease-created child hits a pre-existing,
     * unrelated intermittent EDITOR_ERR_ARG while typing; see the report.) */
    char written[IPC_PATH_CAP];
    T(snprintf(written, sizeof written, "%s/orphan", root) > 0);
    T(snprintf(orphan, sizeof orphan, "%s/session-orphan", dir) > 0);
    child_no_retain = true;
    pid_t child = fork(); T(child >= 0);
    if (!child) _exit(small_child(source, written, NULL));
    child_no_retain = false;
    int status; T(waitpid(child, &status, 0) == child); T(WIFEXITED(status) && WEXITSTATUS(status) == 0);
    T(rename(written, orphan) == 0);
    shape28 shape; T(shape_of(orphan, 1, &shape) == 0); T(shape.deletes == 1 && shape.inserted == 4);
    static const char changed[] = "changed";
    int fd = open(source, O_WRONLY); T(fd >= 0);
    T(pwrite(fd, changed, sizeof changed - 1, 0) == (ssize_t)(sizeof changed - 1)); T(close(fd) == 0);
    journal_replay_result result;
    T(journal_replay_file(orphan, NULL, NULL, &result) == JOURNAL_BASE_CHANGED);
    char journal_path[IPC_PATH_CAP]; int lease = -1;
    T(journal_session_create(dir, journal_path, sizeof journal_path, &lease) == JOURNAL_OK);
    render_backend backend = {0}; T(render_null_backend(&backend) == 0);
    editor_config config = {.journal_path = journal_path, .session_lease = &lease, .start_empty = true, .cols = 32, .rows = 3};
    editor *e = NULL; int rc = editor_open(&e, &config, &backend);
    if (!rc) rc = settle(e);
    size_t got_len = 0; bool aside = false, clean = false, notice = false;
    if (!rc && tabs_count(&e->tabs) == 1) {
        rc = read_all(e, got, SMALL + 8u, &got_len);
        clean = !editor_tab(e, 0)->modified;
        aside = *e->recovery_aside && access(e->recovery_aside, F_OK) == 0 && strstr(e->recovery_aside, ".set-aside-");
        notice = e->recovery_notice != NULL;
    } else if (!rc) rc = 1;
    if (e) editor_close(e);
    (void)journal_session_release(journal_path, lease); close(lease);
    memcpy(base, changed, sizeof changed - 1);
    T(rc == 0); T(got_len == SMALL && !memcmp(got, base, SMALL)); T(clean); T(aside); T(notice);
    T(access(orphan, F_OK) != 0); /* renamed aside, never deleted */
    puts("editor_checkpoint_test: (c) changed BASE refuses the referencing journal; disk text opens, journal set aside");
    return 0;
}
/* (d) A journal in the pre-edit-28u checkpoint shape (DELETE of the whole
 * BASE, then the full text as 4 KiB INSERT records, as checkpoint.c wrote
 * it before this bead), written with the journal API, still recovers. */
static int legacy_body(const char *root, uint8_t *base, uint8_t *want, uint8_t *got)
{
    char source[IPC_PATH_CAP], log[IPC_PATH_CAP];
    T(snprintf(source, sizeof source, "%s/source", root) > 0);
    T(snprintf(log, sizeof log, "%s/session", root) > 0);
    T(small_fixture(source, base) == 0);
    size_t want_len = small_expected(base, want);
    journal_base captured; T(journal_capture_base(source, &captured) == 0);
    /* (P) legacy slice = one 4096-byte page, the old checkpoint.c's chunk. */
    size_t chunks = (want_len + JOURNAL_PAGE - 1) / JOURNAL_PAGE, count = 0;
    journal_record *records = calloc(chunks + 6u, sizeof *records);
    uint8_t *payload = malloc(JOURNAL_BASE_MAX + strlen(source) + 1 + 16 + want_len + chunks * 8u + JOURNAL_VIEW_STATE_BYTES + 24 + JOURNAL_WINDOW_BYTES);
    T(records && payload);
    uint8_t *p = payload; size_t size = 0;
    int rc = journal_encode_base(&captured, p, JOURNAL_BASE_MAX, &size);
    records[count++] = (journal_record){JOURNAL_BASE, 1, 0, p, size}; p += size;
    size = strlen(captured.path) + 1; memcpy(p, captured.path, size);
    records[count++] = (journal_record){JOURNAL_TARGET, 1, 0, p, size}; p += size;
    memset(p, 0, 16); for (unsigned i = 0; i < 8; i++) p[8 + i] = (uint8_t)((uint64_t)SMALL >> (8u * i));
    records[count++] = (journal_record){JOURNAL_DELETE, 1, 0, p, 16}; p += 16;
    for (size_t off = 0; off < want_len; off += JOURNAL_PAGE) {
        size_t n = want_len - off < JOURNAL_PAGE ? want_len - off : JOURNAL_PAGE;
        for (unsigned i = 0; i < 8; i++) p[i] = (uint8_t)((uint64_t)off >> (8u * i));
        memcpy(p + 8, want + off, n);
        records[count++] = (journal_record){JOURNAL_INSERT, 1, 0, p, n + 8}; p += n + 8;
    }
    journal_view_state state = {0}; journal_encode_view_state(&state, p);
    records[count++] = (journal_record){JOURNAL_VIEW, 1, 0, p, JOURNAL_VIEW_STATE_BYTES}; p += JOURNAL_VIEW_STATE_BYTES;
    memset(p, 0, 24); p[0] = 1; p[16] = 1; /* one tab, active index 0, id 1 */
    records[count++] = (journal_record){JOURNAL_TABS, 0, 0, p, 24}; p += 24;
    journal_encode_window(&(journal_window){640, 480, JOURNAL_WINDOW_CHOSEN}, p);
    records[count++] = (journal_record){JOURNAL_WINDOW, 0, 0, p, JOURNAL_WINDOW_BYTES};
    work_pool pool; journal *j = NULL;
    if (!rc) rc = work_pool_init(&pool, 1, 0);
    if (!rc) {
        rc = journal_open(&j, log, &pool, NULL);
        if (!rc) rc = journal_rotate(j, records, count);
        if (j) journal_close(j);
        work_pool_shutdown(&pool);
    }
    free(records); free(payload);
    T(rc == 0);
    shape28 shape; T(shape_of(log, 1, &shape) == 0);
    T(shape.deletes == 1 && shape.first_delete_off == 0 && shape.first_delete_len == SMALL && shape.inserted == want_len);
    render_backend backend = {0}; T(render_null_backend(&backend) == 0);
    editor_config config = {.journal_path = log, .start_empty = true, .cols = 32, .rows = 3};
    editor *e = NULL; T(editor_open(&e, &config, &backend) == 0);
    rc = settle(e);
    size_t got_len = 0;
    if (!rc) rc = tabs_count(&e->tabs) == 1 ? read_all(e, got, SMALL + 8u, &got_len) : 1;
    editor_close(e);
    T(rc == 0); T(got_len == want_len && !memcmp(got, want, want_len));
    puts("editor_checkpoint_test: (d) legacy whole-text checkpoint journal still recovers");
    return 0;
}
/* (e) Saved and unchanged since: after Ctrl+S completes, a checkpoint of the
 * saved revision writes BASE only, and recovery opens the saved text clean. */
static int saved_child(const char *source, const char *log)
{
    render_backend backend = {0}; T(render_null_backend(&backend) == 0);
    editor_config config = {.path = source, .journal_path = log, .cols = 32, .rows = 3};
    editor *e = NULL; T(editor_open(&e, &config, &backend) == 0); T(wait_adopted(e) == 0);
    T(editor_set_cursor(e, P1) == 0); T(type_text(e, "AB") == 0);
    plat_event save = {.kind = PLAT_EV_KEY, .press = true, .keysym = 's', .mods = PLAT_MOD_CTRL, .t0_ns = trace_now_ns()};
    T(editor_inject(e, &save) == 0); T(settle(e) == 0); T(!e->buffer->save_error && !editor_tab(e, 0)->modified);
    T(journal_flush(e->journal) == 0); T(editor_checkpoint(e) == 0);
    _exit(0);
}
static int saved_body(const char *root, uint8_t *base, uint8_t *want, uint8_t *got)
{
    char source[IPC_PATH_CAP], log[IPC_PATH_CAP];
    T(snprintf(source, sizeof source, "%s/source", root) > 0);
    T(snprintf(log, sizeof log, "%s/session", root) > 0);
    T(small_fixture(source, base) == 0);
    memcpy(want, base, P1); memcpy(want + P1, "AB", 2); memcpy(want + P1 + 2, base + P1, SMALL - P1);
    size_t want_len = SMALL + 2u;
    pid_t child = fork(); T(child >= 0);
    if (!child) _exit(saved_child(source, log));
    int status; T(waitpid(child, &status, 0) == child); T(WIFEXITED(status) && WEXITSTATUS(status) == 0);
    shape28 shape; T(shape_of(log, 1, &shape) == 0);
    struct stat st; T(stat(log, &st) == 0);
    printf("editor_checkpoint_test: (e) saved+unchanged checkpoint: journal=%llu B deletes=%llu inserts=%llu\n",
        (unsigned long long)st.st_size, (unsigned long long)shape.deletes, (unsigned long long)shape.inserts);
    T(shape.bases == 1 && !shape.deletes && !shape.inserts);
    render_backend backend = {0}; T(render_null_backend(&backend) == 0);
    editor_config config = {.journal_path = log, .start_empty = true, .cols = 32, .rows = 3};
    editor *e = NULL; T(editor_open(&e, &config, &backend) == 0);
    int rc = settle(e);
    size_t got_len = 0;
    if (!rc) rc = tabs_count(&e->tabs) == 1 ? read_all(e, got, SMALL + 8u, &got_len) : 1;
    bool clean = !rc && !editor_tab(e, 0)->modified;
    editor_close(e);
    T(rc == 0); T(got_len == want_len && !memcmp(got, want, want_len)); T(clean);
    puts("editor_checkpoint_test: (e) saved revision checkpoints as BASE only and recovers the saved text clean");
    return 0;
}
/* ---- edit-f3i: after a save, checkpoints diff against the saved revision --
 * (P) record sizes: INSERT = JOURNAL_HEADER + 8 (offset) + bytes; DELETE =
 * JOURNAL_HEADER + 16 (offset, length). Typed literals are the edits. */
static int press_save(editor *e)
{
    plat_event save = {.kind = PLAT_EV_KEY, .press = true, .keysym = 's', .mods = PLAT_MOD_CTRL, .t0_ns = trace_now_ns()};
    T(editor_inject(e, &save) == 0); return 0;
}
/* (a) 1 GiB sparse file: type one byte at the middle, Ctrl+S (complete),
 * type one more byte, checkpoint. The journal holds metadata plus one INSERT
 * of the one byte typed after the save, not the 2^30-byte text. */
static int saved_gib_body(const char *root)
{
    char source[IPC_PATH_CAP], log[IPC_PATH_CAP], power[32], load[32];
    T(snprintf(source, sizeof source, "%s/big", root) > 0);
    T(snprintf(log, sizeof log, "%s/session", root) > 0);
    T(gib_fixture(source) == 0);
    render_backend backend = {0}; T(render_null_backend(&backend) == 0);
    editor_config config = {.path = source, .journal_path = log, .cols = 32, .rows = 3};
    editor *e = NULL; T(editor_open(&e, &config, &backend) == 0);
    int rc = wait_adopted(e);
    char canonical[IPC_PATH_CAP]; if (!realpath(source, canonical)) { editor_close(e); return 1; }
    if (!rc) rc = editor_set_cursor(e, MID) ? 1 : settle(e);
    if (!rc) rc = type_text(e, "x");
    uint64_t t0 = trace_now_ns();
    if (!rc) rc = press_save(e);
    /* (E) watchdog only: the save writes 2^30 + 1 bytes and the journal
     * retains the previous generation; six idle periods, not a budget. */
    if (!rc) rc = settle_until(e, trace_now_ns() + 6u * EDITOR_IDLE_NS);
    uint64_t save_ns = trace_now_ns() - t0;
    if (!rc && (e->buffer->save_error || editor_tab(e, 0)->modified)) rc = 1;
    if (!rc) rc = type_text(e, "y");
    uint64_t pieces = rc ? 0 : piece_piece_count(e->buffer->tree);
    timed_checkpoint cp = {0}; shape28 shape = {0};
    if (!rc) rc = checkpoint_once(e, log, &cp);
    if (!rc) rc = shape_of(log, e->buffer->id, &shape);
    uint64_t bound = metadata_bound_with(canonical, 1, INSERT_RECORD(1));
    stamp(power, load);
    printf("editor_checkpoint_test: f3i (a) 1 GiB, save, +1 B checkpoint: journal=%llu B bound=%llu B (P) pieces=%llu deletes=%llu deleted=%llu inserts=%llu inserted=%llu B save=%.3f ms checkpoint=%.3f ms (M) allocations=%s%zu power=%s load1=%s\n",
        (unsigned long long)cp.size, (unsigned long long)bound, (unsigned long long)pieces,
        (unsigned long long)shape.deletes, (unsigned long long)shape.deleted, (unsigned long long)shape.inserts,
        (unsigned long long)shape.inserted, (double)save_ns / 1e6, (double)cp.ns / 1e6,
        cp.counted ? "" : "uncounted:", cp.allocations, power, load);
    if (!rc && (cp.size > bound || shape.deletes || shape.inserted != 1 || shape.bases != 1)) rc = 1;
    uint64_t id = e->buffer->id;
    editor_close(e);
    if (rc) return rc;
    /* Replay onto the saved file (2^30 + 1 bytes, 'x' at MID): 'y' after it. */
    int fd = open(source, O_RDONLY); T(fd >= 0);
    struct stat st; T(fstat(fd, &st) == 0); T((uint64_t)st.st_size == GIB + 1u);
    void *map = mmap(NULL, (size_t)st.st_size, PROT_READ, MAP_PRIVATE, fd, 0); T(close(fd) == 0); T(map != MAP_FAILED);
    piece_allocator allocator = piece_default_allocator();
    piece_replay replay = {piece_create(&allocator), id, 0}; journal_replay_result result;
    bool ok = replay.tree && piece_init_mapped(replay.tree, map, (size_t)st.st_size, NULL) == 0 &&
        journal_replay_file(log, replay_piece, &replay, &result) == 0 && !result.corrupt && !replay.error &&
        piece_len(replay.tree) == GIB + 2u;
    uint8_t got[16];
    if (ok) ok = piece_read(replay.tree, 0, got, sizeof head_mark - 1) == 0 && !memcmp(got, head_mark, sizeof head_mark - 1);
    if (ok) ok = piece_read(replay.tree, MID - 1u, got, 3) == 0 && !memcmp(got, "\nxy", 3);
    if (ok) ok = piece_read(replay.tree, MID + 2u, got, 1) == 0 && got[0] == 0;
    if (ok) ok = piece_read(replay.tree, GIB + 2u - (sizeof tail_mark - 1), got, sizeof tail_mark - 1) == 0 &&
        !memcmp(got, tail_mark, sizeof tail_mark - 1);
    if (replay.tree) piece_destroy(replay.tree);
    munmap(map, (size_t)st.st_size);
    printf("editor_checkpoint_test: f3i (a) replay onto saved file: %s\n", ok ? "xy at middle, head/tail intact" : "MISMATCH");
    T(ok);
    return 0;
}
static int saved_gib(void)
{
    char root[] = "build/editor-f3i-gib-XXXXXX"; T(mkdtemp(root));
    int rc = saved_gib_body(root);
    remove_tree(root); /* every path, success or failure */
    return rc;
}
/* (b) Save in flight. step 1: a checkpoint while the write is held encodes
 * against the pre-save BASE; step 2: the finish checkpoint after the save
 * completes references the saved file (only bytes typed after the save
 * request); step 3: a later checkpoint references the saved file too. Each
 * step ends in a crash and the text is recovered. */
typedef struct held_save { atomic_bool entered, release; int phase; } held_save;
static void hold(void *ctx, int phase)
{
    held_save *h = ctx; if (phase != h->phase) return;
    atomic_store(&h->entered, true);
    uint64_t limit = trace_now_ns() + EDITOR_IDLE_NS;
    while (!atomic_load(&h->release) && trace_now_ns() < limit) sched_yield();
}
/* Typing while the save is held: settle() would wait for the save. */
static int type_held(editor *e, const char *text)
{
    for (const char *c = text; *c; c++) {
        plat_event key = {.kind = PLAT_EV_KEY, .press = true, .keysym = (uint8_t)*c, .utf8_len = 1, .t0_ns = trace_now_ns()};
        key.utf8[0] = (uint8_t)*c;
        T(editor_inject(e, &key) == 0);
        uint64_t limit = trace_now_ns() + EDITOR_IDLE_NS;
        do {
            int rc = editor_step(e, 0); T(rc == EDITOR_OK || rc == EDITOR_MORE);
            T(trace_now_ns() < limit); sched_yield();
        } while (editor_get_stats(e).pending || editor_get_stats(e).render_active);
    }
    return 0;
}
static int inflight_child(const char *source, const char *log, int step)
{
    render_backend backend = {0}; T(render_null_backend(&backend) == 0);
    editor_config config = {.path = source, .journal_path = log, .cols = 32, .rows = 3};
    editor *e = NULL; T(editor_open(&e, &config, &backend) == 0); T(wait_adopted(e) == 0);
    T(editor_set_cursor(e, P1) == 0); T(type_text(e, "AB") == 0);
    static held_save held = {.phase = FILE_STEP_TEMP_WRITTEN};
    T(savectl_set_step_hook(e->buffer->save, hold, &held) == 0);
    T(press_save(e) == 0);
    uint64_t limit = trace_now_ns() + EDITOR_IDLE_NS;
    while (!atomic_load(&held.entered)) {
        int rc = editor_step(e, 0); T(rc == EDITOR_OK || rc == EDITOR_MORE);
        T(trace_now_ns() < limit); sched_yield();
    }
    T(editor_set_cursor(e, P2) == 0); T(type_held(e, "CD") == 0);
    if (step == 1) { T(journal_flush(e->journal) == 0); T(editor_checkpoint(e) == 0); _exit(0); }
    atomic_store(&held.release, true); T(settle(e) == 0);
    T(!e->buffer->save_error && editor_tab(e, 0)->modified);
    if (step == 2) _exit(0); /* the finish checkpoint is the journal */
    T(editor_set_cursor(e, editor_length(e)) == 0); T(type_text(e, "EF") == 0);
    T(journal_flush(e->journal) == 0); T(editor_checkpoint(e) == 0);
    _exit(0);
}
static int inflight_step(const char *root, uint8_t *base, uint8_t *want, uint8_t *got, int step)
{
    char source[IPC_PATH_CAP], log[IPC_PATH_CAP];
    T(snprintf(source, sizeof source, "%s/source-%d", root, step) > 0);
    T(snprintf(log, sizeof log, "%s/journal-%d", root, step) > 0);
    T(small_fixture(source, base) == 0);
    /* (P) "AB" at P1, then "CD" at P2 of that text, then (step 3) "EF" at the end. */
    size_t n = 0;
    memcpy(want, base, P1); n += P1; memcpy(want + n, "AB", 2); n += 2;
    memcpy(want + n, base + P1, P2 - 2u - P1); n += P2 - 2u - P1; memcpy(want + n, "CD", 2); n += 2;
    memcpy(want + n, base + P2 - 2u, SMALL - (P2 - 2u)); n += SMALL - (P2 - 2u);
    if (step == 3) { memcpy(want + n, "EF", 2); n += 2; }
    pid_t child = fork(); T(child >= 0);
    if (!child) _exit(inflight_child(source, log, step));
    int status; T(waitpid(child, &status, 0) == child); T(WIFEXITED(status) && WEXITSTATUS(status) == 0);
    shape28 shape; T(shape_of(log, 1, &shape) == 0);
    struct stat st; T(stat(log, &st) == 0);
    char canonical[IPC_PATH_CAP]; T(realpath(source, canonical));
    /* (P) expected text records per step: 1 = "AB" and "CD" against the
     * pre-save BASE; 2 = "CD" against the saved file; 3 = "CD" and "EF". */
    uint64_t inserted = step == 2 ? 2u : 4u;
    uint64_t bound = metadata_bound_with(canonical, 1, INSERT_RECORD(2) * (inserted / 2u));
    printf("editor_checkpoint_test: f3i (b) step %d: journal=%llu B bound=%llu B (P) bases=%llu deletes=%llu deleted=%llu inserts=%llu inserted=%llu B\n",
        step, (unsigned long long)st.st_size, (unsigned long long)bound, (unsigned long long)shape.bases,
        (unsigned long long)shape.deletes, (unsigned long long)shape.deleted,
        (unsigned long long)shape.inserts, (unsigned long long)shape.inserted);
    render_backend backend = {0}; T(render_null_backend(&backend) == 0);
    editor_config config = {.journal_path = log, .start_empty = true, .cols = 32, .rows = 3};
    editor *e = NULL; T(editor_open(&e, &config, &backend) == 0);
    int rc = settle(e);
    size_t got_len = 0;
    if (!rc) rc = tabs_count(&e->tabs) == 1 ? read_all(e, got, SMALL + 8u, &got_len) : 1;
    bool modified = !rc && editor_tab(e, 0)->modified;
    editor_close(e);
    if (got_len != n || memcmp(got, want, n)) {
        size_t at = 0; while (at < n && at < got_len && got[at] == want[at]) at++;
        fprintf(stderr, "editor_checkpoint_test: f3i (b) step %d text got_len=%zu want=%zu first difference at %zu: got \"%.8s\" want \"%.8s\"\n",
            step, got_len, n, at, at < got_len ? (const char *)got + at : "", at < n ? (const char *)want + at : "");
    }
    T(rc == 0); T(got_len == n && !memcmp(got, want, n)); T(modified);
    T(shape.bases == 1 && !shape.deletes && shape.inserted == inserted && (uint64_t)st.st_size <= bound);
    return 0;
}
static int inflight_body(const char *root, uint8_t *base, uint8_t *want, uint8_t *got)
{
    int failed = 0;
    for (int step = 1; step <= 3; step++) if (inflight_step(root, base, want, got, step)) failed++;
    T(!failed);
    puts("editor_checkpoint_test: f3i (b) save in flight: pre-save BASE while held, saved file after; recovered at each step");
    return 0;
}
typedef int (*small_case)(const char *root, uint8_t *base, uint8_t *want, uint8_t *got);
static int changed_case(const char *root, uint8_t *base, uint8_t *want, uint8_t *got)
{ (void)want; return changed_base_body(root, base, got); }
static int run_small(const char *pattern, small_case body)
{
    char made[64]; T(snprintf(made, sizeof made, "%s", pattern) > 0); T(mkdtemp(made));
    char root[IPC_PATH_CAP]; T(realpath(made, root));
    const char *saved = getenv("XDG_DATA_HOME"); char keep[IPC_PATH_CAP] = ""; bool had = saved != NULL;
    if (had) T(snprintf(keep, sizeof keep, "%s", saved) > 0);
    uint8_t *base = malloc(SMALL), *want = malloc(SMALL + 8u), *got = malloc(SMALL + 8u);
    int rc = base && want && got ? body(root, base, want, got) : 1;
    free(base); free(want); free(got);
    remove_tree(root); /* every path */
    if (had) setenv("XDG_DATA_HOME", keep, 1); else unsetenv("XDG_DATA_HOME");
    return rc;
}
/* Optional arguments select cases by name (all when none is given). */
static int only_argc; static char **only_argv;
static bool wanted(const char *name)
{
    if (only_argc < 2) return true;
    for (int i = 1; i < only_argc; i++) if (!strcmp(only_argv[i], name)) return true;
    return false;
}
int main(int argc, char **argv)
{
    only_argc = argc; only_argv = argv;
    trace_init(); T(trace_thread_register() >= 0);
    /* Never touch a real profile: every case runs under a private root. */
    char made[] = "build/editor-28u-xdg-XXXXXX"; T(mkdtemp(made));
    char xdg[IPC_PATH_CAP]; T(realpath(made, xdg)); T(setenv("XDG_DATA_HOME", xdg, 1) == 0);
    int failed = 0;
    if (wanted("busy") && busy_close_checkpoint()) failed++;
    if (wanted("pending") && pending_checkpoint(false)) failed++;
    if (wanted("pending") && pending_checkpoint(true)) failed++;
    if (wanted("empty") && adopted_empty_checkpoint()) failed++;
    if (wanted("gib") && gib_checkpoints()) { failed++; puts("editor_checkpoint_test: FAIL (a)/(b) 1 GiB checkpoints"); }
    if (wanted("b2") && run_small("build/editor-28u-b2-XXXXXX", small_recovery_body)) { failed++; puts("editor_checkpoint_test: FAIL (b2) / f3i (c)"); }
    if (wanted("c") && run_small("build/editor-28u-c-XXXXXX", changed_case)) { failed++; puts("editor_checkpoint_test: FAIL (c)"); }
    if (wanted("d") && run_small("build/editor-28u-d-XXXXXX", legacy_body)) { failed++; puts("editor_checkpoint_test: FAIL (d)"); }
    if (wanted("e") && run_small("build/editor-28u-e-XXXXXX", saved_body)) { failed++; puts("editor_checkpoint_test: FAIL (e)"); }
    if (wanted("f3i-a") && saved_gib()) { failed++; puts("editor_checkpoint_test: FAIL f3i (a) 1 GiB checkpoint after save"); }
    if (wanted("f3i-b") && run_small("build/editor-f3i-b-XXXXXX", inflight_body)) { failed++; puts("editor_checkpoint_test: FAIL f3i (b)"); }
    remove_tree(xdg);
    if (failed) { printf("editor_checkpoint_test: %d case(s) failed\n", failed); return 1; }
    puts("editor_checkpoint_test: pending COPY checkpoint/crash/recovery/save, edit-28u referencing checkpoints, edit-f3i post-save checkpoints passed");
    return 0;
}
