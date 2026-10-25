# edit-177r.2 (s11) worker report — W0.2 `os.h` spec, Linux implementation, conformance suite

## Round 2 result (after the Codex review, coordinator triage in `build/briefs/edit-177r.2-s11-r2.md`)
**Done.** Every finding the triage put in this round is fixed in the header, the Linux implementation and the suite, or (22, and the EINTR half of 2) fixed in the header with the reason it cannot be tested portably. Findings 4, 11 and 19 were left alone (backlog edit-pc3s) and nothing in the header contradicts them. The decision doc has the finding → change → test table (§10), the new migration rules (§9) and the §8.2 amendment for the directory lock.

### What changed
- **`src/os/os.h`** (contract):
  - new entries: `os_rmutex_*` (1), `os_slice_ns` and `os_thread_cpu_resolution_ns` (7), `os_file_pwrite_once` and `OS_E_INTR` (2), `os_file_set_sparse` (20), `os_file_to_stdio` (10), `os_fault_test_trigger` (18);
  - new open flags `OS_OPEN_WRITE` and `OS_OPEN_TRUNC` (10); exactly one access mode per open;
  - timestamps are `int64_t` (23);
  - rewritten text: per-entry retry policy (2), allocation list (9), completed transfers and no pending I/O (14), lock ownership through duplicates (3), identity-keyed directory locks (5), read-only rename and unlink recipe with rights (6), reparse kinds (8), anonymous-file recipe (15), mapping recipe with `PAGE_READONLY` and logical vs native extent (27), mapping past the end of file (17), one fault-service installer per process (16), spawn inheritance (12), UTF-8 argv and the manifest (13), EXCL-over-NOFOLLOW precedence (22);
  - `os_dir` gained a `_WIN32` `lock` field; `OS_E_RANGE` also covers a mapping range past the end of file.
