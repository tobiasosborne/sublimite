/* journal runtime (L2-2b): open, the typing-path append, the worker
 * transport, rotation, recovery and repair. Faults are injected beneath
 * the journal at the os seam (-Wl,--wrap, Makefile LDFLAGS_journal_test);
 * expected texts come from the cases' own byte models, never from the
 * journal's output. Fixtures live under build/journal-tests/. */
#include "journal_fixture.h"

#include <stdatomic.h>

/* ---- os seam ---- */
typedef enum wmode { W_PASS = 0, W_FAIL, W_SHORT, W_ZERO } wmode;
static _Atomic int pwrite_mode;
static _Atomic unsigned pwrite_calls;
static _Atomic int sync_fail;
os_err __real_os_file_pwrite_once(os_file *f, const void *buf, size_t n, uint64_t off, size_t *done);
os_err __wrap_os_file_pwrite_once(os_file *f, const void *buf, size_t n, uint64_t off, size_t *done)
{
    atomic_fetch_add(&pwrite_calls, 1u);
    switch ((wmode)atomic_load(&pwrite_mode)) {
    case W_PASS: return __real_os_file_pwrite_once(f, buf, n, off, done);
    case W_FAIL: *done = 0; return OS_E_IO;
    case W_SHORT: return __real_os_file_pwrite_once(f, buf, n - 1u, off, done);
    case W_ZERO: *done = 0; return OS_OK;
    }
    return OS_E_IO;
}
os_err __real_os_file_sync_data(os_file *f);
os_err __wrap_os_file_sync_data(os_file *f)
{
    if (atomic_load(&sync_fail)) return OS_E_IO;
    return __real_os_file_sync_data(f);
}

static work_pool pool_storage __attribute__((aligned(64)));
static work_pool *pool = &pool_storage;
static piece_reserve *reserve;
static const char *JN = "session.sjnl";
static uint8_t filebuf[1u << 22], filebuf2[1u << 22];

static journal *open_fresh(jf_dir *d)
{
    journal *j = NULL;
    journal_options o = jf_options();
    JF_REQUIRE(journal_open(&j, &d->dir, JN, pool, NULL, &o) == JOURNAL_OK);
    return j;
}

static void recover(jf_dir *d, journal_recovery *r)
{
    JF_REQUIRE(journal_recover(r, &d->dir, JN, reserve, NULL, NULL) == JOURNAL_OK);
}

/* ---- 1. fresh image, typing path, clean recovery ---- */
static void test_fresh_roundtrip(void)
{
    jf_dir d;
    jf_dir_make(&d, "journal_test", "fresh");
    journal *j = open_fresh(&d);
    journal_status s = journal_get_status(j);
    JF_CHECK(s.file_bytes == JOURNAL_PREAMBLE_BYTES, "fresh image is the preamble: %llu",
             (unsigned long long)s.file_bytes);
    JF_CHECK(s.transport == JOURNAL_TRANSPORT_IDLE && s.suspension == JOURNAL_HEALTHY, "idle healthy");
    uint64_t id;
    JF_REQUIRE(journal_new_buffer_id(j, &id) == JOURNAL_OK && id == 1);
    jf_base_empty(j, id);
    unsigned before = atomic_load(&pwrite_calls);
    JF_CHECK(jf_insert(j, id, 0, "hello world") == JOURNAL_OK, "insert");
    JF_CHECK(atomic_load(&pwrite_calls) - before == 1, "one pwrite_once per record");
    JF_CHECK(journal_append_delete(j, id, 5, 6) == JOURNAL_OK, "delete");
    JF_CHECK(journal_group_begin(j) == JOURNAL_OK, "begin");
    JF_CHECK(journal_append_delete(j, id, 0, 1) == JOURNAL_OK, "grouped delete");
    JF_CHECK(jf_insert(j, id, 0, "J") == JOURNAL_OK, "grouped insert");
    JF_CHECK(journal_group_commit(j) == JOURNAL_OK, "commit");
    s = journal_get_status(j);
    /* BASE, INSERT, DELETE, BEGIN, DELETE, INSERT, COMMIT. */
    JF_CHECK(s.accepted_sequence == 7 && s.written_sequence == 7, "sequences %llu %llu",
             (unsigned long long)s.accepted_sequence, (unsigned long long)s.written_sequence);
    jf_durable(j, pool);
    s = journal_get_status(j);
    JF_CHECK(s.durable_sequence == 7, "durable");
    JF_CHECK((s.durable_bytes - JOURNAL_PREAMBLE_BYTES) % JOURNAL_PAGE_BYTES == 0,
             "barrier ends on a block: %llu", (unsigned long long)s.durable_bytes);
    jf_close(j, pool);

    journal_recovery r;
    recover(&d, &r);
    JF_CHECK(r.verdict == JOURNAL_IMAGE_CLEAN, "clean: %d", (int)r.verdict);
    const journal_recovered_buffer *b = jf_buffer(&r, id);
    JF_CHECK(b && b->verdict == JOURNAL_BUFFER_RECOVERED && jf_tree_is(b->tree, "Jello", 5),
             "text Jello");
    JF_CHECK(r.buffer_high_water >= 1, "buffer high-water");
    journal_recovery_release(&r);
    jf_dir_remove(&d);
}

