/* (E) Fixture paths use the platform's PATH_MAX, including the terminator.
 * The nested tree is a minimal witness for each requested walker behaviour. */
#ifndef EDIT_TREE_FIXTURE_H
#define EDIT_TREE_FIXTURE_H
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

typedef struct tree_fixture { char root[PATH_MAX]; } tree_fixture;
static inline int tree_fixture_path(const tree_fixture *tree, const char *name, char *out)
{
    int n = snprintf(out, PATH_MAX, "%s/%s", tree->root, name);
    return n < 0 || n >= PATH_MAX ? -1 : 0;
}
static inline int tree_fixture_dir(const tree_fixture *tree, const char *name)
{
    char path[PATH_MAX];
    return tree_fixture_path(tree, name, path) ? -1 : mkdir(path, S_IRWXU);
}
static inline int tree_fixture_file(const tree_fixture *tree, const char *name,
                                    const void *bytes, size_t length)
{
    char path[PATH_MAX];
    if (tree_fixture_path(tree, name, path)) return -1;
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
    if (fd < 0) return -1;
    size_t at = 0;
    while (at < length) {
        ssize_t n = write(fd, (const unsigned char *)bytes + at, length - at);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) { (void)close(fd); return -1; }
        at += (size_t)n;
    }
    return close(fd);
}
static inline int tree_fixture_remove(const char *path)
{
    struct stat st;
    if (lstat(path, &st)) return -1;
    if (!S_ISDIR(st.st_mode)) return unlink(path);
    DIR *dir = opendir(path);
    if (!dir) return -1;
    int result = 0;
    struct dirent *entry;
    while ((entry = readdir(dir))) {
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;
        char child[PATH_MAX];
        int n = snprintf(child, sizeof child, "%s/%s", path, entry->d_name);
        if (n < 0 || (size_t)n >= sizeof child || tree_fixture_remove(child)) result = -1;
    }
    if (closedir(dir) || rmdir(path)) result = -1;
    return result;
}
static inline int tree_fixture_init(tree_fixture *tree)
{
    strcpy(tree->root, "/tmp/fif-tree-XXXXXX"); /* (P) mkdtemp requires six Xs. */
    if (!mkdtemp(tree->root)) return -1;
    const char a[] = "Alpha cat\ncatapult CAT cat\nlast cat";
    const char b[] = "cat\nscatter\ncat!\n";
    const unsigned char binary[] = {'c', 'a', 't', 0, 'c', 'a', 't'};
    char loop[PATH_MAX];
    if (tree_fixture_dir(tree, "nested") || tree_fixture_dir(tree, ".git") ||
        tree_fixture_file(tree, "a.txt", a, sizeof a - 1u) ||
        tree_fixture_file(tree, "nested/b.c", b, sizeof b - 1u) ||
        tree_fixture_file(tree, "nested/skip.txt", "cat", sizeof "cat" - 1u) ||
        tree_fixture_file(tree, "binary", binary, sizeof binary) ||
        tree_fixture_file(tree, ".git/hidden", "cat", sizeof "cat" - 1u) ||
        tree_fixture_path(tree, "nested/loop", loop) || symlink("..", loop)) {
        (void)tree_fixture_remove(tree->root); return -1;
    }
    return 0;
}
#endif
