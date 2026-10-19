/* edit-nlz: selector lookup preserves names, precedence and callback results. */
#include "select.h"
#include <stdio.h>

/* (P) zero is C success; the error marker is editor_test's existing
 * unknown-selector exit code, distinct from the boolean failure code. */
enum { SELECT_OK = 0, SELECT_ERROR = 2 };
#define T(c) do { if (!(c)) { fprintf(stderr, "select_test:%d: FAIL %s\n", __LINE__, #c); return SELECT_ERROR; } } while (0)

static int success(void) { return SELECT_OK; }
static int failure(void) { return SELECT_ERROR; }

static int exact_lookup(void)
{
    static const test_selector cases[] = {
        {"--success", success},
        {"--failure", failure},
        {"--success", failure},
    };
    T(TEST_SELECT(cases, "--success") == success);
    T(TEST_SELECT(cases, "--failure") == failure);
    T(TEST_SELECT(cases, "--Success") == NULL);
    T(TEST_SELECT(cases, "--succ") == NULL);
    T(TEST_SELECT(cases, "--success-extra") == NULL);
    T(TEST_SELECT(cases, NULL) == NULL);
    T(test_select_find(NULL, 0, "--success") == NULL); /* (P) empty table. */
    T(TEST_SELECT(cases, "--success")() == SELECT_OK);
    T(TEST_SELECT(cases, "--failure")() == SELECT_ERROR);
    puts("select_test: exact names, missing/empty lookup, first match and exit codes passed (P)");
    return SELECT_OK;
}

static int environment_lookup(void)
{
    static const test_selector cases[] = {
        {"EDIT_NLZ_SELECT_FIRST", success},
        {"EDIT_NLZ_SELECT_LAST", failure},
    };
    /* (P) setenv's overwrite flag replaces any inherited fixture value. */
    T(unsetenv("EDIT_NLZ_SELECT_FIRST") == SELECT_OK);
    T(unsetenv("EDIT_NLZ_SELECT_LAST") == SELECT_OK);
    T(TEST_SELECT_ENV(cases) == NULL);
    T(test_select_env(NULL, 0) == NULL); /* (P) empty table. */
    T(setenv("EDIT_NLZ_SELECT_LAST", "", 1) == SELECT_OK);
    T(TEST_SELECT_ENV(cases) == failure);
    T(setenv("EDIT_NLZ_SELECT_FIRST", "present", 1) == SELECT_OK);
    T(TEST_SELECT_ENV(cases) == success);
    T(unsetenv("EDIT_NLZ_SELECT_FIRST") == SELECT_OK);
    T(unsetenv("EDIT_NLZ_SELECT_LAST") == SELECT_OK);
    puts("select_test: unset/empty environment selectors and table precedence passed (P)");
    return SELECT_OK;
}

int main(void)
{
    return exact_lookup() || environment_lookup();
}
