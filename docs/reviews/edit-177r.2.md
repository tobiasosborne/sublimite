**The contract is not ready to freeze.** Both Linux conformance binaries passed all 14 cases. Focused Linux and native Windows/NTFS probes nevertheless confirmed contract violations and incorrect mappings. Findings below distinguish existing implementation defects from problems in the proposed Windows design.

1. **BLOCKER — The mutex contract cannot support `piece`.**  
   **Location:** `src/os/os.h:143`  
   `piece_create()` explicitly creates `PTHREAD_MUTEX_RECURSIVE` (`src/piece/piece.c:1116`). This is necessary: `node_unref()` holds `pool_mu`, calls `node_unref_locked()`, and reaches `pool_free()`, which locks it again. Migrating this to the specified default pthread mutex/SRWLOCK deadlocks reclamation and mutations. The inventory’s generic mutex row misses this requirement.  
   **Fix:** Add a separate recursive mutex operation/type, backed by a recursive pthread mutex and Windows `CRITICAL_SECTION`, with nested acquisition and allocation tests.

2. **BLOCKER — Blanket EINTR retries violate the journal’s typing-path contract.**  
   **Location:** `src/os/os.h:21`; `src/os/os_posix.c:319`  
   `journal/cache_write()` deliberately makes exactly one syscall attempt (`src/journal/journal.c:570`); `journal.h:245` explicitly makes EINTR sticky IO and forbids UI retries. An injected EINTR caused `os_file_pwrite()` to issue two attempts and return success. There is no portable single-attempt alternative.  
   **Fix:** Add a single-attempt positioned-write entry with explicit EINTR behavior and no allocation, locks, or internal retries. Define retry policy per operation. Conversely, operations such as `os_file_open_anon`, `os_file_dup`, and `os_file_set_mode` currently do not implement the blanket retry promise.

3. **MAJOR — Lock lifetime is specified incorrectly for duplicates.**  
   **Location:** `src/os/os.h:305`  
   Closing “the handle that took” a lock does not necessarily release it. Linux and native NTFS probes both retained exclusion while a duplicate remained open; exclusion ended after the last duplicate closed. Existing session adoption intentionally shares a locked open-file description (`src/editor/recovery.c:754`). Following the written contract would break that lease protocol.  
   **Fix:** Specify shared lock ownership through duplicates, release on the last close, and shared sequential-position behavior. Test both close orders and exclusion throughout the session-lease handoff.

