/* libFuzzer target for the journal runtime (L2-2b): append sequences,
 * torn tails, damage and recovery. Two modes, chosen by the first byte:
 *   raw:    the rest of the input is a journal file. Recovery must agree
 *           with journal_stream_validate on the committed prefix, keep a
 *           refused or damaged file byte-identical under its set-aside
 *           name, and repair a torn tail to exactly the valid prefix.
 *   script: the input drives a real journal (single-record edits,
 *           grouped replacements, barriers) against a byte model, then
 *           cuts the file at a fuzzed byte or flips a fuzzed bit, and
 *           recovery must return the model's text at a command boundary
 *           no later than the cut (or, for damage, the original aside).
 * Files live under build/journal-fuzz-<pid>/ and are removed per input. */
#include "journal/journal.h"
#include "os/os.h"
#include "work/work.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define REQUIRE(c) do { if (!(c)) __builtin_trap(); } while (0)

static const char *JN = "fuzz.sjnl";
static os_dir dir = OS_DIR_INIT;
static char dir_path[256];
static work_pool pool __attribute__((aligned(64)));
static piece_reserve *reserve;
static uint8_t filebuf[1u << 21], asidebuf[1u << 21];

static void clear_dir(void);
static void teardown(void)
{
    clear_dir();
    os_dir_close(&dir);
    (void)os_rmdir(dir_path);
}

static void setup(void)
{
    if (os_dir_is_open(&dir)) return;
    atexit(teardown);
    (void)os_mkdir("build");
    int n = snprintf(dir_path, sizeof dir_path, "build/journal-fuzz-%ld", (long)getpid());
    REQUIRE(n > 0 && (size_t)n < sizeof dir_path);
    (void)os_mkdir(dir_path);
    REQUIRE(os_dir_open(&dir, dir_path) == OS_OK);
    REQUIRE(work_pool_init(&pool, 1, 0) == 0);
    REQUIRE(piece_reserve_create(&reserve) == PIECE_OK);
}

static void clear_dir(void)
{
    os_dir_iter it;
    REQUIRE(os_dir_iter_open(&it, &dir) == OS_OK);
    const char *name;
    char names[16][256];
    size_t n = 0;
    while (os_dir_next(&it, &name) == OS_OK && name) {
        REQUIRE(n < 16 && strlen(name) < 256);
        strcpy(names[n++], name);
    }
    os_dir_iter_close(&it);
    for (size_t i = 0; i < n; i++) REQUIRE(os_unlink_at(&dir, names[i]) == OS_OK);
}

static size_t read_file(const char *name, uint8_t *out, size_t cap)
{
    os_file f = OS_FILE_INIT;
    REQUIRE(os_file_open_at(&f, &dir, name, OS_OPEN_READ) == OS_OK);
    size_t at = 0;
    for (;;) {
        size_t got = 0;
        REQUIRE(at < cap);
        REQUIRE(os_file_pread(&f, out + at, cap - at, at, &got) == OS_OK);
        if (!got) break;
        at += got;
    }
    os_file_close(&f);
    return at;
}

static void write_file(const char *name, const uint8_t *b, size_t n)
{
    os_file f = OS_FILE_INIT;
    REQUIRE(os_file_open_at(&f, &dir, name, OS_OPEN_WRITE | OS_OPEN_CREATE | OS_OPEN_TRUNC) == OS_OK);
    size_t done = 0;
    if (n) REQUIRE(os_file_pwrite(&f, b, n, 0, &done) == OS_OK && done == n);
    os_file_close(&f);
}

static bool tree_is(piece_tree *t, const uint8_t *want, size_t n)
{
    static uint8_t buf[1u << 16];
    if (!t) return false;
    const piece_view *v = piece_tree_view(t);
    if (piece_length(v) != n) return false;
    piece_iter it;
    piece_iter_init(&it, v, 0);
    size_t at = 0, k;
    const uint8_t *p;
    while ((k = piece_iter_next(&it, &p)) != 0) {
        memcpy(buf + at, p, k);
        at += k;
    }
    return at == n && (n == 0 || memcmp(buf, want, n) == 0);
}

