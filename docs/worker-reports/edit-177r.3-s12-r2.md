# edit-177r.3 — round-2 worker report

Verdict: done for the scoped fixes and runnable acceptance; the native file-symlink fixture is explicitly unavailable on this host. The coordinator can decide where to run that fixture and a real visible data-tag/provider fixture. No main-checkout changes, state-changing git commands, `bd`, package installs or Linux-side Windows-drive writes were made.

## Changes and files

- `src/os/os_win.c`: explicit per-view ownership and shared recovery state; delayed read-AV retry with owned/readable verification and access-kind filtering; zero-range recreation and failure tracking; synchronized immutable caches; safe NOFOLLOW truncation and directory-reparse classification; identity-safe anonymous-file deletion; literal final path components; strict listing conversion; mode-specific data rights and on-demand metadata handles; failure-preserving stdio conversion; mapping-shrink RANGE translation; complete spawn failure cleanup.
- `tests/win/os_win_test.c`: supervised Windows-only regression cases, deterministic scheduling and failure injection, NTFS scratch created/removed by Windows processes, explicit per-case verdicts and unavailable fixture reporting.
- `mk/win.mk`: explicit supplemental-suite link rules omit the normal `os_win.o` and compile the implementation inside the test TU with shims/hooks; ordinary binaries retain the unmodified production call paths. Existing Windows suite discovery/check rules run both variants.
- `docs/decisions/edit-177r.3.md`: appended Round 2 finding → change → test table and design/fixture notes.
- This report.

The frozen files and `src/os/os_posix.c` are unchanged (`git diff` empty for all). SHA-256 before/after (M, hashes are direct tool output):

```text
f5b030c815c8645c9e844b222396e2739eb993a35594ca2841bb1d76af019ad5  src/os/os.h
ad7e3628c9f0035076646090d26a711ef99dc70f83e1d8e9ecb55fd7ca3ae202  tests/os_test.c
```

## Red first

Commands, before implementation changes:

```sh
make TARGET=win build/win/rel/tests/os_win_test.exe  # exit 0
uptime
tools/winrun.sh build/win/rel/tests/os_win_test.exe  # exit 1
```

[AC, desktop Spiral], BAT0 absent. Initial red stamp (M, direct `uptime` output):

```text
09:25:36 up  1:42,  1 user,  load average: 0.86, 1.46, 2.04
[AC, desktop Spiral]
```

The first interop attempt inside the sandbox failed with `UtilBindVsockAnyPort: socket failed 1`; the recorded run below used the permitted sandbox escalation. Fixture setup was corrected before this run (native junction command-line quoting and the NTFS section-pins-EOF restriction). No implementation code had changed. Full red output, including every failed case:

