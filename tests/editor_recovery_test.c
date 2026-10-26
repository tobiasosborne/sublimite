/* edit-fi0p F7: deterministic BASE rewrites at materialisation's read boundary.
 * (P) Sizes derive from literal bytes and the journal BASE wire schema.
 * Zero/one mean success/failure, empty state, first id and unit counts.
 * Include the implementation to intercept only its bulk read, leaving strict
 * journal BASE validation and file acquisition untouched. */
#include "editor/private.h"
#include "trace/trace.h"
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static struct { const char *path; bool before, armed; } rewrite;
static os_err materialize_read(os_file *source, void *out, size_t size, size_t *got)
{
    if (!rewrite.armed) return os_file_read(source, out, size, got);
    rewrite.armed = false;
    *got = 0;
    /* (P) Split the read in half to force a mixed-generation prefix even
     * on filesystems whose timestamps coalesce adjacent writes. */
    if (!rewrite.before) {
        os_err read_rc = os_file_read(source, out, size / 2u, got);
        if (read_rc != OS_OK) return read_rc;
    }
    os_file writer = OS_FILE_INIT;
    if (os_file_open(&writer, rewrite.path, OS_OPEN_WRITE) != OS_OK) return OS_E_IO;
    const char changed[] = "rewritten";
    size_t wrote;
    os_err written = os_file_pwrite(&writer, changed, sizeof changed - 1u, 0, &wrote);
    os_file_close(&writer);
    if (written != OS_OK || wrote != sizeof changed - 1u) return OS_E_IO;
    return rewrite.before ? os_file_read(source, out, size, got) : OS_OK;
}
#define os_file_read materialize_read
#include "../src/editor/recovery.c"
#undef os_file_read

#define T(c) do { if (!(c)) { fprintf(stderr, "editor_recovery_test:%d FAIL %s\n", __LINE__, #c); rc = 1; goto done; } } while (0)
static int changed_materialization(bool before)
{
    char path[] = "build/editor-fi0p-base-XXXXXX";
    int fd = mkstemp(path), rc = 0;
    recovered_session session = {0};
    T(fd >= 0);
    const char original[] = "original!";
    T(write(fd, original, sizeof original - 1u) == (ssize_t)(sizeof original - 1u));
    T(close(fd) == 0); fd = -1;
    journal_base base;
    T(journal_capture_base(path, &base) == 0);
    /* (P) V2 BASE = size/mtime/inode/device (u64 each), CRC/length (u32
     * each), NUL-terminated path, then ctime (u64). */
    uint8_t payload[sizeof(uint64_t) * 4u + sizeof(uint32_t) * 2u + IPC_PATH_CAP + sizeof(uint64_t)] = {0};
    uint64_t fields[] = {base.size, base.mtime_ns, base.inode, base.device};
    for (size_t i = 0; i < sizeof fields / sizeof *fields; i++)
        for (size_t j = 0; j < sizeof(uint64_t); j++) payload[i * sizeof(uint64_t) + j] = (uint8_t)(fields[i] >> (j * CHAR_BIT));
    uint32_t small[] = {base.prefix_crc, base.prefix_len | JOURNAL_BASE_V2};
    size_t fixed = sizeof fields + sizeof small;
    for (size_t i = 0; i < sizeof small / sizeof *small; i++)
        for (size_t j = 0; j < sizeof(uint32_t); j++) payload[sizeof fields + i * sizeof(uint32_t) + j] = (uint8_t)(small[i] >> (j * CHAR_BIT));
    size_t name = strlen(base.path) + 1u;
    memcpy(payload + fixed, base.path, name);
    for (size_t j = 0; j < sizeof(uint64_t); j++) payload[fixed + name + j] = (uint8_t)(base.ctime_ns >> (j * CHAR_BIT));
    journal_record record = {.type = JOURNAL_BASE, .buffer_id = 1, .data = payload,
        .size = (uint32_t)(fixed + name + sizeof(uint64_t))};
    T(journal_check_base(&base) == 0);
    rewrite.path = path; rewrite.before = before; rewrite.armed = true;
    int restored = restore_record(&session, &record);
    T(!rewrite.armed);
    T(restored == JOURNAL_BASE_CHANGED);
done:
    rewrite.armed = false;
    if (fd >= 0) close(fd);
    fini(&session); unlink(path);
    if (!rc) puts(before ? "editor_recovery_test: rejects BASE rewritten before bulk read" :
        "editor_recovery_test: rejects BASE rewritten during bulk read");
    return rc;
}
int main(void)
{
    int rc = changed_materialization(true);
    if (changed_materialization(false)) rc = 1;
    return rc;
}