/* ---- 2. preamble refusal: set aside intact, never truncated ---- */
static size_t raw_preamble(uint8_t *out, uint32_t version, uint32_t features)
{
    journal_preamble_encode(out, 0, 0);
    for (unsigned i = 0; i < 4; i++) out[4 + i] = (uint8_t)(version >> (8 * i));
    for (unsigned i = 0; i < 4; i++) out[8 + i] = (uint8_t)(features >> (8 * i));
    uint32_t crc = journal_crc32c(out, 28);
    for (unsigned i = 0; i < 4; i++) out[28 + i] = (uint8_t)(crc >> (8 * i));
    return JOURNAL_PREAMBLE_BYTES;
}

static void refused_case(const char *tag, const uint8_t *img, size_t n, journal_codec_err why)
{
    jf_dir d;
    jf_dir_make(&d, "journal_test", tag);
    jf_write_file(&d, JN, img, n);
    journal_recovery r;
    recover(&d, &r);
    JF_CHECK(r.verdict == JOURNAL_IMAGE_REFUSED, "%s: refused (%d)", tag, (int)r.verdict);
    JF_CHECK(r.failure == why, "%s: reason %d want %d", tag, (int)r.failure, (int)why);
    JF_CHECK(r.set_aside[0] != 0, "%s: set-aside name", tag);
    os_stat st;
    JF_CHECK(os_stat_at(&d.dir, JN, 0, &st) == OS_E_NOENT, "%s: name freed", tag);
    size_t got = jf_read_file(&d, r.set_aside, filebuf2, sizeof filebuf2);
    JF_CHECK(got == n && memcmp(filebuf2, img, n) == 0, "%s: bytes intact (%zu of %zu)", tag, got, n);
    JF_CHECK(r.buffer_count == 0, "%s: nothing replayed", tag);
    /* The session continues on a fresh image beside the set-aside one. */
    journal *j = NULL;
    journal_options o = jf_options();
    JF_CHECK(journal_open(&j, &d.dir, JN, pool, &r, &o) == JOURNAL_OK, "%s: fresh open", tag);
    journal_recovery_release(&r);
    if (j) jf_close(j, pool);
    JF_CHECK(jf_dir_count(&d) == 2, "%s: fresh image + set-aside", tag);
    jf_dir_remove(&d);
}

static void test_preamble_refusal(void)
{
    /* A version-2 image whose record stream is a valid version-1 one: a
     * v1-style truncating scan would cut it at byte 0 or at the first
     * record it cannot read. */
    jf_dir d;
    jf_dir_make(&d, "journal_test", "v1src");
    journal *j = open_fresh(&d);
    jf_base_empty(j, 1);
    JF_REQUIRE(jf_insert(j, 1, 0, "future") == JOURNAL_OK);
    jf_close(j, pool);
    size_t n = jf_read_file(&d, JN, filebuf, sizeof filebuf);
    jf_dir_remove(&d);
    uint8_t img[1u << 16];
    JF_REQUIRE(n <= sizeof img);
    memcpy(img, filebuf, n);
    raw_preamble(img, 2, 0);
    refused_case("unknown-version", img, n, JOURNAL_CODEC_UNSUPPORTED);
    raw_preamble(img, 1, 1);
    refused_case("unknown-feature", img, n, JOURNAL_CODEC_UNSUPPORTED);
    memcpy(img, "JNL1", 4);
    refused_case("foreign-v1", img, n, JOURNAL_CODEC_FOREIGN);
    raw_preamble(img, 1, 0);
    img[13] ^= 0x40; /* CRC mismatch */
    refused_case("bad-crc", img, n, JOURNAL_CODEC_INVALID);
    raw_preamble(img, 1, 0);
    refused_case("torn-preamble", img, 20, JOURNAL_CODEC_TRUNCATED);
}

/* ---- 3. torn tails per record class, and in-place repair ---- */
typedef struct image_marks { uint64_t at[16]; size_t n; } image_marks;
static void mark(journal *j, image_marks *m)
{
    m->at[m->n++] = journal_get_status(j).file_bytes;
}

