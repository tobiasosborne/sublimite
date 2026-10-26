/* file.c -- P1.7 open / change detection / durable atomic save. See file.h. */
#include "file/file_internal.h"
#include "file/file_test.h"
#include "base/base.h"

#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

#define FILE_COPY_CAP     (256ull << 20)
#define FILE_PREFETCH_MAX (64ull << 20)     /* madvise(WILLNEED) window of a mapping */
#define FILE_READ_CHUNK   (4u << 20)
#define FILE_WRITE_CHUNK  (256u << 10)
#define FILE_NAME_MAX_KEEP 100

static uint64_t guard_faults(int slot) { return os_fault_epoch(slot); }
static int guard_faulted(int slot) { return os_fault_epoch(slot) != 0; }

/* ---------------- mapping (refcounted; the piece tree/snapshots hold refs) --- */
static void map_acquire(void *ctx) { atomic_fetch_add_explicit(&((file_map *)ctx)->refs, 1, memory_order_relaxed); }
static void map_release(void *ctx)
{
    file_map *m = ctx;
    if (atomic_fetch_sub_explicit(&m->refs, 1, memory_order_acq_rel) == 1) {
        os_fault_retire(m->slot);
        if (m->private_copy) free(m->addr);
        else os_unmap(&m->view);
        if (os_file_is_open(&m->fd)) os_file_close(&m->fd);
        free(m);
    }
}

file_backing *file_snapshot_backing(const piece_snapshot *snapshot)
{
    const piece_map_hooks *hooks = piece_snapshot_mapping(snapshot);
    return hooks && hooks->acquire == map_acquire && hooks->release == map_release ? hooks->ctx : NULL;
}
void file_backing_acquire(file_backing *backing) { if (backing) map_acquire(backing); }
void file_backing_release(file_backing *backing) { if (backing) map_release(backing); }
int file_backing_faulted(const file_backing *backing)
{ return backing && guard_faulted(backing->slot); }
int file_snapshot_faulted(const piece_snapshot *snapshot)
{ return file_backing_faulted(file_snapshot_backing(snapshot)); }
int file_snapshot_bytes(const piece_snapshot *snapshot, const work_ctx *control,
                        const uint8_t **bytes, size_t *length)
{
    if (bytes) *bytes = NULL;
    if (length) *length = 0;
    if (!bytes || !length || (control && work_should_stop(control))) return 0;
    file_backing *backing = file_snapshot_backing(snapshot);
    if (!backing || file_backing_faulted(backing) || piece_snapshot_len(snapshot) != backing->len) return 0;
    piece_iter it; piece_iter_begin_snapshot(&it, snapshot, 0);
    const uint8_t *p; size_t n, off = 0;
    while (piece_iter_next(&it, &p, &n)) {
        if (control && work_should_stop(control)) return 0;
        if (n > backing->len - off || p != (const uint8_t *)backing->addr + off) return 0;
        off += n;
    }
    if (off != backing->len || file_backing_faulted(backing) || (control && work_should_stop(control))) return 0;
    *bytes = backing->addr; *length = backing->len; return 1;
}
struct file_source {
    _Atomic size_t refs;
    file_map *map;
    file_id identity;
    file_mode mode;
    uint64_t faults;
};
file_source *file_source_retain(file_source *source)
{
    if (source) (void)atomic_fetch_add_explicit(&source->refs,1,memory_order_relaxed);
    return source;
}
void file_source_release(file_source *source)
{
    if (source && atomic_fetch_sub_explicit(&source->refs,1,memory_order_acq_rel)==1) {
        if (source->map) map_release(source->map);
        free(source);
    }
}
const file_id *file_source_identity(const file_source *source) { return &source->identity; }
file_mode file_source_mode(const file_source *source) { return source->mode; }
int file_source_validate(void *ctx)
{
    file_source *source=ctx;
    if (!source) return FILE_ERR_STATE;
    if (source->mode==FILE_MODE_COPY) return FILE_OK;
    file_map *mapping=source->map;
    if (!mapping || guard_faulted(mapping->slot) || guard_faults(mapping->slot)!=source->faults)
        return FILE_ERR_CHANGED;
    os_stat st;
    if (os_file_stat(&mapping->fd, &st)) return FILE_ERR_IO;
    file_id observed; file_id_from_stat(&observed,&st);
    return file_id_diff(&source->identity,&observed)==FILE_CHG_NONE ? FILE_OK : FILE_ERR_CHANGED;
}
/* edit-fkpk (recovery-3 F5): our own rename over the pathname drops the
 * backing inode's link and so advances its ctime. Adopt exactly that change:
 * same inode, size, mtime, permission bits and owner; unchanged fault epoch;
 * the pathname now names a different inode (the replacement). Anything else
 * leaves the authorization untouched, so validation keeps refusing. */
int file_source_adopt_retired(file_source *source, const file_id *replacement)
{
    if (!source || !replacement) return FILE_ERR_STATE;
    if (source->mode==FILE_MODE_COPY) return FILE_OK;
    file_map *mapping=source->map;
    if (!mapping || guard_faulted(mapping->slot) || guard_faults(mapping->slot)!=source->faults)
        return FILE_ERR_CHANGED;
    if (!replacement->exists ||
        (replacement->dev==source->identity.dev && replacement->ino==source->identity.ino &&
         replacement->ino_hi==source->identity.ino_hi))
        return FILE_ERR_CHANGED;
    os_stat st;
    if (os_file_stat(&mapping->fd, &st)) return FILE_ERR_IO;
    file_id observed; file_id_from_stat(&observed,&st);
    uint32_t diff=file_id_diff(&source->identity,&observed);
    if (diff==FILE_CHG_NONE) return FILE_OK;
    if (diff!=FILE_CHG_METADATA || !source->identity.metadata_valid ||
        observed.mode!=source->identity.mode || observed.uid!=source->identity.uid ||
        observed.gid!=source->identity.gid) return FILE_ERR_CHANGED;
    source->identity.ctime_ns=observed.ctime_ns;
    return FILE_OK;
}

/* Worker-owned physical transaction. UI passes it from preparation to commit
 * only after receiving SAVE_PREPARED; no descriptor operation is done by decode. */
static void transaction_discard(save_transaction *tx);

/* ---------------- file object ---------------- */
static void file_sync(file *f, uint32_t kind);
static void install_replaced(file *f);
static void open_job(work_ctx *c);
static void save_commit_job(work_ctx *c);
static void save_commit_pump(file *f);
static void invalidate_save(file *f);
static void save_abort_pump(file *f);

uint64_t file_default_copy_threshold(void)
{
    uint64_t ram = os_phys_mem_bytes(), lim = FILE_COPY_CAP;
    if (ram && ram / 32u < lim) lim = ram / 32u;
    return lim;
}

/* ---------------- prefix scan ---------------- */
void file_prefix_scan(const uint8_t *p, size_t n, file_prefix_info *out)
{
    uint64_t lf_total = 0, crlf = 0, cr_total = 0;
    const uint8_t *cur = p, *end = p + n;
    while (cur < end) {
        const uint8_t *q = memchr(cur, '\n', (size_t)(end - cur));
        if (q == NULL) break;
        lf_total++;
        if (q > p && q[-1] == '\r') crlf++;
        cur = q + 1;
    }
    for (size_t i = 0; i < n; i++) cr_total += (p[i] == '\r');
    out->crlf = crlf;
    out->lf = lf_total - crlf;
    out->cr = cr_total - crlf;
    out->kind = lf_total == 0 ? FILE_EOL_NONE
              : crlf == 0 ? FILE_EOL_LF
              : out->lf == 0 ? FILE_EOL_CRLF : FILE_EOL_MIXED;
    out->dominant = crlf > out->lf ? FILE_EOL_CRLF : FILE_EOL_LF;
    out->has_bom = n >= 3 && p[0] == 0xEF && p[1] == 0xBB && p[2] == 0xBF;
}

