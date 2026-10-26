/* File-private acquisition interruption and metadata-entry race regressions. */
#if !defined(_WIN32)
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "file/file_internal.h"
#include "trace/trace.h"
#include "../src/file/file.c"

static int failures, attempts, armed, substitute;
static work_slot *cancel_slot;
static char path[OS_PATH_MAX], away[OS_PATH_MAX];
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "file_acquire_test:%d: FAIL %s\n", __LINE__, #c); failures++; } } while (0)
ssize_t __real_pread(int, void *, size_t, off_t);
ssize_t __wrap_pread(int fd, void *buf, size_t n, off_t off)
{
    if (armed) {
        attempts++;
        atomic_store(&cancel_slot->epoch, 2);
        /* Finite sentinel: old hidden retry must fail the attempt-count
         * assertion rather than hang the test indefinitely. */
        if (attempts <= 3) { errno = EINTR; return -1; }
    }
    return __real_pread(fd, buf, n, off);
}
os_err __real_os_file_open_at(os_file *, const os_dir *, const char *, unsigned);
os_err __wrap_os_file_open_at(os_file *out, const os_dir *dir, const char *name, unsigned flags)
{
    if (substitute && !strcmp(name, "source")) {
        substitute = 0;
        CHECK(os_rename(path, away, OS_RENAME_NOREPLACE) == OS_OK);
        CHECK(os_mkdir(path) == OS_OK);
    }
    return __real_os_file_open_at(out, dir, name, flags);
}
int main(int argc, char **argv)
{
    alarm(120); /* (E) existing OS conformance hang guard */
    char root[] = "/tmp/edit-file-acquire-XXXXXX"; CHECK(mkdtemp(root));
    CHECK(snprintf(path, sizeof path, "%s/source", root) > 0);
    CHECK(snprintf(away, sizeof away, "%s/old", root) > 0);
    os_file fd = OS_FILE_INIT; CHECK(os_file_open(&fd, path, OS_OPEN_RDWR | OS_OPEN_CREATE) == OS_OK);
    CHECK(os_file_set_size(&fd, 2u * FILE_PREFIX_MAX) == OS_OK);
    os_file_close(&fd);
    trace_init(); static work_pool workers; CHECK(work_pool_init(&workers, 1, 0) == 0);
    work_slot *slot = &workers.slots[0]; cancel_slot = slot;
    if (argc == 1 || !strcmp(argv[1], "reads")) {
        for (int suffix = 0; suffix < 2; suffix++) {
            file f = {0}; f.pool = &workers; f.threshold = UINT64_MAX;
            CHECK(snprintf(f.requested_path, sizeof f.requested_path, "%s", path) > 0);
            CHECK(snprintf(f.path, sizeof f.path, "%s", path) > 0);
            f.fd = (os_file)OS_FILE_INIT;
            if (suffix) {
                CHECK(os_file_open(&f.fd, path, OS_OPEN_READ) == OS_OK);
                os_stat st; CHECK(os_file_stat(&f.fd, &st) == OS_OK); file_id_from_stat(&f.open_identity, &st);
                f.size = st.size; f.prefix_len = FILE_PREFIX_MAX; f.prefix = malloc((size_t)f.size); f.mode = FILE_MODE_COPY;
            }
            atomic_store(&slot->epoch, 1); attempts = 0; armed = 1;
            work_ctx context = {.pool = &workers, .slot = slot, .epoch = 1, .arg = &f};
            if (suffix) open_job(&context); else prefix_job(&context);
            armed = 0;
            printf("file_acquire_test: %s attempts=%d status=%d\n", suffix ? "COPY suffix" : "prefix", attempts,
                   suffix ? f.open_result.status : f.prefix_result.status);
            CHECK(attempts == 1);
            CHECK((suffix ? f.open_result.status : f.prefix_result.status) == FILE_ERR_CANCELLED);
            os_file_close(&f.fd); free(f.prefix);
        }
    }
    if (argc == 1 || !strcmp(argv[1], "substitute")) {
        piece_allocator a = piece_default_allocator(); piece_tree *t = piece_create(&a);
        CHECK(piece_init_copy(t, (const uint8_t *)"new", 3) == PIECE_OK); piece_snapshot *snap = piece_snapshot_take(t);
        file_save_args args = {.path = path, .snap = snap}; substitute = 1;
        int result = file_save_write(&args);
        printf("file_acquire_test: directory substitution status=%d\n", result); CHECK(result == FILE_ERR_CHANGED);
        CHECK(os_rmdir(path) == OS_OK && os_rename(away, path, OS_RENAME_NOREPLACE) == OS_OK);
        piece_snapshot_release(snap); piece_destroy(t);
    }
    work_pool_shutdown(&workers); CHECK(os_unlink(path) == OS_OK && os_rmdir(root) == OS_OK);
    printf("file_acquire_test: %s\n", failures ? "FAILED" : "ok"); return failures ? 1 : 0;
}
#else
#include <stdio.h>
int main(void) { puts("file_acquire_test: SKIP POSIX EINTR syscall injection; Windows completed I/O does not return EINTR"); return 0; }
#endif