/* ---- raw mode ---- */
static void raw(const uint8_t *data, size_t size)
{
    write_file(JN, data, size);
    journal_stream_result vr;
    journal_codec_err ve = journal_stream_validate(data, size, &vr);
    journal_recovery r;
    journal_err e = journal_recover(&r, &dir, JN, reserve, NULL, NULL);
    REQUIRE(e == JOURNAL_OK);
    switch (r.verdict) {
    case JOURNAL_IMAGE_ABSENT: __builtin_trap();
    case JOURNAL_IMAGE_REFUSED:
        REQUIRE(ve != JOURNAL_CODEC_OK && vr.valid_bytes == 0);
        REQUIRE(read_file(r.set_aside, asidebuf, sizeof asidebuf) == size);
        REQUIRE(size == 0 || memcmp(asidebuf, data, size) == 0);
        break;
    case JOURNAL_IMAGE_CLEAN:
        REQUIRE(ve == JOURNAL_CODEC_OK && r.valid_bytes == size);
        break;
    case JOURNAL_IMAGE_REPAIRED:
        REQUIRE(ve == JOURNAL_CODEC_TRUNCATED && r.valid_bytes == vr.valid_bytes);
        REQUIRE(read_file(JN, filebuf, sizeof filebuf) == r.valid_bytes);
        REQUIRE(memcmp(filebuf, data, r.valid_bytes) == 0);
        break;
    case JOURNAL_IMAGE_SET_ASIDE:
        REQUIRE(ve != JOURNAL_CODEC_OK && r.valid_bytes == vr.valid_bytes);
        REQUIRE(read_file(r.set_aside, asidebuf, sizeof asidebuf) == size);
        REQUIRE(memcmp(asidebuf, data, size) == 0);
        REQUIRE(read_file(JN, filebuf, sizeof filebuf) == r.valid_bytes);
        REQUIRE(memcmp(filebuf, data, r.valid_bytes) == 0);
        break;
    case JOURNAL_IMAGE_DAMAGED: __builtin_trap(); /* space is not short here */
    }
    if (r.verdict != JOURNAL_IMAGE_REFUSED) REQUIRE(r.last_sequence == vr.last_sequence);
    journal_recovery_release(&r);
    clear_dir();
}

/* ---- script mode ---- */
#define TEXT_MAX 200u
#define BOUNDARIES 64u
typedef struct model {
    uint8_t text[TEXT_MAX];
    size_t len;
} model;
typedef struct boundary {
    uint64_t end;  /* file bytes after the command */
    model m;
} boundary;

static void ignore(const work_msg *m, void *ud) { (void)m; (void)ud; }

static void settle(journal *j)
{
    for (;;) {
        (void)work_mailbox_drain(&pool, ignore, NULL);
        (void)journal_pump(j, os_now_ns());
        if (journal_get_status(j).transport == JOURNAL_TRANSPORT_IDLE) return;
        os_sleep_ns(20000);
    }
}