/* ---------------- messages ---------------- */
typedef struct msg_payload {
    file *f;
    int32_t status;
    uint32_t mode;
    uint64_t size;
    int32_t err_no;
} msg_payload;

static bool post(work_ctx *c, uint32_t kind, file *f, int status, uint32_t mode,
                 uint64_t size, int err_no, uint32_t gen)
{
    work_msg m;
    msg_payload pl = { f, status, mode, size, err_no };
    memset(&m, 0, sizeof m);
    m.kind = kind;
    m.generation = gen;
    memcpy(m.data, &pl, sizeof pl);
    /* Terminal/phase messages transfer ownership. A full mailbox is
     * backpressure, never permission to lose that transfer. Cancellation
     * invalidates the delivery and lets close's physical wait make progress. */
    while (!work_should_stop(c)) {
        if (work_publish(c, &m)) return true;
        os_sleep_ns(1000000);
    }
    return false;
}

int file_msg_decode(const work_msg *m, file_msg *out)
{
    msg_payload pl;
    if (m->kind != FILE_MSG_OPEN_READY && m->kind != FILE_MSG_OPEN_FAILED &&
        m->kind != FILE_MSG_SAVE_DONE && m->kind != FILE_MSG_PREFIX_READY &&
        m->kind != FILE_MSG_CHECK_DONE && m->kind != FILE_MSG_REPLACED &&
        m->kind != FILE_MSG_SAVE_PREPARED)
        return 1;
    memcpy(&pl, m->data, sizeof pl);
    /* Decode can be deferred beyond ordinary delivery. Validate again at the
     * adoption boundary: a saved message is not a perpetual lease. */
    if (!pl.f || m->slot_ >= WORK_MAX_JOBS) return 1;
    file *f = pl.f;
    work_handle h = f->jobs[m->slot_];
    if (!h.epoch || h.slot != m->slot_ || h.epoch != m->epoch_ ||
        atomic_load_explicit(&f->pool->slots[h.slot].epoch, memory_order_acquire) != h.epoch ||
        m->generation != f->pool->slots[h.slot].job.generation) return 1;
    out->kind = m->kind;
    out->generation = m->generation;
    out->f = pl.f;
    out->status = pl.status;
    out->mode = pl.mode;
    out->size = pl.size;
    out->err_no = pl.err_no;
    if (pl.f) file_sync(pl.f, m->kind);
    if (m->kind == FILE_MSG_CHECK_DONE || m->kind == FILE_MSG_REPLACED ||
        m->kind == FILE_MSG_SAVE_PREPARED) return 1;
    if (m->kind == FILE_MSG_PREFIX_READY && pl.f->open_status != FILE_OK) {
        out->kind = FILE_MSG_OPEN_FAILED;
        out->status = pl.f->open_status;
    }
    return 0;
}

/* ---------------- identity ---------------- */
void file_id_from_stat(file_id *id, const os_stat *st)
{
    id->dev = st->id.volume;
    id->ino = st->id.index[0];
    id->ino_hi = st->id.index[1];
    id->size = st->size;
    id->mtime_ns = (uint64_t)st->mtime_ns;
    id->mode = st->perm;
    id->exists = 1;
    id->ctime_ns = (uint64_t)st->ctime_ns;
    id->uid = st->uid; id->gid = st->gid;
    id->metadata_valid = 1;
    id->type = st->kind == OS_KIND_FILE ? FILE_TYPE_REG : st->kind == OS_KIND_DIR ? FILE_TYPE_DIR
             : st->kind == OS_KIND_LINK ? FILE_TYPE_LINK : FILE_TYPE_OTHER;
}

uint32_t file_id_diff(const file_id *a, const file_id *b)
{
    uint32_t r = 0;
    if (!b->exists) return a->exists ? FILE_CHG_GONE : FILE_CHG_NONE;
    if (!a->exists) return FILE_CHG_INODE;
    if (a->ino != b->ino || a->ino_hi != b->ino_hi || a->dev != b->dev) r |= FILE_CHG_INODE;
    if (a->size != b->size) r |= FILE_CHG_SIZE;
    if (a->mtime_ns != b->mtime_ns) r |= FILE_CHG_MTIME;
    if (a->metadata_valid && b->metadata_valid && a->type != b->type) r |= FILE_CHG_TYPE;
    if (a->mode != b->mode || (a->metadata_valid && b->metadata_valid &&
        (a->uid != b->uid || a->gid != b->gid || a->ctime_ns != b->ctime_ns))) r |= FILE_CHG_METADATA;
    return r;
}

int file_id_stat_path(const char *path, file_id *id)
{
    os_stat st;
    os_err result = os_stat_path(path, OS_STAT_NOFOLLOW, &st);
    if (result == OS_OK) { file_id_from_stat(id, &st); return FILE_OK; }
    if (result == OS_E_NOENT || result == OS_E_NOTDIR) {
        memset(id, 0, sizeof *id); return FILE_OK;
    }
    return FILE_ERR_IO; /* Never turn EACCES/EIO/ELOOP into an accepted absence. */
}

/* Acquisition is optimistic: validate descriptor AND name around every phase.
 * ctime catches rewrites even if a writer restores mtime. No filesystem offers
 * an atomic stat/read transaction; the final-validation-to-publication race
 * remains, and mapped originals remain externally mutable (P1.7c s5). */
static int acquisition_validate(const os_file *fd, const char *path, const file_id *expected, int *en)
{
    os_stat st;
    file_id now;
    if (os_file_stat(fd, &st) != 0) { *en = os_native_error(); return FILE_ERR_IO; }
    file_id_from_stat(&now, &st);
    if (file_id_diff(expected, &now) || expected->ctime_ns != now.ctime_ns) return FILE_ERR_CHANGED;
    os_err result = os_stat_path(path, OS_STAT_NOFOLLOW, &st);
    if (result != OS_OK) {
        if (result == OS_E_NOENT || result == OS_E_NOTDIR) return FILE_ERR_CHANGED;
        *en = os_native_error(); return FILE_ERR_IO;
    }
    file_id_from_stat(&now, &st);
    if (!(st.kind == OS_KIND_FILE) || file_id_diff(expected, &now) || expected->ctime_ns != now.ctime_ns)
        return FILE_ERR_CHANGED;
    return FILE_OK;
}

