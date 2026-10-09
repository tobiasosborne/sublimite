/* file.c -- P1.7 open / change detection / durable atomic save. See file.h. */
#include "file/file.h"
#include "file/file_test.h"
#include "base/base.h"

#include <errno.h>
#include <signal.h>
#include <fcntl.h>
#include <limits.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <sys/inotify.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define FILE_COPY_CAP     (256ull << 20)
#define FILE_PREFETCH_MAX (64ull << 20)     /* madvise(WILLNEED) window of a mapping */
#define FILE_READ_CHUNK   (4u << 20)
#define FILE_WRITE_CHUNK  (256u << 10)
#define FILE_NAME_MAX_KEEP 100

/* ---------------- SIGBUS guard (P1.7b; policy in docs/decisions/P1.7.md) -------
 * A mapped original truncated by another process faults with SIGBUS on any read
 * past the new EOF, on any thread, inside any reader of the mapping (piece
 * iterators, scan, lineidx, layout). One process-wide handler serves them all:
 * if the fault address lies in a registered mapping it maps anonymous zero
 * pages over [faulting page, end of mapping) (Linux/glibc mmap), sets
 * the slot's `faulted` flag and returns, so the faulting read completes with
 * zeros. file_check/file_changed turn the flag into FILE_CHG_TRUNCATED and the
 * sticky "source changed" state. Anything else chains to the previous handler
 * (or the default action). The single process service below needs the narrow
 * global exception proposed in docs/decisions/P1.7c.md. */
#define FILE_GUARD_SLOTS 256
#define GUARD_FREE 0u
#define GUARD_RESERVED 1u
#define GUARD_LIVE 2u
#define GUARD_RETIRING 3u
#define GUARD_PHASE 3u
#define GUARD_READER 4u
typedef struct guard_slot {
    _Atomic uint32_t state;       /* phase in low bits; pinned readers above */
    uintptr_t addr;              /* immutable from LIVE through last reader */
    size_t len;
    uintptr_t page_size;
    _Atomic uint64_t faults;      /* changes for EVERY attempted recovery */
} guard_slot;
typedef struct file_signal_service {
    guard_slot slots[FILE_GUARD_SLOTS];
    struct sigaction previous;
    pthread_once_t once;
    _Atomic int previous_reset;
    uintptr_t page_size;
    int installed;
} file_signal_service;
static file_signal_service file_bus = { .once = PTHREAD_ONCE_INIT };

static void guard_chain(int sig, siginfo_t *si, void *uc)
{
    const struct sigaction *old = &file_bus.previous;
    /* sa_handler and sa_sigaction share storage. Classify special values
     * before interpreting that storage as either kind of function pointer. */
    if (old->sa_handler == SIG_IGN) return;
    int reset = atomic_load_explicit(&file_bus.previous_reset, memory_order_acquire);
    if (!reset && old->sa_handler != SIG_DFL && ((unsigned)old->sa_flags & SA_RESETHAND))
        reset = atomic_exchange_explicit(&file_bus.previous_reset, 1, memory_order_acq_rel);
    if (old->sa_handler == SIG_DFL || reset) {
        /* raise targets this thread even for SI_USER/SI_TKILL delivery, which
         * has no faulting instruction to retry. Default SIGBUS terminates. */
        struct sigaction dfl;
        memset(&dfl, 0, sizeof dfl);
        dfl.sa_handler = SIG_DFL;
        sigemptyset(&dfl.sa_mask);
        (void)sigaction(sig, &dfl, NULL);
        (void)raise(sig);
        return;
    }
    if (old->sa_flags & SA_SIGINFO) old->sa_sigaction(sig, si, uc);
    else old->sa_handler(sig);
}

/* Probe is used only by a synthetic, off-signal regression self-check. */
typedef struct guard_probe {
    void (*pinned)(void *, int);
    void *ctx;
} guard_probe;

static int guard_recover(uintptr_t a, const guard_probe *probe)
{
    for (int i = 0; i < FILE_GUARD_SLOTS; i++) {
        guard_slot *slot = &file_bus.slots[i];
        uint32_t state = atomic_load_explicit(&slot->state, memory_order_acquire);
        for (;;) {
            if ((state & GUARD_PHASE) != GUARD_LIVE || state > UINT32_MAX - GUARD_READER) break;
            if (atomic_compare_exchange_weak_explicit(&slot->state, &state, state + GUARD_READER,
                                                     memory_order_acquire, memory_order_relaxed)) {
                uintptr_t base = slot->addr;
                size_t len = slot->len;
                if (a < base || a - base >= len) {
                    atomic_fetch_sub_explicit(&slot->state, GUARD_READER, memory_order_release);
                    break;
                }
                if (probe && probe->pinned) probe->pinned(probe->ctx, i);
                uintptr_t from = a & ~(slot->page_size - 1u);
                /* Invalidate BEFORE anonymous pages become visible to saves. */
                atomic_fetch_add_explicit(&slot->faults, 1, memory_order_release);
                int recovered = mmap((void *)from, base + len - from, PROT_READ,
                                     MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED | MAP_NORESERVE,
                                     -1, 0) != MAP_FAILED;
                atomic_fetch_sub_explicit(&slot->state, GUARD_READER, memory_order_release);
                return recovered;
            }
        }
    }
    return 0;
}