static void torn_case(const char *tag, uint64_t cut, const char *want, size_t want_len,
                      uint64_t want_valid)
{
    jf_dir d;
    jf_dir_make(&d, "journal_test", tag);
    jf_write_file(&d, JN, filebuf, (size_t)cut);
    journal_recovery r;
    recover(&d, &r);
    JF_CHECK(r.verdict == JOURNAL_IMAGE_REPAIRED, "%s: repaired in place (%d)", tag, (int)r.verdict);
    JF_CHECK(r.valid_bytes == want_valid, "%s: valid %llu want %llu", tag,
             (unsigned long long)r.valid_bytes, (unsigned long long)want_valid);
    os_stat st;
    JF_CHECK(os_stat_at(&d.dir, JN, 0, &st) == OS_OK && st.size == want_valid,
             "%s: truncated to the valid prefix", tag);
    JF_CHECK(r.set_aside[0] == 0 && jf_dir_count(&d) == 1, "%s: nothing set aside", tag);
    const journal_recovered_buffer *b = jf_buffer(&r, 1);
    JF_CHECK(b && b->verdict == JOURNAL_BUFFER_RECOVERED && jf_tree_is(b->tree, want, want_len),
             "%s: text", tag);
    /* Appending over the repaired tail loses nothing and recovers. */
    journal *j = NULL;
    journal_options o = jf_options();
    JF_CHECK(journal_open(&j, &d.dir, JN, pool, &r, &o) == JOURNAL_OK, "%s: continue", tag);
    journal_recovery_release(&r);
    if (j) {
        JF_CHECK(jf_insert(j, 1, 0, "+") == JOURNAL_OK, "%s: append after repair", tag);
        jf_close(j, pool);
        recover(&d, &r);
        b = jf_buffer(&r, 1);
        JF_CHECK(r.verdict == JOURNAL_IMAGE_CLEAN, "%s: clean after", tag);
        uint8_t w[64];
        w[0] = '+';
        memcpy(w + 1, want, want_len);
        JF_CHECK(b && jf_tree_is(b->tree, w, want_len + 1), "%s: text after", tag);
        journal_recovery_release(&r);
    }
    jf_dir_remove(&d);
}

static void test_torn_tails(void)
{
    jf_dir d;
    jf_dir_make(&d, "journal_test", "tornsrc");
    journal *j = open_fresh(&d);
    image_marks m = {0};
    jf_base_empty(j, 1);
    JF_REQUIRE(jf_insert(j, 1, 0, "abc") == JOURNAL_OK);
    mark(j, &m); /* 0: after the first insert */
    JF_REQUIRE(jf_insert(j, 1, 3, "def") == JOURNAL_OK);
    mark(j, &m); /* 1: after an ungrouped insert */
    JF_REQUIRE(journal_group_begin(j) == JOURNAL_OK);
    JF_REQUIRE(journal_append_delete(j, 1, 0, 6) == JOURNAL_OK);
    JF_REQUIRE(jf_insert(j, 1, 0, "XY") == JOURNAL_OK);
    mark(j, &m); /* 2: inside the group, before COMMIT */
    JF_REQUIRE(journal_group_commit(j) == JOURNAL_OK);
    mark(j, &m); /* 3: after the group */
    uint8_t view[4 + 8] = {1, 0, 0, 0, 'v', 'i', 'e', 'w', '1', '2', '3', '4'};
    JF_REQUIRE(journal_append(j, JOURNAL_REC_VIEW, 1, view, sizeof view) == JOURNAL_OK);
    mark(j, &m); /* 4: after VIEW */
    jf_durable(j, pool); /* writes a PAD */
    mark(j, &m); /* 5: after the PAD */
    JF_REQUIRE(jf_insert(j, 1, 2, "z") == JOURNAL_OK);
    mark(j, &m); /* 6 */
    jf_close(j, pool);
    size_t n = jf_read_file(&d, JN, filebuf, sizeof filebuf);
    JF_REQUIRE(n == m.at[6]);
    jf_dir_remove(&d);

    /* Ungrouped INSERT torn one byte short: dropped, prefix stands. */
    torn_case("torn-insert", m.at[1] - 1, "abc", 3, m.at[0]);
    /* Torn inside a group (input ends mid-record): no effect at all. */
    torn_case("torn-in-group", m.at[2] - 5, "abcdef", 6, m.at[1]);
    /* Group complete but COMMIT torn: still no effect. */
    torn_case("torn-commit", m.at[3] - 1, "abcdef", 6, m.at[1]);
    /* Group whose COMMIT never started: open group at EOF. */
    torn_case("open-group", m.at[2], "abcdef", 6, m.at[1]);
    /* A torn VIEW: earlier state stands. */
    torn_case("torn-view", m.at[4] - 3, "XY", 2, m.at[3]);
    /* A torn PAD is dropped with no effect. */
    torn_case("torn-pad", m.at[4] + 40, "XY", 2, m.at[4]);
    /* A torn header (fewer than 32 B of the next record). */
    torn_case("torn-header", m.at[5] + 7, "XY", 2, m.at[5]);
}