/* ---------------- open ---------------- */
static void prefix_job(work_ctx *c)
{
    file *f = c->arg;
    os_stat st;
    os_file fd = OS_FILE_INIT;
    int rc = FILE_OK, en = 0;
    uint8_t *bytes = NULL;
    if (work_should_stop(c)) { rc = FILE_ERR_CANCELLED; goto done; }
    if (os_path_canonical(f->requested_path, f->prefix_result.path, sizeof f->prefix_result.path) != OS_OK) {
        en = os_native_error(); rc = FILE_ERR_IO; goto done;
    }
    /* NONBLOCK is required before fstat: FIFOs must not await a writer. */
    os_err opened = os_file_open(&fd, f->prefix_result.path,
                                 OS_OPEN_READ | OS_OPEN_NONBLOCK | OS_OPEN_NOFOLLOW);
    if (opened != OS_OK) {
        en = os_native_error();
        rc = opened == OS_E_ISDIR ? FILE_ERR_NOTREG
           : opened == OS_E_NOENT || opened == OS_E_NOTDIR || opened == OS_E_LOOP
           ? FILE_ERR_CHANGED : FILE_ERR_IO;
        goto done;
    }
    if (os_file_stat(&fd, &st) != 0) { en = os_native_error(); rc = FILE_ERR_IO; goto done; }
    if (!(st.kind == OS_KIND_FILE)) { rc = FILE_ERR_NOTREG; goto done; }
    if (st.size > SIZE_MAX) { en = file_native_error(OS_E_RANGE); rc = FILE_ERR_IO; goto done; }
    file_id_from_stat(&f->prefix_result.id, &st);
    uint64_t threshold = f->threshold ? f->threshold : file_default_copy_threshold();
    f->prefix_result.mode = ((uint64_t)st.size < threshold || st.size == 0)
                           ? FILE_MODE_COPY : FILE_MODE_MMAP;
    rc = acquisition_validate(&fd, f->prefix_result.path, &f->prefix_result.id, &en);
    if (rc != FILE_OK) goto done;
    size_t want = (uint64_t)st.size < FILE_PREFIX_MAX ? (size_t)st.size : FILE_PREFIX_MAX;
    /* COPY owns one allocation from acquisition through tree/snapshot release.
     * The published prefix aliases its head; BULK fills only the suffix. */
    size_t capacity = f->prefix_result.mode == FILE_MODE_COPY ? (size_t)st.size : want;
    bytes = malloc(capacity ? capacity : 1);
    if (!bytes) { rc = FILE_ERR_NOMEM; goto done; }
    size_t off = 0;
    while (off < want) {
        if (work_should_stop(c)) { rc = FILE_ERR_CANCELLED; goto done; }
        /* Prefix acquisition/scanning is off the UI; cancellation is checked
         * even on short reads and repeated EINTR. */
        size_t n = want - off;
        if (n > 65536u) n = 65536u;
        size_t got;
        os_err read_result = file_pread_once(&fd, bytes + off, n, off, &got);
        if (read_result == OS_E_INTR) continue;
        if (read_result != OS_OK) {
            en = os_native_error(); rc = FILE_ERR_IO; goto done;
        }
        if (got == 0) { rc = FILE_ERR_CHANGED; goto done; }
        off += (size_t)got;
    }
    f->prefix_result.len = off;
    file_prefix_info info = {0};
    for (size_t scanned = 0; scanned < off;) {
        if (work_should_stop(c)) { rc = FILE_ERR_CANCELLED; goto done; }
        size_t n = off-scanned < 16384u ? off-scanned : 16384u;
        file_prefix_info chunk; file_prefix_scan(bytes+scanned, n, &chunk);
        info.lf += chunk.lf; info.crlf += chunk.crlf; info.cr += chunk.cr;
        if (scanned && bytes[scanned-1] == '\r' && bytes[scanned] == '\n') {
            info.lf--; info.crlf++; info.cr--;
        }
        scanned += n;
    }
    info.kind = info.lf+info.crlf == 0 ? FILE_EOL_NONE : info.crlf == 0 ? FILE_EOL_LF
                : info.lf == 0 ? FILE_EOL_CRLF : FILE_EOL_MIXED;
    info.dominant = info.crlf > info.lf ? FILE_EOL_CRLF : FILE_EOL_LF;
    info.has_bom = off >= 3 && bytes[0] == 0xEF && bytes[1] == 0xBB && bytes[2] == 0xBF;
    f->prefix_result.info = info;
    rc = acquisition_validate(&fd, f->prefix_result.path, &f->prefix_result.id, &en);
done:
    if (rc != FILE_OK) {
        os_file_close(&fd);
        fd = (os_file)OS_FILE_INIT; free(bytes); bytes = NULL;
        f->prefix_result.len = 0;
    }
    f->prefix_result.fd = fd;
    f->prefix_result.bytes = bytes;
    f->prefix_result.status = rc;
    f->prefix_result.err_no = en;
    atomic_store_explicit(&f->prefix_result_ready, 1, memory_order_release);
    uint32_t kind = rc == FILE_OK ? FILE_MSG_PREFIX_READY : FILE_MSG_OPEN_FAILED;
    (void)post(c, kind, f, rc, (uint32_t)f->prefix_result.mode,
               f->prefix_result.len, en, f->generation);
    if (rc == FILE_OK && f->prefix_result.mode == FILE_MODE_COPY &&
        f->prefix_result.id.size <= f->prefix_result.len)
        (void)post(c, FILE_MSG_OPEN_READY, f, FILE_OK, FILE_MODE_COPY,
                   f->prefix_result.len, 0, f->generation);
    atomic_store_explicit(&f->prefix_fin, 1, memory_order_release);
}

static void open_job(work_ctx *c)
{
    file *f = c->arg;
    int status = 0, en = 0;
    uint64_t avail = f->size;
    status = acquisition_validate(&f->fd, f->path, &f->open_identity, &en);
    if (status != FILE_OK) goto done;

    if (f->mode == FILE_MODE_MMAP) {
        file_map *m = calloc(1, sizeof *m);
        if (m == NULL) { status = FILE_ERR_NOMEM; goto done; }
        m->fd = (os_file)OS_FILE_INIT;
        m->slot = -1;
        m->len = (size_t)f->size;
        if (f->before_map) f->before_map(f->before_map_ctx);
        os_err mapped = os_file_dup(&m->fd, &f->fd);
        if (mapped == OS_OK) mapped = os_map_file(&m->view, &m->fd, 0, m->len);
        if (mapped != OS_OK) {
            en = os_native_error(); status = mapped == OS_E_RANGE ? FILE_ERR_CHANGED : FILE_ERR_IO;
            os_file_close(&m->fd); free(m); goto done;
        }
        m->addr = (void *)m->view.addr;
        if (os_fault_register(&m->view, &m->slot) != OS_OK) {
            en = os_native_error();
            os_unmap(&m->view); os_file_close(&m->fd); free(m);
            status = FILE_ERR_NOMEM; goto done;
        }
        atomic_init(&m->refs, 1);
        m->identity = f->open_identity;
        f->open_result.map = m;
        size_t pf = m->len < FILE_PREFETCH_MAX ? m->len : (size_t)FILE_PREFETCH_MAX;
        os_map_prefetch(&m->view, 0, pf);
    } else {
        uint8_t *buf = f->prefix;
        size_t off = f->prefix_len;
        while (off < f->size) {
            size_t want = (size_t)f->size - off;
            if (want > FILE_READ_CHUNK) want = FILE_READ_CHUNK;
            if (work_should_stop(c)) { status = FILE_ERR_CANCELLED; goto done; }
            size_t r;
            os_err read_result = file_pread_once(&f->fd, buf + off, want, off, &r);
            if (read_result == OS_E_INTR) continue;
            if (read_result != OS_OK) {
                en = os_native_error(); status = FILE_ERR_IO; break;
            }
            if (r == 0) { status = FILE_ERR_CHANGED; break; }
            off += (size_t)r;
        }
        if (status != 0) goto done;
        avail = off;
    }
    status = acquisition_validate(&f->fd, f->path, &f->open_identity, &en);
done:
    if (status != FILE_OK && f->open_result.map) {
        map_release(f->open_result.map); f->open_result.map = NULL;
    }
    f->open_result.err_no = en;
    f->open_result.status = status;
    atomic_store_explicit(&f->open_result_ready, 1, memory_order_release);
    if (status == 0)
        (void)post(c, FILE_MSG_OPEN_READY, f, 0, (uint32_t)f->mode, avail, 0, f->generation);
    else
        (void)post(c, FILE_MSG_OPEN_FAILED, f, status, (uint32_t)f->mode, 0, en, f->generation);
    atomic_store_explicit(&f->open_fin, 1, memory_order_release);
}

