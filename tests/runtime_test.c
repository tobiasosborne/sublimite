#include "editor/editor.h"
#include "editor/runtime.h"
#include "journal/journal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define T(c) do { if (!(c)) { fprintf(stderr, "runtime_test:%d: FAIL %s\n", __LINE__, #c); return 1; } } while (0)
int main(void)
{
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