static void guard_handler(int sig, siginfo_t *si, void *uc)
{
    int saved = errno;
    if ((si->si_code == BUS_ADRERR || si->si_code == BUS_OBJERR) &&
        guard_recover((uintptr_t)si->si_addr, NULL)) {
        errno = saved;
        return;
    }
    errno = saved;
    guard_chain(sig, si, uc);
}

static void guard_install(void)
{
    struct sigaction sa;
    long raw = sysconf(_SC_PAGESIZE);
    if (raw <= 0 || ((uintptr_t)raw & ((uintptr_t)raw - 1u)) != 0) return;
    for (int i = 0; i < FILE_GUARD_SLOTS; i++) {
        if (!atomic_is_lock_free(&file_bus.slots[i].state) ||
            !atomic_is_lock_free(&file_bus.slots[i].faults)) return;
    }
    if (!atomic_is_lock_free(&file_bus.previous_reset)) return;
    file_bus.page_size = (uintptr_t)raw;
    /* Query first: another thread may enter the new handler immediately after
     * installation. Its immutable previous action must already be populated. */
    if (sigaction(SIGBUS, NULL, &file_bus.previous) != 0) return;
    memset(&sa, 0, sizeof sa);
    sa.sa_sigaction = guard_handler;
    sa.sa_flags = SA_SIGINFO | (file_bus.previous.sa_flags & (SA_NODEFER | SA_ONSTACK | SA_RESTART));
    sa.sa_mask = file_bus.previous.sa_mask;
    file_bus.installed = sigaction(SIGBUS, &sa, NULL) == 0;
}

/* Returns a slot index or -1 (table full / handler not installed). */
static int guard_register(void *addr, size_t len)
{
    pthread_once(&file_bus.once, guard_install);
    if (!file_bus.installed || !addr || len == 0 ||
        ((uintptr_t)addr & (file_bus.page_size - 1u)) != 0 ||
        len > UINTPTR_MAX - (uintptr_t)addr) return -1;
    for (int i = 0; i < FILE_GUARD_SLOTS; i++) {
        guard_slot *slot = &file_bus.slots[i];
        uint32_t zero = GUARD_FREE;
        if (atomic_compare_exchange_strong_explicit(&slot->state, &zero, GUARD_RESERVED,
                                                    memory_order_acquire, memory_order_relaxed)) {
            slot->addr = (uintptr_t)addr;
            slot->len = len;
            slot->page_size = file_bus.page_size;
            atomic_store_explicit(&slot->faults, 0, memory_order_relaxed);
            atomic_store_explicit(&slot->state, GUARD_LIVE, memory_order_release);
            return i;
        }
    }
    return -1;
}

static void guard_unregister(int slot)
{
    if (slot < 0) return;
    guard_slot *s = &file_bus.slots[slot];
    uint32_t state = atomic_load_explicit(&s->state, memory_order_acquire);
    for (;;) {
        if ((state & GUARD_PHASE) == GUARD_RESERVED) break;
        uint32_t retiring = (state & ~GUARD_PHASE) | GUARD_RETIRING;
        if (atomic_compare_exchange_weak_explicit(&s->state, &state, retiring,
                                                  memory_order_acq_rel, memory_order_acquire)) {
            while (atomic_load_explicit(&s->state, memory_order_acquire) != GUARD_RETIRING) sched_yield();
            break;
        }
    }
    atomic_store_explicit(&s->state, GUARD_FREE, memory_order_release);
}

static uint64_t guard_faults(int slot)
{
    return slot >= 0 ? atomic_load_explicit(&file_bus.slots[slot].faults, memory_order_acquire) : 0;
}
static int guard_faulted(int slot) { return guard_faults(slot) != 0; }

typedef struct guard_retire_probe {
    int slot, blocked, replacement;
    void *next;
    size_t next_len;
    pthread_t thread;
    _Atomic int started, done;
} guard_retire_probe;
static void *guard_test_retire(void *ctx)
{
    guard_retire_probe *p = ctx;
    atomic_store(&p->started, 1);
    guard_unregister(p->slot);
    atomic_store(&p->done, 1);
    return NULL;
}
static void guard_test_pinned(void *ctx, int slot)
{
    guard_retire_probe *p = ctx;
    if (slot != p->slot) return;
    if (pthread_create(&p->thread, NULL, guard_test_retire, p) != 0) return;
    while (!atomic_load(&p->started)) sched_yield();
    /* Old retirement frees the slot immediately. The fixed implementation
     * exposes RETIRING while this recovery holds a reader reference. */
    while ((atomic_load_explicit(&file_bus.slots[slot].state, memory_order_acquire) & GUARD_PHASE)
           == GUARD_LIVE) sched_yield();
    p->blocked = (atomic_load_explicit(&file_bus.slots[slot].state, memory_order_acquire) & GUARD_PHASE)
                 == GUARD_RETIRING && !atomic_load(&p->done);
    if (!p->blocked) p->replacement = guard_register(p->next, p->next_len);
}