int file_test_open_begin(work_pool *pool, const char *path, const file_open_opts *opts,
                         void (*before_map)(void *), void *hook_ctx, file **out)
{
    *out = NULL;
    if (!pool || !path) return FILE_ERR_STATE;
    size_t len = strnlen(path, OS_PATH_MAX);
    if (len >= OS_PATH_MAX) { (void)file_native_error(OS_E_NAMETOOLONG); return FILE_ERR_IO; }
    file *f = calloc(1, sizeof *f);
    if (f == NULL) return FILE_ERR_NOMEM;
    f->pool = pool;
    f->before_map = before_map; f->before_map_ctx = hook_ctx;
    memcpy(f->requested_path, path, len + 1);
    f->fd = (os_file)OS_FILE_INIT; f->prefix_result.fd = (os_file)OS_FILE_INIT;
    f->prefix_owned = 1;
    f->ifd = -1; f->iwd = -1; f->watch_slot = SIZE_MAX;
    f->generation = opts ? opts->generation : 0;
    f->threshold = opts ? opts->copy_threshold : 0;
    f->transaction.fd = (os_file)OS_FILE_INIT; f->transaction.dfd = (os_dir)OS_DIR_INIT; f->transaction.metadata_fd = (os_file)OS_FILE_INIT;
    atomic_init(&f->ready, 0);
    atomic_init(&f->open_fin, 1);
    atomic_init(&f->prefix_fin, 0);
    atomic_init(&f->prefix_result_ready, 0);
    atomic_init(&f->check_fin, 1);
    atomic_init(&f->check_result_ready, 0);
    f->check_installed = 1;
    atomic_init(&f->save_fin, 1);
    atomic_init(&f->prepare_fin, 1);
    atomic_init(&f->abort_fin, 1);
    atomic_init(&f->open_result_ready, 0);
    atomic_init(&f->save_result_ready, 0);
    atomic_init(&f->save_replaced_ready, 0);
    f->open_installed = 1;
    f->save_installed = 1;
    f->save_replaced_installed = 1;
    /* An existing raster worker provides the interactive lane; configurations
     * without one still use asynchronous BULK, but cannot promise G5 under
     * unrelated bulk I/O. A dedicated work class is a separate proposal. */
    work_class cls = pool->n_workers > pool->n_bulk ? WORK_RASTER : WORK_BULK;
    f->prefix_h = work_submit(pool, (work_job){ prefix_job, f, f->generation, cls });
    if (!f->prefix_h.epoch) {
        free(f); return FILE_ERR_POOL;
    }
    f->jobs[f->prefix_h.slot] = f->prefix_h;
    *out = f;
    return FILE_OK;
}

int file_open_begin(work_pool *pool, const char *path, const file_open_opts *opts, file **out)
{
    return file_test_open_begin(pool, path, opts, NULL, NULL, out);
}

/* Wait until the slot's job for `h` has finished or was skipped. */
static void wait_job(file *f, work_handle h, _Atomic int *fin)
{
    if (h.epoch == 0) return;
    work_cancel(f->pool, h);
    for (;;) {
        if (atomic_load_explicit(fin, memory_order_acquire)) return;
        const work_slot *s = &f->pool->slots[h.slot];
        if (!atomic_load_explicit(&s->busy, memory_order_acquire)) return;
        uint32_t e = atomic_load_explicit(&s->epoch, memory_order_acquire);
        if (e != h.epoch && e != h.epoch + 1u) return;     /* slot reused */
        os_sleep_ns(50000);
    }
}

int file_close_ready(file *f)
{
    if (f == NULL) return 1;
    if (!f->save_installed || f->commit_pending || f->abort_pending ||
        !atomic_load_explicit(&f->save_fin, memory_order_acquire) ||
        !atomic_load_explicit(&f->prepare_fin, memory_order_acquire) ||
        !atomic_load_explicit(&f->abort_fin, memory_order_acquire)) return 0;
    work_cancel(f->pool, f->prefix_h);
    work_cancel(f->pool, f->open_h);
    work_cancel(f->pool, f->check_h);
    for (uint32_t i = 0; i < WORK_MAX_JOBS; i++)
        if (f->jobs[i].epoch && !work_handle_finished(f->pool, f->jobs[i])) return 0;
    /* Named handles may be older than jobs[] entries for a reused slot; a
     * finished handle is what makes each wait_job in file_close immediate. */
    return work_handle_finished(f->pool, f->prefix_h) && work_handle_finished(f->pool, f->open_h) &&
           work_handle_finished(f->pool, f->check_h) && work_handle_finished(f->pool, f->prepare_h) &&
           work_handle_finished(f->pool, f->save_h) && work_handle_finished(f->pool, f->abort_h);
}

void file_close(file *f)
{
    if (f == NULL) return;
    /* Completed slots remain reserved while messages are pending. Remember
     * each slot's handle, so every completion carrying f is invalidated. */
    for (uint32_t i = 0; i < WORK_MAX_JOBS; i++)
        if (f->jobs[i].epoch) work_cancel(f->pool, f->jobs[i]);
    wait_job(f, f->prefix_h, &f->prefix_fin);
    wait_job(f, f->open_h, &f->open_fin);
    wait_job(f, f->prepare_h, &f->prepare_fin);
    wait_job(f, f->save_h, &f->save_fin);
    wait_job(f, f->abort_h, &f->abort_fin);
    wait_job(f, f->check_h, &f->check_fin);
    if (f->save.snap) piece_snapshot_release(f->save.snap);
    transaction_discard(&f->transaction);
    if (os_file_is_open(&f->fd)) os_file_close(&f->fd);
    file_watch_detach(f);
    if (f->map) map_release(f->map);
    if (!f->open_installed && atomic_load_explicit(&f->open_result_ready, memory_order_acquire)) {
        if (f->open_result.map) map_release(f->open_result.map);
    }
    if (f->prefix_owned) free(f->prefix);
    if (!f->prefix_installed && atomic_load_explicit(&f->prefix_result_ready, memory_order_acquire)) {
        if (os_file_is_open(&f->prefix_result.fd)) os_file_close(&f->prefix_result.fd);
        free(f->prefix_result.bytes);
    }
    free(f);
}

const uint8_t *file_prefix(const file *f, size_t *len) { *len = f->prefix_len; return f->prefix; }
const file_prefix_info *file_prefix_info_of(const file *f) { return &f->info; }
uint64_t file_size(const file *f) { return f->size; }
file_mode file_open_mode(const file *f) { return f->mode; }
const file_id *file_prefix_identity(const file *f)
{ return f && file_prefix_ready(f) ? &f->open_identity : NULL; }
const char *file_path(const file *f) { return f->path; }
int file_prefix_ready(const file *f) { return f->prefix_installed && f->open_status == FILE_OK; }
int file_errno(const file *f) { return f->err_no; }
int file_open_ready(const file *f)
{
    return atomic_load_explicit(&((file *)f)->ready, memory_order_acquire);
}

int file_attach(file *f, piece_tree *t)
{
    int rc;
    if (f->attached || !file_open_ready(f)) return FILE_ERR_STATE;
    if (f->mode == FILE_MODE_COPY && !f->map) {
        file_map *m = calloc(1, sizeof *m);
        if (!m) return FILE_ERR_NOMEM;
        atomic_init(&m->refs, 1); m->fd = (os_file)OS_FILE_INIT; m->slot = -1; m->private_copy = 1;
        m->addr = f->prefix; m->len = (size_t)f->size; f->prefix_owned = 0;
        f->map = m;
    }
    piece_map_hooks h = { f->map, map_acquire, map_release };
    rc = piece_init_mapped(t, (const uint8_t *)f->map->addr, f->map->len, &h);
    if (rc != 0) return FILE_ERR_NOMEM;
    f->attached = 1;
    /* The full mapping now owns the prefix bytes. Retire the acquisition copy
     * only after successful tree handoff; prefix queries borrow the mapping. */
    if (f->mode == FILE_MODE_MMAP && f->prefix_owned) {
        free(f->prefix);
        f->prefix = f->map->addr;
        f->prefix_owned = 0;
    }
    /* Source fd is released at off-path close; attachment performs no I/O. */
    return FILE_OK;
}

