#include "file/file.h"
#ifdef FILE_TEST_HOOKS
#include "file/file_test.h"
#endif

#include <string.h>

/* ---- small helpers ------------------------------------------------------- */

static bool identity_eq(const os_identity *a, const os_identity *b)
{
    return a->volume == b->volume && a->index[0] == b->index[0] &&
           a->index[1] == b->index[1];
}

/* strcpy has no symbol grant in this tree; copy through memcpy. */
static void copy_str(char *dst, const char *src)
{
    memcpy(dst, src, strlen(src) + 1);
}

static size_t bounded_len(const char *s, size_t cap)
{
    size_t n = 0;
    while (n < cap && s[n] != '\0') n++;
    return n;
}

/* Split a path into its directory (no trailing slash, "/" for the root,
 * "." for a bare leaf) and leaf. Every refusal is a result (A15; file.h
 * "Path diagnosis"): a name can come from a Save As field. */
static os_err split_path(const char *path, char *dir, size_t dir_cap,
                         char *leaf, size_t leaf_cap)
{
    size_t n = bounded_len(path, OS_PATH_MAX);
    if (n >= OS_PATH_MAX) return OS_E_NAMETOOLONG;
    if (n == 0) return OS_E_INVAL;
    if (path[n - 1] == '/') return OS_E_ISDIR;
    const char *slash = strrchr(path, '/');
    const char *name = slash == NULL ? path : slash + 1;
    size_t dlen = slash == NULL || slash == path ? 1 : (size_t)(slash - path);
    size_t llen = n - (size_t)(name - path);
    if (dlen >= dir_cap || llen >= leaf_cap) return OS_E_NAMETOOLONG;
    memcpy(dir, slash == NULL ? "." : path, dlen);
    dir[dlen] = '\0';
    memcpy(leaf, name, llen + 1);
    return OS_OK;
}

uint64_t file_default_copy_threshold(void)
{
    /* (G) v1 policy (file.h): min(256 MiB, phys/32). Derivations in file.h. */
    uint64_t cap = UINT64_C(256) << 20;
    uint64_t phys = os_phys_mem_bytes();
    if (phys != 0 && phys / 32 < cap) cap = phys / 32;
    return cap;
}

/* ---- CRC32C --------------------------------------------------------------- */

static void crc_table(uint32_t tab[256])
{
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;
        for (int k = 0; k < 8; k++)
            c = (c & 1) ? (UINT32_C(0x82F63B78) ^ (c >> 1)) : (c >> 1);
        tab[i] = c;
    }
}

static uint32_t crc_update(const uint32_t tab[256], uint32_t crc,
                           const uint8_t *data, size_t len)
{
    uint32_t c = ~crc;
    for (size_t i = 0; i < len; i++) c = tab[(c ^ data[i]) & 0xFF] ^ (c >> 8);
    return ~c;
}

uint32_t file_crc32c(uint32_t crc, const uint8_t *data, size_t len)
{
    if (len == 0) return crc;
    ASSERT(data != NULL);
    uint32_t tab[256];
    crc_table(tab);
    return crc_update(tab, crc, data, len);
}

/* ---- prefix scan ---------------------------------------------------------- */

void file_prefix_scan(const uint8_t *p, size_t n, file_prefix_info *out)
{
    ASSERT(out != NULL && (p != NULL || n == 0));
    memset(out, 0, sizeof *out);
    out->has_bom = n >= 3 && p[0] == 0xEF && p[1] == 0xBB && p[2] == 0xBF;
    for (size_t i = 0; i < n; i++) {
        if (p[i] == '\r') {
            if (i + 1 < n && p[i + 1] == '\n') { out->crlf++; i++; }
            else out->cr++;
        } else if (p[i] == '\n') {
            out->lf++;
        }
    }
    bool lf = out->lf != 0, crlf = out->crlf != 0;
    if (lf && crlf) out->kind = FILE_EOL_MIXED;
    else if (crlf) out->kind = FILE_EOL_CRLF;
    else if (lf) out->kind = FILE_EOL_LF;
    else out->kind = FILE_EOL_NONE;
    out->dominant = out->crlf > out->lf ? FILE_EOL_CRLF : FILE_EOL_LF;
}

/* ---- expectation ---------------------------------------------------------- */

uint32_t file_expectation_diff(const file_expectation *want,
                               const file_expectation *got)
{
    ASSERT(want != NULL && got != NULL);
    if (want->presence != got->presence) return FILE_CHG_GONE;
    if (want->presence == FILE_ABSENT) return FILE_CHG_NONE;
    const os_stat *a = &want->stat, *b = &got->stat;
    uint32_t diff = FILE_CHG_NONE;
    if (!identity_eq(&a->id, &b->id)) diff |= FILE_CHG_INODE;
    if (a->size != b->size) diff |= FILE_CHG_SIZE;
    if (a->mtime_ns != b->mtime_ns) diff |= FILE_CHG_MTIME;
    if (a->kind != b->kind || a->perm != b->perm || a->uid != b->uid ||
        a->gid != b->gid || a->ctime_ns != b->ctime_ns)
        diff |= FILE_CHG_METADATA;
    return diff;
}

static int observe_result(os_err e, const os_stat *st, file_expectation *out,
                          os_err *oserr)
{
    if (e == OS_OK) {
        out->presence = FILE_PRESENT;
        out->stat = *st;
        return FILE_OK;
    }
    if (e == OS_E_NOENT || e == OS_E_NOTDIR) {
        out->presence = FILE_ABSENT;
        memset(&out->stat, 0, sizeof out->stat);
        return FILE_OK;
    }
    if (oserr != NULL) *oserr = e;
    return FILE_ERR_IO;
}

int file_expectation_observe_path(const char *path, file_expectation *out,
                                  os_err *oserr)
{
    ASSERT(path != NULL && out != NULL);
    os_stat st;
    return observe_result(os_stat_path(path, OS_STAT_NOFOLLOW, &st), &st, out, oserr);
}

int file_expectation_observe_at(const os_dir *dir, const char *name,
                                file_expectation *out, os_err *oserr)
{
    ASSERT(dir != NULL && name != NULL && out != NULL);
    os_stat st;
    return observe_result(os_stat_at(dir, name, OS_STAT_NOFOLLOW, &st), &st, out, oserr);
}

int file_expectation_observe_fd(const os_file *fd, file_expectation *out,
                                os_err *oserr)
{
    ASSERT(fd != NULL && out != NULL);
    os_stat st;
    os_err e = os_file_stat(fd, &st);
    if (e != OS_OK) {
        if (oserr != NULL) *oserr = e;
        return FILE_ERR_IO;
    }
    out->presence = FILE_PRESENT;
    out->stat = st;
    return FILE_OK;
}

/* ---- backing -------------------------------------------------------------- */

struct file_backing_impl {
    _Atomic uint64_t refs;
    base_arena arena;   /* the reservation holding this struct (and copy bytes) */
    file_mode mode;
    os_file fd;
    os_map view;        /* mapped mode only */
    piece_backing backing;
    os_stat stat;       /* fd-captured at acquisition; immutable once published */
    /* The baseline ctime. Moved only by the publish job, to the value
     * our own rename over this inode produced (file.h, MF5 ruling);
     * validators on any thread load it. */
    _Atomic int64_t ctime_ns;
};

static void backing_acquire_cb(void *ctx)
{
    file_source *b = ctx;
    atomic_fetch_add(&b->refs, 1);
}

static void backing_release_cb(void *ctx)
{
    file_source *b = ctx;
    if (atomic_fetch_sub(&b->refs, 1) != 1) return;
    /* Final release: retire before unmap, exactly once (fault spec). */
    base_arena arena = b->arena;
    switch (b->mode) {
    case FILE_MODE_MMAP:
        os_fault_retire(b->backing.fault);
        os_unmap(&b->view);
        break;
    case FILE_MODE_COPY:
        break;
    }
    os_file_close(&b->fd);
    base_arena_free(&arena);
}

/* Allocate a backing in its own reservation. extra reserves copy bytes
 * after the struct. Returns NULL when base refuses. */
static file_source *backing_new(file_mode mode, size_t extra, uint8_t **bytes_out)
{
    size_t total = sizeof(file_source) + extra;
    base_arena arena;
    if (base_arena_init(&arena, total) != 0) return NULL;
    file_source *b = base_arena_alloc(&arena, sizeof *b, _Alignof(file_source));
    uint8_t *bytes = extra != 0 ? base_arena_alloc(&arena, extra, 1) : NULL;
    if (b == NULL || (extra != 0 && bytes == NULL)) {
        base_arena_free(&arena);
        return NULL;
    }
    memset(b, 0, sizeof *b);
    atomic_init(&b->refs, 1);
    atomic_init(&b->ctime_ns, 0);
    b->arena = arena;
    b->mode = mode;
    b->fd = (os_file)OS_FILE_INIT;
    b->backing.bytes = bytes;
    b->backing.len = extra;
    b->backing.fault = (os_fault_id)OS_FAULT_ID_INIT;
    b->backing.ctx = b;
    b->backing.acquire = backing_acquire_cb;
    b->backing.release = backing_release_cb;
    if (bytes_out != NULL) *bytes_out = bytes;
    return b;
}

static void backing_set_stat(file_source *b, const os_stat *st)
{
    b->stat = *st;
    atomic_store(&b->ctime_ns, st->ctime_ns);
}

static os_stat source_baseline(const file_source *b)
{
    os_stat st = b->stat;
    st.ctime_ns = atomic_load(&b->ctime_ns);
    return st;
}

static bool backing_poll_changed(const file_source *b)
{
    switch (b->mode) {
    case FILE_MODE_COPY:
        return false;
    case FILE_MODE_MMAP: {
        uint64_t epoch = 0;
        return os_fault_poll(b->backing.fault, &epoch) != OS_OK || epoch != 0;
    }
    }
    return true;
}

static bool open_installed(const file *f)
{
    switch (f->open_state) {
    case FILE_OPEN_READY:
    case FILE_OPEN_ATTACHED:
        return true;
    case FILE_OPEN_NONE:
    case FILE_OPEN_PREFIX:
    case FILE_OPEN_FAILED:
        return false;
    }
    return false;
}

int file_source_acquire(file *f, file_source **out)
{
    ASSERT(f != NULL && out != NULL);
    *out = NULL;
    if (!open_installed(f) || f->backing == NULL) return FILE_ERR_STATE;
    *out = file_source_retain(f->backing);
    return FILE_OK;
}

file_source *file_source_retain(file_source *source)
{
    ASSERT(source != NULL);
    atomic_fetch_add(&source->refs, 1);
    return source;
}

