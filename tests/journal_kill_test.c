/* journal kill oracle (L2-2b; PLAN §4.2 at journal level), ported from
 * v1 journal_kill_test and journal_hot_exit_test at the oracle pin. A
 * forked writer is killed with SIGKILL at each boundary; the parent
 * recovers the image and checks it against its own byte model:
 *   before acknowledgment: pre-edit or whole-edit, never torn;
 *   after acknowledgment (the append returned OK): whole-edit.
 * Kill points are injected beneath the journal at the os seam
 * (-Wl,--wrap=os_file_pwrite_once,--wrap=os_rename_at). The parent is
 * single-threaded when it forks; each child builds its own pool. */
#include "journal_fixture.h"

#include <errno.h>
#include <signal.h>
#include <stdatomic.h>
#include <sys/mman.h>
#include <sys/wait.h>

static uint32_t get32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

/* ---- kill seams ---- */
typedef enum kill_at {
    K_NONE = 0,
    K_AFTER_TYPE,   /* after the record of kill_type is written */
    K_BEFORE_TYPE,  /* instead of writing the record of kill_type */
    K_TORN_TYPE,    /* write all but the last byte of kill_type, then die */
    K_BEFORE_RENAME,
    K_AFTER_RENAME
} kill_at;
static kill_at kill_mode;
static uint32_t kill_type;
static unsigned kill_skip; /* matching records to let through first */

static _Noreturn void die(void)
{
    (void)kill(getpid(), SIGKILL);
    for (;;) pause();
}

os_err __real_os_file_pwrite_once(os_file *f, const void *buf, size_t n, uint64_t off, size_t *done);
os_err __wrap_os_file_pwrite_once(os_file *f, const void *buf, size_t n, uint64_t off, size_t *done)
{
    bool match = (kill_mode == K_AFTER_TYPE || kill_mode == K_BEFORE_TYPE ||
                  kill_mode == K_TORN_TYPE) && n >= 12 && get32((const uint8_t *)buf + 8) == kill_type;
    if (match && kill_skip) {
        kill_skip--;
        match = false;
    }
    if (match && kill_mode == K_BEFORE_TYPE) die();
    if (match && kill_mode == K_TORN_TYPE) {
        (void)__real_os_file_pwrite_once(f, buf, n - 1u, off, done);
        die();
    }
    os_err e = __real_os_file_pwrite_once(f, buf, n, off, done);
    if (match && kill_mode == K_AFTER_TYPE && e == OS_OK && *done == n) die();
    return e;
}

os_err __real_os_rename_at(const os_dir *fd, const char *from, const os_dir *td, const char *to,
                           os_rename_mode mode);
os_err __wrap_os_rename_at(const os_dir *fd, const char *from, const os_dir *td, const char *to,
                           os_rename_mode mode)
{
    if (kill_mode == K_BEFORE_RENAME && strncmp(from, "session.sjnl.rot-", 17) == 0) die();
    os_err e = __real_os_rename_at(fd, from, td, to, mode);
    if (kill_mode == K_AFTER_RENAME && strncmp(from, "session.sjnl.rot-", 17) == 0) die();
    return e;
}

static const char *JN = "session.sjnl";

static work_pool *child_pool(void)
{
    static work_pool p __attribute__((aligned(64)));
    if (work_pool_init(&p, 1, 0) != 0) _exit(90);
    return &p;
}

static void reap_killed(pid_t child, const char *tag)
{
    int status = 0;
    while (waitpid(child, &status, 0) < 0) JF_REQUIRE(errno == EINTR);
    JF_CHECK(WIFSIGNALED(status) && WTERMSIG(status) == SIGKILL, "%s: child died by SIGKILL (%d)",
             tag, status);
}

/* ---- 1. kill at every group boundary, with a barrier inside ---- */
typedef enum boundary { AFTER_BEGIN, AFTER_DELETE, BEFORE_COMMIT, AFTER_COMMIT } boundary;

