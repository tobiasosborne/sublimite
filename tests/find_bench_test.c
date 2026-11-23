#include "os/os.h"

#include <errno.h>
#include <spawn.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;

int main(void)
{
    char path[OS_PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", path, sizeof path);
    if (len <= 0 || (size_t)len >= sizeof path) return 1;
    path[len] = '\0';
    char *leaf = strrchr(path, '/');
    if (!leaf) return 1;
    /* The benchmark is in the same configuration's sibling directory. */
    int n = snprintf(leaf, sizeof path - (size_t)(leaf - path), "/../bench/find_bench");
    if (n < 0 || (size_t)n >= sizeof path - (size_t)(leaf - path)) return 1;
    char *args[] = {path, "--self-check", NULL};
    pid_t child;
    int code = posix_spawn(&child, path, NULL, NULL, args, environ);
    if (code != 0) {
        fprintf(stderr, "find_bench_test: cannot start benchmark: %s\n", strerror(code));
        return 1;
    }
    int status;
    pid_t done;
    do { done = waitpid(child, &status, 0); } while (done < 0 && errno == EINTR);
    if (done != child || !WIFEXITED(status) || WEXITSTATUS(status) != 0) return 1;
    puts("find_bench_test: executable checked all generated rows");
    return 0;
}
