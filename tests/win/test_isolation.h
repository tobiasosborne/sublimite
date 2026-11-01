/* Test-only capabilities come from native object names, never the requested
 * WINRUN_ISOLATION environment value. Unknown objects fail closed. */
#ifndef EDIT_TEST_ISOLATION_H
#define EDIT_TEST_ISOLATION_H
#include <windows.h>
#include <stdio.h>
#include <string.h>
typedef enum test_isolation {
    TEST_ISOLATION_UNKNOWN, TEST_ISOLATION_LIVE,
    TEST_ISOLATION_DESKTOP, TEST_ISOLATION_STATION
} test_isolation;
static inline test_isolation test_isolation_names(const char *station, const char *desktop)
{
    if (_stricmp(station,"WinSta0"))
        return !strcmp(desktop,"quiet") ? TEST_ISOLATION_STATION : TEST_ISOLATION_UNKNOWN;
    if (!strncmp(desktop,"sublimite-test-",sizeof "sublimite-test-"-1u)) return TEST_ISOLATION_DESKTOP;
    return !_stricmp(desktop,"Default") ? TEST_ISOLATION_LIVE : TEST_ISOLATION_UNKNOWN;
}
static inline test_isolation test_isolation_query(void)
{
    char station[256], desktop[256];
    if (!GetUserObjectInformationA(GetProcessWindowStation(),UOI_NAME,station,sizeof station,NULL) ||
        !GetUserObjectInformationA(GetThreadDesktop(GetCurrentThreadId()),UOI_NAME,desktop,sizeof desktop,NULL))
        return TEST_ISOLATION_UNKNOWN;
    test_isolation isolation = test_isolation_names(station,desktop);
    printf("isolation: station=%s desktop=%s kind=%d\n",station,desktop,(int)isolation);
    return isolation;
}
static inline void test_skip(const char *name, const char *reason)
{ printf("%s: SKIP (%s)\n",name,reason); }
/* A private station/desktop does NOT isolate the system pointer. This must
 * be the first statement of any test function using native pointer/input
 * operations; tools/check_win_pointer.py checks that contract before check.
 * Empty __VA_ARGS__ returns from void fixtures; integer fixtures pass 0. */
#define LIVE_ONLY_NATIVE_LOOP(name, ...) do { \
    if (test_isolation_query() != TEST_ISOLATION_LIVE) { \
        test_skip(name, "needs live desktop: native move/size loop moves the real pointer"); \
        return __VA_ARGS__; \
    } \
} while (0)
#endif