static void grouped_child(jf_dir *d, boundary at)
{
    work_pool *pool = child_pool();
    journal *j;
    journal_options o = jf_options();
    if (journal_open(&j, &d->dir, JN, pool, NULL, &o) != JOURNAL_OK) _exit(91);
    jf_base_empty(j, 1);
    if (jf_insert(j, 1, 0, "before") != JOURNAL_OK) _exit(92);
    jf_durable(j, pool); /* the baseline is durable before the seam arms */
    switch (at) {
    case AFTER_BEGIN: kill_mode = K_AFTER_TYPE; kill_type = JOURNAL_REC_GROUP_BEGIN; break;
    case AFTER_DELETE: kill_mode = K_AFTER_TYPE; kill_type = JOURNAL_REC_DELETE; break;
    case BEFORE_COMMIT: kill_mode = K_BEFORE_TYPE; kill_type = JOURNAL_REC_GROUP_COMMIT; break;
    case AFTER_COMMIT: kill_mode = K_AFTER_TYPE; kill_type = JOURNAL_REC_GROUP_COMMIT; break;
    }
    if (journal_group_begin(j) != JOURNAL_OK) _exit(93);
    if (journal_append_delete(j, 1, 0, 6) != JOURNAL_OK) _exit(94);
    jf_durable(j, pool); /* a PAD and a barrier inside the group */
    if (jf_insert(j, 1, 0, "after") != JOURNAL_OK) _exit(95);
    (void)journal_group_commit(j);
    _exit(96); /* every boundary must have killed */
}

static void test_group_boundaries(void)
{
    static const char *names[] = {"after-begin", "after-delete", "before-commit", "after-commit"};
    for (boundary at = AFTER_BEGIN; at <= AFTER_COMMIT; at++) {
        jf_dir d;
        jf_dir_make(&d, "journal_kill_test", names[at]);
        pid_t child = fork();
        JF_REQUIRE(child >= 0);
        if (child == 0) grouped_child(&d, at);
        reap_killed(child, names[at]);
        piece_reserve *r;
        JF_REQUIRE(piece_reserve_create(&r) == PIECE_OK);
        journal_recovery rec;
        JF_REQUIRE(journal_recover(&rec, &d.dir, JN, r, NULL, NULL) == JOURNAL_OK);
        const journal_recovered_buffer *b = jf_buffer(&rec, 1);
        const char *want = at == AFTER_COMMIT ? "after" : "before";
        JF_CHECK(b && jf_tree_is(b->tree, want, strlen(want)), "%s: %s", names[at], want);
        JF_CHECK(rec.verdict == (at == AFTER_COMMIT ? JOURNAL_IMAGE_CLEAN : JOURNAL_IMAGE_REPAIRED),
                 "%s: verdict %d", names[at], (int)rec.verdict);
        /* (P) committed records: BASE, INSERT, then BEGIN, DELETE,
         * INSERT, COMMIT only when the group committed. */
        JF_CHECK(rec.last_sequence == (at == AFTER_COMMIT ? 6u : 2u), "%s: last sequence %llu",
                 names[at], (unsigned long long)rec.last_sequence);
        journal_recovery_release(&rec);
        piece_reserve_destroy(r);
        jf_dir_remove(&d);
    }
}

/* ---- 2. hot-exit phases: page cache, durable, torn, rotation ---- */
typedef enum phase {
    P_CACHE, P_DURABLE, P_TORN_INSERT, P_TORN_SLICE, P_TORN_COMMIT, P_ROTATE_BEFORE_RENAME,
    P_ROTATE_AFTER_RENAME, P_END
} phase;
static const char *phase_name[] = {"cache", "durable", "torn-insert", "torn-slice", "torn-commit",
                                   "rotate-before-rename", "rotate-after-rename"};