int file_source_acquire(file *f, file_source **out)
{
    if (!out) return FILE_ERR_STATE;
    *out=NULL;
    if (!f || !file_open_ready(f) || !f->attached || !f->map) return FILE_ERR_STATE;
    file_source *source=calloc(1,sizeof *source);
    if (!source) return FILE_ERR_NOMEM;
    atomic_init(&source->refs,1);
    source->mode=f->mode; source->map=f->map; map_acquire(source->map);
    source->identity=f->mode==FILE_MODE_MMAP ? f->map->identity : f->open_identity;
    source->faults=guard_faults(f->map->slot);
    *out=source; return FILE_OK;
}

/* ---------------- change detection ---------------- */
static uint32_t map_change(file_map *m, int *err_no, file_id *version)
{
    uint32_t r = 0;
    if (version) memset(version, 0, sizeof *version);
    if (m && os_file_is_open(&m->fd)) {
        os_stat st;
        if (os_file_stat(&m->fd, &st) == 0) {
            file_id now; file_id_from_stat(&now, &st);
            if (version) *version = now;
            r = file_id_diff(&m->identity, &now) & ~FILE_CHG_METADATA;
            if ((uint64_t)st.size < (uint64_t)m->len) r |= FILE_CHG_TRUNCATED;
        } else *err_no = os_native_error();
    }
    if (m && guard_faulted(m->slot)) r |= FILE_CHG_TRUNCATED;
    return r;
}

static uint32_t check_now(file *f)
{
    file_id now;
    uint32_t r;
    int en = 0;
    if (file_id_stat_path(f->path, &now) != FILE_OK) {
        f->err_no = os_native_error(); return FILE_CHG_NONE;
    }
    r = file_id_diff(&f->id, &now);
    r |= map_change(f->map, &en, NULL);
    if (en) f->err_no = en;
    if (r && !f->changed) {
        f->changed = 1; f->source_generation++; invalidate_save(f);
    }
    return r;
}

int file_check(file *f, uint32_t *reasons)
{
    uint32_t r;
    if (!f->prefix_installed || f->open_status != FILE_OK) {
        if (reasons) *reasons = 0;
        return file_changed(f);
    }
    r = check_now(f);
    if (reasons) *reasons = r;
    return file_changed(f);
}

int file_changed(const file *f)
{
    file *m = (file *)f;
    int c;
    c = m->changed || (m->map && guard_faulted(m->map->slot));
    return c;
}

int file_resolve_keep(file *f)
{
    file_id now;
    int en = 0;
    if (!file_prefix_ready(f)) return FILE_ERR_STATE;
    if (file_id_stat_path(f->path, &now) != FILE_OK) { f->err_no = os_native_error(); return FILE_ERR_IO; }
    if (now.exists && now.type != FILE_TYPE_REG) return FILE_ERR_CHANGED;
    file_id backing;
    uint32_t backing_reasons = map_change(f->map, &en, &backing);
    if (en) { f->err_no = en; return FILE_ERR_IO; }
    /* A writer can restore mtime after changing bytes/newline counts. When
     * the accepted pathname still names the mapped inode, ctime/metadata is
     * an unvalidated backing version, even if size and mtime compare equal.
     * Retired inodes acquire a new ctime from unlink/rename-over; those may
     * still be kept while their actual bytes and fault epoch remain intact. */
    if (f->map && backing.exists && now.exists && now.dev == backing.dev && now.ino == backing.ino && now.ino_hi == backing.ino_hi)
        backing_reasons |= file_id_diff(&f->map->identity, &backing) & FILE_CHG_METADATA;
    if (backing_reasons) {
        if (!f->changed) { f->source_generation++; invalidate_save(f); }
        f->changed = 1;
        return FILE_ERR_CHANGED;
    }
    if (f->ifd >= 0) {
        f->watch_refresh = 1;
        if (file_watch_start(f) < 0) { f->err_no = os_native_error(); return FILE_ERR_IO; }
    }
    if (!now.exists) now.mode = f->id.mode; /* recreation retains last accepted mode */
    f->id = now;
    f->source_generation++;
    f->changed = 0;
    invalidate_save(f);
    return FILE_OK;
}

static void check_job(work_ctx *c)
{
    file *f = c->arg;
    f->check_result.status = file_id_stat_path(f->path, &f->check_result.observed);
    f->check_result.err_no = f->check_result.status == FILE_OK ? 0 : os_native_error();
    int en = 0;
    f->check_result.map_reasons = map_change(f->check.map, &en, NULL);
    if (en && !f->check_result.err_no) f->check_result.err_no = en;
    f->check_result.generation = f->check.generation;
    atomic_store_explicit(&f->check_result_ready, 1, memory_order_release);
    (void)post(c, FILE_MSG_CHECK_DONE, f, FILE_OK, 0, 0, 0, f->generation);
    atomic_store_explicit(&f->check_fin, 1, memory_order_release);
}

void file_watch_check_pump(file *f)
{
    if (f->check_requested && f->check_installed &&
        atomic_load_explicit(&f->check_fin, memory_order_acquire) && file_prefix_ready(f)) {
        f->check.map = f->map;
        f->check.generation = f->source_generation;
        f->check_installed = 0;
        atomic_store_explicit(&f->check_result_ready, 0, memory_order_relaxed);
        atomic_store(&f->check_fin, 0);
        f->check_h = work_submit(f->pool, (work_job){ check_job, f, f->generation, WORK_BULK });
        if (f->check_h.epoch) {
            f->jobs[f->check_h.slot] = f->check_h;
            f->check_requested = 0;
        } else { atomic_store(&f->check_fin, 1); f->check_installed = 1; }
    }
}

/* UI-only installation reached exclusively through mailbox decode. Readiness
 * and status getters inspect only installed UI state. */
