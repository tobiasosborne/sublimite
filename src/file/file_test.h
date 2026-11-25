/* Test hooks for L2 file. The declarations are always visible to the
 * suites that include this header; the definitions and every hook call
 * site compile only under FILE_TEST_HOOKS into file_test_hooks.o, which
 * tests/lanes link ahead of libpersist.a (CLAUDE.md, v2-0fv.16). No
 * archive and no shipped binary contains them. */
#ifndef FILE_FILE_TEST_H
#define FILE_FILE_TEST_H

#include "file/file.h"

/* The six save steps (v1 file.h FILE_STEP_*), fired on the thread doing
 * the work: TEMP_CREATED after the temp exists, MID_WRITE once half the
 * bytes are written (also for an empty text, after creation),
 * TEMP_WRITTEN after the last write, FSYNCED after the temp sync,
 * RENAMED after the rename and before the directory sync, DIR_SYNCED
 * after it. A hook may block that thread (the kill-at-step and
 * cancel-while-running suites do) and may stop the process. */
enum {
    FILE_STEP_TEMP_CREATED = 1,
    FILE_STEP_MID_WRITE,
    FILE_STEP_TEMP_WRITTEN,
    FILE_STEP_FSYNCED,
    FILE_STEP_RENAMED,
    FILE_STEP_DIR_SYNCED
};

/* Install the step hook used by this file's save jobs and, when f is
 * NULL, by the synchronous file_temp_* core. */
void file_test_set_step_hook(file *f, void (*step)(void *ctx, int step),
                             void *ctx);

#endif