```text
FAIL line 286: bi.State == MEM_COMMIT (native 0)
os_win_test unmap_ownership: FAIL
FAIL line 300: fault_handler(&ep) == EXCEPTION_CONTINUE_EXECUTION (native 0)
FAIL line 302: dispatch(&m, 0, EXCEPTION_IN_PAGE_ERROR, 1) == EXCEPTION_CONTINUE_SEARCH (native 0)
FAIL line 304: *(const volatile unsigned char *)m.addr == 0x5a (native 0)
os_win_test delayed_read_av: FAIL
FAIL line 310: g && g % os_page_size() == 0 (native 0)
FAIL line 321: WaitForSingleObject(ts.done, 100u) == WAIT_TIMEOUT (native 0)
os_win_test cold_caches: FAIL
FAIL line 331: os_fault_epoch(b) == 1 (native 0)
FAIL line 333: os_fault_epoch(b) == 2 (native 0)
os_win_test duplicate_epochs: FAIL
FAIL line 345: *(const volatile unsigned char *)p == 0 (native 0)
os_win_test zero_suffix_fault: FAIL
os_win_test split_stage_failures: PASS
os_win_test same_view_readers: PASS
FAIL line 379: entered == WAIT_OBJECT_0 (native 0)
os_win_test retire_admitted: FAIL
FAIL line 403: ts.destructive == 0 (native 681)
FAIL line 404: st.size == 4 (native 681)
FAIL line 407: ts.destructive == 0 (native 681)
os_win_test nofollow_truncation: FAIL
Junction created for C:\Users\tobia\AppData\Local\Temp\sublimite-test-r2-205052\junction <<===>> C:\Users\tobia\AppData\Local\Temp\sublimite-test-r2-205052\target
FAIL line 519: st.kind == OS_KIND_LINK (native 0)
FAIL line 520: os_file_open(&f, link, OS_OPEN_READ | OS_OPEN_NOFOLLOW) == OS_E_LOOP (native 5)
FAIL line 521: os_file_open(&f, link, OS_OPEN_WRITE | OS_OPEN_CREATE | OS_OPEN_EXCL | OS_OPEN_NOFOLLOW) == OS_E_EXIST (native 5)
FAIL line 522: os_rmdir(link) == OS_E_NOTDIR (native 5)
FAIL line 522: (os_unlink(link)) == OS_OK (native 2)
os_win_test junction: FAIL
fixture unavailable: file symlink native 1314 (requires Developer Mode/elevation)
os_win_test file_symlink: PASS
FAIL line 414: os_unlink_at(&ts.dir, "tagged") == OS_E_ACCESS (native 5)
os_win_test tag_query_error: FAIL
FAIL line 420: os_file_open_anon(&f, &ts.dir) == OS_E_BUSY (native 0)
FAIL line 422: (os_stat_at(&os_win_test_state.dir, name, 0, &st)) == OS_OK (native 2)
FAIL line 422: !os_file_is_open(&f) (native 2)
os_win_test anon_replacement: FAIL
FAIL line 435: (os_file_open(&f, p, OS_OPEN_READ)) == OS_OK (native 2)
FAIL line 436: (os_file_read(&f, data, sizeof data - 1, &got)) == OS_OK (native 6)
FAIL line 436: !strcmp(data, names[i]) (native 6)
FAIL line 438: (os_rename(p, q, OS_RENAME_NOREPLACE)) == OS_OK (native 2)
FAIL line 439: (os_stat_at(&os_win_test_state.dir, renamed, 0, &st)) == OS_OK (native 2)
FAIL line 439: st.size == strlen(names[i]) (native 2)
FAIL line 440: (os_unlink(q)) == OS_OK (native 2)
FAIL line 435: (os_file_open(&f, p, OS_OPEN_READ)) == OS_OK (native 2)
FAIL line 436: (os_file_read(&f, data, sizeof data - 1, &got)) == OS_OK (native 6)
FAIL line 436: !strcmp(data, names[i]) (native 6)
FAIL line 438: (os_rename(p, q, OS_RENAME_NOREPLACE)) == OS_OK (native 2)
FAIL line 439: (os_stat_at(&os_win_test_state.dir, renamed, 0, &st)) == OS_OK (native 2)
FAIL line 439: st.size == strlen(names[i]) (native 2)
FAIL line 440: (os_unlink(q)) == OS_OK (native 2)
os_win_test literal_names: FAIL
FAIL line 455: invalid == 1 (native 18)
FAIL line 455: rep == 1 (native 18)
os_win_test strict_listing: FAIL
FAIL line 464: os_file_open_at(&f, &ts.dir, "acl", OS_OPEN_READ) == OS_OK (native 5)
os_win_test read_acl: FAIL
FAIL line 474: os_file_to_stdio(&f, "r+b", &fp) == OS_E_NOMEM (native 0)
FAIL line 475: os_file_is_open(&f) (native 0)
FAIL line 476: os_file_pwrite(&f, "z", 1, 0, &done) == OS_OK (native 6)
FAIL line 476: done == 1 (native 6)
FAIL line 477: os_file_try_lock(&other, OS_LOCK_EXCLUSIVE) == OS_E_BUSY (native 6)
FAIL line 478: (os_file_try_lock(&other, OS_LOCK_EXCLUSIVE)) == OS_OK (native 33)
os_win_test stdio_failure: FAIL
FAIL line 485: os_map_file(&m, &f, g, g) == OS_E_RANGE (native 1006)
FAIL line 485: os_map_file(&m, &f, g, g) == OS_E_RANGE (native 5)
os_win_test map_shrink: FAIL
FAIL line 498: rc == (stage <= 4 ? OS_E_NOMEM : stage == 5 ? OS_E_ACCESS : OS_E_NOENT) (native 0)
FAIL line 501: after == before (native 0)
FAIL line 498: rc == (stage <= 4 ? OS_E_NOMEM : stage == 5 ? OS_E_ACCESS : OS_E_NOENT) (native 0)
FAIL line 498: rc == (stage <= 4 ? OS_E_NOMEM : stage == 5 ? OS_E_ACCESS : OS_E_NOENT) (native 0)
FAIL line 499: ts.bad_delete == 0 (native 8)
os_win_test spawn_failures: FAIL
os_win_test: 3/19 passed
exit 1
```

