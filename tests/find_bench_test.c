/* Exercise the benchmark's failure verdicts in the ordinary sanitizer suite. */
#define main find_bench_entry
#include "../bench/find_bench.c"
#undef main
int main(void)
{
    char *cancel[] = {"find_bench", "--self-check-cancel"};
    char *deadlines[] = {"find_bench", "--self-check-deadlines"};
    int c = find_bench_entry(2, cancel);
    int d = find_bench_entry(2, deadlines);
    if (c || d) {
        fprintf(stderr, "P1R10/P1R11 FAIL: cancellation verdict=%d deadline verdict=%d\n", c, d);
        return 1;
    }
    puts("find_bench_test: ok (G6c misses rejected; missing events/shutdown bounded)");
    return 0;
}