int file_test_guard_registry(void)
{
    long raw = sysconf(_SC_PAGESIZE);
    if (raw <= 0) return 0;
    size_t pg = (size_t)raw;
    uint8_t *foreign = mmap(NULL, 6 * pg, PROT_READ | PROT_WRITE,
                           MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (foreign == MAP_FAILED) return 0;
    memset(foreign, 'Q', 6 * pg);
    pthread_once(&file_bus.once, guard_install);
    if (!file_bus.installed) { munmap(foreign, 6 * pg); return 0; }
    int result = 0;
    /* No live file mappings may exist during this internal self-check. */
    atomic_store(&file_bus.slots[0].state, GUARD_RESERVED);
    file_bus.slots[0].addr = 1;
    file_bus.slots[0].len = (uintptr_t)foreign + 2 * pg;
    file_bus.slots[0].page_size = (uintptr_t)pg;
    (void)guard_recover((uintptr_t)foreign + pg, NULL);
    if (foreign[0] == 'Q' && foreign[pg] == 'Q' && foreign[2 * pg] == 'Q') result |= 1;
    guard_unregister(0);
    munmap(foreign, 6 * pg);
    foreign = mmap(NULL, 6 * pg, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (foreign == MAP_FAILED) return result;
    memset(foreign, 'Q', 6 * pg);
    int slot = guard_register(foreign, pg);
    if (slot < 0) { munmap(foreign, 6 * pg); return result; }
    guard_retire_probe p = { .slot = slot, .replacement = -1,
                            .next = foreign + 3 * pg, .next_len = 2 * pg };
    guard_probe probe = { guard_test_pinned, &p };
    (void)guard_recover((uintptr_t)foreign + pg / 2, &probe);
    pthread_join(p.thread, NULL);
    if (p.blocked) p.replacement = guard_register(p.next, p.next_len);
    if (p.blocked && foreign[pg] == 'Q' && foreign[2 * pg] == 'Q') result |= 2;
    if (p.replacement >= 0) guard_unregister(p.replacement);
    munmap(foreign, 6 * pg);
    return result;
}

/* ---------------- mapping (refcounted; the piece tree/snapshots hold refs) --- */
typedef struct file_map {
    _Atomic int refs;
    void *addr;
    size_t len;
    int fd;
    int slot;                     /* SIGBUS guard slot */
    file_id identity;             /* identity of the actual snapshot backing */
} file_map;

static void map_acquire(void *ctx) { atomic_fetch_add_explicit(&((file_map *)ctx)->refs, 1, memory_order_relaxed); }
static void map_release(void *ctx)
{
    file_map *m = ctx;
    if (atomic_fetch_sub_explicit(&m->refs, 1, memory_order_acq_rel) == 1) {
        guard_unregister(m->slot);
        munmap(m->addr, m->len);
        if (m->fd >= 0) close(m->fd);
        free(m);
    }
}

/* ---------------- file object ---------------- */
struct file {
    work_pool *pool;
    char path[PATH_MAX];
    char dir[PATH_MAX];
    char base[NAME_MAX + 1];
    uint32_t generation;
    uint64_t threshold;
    int fd;                       /* source fd until attach/close */
    int err_no;

    uint8_t *prefix;
    size_t prefix_len;
    file_prefix_info info;
    file_mode mode;
    uint64_t size;

    /* UI-owned content. Workers publish separate immutable result records. */
    uint8_t *data;                /* copy mode buffer */
    size_t data_len;
    file_map *map;                /* owned reference of the file object */
    _Atomic int ready;            /* 1 = content ready for attach */
    int attached;
    work_handle open_h;
    _Atomic int open_fin;         /* 1 when no open job is pending */
    struct {
        uint8_t *data;
        size_t data_len;
        file_map *map;
        int status, err_no;
    } open_result;
    _Atomic int open_result_ready;
    int open_installed;
    file_id open_identity;        /* immutable open-job input */

    pthread_mutex_t mu;           /* shared precommit validation vs UI changes */
    file_id id;
    int changed;
    int ifd, iwd;
    uint64_t source_generation;
    void (*step)(void *, int);    /* UI-owned hooks, captured at submit */
    void *step_ctx;
    work_handle jobs[WORK_MAX_JOBS]; /* includes completed, undrained jobs */

    struct {
        piece_snapshot *snap;
        unsigned flags;
        uint32_t generation;
        void (*step)(void *, int);
        void *step_ctx;
        file_id expect, source_id;
        uint32_t mode;
        uint64_t source_generation;
        uint64_t source_faults;
        file_map *source_map;
    } save;
    work_handle save_h;
    _Atomic int save_fin;
    struct {
        file_id id;
        int status;
    } save_result;
    _Atomic int save_result_ready;
    int save_installed;
    _Atomic int save_replaced_ready;
    int save_replaced_installed;
};

static void file_sync(file *f);
static void install_replaced_locked(file *f);

uint64_t file_default_copy_threshold(void)
{
    long pages = sysconf(_SC_PHYS_PAGES), ps = sysconf(_SC_PAGESIZE);
    uint64_t lim = FILE_COPY_CAP;
    if (pages > 0 && ps > 0) {
        uint64_t r = ((uint64_t)pages * (uint64_t)ps) / 32u;
        if (r < lim) lim = r;
    }
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
    return work_publish(c, &m);
}

int file_msg_decode(const work_msg *m, file_msg *out)
{
    msg_payload pl;
    if (m->kind != FILE_MSG_OPEN_READY && m->kind != FILE_MSG_OPEN_FAILED &&
        m->kind != FILE_MSG_SAVE_DONE)
        return 1;
    memcpy(&pl, m->data, sizeof pl);
    out->kind = m->kind;
    out->generation = m->generation;
    out->f = pl.f;
    out->status = pl.status;
    out->mode = pl.mode;
    out->size = pl.size;
    out->err_no = pl.err_no;
    if (pl.f) file_sync(pl.f);
    return 0;
}

/* ---------------- identity ---------------- */
static uint64_t mtime_ns(const struct stat *st)
{
    return (uint64_t)st->st_mtim.tv_sec * 1000000000ull + (uint64_t)st->st_mtim.tv_nsec;
}

static void id_from_stat(file_id *id, const struct stat *st)
{
    id->dev = (uint64_t)st->st_dev;
    id->ino = (uint64_t)st->st_ino;
    id->size = (uint64_t)st->st_size;
    id->mtime_ns = mtime_ns(st);
    id->mode = (uint32_t)(st->st_mode & 07777);
    id->exists = 1;
}

static uint32_t id_diff(const file_id *a, const file_id *b)
{
    uint32_t r = 0;
    if (!b->exists) return FILE_CHG_GONE;
    if (a->ino != b->ino || a->dev != b->dev) r |= FILE_CHG_INODE;
    if (a->size != b->size) r |= FILE_CHG_SIZE;
    if (a->mtime_ns != b->mtime_ns) r |= FILE_CHG_MTIME;
    return r;
}

static void id_stat_path(const char *path, file_id *id)
{
    struct stat st;
    memset(id, 0, sizeof *id);
    if (stat(path, &st) == 0) id_from_stat(id, &st);
}

/* ---------------- open ---------------- */
static void open_job(work_ctx *c)
{
    file *f = c->arg;
    int status = 0, en = 0;
    uint64_t avail = f->size;

    if (f->mode == FILE_MODE_MMAP) {
        file_map *m = calloc(1, sizeof *m);
        if (m == NULL) { status = FILE_ERR_NOMEM; goto done; }
        m->fd = dup(f->fd);
        m->len = (size_t)f->size;
        m->addr = m->fd >= 0 ? mmap(NULL, m->len, PROT_READ, MAP_PRIVATE, m->fd, 0) : MAP_FAILED;
        if (m->addr == MAP_FAILED) {
            en = errno; status = FILE_ERR_IO;
            if (m->fd >= 0) close(m->fd);
            free(m);
            goto done;
        }
        m->slot = guard_register(m->addr, m->len);
        if (m->slot < 0) {
            munmap(m->addr, m->len); close(m->fd); free(m);
            status = FILE_ERR_NOMEM; goto done;
        }
        atomic_init(&m->refs, 1);
        m->identity = f->open_identity;
        f->open_result.map = m;
        size_t pf = m->len < FILE_PREFETCH_MAX ? m->len : (size_t)FILE_PREFETCH_MAX;
        (void)madvise(m->addr, pf, MADV_WILLNEED);
    } else {
        uint8_t *buf = malloc(f->size ? (size_t)f->size : 1);
        size_t off = 0;
        if (buf == NULL) { status = FILE_ERR_NOMEM; goto done; }
        while (off < f->size) {
            size_t want = (size_t)f->size - off;
            if (want > FILE_READ_CHUNK) want = FILE_READ_CHUNK;
            if (work_should_stop(c)) { free(buf); status = FILE_ERR_CANCELLED; goto done; }
            ssize_t r = pread(f->fd, buf + off, want, (off_t)off);
            if (r < 0) { if (errno == EINTR) continue; en = errno; status = FILE_ERR_IO; break; }
            if (r == 0) break;                    /* file shrank while reading */
            off += (size_t)r;
        }
        if (status != 0) { free(buf); goto done; }
        f->open_result.data = buf;
        f->open_result.data_len = off;
        avail = off;
    }
done:
    f->open_result.err_no = en;
    f->open_result.status = status;
    atomic_store_explicit(&f->open_result_ready, 1, memory_order_release);
    if (status == 0)
        (void)post(c, FILE_MSG_OPEN_READY, f, 0, (uint32_t)f->mode, avail, 0, f->generation);
    else
        (void)post(c, FILE_MSG_OPEN_FAILED, f, status, (uint32_t)f->mode, 0, en, f->generation);
    atomic_store_explicit(&f->open_fin, 1, memory_order_release);
}

int file_open_begin(work_pool *pool, const char *path, const file_open_opts *opts, file **out)
{
    file *f = NULL;
    struct stat st;
    char resolved[PATH_MAX];
    int fd = -1, rc = FILE_ERR_IO, en = 0;

    *out = NULL;
    if (realpath(path, resolved) == NULL) { en = errno; goto fail; }
    fd = open(resolved, O_RDONLY | O_CLOEXEC);
    if (fd < 0 || fstat(fd, &st) != 0) { en = errno; goto fail; }
    if (!S_ISREG(st.st_mode)) { rc = FILE_ERR_NOTREG; goto fail; }
    f = calloc(1, sizeof *f);
    if (f == NULL) { rc = FILE_ERR_NOMEM; goto fail; }
    f->pool = pool;
    f->fd = fd;
    f->ifd = -1; f->iwd = -1;
    f->generation = opts ? opts->generation : 0;
    f->threshold = (opts && opts->copy_threshold) ? opts->copy_threshold : file_default_copy_threshold();
    f->size = (uint64_t)st.st_size;
    pthread_mutex_init(&f->mu, NULL);
    atomic_init(&f->ready, 0);
    atomic_init(&f->open_fin, 1);
    atomic_init(&f->save_fin, 1);
    atomic_init(&f->open_result_ready, 0);
    atomic_init(&f->save_result_ready, 0);
    atomic_init(&f->save_replaced_ready, 0);
    f->open_installed = 1;
    f->save_installed = 1;
    f->save_replaced_installed = 1;
    id_from_stat(&f->id, &st);
    f->open_identity = f->id;

    if (strlen(resolved) >= sizeof f->path) { rc = FILE_ERR_IO; en = ENAMETOOLONG; goto fail; }
    memcpy(f->path, resolved, strlen(resolved) + 1);
    {
        const char *sl = strrchr(f->path, '/');
        size_t dl = sl == f->path ? 1 : (size_t)(sl - f->path);
        memcpy(f->dir, f->path, dl);
        f->dir[dl] = '\0';
        snprintf(f->base, sizeof f->base, "%s", sl + 1);
    }

    /* bounded prefix: the only read on the UI thread */
    {
        size_t want = f->size < FILE_PREFIX_MAX ? (size_t)f->size : FILE_PREFIX_MAX, off = 0;
        f->prefix = malloc(want ? want : 1);
        if (f->prefix == NULL) { rc = FILE_ERR_NOMEM; goto fail; }
        while (off < want) {
            ssize_t r = pread(fd, f->prefix + off, want - off, (off_t)off);
            if (r < 0) { if (errno == EINTR) continue; en = errno; goto fail; }
            if (r == 0) break;
            off += (size_t)r;
        }
        f->prefix_len = off;
        file_prefix_scan(f->prefix, off, &f->info);
    }

    f->mode = (f->size < f->threshold || f->size == 0) ? FILE_MODE_COPY : FILE_MODE_MMAP;
    if (f->mode == FILE_MODE_COPY && f->size <= f->prefix_len) {
        atomic_store(&f->ready, 1);       /* prefix is the whole file: no job */
    } else {
        f->open_installed = 0;
        atomic_store(&f->open_fin, 0);
        f->open_h = work_submit(pool, (work_job){ open_job, f, f->generation, WORK_BULK });
        if (f->open_h.epoch == 0) { atomic_store(&f->open_fin, 1); rc = FILE_ERR_POOL; goto fail; }
        f->jobs[f->open_h.slot] = f->open_h;
    }
    *out = f;
    return FILE_OK;
fail:
    if (f) {
        free(f->prefix);
        pthread_mutex_destroy(&f->mu);
        free(f);
    }
    if (fd >= 0) close(fd);
    if (en) {
        /* no file object to carry errno; keep it in errno for the caller */
        errno = en;
    }
    return rc;
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
        struct timespec ts = { 0, 50000 };
        nanosleep(&ts, NULL);
    }
}

void file_close(file *f)
{
    if (f == NULL) return;
    /* Completed slots remain reserved while messages are pending. Remember
     * each slot's handle, so every completion carrying f is invalidated. */
    for (uint32_t i = 0; i < WORK_MAX_JOBS; i++)
        if (f->jobs[i].epoch) work_cancel(f->pool, f->jobs[i]);
    wait_job(f, f->open_h, &f->open_fin);
    wait_job(f, f->save_h, &f->save_fin);
    if (f->save.snap) piece_snapshot_release(f->save.snap);
    if (f->fd >= 0) close(f->fd);
    if (f->ifd >= 0) close(f->ifd);
    if (f->map) map_release(f->map);
    if (!f->open_installed && atomic_load_explicit(&f->open_result_ready, memory_order_acquire)) {
        if (f->open_result.map) map_release(f->open_result.map);
        free(f->open_result.data);
    }
    free(f->data);
    free(f->prefix);
    pthread_mutex_destroy(&f->mu);
    free(f);
}

const uint8_t *file_prefix(const file *f, size_t *len) { *len = f->prefix_len; return f->prefix; }
const file_prefix_info *file_prefix_info_of(const file *f) { return &f->info; }
uint64_t file_size(const file *f) { return f->size; }
file_mode file_open_mode(const file *f) { return f->mode; }
const char *file_path(const file *f) { return f->path; }
int file_errno(const file *f) { file_sync((file *)f); return f->err_no; }
int file_open_ready(const file *f)
{
    file_sync((file *)f);
    return atomic_load_explicit(&((file *)f)->ready, memory_order_acquire);
}

int file_attach(file *f, piece_tree *t)
{
    int rc;
    if (f->attached || !file_open_ready(f)) return FILE_ERR_STATE;
    if (f->mode == FILE_MODE_MMAP) {
        piece_map_hooks h = { f->map, map_acquire, map_release };
        rc = piece_init_mapped(t, (const uint8_t *)f->map->addr, f->map->len, &h);
    } else if (f->data) {
        rc = piece_init_copy(t, f->data, f->data_len);
    } else {
        rc = piece_init_copy(t, f->prefix, f->prefix_len);
    }
    if (rc != 0) return FILE_ERR_NOMEM;
    f->attached = 1;
    free(f->data);
    f->data = NULL;
    if (f->fd >= 0) { close(f->fd); f->fd = -1; }
    return FILE_OK;
}

/* ---------------- change detection ---------------- */
static uint32_t map_change(file_map *m)
{
    uint32_t r = 0;
    if (m && m->fd >= 0) {
        struct stat st;
        if (fstat(m->fd, &st) == 0) {
            file_id now; id_from_stat(&now, &st);
            r = id_diff(&m->identity, &now);
            if ((uint64_t)st.st_size < (uint64_t)m->len) r |= FILE_CHG_TRUNCATED;
        }
    }
    if (m && guard_faulted(m->slot)) r |= FILE_CHG_TRUNCATED;
    return r;
}

static uint32_t check_locked(file *f)
{
    file_id now;
    uint32_t r;
    install_replaced_locked(f);
    id_stat_path(f->path, &now);
    r = id_diff(&f->id, &now);
    r |= map_change(f->map);
    if (r && !f->changed) { f->changed = 1; f->source_generation++; }
    return r;
}

int file_check(file *f, uint32_t *reasons)
{
    uint32_t r;
    file_sync(f);
    pthread_mutex_lock(&f->mu);
    r = check_locked(f);
    pthread_mutex_unlock(&f->mu);
    if (reasons) *reasons = r;
    return r != 0;
}

int file_changed(const file *f)
{
    file *m = (file *)f;
    int c;
    file_sync(m);
    pthread_mutex_lock(&m->mu);
    c = m->changed || (m->map && guard_faulted(m->map->slot));
    pthread_mutex_unlock(&m->mu);
    return c;
}

int file_resolve_keep(file *f)
{
    file_sync(f);
    pthread_mutex_lock(&f->mu);
    if (map_change(f->map)) {
        if (!f->changed) f->source_generation++;
        f->changed = 1;
        pthread_mutex_unlock(&f->mu);
        return FILE_ERR_CHANGED;
    }
    id_stat_path(f->path, &f->id);
    f->source_generation++;
    f->changed = 0;
    pthread_mutex_unlock(&f->mu);
    return FILE_OK;
}

static const uint32_t WATCH_MASK = IN_MODIFY | IN_ATTRIB | IN_CLOSE_WRITE | IN_DELETE_SELF | IN_MOVE_SELF;

int file_watch_start(file *f)
{
    file_sync(f);
    if (f->ifd >= 0) return f->ifd;
    f->ifd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    if (f->ifd < 0) return -1;
    f->iwd = inotify_add_watch(f->ifd, f->path, WATCH_MASK);
    if (f->iwd < 0) { close(f->ifd); f->ifd = -1; return -1; }
    return f->ifd;
}

int file_watch_poll(file *f)
{
    int got = 0;
    file_sync(f);
    if (f->ifd >= 0) {
        char buf[4096] __attribute__((aligned(8)));
        for (;;) {
            ssize_t r = read(f->ifd, buf, sizeof buf);
            if (r <= 0) break;
            got = 1;
        }
    }
    if (got) (void)file_check(f, NULL);
    return file_changed(f);
}

/* UI-only installation. The release publication precedes the mailbox message;
 * readiness/status polling also installs it, so no mailbox drain is required
 * merely to observe completion. Workers never mutate UI content or watches. */
static void file_sync(file *f)
{
    if (!f->open_installed && atomic_load_explicit(&f->open_result_ready, memory_order_acquire)) {
        f->map = f->open_result.map;
        f->data = f->open_result.data;
        f->data_len = f->open_result.data_len;
        f->err_no = f->open_result.err_no;
        f->open_installed = 1;
        if (f->open_result.status == FILE_OK) atomic_store(&f->ready, 1);
    }
    if ((!f->save_installed && atomic_load_explicit(&f->save_result_ready, memory_order_acquire)) ||
        (!f->save_replaced_installed && atomic_load_explicit(&f->save_replaced_ready, memory_order_acquire))) {
        pthread_mutex_lock(&f->mu);
        install_replaced_locked(f);
        if (!f->save_installed && atomic_load_explicit(&f->save_result_ready, memory_order_acquire)) {
            int rc = f->save_result.status;
            if (rc == FILE_OK || rc == FILE_ERR_DIRSYNC) {
                if (f->source_generation == f->save.source_generation) f->changed = 0;
                if (f->ifd >= 0) {
                    if (f->iwd >= 0) (void)inotify_rm_watch(f->ifd, f->iwd);
                    f->iwd = inotify_add_watch(f->ifd, f->path, WATCH_MASK);
                }
            } else if (rc == FILE_ERR_CHANGED) {
                if (!f->changed) f->source_generation++;
                f->changed = 1;
            }
            f->save_installed = 1;
        }
        pthread_mutex_unlock(&f->mu);
    }
}

static void install_replaced_locked(file *f)
{
    if (!f->save_replaced_installed &&
        atomic_load_explicit(&f->save_replaced_ready, memory_order_acquire)) {
        f->id = f->save_result.id;
        f->save_replaced_installed = 1;
    }
}

/* ---------------- durable save ---------------- */
static void step_call(file_save_args *a, int s) { if (a->step) a->step(a->step_ctx, s); }

static int write_all(int fd, const uint8_t *p, size_t n)
{
    while (n) {
        ssize_t w = write(fd, p, n);
        if (w < 0) { if (errno == EINTR) continue; return -1; }
        p += (size_t)w;
        n -= (size_t)w;
    }
    return 0;
}

static void split_path(const char *path, char *dir, size_t dn, char *base, size_t bn)
{
    const char *sl = strrchr(path, '/');
    if (sl == NULL) { snprintf(dir, dn, "."); snprintf(base, bn, "%s", path); return; }
    size_t dl = sl == path ? 1 : (size_t)(sl - path);
    if (dl >= dn) dl = dn - 1;
    memcpy(dir, path, dl);
    dir[dl] = '\0';
    snprintf(base, bn, "%s", sl + 1);
}

int file_save_write(file_save_args *a)
{
    char dir[PATH_MAX], base[NAME_MAX + 1], tmp[NAME_MAX + 1];
    struct timespec ts;
    int fd = -1, dfd = -1, rc = FILE_ERR_IO, locked = 0;
    uint64_t total = piece_snapshot_len(a->snap), done = 0;
    int mid_fired = 0;
    piece_iter it;
    const uint8_t *p;
    size_t n;

    a->written = 0;
    a->err_no = 0;
    split_path(a->path, dir, sizeof dir, base, sizeof base);
    dfd = open(dir, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (dfd < 0) { a->err_no = errno; return FILE_ERR_IO; }
    clock_gettime(CLOCK_REALTIME, &ts);
    snprintf(tmp, sizeof tmp, ".%.*s.edit-%ld-%llu.tmp", FILE_NAME_MAX_KEEP, base, (long)getpid(),
             (unsigned long long)ts.tv_sec * 1000000000ull + (unsigned long long)ts.tv_nsec);

    fd = openat(dfd, tmp, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
    if (fd < 0) { a->err_no = errno; close(dfd); return FILE_ERR_IO; }
    step_call(a, FILE_STEP_TEMP_CREATED);

    piece_iter_begin_snapshot(&it, a->snap, 0);
    while (piece_iter_next(&it, &p, &n)) {
        while (n) {
            size_t k = n < FILE_WRITE_CHUNK ? n : FILE_WRITE_CHUNK;
            if (a->stop && a->stop(a->stop_ctx)) { rc = FILE_ERR_CANCELLED; goto fail; }
            if (write_all(fd, p, k) != 0) { a->err_no = errno; goto fail; }
            p += k; n -= k; done += k;
            if (!mid_fired && done * 2 >= total) { mid_fired = 1; step_call(a, FILE_STEP_MID_WRITE); }
        }
    }
    if (!mid_fired) step_call(a, FILE_STEP_MID_WRITE);
    if (fchmod(fd, (mode_t)(a->mode ? a->mode : 0644)) != 0) { a->err_no = errno; goto fail; }
    step_call(a, FILE_STEP_TEMP_WRITTEN);
    if (fsync(fd) != 0) { a->err_no = errno; goto fail; }
    step_call(a, FILE_STEP_FSYNCED);

    if (a->lock) { pthread_mutex_lock(a->lock); locked = 1; }
    if (a->stop && a->stop(a->stop_ctx)) { rc = FILE_ERR_CANCELLED; goto fail; }
    if (a->expect) {
        file_id now;
        struct stat st;
        memset(&now, 0, sizeof now);
        if (fstatat(dfd, base, &st, 0) == 0) id_from_stat(&now, &st);
        if (id_diff(a->expect, &now)) { rc = FILE_ERR_CHANGED; goto fail; }
    }
    {
        struct stat st;
        if (fstat(fd, &st) != 0) { a->err_no = errno; goto fail; }
        if (a->validate) {
            rc = a->validate(a->validate_ctx);
            if (rc != FILE_OK) goto fail;
        }
        rc = FILE_ERR_IO;
        if (renameat(dfd, tmp, dfd, base) != 0) { a->err_no = errno; goto fail; }
        if (a->out_id) id_from_stat(a->out_id, &st);
        if (a->replaced) a->replaced(a->replaced_ctx);
    }
    if (locked) { pthread_mutex_unlock(a->lock); locked = 0; }
    close(fd); fd = -1;
    a->written = done;
    step_call(a, FILE_STEP_RENAMED);

    {
        if (fsync(dfd) != 0) {
            a->err_no = errno;
            close(dfd);
            return FILE_ERR_DIRSYNC;
        }
        close(dfd);
    }
    step_call(a, FILE_STEP_DIR_SYNCED);
    return FILE_OK;
fail:
    if (locked) pthread_mutex_unlock(a->lock);
    if (fd >= 0) close(fd);
    (void)unlinkat(dfd, tmp, 0);
    close(dfd);
    return rc;
}

static int stop_cb(void *ctx) { return work_should_stop((work_ctx *)ctx); }

/* Called under f->mu immediately before rename. Path identity alone cannot
 * validate a tree whose original inode survived an earlier replacement. */
static int source_validate(void *ctx)
{
    file *f = ctx;
    if (f->source_generation != f->save.source_generation) return FILE_ERR_CHANGED;
    if (!(f->save.flags & FILE_SAVE_FORCE) && f->changed) return FILE_ERR_CHANGED;
    file_map *m = f->save.source_map;
    if (m) {
        if (guard_faults(m->slot) != f->save.source_faults) return FILE_ERR_CHANGED;
        struct stat st;
        file_id now;
        if (fstat(m->fd, &st) != 0) return FILE_ERR_CHANGED;
        id_from_stat(&now, &st);
        if (id_diff(&f->save.source_id, &now)) return FILE_ERR_CHANGED;
    }
    return FILE_OK;
}

static void publish_replaced(void *ctx)
{
    file *f = ctx;
    atomic_store_explicit(&f->save_replaced_ready, 1, memory_order_release);
}

static void save_job(work_ctx *c)
{
    file *f = c->arg;
    file_save_args a;
    memset(&a, 0, sizeof a);
    a.mode = f->save.mode;
    a.path = f->path;
    a.snap = f->save.snap;
    a.expect = (f->save.flags & FILE_SAVE_FORCE) ? NULL : &f->save.expect;
    a.step = f->save.step;
    a.step_ctx = f->save.step_ctx;
    a.stop = stop_cb;
    a.stop_ctx = c;
    a.lock = &f->mu;
    a.out_id = &f->save_result.id;
    a.validate = source_validate;
    a.validate_ctx = f;
    a.replaced = publish_replaced;
    a.replaced_ctx = f;

    int rc = file_save_write(&a);
    piece_snapshot_release(f->save.snap);
    f->save.snap = NULL;
    f->save_result.status = rc;
    atomic_store_explicit(&f->save_result_ready, 1, memory_order_release);
    (void)post(c, FILE_MSG_SAVE_DONE, f, rc, 0, a.written, a.err_no, f->save.generation);
    atomic_store_explicit(&f->save_fin, 1, memory_order_release);
}

int file_save_busy(const file *f)
{
    int busy = !atomic_load_explicit(&((file *)f)->save_fin, memory_order_acquire);
    file_sync((file *)f);
    return busy;
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
    if (!(flags & FILE_SAVE_FORCE)) {
        (void)file_check(f, NULL);
        if (file_changed(f)) return FILE_ERR_CHANGED;
    }
    s = piece_snapshot_take(t);
    if (s == NULL) return FILE_ERR_NOMEM;
    f->save.snap = s;
    f->save.flags = flags;
    f->save.generation = generation;
    f->save.step = f->step;
    f->save.step_ctx = f->step_ctx;
    pthread_mutex_lock(&f->mu);
    f->save.expect = f->id;
    f->save.mode = f->id.mode;
    f->save.source_generation = f->source_generation;
    f->save.source_map = f->map;
    f->save.source_faults = f->map ? guard_faults(f->map->slot) : 0;
    pthread_mutex_unlock(&f->mu);
    if (f->map) {
        if (flags & FILE_SAVE_FORCE) {
            struct stat st;
            if (fstat(f->map->fd, &st) != 0) {
                f->err_no = errno;
                piece_snapshot_release(s); f->save.snap = NULL;
                return FILE_ERR_IO;
            }
            id_from_stat(&f->save.source_id, &st);
        } else f->save.source_id = f->map->identity;
    }
    f->save_installed = 0;
    f->save_replaced_installed = 0;
    atomic_store_explicit(&f->save_result_ready, 0, memory_order_relaxed);
    atomic_store_explicit(&f->save_replaced_ready, 0, memory_order_relaxed);
    atomic_store(&f->save_fin, 0);
    f->save_h = work_submit(f->pool, (work_job){ save_job, f, generation, WORK_BULK });
    if (f->save_h.epoch == 0) {
        atomic_store(&f->save_fin, 1);
        piece_snapshot_release(s);
        f->save.snap = NULL;
        f->save_installed = 1;
        f->save_replaced_installed = 1;
        return FILE_ERR_POOL;
    }
    f->jobs[f->save_h.slot] = f->save_h;
    return FILE_OK;
}
