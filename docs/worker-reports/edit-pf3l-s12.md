# edit-pf3l-s12 — W2.1 file on os.h

Verdict: **done**. Linux and Windows acceptance checks pass; the OS contract remains frozen.

## Changes and scope

Inventory was written in `docs/decisions/edit-pf3l.md` before production edits, with the pre-edit native call sites and their OS or platform-file destinations. Raw evidence is `build/worker-logs/pf3l-inventory-before.txt`.

The file core now uses OS identities, handles, maps, fault registration/epochs/retirement, directory-relative saves, syncs, paths and clocks. The independent `file_bus` installer and registry are deleted. Linux inotify and xattr/ownership copying live in `file_posix.c`; `file_win.c` implements overlapped ReadDirectoryChangesW, a shared completion event, and security/attribute copying. The frozen `src/os/os.h` and OS implementations were not edited.

Changes outside file are the necessary `file_id_from_stat(os_stat)` caller adjustments in editor recovery, config settings acquisition, savectl reload, their tests and the savectl fuzzer. CLAUDE's global exception and the symbol check now permit only the OS fault service and the shared notification service. The decision document explains why `file_watches` remains.

Files touched:

- `src/file/file.c`, `file.h`, `file_internal.h`, `file_posix.c`, `file_win.c`, `file_test.h`, `file_test_helpers.c`, `check_signal_globals.py`.
- `src/editor/recovery.c`, `src/config/runtime.c`, `src/savectl/savectl.c`.
- `tests/file_test.c`, `file_os_cases.inc`, `file_win_cases.inc`, `file_fault_test.c`, `savectl_test.c`, `savectl_reload_test.c`; `fuzz/savectl_fuzz.c`.
- `CLAUDE.md`, `mk/win.mk`, this report and `docs/decisions/edit-pf3l.md`.

No state-changing git command, bd command, subagent, main-checkout modification or read of another worker's checkout was used.

## Measurement context

All runs: (M) [AC, desktop Spiral]; `/sys/class/power_supply/BAT0` is absent, as specified by the worker rules. The pre-inventory `uptime` reading was `11:50:06 up 4:06, load average: 0.71, 1.34, 1.63`. At the final build/check launch: `12:13:30 up 4:29, load average: 9.48, 6.83, 4.58`. These are captured shell observations, not budgets. Existing suites/benches sometimes print `power=unknown`; the machine stamp above supplies their actual context.

Numeric exit statuses below are (M), from the command's captured `$?`; test counts are (M), from the make summary or symbol enumeration. The performance numbers below are measured outputs of the existing bench, with its existing sample counts and percentile calculation; no new performance gate was introduced.

## Red and green

### Structural fault-service red

```sh
make -j64 build/rel/src/file/file.o
python3 src/file/check_signal_globals.py build/rel/src/file/file.o
```

Exit 1 (M), before migration:

```text
finding10: FAIL mutable globals=2 file_bus,file_watches
```

The new directory/shrink tests were also present before implementation:

```sh
make -j64 build/tests/file_test
```

Exit 2 (M): `undefined reference to file_test_open_begin`. Log: `build/worker-logs/pf3l-red-test.log`. This is a link-time red for the missing per-acquisition hook, not a claimed runtime failure of the old directory classification.

Green:

```sh
DISPLAY=:97 FT_CASE=os-migration build/tests/file_test
```

Exit 0 (M):

```text
file_test: directory -> FILE_ERR_NOTREG: ok
file_test: shrink between identity validation and map -> FILE_ERR_CHANGED: ok
file_test: ok
```

The same included regression cases run in the Windows executable. Linux maps the shrunken file, then postvalidation rejects its changed identity; Windows's OS_E_RANGE takes the FILE_ERR_CHANGED branch. Real Linux truncation recovery is exercised separately by the existing signal, dataloss and editor-large cases.

### Interrupted-save cancellation red/green

The existing finding25 assertions were kept. Its syscall oracle was extended to positioned writes. Build command:

```sh
gcc -std=c11 -Wall -Wextra -Werror -Wshadow -Wconversion -D_GNU_SOURCE \
  -DFILE_TEST_WRAP -DFILE_REVIEW_WRAP -pthread -O2 -g -Isrc \
  -include tests/display_guard.h tests/file_test.c build/libedit.a \
  -Wl,--wrap=sysconf,--wrap=inotify_add_watch,--wrap=inotify_rm_watch,--wrap=fsync,--wrap=realpath,--wrap=open,--wrap=openat,--wrap=stat,--wrap=fstat,--wrap=pread,--wrap=write,--wrap=pwrite,--wrap=read,--wrap=renameat \
  -lm -ldl -lwayland-client -lwayland-egl -lxcb -lxcb-present -lxcb-sync \
  -lxcb-xfixes -lxcb-shm -lxcb-xkb -lxkbcommon -lxkbcommon-x11 -lxcb-randr \
  -o build/tests/file_wrapped
DISPLAY=:97 FT_CASE=25 build/tests/file_wrapped
```

With os_file_write, exit 1 (M):

```text
FAIL rc == FILE_ERR_CANCELLED && atomic_load(&latency_writes) == 3
finding25: FAIL
file_test: 1 FAILED
```

After using os_file_pwrite_once with the explicit temporary offset, exit 0 (M):

```text
finding25: ok
file_test: ok
```

Logs: `pf3l-eintr-red.log`, `pf3l-eintr-green.log`. The full wrapped file suite also exited 0 (M), including the old signal, save, notification and publication assertions (`pf3l-wrapped-full-final.log`).

### Windows red/green

All Windows executables run through tools/winrun.sh, in place. They create and remove their own NTFS scratch under the native temporary directory.

The initial metadata run failed with `save status=1 native=5 step=2` and `SetSecurityInfo native=5` (M): the reopened metadata handle lacked READ_CONTROL. Adding that right restored saves while preserving a write-only data open. Log: `pf3l-win-file-debug2.log`.

The self-reexec crash matrix was run before its child-save role existed, using a unique executable image:

```sh
tools/winrun.sh build/win/rel/tests/file_test_crash_red.exe
```

Exit 1 (M): the expected crash sentinel assertion failed at each save step; the post-rename generation assertions also failed. After implementing the child role, all crash steps pass, including old/new generation visibility and orphan-temp cleanup. Logs: `pf3l-win-crash-red.log`, `pf3l-win-file-green.log`.

The native path-length regression was added before its fix:

```sh
tools/winrun.sh build/win-pf3l-final/rel/tests/file_test_native_red.exe
```

Exit 1 (M): `FAIL GetLastError() == ERROR_FILENAME_EXCED_RANGE (native 0)`. The platform helper now supplies a native code for module-generated errors, preserving Linux errno behavior. Final Windows checks below include the green run.

## Verification

(M) Final command results captured in this worktree:

| Command | Exit (M) | Evidence |
|---|---:|---|
| `make -j64 all` | 0 | `pf3l-linux-all-final.log` |
| `DISPLAY=:97 make -j64 check` | 0 | `pf3l-linux-check-final.log`: 105 test binaries, replay CLI and display-lock contracts passed (M, make/test enumeration) |
| `make TARGET=win -j64 win-compile` | 0 | `pf3l-win-compile-final.log` |
| Full wrapped Linux file suite, rebuilt against final library | 0 | `pf3l-wrapped-full-final.log` |
| Sanitized private OS registry publication/retirement regression | 0 | `pf3l-fault-green.log`; also in final full check |
| `make -j64 build/fuzz/fuzz/savectl_fuzz.o` | 0 | `pf3l-fuzz-boundary.log`; type-boundary compilation, no fuzz campaign claimed |
| `make TARGET=win WIN_B=build/win-pf3l-complete -j64 check`, private native TEMP | 0 | `pf3l-win-isolated-check-final.log` |

