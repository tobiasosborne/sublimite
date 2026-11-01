/* Test-only timeout evidence for processes created by the calling test. */
#ifndef TEST_WIN_TIMEOUT_DIAG_H
#define TEST_WIN_TIMEOUT_DIAG_H
#include <windows.h>
#include <tlhelp32.h>
#include <wct.h>
#include <stdio.h>
/* (E) Reuse os_test's 120 s whole-case hang guard for a native child phase.
 * (M) [AC, desktop Spiral], eight concurrent copies per affected suite:
 * ready HWND took 17.134 s; startup/cut/close/reopen/exact recovery took
 * 39.536 s, so 120/39.536 > 3. The old 5/10/15 s guards expired while
 * below-normal-priority children had little/no CPU or I/O, before test work.
 * Scheduling and native image/driver I/O have no 5 s bound at that priority.
 * This bounds a hang, not latency; completion conditions stay mandatory.
 * See docs/worker-reports/edit-177r.29-s28.md for red/observation counts. */
#define TEST_WIN_PROCESS_NS UINT64_C(120000000000)
static inline unsigned long long test_filetime(FILETIME time)
{ return ((unsigned long long)time.dwHighDateTime<<32)|time.dwLowDateTime; }
static inline void test_timeout_process(DWORD pid)
{
    HANDLE process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,pid);
    if(process) {
        FILETIME created,exited,kernel,user; IO_COUNTERS io={0}; DWORD code=0;
        (void)GetExitCodeProcess(process,&code);
        if(GetProcessTimes(process,&created,&exited,&kernel,&user))
            fprintf(stderr,"timeout pid=%lu exit=%lu priority=%lu cpu_ms=%llu\n",pid,code,
                GetPriorityClass(process),(test_filetime(kernel)+test_filetime(user))/10000u);
        if(GetProcessIoCounters(process,&io))
            fprintf(stderr,"timeout pid=%lu read_ops=%llu write_ops=%llu read_bytes=%llu write_bytes=%llu\n",
                pid,io.ReadOperationCount,io.WriteOperationCount,io.ReadTransferCount,io.WriteTransferCount);
        CloseHandle(process);
    }
    HWCT session=OpenThreadWaitChainSession(0,NULL);
    HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD,0);
    if(snapshot!=INVALID_HANDLE_VALUE) {
        THREADENTRY32 entry={.dwSize=sizeof entry};
        if(Thread32First(snapshot,&entry)) do {
            if(entry.th32OwnerProcessID!=pid) continue;
            /* (P) WCT supports at most sixteen nodes per chain. */
            WAITCHAIN_NODE_INFO nodes[16]; DWORD count=sizeof nodes/sizeof nodes[0]; BOOL cycle=FALSE;
            if(session && GetThreadWaitChain(session,0,0,entry.th32ThreadID,&count,nodes,&cycle)) {
                fprintf(stderr,"timeout tid=%lu cycle=%d nodes=%lu",entry.th32ThreadID,cycle,count);
                for(DWORD at=0;at<count;at++)
                    fprintf(stderr," %u:%u",(unsigned)nodes[at].ObjectType,(unsigned)nodes[at].ObjectStatus);
                fputc('\n',stderr);
            } else fprintf(stderr,"timeout tid=%lu wait_chain_error=%lu\n",entry.th32ThreadID,GetLastError());
        } while(Thread32Next(snapshot,&entry));
        CloseHandle(snapshot);
    }
    if(session) CloseThreadWaitChainSession(session);
}
#endif
