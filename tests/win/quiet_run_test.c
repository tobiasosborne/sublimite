/* Windows suites must not be able to reach the person using the desktop: every
 * run through tools/winrun.sh lands on a private station or desktop. Reads names
 * only; deliberately injects no input, so a failed isolation still disturbs nothing. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

#include "test_isolation.h"

int main(int argc, char **argv)
{
    test_isolation isolation = test_isolation_query();
    if (test_isolation_names("WinSta0","Default") != TEST_ISOLATION_LIVE ||
        test_isolation_names("WinSta0","sublimite-test-probe") != TEST_ISOLATION_DESKTOP ||
        test_isolation_names("Service-probe","quiet") != TEST_ISOLATION_STATION ||
        test_isolation_names("WinSta0","quiet") != TEST_ISOLATION_UNKNOWN ||
        test_isolation_names("Service-probe","Default") != TEST_ISOLATION_UNKNOWN) {
        puts("quiet_run_test: FAIL classification"); return 1;
    }
    if (isolation != TEST_ISOLATION_STATION && isolation != TEST_ISOLATION_DESKTOP) {
        puts("quiet_run_test: FAIL running outside private station/desktop"); return 1;
    }
    char mode[16] = "station";
    (void)GetEnvironmentVariableA("WINRUN_ISOLATION",mode,sizeof mode);
    if ((!strcmp(mode,"station") && isolation != TEST_ISOLATION_STATION) ||
        (!strcmp(mode,"desktop") && isolation != TEST_ISOLATION_DESKTOP) ||
        (strcmp(mode,"station") && strcmp(mode,"desktop"))) {
        puts("quiet_run_test: FAIL runner did not supply requested isolation"); return 1;
    }
    /* A lying environment must never grant clipboard capability. */
    (void)SetEnvironmentVariableA("WINRUN_ISOLATION","station");
    if (test_isolation_query() != isolation) return 1;
    (void)SetEnvironmentVariableA("WINRUN_ISOLATION","desktop");
    if (test_isolation_query() != isolation) return 1;
    (void)SetEnvironmentVariableA("WINRUN_ISOLATION",mode);
    if (argc == 2 && !strcmp(argv[1],"--hold-tree")) {
        char exe[MAX_PATH], command[MAX_PATH+32];
        if (!GetModuleFileNameA(NULL,exe,sizeof exe)) return 1;
        (void)snprintf(command,sizeof command,"\"%s\" --hold",exe);
        STARTUPINFOA startup = {.cb = sizeof startup}; PROCESS_INFORMATION child;
        if (!CreateProcessA(exe,command,NULL,NULL,TRUE,0,NULL,NULL,&startup,&child)) return 1;
        printf("quiet_run_test: descendant pid=%lu\n",child.dwProcessId); fflush(stdout);
        CloseHandle(child.hThread); CloseHandle(child.hProcess);
    }
    if (argc == 2 && (!strcmp(argv[1],"--hold") || !strcmp(argv[1],"--hold-tree"))) {
        printf("quiet_run_test: holding pid=%lu\n",GetCurrentProcessId()); fflush(stdout);
        Sleep(INFINITE);
    }
    WNDCLASSW cls = {.lpfnWndProc = DefWindowProcW, .hInstance = GetModuleHandleW(NULL), .lpszClassName = L"quiet_run_test"};
    if (!RegisterClassW(&cls)) { puts("quiet_run_test: FAIL RegisterClass"); return 1; }
    HWND window = CreateWindowExW(0, cls.lpszClassName, L"quiet", WS_OVERLAPPEDWINDOW, 0, 0, 64, 64, NULL, NULL, cls.hInstance, NULL);
    if (!window) { printf("quiet_run_test: FAIL CreateWindow %lu\n", GetLastError()); return 1; }
    (void)ShowWindow(window, SW_SHOWNORMAL); /* The call that steals focus on a live desktop. */
    /* Foreground belongs to the input desktop, which this never is. */
    if (GetForegroundWindow() == window) { puts("quiet_run_test: FAIL became the foreground window"); return 1; }
    (void)DestroyWindow(window);
    puts("quiet_run_test: PASS");
    return 0;
}
