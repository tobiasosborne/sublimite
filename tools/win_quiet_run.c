/* win_quiet_run <exe> [args...]: run a Windows test where it cannot touch the
 * person using this desktop (Tobias, 2026-11-01). The child gets a private window
 * station and desktop that are never switched to: its windows are invisible and
 * can never take focus, the clipboard is the station's own, and input injection
 * cannot reach the user's applications. Below-normal priority yields the CPU to
 * the user; a kill-on-close job leaves no orphan test process when a run is cut.
 *
 * WINRUN_ISOLATION selects the isolation (default station):
 *   station  private window station + desktop
 *   desktop  private desktop in the user's station, for what a non-interactive
 *            station refuses; invisible and focus-free, but it SHARES the clipboard
 * Exit code: the child's; 125 when isolation or the launch failed. Never falls
 * back to the live desktop: tools/winrun.sh takes WINRUN_ISOLATION=none, by hand. */
#define _WIN32_WINNT 0x0A00 /* PROC_THREAD_ATTRIBUTE_JOB_LIST */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <wchar.h>

#define FAIL_EXIT 125u /* Above any exit code the suites use; distinct from a test failure. */

static void die(const char *what)
{
    fprintf(stderr, "win_quiet_run: %s failed: native %lu\n", what, GetLastError());
    ExitProcess(FAIL_EXIT);
}
/* The raw command line after argv[0], so child quoting survives untouched. */
static wchar_t *after_argv0(wchar_t *line)
{
    if (*line == L'"') { line++; while (*line && *line != L'"') line++; if (*line) line++; }
    else while (*line && *line != L' ' && *line != L'\t') line++;
    while (*line == L' ' || *line == L'\t') line++;
    return line;
}
static void inherit(DWORD which, HANDLE *out)
{
    *out = GetStdHandle(which);
    if (*out && *out != INVALID_HANDLE_VALUE) (void)SetHandleInformation(*out, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
}
int main(void)
{
    wchar_t *rest = after_argv0(GetCommandLineW());
    if (!*rest) { fputs("usage: win_quiet_run <exe> [args...]\n", stderr); return (int)FAIL_EXIT; }
    /* Per-process names: station and desktop live until their last handle closes, so runs cannot interact. */
    wchar_t station[64], desktop[128], mode[16] = L"station";
    (void)GetEnvironmentVariableW(L"WINRUN_ISOLATION", mode, 16);
    _snwprintf(station, 64, L"sublimite-test-%lu", GetCurrentProcessId());
    STARTUPINFOEXW ex; ZeroMemory(&ex, sizeof ex); ex.StartupInfo.cb = sizeof ex;
    STARTUPINFOW *si = &ex.StartupInfo;
    if (!wcscmp(mode, L"station")) {
        HWINSTA own = GetProcessWindowStation();
        /* NULL name: Windows refuses a named station to this unelevated interop token
         * (M: native 5, 2026-11-01) and generates a per-logon name instead. */
        HWINSTA ws = CreateWindowStationW(NULL, 0, WINSTA_ALL_ACCESS, NULL);
        if (!ws) die("CreateWindowStation");
        if (!GetUserObjectInformationW(ws, UOI_NAME, station, sizeof station, NULL)) die("station name");
        if (!SetProcessWindowStation(ws)) die("SetProcessWindowStation");
        if (!CreateDesktopW(L"quiet", NULL, NULL, 0, GENERIC_ALL, NULL)) die("CreateDesktop");
        if (!SetProcessWindowStation(own)) die("SetProcessWindowStation restore");
        _snwprintf(desktop, 128, L"%ls\\quiet", station);
    } else if (!wcscmp(mode, L"desktop")) {
        if (!CreateDesktopW(station, NULL, NULL, 0, GENERIC_ALL, NULL)) die("CreateDesktop");
        _snwprintf(desktop, 128, L"%ls", station);
    } else {
        fputs("win_quiet_run: WINRUN_ISOLATION must be station or desktop\n", stderr);
        return (int)FAIL_EXIT;
    }
    si->lpDesktop = desktop;
    si->dwFlags = STARTF_USESTDHANDLES;
    inherit(STD_INPUT_HANDLE, &si->hStdInput);
    inherit(STD_OUTPUT_HANDLE, &si->hStdOutput);
    inherit(STD_ERROR_HANDLE, &si->hStdError);
    HANDLE job = CreateJobObjectW(NULL, NULL); if (!job) die("CreateJobObject");
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits; ZeroMemory(&limits, sizeof limits);
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof limits)) die("SetInformationJobObject");
    /* The job is part of creation (Windows 10 1607+; this box's minimum per
     * windows-port.md), so no descendant exists outside it even for an instant:
     * assigning a suspended child afterwards stranded orphans when a run was killed
     * in between (M: eight, 2026-11-01). */
    SIZE_T attribute_bytes = 0;
    (void)InitializeProcThreadAttributeList(NULL, 1, 0, &attribute_bytes);
    static char attribute_storage[256]; /* (M) one attribute needs 48 bytes on x64; checked below. */
    if (attribute_bytes > sizeof attribute_storage) { fputs("win_quiet_run: attribute list too large\n", stderr); return (int)FAIL_EXIT; }
    ex.lpAttributeList = (LPPROC_THREAD_ATTRIBUTE_LIST)attribute_storage;
    if (!InitializeProcThreadAttributeList(ex.lpAttributeList, 1, 0, &attribute_bytes)) die("InitializeProcThreadAttributeList");
    if (!UpdateProcThreadAttribute(ex.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_JOB_LIST, &job, sizeof job, NULL, NULL)) die("UpdateProcThreadAttribute");
    PROCESS_INFORMATION pi;
    if (!CreateProcessW(NULL, rest, NULL, NULL, TRUE, EXTENDED_STARTUPINFO_PRESENT | BELOW_NORMAL_PRIORITY_CLASS, NULL, NULL, si, &pi)) die("CreateProcess");
    if (WaitForSingleObject(pi.hProcess, INFINITE) != WAIT_OBJECT_0) die("WaitForSingleObject");
    DWORD code = FAIL_EXIT;
    if (!GetExitCodeProcess(pi.hProcess, &code)) die("GetExitCodeProcess");
    return (int)code;
}
