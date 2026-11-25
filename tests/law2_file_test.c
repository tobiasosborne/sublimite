/* Law 2 for L2 file (bead L2-1): the release guard is live and reports
 * zero allocations over snapshot take/retain, the undo backing
 * validator, and foreground queries. Fault-spec acceptance:
 * "The Law 2 release guard must be active with zero allocations over
 * snapshot take/retain, token retain/poll/drop and foreground queries." */
#include "base/base.h"
#include "file/file.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

static _Alignas(64) work_pool pool;
static file_msg last;
static void on_msg(const work_msg *m, void *ud)
{
    (void)ud;
    file_msg out;
    if (file_msg_decode(m, &out) == 0) last = out;
}

int main(void)
{
    ASSERT(mkdir("build", 0700) == 0 || errno == EEXIST);
    ASSERT(mkdir("build/law2-file", 0700) == 0 || errno == EEXIST);
    const char *path = "build/law2-file/f.txt";
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    ASSERT(fd >= 0 && write(fd, "guarded text", 12) == 12 && close(fd) == 0);

    ASSERT(work_pool_init(&pool, 1, 0) == 0);
    piece_reserve *reserve;
    ASSERT(piece_reserve_create(&reserve) == PIECE_OK);
    static file f;
    file_open_opts opts = { .copy_threshold = 1, .generation = 1 };
    ASSERT(file_open_begin(&f, &pool, path, &opts) == FILE_OK);
    uint64_t deadline = os_now_ns() + UINT64_C(10000000000);
    while (!file_open_ready(&f)) {
        work_mailbox_drain(&pool, on_msg, NULL);
        ASSERT(os_now_ns() < deadline);
        os_sleep_ns(UINT64_C(1000000));
    }
    piece_tree *t;
    ASSERT(file_open_tree(&f, reserve, &t) == FILE_OK);
    /* Warm the snapshot cache outside the window. */
    piece_snapshot *warm;
    ASSERT(piece_snapshot_take(t, &warm) == PIECE_OK);

    ASSERT(base_malloc_guard_active());
    base_malloc_guard_begin();
    piece_snapshot *s;
    ASSERT(piece_snapshot_take(t, &s) == PIECE_OK);
    piece_snapshot_retain(s);
    const piece_backing *b = piece_view_backing(piece_snapshot_view(s));
    ASSERT(b != NULL);
    ASSERT(file_backing_validate(&f, b));
    ASSERT(!file_changed(&f));
    ASSERT(!file_save_busy(&f));
    ASSERT(file_save_phase_of(&f) == FILE_SAVE_IDLE);
    size_t plen;
    ASSERT(file_prefix(&f, &plen) != NULL && plen == 12);
    piece_snapshot_release(s); /* the retain */
    piece_snapshot_release(s); /* the take (a cache hit on warm) */
    size_t allocated = base_malloc_guard_end();
    ASSERT(base_malloc_guard_active());
    ASSERT(allocated == 0);

    piece_snapshot_release(warm);
    piece_close(t);
    file_close_begin(&f);
    while (!file_close_ready(&f)) {
        work_mailbox_drain(&pool, on_msg, NULL);
        os_sleep_ns(UINT64_C(1000000));
    }
    file_close_finish(&f);
    piece_reserve_destroy(reserve);
    work_pool_shutdown(&pool);
    puts("law2_file_test: ok");
    return 0;
}
