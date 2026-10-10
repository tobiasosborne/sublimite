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
#include <sys/xattr.h>
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

int file_test_recover(const void *address)
{
    return guard_recover((uintptr_t)address, NULL);
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
    int private_copy;             /* malloc backing, using the same lifetime hooks */
} file_map;

static void map_acquire(void *ctx) { atomic_fetch_add_explicit(&((file_map *)ctx)->refs, 1, memory_order_relaxed); }
static void map_release(void *ctx)
{
    file_map *m = ctx;
    if (atomic_fetch_sub_explicit(&m->refs, 1, memory_order_acq_rel) == 1) {
        guard_unregister(m->slot);
        if (m->private_copy) free(m->addr);
        else munmap(m->addr, m->len);
        if (m->fd >= 0) close(m->fd);
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
    struct stat st;
    if (fstat(mapping->fd,&st)) return FILE_ERR_IO;
    file_id observed; file_id_from_stat(&observed,&st);
    return file_id_diff(&source->identity,&observed)==FILE_CHG_NONE ? FILE_OK : FILE_ERR_CHANGED;
}

/* Worker-owned physical transaction. UI passes it from preparation to commit
 * only after receiving SAVE_PREPARED; no descriptor operation is done by decode. */
typedef struct save_transaction {
    file_save_args args;
    int fd, dfd, metadata_fd;
    file_id metadata_id;
    char base[NAME_MAX + 1], tmp[NAME_MAX + 1];
    uint64_t done;
} save_transaction;
static void transaction_discard(save_transaction *tx);

/* ---------------- file object ---------------- */
struct file {
    work_pool *pool;
    char path[PATH_MAX];
    char requested_path[PATH_MAX]; /* immutable worker input */
    uint32_t generation;
    uint64_t threshold;
    int fd;                       /* source fd until attach/close */
    int err_no;

    uint8_t *prefix;
    size_t prefix_len;
    int prefix_owned;
    file_prefix_info info;
    file_mode mode;
    uint64_t size;

    /* UI-owned content. Workers publish separate immutable result records. */
    file_map *map;                /* owned reference of the file object */
    _Atomic int ready;            /* 1 = content ready for attach */
    int attached;
    work_handle open_h;
    work_handle prefix_h;
    _Atomic int prefix_fin;
    struct {
        char path[PATH_MAX];
        file_id id;
        uint8_t *bytes;
        size_t len;
        file_prefix_info info;
        file_mode mode;
        int fd, status, err_no;
    } prefix_result;
    _Atomic int prefix_result_ready;
    int prefix_installed;
    int open_status;
    _Atomic int open_fin;         /* 1 when no open job is pending */
    struct {
        file_map *map;
        int status, err_no;
    } open_result;
    _Atomic int open_result_ready;
    int open_installed;
    file_id open_identity;        /* immutable open-job input */

    file_id id;
    int changed;
    int ifd, iwd;
    int watch_refresh;
    char watch_name[NAME_MAX + 1];
    uint64_t source_generation;
    void (*step)(void *, int);    /* UI-owned hooks, captured at submit */
    void *step_ctx;
    work_handle jobs[WORK_MAX_JOBS]; /* includes completed, undrained jobs */
    work_handle check_h;
    _Atomic int check_fin, check_result_ready;
    int check_installed, check_requested;
    struct { file_id observed; uint32_t map_reasons; uint64_t generation; int status, err_no; } check_result;
    struct { file_map *map; uint64_t generation; } check;

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
        int authorized;          /* immutable commit input, decided by UI */
    } save;
    save_transaction transaction;
    work_handle prepare_h;
    _Atomic int prepare_fin;
    int prepare_status, commit_pending;
    int save_aborted, abort_pending;
    work_handle abort_h;
    _Atomic int abort_fin;
    work_handle save_h;
    _Atomic int save_fin;
    struct {
        file_id id;
        int status, err_no;
    } save_result;
    _Atomic int save_result_ready;
    int save_installed;
    _Atomic int save_replaced_ready;
    int save_replaced_installed;
};