4. **MAJOR — Full sharing leaves name-based operations vulnerable to acting on a moved object.**  
   **Location:** `src/os/os.h:23`; `docs/decisions/edit-177r.2.md:84`  
   The proposed Windows implementation opens a named source, then renames/deletes through its handle. With delete sharing, another thread can move `x` to `y` and create a new `x` between those steps. Deleting through the retained handle then deletes `y`; POSIX `unlinkat(dir,"x")` would remove the current `x`. Pinning the parent does not close this source-name race. This follows from the [handle-based rename interface](https://learn.microsoft.com/en-us/windows/win32/api/winbase/ns-winbase-file_rename_info).  
   **Fix:** Allow temporary namespace-operation handles to deny delete sharing while pinning the source, document resulting contention errors, and test the interleaving deterministically.

5. **MAJOR — Hidden directory-lock files do not provide the promised directory semantics.**  
   **Location:** `src/os/os.h:356`; `src/os/os.h:375`  
   The contract hides a reserved entry and authorizes `os_rmdir()` to remove it, without fixing its name, authenticating ownership, or protecting its identity. An existing user file at that name could become invisible and be deleted. Replacing the lock file can split exclusion between different objects. Creating it also requires permissions that Linux directory `flock` does not require. `journal_retire_directory()` explicitly relies on an inode lock without a replaceable lock-file name (`src/journal/retire.c:377`).  
   **Fix:** Design an identity-keyed lock outside the protected directory, or specify and enforce a protected, authenticated reservation protocol. Never hide or delete an arbitrary colliding entry.

6. **MAJOR — The rename recipe fails on read-only targets.**  
   **Location:** `src/os/os.h:392`; `docs/decisions/edit-177r.2.md:87`  
   Native NTFS testing confirmed that `REPLACE_IF_EXISTS | POSIX_SEMANTICS` fails with access denied when the target has `FILE_ATTRIBUTE_READONLY`. Adding `IGNORE_READONLY_ATTRIBUTE` succeeds. Linux replacement depends on directory permissions, so the current recipe changes save behavior for mode-0444 originals. Microsoft documents the [additional flag and access requirement](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/ns-ntifs-_file_rename_information).  
   **Fix:** Include that flag and required rights, document genuine ACL failures, and test replacement over read-only open/mapped targets.

7. **MAJOR — Neither proposed CPU-clock fallback preserves the callers’ budget semantics.**  
   **Location:** `src/os/os.h:112`; `docs/decisions/edit-177r.2.md:108`  
   `lineidx` uses thread CPU time for 500,000-ns UI slices (`src/lineidx/lineidx.c:20`). Native `GetThreadTimes` advanced in 15.625-ms steps here—measured as 156,250 FILETIME units × 100 ns—so it cannot enforce those slices. The conformance test explicitly permits this. Scaling `QueryThreadCycleTime` is also not a portable time conversion; [Microsoft explicitly advises against it](https://learn.microsoft.com/en-us/windows/win32/api/realtimeapiset/nf-realtimeapiset-querythreadcycletime).  
   **Fix:** Separate CPU accounting from deadline enforcement, expose clock resolution/capability, and define a Windows deadline fallback before migration. Preserve Linux’s existing CPU-clock behavior.

8. **MAJOR — Ordinary reparse-backed files are classified as non-files.**  
   **Location:** `src/os/os.h:227`  
   “Other reparse points” includes ordinary data files using mechanisms such as WOF compression and cloud storage. Classifying all of them as `OS_KIND_OTHER` makes existing regular-file checks reject readable documents. Reparse tags identify several distinct storage mechanisms, as shown in [Microsoft’s definitions](https://learn.microsoft.com/en-us/windows/win32/fileio/reparse-point-tags).  
   **Fix:** Distinguish name-surrogate links and genuinely special objects from ordinary data files carrying reparse metadata. Specify following versus NOFOLLOW classification and add representative fixtures.

9. **MAJOR — The blanket no-allocation guarantee is already false.**  
   **Location:** `src/os/os.h:18`; `docs/decisions/edit-177r.2.md:74`  
   Counting-allocator probes observed one allocation during `os_thread_start()` and three during `os_env_set()`. Neither entry documents an allocation exception. The decision document says only iterators and process spawning are exceptions.  
   **Fix:** Document the allocating entries accurately. Keep explicit allocation guarantees for typing-path operations, including journal positioned writes and condition signaling/broadcasting.

10. **MAJOR — Write-only and truncating opens are missing.**  
    **Location:** `src/os/os.h:264`; `docs/decisions/edit-177r.2.md:60`  
    Existing calls include `O_WRONLY` save temporaries and `fopen(path,"wb")` trace dumps. RDWR adds a read-permission requirement; open followed by `set_size(0)` does not preserve atomic open-and-truncate semantics. The proposed trace migration also overlooks that `trace_dump()` accepts `FILE *`, not `os_file`.  
    **Fix:** Add write-only and truncating open modes. Specify a portable trace writer/stdio bridge and test truncating an existing write-only file.

11. **MAJOR — The process surface omits existing crash-test coordination.**  
    **Location:** `src/os/os.h:549`; `docs/decisions/edit-177r.2.md:54`  
    The existing suites need more than re-execution and waiting. For example, `tests/dataloss_test.c:556` and `:621` use bidirectional readiness/report pipes; `:197` terminates the current process from a save-step callback. There is no portable pipe, selectable child inheritance/redirection, cross-process wake, or immediate self-termination entry.  
    **Fix:** Specify these operations now, or explicitly design replacement protocols for each affected test family. The inventory currently presents an incomplete migration as covered.

12. **MAJOR — Process handle inheritance is wrong on both sides of the proposed mapping.**  
    **Location:** `src/os/os_posix.c:826`; `docs/decisions/edit-177r.2.md:111`  
    Linux `posix_spawn()` with no file actions inherits unrelated non-CLOEXEC descriptors; a probe confirmed this despite the “no other handle” promise. On Windows, `bInheritHandles=FALSE` cannot implement the stated redirected-standard-stream inheritance through `STARTUPINFO`; [that mechanism requires inheritable handles and TRUE](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/ns-processthreadsapi-startupinfow).  
    **Fix:** Close unrelated descriptors in POSIX spawn actions. On Windows, duplicate selected standard handles and use an explicit inheritance list. Test redirected streams and an unrelated sentinel descriptor.

13. **MAJOR — UTF-16 spawning does not make the unchanged child’s `argv` UTF-8.**  
    **Location:** `tests/os_test.c:895`; `docs/decisions/edit-177r.2.md:111`  
    The child consumes narrow `main(argc,argv)` arguments as UTF-8 paths. `CreateProcessW` alone does not establish that encoding; the narrow process command line ordinarily uses its process code page. This can break the lock child under a non-ASCII temp/profile path. Microsoft describes the [lossy conversion and UTF-8 manifest solution](https://learn.microsoft.com/en-us/windows/win32/api/processenv/nf-processenv-getcommandlinea).  
    **Fix:** Require a UTF-8 process manifest or a wide-entry conversion bridge. Test re-execution with non-ASCII paths, empty arguments, spaces, quotes, and trailing backslashes.

14. **MAJOR — Mapping `ERROR_IO_PENDING` to a returned error leaves buffer lifetime undefined.**  
    **Location:** `src/os/os.h:60`; `src/os/os.h:284`  
    These signatures have no completion object or retained-buffer lifetime. Returning `OS_E_AGAIN` for pending overlapped I/O would let callers reuse/free buffers while Windows still accesses them. [Pending I/O is not a completed failure](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-readfile).  
    **Fix:** State that every transfer has completed or been canceled and reaped before return, including error returns. Handle pending completion internally and test concurrent positioned operations with forced pending completion.

15. **MAJOR — The anonymous-file recipe does not remove the name before returning.**  
    **Location:** `src/os/os.h:367`  
    A native NTFS probe successfully requested POSIX disposition on the retained creation handle, but the name remained present until that handle closed. The [namespace removal occurs when the delete handle closes](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntddk/ns-ntddk-_file_disposition_information_ex).  
    **Fix:** Retain the data handle, independently open a delete handle, set POSIX disposition on that handle, and close it before returning. A duplicate of the same open file object should not be assumed sufficient. Preserve the immediate-listing assertion.

16. **MAJOR — Two independent fault-service installers can lose one another.**  
    **Location:** `src/os/os_posix.c:751`; `docs/decisions/edit-177r.2.md:139`  
    `file_bus` and `os_fault_service` have independent once controls and separate query/install calls. Both threads can query the same previous SIGBUS action, then install their respective handlers. The last installation bypasses the other service. Thus the documented claim that they compose by chaining is false under concurrent first registration.  
    **Fix:** Share installation synchronization between both services, or ensure only one installer exists in a process. Add a barrier-controlled concurrent-installation regression test.

17. **MAJOR — New Linux admission checks change existing module outcomes.**  
    **Location:** `src/os/os_posix.c:243`; `src/os/os_posix.c:595`  
    Directory opens now fail before callers can classify them: `file` currently reports `FILE_ERR_NOTREG` after successful open/fstat (`src/file/file.c:733`), whereas its open-failure path reports IO. The mapping bounds check also rejects a shrink race that Linux’s guarded mapping path previously admitted. These are acknowledged implementation differences, but their module-visible consequences are not addressed.  
    **Fix:** Preserve the existing classifications through explicit migration rules and regression tests, including directory opens and truncate-during-acquisition. Do not claim Linux equivalence from wrapper tests alone.

18. **MAJOR — The Windows suite can pass with no functioning fault recovery.**  
    **Location:** `tests/os_test.c:674`  
    NTFS takes the BUSY branch, so the suite never exercises recovery, epoch advancement, prefix preservation, placeholder splitting, or cleanup after recovery. A dummy registration returning a slot with an eternally zero epoch can pass Windows testing. Retirement concurrency, exhaustion, and unrelated-fault chaining are also absent.  
    **Fix:** Add a portable test-only fault trigger backed by native fault delivery/recovery machinery, plus retirement/pinning, capacity, chaining, and recovered-view cleanup cases.

19. **MAJOR — NOFOLLOW and pinned-directory protection are insufficiently tested.**  
    **Location:** `tests/os_test.c:387`; `tests/os_test.c:466`  
    NOFOLLOW flags are exercised only on regular files; no symlink/junction establishes their security behavior. The pinned-directory test renames the directory but never installs a decoy at its former path, leaving the central pathname-redirection scenario unchecked.  
    **Fix:** Supply platform-created link fixtures to the portable suite. Assert NOFOLLOW kind/error behavior, replace the original parent path with a decoy, and exercise relative open/stat/create/rename/unlink against the retained directory.

20. **MAJOR — Large-file, long-path, and mapping-boundary requirements are unpinned.**  
    **Location:** `tests/os_test.c:306`; `tests/os_test.c:626`; `tests/os_test.c:732`  
    Transfers use small offsets; mappings always start at zero and have page-multiple lengths; paths are short and absolute. Implementations that truncate offsets to 32 bits, omit long-path handling, mishandle nonzero mapping offsets, or reject ordinary partial-page files can pass.  
    **Fix:** Add sparse offsets beyond 4 GiB, nonzero aligned mapping offsets, partial-page lengths, relative paths, and nested Unicode paths exceeding 260 characters. Add simultaneous positioned-I/O checks on one handle.

21. **MAJOR — The allocation test silently accepts an inactive release guard.**  
    **Location:** `tests/os_test.c:299`  
    Guard inactivity is always accepted and described as “sanitizer,” including a Windows release build whose guard is missing. The checked operations also omit condition signaling/broadcasting and typing-path journal writes.  
    **Fix:** Require an active guard in release configurations, verify it with a positive-control allocation, and exercise the complete typing-path operation set under cold and contended conditions.

22. **MINOR — NOFOLLOW error precedence contradicts Linux behavior.**  
    **Location:** `src/os/os.h:269`  
    CREATE|EXCL|NOFOLLOW on an existing symlink returned `OS_E_EXIST` in a probe; the NOFOLLOW comment unconditionally promises `OS_E_LOOP`. Both flags cannot have unconditional precedence.  
    **Fix:** Specify EXCL precedence and test the combination on existing and dangling links.

23. **MINOR — Metadata conversion silently destroys pre-epoch timestamps.**  
    **Location:** `src/os/os_posix.c:203`  
    `ts_ns()` clamps every negative timestamp to zero. A file with native mtime −1 second was reported as zero. Existing `file_id_from_stat()` preserves its unsigned encoding, while journal capture explicitly rejects negative mtimes; the new representation cannot preserve either distinction.  
    **Fix:** Define timestamp range/validity explicitly and preserve the information needed by existing callers.

24. **MINOR — The thread-ID test requires more than the contract guarantees.**  
    **Location:** `tests/os_test.c:214`  
    IDs are compared after joining all workers. An early worker may terminate before a later worker starts; Windows may reuse its ID because uniqueness is promised only among live threads.  
    **Fix:** Hold all workers at a barrier until their IDs have been recorded and compared while all remain alive.

25. **MINOR — Blocking conformance cases have no effective hang guard.**  
    **Location:** `tests/os_test.c:229`; `tests/os_test.c:274`  
    Condition waits, joins, and the infinite wake wait can hang permanently. `HANG_GUARD_NS` does not cover them, and the Makefile supplies no suite execution timeout.  
    **Fix:** Run blocking cases in supervised subprocesses with bounded waits and termination on timeout.

26. **MINOR — An unknown case selector produces a false-green run.**  
    **Location:** `tests/os_test.c:906`  
    An unmatched `OS_TEST_CASE` runs zero cases and exits successfully.  
    **Fix:** Reject unknown selectors and require at least one executed case.

27. **MINOR — The Windows mapping recipe names the wrong protection constant family.**  
    **Location:** `src/os/os.h:461`  
    `MapViewOfFile3` takes page-protection constants, not `FILE_MAP_READ` access flags. The specified read-only recipe must use `PAGE_READONLY`. See the [function’s parameter contract](https://learn.microsoft.com/en-us/windows/win32/api/memoryapi/nf-memoryapi-mapviewoffile3).  
    **Fix:** Correct the recipe and explicitly distinguish logical view length from page-rounded native mapping extent.