/* edit-fi0p F7: deterministic BASE rewrites at materialisation's read boundary.
 * (P) Sizes derive from literal bytes and the journal BASE wire schema.
 * Zero/one mean success/failure, empty state, first id and unit counts.
 * Include the implementation to intercept only its bulk read, leaving strict
 * journal BASE validation and file acquisition untouched. */
#include "editor/private.h"
#include "trace/trace.h"
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static struct { const char *path; bool before, armed; } rewrite;
static os_err materialize_read(os_file *source, void *out, size_t size, size_t *got)
{
    if (!rewrite.armed) return os_file_read(source, out, size, got);
    rewrite.armed = false;
    *got = 0;
    /* (P) Split the read in half to force a mixed-generation prefix even
     * on filesystems whose timestamps coalesce adjacent writes. */
    if (!rewrite.before) {
        os_err read_rc = os_file_read(source, out, size / 2u, got);
        if (read_rc != OS_OK) return read_rc;
    }
    os_file writer = OS_FILE_INIT;
    if (os_file_open(&writer, rewrite.path, OS_OPEN_WRITE) != OS_OK) return OS_E_IO;
    const char changed[] = "rewritten";
    size_t wrote;
    os_err written = os_file_pwrite(&writer, changed, sizeof changed - 1u, 0, &wrote);
    os_file_close(&writer);
    if (written != OS_OK || wrote != sizeof changed - 1u) return OS_E_IO;
    return rewrite.before ? os_file_read(source, out, size, got) : OS_OK;
}
/* edit-zizz round 2: deterministic file changes around the over-capacity
 * comparison. after_replay rewrites the target once the source replayed
 * (before classification); during rewrites it after a comparison read
 * (before the descriptor is revalidated); at_checkpoint rewrites it after
 * install, before the replacement checkpoint and the source's retirement. */
static struct { char path[IPC_PATH_CAP]; const char *after_replay, *during, *at_checkpoint; } zz;
static int zz_write(const char *path, const char *text)
{
    int fd = open(path, O_WRONLY | O_TRUNC); if (fd < 0) return -1;
    ssize_t n = write(fd, text, strlen(text)); close(fd);
    return n == (ssize_t)strlen(text) ? 0 : -1;
}
static int zz_replay_file(const char *path, journal_visit visit, void *ctx, journal_replay_result *result)
{
    int rc = journal_replay_file(path, visit, ctx, result);
    if (zz.after_replay && zz_write(zz.path, zz.after_replay)) return JOURNAL_IO;
    zz.after_replay = NULL; return rc;
}
static os_err zz_pread(os_file *f, void *out, size_t size, uint64_t off, size_t *got)
{
    os_err rc = os_file_pread(f, out, size, off, got);
    if (zz.during && zz_write(zz.path, zz.during)) return OS_E_IO;
    zz.during = NULL; return rc;
}
static int zz_checkpoint(editor *e)
{
    if (zz.at_checkpoint && zz_write(zz.path, zz.at_checkpoint)) return EDITOR_ERR_IO;
    zz.at_checkpoint = NULL; return editor_checkpoint(e);
}
#define os_file_read materialize_read
#define journal_replay_file zz_replay_file
#define os_file_pread zz_pread
#define editor_checkpoint zz_checkpoint
#include "../src/editor/recovery.c"
#undef os_file_read
#undef journal_replay_file
#undef os_file_pread
#undef editor_checkpoint