At this initial stage the fixture-unavailable case printed PASS after its diagnostic; the final driver explicitly prints UNAVAILABLE and excludes it from the pass count. The existing split-failure cleanup and same-view synthetic-fault controls were already green; those are regression coverage, not claimed newly reproduced bugs. The admission-retirement red assertion demonstrates the absence of an admission scheduling hook, not a claim that the original retirement algorithm violated its existing pin guarantee.

The delayed-dispatch case was subsequently strengthened to pause a real native read AV before OS dispatch, finish the split, and then release dispatch. Additional ordinary readers, duplicate-registration retirement overlap, cold maps, private-zero failure sites and spawn failure sites were added. These strengthened tests were also run against the original source: `patch --reverse --output=build/r2/os_win_round1.c src/os/os_win.c build/r2/implementation.patch` reconstructed the round-1 file from the worktree-only `git diff`, and a second reconstruction after all edits compared byte-identical with `cmp`. Original source SHA-256 (M): `7fe5abdef4e720f22b99fac64c5e201602ff9b1efccf26b9354632e4fe43eb5f`. This is a test source selection, never a checkout/reset or a change to the live implementation.

The baseline compile uses the same warning/release/link flags as the supplemental Makefile rule, with `-DOS_WIN_TEST_SOURCE='"<this-worktree>/build/r2/os_win_round1.c"'` and output `build/r2/os_win_baseline.exe`. Compiler diagnostics are in `build/r2/baseline-build.log` (empty on success); the compile command was the following, with this worktree as cwd:

```sh
~/opt/llvm-mingw/bin/x86_64-w64-mingw32-clang \
  -std=c11 -Wall -Wextra -Werror -Wshadow -Wconversion -O2 -g -msse2 -Isrc \
  -DOS_WIN_TEST_SOURCE='"/home/tobiasosborne/Projects/sublimite/.wt/edit-177r.3-s12/build/r2/os_win_round1.c"' \
  tests/win/os_win_test.c build/win/rel/src/os/os_posix.o \
  build/win/rel/src/base/malloc_guard.o build/win/rel/src/base/malloc_guard_win.o \
  build/win/utf8_manifest.o -lntdll -lbcrypt -lshell32 -lole32 \
  -Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=free -static \
  -o build/r2/os_win_baseline.exe
```

 Baseline run: `tools/winrun.sh build/r2/os_win_baseline.exe`, exit 1; final strengthened output is recorded below.

Final strengthened baseline run (M), [AC, desktop Spiral], exit 1. The pass count is the printed one-line PASS verdict total; unavailable is reported separately. The lone PASS here is the already-working reader stress control, not evidence of a newly fixed defect.

