/* Exercise the benchmark's failure verdicts in the ordinary sanitizer suite. */
#define main find_bench_entry
#include "../bench/find_bench.c"
#undef main
/* (P) The minimum nonempty population is one observation. These cases stop
 * before worker submission: absent optional input succeeds only in quick
 * mode; missing required input and malformed existing input remain errors. */
static int test_optional_quick_fixture(void)
{
    char root[OS_PATH_MAX], path[OS_PATH_MAX]; uint64_t nonce;
    if (os_temp_dir(root, sizeof root) != OS_OK || os_random(&nonce, sizeof nonce) != OS_OK) return 1;
    int n=snprintf(path, sizeof path, "%s/sublimite-test-find-bench-%llx", root, (unsigned long long)nonce);
    if (n < 0 || (size_t)n >= sizeof path) return 1;
    os_file fd=OS_FILE_INIT;
    if (os_file_open(&fd,path,OS_OPEN_RDWR | OS_OPEN_CREATE | OS_OPEN_EXCL) != OS_OK) return 1;
    os_file_close(&fd);
    int rc = 0;
    const uint8_t needle[] = "a";
    row r = {.name = "G6v_a31b", .file = path, .needle = needle,
             .nn = sizeof needle - 1, .full_size = sizeof needle};
    /* The empty file is present but too short; quick mode must reject it. */
    rc |= measure_row(NULL, &r, 1, true) != 1;
    if (os_unlink(path) != OS_OK) return 1;
    rc |= measure_row(NULL, &r, 1, false) != 1;
    const char *names[] = {"G6_dense_a", "G6v_a31b", "G6v_first_last_middle",
                           "G6v_first_last_early"};
    for (size_t i = 0; i < sizeof names / sizeof names[0]; i++) {
        r.name = names[i];
        rc |= measure_row(NULL, &r, 1, true) != 0;
    }
    r.name = "G6_ERROR";
    rc |= measure_row(NULL, &r, 1, true) != 1;
    rc |= measure_cancel(NULL, &r, 1, true) != 0;
    rc |= measure_cancel(NULL, &r, 1, false) != 1;
    if (rc) fputs("edit-ev0 FAIL: quick absent all-a rows must skip; required/full/bad-size fixtures must fail\n", stderr);
    return rc;
}
int main(int argc, char **argv)
{
#if defined(_WIN32)
    if (argc == 2 && strncmp(argv[1], "--find-fixture=", 15) == 0) return find_bench_entry(argc, argv);
#else
    (void)argc; (void)argv;
#endif
    char *cancel[] = {"find_bench", "--self-check-cancel"};
    char *deadlines[] = {"find_bench", "--self-check-deadlines"};
    int c = find_bench_entry(2, cancel);
    int d = find_bench_entry(2, deadlines);
    int q = test_optional_quick_fixture();
    if (c || d || q) {
        fprintf(stderr, "find_bench_test FAIL: cancellation verdict=%d deadline verdict=%d quick fixtures=%d\n", c, d, q);
        return 1;
    }
    puts("find_bench_test: ok (G6c misses rejected; missing events/shutdown bounded; absent optional quick fixtures skipped)");
    return 0;
}