static void phase_child(jf_dir *d, phase at)
{
    work_pool *pool = child_pool();
    piece_reserve *r;
    if (piece_reserve_create(&r) != PIECE_OK) _exit(80);
    journal *j;
    journal_options o = jf_options();
    if (journal_open(&j, &d->dir, JN, pool, NULL, &o) != JOURNAL_OK) _exit(81);
    jf_base_empty(j, 1);
    if (jf_insert(j, 1, 0, "before") != JOURNAL_OK) _exit(82);
    switch (at) {
    case P_CACHE: die(); /* acknowledged into the page cache, never synced */
    case P_DURABLE: jf_durable(j, pool); die();
    case P_TORN_INSERT:
        /* An ungrouped single-record edit torn one byte short. */
        kill_mode = K_TORN_TYPE;
        kill_type = JOURNAL_REC_INSERT;
        (void)jf_insert(j, 1, 6, "-torn-");
        _exit(83);
    case P_TORN_SLICE: {
        /* A multi-slice insert is a multi-record command, so the caller
         * groups it; (P) two slices plus one byte, the second torn. */
        static uint8_t big[2 * JOURNAL_INSERT_SLICE_BYTES + 1];
        memset(big, 'x', sizeof big);
        if (journal_group_begin(j) != JOURNAL_OK) _exit(73);
        kill_mode = K_TORN_TYPE;
        kill_type = JOURNAL_REC_INSERT;
        kill_skip = 1;
        size_t progress = 0;
        while (journal_insert_step(j, 1, 6, big, sizeof big, &progress) == JOURNAL_MORE) {}
        _exit(74);
    }
    case P_TORN_COMMIT:
        jf_durable(j, pool);
        if (journal_group_begin(j) != JOURNAL_OK) _exit(84);
        if (journal_append_delete(j, 1, 0, 6) != JOURNAL_OK) _exit(85);
        if (jf_insert(j, 1, 0, "after") != JOURNAL_OK) _exit(86);
        kill_mode = K_TORN_TYPE;
        kill_type = JOURNAL_REC_GROUP_COMMIT;
        (void)journal_group_commit(j);
        _exit(87);
    case P_ROTATE_BEFORE_RENAME:
    case P_ROTATE_AFTER_RENAME: {
        jf_durable(j, pool);
        piece_tree *t;
        if (piece_open_copy(r, (const uint8_t *)"checkpoint", 10, &t) != PIECE_OK) _exit(88);
        piece_snapshot *s;
        if (piece_snapshot_take(t, &s) != PIECE_OK) _exit(89);
        journal_rotation_buffer b = {.buffer_id = 1, .text = s, .mode = JOURNAL_ROTATE_BY_VALUE};
        journal_rotation rot = {.buffers = &b, .buffer_count = 1};
        kill_mode = at == P_ROTATE_BEFORE_RENAME ? K_BEFORE_RENAME : K_AFTER_RENAME;
        if (journal_rotate_start(j, &rot) != JOURNAL_OK) _exit(70);
        jf_settle(j, pool);
        _exit(71);
    }
    case P_END: break;
    }
    _exit(72);
}

static void test_phases(void)
{
    for (phase at = P_CACHE; at < P_END; at++) {
        jf_dir d;
        jf_dir_make(&d, "journal_kill_test", phase_name[at]);
        pid_t child = fork();
        JF_REQUIRE(child >= 0);
        if (child == 0) phase_child(&d, at);
        reap_killed(child, phase_name[at]);
        piece_reserve *r;
        JF_REQUIRE(piece_reserve_create(&r) == PIECE_OK);
        journal_recovery rec;
        JF_REQUIRE(journal_recover(&rec, &d.dir, JN, r, NULL, NULL) == JOURNAL_OK);
        const journal_recovered_buffer *b = jf_buffer(&rec, 1);
        const char *want = at == P_ROTATE_AFTER_RENAME ? "checkpoint" : "before";
        JF_CHECK(b && b->verdict == JOURNAL_BUFFER_RECOVERED && jf_tree_is(b->tree, want, strlen(want)),
                 "%s: %s", phase_name[at], want);
        bool torn = at == P_TORN_INSERT || at == P_TORN_SLICE || at == P_TORN_COMMIT;
        JF_CHECK(rec.verdict == (torn ? JOURNAL_IMAGE_REPAIRED : JOURNAL_IMAGE_CLEAN),
                 "%s: verdict %d", phase_name[at], (int)rec.verdict);
        journal_recovery_release(&rec);
        piece_reserve_destroy(r);
        /* A rotation killed before its rename leaves its temporary: an
         * abandoned temp, never the only copy of anything. */
        size_t want_entries = at == P_ROTATE_BEFORE_RENAME ? 2u : 1u;
        JF_CHECK(jf_dir_count(&d) == want_entries, "%s: entries", phase_name[at]);
        jf_dir_remove(&d);
    }
}