```text
09:38:42 up  1:55,  1 user,  load average: 0.12, 0.43, 1.10
[AC, desktop Spiral]
FAIL line 315: bi.State == MEM_COMMIT (native 0)
os_win_test unmap_ownership: FAIL
os_win_test delayed_read_av: FAIL
FAIL line 370: g && g % os_page_size() == 0 (native 0)
FAIL line 370: g && g % os_page_size() == 0 (native 0)
FAIL line 370: g && g % os_page_size() == 0 (native 0)
FAIL line 370: g && g % os_page_size() == 0 (native 0)
FAIL line 392: WaitForSingleObject(ts.done, 100u) == WAIT_TIMEOUT (native 0)
os_win_test cold_caches: FAIL
FAIL line 406: os_fault_epoch(b) == 1 (native 0)
FAIL line 412: entered == WAIT_OBJECT_0 (native 0)
FAIL line 417: os_fault_epoch(b) == 3 (native 0)
os_win_test duplicate_epochs: FAIL
FAIL line 430: *(const volatile unsigned char *)p == 0 (native 0)
os_win_test zero_suffix_fault: FAIL
FAIL line 446: dispatch(&m, off, EXCEPTION_IN_PAGE_ERROR, 0) == EXCEPTION_CONTINUE_SEARCH (native 0)
FAIL line 447: ts.split_fail == 0 (native 0)
FAIL line 446: dispatch(&m, off, EXCEPTION_IN_PAGE_ERROR, 0) == EXCEPTION_CONTINUE_SEARCH (native 0)
FAIL line 447: ts.split_fail == 0 (native 0)
FAIL line 446: dispatch(&m, off, EXCEPTION_IN_PAGE_ERROR, 0) == EXCEPTION_CONTINUE_SEARCH (native 0)
FAIL line 447: ts.split_fail == 0 (native 0)
os_win_test split_stage_failures: FAIL
os_win_test same_view_readers: PASS
FAIL line 468: entered == WAIT_OBJECT_0 (native 0)
os_win_test retire_admitted: FAIL
FAIL line 507: ts.destructive == 0 (native 681)
FAIL line 509: ts.destructive == 0 (native 681)
FAIL line 510: st.size == 4 (native 681)
FAIL line 513: ts.destructive == 0 (native 681)
os_win_test nofollow_truncation: FAIL
Junction created for C:\Users\tobia\AppData\Local\Temp\sublimite-test-r2-40384\junction <<===>> C:\Users\tobia\AppData\Local\Temp\sublimite-test-r2-40384\target
FAIL line 646: st.kind == OS_KIND_LINK (native 0)
FAIL line 647: os_file_open(&f, link, OS_OPEN_READ | OS_OPEN_NOFOLLOW) == OS_E_LOOP (native 5)
FAIL line 648: os_file_open(&f, link, OS_OPEN_WRITE | OS_OPEN_CREATE | OS_OPEN_EXCL | OS_OPEN_NOFOLLOW) == OS_E_EXIST (native 5)
FAIL line 649: os_rmdir(link) == OS_E_NOTDIR (native 5)
FAIL line 649: (os_unlink(link)) == OS_OK (native 2)
os_win_test junction: FAIL
fixture unavailable: file symlink native 1314 (requires Developer Mode/elevation)
os_win_test file_symlink: UNAVAILABLE (file symlink privilege)
FAIL line 520: os_unlink_at(&ts.dir, "tagged") == OS_E_ACCESS (native 5)
os_win_test tag_query_error: FAIL
FAIL line 526: os_file_open_anon(&f, &ts.dir) == OS_E_BUSY (native 0)
FAIL line 528: (os_stat_at(&os_win_test_state.dir, name, 0, &st)) == OS_OK (native 2)
FAIL line 528: !os_file_is_open(&f) (native 2)
FAIL line 529: os_stat_at(&ts.dir, "moved-anon", 0, &st) == OS_E_NOENT (native 2)
os_win_test anon_replacement: FAIL
FAIL line 544: (os_file_open(&relative, names[i], OS_OPEN_READ)) == OS_OK (native 2)
FAIL line 545: (os_file_stat(&relative, &rel)) == OS_OK (native 6)
FAIL line 546: os_identity_equal(&rel.id, &at.id) (native 6)
FAIL line 547: (os_file_open(&f, p, OS_OPEN_READ)) == OS_OK (native 2)
FAIL line 548: (os_file_read(&f, data, sizeof data - 1, &got)) == OS_OK (native 6)
FAIL line 548: !strcmp(data, names[i]) (native 6)
FAIL line 550: (os_rename(p, q, OS_RENAME_NOREPLACE)) == OS_OK (native 2)
FAIL line 551: (os_stat_at(&os_win_test_state.dir, renamed, 0, &st)) == OS_OK (native 2)
FAIL line 551: st.size == strlen(names[i]) (native 2)
FAIL line 552: (os_unlink(q)) == OS_OK (native 2)
FAIL line 544: (os_file_open(&relative, names[i], OS_OPEN_READ)) == OS_OK (native 2)
FAIL line 545: (os_file_stat(&relative, &rel)) == OS_OK (native 6)
FAIL line 546: os_identity_equal(&rel.id, &at.id) (native 6)
FAIL line 547: (os_file_open(&f, p, OS_OPEN_READ)) == OS_OK (native 2)
FAIL line 548: (os_file_read(&f, data, sizeof data - 1, &got)) == OS_OK (native 6)
FAIL line 548: !strcmp(data, names[i]) (native 6)
FAIL line 550: (os_rename(p, q, OS_RENAME_NOREPLACE)) == OS_OK (native 2)
FAIL line 551: (os_stat_at(&os_win_test_state.dir, renamed, 0, &st)) == OS_OK (native 2)
FAIL line 551: st.size == strlen(names[i]) (native 2)
FAIL line 552: (os_unlink(q)) == OS_OK (native 2)
os_win_test literal_names: FAIL
FAIL line 568: invalid == 1 (native 18)
FAIL line 568: rep == 1 (native 18)
os_win_test strict_listing: FAIL
FAIL line 577: os_file_open_at(&f, &ts.dir, "acl", OS_OPEN_READ) == OS_OK (native 5)
os_win_test read_acl: FAIL
FAIL line 595: os_file_to_stdio(&f, "r+b", &fp) == OS_E_NOMEM (native 0)
FAIL line 596: os_file_is_open(&f) (native 0)
FAIL line 597: os_file_pwrite(&f, "z", 1, 0, &done) == OS_OK (native 6)
FAIL line 597: done == 1 (native 6)
FAIL line 598: os_file_try_lock(&other, OS_LOCK_EXCLUSIVE) == OS_E_BUSY (native 6)
FAIL line 599: (os_file_try_lock(&other, OS_LOCK_EXCLUSIVE)) == OS_OK (native 33)
os_win_test stdio_failure: FAIL
FAIL line 606: os_map_file(&m, &f, g, g) == OS_E_RANGE (native 1006)
FAIL line 606: os_map_file(&m, &f, g, g) == OS_E_RANGE (native 5)
os_win_test map_shrink: FAIL
FAIL line 625: rc == (stage <= 4 || stage >= 7 ? OS_E_NOMEM : stage == 5 ? OS_E_ACCESS : OS_E_NOENT) (native 0)
FAIL line 628: after == before (native 0)
FAIL line 625: rc == (stage <= 4 || stage >= 7 ? OS_E_NOMEM : stage == 5 ? OS_E_ACCESS : OS_E_NOENT) (native 0)
FAIL line 625: rc == (stage <= 4 || stage >= 7 ? OS_E_NOMEM : stage == 5 ? OS_E_ACCESS : OS_E_NOENT) (native 0)
FAIL line 626: ts.bad_delete == 0 (native 8)
FAIL line 625: rc == (stage <= 4 || stage >= 7 ? OS_E_NOMEM : stage == 5 ? OS_E_ACCESS : OS_E_NOENT) (native 122)
FAIL line 626: ts.bad_delete == 0 (native 122)
os_win_test spawn_failures: FAIL
os_win_test: 1/19 passed, 1 unavailable
exit 1
```

