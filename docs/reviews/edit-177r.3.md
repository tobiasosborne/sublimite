Review of edit-177r.3 (W0.3), commit 7775321, 2026-10-26. Static review of `git diff main...HEAD`, the frozen `src/os/os.h`, `tests/os_test.c`, the POSIX twin, both decision records, the worker report, and the build/scripts. No Windows executable was run and no runtime reproduction is claimed. API details below were checked against linked primary documentation; race schedules and consequences are deductions from the code. During review the worktree was rebased to 3cc78a5; `os_win.c` is identical to 7775321. Locations below refer to 7775321. The later coordinator acceptance of `os_win_state`, sync through write handles, and registering the handler last is respected.

1. **`src/os/os_win.c:1228` — BLOCKER — Unmapping can free another thread's new allocation.**

   After a successful release, the loop leaves `p` unchanged and calls `VirtualQuery(p)` again. Schedule: A releases the file prefix; B reserves/commits an unrelated allocation at that newly available address; A observes B's allocation and calls `VirtualFree(mbi.AllocationBase, ...)` on it. The same problem exists after releasing a private zero suffix. Distinct OS objects are explicitly usable concurrently; B need not violate the mapping's lifetime rules. This can turn ordinary cleanup into unrelated memory corruption.

   **Fix:** determine the complete extent of the owned allocation before releasing it, then advance past that extent unconditionally. Never discover what to free by querying an address after relinquishing ownership of it. Retain explicit ownership of each split piece, or walk each allocation's regions before release. Add a deterministic hook between release and the next iteration and reserve the released address from another thread; cleanup must leave that new allocation intact.

2. **`src/os/os_win.c:1360` — BLOCKER — A transient access violation can arrive after `busy` clears and crash a valid reader.**

   A sets `busy`, unmaps the prefix into placeholders, and starts remapping. B reads the temporarily inaccessible prefix and takes an access violation, but is descheduled before executing this handler. A finishes and clears `busy`. B resumes, successfully changes `busy` from zero to one, takes the `!in_page` branch, and returns `EXCEPTION_CONTINUE_SEARCH`. B's address is readable again, yet the process dies. Looking only at whether another handler is busy *now* cannot determine whether an already-raised exception was caused by the split.

   **Fix:** recognize read access violations against the current owned mapping state, including completed recoveries: wait for any active split, verify that the fault address is now a readable committed part of this view, and retry it. Check the exception's access kind so writes/execution are not swallowed. Publish the split's success/failure before releasing waiters. Add a test that pauses B's handler until after A clears `busy`; the existing four-thread test recovers four different views and cannot find this.

3. **`src/os/os_win.c:97` — MAJOR — The “write-once” caches have C11 data races and publish partial state.**

   `kb_probed`, its function pointers, `freq`, `cpu_res`, `temp2_probed`, and `SYSTEM_INFO` are plain shared objects. Concurrent first calls read/write them without synchronization. In particular, using `si.dwPageSize` as a readiness flag does not publish `dwAllocationGranularity`; another thread can observe a page size but an uninitialized granularity and reach the modulus in `os_map_file`. Identical eventual values and x64's aligned stores do not remove C11 undefined behavior. The installer barrier tests initialize the maps and system information beforehand.

   **Fix:** use `INIT_ONCE` for immutable system information/API pointers and appropriate atomic publication or separate once initialization for the clocks. Keep CPU calibration independent of initialization that it calls recursively. Keep these once objects inside the sanctioned cache object. Race genuinely cold clock/page/map/temp calls in a fresh process.

4. **`src/os/os_win.c:1417` — MAJOR — Multiple registrations of one view have independent epochs and split locks.**

   Registration accepts the same map repeatedly; the suite itself does this to fill all slots. A fault is handled by the first matching LIVE slot, so later registrants' epochs never change even though their mapped bytes become zeros. If the first slot is retired while its handler is still splitting, another matching LIVE slot can admit a second split with a different `busy` word against the same placeholders. Per-slot pinning protects slot fields, not the shared virtual address topology.

   **Fix:** share recovery serialization and epoch state for registrations of the same view, with lifetime held until the final registration retires. Alternatively prohibit duplicate registrations in an explicitly revised contract and adjust the capacity test; silently accepting them is unsafe. Test two registrations, recovery observed through both, and retirement of one while another reader faults. The POSIX twin also has the duplicate-epoch limitation; it does not validate the Windows topology race.