Windows final summary lines, emitted in both release and sanitizer runs where applicable:

```text
os_win_test: 18/19 passed, 1 unavailable
os_test: 17 cases, 0 failed
file_test: Windows ok
check: 4 Windows release and 4 sanitizer test binaries passed
ASan fired: ok
private TEMP cleanup exit=0
```

Counts (M) are the suites' own enumerations. The unavailable OS white-box fixture is file symlink creation, native error 1314, requiring Developer Mode/elevation; this is the existing suite's explicit unavailable result. File's Windows executable prints explicit SKIP reasons for Linux signal actions/real mapped truncation, POSIX FIFO/xattr/syscall-interposition cases, and journal/findui/editor integration. It runs native equivalents for mapping recovery, DACL/attributes, notification and self-reexec crash durability. Windows ASan/UBSan run; LeakSanitizer is unsupported on that target.

The final Windows command uses a unique native TEMP root so the existing fixed-name reparse fixture cannot collide with other workers. A build-only helper (`build/worker-logs/pf3l_fixture_root.c`, linked with this worktree's OS object and manifest) creates and removes that root as a Windows process. Its emitted path uses backslashes for cmd.exe. Launch form:

```sh
TEMP="$pf3l_native_temp" WSLENV="TEMP${WSLENV:+:$WSLENV}" \
  make TARGET=win WIN_B=build/win-pf3l-complete -j64 check
```

The existing OS tests and fixture script are unchanged; the build directory also avoids the observed stale WSL executable image after overwriting a previous .exe. All native execution uses tools/winrun.sh, including the scratch-root helper.

Fault symbol audit:

```sh
python3 build/worker-logs/pf3l_nm.py
python3 src/file/check_signal_globals.py build/rel/src/file/file.o
python3 src/file/check_signal_globals.py build/rel/src/file/file_posix.o
python3 src/file/check_signal_globals.py build/rel/src/os/os_posix.o
```

```text
build/sublimite: file_bus=0 guard_install=0 os_fault_service=1 fault_install=1
build/tests/dataloss_test: file_bus=0 guard_install=0 os_fault_service=1 fault_install=1
build/tests/editor_large_test: file_bus=0 guard_install=0 os_fault_service=1 fault_install=1
build/tests/file_kill_test: file_bus=0 guard_install=0 os_fault_service=1 fault_install=1
build/tests/file_test: file_bus=0 guard_install=0 os_fault_service=1 fault_install=1
nm audit (M, enumerated ELF files): 150 Linux binaries; no file_bus; all 68 file-linked binaries have exactly one os_fault_service and one fault_install
finding10: ok mutable globals=0
finding10: ok mutable globals=1 file_watches
finding10: ok mutable globals=1 os_fault_service
```

The counts (M) derive from enumerating ELF executables under build/tests, build/tools, build/bench and build/sublimite and inspecting their defined symbols. The checker also passes the final Windows watch and OS objects. Its initial COFF run counted `.bss` and `.data` as variables; it now excludes those section symbols, retaining the same sanctioned C-global set. `git diff --exit-code -- src/os/os.h src/os/os_posix.c src/os/os_win.c` exits 0 (M).

The final Linux log also records the existing recovery suites:

```text
editor_large_test: ok
file_fault_test: OS reserved-slot publication, pinned retirement/reuse, foreign neighbours: ok
file_kill_test: PASS visibility/cancellation; power-loss barriers unverified
P1-1 section 2: ok
finding1: ok
finding2: ok
file_test: ok
```

The OS suite deliberately crashes an unregistered-fault child; its ASan BUS report is followed by `fault: stray fault child -> exited 1` and `fault: ok`. Forced-timeout messages in the CLI/display-lock contract tests are also positive controls, not hidden failed suites. The full make exit code is the acceptance result. Final root output and shell result:

```text
check: 105 test binaries passed
display lock contract: owner PID, alias/timeout, make/scripts, nested inheritance, exit status PASS
Linux make check final exit=0
```

Completion stamp (M) [AC, desktop Spiral]: `12:33:22 up 4:49, load average: 5.37, 6.58, 6.21`.

An earlier full Linux run completed with exit 0 (M), `check: 104 test binaries passed` (M, make enumeration), before the final cancellation/native-error rebuild and the new private-registry suite were included. The final run below covers the completed tree. The first sandboxed attempt exited 2 (M) at base_test with LeakSanitizer's fatal ptrace restriction. All subsequent full sanitizer checks run outside the sandbox with leak detection enabled; detect_leaks=0 was never used.

An earlier Windows run passed its release/sanitizer test loop, then exited 2 (M) from the shared reparse-fixture EXIT cleanup, before the ASan positive control ran. A subsequent shared-fixture run lost the file during the sanitizer reparse case; the final isolated run eliminates that interference. That script uses one fixed native temporary directory shared with other Windows checks; it was not changed by this bead. The final result below includes the complete check and ASan positive control.

## Tracked file benchmark

```sh
DISPLAY=:97 sh tools/with_display_lock.sh build/bench/file_bench --track
```

Exit 0 (M), [AC, desktop Spiral]. Log: `build/worker-logs/pf3l-file-bench-final.log`, recorded after the final write-loop change. Load and final percentile rows are copied below. Shell `uptime` after completion: `12:22:51 up 4:39, load average: 8.62, 7.44, 5.55` (M).

Measured context from the final log (M):

```text
file_bench power=unknown [unknown] load1=8.52 mode=TRACK; real CPU raster submit on DISPLAY=:97
TRACK G8d_1GiB_warm_source (M)[unknown] load1=10.33 n=10 p50=1960.771 ms p99=3331.235 ms gate=1500/2500 ms (G)
```

Representative existing rows, directly from that log:

| Row | Samples (M, emitted count) | p50 / p99 (M, existing bench percentiles) |
|---|---:|---:|
| Warm sparse open, no competing job | 100 | 3.496 / 4.062 ms |
| Warm log open, no competing job | 100 | 4.184 / 5.521 ms |
| Fresh fragmented save acknowledgement, MiB fixture, no competing job | 100 | 0.527 / 0.914 ms |
| Warm MiB durable save | 100 | 30.711 / 44.192 ms |
| Warm GiB durable save | 10 | 1960.771 / 3331.235 ms |

Both durable-save rows exceed their pre-existing p50 targets; the GiB row also exceeds its p99 target. These are tracked measurements under the reported load; no performance work or gate change was made. Fixture units derive from the existing bench's `1u << 20` and `1ull << 30`; sample counts come from SAMPLES and its existing TRACK override. Cold rows remain explicitly unmeasured.

## Found, not fixed

- Editor buffers still register file's Linux notification descriptor in epoll; recovery still owns native open/read/close descriptors. Only recovery's identity-stat boundary changed here. The Windows loop will consume file_watch_handle in its own bead.
- Journal capture still compares file_id.type with S_IFREG and its BASE representation stores a device plus a low inode word. File's own identity comparisons preserve the Windows upper word; a journal format/identity policy belongs to its Windows migration.
- Savectl and config retain their own native I/O outside the adjusted identity boundaries.
- The Windows metadata tests cover owner/group/DACL, DACL protection, and hidden/read-only attributes. SACL auditing, alternate data streams, compression and encryption preservation are not claimed.
- Shared fixed-name Windows reparse-fixture cleanup can conflict with another check; the infrastructure is left unchanged.

## Coordinator handoff

Keep W0.5's wider WIN_MODULES/WIN_TESTS lists when merging mk/win.mk; add file, file_test, its included-case dependencies and advapi32. This worktree started with only os in the Windows link list, so it includes file's minimal dependency closure to verify independently. No frozen OS contract change or blocking design decision is requested. The editor/journal interfaces and metadata coverage limits above are explicit follow-up context.
