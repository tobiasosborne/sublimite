#ifndef EDIT_TEST_FONT_FIXTURE_H
#define EDIT_TEST_FONT_FIXTURE_H
#include "os/os.h"
#include <windows.h>
#include <stdio.h>
#include <wchar.h>
/* Resolve from build/win/{rel,san}/tests/<test>.exe, independent of the
 * runner's cwd. Repeated calls must enter the same worktree root. */
static int font_test_root(void)
{
    (void)setvbuf(stdout, NULL, _IONBF, 0);
    WCHAR path[OS_PATH_MAX];
    DWORD n = GetModuleFileNameW(NULL, path, OS_PATH_MAX);
    if (!n || n >= OS_PATH_MAX) goto fail;
    /* Remove the executable name, tests, configuration, win, and build. */
    for (unsigned part = 0; part < 5u; part++) {
        WCHAR *end = wcsrchr(path, L'\\');
        if (!end) goto fail;
        *end = 0;
    }
    if (!SetCurrentDirectoryW(path)) goto fail;
    return 1;
fail:
    fprintf(stderr, "font fixture: cannot enter worktree root\n"); return 0;
}
#endif