void file_source_release(file_source *source)
{
    if (source == NULL) return;
    backing_release_cb(source);
}

os_stat file_source_stat(const file_source *source)
{
    ASSERT(source != NULL);
    return source_baseline(source);
}

file_mode file_source_mode(const file_source *source)
{
    ASSERT(source != NULL);
    return source->mode;
}

int file_source_validate(file_source *source, os_err *oserr)
{
    ASSERT(source != NULL);
    if (backing_poll_changed(source)) return FILE_ERR_CHANGED;
    file_expectation want = { .presence = FILE_PRESENT, .stat = source_baseline(source) };
    file_expectation got;
    int rc = file_expectation_observe_fd(&source->fd, &got, oserr);
    if (rc != FILE_OK) return rc;
    return file_expectation_diff(&want, &got) == FILE_CHG_NONE ? FILE_OK
                                                               : FILE_ERR_CHANGED;
}

bool file_backing_validate(void *ctx, const piece_backing *backing)
{
    ASSERT(backing != NULL);
    file_source *b;
    if (ctx != NULL) {
        const file *f = ctx;
        b = f->backing;
        if (b == NULL || backing->ctx != (void *)b) return false;
    } else {
        /* Erratum 5: the backing's own source, if file created it. */
        if (backing->acquire != backing_acquire_cb ||
            backing->release != backing_release_cb || backing->ctx == NULL)
            return false;
        b = backing->ctx;
    }
    if (backing->bytes != b->backing.bytes || backing->len != b->backing.len)
        return false;
    if (backing->fault.slot != b->backing.fault.slot ||
        backing->fault.gen != b->backing.fault.gen)
        return false;
    os_err oe = OS_OK;
    return file_source_validate(b, &oe) == FILE_OK;
}

/* ---- save core ------------------------------------------------------------ */

#ifdef FILE_TEST_HOOKS
static void (*sync_step_hook)(void *ctx, int step);
static void *sync_step_ctx;
void file_test_set_step_hook(file *f, void (*step)(void *ctx, int step), void *ctx)
{
    if (f != NULL) {
        f->step_hook = step;
        f->step_ctx = ctx;
    } else {
        sync_step_hook = step;
        sync_step_ctx = ctx;
    }
}
#define STEP(hook, ctx, which) \
    do { if ((hook) != NULL) (hook)((ctx), (which)); } while (0)
#else
#define STEP(hook, ctx, which) ((void)0)
#endif

/* Write v in decimal at dst; returns the byte count. */
static size_t put_u64(char *dst, uint64_t v)
{
    char rev[20]; /* (P) 2^64-1 has 20 decimal digits. */
    size_t n = 0;
    do { rev[n++] = (char)('0' + (v % 10)); v /= 10; } while (v != 0);
    for (size_t i = 0; i < n; i++) dst[i] = rev[n - 1 - i];
    return n;
}

static bool stopped(const work_ctx *stop)
{
    return stop != NULL && work_should_stop(stop);
}

typedef struct prepare_args {
    const char *dest_path;
    const piece_snapshot *snap;
    const file_expectation *expect;
    unsigned flags;
    file_source *source;   /* optional: the mapped original S reads */
    const work_ctx *stop;  /* optional: worker cancellation */
    void (*step)(void *, int);
    void *step_ctx;
} prepare_args;

static void temp_reset(file_temp *t)
{
    memset(t, 0, sizeof *t);
    t->fd = (os_file)OS_FILE_INIT;
    t->dir = (os_dir)OS_DIR_INIT;
}

/* Unlink our temp name, but only while it still names the held fd: a
 * name we no longer own is never removed (erratum 4). */
static void temp_unlink_own(file_temp *t)
{
    if (t->tmp[0] == '\0' || !os_dir_is_open(&t->dir) || !os_file_is_open(&t->fd))
        return;
    os_stat at, mine;
    if (os_stat_at(&t->dir, t->tmp, OS_STAT_NOFOLLOW, &at) == OS_OK &&
        os_file_stat(&t->fd, &mine) == OS_OK && identity_eq(&at.id, &mine.id))
        (void)os_unlink_at(&t->dir, t->tmp);
    t->tmp[0] = '\0';
}

/* Release everything custody owns and reach DONE. */
static void temp_release(file_temp *t, bool unlink_tmp)
{
    if (unlink_tmp) temp_unlink_own(t);
    os_file_close(&t->fd);
    os_dir_close(&t->dir);
    file_source_release(t->source);
    t->source = NULL;
    t->state = FILE_TEMP_DONE;
}

static int copy_metadata(file_temp *t, os_file *dst, os_err *oserr)
{
    base_arena scratch_arena;
    if (base_arena_init(&scratch_arena, sizeof(os_xattr_scratch)) != 0)
        return FILE_ERR_NOMEM;
    os_xattr_scratch *scratch =
        base_arena_alloc(&scratch_arena, sizeof *scratch, _Alignof(os_xattr_scratch));
    int rc = FILE_ERR_NOMEM;
    if (scratch != NULL) {
        os_err e = os_file_copy_metadata(&t->fd, dst, scratch);
        rc = FILE_OK;
        if (e != OS_OK) {
            if (oserr != NULL) *oserr = e;
            rc = FILE_ERR_IO;
        }
    }
    base_arena_free(&scratch_arena);
    return rc;
}

/* The temp's metadata, applied before the sync so the sync covers it.
 * When the destination still is the expected regular file, its own
 * metadata is copied. Otherwise the mode is the destination's observed
 * mode while one exists (an owner-unreadable 0060 stays 0060, review
 * MF-B), and 0666 & ~the creation mask only for a name with no
 * destination (advisor ruling, MF7). Special bits travel only with the
 * owner, that is through the copy. The destination opens non-blocking
 * with the leaf not followed, so a FIFO or device never waits (MF9). */
static int temp_metadata(file_temp *t, const prepare_args *a, os_err *oserr)
{
    uint32_t perm = 0666u & ~os_creation_mask();
    if (a->expect->presence == FILE_PRESENT) {
        os_file dst = OS_FILE_INIT;
        os_err e = os_file_open_at(&dst, &t->dir, t->base,
                                   OS_OPEN_READ | OS_OPEN_NONBLOCK | OS_OPEN_NOFOLLOW);
        if (e == OS_OK) {
            os_stat ds;
            e = os_file_stat(&dst, &ds);
            int rc = FILE_OK;
            if (e != OS_OK) {
                if (oserr != NULL) *oserr = e;
                rc = FILE_ERR_IO;
            } else if (ds.kind == OS_KIND_FILE &&
                       identity_eq(&ds.id, &a->expect->stat.id)) {
                rc = copy_metadata(t, &dst, oserr);
                os_file_close(&dst);
                return rc;
            } else if ((a->flags & FILE_SAVE_FORCE) == 0) {
                rc = FILE_ERR_CHANGED; /* not the file we were told to replace */
            } else {
                perm = ds.perm & 0777u; /* FORCE over what is there now */
            }
            os_file_close(&dst);
            if (rc != FILE_OK) return rc;
        } else if (e != OS_E_NOENT && e != OS_E_NOTDIR) {
            perm = a->expect->stat.perm & 0777u; /* present, unreadable */
        }
    }
    os_err e = os_file_set_perm(&t->fd, perm);
    if (e != OS_OK) {
        if (oserr != NULL) *oserr = e;
        return FILE_ERR_IO;
    }
    return FILE_OK;
}

static int temp_prepare_core(file_temp *t, const prepare_args *a, os_err *oserr)
{
    switch (t->state) {
    case FILE_TEMP_EMPTY:
    case FILE_TEMP_DONE:
        break;
    case FILE_TEMP_SYNCED:
    case FILE_TEMP_INSTALLED_UNSYNCED:
    case FILE_TEMP_HELD:
        return FILE_ERR_STATE;
    }
    temp_reset(t);
    t->expect = *a->expect;
    t->flags = a->flags;
    t->state = FILE_TEMP_DONE; /* every early refusal owns nothing */
    if (a->expect->presence == FILE_PRESENT && a->expect->stat.kind != OS_KIND_FILE)
        return FILE_ERR_NOTREG; /* refused before any open or wait (MF9) */
    char dirpath[OS_PATH_MAX];
    os_err e = split_path(a->dest_path, dirpath, sizeof dirpath, t->base, sizeof t->base);
    if (e == OS_OK) e = os_dir_open(&t->dir, dirpath);
    if (e != OS_OK) {
        if (oserr != NULL) *oserr = e;
        temp_release(t, false);
        return FILE_ERR_IO;
    }
    if (a->source != NULL) t->source = file_source_retain(a->source);

    /* Temp leaf: ".<base>.sublimite-v2-<stamp>-<attempt>.tmp", created
     * exclusively beside the destination (v1's shape). The base is
     * truncated so the leaf and its NUL fit OS_NAME_MAX. Each EXCL retry
     * changes the attempt, so the loop needs only one free name. */
    uint64_t stamp = os_now_ns() ^ os_thread_self();
    char suffix[64];
    suffix[0] = '.';
    copy_str(suffix + 1, os_app_id()); /* EDIT_APP_ID, never a literal */
    size_t suffix_len = strlen(suffix);
    suffix[suffix_len++] = '-';
    suffix[suffix_len] = '\0';
    /* (P) '.', suffix, two counters of at most 20 digits, '-', ".tmp"
     * and the NUL. */
    size_t room = OS_NAME_MAX - 1 - suffix_len - 20 - 1 - 20 - 4 - 1;
    size_t blen = strlen(t->base);
    if (blen > room) blen = room;
    for (uint64_t attempt = 0;; attempt++) {
        char name[OS_NAME_MAX];
        size_t at = 0;
        name[at++] = '.';
        memcpy(name + at, t->base, blen);
        at += blen;
        memcpy(name + at, suffix, suffix_len);
        at += suffix_len;
        at += put_u64(name + at, stamp);
        name[at++] = '-';
        at += put_u64(name + at, attempt);
        copy_str(name + at, ".tmp");
        e = os_file_open_at(&t->fd, &t->dir, name,
                            OS_OPEN_RDWR | OS_OPEN_CREATE | OS_OPEN_EXCL);
        if (e == OS_OK) {
            copy_str(t->tmp, name);
            break;
        }
        if (e != OS_E_EXIST) {
            if (oserr != NULL) *oserr = e;
            temp_release(t, false);
            return FILE_ERR_IO;
        }
    }
    STEP(a->step, a->step_ctx, 1 /* TEMP_CREATED */);

    /* Write the snapshot's whole text, CRC32C over S as it goes. */
    uint32_t tab[256];
    crc_table(tab);
    uint32_t crc = 0;
    const piece_view *view = piece_snapshot_view(a->snap);
    uint64_t total = piece_length(view), done = 0;
    bool mid_fired = false;
    piece_iter it;
    piece_iter_init(&it, view, 0);
    const uint8_t *run;
    size_t run_len;
    int rc = FILE_OK;
    while ((run_len = piece_iter_next(&it, &run)) != 0) {
        if (stopped(a->stop)) { rc = FILE_ERR_CANCELLED; break; }
        crc = crc_update(tab, crc, run, run_len);
        size_t off = 0;
        while (off < run_len) {
            size_t wrote = 0;
            e = os_file_write(&t->fd, run + off, run_len - off, &wrote);
            if (e != OS_OK || wrote == 0) {
                if (oserr != NULL) *oserr = e != OS_OK ? e : OS_E_IO;
                rc = FILE_ERR_IO;
                break;
            }
            off += wrote;
            done += wrote;
        }
        if (rc != FILE_OK) break;
        if (!mid_fired && done * 2 >= total) {
            STEP(a->step, a->step_ctx, 2 /* MID_WRITE */);
            mid_fired = true;
        }
    }
    if (rc == FILE_OK && !mid_fired) STEP(a->step, a->step_ctx, 2);
    if (rc == FILE_OK) STEP(a->step, a->step_ctx, 3 /* TEMP_WRITTEN */);
    /* One closing poll after the last mapped load the write used, then
     * the retained source in full (a same-size in-place overwrite
     * raises no fault: only its fd stat shows it). Only when S reads a
     * mapped backing: a copy's bytes are private, so its original's
     * drift cannot reach what is written (review MF-E; file.h). */
    if (rc == FILE_OK) {
        const piece_backing *sb = piece_view_backing(view);
        if (sb != NULL) {
            uint64_t epoch = 0;
            if (os_fault_poll(sb->fault, &epoch) != OS_OK || epoch != 0)
                rc = FILE_ERR_CHANGED;
            os_err ve = OS_OK;
            if (rc == FILE_OK && t->source != NULL &&
                file_source_validate(t->source, &ve) != FILE_OK)
                rc = FILE_ERR_CHANGED;
        }
    }
    if (rc == FILE_OK) rc = temp_metadata(t, a, oserr);
    if (rc == FILE_OK) {
        e = os_file_sync(&t->fd);
        if (e != OS_OK) {
            if (oserr != NULL) *oserr = e;
            rc = FILE_ERR_IO;
        }
    }
    if (rc == FILE_OK) {
        STEP(a->step, a->step_ctx, 4 /* FSYNCED */);
        e = os_file_stat(&t->fd, &t->fd_stat);
        if (e != OS_OK) {
            if (oserr != NULL) *oserr = e;
            rc = FILE_ERR_IO;
        }
    }
    if (rc != FILE_OK) {
        temp_release(t, true);
        return rc;
    }
    t->witness.volume = t->fd_stat.id.volume;
    t->witness.index_low = t->fd_stat.id.index[0];
    t->witness.index_high = t->fd_stat.id.index[1];
    t->witness.size = total;
    t->witness.content_crc32c = crc;
    t->state = FILE_TEMP_SYNCED;
    return FILE_OK;
}