/* ---- 3. random kill campaign ---- */
/* (G) Script length: 20,000 one-byte edits are 41 or 48 B records, about
 * 0.9 MB, so kill points spread over roughly 220 pages and 78 barriers
 * (one every 256 edits below). (G) 50 trials, v1's count (journal_kill_test
 * at the pin): trial 0 dies before any pump, the rest at uniform targets.
 * Coverage choices, not budgets; v1's 100,000-edit script is shortened
 * because a kill target is uniform over the script either way. */
#define SCRIPT_EDITS 20000u
#define TRIALS 50u
#define TEXT_CAP 256u

typedef struct campaign {
    _Atomic uint64_t issued, page_cache, durable;
    _Atomic int ready, error;
} campaign;

typedef struct model {
    uint8_t data[TEXT_CAP];
    size_t len;
    uint64_t rng;
} model;

static uint64_t next_rng(uint64_t *x)
{
    *x ^= *x << 13;
    *x ^= *x >> 7;
    *x ^= *x << 17;
    return *x;
}

/* One edit decided by the model's generator; applied to the model, and
 * to the journal when j != NULL. */
static int edit(model *m, journal *j)
{
    uint64_t r = next_rng(&m->rng);
    bool del = m->len && (m->len == TEXT_CAP || (r & 3u) == 0);
    size_t off = (size_t)((r >> 8) % (del ? m->len : m->len + 1));
    uint8_t ch = (uint8_t)(r >> 32);
    if (j) {
        journal_err e;
        if (del) {
            e = journal_append_delete(j, 1, off, 1);
        } else {
            size_t progress = 0;
            e = journal_insert_step(j, 1, off, &ch, 1, &progress);
        }
        if (e != JOURNAL_OK) return 1;
    }
    if (del) {
        memmove(m->data + off, m->data + off + 1, m->len - off - 1);
        m->len--;
    } else {
        memmove(m->data + off + 1, m->data + off, m->len - off);
        m->data[off] = ch;
        m->len++;
    }
    return 0;
}

static void campaign_child(jf_dir *d, campaign *p, uint64_t seed, bool before_pump)
{
    work_pool *pool = child_pool();
    journal *j;
    journal_options o = jf_options();
    if (journal_open(&j, &d->dir, JN, pool, NULL, &o) != JOURNAL_OK) _exit(60);
    jf_base_empty(j, 1);
    model m = {.rng = seed};
    atomic_store(&p->ready, 1);
    for (uint64_t i = 0; i < SCRIPT_EDITS; i++) {
        /* issued bounds from above even if the kill lands inside the
         * append; page_cache is the acknowledgment point (OK returned). */
        atomic_store(&p->issued, i + 1);
        if (edit(&m, j)) {
            atomic_store(&p->error, 1);
            _exit(61);
        }
        atomic_store(&p->page_cache, i + 1);
        if (before_pump) die();
        if ((i & 255u) == 255u) {
            journal_request_sync(j);
            (void)jf_tick(j, pool);
            uint64_t ds = journal_get_status(j).durable_sequence;
            atomic_store(&p->durable, ds >= 1 ? ds - 1 : 0);
        }
    }
    jf_durable(j, pool);
    for (;;) pause(); /* wait to be killed */
}

