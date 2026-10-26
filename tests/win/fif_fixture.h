#ifndef EDIT_FIF_WIN_FIXTURE_H
#define EDIT_FIF_WIN_FIXTURE_H
#include "dependency_fixture.h"
#undef PATH_MAX
#define PATH_MAX OS_PATH_MAX
#define sched_yield os_yield
typedef struct tree_fixture { char root[OS_PATH_MAX]; } tree_fixture;
static inline int tree_fixture_path(const tree_fixture *t, const char *name, char *out)
{
    int n = snprintf(out, OS_PATH_MAX, "%s/%s", t->root, name);
    return n < 0 || (size_t)n >= OS_PATH_MAX ? -1 : 0;
}
static inline int tree_fixture_dir(const tree_fixture *t, const char *name)
{ char path[OS_PATH_MAX]; return tree_fixture_path(t, name, path) || os_mkdir(path) ? -1 : 0; }
static inline int tree_fixture_file(const tree_fixture *t, const char *name, const void *bytes, size_t n)
{ char path[OS_PATH_MAX]; return tree_fixture_path(t, name, path) ? -1 : dep_put(path, bytes, n); }
static inline int tree_fixture_remove(const char *path) { return dep_remove(path); }
static inline int tree_fixture_init(tree_fixture *t)
{
    if (dep_temp(t->root, sizeof t->root)) return -1;
    const char a[] = "Alpha cat\ncatapult CAT cat\nlast cat", b[] = "cat\nscatter\ncat!\n";
    const unsigned char binary[] = {'c', 'a', 't', 0, 'c', 'a', 't'};
    char loop[OS_PATH_MAX];
    if (tree_fixture_dir(t, "nested") || tree_fixture_dir(t, ".git") ||
        tree_fixture_file(t, "a.txt", a, sizeof a - 1u) ||
        tree_fixture_file(t, "nested/b.c", b, sizeof b - 1u) ||
        tree_fixture_file(t, "nested/skip.txt", "cat", sizeof "cat" - 1u) ||
        tree_fixture_file(t, "binary", binary, sizeof binary) ||
        tree_fixture_file(t, ".git/hidden", "cat", sizeof "cat" - 1u) ||
        tree_fixture_path(t, "nested/loop", loop) || dep_junction(t->root, loop)) {
        (void)dep_remove(t->root); return -1;
    }
    return 0;
}
#endif