/* The proof of installation: the path names the written fd, whose size
 * and mtime are still those captured after the sync (the rename moves
 * only ctime), so a foreign in-place write into our installed inode
 * disproves it too (review N2). */
static bool temp_proven(const file_temp *t, os_stat *now, os_err *oe)
{
    file_expectation at_path;
    *oe = os_file_stat(&t->fd, now);
    if (*oe != OS_OK) return false;
    if (file_expectation_observe_at(&t->dir, t->base, &at_path, oe) != FILE_OK) return false;
    return at_path.presence == FILE_PRESENT && identity_eq(&at_path.stat.id, &now->id) &&
           at_path.stat.size == now->size && identity_eq(&now->id, &t->fd_stat.id) &&
           now->size == t->fd_stat.size && now->mtime_ns == t->fd_stat.mtime_ns;
}

static bool pair_foreign(const file_temp *t)
{
    switch (t->pair) {
    case FILE_PAIR_FOREIGN:
        return true;
    case FILE_PAIR_NONE:
    case FILE_PAIR_OURS:
        return false;
    }
    return true;
}

/* VERIFIED only on proof and an undisturbed displaced source; the pair's
 * verdict is kept in custody, so no later verification forgets it. */
static int temp_verify_installed(const file_temp *t, file_save_outcome *out)
{
    os_stat now;
    os_err oe = OS_OK;
    if (!temp_proven(t, &now, &oe) || pair_foreign(t)) {
        out->pub = FILE_PUB_CONFLICT;
        out->oserr = oe;
        return FILE_ERR_CONFLICT;
    }
    out->pub = FILE_PUB_VERIFIED;
    out->installed.presence = FILE_PRESENT;
    out->installed.stat = now;
    out->oserr = OS_OK;
    return FILE_OK;
}

static int temp_barrier_and_verify(file_temp *t, const prepare_args *hooks,
                                   file_save_outcome *out)
{
    os_err e = os_dir_sync(&t->dir);
    if (e != OS_OK) {
        os_stat fd_now;
        out->pub = FILE_PUB_INSTALLED_UNSYNCED;
        out->oserr = e;
        if (os_file_stat(&t->fd, &fd_now) == OS_OK) {
            out->installed.presence = FILE_PRESENT;
            out->installed.stat = fd_now;
        }
        t->state = FILE_TEMP_INSTALLED_UNSYNCED;
        return FILE_ERR_DIRSYNC;
    }
    if (hooks != NULL) STEP(hooks->step, hooks->step_ctx, 6 /* DIR_SYNCED */);
    int rc = temp_verify_installed(t, out);
    t->state = FILE_TEMP_HELD;
    return rc;
}

/* The source is the displaced generation: our rename drops its link.
 * Mapped sources only: the pair exists to keep a mapped backing's bytes
 * validating; a copy's bytes are private, so its old inode's drift is
 * nobody's evidence (enumeration §14, the third MF-E site). */
static bool source_displaced(const file_temp *t)
{
    if (t->source == NULL) return false;
    switch (t->source->mode) {
    case FILE_MODE_MMAP:
        break;
    case FILE_MODE_COPY:
        return false;
    }
    return t->expect.presence == FILE_PRESENT &&
           identity_eq(&t->source->stat.id, &t->expect.stat.id);
}

/* MF5 ruling: fstat the displaced backing's held fd immediately before
 * and after our own rename. A pair differing in ctime and a falling
 * link count only is our rename: the after ctime becomes the backing's
 * baseline (when the before value was that baseline; otherwise the
 * backing already fails validation and stays failing). Any other
 * difference, or an unobservable pair, is FOREIGN. */
static file_source_pair source_rebaseline(file_source *b, os_err before_err,
                                          const os_stat *before)
{
    os_stat after;
    if (before_err != OS_OK || os_file_stat(&b->fd, &after) != OS_OK)
        return FILE_PAIR_FOREIGN;
    file_expectation x = { .presence = FILE_PRESENT, .stat = *before };
    file_expectation y = { .presence = FILE_PRESENT, .stat = after };
    uint32_t d = file_expectation_diff(&x, &y);
    if (d != FILE_CHG_NONE) {
        if (d != FILE_CHG_METADATA || after.kind != before->kind ||
            after.perm != before->perm || after.uid != before->uid ||
            after.gid != before->gid || after.nlink >= before->nlink)
            return FILE_PAIR_FOREIGN;
    }
    file_expectation base = { .presence = FILE_PRESENT, .stat = source_baseline(b) };
    if (file_expectation_diff(&base, &x) == FILE_CHG_NONE)
        atomic_store(&b->ctime_ns, after.ctime_ns);
    return FILE_PAIR_OURS;
}

/* What a rename that reported an error actually did, by observation
 * (review N-B): the temp name still naming our fd, with the destination
 * still what it was just before the rename (pre), proves nothing was
 * renamed (a rename removes its source name; pre rules out a foreign
 * rename that moved an installed destination back onto our temp name,
 * R3-A). pre is the recheck's observation without FORCE, which equals the
 * expectation, and a fresh one under FORCE (R4-A);
 * the destination naming our fd with the temp name gone proves it was;
 * anything else is unknown and unlinks nothing. The errno proves neither. */
typedef enum rename_effect { RENAME_NOT_DONE, RENAME_DONE, RENAME_UNKNOWN } rename_effect;

static rename_effect observe_rename(const file_temp *t, const file_expectation *pre)
{
    os_stat mine;
    file_expectation at_tmp, at_dest;
    if (os_file_stat(&t->fd, &mine) != OS_OK ||
        file_expectation_observe_at(&t->dir, t->tmp, &at_tmp, NULL) != FILE_OK ||
        file_expectation_observe_at(&t->dir, t->base, &at_dest, NULL) != FILE_OK)
        return RENAME_UNKNOWN;
    bool tmp_ours = at_tmp.presence == FILE_PRESENT && identity_eq(&at_tmp.stat.id, &mine.id);
    bool dest_ours = at_dest.presence == FILE_PRESENT && identity_eq(&at_dest.stat.id, &mine.id);
    if (tmp_ours && !dest_ours && file_expectation_diff(pre, &at_dest) == FILE_CHG_NONE)
        return RENAME_NOT_DONE;
    if (dest_ours && !tmp_ours) return RENAME_DONE;
    return RENAME_UNKNOWN;
}

