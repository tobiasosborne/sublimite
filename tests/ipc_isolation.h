/* Private IPC endpoints inherited by forked/exec'd clients. No global state.
 * The runtime directory is a per-test mkdtemp under build/ (never the real
 * XDG dirs, never a v1 socket), published through XDG_RUNTIME_DIR, which the
 * product reads. Abstract endpoints are isolated by the link seam in
 * tests/ipc_seam.h, with this directory's basename as the namespace. */
#ifndef TEST_IPC_ISOLATION_H
#define TEST_IPC_ISOLATION_H
#include <dirent.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

typedef struct test_ipc_isolation {
    char runtime[4096]; /* (P) Linux PATH_MAX including the NUL */
    pid_t owner;
} test_ipc_isolation;

static void test_ipc_remove_contents(DIR *dir)
{
    struct dirent *entry;
    while ((entry = readdir(dir))) {
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;
        struct stat info;
        if (fstatat(dirfd(dir), entry->d_name, &info, AT_SYMLINK_NOFOLLOW)) continue;
        if (S_ISDIR(info.st_mode)) {
            int fd = openat(dirfd(dir), entry->d_name, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
            if (fd >= 0) {
                DIR *child = fdopendir(fd);
                if (child) { test_ipc_remove_contents(child); (void)closedir(child); }
                else (void)close(fd);
            }
            (void)unlinkat(dirfd(dir), entry->d_name, AT_REMOVEDIR);
        } else (void)unlinkat(dirfd(dir), entry->d_name, 0);
    }
}
static void test_ipc_isolation_cleanup(int status, void *context)
{
    (void)status;
    test_ipc_isolation *isolation = context;
    /* exit() in a forked client must not remove the parent's endpoint. */
    if (isolation->owner == getpid()) {
        DIR *dir = opendir(isolation->runtime);
        if (dir) { test_ipc_remove_contents(dir); (void)closedir(dir); }
        (void)rmdir(isolation->runtime);
    }
    free(isolation);
}

static const char *test_ipc_isolate(void)
{
    test_ipc_isolation *isolation = calloc(1, sizeof *isolation);
    if (!isolation) return NULL;
    char cwd[4096];
    if (!getcwd(cwd, sizeof cwd)) { free(isolation); return NULL; }
    int n = snprintf(isolation->runtime, sizeof isolation->runtime,
                     "%s/build/ipc-test-XXXXXX", cwd);
    if (n < 0 || (size_t)n >= sizeof isolation->runtime) { free(isolation); return NULL; }
    isolation->owner = getpid();
    if (!mkdtemp(isolation->runtime)) { free(isolation); return NULL; }
    if (chmod(isolation->runtime, 0700) ||
        setenv("XDG_RUNTIME_DIR", isolation->runtime, 1) ||
        on_exit(test_ipc_isolation_cleanup, isolation)) {
        test_ipc_isolation_cleanup(1, isolation); return NULL;
    }
    return isolation->runtime;
}
#endif
