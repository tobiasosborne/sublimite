/* Windows suites must not be able to reach the person using the desktop: every
 * run through tools/winrun.sh lands on a private station or desktop. Reads names
 * only; deliberately injects no input, so a failed isolation still disturbs nothing. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

static const char prefix[] = "sublimite-test-", user_station[] = "WinSta0";
static int name(HANDLE object, char *out, DWORD size)
{ return GetUserObjectInformationA(object, UOI_NAME, out, size, NULL) ? 0 : -1; }

int main(void)
{
    char mode[16] = "station", station[128], desktop[128];
    (void)GetEnvironmentVariableA("WINRUN_ISOLATION", mode, sizeof mode);
    if (!strcmp(mode, "none")) { puts("quiet_run_test: SKIP (WINRUN_ISOLATION=none, by hand)"); return 0; }
    if (name(GetProcessWindowStation(), station, sizeof station) ||
        name(GetThreadDesktop(GetCurrentThreadId()), desktop, sizeof desktop)) { puts("quiet_run_test: FAIL name query"); return 1; }
    printf("quiet_run_test: mode=%s station=%s desktop=%s\n", mode, station, desktop);
    if (!strcmp(mode, "station") ? !strcmp(station, user_station) || strcmp(desktop, "quiet")
                                 : strncmp(desktop, prefix, sizeof prefix - 1u)) {
        puts("quiet_run_test: FAIL running on the user's desktop"); return 1;
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
