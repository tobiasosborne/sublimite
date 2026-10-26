/* Keep the offline trace-join regression in make check's headless test lane. */
#include <stdio.h>
#if defined(_WIN32)
#include "os/os.h"
#include <stdlib.h>
#else
#include <unistd.h>
#endif

int main(void)
{
#if defined(_WIN32)
    const char *python = getenv("SUBLIMITE_TEST_PYTHON");
    if (!python || !*python) {
        puts("SKIP trace_pairs Python cases (SUBLIMITE_TEST_PYTHON native interpreter path unset)");
        return 0;
    }
    const char *args[] = {python, "-B", "tests/refwin_pairs_test.py", NULL};
    os_proc child;
    os_proc_status status;
    if (os_proc_spawn(&child, args) != OS_OK || os_proc_wait(&child, -1, &status) != OS_OK)
        return 1;
    return status.how == OS_PROC_EXITED ? status.code : 1;
#else
    execlp("python3", "python3", "-B", "tests/refwin_pairs_test.py", (char *)NULL);
    perror("trace_pairs_test: python3");
    return 1;
#endif
}