/* ---- 4. damage: set aside byte-identical, session continues ---- */
static void test_damage_set_aside(void)
{
    jf_dir d;
    jf_dir_make(&d, "journal_test", "damage");
    journal *j = open_fresh(&d);
    jf_base_empty(j, 1);
    JF_REQUIRE(jf_insert(j, 1, 0, "keep") == JOURNAL_OK);
    uint64_t victim = journal_get_status(j).file_bytes;
    JF_REQUIRE(jf_insert(j, 1, 4, "-damaged-") == JOURNAL_OK);
    JF_REQUIRE(jf_insert(j, 1, 0, "acked-after:") == JOURNAL_OK);
    jf_close(j, pool);
    size_t n = jf_read_file(&d, JN, filebuf, sizeof filebuf);
    filebuf[victim + JOURNAL_HEADER_BYTES + 9] ^= 0x01; /* one payload bit */
    jf_write_file(&d, JN, filebuf, n);

    journal_recovery r;
    recover(&d, &r);
    JF_CHECK(r.verdict == JOURNAL_IMAGE_SET_ASIDE, "set aside (%d)", (int)r.verdict);
    JF_CHECK(r.failure == JOURNAL_CODEC_INVALID, "complete record failing CRC");
    JF_CHECK(r.valid_bytes == victim, "valid prefix ends before the damage");
    size_t got = jf_read_file(&d, r.set_aside, filebuf2, sizeof filebuf2);
    JF_CHECK(got == n && memcmp(filebuf2, filebuf, n) == 0, "original byte-identical aside");
    got = jf_read_file(&d, JN, filebuf2, sizeof filebuf2);
    JF_CHECK(got == victim && memcmp(filebuf2, filebuf, victim) == 0, "new image = valid prefix");
    const journal_recovered_buffer *b = jf_buffer(&r, 1);
    JF_CHECK(b && jf_tree_is(b->tree, "keep", 4), "prefix text");
    journal_options o = jf_options();
    JF_CHECK(journal_open(&j, &d.dir, JN, pool, &r, &o) == JOURNAL_OK, "continue on new image");
    journal_recovery_release(&r);
    JF_CHECK(jf_insert(j, 1, 4, "!") == JOURNAL_OK, "append");
    jf_close(j, pool);
    recover(&d, &r);
    b = jf_buffer(&r, 1);
    JF_CHECK(r.verdict == JOURNAL_IMAGE_CLEAN && b && jf_tree_is(b->tree, "keep!", 5), "continued");
    journal_recovery_release(&r);
    JF_CHECK(jf_dir_count(&d) == 2, "set-aside kept");
    jf_dir_remove(&d);

    /* A length field corrupted to claim past EOF, with acknowledged
     * records after it: not a torn tail, because a CRC-valid record
     * follows the failing header. */
    jf_dir_make(&d, "journal_test", "badlen");
    j = open_fresh(&d);
    jf_base_empty(j, 1);
    JF_REQUIRE(jf_insert(j, 1, 0, "keep") == JOURNAL_OK);
    victim = journal_get_status(j).file_bytes;
    JF_REQUIRE(jf_insert(j, 1, 4, "x") == JOURNAL_OK);
    JF_REQUIRE(jf_insert(j, 1, 5, "acked") == JOURNAL_OK);
    jf_close(j, pool);
    n = jf_read_file(&d, JN, filebuf, sizeof filebuf);
    filebuf[victim + 4 + 1] ^= 0x10; /* length += 4096 */
    jf_write_file(&d, JN, filebuf, n);
    recover(&d, &r);
    JF_CHECK(r.verdict == JOURNAL_IMAGE_SET_ASIDE, "bad length set aside (%d)", (int)r.verdict);
    JF_CHECK(r.failure == JOURNAL_CODEC_TRUNCATED, "it looked torn: the scan decided");
    got = jf_read_file(&d, r.set_aside, filebuf2, sizeof filebuf2);
    JF_CHECK(got == n && memcmp(filebuf2, filebuf, n) == 0, "bad length: original aside");
    journal_recovery_release(&r);
    jf_dir_remove(&d);
}