static int temp_publish_core(file_temp *t, const prepare_args *hooks,
                             file_save_outcome *out)
{
    ASSERT(t != NULL && out != NULL);
    memset(out, 0, sizeof *out);
    out->pub = FILE_PUB_NONE;
    switch (t->state) {
    case FILE_TEMP_SYNCED:
        break;
    case FILE_TEMP_EMPTY:
    case FILE_TEMP_INSTALLED_UNSYNCED:
    case FILE_TEMP_HELD:
    case FILE_TEMP_DONE:
        return FILE_ERR_STATE; /* at most one rename, only from SYNCED */
    }

    /* The destination just before the rename, in every mode: without
     * FORCE it must equal the expectation; under FORCE it is what the
     * rename may displace. Either way it is the baseline for "nothing
     * was renamed" below. A failed observation refuses, before rename. */
    file_expectation pre;
    {
        os_err oe = OS_OK;
        int rc = file_expectation_observe_at(&t->dir, t->base, &pre, &oe);
        if (rc == FILE_OK && (t->flags & FILE_SAVE_FORCE) == 0 &&
            file_expectation_diff(&t->expect, &pre) != FILE_CHG_NONE)
            rc = FILE_ERR_CHANGED;
        if (rc != FILE_OK) {
            out->pub = FILE_PUB_PRE_RENAME;
            out->oserr = oe;
            temp_release(t, true);
            return rc;
        }
    }

    bool displaced = source_displaced(t);
    os_stat before;
    os_err before_err = displaced ? os_file_stat(&t->source->fd, &before) : OS_E_IO;
    os_rename_mode mode = (t->expect.presence == FILE_ABSENT &&
                           (t->flags & FILE_SAVE_FORCE) == 0)
                              ? OS_RENAME_NOREPLACE
                              : OS_RENAME_REPLACE;
    os_err e = os_rename_at(&t->dir, t->tmp, &t->dir, t->base, mode);
    if (e == OS_E_EXIST || e == OS_E_NOTSUP) {
        /* Definitely nothing renamed. An absent target without atomic
         * exclusion is refused, never a check-then-replace race (Q14). */
        out->pub = FILE_PUB_PRE_RENAME;
        out->oserr = e;
        temp_release(t, true);
        return e == OS_E_EXIST ? FILE_ERR_CHANGED : FILE_ERR_NOTSUP;
    }
    if (e != OS_OK) {
        out->oserr = e;
        switch (observe_rename(t, &pre)) {
        case RENAME_NOT_DONE:
            out->pub = FILE_PUB_PRE_RENAME; /* still ours, definitely unpublished */
            temp_release(t, true);
            return FILE_ERR_IO;
        case RENAME_UNKNOWN:
            out->pub = FILE_PUB_RENAME_UNCERTAIN;
            t->state = FILE_TEMP_HELD; /* evidence: nothing is unlinked */
            return FILE_ERR_IO;
        case RENAME_DONE:
            break; /* performed despite the report */
        }
    }
    t->tmp[0] = '\0'; /* the temp name is gone: nothing there is ours */
    if (hooks != NULL) STEP(hooks->step, hooks->step_ctx, 5 /* RENAMED */);
    if (displaced) t->pair = source_rebaseline(t->source, before_err, &before);
    /* A FOREIGN pair still takes the barrier: the rename happened. */
    return temp_barrier_and_verify(t, hooks, out);
}

static int temp_retry_core(file_temp *t, file_save_outcome *out)
{
    memset(out, 0, sizeof *out);
    out->pub = FILE_PUB_NONE;
    switch (t->state) {
    case FILE_TEMP_INSTALLED_UNSYNCED:
        return temp_barrier_and_verify(t, NULL, out);
    case FILE_TEMP_HELD: {
        /* After an uncertain rename the barrier is owed only once the
         * path is proven to be the held fd; no proof, no sync. */
        os_stat now;
        os_err oe = OS_OK;
        if (!temp_proven(t, &now, &oe)) {
            out->pub = FILE_PUB_CONFLICT;
            out->oserr = oe;
            return FILE_ERR_CONFLICT;
        }
        return temp_barrier_and_verify(t, NULL, out);
    }
    case FILE_TEMP_EMPTY:
    case FILE_TEMP_SYNCED:
    case FILE_TEMP_DONE:
        return FILE_ERR_STATE;
    }
    return FILE_ERR_STATE;
}

/* (E) 64 KiB per pread: the read-back costs size/65536 syscalls, which
 * the CRC over the same bytes dominates; off the input path, not a gate. */
#define VERIFY_CHUNK 65536u

/* Verify an open fd against a whole witness on a stable observation:
 * name -> fd identity, witness identity and size, content CRC32C read
 * back from the fd, then the fd stat and the name's identity re-taken. */
static int witness_check(os_file *fd, const os_dir *dir, const char *name,
                         const file_witness *w, os_stat *out, uint32_t *prefix_crc,
                         os_err *oserr)
{
    os_stat s1, s2;
    file_expectation p;
    os_err e = os_file_stat(fd, &s1);
    if (e != OS_OK) {
        if (oserr != NULL) *oserr = e;
        return FILE_ERR_IO;
    }
    if (s1.kind != OS_KIND_FILE) return FILE_ERR_NOTREG;
    int rc = file_expectation_observe_at(dir, name, &p, oserr);
    if (rc != FILE_OK) return rc;
    if (p.presence != FILE_PRESENT || !identity_eq(&p.stat.id, &s1.id) ||
        s1.id.volume != w->volume || s1.id.index[0] != w->index_low ||
        s1.id.index[1] != w->index_high || s1.size != w->size)
        return FILE_ERR_CHANGED;
    uint32_t tab[256];
    crc_table(tab);
    uint8_t buf[VERIFY_CHUNK];
    uint32_t crc = 0, pcrc = 0;
    uint64_t plen = s1.size < FILE_WIRE_PREFIX_BYTES ? s1.size : FILE_WIRE_PREFIX_BYTES;
    uint64_t off = 0;
    while (off < s1.size) {
        size_t want = (size_t)(s1.size - off < VERIFY_CHUNK ? s1.size - off : VERIFY_CHUNK);
        size_t got = 0;
        e = os_file_pread(fd, buf, want, off, &got);
        if (e != OS_OK) {
            if (oserr != NULL) *oserr = e;
            return FILE_ERR_IO;
        }
        if (got == 0) return FILE_ERR_CHANGED; /* shrank under us */
        if (off < plen)
            pcrc = crc_update(tab, pcrc, buf, (size_t)(plen - off < got ? plen - off : got));
        crc = crc_update(tab, crc, buf, got);
        off += got;
    }
    if (crc != w->content_crc32c) return FILE_ERR_CHANGED;
    e = os_file_stat(fd, &s2);
    if (e != OS_OK) {
        if (oserr != NULL) *oserr = e;
        return FILE_ERR_IO;
    }
    file_expectation x = { .presence = FILE_PRESENT, .stat = s1 };
    file_expectation y = { .presence = FILE_PRESENT, .stat = s2 };
    if (file_expectation_diff(&x, &y) != FILE_CHG_NONE) return FILE_ERR_CHANGED;
    rc = file_expectation_observe_at(dir, name, &p, oserr);
    if (rc != FILE_OK) return rc;
    if (p.presence != FILE_PRESENT || !identity_eq(&p.stat.id, &s2.id))
        return FILE_ERR_CHANGED;
    *out = s2;
    *prefix_crc = pcrc;
    return FILE_OK;
}

int file_temp_prepare(file_temp *t, const char *dest_path,
                      const piece_snapshot *snap, file_source *source,
                      const file_expectation *expect, unsigned flags,
                      os_err *oserr)
{
    ASSERT(t != NULL && dest_path != NULL && snap != NULL && expect != NULL);
    prepare_args a = {
        .dest_path = dest_path, .snap = snap, .expect = expect, .flags = flags,
        .source = source,
    };
#ifdef FILE_TEST_HOOKS
    a.step = sync_step_hook;
    a.step_ctx = sync_step_ctx;
#endif
    return temp_prepare_core(t, &a, oserr);
}

int file_temp_publish(file_temp *t, file_save_outcome *out)
{
    prepare_args hooks = {0};
#ifdef FILE_TEST_HOOKS
    hooks.step = sync_step_hook;
    hooks.step_ctx = sync_step_ctx;
#endif
    return temp_publish_core(t, &hooks, out);
}

int file_temp_retry_barrier(file_temp *t, file_save_outcome *out)
{
    ASSERT(t != NULL && out != NULL);
    return temp_retry_core(t, out);
}

int file_temp_verify(const file_temp *t, file_expectation *installed,
                     uint32_t *prefix_crc32c, os_err *oserr)
{
    ASSERT(t != NULL && installed != NULL && prefix_crc32c != NULL);
    switch (t->state) {
    case FILE_TEMP_INSTALLED_UNSYNCED:
    case FILE_TEMP_HELD:
        break;
    case FILE_TEMP_EMPTY:
    case FILE_TEMP_SYNCED:
    case FILE_TEMP_DONE:
        return FILE_ERR_STATE;
    }
    os_file fd = t->fd; /* a borrowed copy of the handle for reads */
    os_stat st;
    uint32_t pcrc = 0;
    int rc = witness_check(&fd, &t->dir, t->base, &t->witness, &st, &pcrc, oserr);
    if (rc != FILE_OK) return rc;
    installed->presence = FILE_PRESENT;
    installed->stat = st;
    *prefix_crc32c = pcrc;
    return FILE_OK;
}

int file_temp_recover(file_temp *t, const char *path,
                      const file_witness *prepared, os_err *oserr)
{
    ASSERT(t != NULL && path != NULL && prepared != NULL);
    switch (t->state) {
    case FILE_TEMP_EMPTY:
    case FILE_TEMP_DONE:
        break;
    case FILE_TEMP_SYNCED:
    case FILE_TEMP_INSTALLED_UNSYNCED:
    case FILE_TEMP_HELD:
        return FILE_ERR_STATE;
    }
    temp_reset(t);
    char dirpath[OS_PATH_MAX];
    os_err e = split_path(path, dirpath, sizeof dirpath, t->base, sizeof t->base);
    if (e == OS_OK) e = os_dir_open(&t->dir, dirpath);
    if (e == OS_OK)
        e = os_file_open_at(&t->fd, &t->dir, t->base,
                            OS_OPEN_READ | OS_OPEN_NONBLOCK | OS_OPEN_NOFOLLOW);
    int rc = FILE_ERR_IO;
    if (e != OS_OK) {
        if (oserr != NULL) *oserr = e;
        /* What the name is decides the class, never a retryable I/O error
         * (review N-F, R3-C): a symlink or directory is not a regular file;
         * an absent or unreadable name is not the installed candidate, and
         * would refuse again on every restart. */
        if (e == OS_E_LOOP || e == OS_E_ISDIR) rc = FILE_ERR_NOTREG;
        if (e == OS_E_NOENT || e == OS_E_NOTDIR || e == OS_E_ACCESS) rc = FILE_ERR_CHANGED;
    } else {
        rc = witness_check(&t->fd, &t->dir, t->base, prepared, &t->fd_stat, &(uint32_t){0}, oserr);
    }
    if (rc != FILE_OK) {
        temp_release(t, false);
        temp_reset(t); /* EMPTY */
        return rc;
    }
    t->witness = *prepared;
    t->state = FILE_TEMP_INSTALLED_UNSYNCED;
    return FILE_OK;
}

void file_temp_discard(file_temp *t)
{
    ASSERT(t != NULL);
    switch (t->state) {
    case FILE_TEMP_EMPTY:
    case FILE_TEMP_DONE:
        return;
    case FILE_TEMP_SYNCED:
        temp_release(t, true); /* still ours, definitely unpublished */
        return;
    case FILE_TEMP_INSTALLED_UNSYNCED:
    case FILE_TEMP_HELD:
        temp_release(t, false);
        return;
    }
}