#define T(c) do { if (!(c)) { fprintf(stderr, "editor_recovery_test:%d FAIL %s\n", __LINE__, #c); rc = 1; goto done; } } while (0)
static int changed_materialization(bool before)
{
    char path[] = "build/editor-fi0p-base-XXXXXX";
    int fd = mkstemp(path), rc = 0;
    recovered_session session = {0};
    T(fd >= 0);
    const char original[] = "original!";
    T(write(fd, original, sizeof original - 1u) == (ssize_t)(sizeof original - 1u));
    T(close(fd) == 0); fd = -1;
    journal_base base;
    T(journal_capture_base(path, &base) == 0);
    /* (P) V2 BASE = size/mtime/inode/device (u64 each), CRC/length (u32
     * each), NUL-terminated path, then ctime (u64). */
    uint8_t payload[sizeof(uint64_t) * 4u + sizeof(uint32_t) * 2u + IPC_PATH_CAP + sizeof(uint64_t)] = {0};
    uint64_t fields[] = {base.size, base.mtime_ns, base.inode, base.device};
    for (size_t i = 0; i < sizeof fields / sizeof *fields; i++)
        for (size_t j = 0; j < sizeof(uint64_t); j++) payload[i * sizeof(uint64_t) + j] = (uint8_t)(fields[i] >> (j * CHAR_BIT));
    uint32_t small[] = {base.prefix_crc, base.prefix_len | JOURNAL_BASE_V2};
    size_t fixed = sizeof fields + sizeof small;
    for (size_t i = 0; i < sizeof small / sizeof *small; i++)
        for (size_t j = 0; j < sizeof(uint32_t); j++) payload[sizeof fields + i * sizeof(uint32_t) + j] = (uint8_t)(small[i] >> (j * CHAR_BIT));
    size_t name = strlen(base.path) + 1u;
    memcpy(payload + fixed, base.path, name);
    for (size_t j = 0; j < sizeof(uint64_t); j++) payload[fixed + name + j] = (uint8_t)(base.ctime_ns >> (j * CHAR_BIT));
    journal_record record = {.type = JOURNAL_BASE, .buffer_id = 1, .data = payload,
        .size = (uint32_t)(fixed + name + sizeof(uint64_t))};
    T(journal_check_base(&base) == 0);
    rewrite.path = path; rewrite.before = before; rewrite.armed = true;
    int restored = restore_record(&session, &record);
    T(!rewrite.armed);
    T(restored == JOURNAL_BASE_CHANGED);
done:
    rewrite.armed = false;
    if (fd >= 0) close(fd);
    fini(&session); unlink(path);
    if (!rc) puts(before ? "editor_recovery_test: rejects BASE rewritten before bulk read" :
        "editor_recovery_test: rejects BASE rewritten during bulk read");
    return rc;
}
/* edit-zizz round 2. (P) Fixture: clean files c0, c1 and file m ("m") with
 * one typed byte ("Mm"): three buffers in tab order c0 c1 m. tab_capacity 2
 * and closed_capacity 1 give three buffer slots, but three live tabs do not
 * fit two live slots, so the over-capacity path runs. Waits use
 * EDITOR_IDLE_NS (G, the existing loop-policy bound). */
#include <sys/wait.h>
#include <dirent.h>
#include <sched.h>
#define Z(c) do { if (!(c)) { fprintf(stderr, "editor_recovery_test:%d FAIL %s\n", __LINE__, #c); return 1; } } while (0)
static int zz_turn(editor *e) { int rc = editor_step(e, 0); return rc == EDITOR_OK || rc == EDITOR_MORE ? 0 : rc; }
static int zz_settle(editor *e)
{
    uint64_t limit = trace_now_ns() + EDITOR_IDLE_NS;
    do { Z(zz_turn(e) == 0); Z(trace_now_ns() < limit); sched_yield(); }
    while (editor_get_stats(e).pending || editor_get_stats(e).render_active || (e->buffer && e->buffer->pending_open));
    return 0;
}
static int zz_child(const char *root, const char *first)
{
    render_backend backend = {0}; Z(render_null_backend(&backend) == 0);
    editor_config config = {.journal_path = first, .start_empty = true, .cols = 32, .rows = 3};
    editor *e = NULL; Z(editor_open(&e, &config, &backend) == 0); Z(zz_settle(e) == 0);
    const char *names[] = {"c0", "c1", "m"};
    for (size_t i = 0; i < sizeof names / sizeof *names; i++) {
        char path[IPC_PATH_CAP]; Z(snprintf(path, sizeof path, "%s/%s", root, names[i]) > 0);
        uint64_t id; Z(editor_add_buffer(e, path, NULL, 0, &id) == 0); Z(zz_settle(e) == 0);
    }
    plat_event key = {.kind = PLAT_EV_KEY, .press = true, .keysym = 'M', .utf8_len = 1, .utf8 = "M", .t0_ns = trace_now_ns()};
    Z(editor_inject(e, &key) == 0); Z(zz_settle(e) == 0);
    Z(journal_flush(e->journal) == 0 && editor_checkpoint(e) == 0);
    return 0; /* the caller _exits: a crash */
}
static int zz_source(char *root, size_t cap, char *first)
{
    char made[] = "build/editor-zizz-race-XXXXXX"; Z(mkdtemp(made)); Z(realpath(made, root) && strlen(root) < cap);
    const char *names[] = {"c0", "c1", "m"};
    for (size_t i = 0; i < sizeof names / sizeof *names; i++) {
        char path[IPC_PATH_CAP]; Z(snprintf(path, sizeof path, "%s/%s", root, names[i]) > 0);
        int fd = open(path, O_CREAT | O_EXCL | O_WRONLY, 0600); Z(fd >= 0);
        Z(write(fd, names[i], strlen(names[i])) == (ssize_t)strlen(names[i])); Z(close(fd) == 0);
    }
    Z(snprintf(first, IPC_PATH_CAP, "%s/session-XXXXXX", root) > 0);
    int fd = mkstemp(first); Z(fd >= 0); close(fd);
    pid_t child = fork(); Z(child >= 0);
    if (!child) _exit(zz_child(root, first));
    int status; Z(waitpid(child, &status, 0) == child); Z(WIFEXITED(status) && WEXITSTATUS(status) == 0);
    Z(snprintf(zz.path, sizeof zz.path, "%s/m", root) > 0);
    return 0;
}
/* Replay the set-aside source's records for m onto its BASE text "m". */
typedef struct zz_text { uint64_t id; char text[sizeof "Mm" + 1]; size_t len; bool bad; } zz_text;
static int zz_visit(void *ctx, const journal_record *r)
{
    zz_text *t = ctx; char path[IPC_PATH_CAP]; journal_base base;
    if (r->type == JOURNAL_BASE && !journal_decode_base(r, &base, path, sizeof path) && !strcmp(path, zz.path)) {
        t->id = r->buffer_id; strcpy(t->text, "m"); t->len = 1;
    } else if (t->id && r->buffer_id == t->id && (r->type == JOURNAL_INSERT || r->type == JOURNAL_DELETE)) {
        uint64_t off = get64(r->data), n = r->type == JOURNAL_INSERT ? r->size - 8u : get64(r->data + 8);
        if (off > t->len || (r->type == JOURNAL_INSERT ? t->len + n >= sizeof t->text : n > t->len - off)) { t->bad = true; return 0; }
        if (r->type == JOURNAL_INSERT) {
            memmove(t->text + off + n, t->text + off, t->len - off); memcpy(t->text + off, r->data + 8, (size_t)n); t->len += (size_t)n;
        } else { memmove(t->text + off, t->text + off + n, t->len - off - n); t->len -= (size_t)n; }
        t->text[t->len] = 0;
    }
    return 0;
}
static void zz_remove(const char *root)
{
    DIR *d = opendir(root); struct dirent *entry;
    if (d) {
        while ((entry = readdir(d))) if (strcmp(entry->d_name, ".") && strcmp(entry->d_name, ".."))
            (void)unlinkat(dirfd(d), entry->d_name, 0);
        closedir(d);
    }
    (void)rmdir(root);
}
static bool zz_has(editor *e, const char *text, bool modified)
{
    for (size_t i = 0; i < tabs_count(&e->tabs); i++) {
        const tabs_tab *t = editor_tab(e, i); uint8_t bytes[sizeof "Mm"]; size_t len = strlen(text);
        if (piece_len(t->buffer) == len && !piece_read(t->buffer, 0, bytes, len) && !memcmp(bytes, text, len) &&
            t->modified == modified) return true;
    }
    return false;
}
/* changed: true rewrites m between the comparison read and the descriptor's
 * revalidation (the file momentarily held the unsaved text); false rewrites it
 * after classification, before retirement. Either way "Mm" must survive: as
 * a restored modified tab, or in the set-aside source. */