/* ---- 5. A23 T5: foreign replacement of a BASE-relative buffer ---- */
static void test_t5_unresolved_base(void)
{
    for (int variant = 0; variant < 2; variant++) {
        jf_dir d;
        jf_dir_make(&d, "journal_test", variant ? "t5-deleted" : "t5-foreign");
        /* X: longer than the prefix witness, so the foreign F below can
         * share its first block and still differ. */
        static uint8_t x[3 * JOURNAL_PREFIX_BYTES];
        for (size_t i = 0; i < sizeof x; i++) x[i] = (uint8_t)('a' + i % 26);
        jf_write_file(&d, "file.txt", x, sizeof x);
        journal *j = open_fresh(&d);
        char path[OS_PATH_MAX];
        jf_declare_base(j, &d, "file.txt", 7, path, sizeof path);
        JF_REQUIRE(jf_insert(j, 7, sizeof x, "!") == JOURNAL_OK);
        jf_durable(j, pool);
        jf_close(j, pool);
        if (variant == 0) {
            /* Another program renames F over the path. */
            static uint8_t f[sizeof x];
            memcpy(f, x, sizeof f);
            f[sizeof f - 1] = 'F';
            jf_write_file(&d, "foreign.tmp", f, sizeof f);
            JF_REQUIRE(os_rename_at(&d.dir, "foreign.tmp", &d.dir, "file.txt",
                                    OS_RENAME_REPLACE) == OS_OK);
        } else {
            JF_REQUIRE(os_unlink_at(&d.dir, "file.txt") == OS_OK);
        }
        journal_recovery r;
        recover(&d, &r);
        const journal_recovered_buffer *b = jf_buffer(&r, 7);
        JF_CHECK(b && b->verdict == JOURNAL_BUFFER_UNRESOLVED_BASE, "T5 %d: unresolved BASE", variant);
        JF_CHECK(b && b->tree == NULL, "T5 %d: nothing applied", variant);
        JF_CHECK(b && b->literal_count == 1 && b->literals[0].len == 1 &&
                 b->literals[0].bytes[0] == '!' && b->literals[0].destination == sizeof x,
                 "T5 %d: the literal bytes offered", variant);
        journal_recovery_release(&r);
        jf_dir_remove(&d);
    }
}

/* ---- 6. A23 self-contained: recovers after BASE deleted/replaced ---- */
static void test_self_contained(void)
{
    for (int variant = 0; variant < 2; variant++) {
        jf_dir d;
        jf_dir_make(&d, "journal_test", variant ? "sc-replaced" : "sc-deleted");
        const char *x = "the original text\n";
        jf_write_file(&d, "small.txt", x, strlen(x));
        journal *j = open_fresh(&d);
        char path[OS_PATH_MAX];
        jf_declare_base(j, &d, "small.txt", 3, path, sizeof path);
        /* The live text mirrors the journal: a tree over X plus edits. */
        piece_tree *t;
        JF_REQUIRE(piece_open_copy(reserve, (const uint8_t *)x, strlen(x), &t) == PIECE_OK);
        JF_REQUIRE(jf_insert(j, 3, 0, ">> ") == JOURNAL_OK);
        JF_REQUIRE(piece_replace(t, 0, 0, (const uint8_t *)">> ", 3) == PIECE_OK);
        uint64_t covered = journal_get_status(j).written_sequence;
        piece_snapshot *snap;
        JF_REQUIRE(piece_snapshot_take(t, &snap) == PIECE_OK);
        /* An edit between the snapshot and the group: it precedes the
         * group physically, follows its covered boundary. */
        JF_REQUIRE(journal_append_delete(j, 3, 3, 4) == JOURNAL_OK);
        JF_REQUIRE(piece_replace(t, 3, 4, NULL, 0) == PIECE_OK);
        journal_checkpoint_request req = {.buffer_id = 3, .text = snap,
                                          .covered_sequence = covered, .content_id = 9};
        JF_CHECK(journal_checkpoint_start(j, &req) == JOURNAL_OK, "checkpoint start");
        piece_snapshot_release(snap);
        JF_CHECK(journal_get_status(j).transport == JOURNAL_TRANSPORT_LEASE ||
                 journal_checkpoint_committed(j) != 0, "leased");
        jf_settle(j, pool);
        JF_CHECK(journal_checkpoint_committed(j) != 0, "group committed");
        JF_REQUIRE(jf_insert(j, 3, 0, "#") == JOURNAL_OK);
        JF_REQUIRE(piece_replace(t, 0, 0, (const uint8_t *)"#", 1) == PIECE_OK);
        jf_durable(j, pool);
        JF_CHECK(journal_get_status(j).durable_sequence >= journal_checkpoint_committed(j),
                 "self-contained once durable");
        jf_close(j, pool);
        static uint8_t want[256];
        size_t want_len = jf_text(piece_tree_view(t), want, sizeof want);
        piece_close(t);
        if (variant == 0) {
            JF_REQUIRE(os_unlink_at(&d.dir, "small.txt") == OS_OK);
        } else {
            jf_write_file(&d, "other.tmp", "someone else's file", 19);
            JF_REQUIRE(os_rename_at(&d.dir, "other.tmp", &d.dir, "small.txt",
                                    OS_RENAME_REPLACE) == OS_OK);
        }
        journal_recovery r;
        recover(&d, &r);
        const journal_recovered_buffer *b = jf_buffer(&r, 3);
        JF_CHECK(b && b->verdict == JOURNAL_BUFFER_RECOVERED, "sc %d: recovered", variant);
        JF_CHECK(b && b->origin == JOURNAL_ORIGIN_CHECKPOINT && b->origin_sequence == covered,
                 "sc %d: origin is the checkpoint", variant);
        JF_CHECK(b && jf_tree_is(b->tree, want, want_len), "sc %d: exact text", variant);
        journal_recovery_release(&r);
        jf_dir_remove(&d);
    }
}