void file_temp_forget(file_temp *t)
{
    ASSERT(t != NULL);
    temp_reset(t);
}

int file_save_write(const char *dest_path, const piece_snapshot *snap,
                    file_source *source, const file_expectation *expect,
                    unsigned flags, file_witness *witness,
                    file_save_outcome *out)
{
    ASSERT(dest_path != NULL && snap != NULL && expect != NULL && out != NULL);
    memset(out, 0, sizeof *out);
    out->pub = FILE_PUB_NONE;
    file_temp t;
    temp_reset(&t);
    os_err oe = OS_OK;
    int rc = file_temp_prepare(&t, dest_path, snap, source, expect, flags, &oe);
    if (rc != FILE_OK) {
        out->pub = FILE_PUB_PRE_RENAME;
        out->oserr = oe;
        return rc;
    }
    if (witness != NULL) *witness = t.witness;
    rc = file_temp_publish(&t, out);
    /* The one-shot form discards retained custody: the outcome carries
     * any barrier or reconciliation obligation (callers needing the
     * retry use the file_temp_* core). */
    file_temp_discard(&t);
    return rc;
}

/* ---- messages ------------------------------------------------------------- */

typedef struct file_msg_wire {
    file *f;
    int32_t status;
    uint32_t mode;
    uint64_t size;
    int32_t oserr;
} file_msg_wire;
_Static_assert(sizeof(file_msg_wire) <= WORK_MSG_DATA, "wire fits work_msg.data");

static work_msg msg_of(const work_ctx *ctx, uint32_t kind, const file_msg_wire *wire)
{
    work_msg m;
    memset(&m, 0, sizeof m);
    m.kind = kind;
    m.generation = ctx->generation;
    memcpy(m.data, wire, sizeof *wire);
    return m;
}

/* Publish with backpressure: on a full mailbox (and not cancelled) the
 * job retains its immutable result and retries once per continuation
 * invocation (work_continue); it never sleeps (§1.5(c)). The job touches
 * the file object only before this call. */
static void publish_or_continue(work_ctx *ctx, uint32_t kind, const file_msg_wire *wire)
{
    work_msg m = msg_of(ctx, kind, wire);
    if (work_publish(ctx, &m)) return;
    if (work_should_stop(ctx)) return;
    (void)work_continue(ctx);
}

/* ---- acquisition (open and reload share the worker) ----------------------- */

static void acquired_dispose(struct file_acquired *r)
{
    if (r->backing != NULL) backing_release_cb(r->backing);
    if (r->prefix != NULL) base_arena_free(&r->prefix_arena);
    memset(r, 0, sizeof *r);
}

/* Read exactly n bytes at offset (short read = changed). */
static int read_exact(os_file *fd, uint8_t *buf, uint64_t n, const work_ctx *stop,
                      os_err *oserr)
{
    uint64_t off = 0;
    while (off < n) {
        if (stopped(stop)) return FILE_ERR_CANCELLED;
        size_t want = (size_t)((n - off > FILE_PREFIX_MAX) ? FILE_PREFIX_MAX : (n - off));
        size_t got = 0;
        os_err e = os_file_pread(fd, buf + off, want, off, &got);
        if (e != OS_OK) {
            if (oserr != NULL) *oserr = e;
            return FILE_ERR_IO;
        }
        if (got == 0) return FILE_ERR_CHANGED; /* premature EOF */
        off += got;
    }
    return FILE_OK;
}

static file_msg_wire acquired_wire(file *f, const struct file_acquired *r)
{
    file_msg_wire w = { .f = f, .status = r->status, .mode = (uint32_t)r->mode,
                        .size = r->size, .oserr = (int32_t)r->oserr };
    return w;
}

static uint32_t acquired_kind(const struct file_acquired *r, bool is_open)
{
    if (is_open) return r->status == FILE_OK ? FILE_MSG_OPEN_READY : FILE_MSG_OPEN_FAILED;
    return r->status == FILE_OK ? FILE_MSG_RELOAD_READY : FILE_MSG_RELOAD_FAILED;
}

/* The acquisition job. The prefix part of the record is final once
 * PREFIX_READY is posted: from then on this job writes only backing,
 * status and oserr, and never frees the prefix (the UI may be reading
 * it; only the UI frees the record). */
static void acquire_work(file *f, file_job_slot *slot, struct file_acquired *r,
                         const char *requested, work_ctx *ctx, bool is_open)
{
    memset(r, 0, sizeof *r);
    r->status = FILE_OK;
    os_file fd = OS_FILE_INIT;
    os_dir dir = OS_DIR_INIT;
    os_stat first;
    char leaf[OS_NAME_MAX], dirpath[OS_PATH_MAX];

    if (os_path_canonical(requested, r->path, sizeof r->path) != OS_OK) {
        /* Report the precise reason through a plain stat. */
        file_expectation probe;
        os_err oe = OS_E_NOENT;
        if (file_expectation_observe_path(requested, &probe, &oe) == FILE_OK &&
            probe.presence == FILE_PRESENT && probe.stat.kind != OS_KIND_FILE)
            r->status = FILE_ERR_NOTREG;
        else {
            r->status = FILE_ERR_IO;
            r->oserr = oe;
        }
        goto done;
    }
    {
        os_err e = split_path(r->path, dirpath, sizeof dirpath, leaf, sizeof leaf);
        if (e != OS_OK) {
            /* The root (or any directory name) is not a regular file. */
            r->status = e == OS_E_ISDIR ? FILE_ERR_NOTREG : FILE_ERR_IO;
            r->oserr = e;
            goto done;
        }
        e = os_dir_open(&dir, dirpath);
        if (e == OS_OK)
            e = os_file_open_at(&fd, &dir, leaf, OS_OPEN_READ | OS_OPEN_NONBLOCK);
        if (e != OS_OK) {
            r->status = e == OS_E_ISDIR ? FILE_ERR_NOTREG : FILE_ERR_IO;
            r->oserr = e;
            goto done;
        }
        e = os_file_stat(&fd, &first);
        if (e != OS_OK) {
            r->status = FILE_ERR_IO;
            r->oserr = e;
            goto done;
        }
    }
    if (first.kind != OS_KIND_FILE) {
        r->status = FILE_ERR_NOTREG;
        goto done;
    }
    {
        /* The canonical entry must name the opened inode. */
        os_stat at_path;
        if (os_stat_at(&dir, leaf, OS_STAT_NOFOLLOW, &at_path) != OS_OK ||
            !identity_eq(&at_path.id, &first.id)) {
            r->status = FILE_ERR_CHANGED;
            goto done;
        }
    }
    r->size = first.size;
    r->baseline.presence = FILE_PRESENT;
    r->baseline.stat = first;
    r->mode = (first.size < f->threshold || first.size == 0) ? FILE_MODE_COPY
                                                             : FILE_MODE_MMAP;
    {
        size_t plen = first.size < FILE_PREFIX_MAX ? (size_t)first.size
                                                   : (size_t)FILE_PREFIX_MAX;
        if (plen > 0) {
            if (base_arena_init(&r->prefix_arena, plen) != 0) {
                r->status = FILE_ERR_NOMEM;
                goto done;
            }
            r->prefix = base_arena_alloc(&r->prefix_arena, plen, 1);
            if (r->prefix == NULL) {
                base_arena_free(&r->prefix_arena);
                r->status = FILE_ERR_NOMEM;
                goto done;
            }
            int rc = read_exact(&fd, r->prefix, plen, ctx, &r->oserr);
            if (rc != FILE_OK) {
                r->status = rc;
                goto done;
            }
            r->prefix_len = plen;
            file_prefix_scan(r->prefix, plen, &r->info);
        }
    }
    if (is_open) {
        /* Advisory single attempt: the final message installs the same
         * fields, so a dropped prefix costs latency, never state. */
        file_msg_wire w = acquired_wire(f, r);
        work_msg m = msg_of(ctx, FILE_MSG_PREFIX_READY, &w);
        (void)work_publish(ctx, &m);
    }

    switch (r->mode) {
    case FILE_MODE_COPY: {
        uint8_t *bytes = NULL;
        file_source *b = backing_new(FILE_MODE_COPY, (size_t)first.size, &bytes);
        if (b == NULL) {
            r->status = FILE_ERR_NOMEM;
            goto done;
        }
        os_err oe = OS_OK;
        int rc = read_exact(&fd, bytes, first.size, ctx, &oe);
        if (rc != FILE_OK) {
            backing_release_cb(b);
            r->oserr = oe;
            r->status = rc;
            goto done;
        }
        b->fd = fd;
        fd = (os_file)OS_FILE_INIT;
        backing_set_stat(b, &first);
        r->backing = b;
        break;
    }
    case FILE_MODE_MMAP: {
        os_map view;
        os_err e = os_map_file(&view, &fd, 0, (size_t)first.size);
        if (e != OS_OK) {
            r->status = FILE_ERR_IO;
            r->oserr = e;
            goto done;
        }
        /* Registered BEFORE publication; on refusal the unpublished
         * map and fd are disposed and the open is refused (Q12). */
        os_fault_id fid;
        e = os_fault_register(&view, &fid);
        if (e != OS_OK) {
            os_unmap(&view);
            r->status = e == OS_E_NOSPC ? FILE_ERR_NOSPC : FILE_ERR_IO;
            r->oserr = e;
            goto done;
        }
        file_source *b = backing_new(FILE_MODE_MMAP, 0, NULL);
        if (b == NULL) {
            os_fault_retire(fid);
            os_unmap(&view);
            r->status = FILE_ERR_NOMEM;
            goto done;
        }
        b->view = view;
        b->fd = fd;
        fd = (os_file)OS_FILE_INIT;
        b->backing.bytes = view.addr;
        b->backing.len = view.len;
        b->backing.fault = fid;
        backing_set_stat(b, &first);
        r->backing = b;
        break;
    }
    }
    {
        /* Validate descriptor and canonical entry after the read/map. */
        os_stat after, at_path;
        file_expectation now = { .presence = FILE_PRESENT };
        if (os_file_stat(&r->backing->fd, &after) != OS_OK ||
            (now.stat = after, file_expectation_diff(&r->baseline, &now) != FILE_CHG_NONE) ||
            os_stat_at(&dir, leaf, OS_STAT_NOFOLLOW, &at_path) != OS_OK ||
            !identity_eq(&at_path.id, &first.id)) {
            r->status = FILE_ERR_CHANGED;
        }
        if (r->status == FILE_OK && stopped(ctx)) r->status = FILE_ERR_CANCELLED;
    }
done:
    os_file_close(&fd);
    os_dir_close(&dir);
    if (r->status != FILE_OK && r->backing != NULL) {
        /* Never published: no reader exists. The prefix stays. */
        backing_release_cb(r->backing);
        r->backing = NULL;
    }
    atomic_store(&slot->progress, FILE_JOB_RESULT_READY);
    file_msg_wire w = acquired_wire(f, r);
    publish_or_continue(ctx, acquired_kind(r, is_open), &w);
}

