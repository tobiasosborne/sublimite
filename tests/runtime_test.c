#include "editor/editor.h"
#include "editor/runtime.h"
#include "journal/journal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>

#define T(c) do { if (!(c)) { fprintf(stderr, "runtime_test:%d: FAIL %s\n", __LINE__, #c); return 1; } } while (0)
typedef struct directory_oracle { const char *root; unsigned barriers; bool fail; } directory_oracle;
static int parent_barrier(void *ctx, int fd)
{
    directory_oracle *o = ctx; struct stat sb, root;
    if (fstat(fd, &sb) || stat(o->root, &root)) return -1;
    if (sb.st_dev == root.st_dev && sb.st_ino == root.st_ino) o->barriers |= 1u;
    char child[4097]; (void)snprintf(child, sizeof child, "%s/a", o->root);
    if (!stat(child, &root) && sb.st_dev == root.st_dev && sb.st_ino == root.st_ino) o->barriers |= 2u;
    if (o->fail) { errno = EIO; return -1; }
    return fsync(fd);
}
static int directory_test(void)
{
    char root[] = "/tmp/editor-dir-XXXXXX", path[4097]; T(mkdtemp(root) != NULL);
    directory_oracle oracle = {.root = root};
    T(snprintf(path, sizeof path, "%s/a/b", root) > 0);
    T(editor_make_directory(path, parent_barrier, &oracle) == 0); T(oracle.barriers == 3u);
    /* A failed namespace barrier must prevent journal startup acknowledgement. */
    oracle.fail = true; T(snprintf(path, sizeof path, "%s/c/d", root) > 0);
    T(editor_make_directory(path, parent_barrier, &oracle) == EDITOR_ERR_IO);
    struct stat sb; T(stat(path, &sb) < 0 && errno == ENOENT);
    T(snprintf(path, sizeof path, "%s/a/b", root) > 0); T(rmdir(path) == 0);
    T(snprintf(path, sizeof path, "%s/a", root) > 0); T(rmdir(path) == 0); T(rmdir(root) == 0);
    puts("P1.9-2.5: every created directory parent barrier observed; failure stops startup passed"); return 0;
}
int main(void)
{
    T(directory_test() == 0);
    const char *real[] = {NULL, "", ":0", ":0.0", "unix:0", "localhost:0.1", ":00", "bad"};
    for (size_t i = 0; i < sizeof real / sizeof real[0]; i++) {
        T(strcmp(editor_runtime_display(real[i], ":99", NULL), ":99") == 0);
        T(strcmp(editor_runtime_display(real[i], ":0", NULL), ":99") == 0);
        T(editor_runtime_display(real[i], ":99", "1") == real[i]);
        T(editor_runtime_display(real[i], ":99", "") == real[i]);
    }
    T(strcmp(editor_runtime_display(":98.1", ":99", NULL), ":98.1") == 0);
    T(strcmp(editor_runtime_display(":0", "", NULL), ":99") == 0);
    T(strcmp(editor_runtime_display(":0", NULL, NULL), ":99") == 0);
    puts("runtime_test: pure display decision honours explicit allow (no environment opt-in)");
    char root[] = "/tmp/sublimite-paths-XXXXXX", path[4097], want[4097];
    T(mkdtemp(root) != NULL);
    T(setenv("HOME", root, 1) == 0);
    T(setenv("XDG_DATA_HOME", root, 1) == 0);
    T(setenv("XDG_CONFIG_HOME", root, 1) == 0);
    T(snprintf(want, sizeof want, "%s/sublimite", root) > 0);
    T(journal_default_dir(path, sizeof path) == JOURNAL_OK && strcmp(path, want) == 0);
    T(editor_config_dir(path, sizeof path) == EDITOR_OK && strcmp(path, want) == 0);
    T(journal_default_dir(path, 2) == JOURNAL_INVALID);
    T(editor_config_dir(path, 2) == EDITOR_ERR_ARG);
    T(unsetenv("XDG_DATA_HOME") == 0 && unsetenv("XDG_CONFIG_HOME") == 0);
    T(snprintf(want, sizeof want, "%s/.local/share/sublimite", root) > 0);
    T(journal_default_dir(path, sizeof path) == JOURNAL_OK && strcmp(path, want) == 0);
    T(snprintf(want, sizeof want, "%s/.config/sublimite", root) > 0);
    T(editor_config_dir(path, sizeof path) == EDITOR_OK && strcmp(path, want) == 0);
    T(setenv("XDG_DATA_HOME", "relative", 1) == 0 && setenv("XDG_CONFIG_HOME", "", 1) == 0);
    T(journal_default_dir(path, sizeof path) == JOURNAL_OK);
    T(editor_config_dir(path, sizeof path) == EDITOR_OK && strcmp(path, want) == 0);
    T(unsetenv("HOME") == 0);
    T(journal_default_dir(path, sizeof path) == JOURNAL_INVALID);
    T(editor_config_dir(path, sizeof path) == EDITOR_ERR_ARG);
    T(rmdir(root) == 0);
    puts("runtime_test: XDG journal/config and HOME fallback paths passed");
    return 0;
}