static void test_campaign(void)
{
    campaign *p = mmap(NULL, sizeof *p, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    JF_REQUIRE(p != MAP_FAILED);
    uint64_t sum_replayed = 0, sum_cache = 0;
    for (unsigned t = 0; t < TRIALS; t++) {
        atomic_store(&p->issued, 0);
        atomic_store(&p->page_cache, 0);
        atomic_store(&p->durable, 0);
        atomic_store(&p->ready, 0);
        atomic_store(&p->error, 0);
        uint64_t seed = UINT64_C(0x9e3779b97f4a7c15) ^ ((uint64_t)(t + 1) * UINT64_C(0x100000001b3));
        uint64_t trng = seed ^ UINT64_C(0x123456789abcdef);
        uint64_t target = t == 0 ? 1 : 1 + next_rng(&trng) % SCRIPT_EDITS;
        char tag[32];
        snprintf(tag, sizeof tag, "trial%u", t);
        jf_dir d;
        jf_dir_make(&d, "journal_kill_test", tag);
        pid_t child = fork();
        JF_REQUIRE(child >= 0);
        if (child == 0) campaign_child(&d, p, seed, t == 0);
        while (!atomic_load(&p->ready) || atomic_load(&p->page_cache) < target) {
            if (atomic_load(&p->error)) break;
            int status;
            if (waitpid(child, &status, WNOHANG) == child) {
                JF_CHECK(WIFSIGNALED(status) && WTERMSIG(status) == SIGKILL, "%s: early exit", tag);
                child = -1;
                break;
            }
            os_sleep_ns(50000);
        }
        if (child > 0) {
            (void)kill(child, SIGKILL);
            reap_killed(child, tag);
        }
        JF_CHECK(!atomic_load(&p->error), "%s: child error", tag);
        uint64_t issued = atomic_load(&p->issued), cached = atomic_load(&p->page_cache);
        uint64_t durable = atomic_load(&p->durable);
        piece_reserve *r;
        JF_REQUIRE(piece_reserve_create(&r) == PIECE_OK);
        journal_recovery rec;
        JF_REQUIRE(journal_recover(&rec, &d.dir, JN, r, NULL, NULL) == JOURNAL_OK);
        uint64_t replayed = rec.last_sequence >= 1 ? rec.last_sequence - 1 : 0; /* less BASE */
        model want = {.rng = seed};
        for (uint64_t i = 0; i < replayed && i < SCRIPT_EDITS; i++) (void)edit(&want, NULL);
        const journal_recovered_buffer *b = jf_buffer(&rec, 1);
        JF_CHECK(cached <= replayed && replayed <= issued && durable <= replayed,
                 "%s: page_cache %llu <= replayed %llu <= issued %llu (durable %llu)", tag,
                 (unsigned long long)cached, (unsigned long long)replayed,
                 (unsigned long long)issued, (unsigned long long)durable);
        JF_CHECK(b && jf_tree_is(b->tree, want.data, want.len), "%s: text = model at %llu", tag,
                 (unsigned long long)replayed);
        sum_replayed += replayed;
        sum_cache += cached;
        journal_recovery_release(&rec);
        piece_reserve_destroy(r);
        jf_dir_remove(&d);
    }
    printf("journal_kill_test: campaign trials=%u script=%u page_cache=%llu replayed=%llu\n",
           TRIALS, SCRIPT_EDITS, (unsigned long long)sum_cache, (unsigned long long)sum_replayed);
    munmap(p, sizeof *p);
}

int main(void)
{
    test_group_boundaries();
    test_phases();
    test_campaign();
    if (jf_failures) {
        fprintf(stderr, "journal_kill_test: %d failure(s)\n", jf_failures);
        return 1;
    }
    printf("journal_kill_test: PASS\n");
    return 0;
}