static void open_job_fn(work_ctx *ctx)
{
    file *f = ctx->arg;
    if (atomic_load(&f->open_job.progress) == FILE_JOB_RESULT_READY) {
        file_msg_wire w = acquired_wire(f, &f->open_result);
        publish_or_continue(ctx, acquired_kind(&f->open_result, true), &w);
        return;
    }
    acquire_work(f, &f->open_job, &f->open_result, f->requested_path, ctx, true);
}

static void reload_job_fn(work_ctx *ctx)
{
    file *f = ctx->arg;
    if (atomic_load(&f->reload_job.progress) == FILE_JOB_RESULT_READY) {
        file_msg_wire w = acquired_wire(f, &f->reload_result);
        publish_or_continue(ctx, acquired_kind(&f->reload_result, false), &w);
        return;
    }
    acquire_work(f, &f->reload_job, &f->reload_result, f->path, ctx, false);
}

/* ---- save jobs -------------------------------------------------------------- */

static void save_post(file *f, work_ctx *ctx)
{
    file_msg_wire w = { .f = f, .status = f->save_status,
                        .mode = (uint32_t)f->save_outcome.pub,
                        .size = f->save_temp.witness.size,
                        .oserr = (int32_t)f->save_outcome.oserr };
    publish_or_continue(ctx, f->save_msg_kind, &w);
}

/* Off the UI: rewrite the destination with a canonical directory, so a
 * relative or symlinked spelling of file_path(f) is recognized as the
 * file's own path at settle (review N-D). The leaf is kept: a symlink
 * leaf is replaced, never followed, so it is another path. Left as given
 * when the directory cannot be resolved (prepare then refuses it). */
static void canonical_dest(char dest[OS_PATH_MAX])
{
    char dir[OS_PATH_MAX], leaf[OS_NAME_MAX], canon[OS_PATH_MAX];
    if (split_path(dest, dir, sizeof dir, leaf, sizeof leaf) != OS_OK ||
        os_path_canonical(dir, canon, sizeof canon) != OS_OK)
        return;
    size_t dn = strlen(canon), ln = strlen(leaf);
    bool root = dn == 1 && canon[0] == '/';
    if (dn + (root ? 0u : 1u) + ln >= OS_PATH_MAX) return;
    if (!root) canon[dn++] = '/';
    memcpy(canon + dn, leaf, ln + 1);
    copy_str(dest, canon);
}

static void save_job_fn(work_ctx *ctx)
{
    file *f = ctx->arg;
    if (atomic_load(&f->save_job.progress) == FILE_JOB_RESULT_READY) {
        save_post(f, ctx);
        return;
    }
    prepare_args hooks = {0};
#ifdef FILE_TEST_HOOKS
    hooks.step = f->step_hook;
    hooks.step_ctx = f->step_ctx;
#endif
    uint32_t kind = FILE_MSG_SAVE_DONE;
    switch (f->save_op) {
    case FILE_SAVE_OP_PREPARE: {
        canonical_dest(f->save_dest);
        prepare_args a = hooks;
        a.dest_path = f->save_dest;
        a.snap = f->save_snap;
        a.expect = &f->save_expect;
        a.flags = f->save_flags;
        a.source = f->save_source;
        a.stop = ctx;
        os_err oe = OS_OK;
        int rc = temp_prepare_core(&f->save_temp, &a, &oe);
        f->save_status = rc;
        f->save_outcome.pub = rc == FILE_OK ? FILE_PUB_NONE : FILE_PUB_PRE_RENAME;
        f->save_outcome.oserr = oe;
        if (rc == FILE_OK) kind = FILE_MSG_SAVE_PREPARED;
        break;
    }
    case FILE_SAVE_OP_PUBLISH:
        f->save_status = temp_publish_core(&f->save_temp, &hooks, &f->save_outcome);
        break;
    case FILE_SAVE_OP_RETRY:
        f->save_status = temp_retry_core(&f->save_temp, &f->save_outcome);
        break;
    }
    f->save_msg_kind = kind;
    atomic_store(&f->save_job.progress, FILE_JOB_RESULT_READY);
    save_post(f, ctx);
}

static int submit_job(file *f, file_job_slot *slot, void (*fn)(work_ctx *),
                      uint32_t generation)
{
    atomic_store(&slot->progress, FILE_JOB_WORKING);
    work_job job = { .fn = fn, .arg = f, .generation = generation, .cls = WORK_BULK };
    work_handle h = work_submit(f->pool, job);
    if (h.epoch == 0) return FILE_ERR_POOL;
    slot->h = h;
    slot->generation = generation;
    return FILE_OK;
}

static bool slot_finished(const file *f, const file_job_slot *slot)
{
    return slot->h.epoch == 0 || work_handle_finished(f->pool, slot->h);
}

static void save_release_inputs(file *f)
{
    if (f->save_snap != NULL) {
        piece_snapshot_release(f->save_snap);
        f->save_snap = NULL;
    }
    file_source_release(f->save_source);
    f->save_source = NULL;
}

/* The phase a settled job leaves, from custody alone. A cancelled job's
 * synced, unpublished temp is discarded; every exposed effect is kept. */
static file_save_phase settled_phase(const file *f, bool cancelled)
{
    switch (f->save_temp.state) {
    case FILE_TEMP_SYNCED:
        return cancelled ? FILE_SAVE_IDLE : FILE_SAVE_PREPARED;
    case FILE_TEMP_INSTALLED_UNSYNCED:
        return FILE_SAVE_UNSYNCED;
    case FILE_TEMP_HELD:
        return FILE_SAVE_HELD;
    case FILE_TEMP_EMPTY:
    case FILE_TEMP_DONE:
        return FILE_SAVE_IDLE;
    }
    return FILE_SAVE_IDLE;
}

/* UI: install a finished save job's result, from its message or, after
 * a cancel, from the record once the handle is physically finished. */
static void save_settle(file *f, bool cancelled)
{
    save_release_inputs(f); /* only the prepare reads them */
    file_save_phase ph = settled_phase(f, cancelled);
    switch (f->save_temp.state) {
    case FILE_TEMP_SYNCED:
        if (cancelled) file_temp_discard(&f->save_temp);
        break;
    case FILE_TEMP_HELD:
        if (f->save_outcome.pub == FILE_PUB_VERIFIED && strcmp(f->save_dest, f->path) == 0) {
            f->baseline = f->save_outcome.installed;
            f->changed = false;
            f->last_reasons = FILE_CHG_NONE;
        }
        break;
    case FILE_TEMP_EMPTY:
    case FILE_TEMP_INSTALLED_UNSYNCED:
    case FILE_TEMP_DONE:
        break;
    }
    if (f->save_status != FILE_OK) f->oserr = f->save_outcome.oserr;
    f->save_phase = ph;
}

/* Settle a cancelled save whose job has physically finished. Non-const
 * entry points only: it may unlink and close (review N5). */
static void save_reap(file *f)
{
    switch (f->save_phase) {
    case FILE_SAVE_CANCELLING:
        if (work_handle_finished(f->pool, f->save_job.h)) save_settle(f, true);
        return;
    case FILE_SAVE_IDLE:
    case FILE_SAVE_PREPARING:
    case FILE_SAVE_PREPARED:
    case FILE_SAVE_PUBLISHING:
    case FILE_SAVE_UNSYNCED:
    case FILE_SAVE_HELD:
        return;
    }
}

/* ---- decode / install ------------------------------------------------------- */

static void install_prefix(file *f, const struct file_acquired *r)
{
    /* Borrowed: the record keeps the bytes; only the UI frees it. */
    f->prefix = r->prefix;
    f->prefix_len = r->prefix_len;
    f->info = r->info;
    copy_str(f->path, r->path);
    f->baseline = r->baseline;
    f->mode = r->mode;
    f->size = r->size;
    f->open_state = FILE_OPEN_PREFIX;
}

static void install_open(file *f, struct file_acquired *r)
{
    copy_str(f->path, r->path);
    f->baseline = r->baseline;
    f->mode = r->mode;
    f->size = r->size;
    f->info = r->info;
    f->prefix_arena = r->prefix_arena;
    f->prefix = r->prefix;
    f->prefix_len = r->prefix_len;
    f->backing = r->backing;
    memset(r, 0, sizeof *r); /* moved */
    f->open_state = FILE_OPEN_READY;
}

static void withdraw_open(file *f, struct file_acquired *r, os_err oserr)
{
    f->prefix = NULL;
    f->prefix_len = 0;
    memset(&f->info, 0, sizeof f->info);
    f->path[0] = '\0';
    memset(&f->baseline, 0, sizeof f->baseline);
    f->size = 0;
    acquired_dispose(r);
    f->oserr = oserr;
    f->open_state = FILE_OPEN_FAILED;
}