An additional final junction precedence assertion covered CREATE|EXCL without NOFOLLOW. This exposed one remaining gap in the intermediate implementation; the test was run red before its fix:

```sh
tools/winrun.sh build/win/rel/tests/os_win_test.exe --case junction
```

```text
Junction created for C:\Users\tobia\AppData\Local\Temp\sublimite-test-r2-18132\junction <<===>> C:\Users\tobia\AppData\Local\Temp\sublimite-test-r2-18132\target
FAIL line 649: os_file_open(&f, link, OS_OPEN_WRITE | OS_OPEN_CREATE | OS_OPEN_EXCL) == OS_E_EXIST (native 5)
exit 1
```

Exclusive creation now inspects the final name itself, with no directory-only type constraint, regardless of whether subsequent opens ordinarily follow links. Same command after the fix:

```text
Junction created for C:\Users\tobia\AppData\Local\Temp\sublimite-test-r2-212564\junction <<===>> C:\Users\tobia\AppData\Local\Temp\sublimite-test-r2-212564\target
exit 0
```

## Green and acceptance

`make TARGET=win check`: exit 0 (M). All required runnable cases pass. Full log: `build/r2/acceptance.log`; build output is in that log too. [AC, desktop Spiral], final Windows run start (M):

```text
09:43:15 up  2:00,  1 user,  load average: 0.22, 0.29, 0.86
[AC, desktop Spiral]
```

