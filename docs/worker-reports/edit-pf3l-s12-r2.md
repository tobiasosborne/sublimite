# edit-pf3l round 2 worker report

**Verdict: partial.** The save exposure/EFS/stream defects, identity and notification races, ordinary-save/cancellation/build regressions, and directory-substitution classification are addressed. Portable/native safety coverage is substantially expanded. Finding 8's mandatory-label part is fixed, but explicit audit SACLs are still neither preserved nor reliably detected/refused. This is a security-signoff blocker, not an accepted exclusion.

## Changes and scope

- Save temporaries are created exclusively, relative to the retained parent, with a protected owner-only descriptor. Supported source owner/group/DACL/label state is restored and read back **before any document write**, and again before replacement. No permissive creation interval is hidden by moving a later setter.
- EFS sources refuse with `ERROR_FILE_ENCRYPTED` before creation. Named streams are enumerated through the captured source object; additional streams, enumeration errors, or an insufficient enumeration buffer refuse before creation. Original bytes and streams remain intact.
- DACL restore uses the actual temporary handle and `NtSetSecurityObject`, without fresh UNPROTECTED parent inheritance. Owner/group/label updates request WRITE_OWNER only when a comparison shows a real change. Unsupported readable non-label resource/policy ACEs refuse. Full audit state remains the exception described below.
- Replacement identity is sampled through a retained non-writing attribute handle after the data writer closes, before the replacement callback. Failed final identity sampling retains the existing replaced-but-identity-unavailable result behavior.
- Directory requests have separate events and retired one-shot registered waits feeding the aggregate UI event. Native relative directory reopen retains object identity across pathname replacement. Parent identity polling/focus setup rebinds file and path members, including an interval with no directory at the pathname; polling stops only after the last member is gone.
- Windows temporary names are ASCII and independent of the basename. Linux byte-name behavior is retained. Acquisition uses a file-private single-attempt pread adapter so cancellation is visible between EINTR attempts. The Linux metadata-entry directory race returns CHANGED.
- Shared safety assertions run in the actual Linux and Windows file suites. A native white-box suite supplies deterministic Win32/API races and failures without production test callbacks. The Windows crash test writes multiple chunks and checks restrictive abandoned artifacts.
- The existing savectl fuzzer migration is retained. `all` does not build fuzzers; the explicit `fuzz` target was also built. The coordinator must include that already-dirty file in the commit.

Files changed: `src/file/file.c`, `file.h`, `file_internal.h`, `file_posix.c`, `file_win.c`; `tests/file_test.c`, `file_os_cases.inc`, `file_win_cases.inc`; new `tests/file_acquire_test.c` and `tests/win/file_win_test.c`; `Makefile`, `mk/win.mk`; the pre-existing `fuzz/savectl_fuzz.c` diff; `docs/decisions/edit-pf3l.md` (appended Round 2 table); this report.

`src/os/os.h`, HANDOFF.md and docs/worklog are unchanged. No bd, commit, branch, stash, checkout, or other state-changing git command was run. The only main-checkout source reads were the two authorized briefs. The pre-existing untracked `docs/worker-reports/edit-pf3l-review-final.md` was left untouched.

## Red / green evidence

Logs and temporary reconstructed sources are under `build/worker-logs/pf3l-r2/` in this worktree. Initial test build/interposition mistakes were corrected before counting semantic reds. WSL interop initially failed in the sandbox with `UtilBindVsockAnyPort: socket failed`; native runs below used the authorized sandbox escalation. The original native semantic reds are in `red-*.log`.

After strengthening fixtures, the finalized native regressions were also replayed against round-1 `file.c`/`file_win.c` copied into `build/worker-logs/pf3l-r2/round1/`. The original text was reconstructed by reversing **read-only git diff output on those copies**, without reverting the worktree or altering git state. The white-box test supports `FILE_R2_COMMON_SOURCE`, `FILE_R2_WIN_SOURCE` and `FILE_R2_ROUND1` for this replay; ordinary release flags/dependency objects were used. The resulting executable is `build/worker-logs/pf3l-r2/round1-tests.exe`.