int file_msg_decode(const work_msg *m, file_msg *out)
{
    ASSERT(m != NULL && out != NULL);
    file_msg_wire w;
    switch (m->kind) {
    case FILE_MSG_OPEN_READY:
    case FILE_MSG_OPEN_FAILED:
    case FILE_MSG_SAVE_DONE:
    case FILE_MSG_PREFIX_READY:
    case FILE_MSG_SAVE_PREPARED:
    case FILE_MSG_RELOAD_READY:
    case FILE_MSG_RELOAD_FAILED:
        break;
    default:
        return 1;
    }
    memcpy(&w, m->data, sizeof w);
    file *f = w.f;
    if (f == NULL) return 1;
    const file_job_slot *slot;
    switch (m->kind) {
    case FILE_MSG_OPEN_READY:
    case FILE_MSG_OPEN_FAILED:
    case FILE_MSG_PREFIX_READY:
        slot = &f->open_job;
        break;
    case FILE_MSG_RELOAD_READY:
    case FILE_MSG_RELOAD_FAILED:
        slot = &f->reload_job;
        break;
    default:
        slot = &f->save_job;
        break;
    }
    /* Only the job this slot names (review N8): its work slot, epoch and
     * generation. A superseded job or reused storage is refused. */
    if (slot->h.epoch == 0 || m->slot_ != slot->h.slot || m->epoch_ != slot->h.epoch ||
        m->generation != slot->generation)
        return 1;
    memset(out, 0, sizeof *out);
    out->kind = m->kind;
    out->generation = m->generation;
    out->f = f;
    out->status = w.status;
    out->mode = w.mode;
    out->size = w.size;
    out->oserr = w.oserr;

    switch (m->kind) {
    case FILE_MSG_PREFIX_READY:
        if (f->open_state != FILE_OPEN_NONE) return 1;
        install_prefix(f, &f->open_result);
        return 0;
    case FILE_MSG_OPEN_READY:
    case FILE_MSG_OPEN_FAILED:
        switch (f->open_state) {
        case FILE_OPEN_NONE:
        case FILE_OPEN_PREFIX:
            break;
        case FILE_OPEN_READY:
        case FILE_OPEN_ATTACHED:
        case FILE_OPEN_FAILED:
            return 1;
        }
        if (w.status == FILE_OK) install_open(f, &f->open_result);
        else withdraw_open(f, &f->open_result, (os_err)w.oserr);
        return 0;
    case FILE_MSG_RELOAD_READY:
    case FILE_MSG_RELOAD_FAILED:
        if (f->reload_state != FILE_RELOAD_ACQUIRING) return 1;
        if (w.status == FILE_OK) {
            f->reload_state = FILE_RELOAD_READY;
        } else {
            f->oserr = (os_err)w.oserr;
            acquired_dispose(&f->reload_result);
            f->reload_state = FILE_RELOAD_NONE;
        }
        return 0;
    default:
        switch (f->save_phase) {
        case FILE_SAVE_PREPARING:
        case FILE_SAVE_PUBLISHING:
            save_settle(f, false);
            return 0;
        case FILE_SAVE_IDLE:
        case FILE_SAVE_PREPARED:
        case FILE_SAVE_UNSYNCED:
        case FILE_SAVE_HELD:
        case FILE_SAVE_CANCELLING:
            return 1;
        }
        return 1;
    }
}

/* ---- open / close / reload -------------------------------------------------- */

int file_open_begin(file *f, work_pool *pool, const char *path,
                    const file_open_opts *opts)
{
    ASSERT(f != NULL && pool != NULL && path != NULL);
    ASSERT(f->pool == NULL); /* zeroed caller storage */
    f->watch = (os_watch)OS_WATCH_INIT;
    f->save_temp.fd = (os_file)OS_FILE_INIT;
    f->save_temp.dir = (os_dir)OS_DIR_INIT;
    if (bounded_len(path, OS_PATH_MAX) >= (size_t)OS_PATH_MAX) {
        f->oserr = OS_E_NAMETOOLONG;
        return FILE_ERR_IO;
    }
    f->pool = pool;
    f->open_generation = opts != NULL ? opts->generation : 0;
    f->threshold = opts != NULL && opts->copy_threshold != 0
                       ? opts->copy_threshold
                       : file_default_copy_threshold();
    copy_str(f->requested_path, path);
    return submit_job(f, &f->open_job, open_job_fn, f->open_generation);
}

void file_close_begin(file *f)
{
    ASSERT(f != NULL);
    if (f->pool == NULL) return;
    file_watch_stop(f);
    /* Every handle, finished or not: a finished job's undrained message
     * then becomes stale in the pool too (header MESSAGES). */
    file_job_slot *slots[] = { &f->open_job, &f->reload_job, &f->save_job };
    for (size_t i = 0; i < sizeof slots / sizeof slots[0]; i++)
        if (slots[i]->h.epoch != 0) work_cancel(f->pool, slots[i]->h);
    switch (f->save_phase) {
    case FILE_SAVE_PREPARING:
    case FILE_SAVE_PUBLISHING:
        f->save_phase = FILE_SAVE_CANCELLING;
        break;
    case FILE_SAVE_IDLE:
    case FILE_SAVE_PREPARED:
    case FILE_SAVE_UNSYNCED:
    case FILE_SAVE_HELD:
    case FILE_SAVE_CANCELLING:
        break;
    }
}

bool file_close_ready(file *f)
{
    ASSERT(f != NULL);
    if (f->pool == NULL) return true;
    return slot_finished(f, &f->open_job) && slot_finished(f, &f->reload_job) &&
           slot_finished(f, &f->save_job);
}

void file_close_finish(file *f)
{
    ASSERT(f != NULL);
    if (f->pool != NULL) {
        bool ready = file_close_ready(f);
        ASSERT(ready);
        file_watch_stop(f);
        save_reap(f);
        file_temp_discard(&f->save_temp);
        save_release_inputs(f);
        acquired_dispose(&f->open_result);
        acquired_dispose(&f->reload_result);
        if (f->backing != NULL) backing_release_cb(f->backing);
        switch (f->open_state) {
        case FILE_OPEN_READY:
        case FILE_OPEN_ATTACHED:
            if (f->prefix != NULL) base_arena_free(&f->prefix_arena);
            break;
        case FILE_OPEN_NONE:
        case FILE_OPEN_PREFIX:  /* borrowed: the record freed it above */
        case FILE_OPEN_FAILED:
            break;
        }
    }
    memset(f, 0, sizeof *f);
}

const uint8_t *file_prefix(const file *f, size_t *len)
{
    ASSERT(f != NULL);
    if (len != NULL) *len = f->prefix_len;
    return f->prefix;
}
bool file_prefix_ready(const file *f)
{
    ASSERT(f != NULL);
    switch (f->open_state) {
    case FILE_OPEN_PREFIX:
    case FILE_OPEN_READY:
    case FILE_OPEN_ATTACHED:
        return true;
    case FILE_OPEN_NONE:
    case FILE_OPEN_FAILED:
        return false;
    }
    return false;
}
const file_prefix_info *file_prefix_info_of(const file *f)
{ ASSERT(f != NULL); return &f->info; }
uint64_t file_size(const file *f) { ASSERT(f != NULL); return f->size; }
file_mode file_open_mode(const file *f) { ASSERT(f != NULL); return f->mode; }
const char *file_path(const file *f) { ASSERT(f != NULL); return f->path; }
os_err file_os_err(const file *f) { ASSERT(f != NULL); return f->oserr; }
bool file_open_ready(const file *f) { ASSERT(f != NULL); return open_installed(f); }
const file_expectation *file_baseline(const file *f)
{ ASSERT(f != NULL); return &f->baseline; }

static int tree_from_backing(piece_reserve *reserve, file_source *b, piece_tree **out)
{
    piece_err e = PIECE_ERR_NOMEM;
    switch (b->mode) {
    case FILE_MODE_COPY:
        e = piece_open_copy(reserve, b->backing.bytes, b->backing.len, out);
        break;
    case FILE_MODE_MMAP:
        e = piece_open_mapped(reserve, &b->backing, out);
        break;
    }
    return e == PIECE_OK ? FILE_OK : FILE_ERR_NOMEM;
}

int file_open_tree(file *f, piece_reserve *reserve, piece_tree **out)
{
    ASSERT(f != NULL && reserve != NULL && out != NULL);
    *out = NULL;
    switch (f->open_state) {
    case FILE_OPEN_READY:
        break;
    case FILE_OPEN_NONE:
    case FILE_OPEN_PREFIX:
    case FILE_OPEN_ATTACHED:
    case FILE_OPEN_FAILED:
        return FILE_ERR_STATE;
    }
    if (f->backing == NULL) return FILE_ERR_STATE;
    int rc = tree_from_backing(reserve, f->backing, out);
    if (rc == FILE_OK) f->open_state = FILE_OPEN_ATTACHED;
    return rc;
}

int file_reload_begin(file *f, uint32_t generation)
{
    ASSERT(f != NULL);
    if (!open_installed(f)) return FILE_ERR_STATE;
    switch (f->reload_state) {
    case FILE_RELOAD_NONE:
        break;
    case FILE_RELOAD_ACQUIRING:
    case FILE_RELOAD_READY:
        return FILE_ERR_BUSY;
    }
    int rc = submit_job(f, &f->reload_job, reload_job_fn, generation);
    if (rc == FILE_OK) f->reload_state = FILE_RELOAD_ACQUIRING;
    return rc;
}

int file_reload_tree(file *f, piece_reserve *reserve, piece_tree **out)
{
    ASSERT(f != NULL && reserve != NULL && out != NULL);
    *out = NULL;
    switch (f->reload_state) {
    case FILE_RELOAD_READY:
        break;
    case FILE_RELOAD_NONE:
    case FILE_RELOAD_ACQUIRING:
        return FILE_ERR_STATE;
    }
    struct file_acquired *r = &f->reload_result;
    int rc = tree_from_backing(reserve, r->backing, out);
    if (rc != FILE_OK) return rc;
    /* Adopt: the old tree keeps its own backing reference. */
    if (f->backing != NULL) backing_release_cb(f->backing);
    if (f->prefix != NULL) base_arena_free(&f->prefix_arena);
    copy_str(f->path, r->path);
    f->backing = r->backing;
    f->prefix_arena = r->prefix_arena;
    f->prefix = r->prefix;
    f->prefix_len = r->prefix_len;
    f->info = r->info;
    f->baseline = r->baseline;
    f->mode = r->mode;
    f->size = r->size;
    f->changed = false;
    f->last_reasons = FILE_CHG_NONE;
    memset(r, 0, sizeof *r); /* moved */
    f->reload_state = FILE_RELOAD_NONE;
    f->open_state = FILE_OPEN_ATTACHED;
    return FILE_OK;
}

void file_reload_discard(file *f)
{
    ASSERT(f != NULL);
    switch (f->reload_state) {
    case FILE_RELOAD_READY:
        acquired_dispose(&f->reload_result);
        f->reload_state = FILE_RELOAD_NONE;
        return;
    case FILE_RELOAD_NONE:
    case FILE_RELOAD_ACQUIRING:
        return;
    }
}

/* ---- change detection ------------------------------------------------------- */