5. **`src/os/os_win.c:1321` — MAJOR — “Not mapped” is treated as successful recovery without repairing anything.**

   The early return accepts every non-`MEM_MAPPED` region, including reserved/free space and private pages. Most importantly, committed zero pages can themselves suffer an in-page error while being paged back from the pagefile. The handler then increments the epoch and returns CONTINUE_EXECUTION without changing the failed page, so the same instruction faults indefinitely. It also cannot distinguish an intact zero suffix from a placeholder left by an unsuccessful split.

   **Fix:** track the owned file prefix and zero suffix explicitly. Recreate a failed private zero range if it can be recovered, and return failure for an unexpected hole/protection/state; never claim success solely from `mbi.Type != MEM_MAPPED`. Inject failures at every split stage and an in-page error on a previously recovered suffix, checking termination/recovery and complete unmapping.

6. **`src/os/os_win.c:555` — MAJOR — NOFOLLOW validation happens after a destructive overwrite open.**

   `WRITE|TRUNC|NOFOLLOW` and `CREATE|TRUNC|NOFOLLOW` select `FILE_OVERWRITE`/`FILE_OVERWRITE_IF` while also bypassing reparse processing. Only after that open does the code inspect the tag. This can overwrite the reparse-bearing file before deciding to return LOOP; for a data tag it overwrites without the owning filter and then reopens with plain `FILE_OPEN`, losing the truncating disposition at the filtered open. This is especially unsafe for cloud/HSM placeholders. The successful plain-file truncation test does not cover this combination. Microsoft's [reparse operation documentation](https://learn.microsoft.com/en-us/windows/win32/fileio/reparse-point-operations) explicitly describes bypass opens as a way to overwrite a reparse point.

   **Fix:** perform non-destructive tag/identity validation first, obtain the correctly filtered data handle, and apply truncation only to the verified object before publishing the handle. Preserve CREATE/EXCL precedence and document how atomic truncation is maintained. Test file symlinks and visible data-tag fixtures with both truncating dispositions; a rejected link must retain its data and tag, and a cloud file must truncate through its provider.

7. **`src/os/os_win.c:474` — MAJOR — Directory reparse points bypass the promised link semantics.**

   `stat_handle` tests DIRECTORY before the name-surrogate tag, so NOFOLLOW stat of a directory symlink/junction reports DIR rather than LINK. `open_file` requests `FILE_NON_DIRECTORY_FILE` before checking NOFOLLOW, so a directory surrogate can fail ISDIR instead of LOOP. `delete_h` similarly prevents unlink of a directory surrogate, while the rmdir branch can operate on the surrogate as if it were a real directory. The contract classifies junctions and directory symlinks as links, not only file symlinks.

   **Fix:** classify a successfully obtained surrogate tag before directory attributes for NOFOLLOW operations. Open the final component sufficiently generally to inspect it, then apply the file/directory operation's rules to the name itself. Propagate tag-query failures instead of silently classifying them as ordinary files, as lines 472–473 currently do. Add directory-symlink/junction, dangling-link, NOFOLLOW-open, unlink, rmdir, and CREATE|EXCL precedence tests.

8. **`src/os/os_win.c:550` — MAJOR — Reading a file unnecessarily requires write-attributes permission.**

   Every read-only open requests `FILE_WRITE_ATTRIBUTES`. A readable document whose ACL grants read access but denies attribute changes therefore cannot be opened, despite `OS_OPEN_READ` being documented as GENERIC_READ. A write-only handle also asks for read attributes separately, which can reject a deliberately write-only ACL. The suite changes DOS read-only attributes on files the test user owns; that is not an ACL test.

   **Fix:** request only access needed for the selected data mode and metadata required by its documented operations. A later `os_file_set_mode` may fail ACCESS when the handle lacks that right, or must obtain an additional identity-checked handle when the caller is authorized. Do not make permission-changing capability a precondition for reading. Test real ACLs with readable/no-WRITE_ATTRIBUTES and write-data-only grants.