static void file_sync(file *f, uint32_t kind)
{
    if ((kind == FILE_MSG_PREFIX_READY || kind == FILE_MSG_OPEN_FAILED) &&
        !f->prefix_installed && atomic_load_explicit(&f->prefix_result_ready, memory_order_acquire)) {
        f->prefix_installed = 1;
        f->open_status = f->prefix_result.status;
        f->err_no = f->prefix_result.err_no;
        f->fd = f->prefix_result.fd;
        f->prefix = f->prefix_result.bytes;
        f->prefix_len = f->prefix_result.len;
        f->info = f->prefix_result.info;
        f->size = f->prefix_result.id.size;
        f->id = f->prefix_result.id;
        f->open_identity = f->id;
        f->mode = f->prefix_result.mode;
        memcpy(f->path, f->prefix_result.path, sizeof f->path);
        if (f->open_status == FILE_OK) {
            if (!(f->mode == FILE_MODE_COPY && f->size <= f->prefix_len)) {
                f->open_installed = 0;
                atomic_store(&f->open_fin, 0);
                f->open_h = work_submit(f->pool, (work_job){ open_job, f, f->generation, WORK_BULK });
                if (f->open_h.epoch) f->jobs[f->open_h.slot] = f->open_h;
                else {
                    atomic_store(&f->open_fin, 1); f->open_installed = 1;
                    f->open_status = FILE_ERR_POOL;
                }
            }
        }
    }
    if (kind == FILE_MSG_OPEN_READY && f->prefix_installed && !f->open_h.epoch &&
        f->open_status == FILE_OK) atomic_store(&f->ready, 1);
    if ((kind == FILE_MSG_OPEN_READY || kind == FILE_MSG_OPEN_FAILED) &&
        !f->open_installed && atomic_load_explicit(&f->open_result_ready, memory_order_acquire)) {
        f->map = f->open_result.map;
        f->err_no = f->open_result.err_no;
        f->open_installed = 1;
        f->open_status = f->open_result.status;
        if (f->open_status == FILE_OK) atomic_store(&f->ready, 1);
        else {
            if (f->prefix_owned) free(f->prefix);
            f->prefix = NULL; f->prefix_len = 0;
        }
    }
    if (kind == FILE_MSG_OPEN_FAILED && f->open_status == FILE_ERR_CHANGED) {
        if (!f->changed) f->source_generation++;
        f->changed = 1;
    }
    if ((kind == FILE_MSG_SAVE_DONE || kind == FILE_MSG_REPLACED) &&
        ((!f->save_installed && kind == FILE_MSG_SAVE_DONE && atomic_load_explicit(&f->save_result_ready, memory_order_acquire)) ||
        (!f->save_replaced_installed && atomic_load_explicit(&f->save_replaced_ready, memory_order_acquire)))) {
        install_replaced(f);
        if (kind == FILE_MSG_SAVE_DONE && !f->save_installed && atomic_load_explicit(&f->save_result_ready, memory_order_acquire)) {
            int rc = f->save_result.status;
            if (rc == FILE_ERR_IO || rc == FILE_ERR_DIRSYNC) f->err_no = f->save_result.err_no;
            if (rc == FILE_OK || rc == FILE_ERR_DIRSYNC) {
                if (f->source_generation == f->save.source_generation) f->changed = 0;
                if (f->ifd >= 0) f->watch_refresh = 1;
            } else if (rc == FILE_ERR_CHANGED) {
                if (!f->changed) f->source_generation++;
                f->changed = 1;
            }
            f->save_installed = 1;
        }
    }
    if (kind == FILE_MSG_CHECK_DONE && !f->check_installed && atomic_load_explicit(&f->check_result_ready, memory_order_acquire)) {
        uint32_t r = f->check_result.map_reasons;
        if (f->check_result.status == FILE_OK) r |= file_id_diff(&f->id, &f->check_result.observed);
        if (f->check_result.err_no) f->err_no = f->check_result.err_no;
        if (r && f->source_generation == f->check_result.generation) {
            if (!f->changed) { f->source_generation++; invalidate_save(f); }
            f->changed = 1;
        }
        f->check_installed = 1;
    }
    if (kind == FILE_MSG_SAVE_PREPARED) {
        f->commit_pending = 1;
        save_commit_pump(f);
    }
}

static void install_replaced(file *f)
{
    if (!f->save_replaced_installed &&
        atomic_load_explicit(&f->save_replaced_ready, memory_order_acquire)) {
        f->id = f->save_result.id;
        f->save_replaced_installed = 1;
    }
}

/* ---------------- durable save ---------------- */
static void step_call(file_save_args *a, int s) { if (a->step) a->step(a->step_ctx, s); }

static int write_all(file_save_args *a, os_file *fd, const uint8_t *p, size_t n, uint64_t offset)
{
    while (n) {
        if (a->stop && a->stop(a->stop_ctx)) return FILE_ERR_CANCELLED;
        size_t w;
        /* Return to the cancellation check between interrupted attempts;
         * os_file_write retries EINTR internally. The temp has no other writer. */
        os_err result = os_file_pwrite_once(fd, p, n, offset, &w);
        if (result == OS_E_INTR) continue;
        if (result != OS_OK) { a->err_no = os_native_error(); return FILE_ERR_IO; }
        if (w == 0) { a->err_no = file_native_error(OS_E_IO); return FILE_ERR_IO; }
        p += (size_t)w;
        n -= (size_t)w;
        offset += w;
    }
    return 0;
}

void file_split_path(const char *path, char *dir, size_t dn, char *base, size_t bn)
{
    const char *sl = strrchr(path, '/');
#if defined(_WIN32)
    const char *back = strrchr(path, '\\');
    if (back && (!sl || back > sl)) sl = back;
#endif
    if (sl == NULL) { snprintf(dir, dn, "."); snprintf(base, bn, "%s", path); return; }
    size_t dl = sl == path ? 1 : (size_t)(sl - path);
#if defined(_WIN32)
    if (sl == path + 2 && path[1] == ':') dl++;
#endif
    if (dl >= dn) dl = dn - 1;
    memcpy(dir, path, dl);
    dir[dl] = '\0';
    snprintf(base, bn, "%s", sl + 1);
}

static void transaction_discard(save_transaction *tx)
{
    if (os_file_is_open(&tx->metadata_fd)) { os_file_close(&tx->metadata_fd); tx->metadata_fd = (os_file)OS_FILE_INIT; }
    if (os_file_is_open(&tx->fd)) { os_file_close(&tx->fd); tx->fd = (os_file)OS_FILE_INIT; }
    if (os_dir_is_open(&tx->dfd)) {
        if (tx->tmp[0]) (void)os_unlink_at(&tx->dfd, tx->tmp);
        os_dir_close(&tx->dfd); tx->dfd = (os_dir)OS_DIR_INIT;
    }
}

static int metadata_capture(save_transaction *tx)
{
    file_save_args *a = &tx->args;
    os_stat st;
    memset(&tx->metadata_id, 0, sizeof tx->metadata_id);
    os_err result = os_stat_at(&tx->dfd, tx->base, OS_STAT_NOFOLLOW, &st);
    if (result != OS_OK) {
        if (result != OS_E_NOENT) { a->err_no = os_native_error(); return FILE_ERR_IO; }
    } else file_id_from_stat(&tx->metadata_id, &st);
    if (tx->metadata_id.exists && (st.kind == OS_KIND_LINK)) return FILE_ERR_CHANGED;
    if (a->expect && file_id_diff(a->expect, &tx->metadata_id)) return FILE_ERR_CHANGED;
    if (!tx->metadata_id.exists || !(st.kind == OS_KIND_FILE)) return FILE_OK;
    os_err opened = os_file_open_at(&tx->metadata_fd, &tx->dfd, tx->base,
                                    OS_OPEN_READ | OS_OPEN_NONBLOCK | OS_OPEN_NOFOLLOW);
    if (opened != OS_OK) {
        a->err_no = os_native_error();
        return opened == OS_E_ISDIR ? FILE_ERR_CHANGED : FILE_ERR_IO;
    }
    if (os_file_stat(&tx->metadata_fd, &st) != 0) { a->err_no = os_native_error(); return FILE_ERR_IO; }
    file_id observed; file_id_from_stat(&observed, &st);
    if (file_id_diff(&tx->metadata_id, &observed)) return FILE_ERR_CHANGED;
    if (file_metadata_check(&tx->metadata_fd) != 0) { a->err_no = os_native_error(); return FILE_ERR_IO; }
    return FILE_OK;
}
static int metadata_apply(save_transaction *tx)
{
    file_save_args *a = &tx->args;
    uint32_t mode = tx->metadata_id.exists ? tx->metadata_id.mode
                  : (a->mode_valid || a->mode ? a->mode : 0644u);
    if (os_file_is_open(&tx->metadata_fd)) {
        os_stat st;
        if (file_metadata_copy(&tx->metadata_fd, &tx->fd, &tx->metadata_id) != 0) {
            a->err_no = os_native_error(); return FILE_ERR_IO;
        }
        if (os_file_stat(&tx->metadata_fd, &st) != 0) { a->err_no = os_native_error(); return FILE_ERR_IO; }
        file_id observed; file_id_from_stat(&observed, &st);
        if (file_id_diff(&tx->metadata_id, &observed)) return FILE_ERR_CHANGED;
    }
    /* chown and ACL installation can change permission bits; mode comes last. */
    if (os_file_set_mode(&tx->fd, mode) != 0) { a->err_no = os_native_error(); return FILE_ERR_IO; }
    return FILE_OK;
}