/* ---- 7. a failed typing-path write: held, latched, repaired ---- */
static void test_append_gap(void)
{
    const wmode modes[] = {W_FAIL, W_SHORT, W_ZERO};
    for (size_t k = 0; k < sizeof modes / sizeof modes[0]; k++) {
        jf_dir d;
        jf_dir_make(&d, "journal_test", k == 0 ? "gap-fail" : k == 1 ? "gap-short" : "gap-zero");
        journal *j = open_fresh(&d);
        jf_base_empty(j, 1);
        JF_REQUIRE(jf_insert(j, 1, 0, "ab") == JOURNAL_OK);
        atomic_store(&pwrite_mode, (int)modes[k]);
        size_t progress = 0;
        JF_CHECK(journal_insert_step(j, 1, 2, (const uint8_t *)"cd", 2, &progress) == JOURNAL_ERR_IO,
                 "gap %zu: IO", k);
        atomic_store(&pwrite_mode, W_PASS);
        JF_CHECK(progress == 2, "gap %zu: accepted into RAM", k);
        journal_status s = journal_get_status(j);
        JF_CHECK(s.suspension == JOURNAL_APPEND_GAP && s.accepted_sequence == 3 &&
                 s.written_sequence == 2, "gap %zu: latched", k);
        JF_CHECK(jf_insert(j, 1, 0, "x") == JOURNAL_ERR_BUSY, "gap %zu: later appends wait", k);
        jf_settle(j, pool);
        s = journal_get_status(j);
        JF_CHECK(s.suspension == JOURNAL_HEALTHY && s.written_sequence == 3, "gap %zu: repaired", k);
        JF_CHECK(jf_insert(j, 1, 4, "e") == JOURNAL_OK, "gap %zu: appends resume", k);
        jf_close(j, pool);
        journal_recovery r;
        recover(&d, &r);
        const journal_recovered_buffer *b = jf_buffer(&r, 1);
        JF_CHECK(r.verdict == JOURNAL_IMAGE_CLEAN && b && jf_tree_is(b->tree, "abcde", 5),
                 "gap %zu: recorded exactly once", k);
        journal_recovery_release(&r);
        jf_dir_remove(&d);
    }
}

