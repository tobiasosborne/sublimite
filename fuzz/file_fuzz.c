/* P1.7f: scanner + bounded stateful file model. No shared mutable fuzz state. */
#include "file/file.h"
#include "base/base.h"
#include "trace/trace.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define MODEL_CAP 8192u
#define MAX_OPS 32u

typedef struct fuzz_model {
    work_pool pool;
    file *f;
    piece_tree *tree;
    uint8_t bytes[MODEL_CAP], disk[MODEL_CAP];
    size_t len, disk_len;
    char dir[128], path[160], staging[160];
    uint32_t generation;
} fuzz_model;
typedef struct fuzz_completion {
    file *f;
    uint32_t generation;
    file_msg msg;
    int seen;
} fuzz_completion;
typedef struct fuzz_blocker {
    _Atomic int started, release, done;
} fuzz_blocker;
typedef struct fuzz_cancel {
    int stop, observed;
} fuzz_cancel;

static void fuzz_require(int ok)
{
    if (!ok) __builtin_trap();
}
static void fuzz_pause(void)
{
    struct timespec ts = {0, 100000};
    (void)nanosleep(&ts, NULL);
}
static void fuzz_collect(const work_msg *wm, void *ud)
{
    fuzz_completion *c = ud; file_msg m;
    if (file_msg_decode(wm, &m) == FILE_OK && m.f == c->f && m.generation == c->generation) {
        fuzz_require(!c->seen);
        c->msg = m; c->seen = 1;
    }
}
static void fuzz_wait(fuzz_model *m, fuzz_completion *c)
{
    for (unsigned i = 0; i < 100000 && !c->seen; i++) {
        (void)work_mailbox_drain(&m->pool, fuzz_collect, c);
        if (!c->seen) fuzz_pause();
    }
    fuzz_require(c->seen);
}
static void fuzz_discard(const work_msg *wm, void *ud)
{
    (void)wm; (void)ud;
}
static void fuzz_write(const char *path, const uint8_t *bytes, size_t len)
{
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0640);
    fuzz_require(fd >= 0);
    size_t done = 0;
    while (done < len) {
        ssize_t n = write(fd, bytes + done, len - done);
        if (n < 0 && errno == EINTR) continue;
        fuzz_require(n > 0);
        done += (size_t)n;
    }
    fuzz_require(close(fd) == 0);
}
static void fuzz_disk(fuzz_model *m)
{
    int fd = open(m->path, O_RDONLY); struct stat st;
    fuzz_require(fd >= 0 && fstat(fd, &st) == 0 && st.st_size == (off_t)m->disk_len);
    uint8_t got[MODEL_CAP]; size_t done = 0;
    while (done < m->disk_len) {
        ssize_t n = read(fd, got + done, m->disk_len - done);
        if (n < 0 && errno == EINTR) continue;
        fuzz_require(n > 0); done += (size_t)n;
    }
    fuzz_require(memcmp(got, m->disk, m->disk_len) == 0 && close(fd) == 0);
}
static void fuzz_no_temps(fuzz_model *m)
{
    DIR *d = opendir(m->dir); fuzz_require(d != NULL);
    struct dirent *entry;
    while ((entry = readdir(d))) fuzz_require(strstr(entry->d_name, ".edit-") == NULL);
    fuzz_require(closedir(d) == 0);
}
static void fuzz_tree(fuzz_model *m)
{
    uint8_t got[MODEL_CAP];
    fuzz_require(piece_len(m->tree) == m->len && piece_read(m->tree, 0, got, m->len) == PIECE_OK);
    fuzz_require(memcmp(got, m->bytes, m->len) == 0);
}
static void fuzz_open(fuzz_model *m, int mapped)
{
    file_open_opts opts = {.copy_threshold = mapped ? 1 : MODEL_CAP + 1u, .generation = ++m->generation};
    fuzz_require(file_open_begin(&m->pool, m->path, &opts, &m->f) == FILE_OK);
    size_t n = 0; const uint8_t *p = file_prefix(m->f, &n);
    fuzz_require(n == m->disk_len && (n == 0 || (p && memcmp(p, m->disk, n) == 0)));
    fuzz_require(file_size(m->f) == m->disk_len);
    if (!file_open_ready(m->f)) {
        fuzz_completion c = {.f = m->f, .generation = opts.generation};
        fuzz_wait(m, &c);
        fuzz_require(c.msg.kind == FILE_MSG_OPEN_READY && c.msg.status == FILE_OK && c.msg.size == m->disk_len);
    }
    piece_allocator a = piece_default_allocator(); m->tree = piece_create(&a);
    fuzz_require(m->tree && file_attach(m->f, m->tree) == FILE_OK);
    fuzz_require(file_attach(m->f, m->tree) == FILE_ERR_STATE);
    memcpy(m->bytes, m->disk, m->disk_len); m->len = m->disk_len;
    fuzz_tree(m);
}
static void fuzz_insert(fuzz_model *m, uint8_t key, const uint8_t *data, size_t len)
{
    if (len > MODEL_CAP - m->len) len = MODEL_CAP - m->len;
    size_t off = m->len ? key % (m->len + 1u) : 0;
    fuzz_require(piece_insert(m->tree, off, data, len) == PIECE_OK);
    memmove(m->bytes + off + len, m->bytes + off, m->len - off);
    memcpy(m->bytes + off, data, len); m->len += len;
}
static void fuzz_delete(fuzz_model *m, uint8_t key)
{
    if (!m->len) return;
    size_t off = key % m->len, n = 1u + key % 16u;
    if (n > m->len - off) n = m->len - off;
    fuzz_require(piece_delete(m->tree, off, n, NULL) == PIECE_OK);
    memmove(m->bytes + off, m->bytes + off + n, m->len - off - n); m->len -= n;
}
static void fuzz_block(work_ctx *ctx)
{
    fuzz_blocker *b = ctx->arg; atomic_store(&b->started, 1);
    while (!atomic_load(&b->release) && !work_should_stop(ctx)) fuzz_pause();
    atomic_store(&b->done, 1);
}
static void fuzz_save(fuzz_model *m, uint8_t key)
{
    /* Queue deterministically; mutate after ack to verify snapshot isolation. */
    fuzz_blocker b = {0};
    work_handle h = work_submit(&m->pool, (work_job){fuzz_block, &b, 0, WORK_BULK});
    fuzz_require(h.epoch != 0);
    for (unsigned i = 0; i < 100000 && !atomic_load(&b.started); i++) fuzz_pause();
    fuzz_require(atomic_load(&b.started));
    uint32_t gen = ++m->generation;
    fuzz_require(file_save_begin(m->f, m->tree, 0, gen) == FILE_OK);
    fuzz_require(file_save_busy(m->f));
    fuzz_require(file_save_begin(m->f, m->tree, 0, gen + 1u) == FILE_ERR_BUSY);
    memcpy(m->disk, m->bytes, m->len); m->disk_len = m->len;
    fuzz_insert(m, key, &key, 1);
    atomic_store(&b.release, 1);
    fuzz_completion c = {.f = m->f, .generation = gen}; fuzz_wait(m, &c);
    fuzz_require(c.msg.kind == FILE_MSG_SAVE_DONE && c.msg.status == FILE_OK && c.msg.size == m->disk_len);
    for (unsigned i = 0; i < 100000 && file_save_busy(m->f); i++) fuzz_pause();
    fuzz_require(!file_save_busy(m->f) && atomic_load(&b.done));
    fuzz_disk(m); fuzz_no_temps(m); fuzz_tree(m);
}
static void fuzz_external(fuzz_model *m, uint8_t key)
{
    /* Replace the inode, preserving mappings' original bytes. */
    fuzz_write(m->staging, &key, 1); fuzz_require(rename(m->staging, m->path) == 0);
    m->disk[0] = key; m->disk_len = 1;
    uint32_t reasons = 0;
    fuzz_require(file_check(m->f, &reasons) == 1 && (reasons & FILE_CHG_INODE));
    fuzz_require(file_changed(m->f) && file_save_begin(m->f, m->tree, 0, ++m->generation) == FILE_ERR_CHANGED);
    fuzz_tree(m); fuzz_disk(m); fuzz_no_temps(m);
    fuzz_require(file_resolve_keep(m->f) == FILE_OK && !file_changed(m->f));
}
static int fuzz_stop(void *ctx) { return ((fuzz_cancel *)ctx)->stop; }
static void fuzz_step(void *ctx, int step)
{
    fuzz_cancel *c = ctx;
    if (step == FILE_STEP_TEMP_CREATED) { c->observed = 1; c->stop = 1; }
}
static void fuzz_cancel_save(fuzz_model *m)
{
    piece_snapshot *s = piece_snapshot_take(m->tree); fuzz_require(s != NULL);
    fuzz_cancel c = {0};
    file_save_args a = {.path = m->path, .snap = s, .step = fuzz_step, .step_ctx = &c,
        .stop = fuzz_stop, .stop_ctx = &c};
    fuzz_require(file_save_write(&a) == FILE_ERR_CANCELLED && c.observed);
    piece_snapshot_release(s); fuzz_disk(m); fuzz_no_temps(m);
}
static void fuzz_lifetime(fuzz_model *m, int mapped)
{
    piece_snapshot *s = piece_snapshot_take(m->tree); fuzz_require(s != NULL);
    file_close(m->f); m->f = NULL;
    fuzz_tree(m);
    piece_destroy(m->tree); m->tree = NULL;
    uint8_t got[MODEL_CAP];
    fuzz_require(piece_snapshot_len(s) == m->len && piece_snapshot_read(s, 0, got, m->len) == PIECE_OK);
    fuzz_require(memcmp(got, m->bytes, m->len) == 0);
    piece_snapshot_release(s);
    (void)work_mailbox_drain(&m->pool, fuzz_discard, NULL);
    fuzz_open(m, mapped);
}
static void fuzz_scanner(const uint8_t *data, size_t size)
{
    file_prefix_info info; uint64_t lf = 0, crlf = 0, cr = 0;
    file_prefix_scan(data, size, &info);
    for (size_t k = 0; k < size; k++) {
        if (data[k] == '\n') { if (k > 0 && data[k - 1] == '\r') crlf++; else lf++; }
        else if (data[k] == '\r' && (k + 1 == size || data[k + 1] != '\n')) cr++;
    }
    fuzz_require(info.lf == lf && info.crlf == crlf && info.cr == cr);
    file_eol want = (lf + crlf) == 0 ? FILE_EOL_NONE : crlf == 0 ? FILE_EOL_LF : lf == 0 ? FILE_EOL_CRLF : FILE_EOL_MIXED;
    fuzz_require(info.kind == want && info.dominant == (crlf > lf ? FILE_EOL_CRLF : FILE_EOL_LF));
    fuzz_require(info.has_bom == (size >= 3 && memcmp(data, "\xEF\xBB\xBF", 3) == 0));
}
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    fuzz_scanner(data, size);
    fuzz_model m = {0};
    strcpy(m.dir, "/tmp/edit-file-fuzz-XXXXXX"); fuzz_require(mkdtemp(m.dir) != NULL);
    snprintf(m.path, sizeof m.path, "%s/target", m.dir);
    snprintf(m.staging, sizeof m.staging, "%s/staging", m.dir);
    m.disk_len = size < 4096u ? size : 4096u;
    if (m.disk_len) memcpy(m.disk, data, m.disk_len);
    fuzz_write(m.path, m.disk, m.disk_len);
    trace_init(); fuzz_require(work_pool_init(&m.pool, 1, 0) == 0);
    fuzz_open(&m, size && (data[0] & 1u));
    size_t ops = size < MAX_OPS ? size : MAX_OPS;
    for (size_t i = 0; i < ops; i++) {
        uint8_t key = data[i];
        switch (key % 8u) {
        case 0: fuzz_insert(&m, key, data + i, size - i < 16u ? size - i : 16u); break;
        case 1: fuzz_delete(&m, key); break;
        case 2: fuzz_save(&m, key); break;
        case 3: fuzz_external(&m, key); break;
        case 4: fuzz_lifetime(&m, (key & 8u) != 0); break;
        case 5: fuzz_cancel_save(&m); break;
        case 6: {
            piece_snapshot *s = piece_snapshot_take(m.tree); fuzz_require(s != NULL);
            fuzz_require(mkdir(m.staging, 0700) == 0);
            file_save_args a = {.path = m.staging, .snap = s};
            /* Existing directory: temp may be written, rename must fail. */
            fuzz_require(file_save_write(&a) == FILE_ERR_IO && a.err_no != 0);
            piece_snapshot_release(s); fuzz_disk(&m); fuzz_no_temps(&m);
            fuzz_require(rmdir(m.staging) == 0);
            break;
        }
        case 7: {
            file *bad = NULL;
            fuzz_require(file_open_begin(&m.pool, m.dir, NULL, &bad) == FILE_ERR_NOTREG && bad == NULL);
            break;
        }
        }
        fuzz_tree(&m);
    }
    /* Exercise saves even for empty/scanner-only seeds. */
    fuzz_save(&m, 0);
    fuzz_lifetime(&m, 1);
    piece_destroy(m.tree); file_close(m.f);
    work_pool_shutdown(&m.pool);
    fuzz_no_temps(&m);
    fuzz_require(unlink(m.path) == 0 && rmdir(m.dir) == 0);
    return 0;
}