Exact native red runner (each named invocation exited **1 (M)**, the shell's observed status):

```sh
for c in security efs streams descriptor rights label identity wake parent cache unicode; do
  tools/winrun.sh build/worker-logs/pf3l-r2/round1-tests.exe "$c"
done
```

Final replay excerpts (the assertion text is copied from `round1-red-<case>.log`; each case fails against round 1):

`security` — exit 1 (M):

```text
tests/win/file_win_test.c:206: FAIL !(rights & (FILE_READ_DATA | FILE_WRITE_DATA)) (native 203)
tests/win/file_win_test.c:216: FAIL intrusion == INVALID_HANDLE_VALUE && denied == ERROR_ACCESS_DENIED (native 0)
```

`efs` — exit 1 (M):

```text
tests/win/file_win_test.c:251: FAIL r2_save(path, r2_temp_step) == FILE_ERR_IO && r2.last_error == ERROR_FILE_ENCRYPTED (native 0)
tests/win/file_win_test.c:252: FAIL r2.created == 0 (native 0)
```

`streams` — exit 1 (M):

```text
tests/win/file_win_test.c:272: FAIL r2_save(path, r2_temp_step) == FILE_ERR_IO && r2.last_error == ERROR_NOT_SUPPORTED (native 0)
tests/win/file_win_test.c:273: FAIL r2.created == 0 (native 0)
```

`descriptor` — exit 1 (M):

```text
tests/win/file_win_test.c:293: FAIL r2_save(path, NULL) == FILE_OK && r2.unprotect == 1 (native 0)
tests/win/file_win_test.c:297: FAIL acl && next && acl->AclSize == next->AclSize && !memcmp(acl, next, acl->AclSize) (native 87)
```

`rights` — exit 1 (M):

```text
tests/win/file_win_test.c:322: FAIL r2_save(path, NULL) == FILE_OK (native 5)
tests/win/../file_win_cases.inc:39: FAIL os_file_stat(&source, &st) == OS_OK && st.size == n (native 5)
```

`label` — exit 1 (M):

```text
tests/win/file_win_test.c:333: FAIL label && label->AceCount == 1 (native 87)
tests/win/file_win_test.c:339: FAIL r2_save(path, r2_temp_step) == FILE_ERR_IO (native 0)
```

`identity` — exit 1 (M):

```text
tests/win/file_win_test.c:354: FAIL transaction_commit(&tx) == FILE_OK && r2.callback_closed (native 0)
tests/win/file_win_test.c:357: FAIL !file_id_diff(&id, &actual) (native 0)
```

`wake` — exit 1 (M):

```text
tests/win/file_win_test.c:372: FAIL ap->io.hEvent != bp->io.hEvent (native 0)
tests/win/file_win_test.c:373: FAIL WaitForSingleObject(file_watch_handle(), 5000) == WAIT_OBJECT_0 (native 0)
```

`parent` — exit 1 (M):

```text
tests/win/file_win_test.c:406: FAIL WaitForSingleObject(file_watch_handle(), 5000) == WAIT_OBJECT_0 (native 0)
tests/win/file_win_test.c:407: FAIL named.wd < 0 && f->iwd < 0 && file_watch_path_take(&named) (native 0)
```

`cache` — exit 1 (M):

```text
tests/win/file_win_test.c:429: FAIL os_identity_equal(&p->identity, &actual.id) && os_identity_equal(&old.id, &actual.id) (native 0)
```

`unicode` — exit 1 (M):

```text
tests/win/file_win_test.c:446: FAIL r2_save(path, NULL) == FILE_OK (native 0)
tests/win/../file_win_cases.inc:39: FAIL os_file_stat(&source, &st) == OS_OK && st.size == n (native 0)
```


The `security` fixture verifies both the descriptor supplied at atomic creation and an actual restricted-token read/write open before the first write. `descriptor` moves the source between native parents with different inheritable ACLs, retains an unprotected source ACL, and detects both an unwanted inheritance request and a byte-different result. `wake` covers both adding a watch after another completion and completion during another directory's rearm. `parent` includes both file/path members and a missing-directory interval. `identity` supplies a delayed-close timestamp sample and checks callback ordering.

Linux interrupted acquisition:

```sh
DISPLAY=:99 build/tests/file_acquire_test reads
```

Red, exit **1 (M)** (`red-reads.log`):

```text
file_acquire_test:67: FAIL attempts == 1
file_acquire_test:67: FAIL attempts == 1
file_acquire_test:68: FAIL (suffix ? f.open_result.status : f.prefix_result.status) == FILE_ERR_CANCELLED
file_acquire_test: prefix attempts=4 status=6
file_acquire_test: COPY suffix attempts=4 status=0
file_acquire_test: FAILED
```

Green, exit **0 (M)** (`green-acquire.log`):

```text
file_acquire_test: prefix attempts=1 status=6
file_acquire_test: COPY suffix attempts=1 status=6
file_acquire_test: directory substitution status=5
file_acquire_test: ok
```

These counts are syscall-shim observations: the red shim supplied three EINTRs, then a successful-read sentinel; cancellation was requested at the first attempt. The green path returns physically without the sentinel. Status 6 is CANCELLED; status 5 is CHANGED, from the file status enum.

Linux directory substitution:

```sh
DISPLAY=:99 build/tests/file_acquire_test substitute
```

Red, exit **1 (M)** (`red-substitute.log`):

```text
file_acquire_test:77: FAIL result == FILE_ERR_CHANGED
file_acquire_test: directory substitution status=1
file_acquire_test: FAILED
```

The green combined run above reports CHANGED. The shim substitutes a directory after metadata stat and immediately before metadata open; the old source is retained under another name.

Fuzzer API compile red, exit **1 (M)** (`red-fuzzer.log`):

```sh
clang -std=c11 -Wall -Wextra -Werror -Wshadow -Wconversion -D_GNU_SOURCE \
  -Isrc -Ivendor/tree-sitter/lib/include -fsyntax-only \
  build/worker-logs/pf3l-r2/round1/fuzz/savectl_fuzz.c
```

```text
savectl_fuzz.c:114:87: error: incompatible pointer types passing 'struct stat *' to parameter of type 'const os_stat *'
savectl_fuzz.c:230:95: error: incompatible pointer types passing 'struct stat *' to parameter of type 'const os_stat *'
```

The log also contains the other affected callers. The pre-existing worktree fix was preserved rather than temporarily reverted. Green: `make -j64 all fuzz`, exit **0 (M)**, `fuzz: 37 fuzzers built`. The count is make's enumeration of its fuzzer targets. A clean checkout of the coordinator's eventual commit still needs to include this diff; this worker cannot create that commit under the binding rules.

A strengthened parent-lifetime fixture caught a bug in the intermediate repair too. Before that repair, `parent` and `watch-errors` each exited **1 (M)** (`red-refresh-*.log`):

```text
FAIL file_watches.timer != NULL
FAIL WaitForSingleObject(file_watch_handle(), 5000) == WAIT_OBJECT_0
FAIL !file_watches.timer
```

After fixing lifetime ownership, both exit **0 (M)**: polling survives a missing parent, rebinds when it appears, and stops after the last invalidated member detaches.

Native green runner (all named cases exit **0 (M)** in the final release/sanitizer suite):

```sh
make TARGET=win check
```

Its `file_win_test` supervises the native cases as fresh processes. Added failure coverage includes short writes/repeated interruptions, a failed write, an unassignable captured owner, metadata query/set errors, stream-enumeration failure, overflow, failed rearm, and cleanup without replacing the original.

Portable safety cases are coverage ports of already-correct common semantics; they do **not** manufacture a red behavior change against round 1. They pass in Linux and native Windows and cover COPY/MMAP repeated saves and immediate identity checks, retained snapshots, late faults of the old original after replacement, a second fault after FORCE acknowledgement, compromised Keep refusal, cleanup, close with undrained acquisition, physical in-flight acquisition/save cancellation, both identity words, negative time conversion, and noalloc getters/ordinary dispatch. The actual POSIX signal/xattr/FIFO cases remain extra Linux coverage. Windows prints reasons for those platform skips and for journal/findui/editor integrations that cannot yet link there.

## Verification

Final acceptance rows below are from the completed runs. All success/exit values and counts are measured (M), derived from process status and harness/make enumeration. Linux uses DISPLAY=:99 and the repository display lock. LeakSanitizer remains enabled; the sandbox-only attempt aborted with the expected ptrace restriction, so the successful full run uses the authorized escalation.

- Linux standalone `make -j64 all`: **exit 0 (M)** (`linux-all-final.log`).
- Linux `make -j64 all fuzz`: **exit 0 (M)**; `fuzz: 37 fuzzers built` (`linux-all-fuzz-final.log`). This is the required release `all` plus the separate explicit fuzz target.
- Linux `DISPLAY=:99 make -j64 check`: **exit 0 (M)**, `check: 106 test binaries passed`, replay CLI and display-lock shell contracts passed (`linux-check-final.log`). An earlier complete run already exited 0 with `check: 106 test binaries passed` and both shell contracts.
- Windows `make TARGET=win -j64 check`: **exit 0 (M)** (`windows-check-final.log`). File executables were removed/relinked before this final run to avoid stale WSL executable images.
- `git diff --check`: **exit 0 (M)**.
- Symbol/global audit: **exit 0 (M)** (`symbol-audit.log`). `nm --defined-only` finds exactly one `fault_install` definition in the Linux library and exactly one in the Windows file executable. File core has no mutable global; platform file objects contain only `file_watches`; OS objects contain the sanctioned `os_fault_service` and Windows cache.

```text
file_win_test: 13 cases, ok
check: 5 Windows release and 5 sanitizer test binaries passed
ASan fired: ok
check: 106 test binaries passed
display lock contract: owner PID, alias/timeout, make/scripts, nested inheritance, exit status PASS
```

The Windows OS white-box suite separately reports its existing unavailable fixture with a stated reason; this is not a skipped file regression. Intentional negative controls inside Linux signal tests and display-lock shell contracts may print ASan/timeout messages even when the enclosing harness passes; the top-level exit and summary are the acceptance result.

## Tracked file benchmark

`DISPLAY=:99 tools/with_display_lock.sh build/bench/file_bench --track`: **exit 0 (M)**. Warm rows only; cold rows explicitly remain unmeasured. Evidence: `file-bench.log`, `file-bench-load.txt`.

(M) **[AC, desktop Spiral]**: the machine has no BAT0; the binding environment identifies this as the AC desktop. The legacy bench prints `[unknown]` in that situation, so this report supplies the required desktop stamp without altering the benchmark. The captured `uptime` at benchmark start was `13:44:40 up 5:59`, load averages **1.63, 2.29, 2.72 (M)**, copied from `uptime`. The per-row load below is the benchmark's `/proc/loadavg` observation.

| Row | Samples (M, harness count) | p50 / p99 ms (M) | load1 (M) |
|---|---:|---:|---:|
| Fresh fragmented save status, 1 MiB, idle bulk queue | 100 | 0.460 / 0.727 | 1.68 |
| Data-flushed 1 MiB, warm source | 100 | 14.376 / 20.310 | 1.68 |
| Fresh fragmented save status, 1 GiB, idle bulk queue | 10 | 0.484 / 0.639 | 1.58 |
| Data-flushed 1 GiB, warm source | 10 | 1507.304 / 1791.641 | 1.58 |

Derivation: the existing harness samples the relevant trace intervals, computes `bench_p50`/`bench_p99`, then divides nanoseconds by `1e6` to print milliseconds. MiB/GiB fixture sizes are the existing harness's powers-of-two inputs. The data-flush medians exceed the corresponding existing aspirations; TRACK preserves those observations and exits successfully. No timing budget or new perf gate is introduced.

## Remaining scope and coordinator decisions

**Finding 8 remains open for explicit auditing.** Owner/group/DACL and mandatory integrity labels are preserved/read back; readable unsupported resource/policy ACEs refuse. Querying LABEL/ATTRIBUTE/SCOPE/TRUST/FILTER does not reveal audit ACEs. A full audit SACL needs ACCESS_SYSTEM_SECURITY/SeSecurityPrivilege ([Microsoft security-information rights](https://learn.microsoft.com/en-us/windows/win32/secauthz/security-information)). Requiring that privilege indiscriminately would prevent ordinary Windows saves, while omitting it cannot prove that replacing an audited source preserves its auditing. No policy relaxation is assumed: current code must not be signed off as preserving full security descriptors, and an audited source can still lose explicit audit state on replacement. The coordinator must choose and authorize a preserve-or-refuse mechanism before closing this finding/bead. This is why the verdict is partial despite green checks.

The asynchronous clarification offered those policy choices; absent a decision, the blocker is reported rather than disguising audit loss as an accepted exclusion. The new full-security requirement and ordinary non-privileged save requirement need to be reconciled by the coordinator.

**Found, not fixed / integration:** finding 16 (short-name notifications) is unchanged as instructed. A filesystem that cannot enumerate streams is conservatively refused rather than assumed stream-free; share/degraded-mode policy remains with its Windows integration bead. Retain the broader module/test lists from concurrent Windows workers when integrating `mk/win.mk`. Include the pre-existing fuzzer migration and both new regression sources in the coordinator's commit. No clean-final-commit claim is made before that commit exists.

## Coordinator verification (session 22)

Rebased over W0.5, W1.2, W1.3 (mk/win.mk: `… font file`, W0.5's `tests/win/core_file.c` stub dropped since the real module links, `-ladvapi32`). On that tree, (M)[AC, desktop Spiral]:

```
make -j64 all                 → exit 0 (gcc 13)
DISPLAY=:96 make -j64 check   → exit 0, 107 suites, LeakSanitizer on (14:05 → 14:33)
make TARGET=win -j64 check    → 72 results exit=0 (every file suite rel and san); two red: d3d11_test rel and san `first_frame == 0`, W1.2's occlusion-sensitive test failing on main's own binary since 13:49 (edit-kv2g first item), not this bead
```
