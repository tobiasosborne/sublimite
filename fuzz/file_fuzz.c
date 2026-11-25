/* L2 file fuzzer (bead L2-1; ported from v1 fuzz/file_fuzz.c, whose ops
 * were open/insert/delete/save/external/lifetime/cancel_save). The
 * input drives a flat byte-array model side by side with the real
 * synchronous save core and the async open pipeline over one file
 * under build/. After every operation the model, the disk bytes (when
 * a save verified), and the classifier's verdict must agree. External
 * mutations come in the three shapes the module must distinguish:
 * in-place overwrite, rename replacement, and truncation. */
#include "file/file.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#define FROOT "build/file-fuzz"
#define FPATH FROOT "/f.txt"
#define FALT FROOT "/f.alt"
enum { MODEL_MAX = 4096 };

static _Alignas(64) work_pool pool;
static piece_reserve *reserve;
static bool ready;

static void drain_cb(const work_msg *m, void *ud)
{
    (void)ud;
    file_msg out;
    (void)file_msg_decode(m, &out);
}

static void write_all(const char *path, const uint8_t *bytes, size_t n)
{
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    ASSERT(fd >= 0);
    ASSERT(write(fd, bytes, n) == (ssize_t)n);
    ASSERT(close(fd) == 0);
}

static size_t read_all(const char *path, uint8_t *buf, size_t cap)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0) return 0;
    ssize_t n = read(fd, buf, cap);
    ASSERT(close(fd) == 0);
    return n > 0 ? (size_t)n : 0;
}

static void check_view(const piece_view *v, const uint8_t *model, size_t len)
{
    ASSERT(piece_length(v) == len);
    piece_iter it;
    piece_iter_init(&it, v, 0);
    const uint8_t *p;
    size_t n, off = 0;
    while ((n = piece_iter_next(&it, &p)) != 0) {
        ASSERT(off + n <= len && memcmp(p, model + off, n) == 0);
        off += n;
    }
    ASSERT(off == len);
}

