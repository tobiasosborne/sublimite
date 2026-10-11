/* Keep the offline trace-join regression in make check's headless test lane. */
#include <stdio.h>
#include <unistd.h>

int main(void)
{
    execlp("python3", "python3", "-B", "tests/refwin_pairs_test.py", (char *)NULL);
    perror("trace_pairs_test: python3");
    return 1;
}
