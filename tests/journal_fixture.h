/* Shared fixture for the journal runtime suites (L2-2b): a disposable
 * directory under the checkout's build/ tree, a work pool, the pump loop
 * a UI would run, and readers for recovered text. Header-only; every
 * helper fails the process through jf_fail so a suite can never pass on
 * a broken fixture. Paths come from the working directory make runs the
 * suites in (the checkout root), never from the environment. */
#ifndef TESTS_JOURNAL_FIXTURE_H
#define TESTS_JOURNAL_FIXTURE_H

#include "journal/journal.h"
#include "os/os.h"
#include "work/work.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int jf_failures;
#define JF_CHECK(c, ...) do { if (!(c)) { jf_failures++; \
    fprintf(stderr, "%s:%d: FAIL %s: ", __FILE__, __LINE__, #c); \
    fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); } } while (0)
#define JF_REQUIRE(c) do { if (!(c)) { \
    fprintf(stderr, "%s:%d: REQUIRE failed: %s\n", __FILE__, __LINE__, #c); \
    exit(2); } } while (0)

typedef struct jf_dir {
    char path[512];
    os_dir dir;
} jf_dir;

/* build/journal-tests/<suite>-<pid>-<tag>, created fresh. */
static inline void jf_dir_make(jf_dir *d, const char *suite, const char *tag)
{
    (void)os_mkdir("build");
    (void)os_mkdir("build/journal-tests");
    int n = snprintf(d->path, sizeof d->path, "build/journal-tests/%s-%ld-%s",
                     suite, (long)getpid(), tag);
    JF_REQUIRE(n > 0 && (size_t)n < sizeof d->path);
    JF_REQUIRE(os_mkdir(d->path) == OS_OK);
    d->dir = (os_dir)OS_DIR_INIT;
    JF_REQUIRE(os_dir_open(&d->dir, d->path) == OS_OK);
}

/* Removes every entry, then the directory (a suite's own fixtures only). */
static inline void jf_dir_remove(jf_dir *d)
{
    os_dir_iter it;
    JF_REQUIRE(os_dir_iter_open(&it, &d->dir) == OS_OK);
    const char *name;
    char names[64][256];
    size_t n = 0;
    while (os_dir_next(&it, &name) == OS_OK && name) {
        JF_REQUIRE(n < 64 && strlen(name) < 256);
        strcpy(names[n++], name);
    }
    os_dir_iter_close(&it);
    for (size_t i = 0; i < n; i++) JF_REQUIRE(os_unlink_at(&d->dir, names[i]) == OS_OK);
    os_dir_close(&d->dir);
    JF_REQUIRE(os_rmdir(d->path) == OS_OK);
}

static inline size_t jf_dir_count(jf_dir *d)
{
    os_dir_iter it;
    JF_REQUIRE(os_dir_iter_open(&it, &d->dir) == OS_OK);
    const char *name;
    size_t n = 0;
    while (os_dir_next(&it, &name) == OS_OK && name) n++;
    os_dir_iter_close(&it);
    return n;
}

static inline void jf_ignore(const work_msg *m, void *ud) { (void)m; (void)ud; }

/* One iteration of the UI loop's journal duty: drain the pool (the
 * journal's bound completion arrives here), then pump. */
static inline journal_err jf_tick(journal *j, work_pool *pool)
{
    (void)work_mailbox_drain(pool, jf_ignore, NULL);
    return journal_pump(j, os_now_ns());
}

/* Pumps until no job runs. A job that never completes hangs the suite:
 * that is a failure, never a pass. */
static inline void jf_settle(journal *j, work_pool *pool)
{
    for (;;) {
        (void)jf_tick(j, pool);
        if (journal_get_status(j).transport == JOURNAL_TRANSPORT_IDLE) return;
        os_sleep_ns(100000);
    }
}

/* Requests a barrier and pumps until everything accepted is durable. */
static inline void jf_durable(journal *j, work_pool *pool)
{
    journal_request_sync(j);
    for (;;) {
        (void)jf_tick(j, pool);
        journal_status s = journal_get_status(j);
        if (s.transport == JOURNAL_TRANSPORT_IDLE &&
            s.durable_sequence == s.accepted_sequence &&
            s.suspension == JOURNAL_HEALTHY) return;
        if (s.suspension == JOURNAL_WRITEBACK_LOST ||
            s.suspension == JOURNAL_NEEDS_ROTATION) return;
        os_sleep_ns(100000);
    }
}

static inline void jf_close(journal *j, work_pool *pool)
{
    for (;;) {
        (void)work_mailbox_drain(pool, jf_ignore, NULL);
        (void)journal_pump(j, os_now_ns());
        if (journal_close(j) == JOURNAL_OK) return;
        os_sleep_ns(100000);
    }
}

static inline journal_options jf_options(void)
{
    /* Fixture policy: a budget no case reaches unless it means to, and
     * barriers only on request, so each case decides when to sync. */
    journal_options o = {.budget_bytes = UINT64_C(1) << 40,
                         .sync_bytes = UINT64_C(1) << 40,
                         .sync_interval_ns = UINT64_MAX};
    return o;
}