Exact summary lines, associated with their executables (M, totals derive from the printed verdicts):

```text
== build/win/rel/tests/os_win_test.exe
os_win_test: 18/19 passed, 1 unavailable
== build/win/rel/tests/smoke_test.exe
smoke ok (all checks passed)
== build/win/rel/tests/os_test.exe
os_test: 17 cases, 0 failed
== build/win/san/tests/os_win_test.exe
os_win_test: 18/19 passed, 1 unavailable
== build/win/san/tests/smoke_test.exe
smoke ok (all checks passed)
== build/win/san/tests/os_test.exe
os_test: 17 cases, 0 failed
== build/win/san/tests/smoke_test.exe --expect-asan (must fail)
ASan fired: ok
make TARGET=win check exit 0
```

The supplemental total derives from its case table: eighteen PASS verdicts plus one explicit unavailable native file-symlink fixture equals nineteen declared cases (M). The unavailable fixture is allowed by the brief's host-capability limitation and is not counted as a pass. Each frozen-suite run reports seventeen cases with no failures (M); unchanged frozen expectations are the acceptance gate (G). The sanitizer's unregistered-fault child intentionally prints an ASan in-page-error report; the parent verifies that it died and passes. The smoke overflow is also an intentional negative control, verified by `ASan fired: ok`.

Linux commands and results (M), [AC, desktop Spiral]:

```text
09:42:43 up  1:59,  1 user,  load average: 0.30, 0.31, 0.89
[AC, desktop Spiral]
make -j64 all: exit 0
09:43:45 up  2:00,  1 user,  load average: 0.13, 0.26, 0.83
[AC, desktop Spiral]
build/tests/os_test: exit 0
os_test: 17 cases, 0 failed
09:43:48 up  2:00,  1 user,  load average: 0.12, 0.26, 0.83
[AC, desktop Spiral]
make -j64 build/san/tests/os_test: exit 0
build/san/tests/os_test: exit 0
os_test: 17 cases, 0 failed
```

Linux logs: `build/r2/linux-all-final.log`, `build/r2/linux-san-build-final.log`, `build/r2/linux-os-rel.log`, `build/r2/linux-os-san.log`. Release uses the repository's gcc build; sanitizer uses its clang/ASan/UBSan configuration. LeakSanitizer remained enabled; the final sanitizer executable was run with the sandbox restriction lifted. `git diff --check` is clean.

The cold-publication test's observation window is an estimate (E): the existing CPU calibration observes four default scheduler steps, `4 × 15.625 ms = 62.5 ms` (P, existing implementation recipe); the window rounds above that to `100 ms`. It only checks that a caller cannot finish while publication is deliberately paused; no performance budget is asserted. Positive event waits and process guards reuse the frozen suite's estimated hang guards.