/* ---- 8. a failed barrier: WRITEBACK_LOST until a rotation ---- */
static void test_writeback_lost_and_rotation(void)
{
    jf_dir d;
    jf_dir_make(&d, "journal_test", "rotate");
    static uint8_t x[3 * PIECE_CHUNK + 77];
    for (size_t i = 0; i < sizeof x; i++) x[i] = (uint8_t)('A' + i % 23);
    jf_write_file(&d, "big.txt", x, sizeof x);
    journal *j = open_fresh(&d);
    char path[OS_PATH_MAX];
    uint64_t key = jf_declare_base(j, &d, "big.txt", 1, path, sizeof path);
    piece_tree *t1, *t2;
    JF_REQUIRE(piece_open_copy(reserve, x, sizeof x, &t1) == PIECE_OK);
    JF_REQUIRE(piece_open_copy(reserve, NULL, 0, &t2) == PIECE_OK);
    jf_base_empty(j, 2);
    /* Buffer 1: edits that reorder and repeat ORIGINAL ranges. */
    JF_REQUIRE(journal_append_delete(j, 1, 100, 1000) == JOURNAL_OK);
    JF_REQUIRE(piece_replace(t1, 100, 1000, NULL, 0) == PIECE_OK);
    journal_restore rs = {.destination = 0, .generation_key = key, .source_offset = 70000, .len = 500};
    JF_REQUIRE(journal_append_restore(j, 1, &rs) == JOURNAL_OK);
    piece_span sp = {70000, 500};
    JF_REQUIRE(piece_replace_spans(t1, 0, 0, &sp, NULL, 1) == PIECE_OK);
    JF_REQUIRE(journal_append_restore(j, 1, &rs) == JOURNAL_OK);
    JF_REQUIRE(piece_replace_spans(t1, 0, 0, &sp, NULL, 1) == PIECE_OK);
    JF_REQUIRE(jf_insert(j, 1, 7, "<typed>") == JOURNAL_OK);
    JF_REQUIRE(piece_replace(t1, 7, 0, (const uint8_t *)"<typed>", 7) == PIECE_OK);
    JF_REQUIRE(jf_insert(j, 2, 0, "untitled") == JOURNAL_OK);
    JF_REQUIRE(piece_replace(t2, 0, 0, (const uint8_t *)"untitled", 8) == PIECE_OK);

    atomic_store(&sync_fail, 1);
    jf_durable(j, pool);
    atomic_store(&sync_fail, 0);
    journal_status s = journal_get_status(j);
    JF_CHECK(s.suspension == JOURNAL_WRITEBACK_LOST, "writeback lost (%d)", (int)s.suspension);
    JF_CHECK(jf_insert(j, 2, 0, "x") == JOURNAL_ERR_SUSPENDED, "suspended until rotation");

    piece_snapshot *s1, *s2;
    JF_REQUIRE(piece_snapshot_take(t1, &s1) == PIECE_OK);
    JF_REQUIRE(piece_snapshot_take(t2, &s2) == PIECE_OK);
    os_file f = OS_FILE_INIT;
    JF_REQUIRE(os_file_open(&f, path, OS_OPEN_READ) == OS_OK);
    journal_generation g = {0};
    JF_REQUIRE(journal_capture(&f, &g) == OS_OK);
    os_file_close(&f);
    g.key = key;
    g.location = (const uint8_t *)path;
    g.location_len = strlen(path);
    uint8_t vbytes[] = {'v'};
    journal_view v = {.version = 3, .opaque = vbytes, .opaque_len = 1};
    journal_target tg = {.binding = JOURNAL_TARGET_UNTITLED, .presence = JOURNAL_ABSENT};
    journal_rotation_buffer rb[2] = {
        {.buffer_id = 1, .text = s1, .mode = JOURNAL_ROTATE_BY_REFERENCE, .base_key = key,
         .content_id = 11, .view = &v},
        {.buffer_id = 2, .text = s2, .mode = JOURNAL_ROTATE_BY_VALUE, .content_id = 12,
         .target = &tg},
    };
    journal_tabs_entry tabs[2] = {{1, JOURNAL_TABS_OPEN}, {2, JOURNAL_TABS_OPEN}};
    journal_rotation rot = {.generations = &g, .generation_count = 1, .buffers = rb,
                            .buffer_count = 2, .tabs = tabs, .tab_count = 2, .tab_active = 1};
    JF_CHECK(journal_rotate_start(j, &rot) == JOURNAL_OK, "rotation start");
    jf_settle(j, pool);
    piece_snapshot_release(s1);
    piece_snapshot_release(s2);
    s = journal_get_status(j);
    JF_CHECK(s.suspension == JOURNAL_HEALTHY, "rotation repairs (%d)", (int)s.suspension);
    JF_CHECK(s.durable_sequence == s.accepted_sequence, "rotation image durable");
    JF_CHECK(jf_insert(j, 2, 8, "!") == JOURNAL_OK, "appends resume on the new image");
    JF_REQUIRE(piece_replace(t2, 8, 0, (const uint8_t *)"!", 1) == PIECE_OK);
    jf_close(j, pool);
    JF_CHECK(jf_dir_count(&d) == 2, "old image replaced, no temporary left");

    journal_recovery r;
    recover(&d, &r);
    const journal_recovered_buffer *b1 = jf_buffer(&r, 1), *b2 = jf_buffer(&r, 2);
    static uint8_t want[sizeof x + 2048];
    size_t want_len = jf_text(piece_tree_view(t1), want, sizeof want);
    JF_CHECK(b1 && b1->verdict == JOURNAL_BUFFER_RECOVERED && b1->origin == JOURNAL_ORIGIN_BASE &&
             jf_tree_is(b1->tree, want, want_len), "by-reference buffer exact");
    JF_CHECK(b1 && b1->has_view && b1->view.version == 3 && b1->view.opaque_len == 1, "view");
    JF_CHECK(b2 && b2->origin == JOURNAL_ORIGIN_CHECKPOINT && jf_tree_is(b2->tree, "untitled!", 9),
             "by-value buffer exact");
    JF_CHECK(b2 && b2->has_target && b2->target.binding == JOURNAL_TARGET_UNTITLED, "target");
    JF_CHECK(r.has_tabs && r.tab_count == 2 && r.tab_active == 1, "tabs");
    journal_recovery_release(&r);
    piece_close(t1);
    piece_close(t2);
    jf_dir_remove(&d);
}

