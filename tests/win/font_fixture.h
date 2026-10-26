#ifndef EDIT_TEST_FONT_FIXTURE_H
#define EDIT_TEST_FONT_FIXTURE_H
#include <windows.h>
#include <stdio.h>
/* winrun starts beside the executable: build/win/{rel,san}/tests. */
static int font_test_root(void)
{
    (void)setvbuf(stdout, NULL, _IONBF, 0);
    if (!SetCurrentDirectoryW(L"..\\..\\..\\..")) {
        fprintf(stderr, "font fixture: cannot enter worktree root\n"); return 0;
    }
    return 1;
}
#endif