typedef struct rng { const uint8_t *d; size_t n, at; } rng;
static uint8_t next_byte(rng *r)
{
    return r->at < r->n ? r->d[r->at++] : (uint8_t)(r->at++ * 131u + 7u);
}
static size_t next_below(rng *r, size_t bound)
{
    if (bound == 0) return 0;
    unsigned v = (unsigned)next_byte(r) | ((unsigned)next_byte(r) << 8);
    return v % bound;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (!ready) {
        (void)mkdir("build", 0700);
        (void)mkdir(FROOT, 0700);
        ASSERT(work_pool_init(&pool, 1, 0) == 0);
        ASSERT(piece_reserve_create(&reserve) == PIECE_OK);
        ready = true;
    }
    rng r = { data, size, 0 };
    static uint8_t model[MODEL_MAX], disk[MODEL_MAX];
    size_t mlen = next_below(&r, 64);
    for (size_t i = 0; i < mlen; i++) model[i] = next_byte(&r);
    write_all(FPATH, model, mlen);

    static file f;
    memset(&f, 0, sizeof f);
    file_open_opts opts = { .copy_threshold = next_byte(&r) & 1 ? 1 : 0,
                            .generation = 1 };
    if (file_open_begin(&f, &pool, FPATH, &opts) != FILE_OK) return 0;
    uint64_t deadline = os_now_ns() + UINT64_C(5000000000);
    while (!file_open_ready(&f) && os_now_ns() < deadline) {
        work_mailbox_drain(&pool, drain_cb, NULL);
        os_sleep_ns(UINT64_C(100000));
    }
    if (!file_open_ready(&f)) goto close;
    piece_tree *tree;
    if (file_open_tree(&f, reserve, &tree) != FILE_OK) goto close;
    check_view(piece_tree_view(tree), model, mlen);

    for (int op = 0; op < 24 && r.at < r.n + 8; op++) {
        switch (next_below(&r, 6)) {
        case 0: { /* insert */
            uint8_t buf[32];
            size_t n = 1 + next_below(&r, sizeof buf);
            if (mlen + n > MODEL_MAX) break;
            for (size_t i = 0; i < n; i++) buf[i] = next_byte(&r);
            size_t off = next_below(&r, mlen + 1);
            ASSERT(piece_replace(tree, off, 0, buf, n) == PIECE_OK);
            memmove(model + off + n, model + off, mlen - off);
            memcpy(model + off, buf, n);
            mlen += n;
            break;
        }
        case 1: { /* delete */
            if (mlen == 0) break;
            size_t off = next_below(&r, mlen);
            size_t n = 1 + next_below(&r, mlen - off);
            ASSERT(piece_replace(tree, off, n, NULL, 0) == PIECE_OK);
            memmove(model + off, model + off + n, mlen - off - n);
            mlen -= n;
            break;
        }
        case 2: { /* save through the synchronous core */
            piece_snapshot *snap;
            ASSERT(piece_snapshot_take(tree, &snap) == PIECE_OK);
            file_expectation exp;
            os_err oe;
            ASSERT(file_expectation_observe_path(FPATH, &exp, &oe) == FILE_OK);
            file_witness w;
            file_save_outcome out;
            int rc = file_save_write(FPATH, snap, NULL, &exp, FILE_SAVE_FORCE, &w, &out);
            if (rc == FILE_OK) {
                ASSERT(out.pub == FILE_PUB_VERIFIED);
                ASSERT(read_all(FPATH, disk, MODEL_MAX) == mlen);
                ASSERT(memcmp(disk, model, mlen) == 0);
                ASSERT(w.size == mlen);
                ASSERT(w.content_crc32c == file_crc32c(0, model, mlen));
            } else {
                ASSERT(out.pub == FILE_PUB_PRE_RENAME ||
                       out.pub == FILE_PUB_INSTALLED_UNSYNCED ||
                       out.pub == FILE_PUB_CONFLICT ||
                       out.pub == FILE_PUB_RENAME_UNCERTAIN);
            }
            piece_snapshot_release(snap);
            break;
        }
        case 3: { /* external: in-place, rename, or truncate */
            uint8_t buf[64];
            size_t n = next_below(&r, sizeof buf + 1);
            for (size_t i = 0; i < n; i++) buf[i] = next_byte(&r);
            switch (next_below(&r, 3)) {
            case 0:
                write_all(FPATH, buf, n);
                break;
            case 1:
                write_all(FALT, buf, n);
                ASSERT(rename(FALT, FPATH) == 0);
                break;
            default: {
                int fd = open(FPATH, O_WRONLY);
                if (fd >= 0) {
                    ASSERT(ftruncate(fd, (off_t)next_below(&r, 32)) == 0);
                    ASSERT(close(fd) == 0);
                }
                break;
            }
            }
            (void)file_check(&f, NULL); /* must never crash or adopt zeroes */
            break;
        }
        case 4: { /* lifetime: retain the source, validate, release */
            file_source *src = NULL;
            if (file_source_acquire(&f, &src) == FILE_OK) {
                file_source *again = file_source_retain(src);
                os_err oe;
                (void)file_source_validate(src, &oe);
                file_source_release(again);
                file_source_release(src);
            }
            break;
        }
        default: { /* cancelable save through the async pipeline */
            piece_snapshot *snap;
            ASSERT(piece_snapshot_take(tree, &snap) == PIECE_OK);
            file_expectation exp;
            os_err oe;
            if (file_expectation_observe_path(FPATH, &exp, &oe) == FILE_OK &&
                file_save_begin(&f, snap, FPATH, &exp, FILE_SAVE_FORCE, 2) == FILE_OK) {
                if (next_byte(&r) & 1) file_save_abandon(&f);
                uint64_t dl = os_now_ns() + UINT64_C(5000000000);
                while (file_save_busy(&f) && os_now_ns() < dl) {
                    work_mailbox_drain(&pool, drain_cb, NULL);
                    os_sleep_ns(UINT64_C(100000));
                }
                if (file_save_phase_of(&f) == FILE_SAVE_PREPARED && (next_byte(&r) & 1) &&
                    file_save_publish(&f) == FILE_OK) {
                    if (next_byte(&r) & 1) file_save_abandon(&f);
                    while (file_save_busy(&f) && os_now_ns() < dl) {
                        work_mailbox_drain(&pool, drain_cb, NULL);
                        os_sleep_ns(UINT64_C(100000));
                    }
                }
                /* Release whatever custody is left (PREPARED, UNSYNCED,
                 * HELD) so the next save can begin; a no-op when idle. */
                file_save_abandon(&f);
            }
            piece_snapshot_release(snap);
            break;
        }
        }
        check_view(piece_tree_view(tree), model, mlen);
    }
    piece_close(tree);
close:
    file_close_begin(&f);
    uint64_t dl = os_now_ns() + UINT64_C(5000000000);
    while (!file_close_ready(&f) && os_now_ns() < dl) {
        work_mailbox_drain(&pool, drain_cb, NULL);
        os_sleep_ns(UINT64_C(100000));
    }
    ASSERT(file_close_ready(&f));
    file_close_finish(&f);
    return 0;
}