bool file_check(file *f, uint32_t *reasons)
{
    ASSERT(f != NULL);
    if (f->pool != NULL) save_reap(f);
    uint32_t bits = FILE_CHG_NONE;
    if (file_prefix_ready(f)) {
        file_expectation now;
        os_err oe = OS_OK;
        if (file_expectation_observe_path(f->path, &now, &oe) != FILE_OK) {
            bits |= FILE_CHG_METADATA; /* a failed observation is a change */
            f->oserr = oe;
        } else {
            bits |= file_expectation_diff(&f->baseline, &now);
        }
    }
    /* The source leg: the mapped inode the text reads. A copy buffer's old
     * inode backs nothing on screen, so it has no source leg (review MF-F;
     * the rule of file.h file_temp_prepare). The path leg above sees every
     * change at the path for both kinds. */
    if (f->backing != NULL) {
        file_source *b = f->backing;
        switch (b->mode) {
        case FILE_MODE_COPY:
            break;
        case FILE_MODE_MMAP: {
            if (backing_poll_changed(b)) bits |= FILE_CHG_FAULTED;
            file_expectation want = { .presence = FILE_PRESENT, .stat = source_baseline(b) };
            file_expectation got;
            os_err oe = OS_OK;
            if (file_expectation_observe_fd(&b->fd, &got, &oe) != FILE_OK) {
                bits |= FILE_CHG_METADATA; /* review N3 */
                f->oserr = oe;
            } else {
                bits |= file_expectation_diff(&want, &got); /* ctime included (MF6) */
                if (got.stat.size < b->backing.len) bits |= FILE_CHG_TRUNCATED;
            }
            break;
        }
        }
    }
    f->last_reasons = bits;
    if (bits != FILE_CHG_NONE) f->changed = true;
    if (reasons != NULL) *reasons = bits;
    return f->changed;
}

bool file_changed(const file *f) { ASSERT(f != NULL); return f->changed; }

int file_resolve_keep(file *f)
{
    ASSERT(f != NULL);
    if (!file_prefix_ready(f)) return FILE_ERR_STATE;
    file_expectation now;
    os_err oe = OS_OK;
    int rc = file_expectation_observe_path(f->path, &now, &oe);
    if (rc != FILE_OK) {
        f->oserr = oe;
        return rc;
    }
    if (now.presence == FILE_PRESENT && now.stat.kind != OS_KIND_FILE)
        return FILE_ERR_CHANGED;
    if (f->backing != NULL) {
        switch (f->backing->mode) {
        case FILE_MODE_COPY:
            break; /* a private original can keep any disk state */
        case FILE_MODE_MMAP:
            /* The mapped inode must still validate in full. Any ctime
             * move, a dropped link included, is indistinguishable from an
             * in-place rewrite with mtime restored (MF6): refuse. */
            rc = file_source_validate(f->backing, &oe);
            if (rc == FILE_ERR_IO) f->oserr = oe;
            if (rc != FILE_OK) return rc;
            break;
        }
    }
    f->baseline = now;
    f->changed = false;
    f->last_reasons = FILE_CHG_NONE;
    return FILE_OK;
}

int file_watch_start(file *f)
{
    ASSERT(f != NULL);
    if (!file_prefix_ready(f)) return FILE_ERR_STATE;
    switch (f->watch_state) {
    case FILE_WATCH_ON:
        return FILE_OK;
    case FILE_WATCH_OFF:
        break;
    }
    char dirpath[OS_PATH_MAX];
    os_err e = split_path(f->path, dirpath, sizeof dirpath, f->watch_name, sizeof f->watch_name);
    if (e == OS_OK && f->watch.fd < 0) e = os_watch_init(&f->watch, &f->watch_slot, 1);
    if (e == OS_OK) e = os_watch_add(&f->watch, dirpath, &f->watch_id);
    if (e != OS_OK) {
        f->oserr = e;
        return FILE_ERR_IO;
    }
    f->watch_state = FILE_WATCH_ON;
    return FILE_OK;
}

void file_watch_stop(file *f)
{
    ASSERT(f != NULL);
    switch (f->watch_state) {
    case FILE_WATCH_ON:
        os_watch_remove(&f->watch, f->watch_id);
        f->watch_state = FILE_WATCH_OFF;
        break;
    case FILE_WATCH_OFF:
        break;
    }
    if (f->watch.fd >= 0) os_watch_close(&f->watch);
}

int file_watch_fd(const file *f)
{
    ASSERT(f != NULL);
    switch (f->watch_state) {
    case FILE_WATCH_ON:
        return os_watch_fd(&f->watch);
    case FILE_WATCH_OFF:
        return -1;
    }
    return -1;
}

bool file_watch_poll(file *f)
{
    ASSERT(f != NULL);
    switch (f->watch_state) {
    case FILE_WATCH_OFF:
        return f->changed;
    case FILE_WATCH_ON:
        break;
    }
    for (;;) {
        os_watch_event ev[8];
        size_t n = 0;
        os_err e = os_watch_read(&f->watch, ev, 8, &n);
        bool hit = e == OS_E_OVERFLOW;
        for (size_t i = 0; i < n; i++) {
            if (ev[i].id != f->watch_id) continue;
            if (ev[i].name[0] == '\0' || strcmp(ev[i].name, f->watch_name) == 0)
                hit = true;
        }
        if (hit) (void)file_check(f, NULL);
        if (e != OS_OK || n < 8) return f->changed;
    }
}

/* ---- asynchronous save ----------------------------------------------------- */

int file_save_begin(file *f, piece_snapshot *snap, const char *dest_path,
                    const file_expectation *expect, unsigned flags,
                    uint32_t generation)
{
    ASSERT(f != NULL && snap != NULL && dest_path != NULL && expect != NULL);
    ASSERT(f->pool != NULL);
    save_reap(f);
    switch (f->save_phase) {
    case FILE_SAVE_IDLE:
        break;
    case FILE_SAVE_PREPARING:
    case FILE_SAVE_PREPARED:
    case FILE_SAVE_PUBLISHING:
    case FILE_SAVE_UNSYNCED:
    case FILE_SAVE_HELD:
    case FILE_SAVE_CANCELLING:
        return FILE_ERR_BUSY;
    }
    if (bounded_len(dest_path, OS_PATH_MAX) >= (size_t)OS_PATH_MAX) {
        f->oserr = OS_E_NAMETOOLONG;
        return FILE_ERR_IO;
    }
    copy_str(f->save_dest, dest_path);
    f->save_expect = *expect;
    f->save_flags = flags;
    f->save_snap = piece_snapshot_retain(snap);
    f->save_source = NULL;
    if (f->backing != NULL) (void)file_source_acquire(f, &f->save_source);
    temp_reset(&f->save_temp);
    memset(&f->save_outcome, 0, sizeof f->save_outcome);
    f->save_status = FILE_OK;
    f->save_op = FILE_SAVE_OP_PREPARE;
    int rc = submit_job(f, &f->save_job, save_job_fn, generation);
    if (rc != FILE_OK) {
        save_release_inputs(f);
        return rc;
    }
    f->save_phase = FILE_SAVE_PREPARING;
    return FILE_OK;
}

int file_save_publish(file *f)
{
    ASSERT(f != NULL);
    switch (f->save_phase) {
    case FILE_SAVE_PREPARED:
        break;
    case FILE_SAVE_IDLE:
    case FILE_SAVE_PREPARING:
    case FILE_SAVE_PUBLISHING:
    case FILE_SAVE_UNSYNCED:
    case FILE_SAVE_HELD:
    case FILE_SAVE_CANCELLING:
        return FILE_ERR_STATE;
    }
    f->save_op = FILE_SAVE_OP_PUBLISH;
    int rc = submit_job(f, &f->save_job, save_job_fn, f->save_job.generation);
    if (rc == FILE_OK) f->save_phase = FILE_SAVE_PUBLISHING;
    return rc;
}

int file_save_retry_barrier(file *f)
{
    ASSERT(f != NULL);
    save_reap(f);
    switch (f->save_phase) {
    case FILE_SAVE_UNSYNCED:
    case FILE_SAVE_HELD:
        break;
    case FILE_SAVE_IDLE:
    case FILE_SAVE_PREPARING:
    case FILE_SAVE_PREPARED:
    case FILE_SAVE_PUBLISHING:
    case FILE_SAVE_CANCELLING:
        return FILE_ERR_STATE;
    }
    f->save_op = FILE_SAVE_OP_RETRY;
    int rc = submit_job(f, &f->save_job, save_job_fn, f->save_job.generation);
    if (rc == FILE_OK) f->save_phase = FILE_SAVE_PUBLISHING;
    return rc;
}

void file_save_abandon(file *f)
{
    ASSERT(f != NULL);
    if (f->pool == NULL) return;
    save_reap(f);
    switch (f->save_phase) {
    case FILE_SAVE_IDLE:
    case FILE_SAVE_CANCELLING: /* still running: settled once finished */
        return;
    case FILE_SAVE_PREPARING:
    case FILE_SAVE_PUBLISHING:
        work_cancel(f->pool, f->save_job.h);
        f->save_phase = FILE_SAVE_CANCELLING;
        save_reap(f);
        return;
    case FILE_SAVE_PREPARED:
    case FILE_SAVE_UNSYNCED:
    case FILE_SAVE_HELD:
        file_temp_discard(&f->save_temp);
        save_release_inputs(f);
        f->save_phase = FILE_SAVE_IDLE;
        return;
    }
}

bool file_save_busy(const file *f)
{
    ASSERT(f != NULL);
    switch (f->save_phase) {
    case FILE_SAVE_PREPARING:
    case FILE_SAVE_PUBLISHING:
        return true;
    case FILE_SAVE_CANCELLING:
        return !work_handle_finished(f->pool, f->save_job.h);
    case FILE_SAVE_IDLE:
    case FILE_SAVE_PREPARED:
    case FILE_SAVE_UNSYNCED:
    case FILE_SAVE_HELD:
        return false;
    }
    return false;
}

file_save_phase file_save_phase_of(const file *f)
{
    ASSERT(f != NULL);
    switch (f->save_phase) {
    case FILE_SAVE_CANCELLING:
        return work_handle_finished(f->pool, f->save_job.h) ? settled_phase(f, true)
                                                            : FILE_SAVE_CANCELLING;
    case FILE_SAVE_IDLE:
    case FILE_SAVE_PREPARING:
    case FILE_SAVE_PREPARED:
    case FILE_SAVE_PUBLISHING:
    case FILE_SAVE_UNSYNCED:
    case FILE_SAVE_HELD:
        return f->save_phase;
    }
    return f->save_phase;
}

const file_witness *file_save_witness(const file *f)
{
    ASSERT(f != NULL);
    switch (file_save_phase_of(f)) {
    case FILE_SAVE_PREPARED:
    case FILE_SAVE_UNSYNCED:
    case FILE_SAVE_HELD:
        return &f->save_temp.witness;
    case FILE_SAVE_IDLE:
    case FILE_SAVE_PREPARING:
    case FILE_SAVE_PUBLISHING:
    case FILE_SAVE_CANCELLING:
        return NULL;
    }
    return NULL;
}

const file_save_outcome *file_save_outcome_of(const file *f)
{
    ASSERT(f != NULL);
    return &f->save_outcome;
}