static void script(const uint8_t *data, size_t size)
{
    journal *j;
    journal_options o = {.budget_bytes = UINT64_C(1) << 40, .sync_bytes = UINT64_C(1) << 40,
                         .sync_interval_ns = UINT64_MAX};
    REQUIRE(journal_open(&j, &dir, JN, &pool, NULL, &o) == JOURNAL_OK);
    uint8_t base[16];
    size_t bn = 0;
    journal_base b = {.generation_key = 0, .content_id = 1};
    REQUIRE(journal_base_encode(base, sizeof base, &bn, &b) == JOURNAL_CODEC_OK);
    REQUIRE(journal_append(j, JOURNAL_REC_BASE, 1, base, bn) == JOURNAL_OK);
    static boundary bd[BOUNDARIES + 1];
    size_t nb = 0;
    model m = {.len = 0};
    bd[nb++] = (boundary){.end = journal_get_status(j).file_bytes, .m = m};
    size_t at = 0;
    while (at + 3 <= size && nb <= BOUNDARIES) {
        uint8_t op = data[at] % 4, x = data[at + 1], y = data[at + 2];
        at += 3;
        size_t off = m.len ? x % (m.len + 1) : 0;
        switch (op) {
        case 0: { /* single-record INSERT of 1..8 bytes */
            size_t n = 1u + y % 8u;
            if (m.len + n > TEXT_MAX) continue;
            uint8_t bytes[8];
            for (size_t i = 0; i < n; i++) bytes[i] = (uint8_t)(y + i);
            size_t progress = 0;
            REQUIRE(journal_insert_step(j, 1, off, bytes, n, &progress) == JOURNAL_OK);
            memmove(m.text + off + n, m.text + off, m.len - off);
            memcpy(m.text + off, bytes, n);
            m.len += n;
            break;
        }
        case 1: { /* single-record DELETE */
            if (off >= m.len) continue;
            size_t n = 1u + y % (m.len - off);
            REQUIRE(journal_append_delete(j, 1, off, n) == JOURNAL_OK);
            memmove(m.text + off, m.text + off + n, m.len - off - n);
            m.len -= n;
            break;
        }
        case 2: { /* grouped replacement: DELETE then INSERT */
            if (off >= m.len) continue;
            size_t n = 1u + y % (m.len - off);
            REQUIRE(journal_group_begin(j) == JOURNAL_OK);
            REQUIRE(journal_append_delete(j, 1, off, n) == JOURNAL_OK);
            uint8_t rep[2] = {x, y};
            size_t progress = 0;
            REQUIRE(journal_insert_step(j, 1, off, rep, 2, &progress) == JOURNAL_OK);
            REQUIRE(journal_group_commit(j) == JOURNAL_OK);
            memmove(m.text + off + 2, m.text + off + n, m.len - off - n);
            memcpy(m.text + off, rep, 2);
            m.len = m.len - n + 2;
            break;
        }
        case 3: /* a barrier: PAD + sync */
            journal_request_sync(j);
            settle(j);
            break;
        }
        bd[nb++] = (boundary){.end = journal_get_status(j).file_bytes, .m = m};
    }
    settle(j);
    REQUIRE(journal_close(j) == JOURNAL_OK);
    size_t n = read_file(JN, filebuf, sizeof filebuf);
    /* Damage from the last bytes of the input: a cut or a bit flip. */
    uint64_t pos = size >= 2 ? ((uint64_t)data[size - 2] << 8 | data[size - 1]) : 0;
    bool flip = size >= 3 && (data[size - 3] & 1u);
    uint64_t where = n > JOURNAL_PREAMBLE_BYTES ? JOURNAL_PREAMBLE_BYTES + pos % (n - JOURNAL_PREAMBLE_BYTES)
                                                : n;
    if (flip && where < n) filebuf[where] ^= (uint8_t)(1u << (data[size - 3] >> 5));
    size_t len = flip ? n : (size_t)where;
    write_file(JN, filebuf, len);
    journal_recovery r;
    REQUIRE(journal_recover(&r, &dir, JN, reserve, NULL, NULL) == JOURNAL_OK);
    switch (r.verdict) {
    case JOURNAL_IMAGE_CLEAN:
    case JOURNAL_IMAGE_REPAIRED:
    case JOURNAL_IMAGE_SET_ASIDE: {
        /* The committed prefix ends at a command boundary (or a PAD
         * after one) and the text is the model there. */
        size_t k = 0;
        while (k + 1 < nb && bd[k + 1].end <= r.valid_bytes) k++;
        const journal_recovered_buffer *rb = NULL;
        for (size_t i = 0; i < r.buffer_count; i++)
            if (r.buffers[i].id == 1) rb = &r.buffers[i];
        if (r.valid_bytes < bd[0].end) {
            REQUIRE(rb == NULL); /* the cut or flip reached the BASE itself */
        } else {
            REQUIRE(rb != NULL && rb->verdict == JOURNAL_BUFFER_RECOVERED);
            REQUIRE(tree_is(rb->tree, bd[k].m.text, bd[k].m.len));
        }
        if (!flip) REQUIRE(r.verdict != JOURNAL_IMAGE_SET_ASIDE);
        if (r.verdict == JOURNAL_IMAGE_SET_ASIDE) {
            REQUIRE(read_file(r.set_aside, asidebuf, sizeof asidebuf) == len);
            REQUIRE(memcmp(asidebuf, filebuf, len) == 0);
        }
        break;
    }
    case JOURNAL_IMAGE_REFUSED: /* the damage never reaches the preamble */
    case JOURNAL_IMAGE_ABSENT:
    case JOURNAL_IMAGE_DAMAGED: __builtin_trap();
    }
    journal_recovery_release(&r);
    clear_dir();
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    setup();
    if (size == 0) return 0;
    if (size > (1u << 20)) return 0; /* (G) one maximum record: libFuzzer's inputs are smaller */
    if (data[0] & 1u) script(data + 1, size - 1);
    else raw(data + 1, size - 1);
    return 0;
}