- **`src/os/os_posix.c`**: the new entries; `RETRY` around every call that can EINTR except the closes and `pwrite_once`; write-only and truncating opens; signed timestamps; `posix_spawn_file_actions_addclosefrom_np(3)`; swap-form `sigaction` install; the round-1 `fstat` bounds check in `os_map_file` removed (today's call again).
- **`tests/os_test.c`**: 17 cases (was 14; new `fault`, `paths`, `reparse`), each run in a supervised child process; additions per finding in decision doc §4 and §10.
- **`docs/decisions/edit-177r.2.md`**: inventory rows for mutex, thread CPU clock, fd I/O, rename, O_TMPFILE, flock, mmap, test processes, fopen; §2 to §6 rewritten; §8.2 amendment; new §9 (migration rules) and §10 (round-2 table).
- Kept as the coordinator left them: `CLAUDE.md` (globals line) and `src/file/check_signal_globals.py`.

### Files touched (round 2)
`src/os/os.h`, `src/os/os_posix.c`, `tests/os_test.c`, `docs/decisions/edit-177r.2.md`, `docs/worker-reports/edit-177r.2-s11.md`. Scratch probes (not in the tree) under the session scratchpad: `probe/eintr_probe.c`, `probe/alloc_probe.c`, `probe/winsize.c`, `os_test_hang.c`.

### §R2 Red runs
1. **Header + suite first, round-1 implementation** (frozen-spec red). The suite compiled clean against the new header (`make build/rel/tests/os_test.o`, exit 0). Linked against the round-1 `os_posix.o`:
```
$ gcc -pthread build/rel/tests/os_test.o <round-1 os_posix.o> build/libedit.a ...
undefined reference to `os_rmutex_unlock' (11) / `os_rmutex_lock' (11) / `os_fault_test_trigger' (5) /
`os_slice_ns' (4) / `os_rmutex_init' (3) / `os_rmutex_destroy' (3) / `os_file_pwrite_once' (2) /
`os_thread_cpu_resolution_ns' / `os_file_to_stdio' / `os_file_set_sparse'
link exit=1
```
   The signed timestamps are a compile-time red against the round-1 header:
```
tests/os_test.c:212:19: error: unsigned conversion from 'int' to 'uint64_t' changes value from
'-1000000000' to '18446744072709551616' [-Werror=sign-conversion]
```
2. **Behavioural red.** Phase A added only the new entries and left every round-1 behaviour in place:
```
$ build/tests/os_test
tests/os_test.c:206: [errors] FAIL os_file_open(&f, missing, OS_OPEN_READ | OS_OPEN_WRITE | OS_OPEN_CREATE) -> OS_OK, want OS_E_INVAL
tests/os_test.c:680: [files] FAIL os_file_open(&wo, path, OS_OPEN_WRITE) -> OS_E_INVAL (native 0)
tests/os_test.c:698: [files] FAIL os_file_open(&wo, path, OS_OPEN_WRITE | OS_OPEN_TRUNC) -> OS_E_INVAL (native 0)
tests/os_test.c:706: [files] FAIL os_file_open(&wo, path, OS_OPEN_WRITE | OS_OPEN_CREATE | OS_OPEN_TRUNC) -> OS_E_INVAL (native 0)
  ... (14 more lines from the same write-only / truncate / stdio-bridge block)
tests/os_test.c:1070: [locks] FAIL got == 4 && memcmp(buf, "abcd", 4) == 0      (TRUNC ignored)
tests/os_test.c:1234: [map] FAIL past == OS_OK || past == OS_E_RANGE            (finding 17: INVAL)
map: range past end of file -> OS_E_INVAL
tests/os_test.c:1771: [proc] FAIL s.how == OS_PROC_EXITED && s.code == 0        (finding 12: the fdscan
                                                                                  child holds the sentinel)
os_test: work dir not empty: OS_E_EXIST
os_test: 17 cases, 6 failed
exit=1
```
   Lines in this log are from that run; later edits moved them by a few lines.
   The other new tests passed against round-1 Linux behaviour, as expected: Linux already shares `flock` through duplicates (3), replaces and unlinks 0444 files (6), passes argv through (13) and recovers truncation faults. Those tests pin the semantics for W0.3; their red can only appear on Windows.

### Green runs
```
$ make -j64 all                                   # gcc 13 release, whole tree
make all exit=0
$ build/tests/os_test
clocks: cpu resolution 1 ns (reported), smallest cpu step 400 ns, smallest slice step 400 ns (M)
files: clone OS_E_NOTSUP / files: sparse OS_OK / dirs: anon OS_OK
map: truncate while mapped -> OS_OK / map: range past end of file -> OS_OK
fault: stray fault child -> abnormal 7                       (SIGBUS, default action)
paths: long path 370 UTF-16 units
proc: spawn-to-exit 2.499 ms (M)
os_test: 17 cases, 0 failed                                  exit=0
$ build/san/tests/os_test                         # clang 18 ASan/UBSan
noalloc: sanitizer build, allocator guard compiled out; counts not checked
fault: ... AddressSanitizer: BUS on unknown address ...      (the stray-fault child, on purpose)
fault: stray fault child -> exited 1                         (ASan's SIGBUS handler, chained to)
os_test: 17 cases, 0 failed                                  exit=0
```
- **Repeat runs:** 20 release and 20 sanitizer runs back to back: 0 failures in 40; no `os_test-*` directory left in `/tmp`. Load1 went from 12.11 to 9.18 over the 30 s window (`uptime`).
- **Globals:** `python3 src/file/check_signal_globals.py build/rel/src/os/os_posix.o` → `finding10: ok mutable globals=1 os_fault_service`, exit 0. `nm --defined-only`: the only writable object is `os_fault_service` (bss).
- **Windows branch:** `gcc -D_WIN32 -fsyntax-only` passes for `os.h` and `tests/os_test.c` with the full warning set. With the W0.1 llvm-mingw cross compiler (`~/opt/llvm-mingw-20260922-ucrt-ubuntu-22.04-x86_64`), `tests/os_test.c` parses, and a probe including `<windows.h>` static-asserts `sizeof(CRITICAL_SECTION) == 40`, `os_rmutex` large and aligned enough to hold one, and `SRWLOCK` pointer-sized.
- **Other suites:** no binary other than `os_test` links any `os_` symbol (M: `nm` over `build/sublimite`, `build/tests/*`, `build/tools/*`, `build/bench/*`), so no other suite can change behaviour; `make all` rebuilt them all. Display suites were not run (no Xvfb; W0 rules). The six X11 dev packages were present (`pkg-config --exists xcb-present xcb-randr` → ok).
- **Timing stamp:** every (M) figure above was taken on `[AC, desktop Spiral]` (no `BAT0`), load1 between 9 and 20 over the session. The per-case times are printed for information only: nothing is gated on them. The slowest case is the sanitizer `fault` case at 632 ms (M), which sets the case guard's derivation (120 s, about 190× that; in the test's comment).

### Positive controls and probes
- **26, unknown selector:** `OS_TEST_CASE=bogus build/tests/os_test` → `os_test: unknown OS_TEST_CASE (cases: errors clocks ... proc)`, exit 2.
- **25, hang guard:** a scratch copy of the suite whose `random` case sleeps forever, with the case guard set to 2 s: `random: FAIL (hung: killed after the case guard)`, `os_test: 1 cases, 1 failed`, exit 1, and no process left behind (`ps`).
- **21, inactive guard in a release build:** the release `os_test.o` linked with a copy of `libedit.a` whose `malloc_guard.o` is the inert (ASan) variant: `[noalloc] FAIL edit_malloc_guard_active()`, exit 1. In the normal release build the positive-control allocation is seen and the typing-path count is 0.
- **2, EINTR** (scratch probe, `pwrite` interposed to fail EINTR once, then forward): `pwrite_once: OS_E_INTR, attempts 1, done 0`; `pwrite: OS_OK, attempts 2, done 4`. This reproduces the Codex probe and shows the two policies.
- **9, allocations per entry** (scratch probe with the counting guard, one call each, (M)): `os_thread_start` 1, `os_env_set` (new variable) 3, `os_dir_iter_open` 1, `os_file_to_stdio` 1, `os_proc_spawn` 1; every other entry probed 0. That covers mkdir, dir open, file open/create/trunc, stat, map, first fault registration, retire, unmap, close, stat_at, dir_next, canonical, rename, unlink, rmdir, the four inits, thread join, env_get, app/temp dir, cwd, random, self path and proc wait. The header lists exactly the allocating ones.

### Found, not fixed (round 2)
1. Finding 18 also asks for retirement racing a concurrent fault in one slot; not in the suite (decision doc §6.5). Candidate for edit-pc3s.
2. `src/base/malloc_guard.c` has no Windows implementation, so the W0.3 release run fails `noalloc` by design until base gets one (decision doc §6.2).
3. The editor's own Windows `main` needs the UTF-8 manifest or a `wmain` bridge (W1, decision doc §9.11).
4. Round-1 items 1, 2 and 4 below still stand (`journal_retire_test` needs `BAT0`; `display_guard.h` force-include; 745 vs 848 count). Item 3 (the checker) is resolved by the coordinator's amendment.

### For the coordinator to decide
1. **Confirm the directory-lock amendment** (decision doc §8.2): an identity-keyed lock file under `%LOCALAPPDATA%\sublimite\dirlocks` replaces the hidden `.sublimite-dirlock` entry that §8.2 recorded.
2. Who builds the Windows allocator guard and the UTF-8 manifest link step (W0.1 or W0.3; decision doc §6.2–6.3).

---

## Round 1 result
**Done** (superseded by round 2 below).
- **Header:** the frozen `src/os/os.h` names about 100 operations across clocks, threads/mutex/cond/once, wake handles, files, directory-relative operations, listing, locks, mapping and VM, the fault service, random, processes, paths and environment, and app/temp directories.
- **Linux implementation:** `src/os/os_posix.c`, today's calls wrapped in `#if !defined(_WIN32)`.
- **Conformance suite:** `tests/os_test.c`, 14 cases using only `os.h`, C11 and `base.h`. It passes under gcc release and clang ASan/UBSan.
- **Decision doc:** `docs/decisions/edit-177r.2.md` maps all 848 POSIX call sites outside x11/wayland (M), family by family, to an `os.h` entry or to "stays platform-private" with the reason, and gives the Win32 mapping of every entry for W0.3.

No module was migrated. The Makefile is unchanged; its globs pick up `src/os/*.c` and `tests/os_test.c`.

## Files touched
- `src/os/os.h` (new, frozen spec)
- `src/os/os_posix.c` (new)
- `tests/os_test.c` (new)
- `docs/decisions/edit-177r.2.md` (new)
- `docs/worker-reports/edit-177r.2-s11.md` (this file)

## Red run (header + suite, no implementation)
```
$ make build/tests/os_test
gcc ... -c tests/os_test.c -o build/rel/tests/os_test.o          # compiles clean against os.h
/usr/bin/ld: .../tests/os_test.c:78:(.text+0x39): undefined reference to `os_file_open_at'
/usr/bin/ld: .../tests/os_test.c:82:(.text+0x62): undefined reference to `os_file_close'
... 739 undefined references in all
make: *** [Makefile:212: build/tests/os_test] Error 1
exit=2
```

## Green runs
```
$ make build/tests/os_test && build/tests/os_test           # gcc 13 release
errors: ok
clocks: ok
threads: ok
wake: ok
noalloc: ok                      # counting guard active: 0 mallocs in clocks/thread_self/mutex/wake
files: clone OS_E_NOTSUP
files: ok
dirs: anon OS_OK
dirs: ok
listing: ok
locks: ok
map: truncate while mapped -> OS_OK
map: fault epoch 0 -> 1
map: ok
random: ok
utf8: ok
known_dirs: temp=/tmp config=/home/tobiasosborne/.config/sublimite data=/home/tobiasosborne/.local/share/sublimite
known_dirs: ok
proc: spawn-to-exit 2.297 ms (M)
proc: ok
os_test: 14 cases, 0 failures
exit=0

$ make build/san/tests/os_test && build/san/tests/os_test   # clang 18 ASan/UBSan
noalloc: allocator guard inactive in this build (sanitizer), count not checked
... all 14 cases ok ...
proc: spawn-to-exit 16.370 ms (M)
os_test: 14 cases, 0 failures
exit=0
```
- **Repeat runs:** 20 runs of each binary, back to back, 0 failures in 40. No `os_test-*` directories were left in `/tmp`.
- **Single case:** `OS_TEST_CASE=locks build/tests/os_test` runs one case.
- **Timing stamp:** the spawn-to-exit times are (M)[AC, desktop Spiral], with load1 between 2.53 and 8.39 over the session (`uptime`). They are printed for information only; nothing is gated on them.

## Verification
- `make -j64 all` (gcc): exit 0, both before my change (baseline) and after it. The six X11 dev packages were present (`pkg-config --exists xcb-present xcb-randr` → ok).
- Every sanitizer test binary was built (exit 0). The 72 non-display sanitizer suites ran: 71 pass. `journal_retire_test` fails at `measurement()` because it `fopen`s `/sys/class/power_supply/BAT0/status`, which does not exist on this desktop. That failure is environmental and pre-existing; no `os_` symbol is linked into that binary (M: `nm | grep ' T os_'` → 0).
- Display suites (cli, editor*, x11*, ref*, gl*, raster_test) were not run: no Xvfb on this box (W0 rules).
- The header's `_WIN32` branch parses: `gcc -fsyntax-only -D_WIN32` passes for `os.h` and for `tests/os_test.c` with the full warning set. The suite uses nothing from the POSIX-only accessors.
- Globals: `nm --defined-only build/rel/src/os/os_posix.o` shows one mutable object, `os_fault_service` (bss). `check_signal_globals.py` on `file.o` is still ok (`file_bus,file_watches`). The same checker reports FAIL on `os_posix.o`, because its allowed set is file's. See open question 1.
- No fuzzer: `os` neither parses input nor mutates a data structure.

## Design in one paragraph (detail in the decision doc)
- **Errors:** an `os_err` enum, plus `os_native_error()` for errno or `GetLastError()`; no new thread-local state.
- **Paths:** UTF-8 at the API; `OS_PATH_MAX` 4096 (P: Linux PATH_MAX); `OS_NAME_MAX` 768 (P: 255 UTF-16 units × 3 bytes + NUL, rounded up).
- **Handles:** caller-allocated, with private fields. The `_WIN32` layouts are placeholders W0.3 may change.
- **Opens:** every open is non-inheritable and, on Windows, shares read, write and delete.
- **Directories:** a pinned handle. Windows uses NT handle-relative calls, so the P1.7c §7 property holds there too.
- **Rename and unlink:** POSIX semantics on both platforms (`FileRenameInformationEx` / `FileDispositionInformationEx` with POSIX semantics).
- **Locks:** flock semantics. Windows takes `LockFileEx` on one byte past any data, and locks directories through a hidden reserved entry.
- **Durability:** directory sync is a documented no-op on Windows.
- **Wake handles:** eventfd on Linux, a manual-reset event on Windows, with typed native accessors for the platform loop.
- **Fault service:** registers an `os_map`. Linux uses the `file_bus` algorithm. The Windows recipe is a vectored exception handler plus placeholder remap. The suite accepts either recovery (truncation `OS_OK`, zeros, epoch rises) or refusal (`OS_E_BUSY`, which NTFS gives for a mapped file).
- **Processes:** for tests, by self re-exec with a role argument.
- **Stays platform-private:** the event-loop wait set, timerfd, signals, sockets, `fc-match` spawn, dlopen, SysV shm, xattrs and inotify.

## Found, not fixed
1. `tests/journal_retire_test.c:298/617`: `measurement()` requires `/sys/class/power_supply/BAT0/status`, so the suite fails on any battery-less box, including Spiral. CLAUDE.md law 4 asks for a power stamp, but this desktop has no battery.
2. `tests/display_guard.h` is force-included into every test and bench object and pulls in `editor/runtime.h` and `wayland/plat.h`. A Windows test build (W0.1/W0.3) has to make that compile or skip it for non-display suites.
3. `src/file/check_signal_globals.py` only knows the file module's globals. It needs extending once the coordinator decides open question 1.
4. The design doc's §5.1 count (745) and this inventory's (848) use different call lists. The decision doc §1 gives the method; the per-module split in `docs/design/windows-port.md` §5.1 may want updating.

## Open questions for the coordinator
1. **Process-wide state in `src/os`.** The fault service needs one static `os_fault_service`, for the same reason as `file_bus`: there is no context argument for signal handlers or vectored exception handlers. Proposal: when W2 migrates `src/file`, the sanctioned exception moves from `file_bus` to `os_fault_service` (and `file_bus` is deleted); the checker and the CLAUDE.md globals line are amended to match. Until then the two services chain correctly, and only `os_test` installs this one. I did not edit CLAUDE.md or the checker.
2. **Windows directory-lock entry.** The reserved name (`.sublimite-dirlock` suggested) is hidden from listings and removed by `os_rmdir`. Accept it, or let W0.3 decide.
3. **Deny-write sharing on mapped originals** (perf §2.14) is not in this contract. If chosen, it becomes an additive open flag.