static int transaction_prepare(save_transaction *tx)
{
    file_save_args *a = &tx->args;
    char dir[OS_PATH_MAX];
    uint64_t total = piece_snapshot_len(a->snap);
    int mid_fired = 0;
    a->written = 0; a->err_no = 0; tx->done = 0;
    tx->fd = (os_file)OS_FILE_INIT; tx->dfd = (os_dir)OS_DIR_INIT; tx->metadata_fd = (os_file)OS_FILE_INIT; tx->tmp[0] = '\0';
    if (a->stop && a->stop(a->stop_ctx)) return FILE_ERR_CANCELLED;
    file_split_path(a->path, dir, sizeof dir, tx->base, sizeof tx->base);
    if (os_dir_open(&tx->dfd, dir) != OS_OK) { a->err_no = os_native_error(); return FILE_ERR_IO; }
    int rc = metadata_capture(tx);
    if (rc != FILE_OK) { transaction_discard(tx); return rc; }
#if defined(_WIN32)
    /* Native components are UTF-16; never byte-truncate a UTF-8 basename. */
    snprintf(tx->tmp, sizeof tx->tmp, ".edit-%llu-%llu.tmp",
             (unsigned long long)os_proc_id(), (unsigned long long)os_wall_ns());
#else
    snprintf(tx->tmp, sizeof tx->tmp, ".%.*s.edit-%llu-%llu.tmp", FILE_NAME_MAX_KEEP,
             tx->base, (unsigned long long)os_proc_id(), (unsigned long long)os_wall_ns());
#endif
    if (file_save_temp(tx) != 0) {
        /* A failed O_EXCL may name somebody else's temp: never unlink it. */
        a->err_no = os_native_error(); tx->tmp[0] = '\0'; transaction_discard(tx); return FILE_ERR_IO;
    }
    if (file_metadata_prepare(tx) != 0) {
        a->err_no = os_native_error(); transaction_discard(tx); return FILE_ERR_IO;
    }
    step_call(a, FILE_STEP_TEMP_CREATED);
    piece_iter it; const uint8_t *bytes; size_t n;
    piece_iter_begin_snapshot(&it, a->snap, 0);
    rc = FILE_ERR_IO;
    while (piece_iter_next(&it, &bytes, &n)) {
        while (n) {
            size_t k = n < FILE_WRITE_CHUNK ? n : FILE_WRITE_CHUNK;
            rc = write_all(a, &tx->fd, bytes, k, tx->done);
            if (rc != FILE_OK) goto fail;
            bytes += k; n -= k; tx->done += k;
            if (!mid_fired && tx->done * 2 >= total) {
                mid_fired = 1; step_call(a, FILE_STEP_MID_WRITE);
            }
        }
    }
    rc = FILE_ERR_IO;
    if (!mid_fired) step_call(a, FILE_STEP_MID_WRITE);
    rc = metadata_apply(tx);
    if (rc != FILE_OK) goto fail;
    rc = FILE_ERR_IO;
    step_call(a, FILE_STEP_TEMP_WRITTEN);
    if (a->stop && a->stop(a->stop_ctx)) { rc = FILE_ERR_CANCELLED; goto fail; }
    if (os_file_sync(&tx->fd) != 0) { a->err_no = os_native_error(); goto fail; }
    step_call(a, FILE_STEP_FSYNCED);
    return FILE_OK;
fail:
    transaction_discard(tx); return rc;
}

static int transaction_commit(save_transaction *tx)
{
    file_save_args *a = &tx->args;
    int rc = FILE_ERR_IO, locked = 0;
    os_file observer = OS_FILE_INIT;
    if (a->lock) { os_mutex_lock(a->lock); locked = 1; }
    if (a->stop && a->stop(a->stop_ctx)) { rc = FILE_ERR_CANCELLED; goto fail; }
    {
        file_id now; os_stat st;
        memset(&now, 0, sizeof now);
        os_err result = os_stat_at(&tx->dfd, tx->base, OS_STAT_NOFOLLOW, &st);
        if (result == OS_OK) file_id_from_stat(&now, &st);
        else if (result != OS_E_NOENT && result != OS_E_NOTDIR) { a->err_no = os_native_error(); goto fail; }
        if ((now.exists && (st.kind == OS_KIND_LINK)) || file_id_diff(&tx->metadata_id, &now) || (a->expect && file_id_diff(a->expect, &now))) {
            rc = FILE_ERR_CHANGED; goto fail;
        }
    }
    os_stat st;
    if (os_file_stat(&tx->fd, &st) != 0) { a->err_no = os_native_error(); goto fail; }
    if (a->validate) { rc = a->validate(a->validate_ctx); if (rc != FILE_OK) goto fail; }
    rc = FILE_ERR_IO;
    if (file_save_observer(&tx->fd, &observer) != 0) { a->err_no = os_native_error(); goto fail; }
    if (os_rename_at(&tx->dfd, tx->tmp, &tx->dfd, tx->base, OS_RENAME_REPLACE) != 0) { a->err_no = os_native_error(); goto fail; }
    tx->tmp[0] = '\0';
    os_file_close(&tx->fd); tx->fd = (os_file)OS_FILE_INIT;
    int identity_error = 0;
    os_stat renamed;
    if (os_file_stat(&observer, &renamed) != 0) identity_error = os_native_error();
    else st = renamed;
    os_file_close(&observer); observer = (os_file)OS_FILE_INIT;
    if (a->out_id) {
        file_id_from_stat(a->out_id, &st);
        if (identity_error) a->out_id->metadata_valid = 0;
    }
    if (a->replaced) a->replaced(a->replaced_ctx);
    if (locked) { os_mutex_unlock(a->lock); locked = 0; }
    a->written = tx->done;
    step_call(a, FILE_STEP_RENAMED);
    if (os_dir_sync(&tx->dfd) != 0) {
        a->err_no = os_native_error(); transaction_discard(tx); return FILE_ERR_DIRSYNC;
    }
    transaction_discard(tx);
    step_call(a, FILE_STEP_DIR_SYNCED);
    if (identity_error) { a->err_no = identity_error; return FILE_ERR_IO; }
    return FILE_OK;
fail:
    os_file_close(&observer);
    if (locked) os_mutex_unlock(a->lock);
    transaction_discard(tx); return rc;
}

int file_save_write(file_save_args *a)
{
    save_transaction tx = { .args = *a, .fd = OS_FILE_INIT, .dfd = OS_DIR_INIT, .metadata_fd = OS_FILE_INIT };
    int rc = transaction_prepare(&tx);
    if (rc == FILE_OK) rc = transaction_commit(&tx);
    a->written = tx.args.written; a->err_no = tx.args.err_no;
    return rc;
}

static int stop_cb(void *ctx) { return work_should_stop((work_ctx *)ctx); }

/* Called immediately before rename. Path identity alone cannot
 * validate a tree whose original inode survived an earlier replacement. */
static int source_validate(void *ctx)
{
    file *f = ctx;
    file_map *m = f->save.source_map;
    if (m) {
        if (guard_faults(m->slot) != f->save.source_faults) return FILE_ERR_CHANGED;
        os_stat st;
        file_id now;
        if (os_file_stat(&m->fd, &st) != 0) return FILE_ERR_CHANGED;
        file_id_from_stat(&now, &st);
        if (file_id_diff(&f->save.source_id, &now) & ~FILE_CHG_METADATA) return FILE_ERR_CHANGED;
    }
    return f->save.authorized ? FILE_OK : FILE_ERR_CHANGED;
}