/* ---- 9. restores: by reference, by value, refused ---- */
static void test_restores(void)
{
    jf_dir d;
    jf_dir_make(&d, "journal_test", "restore");
    jf_write_file(&d, "a.txt", "0123456789", 10);
    jf_write_file(&d, "b.txt", "abcdefghij", 10);
    journal *j = open_fresh(&d);
    char pa[OS_PATH_MAX], pb[OS_PATH_MAX];
    uint64_t ka = jf_declare_base(j, &d, "a.txt", 1, pa, sizeof pa);
    uint64_t kb = jf_declare_base(j, &d, "b.txt", 2, pb, sizeof pb);
    /* Buffer 1 over a: restore its own range (reference) and b's (value). */
    journal_restore r1 = {.destination = 10, .generation_key = ka, .source_offset = 2, .len = 3};
    journal_restore r2 = {.destination = 0, .generation_key = kb, .source_offset = 7, .len = 3};
    JF_CHECK(journal_append_restore(j, 1, &r1) == JOURNAL_OK, "restore ref");
    JF_CHECK(journal_append_restore(j, 1, &r2) == JOURNAL_OK, "restore value");
    /* Buffer 2 over b: a restore past its source's end is inconsistent. */
    journal_restore r3 = {.destination = 0, .generation_key = kb, .source_offset = 9, .len = 2};
    JF_CHECK(journal_append_restore(j, 2, &r3) == JOURNAL_OK, "bad restore appended");
    JF_REQUIRE(jf_insert(j, 2, 0, "lit") == JOURNAL_OK);
    jf_close(j, pool);
    journal_recovery r;
    recover(&d, &r);
    const journal_recovered_buffer *b1 = jf_buffer(&r, 1), *b2 = jf_buffer(&r, 2);
    JF_CHECK(b1 && jf_tree_is(b1->tree, "hij0123456789234", 16), "mixed-generation restores");
    JF_CHECK(b2 && b2->verdict == JOURNAL_BUFFER_INCONSISTENT && b2->tree == NULL &&
             b2->literal_count == 1 && b2->literals[0].len == 3, "out-of-range restore refused");
    journal_recovery_release(&r);
    jf_dir_remove(&d);
}

/* ---- 10. insert_step slices, FULL, allocators ---- */
static void test_slices_full_allocators(void)
{
    jf_dir d;
    jf_dir_make(&d, "journal_test", "slices");
    journal *j = open_fresh(&d);
    jf_base_empty(j, 1);
    static uint8_t big[3 * JOURNAL_INSERT_SLICE_BYTES + 123];
    for (size_t i = 0; i < sizeof big; i++) big[i] = (uint8_t)(i * 7u);
    size_t progress = 0;
    unsigned steps = 0;
    journal_err e;
    do {
        e = journal_insert_step(j, 1, 0, big, sizeof big, &progress);
        steps++;
    } while (e == JOURNAL_MORE);
    JF_CHECK(e == JOURNAL_OK && steps == 4 && progress == sizeof big, "four slices (%u)", steps);
    jf_close(j, pool);
    journal_recovery r;
    recover(&d, &r);
    const journal_recovered_buffer *b = jf_buffer(&r, 1);
    JF_CHECK(b && jf_tree_is(b->tree, big, sizeof big), "sliced insert exact");
    JF_CHECK(r.buffer_high_water >= 1, "high-water past mentions");
    /* Reopening first seals the valid end with a PAD to the next block. */
    uint64_t start = r.valid_bytes + journal_pad_length(r.valid_bytes - JOURNAL_PREAMBLE_BYTES);
    journal_options o = {.budget_bytes = start + 100, .sync_bytes = UINT64_C(1) << 40,
                         .sync_interval_ns = UINT64_MAX};
    JF_CHECK(journal_open(&j, &d.dir, JN, pool, &r, &o) == JOURNAL_OK, "reopen");
    journal_recovery_release(&r);
    uint64_t id;
    JF_CHECK(journal_new_buffer_id(j, &id) == JOURNAL_OK && id == 2, "ids continue past the image");
    JF_CHECK(jf_insert(j, 1, 0, "x") == JOURNAL_OK, "within budget");
    JF_CHECK(jf_insert(j, 1, 0, "0123456789012345678901234567890123456789") == JOURNAL_ERR_FULL,
             "budget FULL");
    JF_CHECK(journal_get_status(j).suspension == JOURNAL_NEEDS_ROTATION, "needs rotation");
    JF_CHECK(jf_insert(j, 1, 0, "y") == JOURNAL_ERR_SUSPENDED, "then suspended");
    jf_close(j, pool);
    jf_dir_remove(&d);
}

int main(void)
{
    JF_REQUIRE(work_pool_init(pool, 1, 0) == 0);
    JF_REQUIRE(piece_reserve_create(&reserve) == PIECE_OK);
    test_fresh_roundtrip();
    test_preamble_refusal();
    test_torn_tails();
    test_damage_set_aside();
    test_t5_unresolved_base();
    test_self_contained();
    test_append_gap();
    test_writeback_lost_and_rotation();
    test_restores();
    test_slices_full_allocators();
    piece_reserve_destroy(reserve);
    work_pool_shutdown(pool);
    if (jf_failures) {
        fprintf(stderr, "journal_test: %d failure(s)\n", jf_failures);
        return 1;
    }
    printf("journal_test: PASS\n");
    return 0;
}
