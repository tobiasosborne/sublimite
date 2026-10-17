/* Pure policy tests: no connection to any display-zero alias is opened. */
#include "display_guard.h"
#include "editor/runtime.h"
#include <stdio.h>
int main(void) {
    const char *unsafe[] = {":0", ":0.0", ":00", "localhost:00.0", "unix/:0", "hostname/unix:0", "127.0.0.1:00", "[::1]:0"};
    int failed = 0;
    for (size_t i = 0; i < sizeof unsafe / sizeof unsafe[0]; i++) {
        if (!edit_display_is_real(unsafe[i])) {
            fprintf(stderr, "display_guard_test: FAIL §39 alias %s\n", unsafe[i]); failed = 1;
        }
        if (strcmp(editor_runtime_display(unsafe[i], unsafe[i], ""), ":99")) {
            fprintf(stderr, "display_guard_test: FAIL §39 runtime alternate %s\n", unsafe[i]); failed = 1;
        }
    }
    const char *safe[] = {":99", ":099.0", "unix/:99", "hostname/unix:99", "[::1]:99"};
    for (size_t i = 0; i < sizeof safe / sizeof safe[0]; i++) {
        if (edit_display_is_real(safe[i])) {
            fprintf(stderr, "display_guard_test: FAIL §39 safe %s\n", safe[i]); failed = 1;
        }
    }
    /* Exercise constructor policy directly, including unsafe alternate choice. */
    if (setenv("DISPLAY", "hostname/unix:0", 1) || setenv("EDIT_DISPLAY", ":00", 1) ||
        unsetenv("EDIT_ALLOW_REAL_DISPLAY")) return 1;
    edit_display_guard();
    if (!getenv("EDIT_REFUSE_REAL_DISPLAY")) failed = 1; /* (P) presence enables child refusal. */
    if (strcmp(getenv("DISPLAY"), ":99")) {
        fputs("display_guard_test: FAIL §39 unsafe alternate refused\n", stderr); failed = 1;
    }
    if (setenv("DISPLAY", "localhost:00.0", 1) || setenv("EDIT_DISPLAY", ":99", 1)) return 1;
    edit_display_guard();
    if (strcmp(getenv("DISPLAY"), ":99")) failed = 1;
    puts(failed ? "display_guard_test: FAILED" : "display_guard_test: §39 ok");
    return failed;
}