static void file_sync(file *f, uint32_t kind);
static void install_replaced(file *f);
static void open_job(work_ctx *c);
static void save_commit_job(work_ctx *c);
static void save_commit_pump(file *f);
static void invalidate_save(file *f);
static void save_abort_pump(file *f);
static void split_path(const char *path, char *dir, size_t dn, char *base, size_t bn);

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
    /* Terminal/phase messages transfer ownership. A full mailbox is
     * backpressure, never permission to lose that transfer. Cancellation
     * invalidates the delivery and lets close's physical wait make progress. */
    while (!work_should_stop(c)) {
        if (work_publish(c, &m)) return true;
        struct timespec delay = { 0, 1000000 };
        nanosleep(&delay, NULL);
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
static uint64_t mtime_ns(const struct stat *st)
{
    return (uint64_t)st->st_mtim.tv_sec * 1000000000ull + (uint64_t)st->st_mtim.tv_nsec;
}

void file_id_from_stat(file_id *id, const struct stat *st)
{
    id->dev = (uint64_t)st->st_dev;
    id->ino = (uint64_t)st->st_ino;
    id->size = (uint64_t)st->st_size;
    id->mtime_ns = mtime_ns(st);
    id->mode = (uint32_t)(st->st_mode & 07777);
    id->exists = 1;
    id->ctime_ns = (uint64_t)st->st_ctim.tv_sec * 1000000000ull + (uint64_t)st->st_ctim.tv_nsec;
    id->uid = (uint32_t)st->st_uid; id->gid = (uint32_t)st->st_gid;
    id->metadata_valid = 1; id->type = (uint32_t)(st->st_mode & S_IFMT);
}

uint32_t file_id_diff(const file_id *a, const file_id *b)
{
    uint32_t r = 0;
    if (!b->exists) return a->exists ? FILE_CHG_GONE : FILE_CHG_NONE;
    if (!a->exists) return FILE_CHG_INODE;
    if (a->ino != b->ino || a->dev != b->dev) r |= FILE_CHG_INODE;
    if (a->size != b->size) r |= FILE_CHG_SIZE;
    if (a->mtime_ns != b->mtime_ns) r |= FILE_CHG_MTIME;
    if (a->metadata_valid && b->metadata_valid && a->type != b->type) r |= FILE_CHG_TYPE;
    if (a->mode != b->mode || (a->metadata_valid && b->metadata_valid &&
        (a->uid != b->uid || a->gid != b->gid || a->ctime_ns != b->ctime_ns))) r |= FILE_CHG_METADATA;
    return r;
}

int file_id_stat_path(const char *path, file_id *id)
{
    struct stat st;
    if (lstat(path, &st) == 0) { file_id_from_stat(id, &st); return FILE_OK; }
    if (errno == ENOENT || errno == ENOTDIR) {
        memset(id, 0, sizeof *id); return FILE_OK;
    }
    return FILE_ERR_IO; /* Never turn EACCES/EIO/ELOOP into an accepted absence. */
}

/* Acquisition is optimistic: validate descriptor AND name around every phase.
 * ctime catches rewrites even if a writer restores mtime. No filesystem offers
 * an atomic stat/read transaction; the final-validation-to-publication race
 * remains, and mapped originals remain externally mutable (P1.7c s5). */
static int acquisition_validate(int fd, const char *path, const file_id *expected, int *en)
{
    struct stat st;
    file_id now;
    if (fstat(fd, &st) != 0) { *en = errno; return FILE_ERR_IO; }
    file_id_from_stat(&now, &st);
    if (file_id_diff(expected, &now) || expected->ctime_ns != now.ctime_ns) return FILE_ERR_CHANGED;
    if (lstat(path, &st) != 0) {
        if (errno == ENOENT || errno == ENOTDIR) return FILE_ERR_CHANGED;
        *en = errno; return FILE_ERR_IO;
    }
    file_id_from_stat(&now, &st);
    if (!S_ISREG(st.st_mode) || file_id_diff(expected, &now) || expected->ctime_ns != now.ctime_ns)
        return FILE_ERR_CHANGED;
    return FILE_OK;
}

/* ---------------- open ---------------- */
static void prefix_job(work_ctx *c)
{
    file *f = c->arg;
    struct stat st;
    int fd = -1, rc = FILE_OK, en = 0;
    uint8_t *bytes = NULL;
    if (work_should_stop(c)) { rc = FILE_ERR_CANCELLED; goto done; }
    if (realpath(f->requested_path, f->prefix_result.path) == NULL) {
        en = errno; rc = FILE_ERR_IO; goto done;
    }
    /* NONBLOCK is required before fstat: FIFOs must not await a writer. */
    fd = open(f->prefix_result.path, O_RDONLY | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) {
        en = errno;
        /* realpath just established this canonical entry. Disappearance or a
         * new symlink invalidates that acquisition, including before reads. */
        rc = en == ENOENT || en == ENOTDIR || en == ELOOP ? FILE_ERR_CHANGED : FILE_ERR_IO;
        goto done;
    }
    if (fstat(fd, &st) != 0) { en = errno; rc = FILE_ERR_IO; goto done; }
    if (!S_ISREG(st.st_mode)) { rc = FILE_ERR_NOTREG; goto done; }
    if (st.st_size < 0 || (uint64_t)st.st_size > SIZE_MAX) { en = EFBIG; rc = FILE_ERR_IO; goto done; }
    file_id_from_stat(&f->prefix_result.id, &st);
    uint64_t threshold = f->threshold ? f->threshold : file_default_copy_threshold();
    f->prefix_result.mode = ((uint64_t)st.st_size < threshold || st.st_size == 0)
                           ? FILE_MODE_COPY : FILE_MODE_MMAP;
    rc = acquisition_validate(fd, f->prefix_result.path, &f->prefix_result.id, &en);
    if (rc != FILE_OK) goto done;
    size_t want = (uint64_t)st.st_size < FILE_PREFIX_MAX ? (size_t)st.st_size : FILE_PREFIX_MAX;
    /* COPY owns one allocation from acquisition through tree/snapshot release.
     * The published prefix aliases its head; BULK fills only the suffix. */
    size_t capacity = f->prefix_result.mode == FILE_MODE_COPY ? (size_t)st.st_size : want;
    bytes = malloc(capacity ? capacity : 1);
    if (!bytes) { rc = FILE_ERR_NOMEM; goto done; }
    size_t off = 0;
    while (off < want) {
        if (work_should_stop(c)) { rc = FILE_ERR_CANCELLED; goto done; }
        /* Prefix acquisition/scanning is off the UI; cancellation is checked
         * even on short reads and repeated EINTR. */
        size_t n = want - off;
        if (n > 65536u) n = 65536u;
        ssize_t got = pread(fd, bytes + off, n, (off_t)off);
        if (got < 0) { if (errno == EINTR) continue; en = errno; rc = FILE_ERR_IO; goto done; }
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
    rc = acquisition_validate(fd, f->prefix_result.path, &f->prefix_result.id, &en);
done:
    if (rc != FILE_OK) {
        if (fd >= 0) close(fd);
        fd = -1; free(bytes); bytes = NULL;
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
    status = acquisition_validate(f->fd, f->path, &f->open_identity, &en);
    if (status != FILE_OK) goto done;

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
        uint8_t *buf = f->prefix;
        size_t off = f->prefix_len;
        while (off < f->size) {
            size_t want = (size_t)f->size - off;
            if (want > FILE_READ_CHUNK) want = FILE_READ_CHUNK;
            if (work_should_stop(c)) { status = FILE_ERR_CANCELLED; goto done; }
            ssize_t r = pread(f->fd, buf + off, want, (off_t)off);
            if (r < 0) { if (errno == EINTR) continue; en = errno; status = FILE_ERR_IO; break; }
            if (r == 0) { status = FILE_ERR_CHANGED; break; }
            off += (size_t)r;
        }
        if (status != 0) goto done;
        avail = off;
    }
    status = acquisition_validate(f->fd, f->path, &f->open_identity, &en);
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

int file_open_begin(work_pool *pool, const char *path, const file_open_opts *opts, file **out)
{
    *out = NULL;
    if (!pool || !path) return FILE_ERR_STATE;
    size_t len = strnlen(path, PATH_MAX);
    if (len >= PATH_MAX) { errno = ENAMETOOLONG; return FILE_ERR_IO; }
    file *f = calloc(1, sizeof *f);
    if (f == NULL) return FILE_ERR_NOMEM;
    f->pool = pool;
    memcpy(f->requested_path, path, len + 1);
    f->fd = -1; f->prefix_result.fd = -1;
    f->prefix_owned = 1;
    f->ifd = -1; f->iwd = -1;
    f->generation = opts ? opts->generation : 0;
    f->threshold = opts ? opts->copy_threshold : 0;
    f->transaction.fd = -1; f->transaction.dfd = -1; f->transaction.metadata_fd = -1;
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
    wait_job(f, f->prefix_h, &f->prefix_fin);
    wait_job(f, f->open_h, &f->open_fin);
    wait_job(f, f->prepare_h, &f->prepare_fin);
    wait_job(f, f->save_h, &f->save_fin);
    wait_job(f, f->abort_h, &f->abort_fin);
    wait_job(f, f->check_h, &f->check_fin);
    if (f->save.snap) piece_snapshot_release(f->save.snap);
    transaction_discard(&f->transaction);
    if (f->fd >= 0) close(f->fd);
    if (f->ifd >= 0) close(f->ifd);
    if (f->map) map_release(f->map);
    if (!f->open_installed && atomic_load_explicit(&f->open_result_ready, memory_order_acquire)) {
        if (f->open_result.map) map_release(f->open_result.map);
    }
    if (f->prefix_owned) free(f->prefix);
    if (!f->prefix_installed && atomic_load_explicit(&f->prefix_result_ready, memory_order_acquire)) {
        if (f->prefix_result.fd >= 0) close(f->prefix_result.fd);
        free(f->prefix_result.bytes);
    }
    free(f);
}

const uint8_t *file_prefix(const file *f, size_t *len) { *len = f->prefix_len; return f->prefix; }
const file_prefix_info *file_prefix_info_of(const file *f) { return &f->info; }
uint64_t file_size(const file *f) { return f->size; }
file_mode file_open_mode(const file *f) { return f->mode; }
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
        atomic_init(&m->refs, 1); m->fd = -1; m->slot = -1; m->private_copy = 1;
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
    if (m && m->fd >= 0) {
        struct stat st;
        if (fstat(m->fd, &st) == 0) {
            file_id now; file_id_from_stat(&now, &st);
            if (version) *version = now;
            r = file_id_diff(&m->identity, &now) & ~FILE_CHG_METADATA;
            if ((uint64_t)st.st_size < (uint64_t)m->len) r |= FILE_CHG_TRUNCATED;
        } else *err_no = errno;
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
        f->err_no = errno; return FILE_CHG_NONE;
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
    if (file_id_stat_path(f->path, &now) != FILE_OK) { f->err_no = errno; return FILE_ERR_IO; }
    if (now.exists && now.type != S_IFREG) return FILE_ERR_CHANGED;
    file_id backing;
    uint32_t backing_reasons = map_change(f->map, &en, &backing);
    if (en) { f->err_no = en; return FILE_ERR_IO; }
    /* A writer can restore mtime after changing bytes/newline counts. When
     * the accepted pathname still names the mapped inode, ctime/metadata is
     * an unvalidated backing version, even if size and mtime compare equal.
     * Retired inodes acquire a new ctime from unlink/rename-over; those may
     * still be kept while their actual bytes and fault epoch remain intact. */
    if (f->map && backing.exists && now.exists && now.dev == backing.dev && now.ino == backing.ino)
        backing_reasons |= file_id_diff(&f->map->identity, &backing) & FILE_CHG_METADATA;
    if (backing_reasons) {
        if (!f->changed) { f->source_generation++; invalidate_save(f); }
        f->changed = 1;
        return FILE_ERR_CHANGED;
    }
    if (f->ifd >= 0) {
        f->watch_refresh = 1;
        if (file_watch_start(f) < 0) { f->err_no = errno; return FILE_ERR_IO; }
    }
    if (!now.exists) now.mode = f->id.mode; /* recreation retains last accepted mode */
    f->id = now;
    f->source_generation++;
    f->changed = 0;
    invalidate_save(f);
    return FILE_OK;
}

/* Watch the parent/name, which survives target deletion and rename-over.
 * IN_IGNORED and moved/deleted parents make setup explicitly refreshable. */
static const uint32_t WATCH_MASK = IN_MODIFY | IN_ATTRIB | IN_CLOSE_WRITE |
    IN_CREATE | IN_DELETE | IN_MOVED_FROM | IN_MOVED_TO | IN_DELETE_SELF | IN_MOVE_SELF;

int file_watch_start(file *f)
{
    if (!file_prefix_ready(f)) { errno = EINVAL; return -1; }
    if (f->ifd >= 0 && f->iwd >= 0 && !f->watch_refresh) return f->ifd;
    char dir[PATH_MAX];
    split_path(f->path, dir, sizeof dir, f->watch_name, sizeof f->watch_name);
    if (f->ifd < 0) {
        f->ifd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
        if (f->ifd < 0) return -1;
    }
    if (f->iwd >= 0) { (void)inotify_rm_watch(f->ifd, f->iwd); f->iwd = -1; }
    f->iwd = inotify_add_watch(f->ifd, dir, WATCH_MASK | IN_ONLYDIR);
    f->watch_refresh = f->iwd < 0;
    /* Keep the fd for a later retry, but never claim an absent watch is active. */
    return f->iwd < 0 ? -1 : f->ifd;
}

static void check_job(work_ctx *c)
{
    file *f = c->arg;
    f->check_result.status = file_id_stat_path(f->path, &f->check_result.observed);
    f->check_result.err_no = f->check_result.status == FILE_OK ? 0 : errno;
    int en = 0;
    f->check_result.map_reasons = map_change(f->check.map, &en, NULL);
    if (en && !f->check_result.err_no) f->check_result.err_no = en;
    f->check_result.generation = f->check.generation;
    atomic_store_explicit(&f->check_result_ready, 1, memory_order_release);
    (void)post(c, FILE_MSG_CHECK_DONE, f, FILE_OK, 0, 0, 0, f->generation);
    atomic_store_explicit(&f->check_fin, 1, memory_order_release);
}

int file_watch_poll(file *f)
{
    int got = 0;
    if (f->ifd >= 0) {
        char buf[4096] __attribute__((aligned(8)));
        /* One nonblocking buffer per call. Repeated EINTR and a continuous
         * alternating producer both return to the event loop. */
        ssize_t r = read(f->ifd, buf, sizeof buf);
        size_t off = 0, count = r > 0 ? (size_t)r : 0;
        while (count - off >= sizeof(struct inotify_event)) {
            const struct inotify_event *event = (const struct inotify_event *)(const void *)(buf + off);
            size_t length = sizeof *event + (size_t)event->len;
            if (length > count - off) break;
            if (event->mask & IN_Q_OVERFLOW) got = 1;
            if (event->wd == f->iwd) {
                if (event->mask & (IN_IGNORED | IN_DELETE_SELF | IN_MOVE_SELF)) {
                    if (event->mask & IN_IGNORED) f->iwd = -1;
                    f->watch_refresh = 1; got = 1;
                }
                if (event->len == 0 || (strnlen(event->name, event->len) < event->len &&
                    strcmp(event->name, f->watch_name) == 0)) got = 1;
            }
            off += length;
        }
    }
    if (got) f->check_requested = 1;
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
    return file_changed(f);
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

static int write_all(file_save_args *a, int fd, const uint8_t *p, size_t n)
{
    while (n) {
        if (a->stop && a->stop(a->stop_ctx)) return FILE_ERR_CANCELLED;
        ssize_t w = write(fd, p, n);
        if (w < 0) {
            if (errno == EINTR) continue;
            a->err_no = errno; return FILE_ERR_IO;
        }
        if (w == 0) { a->err_no = EIO; return FILE_ERR_IO; }
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

static void transaction_discard(save_transaction *tx)
{
    if (tx->metadata_fd >= 0) { close(tx->metadata_fd); tx->metadata_fd = -1; }
    if (tx->fd >= 0) { close(tx->fd); tx->fd = -1; }
    if (tx->dfd >= 0) {
        if (tx->tmp[0]) (void)unlinkat(tx->dfd, tx->tmp, 0);
        close(tx->dfd); tx->dfd = -1;
    }
}

/* Copy all supported xattrs, including ACLs and security labels, rather than
 * guessing a namespace allowlist. Inherited attributes absent on the source
 * are removed. Unsupported xattrs on both descriptors imply no xattr state;
 * every other read/set/remove failure refuses the replacement. */
static int xattr_list(int fd, char **names, size_t *length)
{
    *names = NULL; *length = 0;
    ssize_t n = flistxattr(fd, NULL, 0);
    if (n < 0) return -1;
    if (n == 0) return 0;
    char *p = malloc((size_t)n);
    if (!p) { errno = ENOMEM; return -1; }
    ssize_t got = flistxattr(fd, p, (size_t)n);
    if (got < 0) { int en = errno; free(p); errno = en; return -1; }
    *names = p; *length = (size_t)got;
    return 0;
}
static int xattr_has(const char *list, size_t length, const char *name)
{
    size_t off = 0;
    while (off < length) {
        size_t n = strnlen(list + off, length - off);
        if (n == length - off) return 0;
        if (strcmp(list + off, name) == 0) return 1;
        off += n + 1;
    }
    return 0;
}
static int metadata_xattrs(int source, int target)
{
    char *names = NULL, *inherited = NULL;
    size_t length = 0, inherited_length = 0;
    int rc = -1, en = 0;
    if (xattr_list(source, &names, &length) != 0) {
        en = errno;
        if (en == ENOTSUP) {
            ssize_t n = flistxattr(target, NULL, 0);
            if (n < 0 && errno == ENOTSUP) return 0;
        }
        errno = en; return -1;
    }
    if (xattr_list(target, &inherited, &inherited_length) != 0) { en = errno; goto done; }
    for (size_t off = 0; off < inherited_length;) {
        const char *name = inherited + off;
        size_t n = strnlen(name, inherited_length - off);
        if (n == inherited_length - off) { en = EIO; goto done; }
        if (!xattr_has(names, length, name) && fremovexattr(target, name) != 0) { en = errno; goto done; }
        off += n + 1;
    }
    for (size_t off = 0; off < length;) {
        const char *name = names + off;
        size_t key_length = strnlen(name, length - off);
        if (key_length == length - off) { en = EIO; goto done; }
        ssize_t n = fgetxattr(source, name, NULL, 0);
        if (n < 0) { en = errno; goto done; }
        void *value = malloc(n ? (size_t)n : 1);
        if (!value) { en = ENOMEM; goto done; }
        ssize_t got = fgetxattr(source, name, value, (size_t)n);
        if (got != n) { en = got < 0 ? errno : EIO; free(value); goto done; }
        if (fsetxattr(target, name, value, (size_t)n, 0) != 0) {
            /* Some labels are immutable to this process but already match the
             * temp's inherited label. That preserves them without a write. */
            en = errno;
            void *current = malloc(n ? (size_t)n : 1);
            if (!current) { en = ENOMEM; free(value); goto done; }
            ssize_t existing = fgetxattr(target, name, current, (size_t)n);
            int equal = existing == n && memcmp(value, current, (size_t)n) == 0;
            free(current); free(value);
            if (!equal) goto done;
        } else free(value);
        off += key_length + 1;
    }
    rc = 0;
done:
    free(names); free(inherited);
    if (rc != 0) errno = en;
    return rc;
}
static int metadata_capture(save_transaction *tx)
{
    file_save_args *a = &tx->args;
    struct stat st;
    memset(&tx->metadata_id, 0, sizeof tx->metadata_id);
    if (fstatat(tx->dfd, tx->base, &st, AT_SYMLINK_NOFOLLOW) != 0) {
        if (errno != ENOENT) { a->err_no = errno; return FILE_ERR_IO; }
    } else file_id_from_stat(&tx->metadata_id, &st);
    if (tx->metadata_id.exists && S_ISLNK(st.st_mode)) return FILE_ERR_CHANGED;
    if (a->expect && file_id_diff(a->expect, &tx->metadata_id)) return FILE_ERR_CHANGED;
    if (!tx->metadata_id.exists || !S_ISREG(st.st_mode)) return FILE_OK;
    tx->metadata_fd = openat(tx->dfd, tx->base, O_RDONLY | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW);
    if (tx->metadata_fd < 0) { a->err_no = errno; return FILE_ERR_IO; }
    if (fstat(tx->metadata_fd, &st) != 0) { a->err_no = errno; return FILE_ERR_IO; }
    file_id observed; file_id_from_stat(&observed, &st);
    if (file_id_diff(&tx->metadata_id, &observed)) return FILE_ERR_CHANGED;
    return FILE_OK;
}
static int metadata_apply(save_transaction *tx)
{
    file_save_args *a = &tx->args;
    uint32_t mode = tx->metadata_id.exists ? tx->metadata_id.mode
                  : (a->mode_valid || a->mode ? a->mode : 0644u);
    if (tx->metadata_fd >= 0) {
        struct stat st;
        if (fstat(tx->fd, &st) != 0) { a->err_no = errno; return FILE_ERR_IO; }
        if (((uint32_t)st.st_uid != tx->metadata_id.uid || (uint32_t)st.st_gid != tx->metadata_id.gid) &&
            fchown(tx->fd, (uid_t)tx->metadata_id.uid, (gid_t)tx->metadata_id.gid) != 0) {
            a->err_no = errno; return FILE_ERR_IO;
        }
        if (metadata_xattrs(tx->metadata_fd, tx->fd) != 0) { a->err_no = errno; return FILE_ERR_IO; }
        if (fstat(tx->metadata_fd, &st) != 0) { a->err_no = errno; return FILE_ERR_IO; }
        file_id observed; file_id_from_stat(&observed, &st);
        if (file_id_diff(&tx->metadata_id, &observed)) return FILE_ERR_CHANGED;
    }
    /* chown and ACL installation can change permission bits; mode comes last. */
    if (fchmod(tx->fd, (mode_t)mode) != 0) { a->err_no = errno; return FILE_ERR_IO; }
    return FILE_OK;
}

static int transaction_prepare(save_transaction *tx)
{
    file_save_args *a = &tx->args;
    char dir[PATH_MAX]; struct timespec ts;
    uint64_t total = piece_snapshot_len(a->snap);
    int mid_fired = 0;
    a->written = 0; a->err_no = 0; tx->done = 0;
    tx->fd = -1; tx->dfd = -1; tx->metadata_fd = -1; tx->tmp[0] = '\0';
    if (a->stop && a->stop(a->stop_ctx)) return FILE_ERR_CANCELLED;
    split_path(a->path, dir, sizeof dir, tx->base, sizeof tx->base);
    tx->dfd = open(dir, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (tx->dfd < 0) { a->err_no = errno; return FILE_ERR_IO; }
    int rc = metadata_capture(tx);
    if (rc != FILE_OK) { transaction_discard(tx); return rc; }
    clock_gettime(CLOCK_REALTIME, &ts);
    snprintf(tx->tmp, sizeof tx->tmp, ".%.*s.edit-%ld-%llu.tmp", FILE_NAME_MAX_KEEP,
             tx->base, (long)getpid(), (unsigned long long)ts.tv_sec * 1000000000ull +
             (unsigned long long)ts.tv_nsec);
    tx->fd = openat(tx->dfd, tx->tmp, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
    if (tx->fd < 0) {
        /* A failed O_EXCL may name somebody else's temp: never unlink it. */
        a->err_no = errno; tx->tmp[0] = '\0'; transaction_discard(tx); return FILE_ERR_IO;
    }
    step_call(a, FILE_STEP_TEMP_CREATED);
    piece_iter it; const uint8_t *bytes; size_t n;
    piece_iter_begin_snapshot(&it, a->snap, 0);
    rc = FILE_ERR_IO;
    while (piece_iter_next(&it, &bytes, &n)) {
        while (n) {
            size_t k = n < FILE_WRITE_CHUNK ? n : FILE_WRITE_CHUNK;
            rc = write_all(a, tx->fd, bytes, k);
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
    if (fsync(tx->fd) != 0) { a->err_no = errno; goto fail; }
    step_call(a, FILE_STEP_FSYNCED);
    return FILE_OK;
fail:
    transaction_discard(tx); return rc;
}

static int transaction_commit(save_transaction *tx)
{
    file_save_args *a = &tx->args;
    int rc = FILE_ERR_IO, locked = 0;
    if (a->lock) { pthread_mutex_lock(a->lock); locked = 1; }
    if (a->stop && a->stop(a->stop_ctx)) { rc = FILE_ERR_CANCELLED; goto fail; }
    {
        file_id now; struct stat st;
        memset(&now, 0, sizeof now);
        if (fstatat(tx->dfd, tx->base, &st, AT_SYMLINK_NOFOLLOW) == 0) file_id_from_stat(&now, &st);
        else if (errno != ENOENT && errno != ENOTDIR) { a->err_no = errno; goto fail; }
        if ((now.exists && S_ISLNK(st.st_mode)) || file_id_diff(&tx->metadata_id, &now) || (a->expect && file_id_diff(a->expect, &now))) {
            rc = FILE_ERR_CHANGED; goto fail;
        }
    }
    struct stat st;
    if (fstat(tx->fd, &st) != 0) { a->err_no = errno; goto fail; }
    if (a->validate) { rc = a->validate(a->validate_ctx); if (rc != FILE_OK) goto fail; }
    rc = FILE_ERR_IO;
    if (renameat(tx->dfd, tx->tmp, tx->dfd, tx->base) != 0) { a->err_no = errno; goto fail; }
    tx->tmp[0] = '\0';
    int identity_error = 0;
    struct stat renamed;
    if (fstat(tx->fd, &renamed) != 0) identity_error = errno;
    else st = renamed;
    if (a->out_id) {
        file_id_from_stat(a->out_id, &st);
        if (identity_error) a->out_id->metadata_valid = 0;
    }
    if (a->replaced) a->replaced(a->replaced_ctx);
    if (locked) { pthread_mutex_unlock(a->lock); locked = 0; }
    close(tx->fd); tx->fd = -1;
    a->written = tx->done;
    step_call(a, FILE_STEP_RENAMED);
    if (fsync(tx->dfd) != 0) {
        a->err_no = errno; transaction_discard(tx); return FILE_ERR_DIRSYNC;
    }
    transaction_discard(tx);
    step_call(a, FILE_STEP_DIR_SYNCED);
    if (identity_error) { a->err_no = identity_error; return FILE_ERR_IO; }
    return FILE_OK;
fail:
    if (locked) pthread_mutex_unlock(a->lock);
    transaction_discard(tx); return rc;
}

int file_save_write(file_save_args *a)
{
    save_transaction tx = { .args = *a, .fd = -1, .dfd = -1, .metadata_fd = -1 };
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
        struct stat st;
        file_id now;
        if (fstat(m->fd, &st) != 0) return FILE_ERR_CHANGED;
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
        struct stat st;
        if (fstat(f->save.source_map->fd, &st) != 0) { a.err_no = errno; rc = FILE_ERR_IO; }
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