static int zz_race(bool during)
{
    char root[IPC_PATH_CAP], first[IPC_PATH_CAP], next[IPC_PATH_CAP];
    memset(&zz, 0, sizeof zz);
    if (zz_source(root, sizeof root, first)) return 1;
    zz.after_replay = "Mm"; /* disk equals the unsaved text at comparison */
    if (during) zz.during = "m"; else zz.at_checkpoint = "m";
    Z(snprintf(next, sizeof next, "%s/session-XXXXXX", root) > 0);
    int fd = mkstemp(next); Z(fd >= 0); close(fd);
    render_backend backend = {0}; Z(render_null_backend(&backend) == 0);
    editor_config config = {.journal_path = next, .start_empty = true, .tab_capacity = 2, .closed_capacity = 1, .cols = 32, .rows = 3};
    editor *e = NULL; Z(editor_open(&e, &config, &backend) == 0 && e);
    zz_text replayed = {0}; journal_replay_result result;
    bool aside = *e->recovery_aside && !journal_scan_file(e->recovery_aside, zz_visit, &replayed, &result) && !replayed.bad;
    bool live = zz_has(e, "Mm", true);
    fprintf(stderr, "editor_recovery_test: zizz race %s: tabs=%zu live_Mm=%d source=%d aside=%d aside_m=%s notice=%s\n",
        during ? "during" : "before-retire", tabs_count(&e->tabs), live, access(first, F_OK) == 0, *e->recovery_aside != 0,
        aside ? replayed.text : "(none)", e->recovery_notice ? e->recovery_notice : "(none)");
    int rc = 0;
    if (!aside || strcmp(replayed.text, "Mm")) rc = 1; /* the set-aside source replays m's unsaved text */
    if (during && !live) rc = 1;  /* revalidation: m ranks unsaved and is restored */
    if (!during && live) rc = 1;  /* classification saw equal bytes: m was dropped */
    editor_close(e); zz_remove(root);
    if (!rc) puts(during ? "editor_recovery_test: zizz a file rewritten during comparison ranks unsaved" :
        "editor_recovery_test: zizz a dropped buffer's text survives in the set-aside source");
    return rc;
}
int main(void)
{
    int rc = changed_materialization(true);
    if (changed_materialization(false)) rc = 1;
    trace_init(); if (trace_thread_register() < 0) return 1;
    if (zz_race(false)) rc = 1;
    if (zz_race(true)) rc = 1;
    return rc;
}