## Design choices and limits

- Duplicate registrations share a descriptor, owned pieces, epoch and serialization. Registration admission/retirement pins remain separate; the frozen capacity case still fills `OS_FAULT_SLOTS` registrations. Ownership survives retirement until unmap. The private `os_map.section` field points to the descriptor, which owns the real section handle.
- The descriptor uses native `VirtualAlloc`, not the CRT heap or another mutable global. Per mapping it reserves native allocation granularity and commits `round_page(sizeof(map_owner))` storage (P: native VM rounding). The prefix/suffix topology is retained through every failed step and unmap never queries released addresses.
- NOFOLLOW truncates once through the verified data handle after non-destructive tag inspection/filter reopen. This preserves atomic EOF mutation of that object, with no destructive bypass open. It does not make the surrounding namespace validation transaction atomic; finding 9 remains out of scope.
- Plain read/write opens request mode-specific rights. NOFOLLOW needs READ_ATTRIBUTES for tag inspection. `set_mode` takes an additional identity-checked `ReOpenFile` metadata handle; permission denial returns ACCESS. `file_stat` obtains READ_ATTRIBUTES on demand after an ordinary write-only handle gets ACCESS. The latter was necessary to preserve the frozen `files` case: an intermediate full check reported write-only `os_file_stat` ACCESS, then passed after the metadata-handle fix.
- The real file-symlink fixture cannot be created: native error 1314 (M, ERROR_PRIVILEGE_NOT_HELD). It is reported UNAVAILABLE, not silently counted as passed. Junctions are native `mklink /J` fixtures. Surrogate and data-tag shims test the open ordering independently; they cannot prove a cloud/HSM provider's behavior. The existing compact/WOF fixture still has no visible tag on this host.
- Native NTFS prevents shrink once a section exists. Shrink before section creation is native; the section-to-view case closes the test section, shrinks the file and supplies a smaller real section so that real `MapViewOfFile3` fails. Both use a nonzero granularity-aligned offset. An unchanged-EOF failure control retains NOMEM.
- The supplemental suite's hook/shim code exists only in its TU; production hooks expand away. Its uninstrumented test VEH intercepts only the designated native read AV and permits ASan's own first handler to service shadow-memory faults.
- No performance benchmark was claimed. Reported run counts are (M), derived by counting the printed case verdicts; expected suite/capacity outcomes are (G), specified by the brief/frozen suite. Failure-site counts derive from the enumerated native operations in the tests.

## Found, not fixed

- All explicitly excluded review findings remain outside this change: 9, 10, 11, 13, 15, 17, 18, 20, 22, 23, 25, 26, 27, 28, 29, 31, 32; external-volume, FAT/exFAT and network-disconnect portions of 30 were not exercised.
- Anonymous-file failure cleanup now has object authority, as finding 12 requires. It still uses the existing POSIX disposition class; the unsupported-volume cleanup limitation from finding 13 is not fixed.
- The existing global-symbol checker is ELF-oriented: GNU `nm` on COFF reports section symbols `.bss`/`.data` as mutable globals, producing a false failure. `llvm-nm --defined-only build/win/rel/src/os/os_win.o` shows exactly the permitted mutable symbols `os_fault_service` and `os_win_state` (M, direct symbol listing). The checker itself is outside this bead's scope.
- Sandboxed Linux LeakSanitizer initially failed its thread inspection with the ptrace restriction. The required suite passes with the sandbox lifted; sanitizers were not disabled.

## Coordinator decisions

No new public-contract decision is required. Decide whether/where to arrange Developer Mode/elevation for the native file-symlink fixture and a real visible cloud/HSM data-tag fixture. The per-view native VM descriptor and the explicit fixture limits are recorded in the decision document for review. The coordinator owns commit/tracking and any main-checkout handoff/worklog updates.