9. **`src/os/os_win.c:507` — MAJOR — The reparse retry loop never refreshes the first observation and hides metadata errors.**

   On identity mismatch, all three tries reopen only `second`; the first handle and its tag remain fixed. A single persistent replacement therefore cannot be retried against the new name. Both identity-query failures are collapsed into BUSY after the loop, hiding I/O or permission failures. There is also a NOFOLLOW hole: replace the original data-tag name with a symlink pointing to that same original object, and the filtered reopen can have the same identity while following a surrogate the API promised to reject.

   **Fix:** retry the entire non-destructive inspection/open/validation transaction, refreshing the first handle and tag. Distinguish identity mismatch from an actual metadata failure and preserve the latter's native error. Fence the final-name surrogate check as well as target identity; where the two-open recipe cannot close that race, state the limitation rather than claiming full NOFOLLOW safety. Test a one-time replacement, a provider metadata failure, and substitution by a link to the original object.

10. **`src/os/os_win.c:451` — MAJOR — Identity encoding can change after an unrelated transient failure.**

    Any failure of `FileIdInfo`, including network I/O/resource/access failures, triggers a fallback to a different identity representation: a 32-bit volume serial and 64-bit index. An earlier successful query used a 64-bit volume serial and 128-bit ID. The same object can thus compare different, and on ReFS the fallback ID is not guaranteed unique. Network APIs can also return partial information, so a successful fallback is not proof that a useful identity exists. See Microsoft's [file-ID guarantees](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/ns-fileapi-by_handle_file_information) and [network caveat](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-getfileinformationbyhandle).

    **Fix:** fall back only for a demonstrated unsupported information class on an eligible filesystem, choose one stable representation per volume, and propagate transient failures. Reject unavailable/non-unique identity rather than manufacturing equality from partial metadata. Test an injected FileIdInfo failure after a successful query, ReFS IDs differing in their upper half, and a redirector that lacks usable IDs.

11. **`src/os/os_win.c:834` — MAJOR — Directory locks report success outside the scope where the identity-keyed recipe excludes other opens.**

    On FAT a rename can change the fallback index, so an already-locked directory can acquire a new lock-file name and be locked again. The decision doc acknowledges this limitation, but this function does not enforce its assumed local NTFS profile scope. A directory on a network share also gets a lock file on each client's LOCALAPPDATA; two machines lock different local files and both succeed. Different user profiles have the analogous scope mismatch. Changing metadata-query formats as in finding 10 introduces another split.

    **Fix:** enforce the recipe's eligibility conditions and return NOTSUP where directory-object exclusion cannot be provided, or extend the design with a shared lock authority and stable identity namespace. Keep ordinary data-file access independent of directory-lock eligibility. Test a FAT rename to a longer name and the same network directory from two clients. This is a limitation of the adopted recipe, not a reason to move back to a replaceable lock entry inside the directory.

12. **`src/os/os_win.c:609` — MAJOR — Anonymous-file creation can delete a different file.**

    The creation handle shares DELETE, so another actor can rename `.anon-...` and replace its directory entry before the second open. The code sets disposition on that second object without comparing identities. It can delete the replacement and return a data handle whose original file still has a name. The failure cleanup's `os_unlink_at(dir, name)` has the same unverified-name problem. A random name reduces accidental collisions; it does not pin the directory entry.

    **Fix:** compare the data and deletion handles' identities before disposition and never delete a name that no longer belongs to this attempt. Retain object-based cleanup authority through all failure paths; on mismatch close safely and retry/fail BUSY. Add a hook between the two opens and replace the name; the replacement must survive and success must never expose a named “anonymous” file.

13. **`src/os/os_win.c:618` — MINOR — Unsupported anonymous-file disposition leaves a permanent temporary entry.**

    When POSIX disposition is unsupported, cleanup calls `os_unlink_at`, which uses the same unsupported disposition class/flags. Its error is ignored. Each attempted anonymous open can therefore leave `.anon-...` behind while returning NOTSUP, including on precisely the volumes where the caller is meant to fall back.

    **Fix:** arrange an internal object-based conventional delete-on-close cleanup for this failed creation after closing other handles, without weakening the successful public unlink semantics. Check cleanup results and retain the relevant native error. Test a filesystem/API shim rejecting POSIX disposition and verify an unchanged directory listing after failure.