static void publish_replaced(void *ctx)
{
    work_ctx *c = ctx;
    file *f = c->arg;
    atomic_store_explicit(&f->save_replaced_ready, 1, memory_order_release);
    (void)post(c, FILE_MSG_REPLACED, f, FILE_OK, 0, 0, 0, f->save.generation);
}

static void save_job(work_ctx *c)
{
    file *f = c->arg;
    file_save_args a;
    memset(&a, 0, sizeof a);
    a.mode = f->save.mode; a.mode_valid = 1;
    a.path = f->path;
    a.snap = f->save.snap;
    a.expect = (f->save.flags & FILE_SAVE_FORCE) ? NULL : &f->save.expect;
    a.step = f->save.step;
    a.step_ctx = f->save.step_ctx;
    a.stop = stop_cb;
    a.stop_ctx = c;
    a.out_id = &f->save_result.id;
    a.validate = source_validate;
    a.validate_ctx = f;
    a.replaced = publish_replaced;
    a.replaced_ctx = c;

    int rc = FILE_OK;
    if (f->save.source_map && (f->save.flags & FILE_SAVE_FORCE)) {
        os_stat st;
        if (os_file_stat(&f->save.source_map->fd, &st) != 0) { a.err_no = os_native_error(); rc = FILE_ERR_IO; }
        else file_id_from_stat(&f->save.source_id, &st);
    }
    f->transaction.args = a;
    if (rc == FILE_OK) rc = transaction_prepare(&f->transaction);
    piece_snapshot_release(f->save.snap);
    f->save.snap = NULL;
    f->prepare_status = rc;
    if (rc == FILE_OK) {
        (void)post(c, FILE_MSG_SAVE_PREPARED, f, FILE_OK, 0, 0, 0, f->save.generation);
    } else {
        f->save_result.status = rc;
        f->save_result.err_no = f->transaction.args.err_no;
        atomic_store_explicit(&f->save_result_ready, 1, memory_order_release);
        (void)post(c, FILE_MSG_SAVE_DONE, f, rc, 0, 0, f->save_result.err_no, f->save.generation);
        atomic_store_explicit(&f->save_fin, 1, memory_order_release);
    }
    atomic_store_explicit(&f->prepare_fin, 1, memory_order_release);
}

/* UI continuation. Mutable source state never crosses to a worker: the
 * mailbox returns preparation ownership, then this captures a fresh immutable
 * authorization. Worker checks backing identity/fault epoch again before
 * rename. The remaining last-validation-to-rename race is unchanged. */
static void save_commit_pump(file *f)
{
    if (!f->commit_pending) return;
    f->save.authorized = f->source_generation == f->save.source_generation &&
                         ((f->save.flags & FILE_SAVE_FORCE) || !file_changed(f));
    work_handle h = work_submit(f->pool, (work_job){ save_commit_job, f, f->save.generation, WORK_BULK });
    if (h.epoch) {
        f->save_h = h; f->jobs[h.slot] = h; f->commit_pending = 0;
    }
}

static void save_commit_job(work_ctx *c)
{
    file *f = c->arg;
    f->transaction.args.stop_ctx = c;
    f->transaction.args.replaced_ctx = c;
    int rc = f->save.authorized ? transaction_commit(&f->transaction) : FILE_ERR_CHANGED;
    if (!f->save.authorized) transaction_discard(&f->transaction);
    f->save_result.status = rc;
    f->save_result.err_no = f->transaction.args.err_no;
    atomic_store_explicit(&f->save_result_ready, 1, memory_order_release);
    (void)post(c, FILE_MSG_SAVE_DONE, f, rc, 0, f->transaction.args.written,
               f->save_result.err_no, f->save.generation);
    atomic_store_explicit(&f->save_fin, 1, memory_order_release);
}

static void save_abort_job(work_ctx *c)
{
    file *f = c->arg;
    /* Same BULK queue as commit: a skipped/cancelled commit has relinquished
     * every descriptor before this continuation runs. Its mailbox epoch is
     * independent, so source invalidation does not lose the terminal result. */
    transaction_discard(&f->transaction);
    f->save_result.status = FILE_ERR_CHANGED;
    f->save_result.err_no = 0;
    atomic_store_explicit(&f->save_result_ready, 1, memory_order_release);
    (void)post(c, FILE_MSG_SAVE_DONE, f, FILE_ERR_CHANGED, 0,
               f->transaction.args.written, 0, f->save.generation);
    atomic_store_explicit(&f->save_fin, 1, memory_order_release);
    atomic_store_explicit(&f->abort_fin, 1, memory_order_release);
}

static void save_abort_pump(file *f)
{
    if (!f->abort_pending) return;
    work_handle h = work_submit(f->pool, (work_job){ save_abort_job, f, f->save.generation, WORK_BULK });
    if (h.epoch) {
        f->abort_h = h; f->jobs[h.slot] = h; f->abort_pending = 0;
    }
}

static void invalidate_save(file *f)
{
    if (f->save_installed || !f->save_h.epoch || f->save_replaced_installed || f->save_aborted) return;
    f->save_aborted = 1;
    work_cancel(f->pool, f->save_h);
    f->abort_pending = 1;
    atomic_store(&f->abort_fin, 0);
    save_abort_pump(f);
}

int file_save_busy(const file *f)
{
    save_commit_pump((file *)f);
    save_abort_pump((file *)f);
    return !f->save_installed || !atomic_load_explicit(&f->save_fin, memory_order_acquire) ||
           !atomic_load_explicit(&f->prepare_fin, memory_order_acquire) ||
           !atomic_load_explicit(&f->abort_fin, memory_order_acquire);
}

void file_set_step_hook(file *f, void (*step)(void *, int), void *ctx)
{
    f->step = step;
    f->step_ctx = ctx;
}

int file_save_begin(file *f, piece_tree *t, unsigned flags, uint32_t generation)
{
    piece_snapshot *s;
    if (file_save_busy(f)) return FILE_ERR_BUSY;
    if (!file_open_ready(f) || !f->attached) return FILE_ERR_STATE;
    if (!(flags & FILE_SAVE_FORCE)) {
        if (file_changed(f)) return FILE_ERR_CHANGED;
    }
    s = piece_snapshot_take(t);
    if (s == NULL) return FILE_ERR_NOMEM;
    f->save.snap = s;
    f->save.flags = flags;
    f->save.generation = generation;
    f->save.step = f->step;
    f->save.step_ctx = f->step_ctx;
    f->save.expect = f->id;
    f->save.mode = f->id.mode;
    f->save.source_generation = f->source_generation;
    f->save.source_map = f->mode == FILE_MODE_MMAP ? f->map : NULL;
    f->save.source_faults = f->save.source_map ? guard_faults(f->save.source_map->slot) : 0;
    if (f->save.source_map) f->save.source_id = f->save.source_map->identity;
    f->save_installed = 0;
    f->save_replaced_installed = 0;
    atomic_store_explicit(&f->save_result_ready, 0, memory_order_relaxed);
    atomic_store_explicit(&f->save_replaced_ready, 0, memory_order_relaxed);
    atomic_store(&f->save_fin, 0);
    atomic_store(&f->prepare_fin, 0);
    f->save_h = (work_handle){0};
    f->abort_h = (work_handle){0};
    f->save_aborted = 0; f->abort_pending = 0;
    f->prepare_h = work_submit(f->pool, (work_job){ save_job, f, generation, WORK_BULK });
    if (f->prepare_h.epoch == 0) {
        atomic_store(&f->save_fin, 1);
        atomic_store(&f->prepare_fin, 1);
        piece_snapshot_release(s);
        f->save.snap = NULL;
        f->save_installed = 1;
        f->save_replaced_installed = 1;
        return FILE_ERR_POOL;
    }
    f->jobs[f->prepare_h.slot] = f->prepare_h;
    return FILE_OK;
}