/* Copies a view's text into out (cap bytes); returns its length. */
static inline size_t jf_text(const piece_view *v, uint8_t *out, size_t cap)
{
    uint64_t len = piece_length(v);
    JF_REQUIRE(len <= cap);
    piece_iter it;
    piece_iter_init(&it, v, 0);
    size_t at = 0;
    const uint8_t *p;
    size_t n;
    while ((n = piece_iter_next(&it, &p)) != 0) {
        memcpy(out + at, p, n);
        at += n;
    }
    JF_REQUIRE(at == len);
    return at;
}

static inline bool jf_tree_is(piece_tree *t, const void *want, size_t n)
{
    static uint8_t buf[1u << 22];
    if (!t) return false;
    size_t got = jf_text(piece_tree_view(t), buf, sizeof buf);
    return got == n && (n == 0 || memcmp(buf, want, n) == 0);
}

static inline const journal_recovered_buffer *jf_buffer(const journal_recovery *r,
                                                        uint64_t id)
{
    for (size_t i = 0; i < r->buffer_count; i++)
        if (r->buffers[i].id == id) return &r->buffers[i];
    return NULL;
}

/* Whole-file helpers through os only. */
static inline size_t jf_read_file(const jf_dir *d, const char *name, uint8_t *out, size_t cap)
{
    os_file f = OS_FILE_INIT;
    JF_REQUIRE(os_file_open_at(&f, &d->dir, name, OS_OPEN_READ) == OS_OK);
    size_t at = 0;
    for (;;) {
        size_t got = 0;
        JF_REQUIRE(at < cap);
        JF_REQUIRE(os_file_pread(&f, out + at, cap - at, at, &got) == OS_OK);
        if (got == 0) break;
        at += got;
    }
    os_file_close(&f);
    return at;
}

static inline void jf_write_file(const jf_dir *d, const char *name, const void *bytes, size_t n)
{
    os_file f = OS_FILE_INIT;
    JF_REQUIRE(os_file_open_at(&f, &d->dir, name,
                               OS_OPEN_WRITE | OS_OPEN_CREATE | OS_OPEN_TRUNC) == OS_OK);
    size_t done = 0;
    JF_REQUIRE(os_file_pwrite(&f, bytes, n, 0, &done) == OS_OK && done == n);
    os_file_close(&f);
}

static inline void jf_truncate(const jf_dir *d, const char *name, uint64_t size)
{
    os_file f = OS_FILE_INIT;
    JF_REQUIRE(os_file_open_at(&f, &d->dir, name, OS_OPEN_RDWR) == OS_OK);
    JF_REQUIRE(os_file_set_size(&f, size) == OS_OK);
    os_file_close(&f);
}

/* Path of a name inside the fixture directory, for absolute GENERATION
 * locations: canonical (symlinks resolved) as the wire requires. */
static inline void jf_abs_path(const jf_dir *d, const char *name, char *out, size_t cap)
{
    char dir[OS_PATH_MAX];
    JF_REQUIRE(os_path_canonical(d->path, dir, sizeof dir) == OS_OK);
    int n = snprintf(out, cap, "%s/%s", dir, name);
    JF_REQUIRE(n > 0 && (size_t)n < cap);
}

/* Declares a BASE generation for the file `name` (which must exist) and
 * appends GENERATION then BASE for buffer id. */
static inline uint64_t jf_declare_base(journal *j, const jf_dir *d, const char *name,
                                       uint64_t buffer_id, char *path, size_t cap)
{
    jf_abs_path(d, name, path, cap);
    os_file f = OS_FILE_INIT;
    JF_REQUIRE(os_file_open(&f, path, OS_OPEN_READ) == OS_OK);
    journal_generation g = {0};
    JF_REQUIRE(journal_capture(&f, &g) == OS_OK);
    os_file_close(&f);
    JF_REQUIRE(journal_new_generation_key(j, &g.key) == JOURNAL_OK);
    g.location = (const uint8_t *)path;
    g.location_len = strlen(path);
    uint8_t payload[JOURNAL_PATH_MAX_BYTES + 128];
    size_t n = 0;
    JF_REQUIRE(journal_generation_encode(payload, sizeof payload, &n, &g) == JOURNAL_CODEC_OK);
    JF_REQUIRE(journal_append(j, JOURNAL_REC_GENERATION, 0, payload, n) == JOURNAL_OK);
    journal_base b = {.generation_key = g.key, .content_id = 1};
    JF_REQUIRE(journal_base_encode(payload, sizeof payload, &n, &b) == JOURNAL_CODEC_OK);
    JF_REQUIRE(journal_append(j, JOURNAL_REC_BASE, buffer_id, payload, n) == JOURNAL_OK);
    return g.key;
}

static inline void jf_base_empty(journal *j, uint64_t buffer_id)
{
    uint8_t payload[16];
    size_t n = 0;
    journal_base b = {.generation_key = 0, .content_id = 1};
    JF_REQUIRE(journal_base_encode(payload, sizeof payload, &n, &b) == JOURNAL_CODEC_OK);
    JF_REQUIRE(journal_append(j, JOURNAL_REC_BASE, buffer_id, payload, n) == JOURNAL_OK);
}

static inline journal_err jf_insert(journal *j, uint64_t id, uint64_t dest, const char *s)
{
    size_t progress = 0;
    journal_err e;
    do {
        e = journal_insert_step(j, id, dest, (const uint8_t *)s, strlen(s), &progress);
    } while (e == JOURNAL_MORE);
    return e;
}

#endif