14. **`src/os/os_win.c:739` — MAJOR — Failed stdio conversion consumes and closes the caller's file.**

    After `_open_osfhandle` succeeds, `_fdopen` failure closes the descriptor and underlying handle and marks `f` closed. The frozen contract explicitly says that on error `f` is unchanged and remains open. This matters for allocation/stream-table exhaustion with a valid mode, not just malformed input. The failure is also always labeled INVAL even if resources ran out. The CRT's [ownership transfer is explicit](https://learn.microsoft.com/en-us/cpp/c-runtime-library/reference/open-osfhandle?view=msvc-170).

    **Fix:** first duplicate the OS handle non-inheritable, convert that duplicate, and close the original only after the FILE is successfully created. On failure close only the duplicate, leave `f` intact, and map the CRT's actual error. Inject `_fdopen` failure and verify subsequent I/O and lock lifetime on the original.

15. **`src/os/os_win.c:648` — MAJOR — The promised pending-I/O fallback does not preserve operation lifetime.**

    Sequential read/write pass `ov == NULL`, yet an ERROR_IO_PENDING goes to `GetOverlappedResult(h, NULL, ...)`, which has no operation state to reap. Positioned calls use no distinct event and return immediately after any GetOverlappedResult failure; they do not establish that an incomplete operation was canceled and reaped. With multiple pending requests on one handle, a handle-wide wake is not enough to identify this operation's completion. The analogous ERROR_IO_PENDING branch in `lock_handle` returns BUSY with a stack OVERLAPPED and could abandon a subsequently granted lock. Normally synchronous handles avoid these paths, but os.h explicitly promises correct behavior even if pending is observed. Microsoft's [completion documentation](https://learn.microsoft.com/en-us/windows/win32/api/ioapiset/nf-ioapiset-getoverlappedresult) requires the originating OVERLAPPED and warns about handle-wide signaling.

    **Fix:** either establish and document the synchronous NT completion guarantee for every supported provider and remove the impossible fallback promise, or maintain valid per-operation completion state and cancel/reap on every exceptional exit. Never submit a second write as the recovery mechanism for pwrite_once. Add mocked/native pending-completion tests for sequential and concurrent positioned transfers, failure while waiting, and a lock request; assert no buffer/OVERLAPPED access or late lock acquisition after return.

16. **`src/os/os_win.c:1194` — MAJOR — The mapping acquisition shrink race escapes as ACCESS/IO rather than RANGE.**

    The bounds check precedes section creation. A writer can shrink the file after GetFileSizeEx but before CreateFileMappingW establishes a section. Shrink-to-zero then fails section creation; shrink-to-a-shorter-nonempty-file can fail MapViewOfFile3. Both errors go through generic mapping instead of the documented OS_E_RANGE “source changed” outcome. The test checks a file already shorter before calling, not this interleaving.

    **Fix:** validate the acquired section's bounds, or recheck the same file handle after an acquisition failure and translate a proven missing range to RANGE while preserving genuine permissions/I/O failures. A section-creation/bounds-query strategy must also handle zero length. Add deterministic hooks for shrink between size query and section creation, and between section creation and view mapping, including a nonzero offset.

17. **`src/os/utf8.manifest:9` — MAJOR — No enforced minimum Windows version protects the Unicode contract.**

    The manifest's activeCodePage setting is ignored before Windows 10 1903, so narrow argv can be lossy even though this implementation reports success. Placeholder APIs need 1803; extended rename/disposition classes need 1709, and individual flags need capability checking. Export probing covers only mappings; it does not make the whole layer conform on an earlier build. “Confirmed on 19045” is not a supported-version policy. Microsoft documents the [1903 code-page boundary](https://learn.microsoft.com/en-us/windows/apps/design/globalizing/use-utf8-code-page).

    **Fix:** declare and enforce an adequate minimum build at startup, or implement a wide-argv bridge and explicit capability handling for older systems. Translate genuinely unsupported classes/flags to NOTSUP at the operation boundary; do not silently fall back to rename/delete semantics that violate the header. Test at the declared minimum and immediately below each feature boundary, especially non-ASCII argv before 1903.

18. **`src/os/os_win.c:205` — MAJOR — Path limits are checked in UTF-16 units instead of UTF-8 bytes.**

    `WCHAR[OS_PATH_MAX]` allows substantially more than OS_PATH_MAX UTF-8 bytes. For example, 1,400 three-byte BMP characters already need 4,200 bytes before separators (P: 1,400 × 3), yet fit this wide buffer. Absolute expansion can also make a short relative input exceed the byte limit. The same mistake affects `rel_name` and output helpers. os.h explicitly requires NAMETOOLONG when the UTF-8 spelling exceeds OS_PATH_MAX/OS_NAME_MAX, independently of native limits.

    **Fix:** check input bytes, normalized absolute-path bytes, and relevant returned path/name bytes with NUL accounting. Keep output-capacity RANGE separate from the contract's NAMETOOLONG. Validate/clamp size_t-to-int conversion capacities and mapping range/page-rounding arithmetic as well; `INT64_MAX - len` underflows when len itself exceeds INT64_MAX. Test boundary-length Unicode paths and names and relative expansion against a long Unicode cwd.

19. **`src/os/os_win.c:209` — MAJOR — Full-path normalization can redirect an operation to another file.**

    Path-form calls normalize with GetFullPathNameW, but *_at calls pass literal NT names. A file created/listed as `note.` or `note ` by the latter (or by WSL) can be opened/unlinked/renamed via a full path that normalizes to `note`. If both exist, the operation targets the wrong object. Removing the long-path prefix in canonical output also makes the returned spelling pass through this normalization again. This is particularly relevant to `\\wsl.localhost` names. Microsoft describes [the trimming performed during normalization](https://learn.microsoft.com/en-us/dotnet/standard/io/file-path-formats).

    **Fix:** preserve literal final components while resolving relative roots and separators into the extended namespace, and make path-form and *_at resolution consistent. If a name cannot be represented safely by the API, fail explicitly rather than aliasing it. Test distinct `note`, `note.`, and `note ` through listing, canonicalization, read, unlink, and rename on NTFS and a WSL share.

20. **`src/os/os_win.c:1516` — MAJOR — Valid long argv elements are rejected, while total command-line sizing is unchecked.**

    Every argument, including non-path data, is converted into a WCHAR[OS_PATH_MAX]. A single argument larger than that buffer is rejected even when the complete quoted command line fits Windows' limit. Conversely, `units += ...` and `units * sizeof(WCHAR)` are unchecked and no quoted-total limit is enforced before allocation. The executable path also bypasses the path conversion used elsewhere, so long-path/relative/UNC launch behavior needs its own guarantee. [CreateProcessW permits a command line of 32,767 UTF-16 units including NUL](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-createprocessw).

    **Fix:** measure UTF-16 lengths and quoting expansion without an OS_PATH_MAX per-argument restriction, perform checked total arithmetic, and allocate one bounded command-line block. Keep the executable's normalized native path separate from byte-preserved argv[0]. Test one argument longer than OS_PATH_MAX, a near-limit heavily quoted argument, quoted trailing backslashes, and re-execution from long Unicode and UNC paths.

21. **`src/os/os_win.c:1538` — MAJOR — Spawn failure handling can corrupt cleanup or silently discard standard streams.**

    DuplicateHandle failure is ignored, so spawning may succeed with a NULL standard handle instead of the inherited stream the contract promises. Separately, `attrs` is deleted whenever malloc returned a pointer, even if InitializeProcThreadAttributeList failed and the memory never became an initialized attribute list. Resource exhaustion is exactly when these paths matter.

    **Fix:** track successful initialization separately from allocation, delete only initialized lists, and make failure to duplicate a valid standard handle fail the spawn with its original error. Treat an originally absent stream as a separate supported case. Inject failure at each duplicate, allocation, attribute initialization/update, and CreateProcess call; verify no leaks, invalid cleanup calls, or successful launches with silently missing streams.

22. **`src/os/os_win.c:1578` — MINOR — A kill attempt relabels an already normally exited child as abnormal.**

    `killed` is set before TerminateProcess succeeds. If the child already exited normally, TerminateProcess returns ACCESS_DENIED, which is treated as success, and wait changes its normal exit status to ABNORMAL. Other termination failures also leave the flag set. The [documented already-terminated result](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-terminateprocess) is not evidence that this caller killed the process.

    **Fix:** set the flag only after an accepted termination request. On ACCESS_DENIED, verify the process is actually signaled and preserve its existing classification; propagate a real denial. Test a normal exit racing kill and an injected failed termination.

23. **`src/os/os_win.c:1146` — MINOR — An existing empty environment value is reported as an error.**

    A successful retrieval of an empty value returns a length of zero; this branch interprets every zero result as failure. Depending on last-error state it returns IO or another unrelated code, instead of OS_OK and an empty string. The suite tests unset and nonempty values only.

    **Fix:** clear last error before GetEnvironmentVariableW, distinguish empty success from ENVVAR_NOT_FOUND and real errors, and return an empty string through the normal capacity checks. Test `os_env_set(name, "")`, cap zero/one, retrieval after a previous failed call, and subsequent unset.

24. **`src/os/os_win.c:1030` — MAJOR — Listing performs lossy UTF-16 conversion and can return another entry's name.**

    Unlike the other converter, this call omits WC_ERR_INVALID_CHARS. NT filenames can contain unpaired surrogate units, which are replaced during conversion. An entry containing such a unit and a real entry containing U+FFFD can therefore produce the same UTF-8 name; opening a returned name can select the wrong file. Conversion failures are also silently skipped, hiding entries without reporting any error.

    **Fix:** use strict conversion and return an explicit error for an unrepresentable name, or adopt a documented reversible encoding in a separate contract change. Do not replace or silently skip names in a file editor's enumeration API. Test an unpaired-surrogate filename beside an actual replacement-character filename, plus the maximum valid Unicode component.

25. **`src/os/os_win.c:1103` — MINOR — Known-folder failure can leak the returned COM allocation.**

    `w` is freed only on S_OK. The API says the caller must free a returned allocation even on failure. Repeated app-directory lookups, including directory-lock setup, can leak on that path. The HRESULT is also discarded before fallback, making the original failure unavailable for diagnosis. See [SHGetKnownFolderPath ownership](https://learn.microsoft.com/en-us/windows/win32/api/shlobj_core/nf-shlobj_core-shgetknownfolderpath).

    **Fix:** retain the HRESULT, free any returned pointer on every path, then apply the chosen environment fallback and preserve a meaningful failure if it also fails. Test a shim returning failure with an allocated string. No new C-heap allocation is needed for this fix.

26. **`src/os/os_win.c:303` — MINOR — Sleep overflow can return immediately or turn a finite sleep into infinity.**

    `os_now_ns() + ns` wraps for a valid uint64_t duration such as UINT64_MAX; the loop can then return without sleeping at all. Rounding additions can wrap too. In the fallback path, casting milliseconds directly to DWORD can produce INFINITE, so a finite request ceases to re-arm. Ordinary short sleeps use the intended re-arming loop correctly.

    **Fix:** use overflow-safe elapsed/remaining accounting or bounded chunks, round with quotient/remainder, and clamp each Sleep call below INFINITE. Apply the same safe deadline helper to wait_handle. Use clock/timer shims for extreme durations, early timer wakes, timer-creation/arming failure, and a fallback duration that rounds to INFINITE; no multi-day runtime test is required.

27. **`src/os/os_win.c:283` — MINOR — Four observed CPU-clock steps do not establish its smallest possible step.**

    Under preemption the sampling thread can miss updates and observe only multiples of the true granularity. Caching their minimum can overstate the resolution for the remainder of the process and violate the suite's own later-observation check. This becomes more likely on a loaded machine than on the reported run; simply making the cache atomic does not fix the measurement.

    **Fix:** use a documented conservative resolution bound or a calibration policy that cannot claim a larger minimum step than the clock actually has. Keep measured sample statistics separate from guaranteed granularity. Test skipped updates and later smaller deltas via a clock shim; do not fix this by making startup spin arbitrarily longer.

28. **`src/os/os_win.c:125` — MINOR — Error classification omits important filesystem cases and invents a loop diagnosis.**

    ERROR_CANT_ACCESS_FILE means a file could not be accessed, not necessarily a name-surrogate loop, yet it is unconditionally LOOP. ERROR_CANT_RESOLVE_FILENAME, disk-quota exhaustion, and unsupported information-level/class failures lack useful operation-specific handling. Additionally, `_beginthreadex` failure is interpreted through GetLastError even though the CRT exposes errno/_doserrno; stdio failures manufacture INVAL/NOMEM regardless of the actual failure. These gaps can send callers down the wrong changed/unsupported/space/permission path. See Microsoft's [file access/name errors](https://learn.microsoft.com/en-us/windows/win32/debug/system-error-codes--1700-3999-) and [quota code](https://learn.microsoft.com/en-us/openspecs/windows_protocols/ms-erref/18d8fbe8-a967-4f1c-ae50-99ca8e491d2d).

    **Fix:** extend the translation table with documented cases, classify ambiguous native statuses at the operation that has context, and translate CRT errors through an explicit helper. Preserve the actual native code through cleanup. Add table/failure-injection tests, including NOSPC for disk quota and NOTSUP for a rejected extended filesystem operation; do not globally turn all INVALID_PARAMETER failures into NOTSUP.

29. **`tools/win_reparse_fixture.sh:30` — MAJOR — The supplied fixture cannot validate the reparse implementation it is meant to cover.**

    The worker proved that this WOF file exposes neither a reparse attribute nor a tag on 19045. `case_reparse` only checks that it behaves like a plain file, so every reopen/identity/provider path can be broken while the case remains green. The gap is explicitly acknowledged in the decision record and backlog, but the 17/17 result is consequently not evidence for OneDrive/HSM safety.

    **Fix:** retain the WOF test as compression coverage, but add a verified visible non-surrogate-tag fixture and assert that the reopen branch actually ran. Include a dehydrated OneDrive/Cloud Files fixture, offline-provider failure, and the destructive-open/race cases above. Add real name-surrogate fixtures separately. If the host cannot supply them, report an explicit missing platform check rather than treating ordinary-file success as reparse coverage.

30. **`tests/os_test.c:1313` — MAJOR — The suite lacks the shared-view, exit, sharing-denial, and external-volume checks needed for these claims.**

    The concurrent installers each own a different view; registration capacity uses duplicate views without faulting them. Fault recovery is synthetic with the backing section still available, at offset zero, on full-page files. Retirement is not raced against an already pinned handler. The process test's sentinel verifies exclusion from inheritance, but no child deliberately retains a file without FILE_SHARE_DELETE while the parent tests rename/unlink. Empty-target directory replacement is documented but not exercised. Relative paths were skipped on the reported box because cwd was on WSL and scratch on C:.

    **Fix:** add Windows-specific supplemental tests while keeping the frozen suite intact: readers and multiple faults on one view; delayed AV dispatch; retire waiting for an admitted handler; nonzero-offset/partial-tail recovery; actual network disconnect/removable-volume backing loss with bounded completion; normal exit after recovery with active/retired slots under ASan; a separate sharing-denial child requiring BUSY and untouched names; empty-directory replacement; and a child started with a controlled cwd to force relative-path coverage. Run the native identity/lock and rename/delete checks on supported network and FAT/exFAT configurations, recording NOTSUP where guarantees cannot be met. Do not infer the behavior of these providers from NTFS's synthetic exception test.

31. **`tools/win_reparse_fixture.sh:14` — MINOR — The check target depends on an English Windows installation and a TEMP path without spaces.**

    The script deliberately rejects a valid TEMP path containing a blank and greps an English `compact` sentence for success. Either condition makes `make TARGET=win check` fail before the portable suite runs. Intermediate creation errors are suppressed, further reducing diagnostics.

    **Fix:** use a helper with structured Unicode arguments and exit/result checks, or a correctly quoted PowerShell/C fixture helper, and inspect compression state through an API rather than localized text. Check each creation step and preserve its diagnostic. Exercise the helper with a spaced/non-ASCII TEMP directory and a non-English Windows UI.

32. **`tools/win_reparse_fixture.sh:22` — MINOR — Concurrent worktree checks recursively delete each other's fixture.**

    Every invocation creates/removes the same TEMP directory, beginning with an unconditional recursive deletion. Two checks can erase a fixture while the other suite is reading it; an unrelated preexisting directory of that name is also removed. The Makefile trap is installed only after creation succeeds, so a failed creation can leave partial state behind.

    **Fix:** allocate a unique per-run fixture directory, return its exact ownership token/path to the Makefile, install cleanup before mutation, and remove only that owned directory. Make `remove` take that path rather than recomputing a global one. Test concurrent checks and failure halfway through setup.

**Fix first, in order:**

1. Repair unmap ownership and delayed-AV recovery (1–2), then add deterministic shared-view and cleanup race tests.
2. Synchronize first-use caches and make registrations share recovery state (3–5); verify partial failures, retirement, and ASan exit paths.
3. Make reparse/truncation/anonymous deletion and failed stdio conversion preserve the right object and handle (6–14).
4. Close the acquisition, provider, Windows-version, path, and spawn contract gaps (15–24), with real platform fixtures (29–30).
5. Correct diagnostics, extreme clock cases, and fixture portability/isolation (25–28, 31–32).
