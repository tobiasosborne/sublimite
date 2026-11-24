/* Law 2 for the journal typing path (L2-2b): every append a command can
 * make -- single-byte INSERTs, a multi-slice insert_step, DELETE,
 * RESTORE_BASE_RANGE, group markers -- under the live release malloc
 * guard. The lane fails closed unless the guard is active and counted
 * zero. Setup (pool, open) and teardown run outside the window. */
#include "base/base.h"
#include "journal/journal.h"
#include "os/os.h"
#include "work/work.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static work_pool pool __attribute__((aligned(64)));
static uint8_t big[2 * JOURNAL_INSERT_SLICE_BYTES + 17]; /* (P) three slices */

static void ignore(const work_msg *m, void *ud) { (void)m; (void)ud; }

int main(void)
{
    char dir_path[256];
    (void)os_mkdir("build");
    (void)os_mkdir("build/journal-tests");
    int n = snprintf(dir_path, sizeof dir_path, "build/journal-tests/law2-%ld", (long)getpid());
    ASSERT(n > 0 && (size_t)n < sizeof dir_path);
    ASSERT(os_mkdir(dir_path) == OS_OK);
    os_dir dir = OS_DIR_INIT;
    ASSERT(os_dir_open(&dir, dir_path) == OS_OK);
    ASSERT(work_pool_init(&pool, 1, 0) == 0);
    journal *j;
    journal_options o = {.budget_bytes = UINT64_C(1) << 40, .sync_bytes = UINT64_C(1) << 40,
                         .sync_interval_ns = UINT64_MAX};
    ASSERT(journal_open(&j, &dir, "law2.sjnl", &pool, NULL, &o) == JOURNAL_OK);
    uint8_t base[16];
    size_t bn = 0;
    journal_base b = {.generation_key = 0, .content_id = 1};
    ASSERT(journal_base_encode(base, sizeof base, &bn, &b) == JOURNAL_CODEC_OK);
    ASSERT(journal_append(j, JOURNAL_REC_BASE, 1, base, bn) == JOURNAL_OK);
    memset(big, 'p', sizeof big);

    base_malloc_guard_begin();
    for (unsigned i = 0; i < 1000; i++) { /* (G) a typed paragraph */
        uint8_t ch = (uint8_t)('a' + i % 26);
        size_t progress = 0;
        ASSERT(journal_insert_step(j, 1, i, &ch, 1, &progress) == JOURNAL_OK);
    }
    ASSERT(journal_append_delete(j, 1, 10, 5) == JOURNAL_OK);
    journal_restore r = {.destination = 0, .generation_key = 1, .source_offset = 0, .len = 3};
    ASSERT(journal_append_restore(j, 1, &r) == JOURNAL_OK);
    ASSERT(journal_group_begin(j) == JOURNAL_OK);
    size_t progress = 0;
    journal_err e;
    do {
        e = journal_insert_step(j, 1, 0, big, sizeof big, &progress);
    } while (e == JOURNAL_MORE);
    ASSERT(e == JOURNAL_OK);
    ASSERT(journal_group_commit(j) == JOURNAL_OK);
    size_t calls = base_malloc_guard_end();

    bool active = base_malloc_guard_active();
    printf("law2_journal_test: active=%d count=%zu\n", (int)active, calls);
    for (;;) {
        (void)work_mailbox_drain(&pool, ignore, NULL);
        if (journal_close(j) == JOURNAL_OK) break;
        (void)journal_pump(j, os_now_ns());
        os_sleep_ns(100000);
    }
    work_pool_shutdown(&pool);
    ASSERT(os_unlink_at(&dir, "law2.sjnl") == OS_OK);
    os_dir_close(&dir);
    ASSERT(os_rmdir(dir_path) == OS_OK);
    return active && calls == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
